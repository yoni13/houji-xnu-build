// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "HoujiLogAllocation.h"

// This is additional to the r24 read-only command gate. Default: disabled.
static bool houjiUfsLogCommandAllowed(const uint8_t *u, unsigned n, unsigned dir,
                                    uint32_t bytes, bool enabled, bool armed) {
  if (!enabled || !u || n != 32 || u[0] != 1 || u[2] || u[3] || u[8] || u[10] || u[11]) return false;
  for (unsigned i=4;i<8;i++) if(u[i]) return false;
  if (u[9]) return false;
  const uint8_t *c=u+16;
  const uint32_t transfer=(uint32_t(u[12])<<24)|(uint32_t(u[13])<<16)|(uint32_t(u[14])<<8)|u[15];
  if (c[0]==0x35) {
    if(!armed || dir || bytes || transfer || u[1]) return false;
    for(unsigned i=1;i<16;i++) if(c[i]) return false;
    return true; // SYNCHRONIZE CACHE(10), no payload, only while writer owns UFS.
  }
  if (c[0]!=0x28 && c[0]!=0x2a) return false;
  bool write=c[0]==0x2a;
  if ((write && !armed) || bytes!=4096 || transfer!=4096 || dir!=(write?1U:2U) ||
      u[1]!=(write?0x20:0x40) || c[1] || c[6] || c[7] || c[8]!=1) return false;
  for(unsigned i=9;i<16;i++) if(c[i]) return false;
  uint32_t lba=(uint32_t(c[2])<<24)|(uint32_t(c[3])<<16)|(uint32_t(c[4])<<8)|c[5];
  if(!write && lba>=4096 && lba<4103) return true; // BPB, FAT copies, root directory.
  for(unsigned i=0;i<16;i++) if(lba==kHoujiLogLbas[i]) return true;
  return false;
}
