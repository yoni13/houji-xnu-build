/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "houji_hfi_inactive.h"
#include <string.h>
#include "houji_hfi_private.h"

size_t hhi_actual_state_size(void) {return sizeof(struct HhiState);}
size_t hhi_actual_state_alignment(void) {return _Alignof(struct HhiState);}
size_t hhi_storage_size(void) {return HHI_STORAGE_BYTES;}
size_t hhi_storage_alignment(void) {return HHI_STORAGE_ALIGNMENT;}

static int valid_interval(const void *p,size_t n) {
    return p && n && (uintptr_t)p<=UINTPTR_MAX-(n-1);
}
static int disjoint(const void *a,size_t n,const void *b,size_t m) {
    if(!valid_interval(a,n) || !valid_interval(b,m))return 0;
    return (uintptr_t)a>(uintptr_t)b+m-1 || (uintptr_t)b>(uintptr_t)a+n-1;
}
static int envelope(const void *p,size_t bytes) {
    if(bytes!=HHI_STORAGE_BYTES || !valid_interval(p,bytes))return HHI_ARGUMENT;
    if((uintptr_t)p&(HHI_STORAGE_ALIGNMENT-1))return HHI_ALIGNMENT;
    return HHI_OK;
}
static int initialized(const struct HhiState *s) {
    return s->magic==HHI_MAGIC && s->magic_inverse==~HHI_MAGIC && s->abi_version==HHI_ABI_VERSION && s->self==(uintptr_t)s;
}
static int zero_storage(const void *p) {
    const unsigned char *b=p;
    for(size_t i=0;i<HHI_STORAGE_BYTES;++i)if(b[i])return 0;
    return 1;
}
static int state(const void *p,size_t bytes,const struct HhiState **out) {
    int e=envelope(p,bytes);if(e)return e;
    const struct HhiState *s=(const struct HhiState *)p;
    if(!initialized(s))return HHI_UNINITIALIZED;
    if(s->busy)return HHI_BUSY;
    *out=s;return HHI_OK;
}
static void trace(HhiCallbackTrace *t,unsigned slot,int status) {
    const uint32_t bit=UINT32_C(1)<<slot;
    ++t->calls;t->attempted_mask|=bit;
    if(status)t->failure_mask|=bit;else t->success_mask|=bit;
}
static int allocate_bridge(void *cookie,unsigned pages,uint64_t *pa,void **cpu) {
    struct HhiState *s=(struct HhiState *)cookie;
    const unsigned slot=s->allocate_index++;
    if(slot>=HHI_RESOURCE_ROWS)return 1;
    int e=s->callbacks.allocate(s->callbacks.cookie,pages,pa,cpu);
    trace(&s->traces[0],slot,e);
    if(e && (*pa || *cpu))s->uncertain_allocate_outputs|=UINT32_C(1)<<slot;
    return e;
}
static int validate_bridge(void *cookie,uint64_t pa,void *cpu,unsigned pages,unsigned nc) {
    struct HhiState *s=(struct HhiState *)cookie;
    /* Upstream validates the just-allocated row. Identity is the row, never a
     * possibly malformed or duplicate physical/CPU tuple supplied by a callback. */
    const unsigned slot=s->allocate_index?s->allocate_index-1:HHI_RESOURCE_ROWS;
    if(slot>=HHI_RESOURCE_ROWS)return 1;
    int e=s->callbacks.validate(s->callbacks.cookie,pa,cpu,pages,nc);
    trace(&s->traces[1],slot,e);return e;
}
static int publish_bridge(void *cookie,const struct hfm_allocation *a) {
    struct HhiState *s=(struct HhiState *)cookie;
    unsigned slot=0;while(slot<HHI_RESOURCE_ROWS && a!=&s->result.memory[slot])++slot;
    if(slot>=HHI_RESOURCE_ROWS)return 1;
    const HhiAllocation row={a->physical,a->cpu,a->pages,a->nc,a->iova,0};
    int e=s->callbacks.publish(s->callbacks.cookie,&row);
    trace(&s->traces[2],slot,e);return e;
}
static int release_bridge(void *cookie,uint64_t pa,void *cpu,unsigned pages) {
    struct HhiState *s=(struct HhiState *)cookie;
    if(s->phase==HHI_PREPARING && !s->first_prepare_error && s->result.code)
        s->first_prepare_error=s->result.code;
    /* hfm_release visits allocated&&!released in descending index order even
     * when a callback fails. The cursor distinguishes equal invalid tuples
     * without fabricating any ownership bit or retrying a successful release. */
    unsigned slot=HHI_RESOURCE_ROWS;
    while(s->release_cursor) {
        const unsigned i=--s->release_cursor;const uint32_t bit=UINT32_C(1)<<i;
        if((s->result.allocated&bit) && !(s->result.released&bit)){slot=i;break;}
    }
    if(slot>=HHI_RESOURCE_ROWS)return 1;
    const struct hfm_allocation *a=&s->result.memory[slot];
    if(a->physical!=pa || a->cpu!=cpu || a->pages!=pages)return 1;
    int e=s->callbacks.release(s->callbacks.cookie,pa,cpu,pages);
    trace(&s->traces[3],slot,e);return e;
}
static struct hfm_io operations(struct HhiState *s) {
    const struct hfm_io io={s,allocate_bridge,validate_bridge,publish_bridge,release_bridge};return io;
}
int hhi_init(void *p,size_t bytes,const HhiCallbacks *callbacks,size_t callback_bytes) {
    int e=envelope(p,bytes);if(e)return e;
    if(callbacks && ((uintptr_t)callbacks%_Alignof(HhiCallbacks)))return HHI_ALIGNMENT;
    if(callback_bytes!=sizeof(*callbacks) || !disjoint(p,bytes,callbacks,callback_bytes) ||
       callbacks->abi_version!=HHI_ABI_VERSION || callbacks->reserved || !callbacks->allocate ||
       !callbacks->validate || !callbacks->publish || !callbacks->release)return HHI_ARGUMENT;
    struct HhiState *s=(struct HhiState *)p;
    if(!zero_storage(p)) {
        if(!initialized(s))return HHI_UNINITIALIZED;
        if(s->busy)return HHI_BUSY;
        if(s->uncertain_allocate_outputs || s->result.retained || (s->result.allocated&~s->result.released))
            return HHI_OWNERS_REMAIN;
        /* No repeated init before the one permitted prepare attempt. */
        if(!s->prepare_called)return HHI_ALREADY_PREPARED;
    }
    const HhiCallbacks copy=*callbacks;
    memset(p,0,bytes);s->magic=HHI_MAGIC;s->magic_inverse=~HHI_MAGIC;s->self=(uintptr_t)s;s->abi_version=HHI_ABI_VERSION;
    s->callbacks=copy;s->phase=HHI_INITIALIZED;return HHI_OK;
}
int hhi_prepare(void *p,size_t bytes,const uint8_t *fw,size_t fw_bytes,uint32_t *out) {
    const struct HhiState *read;int e=state(p,bytes,&read);if(e)return e;
    if(out && ((uintptr_t)out%_Alignof(uint32_t)))return HHI_ALIGNMENT;
    if(fw_bytes!=HHI_FIRMWARE_BYTES || !disjoint(p,bytes,fw,fw_bytes) ||
       !disjoint(p,bytes,out,sizeof(*out)) || !disjoint(fw,fw_bytes,out,sizeof(*out)))return HHI_ARGUMENT;
    if(read->prepare_called)return HHI_ALREADY_PREPARED;
    struct HhiState *s=(struct HhiState *)p;
    s->prepare_called=1;s->phase=HHI_PREPARING;s->busy=1;s->release_cursor=HHI_RESOURCE_ROWS;
    const struct hfm_io io=operations(s);
    const unsigned code=hfm_prepare(&io,fw,fw_bytes,&s->result);
    if(!s->first_prepare_error && code)s->first_prepare_error=code;
    s->prepare_return_code=code;s->phase=HHI_PREPARED;s->busy=0;*out=code;
    return HHI_OK;
}
int hhi_summary(const void *p,size_t bytes,HhiSummary *out,size_t out_bytes) {
    const struct HhiState *s;int e=state(p,bytes,&s);if(e)return e;
    if(out && ((uintptr_t)out%_Alignof(HhiSummary)))return HHI_ALIGNMENT;
    if(out_bytes!=sizeof(*out) || !disjoint(p,bytes,out,out_bytes))return HHI_ARGUMENT;
    hhi_private_summary(s,out);return HHI_OK;
}
int hhi_row(const void *p,size_t bytes,uint32_t index,HhiRow *out,size_t out_bytes) {
    const struct HhiState *s;int e=state(p,bytes,&s);if(e)return e;
    if(out && ((uintptr_t)out%_Alignof(HhiRow)))return HHI_ALIGNMENT;
    if(index>=HHI_RESOURCE_ROWS || out_bytes!=sizeof(*out) || !disjoint(p,bytes,out,out_bytes))return HHI_ARGUMENT;
    hhi_private_row(s,index,out);return HHI_OK;
}
int hhi_release(void *p,size_t bytes,uint32_t *out) {
    const struct HhiState *read;int e=state(p,bytes,&read);if(e)return e;
    if(out && ((uintptr_t)out%_Alignof(uint32_t)))return HHI_ALIGNMENT;
    if(!disjoint(p,bytes,out,sizeof(*out)))return HHI_ARGUMENT;
    if(!read->prepare_called)return HHI_NOT_PREPARED;
    struct HhiState *s=(struct HhiState *)p;s->phase=HHI_RELEASING;s->busy=1;s->release_cursor=HHI_RESOURCE_ROWS;++s->release_calls;
    const struct hfm_io io=operations(s);const unsigned code=hfm_release(&io,&s->result);
    s->release_return_code=code;s->phase=HHI_RELEASE_ATTEMPTED;s->busy=0;*out=code;return HHI_OK;
}

static int contents_zero(const volatile uint8_t *p,size_t bytes) {
    for(size_t i=0;i<bytes;++i)if(p[i])return 0;
    return 1;
}
static void contents_barrier(void *cookie) {(void)cookie;}
static unsigned verify_contents(const struct hfm_result *r,const uint8_t *fw,size_t bytes) {
    struct gg_fw_block blocks[GG_FW_MAX_BLOCKS];
    struct gg_reservation reservations[GG_FW_MAX_BLOCKS];
    AdrenoFirmwareBlock generic[GG_FW_MAX_BLOCKS];size_t nb=0,nr=0,ng=0;
    static const AdrenoFirmwareRegion regions[]={
        {0,0x4000,GG_ITCM},{0x4000,0xffc000,GG_ICACHE},
        {0x10004000,0x4000,GG_DTCM},{0x60000000,0x20000000,GG_KERNEL},
        {0xc0000000,0x20000000,GG_KERNEL_EXT}};
    if(AdrenoFirmwarePlanGmu(fw,bytes,regions,5,generic,GG_FW_MAX_BLOCKS,&ng)!=AdrenoFirmwareSuccess ||
       gg_fw_plan(fw,bytes,blocks,GG_FW_MAX_BLOCKS,&nb)!=GG_FW_OK || nb!=ng ||
       gg_memory_plan(blocks,nb,reservations,GG_FW_MAX_BLOCKS,&nr)!=GG_FW_OK || nr!=5 || nb!=r->records)
        return 1;
    size_t loaded=0,deferred=0;
    for(size_t j=0;j<nb;++j) {
        const struct gg_fw_block *b=&blocks[j];const AdrenoFirmwareBlock *g=&generic[j];
        if(b->address!=g->address || b->size!=g->bytes || b->type!=g->type ||
           b->value!=g->value || b->payload_offset!=g->payload_offset ||
           (b->size && g->region_identifier!=(unsigned)b->region))return 1;
        if(b->region==GG_ITCM || b->region==GG_DTCM)deferred+=b->size;
        else loaded+=b->size;
    }
    if(loaded!=77808 || deferred!=540)return 1;
    for(size_t i=0;i<nr;++i) {
        const struct hfm_allocation *a=&r->memory[i];
        if(a->iova!=reservations[i].iova || (uint64_t)a->pages*4096!=reservations[i].bytes || a->nc)return 1;
        const volatile uint8_t *actual=a->cpu;
        for(size_t offset=0;offset<reservations[i].bytes;++offset) {
            const uint64_t address=(uint64_t)a->iova+offset;const uint8_t *expected=NULL;
            for(size_t j=0;j<nb;++j) {
                const struct gg_fw_block *b=&blocks[j];
                if(b->size && address>=b->address && address-b->address<b->size) {
                    expected=fw+b->payload_offset+(size_t)(address-b->address);break;
                }
            }
            if(expected ? actual[offset]!=*expected : actual[offset]!=0)return expected?2:3;
        }
    }
    const struct hfm_allocation *h=&r->memory[5],*log=&r->memory[6],*vrb=&r->memory[7];
    if(h->iova!=GGH_IOVA || h->pages!=GGH_BYTES/4096 || h->nc!=1 ||
       log->iova!=GGH_LOG_IOVA || log->pages!=GGH_LOG_BYTES/4096 || log->nc!=1 ||
       vrb->iova!=GGH_VRB_IOVA || vrb->pages!=GGH_VRB_BYTES/4096 || vrb->nc!=1 ||
       r->memory[8].pages!=HFM_TABLE_PAGES || r->memory[8].iova || r->memory[8].nc)return 1;
    const struct ggh_io queues={h->cpu,GGH_BYTES,GGH_IOVA,NULL,contents_barrier};
    for(unsigned q=0;q<GGH_QUEUES;++q) {
        volatile uint32_t *header,*ring;unsigned read=0,write=0;
        if(ggh_queue(&queues,q,&header,&ring,&read,&write)!=GGH_OK || read || write)return 4;
        for(unsigned j=4;j<GGH_QUEUE_HEADER_WORDS;++j)if(header[j])return 4;
        if(!contents_zero((const volatile uint8_t *)ring,GGH_RING_WORDS*4))return 4;
    }
    const volatile uint8_t *hfi=h->cpu;
    const size_t table_bytes=(GGH_HEADER_WORDS+GGH_QUEUES*GGH_QUEUE_HEADER_WORDS)*4;
    const size_t ring_end=GGH_RING_OFFSET+GGH_QUEUES*GGH_RING_WORDS*4;
    if(!contents_zero(hfi+table_bytes,GGH_RING_OFFSET-table_bytes) ||
       !contents_zero(hfi+ring_end,GGH_BYTES-ring_end))return 4;
    if(!contents_zero(log->cpu,GGH_LOG_BYTES))return 5;
    if(!contents_zero(vrb->cpu,GGH_VRB_BYTES))return 6;
    return 0;
}
int hhi_verify_contents(void *p,size_t bytes,const uint8_t *fw,size_t fw_bytes,uint32_t *out) {
    const struct HhiState *read;int e=state(p,bytes,&read);if(e)return e;
    if(out && ((uintptr_t)out%_Alignof(uint32_t)))return HHI_ALIGNMENT;
    if(fw_bytes!=HHI_FIRMWARE_BYTES || !disjoint(p,bytes,fw,fw_bytes) ||
       !disjoint(p,bytes,out,sizeof(*out)) || !disjoint(fw,fw_bytes,out,sizeof(*out)))return HHI_ARGUMENT;
    const struct hfm_result *r=&read->result;
    if(!read->prepare_called || read->phase!=HHI_PREPARED || read->prepare_return_code || r->code ||
       read->release_calls || read->uncertain_allocate_outputs || r->count!=HHI_GMU_RANGES ||
       r->allocated!=HFM_ALL || r->validated!=HFM_ALL || r->retained!=HFM_ALL || r->released ||
       !r->published || !r->hfi_initialized || r->hfi_queues!=GGH_QUEUES)return HHI_NOT_PREPARED;
    for(unsigned i=0;i<HHI_RESOURCE_ROWS;++i) {
        const struct hfm_allocation *a=&r->memory[i];const size_t length=(size_t)a->pages*4096;
        if(!a->physical || (a->physical&4095) || !a->pages || a->pages>HSC_LARGE_PAGES ||
           a->physical>(UINT64_C(1)<<36)-length || ((uintptr_t)a->cpu&4095) ||
           !disjoint(p,bytes,a->cpu,length) || !disjoint(fw,fw_bytes,a->cpu,length) ||
           !disjoint(out,sizeof(*out),a->cpu,length))return HHI_ARGUMENT;
    }
    struct HhiState *s=(struct HhiState *)p;s->busy=1;
    const unsigned code=verify_contents(r,fw,fw_bytes);
    s->busy=0;*out=code;return HHI_OK;
}
