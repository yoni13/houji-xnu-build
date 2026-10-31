/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * CANDIDATE: XNU-owned Device access to held-reset GMU internal TCM only.
 * No reset/control/IRQ/RCG/SMMU/external-RAM write or CM3 execution exists.
 *
 * Fixed bytes: native/gpu/uefi/GpuGmuCode.h SHA256
 * 3250956997b9d1356fe2713e3dc48e2bda61876e8fd207fbb3d4753848ebffc5,
 * embedded 136-byte payload SHA256
 * 6c1d70a8de298accbb03a8dd39cc5aaa289b741cb7a7ab3d176261d2cdb795e0;
 * native/gpu/firmware/houji_gmu_tcm_probe.S source SHA256
 * 4a3cfdcce7677cb68061748e438a072ecf969718f35f47220216c48cdf42ec43.
 * The old hgd_tcm_address/guard bind the same 34+8+16 words and CXO gate.
 * Only those bytes/addresses are reused, never hgd_probe or its CM3 controls.
 *
 * fresh() must perform NEW, silent hph_observe_ro loads through XNU-owned
 * Device mappings, not copy an EFI/previous snapshot or synthesize hgb state.
 * This core explicitly hph_recheck()s every fresh observation, then loads CXO.
 * commit(stage1) durably records all original words before any TCM store;
 * stage2 records 42 restored words and an independent 58-word final audit. Each commit returns to another fresh
 * guard. There is no lock across commit; exclusive is software ownership only.
 * Unknown qualification stops all further TCM accesses and retains originals.
 * Returned/valid record real callback return, never an access intent as proof.
 */
#ifndef HOUJI_GMU_HELD_TCM_H
#define HOUJI_GMU_HELD_TCM_H
#include "GpuPowerOnlyHandoff.h"
#include "GpuGmuCode.h"
enum {HHT_CODE_WORDS=34,HHT_MAIL_WORDS=8,HHT_STACK_WORDS=16,
 HHT_PROGRAM_WORDS=42,HHT_TOTAL_WORDS=58};
enum {HHT_OK,HHT_ARGUMENT,HHT_REUSED,HHT_INPUT,HHT_OWNER,HHT_FRESH,
 HHT_RECHECK,HHT_CXO,HHT_READ,HHT_WRITE,HHT_BARRIER,HHT_VERIFY,HHT_COMMIT,HHT_BACKUP};
enum {HHT_PHASE_INPUT=1,HHT_PHASE_BACKUP,HHT_PHASE_BACKUP_COMMIT,
 HHT_PHASE_PROGRAM,HHT_PHASE_STACK,HHT_PHASE_RESTORE,HHT_PHASE_FINAL_VERIFY,HHT_PHASE_FINAL_COMMIT,HHT_PHASE_DONE};
enum {HHT_OP_READ=1,HHT_OP_WRITE,HHT_OP_BARRIER,HHT_OP_FRESH,
 HHT_OP_RECHECK,HHT_OP_COMMIT,HHT_OP_OWNER,HHT_OP_VERIFY};
struct hht_access {GPP_U32 attempted,returned,valid,value;GPP_U64 status;};
struct hht_failure {
 unsigned code,phase,operation,index;
 GPP_U32 address,expected,observed;GPP_U64 status;
};
struct hht_result {
 unsigned started,phase,code,complete,safe_to_save,retained,tcm_retained;
 unsigned reads,writes,barriers,guards,commits,backup_words,programmed_words,stack_checked,restored_words,final_verified_words;
 unsigned input_recheck,last_fresh_code,last_recheck,backup_committed,restore_committed,barrier_uncertain,qualification_lost;
 GPP_U32 record_crc,backup_crc,last_guard_address;
 struct hht_failure first;
 struct hph_cold last_cold;
 struct hph_clock last_clock;
 struct hht_access cxo;
 struct hht_access backup[HHT_TOTAL_WORDS];
 struct hht_access program_store[HHT_PROGRAM_WORDS],program_barrier[HHT_PROGRAM_WORDS],program_verify[HHT_PROGRAM_WORDS];
 struct hht_access stack_verify[HHT_STACK_WORDS];
 struct hht_access restore_store[HHT_PROGRAM_WORDS],restore_barrier[HHT_PROGRAM_WORDS],restore_verify[HHT_PROGRAM_WORDS];
 struct hht_access final_verify[HHT_TOTAL_WORDS];
};
struct hht_io {
 void *cookie;
 GPP_U64 (*read)(void *,GPP_U32,GPP_U32 *);
 GPP_U64 (*write)(void *,GPP_U32,GPP_U32);
 GPP_U64 (*barrier)(void *);
 unsigned (*fresh)(void *,struct hph_cold *,struct hph_clock *,GPP_U32 *failed_address);
 GPP_U64 (*commit)(void *,unsigned stage,const struct hht_result *);
 int (*exclusive)(void *);
};
static inline GPP_U32 hht_address(unsigned i) {
 if(i<HHT_CODE_WORDS)return 0x3d6d000u+4u*i;
 if(i<HHT_PROGRAM_WORDS)return 0x3d71000u+4u*(i-HHT_CODE_WORDS);
 return i<HHT_TOTAL_WORDS?0x3d74f40u+4u*(i-HHT_PROGRAM_WORDS):0;
}
static inline int hht_tcm_allowed(GPP_U32 address) {
 if(address&3u)return 0;
 for(unsigned i=0;i<HHT_TOTAL_WORDS;i++)if(hht_address(i)==address)return 1;
 return 0;
}
static inline GPP_U32 hht_program_value(unsigned i) {return i<HHT_CODE_WORDS?hgd_code[i]:0;}
static inline void hht_fail(struct hht_result *r,unsigned code,unsigned op,unsigned i,
 GPP_U32 address,GPP_U32 expected,GPP_U32 observed,GPP_U64 status) {
 if(!r->code) {
  r->code=code;
  const struct hht_failure first={code,r->phase,op,i,address,expected,observed,status};r->first=first;
 }
 r->complete=0;r->safe_to_save=0;
}
static inline int hht_owned(const struct hht_io *io,struct hht_result *r) {
 if(r->qualification_lost)return 0;
 if(!io->exclusive(io->cookie))return 1;
 r->qualification_lost=1;hht_fail(r,HHT_OWNER,HHT_OP_OWNER,0,0,0,0,0);return 0;
}
static inline int hht_load(const struct hht_io *io,struct hht_result *r,
 GPP_U32 address,struct hht_access *a,unsigned index) {
 if(address!=0x3d99144u && !hht_tcm_allowed(address)) {hht_fail(r,HHT_ARGUMENT,HHT_OP_READ,index,address,0,0,0);return 0;}
 if(!hht_owned(io,r))return 0;
 a->attempted=1;++r->reads;
 a->status=io->read(io->cookie,address,&a->value);a->returned=1;
 if(a->status) {hht_fail(r,HHT_READ,HHT_OP_READ,index,address,0,a->value,a->status);return 0;}
 a->valid=1;return 1; /* UINT32_MAX is legal TCM data. */
}
static inline int hht_guard(const struct hht_io *io,const struct hph_record *record,
 struct hht_result *r) {
 if(!hht_owned(io,r))return 0;
 ++r->guards;r->last_guard_address=0;
 hph_zero(&r->last_cold,sizeof(r->last_cold));hph_zero(&r->last_clock,sizeof(r->last_clock));
 r->last_fresh_code=io->fresh(io->cookie,&r->last_cold,&r->last_clock,&r->last_guard_address);
 if(r->last_fresh_code) {
  r->qualification_lost=1;hht_fail(r,HHT_FRESH,HHT_OP_FRESH,0,r->last_guard_address,0,0,r->last_fresh_code);return 0;
 }
 r->last_recheck=hph_recheck(record,&r->last_cold,&r->last_clock);
 if(r->last_recheck) {r->qualification_lost=1;hht_fail(r,HHT_RECHECK,HHT_OP_RECHECK,0,0,0,0,r->last_recheck);return 0;}
 if(!hht_owned(io,r))return 0;
 hph_zero(&r->cxo,sizeof(r->cxo));
 if(!hht_load(io,r,0x3d99144u,&r->cxo,0)) {r->qualification_lost=1;return 0;}
 if((r->cxo.value&0x80000001u)!=1u) {
  r->qualification_lost=1;hht_fail(r,HHT_CXO,HHT_OP_VERIFY,0,0x3d99144u,1,r->cxo.value,0);return 0;
 }
 return hht_owned(io,r);
}
static inline GPP_U32 hht_backup_fingerprint(const struct hht_result *r) {
 GPP_U32 crc=0xffffffffu;
 for(unsigned i=0;i<HHT_TOTAL_WORDS;i++)for(unsigned j=0;j<4;j++) {
  crc^=(r->backup[i].value>>(8u*j))&0xffu;
  for(unsigned k=0;k<8;k++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
 }
 return ~crc;
}
static inline int hht_backup_intact(struct hht_result *r) {
 if(r->backup_words==HHT_TOTAL_WORDS) {
  unsigned i;for(i=0;i<HHT_TOTAL_WORDS;i++) {
   const struct hht_access *a=&r->backup[i];
   if(a->attempted!=1 || a->returned!=1 || a->valid!=1 || a->status)break;
  }
  if(i==HHT_TOTAL_WORDS && r->backup_crc==hht_backup_fingerprint(r))return 1;
 }
 hht_fail(r,HHT_BACKUP,HHT_OP_VERIFY,0,0,r->backup_crc,hht_backup_fingerprint(r),0);return 0;
}
static inline int hht_store_word(const struct hht_io *io,struct hht_result *r,unsigned i,unsigned restore) {
 if(i>=HHT_PROGRAM_WORDS ||
  r->phase!=(restore?HHT_PHASE_RESTORE:HHT_PHASE_PROGRAM) ||
  !r->backup_committed || !hht_backup_intact(r) || !hht_owned(io,r))return 0;
 struct hht_access *store=restore?&r->restore_store[i]:&r->program_store[i];
 struct hht_access *barrier=restore?&r->restore_barrier[i]:&r->program_barrier[i];
 struct hht_access *verify=restore?&r->restore_verify[i]:&r->program_verify[i];
 const GPP_U32 address=hht_address(i),wanted=restore?r->backup[i].value:hht_program_value(i);
 store->value=wanted;store->attempted=1;++r->writes;r->tcm_retained=1;
 store->status=io->write(io->cookie,address,wanted);store->returned=1;store->valid=store->status==0;
 if(store->status)hht_fail(r,HHT_WRITE,HHT_OP_WRITE,i,address,wanted,0,store->status);
 /* Always drain a returned store, even if its transport reports failure.
  * barrier is CPU-only, not another MMIO access or an ownership release. */
 barrier->attempted=1;++r->barriers;barrier->status=io->barrier(io->cookie);barrier->returned=1;
 barrier->valid=barrier->status==0;
 if(barrier->status) {
  r->barrier_uncertain=1;hht_fail(r,HHT_BARRIER,HHT_OP_BARRIER,i,address,0,0,barrier->status);return 0;
 }
 if(store->status)return 0;
 if(!hht_load(io,r,address,verify,i))return 0;
 if(verify->value!=wanted) {hht_fail(r,HHT_VERIFY,HHT_OP_VERIFY,i,address,wanted,verify->value,0);return 0;}
 return 1;
}
static inline int hht_commit(const struct hht_io *io,struct hht_result *r,unsigned stage) {
 if(!hht_owned(io,r))return 0;
 ++r->commits;GPP_U64 status=io->commit(io->cookie,stage,r);
 if(status) {hht_fail(r,HHT_COMMIT,HHT_OP_COMMIT,stage,0,0,0,status);return 0;}
 return 1;
}
static inline unsigned hgd_handoff_tcm_held_reset(const struct hht_io *io,
 const struct hph_record *record,const struct hph_cold *current,
 const struct hph_clock *current_clock,struct hht_result *r) {
 if(!r)return HHT_ARGUMENT;
 if(r->started)return HHT_REUSED; /* Do not erase a retained backup on replay. */
 hph_zero(r,sizeof(*r));r->started=1;r->phase=HHT_PHASE_INPUT;r->retained=1;
 if(!io || !io->read || !io->write || !io->barrier || !io->fresh ||
  !io->commit || !io->exclusive || !record || !current || !current_clock ||
  sizeof(hgd_code)!=HHT_CODE_WORDS*4u) {
  hht_fail(r,HHT_ARGUMENT,HHT_OP_RECHECK,0,0,0,0,0);return r->code;
 }
 r->record_crc=record->crc32;r->input_recheck=hph_recheck(record,current,current_clock);
 if(r->input_recheck) {hht_fail(r,HHT_INPUT,HHT_OP_RECHECK,0,0,0,0,r->input_recheck);return r->code;}
 if(!hht_guard(io,record,r))return r->code;
 r->phase=HHT_PHASE_BACKUP;
 for(unsigned i=0;i<HHT_TOTAL_WORDS;i++) {
  if(!hht_load(io,r,hht_address(i),&r->backup[i],i))return r->code;
  ++r->backup_words;
 }
 r->backup_crc=hht_backup_fingerprint(r);r->phase=HHT_PHASE_BACKUP_COMMIT;
 if(!hht_guard(io,record,r))return r->code;
 const int backup_commit=hht_commit(io,r,1);
 /* A commit may run other firmware/kernel events; never reuse its old gate. */
 if(!hht_guard(io,record,r) || !backup_commit || !hht_backup_intact(r))return r->code;
 r->backup_committed=1;r->phase=HHT_PHASE_PROGRAM;
 for(unsigned i=0;i<HHT_PROGRAM_WORDS;i++) {
  if(!hht_store_word(io,r,i,0))goto restore;
  ++r->programmed_words;
 }
 r->phase=HHT_PHASE_STACK;
 if(!hht_guard(io,record,r))return r->code;
 for(unsigned i=0;i<HHT_STACK_WORDS;i++) {
  unsigned index=HHT_PROGRAM_WORDS+i;
  if(!hht_load(io,r,hht_address(index),&r->stack_verify[i],index))goto restore;
  if(r->stack_verify[i].value!=r->backup[index].value) {
   hht_fail(r,HHT_VERIFY,HHT_OP_VERIFY,index,hht_address(index),r->backup[index].value,r->stack_verify[i].value,0);goto restore;
  }
  ++r->stack_checked;
 }
restore:
 if(r->barrier_uncertain)return r->code;
 r->phase=HHT_PHASE_RESTORE;
 if(!hht_guard(io,record,r) || !hht_backup_intact(r))return r->code;
 for(unsigned i=0;i<HHT_PROGRAM_WORDS;i++) {
  if(!hht_store_word(io,r,i,1))return r->code;
  ++r->restored_words;
 }
 // Stack was never stored, including during restoration. Independently audit
 // ALL 58 originals after the last restore store, not only per-store echoes.
 r->phase=HHT_PHASE_FINAL_VERIFY;
 if(!hht_guard(io,record,r))return r->code;
 for(unsigned i=0;i<HHT_TOTAL_WORDS;i++) {
  if(!hht_load(io,r,hht_address(i),&r->final_verify[i],i))return r->code;
  if(r->final_verify[i].value!=r->backup[i].value) {
   hht_fail(r,HHT_VERIFY,HHT_OP_VERIFY,i,hht_address(i),r->backup[i].value,r->final_verify[i].value,0);return r->code;
  }
  ++r->final_verified_words;
 }
 r->tcm_retained=0;r->phase=HHT_PHASE_FINAL_COMMIT;
 if(!hht_guard(io,record,r))return r->code;
 const int restore_commit=hht_commit(io,r,2);
 if(!hht_guard(io,record,r) || !restore_commit || !hht_backup_intact(r))return r->code;
 r->restore_committed=1;r->safe_to_save=1;
 r->complete=!r->code && r->programmed_words==HHT_PROGRAM_WORDS && r->stack_checked==HHT_STACK_WORDS;
 r->phase=HHT_PHASE_DONE;return r->code;
}
#endif
