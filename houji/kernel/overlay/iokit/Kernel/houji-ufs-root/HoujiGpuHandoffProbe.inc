/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Opt-in, one-shot scalar power handoff observation, not a GPU owner.
 * r140's sole GPU path runs synchronously after the CPU workers have joined;
 * no legacy RO probe, NativeAdreno service/owner or firmware/context/ring path
 * may run concurrently. exclusive() asserts ONLY that software configuration.
 * Device mappings and partial observations remain alive until platform reset.
 * No firmware pointers, EFI mappings, descriptors, roots or IOVAs are reused.
 */
#ifndef HOUJI_GPU_HANDOFF_PROBE_INC
#define HOUJI_GPU_HANDOFF_PROBE_INC
#include "GpuPowerOnlyHandoff.h"
#ifndef HOUJI_GPU_HANDOFF_PROBE_HOST_TEST
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSSharedPtr.h>
#include <pexpert/pexpert.h>
#endif

enum {HGHP_WINDOWS=4,HGHP_READS=192};
enum {HGHP_OK,HGHP_CHOSEN,HGHP_PROPERTY,HGHP_RECORD,HGHP_OBSERVE,HGHP_RECHECK};
enum {HGHP_IO_OK,HGHP_IO_ARGUMENT,HGHP_IO_OWNER,HGHP_IO_ORDER,HGHP_IO_CAPACITY,
 HGHP_IO_ALLOC,HGHP_IO_MAP,HGHP_IO_MAPPING,HGHP_IO_ALL_ONES};
/* CPU-page-aligned, fixed Device apertures. Only hph_read_allowed() words
 * within them may actually be loaded. Mapping a range is not reading it. */
static const uint64_t kHoujiGpuHandoffBase[HGHP_WINDOWS]={
 UINT64_C(0x170000),UINT64_C(0x3d90000),UINT64_C(0x3d7c000),UINT64_C(0x3da0000)};
static const uint64_t kHoujiGpuHandoffBytes[HGHP_WINDOWS]={0x4000,0x10000,0x4000,0x40000};
static unsigned gHoujiGpuHandoffProbeOnce;
static struct {
 OSSharedPtr<IOMemoryMap> maps[HGHP_WINDOWS];
 struct hph_record record;
 struct hph_cold cold;
 struct hph_clock clock;
 struct Read {uint32_t address,value,status,attempted,returned,valid;} reads[HGHP_READS];
 unsigned phase,code,property_bytes,decoded,record_ready,active,complete;
 unsigned calls,valid_reads,gate_valid,map_attempted,map_valid,first_io_code,first_io_address;
 unsigned observe_code,recheck_code,failed_address;
} gHoujiGpuHandoffProbe;

#ifdef HOUJI_GPU_HANDOFF_PROBE_HOST_TEST
static void houjiGpuHandoffReadBarrier(void);
#else
static void houjiGpuHandoffReadBarrier(void) {__asm__ volatile("dmb oshld":::"memory");}
#endif
static int houjiGpuHandoffExclusive(void *cookie) {
 const auto &p=gHoujiGpuHandoffProbe;
 return cookie!=&gHoujiGpuHandoffProbe || !p.active || !p.decoded || !p.record_ready ||
  p.complete || p.first_io_code || __atomic_load_n(&gHoujiGpuHandoffProbeOnce,__ATOMIC_ACQUIRE)!=1;
}
static unsigned houjiGpuHandoffIoFail(unsigned code,uint32_t address) {
 auto &p=gHoujiGpuHandoffProbe;
 if(!p.first_io_code){p.first_io_code=code;p.first_io_address=address;}
 return code;
}
static int houjiGpuHandoffWindow(uint32_t address) {
 for(unsigned i=0;i<HGHP_WINDOWS;i++)
  if(address>=kHoujiGpuHandoffBase[i] && address-kHoujiGpuHandoffBase[i]<=kHoujiGpuHandoffBytes[i]-4)return (int)i;
 return -1;
}
/* Additional adapter-side access ordering. The portable observer performs
 * the real masked guard checks too. A failed prerequisite must never cause
 * a dependent GPU/GMU/SMMU mapping or load. No failed gate becomes valid. */
static unsigned houjiGpuHandoffRequired(uint32_t address) {
 for(unsigned i=0;i<8;i++)if(address==hgb_address(i))return (1u<<i)-1;
 return 0xff;
}
static void houjiGpuHandoffGate(uint32_t address,uint32_t value) {
 const uint32_t masks[]={0x80000003u,0x80000003u,0x80000001u,1,0x80000000u,
  0x80000000u,0xffffffffu,0x80000001u};
 const uint32_t wanted[]={1,1,1,0,0x80000000u,0,1,1};
 for(unsigned i=0;i<8;i++)if(address==hgb_address(i)) {
  if((value&masks[i])==wanted[i])gHoujiGpuHandoffProbe.gate_valid|=1u<<i;
  else gHoujiGpuHandoffProbe.gate_valid&=~(1u<<i);
  return;
 }
}
static GPP_U64 houjiGpuHandoffRead(void *cookie,GPP_U32 address,GPP_U32 *value) {
 auto &p=gHoujiGpuHandoffProbe;
 if(!value || (address&3) || !hph_read_allowed(address))return houjiGpuHandoffIoFail(HGHP_IO_ARGUMENT,address);
 if(houjiGpuHandoffExclusive(cookie))return houjiGpuHandoffIoFail(HGHP_IO_OWNER,address);
 const unsigned required=houjiGpuHandoffRequired(address);
 if((p.gate_valid&required)!=required)return houjiGpuHandoffIoFail(HGHP_IO_ORDER,address);
 const int window=houjiGpuHandoffWindow(address);
 if(window<0)return houjiGpuHandoffIoFail(HGHP_IO_ARGUMENT,address);
 if(p.calls>=HGHP_READS)return houjiGpuHandoffIoFail(HGHP_IO_CAPACITY,address);
 auto &r=p.reads[p.calls++];r.address=address;r.attempted=1;
 if(!p.maps[window]) {
  p.map_attempted|=1u<<(unsigned)window;
  IODeviceMemory *memory=IODeviceMemory::withRange(kHoujiGpuHandoffBase[window],kHoujiGpuHandoffBytes[window]);
  if(!memory){r.status=houjiGpuHandoffIoFail(HGHP_IO_ALLOC,address);return r.status;}
  p.maps[window]=OSSharedPtr<IOMemoryMap>(memory->map(kIOMapAnywhere|kIOMapReadOnly|kIOMapInhibitCache),OSNoRetain);
  memory->release();
  if(!p.maps[window]){r.status=houjiGpuHandoffIoFail(HGHP_IO_MAP,address);return r.status;}
 }
 const uint64_t va=p.maps[window]->getVirtualAddress(),length=p.maps[window]->getLength();
 const uint64_t offset=address-kHoujiGpuHandoffBase[window];
 if(!va || (va&3) || length<kHoujiGpuHandoffBytes[window] || va>UINT64_MAX-offset-4) {
  r.status=houjiGpuHandoffIoFail(HGHP_IO_MAPPING,address);return r.status;
 }
 p.map_valid|=1u<<(unsigned)window;
 /* This boundary may still fault or stall on hardware. There is deliberately
  * no recoverable-fault framework. The pre-load log is an intent marker only;
  * returned/valid are set only after the real volatile load and read barrier. */
 IOLog("HOUJI GPU HANDOFF LOAD: i=%u address=%x before Device RO load; no fault recovery\n",p.calls-1,address);
 r.value=*reinterpret_cast<const volatile uint32_t *>((uintptr_t)(va+offset));
 houjiGpuHandoffReadBarrier();r.returned=1;*value=r.value;
 if(r.value==UINT32_MAX){r.status=houjiGpuHandoffIoFail(HGHP_IO_ALL_ONES,address);return r.status;}
 r.valid=1;++p.valid_reads;houjiGpuHandoffGate(address,r.value);return 0;
}
static void houjiGpuHandoffProbeReport(void) {
 const auto &p=gHoujiGpuHandoffProbe;const auto &r=p.record;
 IOLog("HOUJI GPU HANDOFF RECORD: bytes=%u decoded=%u ready=%u magic=%x version=%u phase=%u code=%u complete=%u retained=%u crc=%08x; scalars only, no EFI pointers/resources inherited\n",
  p.property_bytes,p.decoded,p.record_ready,r.magic,r.version,r.phase,r.code,r.complete,r.retained,r.crc32);
 for(unsigned i=0;i<p.calls;i++) {
  const auto &v=p.reads[i];
  IOLog("HOUJI GPU HANDOFF READ: i=%u address=%x raw=%08x attempted=%u returned=%u valid=%u status=%u\n",
   i,v.address,v.value,v.attempted,v.returned,v.valid,v.status);
 }
 IOLog("HOUJI GPU HANDOFF COLD: valid=%x expected=ffff CM3-reset=%08x FWINIT=%08x HFI=%08x GXPOWER=%08x SMMU-CR0=%08x GFSR=%08x; values meaningful only when their read is valid\n",
  p.cold.valid,p.cold.values[HGB_RESET],p.cold.values[HGB_FWINIT],p.cold.hfi,
  p.cold.values[HGB_GXPOWER],p.cold.values[HGB_CR0],p.cold.values[HGB_GFSR]);
 for(unsigned at=0;at<2;at++)for(unsigned i=0;i<HCW_REGS;i++)
  IOLog("HOUJI GPU HANDOFF CLOCK: sample=%u i=%u address=%x raw=%08x valid=%u; raw programming, not measured Hz\n",
   at,i,hcw_address(i),p.clock.raw[at][i],(p.clock.valid[at]>>i)&1u);
 for(unsigned i=0;i<2;i++)
  IOLog("HOUJI GPU HANDOFF RATE: i=%u configured-valid=%u status=%x:%08x configured-Hz=%u source=%u hid=%u nominal-valid=%u nominal-Hz=%llu; configured/derived are NOT frequency measurements\n",
   i,(p.clock.configured_valid>>i)&1u,p.clock.configured_status_hi[i],p.clock.configured_status_lo[i],
   p.clock.configured_hz[i],p.clock.source[i],p.clock.hid[i],(p.clock.derived_valid>>i)&1u,
   (unsigned long long)(p.clock.derived_lo[i]|((uint64_t)p.clock.derived_hi[i]<<32)));
 for(unsigned i=0;i<24;i++)
  IOLog("HOUJI GPU HANDOFF STREAM: i=%u SMR=%08x S2CR=%08x; partial validity is in READ rows\n",i,p.cold.smr[i],p.cold.s2cr[i]);
 for(unsigned i=0;i<22;i++)
  IOLog("HOUJI GPU HANDOFF BANK: i=%u SCTLR=%08x; partial validity is in READ rows\n",i,p.cold.sctlr[i]);
 IOLog("HOUJI GPU HANDOFF RESULT: phase=%u code=%u observer=%u recheck=%u complete=%u reads=%u valid-reads=%u gates=%x maps-attempted=%x maps-valid=%x first-io=%u first-address=%x observer-first-address=%x; software single-owner only, mappings retained, GPU-writes=0 GPU-execution=0 DMA=0\n",
  p.phase,p.code,p.observe_code,p.recheck_code,p.complete,p.calls,p.valid_reads,p.gate_valid,
  p.map_attempted,p.map_valid,p.first_io_code,p.first_io_address,p.failed_address);
}
static void houjiGpuHandoffProbeOnce(void) {
 int enabled=0;PE_parse_boot_argn("houji_gpu_handoff_probe",&enabled,sizeof(enabled));if(enabled!=1)return;
 unsigned expected=0;
 if(!__atomic_compare_exchange_n(&gHoujiGpuHandoffProbeOnce,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return;
 auto &p=gHoujiGpuHandoffProbe;p.phase=1;
 IORegistryEntry *chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
 if(!chosen){p.code=HGHP_CHOSEN;houjiGpuHandoffProbeReport();return;}
 OSData *data=OSDynamicCast(OSData,chosen->getProperty("houji,gpu-power-handoff"));
 p.property_bytes=data?data->getLength():0;p.phase=2;
 if(!data || p.property_bytes!=HPH_RECORD_BYTES || !data->getBytesNoCopy()) {
  chosen->release();p.code=HGHP_PROPERTY;houjiGpuHandoffProbeReport();return;
 }
 p.decoded=hph_decode(data->getBytesNoCopy(),p.property_bytes,&p.record)?1u:0u;chosen->release();
 p.record_ready=p.decoded && p.record.complete==1 && p.record.code==HPH_OK;
 if(!p.record_ready){p.code=HGHP_RECORD;houjiGpuHandoffProbeReport();return;}
 p.phase=3;p.active=1;const struct hph_ro_io io={&p,houjiGpuHandoffRead,houjiGpuHandoffExclusive};
 p.observe_code=hph_observe_ro(&io,&p.cold,&p.clock,&p.failed_address);
 if(!p.observe_code && houjiGpuHandoffExclusive(&p)) {
  houjiGpuHandoffIoFail(HGHP_IO_OWNER,0);p.observe_code=HPH_OWNER;
 }
 p.active=0;
 if(p.observe_code){p.code=HGHP_OBSERVE;houjiGpuHandoffProbeReport();return;}
 p.phase=4;p.recheck_code=hph_recheck(&p.record,&p.cold,&p.clock);
 if(p.recheck_code){p.code=HGHP_RECHECK;houjiGpuHandoffProbeReport();return;}
 p.phase=5;p.complete=1;houjiGpuHandoffProbeReport();
}
#endif
