/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "houji_native_full_host_graph.h"
#include <string.h>
#include "ArmSmmu4KLive.h"
#define HFM_WITH_SCRATCH 1
#include "GpuFirmwareMemory.h"
#include "GpuScratchContext.h"
#define FHG_MAGIC UINT64_C(0x46484731494e4143)
struct FhgState {
 _Alignas(FHG_STORAGE_ALIGNMENT) uint64_t magic,inverse;uintptr_t self;
 FhgConfig config;FhgOps ops;
 uint32_t phase,busy,prepare_called,ready,code,stage,allocate_cursor,release_cursor;
 uint32_t allocated,validated,published,uncertain,tail_verified,hfm_first_error,hfm_return,output_conflict;
 FhgFailure failure;FhgTrace traces[4];FhgRow rows[FHG_ROWS];
 const uint8_t *assets[3];size_t asset_bytes[3];const void *output_guard;
 struct hfm_result hfm;
 ArmSmmu4KRange gpu_ranges[HSC_GPU_RANGES];ArmSmmu4KConfig gpu_config;
 ArmSmmu4KResult gpu_built;ArmSmmu4KTableAuditResult gpu_audit;
 uint32_t gpu_build_status,gpu_audit_status;
 ArmSmmu4KLiveArena gpu_arena;ArmSmmu4KLiveState gpu_live;
};
_Static_assert(sizeof(struct FhgState)<=FHG_STORAGE_BYTES,"opaque full graph envelope");
_Static_assert(_Alignof(struct FhgState)==FHG_STORAGE_ALIGNMENT,"opaque alignment");
_Static_assert(HFM_MAX==11 && HSC_GPU_RANGES==12 && HSC_GPU_LEAF_PAGES==1567 && HSC_GPU_TABLE_PAGES==7,"fixed original graph");
_Static_assert(sizeof(FhgConfig)==16 && sizeof(FhgRequest)==40 && sizeof(FhgBacking)==48,"64bit POD ABI");
size_t fhg_actual_state_size(void){return sizeof(struct FhgState);}
size_t fhg_actual_state_alignment(void){return _Alignof(struct FhgState);}
static int interval(const void*p,size_t n){return p && n && (uintptr_t)p<=UINTPTR_MAX-(n-1);}
static int apart(const void*a,size_t n,const void*b,size_t m){return interval(a,n)&&interval(b,m)&&((uintptr_t)a>(uintptr_t)b+m-1 || (uintptr_t)b>(uintptr_t)a+n-1);}
static int envelope(const void*p,size_t n){if(n!=FHG_STORAGE_BYTES || !interval(p,n))return FHG_ARGUMENT;return (uintptr_t)p%FHG_STORAGE_ALIGNMENT?FHG_ALIGNMENT:FHG_OK;}
static int identity(const struct FhgState*s){return s->magic==FHG_MAGIC && s->inverse==~FHG_MAGIC && s->self==(uintptr_t)s && s->config.abi_version==FHG_ABI_VERSION;}
static int get(const void*p,size_t n,const struct FhgState**v){int e=envelope(p,n);if(e)return e;const struct FhgState*s=p;if(!identity(s))return FHG_UNINITIALIZED;if(s->busy)return FHG_BUSY;*v=s;return FHG_OK;}
static void failure(struct FhgState*s,unsigned code,unsigned slot,unsigned kind,int status){if(!s->failure.code)s->failure=(FhgFailure){code,s->stage,slot,kind,status,0};if(!s->code)s->code=code;}
static void trace(FhgTrace*t,unsigned slot,int status){++t->calls;t->attempted|=1u<<slot;if(status)t->failure|=1u<<slot;else t->success|=1u<<slot;}
static unsigned kind(unsigned slot){if(slot<5)return FHG_FW;const unsigned k[]={FHG_HFI,FHG_LOG,FHG_VRB,FHG_SHARED,FHG_GMU_ONLY,FHG_LARGE,FHG_GMU_TABLES,FHG_GPU_POOL,FHG_CODE,FHG_GPU_TABLES};return slot<FHG_ROWS?k[slot-5]:0;}
static void request(struct FhgState*s,unsigned slot,unsigned pages,unsigned nc){const uint64_t logical=(uint64_t)pages*4096,cpu=s->config.cpu_page_bytes;FhgRequest q={slot,kind(slot),pages,nc,logical,(logical+cpu-1)&~(cpu-1),(uint32_t)cpu,0};s->rows[slot].request=q;}
static int callback_intact(struct FhgState*s,const FhgRequest*q,const FhgRequest*expected){if(identity(s) && s->busy && s->phase==FHG_PREPARING && !memcmp(q,expected,sizeof(*q)))return 1;failure(s,FHG_CALLBACK_MUTATION,expected->slot,0,0);return 0;}
static int backing_valid(struct FhgState*s,unsigned slot){
 const FhgRow*r=&s->rows[slot];const FhgBacking*b=&r->backing;const FhgRequest*q=&r->request;
 const uint64_t bytes=q->owned_bytes,cpu=s->config.cpu_page_bytes,limit=UINT64_C(1)<<36;
 if(!q->gpu4k_pages || q->gpu4k_pages>(slot==FHG_GPU_TABLE_SLOT?FHG_LIVE_GPU_TABLE_PAGES:1024u) || !bytes || bytes>(slot==FHG_GPU_TABLE_SLOT?FHG_LIVE_GPU_TABLE_PAGES*4096u:4u*1024u*1024u) || q->cpu_page_bytes!=cpu ||
  b->cpu_page_bytes!=cpu || b->owned_span_count!=1 || b->owned_bytes!=bytes || b->owned_span_bytes!=bytes ||
  b->owned_physical!=b->physical || !b->physical || (b->physical&(cpu-1)) || b->physical>=limit || bytes>limit-b->physical ||
  !interval(b->cpu,(size_t)bytes) || ((uintptr_t)b->cpu&(cpu-1)) || !apart(s,FHG_STORAGE_BYTES,b->cpu,(size_t)bytes))return 0;
 if(s->output_guard && !apart(s->output_guard,sizeof(uint32_t),b->cpu,(size_t)bytes)){s->output_conflict=1;return 0;}
 for(unsigned i=0;i<3;i++)if(!apart(s->assets[i],s->asset_bytes[i],b->cpu,(size_t)bytes))return 0;
 for(unsigned i=0;i<slot;i++)if(s->allocated&(1u<<i)){
  const FhgRow*a=&s->rows[i];if(!apart(a->backing.cpu,(size_t)a->request.owned_bytes,b->cpu,(size_t)bytes) ||
   (b->physical<a->backing.physical+a->request.owned_bytes && a->backing.physical<b->physical+bytes))return 0;
 }
 return 1;
}
static int zeros(const volatile void*data,size_t bytes){const volatile uint8_t*p=data;for(size_t i=0;i<bytes;i++)if(p[i])return 0;return 1;}
static int tail(struct FhgState*s,unsigned slot){const FhgRow*r=&s->rows[slot];return zeros((const volatile uint8_t*)r->backing.cpu+r->request.logical_bytes,(size_t)(r->request.owned_bytes-r->request.logical_bytes));}
static int allocate_row(struct FhgState*s,unsigned slot){
 FhgRow*r=&s->rows[slot];FhgRequest q=r->request;FhgBacking b={0};int e=s->ops.allocate(s->ops.cookie,&q,&b);
 r->backing=b;trace(&s->traces[0],slot,e);if(!e){r->allocated=1;s->allocated|=1u<<slot;}else if(b.physical || b.cpu || b.owned_bytes || b.owned_physical || b.owned_span_bytes || b.cpu_page_bytes || b.owned_span_count){r->uncertain=1;s->uncertain|=1u<<slot;}
 if(!callback_intact(s,&q,&r->request))return 1;
 if(e)failure(s,FHG_ALLOCATION,slot,2,e);return e;
}
static int validate_row(struct FhgState*s,unsigned slot,unsigned clear_tail){
 FhgRow*r=&s->rows[slot];if(!backing_valid(s,slot)){failure(s,FHG_BACKING,slot,0,0);return 1;}
 FhgRequest q=r->request;FhgBacking b=r->backing;int e=s->ops.validate(s->ops.cookie,&q,&b);trace(&s->traces[1],slot,e);
 if(!callback_intact(s,&q,&r->request) || memcmp(&b,&r->backing,sizeof(b))){failure(s,FHG_CALLBACK_MUTATION,slot,3,e);return 1;}
 if(e){failure(s,FHG_VALIDATION,slot,3,e);return e;}
 r->validated=1;s->validated|=1u<<slot;
 if(clear_tail){volatile uint8_t*p=b.cpu;for(size_t i=(size_t)q.logical_bytes;i<(size_t)q.owned_bytes;i++)p[i]=0;}
 return 0;
}
static int publish_row(struct FhgState*s,unsigned slot){
 FhgRow*r=&s->rows[slot];if(!backing_valid(s,slot) || !tail(s,slot)){failure(s,FHG_CONTENTS,slot,0,0);return 1;}
 FhgRequest q=r->request;FhgBacking b=r->backing;int e=s->ops.publish(s->ops.cookie,&q,&b);trace(&s->traces[2],slot,e);
 if(!callback_intact(s,&q,&r->request) || memcmp(&b,&r->backing,sizeof(b))){failure(s,FHG_CALLBACK_MUTATION,slot,4,e);return 1;}
 if(e){failure(s,FHG_PUBLICATION,slot,4,e);return e;}
 r->published=1;s->published|=1u<<slot;return 0;
}
static int alloc_bridge(void*c,unsigned pages,uint64_t*pa,void**cpu){struct FhgState*s=c;unsigned slot=s->allocate_cursor++;if(slot>11)return 1;request(s,slot,pages,s->hfm.memory[slot].nc);int e=allocate_row(s,slot);*pa=s->rows[slot].backing.physical;*cpu=s->rows[slot].backing.cpu;return e;}
static int validate_bridge(void*c,uint64_t pa,void*cpu,unsigned pages,unsigned nc){struct FhgState*s=c;unsigned slot=s->allocate_cursor?s->allocate_cursor-1:FHG_ROWS;if(slot>11)return 1;const FhgRow*r=&s->rows[slot];if(pa!=r->backing.physical || cpu!=r->backing.cpu || pages!=r->request.gpu4k_pages || nc!=r->request.normal_nc)return 1;return validate_row(s,slot,1);}
static int publish_bridge(void*c,const struct hfm_allocation*a){struct FhgState*s=c;unsigned slot=0;while(slot<12 && a!=&s->hfm.memory[slot])++slot;if(slot==12)return 1;return publish_row(s,slot);}
/* Original hfm_prepare invokes hfm_release on error. No resource is freed in
 * this candidate: preserve its exact allocated/released/retained bits and the
 * original failure before HFM_RELEASE overwrites code. */
static int retain_bridge(void*c,uint64_t pa,void*cpu,unsigned pages){struct FhgState*s=c;if(!s->hfm_first_error)s->hfm_first_error=s->hfm.code;unsigned slot=FHG_ROWS;while(s->release_cursor){unsigned i=--s->release_cursor;if((s->hfm.allocated&(1u<<i)) && !(s->hfm.released&(1u<<i))){slot=i;break;}}if(slot>11)return 1;const struct hfm_allocation*a=&s->hfm.memory[slot];if(a->physical!=pa || a->cpu!=cpu || a->pages!=pages)return 1;trace(&s->traces[3],slot,1);return 1;}
static int gpu_descriptor(void*c,uint64_t pa,uint64_t*v){struct FhgState*s=c;const FhgBacking*b=&s->rows[FHG_GPU_TABLE_SLOT].backing;if((pa&7) || pa<b->physical || pa-b->physical>=HSC_GPU_TABLE_PAGES*4096)return 1;*v=((const volatile uint64_t*)b->cpu)[(pa-b->physical)/8];return 0;}
static unsigned audit_graphs(struct FhgState*s){
 unsigned char seen[HFM_TABLE_PAGES]={0};ArmSmmu4KTableIo io={&s->hfm,hfm_descriptor};ArmSmmu4KAuditRegion region={s->hfm.ranges,HFM_MAX,HFM_TABLE_PAGES,HFM_TABLE_PAGES*512,seen,sizeof(seen)};
 s->hfm.audit_status=ArmSmmu4KAuditTables(&io,&s->hfm.config,&s->hfm.built,&region,s->hfm.memory[11].physical,&s->hfm.audit);
 if(s->hfm.audit_status || !s->hfm.audit.complete || s->hfm.built.table_pages!=9 || s->hfm.built.leaf_pages!=1675 || s->hfm.audit.table_pages!=9 || s->hfm.audit.leaf_pages!=1675 || s->hfm.audit.descriptor_reads!=4608)return FHG_TABLES;
 ArmSmmu4KLiveResult live={0};
 s->gpu_audit_status=ArmSmmu4KLiveAudit(&s->gpu_arena,&s->gpu_live,s->gpu_ranges,HSC_GPU_RANGES,&live);
 s->gpu_audit=(ArmSmmu4KTableAuditResult){0};s->gpu_audit.complete=live.complete;
 s->gpu_audit.table_pages=s->gpu_live.tables.table_pages;s->gpu_audit.leaf_pages=s->gpu_live.tables.leaf_pages;s->gpu_audit.descriptor_reads=live.descriptor_reads;
 return s->gpu_audit_status || !live.complete || s->gpu_built.table_pages!=FHG_LIVE_GPU_USED_PAGES || s->gpu_built.leaf_pages!=1567 ||
  s->gpu_live.tables.table_pages!=FHG_LIVE_GPU_USED_PAGES || s->gpu_live.tables.leaf_pages!=1567 || live.descriptor_reads!=FHG_LIVE_GPU_USED_PAGES*512u?FHG_TABLES:FHG_MATCH;
}
static void queue_barrier(void*c){(void)c;}
static unsigned contents(struct FhgState*s){
 const struct hfm_result*r=&s->hfm;struct gg_fw_block blocks[GG_FW_MAX_BLOCKS];struct gg_reservation reservations[GG_FW_MAX_BLOCKS];AdrenoFirmwareBlock generic[GG_FW_MAX_BLOCKS];size_t nb=0,nr=0,ng=0;
 static const AdrenoFirmwareRegion regions[]={{0,0x4000,GG_ITCM},{0x4000,0xffc000,GG_ICACHE},{0x10004000,0x4000,GG_DTCM},{0x60000000,0x20000000,GG_KERNEL},{0xc0000000,0x20000000,GG_KERNEL_EXT}};
 const uint8_t*fw=s->assets[0];
 if(AdrenoFirmwarePlanGmu(fw,FHG_GMU_BYTES,regions,5,generic,GG_FW_MAX_BLOCKS,&ng) || gg_fw_plan(fw,FHG_GMU_BYTES,blocks,GG_FW_MAX_BLOCKS,&nb) || ng!=nb || gg_memory_plan(blocks,nb,reservations,GG_FW_MAX_BLOCKS,&nr) || nr!=5 || nb!=15)return FHG_FIRMWARE;
 for(size_t j=0;j<nb;j++)if(blocks[j].address!=generic[j].address || blocks[j].size!=generic[j].bytes || blocks[j].payload_offset!=generic[j].payload_offset)return FHG_FIRMWARE;
 for(unsigned i=0;i<5;i++){
  const volatile uint8_t*p=r->memory[i].cpu;if(r->memory[i].iova!=reservations[i].iova || (uint64_t)r->memory[i].pages*4096!=reservations[i].bytes)return FHG_FIRMWARE;
  for(size_t at=0;at<reservations[i].bytes;at++){uint64_t address=(uint64_t)r->memory[i].iova+at;uint8_t expected=0;for(size_t j=0;j<nb;j++)if(blocks[j].size && address>=blocks[j].address && address-blocks[j].address<blocks[j].size){expected=fw[blocks[j].payload_offset+(size_t)(address-blocks[j].address)];break;}if(p[at]!=expected)return FHG_CONTENTS;}
 }
 const struct ggh_io queues={r->memory[5].cpu,GGH_BYTES,GGH_IOVA,NULL,queue_barrier};
 for(unsigned q=0;q<GGH_QUEUES;q++){volatile uint32_t*head,*ring;unsigned rd=0,wr=0;if(ggh_queue(&queues,q,&head,&ring,&rd,&wr) || rd || wr)return FHG_CONTENTS;for(unsigned i=4;i<GGH_QUEUE_HEADER_WORDS;i++)if(head[i])return FHG_CONTENTS;if(!zeros(ring,GGH_RING_WORDS*4))return FHG_CONTENTS;}
 const volatile uint8_t*hfi=r->memory[5].cpu;size_t tablebytes=(GGH_HEADER_WORDS+GGH_QUEUES*GGH_QUEUE_HEADER_WORDS)*4,ringend=GGH_RING_OFFSET+GGH_QUEUES*GGH_RING_WORDS*4;
 if(!zeros(hfi+tablebytes,GGH_RING_OFFSET-tablebytes) || !zeros(hfi+ringend,GGH_BYTES-ringend))return FHG_CONTENTS;
 for(unsigned i=6;i<=10;i++)if(!zeros(s->rows[i].backing.cpu,(size_t)s->rows[i].request.logical_bytes))return FHG_CONTENTS;
 if(!zeros(s->rows[12].backing.cpu,HSC_POOL_BYTES))return FHG_CONTENTS;
 const volatile uint8_t*code=s->rows[13].backing.cpu;
 for(size_t i=0;i<HCC_BYTES;i++){uint8_t expected=0;if(i>=HCC_SQE_OFFSET && i<HCC_SQE_OFFSET+FHG_SQE_BYTES-4)expected=s->assets[1][i-HCC_SQE_OFFSET+4];else if(i>=HCC_AQE_OFFSET && i<HCC_AQE_OFFSET+FHG_AQE_BYTES-4)expected=s->assets[2][i-HCC_AQE_OFFSET+4];if(code[i]!=expected)return FHG_CONTENTS;}
 /* Unused table pool pages and every owned CPU tail remain zero/unmapped. */
 if(!zeros((const volatile uint8_t*)s->rows[14].backing.cpu+FHG_LIVE_GPU_USED_PAGES*4096u,4096))return FHG_CONTENTS;
 if(!zeros((const volatile uint8_t*)s->rows[11].backing.cpu+9*4096,4096))return FHG_CONTENTS;
 for(unsigned i=0;i<FHG_ROWS;i++){if(!backing_valid(s,i) || !tail(s,i))return FHG_CONTENTS;s->tail_verified|=1u<<i;}
 return audit_graphs(s);
}
static const uint8_t asset_hash[3][32]={
 {0x96,0x85,0xf6,0x2c,0x42,0x54,0x3b,0xef,0xca,0xee,0x68,0xd2,0x53,0x55,0xe1,0x6b,0xf5,0x5c,0xdb,0x0d,0x57,0x3b,0xdd,0xcc,0x9f,0x66,0x33,0xf5,0x9e,0xec,0x3f,0x72},
 {0x8e,0x2c,0x5c,0xe7,0x41,0x40,0xde,0x7e,0x16,0x1f,0x14,0x65,0x4b,0x44,0xde,0x99,0x98,0x55,0xbc,0xe0,0x1b,0x69,0x0f,0xdd,0x18,0xc3,0x35,0x8a,0x73,0x49,0x8c,0xd1},
 {0x25,0x23,0x7c,0x16,0x1d,0x72,0x7b,0x8e,0xa9,0x2b,0x4b,0xeb,0x83,0x7a,0x31,0x65,0x1b,0x44,0xea,0x78,0xbf,0xc3,0x4d,0x95,0x67,0x2a,0xf8,0xf3,0x46,0xa4,0x58,0x25}};
static unsigned identities(struct FhgState*s){for(unsigned i=0;i<3;i++){uint8_t hash[32]={0};int e=s->ops.sha256(s->ops.cookie,s->assets[i],s->asset_bytes[i],hash);if(!identity(s) || !s->busy || s->phase!=FHG_PREPARING){failure(s,FHG_CALLBACK_MUTATION,i,1,e);return FHG_CALLBACK_MUTATION;}if(e || memcmp(hash,asset_hash[i],32)){failure(s,FHG_IDENTITY,i,1,e);return FHG_IDENTITY;}}return FHG_MATCH;}
int fhg_init(void*p,size_t n,const FhgConfig*cfg,size_t cb,const FhgOps*ops,size_t ob){int e=envelope(p,n);if(e)return e;if((cfg && (uintptr_t)cfg%_Alignof(FhgConfig)) || (ops && (uintptr_t)ops%_Alignof(FhgOps)))return FHG_ALIGNMENT;if(cb!=sizeof(*cfg) || ob!=sizeof(*ops) || !apart(p,n,cfg,cb) || !apart(p,n,ops,ob) || cfg->abi_version!=FHG_ABI_VERSION || (cfg->cpu_page_bytes!=4096 && cfg->cpu_page_bytes!=16384) || cfg->reserved[0] || cfg->reserved[1] || ops->abi_version!=FHG_ABI_VERSION || ops->reserved || !ops->sha256 || !ops->allocate || !ops->validate || !ops->publish)return FHG_ARGUMENT;
 struct FhgState*s=p;const unsigned char*b=p;unsigned nz=0;for(size_t i=0;i<n;i++)nz|=b[i];if(nz){if(!identity(s))return FHG_UNINITIALIZED;return s->busy?FHG_BUSY:FHG_ALREADY_PREPARED;}FhgConfig c=*cfg;FhgOps o=*ops;memset(p,0,n);s->magic=FHG_MAGIC;s->inverse=~FHG_MAGIC;s->self=(uintptr_t)s;s->config=c;s->ops=o;s->phase=FHG_INITIALIZED;return FHG_OK;}
static int output(const struct FhgState*s,const void*p,size_t n,size_t alignment){if(p && (uintptr_t)p%alignment)return FHG_ALIGNMENT;if(!apart(s,FHG_STORAGE_BYTES,p,n))return FHG_ARGUMENT;for(unsigned i=0;i<3;i++)if(s->assets[i] && !apart(s->assets[i],s->asset_bytes[i],p,n))return FHG_ARGUMENT;for(unsigned i=0;i<FHG_ROWS;i++)if((s->allocated&(1u<<i)) && interval(s->rows[i].backing.cpu,(size_t)s->rows[i].request.owned_bytes) && !apart(s->rows[i].backing.cpu,(size_t)s->rows[i].request.owned_bytes,p,n))return FHG_ARGUMENT;return FHG_OK;}
int fhg_prepare(void*p,size_t n,const uint8_t*fw,size_t fb,const uint8_t*sqe,size_t sb,const uint8_t*aqe,size_t ab,uint32_t*out){const struct FhgState*read;int e=get(p,n,&read);if(e)return e;if(read->prepare_called)return FHG_ALREADY_PREPARED;if(out && (uintptr_t)out%_Alignof(uint32_t))return FHG_ALIGNMENT;if(fb!=FHG_GMU_BYTES || sb!=FHG_SQE_BYTES || ab!=FHG_AQE_BYTES || !apart(p,n,fw,fb) || !apart(p,n,sqe,sb) || !apart(p,n,aqe,ab) || !apart(p,n,out,sizeof(*out)) || !apart(fw,fb,sqe,sb) || !apart(fw,fb,aqe,ab) || !apart(sqe,sb,aqe,ab) || !apart(fw,fb,out,sizeof(*out)) || !apart(sqe,sb,out,sizeof(*out)) || !apart(aqe,ab,out,sizeof(*out)))return FHG_ARGUMENT;
 struct FhgState*s=p;s->assets[0]=fw;s->assets[1]=sqe;s->assets[2]=aqe;s->asset_bytes[0]=fb;s->asset_bytes[1]=sb;s->asset_bytes[2]=ab;s->output_guard=out;s->prepare_called=1;s->phase=FHG_PREPARING;s->busy=1;s->stage=1;
 unsigned code=identities(s);if(code)goto done;if(hcc_firmware(sqe,sb,0) || hcc_firmware(aqe,ab,1)){code=FHG_IDENTITY;goto done;}
 s->stage=2;s->release_cursor=12;const struct hfm_io io={s,alloc_bridge,validate_bridge,publish_bridge,retain_bridge};s->hfm_return=hfm_prepare(&io,fw,fb,&s->hfm);if(s->hfm_return){if(!s->hfm_first_error)s->hfm_first_error=s->hfm_return;code=s->code?s->code:FHG_FIRMWARE;goto done;}
 s->stage=3;request(s,12,HSC_POOL_PAGES,1);request(s,13,HCC_PAGES,1);request(s,14,FHG_LIVE_GPU_TABLE_PAGES,0);
 for(unsigned i=12;i<FHG_ROWS;i++){if(allocate_row(s,i) || validate_row(s,i,1)){code=s->code?s->code:FHG_ALLOCATION;goto done;}volatile uint8_t*q=s->rows[i].backing.cpu;for(size_t j=0;j<s->rows[i].request.owned_bytes;j++)q[j]=0;}
 s->stage=4;if(hcc_copy(s->rows[13].backing.cpu,HCC_BYTES,sqe,sb,aqe,ab)){code=FHG_CONTENTS;goto done;}
 if(hsc_pool_ranges(s->hfm.memory[8].physical,s->gpu_ranges) || hsc_large_range(s->hfm.memory[10].physical,&s->gpu_ranges[4]) || hsc_gpu_only_ranges(s->rows[12].backing.physical,&s->gpu_ranges[5]) || hcc_ranges(s->rows[13].backing.physical,&s->gpu_ranges[9])){code=FHG_TABLES;goto done;}
 s->gpu_config=(ArmSmmu4KConfig){32,36,s->rows[14].backing.physical,FHG_LIVE_GPU_APERTURE/4096};
 s->gpu_arena=(ArmSmmu4KLiveArena){s->gpu_config,0,FHG_LIVE_GPU_APERTURE,0,s->rows[14].backing.cpu,FHG_LIVE_GPU_TABLE_PAGES,0,0};
 s->gpu_build_status=ArmSmmu4KLivePrepare(&s->gpu_arena,&s->gpu_live);
 if(!s->gpu_build_status){ArmSmmu4KLiveResult seeded={0};s->gpu_build_status=ArmSmmu4KLiveSeedInactive(&s->gpu_arena,&s->gpu_live,s->gpu_ranges,HSC_GPU_RANGES,&seeded);}
 if(s->gpu_build_status){code=FHG_TABLES;goto done;}s->gpu_built=s->gpu_live.tables;
 s->stage=5;for(unsigned i=12;i<FHG_ROWS;i++)if(publish_row(s,i)){code=s->code?s->code:FHG_PUBLICATION;goto done;}
 s->stage=6;code=identities(s);if(!code)code=contents(s);if(!code && (s->allocated!=FHG_ALL || s->validated!=FHG_ALL || s->published!=FHG_ALL || s->uncertain || s->hfm.allocated!=HFM_ALL || s->hfm.validated!=HFM_ALL || s->hfm.retained!=HFM_ALL || s->hfm.released))code=FHG_FIRMWARE;
 done:if(code)failure(s,code,FHG_ROWS,0,0);s->code=code;s->ready=!code;s->phase=FHG_PREPARED;s->busy=0;s->output_guard=NULL;if(s->output_conflict)return FHG_ARGUMENT;*out=code;return FHG_OK;
}
int fhg_summary(const void*p,size_t n,FhgSummary*out,size_t b){const struct FhgState*s;int e=get(p,n,&s);if(e)return e;if(b!=sizeof(*out))return FHG_ARGUMENT;if((e=output(s,out,b,_Alignof(FhgSummary))))return e;FhgSummary v={0};v.abi_version=FHG_ABI_VERSION;v.phase=s->phase;v.prepare_called=s->prepare_called;v.ready=s->ready;v.code=s->code;v.stage=s->stage;v.allocated=s->allocated;v.validated=s->validated;v.published=s->published;v.uncertain_outputs=s->uncertain;v.tail_verified=s->tail_verified;v.retained=s->allocated;v.hfm_code=s->hfm.code;v.hfm_first_error=s->hfm_first_error;v.hfm_return_code=s->hfm_return;v.hfm_records=s->hfm.records;v.hfm_reservations=s->hfm.reservations;v.hfm_count=s->hfm.count;v.hfm_allocated=s->hfm.allocated;v.hfm_validated=s->hfm.validated;v.hfm_released=s->hfm.released;v.hfm_retained=s->hfm.retained;v.hfm_published=s->hfm.published;v.firmware_bytes_loaded=s->hfm.bytes_loaded;v.tcm_bytes_deferred=s->hfm.tcm_bytes_deferred;v.hfi_queues=s->hfm.hfi_queues;v.hfi_initialized=s->hfm.hfi_initialized;v.cpu_page_bytes=s->config.cpu_page_bytes;v.first_failure=s->failure;v.allocate_trace=s->traces[0];v.validate_trace=s->traces[1];v.publish_trace=s->traces[2];v.retention_trace=s->traces[3];*out=v;return FHG_OK;}
int fhg_row(const void*p,size_t n,uint32_t i,FhgRow*out,size_t b){const struct FhgState*s;int e=get(p,n,&s);if(e)return e;if(i>=FHG_ROWS || b!=sizeof(*out))return FHG_ARGUMENT;if((e=output(s,out,b,_Alignof(FhgRow))))return e;*out=s->rows[i];return FHG_OK;}
int fhg_graph(const void*p,size_t n,uint32_t g,FhgGraph*out,size_t b){const struct FhgState*s;int e=get(p,n,&s);if(e)return e;if(g>1 || b!=sizeof(*out))return FHG_ARGUMENT;if((e=output(s,out,b,_Alignof(FhgGraph))))return e;const ArmSmmu4KConfig*c=g?&s->gpu_config:&s->hfm.config;const ArmSmmu4KResult*r=g?&s->gpu_built:&s->hfm.built;const ArmSmmu4KTableAuditResult*a=g?&s->gpu_audit:&s->hfm.audit;FhgGraph v={g,c->input_bits,c->output_bits,g?12:11,c->table_physical,c->leaf_budget,r->root_physical,r->mair,r->table_pages,r->leaf_pages,a->descriptor_reads,r->start_level,r->upper_region,a->complete,g?s->gpu_build_status:s->hfm.build_status,g?s->gpu_audit_status:s->hfm.audit_status,0};*out=v;return FHG_OK;}
int fhg_range(const void*p,size_t n,uint32_t g,uint32_t i,FhgRange*out,size_t b){const struct FhgState*s;int e=get(p,n,&s);if(e)return e;if(g>1 || i>=(g?12u:11u) || b!=sizeof(*out))return FHG_ARGUMENT;if((e=output(s,out,b,_Alignof(FhgRange))))return e;const ArmSmmu4KRange*r=g?&s->gpu_ranges[i]:&s->hfm.ranges[i];const FhgRange v={r->iova,r->physical,r->bytes,r->permissions,r->writeback,r->shareability,0};*out=v;return FHG_OK;}
int fhg_verify(void*p,size_t n,uint32_t*out){const struct FhgState*read;int e=get(p,n,&read);if(e)return e;if((e=output(read,out,sizeof(*out),_Alignof(uint32_t))))return e;if(!read->ready || read->allocated!=FHG_ALL || read->uncertain)return FHG_NOT_READY;struct FhgState*s=p;s->busy=1;s->phase=FHG_PREPARING;s->stage=7;unsigned code=identities(s);for(unsigned i=0;!code && i<FHG_ROWS;i++)if(validate_row(s,i,0))code=s->code?s->code:FHG_VALIDATION;if(!code)code=identities(s);if(!code)code=contents(s);if(code){failure(s,code,FHG_ROWS,0,0);s->code=code;s->ready=0;}s->phase=FHG_PREPARED;s->busy=0;*out=code;return FHG_OK;}
