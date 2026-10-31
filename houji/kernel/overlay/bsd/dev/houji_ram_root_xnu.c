/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "houji_ram_root_xnu.h"
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/memdev_ops.h>
#include <kern/kalloc.h>
#include <kern/locks.h>
#include <libkern/libkern.h>
#include <libkern/crypto/sha2.h>
#include <vm/pmap.h>

extern void bcopy_phys(addr64_t, addr64_t, vm_size_t);
extern ppnum_t pmap_find_phys(pmap_t, addr64_t);
static LCK_GRP_DECLARE(hrr_lock_group, "houji-ram-root");
struct hrr_xnu {
    struct hrr_physical_span spans[HRR_XNU_MAX_SPANS];
    unsigned int span_count;
    uint64_t packed_bytes;
    lck_mtx_t *mutex;
    struct hrr *root;
    int registered;
};

static void *
xnu_allocate(void *unused, size_t bytes)
{
    (void)unused;
    return kalloc_data(bytes, Z_WAITOK);
}
static void
xnu_free(void *unused, void *buffer, size_t bytes)
{
    (void)unused;
    kfree_data(buffer, bytes);
}
static void xnu_lock(void *ctx) { lck_mtx_lock(((struct hrr_xnu *)ctx)->mutex); }
static void xnu_unlock(void *ctx) { lck_mtx_unlock(((struct hrr_xnu *)ctx)->mutex); }
static int
xnu_sha256(void *unused, const void *bytes, size_t size, uint8_t out[32])
{
    (void)unused;
    SHA256_CTX state;
    SHA256_Init(&state); SHA256_Update(&state, bytes, size); SHA256_Final(out, &state);
    return 0;
}

static int
xnu_read(void *context, uint64_t offset, void *buffer, size_t bytes)
{
    struct hrr_xnu *x = context;
    if (offset > x->packed_bytes || bytes > x->packed_bytes - offset) { return EINVAL; }
    uint8_t *out = buffer;
    unsigned int span = 0;
    while (span < x->span_count && offset >= x->spans[span].bytes) {
        offset -= x->spans[span++].bytes;
    }
    while (bytes) {
        if (span == x->span_count) { return EIO; }
        uint64_t pa = x->spans[span].physical_address + offset;
        size_t n = PAGE_SIZE - ((uintptr_t)out & PAGE_MASK);
        size_t source_page = PAGE_SIZE - (size_t)(pa & PAGE_MASK);
        uint64_t remaining = x->spans[span].bytes - offset;
        if (n > source_page) { n = source_page; }
        if (n > remaining) { n = (size_t)remaining; }
        if (n > bytes) { n = bytes; }
        ppnum_t ppn = pmap_find_phys(kernel_pmap, (addr64_t)(uintptr_t)out);
        if (!ppn) { return EFAULT; }
        /* PFNs use the kernel's 16 KiB PAGE_SHIFT, never the mdev 4 KiB ABI. */
        uint64_t destination = ((uint64_t)ppn << PAGE_SHIFT) | ((uintptr_t)out & PAGE_MASK);
        bcopy_phys(pa, destination, (vm_size_t)n);
        out += n; bytes -= n; offset += n;
        if (offset == x->spans[span].bytes) { ++span; offset = 0; }
    }
    return 0;
}

int
hrr_xnu_destroy(struct hrr_xnu *x)
{
    if (!x) { return 0; }
    if (x->registered) { return EBUSY; }
    /* Exclusive binder ownership is required; no I/O may race destroy. */
    if (x->root) { hrr_close(x->root); }
    if (x->mutex) { lck_mtx_free(x->mutex, &hrr_lock_group); }
    kfree_type(struct hrr_xnu, x);
    return 0;
}

int
hrr_xnu_create(const struct hrr_physical_span *spans, unsigned int count,
    uint64_t packed_bytes, const uint8_t expected_header[32], const uint8_t expected_packed[32],
    int (*validate)(void *, uint64_t, uint64_t), void *ownership,
    uint64_t cow_budget, unsigned int cache_chunks, struct hrr_xnu **out)
{
    if (!out) { return EINVAL; }
    *out = NULL;
    if (!spans || !count || count > HRR_XNU_MAX_SPANS || !validate ||
        !expected_header || !expected_packed || packed_bytes < 4096 ||
        packed_bytes > UINT64_MAX - PAGE_MASK) { return EINVAL; }
    struct hrr_xnu *x = kalloc_type(struct hrr_xnu, Z_WAITOK | Z_ZERO);
    if (!x) { return ENOMEM; }
    x->span_count = count; x->packed_bytes = packed_bytes;
    /* Snapshot descriptors before validation; the trusted binder owns inputs. */
    memcpy(x->spans, spans, count * sizeof(*spans));
    uint64_t total = 0;
    int error = EINVAL;
    for (unsigned int i = 0; i < count; ++i) {
        uint64_t pa = x->spans[i].physical_address, n = x->spans[i].bytes;
        if (!pa || !n || ((pa | n) & PAGE_MASK) || n > UINT64_MAX - pa || n > UINT64_MAX - total) {
            goto failed;
        }
        for (unsigned int j = 0; j < i; ++j) {
            uint64_t other = x->spans[j].physical_address;
            if (pa < other + x->spans[j].bytes && other < pa + n) { goto failed; }
        }
        total += n;
        /* Return zero only for proven reserved, normal-WB, non-managed RAM. */
        error = validate(ownership, pa, n);
        if (error) { goto failed; }
        error = EINVAL;
    }
    if (total != ((packed_bytes + PAGE_MASK) & ~(uint64_t)PAGE_MASK)) { goto failed; }
    x->mutex = lck_mtx_alloc_init(&hrr_lock_group, LCK_ATTR_NULL);
    error = ENOMEM;
    if (!x->mutex) { goto failed; }
    uint8_t *scratch = kalloc_data(HRR_CHUNK_BYTES, Z_WAITOK);
    if (!scratch) { goto failed; }
    SHA256_CTX digest;
    SHA256_Init(&digest);
    uint8_t actual[32];
    for (uint64_t offset = 0; offset < packed_bytes;) {
        size_t n = packed_bytes - offset < HRR_CHUNK_BYTES ? (size_t)(packed_bytes - offset) : HRR_CHUNK_BYTES;
        error = xnu_read(x, offset, scratch, n);
        if (error) { break; }
        if (offset == 0 && memcmp(scratch + 128, expected_header, 32)) { error = EINVAL; break; }
        SHA256_Update(&digest, scratch, n);
        offset += n;
    }
    if (!error) {
        SHA256_Final(actual, &digest);
        if (memcmp(actual, expected_packed, 32)) { error = EINVAL; }
    }
    kfree_data(scratch, HRR_CHUNK_BYTES);
    if (error) { goto failed; }
    struct hrr_env env = { x, xnu_read, xnu_allocate, xnu_free, xnu_sha256, xnu_lock, xnu_unlock };
    error = hrr_open(&env, packed_bytes, cow_budget, cache_chunks, &x->root);
    if (error) { goto failed; }
    *out = x; return 0;
failed:
    (void)hrr_xnu_destroy(x); return error;
}

/* mdev may issue larger requests. Each bounded sub-write is atomic; after an
 * error, earlier completed sub-writes remain in volatile COW RAM. */
static int
xnu_transfer(void *root, uint64_t offset, void *buffer, size_t bytes, int write)
{
    struct hrr_stats stats;
    int error = hrr_get_stats(root, &stats);
    if (error) { return error; }
    if ((write != 0 && write != 1) || (!buffer && bytes) ||
        offset > stats.logical_bytes || bytes > stats.logical_bytes - offset) { return EINVAL; }
    uint8_t *cursor = buffer;
    while (bytes) {
        size_t n = bytes < HRR_MAX_IO_BYTES ? bytes : HRR_MAX_IO_BYTES;
        error = hrr_transfer(root, offset, cursor, n, write);
        if (error) { return error; }
        offset += n; cursor += n; bytes -= n;
    }
    return 0;
}

int
hrr_xnu_register(struct hrr_xnu *x, int devid, dev_t *out)
{
    static const struct mdev_ops operations = { xnu_transfer, hrr_synchronize };
    if (!x || !x->root || !out || x->registered) { return EINVAL; }
    *out = (dev_t)-1;
    struct hrr_stats stats;
    int error = hrr_get_stats(x->root, &stats);
    if (error) { return error; }
    dev_t device = mdevadd_ops(devid, stats.logical_bytes, &operations, x->root);
    if (device == (dev_t)-1) { return EBUSY; }
    x->registered = 1; *out = device; return 0;
}
