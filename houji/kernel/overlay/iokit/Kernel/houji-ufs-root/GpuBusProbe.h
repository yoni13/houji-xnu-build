/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * SM8650 held-reset bus observation. The only writable fields are GCC CFG /
 * MEMNOC HWCG bit 1 and the GPU SMMU HLOS vote bit 0. No RCG/PLL, CM3, TCM,
 * stream/context, GPU command or firmware writes are allowed here.
 * MiCode shennong-u-oss gcc-pineapple/gpucc-pineapple define these fields;
 * Ace 5 aaa100d1 documents the same temporary gate sequence on SM8650.
 */
#ifndef HOUJI_GPU_BUS_PROBE_H
#define HOUJI_GPU_BUS_PROBE_H
#include "GpuPowerPolicy.h"
enum { HGB_CFG, HGB_MEM, HGB_DVM, HGB_CX, HGB_HW, HGB_AHB, HGB_RESET,
       HGB_VOTE, HGB_ID0, HGB_ID1, HGB_ID2, HGB_CR0, HGB_GFSR,
       HGB_FWINIT, HGB_GXPOWER, HGB_REGS };
enum { HGB_OK, HGB_OWNER, HGB_READ, HGB_GUARD, HGB_WRITE, HGB_TIMEOUT,
       HGB_RESTORE, HGB_GEOMETRY, HGB_BUSY, HGB_ACTION, HGB_RETAIN };
struct hgb_result;
struct hgb_io {
  void *cookie;
  GPP_U64 (*read)(void *, GPP_U32, GPP_U32 *);
  GPP_U64 (*write)(void *, GPP_U32, GPP_U32);
  GPP_U64 (*delay)(void *, unsigned);
  // Optional action under this exact live bus window, before gate cleanup.
  // HGB_RETAIN means no gate may be revoked because reset is not confirmed.
  unsigned (*held_reset_action)(void *, const struct hgb_result *);
};
struct hgb_result {
  unsigned code, failed_reg, reads, polls, attempted, restored;
  GPP_U32 initial[8], values[HGB_REGS], valid;
  unsigned streams_seen,banks_seen,active_streams,nonfault_routes,active_banks;
  GPP_U32 smr[24],s2cr[24],sctlr[22];
};
static inline GPP_U32 hgb_address(unsigned reg) {
  static const GPP_U32 addresses[]={0x171004,0x171010,0x171018,
    0x3d99108,0x3d99168,0x3d9911c,0x3d7e000,0x3d97000,
    0x3da0020,0x3da0024,0x3da0028,0x3da0000,0x3da0048,
    0x3d7e070,0x3d7e340};
  return reg<HGB_REGS?addresses[reg]:0;
}
static inline int hgb_survey_address(GPP_U32 a) {
  return (!(a&3) && ((a>=0x3da0800 && a<0x3da0860) ||
          (a>=0x3da0c00 && a<0x3da0c60))) ||
    (a>=0x3dc0000 && a<=0x3dd5000 && !(a&0xfff));
}
static inline int hgb_survey_read(const struct hgb_io *io,struct hgb_result *r,
                                 GPP_U32 address,GPP_U32 *out) {
  r->failed_reg=address;r->reads++;
  if(!hgb_survey_address(address) || io->read(io->cookie,address,out) ||
     *out==0xffffffffu) {
    if(!r->code)r->code=HGB_READ;return 0;
  }
  return 1;
}
static inline int hgb_owned(const struct gpp_report *p) {
  return gpp_report_valid(p) && p->code==GPP_ERR_INITIAL &&
    p->phase==GPP_PHASE_CLOCKS && p->resource==GPP_GMU_CLK &&
    p->attempted==0x1002 && p->confirmed==0x1002 &&
    (p->flags&GPP_ACD_CONSUMED) &&
    !(p->flags&(GPP_QMP_UNCERTAIN|GPP_PROTOCOL_UNCERTAIN));
}
static inline int hgb_read(const struct hgb_io *io, struct hgb_result *r,unsigned reg) {
  GPP_U32 value=0;
  r->failed_reg=reg;r->reads++;
  if(io->read(io->cookie,hgb_address(reg),&value) || value==0xffffffffu) {
    if(!r->code)r->code=HGB_READ;
    return 0;
  }
  r->values[reg]=value;r->valid|=1u<<reg;return 1;
}
static inline int hgb_poll(const struct hgb_io *io,struct hgb_result *r,
                          unsigned reg,GPP_U32 mask,GPP_U32 expected) {
  unsigned i;
  for(i=0;i<1000;i++) {
    if(!hgb_read(io,r,reg))return 0;
    if((r->values[reg]&mask)==expected)return 1;
    r->polls++;
    if(i<999 && io->delay(io->cookie,1)) {
      if(!r->code)r->code=HGB_READ;return 0;
    }
  }
  if(!r->code)r->code=HGB_TIMEOUT;
  return 0;
}
static inline int hgb_masked_write(const struct hgb_io *io,struct hgb_result *r,
                                 unsigned reg,GPP_U32 mask,GPP_U32 expected) {
  // Reject an accidental new register or multi-bit write in the policy itself.
  if(!((reg==HGB_CFG || reg==HGB_MEM)?mask==2:reg==HGB_VOTE && mask==1) ||
     (expected&~mask) || !hgb_read(io,r,reg))return 0;
  if(io->write(io->cookie,hgb_address(reg),(r->values[reg]&~mask)|expected)) {
    if(!r->code)r->code=HGB_WRITE;return 0;
  }
  return hgb_poll(io,r,reg,mask,expected);
}
static inline unsigned hgb_probe(const struct hgb_io *io,const struct gpp_report *p,
                                struct hgb_result *r) {
  unsigned i;
  static const unsigned change[]={HGB_CFG,HGB_MEM,HGB_VOTE};
  static const GPP_U32 mask[]={2,2,1}, want[]={0,0,1};
  if(!r)return HGB_OWNER;
  for(i=0;i<sizeof(*r);i++)((unsigned char *)r)[i]=0;
  if(!hgb_owned(p) || !io || !io->read || !io->write || !io->delay)
    return r->code=HGB_OWNER;
  // Access ordering is significant: prove CFG before GPUCC, CX before CM3.
  if(!hgb_read(io,r,HGB_CFG) || !(r->values[HGB_CFG]&1) ||
     !hgb_read(io,r,HGB_MEM) || !(r->values[HGB_MEM]&1) ||
     !hgb_read(io,r,HGB_DVM) || (r->values[HGB_DVM]&0x80000001u)!=1 ||
     !hgb_read(io,r,HGB_CX) || (r->values[HGB_CX]&1) ||
     !hgb_read(io,r,HGB_HW) || !(r->values[HGB_HW]&0x80000000u) ||
     !hgb_read(io,r,HGB_AHB) || (r->values[HGB_AHB]&0x80000000u) ||
     !hgb_read(io,r,HGB_RESET) || r->values[HGB_RESET]!=1 ||
     !hgb_read(io,r,HGB_VOTE) || (r->values[HGB_VOTE]&1)) {
    if(!r->code)r->code=HGB_GUARD;return r->code;
  }
  for(i=0;i<8;i++)r->initial[i]=r->values[i];
  for(i=0;i<3;i++) {
    r->attempted|=1u<<i;
    if(!hgb_masked_write(io,r,change[i],mask[i],want[i]) ||
       !hgb_poll(io,r,change[i],0x80000001u,1))goto restore;
  }
  // The gate transaction must not have changed CM3 reset or CX state.
  if(!hgb_read(io,r,HGB_CX) || (r->values[HGB_CX]&1) ||
     !hgb_read(io,r,HGB_HW) || !(r->values[HGB_HW]&0x80000000u) ||
     !hgb_read(io,r,HGB_RESET) || r->values[HGB_RESET]!=1) {
    if(!r->code)r->code=HGB_GUARD;goto restore;
  }
  for(i=HGB_ID0;i<HGB_REGS;i++)if(!hgb_read(io,r,i))goto restore;
  // Decode only the exact hardware geometry observed on this phone in r65.
  // These are observations, never permission to reuse a live context bank.
  if(r->values[HGB_ID0]!=0x4c017e18 || r->values[HGB_ID1]!=0x40000016 ||
     r->values[HGB_ID2]!=0x5511) {r->code=HGB_GEOMETRY;goto restore;}
  for(i=0;i<24;i++) {
    if(!hgb_survey_read(io,r,0x3da0800+4*i,&r->smr[i]) ||
       !hgb_survey_read(io,r,0x3da0c00+4*i,&r->s2cr[i]))goto restore;
    r->streams_seen++;
    if(r->smr[i]&0x80000000u)r->active_streams++;
    if((r->s2cr[i]&0x30000)!=0x20000)r->nonfault_routes++;
  }
  for(i=0;i<22;i++) {
    if(!hgb_survey_read(io,r,0x3dc0000+0x1000*i,&r->sctlr[i]))goto restore;
    r->banks_seen++;if(r->sctlr[i]&1)r->active_banks++;
  }
  if(r->active_streams || r->nonfault_routes || r->active_banks ||
     r->values[HGB_FWINIT])r->code=HGB_BUSY;
  if(!r->code && io->held_reset_action) {
    unsigned action=io->held_reset_action(io->cookie,r);
    if(action && action!=HGB_ACTION)return r->code=HGB_RETAIN;
    if(action)r->code=HGB_ACTION;
  }
restore:
  // Restore only the three fields we attempted, preserving all other bits.
  // No DMA or CM3 was started, so this does not revoke live work created here.
  for(i=3;i>0;i--) {
    unsigned n=i-1,reg=change[n];
    if(!(r->attempted&(1u<<n)))continue;
    if(hgb_masked_write(io,r,reg,mask[n],r->initial[reg]&mask[n]))r->restored|=1u<<n;
  }
  if(r->restored!=r->attempted)r->code=HGB_RESTORE;
  return r->code;
}
#endif
