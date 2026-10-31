/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef HOUJI_RAM_ROOT_H
#define HOUJI_RAM_ROOT_H
#include <stddef.h>
#include <stdint.h>

#define HRR_CHUNK_BYTES 65536u
#define HRR_COW_PAGE_BYTES 16384u
#define HRR_MAX_IO_BYTES (1024u * 1024u)
#define HRR_MAX_CACHE_CHUNKS 16u
#define HRR_DEFAULT_COW_BYTES (UINT64_C(512) * 1024 * 1024)

/* All callbacks are trusted kernel/host bindings, never wire-format fields.
 * read is exact and read-only. alloc returns >=16-byte aligned memory.
 * lock/unlock provide one sleepable mutex shared by all operations on a root.
 * open/close require exclusive ownership; close cannot race any operation.
 * The caller verifies the expected packed SHA256 before open, owns immutable
 * backing for the lifetime, and does not alias I/O buffers with backend state.
 */
struct hrr_env {
    void *context;
    int (*read)(void *, uint64_t, void *, size_t);
    void *(*alloc)(void *, size_t);
    void (*free)(void *, void *, size_t);
    int (*sha256)(void *, const void *, size_t, uint8_t[32]);
    void (*lock)(void *);
    void (*unlock)(void *);
};

struct hrr;
struct hrr_stats {
    uint64_t logical_bytes, packed_bytes, cow_budget_bytes, cow_used_bytes;
    uint64_t backing_reads, decompressions, cache_hits;
    unsigned int cache_chunks;
};

#ifdef __cplusplus
extern "C" {
#endif
int hrr_open(const struct hrr_env *, uint64_t packed_bytes, uint64_t cow_budget,
    unsigned int cache_chunks, struct hrr **);
void hrr_close(struct hrr *);
/* offset+bytes must be inside the logical disk; max request is 1 MiB.
 * A failed read may modify its output buffer. A failed write leaves all
 * previously visible COW bytes unchanged (including on allocation/CRC errors).
 */
int hrr_transfer(void *, uint64_t offset, void *buffer, size_t bytes, int write);
int hrr_synchronize(void *);
int hrr_get_stats(struct hrr *, struct hrr_stats *);
#ifdef __cplusplus
}
#endif
#endif
