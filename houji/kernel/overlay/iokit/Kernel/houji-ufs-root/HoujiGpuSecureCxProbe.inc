/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Fixed secure-CX INIT only, after this boot's completed NC canary and inactive
 * firmware memory preparation. Include after both actual adapters. New worker
 * owner/maps; never reactivate either completed adapter. False requires the
 * caller's retained no-filesystem PSCI/reset path. No GPU MMIO writes exist.
 */
#ifndef HOUJI_GPU_SECURE_CX_PROBE_INC
#define HOUJI_GPU_SECURE_CX_PROBE_INC
#include "vendor/Ace5Gmu/giulia_gpu_scm.h"
#ifndef HOUJI_GPU_SECURE_CX_HOST_TEST
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSSharedPtr.h>
#endif
enum {HGSC_WINDOWS=4,HGSC_CALLS=3*(GGS_BUSY_RETRIES+GGS_INTERRUPT_RESUMES+1),HGSC_OBSERVATIONS=3*HGSC_CALLS+4};
enum {HGSC_OK,HGSC_FLAGS,HGSC_OWNER,HGSC_LEASE,HGSC_FRAME,HGSC_CAPACITY,HGSC_READ,HGSC_MAP,HGSC_COLD,HGSC_SAVE,HGSC_SCM};
static const uint64_t kHoujiSecureBase[4]={0x170000,0x3d90000,0x3d7c000,0x3da0000};
static const uint64_t kHoujiSecureBytes[4]={0x4000,0x10000,0x4000,0x40000};
static unsigned gHoujiGpuSecureCxOnce;
static struct {
 OSSharedPtr<IOMemoryMap> maps[HGSC_WINDOWS];
 struct ggs_state scm;
 struct Call {struct ggs_regs input,output;unsigned phase,intent,saved,attempted,returned,transport,pre,ready,post,post_ok;} calls[HGSC_CALLS];
 struct Observation {struct gec_snapshot snapshot;unsigned kind,attempted,returned,code,recheck,failed,reads,gates,lease_before,lease_after;} observations[HGSC_OBSERVATIONS];
 struct Par {uint64_t read,write,alias_read,alias_write;} gec_par[3][4],firmware_par[9][40];
 unsigned enabled,active,in_save,in_observe,current_observation,observation_count,call_count,actual_calls,returned_calls;
 unsigned phase,code,complete,lost,first_address,map_attempted,map_valid,prerequisites,delay_calls,intent_saves,report_saved,report_returned;
 unsigned at_calls,lease_calls,lease_slot,lease_row;uint64_t lease_observed,lease_expected;
} gHoujiGpuSecureCx;
#ifdef HOUJI_GPU_SECURE_CX_HOST_TEST
static bool houjiSecureCxAt(uint64_t,bool,uint64_t*);
static int houjiSecureCxSmc(struct ggs_regs*);
static void houjiSecureCxReadBarrier(void);
#else
static bool houjiSecureCxAt(uint64_t va,bool write,uint64_t*par) {
 if(!par)return false;uint64_t daif,saved;disable_preemption();const int before=cpu_number();
 __asm__ volatile("mrs %0,daif\n\tmsr daifset,#3\n\tmrs %1,par_el1":"=r"(daif),"=r"(saved)::"memory");
 if(write)__asm__ volatile("at s1e1w,%0\n\tisb sy"::"r"(va):"memory");else __asm__ volatile("at s1e1r,%0\n\tisb sy"::"r"(va):"memory");
 __asm__ volatile("mrs %0,par_el1\n\tmsr par_el1,%1\n\tmsr daif,%2":"=&r"(*par):"r"(saved),"r"(daif):"memory");
 const int after=cpu_number();enable_preemption();return before>=0 && before==after;
}
static int houjiSecureCxSmc(struct ggs_regs*r){return ggs_aarch64_call(nullptr,r);}
static void houjiSecureCxReadBarrier(void){__asm__ volatile("dmb oshld":::"memory");}
#endif
static bool houjiSecureCxFail(unsigned code,uint32_t address=0) {
 auto&p=gHoujiGpuSecureCx;if(!p.code){p.code=code;p.first_address=address;}p.lost=1;return false;
}
static bool houjiSecureCxConfiguration(void) {
 const auto&e=gHoujiGpuExternalCanary;const auto&f=gHoujiFirmwareMemory;const auto&r=e.result;
 return gRootEnabled && gLogEnabled && gReturnRecovery &&
  __atomic_load_n(&gHoujiGpuExternalCanaryOnce,__ATOMIC_ACQUIRE)==1 &&
  __atomic_load_n(&gHoujiFirmwareMemoryOnce,__ATOMIC_ACQUIRE)==1 &&
  !e.active && e.phase==4 && e.memory_profile==GEC_MEMORY_NORMAL_NC && e.memory.memory_profile==GEC_MEMORY_NORMAL_NC &&
  e.memory_ready && e.held_mask==7 && e.published_mask==7 && !e.pending_barrier && !e.in_commit && !e.in_fresh &&
  !e.code && !e.core_code && r.phase==GEC_PHASE_DONE && r.safe_to_save==1 && gec_complete_proof(&r) &&
  r.memory_profile==GEC_MEMORY_NORMAL_NC && !r.cpu.invalidated && r.nc_acquired==6 && r.nc_restore_published==1 && r.nc_table_invalidated==1 &&
  e.nc_published==6 && e.nc_acquired==6 && e.nc_restored && e.wb_invalidated_mask==1 && !e.cpu_invalidated && e.cpu_restored &&
  !f.active && f.phase==5 && f.complete==1 && !f.code && !f.bridge_status && !f.prepare_code && f.firmware_valid &&
  f.next==9 && f.validated==0x1ff && f.published==0x1ff && f.content_attempted && !f.content_status && !f.content_code &&
  f.summary.count==8 && f.summary.allocated==0x1ff && f.summary.validated==0x1ff && f.summary.retained==0x1ff && !f.summary.released &&
  !f.summary.uncertain_allocate_outputs && f.summary.published && f.summary.hfi_initialized && f.summary.audit_complete;
}
static int houjiSecureCxOwner(void*cookie) {
 auto&p=gHoujiGpuSecureCx;
 if(cookie!=&p || !p.active || p.in_save || p.lost || p.complete || __atomic_load_n(&gHoujiGpuSecureCxOnce,__ATOMIC_ACQUIRE)!=1 || !houjiSecureCxConfiguration())return 1;
 return 0;
}
/* Inspect immutable completed owners through actual descriptors. No old helper,
 * old active token, cache operation, data alias access, ref change or release. */
template<class Info> static bool houjiSecureCxBo(const NativeAdrenoMemory&bo,const Info&b,unsigned refs,bool nc,bool external,unsigned slot,const uint64_t*er,const uint64_t*ew,const uint64_t*ear,const uint64_t*eaw) {
 auto&p=gHoujiGpuSecureCx;p.lease_slot=slot;
 if(!b.allocated || !bo.backed() || bo.quarantined() || bo.imported() || bo.readOnly() || bo.deviceReferences()!=refs ||
  bo.writeCombined()!=nc || bo.cpuPageBytes()!=16384 || bo.bytes()!=b.logical || bo.ownedBytes()!=b.owned ||
  !b.logical || !b.owned || (b.owned&16383) || bo.ownedSpanCount()!=1 || bo.spanCount()!=1 || !bo.descriptor() ||
  bo.mapping()!=(void*)(uintptr_t)b.va || ((b.va|b.pa)&16383) || b.va>UINT64_MAX-b.owned || b.pa>=(UINT64_C(1)<<36) ||
  b.owned>(UINT64_C(1)<<36)-b.pa || bo.descriptor()->getLength()!=b.owned || b.owned/4096>40 ||
  !gPhysSize || gPhysBase>UINT64_MAX-gPhysSize || b.pa<gPhysBase || b.owned>gPhysSize || b.pa-gPhysBase>gPhysSize-b.owned)return false;
 const auto&o=bo.ownedSpans()[0];const auto&s=bo.spans()[0];NativeAdrenoCpuView view{};
 if(o.offset || s.offset || o.physical!=b.pa || s.physical!=b.pa || o.bytes!=b.owned || s.bytes!=b.logical ||
  !bo.cpuView(&view) || view.spans!=bo.ownedSpans() || view.count!=1 || view.bytes!=b.owned || b.rows!=b.owned/4096 || b.alias_rows!=b.owned/4096)return false;
 for(unsigned row=0;row<b.owned/4096;row++) {
  p.lease_row=row;const uint64_t offset=(uint64_t)row*4096;IOByteCount length=0;
  if(bo.descriptor()->getPhysicalSegment(offset,&length,kIOMemoryMapperNone)!=b.pa+offset || length<4096)return false;
  auto&v=external?p.gec_par[slot][row]:p.firmware_par[slot][row];v={};
  if(!houjiSecureCxAt(b.va+offset+0x5a,false,&v.read) || !houjiSecureCxAt(b.va+offset+0x5a,true,&v.write))return false;p.at_calls+=2;
  const uint64_t alias=(uint64_t)phystokv(b.pa+offset+0x5a);
  if(!alias || !houjiSecureCxAt(alias,false,&v.alias_read) || !houjiSecureCxAt(alias,true,&v.alias_write))return false;p.at_calls+=2;
  const uint64_t expected_r=er[row],expected_w=ew[row],expected_ar=ear[row],expected_aw=eaw[row];
  p.lease_observed=v.read;p.lease_expected=expected_r;
  if((v.read&1) || v.read!=v.write || (v.read>>56)!=(nc?0x44u:0xffu) || ((v.read>>7)&3)==1 ||
   ((v.read&UINT64_C(0x0000fffffffff000))|((b.va+offset+0x5a)&4095))!=b.pa+offset+0x5a ||
   (v.alias_read&1) || v.alias_read!=v.alias_write || ((v.alias_read>>56)!=0xff && (v.alias_read>>56)!=0x44) || ((v.alias_read>>7)&3)==1 ||
   ((v.alias_read&UINT64_C(0x0000fffffffff000))|(alias&4095))!=b.pa+offset+0x5a ||
   v.read!=expected_r || v.write!=expected_w || v.alias_read!=expected_ar || v.alias_write!=expected_aw)return false;
 }
 return true;
}
/* Both completed Info shapes remain private and immutable. */
static bool houjiSecureCxLease(void) {
 auto&p=gHoujiGpuSecureCx;++p.lease_calls;if(!houjiSecureCxConfiguration())return false;
 NativeAdrenoCpuState cpu{};if(NativeAdrenoQueryCpuPages(&cpu) || PAGE_SIZE!=16384 || cpu.kernel_page_bytes!=16384 || cpu.cpu_page_shift!=14 || (cpu.tcr&(UINT64_C(1)<<23)))return false;
 const auto&e=gHoujiGpuExternalCanary;const auto&f=gHoujiFirmwareMemory;
 if(e.memory.owned.tables!=e.bo[0].mapping() || e.memory.owned.data!=e.bo[1].mapping() || e.memory.zero_cpu!=e.bo[2].mapping() ||
  e.memory.owned.table_physical!=e.info[0].pa || e.memory.owned.data_physical!=e.info[1].pa || e.memory.zero_physical!=e.info[2].pa ||
  e.memory.cpu_page_bytes!=16384 || e.memory.table_owned_bytes!=16384 || e.memory.data_owned_bytes!=16384 || e.memory.zero_owned_bytes!=16384)return false;
 for(unsigned i=0;i<3;i++)if(e.info[i].logical!=(i?4096u:16384u) || e.info[i].owned!=16384)return false;
 const unsigned fw_pages[]={19,27,19,37,2,18,16,1,10};
 for(unsigned i=0;i<9;i++)if(f.info[i].logical!=(uint64_t)fw_pages[i]*4096)return false;
 for(unsigned i=0;i<3;i++)if(!e.info[i].identity_ready || !e.info[i].held || e.info[i].nc!=(i!=GECN_TABLE) || !houjiSecureCxBo(e.bo[i],e.info[i],1,i!=GECN_TABLE,true,i,e.info[i].initial_read_par,e.info[i].initial_write_par,e.info[i].initial_alias_read_par,e.info[i].initial_alias_write_par))return false;
 for(unsigned i=0;i<9;i++)if(f.info[i].nc!=(i>=5 && i<=7) || !f.info[i].published || !houjiSecureCxBo(f.bo[i],f.info[i],0,i>=5 && i<=7,false,i,f.info[i].read_par,f.info[i].write_par,f.info[i].alias_read_par,f.info[i].alias_write_par))return false;
 uint64_t va[12],pa[12],bytes[12];
 for(unsigned i=0;i<12;i++){if(i<3){va[i]=e.info[i].va;pa[i]=e.info[i].pa;bytes[i]=e.info[i].owned;}else{va[i]=f.info[i-3].va;pa[i]=f.info[i-3].pa;bytes[i]=f.info[i-3].owned;}}
 for(unsigned i=0;i<12;i++)for(unsigned j=0;j<i;j++)if((pa[i]<pa[j]+bytes[j] && pa[j]<pa[i]+bytes[i]) || (va[i]<va[j]+bytes[j] && va[j]<va[i]+bytes[i]))return false;
 return true;
}
static bool houjiSecureCxRawAllowed(uint32_t a) {
 if(hph_read_allowed(a) || a==0x3d99144u)return true;
 for(unsigned i=0;i<9;i++)if(a==gec_field_address(i) || ((i==5 || i==6) && a==gec_field_address(i)+4))return true;
 return a==HGP_CB(4) || a==0x3da6008u || a==0x3da600cu || a==HGP_CB(0x58) || a==HGP_CB(0x60) || a==HGP_CB(0x64) || a==HGP_CB(0x68);
}
static GPP_U64 houjiSecureCxRead(void*cookie,GPP_U32 address,GPP_U32*value) {
 auto&p=gHoujiGpuSecureCx;if(!value || !p.in_observe || !houjiSecureCxRawAllowed(address) || (address&3) || houjiSecureCxOwner(cookie))return HGSC_OWNER;
 auto&o=p.observations[p.current_observation];unsigned required=0xff;
 for(unsigned i=0;i<8;i++)if(address==hgb_address(i)){required=(1u<<i)-1;break;}
 if((o.gates&required)!=required)return HGSC_READ;
 unsigned window=4;for(unsigned i=0;i<4;i++)if(address>=kHoujiSecureBase[i] && address-kHoujiSecureBase[i]<=kHoujiSecureBytes[i]-4){window=i;break;}
 if(window==4)return HGSC_READ;
 if(!p.maps[window]) {
  p.map_attempted|=1u<<window;IODeviceMemory*memory=IODeviceMemory::withRange(kHoujiSecureBase[window],kHoujiSecureBytes[window]);if(!memory)return HGSC_MAP;
  p.maps[window]=OSSharedPtr<IOMemoryMap>(memory->map(kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly),OSNoRetain);memory->release();if(!p.maps[window])return HGSC_MAP;
 }
 const uint64_t va=p.maps[window]->getVirtualAddress(),offset=address-kHoujiSecureBase[window];
 if(!va || (va&3) || p.maps[window]->getLength()<kHoujiSecureBytes[window] || va>UINT64_MAX-offset-4)return HGSC_MAP;
 p.map_valid|=1u<<window;*value=*reinterpret_cast<const volatile uint32_t*>((uintptr_t)(va+offset));houjiSecureCxReadBarrier();++o.reads;
 const uint32_t masks[]={0x80000003u,0x80000003u,0x80000001u,1,0x80000000u,0x80000000u,0xffffffffu,0x80000001u};
 const uint32_t wanted[]={1,1,1,0,0x80000000u,0,1,1};
 for(unsigned i=0;i<8;i++)if(address==hgb_address(i)){if((*value&masks[i])==wanted[i])o.gates|=1u<<i;else o.gates&=~(1u<<i);break;}
 return 0;
}
static bool houjiSecureCxCold(unsigned kind,unsigned*out=nullptr) {
 auto&p=gHoujiGpuSecureCx;if(houjiSecureCxOwner(&p) || p.in_observe || p.observation_count>=HGSC_OBSERVATIONS)return houjiSecureCxFail(HGSC_OWNER);
 const unsigned i=p.observation_count++;if(out)*out=i+1;p.current_observation=i;auto&o=p.observations[i];o.kind=kind;o.attempted=1;
 if(!houjiSecureCxLease())return houjiSecureCxFail(HGSC_LEASE);o.lease_before=1;p.in_observe=1;
 const struct hph_ro_io io={&p,houjiSecureCxRead,houjiSecureCxOwner};
 o.code=gec_observe_ro(&io,1,&o.snapshot,&o.failed);p.in_observe=0;o.returned=1;
 o.recheck=hph_recheck(&gHoujiGpuExternalCanary.record,&o.snapshot.cold,&o.snapshot.clock);
 if(o.code || o.recheck || o.gates!=0xff || houjiSecureCxOwner(&p) || !gec_state_qualified(&gHoujiGpuExternalCanary.record,&gHoujiGpuExternalCanary.memory,&gHoujiGpuExternalCanary.result,&o.snapshot,1) || o.snapshot.context[0] || o.snapshot.context[5] || o.snapshot.context[6] || o.snapshot.actlr!=3 || o.snapshot.prr_lo || o.snapshot.prr_hi || (o.snapshot.fsr&0xc00001feu))return houjiSecureCxFail(HGSC_COLD,o.failed);
 if(!houjiSecureCxLease())return houjiSecureCxFail(HGSC_LEASE);o.lease_after=1;return true;
}
static int houjiSecureCxPrerequisites(void*cookie,unsigned operation) {
 auto&p=gHoujiGpuSecureCx;++p.prerequisites;return cookie==&p && operation==GGS_INIT && houjiSecureCxCold(0)?1:0;
}
static int houjiSecureCxDelay(void*cookie,unsigned ms) {
 auto&p=gHoujiGpuSecureCx;if(cookie!=&p || ms!=GGS_BUSY_DELAY_MS || houjiSecureCxOwner(cookie))return 1;
 ++p.delay_calls;IOSleep(ms);return houjiSecureCxOwner(cookie)?1:0;
}
static bool houjiSecureCxFrame(const struct ggs_regs&r) {
 const auto&p=gHoujiGpuSecureCx;const unsigned phase=p.scm.phase;const uint64_t f=phase==GGS_PHASE_CONVENTION?0xc2000601u:phase==GGS_PHASE_AVAILABILITY?0x42000601u:phase==GGS_PHASE_INIT?0x42002801u:0;
 const uint64_t a=phase==GGS_PHASE_CONVENTION?0x02000601u:phase==GGS_PHASE_AVAILABILITY?0x02002801u:GGS_INIT_REQUEST;
 if(!f || r.x[1]!=1 || r.x[2]!=a || r.x[3] || r.x[4] || r.x[5] || r.x[7])return false;
 if(r.x[0]==f)return r.x[6]==0;
 return r.x[0]==1 && p.call_count && p.calls[p.call_count-1].returned && p.calls[p.call_count-1].phase==phase && p.calls[p.call_count-1].output.x[0]==1 && r.x[6]==p.calls[p.call_count-1].output.x[6];
}
static int houjiSecureCxCall(void*cookie,struct ggs_regs*registers) {
 auto&p=gHoujiGpuSecureCx;if(cookie!=&p || !registers || houjiSecureCxOwner(cookie) || p.call_count>=HGSC_CALLS || !houjiSecureCxFrame(*registers)){houjiSecureCxFail(HGSC_FRAME);return 1;}
 auto&c=p.calls[p.call_count++];c.input=*registers;c.phase=p.scm.phase;
 if(!houjiSecureCxCold(1,&c.pre))return 1;
 c.intent=1;IOLog("HOUJI SCM INTENT: call=%u phase=%u x0=%llx x1=%llx x2=%llx x3=%llx x4=%llx x5=%llx x6=%llx x7=%llx; fixed secure-CX only, no firmware execution\n",p.call_count,c.phase,(unsigned long long)c.input.x[0],(unsigned long long)c.input.x[1],(unsigned long long)c.input.x[2],(unsigned long long)c.input.x[3],(unsigned long long)c.input.x[4],(unsigned long long)c.input.x[5],(unsigned long long)c.input.x[6],(unsigned long long)c.input.x[7]);
 p.in_save=1;houji_ufs_checkpoint("secure-CX fixed SCM intent; fresh cold and retained NC/FW leases verified; no stock firmware execution",0);p.in_save=0;c.saved=1;++p.intent_saves;
 if(!houjiSecureCxCold(2,&c.ready))return 1;
 c.attempted=1;++p.actual_calls;c.transport=(unsigned)houjiSecureCxSmc(registers);c.output=*registers;c.returned=1;++p.returned_calls;
 IOLog("HOUJI SCM RAW RETURN: call=%u phase=%u returned=1 transport=%u POST_COLD_PENDING x0=%llx x1=%llx x2=%llx x3=%llx x4=%llx x5=%llx x6=%llx x7=%llx; RAM log only, no save permission\n",p.call_count,c.phase,c.transport,(unsigned long long)c.output.x[0],(unsigned long long)c.output.x[1],(unsigned long long)c.output.x[2],(unsigned long long)c.output.x[3],(unsigned long long)c.output.x[4],(unsigned long long)c.output.x[5],(unsigned long long)c.output.x[6],(unsigned long long)c.output.x[7]);
 if(!houjiSecureCxCold(3,&c.post))return 1;c.post_ok=1;
 IOLog("HOUJI SCM POST COLD: call=%u proof=%u POST_COLD_PROVEN; actual raw cold/context/clock and retained BO leases verified, no standalone save permission\n",p.call_count,c.post);
 return c.transport?1:0;
}
static void houjiSecureCxReport(void) {
 const auto&p=gHoujiGpuSecureCx;
 IOLog("HOUJI SCM RESULT: phase=%u native=%u SCM=%u complete=%u quarantine=%u convention=%u available=%u init=%u attempts=%u returned=%u busy=%u interrupted=%u cold=%u refs retained; SMC return/reset1 never DMA drain proof\n",p.phase,p.code,p.scm.result,p.complete,p.scm.quarantined,p.scm.convention_verified,p.scm.available,p.scm.init_succeeded,p.actual_calls,p.returned_calls,p.scm.busy_retries,p.scm.interrupt_resumes,p.observation_count);
 for(unsigned i=0;i<p.call_count;i++){const auto&c=p.calls[i];IOLog("HOUJI SCM RETURNED: call=%u phase=%u intent=%u/%u attempted=%u returned=%u transport=%u cold-index=%u/%u/%u post-ok=%u raw x0=%llx x1=%llx x2=%llx x3=%llx x4=%llx x5=%llx x6=%llx x7=%llx\n",i+1,c.phase,c.intent,c.saved,c.attempted,c.returned,c.transport,c.pre,c.ready,c.post,c.post_ok,(unsigned long long)c.output.x[0],(unsigned long long)c.output.x[1],(unsigned long long)c.output.x[2],(unsigned long long)c.output.x[3],(unsigned long long)c.output.x[4],(unsigned long long)c.output.x[5],(unsigned long long)c.output.x[6],(unsigned long long)c.output.x[7]);}
}
static bool houjiGpuSecureCxProbeOnce(void) {
 int enabled=0;PE_parse_boot_argn("houji_gpu_scm_probe",&enabled,sizeof(enabled));if(!enabled)return true;
 auto&p=gHoujiGpuSecureCx;if(enabled!=1)return houjiSecureCxFail(HGSC_FLAGS);unsigned expected=0;
 if(!__atomic_compare_exchange_n(&gHoujiGpuSecureCxOnce,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return p.complete==1 && !p.active && !p.lost;
 p.enabled=p.active=1;p.phase=1;
 if(!houjiSecureCxConfiguration()){houjiSecureCxFail(HGSC_LEASE);p.active=0;houjiSecureCxReport();return false;}
 const struct ggs_ops ops={&p,houjiSecureCxCall,houjiSecureCxDelay,houjiSecureCxPrerequisites};p.phase=2;
 const int result=ggs_run(&ops,&p.scm,GGS_INIT);
 if(result || p.scm.quarantined || !p.scm.init_succeeded || !p.scm.convention_verified || !p.scm.available || p.actual_calls!=p.returned_calls || !houjiSecureCxCold(4)){if(!p.code)p.code=HGSC_SCM;p.lost=1;p.active=0;p.phase=4;houjiSecureCxReport();return false;}
 p.phase=3;houjiSecureCxReport();p.in_save=1;houji_ufs_checkpoint("secure-CX fixed INIT returned success; new full cold and retained NC/FW leases verified; stock firmware not executed",0);p.in_save=0;p.report_saved=1;
 if(!houjiSecureCxCold(5)){p.active=0;p.phase=4;houjiSecureCxReport();return false;}p.report_returned=1;p.complete=1;p.active=0;p.phase=4;houjiSecureCxReport();return true;
}
#endif
