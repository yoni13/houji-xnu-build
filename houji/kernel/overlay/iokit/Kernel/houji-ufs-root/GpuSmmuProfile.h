/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * GMU uses context bank 2 on SM8650. Banks 0/1 belong to GPU hardware
 * page-table switching. Bank 0 remains a host-test compatibility profile.
 */
#ifndef HOUJI_GPU_SMMU_PROFILE_H
#define HOUJI_GPU_SMMU_PROFILE_H
#ifndef HGP_CONTEXT_BANK
#define HGP_CONTEXT_BANK 2
#endif
#if HGP_CONTEXT_BANK != 0 && HGP_CONTEXT_BANK != 2
#error Unsupported GMU context bank
#endif
#define HGP_CB(offset) (0x03dc0000u + 0x1000u * HGP_CONTEXT_BANK + (offset))
#define HGP_CBAR (0x03da1000u + 4u * HGP_CONTEXT_BANK)
#define HGP_CBA2R (0x03da1800u + 4u * HGP_CONTEXT_BANK)
#endif
