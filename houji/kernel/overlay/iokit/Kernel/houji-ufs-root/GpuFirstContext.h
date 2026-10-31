/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * OFFLINE candidate: first A750 private-context submission prefix only.
 * Not integrated; no SMC, GPU start, MMIO write or command submission.
 * Fixed Linux 21a7f44fd0765fcc5194130b28767786eba1d1e7 a6xx_gpu.c:
 * 499-500 initial SYNC|BR; 258-276 thread/reset/LRZ initialization;
 * 297-305 SMMU update; 323-334 post-update sync/cache; 522-526 BOTH/IFPC.
 * Genuine packet parity/fields are checked against pinned generated packers.
 *
 * This is a TOP-LEVEL PRIVILEGED RING prefix, not an unprivileged IB payload.
 * The target may only be the current audited, unchanged bank0 root/ASID.
 * Matching root addresses does NOT justify omitting SMMU_TABLE_UPDATE on a
 * first context: upstream's 244-245 fast path requires established context
 * identity. The candidate therefore always retains the same-root update.
 *
 * Deliberate, source-backed host-profile omissions (not byte equivalence):
 * - 252-256 previous-fence WAIT_TIMESTAMP: actual prior full-idle proof required.
 * - 311-316 TTBR memstore copy: debug/preemption bookkeeping, preemption off.
 * - 279/336 perf SRAM clear+poll: select no-perf-counter-zap branch; the two
 *   get_stats_counter calls at 519-520 are telemetry, not rendering state.
 * - 508/528 save buffers/pseudo registers/IB-list markers: nr_rings=1 and
 *   no preemption/queue preempt flag. No invented save/fence/statistics BO.
 * IFPC remains disabled for this retained test until the existing final
 * stop/reset. No power-down or resource-release inference is made here.
 * Thread/reset/IFPC retention is source equivalence, not proof of r133 cause.
 */
#ifndef HOUJI_GPU_FIRST_CONTEXT_H
#define HOUJI_GPU_FIRST_CONTEXT_H
#include <stdint.h>
enum {HFC_OK,HFC_ARGUMENT,HFC_REUSED,HFC_QUALIFICATION,HFC_OWNER,HFC_READ,HFC_ROOT};
enum {HFC_WORDS=25,HFC_ASID=0x751};
struct hfc_config {
 uint64_t root_physical,built_root_physical;
 unsigned same_boot_owned,prior_full_idle,mode_exit_idle,single_context,preemption_disabled;
};
struct hfc_io {
 void *cookie;
 int (*owned)(void *); /* Fresh complete live lease, including root/table/route/fault proof; zero success. */
 int (*read_root)(void *,uint32_t,uint64_t *); /* Only fixed bank0 TTBR0/TTBR1. */
};
struct hfc_result {
 unsigned started,code,phase,guards,reads,valid,ready;
 int owner_code,read_code;
 uint64_t ttbr0,ttbr1;
};
static inline uint32_t hfc_pkt7(unsigned op,unsigned n) {
 unsigned a=op,b=n,pa=1,pb=1;for(;a;a>>=1)pa^=a&1;for(;b;b>>=1)pb^=b&1;
 return 0x70000000u|(pa<<23)|(pb<<15)|(op<<16)|n;
}
/* Pure canonical encoder: the packet image is never its own expected value.
 * Root qualification belongs to hfc_build/native owner; no arbitrary opcode,
 * address, ASID or context bank is accepted here. Unknown indices return zero.
 */
static inline uint32_t hfc_word(uint64_t root,unsigned index) {
 const uint32_t candidate[]={
  hfc_pkt7(0x17,1),0x80000001, /* a7xx_submit initial SYNC|BR */
  hfc_pkt7(0x17,1),0x80000003,
  hfc_pkt7(0x1f,1),0xf,
  hfc_pkt7(0x17,1),0x80000003,
  hfc_pkt7(0x46,1),0x26, /* Linux LRZ_FLUSH_INVALIDATE */
  hfc_pkt7(0x17,1),0x80000001,
  hfc_pkt7(0x53,4),(uint32_t)root,
   (uint32_t)(root>>32)|((uint32_t)HFC_ASID<<16),0,0,
  hfc_pkt7(0x17,1),0x80000001,
  hfc_pkt7(0x46,1),0x31, /* Linux CACHE_INVALIDATE; not A7 CCU_CLEAN_COLOR */
  hfc_pkt7(0x17,1),3,
  hfc_pkt7(0x65,1),0x101 /* IFPC disable */
 };
 return index<HFC_WORDS?candidate[index]:0;
}
static inline int hfc_qualified(const struct hfc_config *c) {
 return c && c->root_physical && !(c->root_physical&4095) &&
  c->root_physical<(UINT64_C(1)<<36) && c->root_physical==c->built_root_physical &&
  c->same_boot_owned==1 && c->prior_full_idle==1 && c->mode_exit_idle==1 &&
  c->single_context==1 && c->preemption_disabled==1;
}
static inline unsigned hfc_guard(const struct hfc_io *io,struct hfc_result *r) {
 r->guards++;r->owner_code=io->owned(io->cookie);return r->owner_code?HFC_OWNER:HFC_OK;
}
static inline unsigned hfc_build(const struct hfc_io *io,const struct hfc_config *c,
 uint32_t *words,unsigned capacity,struct hfc_result *r) {
 if(!r)return HFC_ARGUMENT;
 if(r->started)return HFC_REUSED;
 *r=(struct hfc_result){.started=1};
 if(!io || !io->owned || !io->read_root || !words || capacity!=HFC_WORDS)return r->code=HFC_ARGUMENT;
 if(!hfc_qualified(c))return r->code=HFC_QUALIFICATION;
 r->phase=1;r->code=hfc_guard(io,r);if(r->code)return r->code;
 r->reads++;r->read_code=io->read_root(io->cookie,0x3dc0020,&r->ttbr0);
 if(r->read_code){r->ttbr0=0;return r->code=HFC_READ;}r->valid|=1;
 r->code=hfc_guard(io,r);if(r->code)return r->code;
 r->reads++;r->read_code=io->read_root(io->cookie,0x3dc0028,&r->ttbr1);
 if(r->read_code){r->ttbr1=0;return r->code=HFC_READ;}r->valid|=2;
 r->code=hfc_guard(io,r);if(r->code)return r->code;
 if(r->ttbr0!=((uint64_t)HFC_ASID<<48|c->root_physical) ||
  r->ttbr1!=((uint64_t)HFC_ASID<<48))return r->code=HFC_ROOT;
 r->phase=2;
 r->code=hfc_guard(io,r);if(r->code)return r->code;
 for(unsigned i=0;i<HFC_WORDS;i++)words[i]=hfc_word(c->root_physical,i);
 r->ready=1;r->phase=3;return HFC_OK;
}
#endif
