/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * houji_gpu_cpu_probe=1: XNU CPU-only, once per boot. No GPU/clock/SMMU
 * access, pmap/cache-attribute mutation, cache maintenance or payload writes.
 * An exclusive 16 KiB descriptor is never published to a device. Unknown
 * prepare/ownership/complete state retains the descriptor until reset.
 */
#ifndef HOUJI_GPU_CPU_PAGE_PROBE_INC
#define HOUJI_GPU_CPU_PAGE_PROBE_INC
#ifndef HOUJI_CPU_PAGE_PROBE_HOST_TEST
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <kern/cpu_data.h>
#include <kern/thread.h>
#include <mach/vm_param.h>
#include <vm/vm_map.h>
#include <pexpert/pexpert.h>
#endif
#include "GpuCpuPageGeometry.h"
enum {HCP_PROBE_OK,HCP_PROBE_GEOMETRY,HCP_PROBE_ALLOC,HCP_PROBE_PREPARE,
 HCP_PROBE_OWNERSHIP,HCP_PROBE_SEGMENT,HCP_PROBE_COMPLETE};
static unsigned gHoujiCpuPageProbeOnce;
static struct {
 struct hcpg_sample sample;
 struct hcpg_geometry geometry;
 struct hcpg_row rows[HCPG_ROWS];
 IOBufferMemoryDescriptor *descriptor;
 uint64_t cpu_address,descriptor_bytes;
 unsigned phase,code,prepare_attempted,prepared,rows_attempted,rows_valid;
 unsigned complete_attempted,completed,released,retained;
 IOReturn prepare_status,complete_status;
} gHoujiCpuPageProbe;
#ifdef HOUJI_CPU_PAGE_PROBE_HOST_TEST
static void houjiGpuCpuPageReadRegisters(struct hcpg_sample *s);
#else
static void houjiGpuCpuPageReadRegisters(struct hcpg_sample *s) {
 __asm__ volatile("mrs %0,tcr_el1\n\tmrs %1,ttbr0_el1\n\tmrs %2,ttbr1_el1\n\tmrs %3,sctlr_el1"
  :"=r"(s->tcr),"=r"(s->ttbr0),"=r"(s->ttbr1),"=r"(s->sctlr)::"memory");
}
#endif
static void houjiGpuCpuPageProbeReport(void) {
 const auto &p=gHoujiCpuPageProbe;const auto &s=p.sample;const auto &g=p.geometry;
 IOLog("HOUJI GPU CPU GEOMETRY RAW: cpu=%d/%d TCR=%llx TTBR0=%llx TTBR1=%llx SCTLR=%llx PAGE_SIZE=%llu page_size=%llu kernel_map_page=%llu; sampled with preemption disabled, CPU only\n",
  s.cpu_before,s.cpu_after,(unsigned long long)s.tcr,(unsigned long long)s.ttbr0,
  (unsigned long long)s.ttbr1,(unsigned long long)s.sctlr,(unsigned long long)s.compiled_bytes,
  (unsigned long long)s.runtime_bytes,(unsigned long long)s.map_bytes);
 IOLog("HOUJI GPU CPU GEOMETRY POLICY: issues=%x cpu-shift=%u TG0=%u TG1=%u T0SZ=%u T1SZ=%u EPD0=%u EPD1=%u IPS=%u; 4K/16K same-granule policy, mixed/unsupported is not a hardware-fault inference\n",
  g.issues,g.cpu_shift,g.tg0,g.tg1,g.t0sz,g.t1sz,g.epd0,g.epd1,g.ips);
 for(unsigned i=0;i<p.rows_attempted;i++) {
  const auto &r=p.rows[i];
  IOLog("HOUJI GPU CPU PAGE ROW: i=%u offset=%llu PA-byte=%llx segment-bytes=%llu cpu-ppn=%llx gpu4K-ppn=%llx cpu-subpage=%u gpu36-fit=%u valid=%u issues=%x; page numbers only, no GPU mapping\n",
   i,(unsigned long long)r.offset,(unsigned long long)r.physical_byte,
   (unsigned long long)r.segment_bytes,(unsigned long long)r.cpu_ppn,
   (unsigned long long)r.gpu4k_ppn,r.cpu_subpage,r.gpu36_fit,r.valid,r.issues);
 }
 IOLog("HOUJI GPU CPU PAGE PROBE: phase=%u code=%u owned-bytes=%u descriptor-bytes=%llu CPU-VA=%llx prepare-attempted=%u status=%x prepared=%u rows=%u/%u complete-attempted=%u status=%x completed=%u released=%u retained=%u; exclusive CPU descriptor only, GPU-execution=0 attributes-changed=0\n",
  p.phase,p.code,HCPG_OWNED_BYTES,(unsigned long long)p.descriptor_bytes,
  (unsigned long long)p.cpu_address,p.prepare_attempted,(unsigned)p.prepare_status,
  p.prepared,p.rows_valid,p.rows_attempted,p.complete_attempted,(unsigned)p.complete_status,
  p.completed,p.released,p.retained);
}
static void houjiGpuCpuPageProbeOnce(void) {
 int enabled=0;PE_parse_boot_argn("houji_gpu_cpu_probe",&enabled,sizeof(enabled));
 if(enabled!=1)return;
 unsigned expected=0;
 if(!__atomic_compare_exchange_n(&gHoujiCpuPageProbeOnce,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return;
 auto &p=gHoujiCpuPageProbe;p.phase=1;
 disable_preemption();
 p.sample.cpu_before=cpu_number();houjiGpuCpuPageReadRegisters(&p.sample);
 p.sample.compiled_bytes=PAGE_SIZE;p.sample.runtime_bytes=page_size;
 p.sample.map_bytes=kernel_map?vm_map_page_size(kernel_map):0;
 p.sample.cpu_after=cpu_number();enable_preemption();
 if(hcpg_geometry_check(&p.sample,&p.geometry)){p.code=HCP_PROBE_GEOMETRY;houjiGpuCpuPageProbeReport();return;}
 p.phase=2;
 const IOOptionBits options=static_cast<IOOptionBits>(kIODirectionInOut)|
  static_cast<IOOptionBits>(kIOMemoryPhysicallyContiguous)|static_cast<IOOptionBits>(kIOMemoryMapperNone);
 p.descriptor=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,options,HCPG_OWNED_BYTES,HCPG_OWNED_BYTES);
 if(!p.descriptor){p.code=HCP_PROBE_ALLOC;houjiGpuCpuPageProbeReport();return;}
 p.retained=1;p.prepare_attempted=1;p.phase=3;
 p.prepare_status=p.descriptor->prepare(kIODirectionInOut);
 if(p.prepare_status!=kIOReturnSuccess){p.code=HCP_PROBE_PREPARE;houjiGpuCpuPageProbeReport();return;}
 p.prepared=1;p.phase=4;
 p.cpu_address=(uint64_t)(uintptr_t)p.descriptor->getBytesNoCopy();
 p.descriptor_bytes=p.descriptor->getLength();
 if(!p.cpu_address || (p.cpu_address&(p.sample.compiled_bytes-1)) ||
  p.cpu_address>UINT64_MAX-HCPG_OWNED_BYTES || p.descriptor_bytes!=HCPG_OWNED_BYTES) {
  p.code=HCP_PROBE_OWNERSHIP;houjiGpuCpuPageProbeReport();return;
 }
 uint64_t first=0;
 for(unsigned i=0;i<HCPG_ROWS;i++) {
  IOByteCount length=0;uint64_t offset=(uint64_t)i*HCPG_GPU_PAGE;
  uint64_t pa=p.descriptor->getPhysicalSegment((IOByteCount)offset,&length,kIOMemoryMapperNone);
  if(!i)first=pa;++p.rows_attempted;
  if(hcpg_segment_check(p.sample.compiled_bytes,first,offset,pa,(uint64_t)length,&p.rows[i])) {
   p.code=HCP_PROBE_SEGMENT;houjiGpuCpuPageProbeReport();return;
  }
  ++p.rows_valid;
 }
 p.phase=5;p.complete_attempted=1;p.complete_status=p.descriptor->complete(kIODirectionInOut);
 if(p.complete_status!=kIOReturnSuccess){p.code=HCP_PROBE_COMPLETE;houjiGpuCpuPageProbeReport();return;}
 p.prepared=0;p.completed=1;p.descriptor->release();p.descriptor=nullptr;
 p.released=1;p.retained=0;p.phase=6;houjiGpuCpuPageProbeReport();
}
#endif
