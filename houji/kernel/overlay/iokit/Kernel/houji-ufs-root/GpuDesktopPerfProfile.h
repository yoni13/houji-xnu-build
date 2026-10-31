/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Board-owned full factory OPP profile. The matching captured houji speed-bin
 * table supplies all 12 ARC and bandwidth votes. The initial vote remains 500MHz; runtime
 * governor requests only these advertised levels. The boot level is advertised before START and voted
 * once before CP execution using the original blocking HFI30/OOB protocol.
 */
#ifndef HOUJI_DESKTOP_PERF500_H
#define HOUJI_DESKTOP_PERF500_H
#include "GpuHostProfile.h"
#include "GpuGxStart.h"
#include "HoujiGpuFactoryOpp.h"
static inline int hup_perf(uint32_t out[HHP_PERF_WORDS]) {
 if(hhp_perf(out))return 1;
 out[0]=HCO_COUNT+1;
 for(unsigned level=1;level<=HCO_COUNT;++level){
  const struct HoujiGpuOpp*opp=hco_opp(level);unsigned at=2+level*3;
  out[at]=opp->arc_vote;out[at+1]=0xffffffffu;out[at+2]=opp->khz;
 }
 return 0;
}
static inline unsigned hup_run(const struct hhb_io *io,struct hhb_result *r) {
 if(!r)return HHB_ARGUMENT;*r=(struct hhb_result){0};r->next_sequence=2;
 if(!io || !io->healthy || !io->send || !io->receive || !io->doorbell || !io->delay)
  return r->code=HHB_ARGUMENT;
 uint32_t perf[HHP_PERF_WORDS];if(hup_perf(perf))return r->code=HHB_ARGUMENT;
 const uint32_t core[]={0},query[]={125,0};
 const struct {unsigned id,words,query,legacy;const uint32_t *payload;} steps[]={
  {4,HHP_PERF_WORDS,0,1,perf},{3,159,0,1,hhb_bw},
  {14,1,0,1,core},{10,0,0,1,NULL},{12,2,1,0,query}};
 for(unsigned i=0;i<HHP_STEPS;i++) {
  r->code=hhb_exchange(io,r,i,steps[i].id,steps[i].payload,steps[i].words,steps[i].query,steps[i].legacy);
  if(r->code)return r->code;
 }
 return r->code;
}
static inline unsigned hup_gx_run(const struct hgx_io *io,unsigned *sequence,struct hgx_result*r) {
 if(!r)return HGX_ARGUMENT;*r=(struct hgx_result){.retained=1};
 if(!io || !sequence || !*sequence || *sequence>=4095 || !io->read || !io->write || !io->qualified ||
  !io->hfi.healthy || !io->hfi.send || !io->hfi.receive || !io->hfi.doorbell || !io->hfi.delay)return r->code=HGX_ARGUMENT;
#define HGX_REQUIRE(test,error) do {if(!(test))return r->code=(error);} while(0)
 r->phase=1;HGX_REQUIRE(!io->qualified(io->hfi.cookie),HGX_GUARD);r->qualified=1;
 HGX_REQUIRE(hgx_read(io,r,0x3d7e340,&r->gx_before),HGX_IO);r->valid|=1;
 HGX_REQUIRE(hgx_read(io,r,0x3d7e648,&r->irq_before),HGX_IO);r->valid|=2;
 HGX_REQUIRE(hgx_read(io,r,0x3d7e658,&r->host_before),HGX_IO);r->valid|=4;
 HGX_REQUIRE(!(r->irq_before&HGX_ACK) && !(r->host_before&(HGX_SET|HGX_ACK)),HGX_GUARD);
 r->phase=2;r->vote.next_sequence=*sequence;const uint32_t payload[]={1,HCO_BOOT_LEVEL,houjiGpuOpps[HCO_BOOT_LEVEL-1].bw_index};
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
