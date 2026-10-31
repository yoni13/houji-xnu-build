/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Fixed stock signed houji ZAP loader policy. No allocation, cleanup, CPU
 * page-table operation, GPU mode packet, TRUST write, or general firmware API.
 *
 * OnePlus modules453d kgsl_util.c:108-175 + kernel d763 qcom_scm.c:723-831;
 * pinned Linux21a7 mdt_loader.c:231-295,336-439. Exact source/reservation
 * evidence is in native/gpu/zap-load-source-plan.json. Assets are generated
 * only from SHA256 ab6035de...1981931e by make-gpu-zap-assets.py.
 */
#ifndef HOUJI_GPU_ZAP_LOAD_H
#define HOUJI_GPU_ZAP_LOAD_H
#include <stdint.h>
#include "GpuPasProbe.h"
#include "GpuZapAssets.h"

enum { HZL_PHASE_NONE,HZL_PHASE_QUALIFY,HZL_PHASE_METADATA,HZL_PHASE_PUBLISH,
 HZL_PHASE_INIT,HZL_PHASE_MEM_SETUP,HZL_PHASE_COPY,HZL_PHASE_AUTH,HZL_PHASE_DONE };
enum { HZL_OK,HZL_ARGUMENT,HZL_ALREADY_STARTED,HZL_QUALIFICATION,HZL_GUARD,
 HZL_ASSETS,HZL_METADATA,HZL_PUBLISH,HZL_SCM,HZL_SECURE_RESULT,HZL_COPY };
#define HZL_RESERVATION_BASE UINT64_C(0x9b09a000)
enum { HZL_PAS_ID=13,HZL_RESERVATION_BYTES=0x2000,HZL_LOAD_SPAN=0x1000,
 HZL_METADATA_CAPACITY=0x1000,HZL_METADATA_BYTES=4044,HZL_SEGMENT_BYTES=1072 };
struct hzl_config {
 void *metadata_cpu;
 uint64_t metadata_physical;
 uint32_t metadata_capacity;
 uint64_t reservation_base;
 uint32_t reservation_bytes;
};
struct hzl_io {
 void *cookie;
 /* exactly 1 only with current epoch/owner, CP initialization+idle, held
  * OOB/power, reservation/no-overlap and owned metadata proved by the owner.
  * AUTH and later guards MUST NOT read or remap the reserved image range. */
 int (*guard)(void *,unsigned phase);
 int (*publish_metadata)(void *,void *cpu,uint64_t physical,unsigned bytes);
 /* Exact fixed destination and immutable source. The owner must publish the
  * stores and irreversibly end THIS adapter's CPU access before returning.
  * This is a software authority boundary, not a claim of hardware revoke. */
 int (*copy_segment)(void *,uint64_t,const uint8_t *,unsigned);
 int (*call)(void *,struct ggs_regs *);
 int (*delay_ms)(void *,unsigned);
};
struct hzl_result {
 unsigned started,phase,code,retained,metadata_copied,metadata_published;
 unsigned init_attempted,init_succeeded,mem_setup_attempted,mem_setup_succeeded;
 unsigned segment_copy_attempted,segment_published,auth_attempted,authenticated;
 unsigned actual_calls,guard_failed;
 int publish_code,copy_code;
 struct hpp_observation call[3];
 struct ggs_state transport;
};
struct hzl_context {
 const struct hzl_io *io;
 struct hzl_result *result;
 struct ggs_regs response;
 unsigned response_present;
 int response_transport;
};
static inline uint32_t hzl_crc32(const uint8_t *p,unsigned bytes) {
 uint32_t c=UINT32_MAX;
 for(unsigned i=0;i<bytes;i++) {
  c^=p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);
 }
 return ~c;
}
static inline int hzl_overlap(uint64_t a,uint64_t an,uint64_t b,uint64_t bn) {
 return a<=b?b-a<an:a-b<bn;
}
static inline int hzl_config_valid(const struct hzl_config *c) {
 if(!c || !c->metadata_cpu || ((uintptr_t)c->metadata_cpu&4095u) ||
  (uintptr_t)c->metadata_cpu>UINTPTR_MAX-4095u ||
  !c->metadata_physical || (c->metadata_physical&4095u) ||
  c->metadata_physical>UINT64_MAX-4095u || c->metadata_capacity!=HZL_METADATA_CAPACITY ||
  c->reservation_base!=HZL_RESERVATION_BASE || c->reservation_bytes!=HZL_RESERVATION_BYTES ||
  hzl_overlap(c->metadata_physical,HZL_METADATA_CAPACITY,HZL_RESERVATION_BASE,HZL_RESERVATION_BYTES) ||
  hzl_overlap((uintptr_t)c->metadata_cpu,HZL_METADATA_CAPACITY,
   (uintptr_t)mHoujiZapMetadata,sizeof mHoujiZapMetadata) ||
  hzl_overlap((uintptr_t)c->metadata_cpu,HZL_METADATA_CAPACITY,
   (uintptr_t)mHoujiZapSegment,sizeof mHoujiZapSegment))return 0;
 return 1;
}
static inline int hzl_capabilities(const struct ggs_state *q,const struct hpp_result *p) {
 if(!q || q->convention_verified!=1 || q->available!=1 || q->init_succeeded!=1 ||
  q->quarantined || !p || p->started!=1 || p->phase!=HPP_QUERY_COUNT+1 || p->code ||
  p->completed!=HPP_QUERY_COUNT || p->attempted_mask!=31 || p->valid_mask!=31 ||
  p->available_mask!=15 || p->supported_valid!=1 || p->supported!=1 ||
  p->actual_calls<HPP_QUERY_COUNT || p->guard_failed || p->transport.quarantined)return 0;
 for(unsigned i=0;i<HPP_QUERY_COUNT;i++) {
  struct ggs_regs expected;
  const struct hpp_observation *o=&p->query[i];
  if(hpp_frame(i,&expected) || !o->attempted || !o->valid || !o->actual_calls ||
   o->transport_code || !o->response_present || o->response_transport ||
   o->response.x[0] || !o->response.x[1])return 0;
  for(unsigned j=0;j<8;j++)if(o->request.x[j]!=expected.x[j])return 0;
 }
 return 1;
}
static inline int hzl_guard(struct hzl_context *c) {
 if(c->io->guard(c->io->cookie,c->result->phase)==1)return 0;
 c->result->guard_failed=1;return 1;
}
static inline int hzl_call(void *cookie,struct ggs_regs *frame) {
 struct hzl_context *c=(struct hzl_context*)cookie;
 if(hzl_guard(c))return 1;
 ++c->result->actual_calls;
 int code=c->io->call(c->io->cookie,frame);
 c->response=*frame;c->response_present=1;c->response_transport=code;
 int guard_code=hzl_guard(c);return code?code:guard_code;
}
static inline int hzl_delay(void *cookie,unsigned milliseconds) {
 struct hzl_context *c=(struct hzl_context*)cookie;
 if(milliseconds!=GGS_BUSY_DELAY_MS || hzl_guard(c))return 1;
 int code=c->io->delay_ms(c->io->cookie,milliseconds);
 int guard_code=hzl_guard(c);return code?code:guard_code;
}
static inline unsigned hzl_exchange(struct hzl_context *c,unsigned index,
 const struct ggs_regs *request) {
 struct hzl_result *r=c->result;struct hpp_observation *o=&r->call[index];
 const struct ggs_ops io={c,hzl_call,hzl_delay,0};
 o->request=*request;o->attempted=1;c->response_present=0;c->response_transport=0;
 unsigned before=r->actual_calls;
 int code=ggs_exchange_frame(&io,&r->transport,request);
 o->actual_calls=r->actual_calls-before;o->transport_code=(unsigned)code;
 o->last_input=r->transport.last_input;o->response_present=c->response_present;
 o->response_transport=c->response_transport;if(o->response_present)o->response=c->response;
 if(code)return r->guard_failed?HZL_GUARD:HZL_SCM;
 if(!o->response_present || o->response_transport)return HZL_SCM;
 // Unlike PAS_SUPPORTED, these three service returns require a1==0.
 if(o->response.x[1])return HZL_SECURE_RESULT;
 o->valid=1;return HZL_OK;
}
static inline unsigned hzl_run(const struct hzl_io *io,const struct hzl_config *config,
 const struct ggs_state *qualified,const struct hpp_result *capabilities,struct hzl_result *r) {
 if(!r)return HZL_ARGUMENT;
 if(r->started)return HZL_ALREADY_STARTED;
 if(!io || !io->guard || !io->publish_metadata || !io->copy_segment || !io->call ||
  !io->delay_ms || !hzl_config_valid(config))return r->code=HZL_ARGUMENT;
 if(!hzl_capabilities(qualified,capabilities))return r->code=HZL_QUALIFICATION;
 *r=(struct hzl_result){.started=0};r->started=1;r->phase=HZL_PHASE_QUALIFY;
 r->transport.convention_verified=1;
 struct hzl_context context={.io=io,.result=r};
 if(hzl_guard(&context))return r->code=HZL_GUARD;
 if(sizeof mHoujiZapMetadata!=HZL_METADATA_BYTES || sizeof mHoujiZapSegment!=HZL_SEGMENT_BYTES ||
  hzl_crc32(mHoujiZapMetadata,HZL_METADATA_BYTES)!=0xc73aee66u ||
  hzl_crc32(mHoujiZapSegment,HZL_SEGMENT_BYTES)!=0xef6cc69bu)return r->code=HZL_ASSETS;
 r->phase=HZL_PHASE_METADATA;if(hzl_guard(&context))return r->code=HZL_GUARD;
 uint8_t *metadata=(uint8_t*)config->metadata_cpu;
 for(unsigned i=0;i<HZL_METADATA_BYTES;i++)metadata[i]=mHoujiZapMetadata[i];
 r->metadata_copied=1;if(hzl_guard(&context))return r->code=HZL_GUARD;
 r->phase=HZL_PHASE_PUBLISH;if(hzl_guard(&context))return r->code=HZL_GUARD;
 r->publish_code=io->publish_metadata(io->cookie,metadata,config->metadata_physical,HZL_METADATA_BYTES);
 if(hzl_guard(&context))return r->code=HZL_GUARD;
 if(r->publish_code)return r->code=HZL_PUBLISH;
 for(unsigned i=0;i<HZL_METADATA_BYTES;i++)if(metadata[i]!=mHoujiZapMetadata[i])return r->code=HZL_METADATA;
 r->metadata_published=1;
 r->phase=HZL_PHASE_INIT;
 if(hzl_guard(&context))return r->code=HZL_GUARD;
 // Even a transport/guard failure is not proof INIT_IMAGE had no effects.
 r->init_attempted=1;r->retained=1;
 const struct ggs_regs init={{0x42000201u,0x82,HZL_PAS_ID,config->metadata_physical,0,0,0,0}};
 unsigned code=hzl_exchange(&context,0,&init);if(code)return r->code=code;
 r->init_succeeded=1;r->phase=HZL_PHASE_MEM_SETUP;r->mem_setup_attempted=1;
 const struct ggs_regs setup={{0x42000202u,3,HZL_PAS_ID,HZL_RESERVATION_BASE,HZL_LOAD_SPAN,0,0,0}};
 code=hzl_exchange(&context,1,&setup);if(code)return r->code=code;
 r->mem_setup_succeeded=1;r->phase=HZL_PHASE_COPY;
 if(hzl_guard(&context))return r->code=HZL_GUARD;
 r->segment_copy_attempted=1;
 r->copy_code=io->copy_segment(io->cookie,HZL_RESERVATION_BASE,mHoujiZapSegment,HZL_SEGMENT_BYTES);
 if(hzl_guard(&context))return r->code=HZL_GUARD;
 if(r->copy_code)return r->code=HZL_COPY;
 r->segment_published=1;r->phase=HZL_PHASE_AUTH;r->auth_attempted=1;
 const struct ggs_regs auth={{0x42000205u,1,HZL_PAS_ID,0,0,0,0,0}};
 code=hzl_exchange(&context,2,&auth);if(code)return r->code=code;
 r->authenticated=1;r->phase=HZL_PHASE_DONE;
 // No reserved-memory read, reset, zero, unmap or free is performed here.
 return r->code=HZL_OK;
}
#endif
