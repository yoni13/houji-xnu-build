/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Own-device fixed CP firmware files; independent sparse GPU-only code/record
 * arena. Planning/copying only: no MMIO, SMC, CP start or raw command.
 */
#ifndef HOUJI_GPU_CODE_MEMORY_H
#define HOUJI_GPU_CODE_MEMORY_H
#include <stdint.h>
#include <stddef.h>
#include "vendor/ArmSmmu4K/ArmSmmu4K.h"
#define HCC_IOVA UINT32_C(0x63000000)
enum {HCC_PAGES=64,HCC_BYTES=HCC_PAGES*4096,HCC_RANGES=3,HCC_SQE_BYTES=85300,HCC_AQE_BYTES=33176,
 HCC_SQE_OFFSET=0x1000,HCC_AQE_OFFSET=0x21000,HCC_RECORD_OFFSET=0x3f000,HCC_LEAVES=31};
static inline uint32_t hcc_crc32(const uint8_t *p,size_t bytes) {
 uint32_t c=~0u;for(size_t i=0;i<bytes;i++){c^=p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}return ~c;
}
static inline int hcc_firmware(const uint8_t *data,size_t bytes,unsigned aqe) {
 if(!data || (uintptr_t)data>UINTPTR_MAX-bytes || aqe>1 || bytes!=(aqe?HCC_AQE_BYTES:HCC_SQE_BYTES) || data[0] || data[1] || data[2] || data[3])return 1;
 return hcc_crc32(data,bytes)!=(aqe?UINT32_C(0x0d3eee1a):UINT32_C(0xb2840b8d));
}
static inline int hcc_copy(void *cpu,size_t bytes,const uint8_t *sqe,size_t sqe_bytes,const uint8_t *aqe,size_t aqe_bytes) {
 if(!cpu || ((uintptr_t)cpu&4095) || (uintptr_t)cpu>UINTPTR_MAX-HCC_BYTES || bytes!=HCC_BYTES || hcc_firmware(sqe,sqe_bytes,0) || hcc_firmware(aqe,aqe_bytes,1))return 1;
 if(((uintptr_t)sqe<(uintptr_t)cpu+HCC_BYTES && (uintptr_t)cpu<(uintptr_t)sqe+sqe_bytes) ||
  ((uintptr_t)aqe<(uintptr_t)cpu+HCC_BYTES && (uintptr_t)cpu<(uintptr_t)aqe+aqe_bytes))return 1;
 volatile uint8_t *to=cpu;for(size_t i=0;i<HCC_BYTES;i++)to[i]=0;
 for(size_t i=4;i<sqe_bytes;i++)to[HCC_SQE_OFFSET+i-4]=sqe[i];
 for(size_t i=4;i<aqe_bytes;i++)to[HCC_AQE_OFFSET+i-4]=aqe[i];
 for(size_t i=0;i<HCC_BYTES;i++) {
  uint8_t expected=0;if(i>=HCC_SQE_OFFSET && i<HCC_SQE_OFFSET+sqe_bytes-4)expected=sqe[i-HCC_SQE_OFFSET+4];
  else if(i>=HCC_AQE_OFFSET && i<HCC_AQE_OFFSET+aqe_bytes-4)expected=aqe[i-HCC_AQE_OFFSET+4];
  if(to[i]!=expected)return 1;
 }return 0;
}
static inline int hcc_ranges(uint64_t physical,ArmSmmu4KRange ranges[HCC_RANGES]) {
 if(!ranges || !physical || (physical&4095) || physical>=(UINT64_C(1)<<36) || HCC_BYTES>(UINT64_C(1)<<36)-physical)return 1;
 ranges[0]=(ArmSmmu4KRange){HCC_IOVA+HCC_SQE_OFFSET,physical+HCC_SQE_OFFSET,21*4096,ArmSmmu4KRead|ArmSmmu4KExecute,0,3};
 ranges[1]=(ArmSmmu4KRange){HCC_IOVA+HCC_AQE_OFFSET,physical+HCC_AQE_OFFSET,9*4096,ArmSmmu4KRead|ArmSmmu4KExecute,0,3};
 ranges[2]=(ArmSmmu4KRange){HCC_IOVA+HCC_RECORD_OFFSET,physical+HCC_RECORD_OFFSET,4096,ArmSmmu4KRead|ArmSmmu4KWrite|ArmSmmu4KPrivileged,0,3};return 0;
}
#endif
