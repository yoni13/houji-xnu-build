/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Four fixed instruction-base bindings only. Never enables SQE, submits RAW,
 * modifies trust or calls PAS. Fresh power/ownership and disabled SQE required.
 */
#ifndef HOUJI_GPU_CODE_BIND_H
#define HOUJI_GPU_CODE_BIND_H
#include "GpuCodeMemory.h"
enum { HCB_OK,HCB_ARGUMENT,HCB_GUARD,HCB_IO,HCB_VERIFY };
struct hcb_result {unsigned code,phase,attempted,verified,retained,reads,writes,last_operation,observed_valid;uint32_t control_before,control_after,last_address,last_value;};
struct hcb_io {void *cookie;int (*read)(void *,uint32_t,uint32_t*);int (*write)(void*,uint32_t,uint32_t);int(*guard)(void*);};
static inline uint32_t hcb_address(unsigned i) {const uint32_t a[]={0x3d020c0,0x3d020c4,0x3d02dc0,0x3d02dc4};return i<4?a[i]:0;}
static inline uint32_t hcb_value(unsigned i) {const uint32_t v[]={HCC_IOVA+HCC_SQE_OFFSET,0,HCC_IOVA+HCC_AQE_OFFSET,0};return i<4?v[i]:0;}
static inline int hcb_read(const struct hcb_io*io,struct hcb_result*r,uint32_t a,uint32_t*v) {
 r->reads++;r->last_address=a;r->last_operation=1;r->observed_valid=0;if(io->guard(io->cookie) || io->read(io->cookie,a,v))return 0;r->last_value=*v;r->observed_valid=1;return 1;
}
static inline unsigned hcb_run(const struct hcb_io*io,struct hcb_result*r) {
 if(!r)return HCB_ARGUMENT;*r=(struct hcb_result){.retained=1};
 if(!io || !io->read || !io->write || !io->guard)return r->code=HCB_ARGUMENT;
#define HCB_REQUIRE(test,error) do {if(!(test))return r->code=(error);} while(0)
 r->phase=1;HCB_REQUIRE(hcb_read(io,r,0x3d02020,&r->control_before) && r->control_before==0,HCB_GUARD);
 for(unsigned i=0;i<4;i++) {
  r->phase=i+2;HCB_REQUIRE(!io->guard(io->cookie),HCB_GUARD);r->attempted|=1u<<i;r->writes++;
  r->last_address=hcb_address(i);r->last_value=hcb_value(i);r->last_operation=2;r->observed_valid=0;
  HCB_REQUIRE(!io->write(io->cookie,hcb_address(i),hcb_value(i)),HCB_IO);
  uint32_t v;HCB_REQUIRE(hcb_read(io,r,hcb_address(i),&v),HCB_IO);HCB_REQUIRE(v==hcb_value(i),HCB_VERIFY);r->verified|=1u<<i;
 }
 r->phase=6;HCB_REQUIRE(hcb_read(io,r,0x3d02020,&r->control_after) && r->control_after==0,HCB_GUARD);return HCB_OK;
#undef HCB_REQUIRE
}
#endif
