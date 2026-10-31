/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Fixed r135 authenticated CP_BLIT path under the retained Native owner.
 * Low metadata is separately reserved before EBS; the IPA neighbour is never
 * read, cleared or written. No arbitrary submissions or resource retirement.
 */
#ifndef HOUJI_GPU_NATIVE_GFX_INC
#define HOUJI_GPU_NATIVE_GFX_INC
#include "HoujiZapHandoff.h"
#include "HoujiZapTrial.h"
#include "GpuPasProbe.h"
#include "GpuZapLoad.h"
#include "GpuFirstContext.h"
#define HGKI_PROGRESS 1
#include "GpuKnownImage2d.h"
static struct {
 HzrRecord handoff;OSSharedPtr<IOMemoryMap> maps[2];
 uint64_t pa[2],va[2],raw,raw_bytes,par[2][4];
 unsigned import_stage,import_which,import_page,imported,import_started,target_closed,target_started,target_complete;
 unsigned image_attempted,prepared,published,phase,pending_wptr,pending_kind,mode_idle,context_idle,image_idle,complete;
 unsigned code,first_phase,diagnostic_valid,diagnostic_red,diagnostic_markers;
 uint32_t diagnostic_crc,diagnostic_first_bad,context_words[HFC_WORDS];
 uint8_t readback[HGKI_BYTES];
 hpp_result capabilities;hzl_result zap;hfc_result context;hgki_result image;
} gHoujiGfx;
#ifdef HOUJI_FULL_HOST_NATIVE_HOST_TEST
static const HzrRecord*houjiGfxHostRecord(void);
static void*houjiGfxHostMap(uint64_t,unsigned);
static int houjiGfxHostSmc(ggs_regs*);
#endif
static bool houjiGfxOutside(uint64_t a,uint64_t n){return hzr_span(a,n) && !hzr_overlap(a,n,gPhysBase,gPhysSize);}
static bool houjiGfxTranslations(unsigned target){
 auto&g=gHoujiGfx;if(!g.imported || (target && g.target_closed))return false;
 for(unsigned which=0;which<(target?2u:1u);which++){for(unsigned i=0;i<4;i++){
  uint64_t r=0,w=0;const uint64_t va=g.va[which]+i*4096;
  if(!houjiFullHostAt(va,false,&r) || !houjiFullHostAt(va,true,&w) || r!=w || !hzr_par(r,g.pa[which]+i*4096) || r!=g.par[which][i])return false;
 }}
 return true;
}
static bool houjiGfxForeign(void){
 const auto&g=gHoujiGfx;if(!g.imported || !hzr_valid(&g.handoff,sizeof(g.handoff),kHoujiZapTrial) ||
  !houjiGfxOutside(g.raw,g.raw_bytes) || !houjiGfxOutside(g.pa[1],16384) ||
  !hzr_covers(g.raw,g.raw_bytes,g.pa[0],16384) || hzr_overlap(g.raw,g.raw_bytes,g.pa[1],16384))return false;
 for(unsigned i=0;i<15;i++){const auto&e=gHoujiFullMemory.entry(i);if(e.backing.physical && (hzr_overlap(e.backing.physical,e.request.owned_bytes,g.raw,g.raw_bytes) || hzr_overlap(e.backing.physical,e.request.owned_bytes,g.pa[1],16384)))return false;}
 return true;
}
static int houjiGfxImport(void){
 auto&g=gHoujiGfx;if(g.import_started)return 1;g.import_started=1;g.import_stage=1;
#ifdef HOUJI_FULL_HOST_NATIVE_HOST_TEST
 const HzrRecord*wire=houjiGfxHostRecord();if(!wire)return 1;memcpy(&g.handoff,wire,sizeof(g.handoff));
#else
 IORegistryEntry*chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);if(!chosen)return 1;
 OSData*data=OSDynamicCast(OSData,chosen->getProperty(HZR_PROPERTY));
 if(!data || data->getLength()!=HZR_BYTES || !data->getBytesNoCopy()){chosen->release();return 1;}
 memcpy(&g.handoff,data->getBytesNoCopy(),sizeof(g.handoff));chosen->release();
#endif
 g.import_stage=2;if(!hzr_valid(&g.handoff,sizeof(g.handoff),kHoujiZapTrial))return 1;
 g.pa[0]=hzr_get64(&g.handoff,HZR_METADATA_PA_O);g.pa[1]=HZR_TARGET_PAGE;g.raw=hzr_get64(&g.handoff,HZR_RAW_BASE_O);g.raw_bytes=hzr_get64(&g.handoff,HZR_RAW_BYTES_O);
 if(!houjiGfxOutside(g.raw,g.raw_bytes) || !houjiGfxOutside(g.pa[1],16384))return 1;
 for(unsigned which=0;which<2;which++){g.import_stage=3;g.import_which=which;
#ifdef HOUJI_FULL_HOST_NATIVE_HOST_TEST
  g.va[which]=(uintptr_t)houjiGfxHostMap(g.pa[which],which);
#else
  IODeviceMemory*m=IODeviceMemory::withRange(g.pa[which],16384);if(!m)return 1;
  g.maps[which]=OSSharedPtr<IOMemoryMap>(m->map(kIOMapAnywhere|kIOMapWriteCombineCache),OSNoRetain);m->release();
  if(!g.maps[which] || g.maps[which]->getLength()!=16384)return 1;g.va[which]=g.maps[which]->getVirtualAddress();
#endif
  if(!g.va[which] || (g.va[which]&16383))return 1;
  for(unsigned i=0;i<4;i++){g.import_stage=4;g.import_page=i;uint64_t r=0,w=0;if(!houjiFullHostAt(g.va[which]+i*4096,false,&r) || !houjiFullHostAt(g.va[which]+i*4096,true,&w) || r!=w || !hzr_par(r,g.pa[which]+i*4096))return 1;g.par[which][i]=r;}
 }
 g.import_stage=5;g.imported=1;if(!houjiGfxForeign() || !houjiGfxTranslations(1))return 1;
 const volatile uint8_t*meta=(const volatile uint8_t*)(uintptr_t)g.va[0];for(unsigned i=0;i<16384;i++)if(meta[i])return 1;
 g.import_stage=6;IOLog("HOUJI NATIVE GFX IMPORT: current trial verified metadata=%llx raw=%llx/%llu; two CPU16K NormalNC maps retained; target data untouched\n",g.pa[0],g.raw,g.raw_bytes);return 0;
}
static int houjiGfxGuard(void){return !houjiGfxForeign() || !houjiGfxTranslations(0) || houjiCpGuard(nullptr);}
static int houjiGfxPrepare(void){
 auto&g=gHoujiGfx;if(g.prepared || !g.imported || gHoujiCp.enabled || houjiGfxGuard() || !houjiGfxTranslations(1))return 1;
 auto*to=(volatile uint8_t*)((uintptr_t)gHoujiFullMemory.entry(12).backing.cpu+HGKI_CLASS_CURSOR);
 for(unsigned i=0;i<HGKI_BYTES;i++)if(to[i])return 1;
 if(hgki_build(g.readback,HGKI_BYTES))return 1;
 for(unsigned i=0;i<HGKI_BYTES;i++)to[i]=g.readback[i];houjiStockFullBarrier();
 for(unsigned i=0;i<HGKI_BYTES;i++)if(to[i]!=hgki_initial_byte(i))return 1;
 g.prepared=1;return houjiGfxGuard();
}
static int houjiGfxCapGuard(void*){return gHoujiCp.prepared && !gHoujiCp.enabled && !houjiGfxGuard()?1:0;}
static bool houjiGfxFrameEqual(const ggs_regs*r,const ggs_regs*e,const ggs_state*t){
 bool resume=r->x[0]==1 && t->last_output.x[0]==1 && r->x[6]==t->last_output.x[6];
 if(!resume && (r->x[0]!=e->x[0] || r->x[6]))return false;
 for(unsigned i=1;i<8;i++)if(i!=6 && r->x[i]!=e->x[i])return false;return true;
}
static int houjiGfxScm(void*,ggs_regs*r){
 auto&g=gHoujiGfx;if(!r || houjiGfxGuard())return 1;ggs_regs expected{};const ggs_state*t=nullptr;
 if(!gHoujiCp.enabled){if(!g.capabilities.started || g.capabilities.phase<1 || g.capabilities.phase>HPP_QUERY_COUNT || hpp_frame(g.capabilities.phase-1,&expected))return 1;t=&g.capabilities.transport;}
 else {auto&z=g.zap;t=&z.transport;
  if(!z.started || !gHoujiCp.ring.init_idle || gHoujiCp.ring.rptr!=10 || gHoujiCp.ring.wptr!=10)return 1;
  if(z.phase==HZL_PHASE_INIT && z.init_attempted && z.metadata_published)expected=(ggs_regs){{0x42000201,0x82,13,g.pa[0],0,0,0,0}};
  else if(z.phase==HZL_PHASE_MEM_SETUP && z.init_succeeded && z.mem_setup_attempted)expected=(ggs_regs){{0x42000202,3,13,HZL_RESERVATION_BASE,HZL_LOAD_SPAN,0,0,0}};
  else if(z.phase==HZL_PHASE_AUTH && z.auth_attempted && z.segment_published && g.target_closed && g.target_complete)expected=(ggs_regs){{0x42000205,1,13,0,0,0,0,0}};
  else return 1;
 }
 if(!houjiGfxFrameEqual(r,&expected,t))return 1;
 IOLog("HOUJI NATIVE GFX SMC: phase=%u fn=%llx args=%llx/%llx/%llx/%llx/%llx cookie=%llx; exact signed PAS13 path\n",g.phase,r->x[0],r->x[1],r->x[2],r->x[3],r->x[4],r->x[5],r->x[6]);
#ifdef HOUJI_FULL_HOST_NATIVE_HOST_TEST
 int code=houjiGfxHostSmc(r);
#else
 int code=ggs_aarch64_call(nullptr,r);
#endif
 IOLog("HOUJI NATIVE GFX SMC RETURN: fn=%llx a0=%llx a1=%llx cookie=%llx transport=%d\n",expected.x[0],r->x[0],r->x[1],r->x[6],code);return code;
}
static int houjiGfxDelay(void*,unsigned ms){if(ms!=GGS_BUSY_DELAY_MS || houjiGfxGuard())return 1;IOSleep(ms);return houjiGfxGuard();}
static int houjiGfxCapabilities(void){const hpp_io io={nullptr,houjiGfxCapGuard,houjiGfxScm,houjiGfxDelay};unsigned code=hpp_run(&io,&gHoujiGpuSecureCx.scm,&gHoujiGfx.capabilities);IOLog("HOUJI NATIVE GFX CAPS: code=%u completed=%u available=%x supported=%u; no authentication claim\n",code,gHoujiGfx.capabilities.completed,gHoujiGfx.capabilities.available_mask,gHoujiGfx.capabilities.supported);return code || !hzl_capabilities(&gHoujiGpuSecureCx.scm,&gHoujiGfx.capabilities);}
static int houjiGfxZapGuard(void*,unsigned phase){auto&g=gHoujiGfx;if(phase!=g.zap.phase || houjiGfxGuard() || !g.prepared)return 0;if(g.target_closed && phase<HZL_PHASE_COPY)return 0;if(phase>=HZL_PHASE_AUTH && (!g.target_closed || !g.target_complete))return 0;return g.target_closed || houjiGfxTranslations(1)?1:0;}
static int houjiGfxMetadata(void*,void*cpu,uint64_t pa,unsigned bytes){auto&g=gHoujiGfx;if(g.zap.phase!=HZL_PHASE_PUBLISH || cpu!=(void*)(uintptr_t)g.va[0] || pa!=g.pa[0] || bytes!=HZL_METADATA_BYTES || houjiGfxZapGuard(nullptr,HZL_PHASE_PUBLISH)!=1)return 1;houjiStockFullBarrier();return 0;}
static int houjiGfxCopy(void*,uint64_t dest,const uint8_t*src,unsigned bytes){
 auto&g=gHoujiGfx;if(g.zap.phase!=HZL_PHASE_COPY || !g.zap.init_succeeded || !g.zap.mem_setup_succeeded || !g.zap.segment_copy_attempted || g.target_closed || g.target_started || dest!=HZL_RESERVATION_BASE || src!=mHoujiZapSegment || bytes!=HZL_SEGMENT_BYTES || houjiGfxZapGuard(nullptr,HZL_PHASE_COPY)!=1)return 1;
 g.target_started=g.target_closed=1;auto*to=(volatile uint8_t*)(uintptr_t)(g.va[1]+0x2000);for(unsigned i=0;i<HZL_SEGMENT_BYTES;i++)to[i]=src[i];houjiStockFullBarrier();g.target_complete=1;return 0;
}
static unsigned houjiGfxCanaryBase(void){return gHoujiGfx.complete?41u:10u;}
static bool houjiGfxRingWrite(uint32_t a,uint32_t v){auto&g=gHoujiGfx;return a==0x3d0201c && g.pending_kind && v==g.pending_wptr && ((g.pending_kind==1 && v==12) || (g.pending_kind==2 && v==37 && g.mode_idle) || (g.pending_kind==3 && v==41 && g.context_idle));}
static int houjiGfxSubmit(unsigned kind){
 auto&g=gHoujiGfx;auto&r=gHoujiCp.ring;if(houjiGfxGuard() || !g.zap.authenticated || r.phase!=4 || r.rptr!=r.wptr || !r.busy_valid || (r.busy&(1u<<23)) || g.pending_kind)return 1;
 const unsigned offset=kind==1?10:kind==2?12:37,count=kind==1?2:kind==2?HFC_WORDS:4,next=offset+count;
 if(kind<1 || kind>3 || r.wptr!=offset || (kind>=2 && !g.mode_idle) || (kind==3 && !g.context_idle))return 1;
 auto*cpu=houjiCpCpu();for(unsigned i=0;i<count;i++)if(cpu[offset+i])return 1;
 for(unsigned i=0;i<count;i++){uint32_t word=kind==1?(i?0:hhr_pkt7(0x66,1)):kind==2?hfc_word(gHoujiFull.graph[1].root_physical,i):hgki_ib[i];if(kind==2 && word!=g.context_words[i])return 1;cpu[offset+i]=word;}
 houjiStockFullBarrier();for(unsigned i=0;i<count;i++){uint32_t word=kind==1?(i?0:hhr_pkt7(0x66,1)):kind==2?hfc_word(gHoujiFull.graph[1].root_physical,i):hgki_ib[i];if(cpu[offset+i]!=word)return 1;}
 g.pending_kind=kind;g.pending_wptr=next;r.wptr=next;const hhr_io io={.cookie=nullptr,.guard=houjiCpGuard,.read=houjiCpRead,.write=houjiCpWrite,.delay=houjiCpDelay};
 unsigned code=hhr_kick(&io,&r);if(!code)code=hhr_idle(&io,&r);IOLog("HOUJI NATIVE GFX SUBMIT: kind=%u code=%u RPTR=%u WPTR=%u IRQ=%x fault=%x status=%x busy=%x last=%08x/%08x valid=%u\n",kind,code,r.rptr,r.wptr,r.irq,r.hwfault,r.status,r.busy,r.last_address,r.last_value,r.last_valid);
 if(code)return 1;g.pending_kind=0;return 0;
}
static int houjiGfxContextOwner(void*){return !gHoujiGfx.mode_idle || houjiGfxGuard() || !houjiFullLease() || !houjiFullTables();}
static int houjiGfxContextRoot(void*,uint32_t a,uint64_t*v){return (a!=0x3dc0020 && a!=0x3dc0028) || houjiGfxContextOwner(nullptr)?1:houjiFullRaw64(a,v);}
static void houjiGfxReadImage(void){houjiStockFullBarrier();auto*from=(const volatile uint8_t*)((uintptr_t)gHoujiFullMemory.entry(12).backing.cpu+HGKI_CLASS_CURSOR);for(unsigned i=0;i<HGKI_BYTES;i++)gHoujiGfx.readback[i]=from[i];}
static int houjiGfxPostInit(void*,hhr_result*r){
 auto&g=gHoujiGfx;if(r!=&gHoujiCp.ring || g.phase || !g.prepared || !r->init_idle || r->rptr!=10 || r->wptr!=10 || houjiGfxGuard())return 1;
 g.phase=1;const hzl_io io={nullptr,houjiGfxZapGuard,houjiGfxMetadata,houjiGfxCopy,houjiGfxScm,houjiGfxDelay};const hzl_config config={(void*)(uintptr_t)g.va[0],g.pa[0],4096,HZL_RESERVATION_BASE,HZL_RESERVATION_BYTES};
 g.code=hzl_run(&io,&config,&gHoujiGpuSecureCx.scm,&g.capabilities,&g.zap);if(g.code)goto failed;
 g.phase=2;if(houjiGfxSubmit(1))goto failed;g.mode_idle=1;
 {const hfc_config c={gHoujiFull.graph[1].root_physical,gHoujiFull.graph[1].root_physical,1,1,1,1,1};const hfc_io f={nullptr,houjiGfxContextOwner,houjiGfxContextRoot};g.code=hfc_build(&f,&c,g.context_words,HFC_WORDS,&g.context);if(g.code)goto failed;}
 g.phase=3;if(houjiGfxSubmit(2))goto failed;g.context_idle=1;
 g.phase=4;houjiGfxReadImage();for(unsigned i=0;i<HGKI_BYTES;i++)if(g.readback[i]!=hgki_initial_byte(i))goto failed;
 g.phase=5;g.image_attempted=1;if(houjiGfxSubmit(3))goto failed;g.image_idle=1;
 g.phase=6;houjiGfxReadImage();g.code=hgki_verify(g.readback,HGKI_BYTES,&g.image);if(g.code)goto failed;
 g.complete=1;g.phase=7;IOLog("HOUJI NATIVE GFX RESULT: code=0 authenticated=1 mode-idle=1 context-idle=1 image-idle=1 verified=%u CRC=%08x pixels=1024 markers=8; dedicated2D only, no shaders/Metal\n",g.image.verified,g.image.output_crc);return 0;
failed:
 if(!g.code)g.code=100+g.phase;g.first_phase=g.phase;IOLog("HOUJI NATIVE GFX RESULT: code=%u phase=%u ZAP=%u/%u auth=%u target=%u/%u mode=%u context=%u image=%u firstbad=%u expected=%02x actual=%02x; failure retained\n",g.code,g.phase,g.zap.code,g.zap.phase,g.zap.authenticated,g.target_started,g.target_complete,g.mode_idle,g.context_idle,g.image_idle,g.image.first_bad,g.image.expected,g.image.actual);return 1;
}
static unsigned houjiGfxZapAttempted(void){return gHoujiGfx.zap.init_attempted;}
static unsigned houjiGfxImageAttempted(void){return gHoujiGfx.image_attempted;}
static void houjiGfxAfterStop(void){
 auto&g=gHoujiGfx;if(!g.prepared || !gHoujiCp.isolated || gHoujiCp.stop_stage!=5 || !houjiGfxForeign())return;houjiGfxReadImage();g.diagnostic_valid=1;g.diagnostic_crc=hgki_crc32(g.readback+5*4096,4096);for(unsigned i=0;i<1024;i++){const auto*q=g.readback+5*4096+4*i;if(q[0]==255 && !q[1] && !q[2] && q[3]==255)++g.diagnostic_red;}for(unsigned i=0;i<8;i++)if(hzr_u32(g.readback+4*i)==hgki_marker_value(i))g.diagnostic_markers|=1u<<i;IOLog("HOUJI NATIVE GFX POSTSTOP: complete=%u phase=%u code=%u red=%u CRC=%08x markers=%02x target-closed=%u; retained CPU snapshot, not extra execution\n",g.complete,g.phase,g.code,g.diagnostic_red,g.diagnostic_crc,g.diagnostic_markers,g.target_closed);
}
#endif
