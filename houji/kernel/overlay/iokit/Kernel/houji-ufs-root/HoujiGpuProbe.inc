// SPDX-License-Identifier: BSD-2-Clause-Patent
// Only inherited state. No GPU, GMU, SMMU, clock, power or firmware writes.
#include "GpuReadPolicy.h"
#include "GpuPowerObserve.h"
struct HoujiGpuReadState { OSSharedPtr<IOMemoryMap> maps[3];unsigned reads; };
static int houjiGpuRead(void *cookie,unsigned window,uint32_t offset,uint32_t *value) {
  if(!cookie || !value || !hgr_allowed(window,offset))return 1;
  auto *state=static_cast<HoujiGpuReadState *>(cookie);
  char stage[96];snprintf(stage,sizeof(stage),"GPU RO before read %u PA=%llx",
    state->reads+1,hgr_base[window]+offset);
  houji_ufs_checkpoint(stage,0);
  // Lazy, read-only mappings: failed clock/power prerequisites never map SMMU.
  if(!state->maps[window]) {
    IODeviceMemory *memory=IODeviceMemory::withRange(hgr_base[window],hgr_size[window]);
    if(!memory)return 2;
    state->maps[window]=OSSharedPtr<IOMemoryMap>(memory->map(kIOMapAnywhere|kIOMapReadOnly|kIOMapInhibitCache),OSNoRetain);
    memory->release();if(!state->maps[window])return 3;
  }
  *value=*reinterpret_cast<volatile uint32_t *>(state->maps[window]->getVirtualAddress()+offset);
  __asm__ volatile("dmb oshld" ::: "memory");++state->reads;
  IOLog("HOUJI GPU RO: read=%u PA=%llx value=%08x\n",state->reads,hgr_base[window]+offset,*value);
  return 0;
}
static void houjiGpuQmpObserve(void) {
  // FDT /soc/power-controller@c300000 is qcom,aoss-qmp, reg size 0x400.
  // Inspect only the transport descriptor; do not open a channel or ring IPCC.
  houji_ufs_checkpoint("GPU QMP RO before descriptor",0);
  IODeviceMemory *memory=IODeviceMemory::withRange(0xc300000,0x400);
  if(!memory){IOLog("HOUJI GPU QMP RO: descriptor allocation failed\n");return;}
  OSSharedPtr<IOMemoryMap> map(memory->map(kIOMapAnywhere|kIOMapReadOnly|kIOMapInhibitCache),OSNoRetain);
  memory->release();if(!map){IOLog("HOUJI GPU QMP RO: mapping failed\n");return;}
  auto p=reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress());
  uint32_t words[15];
  for(unsigned i=0;i<15;i++)words[i]=p[i];
  __asm__ volatile("dmb oshld" ::: "memory");
  uint32_t remoteBytes=words[7],remoteOffset=words[8],hostBytes=words[13],hostOffset=words[14];
  bool geometry=words[0]==0x4d41494c && words[1]==1 && remoteBytes>=8 && hostBytes>=8 &&
    !((remoteBytes|remoteOffset|hostBytes|hostOffset)&3) &&
    remoteOffset>=0x3c && hostOffset>=0x3c && remoteOffset<=0x400 && hostOffset<=0x400 &&
    remoteBytes<=0x400-remoteOffset && hostBytes<=0x400-hostOffset &&
    (hostOffset>=remoteOffset+remoteBytes || remoteOffset>=hostOffset+hostBytes);
  IOLog("HOUJI GPU QMP RO: magic=%08x version=%u geometry=%u remote=%x/%x host=%x/%x\n",
    words[0],words[1],geometry,remoteOffset,remoteBytes,hostOffset,hostBytes);
  IOLog("HOUJI GPU QMP RO: remote-link=%08x/%08x remote-channel=%08x/%08x host-link=%08x/%08x host-channel=%08x/%08x\n",
    words[3],words[4],words[5],words[6],words[9],words[10],words[11],words[12]);
  if(geometry)IOLog("HOUJI GPU QMP RO: remote-pending=%u host-pending=%u; no channel or doorbell writes\n",
    p[remoteOffset/4],p[hostOffset/4]);
}
static void houjiGpuProbeOnce(void) {
  int enabled=0;PE_parse_boot_argn("houji_gpu_ro",&enabled,sizeof(enabled));
  if(enabled!=1)return;
  HoujiGpuReadState state={};struct ggp_report report={};
  struct ggp_io io={&state,houjiGpuRead};
  IOLog("HOUJI GPU RO: houji SM8650 Adreno750v2; inherited state only; no GPU/DMA/firmware writes\n");
  ggp_observe(&io,&report);
  IOLog("HOUJI GPU RO: result stage=%u valid=%x reads=%u window=%u offset=%x expected=%x observed=%x mask=%x io-error=%d\n",
    report.stage,report.valid_fields,state.reads,report.window,report.offset,
    report.expected,report.observed,report.mask,report.io_error);
  IOLog("HOUJI GPU RO: cfg=%08x memnoc=%08x dvm=%08x CX=%08x CX-hw=%08x AHB=%08x SMMU-clock=%08x GMU-clock=%08x\n",
    report.cfg,report.memnoc,report.dvm,report.cx,report.cx_hw,report.ahb,report.smmu_clock,report.gmu_clock);
  IOLog("HOUJI GPU RO: SMMU SCR0=%08x ID0=%08x ID1=%08x ID2=%08x fault=%08x; values meaningful only when valid bits set; acceleration=0\n",
    report.scr0,report.id0,report.id1,report.id2,report.fault);
  houjiGpuQmpObserve();
  houji_ufs_checkpoint("GPU RO inherited-state observation finished",0);
}
