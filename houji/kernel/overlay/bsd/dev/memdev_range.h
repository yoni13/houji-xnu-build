/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Pure arithmetic shared by memdev and host boundary tests. */
#ifndef _BSD_DEV_MEMDEV_RANGE_H_
#define _BSD_DEV_MEMDEV_RANGE_H_
#include <stdint.h>

static inline uint64_t
mdev_bytes_from_pages(uint32_t pages)
{
    /* The mdev ABI uses 4 KiB units, independently of kernel PAGE_SHIFT. */
    return ((uint64_t)pages) << 12;
}

static inline int
mdev_block_offset(int64_t block, uint32_t sector_bytes, uint64_t *offset)
{
    if (block < 0 || sector_bytes == 0 ||
        (uint64_t)block > UINT64_MAX / sector_bytes) {
        return 0;
    }
    *offset = (uint64_t)block * sector_bytes;
    return 1;
}

static inline uint64_t
mdev_transfer_bytes(uint64_t disk_bytes, uint64_t offset,
    uint64_t requested, uint64_t limit)
{
    if (offset >= disk_bytes) {
        return 0;
    }
    uint64_t remaining = disk_bytes - offset;
    uint64_t count = requested < remaining ? requested : remaining;
    return count < limit ? count : limit;
}

static inline uint64_t
mdev_buffer_pa(uint32_t ppn, uint64_t va, unsigned int page_shift)
{
    /* ppn is a kernel page number, not a mdev 4 KiB page number. */
    return ((uint64_t)ppn << page_shift) | (va & ((UINT64_C(1) << page_shift) - 1));
}
#endif
