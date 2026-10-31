/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef HOUJI_SURFACE_ACQUIRE_H
#define HOUJI_SURFACE_ACQUIRE_H
#include <stddef.h>
#include <stdint.h>
struct HoujiSurfaceCacheRange {uintptr_t begin,end;};
static inline int houji_surface_cache_range(uintptr_t base,size_t allocation,uintptr_t row,size_t bytes,unsigned line,struct HoujiSurfaceCacheRange*out){
 if(!out || !base || !allocation || !bytes || line<16 || line>2048 || (line&(line-1)) || allocation>UINTPTR_MAX-base || row<base || row>base+allocation || bytes>base+allocation-row || bytes>UINTPTR_MAX-row || row+bytes>UINTPTR_MAX-(line-1))return 0;
 uintptr_t begin=row&~(uintptr_t)(line-1),end=(row+bytes+line-1)&~(uintptr_t)(line-1);
 if(begin<base || end>base+allocation || end<=begin)return 0;
 *out=(struct HoujiSurfaceCacheRange){begin,end};return 1;
}
#endif
