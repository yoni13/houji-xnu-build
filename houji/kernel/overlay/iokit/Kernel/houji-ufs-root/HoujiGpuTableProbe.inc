/* SPDX-License-Identifier: MIT
 * houji_gpu_table_probe=1: CPU-owned, INACTIVE 4K SMMU table qualification.
 * Actual ArmSmmu4K Build/Audit; no hardware context, TLB, GPU or DMA access.
 * Every unknown result retains all still-backed allocations until reset.
 */
#ifndef HOUJI_GPU_TABLE_PROBE_INC
#define HOUJI_GPU_TABLE_PROBE_INC
#include "Adreno740/NativeAdrenoMemory.h"
#include "ArmSmmu4K.h"
#include "ArmSmmu4KAudit.h"
#ifndef HOUJI_GPU_TABLE_PROBE_HOST_TEST
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <kern/cpu_data.h>
#include <mach/vm_param.h>
#include <pexpert/pexpert.h>
#endif

enum {HGTP_BYTES=16384,HGTP_PAGE=4096,HGTP_BOS=2,HGTP_ROWS=4,HGTP_DATA=0,HGTP_TABLE=1};
enum {HGTP_OK,HGTP_GEOMETRY,HGTP_ALLOC,HGTP_OWNERSHIP,HGTP_TRANSLATION,
 HGTP_ZERO,HGTP_ALIAS,HGTP_BUILD,HGTP_CACHE,HGTP_AUDIT,HGTP_LEAF,HGTP_SPARE,HGTP_RELEASE};
static const uint64_t kHoujiGpuTableIova=UINT64_C(0x62080000);
static unsigned gHoujiGpuTableProbeOnce;
static struct {
 NativeAdrenoCpuState cpu;
 NativeAdrenoMemory bo[HGTP_BOS];
 struct Bo {
  uint64_t va,pa,logical_bytes,owned_bytes,read_par[HGTP_ROWS],write_par[HGTP_ROWS];
  unsigned allocated,rows,zero_valid,release_attempted,released;
  IOReturn allocation_status,release_status;
 } info[HGTP_BOS];
 ArmSmmu4KConfig config;
 ArmSmmu4KRange range;
 ArmSmmu4KResult built;
 ArmSmmu4KTableAuditResult audit;
 unsigned char visited[HGTP_ROWS];
 uint64_t leaf[HGTP_ROWS],byte_pa[HGTP_ROWS];
 size_t physical_reads,physical_refusals;
 unsigned phase,code,retained,completed,spare_zero,neighbors_invalid,leaves_valid,cleaned;
 IOReturn status,geometry_status;
 ArmSmmu4KStatus build_status,audit_status;
} gHoujiGpuTableProbe;

#ifdef HOUJI_GPU_TABLE_PROBE_HOST_TEST
static bool houjiGpuTableAt(uint64_t,bool,uint64_t *);
#else
static bool houjiGpuTableAt(uint64_t va,bool write,uint64_t *par) {
 if(!par)return false;
 uint64_t daif,saved_par;disable_preemption();const int before=cpu_number();
 __asm__ volatile("mrs %0,daif\n\tmsr daifset,#3\n\tmrs %1,par_el1"
  :"=r"(daif),"=r"(saved_par)::"memory");
 if(write)__asm__ volatile("at s1e1w,%0\n\tisb sy"::"r"(va):"memory");
 else __asm__ volatile("at s1e1r,%0\n\tisb sy"::"r"(va):"memory");
 __asm__ volatile("mrs %0,par_el1\n\tmsr par_el1,%1\n\tmsr daif,%2"
  :"=&r"(*par):"r"(saved_par),"r"(daif):"memory");
 const int after=cpu_number();enable_preemption();return before>=0 && before==after;
}
#endif

static void houjiGpuTableProbeReport(void) {
 const auto &p=gHoujiGpuTableProbe;
 IOLog("HOUJI GPU TABLE GEOMETRY: status=%x PAGE_SIZE=%llu runtime=%llu map=%llu shift=%u TCR=%llx TTBR0=%llx TTBR1=%llx SCTLR=%llx\n",
  (unsigned)p.geometry_status,(unsigned long long)PAGE_SIZE,(unsigned long long)p.cpu.kernel_page_bytes,
  (unsigned long long)p.cpu.kernel_map_page_bytes,p.cpu.cpu_page_shift,(unsigned long long)p.cpu.tcr,
  (unsigned long long)p.cpu.ttbr0,(unsigned long long)p.cpu.ttbr1,(unsigned long long)p.cpu.sctlr);
 for(unsigned i=0;i<HGTP_BOS;++i) {
  const auto &b=p.info[i];
  IOLog("HOUJI GPU TABLE BO: i=%u allocated=%u alloc-status=%x logical=%llu owned=%llu VA=%llx PA=%llx zero=%u rows=%u release-attempted=%u release-status=%x released=%u retained=%u quarantined=%u device-refs=%u\n",
   i,b.allocated,(unsigned)b.allocation_status,(unsigned long long)b.logical_bytes,(unsigned long long)b.owned_bytes,
   (unsigned long long)b.va,(unsigned long long)b.pa,b.zero_valid,b.rows,b.release_attempted,
   (unsigned)b.release_status,b.released,p.bo[i].backed()?1u:0u,p.bo[i].quarantined()?1u:0u,p.bo[i].deviceReferences());
  for(unsigned row=0;row<b.rows;++row)
   IOLog("HOUJI GPU TABLE CPU AT: i=%u offset=%x read-PAR=%llx write-PAR=%llx expected-byte-PA=%llx\n",
    i,row*HGTP_PAGE+0x5a,(unsigned long long)b.read_par[row],(unsigned long long)b.write_par[row],
    (unsigned long long)(b.pa+row*HGTP_PAGE+0x5a));
 }
 for(unsigned i=0;i<p.leaves_valid;++i)
  IOLog("HOUJI GPU TABLE LEAF: i=%u IOVA=%llx raw=%llx byte-PA=%llx expected=%llx; inactive CPU walk\n",
   i,(unsigned long long)(kHoujiGpuTableIova+i*HGTP_PAGE),(unsigned long long)p.leaf[i],
   (unsigned long long)p.byte_pa[i],(unsigned long long)(p.info[HGTP_DATA].pa+i*HGTP_PAGE+0x5a));
 IOLog("HOUJI GPU TABLE RESULT: phase=%u code=%u status=%x build-status=%u audit-status=%u root=%llx start=%u upper=%u mair=%llx tables=%llu/4 leaves=%llu audit-complete=%u audit-tables=%llu audit-leaves=%llu audit-reads=%llu physical-reads=%llu refused=%llu cleaned=%u spare-zero=%u neighbors-invalid=%u completed=%u retained=%u GPU-mapped=0 GPU-execution=0 DMA=0\n",
  p.phase,p.code,(unsigned)p.status,(unsigned)p.build_status,(unsigned)p.audit_status,
  (unsigned long long)p.built.root_physical,p.built.start_level,p.built.upper_region,(unsigned long long)p.built.mair,
  (unsigned long long)p.built.table_pages,(unsigned long long)p.built.leaf_pages,p.audit.complete,
  (unsigned long long)p.audit.table_pages,(unsigned long long)p.audit.leaf_pages,(unsigned long long)p.audit.descriptor_reads,
  (unsigned long long)p.physical_reads,(unsigned long long)p.physical_refusals,p.cleaned,p.spare_zero,p.neighbors_invalid,p.completed,p.retained);
}
static void houjiGpuTableProbeStop(unsigned code,IOReturn status) {
 auto &p=gHoujiGpuTableProbe;p.code=code;p.status=status;p.retained=0;
 for(auto &bo:p.bo)if(bo.backed()){bo.quarantine();++p.retained;}
 houjiGpuTableProbeReport();
}
/* The independent physical reader accepts only this allocation's entire
 * exclusive table backing. No data-BO, phystokv or synthetic table fallback. */
static int houjiGpuTableRead(void *cookie,uint64_t pa,uint64_t *value) {
 auto &p=gHoujiGpuTableProbe;auto &bo=p.bo[HGTP_TABLE];const auto &b=p.info[HGTP_TABLE];
 if(cookie!=&p || !value || !bo.backed() || bo.quarantined() || bo.deviceReferences() ||
  !b.allocated || !b.va || b.owned_bytes!=HGTP_BYTES || (pa&7) || pa<b.pa || pa-b.pa>HGTP_BYTES-8) {
  ++p.physical_refusals;return 1;
 }
 *value=*reinterpret_cast<const volatile uint64_t *>((uintptr_t)b.va+(uintptr_t)(pa-b.pa));
 ++p.physical_reads;return 0;
}
/* Independent, bounded three-level lookup of the actual physical graph.
 * 0=invalid descriptor, 1=mapped leaf, -1=malformed/foreign table. */
static int houjiGpuTableLookup(uint64_t iova,uint64_t *leaf,uint64_t *byte_pa) {
 auto &p=gHoujiGpuTableProbe;
 if(!leaf || !byte_pa || iova>=(UINT64_C(1)<<36))return -1;
 uint64_t table=p.info[HGTP_TABLE].pa;
 for(unsigned level=1;level<=3;++level) {
  const unsigned shift=39-9*level;uint64_t d=0;
  if(houjiGpuTableRead(&p,table+8*((iova>>shift)&511),&d))return -1;
  if(!d)return 0;
  if((d&3)!=3)return -1;
  if(level==3){*leaf=d;*byte_pa=(d&UINT64_C(0x0000fffffffff000))|(iova&4095);return 1;}
  if(d!=((d&UINT64_C(0x0000fffffffff000))|3))return -1;
  table=d&UINT64_C(0x0000fffffffff000);
 }
 return -1;
}
static bool houjiGpuTableProbeOwn(unsigned i) {
 auto &p=gHoujiGpuTableProbe;auto &bo=p.bo[i];auto &b=p.info[i];NativeAdrenoCpuView view{};
 b.va=(uint64_t)(uintptr_t)bo.mapping();b.logical_bytes=bo.bytes();b.owned_bytes=bo.ownedBytes();
 if(!b.va || (b.va&(HGTP_BYTES-1)) || b.va>UINT64_MAX-HGTP_BYTES || b.logical_bytes!=HGTP_BYTES ||
  b.owned_bytes!=HGTP_BYTES || bo.cpuPageBytes()!=HGTP_BYTES || bo.spanCount()!=1 || bo.ownedSpanCount()!=1 ||
  bo.imported() || bo.readOnly() || bo.deviceReferences() || bo.quarantined() || !bo.cpuView(&view) ||
  view.spans!=bo.ownedSpans() || view.count!=1 || view.bytes!=HGTP_BYTES)return false;
 const auto &s=bo.spans()[0];const auto &o=bo.ownedSpans()[0];b.pa=o.physical;
 if(s.offset || o.offset || s.bytes!=HGTP_BYTES || o.bytes!=HGTP_BYTES || s.physical!=o.physical ||
  !b.pa || (b.pa&(HGTP_BYTES-1)) || b.pa>=(UINT64_C(1)<<36) || HGTP_BYTES>(UINT64_C(1)<<36)-b.pa)return false;
 /* Recheck raw mapper-free PA at each 4K, rather than derive all subpages
  * solely from the first 16K segment or CPU page number. */
 for(unsigned row=0;row<HGTP_ROWS;++row) {
  IOByteCount length=0;const uint64_t offset=(uint64_t)row*HGTP_PAGE;
  const uint64_t pa=bo.descriptor()->getPhysicalSegment(offset,&length,kIOMemoryMapperNone);
  if(pa!=b.pa+offset || length<HGTP_PAGE)return false;
 }
 return true;
}
static bool houjiGpuTableProbeAtRows(unsigned i) {
 auto &b=gHoujiGpuTableProbe.info[i];
 for(unsigned row=0;row<HGTP_ROWS;++row) {
  const uint64_t offset=(uint64_t)row*HGTP_PAGE+0x5a,va=b.va+offset;uint64_t r=0,w=0;
  const bool valid=houjiGpuTableAt(va,false,&r) && houjiGpuTableAt(va,true,&w);
  b.read_par[row]=r;b.write_par[row]=w;b.rows=row+1;
  if(!valid || (r&1) || (w&1) || ((r&UINT64_C(0x0000fffffffff000))|(va&4095))!=b.pa+offset ||
   ((w&UINT64_C(0x0000fffffffff000))|(va&4095))!=b.pa+offset)return false;
 }
 return true;
}
static bool houjiGpuTableProbeZero(unsigned i,size_t offset,size_t bytes) {
 const auto *p=static_cast<const volatile unsigned char *>(gHoujiGpuTableProbe.bo[i].mapping());
 for(size_t at=offset;at<offset+bytes;++at)if(p[at])return false;
 return true;
}
static void houjiGpuTableProbeOnce(void) {
 int enabled=0;PE_parse_boot_argn("houji_gpu_table_probe",&enabled,sizeof(enabled));if(enabled!=1)return;
 unsigned expected=0;
 if(!__atomic_compare_exchange_n(&gHoujiGpuTableProbeOnce,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return;
 auto &p=gHoujiGpuTableProbe;p.phase=1;p.geometry_status=NativeAdrenoQueryCpuPages(&p.cpu);
 if(p.geometry_status || PAGE_SIZE!=HGTP_BYTES || p.cpu.kernel_page_bytes!=HGTP_BYTES ||
  p.cpu.cpu_page_shift!=14 || (p.cpu.tcr&(UINT64_C(1)<<23))) {
  houjiGpuTableProbeStop(HGTP_GEOMETRY,p.geometry_status?p.geometry_status:kIOReturnUnsupported);return;
 }
 for(unsigned i=0;i<HGTP_BOS;++i) {
  p.phase=2+i;auto &b=p.info[i];auto &bo=p.bo[i];
  b.allocation_status=bo.allocate(HGTP_BYTES,36,true,HGTP_BYTES,false);
  if(b.allocation_status){houjiGpuTableProbeStop(HGTP_ALLOC,b.allocation_status);return;}b.allocated=1;
  if(!houjiGpuTableProbeOwn(i)){houjiGpuTableProbeStop(HGTP_OWNERSHIP,kIOReturnUnsupported);return;}
  if(!houjiGpuTableProbeAtRows(i)){houjiGpuTableProbeStop(HGTP_TRANSLATION,kIOReturnIOError);return;}
  if(!houjiGpuTableProbeZero(i,0,HGTP_BYTES)){houjiGpuTableProbeStop(HGTP_ZERO,kIOReturnIOError);return;}b.zero_valid=1;
 }
 p.phase=4;const auto &data=p.info[HGTP_DATA];const auto &table=p.info[HGTP_TABLE];
 if((data.pa<table.pa+HGTP_BYTES && table.pa<data.pa+HGTP_BYTES) ||
  (data.va<table.va+HGTP_BYTES && table.va<data.va+HGTP_BYTES)) {
  houjiGpuTableProbeStop(HGTP_ALIAS,kIOReturnIOError);return;
 }
 p.config={36,36,table.pa,HGTP_ROWS};
 p.range={kHoujiGpuTableIova,data.pa,HGTP_BYTES,ArmSmmu4KRead|ArmSmmu4KWrite,1,3};
 p.build_status=ArmSmmu4KBuild(&p.config,&p.range,1,static_cast<uint64_t *>(p.bo[HGTP_TABLE].mapping()),HGTP_ROWS,&p.built);
 if(p.build_status || p.built.root_physical!=table.pa || p.built.table_pages!=3 || p.built.leaf_pages!=HGTP_ROWS ||
  p.built.start_level!=1 || p.built.upper_region || p.built.mair!=0xff44) {
  houjiGpuTableProbeStop(HGTP_BUILD,kIOReturnIOError);return;
 }
 p.phase=5;p.status=NativeAdrenoMaintainOwnedPages(p.bo[HGTP_TABLE].mapping(),HGTP_BYTES,NativeAdrenoClean);
 if(p.status){houjiGpuTableProbeStop(HGTP_CACHE,p.status);return;}p.cleaned=1;
 p.phase=6;const ArmSmmu4KTableIo io={&p,houjiGpuTableRead};
 const ArmSmmu4KAuditRegion region={&p.range,1,HGTP_ROWS,HGTP_ROWS*512,p.visited,sizeof(p.visited)};
 p.audit_status=ArmSmmu4KAuditTables(&io,&p.config,&p.built,&region,table.pa,&p.audit);
 if(p.audit_status || !p.audit.complete || p.audit.table_pages!=3 || p.audit.leaf_pages!=HGTP_ROWS || p.audit.descriptor_reads!=3*512) {
  houjiGpuTableProbeStop(HGTP_AUDIT,kIOReturnIOError);return;
 }
 p.phase=7;
 const uint64_t attributes=UINT64_C(3)|(UINT64_C(1)<<11)|(UINT64_C(1)<<10)|(UINT64_C(3)<<8)|(UINT64_C(1)<<6)|(UINT64_C(1)<<2)|(UINT64_C(3)<<53);
 for(unsigned row=0;row<HGTP_ROWS;++row) {
  if(houjiGpuTableLookup(kHoujiGpuTableIova+row*HGTP_PAGE+0x5a,&p.leaf[row],&p.byte_pa[row])!=1 ||
   p.leaf[row]!=((data.pa+row*HGTP_PAGE)|attributes) || p.byte_pa[row]!=data.pa+row*HGTP_PAGE+0x5a) {
   houjiGpuTableProbeStop(HGTP_LEAF,kIOReturnIOError);return;
  }++p.leaves_valid;
 }
 uint64_t ignored_leaf=0,ignored_pa=0;
 if(houjiGpuTableLookup(kHoujiGpuTableIova-1,&ignored_leaf,&ignored_pa)!=0 ||
  houjiGpuTableLookup(kHoujiGpuTableIova+HGTP_BYTES,&ignored_leaf,&ignored_pa)!=0) {
  houjiGpuTableProbeStop(HGTP_LEAF,kIOReturnIOError);return;
 }p.neighbors_invalid=1;
 if(!houjiGpuTableProbeZero(HGTP_TABLE,3*HGTP_PAGE,HGTP_PAGE) || !houjiGpuTableProbeZero(HGTP_DATA,0,HGTP_BYTES)) {
  houjiGpuTableProbeStop(HGTP_SPARE,kIOReturnIOError);return;
 }p.spare_zero=1;p.phase=8;
 for(unsigned i=0;i<HGTP_BOS;++i) {
  auto &b=p.info[i];b.release_attempted=1;b.release_status=p.bo[i].release();
  if(b.release_status){houjiGpuTableProbeStop(HGTP_RELEASE,b.release_status);return;}b.released=1;
 }
 p.phase=9;p.completed=1;p.status=kIOReturnSuccess;houjiGpuTableProbeReport();
}
#endif
