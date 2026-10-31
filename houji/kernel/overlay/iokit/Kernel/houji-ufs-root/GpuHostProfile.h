/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Host-ring GMU cold-start protocol, independently encoded from Linux
 * a6xx_hfi_send_perf_table/a6xx_hfi_start. Own houji vote data only.
 * No HWSCHED/AQE feature enable, RAW, CP, PAS or GPU-register operation.
 * Existing 17-queue transport remains unchanged: only queues0/1/2 are used.
 * This establishes message compatibility, not proof of a selected GPU mode.
 */
#ifndef HOUJI_GPU_HOST_PROFILE_H
#define HOUJI_GPU_HOST_PROFILE_H
#include "GpuHfiBootstrap.h"
enum { HHP_STEPS=5,HHP_PERF_WORDS=58 };
static inline int hhp_perf(uint32_t out[HHP_PERF_WORDS]) {
 if(!out || hhb_perf[0] || hhb_perf[1]!=1 || hhb_perf[2]!=2 || hhb_perf[3]!=3 ||
    hhb_perf[10]!=3 || hhb_perf[11]!=2)return 1;
 for(unsigned i=0;i<HHP_PERF_WORDS;i++)out[i]=0;
 out[0]=2;out[1]=3;
 for(unsigned i=0;i<6;i++)out[2+i]=hhb_perf[4+i];
 for(unsigned i=0;i<6;i++)out[50+i]=hhb_perf[12+i];
 return 0;
}
static inline unsigned hhp_run(const struct hhb_io *io,struct hhb_result *r) {
 if(!r)return HHB_ARGUMENT;*r=(struct hhb_result){0};r->next_sequence=2;
 if(!io || !io->healthy || !io->send || !io->receive || !io->doorbell || !io->delay)
  return r->code=HHB_ARGUMENT;
 uint32_t perf[HHP_PERF_WORDS];if(hhp_perf(perf))return r->code=HHB_ARGUMENT;
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
#endif
