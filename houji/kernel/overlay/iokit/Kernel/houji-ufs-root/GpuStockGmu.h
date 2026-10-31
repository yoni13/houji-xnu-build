/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * SM8650/gen7-9-1 stock firmware + HFI readiness + one property query.
 * Register/value protocol: OnePlus graphics-kernel 453d8875, gen7_reg.h and
 * adreno_gen7_gmu.c; same-chip Ace5 aaa100d1 native HFI experiment.
 * Optional bounded HWSCHED bootstrap stops at resource requests. NO GPU
 * stream, CP, PM4, PAS/ZAP or secure allocation is available.
 * All resources and changed hardware remain owned until PLATFORM RESET.
 */
#ifndef HOUJI_GPU_STOCK_GMU_H
#define HOUJI_GPU_STOCK_GMU_H
#include "GpuSmmuContext.h"
#include "GpuFirmwareMemory.h"
#include "GpuHfiBootstrap.h"
enum { HGF_OK,HGF_ARGUMENT,HGF_GUARD,HGF_IO,HGF_VERIFY,HGF_TIMEOUT,HGF_FIRMWARE,HGF_HFI };
struct hgf_write {uint32_t address,value,mask;};
static const struct hgf_write hgf_rsc[]={
 {0xb284520,0,0xffffffffu},{0xb284500,0x80000001u,0xffffffffu},
 {0x3d50010,1u<<24,0xffffffffu},{0x3d5002c,1,0xffffffffu},
 // Vendor hidden TCS address/data writes have NO equality readback contract.
 {0x3d50038,0,0},{0x3d50034,0,0},{0x3d50040,0,0},{0x3d5003c,0,0},
 {0x3d50048,0x80000021u,0},{0x3d50044,0,0},
 {0x3d50400,0,0xffffffffu},{0x3d50020,0x4520,0xffffffffu},
 {0x3d50024,0x4510,0xffffffffu},{0x3d50028,0x4514,0xffffffffu},
 {0x3d50550,0xeaaae5a0,0xffffffffu},{0x3d50554,0xe1a1ebab,0xffffffffu},
 {0x3d50558,0xa2e0a581,0xffffffffu},{0x3d5055c,0xecac82e2,0xffffffffu},
 {0x3d50560,0x0020edad,0xffffffffu}};
static const struct hgf_write hgf_boot[]={
 {0x3d7e3c4,1,0xffffffffu},{0x3d7e3c0,1,0xffffffffu},
 {0x3d7e070,0,0xffffffffu},{0x3d7e004,2,0xffffffffu},
 {0x3d7e614,GGH_IOVA,0xffffffffu},{0x3d7e610,1,0xffffffffu},
 {0x3d8ec44,0x80c808a0u,0xffffffffu},{0x3d7e0b4,0x4052,0xffffffffu},
 {0x3d0f114,0,0xffffffffu},{0x3d7e740,GGH_VRB_IOVA,0xffffffffu},
 {0x3d7e738,0,0xffffffffu},{0x3d7e73c,0x07090100,0xffffffffu},
 {0x3d7e734,GGH_LOG_IOVA|15,0xffffffffu},
 {0x3d7d03c,1,0xffffffffu},{0x3d7d000,1,0xffffffffu},{0x3d7d004,1,0xffffffffu},
 {0x3d7e64c,0xffffffffu,0xffffffffu},{0x3d8ec18,0xffffffffu,0x7f},
 {0x3d7e618,0,0xffffffffu}};
struct hgf_io {
 struct hgp_io smmu;
 void *cookie;
 int (*read)(void *,uint32_t,uint32_t *);
 int (*write)(void *,uint32_t,uint32_t);
 int (*barrier)(void *);
 int (*delay)(void *,unsigned);
 int (*prerequisite)(void *);
 int (*publish)(void *);
 int (*checkpoint)(void *,unsigned);
 unsigned (*bootstrap)(void *,struct hhb_result *);
 int (*stopped)(void *); /* Optional snapshot after confirmed reset, before fault MMIO. */
 int (*retain_live)(void *); /* Exactly1 retains this successful same-owner graph. */
};
struct hgf_result {
 unsigned retained_live;
 unsigned code,phase,attempted,retained,reads,writes,polls,reset_held,release_attempted;
 unsigned tcm_backup_words,tcm_loaded_words,rsc_attempted,boot_attempted,stream_attached;
 unsigned fw_ready,hfi_ready,query_sent,query_ack,query_value,query_status,reply_count,debug_count,diagnostic_mask;
 uint32_t last_address,last_value,last_operation,last_observed_valid;
 uint32_t failed_phase,failed_address,failed_value,failed_operation,failed_observed_valid,fw_status,hfi_status,fsr,fsynr,gfsr,irq,ao_irq;
 uint32_t counter_before,counter_after,query_header,reply[255],tcm_before[8192];
 uint64_t far;
 struct hgp_result context;
 struct hhb_result bootstrap;
};
static inline int hgf_read(const struct hgf_io *io,struct hgf_result *r,uint32_t a,uint32_t *v) {
 r->last_address=a;r->last_operation=1;r->last_observed_valid=0;r->reads++;
 if(io->read(io->cookie,a,v))return 0;r->last_value=*v;r->last_observed_valid=1;return 1;
}
static inline int hgf_write(const struct hgf_io *io,struct hgf_result *r,uint32_t a,uint32_t v,uint32_t mask) {
 uint32_t observed=0;r->last_address=a;r->last_value=v;r->last_operation=2;r->last_observed_valid=0;r->writes++;
 if(io->write(io->cookie,a,v) || io->barrier(io->cookie))return 0;
 return !mask || (hgf_read(io,r,a,&observed) && !((observed^v)&mask));
}
static inline int hgf_power(const struct hgf_io *io,struct hgf_result *r) {
 uint32_t value;
 // Observe only the proven CX access/clock state while CM3 runs. The
 // stopped-master precondition is deliberately NOT called in this phase.
 const uint32_t a[]={0x171004,0x171010,0x171018,0x3d97000,0x3d99108,0x3d99168,0x3d9911c};
 const uint32_t masks[]={0x80000003,0x80000003,0x80000001,0x80000001,1,0x80000000,0x80000000};
 const uint32_t wanted[]={1,1,1,1,0,0x80000000,0};
 for(unsigned i=0;i<7;i++)if(!hgf_read(io,r,a[i],&value) || (value&masks[i])!=wanted[i])return 0;
 return 1;
}
static inline int hgf_health(const struct hgf_io *io,struct hgf_result *r) {
 // Gated registers must never be touched after their clock proof disappears.
 return hgf_power(io,r) && hgf_read(io,r,0x3da0048,&r->gfsr) && !r->gfsr &&
   hgf_read(io,r,HGP_CB(0x58),&r->fsr) && !(r->fsr&0xc00001feu);
}
static inline int hgf_live_context(const struct hgf_io *io,struct hgf_result *r,const uint64_t *fields,uint64_t zero) {
 uint64_t value;uint32_t v;
 if(!hgf_health(io,r))return 0;
 for(unsigned i=0;i<24;i++) {
  if(!hgf_read(io,r,0x3da0800+4*i,&v) || v!=(i?0:0x80000005u) ||
    !hgf_read(io,r,0x3da0c00+4*i,&v) || v!=(i?0x20000:HGP_CONTEXT_BANK))return 0;
 }
 for(unsigned i=0;i<22;i++) {
  if(!hgf_read(io,r,0x3dc0000+4096*i,&v))return 0;
  if(i==HGP_CONTEXT_BANK){if((v&~0x1000u)!=0x27)return 0;}else if(v&1)return 0;
 }
 for(unsigned i=1;i<9;i++)if(!hgp_read(&io->smmu,&r->context,hgp_field_address(i),&value,i==5 || i==6) ||
   (i==3?((value^fields[i])&~UINT64_C(0x60))!=0:value!=fields[i]))return 0;
 return hgf_read(io,r,HGP_CB(4),&v) && v==0x32b &&
   hgf_read(io,r,0x3da6008,&v) && v==(uint32_t)zero &&
   hgf_read(io,r,0x3da600c,&v) && v==(uint32_t)(zero>>32);
}
static inline int hgf_poll(const struct hgf_io *io,struct hgf_result *r,uint32_t address,uint32_t mask,uint32_t expected,uint32_t *last) {
 for(unsigned i=0;i<10000;i++) {
  if(!hgf_health(io,r) || !hgf_read(io,r,address,last))return HGF_IO;
  if((*last&mask)==expected)return HGF_OK;
  r->polls++;if(io->delay(io->cookie,10))return HGF_IO;
 }
 return HGF_TIMEOUT;
}
struct hgf_queue_cookie {const struct hgf_io *io;unsigned error;};
static inline void hgf_hfi_barrier(void *cookie) {
 struct hgf_queue_cookie *q=cookie;if(q->io->barrier(q->io->cookie))q->error=1;
}
static inline unsigned hgf_run(const struct hgf_io *io,const struct hfm_result *memory,
 const uint8_t *firmware,size_t firmware_bytes,uint64_t zero,struct hgf_result *r) {
 struct gg_fw_block blocks[GG_FW_MAX_BLOCKS];size_t count=0;ArmSmmu4KContextImage encoded;
 uint64_t raw[9],v=0;uint32_t value=0;
 if(!r)return HGF_ARGUMENT;*r=(struct hgf_result){0};r->retained=1;
 if(!io || !io->read || !io->write || !io->barrier || !io->delay || !io->prerequisite ||
   !io->publish || !io->checkpoint || !io->smmu.read32 || !io->smmu.read64 || !io->smmu.write32 ||
   !io->smmu.write64 || !io->smmu.delay || !memory || !firmware || firmware_bytes!=HFM_FW_BYTES || !zero || (zero&4095) ||
   zero>=(UINT64_C(1)<<36) || memory->code || !memory->audit.complete ||
   memory->count!=HFM_MAX || memory->retained!=HFM_ALL || memory->released ||
   memory->validated!=HFM_ALL || !memory->published || !memory->hfi_initialized)return r->code=HGF_ARGUMENT;
#define HGF_REQUIRE(test,error) do {if(!(test)){r->code=(error);goto stopped;}} while(0)
 r->phase=1;
 HGF_REQUIRE(gg_fw_plan(firmware,firmware_bytes,blocks,GG_FW_MAX_BLOCKS,&count)==GG_FW_OK && count==15,HGF_FIRMWARE);
 ArmSmmu4KContextConfig config={&memory->config,0,&memory->built,0,0x4c017e18,0x5511,0x750,0,0,0};
 HGF_REQUIRE(ArmSmmu4KEncodeContext(&config,&encoded)==ArmSmmu4KSuccess,HGF_ARGUMENT);
 const uint64_t fields[]={0,encoded.cba2r,encoded.cbar,encoded.tcr2,encoded.tcr,encoded.ttbr0,encoded.ttbr1,encoded.mair0,encoded.mair1};
 for(unsigned i=0;i<9;i++)raw[i]=fields[i];
 HGF_REQUIRE(io->prerequisite(io->cookie)==1,HGF_GUARD);r->reset_held=1;
 HGF_REQUIRE(hgf_read(io,r,0x3d50404,&value) && value==0 &&
   hgf_read(io,r,0x3d7e600,&value) && !value &&
   hgf_read(io,r,HGP_CB(4),&value) && value==3 &&
   hgf_read(io,r,0x3da6008,&value) && !value &&
   hgf_read(io,r,0x3da600c,&value) && !value,HGF_GUARD);
 HGF_REQUIRE(!io->publish(io->cookie),HGF_VERIFY);
 HGF_REQUIRE(!io->checkpoint(io->cookie,1) && io->prerequisite(io->cookie)==1,HGF_GUARD);
 r->phase=2;
 for(unsigned i=0;i<8192;i++) {
  HGF_REQUIRE(hgf_read(io,r,0x3d6d000+4*i,&r->tcm_before[i]),HGF_IO);r->tcm_backup_words++;
 }
 r->phase=3;r->attempted=1;
 for(unsigned i=0;i<sizeof(hgf_rsc)/sizeof(hgf_rsc[0]);i++) {
  const struct hgf_write *w=&hgf_rsc[i];r->rsc_attempted++;
  HGF_REQUIRE(hgf_write(io,r,w->address,w->value,w->mask),HGF_VERIFY);
 }
 r->phase=4;
 for(size_t i=0;i<count;i++) {
  const struct gg_fw_block *b=&blocks[i];
  if(!b->size || (b->region!=GG_ITCM && b->region!=GG_DTCM))continue;
  uint32_t a=b->region==GG_ITCM?0x3d6d000+b->address:0x3d71000+b->address-0x10004000;
  for(unsigned n=0;n<b->size;n+=4) {
   HGF_REQUIRE(hgf_write(io,r,a+n,gg_le32(firmware+b->payload_offset+n),0xffffffffu),HGF_VERIFY);r->tcm_loaded_words++;
  }
 }
 HGF_REQUIRE(r->tcm_loaded_words==135 && hgf_read(io,r,0x3d74ff0,&value) && value==0x20060001,HGF_FIRMWARE);
 for(unsigned i=0;i<sizeof(hgf_boot)/sizeof(hgf_boot[0]);i++) {
  const struct hgf_write *w=&hgf_boot[i];r->boot_attempted++;
  HGF_REQUIRE(hgf_write(io,r,w->address,w->value,w->mask),HGF_VERIFY);
 }
 r->phase=5;
 HGF_REQUIRE(hgf_read(io,r,0x3d7e200,&r->counter_before),HGF_IO);
 for(unsigned i=0;i<64;i++) {HGF_REQUIRE(hgf_read(io,r,0x3d7e200,&r->counter_after),HGF_IO);if(r->counter_after!=r->counter_before)break;}
 HGF_REQUIRE(r->counter_before!=r->counter_after,HGF_GUARD);
 HGF_REQUIRE(io->prerequisite(io->cookie)==1 && !io->publish(io->cookie),HGF_GUARD);
 for(unsigned i=0;i<9;i++)HGF_REQUIRE(hgp_write(&io->smmu,&r->context,hgp_field_address(i),raw[i],i==5 || i==6),HGF_IO);
 HGF_REQUIRE(hgf_write(io,r,0x3da6008,(uint32_t)zero,0xffffffffu) &&
   hgf_write(io,r,0x3da600c,(uint32_t)(zero>>32),0xffffffffu) &&
   hgf_write(io,r,HGP_CB(4),0x32b,0xffffffffu) && hgp_sync(&io->smmu,&r->context),HGF_IO);
 for(unsigned i=0;i<9;i++)HGF_REQUIRE(hgp_read(&io->smmu,&r->context,hgp_field_address(i),&v,i==5 || i==6) &&
   (i==3?((v^raw[i])&~UINT64_C(0x60))==0:v==raw[i]),HGF_VERIFY);
 r->phase=6;
 HGF_REQUIRE(hgf_write(io,r,HGP_CB(0),0x1027,~0x1000u) &&
   hgf_write(io,r,0x3da0c00,HGP_CONTEXT_BANK,0xffffffffu) &&
   hgf_write(io,r,0x3da0800,0x80000005u,0xffffffffu),HGF_VERIFY);r->stream_attached=1;
 HGF_REQUIRE(hgf_live_context(io,r,raw,zero) && !io->checkpoint(io->cookie,2) &&
   hgf_live_context(io,r,raw,zero) && hgf_read(io,r,0x3d7e000,&value) && value==1,HGF_GUARD);
 r->phase=7;r->release_attempted=1;r->reset_held=0;
 HGF_REQUIRE(hgf_write(io,r,0x3d7e000,0,0xffffffffu),HGF_IO);
 r->code=hgf_poll(io,r,0x3d7e070,0x1ff,0x100,&r->fw_status);if(r->code)goto stopped;r->fw_ready=1;
 r->phase=8;
 HGF_REQUIRE(hgf_write(io,r,0x3d7e618,1,0xffffffffu),HGF_IO);
 r->code=hgf_poll(io,r,0x3d7e600,1,1,&r->hfi_status);if(r->code)goto stopped;r->hfi_ready=1;
 r->phase=9;
 struct hgf_queue_cookie queue_cookie={io,0};
 struct ggh_io queues={(volatile uint32_t *)memory->memory[5].cpu,GGH_BYTES,GGH_IOVA,&queue_cookie,hgf_hfi_barrier};
 r->query_header=(1u<<20)|(3u<<8)|12;const uint32_t request[]={r->query_header,125,0};
 HGF_REQUIRE(ggh_send(&queues,request,3)==GGH_OK && !queue_cookie.error &&
   hgf_write(io,r,0x3d7e650,1,0),HGF_HFI);r->query_sent=1;
 for(unsigned poll=0;poll<10000 && !r->query_ack;poll++) {
  HGF_REQUIRE(hgf_health(io,r),HGF_GUARD);
  for(unsigned q=1;q<=2;q++) {
   unsigned n=0;int status=ggh_receive(&queues,q,r->reply,255,&n);
   HGF_REQUIRE(!queue_cookie.error && (status==GGH_OK || status==GGH_EMPTY),HGF_HFI);
   if(status==GGH_EMPTY)continue;
   r->reply_count=n;
   if(q==2){HGF_REQUIRE(++r->debug_count<=32,HGF_HFI);continue;}
   HGF_REQUIRE(n>=4 && ((r->reply[0]>>16)&15)==1 && r->reply[1]==r->query_header,HGF_HFI);
   r->query_value=r->reply[2];r->query_status=r->reply[3];r->query_ack=1;break;
  }
  if(!r->query_ack){r->polls++;HGF_REQUIRE(!io->delay(io->cookie,10),HGF_IO);}
 }
 HGF_REQUIRE(r->query_ack && r->query_value==1 && !r->query_status,HGF_HFI);
 r->phase=10;
 if(io->bootstrap)HGF_REQUIRE(!io->bootstrap(io->cookie,&r->bootstrap),HGF_HFI);
stopped:
 if(!r->code && r->phase==10 && io->retain_live && io->retain_live(io->cookie)==1){r->retained_live=1;r->reset_held=0;return 0;}
 if(r->code){r->failed_phase=r->phase;r->failed_address=r->last_address;r->failed_value=r->last_value;
   r->failed_operation=r->last_operation;r->failed_observed_valid=r->last_observed_valid;}
 // Never revoke or free any stream/table/TCM after running stock firmware.
 // Even a successful reset readback is not a proof of drained DMA. The
 // direct platform-reset path retains all allocations and gate references.
 if(r->release_attempted) {
  r->reset_held=0;
  if(!hgf_power(io,r)){if(!r->code)r->code=HGF_GUARD;return r->code;}
  if(!hgf_write(io,r,0x3d7e000,1,0) && !r->code)r->code=HGF_IO;
  for(unsigned i=0;i<1000;i++) {
   if(!hgf_read(io,r,0x3d7e000,&value)){if(!r->code)r->code=HGF_IO;break;}
   if(value==1){r->reset_held=1;break;}
   if(io->delay(io->cookie,10)){if(!r->code)r->code=HGF_IO;break;}
  }
  if(!r->reset_held && !r->code)r->code=HGF_TIMEOUT;
  if(r->code && !r->failed_phase){r->failed_phase=11;r->failed_address=r->last_address;r->failed_value=r->last_value;
   r->failed_operation=r->last_operation;r->failed_observed_valid=r->last_observed_valid;}
  if(r->reset_held && io->stopped && io->stopped(io->cookie) && !r->code)r->code=HGF_IO;
 }
 if(r->attempted) {
  if(!hgf_power(io,r)){r->reset_held=0;if(!r->code)r->code=HGF_GUARD;return r->code;}
  if(hgf_read(io,r,0x3da0048,&r->gfsr))r->diagnostic_mask|=1;
  if(hgf_read(io,r,HGP_CB(0x58),&r->fsr))r->diagnostic_mask|=2;
  if(hgf_read(io,r,HGP_CB(0x68),&r->fsynr))r->diagnostic_mask|=4;
  if(hgp_read(&io->smmu,&r->context,HGP_CB(0x60),&r->far,1))r->diagnostic_mask|=8;
  if(hgf_read(io,r,0x3d7e648,&r->irq))r->diagnostic_mask|=16;
  if(hgf_read(io,r,0x3d8ec14,&r->ao_irq))r->diagnostic_mask|=32;
  if(r->diagnostic_mask!=63 && !r->code){r->code=HGF_IO;r->failed_phase=12;}
 }
 return r->code;
#undef HGF_REQUIRE
}
#endif
