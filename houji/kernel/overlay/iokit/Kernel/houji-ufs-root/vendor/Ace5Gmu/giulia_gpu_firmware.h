/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef GIULIA_GPU_FIRMWARE_H
#define GIULIA_GPU_FIRMWARE_H
#include <stddef.h>
#include <stdint.h>

/* Gen7 GMU firmware layout, verified against the pinned OnePlus source.
 * These are GMU virtual addresses, never physical/DMA allocation addresses.
 * This planner performs no allocation, MMIO, authentication or cache work.
 * The caller must verify the pinned firmware digest before using the plan.
 */
enum gg_region { GG_ITCM, GG_ICACHE, GG_DTCM, GG_KERNEL, GG_KERNEL_EXT, GG_NONE };
enum gg_fw_status { GG_FW_OK, GG_FW_INVALID, GG_FW_TRUNCATED, GG_FW_RANGE,
                    GG_FW_CAPACITY, GG_FW_ORDER, GG_FW_OVERLAP };
enum { GG_FW_MAX_BLOCKS = 64, GG_PREALLOC = 1, GG_PREALLOC_PERSIST = 7 };
struct gg_fw_block {
    uint32_t address, size, type, value;
    size_t payload_offset;
    enum gg_region region;
};

static inline uint32_t gg_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline enum gg_region gg_fw_region(uint32_t address, uint32_t size)
{
    static const uint32_t ranges[][2] = {
        {0, 0x4000}, {0x4000, 0xffc000}, {0x10004000, 0x4000},
        {0x60000000, 0x20000000}, {0xc0000000, 0x20000000}
    };
    unsigned i;
    if (!size || (address & 3) || (size & 3)) return GG_NONE;
    for (i = 0; i < sizeof(ranges)/sizeof(ranges[0]); ++i)
        if (address >= ranges[i][0] && size <= ranges[i][1] &&
            address - ranges[i][0] <= ranges[i][1] - size)
            return (enum gg_region)i;
    return GG_NONE;
}

/* Input must remain immutable and may not alias output. No output entries
 * change on failure; *count is reset even when the input is malformed.
 * Preserve all metadata, including persistent allocations and version tags.
 */
static inline enum gg_fw_status gg_fw_plan(const uint8_t *data, size_t bytes,
    struct gg_fw_block *out, size_t capacity, size_t *count)
{
    unsigned pass;
    if (!count) return GG_FW_INVALID;
    *count = 0;
    if (!data || !out || !capacity) return GG_FW_INVALID;
    if (bytes < 16) return GG_FW_TRUNCATED;
    for (pass = 0; pass < 2; ++pass) {
        size_t offset = 0, n = 0;
        int payload_seen = 0;
        while (offset < bytes) {
            struct gg_fw_block b;
            if (bytes - offset < 16) return GG_FW_TRUNCATED;
            b.address = gg_le32(data + offset);
            b.size = gg_le32(data + offset + 4);
            b.type = gg_le32(data + offset + 8);
            b.value = gg_le32(data + offset + 12);
            b.payload_offset = offset + 16;
            b.region = GG_NONE;
            if (b.size > bytes - b.payload_offset) return GG_FW_TRUNCATED;
            if (n == capacity || n == GG_FW_MAX_BLOCKS) return GG_FW_CAPACITY;
            if (!b.size) {
                if (payload_seen) return GG_FW_ORDER;
                if (b.type < 1 || b.type > 7) return GG_FW_INVALID;
                if (b.type == GG_PREALLOC || b.type == GG_PREALLOC_PERSIST) {
                    b.region = gg_fw_region(b.address, b.value);
                    if (b.region == GG_NONE) return GG_FW_RANGE;
                }
            } else {
                size_t previous = 0;
                payload_seen = 1;
                b.region = gg_fw_region(b.address, b.size);
                if (b.region == GG_NONE) return GG_FW_RANGE;
                /* Firmware data headers do not necessarily have type=0. */
                while (previous < offset) {
                    uint32_t a = gg_le32(data + previous);
                    uint32_t s = gg_le32(data + previous + 4);
                    if (s && (uint64_t)a < (uint64_t)b.address + b.size &&
                        (uint64_t)b.address < (uint64_t)a + s)
                        return GG_FW_OVERLAP;
                    previous += 16 + (size_t)s;
                }
            }
            if (pass) out[n] = b;
            ++n;
            offset = b.payload_offset + b.size;
        }
        if (!payload_seen) return GG_FW_INVALID;
        if (pass) *count = n;
    }
    return GG_FW_OK;
}

/* Both SQE and AQE omit their first file dword when copied into GPU memory. */
static inline enum gg_fw_status gg_ucode_payload(const uint8_t *data,
    size_t bytes, size_t *offset, size_t *size)
{
    if (!offset || !size) return GG_FW_INVALID;
    *offset = *size = 0;
    if (!data || bytes < 8 || (bytes & 3)) return GG_FW_INVALID;
    *offset = 4;
    *size = bytes - 4;
    return GG_FW_OK;
}
#endif
