/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * One original SM8650 stock GMU firmware/HFI attempt on Native XNU backing.
 * Independent owner/maps, after the same worker's completed NC canary, CPU
 * firmware preparation and fixed secure-CX INIT. No CP, PAS/ZAP or rendering.
 * Always terminal: all references/maps/TCM retained until platform reset.
 * A returned/reset-held firmware is NOT a DMA drain or filesystem permission.
 */
#ifndef HOUJI_GPU_STOCK_FIRMWARE_PROBE_INC
#define HOUJI_GPU_STOCK_FIRMWARE_PROBE_INC
#include "houji_stock_native.h"
#include "HoujiStockMmioPolicy.h"
enum {HSNP_WINDOWS=9};
enum {HSNP_OK=0,HSNP_FLAGS,HSNP_OWNER,HSNP_LEASE,HSNP_MAP,
 HSNP_ADDRESS,HSNP_COLD,HSNP_PUBLISH,HSNP_BRIDGE,HSNP_FIRMWARE,HSNP_ORDER};
static const uint64_t kHoujiStockBase[HSNP_WINDOWS]={0x170000,0x3d90000,0x3d7c000,0x3da0000,0x3d6c000,0x3d8c000,0x3d50000,0xb284000,0x3d0c000};
static const uint64_t kHoujiStockBytes[HSNP_WINDOWS]={0x4000,0x10000,0x4000,0x40000,0x10000,0x4000,0x4000,0x4000,0x4000};
static unsigned gHoujiGpuStockOnce;
static struct {
 HsnStorage storage;HsnSummary summary;HsnFailure failure;HsnHostProfileSummary profile;unsigned profile_mode;
 OSSharedPtr<IOMemoryMap> maps[HSNP_WINDOWS];
 struct Par {uint64_t read,write,alias_read,alias_write;} gec_par[3][4],firmware_par[9][40];
 struct Observation {struct gec_snapshot snapshot;unsigned attempted,returned,code,recheck,failed,reads,gates;} cold[8];
 ArmSmmu4KTableAuditResult audit;unsigned char visited[10];
 unsigned active,lost,code,complete,bridge_status,stock_code,held_mask,zero_held;
 unsigned in_observe,current_observation,observation_count,in_save;
 unsigned actual_reads,actual_writes,barriers,delay_calls,map_attempted,map_valid,lease_calls,at_calls;
 unsigned lease_slot,lease_row,publishes,checkpoints,possible_execution,reset_attempted,reset_returned;
 unsigned pending_write,first_address,first_operation,first_width,first_attempted,first_returned,phase;
 uint64_t lease_observed,lease_expected,first_value,last_value;unsigned last_address,last_operation,last_width,last_attempted,last_returned;
 HsnpBindings bindings;uint64_t context[9],zero_pa;uint32_t table_crc,clock_gates;
 unsigned table_crc_valid,post_reset_observed,pinning_attempted;uint32_t post_stream[24],post_route[24],post_bank[22];
 uint64_t post_context[9];uint32_t post_fsr,post_gfsr,post_reset;
} gHoujiGpuStock;
#ifdef HOUJI_GPU_STOCK_FIRMWARE_HOST_TEST
static void houjiStockReadBarrier(void);
static void houjiStockWriteBarrier(void);
static void houjiStockFullBarrier(void);
#else
static void houjiStockReadBarrier(void){__asm__ volatile("dmb oshld":::"memory");}
static void houjiStockWriteBarrier(void){__asm__ volatile("dsb sy":::"memory");}
static void houjiStockFullBarrier(void){__asm__ volatile("dsb sy":::"memory");}
#endif
static int houjiStockFail(unsigned code,unsigned address=0) {
 auto&p=gHoujiGpuStock;if(!p.code){p.code=code;p.first_address=address;p.first_operation=p.last_operation;p.first_width=p.last_width;p.first_value=p.last_value;p.first_attempted=p.last_attempted;p.first_returned=p.last_returned;}p.lost=1;return 1;
}
/* Pure completed metadata; neither earlier owner is reactivated. BO references
 * are accounted separately by this owner; no old result becomes executable. */
static bool houjiStockConfiguration(void) {
 const auto&s=gHoujiGpuSecureCx;
 return gRootEnabled && gLogEnabled && gReturnRecovery && houjiSecureCxConfiguration() &&
  __atomic_load_n(&gHoujiGpuSecureCxOnce,__ATOMIC_ACQUIRE)==1 && !s.active && !s.lost && !s.code && s.complete==1 && s.phase==4 &&
  s.report_saved && s.report_returned && s.scm.init_succeeded && s.scm.convention_verified && s.scm.available && !s.scm.quarantined &&
  s.actual_calls==s.returned_calls && s.actual_calls>=3;
}
static int houjiStockOwner(void*cookie) {
 auto&p=gHoujiGpuStock;return cookie==&p && p.active && !p.lost && !p.in_save && !p.complete &&
  __atomic_load_n(&gHoujiGpuStockOnce,__ATOMIC_ACQUIRE)==1 && houjiStockConfiguration()?0:1;
}
/* This prototype is followed by the exact address/descriptor/AT lease checks.
 * No data access through phystokv; the old WB alias remains dormant. */
template<class Info> static bool houjiStockBo(const NativeAdrenoMemory&bo,const Info&b,unsigned refs,bool nc,bool external,unsigned slot,const uint64_t*er,const uint64_t*ew,const uint64_t*ear,const uint64_t*eaw) {
 auto&p=gHoujiGpuStock;p.lease_slot=slot;
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
static bool houjiStockLease(void) {
 auto&p=gHoujiGpuStock;++p.lease_calls;if(!houjiStockConfiguration())return false;
 NativeAdrenoCpuState cpu{};if(NativeAdrenoQueryCpuPages(&cpu) || PAGE_SIZE!=16384 || cpu.kernel_page_bytes!=16384 || cpu.cpu_page_shift!=14 || (cpu.tcr&(UINT64_C(1)<<23)))return false;
 const auto&e=gHoujiGpuExternalCanary;const auto&f=gHoujiFirmwareMemory;
 if(e.memory.owned.tables!=e.bo[0].mapping() || e.memory.owned.data!=e.bo[1].mapping() || e.memory.zero_cpu!=e.bo[2].mapping() ||
  e.memory.owned.table_physical!=e.info[0].pa || e.memory.owned.data_physical!=e.info[1].pa || e.memory.zero_physical!=e.info[2].pa ||
  e.memory.cpu_page_bytes!=16384 || e.memory.table_owned_bytes!=16384 || e.memory.data_owned_bytes!=16384 || e.memory.zero_owned_bytes!=16384)return false;
 for(unsigned i=0;i<3;i++)if(e.info[i].logical!=(i?4096u:16384u) || e.info[i].owned!=16384)return false;
 const unsigned fw_pages[]={19,27,19,37,2,18,16,1,10};
 for(unsigned i=0;i<9;i++)if(f.info[i].logical!=(uint64_t)fw_pages[i]*4096)return false;
 for(unsigned i=0;i<3;i++)if(!e.info[i].identity_ready || !e.info[i].held || e.info[i].nc!=(i!=GECN_TABLE) || !houjiStockBo(e.bo[i],e.info[i],1u+(i==2?p.zero_held:0u),i!=GECN_TABLE,true,i,e.info[i].initial_read_par,e.info[i].initial_write_par,e.info[i].initial_alias_read_par,e.info[i].initial_alias_write_par))return false;
 for(unsigned i=0;i<9;i++)if(f.info[i].nc!=(i>=5 && i<=7) || !f.info[i].published || !houjiStockBo(f.bo[i],f.info[i],(p.held_mask>>i)&1u,i>=5 && i<=7,false,i,f.info[i].read_par,f.info[i].write_par,f.info[i].alias_read_par,f.info[i].alias_write_par))return false;
 uint64_t va[12],pa[12],bytes[12];
 for(unsigned i=0;i<12;i++){if(i<3){va[i]=e.info[i].va;pa[i]=e.info[i].pa;bytes[i]=e.info[i].owned;}else{va[i]=f.info[i-3].va;pa[i]=f.info[i-3].pa;bytes[i]=f.info[i-3].owned;}}
 for(unsigned i=0;i<12;i++)for(unsigned j=0;j<i;j++)if((pa[i]<pa[j]+bytes[j] && pa[j]<pa[i]+bytes[i]) || (va[i]<va[j]+bytes[j] && va[j]<va[i]+bytes[i]))return false;
 return true;
}
static bool houjiStockRawColdAllowed(uint32_t a) {return houjiSecureCxRawAllowed(a);}
static volatile uint8_t*houjiStockPointer(uint32_t address,unsigned width,bool write) {
 auto&p=gHoujiGpuStock;unsigned window=HSNP_WINDOWS;
 if((width!=4 && width!=8) || (address&(width-1)))return nullptr;
 for(unsigned i=0;i<HSNP_WINDOWS;i++)if(address>=kHoujiStockBase[i] && address-kHoujiStockBase[i]<=kHoujiStockBytes[i]-width){window=i;break;}
 if(window==HSNP_WINDOWS || (write && window<2))return nullptr;
 if(!p.maps[window]) {
  p.map_attempted|=1u<<window;IODeviceMemory*memory=IODeviceMemory::withRange(kHoujiStockBase[window],kHoujiStockBytes[window]);if(!memory)return nullptr;
  p.maps[window]=OSSharedPtr<IOMemoryMap>(memory->map(kIOMapAnywhere|kIOMapInhibitCache|(window<2?kIOMapReadOnly:0)),OSNoRetain);memory->release();if(!p.maps[window])return nullptr;
 }
 const uint64_t va=p.maps[window]->getVirtualAddress(),offset=address-kHoujiStockBase[window];
 if(!va || (va&16383) || p.maps[window]->getLength()!=kHoujiStockBytes[window] || va>UINT64_MAX-offset-width)return nullptr;
 p.map_valid|=1u<<window;return reinterpret_cast<volatile uint8_t*>((uintptr_t)(va+offset));
}
static void houjiStockObserveGate(uint32_t a,uint32_t value) {
 auto&p=gHoujiGpuStock;
 for(unsigned i=0;i<7;i++)if(a==hsnp_gate_address(i)){const uint32_t mask=hsnp_gate_mask(i),wanted=hsnp_gate_value(i);if((value&mask)==wanted)p.clock_gates|=1u<<i;else p.clock_gates&=~(1u<<i);break;}
}
static bool houjiStockGateRead(uint32_t a) {
 auto&p=gHoujiGpuStock;
 for(unsigned i=0;i<7;i++)if(a==hsnp_gate_address(i))return (p.clock_gates&((1u<<i)-1))==((1u<<i)-1);
 return p.clock_gates==0x7f;
}
static GPP_U64 houjiStockColdRead(void*cookie,GPP_U32 a,GPP_U32*v) {
 auto&p=gHoujiGpuStock;if(!v || !p.in_observe || !houjiStockRawColdAllowed(a) || houjiStockOwner(cookie))return HSNP_OWNER;
 auto&o=p.cold[p.current_observation];unsigned required=0xff;
 for(unsigned i=0;i<8;i++)if(a==hgb_address(i)){required=(1u<<i)-1;break;}
 if((o.gates&required)!=required)return HSNP_COLD;
 auto*ptr=houjiStockPointer(a,4,false);if(!ptr)return HSNP_MAP;*v=*reinterpret_cast<const volatile uint32_t*>(ptr);houjiStockReadBarrier();++o.reads;
 const uint32_t mask[]={0x80000003u,0x80000003u,0x80000001u,1,0x80000000u,0x80000000u,0xffffffffu,0x80000001u},want[]={1,1,1,0,0x80000000u,0,1,1};
 for(unsigned i=0;i<8;i++)if(a==hgb_address(i)){if((*v&mask[i])==want[i])o.gates|=1u<<i;else o.gates&=~(1u<<i);break;}
 return 0;
}
static bool houjiStockCold(void) {
 auto&p=gHoujiGpuStock;if(houjiStockOwner(&p) || p.in_observe || p.possible_execution || p.observation_count>=8 || !houjiStockLease())return false;
 p.current_observation=p.observation_count++;auto&o=p.cold[p.current_observation];o.attempted=1;p.in_observe=1;
 const struct hph_ro_io io={&p,houjiStockColdRead,houjiStockOwner};o.code=gec_observe_ro(&io,1,&o.snapshot,&o.failed);p.in_observe=0;o.returned=1;
 o.recheck=hph_recheck(&gHoujiGpuExternalCanary.record,&o.snapshot.cold,&o.snapshot.clock);
 return !o.code && !o.recheck && o.gates==0xff &&
  gec_state_qualified(&gHoujiGpuExternalCanary.record,&gHoujiGpuExternalCanary.memory,&gHoujiGpuExternalCanary.result,&o.snapshot,1) &&
  !o.snapshot.context[0] && !o.snapshot.context[5] && !o.snapshot.context[6] && o.snapshot.actlr==3 && !o.snapshot.prr_lo && !o.snapshot.prr_hi &&
  !(o.snapshot.fsr&0xc00001feu) && houjiStockLease();
}
static void houjiStockStage(void) {
 auto&p=gHoujiGpuStock;HsnSummary summary{};
 if(!hsn_summary(&p.storage,sizeof(p.storage),&summary,sizeof(summary))){p.summary=summary;if(p.phase!=summary.hgf_phase){p.phase=summary.hgf_phase;IOLog("HOUJI STOCK PHASE: %u FW=%u HFI=%u QUERY=%u/%u reset=%u possible-exec=%u; RAM-only after release\n",p.phase,summary.fw_ready,summary.hfi_ready,summary.query_sent,summary.query_ack,summary.reset_held,p.possible_execution);}}
}
static int houjiStockRead32(void*cookie,uint32_t a,uint32_t*v) {
 auto&p=gHoujiGpuStock;if(!v || houjiStockOwner(cookie) || p.in_observe || !hsnp_allowed_read(a,32) || !houjiStockGateRead(a))return houjiStockFail(HSNP_ADDRESS,a);
 if(a==hsnp_gate_address(0))p.clock_gates=0;
 houjiStockStage();p.last_address=a;p.last_operation=1;p.last_width=4;p.last_attempted=p.last_returned=0;
 auto*ptr=houjiStockPointer(a,4,false);if(!ptr)return houjiStockFail(HSNP_MAP,a);p.last_attempted=1;
 *v=*reinterpret_cast<const volatile uint32_t*>(ptr);houjiStockReadBarrier();p.last_returned=1;p.last_value=*v;++p.actual_reads;houjiStockObserveGate(a,*v);return 0;
}
static int houjiStockRead64(void*cookie,uint32_t a,uint64_t*v) {
 auto&p=gHoujiGpuStock;if(!v || houjiStockOwner(cookie) || p.in_observe || !hsnp_allowed_read(a,64) || p.clock_gates!=0x7f)return houjiStockFail(HSNP_ADDRESS,a);
 houjiStockStage();p.last_address=a;p.last_operation=1;p.last_width=8;p.last_attempted=p.last_returned=0;
 auto*ptr=houjiStockPointer(a,8,false);if(!ptr)return houjiStockFail(HSNP_MAP,a);p.last_attempted=1;
 *v=*reinterpret_cast<const volatile uint64_t*>(ptr);houjiStockReadBarrier();p.last_returned=1;p.last_value=*v;++p.actual_reads;return 0;
}
static int houjiStockWrite32(void*cookie,uint32_t a,uint32_t v) {
 auto&p=gHoujiGpuStock;if(houjiStockOwner(cookie) || p.in_observe || p.clock_gates!=0x7f || p.held_mask!=0x1ff || !p.zero_held ||
  !hsnp_allowed_write(&p.bindings,a,32,v))return houjiStockFail(HSNP_ADDRESS,a);
 if(p.possible_execution && !((a==0x3d7e000 || a==0x3d7e618 || a==0x3d7e650) && v==1))return houjiStockFail(HSNP_ORDER,a);
 houjiStockStage();p.last_address=a;p.last_operation=2;p.last_width=4;p.last_value=v;p.last_attempted=p.last_returned=0;
 auto*ptr=houjiStockPointer(a,4,true);if(!ptr)return houjiStockFail(HSNP_MAP,a);
 if(a==0x3d7e000 && !v){if(p.checkpoints!=2 || p.publishes!=2 || p.possible_execution || !houjiStockLease())return houjiStockFail(HSNP_ORDER,a);p.possible_execution=1;IOLog("HOUJI STOCK RELEASE: about to deassert CM3 reset; no later FS, cache operation, unmap, ref retirement or retry\n");}
 if(a==0x3d7e000 && v==1)p.reset_attempted=1;
 p.pending_write=1;p.last_attempted=1;*reinterpret_cast<volatile uint32_t*>(ptr)=v;houjiStockWriteBarrier();p.pending_write=0;p.last_returned=1;++p.actual_writes;
 if(a==0x3d7e000 && v==1)p.reset_returned=1;return 0;
}
static int houjiStockWrite64(void*cookie,uint32_t a,uint64_t v) {
 auto&p=gHoujiGpuStock;if(houjiStockOwner(cookie) || p.in_observe || p.clock_gates!=0x7f || p.held_mask!=0x1ff || !p.zero_held || p.possible_execution ||
  !hsnp_allowed_write(&p.bindings,a,64,v))return houjiStockFail(HSNP_ADDRESS,a);
 houjiStockStage();p.last_address=a;p.last_operation=2;p.last_width=8;p.last_value=v;p.last_attempted=p.last_returned=0;
 auto*ptr=houjiStockPointer(a,8,true);if(!ptr)return houjiStockFail(HSNP_MAP,a);p.pending_write=1;p.last_attempted=1;
 *reinterpret_cast<volatile uint64_t*>(ptr)=v;houjiStockWriteBarrier();p.pending_write=0;p.last_returned=1;++p.actual_writes;return 0;
}
static int houjiStockBarrier(void*cookie){auto&p=gHoujiGpuStock;if(houjiStockOwner(cookie) || p.pending_write)return houjiStockFail(HSNP_OWNER);houjiStockFullBarrier();++p.barriers;return 0;}
static int houjiStockDelay(void*cookie,unsigned us){auto&p=gHoujiGpuStock;if(houjiStockOwner(cookie) || us!=10 || p.pending_write)return houjiStockFail(HSNP_OWNER);IODelay(us);++p.delay_calls;return houjiStockOwner(cookie)?1:0;}
static int houjiStockPrerequisite(void*cookie){
 auto&p=gHoujiGpuStock;if(cookie!=&p || !houjiStockCold())return 0;
 /* The bridge has just verified the public inactive-HHI contract with zero
  * firmware device references and sealed its exact graph. Acquire references
  * at the first original HGF prerequisite, before any register publication. */
 if(!p.pinning_attempted){p.pinning_attempted=1;
  for(unsigned i=0;i<9;i++){if(gHoujiFirmwareMemory.bo[i].holdForDevice())return houjiStockFail(HSNP_LEASE),0;p.held_mask|=1u<<i;}
  if(gHoujiGpuExternalCanary.bo[2].holdForDevice())return houjiStockFail(HSNP_LEASE),0;p.zero_held=1;
 }
 if(p.held_mask!=0x1ff || !p.zero_held || !houjiStockLease())return houjiStockFail(HSNP_LEASE),0;p.clock_gates=0x7f;return 1;
}
static int houjiStockTableRead(void*cookie,uint64_t pa,uint64_t*value){const auto&b=gHoujiFirmwareMemory.info[8];if(cookie!=&gHoujiGpuStock || !value || (pa&7) || pa<b.pa || pa-b.pa>40960-8)return 1;*value=*reinterpret_cast<const volatile uint64_t*>((uintptr_t)b.va+(uintptr_t)(pa-b.pa));return 0;}
static bool houjiStockTables(void) {
 auto&p=gHoujiGpuStock;const auto&f=gHoujiFirmwareMemory;ArmSmmu4KRange ranges[8];
 for(unsigned i=0;i<8;i++){const auto&r=f.row[i].range;ranges[i]={r.iova,r.physical,r.bytes,r.permissions,r.writeback,r.shareability};}
 ArmSmmu4KConfig config={32,36,f.info[8].pa,2048};ArmSmmu4KResult built={};const auto&s=f.summary;
 built.root_physical=s.root_physical;built.mair=s.mair;built.table_pages=s.built_table_pages;built.leaf_pages=s.built_leaf_pages;built.start_level=s.start_level;built.upper_region=s.upper_region;
 const ArmSmmu4KTableIo io={&p,houjiStockTableRead};const ArmSmmu4KAuditRegion region={ranges,8,10,5120,p.visited,sizeof(p.visited)};
 if(ArmSmmu4KAuditTables(&io,&config,&built,&region,f.info[8].pa,&p.audit) || !p.audit.complete || p.audit.table_pages!=6 || p.audit.leaf_pages!=139 || p.audit.descriptor_reads!=3072)return false;
 const uint32_t crc=gpp_crc32(f.bo[8].mapping(),f.info[8].owned);if(p.table_crc_valid && crc!=p.table_crc)return false;p.table_crc=crc;p.table_crc_valid=1;return true;
}
static int houjiStockPublish(void*cookie) {
 auto&p=gHoujiGpuStock;if(houjiStockOwner(cookie) || p.possible_execution || p.publishes>=2 || p.held_mask!=0x1ff || !p.zero_held || !houjiStockLease() || !houjiStockTables())return houjiStockFail(HSNP_PUBLISH);
 /* FW/table WB backing is published as full exclusive CPU16K pages. HFI,
  * log, VRB and PRR-zero use ONLY the finite NC lease, no aperture data/DC. */
 for(unsigned i=0;i<9;i++)if(i<5 || i==8){const auto&b=gHoujiFirmwareMemory.info[i];if(NativeAdrenoMaintainOwnedPages((void*)(uintptr_t)b.va,b.owned,NativeAdrenoCleanInvalidate))return houjiStockFail(HSNP_PUBLISH);}
 houjiStockFullBarrier();++p.publishes;return houjiStockLease() && houjiStockTables()?0:houjiStockFail(HSNP_PUBLISH);
}
static int houjiStockCheckpoint(void*cookie,unsigned stage) {
 auto&p=gHoujiGpuStock;if(houjiStockOwner(cookie) || p.possible_execution || stage!=p.checkpoints+1 || stage>2 || p.pending_write || !houjiStockLease() || !houjiStockTables())return houjiStockFail(HSNP_ORDER);
 ++p.checkpoints;
 if(stage==1){if(p.actual_writes || !houjiStockCold())return houjiStockFail(HSNP_COLD);IOLog("HOUJI STOCK CHECKPOINT1: exact original firmware/HFI graph retained, pre-stock-write cold proof passed\n");p.in_save=1;houji_ufs_checkpoint("stock GMU fixed firmware/HFI attempt armed; pre-write cold proof and complete retained NC/FW leases verified",0);p.in_save=0;if(!houjiStockCold())return houjiStockFail(HSNP_COLD);}
 else IOLog("HOUJI STOCK CHECKPOINT2: original HGF verified live bank2/SID5 before release; RAM-only, every BO/ref retained\n");
 return 0;
}
static int houjiStockStopped(void*cookie) {
 auto&p=gHoujiGpuStock;if(houjiStockOwner(cookie) || !p.possible_execution || !p.reset_attempted || !p.reset_returned || !houjiStockLease())return houjiStockFail(HSNP_OWNER);
 /* Read-only post-reset diagnostics. No retirement, stream faulting, root
  * clearing, invalidate, drain claim or filesystem permission is added. */
 for(unsigned i=0;i<7;i++){uint32_t value=0;if(houjiStockRead32(cookie,hsnp_gate_address(i),&value) || (value&hsnp_gate_mask(i))!=hsnp_gate_value(i))return houjiStockFail(HSNP_COLD,hsnp_gate_address(i));}
 if(houjiStockRead32(cookie,0x3d7e000,&p.post_reset) || p.post_reset!=1 || houjiStockRead32(cookie,0x3da0048,&p.post_gfsr) || houjiStockRead32(cookie,HGP_CB(0x58),&p.post_fsr))return 1;
 for(unsigned i=0;i<24;i++)if(houjiStockRead32(cookie,0x3da0800+4*i,&p.post_stream[i]) || houjiStockRead32(cookie,0x3da0c00+4*i,&p.post_route[i]))return 1;
 for(unsigned i=0;i<22;i++)if(houjiStockRead32(cookie,0x3dc0000+4096*i,&p.post_bank[i]))return 1;
 for(unsigned i=0;i<9;i++){uint32_t narrow=0;if(i==5 || i==6){if(houjiStockRead64(cookie,gec_field_address(i),&p.post_context[i]))return 1;}else{if(houjiStockRead32(cookie,gec_field_address(i),&narrow))return 1;p.post_context[i]=narrow;}}
 p.post_reset_observed=1;return houjiStockLease()?0:houjiStockFail(HSNP_LEASE);
}
/* Terminal screen transcript reads only completed CPU records. Never reads
 * MMIO/BOs and never saves, releases or cleans up the active graph. */
static void houjiStockReport(void) {
 auto&p=gHoujiGpuStock;const auto&s=p.summary;const auto&f=p.failure;
 IOLog("HOUJI STOCK STOP1 phase=%u code=%u native=%u bridge=%u FW=%u HFI=%u QUERY=%u/%u VALUE=%u STATUS=%u\n",s.hgf_phase,s.hgf_code,p.code,p.bridge_status,s.fw_ready,s.hfi_ready,s.query_sent,s.query_ack,s.query_value,s.query_status);
 IOLog("HOUJI STOCK STOP2 failed=%u OP=%u A=%08x V=%08x valid=%u reset=%u/%u exec=%u RETAINED refs=%03x zero=%u\n",f.failed_phase,f.failed_operation,f.failed_address,f.failed_value,f.failed_observed_valid,p.reset_attempted,s.reset_held,p.possible_execution,p.held_mask,p.zero_held);
 IOLog("HOUJI STOCK STOP3 CB=%u/%u A=%08x status=%d FSR=%08x FAR=%llx GFSR=%08x IRQ=%08x/%08x\n",f.first_callback.kind,f.first_callback.phase,f.first_callback.address,f.first_callback.callback_status,s.fsr,(unsigned long long)s.far,s.gfsr,s.irq,s.ao_irq);
 IOLog("HOUJI STOCK STOP4 TCM=%u/%u RSC=%u BOOT=%u RX=%u/%u dropped=%u reads=%u writes=%u; RAM-only, HOLD 8s then PSCI\n",s.tcm_loaded_words,s.tcm_backup_words,s.rsc_attempted,s.boot_attempted,s.receive_records,s.receive_calls,s.receive_overflow,p.actual_reads,p.actual_writes);
 IOLog("HOUJI HOST PROFILE: mode=%u attempted=%u code=%u/%u stage=%u completed=%u/5 qualified=%u resources=%u/%u; no HWSCHED/AQE/CP/graphics\n",p.profile_mode,p.profile.attempted,p.profile.code,p.profile.return_code,p.profile.stage,p.profile.completed,p.profile.qualified,p.profile.resource_count,p.profile.resource_acks);
 if(p.profile_mode==2)for(unsigned i=0;i<5;i++){HsnHostProfileStep step{};if(!hsn_host_profile_step(&p.storage,sizeof(p.storage),i,&step,sizeof(step)))IOLog("HOUJI HOST STEP: i=%u H=%08x sent=%u ACK=%u value=%08x status=%08x present=%u words=%u\n",i,step.header,step.sent,step.ack,step.value,step.status,step.status_present,step.word_count);}
 for(unsigned i=0;i<s.receive_records && i<HSN_RECEIVE_RECORDS;i++){HsnReceive rx{};if(!hsn_receive_log(&p.storage,sizeof(p.storage),i,&rx,sizeof(rx)))IOLog("HOUJI STOCK RX: %u Q=%u status=%u words=%u first=%08x/%08x/%08x/%08x\n",rx.sequence,rx.queue,rx.status,rx.word_count,rx.words[0],rx.words[1],rx.words[2],rx.words[3]);}
}
static void houjiGpuStockFirmwareProbeOnce(void) {
 int enabled=0;PE_parse_boot_argn("houji_gpu_stock_probe",&enabled,sizeof(enabled));if(!enabled)return;
 auto&p=gHoujiGpuStock;unsigned expected=0;
 if((enabled!=1 && enabled!=2) || !__atomic_compare_exchange_n(&gHoujiGpuStockOnce,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))panic("HOUJI STOCK: invalid/repeated one-shot attempt; no FS, retained PSCI return");
 p.active=1;p.profile_mode=(unsigned)enabled;p.stock_code=UINT32_MAX;
 if(!houjiStockConfiguration() || !houjiStockLease() || !houjiStockCold()){houjiStockFail(HSNP_LEASE);goto terminal;}
 {const auto&f=gHoujiFirmwareMemory;ArmSmmu4KConfig cfg={32,36,f.info[8].pa,2048};ArmSmmu4KResult built={};const auto&s=f.summary;built.root_physical=s.root_physical;built.mair=s.mair;built.table_pages=s.built_table_pages;built.leaf_pages=s.built_leaf_pages;built.start_level=s.start_level;built.upper_region=s.upper_region;
  ArmSmmu4KContextConfig cc={&cfg,0,&built,0,0x4c017e18,0x5511,0x750,0,0,0};ArmSmmu4KContextImage ci{};if(ArmSmmu4KEncodeContext(&cc,&ci)){houjiStockFail(HSNP_LEASE);goto terminal;}const uint64_t fields[]={0,ci.cba2r,ci.cbar,ci.tcr2,ci.tcr,ci.ttbr0,ci.ttbr1,ci.mair0,ci.mair1};for(unsigned i=0;i<9;i++)p.bindings.encoded[i]=p.context[i]=fields[i];p.bindings.zero_physical=p.zero_pa=gHoujiGpuExternalCanary.info[2].pa;}
 if(!houjiStockLease() || !houjiStockTables()){houjiStockFail(HSNP_LEASE);goto terminal;}
 {const HsnCallbacks cb={HSN_ABI_VERSION,0,&p,houjiStockRead32,houjiStockRead64,houjiStockWrite32,houjiStockWrite64,houjiStockBarrier,houjiStockDelay,houjiStockPrerequisite,houjiStockPublish,houjiStockCheckpoint,houjiStockStopped};p.bridge_status=hsn_init(&p.storage,sizeof(p.storage),&cb,sizeof(cb));if(p.bridge_status){houjiStockFail(HSNP_BRIDGE);goto terminal;}}
 p.bridge_status=(enabled==2?hsn_run_host_profile:hsn_run)(&p.storage,sizeof(p.storage),&gHoujiFirmwareMemory.storage,sizeof(gHoujiFirmwareMemory.storage),gHoujiGmuFirmwareImage,sizeof(gHoujiGmuFirmwareImage),p.zero_pa,&p.stock_code);
 if(hsn_summary(&p.storage,sizeof(p.storage),&p.summary,sizeof(p.summary)) || hsn_failure(&p.storage,sizeof(p.storage),&p.failure,sizeof(p.failure)))houjiStockFail(HSNP_BRIDGE);
 if(hsn_host_profile_summary(&p.storage,sizeof(p.storage),&p.profile,sizeof(p.profile)))houjiStockFail(HSNP_BRIDGE);
 p.complete=!p.code && !p.lost && !p.bridge_status && !p.stock_code && p.summary.fw_ready && p.summary.hfi_ready && p.summary.query_ack && p.summary.query_value==1 && !p.summary.query_status && !p.summary.receive_overflow && (enabled==1 || (p.profile.requested && p.profile.attempted && p.profile.qualified && !p.profile.code && !p.profile.return_code && p.profile.stage==5 && p.profile.completed==5 && !p.profile.resource_count && !p.profile.resource_acks));
 terminal:p.active=0;houjiStockReport();IOSleep(8000);
 /* Even success is a terminal first stock attempt. Do NOT reach checkpoint3. */
 panic("HOUJI STOCK: one-shot complete=%u FW=%u HFI=%u ACK=%u; all resources retained, no FS; armed PSCI recovery",p.complete,p.summary.fw_ready,p.summary.hfi_ready,p.summary.query_ack);
}
#endif
