/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Read-only SM8650 GMU/HUB clock witness. Register bindings and decoding:
 * MiCode Xiaomi_Kernel_OpenSource 317e609aa4fb956c3aff565b62215e5a8041ca0c,
 * drivers/clk/qcom/{gpucc-pineapple.c,clk-rcg2.c,clk-alpha-pll.c}.
 * No write/rate/enable/measurement callback exists. A decoded nominal rate
 * is not a hardware frequency measurement or proof of GPU execution.
 */
#ifndef HOUJI_GPU_CLOCK_WITNESS_H
#define HOUJI_GPU_CLOCK_WITNESS_H
#include <stdint.h>
enum {HCW_OK,HCW_ARGUMENT,HCW_REUSED,HCW_GUARD,HCW_READ,HCW_UNSTABLE};
enum {HCW_GMU_CMD,HCW_GMU_CFG,HCW_GMU_BRANCH,HCW_HUB_CMD,HCW_HUB_CFG,
 HCW_HUB_AON,HCW_HUB_CX,HCW_AHB_DIV,HCW_PLL1_MODE,HCW_PLL1_OPMODE,
 HCW_PLL1_STATUS,HCW_PLL1_L,HCW_PLL1_ALPHA,HCW_PLL1_USER,
 HCW_PLL0_MODE,HCW_PLL0_OPMODE,HCW_PLL0_STATUS,HCW_PLL0_L,HCW_PLL0_ALPHA,
 HCW_PLL0_USER,HCW_REGS};
enum {HCW_GUARD_REGS=8};
struct hcw_io {
 void *cookie;
 int (*owned)(void *,unsigned);
 int (*read)(void *,uint32_t,uint32_t *);
 uint64_t (*configured)(void *,unsigned,uint32_t *);
};
struct hcw_result {
 unsigned started,mode,code,phase,reads,queries,guards,stable;
 uint32_t valid[2],raw[2][HCW_REGS],guard_valid[2],guard[2][HCW_GUARD_REGS];
 uint32_t configured_hz[2],source[2],hid[2],derived_valid[2];
 uint64_t configured_status[2],derived_hz[2];
};
static inline uint32_t hcw_address(unsigned reg) {
 static const uint32_t a[]={0x3d99318,0x3d9931c,0x3d9913c,0x3d993ec,
  0x3d993f0,0x3d993e8,0x3d99148,0x3d9942c,0x3d91000,0x3d91004,
  0x3d9100c,0x3d91010,0x3d91014,0x3d91018,0x3d90000,0x3d90004,
  0x3d9000c,0x3d90010,0x3d90014,0x3d90018};
 return reg<HCW_REGS?a[reg]:0;
}
static inline int hcw_read(const struct hcw_io *io,struct hcw_result *r,
 uint32_t address,uint32_t *value) {
 r->reads++;return !io->read(io->cookie,address,value);
}
static inline int hcw_guard(const struct hcw_io *io,struct hcw_result *r,unsigned at) {
 static const uint32_t a[]={0x171004,0x171010,0x171018,0x3d97000,
  0x3d99108,0x3d99168,0x3d9911c,0x3d7e000};
 static const uint32_t mask[]={0x80000003,0x80000003,0x80000001,0x80000001,
  1,0x80000000,0x80000000,0xffffffff};
 static const uint32_t want[]={1,1,1,1,0,0x80000000,0,0};
 r->guards++;
 if(io->owned(io->cookie,r->mode))return 0;
 for(unsigned i=0;i<HCW_GUARD_REGS;i++) {
  if(!hcw_read(io,r,a[i],&r->guard[at][i]))return 0;
  r->guard_valid[at]|=1u<<i;
  if((r->guard[at][i]&mask[i])!=(i==7?(r->mode?0u:1u):want[i]))return 0;
 }
 return 1;
}
static inline int hcw_snapshot(const struct hcw_io *io,struct hcw_result *r,unsigned at) {
 for(unsigned i=0;i<HCW_REGS;i++) {
  /* PLL0 is unrelated unless the actual GMU mux selects it. HUB's target
   * parent map has no PLL0 entry. Do not read an unused PLL speculatively. */
  if(i>=HCW_PLL0_MODE && ((r->raw[at][HCW_GMU_CFG]>>8)&7)!=1)break;
  if(!hcw_read(io,r,hcw_address(i),&r->raw[at][i]))return 0;
  r->valid[at]|=1u<<i;
 }
 return 1;
}
static inline void hcw_derive(struct hcw_result *r,unsigned which) {
 unsigned cfg=which?HCW_HUB_CFG:HCW_GMU_CFG;
 unsigned cmd=which?HCW_HUB_CMD:HCW_GMU_CMD;
 unsigned branch=which?HCW_HUB_CX:HCW_GMU_BRANCH;
 uint32_t v=r->raw[1][cfg];uint64_t parent=0;
 r->source[which]=(v>>8)&7;r->hid[which]=v&31;
 if(!r->stable || (r->raw[1][cmd]&(0x80000000u|0xf1u)) ||
  (r->raw[1][branch]&0x80000000u) || (v&0x3000u))return;
 if(!r->source[which])parent=19200000; /* nominal XO, not measured */
 else if(r->source[which]==3 || (!which && r->source[which]==1)) {
  unsigned base=r->source[which]==3?HCW_PLL1_MODE:HCW_PLL0_MODE;
  uint32_t needed=0x3fu<<base;
  if((r->valid[0]&needed)!=needed || (r->valid[1]&needed)!=needed)return;
  for(unsigned i=0;i<6;i++) {
   /* STATUS is observational; only stable programming fields are required. */
   if(i!=2 && r->raw[0][base+i]!=r->raw[1][base+i])return;
  }
  if((r->raw[1][base]&0x80000005u)!=0x80000005u ||
   !(r->raw[1][base+1]&1) || !(r->raw[1][base+5]&1))return;
  parent=UINT64_C(19200000)*(r->raw[1][base+3]&65535u)+
   ((UINT64_C(19200000)*(r->raw[1][base+4]&65535u))>>16);
 } else return; /* GPLL0 external parent was not observed; no guessed rate. */
 r->derived_hz[which]=r->hid[which]?parent*2/(r->hid[which]+1):parent;
 r->derived_valid[which]=1;
}
static inline unsigned hcw_run(const struct hcw_io *io,unsigned mode,struct hcw_result *r) {
 if(!r)return HCW_ARGUMENT;
 if(r->started)return HCW_REUSED;
 *r=(struct hcw_result){.started=1,.mode=mode};
 if(mode>1 || !io || !io->owned || !io->read || !io->configured)return r->code=HCW_ARGUMENT;
 r->phase=1;if(!hcw_guard(io,r,0))return r->code=HCW_GUARD;
 r->phase=2;if(!hcw_snapshot(io,r,0))return r->code=HCW_READ;
 r->phase=3;
 for(unsigned i=0;i<2;i++) {
  r->queries++;r->configured_status[i]=io->configured(io->cookie,i,&r->configured_hz[i]);
 }
 r->phase=4;if(!hcw_snapshot(io,r,1))return r->code=HCW_READ;
 r->phase=5;if(!hcw_guard(io,r,1))return r->code=HCW_GUARD;
 r->phase=6;
 const unsigned stable[]={HCW_GMU_CMD,HCW_GMU_CFG,HCW_HUB_CMD,HCW_HUB_CFG};
 for(unsigned i=0;i<sizeof(stable)/sizeof(stable[0]);i++)
  if(r->raw[0][stable[i]]!=r->raw[1][stable[i]])return r->code=HCW_UNSTABLE;
 r->stable=1;hcw_derive(r,0);hcw_derive(r,1);r->phase=7;return r->code;
}
#endif
