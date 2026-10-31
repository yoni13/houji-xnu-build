#include "HoujiGpuFactoryOpp.h"
/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Compile THIS TU, not an additional copy of the included FHG C file. */
#include "houji_full_host_runtime.h"
#include "houji_native_full_host_graph.c"
static int rt_receive_tap(const struct ggh_io *,unsigned,uint32_t *,unsigned,unsigned *);
#define ggh_receive rt_receive_tap
#include "GpuStockGmu.h"
#undef ggh_receive
#include "GpuHostProfile.h"
#include "GpuGxStart.h"
#include "GpuDesktopPerfProfile.h"
#include "GpuCodeBind.h"
#include "GpuGen7Config.h"
#define FHR_MAGIC UINT64_C(0x4648523152554e31)
struct FhrState {
 _Alignas(FHR_STORAGE_ALIGNMENT) uint64_t magic,inverse;uintptr_t self;
 FhrOps ops;uint32_t busy,run_called,sealed,lost,stage,completed,code,possible;
 uint32_t cp_attempted,host_attempted,host_qualified,pre_cp,rx_calls,rx_count,rx_empty[2],rx_overflow;
 struct FhgState *graph;uint64_t graph_hash;
 struct hgf_io io;struct hgf_result stock;
 struct hsc_result bank;struct hgx_result gx;struct hcb_result bind;struct h7c_result gen7;
 FhrFailure first;FhrRx rx[FHR_RX_RECORDS];
};
_Static_assert(sizeof(struct FhrState)<=FHR_STORAGE_BYTES,"runtime persistent envelope");
_Static_assert(_Alignof(struct FhrState)==FHR_STORAGE_ALIGNMENT,"runtime alignment");
_Static_assert(H7C_OPS==FHR_GEN7_OPS && H7C_PAIRS==FHR_GEN7_PAIRS,"fixed gen7 facts");
size_t fhr_actual_state_size(void){return sizeof(struct FhrState);}
static int rt_envelope(const void*p,size_t n){if(n!=FHR_STORAGE_BYTES || !interval(p,n))return FHR_ARGUMENT;return (uintptr_t)p%FHR_STORAGE_ALIGNMENT?FHR_ALIGNMENT:FHR_OK;}
static int rt_identity(const struct FhrState*s){return s->magic==FHR_MAGIC && s->inverse==~FHR_MAGIC && s->self==(uintptr_t)s && s->ops.abi_version==FHR_ABI_VERSION;}
static int rt_get(const void*p,size_t n,const struct FhrState**out){int e=rt_envelope(p,n);if(e)return e;const struct FhrState*s=p;if(!rt_identity(s))return FHR_UNINITIALIZED;*out=s;return FHR_OK;}
static uint64_t rt_hash_graph(const struct FhgState*g){const unsigned char*p=(const unsigned char*)g;uint64_t h=UINT64_C(1469598103934665603);for(size_t i=0;i<sizeof(*g);i++){if(i>=offsetof(struct FhgState,gpu_live) && i<offsetof(struct FhgState,gpu_live)+sizeof(g->gpu_live))continue;h^=p[i];h*=UINT64_C(1099511628211);}return h;}
static int rt_bound(const struct FhrState*s){return s->sealed && identity(s->graph) && s->graph->busy==1 && s->graph->phase==FHG_PREPARED && s->graph->ready==1 && s->graph->allocated==FHG_ALL && rt_hash_graph(s->graph)==s->graph_hash;}
static void rt_failed(struct FhrState*s,unsigned kind,uint32_t a,unsigned width,uint64_t v,int status){if(!s->first.attempted)s->first=(FhrFailure){1,s->stage,kind,a,width,status,v};}
static int rt_begin(struct FhrState*s){if(s->busy && rt_identity(s) && !s->lost && rt_bound(s))return 1;s->lost=1;if(!s->code)s->code=FHR_BINDING;rt_failed(s,13,0,0,0,FHR_GRAPH_NOT_READY);return 0;}
static int rt_end(struct FhrState*s,unsigned kind,uint32_t a,unsigned width,uint64_t v,int status,int success){if(status!=success)rt_failed(s,kind,a,width,v,status);if(!rt_identity(s) || !rt_bound(s)){s->lost=1;if(!s->code)s->code=FHR_BINDING;rt_failed(s,13,0,0,0,FHR_GRAPH_NOT_READY);return 0;}return 1;}
static int rt_rd32(void*c,uint32_t a,uint32_t*v){struct FhrState*s=c;if(!rt_begin(s))return 1;int e=s->ops.read32(s->ops.cookie,a,v);return rt_end(s,1,a,32,0,e,0)?e:1;}
static int rt_rd64(void*c,uint32_t a,uint64_t*v){struct FhrState*s=c;if(!rt_begin(s))return 1;int e=s->ops.read64(s->ops.cookie,a,v);return rt_end(s,2,a,64,0,e,0)?e:1;}
static int rt_wr32(void*c,uint32_t a,uint32_t v){struct FhrState*s=c;if(!rt_begin(s))return 1;if(a==0x3d7e000 && !v)s->possible=1;int e=s->ops.write32(s->ops.cookie,a,v);return rt_end(s,3,a,32,v,e,0)?e:1;}
static int rt_wr64(void*c,uint32_t a,uint64_t v){struct FhrState*s=c;if(!rt_begin(s))return 1;int e=s->ops.write64(s->ops.cookie,a,v);return rt_end(s,4,a,64,v,e,0)?e:1;}
static int rt_barrier(void*c){struct FhrState*s=c;if(!rt_begin(s))return 1;int e=s->ops.barrier(s->ops.cookie);return rt_end(s,5,0,0,0,e,0)?e:1;}
static int rt_delay(void*c,unsigned us){struct FhrState*s=c;if(!rt_begin(s))return 1;int e=s->ops.delay_us(s->ops.cookie,us);return rt_end(s,6,0,0,us,e,0)?e:1;}
static int rt_prerequisite(void*c){struct FhrState*s=c;if(!rt_begin(s))return 0;int e=s->ops.prerequisite(s->ops.cookie);return rt_end(s,7,0,0,0,e,1)?e:0;}
static int rt_publish(void*c){struct FhrState*s=c;if(!rt_begin(s) || s->possible)return 1;int e=s->ops.publish_graph(s->ops.cookie);return rt_end(s,8,0,0,0,e,0)?e:1;}
static int rt_checkpoint(void*c,unsigned stage){struct FhrState*s=c;if(!rt_begin(s) || s->possible || stage<1 || stage>2)return 1;int e=s->ops.checkpoint(s->ops.cookie,stage);return rt_end(s,9,0,0,stage,e,0)?e:1;}
static int rt_keep_live(void*c){struct FhrState*s=c;return !s->code && s->pre_cp && s->cp_attempted && s->ops.retain_live && rt_begin(s) && s->ops.retain_live(s->ops.cookie)==1 && rt_begin(s)?1:0;}
static int rt_stopped(void*c){struct FhrState*s=c;if(!rt_begin(s))return 1;int e=s->ops.stopped(s->ops.cookie);return rt_end(s,10,0,0,0,e,0)?e:1;}
static int rt_guard(void*c){struct FhrState*s=c;if(!rt_begin(s))return 1;int e=s->ops.stage_guard(s->ops.cookie,s->stage);return rt_end(s,11,0,0,s->stage,e,0)?e:1;}
static int rt_live(struct FhrState*s){return rt_begin(s) && s->stock.phase==10 && s->stock.fw_ready && s->stock.hfi_ready && s->stock.query_ack && s->stock.query_value==1 && !s->stock.query_status;}
static int rt_healthy(void*c){struct FhrState*s=c;return rt_live(s) && !rt_guard(s) && hgf_health(&s->io,&s->stock);}
static int rt_send(void*c,const uint32_t*w,unsigned n){struct FhrState*s=c;if(!rt_live(s))return GGH_INVALID;struct hgf_queue_cookie cookie={&s->io,0};const struct ggh_io q={s->graph->hfm.memory[5].cpu,GGH_BYTES,GGH_IOVA,&cookie,hgf_hfi_barrier};int e=ggh_send(&q,w,n);return cookie.error?GGH_INVALID:e;}
static int rt_receive(void*c,unsigned queue,uint32_t*w,unsigned*n){struct FhrState*s=c;if(!rt_live(s))return GGH_INVALID;struct hgf_queue_cookie cookie={&s->io,0};const struct ggh_io q={s->graph->hfm.memory[5].cpu,GGH_BYTES,GGH_IOVA,&cookie,hgf_hfi_barrier};int e=rt_receive_tap(&q,queue,w,HHB_WORDS,n);return cookie.error?GGH_INVALID:e;}
static int rt_doorbell(void*c){struct FhrState*s=c;return rt_live(s) && hgf_write(&s->io,&s->stock,0x3d7e650,1,0)?0:1;}
static int rt_receive_tap(const struct ggh_io*io,unsigned queue,uint32_t*w,unsigned cap,unsigned*n){const struct hgf_queue_cookie*q=io->cookie;struct FhrState*s=q->io->cookie;if(!rt_begin(s)){if(n)*n=0;return GGH_INVALID;}int e=ggh_receive(io,queue,w,cap,n);++s->rx_calls;if(e==GGH_EMPTY){if(queue==1 || queue==2)++s->rx_empty[queue-1];return e;}if(s->rx_count>=FHR_RX_RECORDS){++s->rx_overflow;return e;}FhrRx*r=&s->rx[s->rx_count++];*r=(FhrRx){0};r->sequence=s->rx_calls;r->stage=s->stage;r->queue=queue;r->status=(uint32_t)e;r->word_count=n?*n:0;if(!e && w && n && *n<=cap && *n<=FHR_MESSAGE_WORDS)for(unsigned i=0;i<*n;i++)r->words[i]=w[i];return e;}
static int rt_record(void*c,const uint32_t*words,unsigned count){struct FhrState*s=c;if(!words || count!=H7C_WORDS || s->gen7.power_pairs!=H7C_PAIRS || s->gen7.completed!=H7C_OPS || rt_guard(s))return 1;
 if(words[0] || words[1] || words[2] || words[3]!=(H7C_IFPC|(H7C_PREEMPT<<8)))return 1;
 for(unsigned i=0;i<H7C_PAIRS;i++)if(words[4+i*2]!=(h7c_power_regs[i].address-0x3d00000)/4 || words[5+i*2]!=s->gen7.power_values[i])return 1;
 volatile uint32_t*to=(volatile uint32_t*)((uint8_t*)s->graph->rows[FHG_CODE_SLOT].backing.cpu+HCC_RECORD_OFFSET);
 for(unsigned i=0;i<1024;i++)if(to[i])return 1;
 for(unsigned i=0;i<count;i++)to[i]=words[i];
 for(unsigned i=0;i<1024;i++)if(to[i]!=(i<count?words[i]:0))return 1;
 if(!rt_begin(s))return 1;int e=s->ops.publish_record(s->ops.cookie);if(!rt_end(s,12,0,0,count,e,0) || e)return 1;
 s->gen7.record_crc=hcc_crc32((const uint8_t*)to,4096);return rt_guard(s);
}
static unsigned rt_bootstrap(void*c,struct hhb_result*r){struct FhrState*s=c;
 if(r!=&s->stock.bootstrap || !rt_live(s)){s->code=FHR_BINDING;return HHB_IO;}
 s->completed=FHR_STOCK;s->stage=FHR_HOST;s->host_attempted=1;
 if(rt_guard(s)){s->code=FHR_HOST_FAILED;return HHB_IO;}
 const struct hhb_io hfi={s,rt_healthy,rt_send,rt_receive,rt_doorbell,rt_delay,NULL};unsigned code=hup_run(&hfi,r);
 s->host_qualified=!code && r->completed==5 && !r->resource_count && !r->resource_acks && r->record[4].ack && r->record[4].value==1 && r->record[4].status_present && !r->record[4].status;
 if(!s->host_qualified){s->code=FHR_HOST_FAILED;return code?code:HHB_REJECTED;}s->completed=FHR_HOST;
 s->stage=FHR_BANK0;if(rt_guard(s)){s->code=FHR_BANK0_FAILED;return HHB_IO;}
 const struct hgp_io bank={s,rt_rd32,rt_rd64,rt_wr32,rt_wr64,rt_delay,rt_guard,NULL};
 code=hsc_run_lpac(&bank,&s->graph->gpu_config,&s->graph->gpu_built,&s->bank);
 if(code || !s->bank.enabled || !s->bank.attached || !s->bank.lpac || !s->bank.sid0_attached || !s->bank.lpac_attached){s->code=FHR_BANK0_FAILED;return HHB_IO;}s->completed=FHR_BANK0;
 s->stage=FHR_GX;if(rt_guard(s)){s->code=FHR_GX_FAILED;return HHB_IO;}const struct hgx_io gx={hfi,rt_rd32,rt_wr32,rt_guard};code=hup_gx_run(&gx,&r->next_sequence,&s->gx);
 if(code || !s->gx.gx_ready || !s->gx.acked || !s->gx.clear_confirmed){s->code=FHR_GX_FAILED;return HHB_IO;}s->completed=FHR_GX;
 s->stage=FHR_CODE;if(rt_guard(s)){s->code=FHR_CODE_FAILED;return HHB_IO;}const struct hcb_io bind={s,rt_rd32,rt_wr32,rt_guard};code=hcb_run(&bind,&s->bind);
 if(code || s->bind.verified!=15){s->code=FHR_CODE_FAILED;return HHB_IO;}s->completed=FHR_CODE;
 s->stage=FHR_GEN7;if(rt_guard(s)){s->code=FHR_GEN7_FAILED;return HHB_IO;}const struct h7c_io gen7={s,rt_guard,rt_rd32,rt_wr32,rt_delay,rt_record};code=h7c_run(&gen7,&s->gen7);
 if(code || !s->gen7.record_ready || s->gen7.completed!=H7C_OPS || s->gen7.power_pairs!=H7C_PAIRS){s->code=FHR_GEN7_FAILED;return HHB_IO;}s->completed=FHR_GEN7;
 s->stage=FHR_PRE_CP_BOUNDARY;if(rt_guard(s)){s->code=FHR_GEN7_FAILED;return HHB_IO;}s->pre_cp=1;s->completed=FHR_PRE_CP_BOUNDARY;
 if(s->ops.after_prepare){s->cp_attempted=1;if(!rt_begin(s) || s->ops.after_prepare(s->ops.cookie) || !rt_begin(s)){s->code=FHR_RECORD_FAILED;return HHB_IO;}}
 return HHB_OK;
}
int fhr_init(void*p,size_t n,const FhrOps*ops,size_t bytes){int e=rt_envelope(p,n);if(e)return e;if(ops && (uintptr_t)ops%_Alignof(FhrOps))return FHR_ALIGNMENT;if(bytes!=sizeof(*ops) || !apart(p,n,ops,bytes) || ops->abi_version!=FHR_ABI_VERSION || ops->reserved || !ops->sha256 || !ops->prepare_claim || !ops->read32 || !ops->read64 || !ops->write32 || !ops->write64 || !ops->barrier || !ops->delay_us || !ops->prerequisite || !ops->publish_graph || !ops->checkpoint || !ops->stopped || !ops->stage_guard || !ops->publish_record)return FHR_ARGUMENT;struct FhrState*s=p;const unsigned char*b=p;unsigned nz=0;for(size_t i=0;i<n;i++)nz|=b[i];if(nz){if(!rt_identity(s))return FHR_UNINITIALIZED;return s->busy?FHR_BUSY:FHR_ALREADY_RUN;}const FhrOps copy=*ops;memset(p,0,n);s->magic=FHR_MAGIC;s->inverse=~FHR_MAGIC;s->self=(uintptr_t)s;s->ops=copy;return FHR_OK;}
int fhr_run(void*p,size_t n,FhgStorage*memory,size_t bytes,uint64_t zero,uint32_t*out){const struct FhrState*read;int e=rt_get(p,n,&read);if(e)return e;if(read->busy)return FHR_BUSY;if(read->run_called)return FHR_ALREADY_RUN;if((memory && (uintptr_t)memory%FHG_STORAGE_ALIGNMENT) || (out && (uintptr_t)out%_Alignof(uint32_t)))return FHR_ALIGNMENT;
 if(bytes!=FHG_STORAGE_BYTES || !zero || (zero&4095) || zero>=(UINT64_C(1)<<36) || !apart(p,n,memory,bytes) || !apart(p,n,out,sizeof(*out)) || !apart(memory,bytes,out,sizeof(*out)))return FHR_ARGUMENT;
 const struct FhgState*gread;int fe=get(memory,bytes,&gread);if(fe || !gread->ready || gread->phase!=FHG_PREPARED || gread->allocated!=FHG_ALL || gread->validated!=FHG_ALL || gread->published!=FHG_ALL || gread->uncertain || gread->hfm.count!=11 || gread->hfm.allocated!=HFM_ALL || gread->hfm.retained!=HFM_ALL || gread->hfm.released || gread->hfm.code)return FHR_GRAPH_NOT_READY;
 for(unsigned i=0;i<15;i++)if(!apart(p,n,gread->rows[i].backing.cpu,(size_t)gread->rows[i].request.owned_bytes) || !apart(out,sizeof(*out),gread->rows[i].backing.cpu,(size_t)gread->rows[i].request.owned_bytes))return FHR_ARGUMENT;
 for(unsigned i=0;i<3;i++)if(!apart(p,n,gread->assets[i],gread->asset_bytes[i]) || !apart(out,sizeof(*out),gread->assets[i],gread->asset_bytes[i]))return FHR_ARGUMENT;
 struct FhrState*s=p;struct FhgState*g=(struct FhgState*)memory;s->run_called=1;s->busy=1;s->stage=FHR_PREFLIGHT;
 e=s->ops.prepare_claim(s->ops.cookie);if(e){rt_failed(s,15,0,0,0,e);s->code=FHR_CLAIM;s->busy=0;return FHR_GRAPH_NOT_READY;}
 if(!identity(g) || g->busy || !g->ready){s->code=FHR_BINDING;s->busy=0;return FHR_GRAPH_NOT_READY;}
 for(unsigned i=0;i<3;i++){uint8_t hash[32]={0};e=s->ops.sha256(s->ops.cookie,g->assets[i],g->asset_bytes[i],hash);if(e || memcmp(hash,asset_hash[i],32)){rt_failed(s,14,0,0,i,e);s->code=FHR_IDENTITY;s->busy=0;return FHR_GRAPH_NOT_READY;}}
 if(contents(g)){s->code=FHR_GRAPH;s->busy=0;return FHR_GRAPH_NOT_READY;}
 s->graph=g;s->sealed=1;g->busy=1;s->graph_hash=rt_hash_graph(g);s->stage=FHR_STOCK;
 s->io=(struct hgf_io){{s,rt_rd32,rt_rd64,rt_wr32,rt_wr64,rt_delay,NULL,NULL},s,rt_rd32,rt_wr32,rt_barrier,rt_delay,rt_prerequisite,rt_publish,rt_checkpoint,rt_bootstrap,rt_stopped,rt_keep_live};
 const unsigned code=hgf_run(&s->io,&g->hfm,g->assets[0],g->asset_bytes[0],zero,&s->stock);s->stage=s->stock.retained_live?FHR_RETAINED_LIVE:FHR_RETURNED;s->busy=s->stock.retained_live?1:0;*out=code;return FHR_OK;
}
static int rt_output(const struct FhrState*s,const void*p,size_t n,size_t a){if(p && (uintptr_t)p%a)return FHR_ALIGNMENT;if(!apart(s,FHR_STORAGE_BYTES,p,n))return FHR_ARGUMENT;if(s->graph && output(s->graph,p,n,a))return FHR_ARGUMENT;return FHR_OK;}
int fhr_gpu_live_binding(void*p,size_t n,FhrGpuLiveBinding*out,size_t bytes){
 const struct FhrState*read;int e=rt_get(p,n,&read);if(e)return e;
 if(bytes!=sizeof(*out) || (e=rt_output(read,out,bytes,_Alignof(FhrGpuLiveBinding))))return e?e:FHR_ARGUMENT;
 struct FhrState*s=p;if(!rt_begin(s) || s->stage!=FHR_PRE_CP_BOUNDARY || !s->pre_cp || !s->graph->gpu_live.prepared || s->graph->gpu_live.poisoned)return FHR_NOT_AVAILABLE;
 *out=(FhrGpuLiveBinding){&s->graph->gpu_arena,&s->graph->gpu_live,s->graph->gpu_ranges,HSC_GPU_RANGES};return FHR_OK;
}
int fhr_summary(const void*p,size_t n,FhrSummary*out,size_t bytes){const struct FhrState*s;int e=rt_get(p,n,&s);if(e)return e;if(bytes!=sizeof(*out))return FHR_ARGUMENT;if((e=rt_output(s,out,bytes,_Alignof(FhrSummary))))return e;FhrSummary v={0};v.abi_version=FHR_ABI_VERSION;v.busy=s->busy;v.run_called=s->run_called;v.graph_sealed=s->sealed;v.stage=s->stage;v.completed_stage=s->completed;v.code=s->code;v.possible_execution=s->possible;v.retained_live=s->stock.retained_live;
 const struct hgf_result*r=&s->stock;v.stock_code=r->code;v.stock_phase=r->phase;
#define C(f) v.f=r->f
 C(reset_held);C(release_attempted);C(fw_ready);C(hfi_ready);C(query_sent);C(query_ack);C(query_value);C(query_status);C(tcm_backup_words);C(tcm_loaded_words);C(rsc_attempted);C(boot_attempted);C(fsr);C(fsynr);C(gfsr);C(irq);C(ao_irq);C(diagnostic_mask);C(far);C(failed_phase);C(failed_address);C(failed_value);C(failed_operation);C(failed_observed_valid);
#undef C
 v.host_attempted=s->host_attempted;v.host_qualified=s->host_qualified;v.host_code=r->bootstrap.code;v.host_stage=r->bootstrap.stage;v.host_completed=r->bootstrap.completed;v.resource_count=r->bootstrap.resource_count;v.resource_acks=r->bootstrap.resource_acks;v.next_sequence=r->bootstrap.next_sequence;
 v.bank0_code=s->bank.code;v.bank0_phase=s->bank.phase;v.bank0_attempted=s->bank.attempted;v.bank0_enabled=s->bank.enabled;v.bank0_attached=s->bank.attached;v.bank0_lpac=s->bank.lpac;v.sid0_attached=s->bank.sid0_attached;v.sid1_attached=s->bank.lpac_attached;v.sid0_smr=s->bank.sid0_smr;v.sid0_s2cr=s->bank.sid0_s2cr;v.sid1_smr=s->bank.lpac_smr;v.sid1_s2cr=s->bank.lpac_s2cr;
 v.gx_code=s->gx.code;v.gx_phase=s->gx.phase;v.gx_requested=s->gx.requested;v.gx_acked=s->gx.acked;v.gx_clear_attempted=s->gx.clear_attempted;v.gx_clear_confirmed=s->gx.clear_confirmed;v.gx_ready=s->gx.gx_ready;v.gx_valid=s->gx.valid;v.gx_before=s->gx.gx_before;v.gx_after=s->gx.gx_after;v.gx_irq_before=s->gx.irq_before;v.gx_irq_ack=s->gx.irq_ack;v.gx_irq_after=s->gx.irq_after;v.gx_fence=s->gx.fence;v.gx_busy=s->gx.busy;v.gx_rbbm=s->gx.rbbm;v.gx_rbbm3=s->gx.rbbm3;v.gx_sqe=s->gx.sqe;
 v.code_bind_code=s->bind.code;v.code_bind_phase=s->bind.phase;v.code_bind_attempted=s->bind.attempted;v.code_bind_verified=s->bind.verified;
 v.gen7_code=s->gen7.code;v.gen7_phase=s->gen7.phase;v.gen7_step=s->gen7.step;v.gen7_completed=s->gen7.completed;v.gen7_record_ready=s->gen7.record_ready;v.gen7_pairs=s->gen7.power_pairs;v.gen7_record_crc=s->gen7.record_crc;
 v.cp_attempted=s->cp_attempted;v.pre_cp_boundary=s->pre_cp;v.rx_calls=s->rx_calls;v.rx_records=s->rx_count;v.rx_empty[0]=s->rx_empty[0];v.rx_empty[1]=s->rx_empty[1];v.rx_overflow=s->rx_overflow;v.first_callback=s->first;*out=v;return FHR_OK;
}
int fhr_tcm_backup(const void*p,size_t n,uint32_t i,FhrTcmWord*out,size_t bytes){const struct FhrState*s;int e=rt_get(p,n,&s);if(e)return e;if(i>=8192 || bytes!=sizeof(*out))return FHR_ARGUMENT;if((e=rt_output(s,out,bytes,_Alignof(FhrTcmWord))))return e;if(i>=s->stock.tcm_backup_words)return FHR_NOT_AVAILABLE;const FhrTcmWord v={i,0x3d6d000+4*i,s->stock.tcm_before[i],1};*out=v;return FHR_OK;}
int fhr_rx(const void*p,size_t n,uint32_t i,FhrRx*out,size_t bytes){const struct FhrState*s;int e=rt_get(p,n,&s);if(e)return e;if(i>=FHR_RX_RECORDS || bytes!=sizeof(*out))return FHR_ARGUMENT;if((e=rt_output(s,out,bytes,_Alignof(FhrRx))))return e;if(i>=s->rx_count)return FHR_NOT_AVAILABLE;*out=s->rx[i];return FHR_OK;}
int fhr_host_step(const void*p,size_t n,uint32_t i,FhrHostStep*out,size_t bytes){const struct FhrState*s;int e=rt_get(p,n,&s);if(e)return e;if(i>=5 || bytes!=sizeof(*out))return FHR_ARGUMENT;if((e=rt_output(s,out,bytes,_Alignof(FhrHostStep))))return e;const struct hhb_record*r=&s->stock.bootstrap.record[i];if(!r->attempted)return FHR_NOT_AVAILABLE;FhrHostStep v={0};v.attempted=r->attempted;v.sent=r->sent;v.ack=r->ack;v.word_count=r->count;v.polls=r->polls;v.status_present=r->status_present;v.legacy_contract=r->legacy_contract;v.header=r->header;v.value=r->value;v.status=r->status;for(unsigned j=0;j<FHR_MESSAGE_WORDS;j++)v.reply[j]=r->reply[j];*out=v;return FHR_OK;}
int fhr_gen7_op(uint32_t i,FhrGen7Op*out,size_t bytes){if(i>=H7C_OPS || bytes!=sizeof(*out) || !interval(out,bytes))return FHR_ARGUMENT;if((uintptr_t)out%_Alignof(FhrGen7Op))return FHR_ALIGNMENT;const struct h7c_op*p=&h7c_ops[i];*out=(FhrGen7Op){i,p->address,p->value,p->mask,p->verify,p->kind};return FHR_OK;}
int fhr_gen7_power_address(uint32_t i,uint32_t*out){if(i>=H7C_PAIRS || !interval(out,sizeof(*out)))return FHR_ARGUMENT;if((uintptr_t)out%_Alignof(uint32_t))return FHR_ALIGNMENT;*out=h7c_power_regs[i].address;return FHR_OK;}
int fhr_gen7_record(const void*p,size_t n,uint32_t i,FhrGen7Record*out,size_t bytes){const struct FhrState*s;int e=rt_get(p,n,&s);if(e)return e;if(i>=H7C_OPS || bytes!=sizeof(*out))return FHR_ARGUMENT;if((e=rt_output(s,out,bytes,_Alignof(FhrGen7Record))))return e;FhrGen7Record v={0};fhr_gen7_op(i,&v.op,sizeof(v.op));const struct h7c_record*r=&s->gen7.records[i];v.before=r->before;v.expected=r->expected;v.after=r->after;v.valid=r->valid;v.written=r->written;v.polls=r->polls;*out=v;return FHR_OK;}


/* Published live owner supplies a NEW transport lease. No retired cold ops. */
struct RuntimeQueue {struct FhrState*s;const FhrRuntimeIo*io;int error;};
static int rt_dvfs_health(void*c){struct RuntimeQueue*q=c;struct FhrState*s=q->s;return !q->error && rt_begin(s) && s->stage==FHR_RETAINED_LIVE && s->stock.retained_live && s->cp_attempted && s->host_qualified && q->io->live(q->io->cookie);}
static void rt_dvfs_barrier(void*c){struct RuntimeQueue*q=c;if(!rt_dvfs_health(c) || q->io->barrier(q->io->cookie))q->error=1;}
static int rt_dvfs_send(void*c,const uint32_t*w,unsigned n){struct RuntimeQueue*q=c;if(!rt_dvfs_health(c))return GGH_INVALID;const struct ggh_io io={q->s->graph->hfm.memory[5].cpu,GGH_BYTES,GGH_IOVA,q,rt_dvfs_barrier};int e=ggh_send(&io,w,n);return q->error?GGH_INVALID:e;}
static int rt_dvfs_receive(void*c,unsigned queue,uint32_t*w,unsigned*n){struct RuntimeQueue*q=c;if(!rt_dvfs_health(c))return GGH_INVALID;const struct ggh_io io={q->s->graph->hfm.memory[5].cpu,GGH_BYTES,GGH_IOVA,q,rt_dvfs_barrier};int e=ggh_receive(&io,queue,w,HHB_WORDS,n);return q->error?GGH_INVALID:e;}
static int rt_dvfs_doorbell(void*c){struct RuntimeQueue*q=c;return !rt_dvfs_health(c) || q->io->doorbell(q->io->cookie);}
static int rt_dvfs_delay(void*c,unsigned us){struct RuntimeQueue*q=c;return !rt_dvfs_health(c) || q->io->delay(q->io->cookie,us);}
int fhr_runtime_vote(void*p,size_t n,const FhrRuntimeIo*transport,uint32_t level,uint32_t out[6]){
 const struct FhrState*read;int e=rt_get(p,n,&read);if(e)return e;
 if(!out || !transport || !transport->live || !transport->doorbell || !transport->barrier || !transport->delay || !hco_opp(level))return FHR_ARGUMENT;
 for(unsigned i=0;i<6;i++)out[i]=0;
 struct FhrState*s=p;struct RuntimeQueue queue={s,transport,0};if(!rt_dvfs_health(&queue))return FHR_NOT_AVAILABLE;
 static struct hhb_result result;memset(&result,0,sizeof(result));
 result.next_sequence=s->stock.bootstrap.next_sequence;
 if(!result.next_sequence || result.next_sequence>=4095)result.next_sequence=1;
 const struct hhb_io io={&queue,rt_dvfs_health,rt_dvfs_send,rt_dvfs_receive,rt_dvfs_doorbell,rt_dvfs_delay,NULL};
 const uint32_t payload[]={1,level,hco_opp(level)->bw_index};
 out[0]=hhb_exchange(&io,&result,0,30,payload,3,0,1);out[1]=result.record[0].sent;out[2]=result.record[0].ack;out[3]=result.record[0].value;out[4]=result.record[0].status;out[5]=result.record[0].polls;
 s->stock.bootstrap.next_sequence=result.next_sequence;
 if(out[0] || !result.record[0].ack || !rt_dvfs_health(&queue))return FHR_NOT_AVAILABLE;
 return FHR_OK;
}
