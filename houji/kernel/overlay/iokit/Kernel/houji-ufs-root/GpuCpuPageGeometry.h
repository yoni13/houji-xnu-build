/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * CPU VM granules and GPU 4 KiB page numbers are independent units.
 * Pure read-only policy: no MMIO, pmap, cache or device operation.
 */
#ifndef HOUJI_GPU_CPU_PAGE_GEOMETRY_H
#define HOUJI_GPU_CPU_PAGE_GEOMETRY_H
#include <stdint.h>
enum {HCPG_GPU_PAGE=4096,HCPG_OWNED_BYTES=16384,HCPG_ROWS=4};
enum {HCPG_CPU=1,HCPG_COMPILED=2,HCPG_RUNTIME=4,HCPG_MAP=8,HCPG_TG0=16,
 HCPG_TG1=32,HCPG_MMU=64,HCPG_TTBR1=128,HCPG_EPD1=256};
struct hcpg_sample {
 int cpu_before,cpu_after;
 uint64_t tcr,ttbr0,ttbr1,sctlr,compiled_bytes,runtime_bytes,map_bytes;
};
struct hcpg_geometry {
 unsigned issues,cpu_shift,tg0,tg1,t0sz,t1sz,epd0,epd1,ips;
};
static inline unsigned hcpg_geometry_check(const struct hcpg_sample *s,struct hcpg_geometry *r) {
 if(!s || !r)return HCPG_COMPILED;
 *r=(struct hcpg_geometry){0,0,0,0,0,0,0,0,0};
 r->tg0=(unsigned)(s->tcr>>14)&3;r->tg1=(unsigned)(s->tcr>>30)&3;
 r->t0sz=(unsigned)s->tcr&63;r->t1sz=(unsigned)(s->tcr>>16)&63;
 r->epd0=(unsigned)(s->tcr>>7)&1;r->epd1=(unsigned)(s->tcr>>23)&1;
 r->ips=(unsigned)(s->tcr>>32)&7;
 if(s->cpu_before<0 || s->cpu_before!=s->cpu_after)r->issues|=HCPG_CPU;
 if(s->compiled_bytes!=4096 && s->compiled_bytes!=16384)r->issues|=HCPG_COMPILED;
 else r->cpu_shift=s->compiled_bytes==16384?14:12;
 if(s->runtime_bytes!=s->compiled_bytes)r->issues|=HCPG_RUNTIME;
 if(s->map_bytes!=s->compiled_bytes)r->issues|=HCPG_MAP;
 if(r->cpu_shift) {
  if(r->tg0!=(r->cpu_shift==14?2u:0u))r->issues|=HCPG_TG0;
  if(r->tg1!=(r->cpu_shift==14?1u:2u))r->issues|=HCPG_TG1;
  if(!(s->ttbr1&(UINT64_C(0x0000ffffffffffff)&~(s->compiled_bytes-1))))r->issues|=HCPG_TTBR1;
 }
 if(!(s->sctlr&1))r->issues|=HCPG_MMU;
 if(r->epd1)r->issues|=HCPG_EPD1;
 return r->issues;
}
enum {HCPG_ROW_ARGUMENT=1,HCPG_ROW_PA=2,HCPG_ROW_LENGTH=4,HCPG_ROW_CONTIGUITY=8,
 HCPG_ROW_CPU_ALIGNMENT=16,HCPG_ROW_NUMBER=32};
struct hcpg_row {
 uint64_t offset,physical_byte,segment_bytes,cpu_ppn,gpu4k_ppn;
 unsigned issues,valid,cpu_subpage,gpu36_fit;
};
static inline unsigned hcpg_segment_check(uint64_t cpu_bytes,uint64_t first_pa,
 uint64_t offset,uint64_t pa,uint64_t length,struct hcpg_row *r) {
 if(!r)return HCPG_ROW_ARGUMENT;
 *r=(struct hcpg_row){.offset=offset,.physical_byte=pa,.segment_bytes=length};
 if((cpu_bytes!=4096 && cpu_bytes!=16384) || offset>=HCPG_OWNED_BYTES ||
  (offset&(HCPG_GPU_PAGE-1)))return r->issues=HCPG_ROW_ARGUMENT;
 if(!pa || (pa&(HCPG_GPU_PAGE-1)) || pa>UINT64_MAX-HCPG_GPU_PAGE)r->issues|=HCPG_ROW_PA;
 if(length<HCPG_GPU_PAGE)r->issues|=HCPG_ROW_LENGTH;
 if(!first_pa || first_pa>UINT64_MAX-HCPG_OWNED_BYTES || pa!=first_pa+offset)
  r->issues|=HCPG_ROW_CONTIGUITY;
 if(first_pa&(cpu_bytes-1))r->issues|=HCPG_ROW_CPU_ALIGNMENT;
 unsigned shift=cpu_bytes==16384?14:12;
 r->cpu_ppn=pa>>shift;r->gpu4k_ppn=pa>>12;
 r->cpu_subpage=(unsigned)(pa&(cpu_bytes-1))>>12;
 if(r->gpu4k_ppn!=(r->cpu_ppn<<(shift-12))+r->cpu_subpage)r->issues|=HCPG_ROW_NUMBER;
 r->gpu36_fit=pa<(UINT64_C(1)<<36) && HCPG_GPU_PAGE<=(UINT64_C(1)<<36)-pa;
 r->valid=!r->issues;return r->issues;
}
#endif
