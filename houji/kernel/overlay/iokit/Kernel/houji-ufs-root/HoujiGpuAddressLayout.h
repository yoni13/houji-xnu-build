/* SPDX-License-Identifier: MIT */
#ifndef HOUJI_GPU_ADDRESS_LAYOUT_H
#define HOUJI_GPU_ADDRESS_LAYOUT_H
#include <stdint.h>
/* The existing SMMU context has 32 input bits and 36 physical output bits.
 * Prebuild its whole lower 4 GiB, without changing TCR or opening TTBR1.
 * One L1 root, four L2 pages, 2048 L3 pages; round ownership to CPU16K.
 * These are address-space limits, not a reservation of 4 GiB of RAM. */
#define HOUJI_GPU_APERTURE UINT64_C(0x100000000)
#define HOUJI_GPU_TABLE_USED_PAGES 2053u
#define HOUJI_GPU_TABLE_OWNED_PAGES 2056u
/* The low half retains firmware/ring/bootstrap mappings and wallpaper
 * clients. Slot zero gets a contiguous 2 GiB above all boot mappings. */
#define HOUJI_GPU_RENDER_START UINT64_C(0x80000000)
#define HOUJI_GPU_RENDER_BYTES UINT64_C(0x80000000)
#endif
