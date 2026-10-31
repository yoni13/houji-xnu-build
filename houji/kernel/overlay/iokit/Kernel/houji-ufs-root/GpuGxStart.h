/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Fixed minimum DCVS vote + one GPU OOB lease and gated GX read-only snapshot.
 * No CP/PM4, register configuration, PAS, IRQ enable or automatic lease release.
 */
#ifndef HOUJI_GPU_GX_START_H
#define HOUJI_GPU_GX_START_H
#include "GpuHfiBootstrap.h"
#define HGX_SET UINT32_C(0x40000000)
#define HGX_ACK UINT32_C(0x80000000)
enum { HGX_OK,HGX_ARGUMENT,HGX_GUARD,HGX_HFI,HGX_IO,HGX_TIMEOUT,HGX_POWER };
struct hgx_result {
 unsigned code,phase,retained,reads,writes,polls,qualified,requested,acked,clear_attempted,clear_confirmed,gx_ready,last_operation,observed_valid,valid;
 uint32_t gx_before,irq_before,host_before,irq_ack,irq_after,gx_after,fence,busy,rbbm,rbbm3,sqe,last_address,last_value;
 struct hhb_result vote;
};
struct hgx_io {
 struct hhb_io hfi;
 int (*read)(void *,uint32_t,uint32_t *);
 int (*write)(void *,uint32_t,uint32_t);
 int (*qualified)(void *);
};
static inline int hgx_read(const struct hgx_io *io,struct hgx_result*r,uint32_t a,uint32_t *value) {
 r->reads++;r->last_address=a;r->last_operation=1;r->observed_valid=0;
 if(!io->hfi.healthy(io->hfi.cookie) || io->read(io->hfi.cookie,a,value))return 0;
 r->last_value=*value;r->observed_valid=1;return 1;
}
static inline int hgx_write(const struct hgx_io *io,struct hgx_result*r,uint32_t a,uint32_t value) {
 r->writes++;r->last_address=a;r->last_value=value;r->last_operation=2;r->observed_valid=0;
 return io->hfi.healthy(io->hfi.cookie) && !io->write(io->hfi.cookie,a,value);
}
static inline unsigned hgx_run(const struct hgx_io *io,unsigned *sequence,struct hgx_result*r) {
 if(!r)return HGX_ARGUMENT;*r=(struct hgx_result){.retained=1};
 if(!io || !sequence || !*sequence || *sequence>=4095 || !io->read || !io->write || !io->qualified ||
  !io->hfi.healthy || !io->hfi.send || !io->hfi.receive || !io->hfi.doorbell || !io->hfi.delay)return r->code=HGX_ARGUMENT;
#define HGX_REQUIRE(test,error) do {if(!(test))return r->code=(error);} while(0)
 r->phase=1;HGX_REQUIRE(!io->qualified(io->hfi.cookie),HGX_GUARD);r->qualified=1;
 HGX_REQUIRE(hgx_read(io,r,0x3d7e340,&r->gx_before),HGX_IO);r->valid|=1;
 HGX_REQUIRE(hgx_read(io,r,0x3d7e648,&r->irq_before),HGX_IO);r->valid|=2;
 HGX_REQUIRE(hgx_read(io,r,0x3d7e658,&r->host_before),HGX_IO);r->valid|=4;
 HGX_REQUIRE(!(r->irq_before&HGX_ACK) && !(r->host_before&(HGX_SET|HGX_ACK)),HGX_GUARD);
 r->phase=2;r->vote.next_sequence=*sequence;const uint32_t payload[]={1,1,1};
 unsigned vote=hhb_exchange(&io->hfi,&r->vote,0,30,payload,3,0,1);r->vote.code=vote;*sequence=r->vote.next_sequence;
 HGX_REQUIRE(!vote,HGX_HFI);
 uint32_t value;HGX_REQUIRE(hgx_read(io,r,0x3d7e648,&value) && !(value&HGX_ACK),HGX_GUARD);
 r->phase=3;r->requested=1;HGX_REQUIRE(hgx_write(io,r,0x3d7e650,HGX_SET),HGX_IO);
 for(unsigned i=0;i<10000;i++) {
  HGX_REQUIRE(hgx_read(io,r,0x3d7e648,&r->irq_ack),HGX_IO);r->valid|=8;
  if(r->irq_ack&HGX_ACK){r->acked=1;break;}r->polls++;HGX_REQUIRE(!io->hfi.delay(io->hfi.cookie,10),HGX_IO);
 }
 HGX_REQUIRE(r->acked,HGX_TIMEOUT);r->clear_attempted=1;
 HGX_REQUIRE(hgx_write(io,r,0x3d7e644,HGX_ACK),HGX_IO);
 HGX_REQUIRE(hgx_read(io,r,0x3d7e648,&r->irq_after),HGX_IO);r->valid|=16;
 HGX_REQUIRE(!(r->irq_after&HGX_ACK),HGX_IO);r->clear_confirmed=1;
 r->phase=4;
 for(unsigned i=0;i<10000;i++) {
  HGX_REQUIRE(hgx_read(io,r,0x3d7e340,&r->gx_after),HGX_IO);r->valid|=32;
  if(!(r->gx_after&3))break;r->polls++;HGX_REQUIRE(!io->hfi.delay(io->hfi.cookie,10),HGX_IO);
 }
 HGX_REQUIRE(!(r->gx_after&3),HGX_POWER);
 HGX_REQUIRE(hgx_read(io,r,0x3d8ec40,&r->fence),HGX_IO);r->valid|=64;
 HGX_REQUIRE(hgx_read(io,r,0x3d8ec30,&r->busy),HGX_IO);r->valid|=128;
 HGX_REQUIRE(!r->fence && !(r->busy&(1u<<23)) && !io->qualified(io->hfi.cookie),HGX_POWER);r->gx_ready=1;
 r->phase=5;
 HGX_REQUIRE(hgx_read(io,r,0x3d00840,&r->rbbm),HGX_IO);r->valid|=256;
 HGX_REQUIRE(hgx_read(io,r,0x3d0084c,&r->rbbm3),HGX_IO);r->valid|=512;
 HGX_REQUIRE(hgx_read(io,r,0x3d02020,&r->sqe),HGX_IO);r->valid|=1024;
 r->phase=6;return HGX_OK;
#undef HGX_REQUIRE
}
#endif
