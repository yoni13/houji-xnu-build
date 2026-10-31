/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Allocate and audit the real SM8650 firmware/HFI layout, INACTIVE only.
 * No TTBR/stream, TCM, power, SMC or command access in this module.
 */
#ifndef HOUJI_GPU_FIRMWARE_MEMORY_H
#define HOUJI_GPU_FIRMWARE_MEMORY_H
#include "vendor/AdrenoFirmware/AdrenoFirmware.h"
#include "vendor/Ace5Gmu/giulia_gpu_memory.h"
#include "vendor/Ace5Gmu/giulia_gpu_hfi.h"
#include "vendor/ArmSmmu4K/ArmSmmu4KAudit.h"
#include "GpuSharedMemory.h"
#ifndef HFM_WITH_SCRATCH
#define HFM_WITH_SCRATCH 0
#endif
#if HFM_WITH_SCRATCH != 0 && HFM_WITH_SCRATCH != 1
#error Unsupported scratch profile
#endif
enum { HFM_MAX=8+3*HFM_WITH_SCRATCH,HFM_TABLE_PAGES=10,HFM_FW_BYTES=78588,
 HFM_ALL=(1u<<(HFM_MAX+1))-1,HFM_SCRATCH_IOVA=HSC_POOL_IOVA };
enum { HFM_OK,HFM_ARGUMENT,HFM_PLAN,HFM_ALLOC,HFM_MAPPING,HFM_COPY,HFM_TABLES,HFM_RELEASE };
struct hfm_allocation {uint64_t physical;void *cpu;unsigned pages,nc;uint32_t iova;};
struct hfm_result {
  unsigned code,records,reservations,count,allocated,validated,released,retained;
  unsigned bytes_loaded,tcm_bytes_deferred,hfi_queues,hfi_initialized,published;
  unsigned build_status,audit_status;
  struct hfm_allocation memory[HFM_MAX+1];
  ArmSmmu4KRange ranges[HFM_MAX];ArmSmmu4KConfig config;ArmSmmu4KResult built;
  ArmSmmu4KTableAuditResult audit;
};
struct hfm_io {
  void *cookie;
  int (*allocate)(void *,unsigned,uint64_t *,void **);
  /* nc=1 requires Normal NC CPU mapping, never Device. Must restore the
   * original CPU attributes before release; callback owns that bookkeeping. */
  int (*validate)(void *,uint64_t,void *,unsigned,unsigned nc);
  int (*publish)(void *,const struct hfm_allocation *);
  int (*release)(void *,uint64_t,void *,unsigned);
};
static inline unsigned hfm_release(const struct hfm_io *io,struct hfm_result *r) {
  for(unsigned i=r->count+1;i>0;i--) {
    unsigned n=i-1,bit=1u<<n;
    if((r->allocated&bit) && !(r->released&bit)) {
      const struct hfm_allocation *m=&r->memory[n];
      if(io->release(io->cookie,m->physical,m->cpu,m->pages))r->retained|=bit;
      else {r->released|=bit;r->retained&=~bit;}
    }
  }
  if(r->retained)r->code=HFM_RELEASE;
  return r->code;
}
static inline int hfm_descriptor(void *cookie,uint64_t pa,uint64_t *value) {
  const struct hfm_result *r=cookie;const struct hfm_allocation *m=&r->memory[r->count];
  if((pa&7) || pa<m->physical || pa-m->physical>=HFM_TABLE_PAGES*4096)return 1;
  *value=((const volatile uint64_t *)m->cpu)[(pa-m->physical)/8];return 0;
}
static inline unsigned hfm_prepare(const struct hfm_io *io,const uint8_t *fw,size_t bytes,struct hfm_result *r) {
  struct gg_fw_block blocks[GG_FW_MAX_BLOCKS];struct gg_reservation reservations[GG_FW_MAX_BLOCKS];
  AdrenoFirmwareBlock generic[GG_FW_MAX_BLOCKS];size_t nb=0,nr=0,ng=0;
  static const AdrenoFirmwareRegion regions[]={{0,0x4000,GG_ITCM},{0x4000,0xffc000,GG_ICACHE},
    {0x10004000,0x4000,GG_DTCM},{0x60000000,0x20000000,GG_KERNEL},{0xc0000000,0x20000000,GG_KERNEL_EXT}};
  if(!r)return HFM_ARGUMENT;*r=(struct hfm_result){0};
  if(!io || !io->allocate || !io->validate || !io->publish || !io->release ||
     !fw || bytes!=HFM_FW_BYTES)return r->code=HFM_ARGUMENT;
#define HFM_REQUIRE(test,error) do {if(!(test)){r->code=(error);goto fail;}} while(0)
  HFM_REQUIRE(AdrenoFirmwarePlanGmu(fw,bytes,regions,5,generic,GG_FW_MAX_BLOCKS,&ng)==AdrenoFirmwareSuccess &&
    gg_fw_plan(fw,bytes,blocks,GG_FW_MAX_BLOCKS,&nb)==GG_FW_OK && nb==ng &&
    gg_memory_plan(blocks,nb,reservations,GG_FW_MAX_BLOCKS,&nr)==GG_FW_OK && nr==5,HFM_PLAN);
  r->records=(unsigned)nb;r->reservations=(unsigned)nr;r->count=(unsigned)nr+3+3*HFM_WITH_SCRATCH;
  HFM_REQUIRE(r->count<=HFM_MAX,HFM_PLAN);
  for(size_t i=0;i<nb;i++) {
    HFM_REQUIRE(blocks[i].address==generic[i].address && blocks[i].size==generic[i].bytes &&
      blocks[i].payload_offset==generic[i].payload_offset,HFM_PLAN);
    if(blocks[i].size)HFM_REQUIRE(generic[i].region_identifier==(unsigned)blocks[i].region,HFM_PLAN);
  }
  for(unsigned i=0;i<r->count;i++) {
    struct hfm_allocation *m=&r->memory[i];
    if(i<nr){m->iova=reservations[i].iova;m->pages=reservations[i].bytes/4096;}
    else if(i==nr){m->iova=GGH_IOVA;m->pages=GGH_BYTES/4096;m->nc=1;}
    else if(i==nr+1){m->iova=GGH_LOG_IOVA;m->pages=GGH_LOG_BYTES/4096;m->nc=1;}
    else if(i==nr+2){m->iova=GGH_VRB_IOVA;m->pages=GGH_VRB_BYTES/4096;m->nc=1;}
    else {m->iova=i==nr+3?HFM_SCRATCH_IOVA:i==nr+4?HSC_GMU_ONLY_IOVA:HSC_LARGE_IOVA;
      m->pages=i==nr+5?HSC_LARGE_PAGES:HSC_POOL_PAGES;m->nc=1;}
  }
  r->memory[r->count].pages=HFM_TABLE_PAGES;
  for(unsigned i=0;i<=r->count;i++) {
    struct hfm_allocation *m=&r->memory[i];uint64_t length=(uint64_t)m->pages*4096;
    HFM_REQUIRE(m->pages && m->pages<=HSC_LARGE_PAGES && !io->allocate(io->cookie,m->pages,&m->physical,&m->cpu),HFM_ALLOC);
    r->allocated|=1u<<i;r->retained|=1u<<i;
    HFM_REQUIRE(m->physical && !(m->physical&4095) && m->cpu && !((uintptr_t)m->cpu&4095) &&
      m->physical<=(UINT64_C(1)<<36)-length && (uintptr_t)m->cpu<=UINTPTR_MAX-length,HFM_MAPPING);
    for(unsigned j=0;j<i;j++) {
      const struct hfm_allocation *a=&r->memory[j];uint64_t alen=(uint64_t)a->pages*4096;
      HFM_REQUIRE(!(m->physical<a->physical+alen && a->physical<m->physical+length) &&
        !((uintptr_t)m->cpu<(uintptr_t)a->cpu+alen && (uintptr_t)a->cpu<(uintptr_t)m->cpu+length),HFM_MAPPING);
    }
    HFM_REQUIRE(!io->validate(io->cookie,m->physical,m->cpu,m->pages,m->nc),HFM_MAPPING);
    r->validated|=1u<<i;
    for(unsigned w=0;w<m->pages*1024;w++)((volatile uint32_t *)m->cpu)[w]=0;
    if(i<r->count)r->ranges[i]=(ArmSmmu4KRange){m->iova,m->physical,length,
      ArmSmmu4KRead|ArmSmmu4KWrite|ArmSmmu4KPrivileged|
      ((i<nr && reservations[i].region==GG_ICACHE) || (HFM_WITH_SCRATCH && i==9)?ArmSmmu4KExecute:0),!m->nc,3};
  }
  for(size_t i=0;i<nb;i++) {
    const struct gg_fw_block *b=&blocks[i];if(!b->size)continue;
    if(b->region==GG_ITCM || b->region==GG_DTCM){r->tcm_bytes_deferred+=b->size;continue;}
    unsigned j;for(j=0;j<nr;j++)if(b->address>=r->memory[j].iova && b->size<=r->memory[j].pages*4096 &&
      b->address-r->memory[j].iova<=r->memory[j].pages*4096-b->size)break;
    HFM_REQUIRE(j<nr,HFM_COPY);
    volatile uint8_t *to=(volatile uint8_t *)r->memory[j].cpu+b->address-r->memory[j].iova;
    for(unsigned k=0;k<b->size;k++)to[k]=fw[b->payload_offset+k];
    for(unsigned k=0;k<b->size;k++)HFM_REQUIRE(to[k]==fw[b->payload_offset+k],HFM_COPY);
    r->bytes_loaded+=b->size;
  }
  HFM_REQUIRE(r->bytes_loaded==77808 && r->tcm_bytes_deferred==540 &&
    ggh_init(r->memory[nr].cpu,GGH_BYTES,GGH_IOVA)==GGH_OK,HFM_COPY);
  r->hfi_queues=GGH_QUEUES;r->hfi_initialized=1;
  struct hfm_allocation *t=&r->memory[r->count];r->config=(ArmSmmu4KConfig){32,36,t->physical,2048};
  r->build_status=ArmSmmu4KBuild(&r->config,r->ranges,r->count,t->cpu,HFM_TABLE_PAGES,&r->built);
  HFM_REQUIRE(!r->build_status,HFM_TABLES);
  for(unsigned i=0;i<=r->count;i++)HFM_REQUIRE(!io->publish(io->cookie,&r->memory[i]),HFM_MAPPING);
  r->published=1;
  unsigned char visited[HFM_TABLE_PAGES]={0};
  ArmSmmu4KTableIo actual={r,hfm_descriptor};
  ArmSmmu4KAuditRegion region={r->ranges,r->count,HFM_TABLE_PAGES,HFM_TABLE_PAGES*512,visited,sizeof(visited)};
  r->audit_status=ArmSmmu4KAuditTables(&actual,&r->config,&r->built,&region,t->physical,&r->audit);
  HFM_REQUIRE(!r->audit_status && r->audit.complete,HFM_TABLES);
  return HFM_OK;
fail:return hfm_release(io,r);
#undef HFM_REQUIRE
}
#endif
