/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * CANDIDATE: fixed 136-byte internal-only CM3 program, no firmware/DDR/DMA.
 * Source: OnePlus graphics-kernel 453d8875f815bf9dfad9035982861226a4823a6c,
 * gen7_reg.h:1119-29,1196,1218-29; adreno_gen7_gmu.c:314-21,673-88,924-54.
 * hgd payload/address provenance is pinned by GmuHeldTcm.h. That held-reset
 * policy is NOT run here. No synthetic hgb_result or altered reset snapshot.
 * All callbacks are synchronous; a bounded software poll cannot preempt an
 * MMIO instruction that does not return. No recoverable fault is introduced.
 */
#ifndef HOUJI_GMU_INTERNAL_EXEC_H
#define HOUJI_GMU_INTERNAL_EXEC_H
#include "GmuHeldTcm.h"
enum {GIE_CODE_WORDS=34,GIE_MAIL_WORDS=8,GIE_STACK_WORDS=16,GIE_PROGRAM_WORDS=42,
 GIE_TOTAL_WORDS=58,GIE_CONTROL_WORDS=9,GIE_RESTORE_CONTROLS=7,GIE_POLLS=1000};
enum {GIE_OK,GIE_ARGUMENT,GIE_REUSED,GIE_INPUT,GIE_OWNER,GIE_FRESH,GIE_RECHECK,
 GIE_CXO,GIE_READ,GIE_WRITE,GIE_BARRIER,GIE_VERIFY,GIE_COMMIT,GIE_BACKUP,GIE_TIMEOUT,GIE_FAULT,GIE_DELAY};
enum {GIE_PHASE_INPUT=1,GIE_PHASE_BACKUP,GIE_PHASE_BACKUP_COMMIT,GIE_PHASE_PROGRAM,
 GIE_PHASE_CONTROL,GIE_PHASE_PREPARED_COMMIT,GIE_PHASE_RELEASE,GIE_PHASE_POLL,
 GIE_PHASE_RESET,GIE_PHASE_RESET_POLL,GIE_PHASE_STACK,GIE_PHASE_RESTORE_TCM,
 GIE_PHASE_RESTORE_CONTROL,GIE_PHASE_FINAL_VERIFY,GIE_PHASE_FINAL_COMMIT,GIE_PHASE_DONE};
enum {GIE_OP_READ=1,GIE_OP_WRITE,GIE_OP_BARRIER,GIE_OP_FRESH,GIE_OP_RECHECK,
 GIE_OP_COMMIT,GIE_OP_OWNER,GIE_OP_VERIFY,GIE_OP_DELAY};
struct gie_access {GPP_U32 attempted,returned,valid,value;GPP_U64 status;};
struct gie_failure {unsigned code,phase,operation,index;GPP_U32 address,expected,observed;GPP_U64 status;};
struct gie_result {
 unsigned started,phase,code,complete,safe_to_save,retained,tcm_retained,qualification_lost,barrier_uncertain,possible_execution,reset_held;
 unsigned reads,writes,barriers,guards,commits,delays,backup_words,control_backup_words,programmed_words,control_programmed,restored_words,control_restored,final_verified_words,polls,reset_polls;
 unsigned input_recheck,last_fresh_code,last_recheck,backup_committed,prepared_committed,restore_committed,guard_reset;
 GPP_U32 record_crc,backup_crc,control_crc,last_guard_address,stack_changed_mask,irq_retained;
 struct gie_failure first;struct hph_cold last_cold;struct hph_clock last_clock;struct gie_access cxo;
 struct gie_access backup[GIE_TOTAL_WORDS],control_backup[GIE_CONTROL_WORDS];
 struct gie_access program_store[GIE_PROGRAM_WORDS],program_barrier[GIE_PROGRAM_WORDS],program_verify[GIE_PROGRAM_WORDS];
 struct gie_access control_store[GIE_CONTROL_WORDS],control_barrier[GIE_CONTROL_WORDS],control_verify[GIE_CONTROL_WORDS];
 struct gie_access prepared_tcm_verify[GIE_PROGRAM_WORDS],prepared_control_verify[GIE_CONTROL_WORDS];
 struct gie_access release_store,release_barrier,release_verify,reset_store,reset_barrier;
 struct gie_access mailbox_poll[GIE_POLLS],reset_poll[GIE_POLLS],mailbox[GIE_MAIL_WORDS],stack_after[GIE_STACK_WORDS];
 struct gie_access restore_store[GIE_TOTAL_WORDS],restore_barrier[GIE_TOTAL_WORDS],restore_verify[GIE_TOTAL_WORDS];
 struct gie_access control_restore_store[GIE_RESTORE_CONTROLS],control_restore_barrier[GIE_RESTORE_CONTROLS],control_restore_verify[GIE_RESTORE_CONTROLS];
 struct gie_access final_verify[GIE_TOTAL_WORDS],control_final_verify[GIE_CONTROL_WORDS];
};
struct gie_io {
 void *cookie;GPP_U64 (*read)(void *,GPP_U32,GPP_U32 *);GPP_U64 (*write)(void *,GPP_U32,GPP_U32);
 GPP_U64 (*barrier)(void *);GPP_U64 (*delay)(void *,unsigned);
 unsigned (*fresh)(void *,unsigned,struct hph_cold *,struct hph_clock *,GPP_U32 *);
 GPP_U64 (*commit)(void *,unsigned,const struct gie_result *);int (*exclusive)(void *);
};
static inline GPP_U32 gie_address(unsigned i){return hht_address(i);}
static inline GPP_U32 gie_program_value(unsigned i){return hht_program_value(i);}
static inline GPP_U32 gie_control_address(unsigned i){
 static const GPP_U32 a[]={0x3d7e3c0,0x3d7e3c4,0x3d7e004,0x3d7e0b4,0x3d7d03c,0x3d7d000,0x3d7d004,0x3d7e64c,0x3d8ec18};
 return i<GIE_CONTROL_WORDS?a[i]:0;
}
static inline GPP_U32 gie_control_value(unsigned i){
 static const GPP_U32 v[]={1,1,2,0x4052,1,1,1,0xffffffffu,0xffffffffu};return i<GIE_CONTROL_WORDS?v[i]:0;
}
static inline GPP_U32 gie_control_mask(unsigned i){return i==8?0x7fu:0xffffffffu;}
static inline int gie_control_index(GPP_U32 a){for(unsigned i=0;i<9;i++)if(gie_control_address(i)==a)return (int)i;return -1;}
/* Unlike hph_cold_qualified, this validator accepts the REAL reset value for
 * the explicit phase: 1=cold, 0=running, 2=reset-recovery access (0 or 1).
 * Mode2 is not running/cold success and is only used to authorize reset1. */
static inline int gie_state_qualified(const struct hph_cold *s,unsigned reset){
 if(!s || reset>2 || s->valid!=0xffffu || s->hfi ||
  (s->values[HGB_CFG]&0x80000003u)!=1 || (s->values[HGB_MEM]&0x80000003u)!=1 ||
  (s->values[HGB_DVM]&0x80000001u)!=1 || (s->values[HGB_CX]&1) ||
  !(s->values[HGB_HW]&0x80000000u) || (s->values[HGB_AHB]&0x80000000u) ||
  (reset<2?s->values[HGB_RESET]!=reset:s->values[HGB_RESET]>1) ||
  (s->values[HGB_VOTE]&0x80000001u)!=1 || s->values[HGB_ID0]!=0x4c017e18 ||
  s->values[HGB_ID1]!=0x40000016 || s->values[HGB_ID2]!=0x5511 ||
  s->values[HGB_GFSR] || s->values[HGB_FWINIT] || s->values[HGB_GXPOWER]!=3)return 0;
 for(unsigned i=0;i<24;i++)if(s->smr[i] || s->s2cr[i]!=0x20000u)return 0;
 for(unsigned i=0;i<22;i++)if(s->sctlr[i]&1)return 0;
 return 1;
}
static inline unsigned gie_recheck_running(const struct hph_record *r,const struct hph_cold *s,const struct hph_clock *c,unsigned reset){
 if(reset>2 || !hph_record_valid(r) || !r->complete)return HPH_RECORD;
 if(!gie_state_qualified(s,reset) || !hph_hub_cold(c))return HPH_COLD;
 const unsigned p[]={HCW_GMU_CMD,HCW_GMU_CFG,HCW_GMU_BRANCH,HCW_HUB_CMD,HCW_HUB_CFG,HCW_HUB_AON,HCW_HUB_CX,HCW_AHB_DIV,HCW_PLL1_MODE,HCW_PLL1_OPMODE,HCW_PLL1_L,HCW_PLL1_ALPHA,HCW_PLL1_USER};
 for(unsigned at=0;at<2;at++)for(unsigned i=0;i<sizeof(p)/sizeof(p[0]);i++)
  if(c->raw[at][p[i]]!=r->clock.raw[at][p[i]])return HPH_CHANGED;
 if(((c->raw[1][HCW_GMU_CFG]>>8)&7u)==1)for(unsigned at=0;at<2;at++)for(unsigned i=0;i<6;i++)
  if(i!=2 && c->raw[at][HCW_PLL0_MODE+i]!=r->clock.raw[at][HCW_PLL0_MODE+i])return HPH_CHANGED;
 return HPH_OK; /* PLL STATUS is asynchronous, not a programming field. */
}
struct gie_ro_context{const struct hph_ro_io *io;unsigned code,mode;GPP_U32 address;};
static inline int gie_ro_read(void *cookie,GPP_U32 a,GPP_U32 *v){
 struct gie_ro_context *c=(struct gie_ro_context *)cookie;
 if(c->code)return 1;c->address=a;
 if(!hph_read_allowed(a) || c->io->exclusive(c->io->cookie)){c->code=HPH_OWNER;return 1;}
 if(c->io->read(c->io->cookie,a,v) || *v==UINT32_MAX){c->code=HPH_READ;return 1;}return 0;
}
static inline int gie_ro_owned(void *cookie,unsigned mode){
 struct gie_ro_context *c=(struct gie_ro_context *)cookie;
 if(c->code || mode!=c->mode || c->io->exclusive(c->io->cookie)){if(!c->code)c->code=HPH_OWNER;return 1;}return 0;
}
static inline unsigned gie_observe_ro(const struct hph_ro_io *io,unsigned reset,struct hph_cold *s,struct hph_clock *clock,GPP_U32 *failed){
 if(failed)*failed=0;
 if(!io || !io->read || !io->exclusive || !s || !clock || !failed || reset>2)return HPH_ARGUMENT;
 hph_zero(s,sizeof(*s));hph_zero(clock,sizeof(*clock));struct gie_ro_context c={io,0,0,0};
 const GPP_U32 masks[]={0x80000003u,0x80000003u,0x80000001u,1,0x80000000u,0x80000000u,0xffffffffu,0x80000001u};
 const GPP_U32 want[]={1,1,1,0,0x80000000u,0,reset,1};
 for(unsigned i=0;i<HGB_REGS;i++){
  if(gie_ro_read(&c,hgb_address(i),&s->values[i]))goto done;s->valid|=1u<<i;
  if(i<8 && (i==HGB_RESET && reset==2?s->values[i]>1:(s->values[i]&masks[i])!=want[i])){c.code=HPH_COLD;goto done;}
 }
 for(unsigned i=0;i<24;i++)if(gie_ro_read(&c,0x3da0800u+4*i,&s->smr[i]) || gie_ro_read(&c,0x3da0c00u+4*i,&s->s2cr[i]))goto done;
 for(unsigned i=0;i<22;i++)if(gie_ro_read(&c,0x3dc0000u+4096*i,&s->sctlr[i]))goto done;
 if(gie_ro_read(&c,0x3d7e600u,&s->hfi))goto done;s->valid|=HPH_HFI_VALID;
 if(!gie_state_qualified(s,reset)){c.code=HPH_COLD;goto done;}
 {
  c.mode=s->values[HGB_RESET]?0u:1u;struct hcw_result w;hph_zero(&w,sizeof(w));
  const struct hcw_io witness={&c,gie_ro_owned,gie_ro_read,hph_ro_configured};
  unsigned code=hcw_run(&witness,c.mode,&w);hph_clock_copy(clock,&w);
  if(code || !hph_hub_cold(clock))c.code=c.code?c.code:HPH_CLOCK;
 }
 done:if(c.code)*failed=c.address;return c.code;
}
static inline void gie_fail(struct gie_result *r,unsigned code,unsigned op,unsigned index,GPP_U32 a,GPP_U32 expected,GPP_U32 observed,GPP_U64 status){
 if(!r->code){r->code=code;const struct gie_failure first={code,r->phase,op,index,a,expected,observed,status};r->first=first;}
 r->complete=r->safe_to_save=0;
}
static inline int gie_owned(const struct gie_io *io,struct gie_result *r){
 if(r->qualification_lost)return 0;
 if(!io->exclusive(io->cookie))return 1;r->qualification_lost=1;gie_fail(r,GIE_OWNER,GIE_OP_OWNER,0,0,0,0,0);return 0;
}
static inline int gie_read_allowed(GPP_U32 a){return hht_tcm_allowed(a) || gie_control_index(a)>=0 || a==0x3d7e000u || a==0x3d99144u;}
static inline int gie_load(const struct gie_io *io,struct gie_result *r,GPP_U32 a,struct gie_access *x,unsigned index){
 if(!gie_read_allowed(a)){gie_fail(r,GIE_ARGUMENT,GIE_OP_READ,index,a,0,0,0);return 0;}
 if(!gie_owned(io,r))return 0;x->attempted=1;++r->reads;x->status=io->read(io->cookie,a,&x->value);x->returned=1;
 if(x->status){gie_fail(r,GIE_READ,GIE_OP_READ,index,a,0,x->value,x->status);return 0;}x->valid=1;return 1;
}
static inline int gie_guard(const struct gie_io *io,const struct hph_record *record,struct gie_result *r,unsigned reset){
 if(!gie_owned(io,r))return 0;
 ++r->guards;r->guard_reset=reset;r->last_guard_address=0;
 hph_zero(&r->last_cold,sizeof(r->last_cold));hph_zero(&r->last_clock,sizeof(r->last_clock));
 r->last_fresh_code=io->fresh(io->cookie,reset,&r->last_cold,&r->last_clock,&r->last_guard_address);
 if(r->last_fresh_code){r->qualification_lost=1;gie_fail(r,GIE_FRESH,GIE_OP_FRESH,0,r->last_guard_address,0,0,r->last_fresh_code);return 0;}
 r->last_recheck=reset==1?hph_recheck(record,&r->last_cold,&r->last_clock):gie_recheck_running(record,&r->last_cold,&r->last_clock,reset);
 if(r->last_recheck){r->qualification_lost=1;gie_fail(r,GIE_RECHECK,GIE_OP_RECHECK,0,0,0,0,r->last_recheck);return 0;}
 hph_zero(&r->cxo,sizeof(r->cxo));
 if(!gie_load(io,r,0x3d99144u,&r->cxo,0)){r->qualification_lost=1;return 0;}
 if((r->cxo.value&0x80000001u)!=1u){r->qualification_lost=1;gie_fail(r,GIE_CXO,GIE_OP_VERIFY,0,0x3d99144u,1,r->cxo.value,0);return 0;}
 return gie_owned(io,r);
}
static inline GPP_U32 gie_fingerprint(const struct gie_access *x,unsigned words){
 GPP_U32 crc=0xffffffffu;
 for(unsigned i=0;i<words;i++)for(unsigned j=0;j<4;j++){crc^=(x[i].value>>(8*j))&255u;for(unsigned k=0;k<8;k++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}return ~crc;
}
static inline int gie_backup_intact(struct gie_result *r){
 unsigned good=r->backup_words==58 && r->control_backup_words==9;
 for(unsigned i=0;i<58;i++)good&=r->backup[i].attempted==1 && r->backup[i].returned==1 && r->backup[i].valid==1 && !r->backup[i].status;
 for(unsigned i=0;i<9;i++)good&=r->control_backup[i].attempted==1 && r->control_backup[i].returned==1 && r->control_backup[i].valid==1 && !r->control_backup[i].status;
 if(good && r->backup_crc==gie_fingerprint(r->backup,58) && r->control_crc==gie_fingerprint(r->control_backup,9))return 1;
 gie_fail(r,GIE_BACKUP,GIE_OP_VERIFY,0,0,r->backup_crc,gie_fingerprint(r->backup,58),0);return 0;
}
static inline int gie_store_allowed(const struct gie_result *r,GPP_U32 a,GPP_U32 value){
 for(unsigned i=0;i<58;i++)if(a==gie_address(i))return r->phase==GIE_PHASE_PROGRAM?(i<42 && value==gie_program_value(i)):r->phase==GIE_PHASE_RESTORE_TCM && value==r->backup[i].value;
 int i=gie_control_index(a);
 if(i>=0)return r->phase==GIE_PHASE_CONTROL?value==gie_control_value((unsigned)i):r->phase==GIE_PHASE_RESTORE_CONTROL && i<7 && value==r->control_backup[i].value;
 return a==0x3d7e000u && r->possible_execution && ((r->phase==GIE_PHASE_RELEASE && value==0) ||
  (r->phase==GIE_PHASE_RESET && value==1 && r->guard_reset==2));
}
static inline int gie_store(const struct gie_io *io,struct gie_result *r,GPP_U32 a,GPP_U32 value,
 struct gie_access *store,struct gie_access *barrier,struct gie_access *verify,GPP_U32 mask,unsigned index){
 if(!r->backup_committed || !gie_backup_intact(r) || !gie_store_allowed(r,a,value) || !gie_owned(io,r))return 0;
 store->value=value;store->attempted=1;++r->writes;
 if(hht_tcm_allowed(a))r->tcm_retained=1;
 store->status=io->write(io->cookie,a,value);store->returned=1;store->valid=store->status==0;
 if(store->status)gie_fail(r,GIE_WRITE,GIE_OP_WRITE,index,a,value,0,store->status);
 barrier->attempted=1;++r->barriers;barrier->status=io->barrier(io->cookie);barrier->returned=1;barrier->valid=barrier->status==0;
 if(barrier->status){r->barrier_uncertain=1;gie_fail(r,GIE_BARRIER,GIE_OP_BARRIER,index,a,0,0,barrier->status);return 0;}
 if(store->status)return 0;
 if(!verify)return 1;
 if(!gie_load(io,r,a,verify,index))return 0;
 if((verify->value&mask)!=(value&mask)){if(a==0x3d7e000u && verify->value>1)r->qualification_lost=1;gie_fail(r,GIE_VERIFY,GIE_OP_VERIFY,index,a,value,verify->value,0);return 0;}return 1;
}
static inline int gie_commit(const struct gie_io *io,struct gie_result *r,unsigned stage){
 if(!gie_owned(io,r))return 0;++r->commits;GPP_U64 status=io->commit(io->cookie,stage,r);
 if(status){gie_fail(r,GIE_COMMIT,GIE_OP_COMMIT,stage,0,0,0,status);return 0;}return 1;
}
static inline int gie_delay(const struct gie_io *io,struct gie_result *r){
 if(!gie_owned(io,r))return 0;++r->delays;GPP_U64 status=io->delay(io->cookie,10);
 if(status){gie_fail(r,GIE_DELAY,GIE_OP_DELAY,0,0,10,0,status);return 0;}return 1;
}
static inline int gie_access_ok(const struct gie_access *a){return a->attempted==1 && a->returned==1 && a->valid==1 && !a->status;}
static inline int gie_complete_proof(const struct gie_result *r){
 if(!r || r->code || !r->safe_to_save || !r->backup_committed || !r->prepared_committed || !r->restore_committed ||
  !r->possible_execution || !r->reset_held || r->qualification_lost || r->barrier_uncertain || r->tcm_retained ||
  r->backup_words!=58 || r->control_backup_words!=9 || r->programmed_words!=42 || r->control_programmed!=9 ||
  r->restored_words!=58 || r->control_restored!=7 || r->final_verified_words!=58 || r->stack_changed_mask ||
  r->irq_retained!=3 || !r->polls || r->polls>1000 || !r->reset_polls || r->reset_polls>1000 ||
  !gie_access_ok(&r->release_store) || r->release_store.value || !gie_access_ok(&r->release_barrier) ||
  !gie_access_ok(&r->release_verify) || r->release_verify.value || !gie_access_ok(&r->reset_store) || r->reset_store.value!=1 ||
  !gie_access_ok(&r->reset_barrier) || !gie_access_ok(&r->reset_poll[r->reset_polls-1]) || r->reset_poll[r->reset_polls-1].value!=1)return 0;
 for(unsigned i=0;i<58;i++)if(!gie_access_ok(&r->backup[i]) || !gie_access_ok(&r->restore_store[i]) ||
  !gie_access_ok(&r->restore_barrier[i]) || !gie_access_ok(&r->restore_verify[i]) || !gie_access_ok(&r->final_verify[i]) ||
  r->restore_store[i].value!=r->backup[i].value || r->restore_verify[i].value!=r->backup[i].value || r->final_verify[i].value!=r->backup[i].value)return 0;
 for(unsigned i=0;i<42;i++)if(!gie_access_ok(&r->program_store[i]) || !gie_access_ok(&r->program_barrier[i]) ||
  !gie_access_ok(&r->program_verify[i]) || !gie_access_ok(&r->prepared_tcm_verify[i]) || r->program_store[i].value!=gie_program_value(i) ||
  r->program_verify[i].value!=gie_program_value(i) || r->prepared_tcm_verify[i].value!=gie_program_value(i))return 0;
 for(unsigned i=0;i<9;i++)if(!gie_access_ok(&r->control_backup[i]) || !gie_access_ok(&r->control_store[i]) ||
  !gie_access_ok(&r->control_barrier[i]) || !gie_access_ok(&r->control_verify[i]) || !gie_access_ok(&r->prepared_control_verify[i]) ||
  !gie_access_ok(&r->control_final_verify[i]) || r->control_store[i].value!=gie_control_value(i) ||
  (r->control_verify[i].value&gie_control_mask(i))!=(gie_control_value(i)&gie_control_mask(i)) ||
  (r->prepared_control_verify[i].value&gie_control_mask(i))!=(gie_control_value(i)&gie_control_mask(i)))return 0;
 for(unsigned i=0;i<7;i++)if(!gie_access_ok(&r->control_restore_store[i]) || !gie_access_ok(&r->control_restore_barrier[i]) ||
  !gie_access_ok(&r->control_restore_verify[i]) || r->control_restore_store[i].value!=r->control_backup[i].value ||
  r->control_restore_verify[i].value!=r->control_backup[i].value || r->control_final_verify[i].value!=r->control_backup[i].value)return 0;
 for(unsigned i=7;i<9;i++)if((r->control_final_verify[i].value&gie_control_mask(i))!=(gie_control_value(i)&gie_control_mask(i)))return 0;
 for(unsigned i=0;i<16;i++)if(!gie_access_ok(&r->stack_after[i]) || r->stack_after[i].value!=r->backup[42+i].value)return 0;
 for(unsigned i=0;i<r->polls;i++)if(!gie_access_ok(&r->mailbox_poll[i]) ||
  (i+1==r->polls?r->mailbox_poll[i].value!=0xa7500002u:(r->mailbox_poll[i].value!=0 && r->mailbox_poll[i].value!=0xa7500001u)))return 0;
 for(unsigned i=0;i<r->reset_polls;i++)if(!gie_access_ok(&r->reset_poll[i]) || r->reset_poll[i].value!=(i+1==r->reset_polls?1u:0u))return 0;
 for(unsigned i=0;i<8;i++){GPP_U32 wanted=i==0?0xa7500002u:i==1?0x750c0de0u:i==2?128u:0u;if(!gie_access_ok(&r->mailbox[i]) || r->mailbox[i].value!=wanted)return 0;}
 return r->backup_crc==gie_fingerprint(r->backup,58) && r->control_crc==gie_fingerprint(r->control_backup,9);
}
static inline unsigned gie_run(const struct gie_io *io,const struct hph_record *record,
 const struct hph_cold *current,const struct hph_clock *current_clock,struct gie_result *r){
 if(!r)return GIE_ARGUMENT;if(r->started)return GIE_REUSED;
 hph_zero(r,sizeof(*r));r->started=1;r->phase=GIE_PHASE_INPUT;r->retained=1;
 if(!io || !io->read || !io->write || !io->barrier || !io->delay || !io->fresh || !io->commit || !io->exclusive || !record || !current || !current_clock){gie_fail(r,GIE_ARGUMENT,GIE_OP_RECHECK,0,0,0,0,0);return r->code;}
 r->record_crc=record->crc32;r->input_recheck=hph_recheck(record,current,current_clock);
 if(r->input_recheck){gie_fail(r,GIE_INPUT,GIE_OP_RECHECK,0,0,0,0,r->input_recheck);return r->code;}
 if(!gie_guard(io,record,r,1))return r->code;
 r->reset_held=1;r->phase=GIE_PHASE_BACKUP;
 for(unsigned i=0;i<58;i++){if(!gie_load(io,r,gie_address(i),&r->backup[i],i))return r->code;++r->backup_words;}
 for(unsigned i=0;i<9;i++){if(!gie_load(io,r,gie_control_address(i),&r->control_backup[i],i))return r->code;++r->control_backup_words;}
 r->backup_crc=gie_fingerprint(r->backup,58);r->control_crc=gie_fingerprint(r->control_backup,9);r->phase=GIE_PHASE_BACKUP_COMMIT;
 if(!gie_guard(io,record,r,1))return r->code;
 {int committed=gie_commit(io,r,1);if(!gie_guard(io,record,r,1) || !committed || !gie_backup_intact(r))return r->code;}
 r->backup_committed=1;r->phase=GIE_PHASE_PROGRAM;
 for(unsigned i=0;i<42;i++){if(!gie_store(io,r,gie_address(i),gie_program_value(i),&r->program_store[i],&r->program_barrier[i],&r->program_verify[i],0xffffffffu,i))goto cleanup;++r->programmed_words;}
 r->phase=GIE_PHASE_CONTROL;if(!gie_guard(io,record,r,1))return r->code;
 {const unsigned order[]={1,0,2,3,4,5,6,7,8};for(unsigned n=0;n<9;n++){unsigned i=order[n];if(!gie_store(io,r,gie_control_address(i),gie_control_value(i),&r->control_store[i],&r->control_barrier[i],&r->control_verify[i],gie_control_mask(i),i))goto cleanup;++r->control_programmed;}}
 r->phase=GIE_PHASE_PREPARED_COMMIT;if(!gie_guard(io,record,r,1))return r->code;
 {int committed=gie_commit(io,r,2);if(!gie_guard(io,record,r,1) || !committed)goto cleanup;}
 for(unsigned i=0;i<42;i++){
  if(!gie_load(io,r,gie_address(i),&r->prepared_tcm_verify[i],i))goto cleanup;
  if(r->prepared_tcm_verify[i].value!=gie_program_value(i)){gie_fail(r,GIE_VERIFY,GIE_OP_VERIFY,i,gie_address(i),gie_program_value(i),r->prepared_tcm_verify[i].value,0);goto cleanup;}
 }
 for(unsigned i=0;i<9;i++){
  if(!gie_load(io,r,gie_control_address(i),&r->prepared_control_verify[i],i))goto cleanup;
  if((r->prepared_control_verify[i].value&gie_control_mask(i))!=(gie_control_value(i)&gie_control_mask(i))){gie_fail(r,GIE_VERIFY,GIE_OP_VERIFY,i,gie_control_address(i),gie_control_value(i),r->prepared_control_verify[i].value,0);goto cleanup;}
 }
 r->prepared_committed=1;r->phase=GIE_PHASE_RELEASE;r->possible_execution=1;r->reset_held=0;
 if(!gie_store(io,r,0x3d7e000u,0,&r->release_store,&r->release_barrier,&r->release_verify,0xffffffffu,0))goto cleanup;
 r->phase=GIE_PHASE_POLL;
 for(unsigned i=0;i<1000;i++){
  if(!gie_guard(io,record,r,0))return r->code;
  r->polls=i+1;if(!gie_load(io,r,0x3d71000u,&r->mailbox_poll[i],i))goto cleanup;
  if(r->mailbox_poll[i].value==0xa7500002u)break;
  if(r->mailbox_poll[i].value==0xa750deadu){gie_fail(r,GIE_FAULT,GIE_OP_VERIFY,i,0x3d71000u,0xa7500002u,0xa750deadu,0);for(unsigned j=0;j<8;j++)if(!gie_load(io,r,0x3d71000u+4*j,&r->mailbox[j],j))break;goto cleanup;}
  if(r->mailbox_poll[i].value!=0 && r->mailbox_poll[i].value!=0xa7500001u){gie_fail(r,GIE_VERIFY,GIE_OP_VERIFY,i,0x3d71000u,0xa7500002u,r->mailbox_poll[i].value,0);goto cleanup;}
  if(i==999){gie_fail(r,GIE_TIMEOUT,GIE_OP_VERIFY,i,0x3d71000u,0xa7500002u,r->mailbox_poll[i].value,0);goto cleanup;}
  if(!gie_delay(io,r))goto cleanup;
 }
 for(unsigned i=0;i<8;i++)if(!gie_load(io,r,0x3d71000u+4*i,&r->mailbox[i],i))goto cleanup;
 for(unsigned i=0;i<8;i++){
  GPP_U32 wanted=i==0?0xa7500002u:i==1?0x750c0de0u:i==2?128u:0u;
  if(r->mailbox[i].value!=wanted)gie_fail(r,GIE_VERIFY,GIE_OP_VERIFY,i,0x3d71000u+4*i,wanted,r->mailbox[i].value,0);
 }
 cleanup:
 if(r->qualification_lost || r->barrier_uncertain)return r->code;
 if(r->possible_execution){
  r->phase=GIE_PHASE_RESET;if(!gie_guard(io,record,r,2))return r->code;
  (void)gie_store(io,r,0x3d7e000u,1,&r->reset_store,&r->reset_barrier,0,0xffffffffu,0);
  if(r->qualification_lost || r->barrier_uncertain)return r->code;
  r->phase=GIE_PHASE_RESET_POLL;
  for(unsigned i=0;i<1000;i++){
   if(!gie_guard(io,record,r,2))return r->code;
   r->reset_polls=i+1;if(!gie_load(io,r,0x3d7e000u,&r->reset_poll[i],i))return r->code;
   if(r->reset_poll[i].value==1){r->reset_held=1;break;}
   if(r->reset_poll[i].value>1){r->qualification_lost=1;gie_fail(r,GIE_VERIFY,GIE_OP_VERIFY,i,0x3d7e000u,1,r->reset_poll[i].value,0);return r->code;}
   if(i==999){gie_fail(r,GIE_TIMEOUT,GIE_OP_VERIFY,i,0x3d7e000u,1,r->reset_poll[i].value,0);return r->code;}
   if(!gie_delay(io,r))return r->code;
  }
 }
 if(!gie_guard(io,record,r,1) || !gie_backup_intact(r))return r->code;
 r->reset_held=1;r->phase=GIE_PHASE_STACK;
 for(unsigned i=0;i<16;i++){if(!gie_load(io,r,gie_address(42+i),&r->stack_after[i],42+i))return r->code;if(r->stack_after[i].value!=r->backup[42+i].value)r->stack_changed_mask|=1u<<i;}
 if(r->stack_changed_mask && !r->code)gie_fail(r,GIE_VERIFY,GIE_OP_VERIFY,42,gie_address(42),0,r->stack_changed_mask,0);
 r->phase=GIE_PHASE_RESTORE_TCM;if(!gie_guard(io,record,r,1))return r->code;
 for(unsigned i=0;i<58;i++){if(!gie_store(io,r,gie_address(i),r->backup[i].value,&r->restore_store[i],&r->restore_barrier[i],&r->restore_verify[i],0xffffffffu,i))return r->code;++r->restored_words;}
 r->phase=GIE_PHASE_RESTORE_CONTROL;if(!gie_guard(io,record,r,1))return r->code;
 for(unsigned n=7;n>0;n--){unsigned i=n-1;if(!gie_store(io,r,gie_control_address(i),r->control_backup[i].value,&r->control_restore_store[i],&r->control_restore_barrier[i],&r->control_restore_verify[i],0xffffffffu,i))return r->code;++r->control_restored;}
 r->phase=GIE_PHASE_FINAL_VERIFY;if(!gie_guard(io,record,r,1))return r->code;
 for(unsigned i=0;i<58;i++){if(!gie_load(io,r,gie_address(i),&r->final_verify[i],i))return r->code;if(r->final_verify[i].value!=r->backup[i].value){gie_fail(r,GIE_VERIFY,GIE_OP_VERIFY,i,gie_address(i),r->backup[i].value,r->final_verify[i].value,0);return r->code;}++r->final_verified_words;}
 for(unsigned i=0;i<9;i++){
  if(!gie_load(io,r,gie_control_address(i),&r->control_final_verify[i],i))return r->code;
  unsigned keep=i>=7 && r->control_store[i].attempted;GPP_U32 wanted=keep?gie_control_value(i):r->control_backup[i].value;
  GPP_U32 mask=keep?gie_control_mask(i):0xffffffffu;
  if((r->control_final_verify[i].value&mask)!=(wanted&mask)){gie_fail(r,GIE_VERIFY,GIE_OP_VERIFY,i,gie_control_address(i),wanted,r->control_final_verify[i].value,0);return r->code;}
  if(keep)r->irq_retained|=1u<<(i-7);
 }
 r->tcm_retained=0;r->phase=GIE_PHASE_FINAL_COMMIT;if(!gie_guard(io,record,r,1))return r->code;
 {int committed=gie_commit(io,r,3);if(!gie_guard(io,record,r,1) || !committed || !gie_backup_intact(r))return r->code;}
 r->restore_committed=1;r->safe_to_save=1;r->complete=gie_complete_proof(r)?1u:0u;
 r->phase=GIE_PHASE_DONE;return r->code;
}
#endif
