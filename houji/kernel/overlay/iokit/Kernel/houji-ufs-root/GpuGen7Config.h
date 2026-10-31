/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Fixed disabled-SQE register configuration and owned powerup record. No CP
 * submission or security service. All observations retain validity explicitly.
 */
#ifndef HOUJI_GPU_GEN7_CONFIG_H
#define HOUJI_GPU_GEN7_CONFIG_H
#include <stddef.h>
#include "GpuGen7Table.h"
enum {H7C_OK,H7C_ARGUMENT,H7C_GUARD,H7C_IO,H7C_VERIFY,H7C_TIMEOUT,H7C_PUBLISH};
struct h7c_record {uint32_t before,expected,after;unsigned valid,written,polls;};
struct h7c_result {
 unsigned code,phase,step,completed,reads,writes,retained,power_pairs,record_ready;
 uint32_t last_address,last_value,control_after,record_crc;
 unsigned last_operation,observed_valid;
 struct h7c_record records[H7C_OPS];uint32_t power_values[H7C_PAIRS];
};
struct h7c_io {
 void *cookie;int (*guard)(void *);int (*read)(void *,uint32_t,uint32_t *);
 int (*write)(void *,uint32_t,uint32_t);int (*delay)(void *,unsigned);
 int (*publish)(void *,const uint32_t *,unsigned);
};
static inline int h7c_read(const struct h7c_io *io,struct h7c_result *r,uint32_t a,uint32_t *v) {
 r->reads++;r->last_address=a;r->last_operation=1;r->observed_valid=0;
 if(io->guard(io->cookie) || io->read(io->cookie,a,v))return 0;
 r->last_value=*v;r->observed_valid=1;return 1;
}
static inline unsigned h7c_run(const struct h7c_io *io,struct h7c_result *r) {
 if(!r)return H7C_ARGUMENT;*r=(struct h7c_result){.retained=1};
 if(!io || !io->guard || !io->read || !io->write || !io->delay || !io->publish)return r->code=H7C_ARGUMENT;
#define H7C_REQUIRE(x,e) do {if(!(x))return r->code=(e);} while(0)
 r->phase=1;H7C_REQUIRE(!io->guard(io->cookie),H7C_GUARD);
 for(unsigned i=0;i<H7C_OPS;i++) {
  r->phase=2;r->step=i;const struct h7c_op *op=&h7c_ops[i];struct h7c_record *record=&r->records[i];
  record->expected=op->value;record->valid=2;
  if(op->kind==1) {
   H7C_REQUIRE(h7c_read(io,r,op->address,&record->before),H7C_IO);record->valid|=1;
   record->expected=(record->before&~op->mask)|op->value;
  }
  if(op->kind==3) {
   for(unsigned poll=0;poll<H7C_POLLS;poll++) {
    record->polls++;H7C_REQUIRE(!io->delay(io->cookie,1),H7C_IO);
    H7C_REQUIRE(h7c_read(io,r,op->address,&record->after),H7C_IO);record->valid|=4;
    if((record->after&op->verify)==op->value)break;
   }
   H7C_REQUIRE((record->after&op->verify)==op->value,H7C_TIMEOUT);
  } else {
   H7C_REQUIRE(!io->guard(io->cookie),H7C_GUARD);r->writes++;r->last_address=op->address;
   r->last_value=record->expected;r->last_operation=2;r->observed_valid=0;record->written=1;
   H7C_REQUIRE(!io->write(io->cookie,op->address,record->expected),H7C_IO);
   // Command registers may self-clear or be write-only; no fabricated readback.
   if(op->kind!=2) {
    H7C_REQUIRE(h7c_read(io,r,op->address,&record->after),H7C_IO);record->valid|=4;
    H7C_REQUIRE(((record->after^record->expected)&op->verify)==0,H7C_VERIFY);
   }
  }
  r->completed=i+1;
 }
 r->phase=3;uint32_t words[H7C_WORDS]={0};words[3]=H7C_IFPC|(H7C_PREEMPT<<8);
 for(unsigned i=0;i<H7C_PAIRS;i++) {
  H7C_REQUIRE(h7c_read(io,r,h7c_power_regs[i].address,&r->power_values[i]),H7C_IO);
  words[4+i*2]=(h7c_power_regs[i].address-0x3d00000)/4;words[5+i*2]=r->power_values[i];r->power_pairs=i+1;
 }
 r->phase=4;H7C_REQUIRE(!io->guard(io->cookie),H7C_GUARD);
 H7C_REQUIRE(!io->publish(io->cookie,words,H7C_WORDS),H7C_PUBLISH);r->record_ready=1;
 r->phase=5;H7C_REQUIRE(h7c_read(io,r,0x3d02020,&r->control_after) && !r->control_after,H7C_GUARD);
 r->phase=6;return H7C_OK;
#undef H7C_REQUIRE
}
#endif
