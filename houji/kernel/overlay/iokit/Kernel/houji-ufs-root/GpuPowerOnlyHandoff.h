/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * CANDIDATE ONLY: retained SM8650 boot power, never a GPU-resource handoff.
 *
 * Source binding: the actual GpuPowerPolicy/GpuBusProbe and the held-reset
 * PrepareGmuHub in GpuPower.c. Clock decoding uses GpuClockWitness, pinned to
 * MiCode 317e609aa4fb956c3aff565b62215e5a8041ca0c. This file starts no CM3,
 * firmware, DMA, SMMU context or CP, and allocates/maps/releases nothing.
 *
 * Adapter contract (not integrated): at application TPL call existing Prepare
 * with a pristine gpp_report; retain its exact returned status. Under the
 * caller's sole, persistent power/bus ownership call hph_run. read/write/delay
 * may wrap BusRead/BusWrite/Delay; read additionally admits the clock witness
 * whitelist and HFI_CTRL_STATUS. hub wraps PrepareGmuHub, including its checked
 * TPL lowering/restoration. configured wraps GetClockFreqHz, never Measure.
 * exclusive returns zero only while this one-shot owner still excludes other
 * clients; it is not established by a register value or by this record.
 *
 * Every attempted native reference and temporary bus gate is retained to
 * whole-platform reset on success OR uncertainty. There is no release hook.
 * hgb_probe's ordinary cleanup is deliberately denied: the action returns its
 * existing HGB_RETAIN sentinel, and failed paths cannot issue rollback writes.
 * The raw sentinel remains in the record; a separate complete bit qualifies
 * only the power-only stage. A caller must not treat any HGB_RETAIN as success.
 *
 * The wire record contains fixed-width scalars only. No EFI pointers, firmware
 * buffers, table roots, descriptors or GPU IOVAs cross ExitBootServices. XNU
 * must map/register-check afresh and own all later allocations and streams.
 * Configured and nominal/derived clocks are NOT measured frequencies. A valid
 * power-only record does not qualify a firmware boot rate, DMA or GPU use.
 */
#ifndef HOUJI_GPU_POWER_ONLY_HANDOFF_H
#define HOUJI_GPU_POWER_ONLY_HANDOFF_H
#include "GpuBusProbe.h"
#include "GpuClockWitness.h"
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error HPH v1 requires little-endian scalar serialization
#endif

#define HPH_MAGIC UINT32_C(0x31485048) /* HPH1 */
#define HPH_VERSION 1u
#define HPH_PREPARE_STATUS UINT64_C(0x8000000000000007) /* EFI_DEVICE_ERROR */
enum {HPH_OK,HPH_ARGUMENT,HPH_REUSED,HPH_POWER,HPH_OWNER,HPH_BUS,
 HPH_READ,HPH_WRITE,HPH_DELAY,HPH_HUB,HPH_CLOCK,HPH_COLD,HPH_RECORD,HPH_CHANGED};
enum {HPH_PHASE_POWER=1,HPH_PHASE_BUS,HPH_PHASE_HUB,HPH_PHASE_CLOCK,
 HPH_PHASE_FINAL,HPH_PHASE_DONE};
enum {HPH_FIELDS=3,HPH_BUS_VALID=0x7fff,HPH_HFI_VALID=0x8000};
struct hph_cold {
 uint32_t valid,values[HGB_REGS],smr[24],s2cr[24],sctlr[22],hfi;
};
struct hph_clock {
 uint32_t code,stable,valid[2],raw[2][HCW_REGS];
 uint32_t configured_valid,configured_hz[2],configured_status_lo[2],configured_status_hi[2];
 uint32_t source[2],hid[2],derived_valid,derived_lo[2],derived_hi[2];
};
struct hph_record {
 uint32_t magic,version,bytes,started,code,phase,complete,retained;
 uint32_t prepare_status_lo,prepare_status_hi,bus_code,first_bus_code;
 uint32_t action_seen,action_complete,hub_attempted,hub_status_lo,hub_status_hi;
 uint32_t hub_before_valid,hub_before[3];
 uint32_t original_valid,final_valid,attempted,original[3],final[3],mask[3];
 uint32_t first_operation,first_address,first_status_lo,first_status_hi;
 uint32_t power_words[GPP_REPORT_BYTES/4];
 struct hph_cold before,after;
 struct hph_clock clock;
 uint32_t reserved[8],crc32;
};
/* All fields are 32-bit, including explicitly split status values. No host
 * padding or pointer width participates in this little-endian v1 format. */
enum {HPH_RECORD_BYTES=1376};
typedef char hph_u32_width[sizeof(uint32_t)==4?1:-1];
typedef char hph_wire_size[sizeof(struct hph_record)==HPH_RECORD_BYTES?1:-1];
struct hph_io {
 void *cookie;
 GPP_U64 (*read)(void *,GPP_U32,GPP_U32 *);
 GPP_U64 (*write)(void *,GPP_U32,GPP_U32);
 GPP_U64 (*delay)(void *,unsigned);
 GPP_U64 (*hub)(void *,const struct gpp_report *,const struct hgb_result *);
 GPP_U64 (*configured)(void *,unsigned,uint32_t *);
 int (*exclusive)(void *);
};
static inline void hph_copy(void *to,const void *from,unsigned bytes) {
 unsigned char *d=(unsigned char *)to;const unsigned char *s=(const unsigned char *)from;
 for(unsigned i=0;i<bytes;i++)d[i]=s[i];
}
static inline void hph_zero(void *to,unsigned bytes) {
 unsigned char *d=(unsigned char *)to;for(unsigned i=0;i<bytes;i++)d[i]=0;
}
static inline void hph_seal(struct hph_record *r) {
 r->crc32=gpp_crc32(r,HPH_RECORD_BYTES-4);
}
static inline int hph_power_qualified(const struct gpp_report *p,uint64_t status) {
 const uint32_t required=GPP_CFG_INHERITED|GPP_ACD_CONSUMED|GPP_PROVIDER_UNBOUNDED|
  GPP_EXPECTED_VALID|GPP_OBSERVED_VALID|GPP_MASK_VALID;
 const uint32_t known=GPP_CFG_INHERITED|GPP_ACD_CONSUMED|GPP_PROVIDER_UNBOUNDED|
  GPP_EXPECTED_VALID|GPP_OBSERVED_VALID|GPP_MASK_VALID;
 return status==HPH_PREPARE_STATUS && hgb_owned(p) && p->retained==1 &&
  p->operation==GPP_OP_VALIDATE && !p->firmware_status && !p->last_firmware_status &&
  !p->protocol_status && (p->flags&required)==required && !(p->flags&~known) &&
  p->failure_phase==GPP_PHASE_CLOCKS && p->failure_operation==GPP_OP_VALIDATE &&
  p->failure_resource==GPP_GMU_CLK && p->valid_reads==0xc00fu &&
  /* Prepare records the completed qpt close: phase/error/pending all zero.
   * Bit17 only describes the already-consumed publication, not ownership. */
  !(p->values[14]&~0x20000u) && p->values[15]<0x400u && !(p->values[15]&3u) &&
  (p->values[GPP_REG_CFG]&1u) && !(p->values[GPP_REG_CX]&1u) &&
  (p->values[GPP_REG_CX_HW]&0x80000000u) && (p->values[GPP_REG_GMU]&1u) &&
  p->expected==0 && p->mask==1 && p->observed==p->values[GPP_REG_GMU];
}
static inline void hph_bus_copy(struct hph_cold *d,const struct hgb_result *b) {
 d->valid=b->valid;
 for(unsigned i=0;i<HGB_REGS;i++)d->values[i]=b->values[i];
 for(unsigned i=0;i<24;i++){d->smr[i]=b->smr[i];d->s2cr[i]=b->s2cr[i];}
 for(unsigned i=0;i<22;i++)d->sctlr[i]=b->sctlr[i];
}
static inline int hph_cold_qualified(const struct hph_cold *s) {
 if(!s || s->valid!=(HPH_BUS_VALID|HPH_HFI_VALID) || s->hfi ||
  (s->values[HGB_CFG]&0x80000003u)!=1 ||
  (s->values[HGB_MEM]&0x80000003u)!=1 ||
  (s->values[HGB_DVM]&0x80000001u)!=1 ||
  (s->values[HGB_CX]&1) || !(s->values[HGB_HW]&0x80000000u) ||
  (s->values[HGB_AHB]&0x80000000u) || s->values[HGB_RESET]!=1 ||
  (s->values[HGB_VOTE]&0x80000001u)!=1 ||
  s->values[HGB_ID0]!=0x4c017e18 || s->values[HGB_ID1]!=0x40000016 ||
  s->values[HGB_ID2]!=0x5511 || s->values[HGB_GFSR] ||
  s->values[HGB_FWINIT] || s->values[HGB_GXPOWER]!=3)return 0;
 for(unsigned i=0;i<24;i++)if(s->smr[i] || s->s2cr[i]!=0x20000)return 0;
 for(unsigned i=0;i<22;i++)if(s->sctlr[i]&1)return 0;
 return 1;
}
static inline int hph_hub_cold(const struct hph_clock *c) {
 const uint32_t required=(1u<<HCW_PLL0_MODE)-1u;
 if(c->code || c->stable!=1 || (c->valid[0]&required)!=required ||
  (c->valid[1]&required)!=required)return 0;
 for(unsigned at=0;at<2;at++) {
  const uint32_t *v=c->raw[at];
  uint32_t expected=((v[HCW_GMU_CFG]>>8)&7u)==1?(1u<<HCW_REGS)-1u:required;
  if(c->valid[at]!=expected)return 0;
  if((v[HCW_GMU_BRANCH]&0x80000001u)!=1 || (v[HCW_GMU_CMD]&0x80000001u) ||
   (v[HCW_HUB_AON]&0x80000000u) || (v[HCW_HUB_CMD]&0x80000001u) ||
   (v[HCW_HUB_CFG]&0x71fu)!=0x505u)return 0;
 }
 const unsigned stable[]={HCW_GMU_CMD,HCW_GMU_CFG,HCW_HUB_CMD,HCW_HUB_CFG};
 for(unsigned i=0;i<4;i++)if(c->raw[0][stable[i]]!=c->raw[1][stable[i]])return 0;
 /* A successful software query and its value are recorded independently.
  * NotStarted remains unavailable; it is never silently converted to 0 Hz. */
 for(unsigned i=0;i<2;i++) {
  unsigned success=!(c->configured_status_lo[i]|c->configured_status_hi[i]);
  if(((c->configured_valid>>i)&1u)!=success || (!success && c->configured_hz[i]))return 0;
  unsigned cfg=i?HCW_HUB_CFG:HCW_GMU_CFG;
  if(c->source[i]!=((c->raw[1][cfg]>>8)&7u) || c->hid[i]!=(c->raw[1][cfg]&31u))return 0;
 }
 if((c->configured_valid&~3u) || (c->derived_valid&~3u))return 0;
 struct hcw_result decoded;hph_zero(&decoded,sizeof(decoded));decoded.stable=c->stable;
 for(unsigned at=0;at<2;at++) {
  decoded.valid[at]=c->valid[at];
  for(unsigned i=0;i<HCW_REGS;i++)decoded.raw[at][i]=c->raw[at][i];
 }
 for(unsigned i=0;i<2;i++) {
  hcw_derive(&decoded,i);
  uint64_t derived=c->derived_lo[i]|((uint64_t)c->derived_hi[i]<<32);
  if(decoded.derived_valid[i]!=((c->derived_valid>>i)&1u) || decoded.derived_hz[i]!=derived)return 0;
 }
 return 1;
}
struct hph_context {
 const struct hph_io *io;const struct gpp_report *power;
 struct hph_record *record;struct hgb_result *bus;
 unsigned failed;
};
static inline int hph_fail(struct hph_context *c,unsigned code,unsigned op,uint32_t a,uint64_t status) {
 if(!c->record->code) {
  c->record->code=code;c->record->first_operation=op;c->record->first_address=a;
  c->record->first_status_lo=(uint32_t)status;c->record->first_status_hi=(uint32_t)(status>>32);
 }
 c->failed=1;return 1;
}
static inline int hph_owned(struct hph_context *c,unsigned op,uint32_t a) {
 if(c->failed)return 0;
 if(c->bus->code) {
  if(!c->record->first_bus_code)c->record->first_bus_code=c->bus->code;
  return !hph_fail(c,HPH_BUS,op,a,c->bus->code);
 }
 return !c->io->exclusive(c->io->cookie) || !hph_fail(c,HPH_OWNER,op,a,0);
}
static inline int hph_field(uint32_t a) {
 return a==0x171004?0:a==0x171010?1:a==0x3d97000?2:-1;
}
static inline int hph_read_allowed(uint32_t a) {
 for(unsigned i=0;i<HGB_REGS;i++)if(a==hgb_address(i))return 1;
 for(unsigned i=0;i<HCW_REGS;i++)if(a==hcw_address(i))return 1;
 return hgb_survey_address(a) || a==0x3d7e600;
}
static inline GPP_U64 hph_read(void *cookie,GPP_U32 a,GPP_U32 *v) {
 struct hph_context *c=(struct hph_context *)cookie;
 if(!v || !hph_read_allowed(a))return (GPP_U64)hph_fail(c,HPH_READ,1,a,0);
 if(!hph_owned(c,1,a))return 1;
 uint64_t status=c->io->read(c->io->cookie,a,v);
 if(status || *v==UINT32_MAX)return (GPP_U64)hph_fail(c,HPH_READ,1,a,status);
 int field=hph_field(a);
 if(field>=0) {
  unsigned bit=1u<<(unsigned)field;
  if(!(c->record->original_valid&bit)) {
   c->record->original[field]=*v;c->record->original_valid|=bit;
  }
  if(c->record->attempted&bit) {
   c->record->final[field]=*v;c->record->final_valid|=bit;
  }
 }
 return 0;
}
static inline GPP_U64 hph_write(void *cookie,GPP_U32 a,GPP_U32 v) {
 struct hph_context *c=(struct hph_context *)cookie;int field=hph_field(a);
 if(field<0)return (GPP_U64)hph_fail(c,HPH_WRITE,2,a,0);
 if(!hph_owned(c,2,a))return 1;
 unsigned bit=1u<<(unsigned)field;uint32_t mask=field==2?1u:2u,want=field==2?1u:0u;
 /* Exactly one forward write per fixed field. No restore/retry can reach HW. */
 if((c->record->attempted&bit) || (v&mask)!=want ||
  ((v^c->bus->initial[field==0?HGB_CFG:field==1?HGB_MEM:HGB_VOTE])&~(mask|0x80000000u)))
  return (GPP_U64)hph_fail(c,HPH_WRITE,2,a,0);
 c->record->attempted|=bit;c->record->final_valid&=~bit;c->record->retained=1;
 uint64_t status=c->io->write(c->io->cookie,a,v);
 return status?(GPP_U64)hph_fail(c,HPH_WRITE,2,a,status):0;
}
static inline GPP_U64 hph_delay(void *cookie,unsigned us) {
 struct hph_context *c=(struct hph_context *)cookie;
 if(us!=1)return (GPP_U64)hph_fail(c,HPH_DELAY,3,0,0);
 if(!hph_owned(c,3,0))return 1;
 uint64_t status=c->io->delay(c->io->cookie,us);
 return status?(GPP_U64)hph_fail(c,HPH_DELAY,3,0,status):0;
}
static inline int hph_clock_owned(void *cookie,unsigned mode) {
 struct hph_context *c=(struct hph_context *)cookie;return mode || !hph_owned(c,4,0);
}
static inline int hph_clock_read(void *cookie,uint32_t a,uint32_t *v) {
 return hph_read(cookie,a,v)?1:0;
}
static inline uint64_t hph_configured(void *cookie,unsigned which,uint32_t *hz) {
 struct hph_context *c=(struct hph_context *)cookie;
 if(which>1 || !hz)return (GPP_U64)hph_fail(c,HPH_ARGUMENT,5,which,0);
 if(!hph_owned(c,5,which))return 1;
 *hz=0;uint64_t status=c->io->configured(c->io->cookie,which,hz);
 if(status)*hz=0;return status;
}
static inline int hph_survey(struct hph_context *c,struct hph_cold *s) {
 struct hgb_result b;hph_zero(&b,sizeof(b));hph_zero(s,sizeof(*s));
 const struct hgb_io io={c,hph_read,hph_write,hph_delay,0};
 const uint32_t masks[]={0x80000003u,0x80000003u,0x80000001u,1,0x80000000u,
  0x80000000u,0xffffffffu,0x80000001u};
 const uint32_t wanted[]={1,1,1,0,0x80000000u,0,1,1};
 /* Revalidate each upstream gate BEFORE reading the dependent SMMU block.
  * A collapsed CX observed here must never be followed by a SMMU load. */
 for(unsigned i=0;i<8;i++) {
  if(!hgb_read(&io,&b,i))return 0;
  s->valid=b.valid;s->values[i]=b.values[i];
  if((b.values[i]&masks[i])!=wanted[i])return !hph_fail(c,HPH_COLD,9,hgb_address(i),0);
 }
 for(unsigned i=8;i<HGB_REGS;i++) {
  if(!hgb_read(&io,&b,i))return 0;s->valid=b.valid;s->values[i]=b.values[i];
 }
 /* Partial raw words remain available on failure. There is no per-stream
  * validity bitmap in v1: only full observer success qualifies every stream;
  * an adapter may additionally retain its own per-read trace for diagnostics. */
 for(unsigned i=0;i<24;i++) {
  if(!hgb_survey_read(&io,&b,0x3da0800+4*i,&b.smr[i]))return 0;s->smr[i]=b.smr[i];
  if(!hgb_survey_read(&io,&b,0x3da0c00+4*i,&b.s2cr[i]))return 0;s->s2cr[i]=b.s2cr[i];
 }
 for(unsigned i=0;i<22;i++) {
  if(!hgb_survey_read(&io,&b,0x3dc0000+4096*i,&b.sctlr[i]))return 0;s->sctlr[i]=b.sctlr[i];
 }
 hph_bus_copy(s,&b);
 uint32_t branch=0;
 if(hph_read(c,0x3d9913c,&branch))return 0;
 if((branch&0x80000001u)!=1)return !hph_fail(c,HPH_COLD,9,0x3d9913c,0);
 if(hph_read(c,0x3d7e600,&s->hfi))return 0;s->valid|=HPH_HFI_VALID;
 return hph_cold_qualified(s);
}
static inline void hph_clock_copy(struct hph_clock *d,const struct hcw_result *s) {
 d->code=s->code;d->stable=s->stable;
 for(unsigned at=0;at<2;at++) {
  d->valid[at]=s->valid[at];
  for(unsigned i=0;i<HCW_REGS;i++)d->raw[at][i]=s->raw[at][i];
 }
 for(unsigned i=0;i<2;i++) {
  d->configured_status_lo[i]=(uint32_t)s->configured_status[i];
  d->configured_status_hi[i]=(uint32_t)(s->configured_status[i]>>32);
  if(!s->configured_status[i]) {d->configured_valid|=1u<<i;d->configured_hz[i]=s->configured_hz[i];}
  d->source[i]=s->source[i];d->hid[i]=s->hid[i];
  if(s->derived_valid[i])d->derived_valid|=1u<<i;
  d->derived_lo[i]=(uint32_t)s->derived_hz[i];d->derived_hi[i]=(uint32_t)(s->derived_hz[i]>>32);
 }
}
static inline unsigned hph_action(void *cookie,const struct hgb_result *bus) {
 struct hph_context *c=(struct hph_context *)cookie;struct hph_record *r=c->record;
 r->action_seen=1;hph_bus_copy(&r->before,bus);
 uint32_t branch=0;
 if(hph_read(c,0x3d9913c,&branch))return HGB_RETAIN;
 if((branch&0x80000001u)!=1) {hph_fail(c,HPH_COLD,6,0x3d9913c,0);return HGB_RETAIN;}
 if(hph_read(c,0x3d7e600,&r->before.hfi))return HGB_RETAIN;
 r->before.valid|=HPH_HFI_VALID;
 if(bus->attempted!=7 || bus->restored || bus->streams_seen!=24 || bus->banks_seen!=22 ||
  !hph_cold_qualified(&r->before)) {hph_fail(c,HPH_COLD,6,0,0);return HGB_RETAIN;}
 r->phase=HPH_PHASE_HUB;
 if(!hph_owned(c,7,0))return HGB_RETAIN;
 const uint32_t cold_hub[]={0x3d993e8,0x3d993ec,0x3d993f0};
 for(unsigned i=0;i<3;i++) {
  if(hph_read(c,cold_hub[i],&r->hub_before[i]))return HGB_RETAIN;
  r->hub_before_valid|=1u<<i;
  if(r->hub_before[i]) {hph_fail(c,HPH_COLD,7,cold_hub[i],0);return HGB_RETAIN;}
 }
 r->hub_attempted=1;uint64_t status=c->io->hub(c->io->cookie,c->power,bus);
 r->hub_status_lo=(uint32_t)status;r->hub_status_hi=(uint32_t)(status>>32);
 if(status) {hph_fail(c,HPH_HUB,7,0,status);return HGB_RETAIN;}
 if(!hph_owned(c,7,0))return HGB_RETAIN;
 r->phase=HPH_PHASE_CLOCK;
 struct hcw_result clock;hph_zero(&clock,sizeof(clock));
 const struct hcw_io io={c,hph_clock_owned,hph_clock_read,hph_configured};
 unsigned code=hcw_run(&io,0,&clock);hph_clock_copy(&r->clock,&clock);
 if(code || !hph_hub_cold(&r->clock)) {hph_fail(c,HPH_CLOCK,8,0,code);return HGB_RETAIN;}
 r->phase=HPH_PHASE_FINAL;
 if(!hph_survey(c,&r->after)) {hph_fail(c,HPH_COLD,9,0,0);return HGB_RETAIN;}
 if(!hph_owned(c,9,0))return HGB_RETAIN;
 r->action_complete=1;return HGB_RETAIN; /* Deliberate retained gate exit, NOT a generic success. */
}
static inline unsigned hph_run(const struct hph_io *io,const struct gpp_report *power,
 uint64_t prepare_status,struct hph_record *r) {
 if(!r)return HPH_ARGUMENT;
 if(r->started)return HPH_REUSED;
 hph_zero(r,sizeof(*r));r->magic=HPH_MAGIC;r->version=HPH_VERSION;
 r->bytes=HPH_RECORD_BYTES;r->started=1;r->phase=HPH_PHASE_POWER;
 r->prepare_status_lo=(uint32_t)prepare_status;r->prepare_status_hi=(uint32_t)(prepare_status>>32);
 r->mask[0]=r->mask[1]=2;r->mask[2]=1;
 if(power) {hph_copy(r->power_words,power,sizeof(*power));r->retained=!!power->attempted;}
 if(!io || !io->read || !io->write || !io->delay || !io->hub || !io->configured || !io->exclusive) {
  r->code=HPH_ARGUMENT;hph_seal(r);return r->code;
 }
 if(!hph_power_qualified(power,prepare_status)) {r->code=HPH_POWER;hph_seal(r);return r->code;}
 r->retained=1;r->phase=HPH_PHASE_BUS;
 struct hgb_result bus;hph_zero(&bus,sizeof(bus));struct hph_context c={io,power,r,&bus,0};
 const struct hgb_io b={&c,hph_read,hph_write,hph_delay,hph_action};
 unsigned code=hgb_probe(&b,power,&bus);r->bus_code=code;
 if(!r->code && (code!=HGB_RETAIN || bus.restored || !r->action_complete || r->attempted!=7)) {
  r->first_bus_code=code;r->code=HPH_BUS;
 }
 if(!r->code) {r->phase=HPH_PHASE_DONE;r->complete=1;}
 hph_seal(r);return r->code;
}
static inline int hph_record_valid(const struct hph_record *r) {
 if(!r || r->magic!=HPH_MAGIC || r->version!=HPH_VERSION || r->bytes!=HPH_RECORD_BYTES ||
  r->started!=1 || r->code>HPH_CHANGED || r->phase<HPH_PHASE_POWER || r->phase>HPH_PHASE_DONE ||
  r->complete>1 || r->retained>1 || r->action_seen>1 || r->action_complete>1 || r->hub_attempted>1 ||
  (r->original_valid&~7u) || (r->final_valid&~7u) ||
  (r->attempted&~7u) || r->mask[0]!=2 || r->mask[1]!=2 || r->mask[2]!=1 ||
  r->crc32!=gpp_crc32(r,HPH_RECORD_BYTES-4))return 0;
 for(unsigned i=0;i<8;i++)if(r->reserved[i])return 0;
 if(!r->complete)return r->code!=HPH_OK;
 for(unsigned i=0;i<3;i++)if((r->original[i]^r->final[i])&~(r->mask[i]|0x80000000u))return 0;
 struct gpp_report p;hph_copy(&p,r->power_words,sizeof(p));
 uint64_t status=r->prepare_status_lo|((uint64_t)r->prepare_status_hi<<32);
 return !r->code && r->phase==HPH_PHASE_DONE && r->retained==1 &&
  r->bus_code==HGB_RETAIN && !r->first_bus_code && r->action_seen==1 && r->action_complete==1 &&
  r->hub_attempted==1 && !(r->hub_status_lo|r->hub_status_hi) && r->original_valid==7 &&
  r->hub_before_valid==7 && !(r->hub_before[0]|r->hub_before[1]|r->hub_before[2]) &&
  r->final_valid==7 && r->attempted==7 && hph_power_qualified(&p,status) &&
  (r->original[0]&1) && (r->original[1]&1) && !(r->original[2]&1) &&
  (r->final[0]&0x80000003u)==1 && (r->final[1]&0x80000003u)==1 &&
  (r->final[2]&0x80000001u)==1 && hph_cold_qualified(&r->before) &&
  hph_cold_qualified(&r->after) && hph_hub_cold(&r->clock);
}
/* Bounded wire import; byte copy tolerates unaligned firmware-property data. */
static inline int hph_decode(const void *bytes,unsigned length,struct hph_record *out) {
 if(!out)return 0;
 if(!bytes || length!=HPH_RECORD_BYTES) {hph_zero(out,sizeof(*out));return 0;}
 struct hph_record local;hph_copy(&local,bytes,sizeof(local));
 if(!hph_record_valid(&local)) {hph_zero(out,sizeof(*out));return 0;}
 hph_copy(out,&local,sizeof(local));return 1;
}
/* Actual read-only capture boundary for the later XNU adapter. No write,
 * power, firmware-cache query, hub, allocation or release callback exists.
 * Caller must provide NEW owned Device mappings; same register whitelist and
 * gated survey as the EFI producer. Software configured clocks are unavailable
 * here; only raw programming and explicitly labelled nominal decoding remain. */
struct hph_ro_io {
 void *cookie;
 GPP_U64 (*read)(void *,GPP_U32,GPP_U32 *);
 int (*exclusive)(void *);
};
static inline GPP_U64 hph_ro_configured(void *cookie,unsigned which,uint32_t *hz) {
 (void)cookie;(void)which;*hz=0;return UINT64_C(0x8000000000000013);
}
static inline unsigned hph_observe_ro(const struct hph_ro_io *io,struct hph_cold *cold,
 struct hph_clock *clock,uint32_t *failed_address) {
 if(failed_address)*failed_address=0;
 if(!cold || !clock)return HPH_ARGUMENT;
 hph_zero(cold,sizeof(*cold));hph_zero(clock,sizeof(*clock));
 if(!io || !io->read || !io->exclusive)return HPH_ARGUMENT;
 const struct hph_io bridge={io->cookie,io->read,0,0,0,hph_ro_configured,io->exclusive};
 struct hph_record report;struct hgb_result bus;
 hph_zero(&report,sizeof(report));hph_zero(&bus,sizeof(bus));
 struct hph_context context={&bridge,0,&report,&bus,0};
 if(!hph_survey(&context,cold)) {
  if(failed_address)*failed_address=report.first_address;
  return report.code?report.code:HPH_COLD;
 }
 struct hcw_result result;hph_zero(&result,sizeof(result));
 const struct hcw_io witness={&context,hph_clock_owned,hph_clock_read,hph_configured};
 unsigned code=hcw_run(&witness,0,&result);hph_clock_copy(clock,&result);
 if(failed_address)*failed_address=report.first_address;
 return report.code?report.code:(code || !hph_hub_cold(clock)?HPH_CLOCK:HPH_OK);
}
/* Pure XNU comparison. Fresh is from NEW XNU Device mappings and actual loads,
 * not a copied EFI snapshot. Software GetClockFreq is unavailable after EBS;
 * compare real programming fields, leaving asynchronous PLL STATUS separate.
 * No allocation/stream/GPU authority is granted by this comparison. */
static inline unsigned hph_recheck(const struct hph_record *r,const struct hph_cold *fresh,
 const struct hph_clock *clock) {
 if(!hph_record_valid(r) || !r->complete)return HPH_RECORD;
 if(!hph_cold_qualified(fresh) || !hph_hub_cold(clock))return HPH_COLD;
 const unsigned programming[]={HCW_GMU_CMD,HCW_GMU_CFG,HCW_GMU_BRANCH,
  HCW_HUB_CMD,HCW_HUB_CFG,HCW_HUB_AON,HCW_HUB_CX,HCW_AHB_DIV,
  HCW_PLL1_MODE,HCW_PLL1_OPMODE,HCW_PLL1_L,HCW_PLL1_ALPHA,HCW_PLL1_USER};
 for(unsigned i=0;i<sizeof(programming)/sizeof(programming[0]);i++)
  if(clock->raw[1][programming[i]]!=r->clock.raw[1][programming[i]])return HPH_CHANGED;
 if(((clock->raw[1][HCW_GMU_CFG]>>8)&7u)==1) {
  const uint32_t mask=0x3fu<<HCW_PLL0_MODE;
  if((clock->valid[0]&mask)!=mask || (clock->valid[1]&mask)!=mask)return HPH_COLD;
  for(unsigned i=0;i<6;i++)if(i!=2 && clock->raw[1][HCW_PLL0_MODE+i]!=
    r->clock.raw[1][HCW_PLL0_MODE+i])return HPH_CHANGED;
 }
 return HPH_OK;
}
#endif
