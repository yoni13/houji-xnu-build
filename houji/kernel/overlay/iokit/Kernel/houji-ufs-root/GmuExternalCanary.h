/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * CANDIDATE. XNU-owned bank2/ASID750/SID5 external-memory diagnostic.
 * Payload: houji_gmu_dma_probe.S, binary SHA256
 * b3a876b2a2a6aed251b824ba412fdf9e1d1f4c340a8093868e63c3bba721e076.
 * Register protocol: GpuDmaProbe.h / OnePlus graphics-kernel 453d8875.
 * Original UEFI hga runs BEFORE secure-CX initialization and performs no
 * HYP_ASSIGN/VMID assignment. CPU PA visibility is NOT a device-visibility
 * proof: only the positive writeback plus exact unmapped negative read can
 * qualify it here. Every PA/descriptor remains 64-bit with a 36-bit bound.
 * No stock firmware, HFI, GPU stream, CP, arbitrary IOVA or old hgb proof.
 * A reset readback alone NEVER grants CPU access or resource retirement.
 */
#ifndef HOUJI_GMU_EXTERNAL_CANARY_H
#define HOUJI_GMU_EXTERNAL_CANARY_H
#include "GmuInternalExec.h"
#include "ArmSmmu4KAudit.h"
#include "ArmSmmu4KContext.h"
#include "GpuSmmuProfile.h"
/* Native-only data types; do not include UEFI C allocation/control flows. */
struct gec_owned {
 void *tables,*data;GPP_U64 table_physical,data_physical;
 ArmSmmu4KConfig config;ArmSmmu4KResult built;ArmSmmu4KRange range;
};
struct gec_tables {const volatile uint64_t *cpu;uint64_t physical;};
static inline int gec_read_descriptor(void *cookie,uint64_t physical,uint64_t *value){
 const struct gec_tables *t=(const struct gec_tables *)cookie;
 if(!t || !value || (physical&7) || physical<t->physical || physical-t->physical>=12288)return 1;
 *value=t->cpu[(physical-t->physical)/8];return 0;
}
static inline GPP_U32 gec_field_address(unsigned i){
 const GPP_U32 a[]={HGP_CB(0),HGP_CBA2R,HGP_CBAR,HGP_CB(0x10),HGP_CB(0x30),HGP_CB(0x20),HGP_CB(0x28),HGP_CB(0x38),HGP_CB(0x3c)};
 return i<9?a[i]:0;
}
static inline int gec_field_index(GPP_U32 a){for(unsigned i=0;i<9;i++)if(gec_field_address(i)==a)return (int)i;return -1;}
#include "GpuDmaCode.h"
enum {GEC_CODE_WORDS=44,GEC_MAIL_WORDS=8,GEC_STACK_WORDS=16,GEC_PROGRAM_WORDS=52,
 GEC_TOTAL_WORDS=68,GEC_CONTROL_WORDS=9,GEC_CONTEXT_WORDS=9,GEC_POLLS=1000,
 GEC_MAX_EVENTS=256,GEC_MAX_LOADS=3072};
enum {GEC_OK,GEC_ARGUMENT,GEC_REUSED,GEC_INPUT,GEC_OWNER,GEC_FRESH,GEC_RECHECK,
 GEC_CXO,GEC_READ,GEC_WRITE,GEC_BARRIER,GEC_VERIFY,GEC_COMMIT,GEC_BACKUP,
 GEC_TIMEOUT,GEC_FAULT,GEC_DELAY,GEC_MEMORY,GEC_PUBLISH,GEC_TABLE,GEC_CPU};
enum {GEC_PHASE_INPUT=1,GEC_PHASE_BACKUP,GEC_PHASE_BACKUP_COMMIT,GEC_PHASE_CONTEXT,
 GEC_PHASE_PROGRAM,GEC_PHASE_CONTROL,GEC_PHASE_PLATFORM,GEC_PHASE_PREPARED_COMMIT,
 GEC_PHASE_ENABLE,GEC_PHASE_ROUTE,GEC_PHASE_RELEASE,GEC_PHASE_POLL,GEC_PHASE_RESET,
 GEC_PHASE_RESET_POLL,GEC_PHASE_CAPTURE,GEC_PHASE_DETACH,GEC_PHASE_CLEAR_FAULT,
 GEC_PHASE_PLATFORM_RETIRE,GEC_PHASE_CONTEXT_RETIRE,GEC_PHASE_RESTORE_TCM,
 GEC_PHASE_RESTORE_CONTROL,GEC_PHASE_FINAL_VERIFY,GEC_PHASE_CPU_FINISH,
 GEC_PHASE_FINAL_COMMIT,GEC_PHASE_DONE};
enum {GEC_OP_READ=1,GEC_OP_WRITE,GEC_OP_BARRIER,GEC_OP_FRESH,GEC_OP_RECHECK,
 GEC_OP_COMMIT,GEC_OP_OWNER,GEC_OP_VERIFY,GEC_OP_DELAY,GEC_OP_MEMORY};
struct gec_access {unsigned attempted,returned,valid;GPP_U64 value,status;};
struct gec_event {unsigned phase,wide;GPP_U32 address;GPP_U64 wanted,mask;
 struct gec_access store,barrier,verify;};
struct gec_load {unsigned phase,wide;GPP_U32 address;struct gec_access access;};
struct gec_failure {unsigned code,phase,operation,index;GPP_U32 address;GPP_U64 expected,observed,status;};
enum gec_memory_profile {GEC_MEMORY_WRITEBACK=0,GEC_MEMORY_NORMAL_NC=1};
struct gec_memory {
 struct gec_owned owned;GPP_U64 zero_physical;void *zero_cpu;
 unsigned cpu_page_bytes;size_t table_owned_bytes,data_owned_bytes,zero_owned_bytes;
 /* Explicit device-data memory profile. Zero-initialized callers retain the
  * original WB contract. Tables remain WB in both profiles; only the data
  * leaf changes to NC. PRR zero is a physical page, not an added IOVA/leaf.
  * The platform must qualify exclusive CPU backing and descriptor attributes:
  * NC data/zero are accessed only through their NC views, never a WB alias. */
 unsigned memory_profile;
};
/* Actual raw observation, never an edited cold snapshot. All fields are valid
 * only when fresh returns zero AND valid==0x1fff. context[] uses hgp order. */
struct gec_snapshot {struct hph_cold cold;struct hph_clock clock;
 unsigned valid;GPP_U64 context[9],far;GPP_U32 actlr,prr_lo,prr_hi,fsr,fsynr,cxo;};
struct gec_cpu_result {unsigned invalidated,verified,restored,checked_words,padding_checked;
 GPP_U32 observed;};
struct gec_result {
 unsigned started,phase,code,complete,safe_to_save,retained,qualification_lost,barrier_uncertain;
 unsigned possible_execution,reset_held,context_prepared,context_retired,platform_prepared;
 unsigned enabled,route_prepared,stream_attached,detached,isolated,table_held,tcm_retained;
 unsigned backup_committed,prepared_committed,restore_committed,published,positive,negative;
 unsigned reads,writes,barriers,guards,commits,delays,polls,reset_polls,event_count,load_count;
 unsigned backup_words,control_backup_words,context_backup_words,programmed_words,control_programmed;
 unsigned restored_words,control_restored,final_verified_words,irq_retained,stack_changed_mask;
 unsigned input_recheck,last_fresh_code,last_recheck,guard_reset,allow_fault;
 unsigned memory_check_calls;
 GPP_U32 record_crc,backup_crc,control_crc,last_guard_address,fsr,fsynr;GPP_U64 far;
 GPP_U64 context_image[9];struct gec_failure first;struct gec_snapshot last;
 struct gec_access backup[68],control_backup[9],context_backup[9],mailbox[8],stack_after[16];
 struct gec_access final_verify[68],control_final_verify[9];struct gec_cpu_result cpu;
 struct gec_access memory_access[4],publication,commit_access[3],cpu_finish_access;
 struct gec_event events[GEC_MAX_EVENTS];struct gec_load loads[GEC_MAX_LOADS];
 /* Appended proof: every pre-existing result byte/offset remains unchanged.
  * WB leaves this extension zero. NC never claims data/zero invalidation:
  * table maintenance and ordered NC acquire/restore have separate evidence. */
 unsigned memory_profile,nc_acquired,nc_restore_published,nc_table_invalidated;
};
struct gec_io {
 void *cookie;GPP_U64 (*read32)(void *,GPP_U32,GPP_U32 *);GPP_U64 (*read64)(void *,GPP_U32,GPP_U64 *);
 GPP_U64 (*write32)(void *,GPP_U32,GPP_U32);GPP_U64 (*write64)(void *,GPP_U32,GPP_U64);
 GPP_U64 (*barrier)(void *);GPP_U64 (*delay)(void *,unsigned);
 unsigned (*fresh)(void *,unsigned expected_reset,struct gec_snapshot *,GPP_U32 *);
 GPP_U64 (*commit)(void *,unsigned,const struct gec_result *);int (*exclusive)(void *);
 /* stage0 initial, stage1 pre-release, stage2 fully isolated. No mutation. */
 GPP_U64 (*memory_check)(void *,unsigned,const struct gec_memory *);
 GPP_U64 (*publish)(void *,const struct gec_memory *);
 GPP_U64 (*cpu_finish)(void *,const struct gec_memory *,struct gec_cpu_result *);
};
static inline GPP_U32 gec_address(unsigned i){return i<44?0x3d6d000u+4*i:i<52?0x3d71000u+4*(i-44):i<68?0x3d74f40u+4*(i-52):0;}
static inline GPP_U32 gec_program_value(unsigned i){return i<44?hga_code[i]:0;}
static inline int gec_tcm_index(GPP_U32 a){for(unsigned i=0;i<68;i++)if(gec_address(i)==a)return (int)i;return -1;}
static inline int gec_access_ok(const struct gec_access *a){return a->attempted==1 && a->returned==1 && a->valid==1 && !a->status;}
static inline int gec_image_valid(const struct gec_memory *m,const struct gec_result *r){
 ArmSmmu4KContextConfig cfg={&m->owned.config,0,&m->owned.built,0,0x4c017e18u,0x5511u,0x750,0,0,0};ArmSmmu4KContextImage image;
 if(ArmSmmu4KEncodeContext(&cfg,&image))return 0;
 const GPP_U64 want[]={0,image.cba2r,image.cbar,image.tcr2,image.tcr,image.ttbr0,image.ttbr1,image.mair0,image.mair1};
 for(unsigned i=0;i<9;i++)if(r->context_image[i]!=want[i])return 0;return 1;
}
static inline void gec_fail(struct gec_result *r,unsigned code,unsigned op,unsigned index,GPP_U32 a,GPP_U64 expected,GPP_U64 observed,GPP_U64 status){
 if(!r->code){r->code=code;const struct gec_failure first={code,r->phase,op,index,a,expected,observed,status};r->first=first;}
 r->complete=r->safe_to_save=0;
}
static inline int gec_owned(const struct gec_io *io,struct gec_result *r){
 if(r->qualification_lost)return 0;if(!io->exclusive(io->cookie))return 1;
 r->qualification_lost=1;gec_fail(r,GEC_OWNER,GEC_OP_OWNER,0,0,0,0,0);return 0;
}
static inline int gec_read_allowed(GPP_U32 a,unsigned wide){
 if(wide)return a==HGP_CB(0x20) || a==HGP_CB(0x28) || a==HGP_CB(0x60);
 int field=gec_field_index(a);
 return gec_tcm_index(a)>=0 || gie_control_index(a)>=0 || (field>=0 && field!=5 && field!=6) ||
  a==HGP_CB(4) || a==HGP_CB(0x58) || a==HGP_CB(0x68) || a==HGP_CB(0x7f4) ||
  a==0x3da6008u || a==0x3da600cu || a==0x3da0800u || a==0x3da0c00u || a==0x3d7e000u;
}
static inline int gec_load(const struct gec_io *io,struct gec_result *r,GPP_U32 a,unsigned wide,struct gec_access *out){
 if(!out || !gec_read_allowed(a,wide) || r->load_count>=GEC_MAX_LOADS){gec_fail(r,GEC_ARGUMENT,GEC_OP_READ,0,a,0,0,0);return 0;}
 if(!gec_owned(io,r))return 0;struct gec_load *l=&r->loads[r->load_count++];l->phase=r->phase;l->address=a;l->wide=wide;l->access.attempted=1;++r->reads;
 GPP_U32 v=0;l->access.status=wide?io->read64(io->cookie,a,&l->access.value):io->read32(io->cookie,a,&v);
 if(!wide)l->access.value=v;l->access.returned=1;l->access.valid=l->access.status==0;*out=l->access;
 if(!l->access.valid){gec_fail(r,GEC_READ,GEC_OP_READ,r->load_count-1,a,0,l->access.value,l->access.status);return 0;}return 1;
}
/* RO observer reads access gates FIRST, then the real selected-bank image.
 * Unlike gie_observe_ro it deliberately does not invent/require a cold route.
 * The separate state validator below decides whether these raw facts match
 * the exact lease held by THIS native owner. */
static inline unsigned gec_observe_ro(const struct hph_ro_io *io,unsigned reset,struct gec_snapshot *s,GPP_U32 *failed){
 if(failed)*failed=0;if(!io || !io->read || !io->exclusive || !s || !failed || reset>2)return HPH_ARGUMENT;
 hph_zero(s,sizeof(*s));struct gie_ro_context c={io,0,0,0};
 const GPP_U32 masks[]={0x80000003u,0x80000003u,0x80000001u,1,0x80000000u,0x80000000u,0xffffffffu,0x80000001u};
 const GPP_U32 want[]={1,1,1,0,0x80000000u,0,reset,1};
 for(unsigned i=0;i<HGB_REGS;i++){
  if(gie_ro_read(&c,hgb_address(i),&s->cold.values[i]))goto done;s->cold.valid|=1u<<i;
  if(i<8 && (i==HGB_RESET && reset==2?s->cold.values[i]>1:(s->cold.values[i]&masks[i])!=want[i])){c.code=HPH_COLD;goto done;}
 }
 for(unsigned i=0;i<24;i++)if(gie_ro_read(&c,0x3da0800u+4*i,&s->cold.smr[i]) || gie_ro_read(&c,0x3da0c00u+4*i,&s->cold.s2cr[i]))goto done;
 for(unsigned i=0;i<22;i++)if(gie_ro_read(&c,0x3dc0000u+4096*i,&s->cold.sctlr[i]))goto done;
 if(gie_ro_read(&c,0x3d7e600u,&s->cold.hfi))goto done;s->cold.valid|=HPH_HFI_VALID;
 {c.mode=s->cold.values[HGB_RESET]?0u:1u;struct hcw_result w;hph_zero(&w,sizeof(w));
  const struct hcw_io witness={&c,gie_ro_owned,gie_ro_read,hph_ro_configured};
  unsigned code=hcw_run(&witness,c.mode,&w);hph_clock_copy(&s->clock,&w);
  if(code || !hph_hub_cold(&s->clock)){c.code=c.code?c.code:HPH_CLOCK;goto done;}}
 /* io is intentionally 32-bit; split 64-bit registers in little-endian
  * low/high order, with all clocks already qualified and an exclusive owner. */
 for(unsigned i=0;i<9;i++){
  GPP_U32 lo=0,hi=0,a=gec_field_address(i);c.address=a;
  if(io->exclusive(io->cookie) || io->read(io->cookie,a,&lo)){c.code=HPH_READ;goto done;}
  if(i==5 || i==6){c.address=a+4;if(io->exclusive(io->cookie) || io->read(io->cookie,a+4,&hi)){c.code=HPH_READ;goto done;}}
  s->context[i]=lo|((GPP_U64)hi<<32);s->valid|=1u<<i;
 }
 {const GPP_U32 a[]={HGP_CB(4),0x3da6008u,0x3da600cu,HGP_CB(0x58),HGP_CB(0x60),HGP_CB(0x64),HGP_CB(0x68),0x3d99144u};GPP_U32 v[8]={0};
  for(unsigned i=0;i<8;i++){c.address=a[i];if(io->exclusive(io->cookie) || io->read(io->cookie,a[i],&v[i])){c.code=HPH_READ;goto done;}
   /* ARM IHI0062B 15.5.8/15.5.10: FAR/FSYNR reset contents are unpredictable.
    * Retain their raw values, but do not classify all-ones as a bus failure in
    * the exact untouched cold state with FSR=0. No active context or reported
    * fault gets this exception; actual read/owner failures remain fatal. */
   int cold_unreported=i>=4 && i<=6 && reset==1 && s->cold.values[HGB_RESET]==1 &&
     s->context[0]==s->cold.sctlr[2] && !(s->context[0]&0x41u) &&
     s->actlr==3 && !s->prr_hi && !s->prr_lo && v[3]==0;
   for(unsigned bank=0;bank<22 && cold_unreported;bank++)if(s->cold.sctlr[bank]&1u)cold_unreported=0;
   for(unsigned stream=0;stream<24 && cold_unreported;stream++)
     if(s->cold.smr[stream] || s->cold.s2cr[stream]!=0x20000u)cold_unreported=0;
   if(v[i]==UINT32_MAX && !cold_unreported){c.code=HPH_READ;goto done;}
   if(i==0){s->actlr=v[i];s->valid|=1u<<9;}
   if(i==2){s->prr_lo=v[1];s->prr_hi=v[2];s->valid|=1u<<10;}
   if(i==6){s->fsr=v[3];s->far=v[4]|((GPP_U64)v[5]<<32);s->fsynr=v[6];s->valid|=1u<<11;}
   if(i==7){s->cxo=v[7];s->valid|=1u<<12;}
  }}
 done:if(c.code)*failed=c.address;return c.code;
}
static inline int gec_state_qualified(const struct hph_record *record,const struct gec_memory *m,const struct gec_result *r,const struct gec_snapshot *s,unsigned reset){
 const struct hph_cold *b=&s->cold;
 if(reset>2 || s->valid!=0x1fffu || !gec_image_valid(m,r) || !hph_record_valid(record) || !record->complete || b->valid!=0xffffu || b->hfi ||
  (b->values[HGB_CFG]&0x80000003u)!=1 || (b->values[HGB_MEM]&0x80000003u)!=1 ||
  (b->values[HGB_DVM]&0x80000001u)!=1 || (b->values[HGB_CX]&1) || !(b->values[HGB_HW]&0x80000000u) ||
  (b->values[HGB_AHB]&0x80000000u) || (reset<2?b->values[HGB_RESET]!=reset:b->values[HGB_RESET]>1) ||
  (b->values[HGB_VOTE]&0x80000001u)!=1 || b->values[HGB_ID0]!=0x4c017e18u || b->values[HGB_ID1]!=0x40000016u ||
  b->values[HGB_ID2]!=0x5511u || b->values[HGB_GFSR] || b->values[HGB_FWINIT] || b->values[HGB_GXPOWER]!=3 ||
  (s->cxo&0x80000001u)!=1 || !hph_hub_cold(&s->clock))return 0;
 for(unsigned i=0;i<24;i++)if(b->smr[i]!=(i==0 && r->stream_attached?0x80000005u:0) ||
  b->s2cr[i]!=(i==0 && r->route_prepared?2u:0x20000u))return 0;
 /* Before our first SCTLR0/known-image publication, the disabled bank may
  * retain arbitrary reset values. Match the established UEFI M/CFRE gate;
  * after publication, disabled SCTLR must be exactly zero. Never replay the
  * captured original TTBR or other uninitialized context fields. */
 for(unsigned i=0;i<22;i++)if(i==2?(r->enabled?(b->sctlr[i]&~0x1000u)!=0x27u:
  r->context_prepared?b->sctlr[i]!=0:(b->sctlr[i]&0x41u)!=0):(b->sctlr[i]&1)!=0)return 0;
 if(s->context[0]!=b->sctlr[2] || s->actlr!=(r->platform_prepared?0x32bu:3u) ||
  s->prr_lo!=(r->platform_prepared?(GPP_U32)m->zero_physical:0) ||
  s->prr_hi!=(r->platform_prepared?(GPP_U32)(m->zero_physical>>32):0))return 0;
 if(r->context_prepared)for(unsigned i=1;i<9;i++){
  GPP_U64 want=r->context_retired && (i==5 || i==6)?0:r->context_image[i];
  if(i==3?((s->context[i]^want)&~UINT64_C(0x60))!=0:s->context[i]!=want)return 0;
 }
 GPP_U32 fault=s->fsr&0xc00001feu;
 if(fault && !(r->allow_fault && fault==2 && s->far==UINT64_C(0x60002000) && !(s->fsynr&16)))return 0;
 const unsigned p[]={HCW_GMU_CMD,HCW_GMU_CFG,HCW_GMU_BRANCH,HCW_HUB_CMD,HCW_HUB_CFG,HCW_HUB_AON,HCW_HUB_CX,HCW_AHB_DIV,HCW_PLL1_MODE,HCW_PLL1_OPMODE,HCW_PLL1_L,HCW_PLL1_ALPHA,HCW_PLL1_USER};
 for(unsigned at=0;at<2;at++)for(unsigned i=0;i<sizeof(p)/sizeof(p[0]);i++)if(s->clock.raw[at][p[i]]!=record->clock.raw[at][p[i]])return 0;
 if(((s->clock.raw[1][HCW_GMU_CFG]>>8)&7u)==1)for(unsigned at=0;at<2;at++)for(unsigned i=0;i<6;i++)if(i!=2 && s->clock.raw[at][HCW_PLL0_MODE+i]!=record->clock.raw[at][HCW_PLL0_MODE+i])return 0;
 return 1;
}
static inline int gec_guard(const struct gec_io *io,const struct hph_record *record,const struct gec_memory *m,struct gec_result *r,unsigned reset){
 if(!gec_owned(io,r))return 0;++r->guards;r->guard_reset=reset;r->last_guard_address=0;hph_zero(&r->last,sizeof(r->last));
 r->last_fresh_code=io->fresh(io->cookie,reset,&r->last,&r->last_guard_address);
 if(r->last_fresh_code || !gec_state_qualified(record,m,r,&r->last,reset)){
  r->qualification_lost=1;gec_fail(r,r->last_fresh_code?GEC_FRESH:GEC_RECHECK,GEC_OP_FRESH,0,r->last_guard_address,0,0,r->last_fresh_code);return 0;}
 /* Only the real all-FAULT/M0 phase gets the ordinary hph cold recheck. */
 if(!r->enabled && !r->route_prepared && !r->stream_attached && reset==1){
  r->last_recheck=hph_recheck(record,&r->last.cold,&r->last.clock);
  if(r->last_recheck){r->qualification_lost=1;gec_fail(r,GEC_RECHECK,GEC_OP_RECHECK,0,0,0,0,r->last_recheck);return 0;}}
 return gec_owned(io,r);
}
static inline GPP_U32 gec_fingerprint(const struct gec_access *a,unsigned count){
 GPP_U32 crc=0xffffffffu;
 for(unsigned i=0;i<count;i++){for(unsigned j=0;j<4;j++){
  crc^=(GPP_U32)(a[i].value>>(8*j))&255u;for(unsigned k=0;k<8;k++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}}
 return ~crc;
}
static inline int gec_backup_intact(struct gec_result *r){
 int good=r->backup_words==68 && r->control_backup_words==9 && r->context_backup_words==9;
 for(unsigned i=0;i<68;i++)good&=gec_access_ok(&r->backup[i]);for(unsigned i=0;i<9;i++)good&=gec_access_ok(&r->control_backup[i]) && gec_access_ok(&r->context_backup[i]);
 if(good && r->backup_crc==gec_fingerprint(r->backup,68) && r->control_crc==gec_fingerprint(r->control_backup,9))return 1;
 gec_fail(r,GEC_BACKUP,GEC_OP_VERIFY,0,0,r->backup_crc,gec_fingerprint(r->backup,68),0);return 0;
}
static inline int gec_memory_valid(const struct gec_memory *m){
 if(!m || m->memory_profile>GEC_MEMORY_NORMAL_NC || (m->cpu_page_bytes!=4096 && m->cpu_page_bytes!=16384))return 0;
 const struct gec_owned *o=&m->owned;size_t p=m->cpu_page_bytes;
 const size_t want[]={((12288+p-1)/p)*p,p,p},have[]={m->table_owned_bytes,m->data_owned_bytes,m->zero_owned_bytes};
 const GPP_U64 pa[]={o->table_physical,o->data_physical,m->zero_physical};const uintptr_t va[]={(uintptr_t)o->tables,(uintptr_t)o->data,(uintptr_t)m->zero_cpu};
 for(unsigned i=0;i<3;i++){
  if(have[i]!=want[i] || !pa[i] || (pa[i]&(p-1)) || !va[i] || (va[i]&(p-1)) || pa[i]>=(UINT64_C(1)<<36)-have[i] || va[i]>UINTPTR_MAX-have[i])return 0;
  for(unsigned j=0;j<i;j++)if((pa[i]<pa[j]+have[j] && pa[j]<pa[i]+have[i]) || (va[i]<va[j]+have[j] && va[j]<va[i]+have[i]))return 0;
 }
 return o->config.input_bits==32 && o->config.output_bits==36 && o->config.table_physical==o->table_physical &&
  o->config.leaf_budget==1 && o->built.table_pages==3 && o->built.leaf_pages==1 && o->built.start_level==1 && o->built.root_physical==o->table_physical &&
  o->range.iova==UINT64_C(0x60000000) && o->range.physical==o->data_physical && o->range.bytes==4096 &&
  o->range.permissions==(ArmSmmu4KRead|ArmSmmu4KWrite) && o->range.writeback==(m->memory_profile==GEC_MEMORY_WRITEBACK) && o->range.shareability==3;
}
static inline int gec_tables_valid(const struct gec_memory *m){
 unsigned char visited[3]={0};struct gec_tables table={(const volatile uint64_t *)m->owned.tables,m->owned.table_physical};
 ArmSmmu4KTableIo io={&table,gec_read_descriptor};ArmSmmu4KAuditRegion region={&m->owned.range,1,3,1536,visited,sizeof(visited)};ArmSmmu4KTableAuditResult audit;
 hph_zero(&audit,sizeof(audit));return !ArmSmmu4KAuditTables(&io,&m->owned.config,&m->owned.built,&region,m->owned.table_physical,&audit) && audit.complete && audit.table_pages==3 && audit.leaf_pages==1;
}
static inline int gec_cpu_initial(const struct gec_memory *m){
 const volatile GPP_U32 *data=(const volatile GPP_U32 *)m->owned.data,*zero=(const volatile GPP_U32 *)m->zero_cpu;
 for(unsigned i=0;i<1024;i++)if(data[i]!=(i==0?0x7504cafeu:i==1?0x86500071u:0u) || zero[i])return 0;return 1;
}
static inline int gec_store_allowed(const struct gec_memory *m,const struct gec_result *r,GPP_U32 a,GPP_U64 value,unsigned wide){
 int ti=gec_tcm_index(a),ci=gie_control_index(a),field=gec_field_index(a);
 if(ti>=0)return !wide && (r->phase==GEC_PHASE_PROGRAM?ti<52 && value==gec_program_value((unsigned)ti):
  r->phase==GEC_PHASE_RESTORE_TCM && value==r->backup[ti].value);
 if(ci>=0)return !wide && (r->phase==GEC_PHASE_CONTROL?value==gie_control_value((unsigned)ci):
  r->phase==GEC_PHASE_RESTORE_CONTROL && ci<7 && value==r->control_backup[ci].value);
 if(field>=0){
  if(wide!=(unsigned)(field==5 || field==6))return 0;
  if(r->phase==GEC_PHASE_CONTEXT)return value==r->context_image[field];
  if(r->phase==GEC_PHASE_CONTEXT_RETIRE)return value==((field==5 || field==6)?0:r->context_image[field]);
  return !wide && field==0 && ((r->phase==GEC_PHASE_ENABLE && value==0x1027u) || (r->phase==GEC_PHASE_DETACH && value==0));
 }
 if(wide)return 0;
 if(a==HGP_CB(0x618) || a==HGP_CB(0x7f0))return value==(a==HGP_CB(0x618)?0u:0xffffffffu) &&
  (r->phase==GEC_PHASE_CONTEXT || r->phase==GEC_PHASE_PLATFORM || r->phase==GEC_PHASE_DETACH || r->phase==GEC_PHASE_PLATFORM_RETIRE || r->phase==GEC_PHASE_CONTEXT_RETIRE);
 if(a==HGP_CB(4))return (r->phase==GEC_PHASE_PLATFORM && value==0x32bu) || (r->phase==GEC_PHASE_PLATFORM_RETIRE && value==3);
 if(a==0x3da6008u || a==0x3da600cu)return r->phase==GEC_PHASE_PLATFORM?
  value==(a==0x3da6008u?(GPP_U32)m->zero_physical:(GPP_U32)(m->zero_physical>>32)):
  r->phase==GEC_PHASE_PLATFORM_RETIRE && value==0;
 if(a==0x3da0800u)return (r->phase==GEC_PHASE_ROUTE && value==0x80000005u) || (r->phase==GEC_PHASE_DETACH && value==0);
 if(a==0x3da0c00u)return (r->phase==GEC_PHASE_ROUTE && value==2) || (r->phase==GEC_PHASE_DETACH && value==0x20000u);
 if(a==HGP_CB(0x58))return r->phase==GEC_PHASE_CLEAR_FAULT && value==2 && r->negative && r->isolated && r->reset_held;
 return a==0x3d7e000u && r->possible_execution &&
  ((r->phase==GEC_PHASE_RELEASE && value==0 && r->prepared_committed && r->enabled && r->stream_attached) ||
   (r->phase==GEC_PHASE_RESET && value==1 && r->guard_reset==2));
}
static inline int gec_store(const struct gec_io *io,const struct gec_memory *m,struct gec_result *r,GPP_U32 a,GPP_U64 value,unsigned wide,GPP_U64 mask){
 if(r->barrier_uncertain || r->event_count>=GEC_MAX_EVENTS || !gec_image_valid(m,r) || !r->backup_committed || !gec_backup_intact(r) || !gec_store_allowed(m,r,a,value,wide)){
  gec_fail(r,GEC_ARGUMENT,GEC_OP_WRITE,0,a,0,value,0);return 0;}
 if(!gec_owned(io,r))return 0;struct gec_event *e=&r->events[r->event_count++];e->phase=r->phase;e->address=a;e->wide=wide;e->wanted=value;e->mask=mask;
 e->store.attempted=1;e->store.value=value;++r->writes;
 if(gec_tcm_index(a)>=0)r->tcm_retained=1;
 e->store.status=wide?io->write64(io->cookie,a,value):io->write32(io->cookie,a,(GPP_U32)value);
 e->store.returned=1;e->store.valid=e->store.status==0;
 if(e->store.status)gec_fail(r,GEC_WRITE,GEC_OP_WRITE,r->event_count-1,a,value,0,e->store.status);
 e->barrier.attempted=1;++r->barriers;e->barrier.status=io->barrier(io->cookie);e->barrier.returned=1;e->barrier.valid=e->barrier.status==0;
 if(e->barrier.status){r->barrier_uncertain=1;gec_fail(r,GEC_BARRIER,GEC_OP_BARRIER,r->event_count-1,a,0,0,e->barrier.status);return 0;}
 if(!e->store.valid)return 0;
 if(!mask)return 1;
 if(!gec_load(io,r,a,wide,&e->verify))return 0;
 if(((e->verify.value^value)&mask)!=0){gec_fail(r,GEC_VERIFY,GEC_OP_VERIFY,r->event_count-1,a,value,e->verify.value,0);return 0;}return 1;
}
static inline int gec_sync(const struct gec_io *io,const struct gec_memory *m,struct gec_result *r){
 if(!gec_store(io,m,r,HGP_CB(0x618),0,0,0) || !gec_store(io,m,r,HGP_CB(0x7f0),0xffffffffu,0,0))return 0;
 for(unsigned i=0;i<100;i++){
  struct gec_access a;hph_zero(&a,sizeof(a));if(!gec_load(io,r,HGP_CB(0x7f4),0,&a))return 0;if(!(a.value&1))return 1;
  if(i==99){gec_fail(r,GEC_TIMEOUT,GEC_OP_VERIFY,i,HGP_CB(0x7f4),0,a.value,0);return 0;}
  if(!gec_owned(io,r))return 0;++r->delays;GPP_U64 status=io->delay(io->cookie,10);
  if(status){gec_fail(r,GEC_DELAY,GEC_OP_DELAY,i,0,10,0,status);return 0;}
 }return 0;
}
static inline int gec_commit(const struct gec_io *io,struct gec_result *r,unsigned stage){
 if(stage<1 || stage>3 || !gec_owned(io,r))return 0;++r->commits;struct gec_access *a=&r->commit_access[stage-1];
 a->attempted=1;a->status=io->commit(io->cookie,stage,r);a->returned=1;a->valid=a->status==0;
 if(a->status){gec_fail(r,GEC_COMMIT,GEC_OP_COMMIT,stage,0,0,0,a->status);return 0;}return 1;
}
static inline int gec_delay(const struct gec_io *io,struct gec_result *r){
 if(!gec_owned(io,r))return 0;++r->delays;GPP_U64 status=io->delay(io->cookie,10);
 if(status){gec_fail(r,GEC_DELAY,GEC_OP_DELAY,0,0,10,0,status);return 0;}return 1;
}
static inline int gec_memory_check(const struct gec_io *io,const struct gec_memory *m,struct gec_result *r,unsigned stage){
 if(!gec_owned(io,r))return 0;if(r->memory_check_calls>=4){gec_fail(r,GEC_ARGUMENT,GEC_OP_MEMORY,stage,0,0,0,0);return 0;}
 struct gec_access *a=&r->memory_access[r->memory_check_calls++];a->value=stage;a->attempted=1;a->status=io->memory_check(io->cookie,stage,m);a->returned=1;a->valid=a->status==0;
 if(a->status){r->qualification_lost=1;gec_fail(r,GEC_MEMORY,GEC_OP_MEMORY,stage,0,0,0,a->status);return 0;}return 1;
}
/* The fixed 176-byte image has two bounded terminals. A precise fault at
 * its single negative LDR must carry the exact SCB and SMMU evidence, not
 * merely DEAD. R12/LR/APSR were not initialized by this image and are data
 * only: the handler never uses them and no exception return is executed. */
static inline GPP_U32 gec_terminal_mail_word(unsigned fault,unsigned i){
 const GPP_U32 normal[]={0xa7500003u,0x7504cafeu,0x86500071u,0xa750cafeu,0,0,0,0};
 const GPP_U32 precise[]={0xa750deadu,0x7504cafeu,0x86500071u,0xa750cafeu,0,0,0x8200u,0x60002000u};
 return i<8?(fault?precise[i]:normal[i]):0;
}
static inline int gec_terminal_mailbox_valid(const struct gec_result *r){
 if(!r)return 0;unsigned fault=r->mailbox[0].value==0xa750deadu;
 for(unsigned i=0;i<8;i++)if(!gec_access_ok(&r->mailbox[i]) || r->mailbox[i].value!=gec_terminal_mail_word(fault,i))return 0;
 return 1;
}
static inline GPP_U64 gec_terminal_stack_expected(const struct gec_result *r,unsigned i){
 if(i>=16)return 0;
 if(r->mailbox[0].value!=0xa750deadu || i<8)return r->backup[52+i].value;
 const GPP_U32 frame[]={0x10004000u,0xa7500001u,0x60002000u,0xa750cafeu,0,0,0x6eu,0x01000000u};
 return frame[i-8];
}
static inline int gec_terminal_stack_word_valid(const struct gec_result *r,unsigned i){
 if(!r || i>=16 || !gec_access_ok(&r->backup[52+i]) || !gec_access_ok(&r->stack_after[i]) || r->stack_after[i].value>UINT32_MAX)return 0;
 GPP_U64 value=r->stack_after[i].value;
 if(r->mailbox[0].value==0xa750deadu && i>=8){
  if(i==12 || i==13)return 1; /* raw R12/LR, never consumed as control */
  if(i==15)return (value&~UINT64_C(0xf8000000))==UINT64_C(0x01000000); /* Thread/T, no IT or alignment pad */
 }
 return value==gec_terminal_stack_expected(r,i);
}
static inline int gec_terminal_stack_valid(const struct gec_result *r){
 if(!gec_terminal_mailbox_valid(r))return 0;unsigned changed=0;
 for(unsigned i=0;i<16;i++){
  if(!gec_terminal_stack_word_valid(r,i))return 0;
  if(r->stack_after[i].value!=r->backup[52+i].value)changed|=1u<<i;
 }
 return changed==r->stack_changed_mask;
}
static inline int gec_cpu_memory_complete(const struct gec_result *r){
 if(!r)return 0;
 if(r->memory_profile==GEC_MEMORY_WRITEBACK)
  return r->cpu.invalidated && !r->nc_acquired && !r->nc_restore_published && !r->nc_table_invalidated;
 return r->memory_profile==GEC_MEMORY_NORMAL_NC && !r->cpu.invalidated &&
  r->nc_acquired==6 && r->nc_restore_published==1 && r->nc_table_invalidated==1;
}
static inline int gec_complete_proof(const struct gec_result *r){
 if(!r || r->code || !r->safe_to_save || !r->restore_committed || !r->backup_committed || !r->prepared_committed ||
  r->qualification_lost || r->barrier_uncertain || !r->possible_execution || !r->reset_held || !r->context_prepared || !r->context_retired ||
  r->platform_prepared || r->enabled || r->stream_attached || r->route_prepared || !r->detached || !r->isolated || !r->table_held || r->tcm_retained ||
  !r->published || !r->positive || !r->negative || r->backup_words!=68 || r->control_backup_words!=9 || r->context_backup_words!=9 ||
  r->programmed_words!=52 || r->control_programmed!=9 || r->restored_words!=68 || r->control_restored!=7 || r->final_verified_words!=68 ||
  r->irq_retained!=3 || !r->polls || r->polls>1000 || !r->reset_polls || r->reset_polls>1000 ||
  !gec_cpu_memory_complete(r) || !r->cpu.verified || !r->cpu.restored || r->cpu.checked_words!=1024 || !r->cpu.padding_checked || r->cpu.observed!=0xa750cafeu ||
  r->memory_check_calls!=4 || r->event_count!=179 || r->writes!=179 || r->barriers!=179 || r->reads!=r->load_count || r->load_count<422 ||
  (r->fsr&0xc00001feu)!=2 || r->far!=UINT64_C(0x60002000) || (r->fsynr&16) ||
  !gec_access_ok(&r->publication) || !gec_access_ok(&r->cpu_finish_access))return 0;
 for(unsigned i=0;i<4;i++)if(!gec_access_ok(&r->memory_access[i]))return 0;
 for(unsigned i=0;i<3;i++)if(!gec_access_ok(&r->commit_access[i]))return 0;
 for(unsigned i=0;i<68;i++)if(!gec_access_ok(&r->backup[i]) || !gec_access_ok(&r->final_verify[i]) || r->backup[i].value!=r->final_verify[i].value)return 0;
 if(!gec_terminal_stack_valid(r))return 0;
 for(unsigned i=0;i<9;i++)if(!gec_access_ok(&r->control_backup[i]) || !gec_access_ok(&r->context_backup[i]) || !gec_access_ok(&r->control_final_verify[i]) ||
  ((r->control_final_verify[i].value^(i<7?r->control_backup[i].value:gie_control_value(i)))&(i<7?UINT64_C(0xffffffff):gie_control_mask(i))))return 0;
 for(unsigned i=0;i<r->event_count;i++)if(!gec_access_ok(&r->events[i].store) || !gec_access_ok(&r->events[i].barrier) ||
  (r->events[i].mask && (!gec_access_ok(&r->events[i].verify) || ((r->events[i].verify.value^r->events[i].wanted)&r->events[i].mask))))return 0;
 for(unsigned i=0;i<r->load_count;i++)if(!gec_access_ok(&r->loads[i].access))return 0;
 if(!gec_terminal_mailbox_valid(r))return 0;
 return r->backup_crc==gec_fingerprint(r->backup,68) && r->control_crc==gec_fingerprint(r->control_backup,9);
}
static inline unsigned gec_run(const struct gec_io *io,const struct hph_record *record,const struct hph_cold *current,const struct hph_clock *clock,
 const struct gec_memory *m,struct gec_result *r){
 if(!r)return GEC_ARGUMENT;if(r->started)return GEC_REUSED;hph_zero(r,sizeof(*r));r->started=1;r->phase=GEC_PHASE_INPUT;r->retained=1;r->table_held=1;
 if(!io || !io->read32 || !io->read64 || !io->write32 || !io->write64 || !io->barrier || !io->delay || !io->fresh || !io->commit ||
  !io->exclusive || !io->memory_check || !io->publish || !io->cpu_finish || !record || !current || !clock || !m){gec_fail(r,GEC_ARGUMENT,GEC_OP_RECHECK,0,0,0,0,0);return r->code;}
 r->record_crc=record->crc32;r->input_recheck=hph_recheck(record,current,clock);
 if(r->input_recheck || !gec_memory_valid(m)){gec_fail(r,GEC_INPUT,GEC_OP_RECHECK,0,0,0,0,r->input_recheck);return r->code;}
 r->memory_profile=m->memory_profile;
 if(!gec_memory_check(io,m,r,0))return r->code;
 if(!gec_tables_valid(m) || !gec_cpu_initial(m)){gec_fail(r,GEC_TABLE,GEC_OP_VERIFY,0,0,0,0,0);return r->code;}
 ArmSmmu4KContextConfig cfg={&m->owned.config,0,&m->owned.built,0,0x4c017e18u,0x5511u,0x750,0,0,0};ArmSmmu4KContextImage image;
 if(ArmSmmu4KEncodeContext(&cfg,&image)){gec_fail(r,GEC_TABLE,GEC_OP_VERIFY,0,0,0,0,0);return r->code;}
 {const GPP_U64 v[]={0,image.cba2r,image.cbar,image.tcr2,image.tcr,image.ttbr0,image.ttbr1,image.mair0,image.mair1};for(unsigned i=0;i<9;i++)r->context_image[i]=v[i];}
 if(!gec_guard(io,record,m,r,1))return r->code;r->reset_held=1;r->phase=GEC_PHASE_BACKUP;
 for(unsigned i=0;i<68;i++){if(!gec_load(io,r,gec_address(i),0,&r->backup[i]))return r->code;++r->backup_words;}
 for(unsigned i=0;i<9;i++){
  if(!gec_load(io,r,gie_control_address(i),0,&r->control_backup[i]))return r->code;++r->control_backup_words;
  if(!gec_load(io,r,gec_field_address(i),i==5 || i==6,&r->context_backup[i]))return r->code;++r->context_backup_words;
 }
 r->backup_crc=gec_fingerprint(r->backup,68);r->control_crc=gec_fingerprint(r->control_backup,9);r->phase=GEC_PHASE_BACKUP_COMMIT;
 if(!gec_guard(io,record,m,r,1))return r->code;
 {struct gec_access *a=&r->publication;a->attempted=1;a->status=io->publish(io->cookie,m);a->returned=1;a->valid=a->status==0;
  if(a->status){gec_fail(r,GEC_PUBLISH,GEC_OP_MEMORY,0,0,0,0,a->status);return r->code;}r->published=1;}
 {int committed=gec_commit(io,r,1);if(!gec_guard(io,record,m,r,1) || !committed || !gec_backup_intact(r))return r->code;}r->backup_committed=1;
 r->phase=GEC_PHASE_CONTEXT;
 for(unsigned i=0;i<9;i++)if(!gec_store(io,m,r,gec_field_address(i),r->context_image[i],i==5 || i==6,i==3?~UINT64_C(0x60):i==5 || i==6?UINT64_MAX:UINT64_C(0xffffffff)))return r->code;
 if(!gec_sync(io,m,r))return r->code;r->context_prepared=1;
 r->phase=GEC_PHASE_PROGRAM;if(!gec_guard(io,record,m,r,1))return r->code;
 for(unsigned i=0;i<52;i++){if(!gec_store(io,m,r,gec_address(i),gec_program_value(i),0,0xffffffffu))goto cleanup;++r->programmed_words;}
 r->phase=GEC_PHASE_CONTROL;if(!gec_guard(io,record,m,r,1))return r->code;
 {const unsigned order[]={1,0,2,3,4,5,6,7,8};for(unsigned n=0;n<9;n++){unsigned i=order[n];if(!gec_store(io,m,r,gie_control_address(i),gie_control_value(i),0,gie_control_mask(i)))goto cleanup;++r->control_programmed;}}
 r->phase=GEC_PHASE_PLATFORM;if(!gec_guard(io,record,m,r,1))return r->code;
 if(!gec_store(io,m,r,0x3da6008u,(GPP_U32)m->zero_physical,0,0xffffffffu) || !gec_store(io,m,r,0x3da600cu,(GPP_U32)(m->zero_physical>>32),0,0xffffffffu) ||
  !gec_store(io,m,r,HGP_CB(4),0x32b,0,0xffffffffu) || !gec_sync(io,m,r)){return r->code;}
 r->platform_prepared=1;
 r->phase=GEC_PHASE_PREPARED_COMMIT;if(!gec_guard(io,record,m,r,1) || !gec_memory_check(io,m,r,1))return r->code;
 {int committed=gec_commit(io,r,2);if(!gec_guard(io,record,m,r,1) || !committed)goto cleanup;}
 if(!gec_tables_valid(m) || !gec_cpu_initial(m)){gec_fail(r,GEC_TABLE,GEC_OP_VERIFY,0,0,0,0,0);goto cleanup;}
 for(unsigned i=0;i<52;i++){struct gec_access a;hph_zero(&a,sizeof(a));if(!gec_load(io,r,gec_address(i),0,&a))goto cleanup;if(a.value!=gec_program_value(i)){gec_fail(r,GEC_VERIFY,GEC_OP_VERIFY,i,gec_address(i),gec_program_value(i),a.value,0);goto cleanup;}}
 for(unsigned i=0;i<9;i++){struct gec_access a;hph_zero(&a,sizeof(a));if(!gec_load(io,r,gie_control_address(i),0,&a))goto cleanup;if(((a.value^gie_control_value(i))&gie_control_mask(i))!=0){gec_fail(r,GEC_VERIFY,GEC_OP_VERIFY,i,gie_control_address(i),gie_control_value(i),a.value,0);goto cleanup;}}
 r->prepared_committed=1;r->phase=GEC_PHASE_ENABLE;
 if(!gec_store(io,m,r,HGP_CB(0),0x1027,0,~UINT64_C(0x1000)))return r->code;r->enabled=1;
 r->phase=GEC_PHASE_ROUTE;
 if(!gec_store(io,m,r,0x3da0c00u,2,0,0xffffffffu))return r->code;r->route_prepared=1;
 if(!gec_store(io,m,r,0x3da0800u,0x80000005u,0,0xffffffffu))return r->code;r->stream_attached=1;
 if(!gec_guard(io,record,m,r,1))return r->code;
 r->phase=GEC_PHASE_RELEASE;r->possible_execution=1;r->reset_held=0;
 if(!gec_store(io,m,r,0x3d7e000u,0,0,0xffffffffu))goto cleanup;r->allow_fault=1;r->phase=GEC_PHASE_POLL;
 for(unsigned i=0;i<1000;i++){
  struct gec_access a;hph_zero(&a,sizeof(a));if(!gec_guard(io,record,m,r,0))return r->code;r->polls=i+1;
  if(!gec_load(io,r,0x3d71000u,0,&a))goto cleanup;
  if(a.value==0xa7500003u || a.value==0xa750deadu)break; /* qualify all eight words plus the real exception frame below */
  if(a.value!=0 && a.value!=0xa7500001u){gec_fail(r,GEC_VERIFY,GEC_OP_VERIFY,0,0x3d71000u,0xa7500003u,a.value,0);goto cleanup;}
  if(i==999){gec_fail(r,GEC_TIMEOUT,GEC_OP_VERIFY,i,0x3d71000u,0xa7500003u,a.value,0);goto cleanup;}if(!gec_delay(io,r))goto cleanup;
 }
 cleanup:
 if(r->qualification_lost || r->barrier_uncertain || !r->context_prepared)return r->code;
 if(r->possible_execution){
  r->phase=GEC_PHASE_RESET;if(!gec_guard(io,record,m,r,2))return r->code;
  if(!gec_store(io,m,r,0x3d7e000u,1,0,0))return r->code;r->phase=GEC_PHASE_RESET_POLL;
  for(unsigned i=0;i<1000;i++){
   struct gec_access a;hph_zero(&a,sizeof(a));if(!gec_guard(io,record,m,r,2))return r->code;r->reset_polls=i+1;if(!gec_load(io,r,0x3d7e000u,0,&a))return r->code;
   if(a.value==1){r->reset_held=1;break;}if(a.value>1){r->qualification_lost=1;gec_fail(r,GEC_VERIFY,GEC_OP_VERIFY,i,0x3d7e000u,1,a.value,0);return r->code;}
   if(i==999){gec_fail(r,GEC_TIMEOUT,GEC_OP_VERIFY,i,0x3d7e000u,1,a.value,0);return r->code;}if(!gec_delay(io,r))return r->code;
  }
 }
 if(!gec_guard(io,record,m,r,1) || !gec_backup_intact(r))return r->code;r->reset_held=1;
 if(r->possible_execution){
  r->phase=GEC_PHASE_CAPTURE;for(unsigned i=0;i<8;i++)if(!gec_load(io,r,0x3d71000u+4*i,0,&r->mailbox[i]))return r->code;
  r->fsr=r->last.fsr;r->far=r->last.far;r->fsynr=r->last.fsynr;
  r->negative=(r->fsr&0xc00001feu)==2 && r->far==UINT64_C(0x60002000) && !(r->fsynr&16);
  const unsigned precise=r->mailbox[0].value==0xa750deadu;r->positive=1;
  for(unsigned i=0;i<8;i++){GPP_U32 wanted=gec_terminal_mail_word(precise,i);if(!gec_access_ok(&r->mailbox[i]) || r->mailbox[i].value!=wanted){r->positive=0;gec_fail(r,precise?GEC_FAULT:GEC_VERIFY,GEC_OP_VERIFY,i,0x3d71000u+4*i,wanted,r->mailbox[i].value,0);}}
  if(!r->negative)gec_fail(r,GEC_VERIFY,GEC_OP_VERIFY,0,HGP_CB(0x58),2,r->fsr,0);
  /* Only the fixed program's DSB-ordered full mailbox and exact negative
   * translation fault prove this finite master completed. Reset1/TLBSYNC
   * cannot drain unknown posted DMA. Preserve every BO and table on any
   * incomplete observation; caller performs platform reset without HFS. */
  if(r->code || !r->positive || !r->negative)return r->code;
 }
 for(unsigned i=0;i<16;i++){if(!gec_load(io,r,gec_address(52+i),0,&r->stack_after[i]))return r->code;if(r->stack_after[i].value!=r->backup[52+i].value)r->stack_changed_mask|=1u<<i;}
 if(r->mailbox[0].value==0xa750deadu){
  /* Unknown frame is not proof of completion. Do not retire, invalidate CPU
   * data or enter any later checkpoint on this path. Keep all BOs retained. */
  for(unsigned i=0;i<16;i++)if(!gec_terminal_stack_word_valid(r,i)){gec_fail(r,GEC_VERIFY,GEC_OP_VERIFY,52+i,gec_address(52+i),gec_terminal_stack_expected(r,i),r->stack_after[i].value,0);return r->code;}
  if(!gec_terminal_stack_valid(r)){gec_fail(r,GEC_VERIFY,GEC_OP_VERIFY,52,gec_address(52),0,r->stack_changed_mask,0);return r->code;}
 }else if(r->stack_changed_mask){gec_fail(r,GEC_VERIFY,GEC_OP_VERIFY,52,gec_address(52),0,r->stack_changed_mask,0);return r->code;}
 r->phase=GEC_PHASE_DETACH;if(!gec_guard(io,record,m,r,1))return r->code;
 if(r->stream_attached){if(!gec_store(io,m,r,0x3da0800u,0,0,0xffffffffu))return r->code;r->stream_attached=0;}
 if(r->route_prepared){if(!gec_store(io,m,r,0x3da0c00u,0x20000,0,0xffffffffu))return r->code;r->route_prepared=0;}
 if(r->enabled){if(!gec_store(io,m,r,HGP_CB(0),0,0,0xffffffffu))return r->code;r->enabled=0;}
 if(!gec_sync(io,m,r) || !gec_guard(io,record,m,r,1))return r->code;r->detached=r->isolated=1;
 r->phase=GEC_PHASE_CLEAR_FAULT;
 if(r->negative && !gec_store(io,m,r,HGP_CB(0x58),2,0,0))return r->code;r->allow_fault=0;
 if(!gec_guard(io,record,m,r,1))return r->code;
 r->phase=GEC_PHASE_PLATFORM_RETIRE;
 if(r->platform_prepared){if(!gec_store(io,m,r,HGP_CB(4),3,0,0xffffffffu) ||
  !gec_store(io,m,r,0x3da6008u,0,0,0xffffffffu) || !gec_store(io,m,r,0x3da600cu,0,0,0xffffffffu) || !gec_sync(io,m,r)){return r->code;}
  r->platform_prepared=0;}
 r->phase=GEC_PHASE_CONTEXT_RETIRE;if(!gec_guard(io,record,m,r,1))return r->code;
 for(unsigned i=0;i<9;i++)if(!gec_store(io,m,r,gec_field_address(i),(i==5 || i==6)?0:r->context_image[i],i==5 || i==6,i==3?~UINT64_C(0x60):i==5 || i==6?UINT64_MAX:UINT64_C(0xffffffff)))return r->code;
 if(!gec_sync(io,m,r))return r->code;r->context_retired=1;
 if(!gec_guard(io,record,m,r,1) || !gec_memory_check(io,m,r,2))return r->code;
 r->phase=GEC_PHASE_RESTORE_TCM;
 for(unsigned i=0;i<68;i++){if(!gec_store(io,m,r,gec_address(i),r->backup[i].value,0,0xffffffffu))return r->code;++r->restored_words;}
 r->phase=GEC_PHASE_RESTORE_CONTROL;if(!gec_guard(io,record,m,r,1))return r->code;
 for(unsigned n=7;n>0;n--){unsigned i=n-1;if(!gec_store(io,m,r,gie_control_address(i),r->control_backup[i].value,0,0xffffffffu))return r->code;++r->control_restored;}
 r->phase=GEC_PHASE_FINAL_VERIFY;if(!gec_guard(io,record,m,r,1))return r->code;
 for(unsigned i=0;i<68;i++){if(!gec_load(io,r,gec_address(i),0,&r->final_verify[i]))return r->code;if(r->final_verify[i].value!=r->backup[i].value){gec_fail(r,GEC_VERIFY,GEC_OP_VERIFY,i,gec_address(i),r->backup[i].value,r->final_verify[i].value,0);return r->code;}++r->final_verified_words;}
 for(unsigned i=0;i<9;i++){
  if(!gec_load(io,r,gie_control_address(i),0,&r->control_final_verify[i]))return r->code;unsigned keep=i>=7 && r->control_programmed>i;
  GPP_U64 want=keep?gie_control_value(i):r->control_backup[i].value,mask=keep?gie_control_mask(i):0xffffffffu;
  if(((r->control_final_verify[i].value^want)&mask)!=0){gec_fail(r,GEC_VERIFY,GEC_OP_VERIFY,i,gie_control_address(i),want,r->control_final_verify[i].value,0);return r->code;}if(keep)r->irq_retained|=1u<<(i-7);
 }
 r->tcm_retained=0;r->phase=GEC_PHASE_CPU_FINISH;
 if(r->possible_execution){
  if(!gec_guard(io,record,m,r,1) || !gec_memory_check(io,m,r,2))return r->code;
  struct gec_access *a=&r->cpu_finish_access;a->attempted=1;a->status=io->cpu_finish(io->cookie,m,&r->cpu);a->returned=1;a->valid=a->status==0;
  if(a->status || r->memory_profile!=m->memory_profile || !gec_cpu_memory_complete(r) || !r->cpu.verified || !r->cpu.restored || r->cpu.checked_words!=1024 || !r->cpu.padding_checked || r->cpu.observed!=0xa750cafeu || !gec_cpu_initial(m)){
   gec_fail(r,GEC_CPU,GEC_OP_MEMORY,2,0,0,0,a->status);return r->code;}
 }
 r->phase=GEC_PHASE_FINAL_COMMIT;if(!gec_guard(io,record,m,r,1))return r->code;
 {int committed=gec_commit(io,r,3);if(!gec_guard(io,record,m,r,1) || !committed || !gec_backup_intact(r))return r->code;}
 r->restore_committed=1;r->safe_to_save=1;r->complete=gec_complete_proof(r)?1u:0u;r->phase=GEC_PHASE_DONE;return r->code;
}
#endif
