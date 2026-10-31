/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Selected-bank DISABLED-context register test. M is never set; no stream writes.
 * Record initial values, but never replay uninitialized register contents.
 * Retire to a known disabled image, clear TTBRs, then bank-local TLB sync.
 */
#ifndef HOUJI_GPU_SMMU_CONTEXT_H
#define HOUJI_GPU_SMMU_CONTEXT_H
#include "GpuSmmuPrepare.h"
#include "GpuSmmuProfile.h"
enum { HGP_OK,HGP_ARGUMENT,HGP_GUARD,HGP_IO,HGP_VERIFY,HGP_TIMEOUT,HGP_RESTORE };
struct hgp_io {
  void *cookie;
  int (*read32)(void *,uint32_t,uint32_t *);
  int (*read64)(void *,uint32_t,uint64_t *);
  int (*write32)(void *,uint32_t,uint32_t);
  int (*write64)(void *,uint32_t,uint64_t);
  int (*delay)(void *,unsigned);
  int (*power_guard)(void *);
  int (*checkpoint)(void *,unsigned);
};
struct hgp_result {
  unsigned code,reads,writes,polls,backed_up,attempted,retired,safe_to_release;
  uint32_t last_address,fsr;uint64_t last_value,saved[9],configured[9];
};
static inline uint32_t hgp_field_address(unsigned i) {
  static const uint32_t a[]={HGP_CB(0),HGP_CBA2R,HGP_CBAR,HGP_CB(0x10),
    HGP_CB(0x30),HGP_CB(0x20),HGP_CB(0x28),HGP_CB(0x38),HGP_CB(0x3c)};
  return i<9?a[i]:0;
}
static inline int hgp_field_index(uint32_t a) {
  for(unsigned i=0;i<9;i++)if(hgp_field_address(i)==a)return (int)i;return -1;
}
static inline int hgp_read(const struct hgp_io *io,struct hgp_result *r,uint32_t a,uint64_t *v,int wide) {
  uint32_t narrow=0;r->last_address=a;r->reads++;
  int error=wide?io->read64(io->cookie,a,v):io->read32(io->cookie,a,&narrow);
  if(error) {if(!r->code)r->code=HGP_IO;return 0;}
  if(!wide)*v=narrow;r->last_value=*v;return 1;
}
static inline int hgp_write(const struct hgp_io *io,struct hgp_result *r,uint32_t a,uint64_t v,int wide) {
  r->last_address=a;r->writes++;
  int error=wide?io->write64(io->cookie,a,v):io->write32(io->cookie,a,(uint32_t)v);
  if(error) {if(!r->code)r->code=HGP_IO;return 0;}return 1;
}
static inline int hgp_guard(const struct hgp_io *io,struct hgp_result *r) {
  uint64_t v=0;
  if(io->power_guard(io->cookie)) {r->code=HGP_GUARD;return 0;}
  const uint32_t a[]={0x3da0020,0x3da0024,0x3da0028,0x3da0048};
  const uint32_t wanted[]={0x4c017e18,0x40000016,0x5511,0};
  for(unsigned i=0;i<4;i++)if(!hgp_read(io,r,a[i],&v,0) || v!=wanted[i])goto bad;
  for(unsigned i=0;i<24;i++) {
    if(!hgp_read(io,r,0x3da0800+4*i,&v,0) || v)goto bad;
    if(!hgp_read(io,r,0x3da0c00+4*i,&v,0) || v!=0x20000)goto bad;
  }
  for(unsigned i=0;i<22;i++)if(!hgp_read(io,r,0x3dc0000+4096*i,&v,0) || (v&1))goto bad;
  return 1;
bad:if(!r->code)r->code=HGP_GUARD;return 0;
}
static inline int hgp_sync(const struct hgp_io *io,struct hgp_result *r) {
  uint64_t v=0;
  if(!hgp_write(io,r,HGP_CB(0x618),0,0) || !hgp_write(io,r,HGP_CB(0x7f0),0xffffffffu,0))return 0;
  for(unsigned i=0;i<100;i++) {
    if(!hgp_read(io,r,HGP_CB(0x7f4),&v,0))return 0;
    if(!(v&1))return 1;
    r->polls++;
    if(i<99 && io->delay(io->cookie,10)) {if(!r->code)r->code=HGP_IO;return 0;}
  }
  if(!r->code)r->code=HGP_TIMEOUT;return 0;
}
/* An optional owned visitor must return with exactly the configured disabled
 * image and every stream invalid/FAULT. Any visitor failure retains all
 * resources immediately; no speculative register cleanup is attempted. */
typedef unsigned (*hgp_owned_visitor)(void *,const struct hgp_io *,
  const struct hgt_owned *,const uint64_t *);
static inline unsigned hgp_disabled_context_visit(const struct hgp_io *io,
    const struct hgt_owned *owned,struct hgp_result *r,hgp_owned_visitor visitor,void *state) {
  uint64_t v=0;ArmSmmu4KContextImage image;
  if(!r)return HGP_ARGUMENT;*r=(struct hgp_result){0};r->safe_to_release=1;
  if(!io || !io->read32 || !io->read64 || !io->write32 || !io->write64 ||
     !io->delay || !io->power_guard || !owned || !owned->tables || !owned->data ||
     owned->built.table_pages!=3 || owned->built.leaf_pages!=1 ||
     owned->built.root_physical!=owned->table_physical)return r->code=HGP_ARGUMENT;
  ArmSmmu4KContextConfig cfg={&owned->config,0,&owned->built,0,0x4c017e18,0x5511,0x750,0,0,0};
  if(ArmSmmu4KEncodeContext(&cfg,&image))return r->code=HGP_ARGUMENT;
  if(!hgp_guard(io,r))return r->code;
  if(!hgp_read(io,r,HGP_CB(0x58),&v,0))return r->code;
  r->fsr=(uint32_t)v;if(v&0xc00001feu)return r->code=HGP_GUARD;
  for(unsigned i=0;i<9;i++) {
    if(!hgp_read(io,r,hgp_field_address(i),&r->saved[i],i==5 || i==6))return r->code;
    r->backed_up++;
  }
  if(r->saved[0]&0x41)return r->code=HGP_GUARD;
  if(io->checkpoint && io->checkpoint(io->cookie,1))return r->code=HGP_IO;
  if(!hgp_guard(io,r))return r->code;
  // Filesystem events must not have transferred bank ownership.
  for(unsigned i=0;i<9;i++) {
    if(!hgp_read(io,r,hgp_field_address(i),&v,i==5 || i==6))return r->code;
    if(v!=r->saved[i])return r->code=HGP_GUARD;
  }
  const uint64_t expected[]={0,image.cba2r,image.cbar,image.tcr2,image.tcr,
    image.ttbr0,image.ttbr1,image.mair0,image.mair1};
  r->safe_to_release=0;
  for(unsigned i=0;i<9;i++) {
    r->attempted|=1u<<i;
    if(!hgp_write(io,r,hgp_field_address(i),expected[i],i==5 || i==6))goto cleanup;
  }
  if(!hgp_sync(io,r))goto cleanup;
  for(unsigned i=0;i<9;i++)
    if(!hgp_read(io,r,hgp_field_address(i),&r->configured[i],i==5 || i==6))goto cleanup;
  ArmSmmu4KContextImage observed={
    (uint32_t)r->configured[2],(uint32_t)r->configured[1],(uint32_t)r->configured[4],
    (uint32_t)r->configured[3],(uint32_t)r->configured[7],(uint32_t)r->configured[8],
    image.sctlr,0,r->configured[5],r->configured[6]};
  // Exactly the library's pre-enable check: verify raw SCTLR=0 separately.
  // Supplying encoded SCTLR here does NOT qualify an enabled context.
  if(r->configured[0] || ArmSmmu4KValidateContextReadback(&cfg,&observed))r->code=HGP_VERIFY;
  if(!hgp_read(io,r,HGP_CB(0x58),&v,0))goto cleanup;
  r->fsr=(uint32_t)v;if(v&0xc00001feu)r->code=HGP_GUARD;
  if(r->code)goto cleanup;
  if(io->checkpoint && io->checkpoint(io->cookie,2))r->code=HGP_IO;
  if(!r->code && visitor) {
    unsigned code=visitor(state,io,owned,r->configured);
    if(code)return r->code=code;
  }
  if(!hgp_guard(io,r))return r->code;
  for(unsigned i=0;i<9;i++) {
    if(!hgp_read(io,r,hgp_field_address(i),&v,i==5 || i==6))return r->code;
    if(v!=r->configured[i])return r->code=HGP_GUARD;
  }
cleanup:
  // Refuse cleanup under changed power/master/stream ownership.
  if(!hgp_guard(io,r))return r->code;
  if(!hgp_write(io,r,HGP_CB(0),0,0) || !hgp_read(io,r,HGP_CB(0),&v,0) || v ||
     !hgp_sync(io,r))return r->code?r->code:(r->code=HGP_RESTORE);
  r->retired=1;
  // r73 faulted when replaying an uninitialized CBAR. Like the same-chip
  // reference, leave only known valid disabled fields, with NO table roots.
  // Preserve the captured initial image for evidence, not for hardware writes.
  for(unsigned n=1;n<9;n++) {
    uint64_t wanted=(n==5 || n==6)?0:expected[n];
    if(hgp_write(io,r,hgp_field_address(n),wanted,n==5 || n==6) &&
       hgp_read(io,r,hgp_field_address(n),&v,n==5 || n==6) &&
       (n==3?((v^wanted)&~UINT64_C(0x60))==0:v==wanted))r->retired|=1u<<n;
  }
  if(r->retired!=0x1ff || !hgp_sync(io,r) || !hgp_guard(io,r))return r->code=HGP_RESTORE;
  if(io->checkpoint && io->checkpoint(io->cookie,3))return r->code=HGP_IO;
  if(!hgp_guard(io,r))return r->code;
  for(unsigned n=0;n<9;n++) {
    uint64_t wanted=(n==5 || n==6)?0:expected[n];
    if(!hgp_read(io,r,hgp_field_address(n),&v,n==5 || n==6))return r->code;
    if(n==3?((v^wanted)&~UINT64_C(0x60))!=0:v!=wanted)return r->code=HGP_GUARD;
  }
  r->safe_to_release=1;return r->code;
}
static inline unsigned hgp_disabled_context(const struct hgp_io *io,
    const struct hgt_owned *owned,struct hgp_result *r) {
  return hgp_disabled_context_visit(io,owned,r,0,0);
}
#endif
