/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Native descriptor binding for the CPU-only full r135 graph. This object does
 * not attach a domain, publish a root to hardware, hold device refs or execute.
 * A later execution owner needs its own retained lease; ready is not DMA proof.
 * Approximately 500KiB: static or explicitly retained heap storage only, NEVER
 * a kernel-stack local. FhgStorage is also never a kernel-stack local.
 */
#ifndef HOUJI_FULL_HOST_NATIVE_MEMORY_H
#define HOUJI_FULL_HOST_NATIVE_MEMORY_H
#include "houji_native_full_host_graph.h"
#include "Adreno740/NativeAdrenoMemory.h"
#include "Adreno740/NativeAdrenoSha256.h"
#ifndef HOUJI_FULL_HOST_NATIVE_HOST_TEST
#include <mach/arm/vm_param.h>
#endif
extern "C" vm_offset_t phystokv(uint64_t);
#ifdef HOUJI_FULL_HOST_NATIVE_HOST_TEST
static bool houjiFullHostAt(uint64_t,bool,uint64_t*);
static void houjiFullHostBarrier(void);
#else
static bool houjiFullHostAt(uint64_t va,bool write,uint64_t*par) {
 if(!par)return false;
 uint64_t daif,saved;disable_preemption();const int before=cpu_number();
 __asm__ volatile("mrs %0,daif\n\tmsr daifset,#3\n\tmrs %1,par_el1":"=r"(daif),"=r"(saved)::"memory");
 if(write)__asm__ volatile("at s1e1w,%0\n\tisb sy"::"r"(va):"memory");
 else __asm__ volatile("at s1e1r,%0\n\tisb sy"::"r"(va):"memory");
 __asm__ volatile("mrs %0,par_el1\n\tmsr par_el1,%1\n\tmsr daif,%2":"=&r"(*par):"r"(saved),"r"(daif):"memory");
 const int after=cpu_number();enable_preemption();return before>=0 && before==after;
}
static void houjiFullHostBarrier(void){__asm__ volatile("dsb sy":::"memory");}
#endif
class HoujiFullHostNativeMemory final {
public:
 enum {Slots=15,MaxGpuRows=FHG_LIVE_GPU_TABLE_PAGES,MaxForeign=32};
 struct Span {uint64_t va,pa,bytes;};
 struct Entry {
  FhgRequest request{};FhgBacking backing{};
  uint64_t read_par[MaxGpuRows]{},alias_par[MaxGpuRows]{};
  unsigned attempted=0,allocated=0,nc_attempted=0,nc=0,validated=0,published=0,rows=0;
  IOReturn status=kIOReturnSuccess;
 };
 struct Failure {unsigned code=0,slot=Slots,row=0;uint64_t observed=0,expected=0;};
private:
 NativeAdrenoMemory bo_[Slots];Entry entry_[Slots];Span foreign_[MaxForeign]{};
 unsigned foreign_count_=0,next_=0,active_=0,sealed_=0,failed_=0,at_calls_=0,publishes_=0;
 Failure failure_{};
 int fail(unsigned code,unsigned slot,unsigned row=0,uint64_t observed=0,uint64_t expected=0) {
  if(!failed_)failure_={code,slot,row,observed,expected};failed_=1;return 1;
 }
 bool overlap(uint64_t a,uint64_t ab,uint64_t b,uint64_t bb)const {
  return a<b+bb && b<a+ab;
 }
 bool own(unsigned slot,bool capture) {
  if(!active_ || failed_ || sealed_ || slot>=Slots)return false;
  auto&e=entry_[slot];auto&bo=bo_[slot];const auto&r=e.request;const auto&b=e.backing;
  if(!e.allocated || !bo.backed() || bo.quarantined() || bo.imported() || bo.readOnly() || bo.deviceReferences() ||
   bo.mapping()!=b.cpu || bo.bytes()!=r.logical_bytes || bo.ownedBytes()!=r.owned_bytes ||
   bo.cpuPageBytes()!=r.cpu_page_bytes || bo.writeCombined()!=bool(e.nc) || bo.ownedSpanCount()!=1 ||
   bo.spanCount()!=1 || !bo.descriptor() || bo.descriptor()->getLength()!=r.owned_bytes ||
   !r.logical_bytes || !r.owned_bytes || r.owned_bytes!=NativeAdrenoMemory::backingBudget(r.logical_bytes) ||
   ((b.physical|(uintptr_t)b.cpu|r.owned_bytes)&(r.cpu_page_bytes-1)) ||
   r.owned_bytes/4096>MaxGpuRows || b.physical>=(UINT64_C(1)<<36) || r.owned_bytes>(UINT64_C(1)<<36)-b.physical ||
   (uintptr_t)b.cpu>UINTPTR_MAX-r.owned_bytes || !gPhysSize || gPhysBase>UINT64_MAX-gPhysSize ||
   b.physical<gPhysBase || r.owned_bytes>gPhysSize || b.physical-gPhysBase>gPhysSize-r.owned_bytes)return false;
  const auto&o=bo.ownedSpans()[0];const auto&s=bo.spans()[0];NativeAdrenoCpuView view{};
  if(o.offset || s.offset || o.physical!=b.physical || s.physical!=b.physical || o.bytes!=r.owned_bytes ||
   s.bytes!=r.logical_bytes || !bo.cpuView(&view) || view.spans!=bo.ownedSpans() || view.count!=1 || view.bytes!=r.owned_bytes)return false;
  for(unsigned i=0;i<Slots;i++)if(i!=slot && entry_[i].allocated) {
   const auto&a=entry_[i];if(overlap(b.physical,r.owned_bytes,a.backing.physical,a.request.owned_bytes) ||
    overlap((uintptr_t)b.cpu,r.owned_bytes,(uintptr_t)a.backing.cpu,a.request.owned_bytes))return false;
  }
  for(unsigned i=0;i<foreign_count_;i++)if(overlap(b.physical,r.owned_bytes,foreign_[i].pa,foreign_[i].bytes) ||
   overlap((uintptr_t)b.cpu,r.owned_bytes,foreign_[i].va,foreign_[i].bytes))return false;
  for(unsigned row=0;row<r.owned_bytes/4096;row++) {
   const uint64_t offset=(uint64_t)row*4096,va=(uintptr_t)b.cpu+offset+0x5a;
   IOByteCount length=0;uint64_t read=0,write=0,ar=0,aw=0;
   if(bo.descriptor()->getPhysicalSegment(offset,&length,kIOMemoryMapperNone)!=b.physical+offset || length<4096 ||
    !houjiFullHostAt(va,false,&read) || !houjiFullHostAt(va,true,&write))return false;
   at_calls_+=2;const uint64_t alias=(uint64_t)phystokv(b.physical+offset+0x5a);
   if(!alias || !houjiFullHostAt(alias,false,&ar) || !houjiFullHostAt(alias,true,&aw))return false;at_calls_+=2;
   if((read&1) || read!=write || (read>>56)!=(e.nc?0x44u:0xffu) || ((read>>7)&3)==1 ||
    ((read&UINT64_C(0x0000fffffffff000))|(va&4095))!=b.physical+offset+0x5a ||
    (ar&1) || ar!=aw || ((ar>>56)!=0xff && (ar>>56)!=0x44) || ((ar>>7)&3)==1 ||
    ((ar&UINT64_C(0x0000fffffffff000))|(alias&4095))!=b.physical+offset+0x5a)return false;
   if(capture){e.read_par[row]=read;e.alias_par[row]=ar;}
   else if(read!=e.read_par[row] || ar!=e.alias_par[row])return false;
   e.rows=row+1;
  }
  return true;
 }
 static bool requestValid(const FhgRequest&r) {
  static const unsigned pages[Slots]={19,27,19,37,2,18,16,1,256,256,1024,10,256,64,FHG_LIVE_GPU_TABLE_PAGES};
  static const unsigned kinds[Slots]={FHG_FW,FHG_FW,FHG_FW,FHG_FW,FHG_FW,FHG_HFI,FHG_LOG,FHG_VRB,FHG_SHARED,FHG_GMU_ONLY,FHG_LARGE,FHG_GMU_TABLES,FHG_GPU_POOL,FHG_CODE,FHG_GPU_TABLES};
  return r.slot<Slots && (r.cpu_page_bytes==4096 || r.cpu_page_bytes==16384) &&
   !r.reserved && r.kind==kinds[r.slot] && r.gpu4k_pages==pages[r.slot] && r.logical_bytes==(uint64_t)pages[r.slot]*4096 &&
   r.owned_bytes==((r.logical_bytes+r.cpu_page_bytes-1)&~uint64_t(r.cpu_page_bytes-1)) &&
   r.normal_nc==unsigned((r.slot>=5 && r.slot<=10) || r.slot==12 || r.slot==13);
 }
 static int sha256(void*cookie,const uint8_t*data,size_t bytes,uint8_t digest[32]) {
  if(!cookie)return 1;auto&p=*static_cast<HoujiFullHostNativeMemory*>(cookie);
  if(!p.active_ || p.failed_ || p.sealed_ || !data || !bytes || !digest || (uintptr_t)data>UINTPTR_MAX-bytes)return p.fail(15,Slots);
  NativeAdrenoSha256::hash(data,bytes,digest);return 0;
 }
 static int allocate(void*cookie,const FhgRequest*r,FhgBacking*out) {
  if(!cookie)return 1;auto&p=*static_cast<HoujiFullHostNativeMemory*>(cookie);
  if(!r || !out || !p.active_ || p.failed_ || p.sealed_ || !requestValid(*r) || r->slot!=p.next_)return p.fail(1,r?r->slot:Slots);
  *out={};const unsigned i=p.next_++;auto&e=p.entry_[i];e.request=*r;e.attempted=1;
  NativeAdrenoCpuState cpu{};
  if(NativeAdrenoQueryCpuPages(&cpu) || cpu.kernel_page_bytes!=r->cpu_page_bytes ||
   cpu.kernel_map_page_bytes!=r->cpu_page_bytes || NativeAdrenoMemory::backingBudget(r->logical_bytes)!=r->owned_bytes)return p.fail(2,i);
  e.status=p.bo_[i].allocate(r->logical_bytes,36,true,r->owned_bytes,false);
  if(e.status){if(p.bo_[i].backed())p.bo_[i].quarantine();return p.fail(3,i,0,e.status);}
  e.allocated=1;e.backing={};e.backing.physical=p.bo_[i].ownedSpanCount()?p.bo_[i].ownedSpans()[0].physical:0;
  e.backing.cpu=p.bo_[i].mapping();e.backing.owned_bytes=p.bo_[i].ownedBytes();e.backing.owned_physical=e.backing.physical;
  e.backing.owned_span_bytes=e.backing.owned_bytes;e.backing.cpu_page_bytes=p.bo_[i].cpuPageBytes();e.backing.owned_span_count=p.bo_[i].ownedSpanCount();
  if(!p.own(i,true))return p.fail(4,i);*out=e.backing;return 0;
 }
 static int validate(void*cookie,const FhgRequest*r,const FhgBacking*b) {
  if(!cookie)return 1;auto&p=*static_cast<HoujiFullHostNativeMemory*>(cookie);
  if(!r || !b || r->slot>=Slots || !p.matches(r,b))return p.fail(5,r?r->slot:Slots);
  auto&e=p.entry_[r->slot];
  /* fhg_verify repeats validate under the same inactive zero-ref owner. A
   * repeated validation is read-only and cannot repeat the NC transition. */
  if(e.validated)return e.nc==r->normal_nc && p.own(r->slot,false)?0:p.fail(6,r->slot);
  if(!p.own(r->slot,false))return p.fail(7,r->slot);
  if(r->normal_nc){e.nc_attempted=1;e.status=p.bo_[r->slot].makeWriteCombined();if(e.status)return p.fail(8,r->slot,0,e.status);e.nc=1;}
  if(!p.own(r->slot,true))return p.fail(9,r->slot);e.validated=1;return 0;
 }
 bool matches(const FhgRequest*r,const FhgBacking*b)const {
  if(!active_ || failed_ || sealed_ || !requestValid(*r))return false;
  const auto&e=entry_[r->slot];const auto&a=e.backing;
  return e.allocated && r->kind==e.request.kind && r->gpu4k_pages==e.request.gpu4k_pages && r->normal_nc==e.request.normal_nc &&
   r->logical_bytes==e.request.logical_bytes && r->owned_bytes==e.request.owned_bytes && r->cpu_page_bytes==e.request.cpu_page_bytes &&
   b->physical==a.physical && b->cpu==a.cpu && b->owned_bytes==a.owned_bytes && b->owned_physical==a.owned_physical &&
   b->owned_span_bytes==a.owned_span_bytes && b->cpu_page_bytes==a.cpu_page_bytes && b->owned_span_count==a.owned_span_count;
 }
 static int publish(void*cookie,const FhgRequest*r,const FhgBacking*b) {
  if(!cookie)return 1;auto&p=*static_cast<HoujiFullHostNativeMemory*>(cookie);
  if(!r || !b || r->slot>=Slots || !p.matches(r,b))return p.fail(10,r?r->slot:Slots);
  auto&e=p.entry_[r->slot];if(!e.validated || e.published || !p.own(r->slot,false))return p.fail(11,r->slot);
  for(uint64_t offset=r->logical_bytes;offset<r->owned_bytes;offset++)if(static_cast<const volatile unsigned char*>(b->cpu)[offset])return p.fail(12,r->slot,offset);
  if(!e.nc){e.status=NativeAdrenoMaintainOwnedPages(b->cpu,r->owned_bytes,NativeAdrenoCleanInvalidate);if(e.status)return p.fail(13,r->slot,0,e.status);}
  houjiFullHostBarrier();if(!p.own(r->slot,false))return p.fail(14,r->slot);e.published=1;++p.publishes_;return 0;
 }
public:
 HoujiFullHostNativeMemory()=default;
 HoujiFullHostNativeMemory(const HoujiFullHostNativeMemory&)=delete;
 HoujiFullHostNativeMemory&operator=(const HoujiFullHostNativeMemory&)=delete;
 bool begin(const Span*foreign,unsigned count) {
  if(active_ || sealed_ || failed_ || next_ || count>MaxForeign || (count && !foreign))return false;
  for(unsigned i=0;i<count;i++){
   const auto&s=foreign[i];if(!s.bytes || !s.pa || !s.va || s.pa>UINT64_MAX-s.bytes || s.va>UINT64_MAX-s.bytes)return false;
  }
  for(unsigned i=0;i<count;i++)foreign_[i]=foreign[i];foreign_count_=count;active_=1;return true;
 }
 FhgOps callbacks() {FhgOps out{};out.abi_version=FHG_ABI_VERSION;out.cookie=this;out.sha256=sha256;out.allocate=allocate;out.validate=validate;out.publish=publish;return out;}
 bool sealPrepared(FhgStorage&graph) {
  if(!active_ || sealed_ || failed_ || next_!=Slots || publishes_!=Slots)return false;
  for(unsigned i=0;i<Slots;i++)if(!entry_[i].validated || !entry_[i].published || !own(i,false)){fail(16,i);return false;}
  /* Bind the ready flag to this actual graph's fresh content verification,
   * not to stale Native publication counters or a caller-supplied summary. */
  uint32_t code=UINT32_MAX;FhgSummary summary{};
  if(fhg_verify(&graph,sizeof(graph),&code) || code ||
   fhg_summary(&graph,sizeof(graph),&summary,sizeof(summary)) || !summary.ready || summary.code ||
   summary.allocated!=FHG_ALL || summary.validated!=FHG_ALL || summary.published!=FHG_ALL ||
   summary.retained!=FHG_ALL || summary.uncertain_outputs){fail(17,Slots,0,code);return false;}
  for(unsigned i=0;i<Slots;i++){
   FhgRow row{};if(fhg_row(&graph,sizeof(graph),i,&row,sizeof(row)) ||
    !row.allocated || !row.validated || !row.published || row.uncertain || !matches(&row.request,&row.backing)){fail(18,i);return false;}
  }
  houjiFullHostBarrier();active_=0;sealed_=1;return true;
 }
 void retainFailure() {active_=0;failed_=1;for(auto&bo:bo_)if(bo.backed())bo.quarantine();}
 bool prepared()const{return sealed_ && !active_ && !failed_;}
 const Entry&entry(unsigned i)const{return entry_[i<Slots?i:0];}
 const Failure&failure()const{return failure_;}
 const NativeAdrenoMemory&bo(unsigned i)const{return bo_[i<Slots?i:0];}
 unsigned count()const{return next_;}
 unsigned atCalls()const{return at_calls_;}
 unsigned publishes()const{return publishes_;}
};
#endif
