/* SPDX-License-Identifier: MIT
 * Real NativeAdrenoVm/Submit/UserClient core over the same live owned root.
 * Kernel-only ABI v1 qualification. Three exclusive CPU16K NormalNC BOs;
 * every BO/descriptor remains retained even after completed TLB unmapping.
 */
#ifndef HOUJI_GPU_NATIVE_CLIENT_INC
#define HOUJI_GPU_NATIVE_CLIENT_INC
#include "Adreno740/NativeAdrenoUserClient.h"
#include "Adreno740/NativeAdrenoStatus.h"
extern "C" {extern uint64_t (*native_adreno_vm_now_us)(void);}
static struct {
 NativeAdrenoRingClock clock;NativeAdrenoVm vm;NativeAdrenoSubmit submit;NativeAdrenoUserClient client;NativeAdrenoStatus status;
 ArmSmmu4KStreamState streams;NativeAdrenoVmRegion region;
 NativeAdrenoMemory *bo[3];uint64_t handle[3],va[3];
 unsigned active,in_op,phase,code,opened,maps,unmaps,draws,refs_checked;uint32_t crc;
 NativeAdrenoAbiParams params;NativeAdrenoAbiStatus counts;uint64_t started_us;
} gHoujiApi;
static bool houjiApiActive(void){return gHoujiApi.active;}
static bool houjiApiInOperation(void){return gHoujiApi.active && gHoujiApi.in_op;}
static bool houjiProviderTlbValue(uint64_t);
static bool houjiApiTlbValue(uint64_t v){
 if(gHoujiApi.phase==11)return houjiProviderTlbValue(v);
 return houjiApiInOperation() && (v>>48)==0x751 && (v&UINT64_C(0x0000ffffffffffff))>=0x40000 && (v&UINT64_C(0x0000ffffffffffff))<0x4000c;
}
static int houjiProviderPublish(const volatile uint32_t*,size_t);
static int houjiLiveCoexistPublish(const volatile uint32_t*,size_t);
static int houjiApiPublish(const volatile uint32_t*words,size_t bytes){
 auto&s=gHoujiSubmit;auto&a=gHoujiApi;if(a.phase==11)return houjiProviderPublish(words,bytes);if(a.phase==9)return houjiLiveCoexistPublish(words,bytes);if(!houjiApiInOperation() || a.phase!=5 || houjiSubmitGuard() || !s.adopted ||
  s.published || s.pending || words!=houjiCpCpu() || bytes!=HHR_BYTES || s.iteration<65 || s.iteration>80)return 1;
 const uint32_t seq=s.iteration,fence=HHR_SCRATCH+0x104,bv=HHR_SCRATCH+0x10c;
 const uint32_t expected[]={
  hhr_pkt7(0x17,1),0x80000001,hhr_pkt7(0x46,1),0x32,hhr_pkt7(0x46,1),0x33,hhr_pkt7(0x26,0),hhr_pkt7(0x17,1),3,hhr_pkt7(0x65,1),0x101,
  0x70bf8003,0x40008000,0,HGKI_SELECTED_COMMAND_WORDS,
  0x48088501,seq,hhr_pkt7(0x17,1),1,hhr_pkt7(0x46,1),0x18,hhr_pkt7(0x46,1),0x19,hhr_pkt7(0x17,1),2,
  hhr_pkt7(0x46,4),0x08000031,bv,0,seq,hhr_pkt7(0x17,1),1,hhr_pkt7(0x14,4),0,bv,0,seq,
  hhr_pkt7(0x46,4),0x88000031,fence,0,seq,hhr_pkt7(0x17,1),3,hhr_pkt7(0x65,1),0x100};
 static_assert(sizeof(expected)/sizeof(expected[0])==46,"source-bound Native submit batch");
 for(unsigned i=0;i<46;i++)if(words[(s.end+i)&(HHR_WORDS-1)]!=expected[i])return 1;
 s.expected_end=(s.end+46)&(HHR_WORDS-1);s.published=1;houjiStockFullBarrier();return 0;
}
static uint64_t houjiApiNow(void){return houjiSubmitNow(nullptr);}
static bool houjiApiBo(unsigned which,unsigned refs){
 auto&a=gHoujiApi;if(which>=3 || !a.bo[which])return false;auto&bo=*a.bo[which];
 if(!bo.backed() || bo.quarantined() || bo.imported() || bo.readOnly() || !bo.writeCombined() || bo.deviceReferences()!=refs ||
  bo.bytes()!=16384 || bo.ownedBytes()!=16384 || bo.cpuPageBytes()!=16384 || !bo.mapping() ||
  ((uintptr_t)bo.mapping()&16383) || bo.ownedSpanCount()!=1 || bo.spanCount()!=1 || !bo.descriptor() || bo.descriptor()->getLength()!=16384)return false;
 const auto&span=bo.ownedSpans()[0];if(span.offset || span.bytes!=16384 || !span.physical || (span.physical&16383) ||
  span.physical>=(UINT64_C(1)<<36) || bo.spans()[0].physical!=span.physical || bo.spans()[0].offset || bo.spans()[0].bytes!=16384)return false;
 for(unsigned row=0;row<4;row++){
  uint64_t off=row*4096,r=0,w=0,ar=0,aw=0,probe=(uintptr_t)bo.mapping()+off+0x5a,alias=(uint64_t)phystokv(span.physical+off+0x5a);IOByteCount length=0;
  if(bo.descriptor()->getPhysicalSegment(off,&length,kIOMemoryMapperNone)!=span.physical+off || length<4096 ||
   !houjiFullHostAt(probe,false,&r) || !houjiFullHostAt(probe,true,&w) || !alias || !houjiFullHostAt(alias,false,&ar) || !houjiFullHostAt(alias,true,&aw) ||
   (r&1) || r!=w || (r>>56)!=0x44 || (ar&1) || ar!=aw || ((ar>>56)!=0x44 && (ar>>56)!=0xff) ||
   ((r&UINT64_C(0x0000fffffffff000))|(probe&4095))!=span.physical+off+0x5a ||
   ((ar&UINT64_C(0x0000fffffffff000))|(alias&4095))!=span.physical+off+0x5a)return false;
 }
 for(unsigned i=0;i<15;i++){const auto&e=gHoujiFullMemory.entry(i);if(hzr_overlap(span.physical,16384,e.backing.physical,e.request.owned_bytes))return false;}
 for(unsigned i=0;i<3;i++){const auto&e=gHoujiGpuExternalCanary.info[i];if(hzr_overlap(span.physical,16384,e.pa,e.owned))return false;}
 for(unsigned i=0;i<9;i++){const auto&e=gHoujiFirmwareMemory.info[i];if(hzr_overlap(span.physical,16384,e.pa,e.owned))return false;}
 for(unsigned i=0;i<which;i++)if(a.bo[i] && hzr_overlap(span.physical,16384,a.bo[i]->ownedSpans()[0].physical,a.bo[i]->ownedBytes()))return false;
 ++a.refs_checked;return true;
}
static int houjiApiAuditTables(void){
 auto&a=gHoujiApi;if(!houjiVmIdle() || a.vm.auditLive())return 1;
 const auto&e=gHoujiFullMemory.entry(14);gHoujiFull.table_crc[1]=gpp_crc32(e.backing.cpu,e.request.owned_bytes);
 return houjiFullTables()?0:1;
}
static int houjiNativeClientBatch(void){
 auto&a=gHoujiApi;auto&s=gHoujiSubmit;auto&v=gHoujiVm;
 if(a.active || a.phase || s.completed!=64 || s.seq!=64 || !houjiVmIdle() || !v.binding.state || v.binding.state->poisoned)return 1;
 a.clock={nullptr,houjiSubmitNow,houjiSubmitIdle};a.active=a.in_op=1;a.phase=1;a.started_us=houjiSubmitNow(nullptr);
 const ArmSmmu4KIo io={nullptr,0x40000,houjiVmRead32,houjiVmRead64,houjiVmWrite32,houjiVmWrite64,houjiVmBarrier};
 const ArmSmmu4KStream routes[]={{1,0},{2,1}};ArmSmmu4KStreamResult observed{};
 a.code=ArmSmmu4KAdoptStreams(&io,&a.streams,routes,2,0,&v.context,&observed);if(a.code)goto done;
 a.region={const_cast<NativeAdrenoMemory*>(&gHoujiFullMemory.bo(14)),const_cast<ArmSmmu4KLiveArena*>(v.binding.arena),v.binding.state};
 a.code=a.vm.attachSeeded(&io,&v.context,&a.streams,&a.region,nullptr,65536,1000,v.binding.boot_ranges,12,nullptr,0);if(a.code)goto done;
 a.phase=2;native_adreno_vm_now_us=houjiApiNow;
 a.code=a.submit.attach(&a.vm,&s.ring);if(a.code)goto done;
 /* Same kernel owner has actually completed HFC and64 framed draws. Only this
  * board-only call reuses that existing context; normal opens keep reset policy. */
 a.code=a.client.open(NativeAdrenoAbiVersion,&a.vm,&a.submit,&s.ring,32,36,true);if(a.code)goto done;a.opened=1;
 a.code=a.client.attachStatus(&a.status);if(a.code)goto done;
 a.code=a.client.dispatch(NativeAdrenoMethodQuery,nullptr,0,&a.params,sizeof(a.params),&a.clock);
 if(a.code || a.params.abi_version!=1 || a.params.input_bits!=32 || a.params.output_bits!=36){if(!a.code)a.code=1;goto done;}
 for(unsigned i=0;i<3;i++){
  NativeAdrenoAbiBoAlloc request={16384,14,1};NativeAdrenoAbiBoAllocReply reply{};a.phase=3;
  a.code=a.client.dispatch(NativeAdrenoMethodBoAlloc,&request,sizeof(request),&reply,sizeof(reply),&a.clock);if(a.code || !reply.handle){if(!a.code)a.code=2;goto done;}
  a.handle[i]=reply.handle;a.bo[i]=a.client.memoryOf(reply.handle);if(!houjiApiBo(i,0)){a.code=3;goto done;}
  a.va[i]=0x40000000+16384*i;
  NativeAdrenoAbiBoMap map={reply.handle,a.va[i],i==2?1u:3u,0,3,0};NativeAdrenoAbiBoMapReply mapped{};
  a.code=a.client.dispatch(NativeAdrenoMethodBoMap,&map,sizeof(map),&mapped,sizeof(mapped),&a.clock);
  if(a.code || mapped.iova!=a.va[i] || !houjiApiBo(i,1) || houjiApiAuditTables()){if(!a.code)a.code=4;goto done;}++a.maps;
 }
 for(unsigned frame=0;frame<16;frame++){
  unsigned target=frame&1;auto*out=(volatile uint8_t*)a.bo[target]->mapping();auto*cmd=(volatile uint32_t*)a.bo[2]->mapping();a.phase=4;
  if(!houjiVmIdle() || a.submit.outstanding() || !houjiApiBo(target,1) || !houjiApiBo(2,1)){a.code=5;goto done;}
  for(unsigned i=0;i<16384;i++)out[i]=0x6d;
  for(unsigned i=0;i<4096;i++){uint32_t word=i<HGKI_SELECTED_COMMAND_WORDS?hgki_command_word(i):0;cmd[i]=word==HGKI_OUTPUT?(uint32_t)a.va[target]:word;}
  houjiStockFullBarrier();a.phase=5;s.published=s.pending=0;s.iteration=s.seq+1;
  {NativeAdrenoAbiSubmit request{};NativeAdrenoAbiSubmitReply reply{};request.buffer_count=1;request.buffers[0]={a.handle[2],a.va[2],HGKI_SELECTED_COMMAND_WORDS*4};
   const NativeAdrenoRingClock&clock=a.clock;
   a.code=a.client.dispatch(NativeAdrenoMethodSubmit,&request,sizeof(request),&reply,sizeof(reply),&clock);
   if(a.code || reply.fence!=s.iteration){if(!a.code)a.code=6;goto done;}s.seq=(uint32_t)reply.fence;
   NativeAdrenoAbiWaitFence wait={reply.fence,100000000};
   a.code=a.client.dispatch(NativeAdrenoMethodWaitFence,&wait,sizeof(wait),nullptr,0,&clock);
   if(a.code)goto done;s.observed=s.ring.completedFence();}
  a.phase=6;if(s.observed!=s.seq || houjiSubmitDrainPointers() || a.submit.outstanding() || houjiApiAuditTables()){a.code=7;goto done;}
  houjiStockFullBarrier();a.crc=hgki_crc32((const uint8_t*)out,4096);if(a.crc!=0xe843b26a){a.code=8;goto done;}
  for(unsigned i=0;i<16384;i++){uint8_t want=i<4096?((i&3)==0 || (i&3)==3?0xff:0):0x6d;if(out[i]!=want){a.code=9;goto done;}}
  ++a.draws;++s.completed;
  if(frame==0 || frame==15)IOLog("HOUJI NATIVE API STEP: draws=%u target=%u GPU-VA=%llx PA=%llx seq=%u observed=%u RPTR=%u WPTR=%u CRC=%x live-BO=%lu jobs=%lu\n",a.draws,target,a.va[target],a.bo[target]->ownedSpans()[0].physical,s.seq,s.observed,s.rptr,s.wptr,a.crc,a.client.liveObjects(),a.submit.outstanding());
 }
 a.phase=7;
 for(unsigned i=0;i<3;i++){
  a.code=a.client.unmapRetainedForTrial(a.handle[i]);if(a.code || !houjiApiBo(i,0) || houjiApiAuditTables()){if(!a.code)a.code=10;goto done;}++a.unmaps;
 }
 a.code=a.client.dispatch(NativeAdrenoMethodGetStatus,nullptr,0,&a.counts,sizeof(a.counts),&a.clock);if(a.code)goto done;
 a.phase=8;
done:
 IOLog("HOUJI NATIVE API RESULT: code=%x phase=%u opened=%u ABI=%u maps=%u unmaps=%u draws=%u seq=%u observed=%u RPTR=%u WPTR=%u CRC=%x generations=%llu live-BO=%lu jobs=%lu owner-checks=%u elapsed-us=%llu last-IO=%x/%u/%llx; exact Native VM/Submit/UserClient v1, allocated BOs retained; no provider/Metal/desktop claim\n",a.code,a.phase,a.opened,a.params.abi_version,a.maps,a.unmaps,a.draws,s.seq,s.observed,s.rptr,s.wptr,a.crc,v.binding.state->generation,a.client.liveObjects(),a.submit.outstanding(),a.refs_checked,houjiSubmitNow(nullptr)-a.started_us,v.last_offset,v.last_op,v.last_value);
 a.in_op=a.active=0;return a.code || a.draws!=16 || a.unmaps!=3;
}
#endif
