/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef GIULIA_GPU_HFI_H
#define GIULIA_GPU_HFI_H
#include <stdint.h>
#include <stddef.h>
/* Gen7 HWSCHED wire layout from the fixed OnePlus adreno_hfi.h. All sizes
 * and indices on the wire are dwords; each queue packet occupies 4-dword
 * aligned storage. CPU mapping must be Normal NC, not Device memory. */
enum { GGH_QUEUES=17, GGH_HEADER_WORDS=6, GGH_QUEUE_HEADER_WORDS=12,
    GGH_TABLE_WORDS=210, GGH_RING_OFFSET=848, GGH_RING_WORDS=1024,
    GGH_BYTES=0x12000, GGH_IOVA=0x60010000,
    GGH_LOG_IOVA=0x60030000, GGH_LOG_BYTES=0x10000,
    GGH_VRB_IOVA=0x60040000, GGH_VRB_BYTES=0x1000,
    GGH_OK=0, GGH_EMPTY=1, GGH_FULL=2, GGH_INVALID=3, GGH_CORRUPT=4 };
typedef void (*ggh_barrier_fn)(void *);
struct ggh_io { volatile uint32_t *words; size_t bytes; uint32_t iova;
    void *cookie; ggh_barrier_fn barrier; };
static inline unsigned ggh_priority(unsigned q)
{ return q<7?0:q<11?1:q<14?2:q<16?3:4; }
static inline int ggh_init(void *memory,size_t bytes,uint32_t iova)
{
    uint32_t *w=(uint32_t *)memory;
    if(!w || ((uintptr_t)w&3) || bytes<GGH_BYTES || (iova&4095) ||
       iova>UINT32_MAX-GGH_BYTES)return GGH_INVALID;
    for(unsigned i=0;i<GGH_BYTES/4;++i)w[i]=0;
    w[1]=GGH_TABLE_WORDS;w[2]=GGH_HEADER_WORDS;w[3]=GGH_QUEUE_HEADER_WORDS;
    w[4]=w[5]=GGH_QUEUES;
    for(unsigned q=0;q<GGH_QUEUES;++q) {
        uint32_t *h=w+GGH_HEADER_WORDS+q*GGH_QUEUE_HEADER_WORDS;
        h[0]=1;h[1]=iova+GGH_RING_OFFSET+4096*q;
        h[2]=q|(ggh_priority(q)<<8);h[3]=GGH_RING_WORDS;
    }
    return GGH_OK;
}
static inline int ggh_queue(const struct ggh_io *io,unsigned q,
    volatile uint32_t **header,volatile uint32_t **ring,unsigned *r,unsigned *w)
{
    if(!io || !io->words || ((uintptr_t)io->words&3) || io->bytes<GGH_BYTES ||
       !io->barrier || q>=GGH_QUEUES || (io->iova&4095) || io->iova>UINT32_MAX-GGH_BYTES)
        return GGH_INVALID;
    volatile uint32_t *p=io->words,*h=p+GGH_HEADER_WORDS+q*GGH_QUEUE_HEADER_WORDS;
    if(p[0] || p[1]!=GGH_TABLE_WORDS || p[2]!=6 || p[3]!=12 ||
       p[4]!=GGH_QUEUES || p[5]!=GGH_QUEUES || h[0]!=1 ||
       h[1]!=io->iova+GGH_RING_OFFSET+q*4096 ||
       h[2]!=(q|(ggh_priority(q)<<8)) || h[3]!=GGH_RING_WORDS || h[4])return GGH_CORRUPT;
    *r=h[10];*w=h[11];
    if(*r>=GGH_RING_WORDS || *w>=GGH_RING_WORDS || ((*r|*w)&3))return GGH_CORRUPT;
    io->barrier(io->cookie);
    *header=h;*ring=p+GGH_RING_OFFSET/4+q*GGH_RING_WORDS;
    return GGH_OK;
}
static inline int ggh_send(const struct ggh_io *io,const uint32_t *msg,unsigned n)
{
    volatile uint32_t *h,*ring;unsigned r,w,padded;int e;
    if(!msg || !n || n>255 || ((msg[0]>>8)&255)!=n)return GGH_INVALID;
    e=ggh_queue(io,0,&h,&ring,&r,&w);if(e)return e;
    padded=(n+3)&~3u;
    if(((r-w-1)&1023)<padded)return GGH_FULL;
    for(unsigned i=0;i<padded;++i)ring[(w+i)&1023]=i<n?msg[i]:0xfafafafa;
    io->barrier(io->cookie);h[11]=(w+padded)&1023;io->barrier(io->cookie);
    return GGH_OK;
}
static inline int ggh_receive(const struct ggh_io *io,unsigned q,uint32_t *out,
    unsigned capacity,unsigned *count)
{
    volatile uint32_t *h,*ring;unsigned r,w,n,padded;int e;
    if(!count)return GGH_INVALID;
    *count=0;
    if(!out || (q!=1 && q!=2))return GGH_INVALID;
    e=ggh_queue(io,q,&h,&ring,&r,&w);if(e)return e;
    if(r==w)return GGH_EMPTY;
    n=(ring[r]>>8)&255;padded=(n+3)&~3u;
    if(!n || n>capacity || padded>((w-r)&1023))return GGH_CORRUPT;
    for(unsigned i=0;i<n;++i)out[i]=ring[(r+i)&1023];
    io->barrier(io->cookie);h[10]=(r+padded)&1023;io->barrier(io->cookie);
    *count=n;return GGH_OK;
}
#endif
