/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Fixed query-only PAS capability probe. No firmware pointers, INIT_IMAGE,
 * MEM_SETUP, AUTH_RESET, shutdown, secure-mode packet, or register write.
 *
 * ABI: OnePlusOSS/android_kernel_oneplus_sm8650 at
 * d763ed47deec55451366c1187f813f2f309d88a7, qcom_scm.c:333-363,
 * 898-918 and qcom_scm-smc.c:209-221,284-286. Full source/load audit:
 * native/gpu/zap-load-source-plan.json. A supported service is NOT evidence
 * that this boot authenticated ZAP or entered non-secure graphics mode.
 *
 * Reuses the existing bounded returned-status transport. Its retry bounds
 * cannot cancel a single SMC that does not return. The owner must run this
 * while its ordinary, safe pre-SQE power/context guards still apply.
 */
#ifndef HOUJI_GPU_PAS_PROBE_H
#define HOUJI_GPU_PAS_PROBE_H
#include <stdint.h>
#ifndef GIULIA_GPU_SCM_H
#include "vendor/Ace5Gmu/giulia_gpu_scm.h"
#endif

enum { HPP_QUERY_COUNT=5, HPP_AVAILABILITY_COUNT=4 };
enum { HPP_OK, HPP_ARGUMENT, HPP_PREREQUISITE, HPP_CONVENTION,
 HPP_GUARD, HPP_SCM, HPP_ALREADY_STARTED };
struct hpp_io {
 void *cookie;
 int (*guard)(void *); /* exactly 1: owned, safe, same-boot pre-SQE state */
 int (*call)(void *,struct ggs_regs *);
 int (*delay_ms)(void *,unsigned);
};
struct hpp_observation {
 unsigned attempted,valid,actual_calls,transport_code,response_present;
 int response_transport;
 struct ggs_regs request,last_input,response;
};
struct hpp_result {
 unsigned started,phase,code,completed,attempted_mask,valid_mask,available_mask;
 unsigned supported_valid,supported,actual_calls,guard_failed;
 struct hpp_observation query[HPP_QUERY_COUNT];
 struct ggs_state transport;
};
struct hpp_context {
 const struct hpp_io *io;
 struct hpp_result *result;
 struct ggs_regs response;
 unsigned response_present;
 int response_transport;
};

static inline int hpp_guard(struct hpp_context *c) {
 if(c->io->guard(c->io->cookie)==1)return 0;
 c->result->guard_failed=1;
 return 1;
}
static inline int hpp_call(void *cookie,struct ggs_regs *frame) {
 struct hpp_context *c=(struct hpp_context *)cookie;
 if(hpp_guard(c))return 1;
 ++c->result->actual_calls;
 int code=c->io->call(c->io->cookie,frame);
 c->response=*frame;c->response_present=1;c->response_transport=code;
 int guard_code=hpp_guard(c);
 return code?code:guard_code;
}
static inline int hpp_delay(void *cookie,unsigned milliseconds) {
 struct hpp_context *c=(struct hpp_context *)cookie;
 if(milliseconds!=GGS_BUSY_DELAY_MS || hpp_guard(c))return 1;
 int code=c->io->delay_ms(c->io->cookie,milliseconds);
 int guard_code=hpp_guard(c);
 return code?code:guard_code;
}
static inline int hpp_frame(unsigned index,struct ggs_regs *frame) {
 static const uint32_t targets[HPP_AVAILABILITY_COUNT]={
  0x02000207u,0x02000201u,0x02000202u,0x02000205u};
 if(!frame || index>=HPP_QUERY_COUNT)return HPP_ARGUMENT;
 const struct ggs_regs next={{index<HPP_AVAILABILITY_COUNT?0x42000601u:0x42000207u,
  1,index<HPP_AVAILABILITY_COUNT?targets[index]:13,0,0,0,0,0}};
 *frame=next;return HPP_OK;
}

/* The live CX initializer supplies the already-verified convention. This
 * probe never retries convention discovery, initializes CX, or clears an
 * existing quarantine. A started probe cannot be silently rerun after error.
 * An unavailable call is a valid false result; an SMC/transport error is not.
 */
static inline unsigned hpp_run(const struct hpp_io *io,
 const struct ggs_state *qualified,struct hpp_result *r) {
 if(!r)return HPP_ARGUMENT;
 if(r->started)return HPP_ALREADY_STARTED;
 if(!io || !io->guard || !io->call || !io->delay_ms || !qualified)
  return r->code=HPP_ARGUMENT;
 if(qualified->quarantined || qualified->convention_verified!=1 ||
  qualified->available!=1 || qualified->init_succeeded!=1)
  return r->code=HPP_CONVENTION;
 if(io->guard(io->cookie)!=1)return r->code=HPP_PREREQUISITE;
 *r=(struct hpp_result){.started=0};r->started=1;
 r->transport.convention_verified=1;
 struct hpp_context context={.io=io,.result=r};
 const struct ggs_ops transport={&context,hpp_call,hpp_delay,0};
 for(unsigned i=0;i<HPP_QUERY_COUNT;i++) {
  /* A missing PAS_SUPPORTED entry point is not permission to call it. */
  if(i==HPP_AVAILABILITY_COUNT && !(r->available_mask&1u))continue;
  r->phase=i+1;
  struct hpp_observation *o=&r->query[i];
  if(hpp_frame(i,&o->request))return r->code=HPP_ARGUMENT;
  o->attempted=1;r->attempted_mask|=1u<<i;
  context.response_present=0;context.response_transport=0;
  unsigned before=r->actual_calls;
  int code=ggs_exchange_frame(&transport,&r->transport,&o->request);
  o->actual_calls=r->actual_calls-before;
  o->transport_code=(unsigned)code;
  o->last_input=r->transport.last_input;
  o->response_present=context.response_present;
  o->response_transport=context.response_transport;
  if(o->response_present)o->response=context.response;
  if(code)return r->code=r->guard_failed?HPP_GUARD:HPP_SCM;
  /* ggs_exchange_frame requires full-width a0==0, never a truncated test.
   * Only that successful response makes a1 a boolean capability result. */
  o->valid=1;r->valid_mask|=1u<<i;++r->completed;
  if(i<HPP_AVAILABILITY_COUNT) {
   if(o->response.x[1])r->available_mask|=1u<<i;
  } else {
   r->supported_valid=1;r->supported=!!o->response.x[1];
  }
 }
 r->phase=HPP_QUERY_COUNT+1;
 return r->code=HPP_OK;
}
#endif
