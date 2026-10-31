// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef HOUJI_GPU_READ_POLICY_H
#define HOUJI_GPU_READ_POLICY_H
#include <stdint.h>
// Captured houji FDT + MiCode shennong-u-oss gcc/gpucc-pineapple.
static const uint64_t hgr_base[3]={0x171000,0x3d90000,0x3da0000};
static const uint32_t hgr_size[3]={0x1000,0x9800,0x1000};
static inline int hgr_allowed(unsigned window,uint32_t offset) {
  if(window>=3 || (offset&3) || offset>hgr_size[window]-4)return 0;
  if(window==0)return offset==4 || offset==0x10 || offset==0x18;
  if(window==1)return offset==0x9108 || offset==0x9168 || offset==0x911c ||
    offset==0x7000 || offset==0x913c;
  return offset==0 || offset==0x20 || offset==0x24 || offset==0x28 || offset==0x48;
}
#endif
