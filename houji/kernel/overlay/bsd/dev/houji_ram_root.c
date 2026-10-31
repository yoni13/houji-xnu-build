/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "houji_ram_root.h"
#if defined(KERNEL)
#include <libkern/libkern.h>
#include <libkern/zlib.h>
#include <sys/errno.h>
#else
#include <string.h>
#include <errno.h>
#include <zlib.h>
#endif

#if defined(KERNEL) || defined(HRR_XNU_ZLIB)
#define hrr_crc32 z_crc32
#else
#define hrr_crc32 crc32
#endif

#define HRR_HEADER_BYTES 4096u
#define HRR_INDEX_BYTES 16u
#define HRR_MAX_CHUNKS (UINT64_C(1) << 20)
#define HRR_MAX_PACKED_CHUNK (HRR_CHUNK_BYTES + 1024u)
#define HRR_ZLIB_BUDGET (256u * 1024u)
#define HRR_MAX_WRITE_PAGES (HRR_MAX_IO_BYTES / HRR_COW_PAGE_BYTES + 1u)

struct hrr_cache {
    uint8_t *bytes;
    uint64_t chunk;
    int valid;
};
struct hrr {
    struct hrr_env env;
    struct hrr_stats stats;
    uint64_t chunk_count, page_count, cow_pages, cow_limit;
    size_t index_bytes, cow_table_bytes;
    uint8_t *index, *packed_scratch;
    uint8_t **cow;
    struct hrr_cache cache[HRR_MAX_CACHE_CHUNKS];
    unsigned int next_cache;
    z_stream stream;
    int stream_initialized;
    size_t zlib_live;
};

static uint32_t
get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t
get64(const uint8_t *p)
{
    return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32;
}
static size_t
chunk_length(const struct hrr *r, uint64_t chunk)
{
    uint64_t remaining = r->stats.logical_bytes - chunk * HRR_CHUNK_BYTES;
    return remaining < HRR_CHUNK_BYTES ? (size_t)remaining : HRR_CHUNK_BYTES;
}
static void *
allocate_zero(struct hrr *r, size_t n)
{
    void *p = r->env.alloc(r->env.context, n);
    if (p) { memset(p, 0, n); }
    return p;
}
static int
read_packed(struct hrr *r, uint64_t offset, void *buffer, size_t bytes)
{
    if (offset > r->stats.packed_bytes || bytes > r->stats.packed_bytes - offset) {
        return EINVAL;
    }
    uint8_t *out = buffer;
    while (bytes) {
        size_t n = bytes < HRR_CHUNK_BYTES ? bytes : HRR_CHUNK_BYTES;
        ++r->stats.backing_reads;
        int e = r->env.read(r->env.context, offset, out, n);
        if (e) { return e; }
        out += n; offset += n; bytes -= n;
    }
    return 0;
}

/* A 16-byte prefix preserves the allocator's alignment and carries free size. */
struct hrr_zalloc_header { uint64_t bytes, reserved; };
static voidpf
z_allocate(voidpf context, uInt count, uInt size)
{
    struct hrr *r = context;
    if (!count || !size || size > HRR_ZLIB_BUDGET / count) { return Z_NULL; }
    size_t bytes = (size_t)count * size;
    if (bytes > HRR_ZLIB_BUDGET - r->zlib_live) { return Z_NULL; }
    struct hrr_zalloc_header *p = allocate_zero(r, sizeof(*p) + bytes);
    if (!p) { return Z_NULL; }
    p->bytes = bytes; r->zlib_live += bytes;
    return p + 1;
}
static void
z_free(voidpf context, voidpf buffer)
{
    if (!buffer) { return; }
    struct hrr *r = context;
    struct hrr_zalloc_header *p = (struct hrr_zalloc_header *)buffer - 1;
    size_t bytes = (size_t)p->bytes;
    r->zlib_live -= bytes;
    r->env.free(r->env.context, p, sizeof(*p) + bytes);
}

void
hrr_close(struct hrr *r)
{
    if (!r) { return; }
    struct hrr_env env = r->env;
    if (r->stream_initialized) { (void)inflateEnd(&r->stream); }
    if (r->cow) {
        for (uint64_t i = 0; i < r->page_count; ++i) {
            if (r->cow[i]) { env.free(env.context, r->cow[i], HRR_COW_PAGE_BYTES); }
        }
        env.free(env.context, r->cow, r->cow_table_bytes);
    }
    for (unsigned int i = 0; i < r->stats.cache_chunks; ++i) {
        if (r->cache[i].bytes) { env.free(env.context, r->cache[i].bytes, HRR_CHUNK_BYTES + 1u); }
    }
    if (r->packed_scratch) { env.free(env.context, r->packed_scratch, HRR_MAX_PACKED_CHUNK); }
    if (r->index) { env.free(env.context, r->index, r->index_bytes); }
    env.free(env.context, r, sizeof(*r));
}

int
hrr_open(const struct hrr_env *env, uint64_t packed_bytes, uint64_t cow_budget,
    unsigned int cache_chunks, struct hrr **out)
{
    if (!out) { return EINVAL; }
    *out = NULL;
    if (!env || !env->read || !env->alloc || !env->free || !env->sha256 ||
        !env->lock || !env->unlock || packed_bytes < HRR_HEADER_BYTES ||
        !cache_chunks || cache_chunks > HRR_MAX_CACHE_CHUNKS ||
        (cow_budget & (HRR_COW_PAGE_BYTES - 1u))) { return EINVAL; }
    struct hrr *r = env->alloc(env->context, sizeof(*r));
    if (!r) { return ENOMEM; }
    memset(r, 0, sizeof(*r)); r->env = *env;
    r->stats.packed_bytes = packed_bytes; r->stats.cache_chunks = cache_chunks;
    uint8_t *header = allocate_zero(r, HRR_HEADER_BYTES);
    int error = ENOMEM;
    if (!header) { goto failed; }
    error = read_packed(r, 0, header, HRR_HEADER_BYTES);
    if (error) { goto failed; }
    error = EINVAL;
    if (memcmp(header, "HJRDCMP\0", 8) || get32(header+8) != 1 ||
        get32(header+12) != HRR_HEADER_BYTES || get32(header+16) != HRR_CHUNK_BYTES ||
        get32(header+20) != HRR_INDEX_BYTES || get64(header+40) != HRR_HEADER_BYTES ||
        get64(header+56) != packed_bytes) { goto failed; }
    for (size_t i = 160; i < HRR_HEADER_BYTES; ++i) {
        if (header[i]) { goto failed; }
    }
    uint8_t expected[32], actual[32];
    memcpy(expected, header+128, 32); memset(header+128, 0, 32);
    error = env->sha256(env->context, header, HRR_HEADER_BYTES, actual);
    if (error) { goto failed; }
    error = EINVAL;
    if (memcmp(expected, actual, 32)) { goto failed; }
    uint64_t raw_bytes = get64(header+24), count = get64(header+32);
    if (!raw_bytes || raw_bytes > HRR_MAX_CHUNKS * HRR_CHUNK_BYTES ||
        !count || count > HRR_MAX_CHUNKS ||
        count != (raw_bytes - 1) / HRR_CHUNK_BYTES + 1) { goto failed; }
    uint64_t index_bytes = count * HRR_INDEX_BYTES;
    uint64_t data_offset = (HRR_HEADER_BYTES + index_bytes + 4095) & ~UINT64_C(4095);
    if (get64(header+48) != data_offset || data_offset > packed_bytes) { goto failed; }
    r->stats.logical_bytes = raw_bytes; r->chunk_count = count;
    r->index_bytes = (size_t)index_bytes;
    r->page_count = (raw_bytes - 1) / HRR_COW_PAGE_BYTES + 1;
    if (cow_budget / HRR_COW_PAGE_BYTES > r->page_count) { goto failed; }
    r->cow_limit = cow_budget / HRR_COW_PAGE_BYTES;
    r->stats.cow_budget_bytes = cow_budget;
    r->cow_table_bytes = (size_t)r->page_count * sizeof(*r->cow);
    error = ENOMEM;
    r->index = allocate_zero(r, r->index_bytes);
    r->packed_scratch = allocate_zero(r, HRR_MAX_PACKED_CHUNK);
    r->cow = allocate_zero(r, r->cow_table_bytes);
    if (!r->index || !r->packed_scratch || !r->cow) { goto failed; }
    for (unsigned int i = 0; i < cache_chunks; ++i) {
        r->cache[i].bytes = allocate_zero(r, HRR_CHUNK_BYTES + 1u);
        if (!r->cache[i].bytes) { goto failed; }
    }
    error = read_packed(r, HRR_HEADER_BYTES, r->index, r->index_bytes);
    if (error) { goto failed; }
    error = env->sha256(env->context, r->index, r->index_bytes, actual);
    if (error) { goto failed; }
    error = EINVAL;
    if (memcmp(header+96, actual, 32)) { goto failed; }
    size_t padding = (size_t)(data_offset - HRR_HEADER_BYTES - index_bytes);
    error = read_packed(r, HRR_HEADER_BYTES + index_bytes, r->packed_scratch, padding);
    if (error) { goto failed; }
    error = EINVAL;
    for (size_t i = 0; i < padding; ++i) { if (r->packed_scratch[i]) { goto failed; } }
    uint64_t cursor = data_offset;
    uint32_t zero_crc = (uint32_t)hrr_crc32(0, r->cache[0].bytes, HRR_CHUNK_BYTES);
    for (uint64_t i = 0; i < count; ++i) {
        const uint8_t *entry = r->index + i * HRR_INDEX_BYTES;
        uint64_t offset = get64(entry);
        uint32_t length = get32(entry+8), crc = get32(entry+12);
        if (!length) {
            size_t needed = chunk_length(r, i);
            uint32_t expected_crc = needed == HRR_CHUNK_BYTES ? zero_crc :
                (uint32_t)hrr_crc32(0, r->cache[0].bytes, (uInt)needed);
            if (offset || crc != expected_crc) { goto failed; }
        } else {
            if (offset != cursor || length > HRR_MAX_PACKED_CHUNK ||
                length > packed_bytes - cursor) { goto failed; }
            cursor += length;
        }
    }
    if (cursor != packed_bytes) { goto failed; }
    r->stream.zalloc = z_allocate; r->stream.zfree = z_free; r->stream.opaque = r;
    int zerr = inflateInit2(&r->stream, 15); /* RFC1950 only, no gzip/raw auto-detection. */
    if (zerr != Z_OK) { error = zerr == Z_MEM_ERROR ? ENOMEM : EIO; goto failed; }
    r->stream_initialized = 1;
    env->free(env->context, header, HRR_HEADER_BYTES);
    *out = r; return 0;
failed:
    if (header) { env->free(env->context, header, HRR_HEADER_BYTES); }
    hrr_close(r); return error;
}

static int
decoded_chunk(struct hrr *r, uint64_t chunk, const uint8_t **out)
{
    for (unsigned int i = 0; i < r->stats.cache_chunks; ++i) {
        if (r->cache[i].valid && r->cache[i].chunk == chunk) {
            ++r->stats.cache_hits; *out = r->cache[i].bytes; return 0;
        }
    }
    struct hrr_cache *cache = &r->cache[r->next_cache];
    r->next_cache = (r->next_cache + 1) % r->stats.cache_chunks;
    cache->valid = 0;
    const uint8_t *entry = r->index + chunk * HRR_INDEX_BYTES;
    uint32_t length = get32(entry+8), expected_crc = get32(entry+12);
    size_t needed = chunk_length(r, chunk);
    if (!length) {
        memset(cache->bytes, 0, needed);
    } else {
        int error = read_packed(r, get64(entry), r->packed_scratch, length);
        if (error) { return error; }
        if (inflateReset(&r->stream) != Z_OK) { return EIO; }
        r->stream.next_in = r->packed_scratch; r->stream.avail_in = length;
        r->stream.next_out = cache->bytes; r->stream.avail_out = (uInt)needed + 1u;
        int result = inflate(&r->stream, Z_FINISH);
        ++r->stats.decompressions;
        if (result != Z_STREAM_END || r->stream.total_out != needed ||
            r->stream.total_in != length || r->stream.avail_in != 0) {
            return result == Z_MEM_ERROR ? ENOMEM : EIO;
        }
    }
    if ((uint32_t)hrr_crc32(0, cache->bytes, (uInt)needed) != expected_crc) { return EIO; }
    cache->chunk = chunk; cache->valid = 1; *out = cache->bytes; return 0;
}

static int
base_read(struct hrr *r, uint64_t offset, uint8_t *buffer, size_t bytes)
{
    while (bytes) {
        uint64_t chunk = offset / HRR_CHUNK_BYTES;
        size_t within = (size_t)(offset % HRR_CHUNK_BYTES);
        size_t n = HRR_CHUNK_BYTES - within;
        if (n > bytes) { n = bytes; }
        const uint8_t *decoded;
        int error = decoded_chunk(r, chunk, &decoded);
        if (error) { return error; }
        memcpy(buffer, decoded + within, n);
        offset += n; buffer += n; bytes -= n;
    }
    return 0;
}

static int
read_locked(struct hrr *r, uint64_t offset, uint8_t *buffer, size_t bytes)
{
    while (bytes) {
        uint64_t page = offset / HRR_COW_PAGE_BYTES;
        size_t within = (size_t)(offset % HRR_COW_PAGE_BYTES);
        size_t n = HRR_COW_PAGE_BYTES - within;
        if (n > bytes) { n = bytes; }
        if (r->cow[page]) {
            memcpy(buffer, r->cow[page] + within, n);
        } else {
            int error = base_read(r, offset, buffer, n);
            if (error) { return error; }
        }
        offset += n; buffer += n; bytes -= n;
    }
    return 0;
}

static int
write_locked(struct hrr *r, uint64_t offset, const uint8_t *buffer, size_t bytes)
{
    struct pending_page { uint64_t number; uint8_t *bytes; } pending[HRR_MAX_WRITE_PAGES];
    unsigned int pending_count = 0;
    uint64_t first = offset / HRR_COW_PAGE_BYTES;
    uint64_t last = (offset + bytes - 1) / HRR_COW_PAGE_BYTES;
    uint64_t needed = 0;
    for (uint64_t page = first; page <= last; ++page) { if (!r->cow[page]) { ++needed; } }
    if (needed > r->cow_limit - r->cow_pages) { return ENOSPC; }
    int error = ENOMEM;
    /* Preallocate and validate every new page before changing any visible byte. */
    for (uint64_t page = first; page <= last; ++page) {
        if (r->cow[page]) { continue; }
        uint8_t *p = allocate_zero(r, HRR_COW_PAGE_BYTES);
        if (!p) { goto failed; }
        pending[pending_count].number = page;
        pending[pending_count++].bytes = p;
        uint64_t page_offset = page * HRR_COW_PAGE_BYTES;
        size_t n = (size_t)(r->stats.logical_bytes - page_offset);
        if (n > HRR_COW_PAGE_BYTES) { n = HRR_COW_PAGE_BYTES; }
        error = base_read(r, page_offset, p, n);
        if (error) { goto failed; }
        error = ENOMEM;
    }
    for (unsigned int i = 0; i < pending_count; ++i) {
        r->cow[pending[i].number] = pending[i].bytes;
    }
    r->cow_pages += pending_count;
    r->stats.cow_used_bytes = r->cow_pages * HRR_COW_PAGE_BYTES;
    while (bytes) {
        uint64_t page = offset / HRR_COW_PAGE_BYTES;
        size_t within = (size_t)(offset % HRR_COW_PAGE_BYTES);
        size_t n = HRR_COW_PAGE_BYTES - within;
        if (n > bytes) { n = bytes; }
        memcpy(r->cow[page] + within, buffer, n);
        offset += n; buffer += n; bytes -= n;
    }
    return 0;
failed:
    for (unsigned int i = 0; i < pending_count; ++i) {
        r->env.free(r->env.context, pending[i].bytes, HRR_COW_PAGE_BYTES);
    }
    return error;
}

int
hrr_transfer(void *context, uint64_t offset, void *buffer, size_t bytes, int write)
{
    struct hrr *r = context;
    if (!r || (write != 0 && write != 1) || (!buffer && bytes) ||
        bytes > HRR_MAX_IO_BYTES || offset > r->stats.logical_bytes ||
        bytes > r->stats.logical_bytes - offset) { return EINVAL; }
    if (!bytes) { return 0; }
    r->env.lock(r->env.context);
    int error = write ? write_locked(r, offset, buffer, bytes) : read_locked(r, offset, buffer, bytes);
    r->env.unlock(r->env.context);
    return error;
}

int
hrr_synchronize(void *context)
{
    struct hrr *r = context;
    if (!r) { return EINVAL; }
    r->env.lock(r->env.context);
    /* Every copy is synchronous and protected by this lock; no disk I/O. */
    r->env.unlock(r->env.context);
    return 0;
}

int
hrr_get_stats(struct hrr *r, struct hrr_stats *out)
{
    if (!r || !out) { return EINVAL; }
    r->env.lock(r->env.context); *out = r->stats; r->env.unlock(r->env.context);
    return 0;
}
