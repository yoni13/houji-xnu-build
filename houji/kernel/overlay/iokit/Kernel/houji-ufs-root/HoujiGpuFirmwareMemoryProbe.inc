/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * CPU-only preparation of the real houji firmware/HFI profile. No GPU MMIO,
 * firmware execution, TTBR/stream installation or device references here.
 */
#ifndef HOUJI_GPU_FIRMWARE_MEMORY_PROBE_INC
#define HOUJI_GPU_FIRMWARE_MEMORY_PROBE_INC
#include "houji_hfi_inactive.h"
#include "ArmSmmu4KAudit.h"
#include "Adreno740/NativeAdrenoMemory.h"
#include "Adreno740/NativeAdrenoFirmware.h"
#include "HoujiGmuFirmwareImage.h"
#ifndef HOUJI_GPU_FIRMWARE_MEMORY_HOST_TEST
#include <mach/arm/vm_param.h>
#endif
extern "C" vm_offset_t phystokv(uint64_t);
enum { HFMN_BOS=9,HFMN_MAX_ROWS=40 };
static unsigned gHoujiFirmwareMemoryOnce;
static struct {
 HhiStorage storage;HhiSummary summary;HhiRow row[HFMN_BOS];
 NativeAdrenoMemory bo[HFMN_BOS];NativeAdrenoCpuState cpu;
 struct Info {uint64_t va,pa,logical,owned,read_par[HFMN_MAX_ROWS],write_par[HFMN_MAX_ROWS],alias_read_par[HFMN_MAX_ROWS],alias_write_par[HFMN_MAX_ROWS];
  unsigned attempted,allocated,nc_attempted,nc,rows,published,alias_rows,alias_wb,alias_nc;IOReturn status;} info[HFMN_BOS];
 unsigned active,next,phase,code,bridge_status,prepare_code,firmware_valid,complete,validated,published,at_calls,content_attempted,content_status,content_code;
 unsigned first_slot,first_row,first_code;uint64_t first_value,first_expected;
 ArmSmmu4KTableAuditResult audit;unsigned char visited[10];uint64_t physical_reads;
} gHoujiFirmwareMemory;
#ifdef HOUJI_GPU_FIRMWARE_MEMORY_HOST_TEST
static bool houjiFirmwareAt(uint64_t,bool,uint64_t*);
static void houjiFirmwareBarrier(void);
#else
static bool houjiFirmwareAt(uint64_t va,bool write,uint64_t*par) {
 if(!par)return false;uint64_t daif,saved;disable_preemption();const int before=cpu_number();
 __asm__ volatile("mrs %0,daif\n\tmsr daifset,#3\n\tmrs %1,par_el1":"=r"(daif),"=r"(saved)::"memory");
 if(write)__asm__ volatile("at s1e1w,%0\n\tisb sy"::"r"(va):"memory");else __asm__ volatile("at s1e1r,%0\n\tisb sy"::"r"(va):"memory");
 __asm__ volatile("mrs %0,par_el1\n\tmsr par_el1,%1\n\tmsr daif,%2":"=&r"(*par):"r"(saved),"r"(daif):"memory");
 const int after=cpu_number();enable_preemption();return before>=0 && before==after;
}
static void houjiFirmwareBarrier(void){__asm__ volatile("dsb sy":::"memory");}
#endif
static int houjiFirmwareFail(unsigned code,unsigned slot,unsigned row=0,uint64_t value=0,uint64_t expected=0) {
 auto&p=gHoujiFirmwareMemory;if(!p.first_code){p.first_code=code;p.first_slot=slot;p.first_row=row;p.first_value=value;p.first_expected=expected;}return (int)code;
}
static unsigned houjiFirmwareSlot(uint64_t pa,const void*cpu,unsigned pages) {
 const auto&p=gHoujiFirmwareMemory;for(unsigned i=0;i<HFMN_BOS;i++)if(p.info[i].allocated && p.info[i].pa==pa && p.info[i].va==(uintptr_t)cpu && p.info[i].logical==(uint64_t)pages*4096)return i;return HFMN_BOS;
}
static bool houjiFirmwareOwn(unsigned i) {
 auto&p=gHoujiFirmwareMemory;if(i>=HFMN_BOS)return false;auto&b=p.info[i];const auto&bo=p.bo[i];
 if(!p.active || !b.allocated || !bo.backed() || bo.quarantined() || bo.imported() || bo.deviceReferences() ||
  bo.mapping()!=(void*)(uintptr_t)b.va || bo.bytes()!=b.logical || bo.ownedBytes()!=b.owned || bo.ownedSpanCount()!=1 || bo.writeCombined()!=bool(b.nc) ||
  !b.logical || b.owned!=NativeAdrenoMemory::backingBudget(b.logical) || ((b.va|b.pa|b.owned)&16383) || b.pa>(UINT64_C(1)<<36)-b.owned || b.va>UINT64_MAX-b.owned || b.owned/4096>HFMN_MAX_ROWS)return false;
 if(b.pa<gPhysBase || b.owned>gPhysSize || b.pa-gPhysBase>gPhysSize-b.owned)return false;
 NativeAdrenoCpuView view={};if(!bo.cpuView(&view) || view.spans!=bo.ownedSpans() || view.count!=1 || view.bytes!=b.owned)return false;
 const auto&span=bo.ownedSpans()[0];if(span.offset || span.physical!=b.pa || span.bytes!=b.owned)return false;
 for(unsigned j=0;j<HFMN_BOS;j++)if(j!=i && p.info[j].allocated){const auto&a=p.info[j];if((b.pa<a.pa+a.owned && a.pa<b.pa+b.owned) || (b.va<a.va+a.owned && a.va<b.va+b.owned))return false;}
 b.alias_rows=b.alias_wb=b.alias_nc=0;
 for(unsigned row=0;row<b.owned/4096;row++){
  IOByteCount length=0;const uint64_t offset=(uint64_t)row*4096;uint64_t r=0,w=0;
  if(bo.descriptor()->getPhysicalSegment(offset,&length,kIOMemoryMapperNone)!=b.pa+offset || length<4096 ||
    !houjiFirmwareAt(b.va+offset+0x5a,false,&r) || !houjiFirmwareAt(b.va+offset+0x5a,true,&w))return false;
  p.at_calls+=2;b.read_par[row]=r;b.write_par[row]=w;b.rows=row+1;
  const unsigned attribute=b.nc?0x44:0xff;
  if((r&1) || (w&1) || (r>>56)!=attribute || (w>>56)!=attribute ||
    ((r&UINT64_C(0x0000fffffffff000))|((b.va+offset+0x5a)&4095))!=b.pa+offset+0x5a ||
    ((w&UINT64_C(0x0000fffffffff000))|((b.va+offset+0x5a)&4095))!=b.pa+offset+0x5a)return false;
  // Observe the known physical-aperture alias without dereferencing its data.
  // It may remain WB after the descriptor becomes NC. This is NOT DMA-ready.
  const uint64_t alias=(uint64_t)phystokv(b.pa+offset+0x5a);uint64_t ar=0,aw=0;
  if(!alias || !houjiFirmwareAt(alias,false,&ar) || !houjiFirmwareAt(alias,true,&aw))return false;
  p.at_calls+=2;b.alias_read_par[row]=ar;b.alias_write_par[row]=aw;b.alias_rows=row+1;
  const unsigned attr=(unsigned)(ar>>56);
  if((ar&1) || (aw&1) || (attr!=0xff && attr!=0x44) || (aw>>56)!=attr ||
    ((ar&UINT64_C(0x0000fffffffff000))|(alias&4095))!=b.pa+offset+0x5a ||
    ((aw&UINT64_C(0x0000fffffffff000))|(alias&4095))!=b.pa+offset+0x5a)return false;
  if(attr==0xff)++b.alias_wb;else ++b.alias_nc;
 }
 return true;
}
static int houjiFirmwareAllocate(void*cookie,uint32_t pages,uint64_t*pa,void**cpu) {
 auto&p=gHoujiFirmwareMemory;const unsigned sizes[]={19,27,19,37,2,18,16,1,10};
 if(cookie!=&p || !p.active || !pa || !cpu || p.next>=HFMN_BOS)return houjiFirmwareFail(1,p.next);
 *pa=0;*cpu=nullptr;const unsigned i=p.next++;auto&b=p.info[i];b.attempted=1;b.logical=(uint64_t)pages*4096;
 if(pages!=sizes[i])return houjiFirmwareFail(2,i,0,pages,sizes[i]);
 b.status=p.bo[i].allocate(b.logical,36,true,NativeAdrenoMemory::backingBudget(b.logical),false);
 if(b.status){if(p.bo[i].backed())p.bo[i].quarantine();return houjiFirmwareFail(3,i,0,(uint64_t)b.status);}
 b.allocated=1;b.va=(uintptr_t)p.bo[i].mapping();b.owned=p.bo[i].ownedBytes();b.pa=p.bo[i].ownedSpanCount()?p.bo[i].ownedSpans()[0].physical:0;
 if(!houjiFirmwareOwn(i))return houjiFirmwareFail(4,i);
 *pa=b.pa;*cpu=(void*)(uintptr_t)b.va;return 0;
}
static int houjiFirmwareValidate(void*cookie,uint64_t pa,void*cpu,uint32_t pages,uint32_t nc) {
 auto&p=gHoujiFirmwareMemory;const unsigned i=houjiFirmwareSlot(pa,cpu,pages);if(cookie!=&p || i>=HFMN_BOS || nc!=(i>=5 && i<=7?1u:0u))return houjiFirmwareFail(5,i);
 auto&b=p.info[i];if(nc && !b.nc_attempted){b.nc_attempted=1;b.status=p.bo[i].makeWriteCombined();if(b.status)return houjiFirmwareFail(6,i,0,(uint64_t)b.status);b.nc=1;}
 if(!houjiFirmwareOwn(i))return houjiFirmwareFail(7,i);p.validated|=1u<<i;return 0;
}
static int houjiFirmwarePublish(void*cookie,const HhiAllocation*m) {
 auto&p=gHoujiFirmwareMemory;if(cookie!=&p || !m)return houjiFirmwareFail(8,HFMN_BOS);const unsigned i=houjiFirmwareSlot(m->physical,m->cpu,m->gpu4k_pages);
 if(i>=HFMN_BOS || m->normal_nc!=p.info[i].nc || !houjiFirmwareOwn(i))return houjiFirmwareFail(9,i);
 auto&b=p.info[i];if(!b.nc){b.status=NativeAdrenoMaintainOwnedPages((void*)(uintptr_t)b.va,b.owned,NativeAdrenoClean);if(b.status)return houjiFirmwareFail(10,i,0,(uint64_t)b.status);}
 houjiFirmwareBarrier();b.published=1;p.published|=1u<<i;return 0;
}
static int houjiFirmwareRetain(void*cookie,uint64_t pa,void*cpu,uint32_t pages) {
 auto&p=gHoujiFirmwareMemory;const unsigned i=houjiFirmwareSlot(pa,cpu,pages);if(cookie==&p && i<HFMN_BOS)p.bo[i].quarantine();
 /* Explicit failed release: retain uncertain CPU allocations until reset.
  * The C bridge/upstream retain the original failure and ownership masks. */
 return 1;
}
static int houjiFirmwareTableRead(void*cookie,uint64_t pa,uint64_t*value) {
 auto&p=gHoujiFirmwareMemory;const auto&b=p.info[8];if(cookie!=&p || !value || !b.allocated || (pa&7) || pa<b.pa || pa-b.pa>40960-8)return 1;
 *value=*reinterpret_cast<const volatile uint64_t*>((uintptr_t)b.va+(uintptr_t)(pa-b.pa));++p.physical_reads;return 0;
}
static bool houjiFirmwareAudit(void) {
 auto&p=gHoujiFirmwareMemory;const auto&s=p.summary;ArmSmmu4KRange ranges[8];
 for(unsigned i=0;i<HFMN_BOS;i++){
  if(hhi_row(&p.storage,sizeof(p.storage),i,&p.row[i],sizeof(HhiRow)) || !houjiFirmwareOwn(i))return false;
  const auto&r=p.row[i];const auto&b=p.info[i];if(!r.planned || !r.allocated || !r.validated || r.released || !r.retained || r.is_table!=(i==8) || r.range_initialized!=(i!=8) || r.allocation.physical!=b.pa || r.allocation.cpu!=(void*)(uintptr_t)b.va || (uint64_t)r.allocation.gpu4k_pages*4096!=b.logical || r.allocation.normal_nc!=b.nc)return false;
  const auto*bytes=reinterpret_cast<const volatile unsigned char*>((uintptr_t)b.va);for(uint64_t offset=b.logical;offset<b.owned;offset++)if(bytes[offset])return false;
  if(i<8){const auto&q=r.range;const uint64_t iova[]={0x4000,0x100000,0x200000,0x300000,0x60000000,0x60010000,0x60030000,0x60040000};
   if(q.iova!=iova[i] || q.physical!=b.pa || q.bytes!=b.logical || q.permissions!=(ArmSmmu4KRead|ArmSmmu4KWrite|ArmSmmu4KPrivileged|(i<4?ArmSmmu4KExecute:0)) || q.writeback!=!b.nc || q.shareability!=3)return false;
   ranges[i]={q.iova,q.physical,q.bytes,q.permissions,q.writeback,q.shareability};}
 }
 p.content_attempted=1;p.content_code=UINT32_MAX;p.content_status=hhi_verify_contents(&p.storage,sizeof(p.storage),gHoujiGmuFirmwareImage,sizeof(gHoujiGmuFirmwareImage),&p.content_code);
 if(p.content_status || p.content_code)return false;
 ArmSmmu4KConfig config={32,36,p.info[8].pa,2048};ArmSmmu4KResult built={};built.root_physical=s.root_physical;built.mair=s.mair;built.table_pages=s.built_table_pages;built.leaf_pages=s.built_leaf_pages;built.start_level=s.start_level;built.upper_region=s.upper_region;
 const ArmSmmu4KTableIo io={&p,houjiFirmwareTableRead};const ArmSmmu4KAuditRegion region={ranges,8,10,5120,p.visited,sizeof(p.visited)};
 if(ArmSmmu4KAuditTables(&io,&config,&built,&region,p.info[8].pa,&p.audit) || !p.audit.complete || p.audit.table_pages!=6 || p.audit.leaf_pages!=139 || p.audit.descriptor_reads!=3072)return false;
 const auto*bytes=reinterpret_cast<const volatile unsigned char*>((uintptr_t)p.info[8].va);for(uint64_t i=6*4096;i<p.info[8].owned;i++)if(bytes[i])return false;
 return true;
}
static void houjiFirmwareReport(void) {
 const auto&p=gHoujiFirmwareMemory;const auto&s=p.summary;
 IOLog("HOUJI FW MEMORY: phase=%u code=%u bridge=%u hfm=%u complete=%u FW-valid=%u BOs=%u allocated=%x validated=%x retained=%x released=%x CPU-published=%x records=%u loaded=%u TCM-deferred=%u HFI=%u/%u tables=%llu leaves=%llu audit=%u/%llu AT=%u; inactive, TTBR/stream/MMIO/SMC/TCM/firmware-exec=0\n",p.phase,p.code,p.bridge_status,p.prepare_code,p.complete,p.firmware_valid,p.next,s.allocated,s.validated,s.retained,s.released,p.published,s.records,s.bytes_loaded,s.tcm_bytes_deferred,s.hfi_queues,s.hfi_initialized,(unsigned long long)s.built_table_pages,(unsigned long long)s.built_leaf_pages,p.audit.complete,(unsigned long long)p.audit.descriptor_reads,p.at_calls);
 IOLog("HOUJI FW MEMORY CONTENT: attempted=%u bridge=%u code=%u verified=%u; scope=FW payload/holes,HFI headers/rings/pad,log/VRB; DMA-eligible=0\n",p.content_attempted,p.content_status,p.content_code,p.content_attempted && !p.content_status && !p.content_code);
 IOLog("HOUJI FW MEMORY FIRST: code=%u slot=%u row=%u observed=%llx expected=%llx; CPU ownership retained until reset\n",p.first_code,p.first_slot,p.first_row,(unsigned long long)p.first_value,(unsigned long long)p.first_expected);
 for(unsigned i=0;i<p.next && i<HFMN_BOS;i++){const auto&b=p.info[i];IOLog("HOUJI FW MEMORY BO: i=%u attempted=%u allocated=%u status=%x PA=%llx logical=%llu owned=%llu nc=%u/%u rows=%u refs=%u quarantine=%u alias-rows=%u WB=%u NC=%u DMA-eligible=0\n",i,b.attempted,b.allocated,b.status,(unsigned long long)b.pa,(unsigned long long)b.logical,(unsigned long long)b.owned,b.nc_attempted,b.nc,b.rows,p.bo[i].deviceReferences(),p.bo[i].quarantined(),b.alias_rows,b.alias_wb,b.alias_nc);}
}
static bool houjiGpuFirmwareMemoryProbeOnce(void) {
 int enabled=0;PE_parse_boot_argn("houji_gpu_fw_mem_probe",&enabled,sizeof(enabled));if(enabled!=1)return true;unsigned expected=0;
 if(!__atomic_compare_exchange_n(&gHoujiFirmwareMemoryOnce,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return gHoujiFirmwareMemory.complete==1;
 auto&p=gHoujiFirmwareMemory;p.active=1;p.phase=1;p.content_code=UINT32_MAX;
 NativeAdrenoFirmwareRules rules={78588,{0x96,0x85,0xf6,0x2c,0x42,0x54,0x3b,0xef,0xca,0xee,0x68,0xd2,0x53,0x55,0xe1,0x6b,0xf5,0x5c,0xdb,0x0d,0x57,0x3b,0xdd,0xcc,0x9f,0x66,0x33,0xf5,0x9e,0xec,0x3f,0x72}};NativeAdrenoFirmwareView view={};
 if(!gRootEnabled || !gLogEnabled || !gReturnRecovery){p.code=16;goto done;}
 if(NativeAdrenoQueryCpuPages(&p.cpu) || PAGE_SIZE!=16384 || p.cpu.kernel_page_bytes!=16384 || p.cpu.cpu_page_shift!=14 || NativeAdrenoFirmware::validate(gHoujiGmuFirmwareImage,sizeof(gHoujiGmuFirmwareImage),&rules,&view)){p.code=11;goto done;}
 p.firmware_valid=1;p.phase=2;
 {const HhiCallbacks callbacks={HHI_ABI_VERSION,0,&p,houjiFirmwareAllocate,houjiFirmwareValidate,houjiFirmwarePublish,houjiFirmwareRetain};p.bridge_status=hhi_init(&p.storage,sizeof(p.storage),&callbacks,sizeof(callbacks));if(p.bridge_status){p.code=12;goto done;}}
 p.phase=3;p.bridge_status=hhi_prepare(&p.storage,sizeof(p.storage),view.bytes,view.length,&p.prepare_code);
 if(hhi_summary(&p.storage,sizeof(p.storage),&p.summary,sizeof(p.summary)) || p.bridge_status || p.prepare_code){p.code=13;goto done;}
 {const auto&s=p.summary;if(s.count!=8 || s.records!=15 || s.reservations!=5 || s.allocated!=0x1ff || s.validated!=0x1ff || s.retained!=0x1ff || s.released || s.uncertain_allocate_outputs || s.bytes_loaded!=77808 || s.tcm_bytes_deferred!=540 || s.hfi_queues!=17 || !s.hfi_initialized || !s.published || s.build_status || s.audit_status || !s.audit_complete || p.next!=9 || p.validated!=0x1ff || p.published!=0x1ff || s.built_table_pages!=6 || s.built_leaf_pages!=139 || s.audit_descriptor_reads!=3072){p.code=14;goto done;}}
 p.phase=4;if(!houjiFirmwareAudit()){p.code=15;goto done;}p.complete=1;
 done:if(!p.complete)for(auto&bo:p.bo)if(bo.backed())bo.quarantine();p.active=0;p.phase=5;houjiFirmwareReport();return p.complete==1;
}
#endif
