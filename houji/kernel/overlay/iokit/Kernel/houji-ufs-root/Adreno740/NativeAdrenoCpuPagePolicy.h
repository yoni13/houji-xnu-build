/* SPDX-License-Identifier: MIT */
/* Candidate only: CPU VM geometry is independent of the GPU's 4 KiB tables. */
#ifndef NATIVE_ADRENO_CPU_PAGE_POLICY_H
#define NATIVE_ADRENO_CPU_PAGE_POLICY_H
#include <stddef.h>
#include <stdint.h>
static inline bool NativeAdrenoCpuPageGeometry(uint64_t compiled,uint64_t runtime,
    uint64_t map_bytes,uint64_t tcr,uint64_t sctlr,uint64_t ttbr1,unsigned *shift)
{
    if(shift)*shift=0;
    if(!shift || (compiled!=4096 && compiled!=16384) || runtime!=compiled || map_bytes!=compiled || !(sctlr&1))return false;
    const unsigned bits=compiled==16384?14:12;
    const unsigned tg0=compiled==16384?2:0,tg1=compiled==16384?1:2;
    if(((tcr>>14)&3)!=tg0 || ((tcr>>30)&3)!=tg1 ||
        !(ttbr1&(UINT64_C(0x0000ffffffffffff)&~(compiled-1))))return false;
    *shift=bits;return true;
}
static inline bool NativeAdrenoRoundCpuBacking(size_t logical,size_t cpu_bytes,size_t *owned)
{
    if(owned)*owned=0;
    if(!owned || !logical || (logical&4095) || (cpu_bytes!=4096 && cpu_bytes!=16384) ||
        logical>SIZE_MAX-(cpu_bytes-1))return false;
    *owned=(logical+cpu_bytes-1)&~(cpu_bytes-1);return true;
}
#endif
