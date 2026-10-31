/* SPDX-License-Identifier: MIT
 * houji_gpu_mem_probe=1: exclusive XNU CPU memory qualification only.
 * No GPU MMIO, SMMU table publication, device references, DMA or provider.
 * Unknown state stops mutation and retains every still-backed BO until reset.
 */
#ifndef HOUJI_GPU_MEMORY_PROBE_INC
#define HOUJI_GPU_MEMORY_PROBE_INC
#include "Adreno740/NativeAdrenoMemory.h"
#ifndef HOUJI_GPU_MEMORY_PROBE_HOST_TEST
#include <IOKit/IOLib.h>
#include <kern/cpu_data.h>
#include <mach/vm_param.h>
#include <pexpert/pexpert.h>
#include <vm/memory_types.h>
#endif
extern "C" unsigned int pmap_cache_attributes(ppnum_t);

enum {HGMP_LOGICAL=4096,HGMP_BACKING=16384,HGMP_BOS=2,HGMP_ROWS=4};
enum {HGMP_OK,HGMP_GEOMETRY,HGMP_ALLOC,HGMP_OWNERSHIP,HGMP_ATTRIBUTE,
 HGMP_TRANSLATION,HGMP_ZERO,HGMP_ALIAS,HGMP_CACHE,HGMP_PATTERN,HGMP_RELEASE};
static unsigned gHoujiGpuMemoryProbeOnce;
static struct {
 NativeAdrenoCpuState cpu;
 NativeAdrenoMemory bo[HGMP_BOS];
 struct Bo {
  uint64_t va,owned_pa,logical_pa,owned_bytes,logical_bytes;
  uint64_t read_par[HGMP_ROWS],write_par[HGMP_ROWS];
  unsigned allocated,owned_count,logical_count,rows,initial_attr,wc_attr,final_attr,attr_sampled;
  unsigned zero_valid,pattern_valid,release_attempted,released;
  IOReturn allocation_status,release_status;
 } info[HGMP_BOS];
 uint64_t wc_read_par[HGMP_ROWS],wc_write_par[HGMP_ROWS];
 unsigned phase,code,geometry_attempted,wc_attempted,reassert_attempted;
 unsigned neighbor_valid,retained,completed,corrected,wc_rows;
 IOReturn status,geometry_status,wc_status,reassert_status;
} gHoujiGpuMemoryProbe;

#ifdef HOUJI_GPU_MEMORY_PROBE_HOST_TEST
static bool houjiGpuMemoryAt(uint64_t,bool,uint64_t *);
#else
static bool houjiGpuMemoryAt(uint64_t va,bool write,uint64_t *par) {
 if(!par)return false;
 uint64_t daif,saved_par;disable_preemption();
 const int before=cpu_number();
 __asm__ volatile("mrs %0,daif\n\tmsr daifset,#3\n\tmrs %1,par_el1"
  :"=r"(daif),"=r"(saved_par)::"memory");
 if(write)__asm__ volatile("at s1e1w,%0\n\tisb sy"::"r"(va):"memory");
 else __asm__ volatile("at s1e1r,%0\n\tisb sy"::"r"(va):"memory");
 __asm__ volatile("mrs %0,par_el1\n\tmsr par_el1,%1\n\tmsr daif,%2"
  :"=&r"(*par):"r"(saved_par),"r"(daif):"memory");
 const int after=cpu_number();enable_preemption();
 return before>=0 && before==after;
}
#endif

static void houjiGpuMemoryProbeReport(void) {
 const auto &p=gHoujiGpuMemoryProbe;const auto &c=p.cpu;
 IOLog("HOUJI GPU MEM GEOMETRY: attempted=%u status=%x PAGE_SIZE=%llu page_size=%llu kernel_map_page=%llu shift=%u TCR=%llx TTBR0=%llx TTBR1=%llx SCTLR=%llx\n",
  p.geometry_attempted,(unsigned)p.geometry_status,(unsigned long long)PAGE_SIZE,
  (unsigned long long)c.kernel_page_bytes,(unsigned long long)c.kernel_map_page_bytes,c.cpu_page_shift,
  (unsigned long long)c.tcr,(unsigned long long)c.ttbr0,(unsigned long long)c.ttbr1,(unsigned long long)c.sctlr);
 for(unsigned i=0;i<HGMP_BOS;++i) {
  const auto &b=p.info[i];
  IOLog("HOUJI GPU MEM BO: i=%u allocated=%u status=%x logical=%llu/%u PA=%llx owned=%llu/%u PA=%llx VA=%llx pmap-attrs=%x/%x/%x sampled-mask=%x zero=%u pattern=%u release-attempted=%u status=%x released=%u quarantined=%u retained=%u\n",
   i,b.allocated,(unsigned)b.allocation_status,(unsigned long long)b.logical_bytes,b.logical_count,
   (unsigned long long)b.logical_pa,(unsigned long long)b.owned_bytes,b.owned_count,
   (unsigned long long)b.owned_pa,(unsigned long long)b.va,b.initial_attr,b.wc_attr,b.final_attr,b.attr_sampled,
   b.zero_valid,b.pattern_valid,b.release_attempted,(unsigned)b.release_status,b.released,
   p.bo[i].quarantined()?1u:0u,p.bo[i].backed()?1u:0u);
  for(unsigned r=0;r<b.rows;++r)
   IOLog("HOUJI GPU MEM AT: i=%u offset=%x read-PAR=%llx write-PAR=%llx expected-byte-PA=%llx; CPU translation only\n",
    i,r*4096+0x5a,(unsigned long long)b.read_par[r],(unsigned long long)b.write_par[r],
    (unsigned long long)(b.owned_pa+r*4096+0x5a));
 }
 for(unsigned r=0;r<p.wc_rows;++r)
  IOLog("HOUJI GPU MEM WC AT: offset=%x read-PAR=%llx write-PAR=%llx; raw CPU attributes, not a frequency or DMA measurement\n",
   r*4096+0x5a,(unsigned long long)p.wc_read_par[r],(unsigned long long)p.wc_write_par[r]);
 IOLog("HOUJI GPU MEM RESULT: phase=%u code=%u status=%x WC-attempted=%u status=%x reassert-attempted=%u status=%x corrected=%u neighbor=%u completed=%u retained=%u GPU-mapped=0 GPU-execution=0 DMA=0\n",
  p.phase,p.code,(unsigned)p.status,p.wc_attempted,(unsigned)p.wc_status,p.reassert_attempted,
  (unsigned)p.reassert_status,p.corrected,p.neighbor_valid,p.completed,p.retained);
}
static void houjiGpuMemoryProbeStop(unsigned code,IOReturn status) {
 auto &p=gHoujiGpuMemoryProbe;p.code=code;p.status=status;p.retained=0;
 for(auto &bo:p.bo)if(bo.backed()) {bo.quarantine();++p.retained;}
 houjiGpuMemoryProbeReport();
}
static bool houjiGpuMemoryProbeRows(unsigned index,bool wc) {
 auto &p=gHoujiGpuMemoryProbe;auto &b=p.info[index];
 for(unsigned row=0;row<HGMP_ROWS;++row) {
  const uint64_t offset=(uint64_t)row*4096+0x5a,va=b.va+offset;
  uint64_t r=0,w=0;
  const bool sampled=houjiGpuMemoryAt(va,false,&r) && houjiGpuMemoryAt(va,true,&w);
  if(wc) {p.wc_read_par[row]=r;p.wc_write_par[row]=w;p.wc_rows=row+1;}
  else {b.read_par[row]=r;b.write_par[row]=w;b.rows=row+1;}
  /* Same byte-PA reconstruction as current XNU mmu_kvtop (asm:385–391).
   * No pmap page number or inferred 16K offset is substituted for PAR.PA. */
  const uint64_t read_pa=(r&UINT64_C(0x0000fffffffff000))|(va&4095);
  const uint64_t write_pa=(w&UINT64_C(0x0000fffffffff000))|(va&4095);
  if(!sampled || (r&1) || (w&1) || read_pa!=b.owned_pa+offset || write_pa!=b.owned_pa+offset)return false;
 }
 return true;
}
static unsigned houjiGpuMemoryProbeAttr(uint64_t pa,unsigned shift) {
 return pmap_cache_attributes((ppnum_t)(pa>>shift))&VM_WIMG_MASK;
}
static bool houjiGpuMemoryProbePattern(unsigned index,unsigned char value,bool per4k) {
 auto &bo=gHoujiGpuMemoryProbe.bo[index];auto &b=gHoujiGpuMemoryProbe.info[index];
 const auto *bytes=static_cast<const volatile unsigned char *>(bo.mapping());
 for(size_t i=0;i<HGMP_BACKING;++i)
  if(bytes[i]!=(per4k?(unsigned char)(value+(i/4096)*0x11):value))return false;
 b.pattern_valid=1;return true;
}
static void houjiGpuMemoryProbeOnce(void) {
 int enabled=0;PE_parse_boot_argn("houji_gpu_mem_probe",&enabled,sizeof(enabled));
 if(enabled!=1)return;
 unsigned expected=0;
 if(!__atomic_compare_exchange_n(&gHoujiGpuMemoryProbeOnce,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return;
 auto &p=gHoujiGpuMemoryProbe;p.phase=1;p.geometry_attempted=1;
 p.geometry_status=NativeAdrenoQueryCpuPages(&p.cpu);
 if(p.geometry_status || PAGE_SIZE!=HGMP_BACKING || p.cpu.kernel_page_bytes!=HGMP_BACKING ||
  p.cpu.cpu_page_shift!=14 || (p.cpu.tcr&(UINT64_C(1)<<23))) {
  houjiGpuMemoryProbeStop(HGMP_GEOMETRY,p.geometry_status?p.geometry_status:kIOReturnUnsupported);return;
 }
 for(unsigned i=0;i<HGMP_BOS;++i) {
  p.phase=2+i;auto &bo=p.bo[i];auto &b=p.info[i];
  IOLog("HOUJI GPU MEM STEP: allocate BO=%u logical=%u backing-budget=%u; CPU-only\n",i,HGMP_LOGICAL,HGMP_BACKING);
  b.allocation_status=bo.allocate(HGMP_LOGICAL,40,true,HGMP_BACKING,false);
  if(b.allocation_status) {houjiGpuMemoryProbeStop(HGMP_ALLOC,b.allocation_status);return;}
  b.allocated=1;b.va=(uint64_t)(uintptr_t)bo.mapping();
  b.logical_bytes=bo.bytes();b.owned_bytes=bo.ownedBytes();b.logical_count=(unsigned)bo.spanCount();b.owned_count=(unsigned)bo.ownedSpanCount();
  NativeAdrenoCpuView view{};
  if(!b.va || (b.va&(HGMP_BACKING-1)) || b.va>UINT64_MAX-HGMP_BACKING || b.logical_bytes!=HGMP_LOGICAL ||
   b.owned_bytes!=HGMP_BACKING || b.logical_count!=1 || b.owned_count!=1 || !bo.cpuView(&view) ||
   view.spans!=bo.ownedSpans() || view.count!=1 || view.bytes!=HGMP_BACKING || bo.cpuPageBytes()!=HGMP_BACKING) {
   houjiGpuMemoryProbeStop(HGMP_OWNERSHIP,kIOReturnUnsupported);return;
  }
  const auto &logical=bo.spans()[0];const auto &owned=bo.ownedSpans()[0];
  b.logical_pa=logical.physical;b.owned_pa=owned.physical;
  if(logical.offset || owned.offset || logical.bytes!=HGMP_LOGICAL || owned.bytes!=HGMP_BACKING ||
   logical.physical!=owned.physical || !owned.physical || (owned.physical&(HGMP_BACKING-1)) ||
   owned.physical>=(UINT64_C(1)<<40) || HGMP_BACKING>(UINT64_C(1)<<40)-owned.physical) {
   houjiGpuMemoryProbeStop(HGMP_OWNERSHIP,kIOReturnUnsupported);return;
  }
  b.initial_attr=houjiGpuMemoryProbeAttr(b.owned_pa,p.cpu.cpu_page_shift);
  b.attr_sampled|=1;
  if(b.initial_attr!=(VM_WIMG_DEFAULT&VM_WIMG_MASK)) {houjiGpuMemoryProbeStop(HGMP_ATTRIBUTE,kIOReturnUnsupported);return;}
  if(!houjiGpuMemoryProbeRows(i,false)) {houjiGpuMemoryProbeStop(HGMP_TRANSLATION,kIOReturnIOError);return;}
  if(!houjiGpuMemoryProbePattern(i,0,false)) {houjiGpuMemoryProbeStop(HGMP_ZERO,kIOReturnIOError);return;}
  b.zero_valid=1;b.pattern_valid=0;
 }
 p.phase=4;
 if(p.info[0].owned_pa==p.info[1].owned_pa) {houjiGpuMemoryProbeStop(HGMP_ALIAS,kIOReturnIOError);return;}
 auto *neighbor=static_cast<volatile unsigned char *>(p.bo[1].mapping());
 for(size_t i=0;i<HGMP_BACKING;++i)neighbor[i]=0xa5;
 p.status=NativeAdrenoMaintainOwnedPages(p.bo[1].mapping(),HGMP_BACKING,NativeAdrenoClean);
 if(p.status) {houjiGpuMemoryProbeStop(HGMP_CACHE,p.status);return;}
 p.phase=5;p.wc_attempted=1;
 IOLog("HOUJI GPU MEM STEP: make-WC BO0 PA=%llx neighbor-PA=%llx; full owned CPU page, software pmap API\n",
  (unsigned long long)p.info[0].owned_pa,(unsigned long long)p.info[1].owned_pa);
 p.wc_status=p.bo[0].makeWriteCombined();
 if(p.wc_status) {houjiGpuMemoryProbeStop(HGMP_CACHE,p.wc_status);return;}
 p.info[0].wc_attr=houjiGpuMemoryProbeAttr(p.info[0].owned_pa,p.cpu.cpu_page_shift);
 p.info[1].wc_attr=houjiGpuMemoryProbeAttr(p.info[1].owned_pa,p.cpu.cpu_page_shift);
 p.info[0].attr_sampled|=2;p.info[1].attr_sampled|=2;
 if(p.info[0].wc_attr!=VM_WIMG_WCOMB || p.info[1].wc_attr!=(VM_WIMG_DEFAULT&VM_WIMG_MASK)) {
  houjiGpuMemoryProbeStop(HGMP_ATTRIBUTE,kIOReturnIOError);return;
 }
 if(!houjiGpuMemoryProbeRows(0,true)) {houjiGpuMemoryProbeStop(HGMP_TRANSLATION,kIOReturnIOError);return;}
 p.reassert_attempted=1;p.reassert_status=p.bo[0].reassertWriteCombined(&p.corrected);
 if(p.reassert_status) {houjiGpuMemoryProbeStop(HGMP_CACHE,p.reassert_status);return;}
 p.phase=6;auto *bytes=static_cast<volatile unsigned char *>(p.bo[0].mapping());
 for(size_t i=0;i<HGMP_BACKING;++i)bytes[i]=(unsigned char)(0x11+(i/4096)*0x11);
 p.status=NativeAdrenoMaintainOwnedPages(p.bo[0].mapping(),HGMP_BACKING,NativeAdrenoCleanInvalidate);
 if(p.status) {houjiGpuMemoryProbeStop(HGMP_CACHE,p.status);return;}
 if(!houjiGpuMemoryProbePattern(0,0x11,true) || !houjiGpuMemoryProbePattern(1,0xa5,false)) {
  houjiGpuMemoryProbeStop(HGMP_PATTERN,kIOReturnIOError);return;
 }
 p.neighbor_valid=1;p.phase=7;p.info[0].release_attempted=1;
 IOLog("HOUJI GPU MEM STEP: release BO0 through verified-default-restore then complete; no GPU references\n");
 p.info[0].release_status=p.bo[0].release();
 if(p.info[0].release_status) {houjiGpuMemoryProbeStop(HGMP_RELEASE,p.info[0].release_status);return;}
 /* release internally restores and reads back the whole CPU page before
  * complete/free. Never read its old PA/VA after successful release. */
 p.info[0].released=1;p.phase=8;
 p.info[1].final_attr=houjiGpuMemoryProbeAttr(p.info[1].owned_pa,p.cpu.cpu_page_shift);
 p.info[1].attr_sampled|=4;
 if(p.info[1].final_attr!=(VM_WIMG_DEFAULT&VM_WIMG_MASK) || !houjiGpuMemoryProbePattern(1,0xa5,false)) {
  houjiGpuMemoryProbeStop(HGMP_PATTERN,kIOReturnIOError);return;
 }
 p.info[1].release_attempted=1;
 IOLog("HOUJI GPU MEM STEP: release unchanged BO1 through complete; no GPU references\n");
 p.info[1].release_status=p.bo[1].release();
 if(p.info[1].release_status) {houjiGpuMemoryProbeStop(HGMP_RELEASE,p.info[1].release_status);return;}
 p.info[1].released=1;p.completed=1;p.phase=9;p.status=kIOReturnSuccess;houjiGpuMemoryProbeReport();
}
#endif
