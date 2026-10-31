/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Candidate: independent XNU internal-only CM3 execution, no firmware/DDR/DMA.
 * The scalar /chosen record conveys no UEFI pointer or ownership. Private
 * Device maps and original values remain retained until platform reset.
 * Include after normal-context houji_ufs_checkpoint and its logging flags.
 * False MUST select the armed no-FS panic/reset path before another checkpoint.
 * A bounded software poll cannot recover a CPU-stalled MMIO instruction.
 */
#ifndef HOUJI_GPU_INTERNAL_EXEC_PROBE_INC
#define HOUJI_GPU_INTERNAL_EXEC_PROBE_INC
#include "GmuInternalExec.h"
#ifndef HOUJI_GPU_INTERNAL_EXEC_PROBE_HOST_TEST
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSSharedPtr.h>
#include <pexpert/pexpert.h>
#endif
enum {GIEN_WINDOWS=6,GIEN_FRESH_SLOTS=32,GIEN_FRESH_LIMIT=2*GIE_POLLS+32};
enum {GIEN_OK,GIEN_FLAGS,GIEN_PROPERTY,GIEN_RECORD,GIEN_OWNER,GIEN_ORDER,
 GIEN_ADDRESS,GIEN_CAPACITY,GIEN_ALLOC,GIEN_MAP,GIEN_MAPPING,GIEN_SENTINEL,
 GIEN_VALUE,GIEN_PHASE,GIEN_COMMIT,GIEN_DELAY};
static const uint64_t kHoujiInternalBase[GIEN_WINDOWS]={0x170000,0x3d90000,0x3d7c000,0x3da0000,0x3d6c000,0x3d8c000};
static const uint64_t kHoujiInternalBytes[GIEN_WINDOWS]={0x4000,0x10000,0x4000,0x40000,0x10000,0x4000};
static unsigned gHoujiGpuInternalExecOnce;
static struct {
 OSSharedPtr<IOMemoryMap> maps[GIEN_WINDOWS];
 struct hph_record record;struct gie_result result;
 struct Fresh {
  struct hph_cold cold;struct hph_clock clock;
  unsigned sequence,mode,attempted,returned,valid,code,recheck,reads,valid_reads,gate_valid,failed_address;
 } fresh[GIEN_FRESH_SLOTS];
 unsigned enabled,active,decoded,record_ready,in_fresh,in_commit,fresh_count,current_fresh,fresh_ready,fresh_mode;
 unsigned phase,code,core_called,core_code,cxo_valid,cxo_raw,property_bytes;
 unsigned read_calls,tcm_reads,control_reads,tcm_writes,control_writes,reset_writes,read_barriers,write_barriers,barriers,delay_calls;
 unsigned map_attempted,map_valid,commit_attempted,commit_returned,last_commit_stage,pending_barrier;
 unsigned first_io_code,first_io_phase,first_io_address,first_io_expected,first_io_observed;
 unsigned last_address,last_value,store_seen[2][GIE_TOTAL_WORDS],control_seen[2][GIE_CONTROL_WORDS],release_seen,reset_seen;
} gHoujiGpuInternalExec;
#ifdef HOUJI_GPU_INTERNAL_EXEC_PROBE_HOST_TEST
static void houjiGpuInternalReadBarrier(void);
static void houjiGpuInternalWriteBarrier(void);
static void houjiGpuInternalFullBarrier(void);
#else
static void houjiGpuInternalReadBarrier(void){__asm__ volatile("dmb oshld":::"memory");}
static void houjiGpuInternalWriteBarrier(void){__asm__ volatile("dsb sy":::"memory");}
static void houjiGpuInternalFullBarrier(void){__asm__ volatile("dsb sy":::"memory");}
#endif
static unsigned houjiGpuInternalFail(unsigned code,uint32_t address,uint32_t expected=0,uint32_t observed=0) {
 auto&p=gHoujiGpuInternalExec;
 if(!p.first_io_code){p.first_io_code=code;p.first_io_phase=p.result.phase;p.first_io_address=address;p.first_io_expected=expected;p.first_io_observed=observed;}
 return code;
}
/* Software single owner/configuration, not a hardware lease. Actual fresh
 * power/clock/reset/FAULT-all stream/bank observations separately authorize I/O. */
static int houjiGpuInternalExclusive(void*cookie) {
 const auto&p=gHoujiGpuInternalExec;
 return cookie!=&gHoujiGpuInternalExec || !p.active || !p.decoded || !p.record_ready || p.in_commit || p.result.qualification_lost ||
  !gRootEnabled || !gLogEnabled || !gReturnRecovery ||
  __atomic_load_n(&gHoujiGpuInternalExecOnce,__ATOMIC_ACQUIRE)!=1;
}
static int houjiGpuInternalWindow(uint32_t address) {
 for(unsigned i=0;i<GIEN_WINDOWS;i++)if(address>=kHoujiInternalBase[i] && address-kHoujiInternalBase[i]<=kHoujiInternalBytes[i]-4)return (int)i;
 return -1;
}
static volatile uint32_t *houjiGpuInternalPointer(uint32_t address,unsigned window,unsigned *status) {
 auto&p=gHoujiGpuInternalExec;*status=0;
 if(window>=GIEN_WINDOWS || (address&3) || address<kHoujiInternalBase[window] || address-kHoujiInternalBase[window]>kHoujiInternalBytes[window]-4) {
  *status=houjiGpuInternalFail(GIEN_ADDRESS,address);return nullptr;
 }
 if(!p.maps[window]) {
  p.map_attempted|=1u<<window;
  IODeviceMemory *memory=IODeviceMemory::withRange(kHoujiInternalBase[window],kHoujiInternalBytes[window]);
  if(!memory){*status=houjiGpuInternalFail(GIEN_ALLOC,address);return nullptr;}
  const unsigned options=kIOMapAnywhere|kIOMapInhibitCache|((window==2 || window==4 || window==5)?0u:(unsigned)kIOMapReadOnly);
  p.maps[window]=OSSharedPtr<IOMemoryMap>(memory->map(options),OSNoRetain);memory->release();
  if(!p.maps[window]){*status=houjiGpuInternalFail(GIEN_MAP,address);return nullptr;}
 }
 const uint64_t va=p.maps[window]->getVirtualAddress(),length=p.maps[window]->getLength(),offset=address-kHoujiInternalBase[window];
 if(!va || (va&3) || length<kHoujiInternalBytes[window] || va>UINT64_MAX-offset-4) {
  *status=houjiGpuInternalFail(GIEN_MAPPING,address);return nullptr;
 }
 p.map_valid|=1u<<window;return reinterpret_cast<volatile uint32_t *>((uintptr_t)(va+offset));
}
static unsigned houjiGpuInternalRequired(uint32_t address) {
 for(unsigned i=0;i<8;i++)if(address==hgb_address(i))return (1u<<i)-1;
 return 0xff;
}
static void houjiGpuInternalGate(uint32_t address,uint32_t value) {
 auto&p=gHoujiGpuInternalExec;auto&s=p.fresh[p.current_fresh];
 const uint32_t masks[]={0x80000003u,0x80000003u,0x80000001u,1,0x80000000u,0x80000000u,0xffffffffu,0x80000001u};
 const uint32_t want[]={1,1,1,0,0x80000000u,0,s.mode,1};
 for(unsigned i=0;i<8;i++)if(address==hgb_address(i)) {
  const bool good=i==HGB_RESET && s.mode==2?value<=1:(value&masks[i])==want[i];
  if(good)s.gate_valid|=1u<<i;else s.gate_valid&=~(1u<<i);return;
 }
}
/* ONLY the real quiet observer may use this callback. State/clock all-ones
 * rejection deliberately does not extend to TCM/control/IRQ original data. */
static GPP_U64 houjiGpuInternalFreshRead(void*cookie,GPP_U32 address,GPP_U32*value) {
 auto&p=gHoujiGpuInternalExec;
 if(!value || !p.in_fresh || !p.fresh_count || !hph_read_allowed(address) || (address&3))return houjiGpuInternalFail(GIEN_ADDRESS,address);
 if(houjiGpuInternalExclusive(cookie))return houjiGpuInternalFail(GIEN_OWNER,address);
 auto&s=p.fresh[p.current_fresh];const unsigned required=houjiGpuInternalRequired(address);
 if((s.gate_valid&required)!=required)return houjiGpuInternalFail(GIEN_ORDER,address);
 int window=houjiGpuInternalWindow(address);if(window<0 || window==4 || window==5)return houjiGpuInternalFail(GIEN_ADDRESS,address);
 unsigned status=0;auto*pointer=houjiGpuInternalPointer(address,(unsigned)window,&status);if(!pointer)return status;
 ++p.read_calls;++s.reads;p.last_address=address;*value=*pointer;p.last_value=*value;
 houjiGpuInternalReadBarrier();++p.read_barriers;
 if(*value==UINT32_MAX)return houjiGpuInternalFail(GIEN_SENTINEL,address);
 ++s.valid_reads;houjiGpuInternalGate(address,*value);return 0;
}
static unsigned houjiGpuInternalFresh(void*cookie,unsigned mode,struct hph_cold*cold,struct hph_clock*clock,GPP_U32*failed) {
 auto&p=gHoujiGpuInternalExec;auto&r=p.result;if(failed)*failed=0;
 if(!cold || !clock || !failed || mode>2 || p.in_fresh || p.pending_barrier || houjiGpuInternalExclusive(cookie))return HPH_OWNER;
 if(mode==0 && (!r.possible_execution || !p.release_seen || r.phase!=GIE_PHASE_POLL))return HPH_OWNER;
 if(mode==2 && (!r.possible_execution || !p.release_seen || (r.phase!=GIE_PHASE_RESET && r.phase!=GIE_PHASE_RESET_POLL)))return HPH_OWNER;
 p.fresh_ready=0;p.cxo_valid=0;
 if(p.fresh_count>=GIEN_FRESH_LIMIT)return HPH_ARGUMENT;
 p.current_fresh=p.fresh_count%GIEN_FRESH_SLOTS;++p.fresh_count;auto&s=p.fresh[p.current_fresh];hph_zero(&s,sizeof(s));
 s.sequence=p.fresh_count;s.mode=mode;s.attempted=1;p.in_fresh=1;
 const struct hph_ro_io io={&p,houjiGpuInternalFreshRead,houjiGpuInternalExclusive};
 s.code=gie_observe_ro(&io,mode,&s.cold,&s.clock,&s.failed_address);p.in_fresh=0;s.returned=1;
 if(!s.code && houjiGpuInternalExclusive(cookie))s.code=HPH_OWNER;
 if(!s.code){s.recheck=mode==1?hph_recheck(&p.record,&s.cold,&s.clock):gie_recheck_running(&p.record,&s.cold,&s.clock,mode);if(s.recheck)s.code=s.recheck;}
 s.valid=s.code==0;p.fresh_ready=s.valid;p.fresh_mode=mode;*cold=s.cold;*clock=s.clock;*failed=s.failed_address;return s.code;
}
static int houjiGpuInternalIndex(uint32_t address) {
 if(address&3)return -1;for(unsigned i=0;i<GIE_TOTAL_WORDS;i++)if(gie_address(i)==address)return (int)i;return -1;
}
static const struct gie_access *houjiGpuInternalExpectedRead(uint32_t address) {
 const auto&r=gHoujiGpuInternalExec.result;const int index=houjiGpuInternalIndex(address),control=gie_control_index(address);
 if(index>=0) {
  const unsigned i=(unsigned)index;
  if(r.phase==GIE_PHASE_BACKUP)return &r.backup[i];
  if(r.phase==GIE_PHASE_PROGRAM && i<42)return &r.program_verify[i];
  if(r.phase==GIE_PHASE_PREPARED_COMMIT && i<42)return &r.prepared_tcm_verify[i];
  if(r.phase==GIE_PHASE_STACK && i>=42)return &r.stack_after[i-42];
  if(r.phase==GIE_PHASE_RESTORE_TCM)return &r.restore_verify[i];
  if(r.phase==GIE_PHASE_FINAL_VERIFY)return &r.final_verify[i];
  if(r.phase==GIE_PHASE_POLL && i>=34 && i<42) {
   if(i==34 && r.polls && r.polls<=GIE_POLLS && r.mailbox_poll[r.polls-1].attempted && !r.mailbox_poll[r.polls-1].returned)return &r.mailbox_poll[r.polls-1];
   return &r.mailbox[i-34];
  }
 }
 if(control>=0) {
  const unsigned i=(unsigned)control;
  if(r.phase==GIE_PHASE_BACKUP)return &r.control_backup[i];
  if(r.phase==GIE_PHASE_CONTROL)return &r.control_verify[i];
  if(r.phase==GIE_PHASE_PREPARED_COMMIT)return &r.prepared_control_verify[i];
  if(r.phase==GIE_PHASE_RESTORE_CONTROL && i<7)return &r.control_restore_verify[i];
  if(r.phase==GIE_PHASE_FINAL_VERIFY)return &r.control_final_verify[i];
 }
 if(address==0x3d7e000u) {
  if(r.phase==GIE_PHASE_RELEASE && gHoujiGpuInternalExec.release_seen)return &r.release_verify;
  if(r.phase==GIE_PHASE_RESET_POLL && r.reset_polls && r.reset_polls<=GIE_POLLS)return &r.reset_poll[r.reset_polls-1];
 }
 return nullptr;
}
static GPP_U64 houjiGpuInternalRead(void*cookie,GPP_U32 address,GPP_U32*value) {
 auto&p=gHoujiGpuInternalExec;
 if(!value || p.in_fresh || !p.fresh_ready || p.pending_barrier || houjiGpuInternalExclusive(cookie))return houjiGpuInternalFail(GIEN_OWNER,address);
 if(address==0x3d99144u) {
  const auto&a=p.result.cxo;if(!a.attempted || a.returned)return houjiGpuInternalFail(GIEN_PHASE,address);
 }else {
  const auto*a=houjiGpuInternalExpectedRead(address);
  if(!p.cxo_valid || (p.cxo_raw&0x80000001u)!=1 || !a || a->attempted!=1 || a->returned)return houjiGpuInternalFail(GIEN_PHASE,address);
 }
 int window=houjiGpuInternalWindow(address);if(window<0)return houjiGpuInternalFail(GIEN_ADDRESS,address);
 unsigned status=0;auto*pointer=houjiGpuInternalPointer(address,(unsigned)window,&status);if(!pointer)return status;
 ++p.read_calls;p.last_address=address;*value=*pointer;p.last_value=*value;houjiGpuInternalReadBarrier();++p.read_barriers;
 if(address==0x3d99144u) {
  p.cxo_raw=*value;p.cxo_valid=*value!=UINT32_MAX;
  if(!p.cxo_valid)return houjiGpuInternalFail(GIEN_SENTINEL,address);
 }else if(houjiGpuInternalIndex(address)>=0)++p.tcm_reads;else ++p.control_reads;
 return 0;
}
static bool houjiGpuInternalPrepared(void) {
 const auto&r=gHoujiGpuInternalExec.result;
 if(!r.prepared_committed || r.programmed_words!=42 || r.control_programmed!=9)return false;
 for(unsigned i=0;i<42;i++){const auto&a=r.prepared_tcm_verify[i];if(!a.attempted || !a.returned || !a.valid || a.status || a.value!=gie_program_value(i))return false;}
 for(unsigned i=0;i<9;i++){const auto&a=r.prepared_control_verify[i];if(!a.attempted || !a.returned || !a.valid || a.status || (a.value&gie_control_mask(i))!=(gie_control_value(i)&gie_control_mask(i)))return false;}
 return true;
}
static GPP_U64 houjiGpuInternalWrite(void*cookie,GPP_U32 address,GPP_U32 value) {
 auto&p=gHoujiGpuInternalExec;auto&r=p.result;const int index=houjiGpuInternalIndex(address),control=gie_control_index(address);
 if(p.in_fresh || !p.fresh_ready || !p.cxo_valid || (p.cxo_raw&0x80000001u)!=1 || p.pending_barrier || houjiGpuInternalExclusive(cookie))return houjiGpuInternalFail(GIEN_OWNER,address);
 if(!r.backup_committed || !gie_backup_intact(&r))return houjiGpuInternalFail(GIEN_PHASE,address);
 const struct gie_access*a=nullptr;unsigned*seen=nullptr;uint32_t wanted=0;
 if(index>=0) {
  unsigned i=(unsigned)index,restore=r.phase==GIE_PHASE_RESTORE_TCM;
  if(r.phase!=GIE_PHASE_PROGRAM && !restore)return houjiGpuInternalFail(GIEN_PHASE,address);
  if(!restore && i>=42)return houjiGpuInternalFail(GIEN_ADDRESS,address);
  if(p.fresh_mode!=1 || p.fresh[p.current_fresh].cold.values[HGB_RESET]!=1)return houjiGpuInternalFail(GIEN_PHASE,address);
  a=restore?&r.restore_store[i]:&r.program_store[i];seen=&p.store_seen[restore][i];wanted=restore?r.backup[i].value:gie_program_value(i);
 }else if(control>=0) {
  unsigned i=(unsigned)control,restore=r.phase==GIE_PHASE_RESTORE_CONTROL;
  const unsigned order[]={1,0,2,3,4,5,6,7,8};
  if(r.phase!=GIE_PHASE_CONTROL && !restore)return houjiGpuInternalFail(GIEN_PHASE,address);
  if(restore?(i>=7 || r.control_restored>=7 || i!=6-r.control_restored):(r.control_programmed>=9 || i!=order[r.control_programmed]))return houjiGpuInternalFail(GIEN_PHASE,address);
  if(p.fresh_mode!=1 || p.fresh[p.current_fresh].cold.values[HGB_RESET]!=1)return houjiGpuInternalFail(GIEN_PHASE,address);
  a=restore?&r.control_restore_store[i]:&r.control_store[i];seen=&p.control_seen[restore][i];wanted=restore?r.control_backup[i].value:gie_control_value(i);
 }else if(address==0x3d7e000u) {
  if(r.phase==GIE_PHASE_RELEASE && value==0 && r.possible_execution && houjiGpuInternalPrepared() && p.fresh_mode==1) {
   a=&r.release_store;seen=&p.release_seen;wanted=0;
  }else if(r.phase==GIE_PHASE_RESET && value==1 && r.possible_execution && p.release_seen && r.guard_reset==2 && p.fresh_mode==2) {
   a=&r.reset_store;seen=&p.reset_seen;wanted=1;
  }else return houjiGpuInternalFail(GIEN_PHASE,address);
 }else return houjiGpuInternalFail(GIEN_ADDRESS,address);
 if(!a || !seen || *seen || a->attempted!=1 || a->returned)return houjiGpuInternalFail(GIEN_PHASE,address);
 if(value!=wanted || a->value!=wanted)return houjiGpuInternalFail(GIEN_VALUE,address,wanted,value);
 int window=houjiGpuInternalWindow(address);if(window<0)return houjiGpuInternalFail(GIEN_ADDRESS,address);
 unsigned status=0;auto*pointer=houjiGpuInternalPointer(address,(unsigned)window,&status);if(!pointer)return status;
 *seen=1;p.pending_barrier=1;p.last_address=address;p.last_value=value;
 if(index>=0)++p.tcm_writes;else if(control>=0)++p.control_writes;else ++p.reset_writes;
 *pointer=value;houjiGpuInternalWriteBarrier();++p.write_barriers;return 0;
}
/* Always drain a returned store even after loss of MMIO qualification. */
static GPP_U64 houjiGpuInternalBarrier(void*cookie) {
 if(cookie!=&gHoujiGpuInternalExec)return GIEN_OWNER;
 houjiGpuInternalFullBarrier();++gHoujiGpuInternalExec.barriers;gHoujiGpuInternalExec.pending_barrier=0;return 0;
}
static GPP_U64 houjiGpuInternalDelay(void*cookie,unsigned us) {
 auto&p=gHoujiGpuInternalExec;const auto&r=p.result;
 if(us!=10 || p.in_fresh || p.pending_barrier || houjiGpuInternalExclusive(cookie) || !p.fresh_ready || !p.cxo_valid ||
  !r.possible_execution || !p.release_seen || (r.phase!=GIE_PHASE_POLL && r.phase!=GIE_PHASE_RESET_POLL) || p.delay_calls>=2*GIE_POLLS)return GIEN_DELAY;
 ++p.delay_calls;IODelay(10);return 0;
}
static uint64_t houjiGpuInternalValid(const struct gie_access*a,unsigned n) {
 uint64_t bits=0;for(unsigned i=0;i<n && i<64;i++)bits|=(uint64_t)!!a[i].valid<<i;return bits;
}
static void houjiGpuInternalReport(unsigned stage) {
 const auto&p=gHoujiGpuInternalExec;const auto&r=p.result;
 IOLog("HOUJI GMU EXEC RESULT: stage=%u phase=%u code=%u native=%u complete=%u safe-to-save=%u retained=%u TCM-retained=%u lost=%u barrier-uncertain=%u possible-execution=%u reset-held=%u record=%08x backup=%u/58 control-backup=%u/9 crc=%08x/%08x programmed=%u/42 controls=%u/9 restore=%u/58 controls=%u/7 final=%u/58 commits=%u/%u core-commits=%u/%u/%u fresh=%u reads=%u TCM-reads=%u TCM-stores=%u control-stores=%u reset-stores=%u DMB=%u write-DSB=%u barrier-DSB=%u delay10=%u polls=%u reset-polls=%u stack-changed=%x IRQ-retained=%x maps=%x/%x; GPU-context=0 GPU-execution=0 DDR-DMA=0\n",
  stage,r.phase,r.code,p.code,r.complete,r.safe_to_save,r.retained,r.tcm_retained,r.qualification_lost,r.barrier_uncertain,r.possible_execution,r.reset_held,p.record.crc32,
  r.backup_words,r.control_backup_words,r.backup_crc,r.control_crc,r.programmed_words,r.control_programmed,r.restored_words,r.control_restored,r.final_verified_words,
  p.commit_attempted,p.commit_returned,r.backup_committed,r.prepared_committed,r.restore_committed,p.fresh_count,p.read_calls,p.tcm_reads,p.tcm_writes,p.control_writes,p.reset_writes,p.read_barriers,p.write_barriers,p.barriers,p.delay_calls,r.polls,r.reset_polls,r.stack_changed_mask,r.irq_retained,p.map_attempted,p.map_valid);
 IOLog("HOUJI GMU EXEC FIRST: code=%u phase=%u op=%u index=%u address=%x expected=%08x observed=%08x status=%llx native=%u phase=%u address=%x expected=%08x observed=%08x last=%x/%08x; returned/valid are actual CPU callback outcomes\n",
  r.first.code,r.first.phase,r.first.operation,r.first.index,r.first.address,r.first.expected,r.first.observed,(unsigned long long)r.first.status,p.first_io_code,p.first_io_phase,p.first_io_address,p.first_io_expected,p.first_io_observed,p.last_address,p.last_value);
 const unsigned retained=p.fresh_count<GIEN_FRESH_SLOTS?p.fresh_count:GIEN_FRESH_SLOTS;
 const unsigned show=stage?1u:8u,start=p.fresh_count>show?p.fresh_count-show:0;
 for(unsigned seq=start;seq<p.fresh_count && seq+retained>=p.fresh_count;seq++) {
  const auto&s=p.fresh[seq%GIEN_FRESH_SLOTS];
  IOLog("HOUJI GMU EXEC FRESH: seq=%u mode=%u returned=%u valid=%u code=%u recheck=%u address=%x reads=%u/%u gates=%x cold=%x reset=%x FW=%x HFI=%x GFSR=%x clock=%u/%x/%x CXO=%08x/%u; software owner/raw programming only\n",
   s.sequence,s.mode,s.returned,s.valid,s.code,s.recheck,s.failed_address,s.reads,s.valid_reads,s.gate_valid,s.cold.valid,s.cold.values[HGB_RESET],s.cold.values[HGB_FWINIT],s.cold.hfi,s.cold.values[HGB_GFSR],s.clock.stable,s.clock.valid[0],s.clock.valid[1],p.cxo_raw,p.cxo_valid);
 }
 IOLog("HOUJI GMU EXEC VALID: program=%llx/%llx/%llx prepared=%llx/%llx controls=%llx/%llx/%llx restore=%llx/%llx/%llx final=%llx/%llx release=%u/%u/%u reset=%u/%u; full per-action and bounded poll arrays retained in CPU RAM\n",
  (unsigned long long)houjiGpuInternalValid(r.program_store,42),(unsigned long long)houjiGpuInternalValid(r.program_barrier,42),(unsigned long long)houjiGpuInternalValid(r.program_verify,42),
  (unsigned long long)houjiGpuInternalValid(r.prepared_tcm_verify,42),(unsigned long long)houjiGpuInternalValid(r.prepared_control_verify,9),
  (unsigned long long)houjiGpuInternalValid(r.control_store,9),(unsigned long long)houjiGpuInternalValid(r.control_barrier,9),(unsigned long long)houjiGpuInternalValid(r.control_verify,9),
  (unsigned long long)houjiGpuInternalValid(r.restore_store,58),(unsigned long long)houjiGpuInternalValid(r.restore_barrier,58),(unsigned long long)houjiGpuInternalValid(r.restore_verify,58),
  (unsigned long long)houjiGpuInternalValid(r.final_verify,58),(unsigned long long)houjiGpuInternalValid(r.control_final_verify,9),r.release_store.valid,r.release_barrier.valid,r.release_verify.valid,r.reset_store.valid,r.reset_barrier.valid);
 uint32_t mail[8]={0,0,0,0,0,0,0,0};for(unsigned i=0;i<8;i++)mail[i]=r.mailbox[i].value;
 IOLog("HOUJI GMU EXEC MAIL: %08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x valid=%llx lastpoll=%08x/%u resetpoll=%08x/%u\n",mail[0],mail[1],mail[2],mail[3],mail[4],mail[5],mail[6],mail[7],(unsigned long long)houjiGpuInternalValid(r.mailbox,8),r.polls?r.mailbox_poll[r.polls-1].value:0,r.polls?r.mailbox_poll[r.polls-1].valid:0,r.reset_polls?r.reset_poll[r.reset_polls-1].value:0,r.reset_polls?r.reset_poll[r.reset_polls-1].valid:0);
 /* Repeat complete originals at each durable point and final snapshot. */
 if(r.backup_words==58)for(unsigned i=0;i<58;i+=8) {
  uint32_t w[8]={0,0,0,0,0,0,0,0};unsigned valid=0;for(unsigned j=0;j<8 && i+j<58;j++){w[j]=r.backup[i+j].value;valid|=(r.backup[i+j].valid?1u:0u)<<j;}
  IOLog("HOUJI GMU EXEC ORIGINAL: i=%u %08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x valid=%02x crc=%08x\n",i,w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7],valid,r.backup_crc);
 }
 if(r.control_backup_words==9)for(unsigned i=0;i<9;i+=8) {
  uint32_t w[8]={0,0,0,0,0,0,0,0};unsigned valid=0;for(unsigned j=0;j<8 && i+j<9;j++){w[j]=r.control_backup[i+j].value;valid|=(r.control_backup[i+j].valid?1u:0u)<<j;}
  IOLog("HOUJI GMU EXEC CONTROL ORIGINAL: i=%u %08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x valid=%02x crc=%08x\n",i,w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7],valid,r.control_crc);
 }
}
static GPP_U64 houjiGpuInternalCommit(void*cookie,unsigned stage,const struct gie_result*r) {
 auto&p=gHoujiGpuInternalExec;
 if(houjiGpuInternalExclusive(cookie) || r!=&p.result || !p.fresh_ready || !p.cxo_valid || (p.cxo_raw&0x80000001u)!=1 ||
  p.fresh_mode!=1 || p.fresh[p.current_fresh].cold.values[HGB_RESET]!=1 || p.pending_barrier || stage<1 || stage>3)return GIEN_COMMIT;
 if((stage==1 && p.last_commit_stage) || (stage==2 && p.last_commit_stage!=1) || (stage==3 && (p.last_commit_stage<1 || p.last_commit_stage>=3)))return GIEN_COMMIT;
 if(stage==1 && (r->phase!=GIE_PHASE_BACKUP_COMMIT || r->backup_words!=58 || r->control_backup_words!=9 || p.tcm_writes || p.control_writes || p.reset_writes))return GIEN_COMMIT;
 if(stage==2 && (r->phase!=GIE_PHASE_PREPARED_COMMIT || !r->backup_committed || r->programmed_words!=42 || r->control_programmed!=9 || p.release_seen || p.reset_writes || houjiGpuInternalValid(r->program_verify,42)!=((1ull<<42)-1) || houjiGpuInternalValid(r->control_verify,9)!=511))return GIEN_COMMIT;
 if(stage==3 && (r->phase!=GIE_PHASE_FINAL_COMMIT || r->restored_words!=58 || r->control_restored!=7 || r->final_verified_words!=58 ||
  !r->reset_held || r->tcm_retained || r->barrier_uncertain || r->qualification_lost || houjiGpuInternalValid(r->final_verify,58)!=((1ull<<58)-1) || houjiGpuInternalValid(r->control_final_verify,9)!=511))return GIEN_COMMIT;
 if(!gie_backup_intact(&p.result))return GIEN_COMMIT;
 ++p.commit_attempted;p.last_commit_stage=stage;houjiGpuInternalReport(stage);
 p.fresh_ready=0;p.cxo_valid=0;p.in_commit=1;
 houji_ufs_checkpoint(stage==1?"internal CM3 originals58+controls9 complete; no store yet; post-save fresh pending":
  stage==2?"internal CM3 code42+controls9 verified reset1; post-save fresh and prepared re-read pending":
  "internal CM3 reset1 originals58+controls7 restored and final audit complete; post-save fresh pending",0);
 p.in_commit=0;++p.commit_returned;
 return gRootEnabled && gLogEnabled && gReturnRecovery?0:GIEN_FLAGS;
}
static bool houjiGpuInternalExecProbeOnce(void) {
 int enabled=0;PE_parse_boot_argn("houji_gpu_internal_exec_probe",&enabled,sizeof(enabled));if(enabled!=1)return true;
 unsigned expected=0;
 if(!__atomic_compare_exchange_n(&gHoujiGpuInternalExecOnce,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return gHoujiGpuInternalExec.result.safe_to_save==1 && !gHoujiGpuInternalExec.active;
 auto&p=gHoujiGpuInternalExec;p.enabled=1;p.phase=1;
 if(!gRootEnabled || !gLogEnabled || !gReturnRecovery){p.code=GIEN_FLAGS;houjiGpuInternalReport(0);return false;}
 IORegistryEntry*chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
 if(!chosen){p.code=GIEN_PROPERTY;houjiGpuInternalReport(0);return false;}
 OSData*data=OSDynamicCast(OSData,chosen->getProperty("houji,gpu-power-handoff"));p.property_bytes=data?data->getLength():0;
 if(!data || p.property_bytes!=HPH_RECORD_BYTES || !data->getBytesNoCopy()){chosen->release();p.code=GIEN_PROPERTY;houjiGpuInternalReport(0);return false;}
 p.decoded=hph_decode(data->getBytesNoCopy(),p.property_bytes,&p.record)?1u:0u;chosen->release();
 p.record_ready=p.decoded && p.record.complete==1 && p.record.code==0;
 if(!p.record_ready){p.code=GIEN_RECORD;houjiGpuInternalReport(0);return false;}
 p.active=1;p.phase=2;struct hph_cold cold;struct hph_clock clock;uint32_t failed=0;
 unsigned observed=houjiGpuInternalFresh(&p,1,&cold,&clock,&failed);
 if(observed){p.code=GIEN_ORDER;p.active=0;houjiGpuInternalReport(0);return false;}
 p.phase=3;p.core_called=1;
 const struct gie_io io={&p,houjiGpuInternalRead,houjiGpuInternalWrite,houjiGpuInternalBarrier,houjiGpuInternalDelay,houjiGpuInternalFresh,houjiGpuInternalCommit,houjiGpuInternalExclusive};
 p.core_code=gie_run(&io,&p.record,&cold,&clock,&p.result);p.active=0;p.phase=4;houjiGpuInternalReport(0);
 return p.result.safe_to_save==1;
}
#endif
