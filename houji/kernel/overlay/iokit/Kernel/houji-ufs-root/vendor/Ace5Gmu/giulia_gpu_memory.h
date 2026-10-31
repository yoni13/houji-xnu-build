/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef GIULIA_GPU_MEMORY_H
#define GIULIA_GPU_MEMORY_H
#include "giulia_gpu_firmware.h"
struct gg_reservation { uint32_t iova, bytes; enum gg_region region; unsigned persistent; };
/* Produce sorted, coalesced 4 KiB reservations from GMU allocation metadata.
 * TCM is device memory, never allocated as host RAM. Data outside TCM must
 * be covered by a reservation. No output is published until all checks pass.
 */
static inline enum gg_fw_status gg_memory_plan(const struct gg_fw_block *blocks,
    size_t count, struct gg_reservation *out, size_t capacity, size_t *out_count)
{
    struct gg_reservation work[GG_FW_MAX_BLOCKS];
    size_t n = 0, i, j;
    if (!out_count) return GG_FW_INVALID;
    *out_count = 0;
    if (!blocks || !count || count > GG_FW_MAX_BLOCKS || !out) return GG_FW_INVALID;
    for (i = 0; i < count; ++i) {
        const struct gg_fw_block *b = blocks+i;
        struct gg_reservation r;
        uint64_t end;
        if (b->size || (b->type != GG_PREALLOC && b->type != GG_PREALLOC_PERSIST)) continue;
        if (gg_fw_region(b->address,b->value) != b->region || b->region == GG_NONE) return GG_FW_RANGE;
        if (b->region == GG_ITCM || b->region == GG_DTCM) continue;
        end = ((uint64_t)b->address + b->value + 4095) & ~(uint64_t)4095;
        r.iova = b->address & ~4095u;
        r.bytes = (uint32_t)(end-r.iova); r.region = b->region;
        r.persistent = b->type == GG_PREALLOC_PERSIST;
        if (gg_fw_region(r.iova,r.bytes) != r.region) return GG_FW_RANGE;
        j=n;
        while (j && work[j-1].iova > r.iova) { work[j]=work[j-1]; --j; }
        work[j]=r; ++n;
    }
    /* Coalesce overlapping or adjacent pages, retaining the stronger lifetime. */
    j=0;
    for (i=0;i<n;++i) {
        if (j && work[j-1].region == work[i].region &&
            (uint64_t)work[i].iova <= (uint64_t)work[j-1].iova+work[j-1].bytes) {
            uint64_t end=(uint64_t)work[i].iova+work[i].bytes;
            if (end > (uint64_t)work[j-1].iova+work[j-1].bytes)
                work[j-1].bytes=(uint32_t)(end-work[j-1].iova);
            work[j-1].persistent |= work[i].persistent;
        } else work[j++]=work[i];
    }
    n=j;
    for (i=0;i<count;++i) {
        const struct gg_fw_block *b=blocks+i;
        if (!b->size) continue;
        if (gg_fw_region(b->address,b->size) != b->region || b->region == GG_NONE) return GG_FW_RANGE;
        if (b->region == GG_ITCM || b->region == GG_DTCM) continue;
        for (j=0;j<n;++j)
            if (b->address >= work[j].iova && b->size <= work[j].bytes &&
                b->address-work[j].iova <= work[j].bytes-b->size) break;
        if (j==n) return GG_FW_RANGE;
    }
    if (!n) return GG_FW_INVALID;
    if (n>capacity) return GG_FW_CAPACITY;
    for (i=0;i<n;++i) out[i]=work[i];
    *out_count=n;
    return GG_FW_OK;
}
#endif
