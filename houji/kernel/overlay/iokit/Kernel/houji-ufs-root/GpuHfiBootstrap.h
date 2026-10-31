/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Bounded gen7-9-1 HWSCHED initialization over the already-qualified HFI.
 * A platform may satisfy one audited resource request; unknown resources stop.
 * No GX MMIO, CP/PM4, PAS, OOB or secure ownership operation exists here.
 */
#ifndef HOUJI_GPU_HFI_BOOTSTRAP_H
#define HOUJI_GPU_HFI_BOOTSTRAP_H
#include "GpuHfiTables.h"
#include "GpuSharedMemory.h"
enum { HHB_OK,HHB_ARGUMENT,HHB_IO,HHB_TIMEOUT,HHB_PROTOCOL,HHB_REJECTED,HHB_RESOURCE };
enum { HHB_STEPS=9,HHB_WORDS=255 };
struct hhb_record {
 unsigned attempted,sent,ack,count,polls,status_present,legacy_contract;
 uint32_t header,value,status,reply[HHB_WORDS];
};
struct hhb_result {
 unsigned code,stage,completed,debug_count,resource_request,resource_words;
 struct hhb_record record[HHB_STEPS];
 unsigned resource_count,resource_acks,next_sequence;
 struct {unsigned count,handled,sent;uint32_t request[HHB_WORDS],response[11];} resources[HSC_MAX_RESOURCES];
};
struct hhb_io {
 void *cookie;
 int (*healthy)(void *);
 int (*send)(void *,const uint32_t *,unsigned);
 int (*receive)(void *,unsigned,uint32_t *,unsigned *);
 int (*doorbell)(void *);
 int (*delay)(void *,unsigned);
 /* 0 = fully mapped/validated response, 1 = unsupported, -1 = uncertain I/O. */
 int (*resource)(void *,const uint32_t *,unsigned,uint32_t *);
};
static inline unsigned hhb_exchange(const struct hhb_io *io,struct hhb_result *r,
 unsigned step,unsigned id,const uint32_t *payload,unsigned words,unsigned query,unsigned legacy) {
 if(!io || !r || step>=HHB_STEPS || id>255 || words>159 || (words && !payload) || query>1 || legacy>1)
  return HHB_ARGUMENT;
 uint32_t request[160];struct hhb_record *record=&r->record[step];
 r->stage=step+1;record->attempted=1;record->legacy_contract=legacy;
 record->header=(r->next_sequence++<<20)|((words+1u)<<8)|id;
 request[0]=record->header;for(unsigned i=0;i<words;i++)request[i+1]=payload[i];
 if(!io->healthy(io->cookie) || io->send(io->cookie,request,words+1) ||
    !io->healthy(io->cookie) || io->doorbell(io->cookie))return HHB_IO;
 record->sent=1;
 for(unsigned poll=0;poll<10000;poll++) {
  if(!io->healthy(io->cookie))return HHB_IO;
  for(unsigned queue=1;queue<=2;queue++) {
   unsigned count=0;int result=io->receive(io->cookie,queue,record->reply,&count);
   if(result==1)continue; // GGH_EMPTY, no message consumed.
   if(result || !count || count>HHB_WORDS)return HHB_PROTOCOL;
   record->count=count;
   if(queue==2){if(++r->debug_count>32)return HHB_PROTOCOL;continue;}
   uint32_t header=record->reply[0];
   if(((header>>8)&255)!=count)return HHB_PROTOCOL;
   if(((header>>16)&15)!=1) {
    if((header&255)==20 && count>=2) {
     r->resource_request=1;r->resource_words=count;
     if(r->resource_count>=HSC_MAX_RESOURCES)return HHB_PROTOCOL;
     unsigned slot=r->resource_count++;r->resources[slot].count=count;
     for(unsigned i=0;i<count;i++)r->resources[slot].request[i]=record->reply[i];
     if(step!=8 || !io->resource || count!=11)return HHB_RESOURCE;
     int supplied=io->resource(io->cookie,record->reply,count,r->resources[slot].response);
     if(supplied)return supplied<0?HHB_IO:HHB_RESOURCE;
     uint32_t *out=r->resources[slot].response;
     // Preserve all request semantics; only addresses and host handle change.
     if(out[4]!=record->reply[4] || out[5]!=record->reply[5] || out[6]!=record->reply[7] ||
        out[7]!=record->reply[7] || out[9]!=record->reply[9] || out[10]!=record->reply[10])return HHB_PROTOCOL;
     out[0]=(r->next_sequence++<<20)|(1u<<16)|(11u<<8)|20;out[1]=header;r->resources[slot].handled=1;
     if(!io->healthy(io->cookie) || io->send(io->cookie,out,11) ||
        !io->healthy(io->cookie) || io->doorbell(io->cookie))return HHB_IO;
     r->resources[slot].sent=1;r->resource_acks++;
     continue;
    }
    return HHB_PROTOCOL;
   }
   // Pinned vendor gen7_hfi_send_generic_req checks word2 for TABLE/BW/CORE/START.
   // GET_VALUE and v5 FEATURE_CTRL require word3 status. Never invent that word.
   if((count<4 && !(legacy && count==3)) || (header&255)!=id || record->reply[1]!=record->header)return HHB_PROTOCOL;
   record->value=record->reply[2];record->status_present=count>=4;
   if(record->status_present)record->status=record->reply[3];record->ack=1;
   if((record->status_present && record->status) || (!query && record->value))return HHB_REJECTED;
   r->completed++;return HHB_OK;
  }
  record->polls++;if(io->delay(io->cookie,10))return HHB_IO;
 }
 return HHB_TIMEOUT;
}
static inline unsigned hhb_run(const struct hhb_io *io,struct hhb_result *r) {
 if(!r)return HHB_ARGUMENT;*r=(struct hhb_result){0};r->next_sequence=2;
 if(!io || !io->healthy || !io->send || !io->receive || !io->doorbell || !io->delay)
  return r->code=HHB_ARGUMENT;
 const uint32_t hwsched[]={1,1,0},kprof[]={16,1,0},core[]={0},context[]={121,0};
 const uint32_t preempt[]={2,0,0},aqe[]={29,1,0};
 const struct {unsigned id,words,query,legacy;const uint32_t *payload;} steps[]={
  {15,18,0,1,hhb_perf},{3,159,0,1,hhb_bw},{11,3,0,0,hwsched},{11,3,0,0,kprof},
  {14,1,0,1,core},{12,2,1,0,context},{11,3,0,0,preempt},{11,3,0,0,aqe},{10,0,0,1,0}};
 for(unsigned step=0;step<HHB_STEPS;step++) {
  r->code=hhb_exchange(io,r,step,steps[step].id,steps[step].payload,steps[step].words,steps[step].query,steps[step].legacy);
  if(r->code)return r->code;
 }
 return r->code;
}
#endif
