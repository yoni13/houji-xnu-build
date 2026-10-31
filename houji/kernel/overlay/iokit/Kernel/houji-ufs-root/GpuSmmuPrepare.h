/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Inactive native4K table preparation only. No SMMU register or DMA access.
 */
#ifndef HOUJI_GPU_SMMU_PREPARE_H
#define HOUJI_GPU_SMMU_PREPARE_H
#include "vendor/ArmSmmu4K/ArmSmmu4KAudit.h"
enum { HGT_OK,HGT_ARGUMENT,HGT_ALLOC,HGT_MAPPING,HGT_BUILD,HGT_AUDIT,HGT_FREE };
struct hgt_io {
  void *cookie;
  int (*allocate)(void *,unsigned,uint64_t *,void **);
  int (*validate)(void *,uint64_t,void *,unsigned);
  int (*release)(void *,uint64_t,unsigned);
};
struct hgt_result {
  unsigned code,allocated,validated,released,retained;
  uint64_t table_physical,data_physical;
  size_t table_pages,leaf_pages,descriptor_reads;
  unsigned build_status,audit_status;
};
struct hgt_tables {const volatile uint64_t *cpu;uint64_t physical;};
struct hgt_owned {
  void *tables,*data;uint64_t table_physical,data_physical;
  ArmSmmu4KConfig config;ArmSmmu4KResult built;ArmSmmu4KRange range;
};
static inline int hgt_page_attributes(uint64_t par,uint64_t wanted) {
  return !(par&1) && (par&UINT64_C(0x0000fffffffff000))==wanted &&
    (par>>56)==0xff && ((par>>7)&3)==3;
}
static inline int hgt_read_descriptor(void *cookie,uint64_t physical,uint64_t *value) {
  struct hgt_tables *t=cookie;
  if((physical&7) || physical<t->physical || physical-t->physical>=3*4096)return 1;
  *value=t->cpu[(physical-t->physical)/8];return 0;
}
static inline unsigned hgt_prepare_owned(const struct hgt_io *io,struct hgt_result *r,struct hgt_owned *owned) {
  void *tables=0,*data=0;unsigned char visited[3]={0};
  if(!r)return HGT_ARGUMENT;
  *r=(struct hgt_result){0};
  if(owned)*owned=(struct hgt_owned){0};
  if(!io || !io->allocate || !io->validate || !io->release)return r->code=HGT_ARGUMENT;
  if(io->allocate(io->cookie,3,&r->table_physical,&tables))return r->code=HGT_ALLOC;
  r->allocated|=1;
  if(io->allocate(io->cookie,1,&r->data_physical,&data)) {r->code=HGT_ALLOC;goto cleanup;}
  r->allocated|=2;
  if(!tables || !data || ((uintptr_t)tables&4095) || ((uintptr_t)data&4095) ||
     (r->table_physical&4095) || (r->data_physical&4095) ||
     (uintptr_t)tables>UINTPTR_MAX-3*4096 || (uintptr_t)data>UINTPTR_MAX-4096 ||
     r->table_physical>=(UINT64_C(1)<<36)-3*4096 ||
     r->data_physical>=(UINT64_C(1)<<36)-4096 ||
     (r->table_physical<r->data_physical+4096 && r->data_physical<r->table_physical+3*4096) ||
     ((uintptr_t)tables<(uintptr_t)data+4096 && (uintptr_t)data<(uintptr_t)tables+3*4096)) {
    r->code=HGT_MAPPING;goto cleanup;
  }
  if(io->validate(io->cookie,r->table_physical,tables,3) ||
     io->validate(io->cookie,r->data_physical,data,1)) {r->code=HGT_MAPPING;goto cleanup;}
  r->validated=3;
  for(unsigned i=0;i<4096/4;i++)((uint32_t *)data)[i]=0;
  ((uint32_t *)data)[0]=0x7504cafe;((uint32_t *)data)[1]=0x86500071;
  ArmSmmu4KConfig config={32,36,r->table_physical,1};
  ArmSmmu4KRange range={UINT64_C(0x60000000),r->data_physical,4096,
    ArmSmmu4KRead|ArmSmmu4KWrite,1,3};
  ArmSmmu4KResult built={0};
  r->build_status=ArmSmmu4KBuild(&config,&range,1,tables,3,&built);
  if(r->build_status || built.table_pages!=3 || built.leaf_pages!=1 || built.start_level!=1) {
    r->code=HGT_BUILD;goto cleanup;
  }
  r->table_pages=built.table_pages;r->leaf_pages=built.leaf_pages;
  struct hgt_tables actual={tables,r->table_physical};
  ArmSmmu4KTableIo physical={&actual,hgt_read_descriptor};
  ArmSmmu4KAuditRegion region={&range,1,3,3*512,visited,sizeof(visited)};
  ArmSmmu4KTableAuditResult audit={0};
  r->audit_status=ArmSmmu4KAuditTables(&physical,&config,&built,&region,built.root_physical,&audit);
  r->descriptor_reads=audit.descriptor_reads;
  if(r->audit_status || !audit.complete || audit.table_pages!=3 || audit.leaf_pages!=1)r->code=HGT_AUDIT;
  if(!r->code && owned) {
    *owned=(struct hgt_owned){tables,data,r->table_physical,r->data_physical,config,built,range};
    r->retained=3;return HGT_OK;
  }
cleanup:
  // These allocations were NEVER installed in a TTBR or exposed to a master.
  if(r->allocated&2) {
    if(io->release(io->cookie,r->data_physical,1))r->retained|=2;else r->released|=2;
  }
  if(r->allocated&1) {
    if(io->release(io->cookie,r->table_physical,3))r->retained|=1;else r->released|=1;
  }
  if(r->retained)r->code=HGT_FREE;
  return r->code;
}
static inline unsigned hgt_prepare(const struct hgt_io *io,struct hgt_result *r) {
  return hgt_prepare_owned(io,r,0);
}
#endif
