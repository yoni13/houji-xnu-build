/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Candidate only: synchronous, software-single-owner XNU held-reset TCM test.
 * No EFI pointers/resources, CM3 reset/control, SMMU or clock writes. Fresh
 * Device mappings and CPU backups remain alive until platform reset.
 * Include after normal-context houji_ufs_checkpoint and its logging flags.
 * Caller MUST panic/reset without another checkpoint if this returns false.
 */
#ifndef HOUJI_GPU_HELD_TCM_PROBE_INC
#define HOUJI_GPU_HELD_TCM_PROBE_INC
#include "GmuHeldTcm.h"
#ifndef HOUJI_GPU_HELD_TCM_PROBE_HOST_TEST
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSSharedPtr.h>
#include <pexpert/pexpert.h>
#endif
enum {HHTN_WINDOWS=5,HHTN_FRESH_MAX=32};
enum {HHTN_OK,HHTN_FLAGS,HHTN_PROPERTY,HHTN_RECORD,HHTN_OWNER,HHTN_ORDER,
 HHTN_ADDRESS,HHTN_CAPACITY,HHTN_ALLOC,HHTN_MAP,HHTN_MAPPING,HHTN_SENTINEL,
 HHTN_VALUE,HHTN_PHASE,HHTN_COMMIT};
static const uint64_t kHoujiHeldBase[HHTN_WINDOWS]={0x170000,0x3d90000,0x3d7c000,0x3da0000,0x3d6c000};
static const uint64_t kHoujiHeldBytes[HHTN_WINDOWS]={0x4000,0x10000,0x4000,0x40000,0x10000};
static unsigned gHoujiGpuHeldTcmOnce;
static struct {
 OSSharedPtr<IOMemoryMap> maps[HHTN_WINDOWS];
 struct hph_record record;
 struct hht_result result;
 struct Fresh {
  struct hph_cold cold;struct hph_clock clock;
  unsigned attempted,returned,valid,code,recheck,reads,valid_reads,gate_valid,failed_address;
 } fresh[HHTN_FRESH_MAX];
 unsigned enabled,active,decoded,record_ready,in_fresh,in_commit,fresh_count,current_fresh,fresh_ready;
 unsigned phase,code,core_called,core_code,cxo_valid,cxo_raw,property_bytes;
 unsigned read_calls,tcm_reads,tcm_writes,read_barriers,write_barriers,barriers;
 unsigned map_attempted,map_valid,commit_attempted,commit_returned,pending_barrier;
 unsigned first_io_code,first_io_phase,first_io_address,first_io_expected,first_io_observed;
 unsigned last_address,last_value,store_seen[2][HHT_PROGRAM_WORDS];
} gHoujiGpuHeldTcm;
#ifdef HOUJI_GPU_HELD_TCM_PROBE_HOST_TEST
static void houjiGpuHeldReadBarrier(void);
static void houjiGpuHeldWriteBarrier(void);
static void houjiGpuHeldFullBarrier(void);
#else
static void houjiGpuHeldReadBarrier(void){__asm__ volatile("dmb oshld":::"memory");}
static void houjiGpuHeldWriteBarrier(void){__asm__ volatile("dsb sy":::"memory");}
static void houjiGpuHeldFullBarrier(void){__asm__ volatile("dsb sy":::"memory");}
#endif
static unsigned houjiGpuHeldFail(unsigned code,uint32_t address,uint32_t expected=0,uint32_t observed=0) {
 auto&p=gHoujiGpuHeldTcm;
 if(!p.first_io_code){p.first_io_code=code;p.first_io_phase=p.result.phase;p.first_io_address=address;p.first_io_expected=expected;p.first_io_observed=observed;}
 return code;
}
static int houjiGpuHeldExclusive(void*cookie) {
 const auto&p=gHoujiGpuHeldTcm;
 return cookie!=&gHoujiGpuHeldTcm || !p.active || !p.decoded || !p.record_ready || p.in_commit || p.result.qualification_lost ||
  !gRootEnabled || !gLogEnabled || !gReturnRecovery ||
  __atomic_load_n(&gHoujiGpuHeldTcmOnce,__ATOMIC_ACQUIRE)!=1;
}
static int houjiGpuHeldWindow(uint32_t address) {
 for(unsigned i=0;i<HHTN_WINDOWS;i++)if(address>=kHoujiHeldBase[i] && address-kHoujiHeldBase[i]<=kHoujiHeldBytes[i]-4)return (int)i;
 return -1;
}
static volatile uint32_t *houjiGpuHeldPointer(uint32_t address,unsigned window,unsigned *status) {
 auto&p=gHoujiGpuHeldTcm;*status=0;
 if(window>=HHTN_WINDOWS || (address&3) || address<kHoujiHeldBase[window] ||
  address-kHoujiHeldBase[window]>kHoujiHeldBytes[window]-4){*status=houjiGpuHeldFail(HHTN_ADDRESS,address);return nullptr;}
 if(!p.maps[window]) {
  p.map_attempted|=1u<<window;
  IODeviceMemory *memory=IODeviceMemory::withRange(kHoujiHeldBase[window],kHoujiHeldBytes[window]);
  if(!memory){*status=houjiGpuHeldFail(HHTN_ALLOC,address);return nullptr;}
  const unsigned options=kIOMapAnywhere|kIOMapInhibitCache|(window==4?0u:(unsigned)kIOMapReadOnly);
  p.maps[window]=OSSharedPtr<IOMemoryMap>(memory->map(options),OSNoRetain);memory->release();
  if(!p.maps[window]){*status=houjiGpuHeldFail(HHTN_MAP,address);return nullptr;}
 }
 const uint64_t va=p.maps[window]->getVirtualAddress(),length=p.maps[window]->getLength(),offset=address-kHoujiHeldBase[window];
 if(!va || (va&3) || length<kHoujiHeldBytes[window] || va>UINT64_MAX-offset-4) {
  *status=houjiGpuHeldFail(HHTN_MAPPING,address);return nullptr;
 }
 p.map_valid|=1u<<window;return reinterpret_cast<volatile uint32_t *>((uintptr_t)(va+offset));
}
static unsigned houjiGpuHeldRequired(uint32_t address) {
 for(unsigned i=0;i<8;i++)if(address==hgb_address(i))return (1u<<i)-1;
 return 0xff;
}
static void houjiGpuHeldGate(uint32_t address,uint32_t value) {
 auto&p=gHoujiGpuHeldTcm;auto&s=p.fresh[p.current_fresh];
 const uint32_t masks[]={0x80000003u,0x80000003u,0x80000001u,1,0x80000000u,0x80000000u,0xffffffffu,0x80000001u};
 const uint32_t want[]={1,1,1,0,0x80000000u,0,1,1};
 for(unsigned i=0;i<8;i++)if(address==hgb_address(i)) {
  if((value&masks[i])==want[i])s.gate_valid|=1u<<i;else s.gate_valid&=~(1u<<i);
  return;
 }
}
/* This callback is used ONLY inside the quiet, real portable RO observer. */
static GPP_U64 houjiGpuHeldFreshRead(void*cookie,GPP_U32 address,GPP_U32*value) {
 auto&p=gHoujiGpuHeldTcm;
 if(!value || !p.in_fresh || !p.fresh_count || !hph_read_allowed(address) || (address&3))return houjiGpuHeldFail(HHTN_ADDRESS,address);
 if(houjiGpuHeldExclusive(cookie))return houjiGpuHeldFail(HHTN_OWNER,address);
 auto&s=p.fresh[p.current_fresh];const unsigned required=houjiGpuHeldRequired(address);
 if((s.gate_valid&required)!=required)return houjiGpuHeldFail(HHTN_ORDER,address);
 int window=houjiGpuHeldWindow(address);if(window<0 || window==4)return houjiGpuHeldFail(HHTN_ADDRESS,address);
 unsigned status=0;auto*pointer=houjiGpuHeldPointer(address,(unsigned)window,&status);if(!pointer)return status;
 ++p.read_calls;++s.reads;p.last_address=address;*value=*pointer;p.last_value=*value;
 houjiGpuHeldReadBarrier();++p.read_barriers;
 if(*value==UINT32_MAX)return houjiGpuHeldFail(HHTN_SENTINEL,address);
 ++s.valid_reads;houjiGpuHeldGate(address,*value);return 0;
}
static unsigned houjiGpuHeldFresh(void*cookie,struct hph_cold*cold,struct hph_clock*clock,GPP_U32*failed) {
 auto&p=gHoujiGpuHeldTcm;if(failed)*failed=0;
 if(!cold || !clock || !failed || p.in_fresh || p.pending_barrier || houjiGpuHeldExclusive(cookie))return HPH_OWNER;
 p.fresh_ready=0;p.cxo_valid=0;
 if(p.fresh_count>=HHTN_FRESH_MAX)return HPH_ARGUMENT;
 p.current_fresh=p.fresh_count++;auto&s=p.fresh[p.current_fresh];s.attempted=1;p.in_fresh=1;
 const struct hph_ro_io io={&p,houjiGpuHeldFreshRead,houjiGpuHeldExclusive};
 s.code=hph_observe_ro(&io,&s.cold,&s.clock,&s.failed_address);p.in_fresh=0;s.returned=1;
 if(!s.code && houjiGpuHeldExclusive(cookie))s.code=HPH_OWNER;
 if(!s.code){s.recheck=hph_recheck(&p.record,&s.cold,&s.clock);if(s.recheck)s.code=s.recheck;}
 s.valid=s.code==0;p.fresh_ready=s.valid;*cold=s.cold;*clock=s.clock;*failed=s.failed_address;return s.code;
}
static int houjiGpuHeldIndex(uint32_t address) {
 if(address&3)return -1;for(unsigned i=0;i<HHT_TOTAL_WORDS;i++)if(hht_address(i)==address)return (int)i;return -1;
}
static const struct hht_access *houjiGpuHeldExpectedRead(unsigned index) {
 const auto&r=gHoujiGpuHeldTcm.result;
 if(index>=HHT_TOTAL_WORDS)return nullptr;
 if(r.phase==HHT_PHASE_BACKUP)return &r.backup[index];
 if(r.phase==HHT_PHASE_PROGRAM && index<HHT_PROGRAM_WORDS)return &r.program_verify[index];
 if(r.phase==HHT_PHASE_STACK && index>=HHT_PROGRAM_WORDS)return &r.stack_verify[index-HHT_PROGRAM_WORDS];
 if(r.phase==HHT_PHASE_RESTORE && index<HHT_PROGRAM_WORDS)return &r.restore_verify[index];
 if(r.phase==HHT_PHASE_FINAL_VERIFY)return &r.final_verify[index];
 return nullptr;
}
static GPP_U64 houjiGpuHeldRead(void*cookie,GPP_U32 address,GPP_U32*value) {
 auto&p=gHoujiGpuHeldTcm;
 if(!value || p.in_fresh || !p.fresh_ready || p.pending_barrier || houjiGpuHeldExclusive(cookie))return houjiGpuHeldFail(HHTN_OWNER,address);
 unsigned window=1;
 if(address!=0x3d99144u) {
  const int index=houjiGpuHeldIndex(address);
  if(index<0 || !p.cxo_valid || (p.cxo_raw&0x80000001u)!=1)return houjiGpuHeldFail(HHTN_ADDRESS,address);
  const auto*a=houjiGpuHeldExpectedRead((unsigned)index);
  if(!a || a->attempted!=1 || a->returned)return houjiGpuHeldFail(HHTN_PHASE,address);
  window=4;
 }
 unsigned status=0;auto*pointer=houjiGpuHeldPointer(address,window,&status);if(!pointer)return status;
 ++p.read_calls;p.last_address=address;*value=*pointer;p.last_value=*value;houjiGpuHeldReadBarrier();++p.read_barriers;
 if(address==0x3d99144u) {
  p.cxo_raw=*value;p.cxo_valid=*value!=UINT32_MAX;
  if(!p.cxo_valid)return houjiGpuHeldFail(HHTN_SENTINEL,address);
 }else ++p.tcm_reads; /* All ones is valid TCM data. */
 return 0;
}
static GPP_U64 houjiGpuHeldWrite(void*cookie,GPP_U32 address,GPP_U32 value) {
 auto&p=gHoujiGpuHeldTcm;auto&r=p.result;const int index=houjiGpuHeldIndex(address);
 if(p.in_fresh || !p.fresh_ready || !p.cxo_valid || (p.cxo_raw&0x80000001u)!=1 || p.pending_barrier ||
  houjiGpuHeldExclusive(cookie))return houjiGpuHeldFail(HHTN_OWNER,address);
 if(index<0 || (unsigned)index>=HHT_PROGRAM_WORDS)return houjiGpuHeldFail(HHTN_ADDRESS,address);
 const unsigned i=(unsigned)index;unsigned restore=0;
 if(r.phase==HHT_PHASE_RESTORE)restore=1;
 else if(r.phase!=HHT_PHASE_PROGRAM)return houjiGpuHeldFail(HHTN_PHASE,address);
 const auto&a=restore?r.restore_store[i]:r.program_store[i];
 if(!r.backup_committed || r.backup_words!=HHT_TOTAL_WORDS || !hht_backup_intact(&r) ||
  a.attempted!=1 || a.returned || p.store_seen[restore][i] || !r.backup[i].valid)return houjiGpuHeldFail(HHTN_PHASE,address);
 const uint32_t wanted=restore?r.backup[i].value:hht_program_value(i);
 if(value!=wanted || a.value!=wanted)return houjiGpuHeldFail(HHTN_VALUE,address,wanted,value);
 unsigned status=0;auto*pointer=houjiGpuHeldPointer(address,4,&status);if(!pointer)return status;
 p.store_seen[restore][i]=1;p.pending_barrier=1;p.last_address=address;p.last_value=value;
 ++p.tcm_writes;*pointer=value;houjiGpuHeldWriteBarrier();++p.write_barriers;return 0;
}
/* Always drain a returned store, even if the core reports a transport error.
 * This callback is CPU-only and does not require a possibly lost MMIO lease. */
static GPP_U64 houjiGpuHeldBarrier(void*cookie) {
 if(cookie!=&gHoujiGpuHeldTcm)return HHTN_OWNER;
 houjiGpuHeldFullBarrier();++gHoujiGpuHeldTcm.barriers;gHoujiGpuHeldTcm.pending_barrier=0;return 0;
}
static void houjiGpuHeldReport(unsigned stage) {
 const auto&p=gHoujiGpuHeldTcm;const auto&r=p.result;
 IOLog("HOUJI HELD TCM RESULT: stage=%u phase=%u code=%u native=%u complete=%u safe-to-save=%u retained=%u tcm-retained=%u lost=%u record=%08x backup=%u/%u crc=%08x programmed=%u/42 stack=%u/16 restored=%u/42 final=%u/58 commits=%u/%u core-commits=%u/%u fresh=%u reads=%u TCM-reads=%u TCM-writes=%u DMB=%u write-DSB=%u barrier-DSB=%u maps=%x/%x; CM3-reset-writes=0 CM3-execution=0 GPU-context=0 GPU-execution=0 DMA=0\n",
  stage,r.phase,r.code,p.code,r.complete,r.safe_to_save,r.retained,r.tcm_retained,r.qualification_lost,p.record.crc32,
  r.backup_words,HHT_TOTAL_WORDS,r.backup_crc,r.programmed_words,r.stack_checked,r.restored_words,r.final_verified_words,
  p.commit_attempted,p.commit_returned,r.backup_committed,r.restore_committed,p.fresh_count,p.read_calls,p.tcm_reads,p.tcm_writes,
  p.read_barriers,p.write_barriers,p.barriers,p.map_attempted,p.map_valid);
 IOLog("HOUJI HELD TCM FIRST: code=%u phase=%u op=%u index=%u address=%x expected=%08x observed=%08x status=%llx native=%u phase=%u address=%x expected=%08x observed=%08x last=%x/%08x; callback return and valid flags, not access intent\n",
  r.first.code,r.first.phase,r.first.operation,r.first.index,r.first.address,r.first.expected,r.first.observed,(unsigned long long)r.first.status,
  p.first_io_code,p.first_io_phase,p.first_io_address,p.first_io_expected,p.first_io_observed,p.last_address,p.last_value);
 for(unsigned i=0;i<p.fresh_count;i++) {
  if(stage && i+1!=p.fresh_count)continue;
  const auto&s=p.fresh[i];
  IOLog("HOUJI HELD TCM FRESH: i=%u attempted=%u returned=%u valid=%u code=%u recheck=%u address=%x reads=%u/%u gates=%x cold=%x reset=%x FW=%x HFI=%x GFSR=%x clock=%u/%x/%x; software exclusive only, raw programming not measured frequency\n",
   i,s.attempted,s.returned,s.valid,s.code,s.recheck,s.failed_address,s.reads,s.valid_reads,s.gate_valid,s.cold.valid,
   s.cold.values[HGB_RESET],s.cold.values[HGB_FWINIT],s.cold.hfi,s.cold.values[HGB_GFSR],s.clock.stable,s.clock.valid[0],s.clock.valid[1]);
 }
 uint64_t program[3]={0,0,0},restore[3]={0,0,0},final_valid=0;
 for(unsigned i=0;i<HHT_PROGRAM_WORDS;i++) {
  program[0]|=(uint64_t)!!r.program_store[i].valid<<i;
  program[1]|=(uint64_t)!!r.program_barrier[i].valid<<i;
  program[2]|=(uint64_t)!!r.program_verify[i].valid<<i;
  restore[0]|=(uint64_t)!!r.restore_store[i].valid<<i;
  restore[1]|=(uint64_t)!!r.restore_barrier[i].valid<<i;
  restore[2]|=(uint64_t)!!r.restore_verify[i].valid<<i;
 }
 for(unsigned i=0;i<HHT_TOTAL_WORDS;i++)final_valid|=(uint64_t)!!r.final_verify[i].valid<<i;
 IOLog("HOUJI HELD TCM VALID: program-store/barrier/read=%llx/%llx/%llx restore-store/barrier/read=%llx/%llx/%llx final-read=%llx; full per-action fields retained in CPU arrays\n",
  (unsigned long long)program[0],(unsigned long long)program[1],(unsigned long long)program[2],
  (unsigned long long)restore[0],(unsigned long long)restore[1],(unsigned long long)restore[2],(unsigned long long)final_valid);
 IOLog("HOUJI HELD TCM CXO: raw=%08x valid=%u; additional fixed CBCR observation, no measured Hz\n",p.cxo_raw,p.cxo_valid);
 /* Repeat all originals at both durable points and in the final report, so
  * a bounded msgbuf does not leave only a CRC after the test has progressed. */
 if(r.backup_words==HHT_TOTAL_WORDS)for(unsigned i=0;i<HHT_TOTAL_WORDS;i+=8) {
  uint32_t words[8]={0,0,0,0,0,0,0,0};unsigned valid=0;
  for(unsigned j=0;j<8 && i+j<HHT_TOTAL_WORDS;j++){words[j]=r.backup[i+j].value;valid|=(r.backup[i+j].valid?1u:0u)<<j;}
  IOLog("HOUJI HELD TCM ORIGINAL: i=%u raw=%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x valid-mask=%02x crc=%08x\n",
   i,words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7],valid,r.backup_crc);
 }
}
static GPP_U64 houjiGpuHeldCommit(void*cookie,unsigned stage,const struct hht_result*r) {
 auto&p=gHoujiGpuHeldTcm;
 if(houjiGpuHeldExclusive(cookie) || r!=&p.result || !p.fresh_ready || !p.cxo_valid ||
  (p.cxo_raw&0x80000001u)!=1 || p.pending_barrier || stage<1 || stage>2)return HHTN_COMMIT;
 if(stage==1 && (r->phase!=HHT_PHASE_BACKUP_COMMIT || r->backup_words!=HHT_TOTAL_WORDS || p.tcm_writes))return HHTN_COMMIT;
 if(stage==2 && (r->phase!=HHT_PHASE_FINAL_COMMIT || r->restored_words!=HHT_PROGRAM_WORDS ||
  r->final_verified_words!=HHT_TOTAL_WORDS || r->tcm_retained))return HHTN_COMMIT;
 if(!hht_backup_intact(&p.result))return HHTN_COMMIT;
 ++p.commit_attempted;houjiGpuHeldReport(stage);
 /* We hold no adapter lock. This is the normal UFS helper, with logging
  * positively enabled: a returned call means its commit/readback returned.
  * Its pre-save stage text is never proof that subsequent work completed. */
 p.fresh_ready=0;p.cxo_valid=0;p.in_commit=1;
 houji_ufs_checkpoint(stage==1?"held-reset TCM originals complete; no store yet; post-save fresh guard pending":
  "held-reset TCM 42 originals restored and all58 audited; post-save fresh guard pending",0);
 p.in_commit=0;++p.commit_returned;
 return gRootEnabled && gLogEnabled && gReturnRecovery?0:HHTN_FLAGS;
}
/* True means the caller may use its existing normal final checkpoint. False
 * MUST take the already-armed no-FS panic/reset exit instead. Not an assurance
 * that a hardware-stalled load/store can return or be recovered here. */
static bool houjiGpuHeldTcmProbeOnce(void) {
 int enabled=0;PE_parse_boot_argn("houji_gpu_held_tcm_probe",&enabled,sizeof(enabled));if(enabled!=1)return true;
 unsigned expected=0;
 if(!__atomic_compare_exchange_n(&gHoujiGpuHeldTcmOnce,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))
  return gHoujiGpuHeldTcm.result.safe_to_save==1 && !gHoujiGpuHeldTcm.active;
 auto&p=gHoujiGpuHeldTcm;p.enabled=1;p.phase=1;
 if(!gRootEnabled || !gLogEnabled || !gReturnRecovery){p.code=HHTN_FLAGS;houjiGpuHeldReport(0);return false;}
 IORegistryEntry*chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
 if(!chosen){p.code=HHTN_PROPERTY;houjiGpuHeldReport(0);return false;}
 OSData*data=OSDynamicCast(OSData,chosen->getProperty("houji,gpu-power-handoff"));
 p.property_bytes=data?data->getLength():0;
 if(!data || p.property_bytes!=HPH_RECORD_BYTES || !data->getBytesNoCopy()){
  chosen->release();p.code=HHTN_PROPERTY;houjiGpuHeldReport(0);return false;
 }
 p.decoded=hph_decode(data->getBytesNoCopy(),p.property_bytes,&p.record)?1u:0u;chosen->release();
 p.record_ready=p.decoded && p.record.complete==1 && p.record.code==0;
 if(!p.record_ready){p.code=HHTN_RECORD;houjiGpuHeldReport(0);return false;}
 p.active=1;p.phase=2;struct hph_cold cold;struct hph_clock clock;uint32_t failed=0;
 unsigned observed=houjiGpuHeldFresh(&p,&cold,&clock,&failed);
 if(observed){p.code=HHTN_ORDER;p.active=0;houjiGpuHeldReport(0);return false;}
 p.phase=3;p.core_called=1;
 const struct hht_io io={&p,houjiGpuHeldRead,houjiGpuHeldWrite,houjiGpuHeldBarrier,houjiGpuHeldFresh,houjiGpuHeldCommit,houjiGpuHeldExclusive};
 p.core_code=hgd_handoff_tcm_held_reset(&io,&p.record,&cold,&clock,&p.result);
 p.active=0;p.phase=4;houjiGpuHeldReport(0);
 return p.result.safe_to_save==1;
}
#endif
