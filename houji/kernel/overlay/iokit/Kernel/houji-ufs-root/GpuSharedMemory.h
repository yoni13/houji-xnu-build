/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Bounded, single-owner development arena. Both device graphs are prepared
 * before activation; no live leaf edit, alias, allocation or MMIO in this file.
 * Only ordinary known-permission shared/GMU-only data; never secure/MMIO,
 * dedicated memstore/fence resources or executable microcode.
 */
#ifndef HOUJI_GPU_SHARED_MEMORY_H
#define HOUJI_GPU_SHARED_MEMORY_H
#include <stdint.h>
#include "GpuCodeMemory.h"
enum { HSC_POOL_PAGES=256,HSC_POOL_BYTES=HSC_POOL_PAGES*4096,HSC_POOL_IOVA=0x61000000,
 HSC_GMU_ONLY_IOVA=0x61100000,HSC_LARGE_IOVA=0x61200000,HSC_LARGE_PAGES=1024,
 HSC_LARGE_BYTES=HSC_LARGE_PAGES*4096,HSC_LARGE_TABLE_PAGES=5,HSC_LARGE_LEAF_PAGES=HSC_POOL_PAGES+HSC_LARGE_PAGES,
 HSC_DATA_TABLE_PAGES=6,HSC_DATA_LEAF_PAGES=2*HSC_POOL_PAGES+HSC_LARGE_PAGES,HSC_DATA_RANGES=9,
 HSC_GPU_TABLE_PAGES=7,HSC_GPU_LEAF_PAGES=HSC_DATA_LEAF_PAGES+HCC_LEAVES,HSC_GPU_RANGES=HSC_DATA_RANGES+HCC_RANGES,
 HSC_CLASSES=4,HSC_CLASS_BYTES=HSC_POOL_BYTES/HSC_CLASSES,HSC_MAX_RESOURCES=32 };
#define HSC_GPU_ONLY_IOVA UINT32_C(0x62000000)
struct hsc_slice {uint32_t offset,bytes,next;};
/* BUFFERABLE bit6 describes the same noncached GMU VA range used here.
 * HOST_ACC/HOST_INIT do not change device page permissions. This private
 * owner initializes its own backing; the requested logical size/host bits
 * remain intact on the wire. CACHE bit7 stays unsupported. */
static inline unsigned hsc_resource_class(uint32_t flags) {return (flags&~0x346u)==0x39u?(flags>>1)&3:HSC_CLASSES;}
static inline int hsc_plan_slice(const uint32_t *request,unsigned words,uint32_t cursor,
 uint32_t start,uint32_t end,uint32_t iova,struct hsc_slice *out) {
 if(!request || !out || words!=11 || (request[0]&0xfffffu)!=0xb14 || request[1]!=1 ||
    request[2] || request[3] || request[6] || request[8] ||
    !request[9] || start>end || end>HSC_LARGE_BYTES || request[9]>end-start || (request[10]>>16) ||
    (cursor&4095) || cursor<start || cursor>end)return 1;
 unsigned kind=request[5],va_bits=request[10]&255,size_bits=(request[10]>>8)&255;
 if(kind>=26 || kind==5 || kind==11 || kind==12 || kind==13 || kind==19 || kind==20 || kind==22 || kind==23 ||
    va_bits>20 || size_bits>20)return 1;
 unsigned va_align=1u<<(va_bits<12?12:va_bits),size_align=1u<<(size_bits<12?12:size_bits);
 uint32_t offset=((iova+cursor+va_align-1)&~(va_align-1))-iova;
 uint32_t bytes=(request[9]+size_align-1)&~(size_align-1);
 if(offset>end || bytes>end-offset)return 1;
 *out=(struct hsc_slice){offset,bytes,offset+bytes};return 0;
}
static inline int hsc_resource_plan(const uint32_t *request,unsigned words,uint32_t cursor,struct hsc_slice *out) {
 if(!request || words!=11)return 1;unsigned cls=hsc_resource_class(request[4]);if(cls>=HSC_CLASSES)return 1;
 return hsc_plan_slice(request,words,cursor,cls*HSC_CLASS_BYTES,(cls+1)*HSC_CLASS_BYTES,HSC_POOL_IOVA,out);
}
static inline int hsc_gmu_only_flags(uint32_t flags) {return (flags&~0x300u)==0x78u;}
static inline int hsc_gmu_only_plan(const uint32_t *request,unsigned words,uint32_t cursor,struct hsc_slice *out) {
 if(!request || words!=11 || !hsc_gmu_only_flags(request[4]))return 1;
 return hsc_plan_slice(request,words,cursor,0,HSC_POOL_BYTES,HSC_GMU_ONLY_IOVA,out);
}
/* Ordinary GFX-only family: no GMU access/privilege/write/cache bit at all.
 * Four exact GFX permission classes; optional host/bufferable bits preserved. */
static inline unsigned hsc_gpu_only_class(uint32_t flags) {return (flags&~0x346u)==1u?(flags>>1)&3:HSC_CLASSES;}
static inline int hsc_gpu_only_plan(const uint32_t *request,unsigned words,uint32_t cursor,struct hsc_slice *out) {
 if(!request || words!=11)return 1;unsigned cls=hsc_gpu_only_class(request[4]);if(cls>=HSC_CLASSES)return 1;
 return hsc_plan_slice(request,words,cursor,cls*HSC_CLASS_BYTES,(cls+1)*HSC_CLASS_BYTES,HSC_GPU_ONLY_IOVA,out);
}
/* Large ordinary nonsecure CONTEXT in a distinct prepublished privileged RW
 * shared arena. The secure CONTEXT request is never accepted by this path. */
static inline int hsc_large_request(const uint32_t *request,unsigned words) {
 return request && words==11 && request[5]==4 && hsc_resource_class(request[4])==3 && request[9]>HSC_CLASS_BYTES;
}
static inline int hsc_large_plan(const uint32_t *request,unsigned words,uint32_t cursor,struct hsc_slice *out) {
 if(!hsc_large_request(request,words))return 1;
 return hsc_plan_slice(request,words,cursor,0,HSC_LARGE_BYTES,HSC_LARGE_IOVA,out);
}
/* Wire encoding only; the caller must already prove actual mapping/ownership. */
static inline int hsc_resource_response(const uint32_t *request,unsigned words,const struct hsc_slice *slice,uint32_t *response) {
 if(!request || words!=11 || !slice || !response)return 1;
 unsigned gmu_only=hsc_gmu_only_flags(request[4]),large=hsc_large_request(request,words),gpu_only=hsc_gpu_only_class(request[4])<HSC_CLASSES;struct hsc_slice checked;
 int error=gpu_only?hsc_gpu_only_plan(request,words,slice->offset,&checked):large?hsc_large_plan(request,words,slice->offset,&checked):gmu_only?hsc_gmu_only_plan(request,words,slice->offset,&checked):hsc_resource_plan(request,words,slice->offset,&checked);
 if(error || checked.offset!=slice->offset || checked.bytes!=slice->bytes || checked.next!=slice->next)return 1;
 for(unsigned i=0;i<11;i++)response[i]=request[i];
 response[2]=gmu_only?0:(gpu_only?HSC_GPU_ONLY_IOVA:large?HSC_LARGE_IOVA:HSC_POOL_IOVA)+slice->offset;response[3]=0;response[6]=request[7];
 response[8]=gpu_only?0:(large?HSC_LARGE_IOVA:gmu_only?HSC_GMU_ONLY_IOVA:HSC_POOL_IOVA)+slice->offset;return 0;
}
#endif
