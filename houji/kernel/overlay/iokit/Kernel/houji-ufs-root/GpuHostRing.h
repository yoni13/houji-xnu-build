/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * A750/gen7 host ring: fixed initialization and two device-written canaries.
 * Linux 21a7f44 a7xx_cp_init / OnePlus 453d8875 gen7_cp_init_cmds.
 * No arbitrary packet, address, shader, PAS or user submission interface.
 */
#ifndef HOUJI_GPU_HOST_RING_H
#define HOUJI_GPU_HOST_RING_H
#include <stdint.h>
#ifdef HOUJI_CALL_TRACE
#define HOUJI_RING_TRACE(step,address,value) HOUJI_CALL_TRACE(step,address,value)
#else
#define HOUJI_RING_TRACE(step,address,value) ((void)0)
#endif
enum {HHR_RING=0x620c0000,HHR_SCRATCH=0x620c8000,HHR_RECORD=0x6303f000,
 HHR_BYTES=0x8000,HHR_WORDS=HHR_BYTES/4,HHR_POLLS=10000};
enum {HHR_OK,HHR_ARGUMENT,HHR_GUARD,HHR_MEMORY,HHR_APERTURE,HHR_IO,HHR_VERIFY,
 HHR_CHECKPOINT,HHR_TIMEOUT,HHR_FAULT,HHR_WRITEBACK,HHR_POST_INIT};
struct hhr_result {
 unsigned retained;
 unsigned started,code,phase,configured,enabled,kicks,polls,init_idle,writebacks,stopped;
 unsigned reads,writes,last_valid,last_operation,fence_retries,busy_valid;
 uint32_t last_address,last_value,wptr,rptr,status,irq,hwfault,sqe,busy,observed[2];
 unsigned post_init_attempted,post_init_complete;
};
struct hhr_io {
 void *cookie;
 int (*guard)(void *); /* Fresh pinned context / power / fault proof, no SQE=0 assumption. */
 int (*prepare)(void *);
 int (*aperture)(void *);
 int (*read)(void *,uint32_t,uint32_t *);
 int (*write)(void *,uint32_t,uint32_t);
 int (*publish)(void *,unsigned,const uint32_t *,unsigned);
 int (*peek)(void *,unsigned,uint32_t *);
 int (*delay)(void *,unsigned);
 int (*checkpoint)(void *); /* Before any SQE enable. */
 void (*snapshot)(void *,unsigned); /* RAM/console only; never filesystem. */
 int (*stop)(void *,uint32_t); /* SQE halt + readback + pointer/idle/fault proof. */
 int (*engine_probe)(void *); /* Native profile validates live start before CP_INIT; no required halt/save/restart. */
 int (*post_init)(void *,struct hhr_result *); /* Optional board-owned authenticated transition, after actual idle. */
 int (*retain_live)(void *,struct hhr_result *); /* Exact same-owner success only, zero means stop. */
};
static inline uint32_t hhr_pkt7(unsigned op,unsigned count) {
 unsigned a=count,b=op,pa=1,pb=1;
 for(;a;a>>=1)pa^=a&1;
 for(;b;b>>=1)pb^=b&1;
 return 0x70000000u|(pa<<15)|(pb<<23)|(op<<16)|count;
}
static inline int hhr_read(const struct hhr_io *io,struct hhr_result *r,uint32_t a,uint32_t *v) {
 r->reads++;r->last_address=a;r->last_operation=1;r->last_valid=0;
 if(io->read(io->cookie,a,v))return 1;r->last_value=*v;r->last_valid=1;return 0;
}
static inline int hhr_write(const struct hhr_io *io,struct hhr_result *r,uint32_t a,uint32_t v) {
 r->writes++;r->last_address=a;r->last_value=v;r->last_operation=2;r->last_valid=0;
 return io->write(io->cookie,a,v);
}
static inline unsigned hhr_idle(const struct hhr_io *io,struct hhr_result *r) {
 for(unsigned i=0;i<HHR_POLLS;i++) {
  r->polls++;r->busy_valid=0;
  if(io->guard(io->cookie))return HHR_GUARD;
  if(hhr_read(io,r,0x3d02018,&r->rptr) || hhr_read(io,r,0x3d00840,&r->status) ||
   hhr_read(io,r,0x3d00804,&r->irq) || hhr_read(io,r,0x3d02084,&r->hwfault))return HHR_IO;
  if(r->irq || r->hwfault)return HHR_FAULT;
  if(r->rptr==r->wptr && !(r->status&~1u)) {
   if(hhr_read(io,r,0x3d8ec30,&r->busy))return HHR_IO;
   r->busy_valid=1;
   if(!(r->busy&(1u<<23)))return HHR_OK;
  }
  if(io->delay(io->cookie,100))return HHR_IO;
 }
 return HHR_TIMEOUT;
}
static inline unsigned hhr_kick(const struct hhr_io *io,struct hhr_result *r) {
 uint32_t fence=0;
 for(unsigned i=0;i<200;i++) {
  if(io->guard(io->cookie))return HHR_GUARD;
  if(hhr_write(io,r,0x3d0201c,r->wptr) || hhr_read(io,r,0x3d8ec4c,&fence))return HHR_IO;
  if(!(fence&1)){r->kicks++;return HHR_OK;}
  r->fence_retries++;if(io->delay(io->cookie,10))return HHR_IO;
 }
 return HHR_TIMEOUT;
}
static inline unsigned hhr_run(const struct hhr_io *io,struct hhr_result *r) {
 if(!r)return HHR_ARGUMENT;
 if(r->started)return HHR_GUARD;
 *r=(struct hhr_result){.started=1};
 if(!io || !io->guard || !io->prepare || !io->aperture || !io->read || !io->write ||
  !io->publish || !io->peek || !io->delay || !io->checkpoint || !io->snapshot || !io->stop || !io->engine_probe)return r->code=HHR_ARGUMENT;
#define HHR_REQUIRE(x,e) do {if(!(x)){r->code=(e);goto stop;}}while(0)
 const uint32_t a[]={0x3d02000,0x3d02004,0x3d02008,0x3d02010,0x3d02014,0x3d02a60,0x3d02a64};
 const uint32_t v[]={HHR_RING,0,0x20c,HHR_SCRATCH+0x100,0,HHR_SCRATCH+0x108,0};
 const uint32_t init[]={0x70970001,0x08000000,0x70c80007,0x94b,3,0x20000000,2,HHR_RECORD,0,0x80000000};
 int Checkpoint=0,Guard=0,Store=0,Read=0,Engine=0;
 r->phase=1;HHR_REQUIRE(!io->guard(io->cookie),HHR_GUARD);
 HHR_REQUIRE(!io->prepare(io->cookie),HHR_MEMORY);
 HHR_REQUIRE(!io->aperture(io->cookie),HHR_APERTURE);
 r->phase=2;
 for(unsigned i=0;i<7;i++) {
  uint32_t value;HHR_REQUIRE(!io->guard(io->cookie),HHR_GUARD);
  HHR_REQUIRE(!hhr_write(io,r,a[i],v[i]) && !hhr_read(io,r,a[i],&value),HHR_IO);
  HHR_REQUIRE(value==v[i],HHR_VERIFY);r->configured++;
 }
 HHR_REQUIRE(!hhr_read(io,r,0x3d02018,&r->rptr) && !hhr_read(io,r,0x3d0201c,&r->wptr) &&
  !hhr_read(io,r,0x3d02020,&r->sqe),HHR_IO);
 HHR_REQUIRE(!r->rptr && !r->wptr && !r->sqe,HHR_VERIFY);
 // Fixed mask 94b: vendor bit11 DISABLES unsupported DMS; not SMMU-info enable.
 HHR_REQUIRE(!io->publish(io->cookie,0,init,10),HHR_MEMORY);
 r->phase=3;io->snapshot(io->cookie,0);
 HOUJI_RING_TRACE(0x3120,0,r->phase);
 Checkpoint=io->checkpoint(io->cookie);HOUJI_RING_TRACE(0x3121,0,Checkpoint);
 HHR_REQUIRE(!Checkpoint,HHR_CHECKPOINT);
 HOUJI_RING_TRACE(0x3122,0,r->phase);
 Guard=io->guard(io->cookie);HOUJI_RING_TRACE(0x3123,0,Guard);
 HHR_REQUIRE(!Guard,HHR_GUARD);
 // Attempted is set BEFORE the write: failure cannot reopen filesystem access.
 r->enabled=1;HOUJI_RING_TRACE(0x3124,0x3d02020,1);
 Store=hhr_write(io,r,0x3d02020,1);HOUJI_RING_TRACE(0x3125,0x3d02020,Store);
 HHR_REQUIRE(!Store,HHR_IO);
 HOUJI_RING_TRACE(0x3126,0x3d02020,0);
 Read=hhr_read(io,r,0x3d02020,&r->sqe);HOUJI_RING_TRACE(0x3127,0x3d02020,Read);
 HHR_REQUIRE(!Read,HHR_IO);
 HHR_REQUIRE(r->sqe==1,HHR_VERIFY);
 HOUJI_RING_TRACE(0x3128,0,r->sqe);
 Engine=io->engine_probe(io->cookie);HOUJI_RING_TRACE(0x3129,0,Engine);
 HHR_REQUIRE(!Engine,HHR_IO);
 r->phase=4;r->wptr=10;r->code=hhr_kick(io,r);if(r->code)goto stop;
 r->code=hhr_idle(io,r);io->snapshot(io->cookie,1);if(r->code)goto stop;r->init_idle=1;
 if(io->post_init) {
  r->post_init_attempted=1;
  HHR_REQUIRE(!io->post_init(io->cookie,r),HHR_POST_INIT);
  r->post_init_complete=1;
 }
 // Separate submissions and distinct CPU-observed GPU words prevent a stale
 // rptr, firmware counter or CPU store from being mistaken for GPU execution.
 for(unsigned i=0;i<2;i++) {
  r->phase=5+i;uint32_t expected=0xc0ffee00u+i;
  const uint32_t packet[]={hhr_pkt7(0x3d,3),HHR_SCRATCH+4*i,0,expected,hhr_pkt7(0x12,0)};
  HHR_REQUIRE(!io->publish(io->cookie,r->wptr,packet,5),HHR_MEMORY);
  r->wptr+=5;r->code=hhr_kick(io,r);if(r->code)goto stop;
  r->code=hhr_idle(io,r);if(r->code)goto stop;
  HHR_REQUIRE(!io->peek(io->cookie,i,&r->observed[i]),HHR_MEMORY);
  HHR_REQUIRE(r->observed[i]==expected,HHR_WRITEBACK);r->writebacks++;
  io->snapshot(io->cookie,2+i);
 }
 r->phase=7;
stop:
 io->snapshot(io->cookie,4);
 if(!r->code && io->retain_live && io->retain_live(io->cookie,r)==1)r->retained=1;
 if(r->retained)r->stopped=0;
 else if(r->enabled)r->stopped=!io->stop(io->cookie,r->wptr);
 else r->stopped=1;
 if(!r->code && !r->stopped && !r->retained)r->code=HHR_GUARD;
 if(!r->code)r->phase=8;
#undef HHR_REQUIRE
 return r->code;
}
#undef HOUJI_RING_TRACE
#endif
