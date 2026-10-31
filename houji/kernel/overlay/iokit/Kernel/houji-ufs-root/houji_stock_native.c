/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "houji_stock_native.h"
#include "houji_hfi_private.h"
#include <string.h>
/* The actual queue definition is included before the call-binding tap. */
static int hsn_receive_tap(const struct ggh_io *,unsigned,uint32_t *,unsigned,unsigned *);
#define ggh_receive hsn_receive_tap
#include "GpuStockGmu.h"
#undef ggh_receive
#include "GpuHostProfile.h"
#define HSN_MAGIC UINT64_C(0x48534e53544f434b)
struct HsnState {
 _Alignas(HSN_STORAGE_ALIGNMENT) uint64_t magic;
 uint64_t magic_inverse;uintptr_t self;
 uint32_t abi_version,busy,phase,run_called,hhi_sealed,callback_sequence,binding_lost;
 uint32_t receive_calls,receive_records,receive_empty[2],receive_overflow;
 uint32_t host_requested,host_attempted,host_return_code,host_qualified;
 struct HhiState *hhi;uint64_t hhi_fingerprint;const uint8_t *firmware;size_t firmware_bytes;
 HsnCallbacks callbacks;HsnCallbackFailure first_callback;
 struct hgf_io io;struct hgf_result result;
 HsnReceive receive[HSN_RECEIVE_RECORDS];
};
_Static_assert(sizeof(struct HsnState)<=HSN_STORAGE_BYTES,"HSN opaque envelope");
_Static_assert(_Alignof(struct HsnState)==HSN_STORAGE_ALIGNMENT,"HSN alignment");
_Static_assert(sizeof(unsigned)==sizeof(uint32_t) && sizeof(int)==sizeof(int32_t),"callback ABI");
_Static_assert(HSN_RECEIVE_RECORDS==80 && HSN_MESSAGE_WORDS==255,"bounded stock transcript");
size_t hsn_actual_state_size(void){return sizeof(struct HsnState);}
size_t hsn_actual_state_alignment(void){return _Alignof(struct HsnState);}
static int interval(const void*p,size_t n){return p && n && (uintptr_t)p<=UINTPTR_MAX-(n-1);}
static int apart(const void*a,size_t n,const void*b,size_t m){return interval(a,n)&&interval(b,m)&&((uintptr_t)a>(uintptr_t)b+m-1 || (uintptr_t)b>(uintptr_t)a+n-1);}
static int envelope(const void*p,size_t n){if(n!=HSN_STORAGE_BYTES || !interval(p,n))return HSN_ARGUMENT;return (uintptr_t)p%HSN_STORAGE_ALIGNMENT?HSN_ALIGNMENT:HSN_OK;}
static int identity(const struct HsnState*s){return s->magic==HSN_MAGIC && s->magic_inverse==~HSN_MAGIC && s->self==(uintptr_t)s && s->abi_version==HSN_ABI_VERSION;}
static int get(const void*p,size_t n,const struct HsnState**out){int e=envelope(p,n);if(e)return e;const struct HsnState*s=p;if(!identity(s))return HSN_UNINITIALIZED;*out=s;return HSN_OK;}
static int hhi_identity(const struct HhiState*h){return h && h->magic==HHI_MAGIC && h->magic_inverse==~HHI_MAGIC && h->self==(uintptr_t)h && h->abi_version==HHI_ABI_VERSION;}
static uint64_t fingerprint(const struct HhiState*h){const unsigned char*p=(const unsigned char*)h;uint64_t v=UINT64_C(1469598103934665603);for(size_t i=0;i<sizeof(*h);i++){v^=p[i];v*=UINT64_C(1099511628211);}return v;}
static int bound(const struct HsnState*s){return s->hhi_sealed && hhi_identity(s->hhi) && s->hhi->busy==1 && s->hhi->phase==HHI_PREPARED && s->hhi->prepare_called==1 && !s->hhi->release_calls && fingerprint(s->hhi)==s->hhi_fingerprint;}
/* Repair only the bridge-owned busy seal after a forbidden callback mutation;
 * never repair or normalize allocation/result fields into a valid graph. */
static int seal_intact(struct HsnState*s){int ok=bound(s);if(!ok && s->hhi_sealed && hhi_identity(s->hhi))s->hhi->busy=1;return ok;}
static void failed(struct HsnState*s,unsigned kind,uint32_t a,unsigned width,uint64_t v,int status){
 if(s->first_callback.attempted)return;
 s->first_callback=(HsnCallbackFailure){1,s->callback_sequence,kind,s->result.phase,a,width,v,(int32_t)status,0};
}
static int begin(struct HsnState*s){
 ++s->callback_sequence;
 if(!s->busy || !identity(s) || s->binding_lost || !seal_intact(s)){s->binding_lost=1;failed(s,HSN_CB_BINDING,0,0,0,HSN_HHI_NOT_READY);return 0;}return 1;
}
static int end(struct HsnState*s,unsigned kind,uint32_t a,unsigned width,uint64_t v,int status,int success){
 if(status!=success)failed(s,kind,a,width,v,status);
 if(!identity(s) || !seal_intact(s)){s->binding_lost=1;failed(s,HSN_CB_BINDING,0,0,0,HSN_HHI_NOT_READY);return 0;}return 1;
}
static int rd32(void*c,uint32_t a,uint32_t*v){struct HsnState*s=c;if(!begin(s))return 1;int e=s->callbacks.read32(s->callbacks.cookie,a,v);return end(s,HSN_CB_READ32,a,32,0,e,0)?e:1;}
static int rd64(void*c,uint32_t a,uint64_t*v){struct HsnState*s=c;if(!begin(s))return 1;int e=s->callbacks.read64(s->callbacks.cookie,a,v);return end(s,HSN_CB_READ64,a,64,0,e,0)?e:1;}
static int wr32(void*c,uint32_t a,uint32_t v){struct HsnState*s=c;if(!begin(s))return 1;int e=s->callbacks.write32(s->callbacks.cookie,a,v);return end(s,HSN_CB_WRITE32,a,32,v,e,0)?e:1;}
static int wr64(void*c,uint32_t a,uint64_t v){struct HsnState*s=c;if(!begin(s))return 1;int e=s->callbacks.write64(s->callbacks.cookie,a,v);return end(s,HSN_CB_WRITE64,a,64,v,e,0)?e:1;}
static int barrier(void*c){struct HsnState*s=c;if(!begin(s))return 1;int e=s->callbacks.barrier(s->callbacks.cookie);return end(s,HSN_CB_BARRIER,0,0,0,e,0)?e:1;}
static int delay(void*c,unsigned us){struct HsnState*s=c;if(!begin(s))return 1;int e=s->callbacks.delay_us(s->callbacks.cookie,us);return end(s,HSN_CB_DELAY,0,0,us,e,0)?e:1;}
static int prerequisite(void*c){struct HsnState*s=c;if(!begin(s))return 0;int e=s->callbacks.prerequisite(s->callbacks.cookie);return end(s,HSN_CB_PREREQUISITE,0,0,0,e,1)?e:0;}
static int publish(void*c){struct HsnState*s=c;if(!begin(s))return 1;int e=s->callbacks.publish(s->callbacks.cookie);return end(s,HSN_CB_PUBLISH,0,0,0,e,0)?e:1;}
static int checkpoint(void*c,unsigned stage){struct HsnState*s=c;if(!begin(s))return 1;int e=s->callbacks.checkpoint(s->callbacks.cookie,stage);return end(s,HSN_CB_CHECKPOINT,0,0,stage,e,0)?e:1;}
static int stopped(void*c){struct HsnState*s=c;if(!begin(s))return 1;int e=s->callbacks.stopped(s->callbacks.cookie);return end(s,HSN_CB_STOPPED,0,0,0,e,0)?e:1;}
/* Identical original call once; no status/count/output/queue/result rewrite. */
static int hsn_receive_tap(const struct ggh_io*io,unsigned q,uint32_t*out,unsigned capacity,unsigned*count){
 const int status=ggh_receive(io,q,out,capacity,count);
 const struct hgf_queue_cookie*queue=io->cookie;
 struct HsnState*s=queue->io->cookie;
 ++s->receive_calls;
 if(status==GGH_EMPTY){if(q==1 || q==2)++s->receive_empty[q-1];return status;}
 if(s->receive_records>=HSN_RECEIVE_RECORDS){++s->receive_overflow;return status;}
 HsnReceive*r=&s->receive[s->receive_records++];
 *r=(HsnReceive){0};r->sequence=s->receive_calls;r->queue=q;r->status=(uint32_t)status;r->word_count=count?*count:0;
 if(status==GGH_OK && out && count && *count<=capacity && *count<=HSN_MESSAGE_WORDS)
  for(unsigned i=0;i<*count;i++)r->words[i]=out[i];
 return status;
}
int hsn_init(void*p,size_t n,const HsnCallbacks*callbacks,size_t bytes){
 int e=envelope(p,n);if(e)return e;
 if(callbacks && (uintptr_t)callbacks%_Alignof(HsnCallbacks))return HSN_ALIGNMENT;
 if(bytes!=sizeof(*callbacks) || !apart(p,n,callbacks,bytes) || callbacks->abi_version!=HSN_ABI_VERSION || callbacks->reserved ||
  !callbacks->read32 || !callbacks->read64 || !callbacks->write32 || !callbacks->write64 || !callbacks->barrier || !callbacks->delay_us || !callbacks->prerequisite || !callbacks->publish || !callbacks->checkpoint)return HSN_ARGUMENT;
 struct HsnState*s=p;const unsigned char*b=p;unsigned nonzero=0;for(size_t i=0;i<n;i++)nonzero|=b[i];
 if(nonzero){if(!identity(s))return HSN_UNINITIALIZED;return s->busy?HSN_BUSY:HSN_ALREADY_RUN;}
 const HsnCallbacks copy=*callbacks;memset(p,0,n);s->magic=HSN_MAGIC;s->magic_inverse=~HSN_MAGIC;s->self=(uintptr_t)s;s->abi_version=HSN_ABI_VERSION;s->callbacks=copy;s->phase=1;return HSN_OK;
}
static int ready(const struct HhiState*h){
 if(!hhi_identity(h) || h->busy || h->phase!=HHI_PREPARED || h->prepare_called!=1 || h->release_calls || h->first_prepare_error || h->prepare_return_code || h->release_return_code || h->uncertain_allocate_outputs)return 0;
 const struct hfm_result*r=&h->result;
 return !r->code && r->records==15 && r->reservations==5 && r->count==8 && r->allocated==HFM_ALL && r->validated==HFM_ALL && r->retained==HFM_ALL && !r->released && r->bytes_loaded==77808 && r->tcm_bytes_deferred==540 && r->hfi_queues==17 && r->hfi_initialized==1 && r->published==1 && !r->build_status && !r->audit_status && r->audit.complete && r->audit.table_pages==6 && r->audit.leaf_pages==139 && r->audit.descriptor_reads==3072 && r->built.table_pages==6 && r->built.leaf_pages==139 && r->built.start_level==1 && !r->built.upper_region && r->built.root_physical==r->memory[8].physical && r->built.mair==0xff44 && r->config.input_bits==32 && r->config.output_bits==36 && r->config.table_physical==r->memory[8].physical && r->config.leaf_budget==2048;
}
static int bo_apart(const struct HhiState*h,const void*p,size_t n){
 for(unsigned i=0;i<HHI_RESOURCE_ROWS;i++){const struct hfm_allocation*a=&h->result.memory[i];
  if(!a->pages || a->pages>HSC_LARGE_PAGES || !apart(p,n,a->cpu,(size_t)a->pages*4096))return 0;
 }return 1;
}
/* The same original queues/barriers and health/doorbell used by stock. The
 * resource callback is deliberately absent; this is not hhb_run/HWSCHED. */
static int host_ready(struct HsnState*s){return s->busy && !s->binding_lost && bound(s) && s->result.phase==10 && s->result.fw_ready && s->result.hfi_ready && s->result.query_ack;}
static int host_healthy(void*c){struct HsnState*s=c;return host_ready(s) && hgf_health(&s->io,&s->result);}
static int host_send(void*c,const uint32_t*words,unsigned count){
 struct HsnState*s=c;if(!host_ready(s))return GGH_INVALID;
 struct hgf_queue_cookie cookie={&s->io,0};
 const struct ggh_io queues={s->hhi->result.memory[5].cpu,GGH_BYTES,GGH_IOVA,&cookie,hgf_hfi_barrier};
 const int code=ggh_send(&queues,words,count);return cookie.error?GGH_INVALID:code;
}
static int host_receive(void*c,unsigned queue,uint32_t*words,unsigned*count){
 struct HsnState*s=c;if(!host_ready(s))return GGH_INVALID;
 struct hgf_queue_cookie cookie={&s->io,0};
 const struct ggh_io queues={s->hhi->result.memory[5].cpu,GGH_BYTES,GGH_IOVA,&cookie,hgf_hfi_barrier};
 const int code=hsn_receive_tap(&queues,queue,words,HHB_WORDS,count);
 return cookie.error?GGH_INVALID:code;
}
static int host_doorbell(void*c){struct HsnState*s=c;return host_ready(s) && hgf_write(&s->io,&s->result,0x3d7e650,1,0)?0:1;}
static unsigned host_bootstrap(void*c,struct hhb_result*r){
 struct HsnState*s=c;s->host_attempted=1;
 if(r!=&s->result.bootstrap || !host_ready(s)){s->host_return_code=HHB_IO;return HHB_IO;}
 const struct hhb_io io={s,host_healthy,host_send,host_receive,host_doorbell,delay,NULL};
 const unsigned code=hhp_run(&io,r);
 s->host_qualified=!code && r->completed==HHP_STEPS && !r->resource_count && !r->resource_acks && r->record[4].ack && r->record[4].value==1 && r->record[4].status_present && !r->record[4].status;
 s->host_return_code=code?code:(s->host_qualified?HHB_OK:HHB_REJECTED);
 return s->host_return_code;
}
static int run_mode(void*p,size_t n,HhiStorage*memory,size_t bytes,const uint8_t*fw,size_t fw_bytes,uint64_t zero,uint32_t*out,unsigned host){
 const struct HsnState*read;int e=get(p,n,&read);if(e)return e;
 if(read->busy)return HSN_BUSY;if(read->run_called)return HSN_ALREADY_RUN;
 if(memory && (uintptr_t)memory%HHI_STORAGE_ALIGNMENT)return HSN_ALIGNMENT;
 if(out && (uintptr_t)out%_Alignof(uint32_t))return HSN_ALIGNMENT;
 if(bytes!=HHI_STORAGE_BYTES || fw_bytes!=HHI_FIRMWARE_BYTES || !zero || (zero&4095) || zero>=(UINT64_C(1)<<36) ||
  !apart(p,n,memory,bytes) || !apart(p,n,fw,fw_bytes) || !apart(p,n,out,sizeof(*out)) || !apart(memory,bytes,fw,fw_bytes) || !apart(memory,bytes,out,sizeof(*out)) || !apart(fw,fw_bytes,out,sizeof(*out)))return HSN_ARGUMENT;
 struct HhiState*h=(struct HhiState*)memory;struct HsnState*s=p;
 if(!ready(h))return HSN_HHI_NOT_READY;
 if(!bo_apart(h,p,n) || !bo_apart(h,out,sizeof(*out)))return HSN_ARGUMENT;
 s->busy=1;uint32_t contents=UINT32_MAX;
 const int verified=hhi_verify_contents(memory,bytes,fw,fw_bytes,&contents);
 if(verified || contents || !ready(h)){s->busy=0;return HSN_HHI_NOT_READY;}
 /* Audit current actual table RAM once more while all allocations are inactive. */
 unsigned char visited[HFM_TABLE_PAGES]={0};ArmSmmu4KTableAuditResult audit={0};
 ArmSmmu4KTableIo table={&h->result,hfm_descriptor};
 ArmSmmu4KAuditRegion region={h->result.ranges,8,HFM_TABLE_PAGES,HFM_TABLE_PAGES*512,visited,sizeof(visited)};
 if(ArmSmmu4KAuditTables(&table,&h->result.config,&h->result.built,&region,h->result.memory[8].physical,&audit) || !audit.complete || audit.table_pages!=6 || audit.leaf_pages!=139 || audit.descriptor_reads!=3072){s->busy=0;return HSN_HHI_NOT_READY;}
 s->hhi=h;s->firmware=fw;s->firmware_bytes=fw_bytes;s->hhi_sealed=1;h->busy=1;s->hhi_fingerprint=fingerprint(h);s->run_called=1;s->phase=2;s->host_requested=host;
 s->io=(struct hgf_io){{s,rd32,rd64,wr32,wr64,delay,NULL,NULL},s,rd32,wr32,barrier,delay,prerequisite,publish,checkpoint,host?host_bootstrap:NULL,s->callbacks.stopped?stopped:NULL};
 /* The exact real prepared object is the input; no HhiSummary/Row reconstruction. */
 const unsigned code=hgf_run(&s->io,&h->result,fw,fw_bytes,zero,&s->result);
 s->phase=3;s->busy=0;*out=code;return HSN_OK;
}
int hsn_run(void*p,size_t n,HhiStorage*memory,size_t bytes,const uint8_t*fw,size_t fw_bytes,uint64_t zero,uint32_t*out){return run_mode(p,n,memory,bytes,fw,fw_bytes,zero,out,0);}
int hsn_run_host_profile(void*p,size_t n,HhiStorage*memory,size_t bytes,const uint8_t*fw,size_t fw_bytes,uint64_t zero,uint32_t*out){return run_mode(p,n,memory,bytes,fw,fw_bytes,zero,out,1);}
static int output(const struct HsnState*s,const void*out,size_t n,size_t alignment){
 if(out && (uintptr_t)out%alignment)return HSN_ALIGNMENT;
 if(!apart(s,HSN_STORAGE_BYTES,out,n))return HSN_ARGUMENT;
 if(s->hhi && (!apart(s->hhi,HHI_STORAGE_BYTES,out,n) || !apart(s->firmware,s->firmware_bytes,out,n) || !bo_apart(s->hhi,out,n)))return HSN_ARGUMENT;
 return HSN_OK;
}
int hsn_summary(const void*p,size_t n,HsnSummary*out,size_t bytes){
 const struct HsnState*s;int e=get(p,n,&s);if(e)return e;if(bytes!=sizeof(*out))return HSN_ARGUMENT;if((e=output(s,out,bytes,_Alignof(HsnSummary))))return e;
 const struct hgf_result*r=&s->result;HsnSummary v={0};v.abi_version=HSN_ABI_VERSION;v.run_called=s->run_called;v.bridge_phase=s->phase;v.hhi_sealed=s->hhi_sealed;
#define COPY(f) v.f=r->f
 v.hgf_code=r->code;v.hgf_phase=r->phase;COPY(attempted);COPY(retained);COPY(reads);COPY(writes);COPY(polls);COPY(reset_held);COPY(release_attempted);COPY(tcm_backup_words);COPY(tcm_loaded_words);COPY(rsc_attempted);COPY(boot_attempted);COPY(stream_attached);COPY(fw_ready);COPY(hfi_ready);COPY(query_sent);COPY(query_ack);COPY(query_header);COPY(query_value);COPY(query_status);COPY(reply_count);COPY(debug_count);COPY(diagnostic_mask);COPY(fw_status);COPY(hfi_status);COPY(fsr);COPY(fsynr);COPY(gfsr);COPY(irq);COPY(ao_irq);COPY(counter_before);COPY(counter_after);COPY(far);
#undef COPY
 v.receive_calls=s->receive_calls;v.receive_records=s->receive_records;v.receive_empty[0]=s->receive_empty[0];v.receive_empty[1]=s->receive_empty[1];v.receive_overflow=s->receive_overflow;*out=v;return HSN_OK;
}
int hsn_failure(const void*p,size_t n,HsnFailure*out,size_t bytes){
 const struct HsnState*s;int e=get(p,n,&s);if(e)return e;if(bytes!=sizeof(*out))return HSN_ARGUMENT;if((e=output(s,out,bytes,_Alignof(HsnFailure))))return e;
 const struct hgf_result*r=&s->result;HsnFailure v={0};v.hgf_code=r->code;v.failed_phase=r->failed_phase;v.failed_address=r->failed_address;v.failed_value=r->failed_value;v.failed_operation=r->failed_operation;v.failed_observed_valid=r->failed_observed_valid;v.first_callback=s->first_callback;*out=v;return HSN_OK;
}
int hsn_tcm_backup(const void*p,size_t n,uint32_t index,HsnTcmWord*out,size_t bytes){
 const struct HsnState*s;int e=get(p,n,&s);if(e)return e;if(bytes!=sizeof(*out) || index>=HSN_TCM_WORDS)return HSN_ARGUMENT;if((e=output(s,out,bytes,_Alignof(HsnTcmWord))))return e;
 if(index>=s->result.tcm_backup_words)return HSN_NOT_AVAILABLE;const HsnTcmWord v={index,0x3d6d000+4*index,s->result.tcm_before[index],1};*out=v;return HSN_OK;
}
int hsn_receive_log(const void*p,size_t n,uint32_t index,HsnReceive*out,size_t bytes){
 const struct HsnState*s;int e=get(p,n,&s);if(e)return e;if(bytes!=sizeof(*out) || index>=HSN_RECEIVE_RECORDS)return HSN_ARGUMENT;if((e=output(s,out,bytes,_Alignof(HsnReceive))))return e;
 if(index>=s->receive_records)return HSN_NOT_AVAILABLE;*out=s->receive[index];return HSN_OK;
}
int hsn_memory_summary(const void*p,size_t n,HhiSummary*out,size_t bytes){
 const struct HsnState*s;int e=get(p,n,&s);if(e)return e;if(bytes!=sizeof(*out))return HSN_ARGUMENT;if((e=output(s,out,bytes,_Alignof(HhiSummary))))return e;if(!s->hhi)return HSN_NOT_AVAILABLE;if(s->binding_lost || !bound(s))return HSN_HHI_NOT_READY;
 hhi_private_summary(s->hhi,out);return HSN_OK;
}
int hsn_memory_row(const void*p,size_t n,uint32_t index,HhiRow*out,size_t bytes){
 const struct HsnState*s;int e=get(p,n,&s);if(e)return e;if(bytes!=sizeof(*out) || index>=HHI_RESOURCE_ROWS)return HSN_ARGUMENT;if((e=output(s,out,bytes,_Alignof(HhiRow))))return e;if(!s->hhi)return HSN_NOT_AVAILABLE;if(s->binding_lost || !bound(s))return HSN_HHI_NOT_READY;
 hhi_private_row(s->hhi,index,out);return HSN_OK;
}

int hsn_host_profile_summary(const void*p,size_t n,HsnHostProfileSummary*out,size_t bytes){
 const struct HsnState*s;int e=get(p,n,&s);if(e)return e;if(bytes!=sizeof(*out))return HSN_ARGUMENT;if((e=output(s,out,bytes,_Alignof(HsnHostProfileSummary))))return e;
 const struct hhb_result*r=&s->result.bootstrap;
 const HsnHostProfileSummary v={s->host_requested,s->host_attempted,r->code,s->host_return_code,r->stage,r->completed,r->debug_count,r->resource_request,r->resource_words,r->resource_count,r->resource_acks,r->next_sequence,s->host_qualified};*out=v;return HSN_OK;
}
int hsn_host_profile_step(const void*p,size_t n,uint32_t index,HsnHostProfileStep*out,size_t bytes){
 const struct HsnState*s;int e=get(p,n,&s);if(e)return e;if(bytes!=sizeof(*out) || index>=HSN_HOST_PROFILE_STEPS)return HSN_ARGUMENT;if((e=output(s,out,bytes,_Alignof(HsnHostProfileStep))))return e;
 const struct hhb_record*r=&s->result.bootstrap.record[index];if(!s->host_attempted || !r->attempted)return HSN_NOT_AVAILABLE;
 HsnHostProfileStep v={0};v.attempted=r->attempted;v.sent=r->sent;v.ack=r->ack;v.word_count=r->count;v.polls=r->polls;v.status_present=r->status_present;v.legacy_contract=r->legacy_contract;v.header=r->header;v.value=r->value;v.status=r->status;
 for(unsigned i=0;i<HSN_MESSAGE_WORDS;i++)v.reply[i]=r->reply[i];*out=v;return HSN_OK;
}
