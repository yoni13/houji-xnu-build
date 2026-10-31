/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Generated from this houji FDT/AOP and read-only SKU evidence.
 * HFI ACKs are requests, not measured core frequencies. */
#ifndef HOUJI_GPU_FACTORY_OPP_H
#define HOUJI_GPU_FACTORY_OPP_H
#include <stdint.h>
enum { HCO_COUNT=12, HCO_BOOT_LEVEL=5 };
struct HoujiGpuOpp {uint32_t khz,arc_vote,bw_index;};
static const struct HoujiGpuOpp houjiGpuOpps[HCO_COUNT]={
 {231000u,0x00340201u,1u},
 {310000u,0x00380202u,1u},
 {366000u,0x003c0203u,3u},
 {422000u,0x00400204u,5u},
 {500000u,0x00500305u,5u},
 {578000u,0x00800306u,5u},
 {629000u,0x00900407u,6u},
 {680000u,0x00c00408u,7u},
 {720000u,0x00e00509u,7u},
 {770000u,0x0100050au,7u},
 {834000u,0x0140060bu,8u},
 {903000u,0x0180070cu,9u},
};
static inline const struct HoujiGpuOpp *hco_opp(unsigned level) {
 return level && level<=HCO_COUNT ? &houjiGpuOpps[level-1] : 0;
}
static inline unsigned hco_at_least(uint64_t khz,unsigned cap) {
 if(!cap || cap>HCO_COUNT)return 0;
 unsigned level=1;while(level<cap && houjiGpuOpps[level-1].khz<khz)++level;
 return level;
}
#endif
