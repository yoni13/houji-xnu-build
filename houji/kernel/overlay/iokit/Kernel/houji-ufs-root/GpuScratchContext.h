/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * One owned normal GPU bank0 scratch mapping beside the running GMU bank2.
 * Fixed profiles attach SID0 only (legacy), or SID0+LPAC SID1 to the same
 * owned normal domain. Secure SID2 and all other streams remain FAULT.
 * No GX/CP access, global writes, fault clearing or cleanup/free operation.
 * All pages and both contexts stay pinned until whole-platform reset.
 */
#ifndef HOUJI_GPU_SCRATCH_CONTEXT_H
#define HOUJI_GPU_SCRATCH_CONTEXT_H
#include "GpuSmmuContext.h"
#include "GpuSharedMemory.h"
#include "HoujiGpuAddressLayout.h"
enum { HSC_OK,HSC_ARGUMENT,HSC_GUARD,HSC_IO,HSC_VERIFY,HSC_TIMEOUT };
struct hsc_result {
 unsigned code,phase,reads,writes,polls,attempted,enabled,attached,retained,last_operation,observed_valid;
 uint32_t last_address,last_value,fsr;uint64_t last_wide;
 unsigned lpac,sid0_attached,lpac_attached;
 uint32_t sid0_smr,sid0_s2cr,lpac_smr,lpac_s2cr;
};
static inline int hsc_ranges_at(uint64_t physical,uint32_t iova,ArmSmmu4KRange ranges[HSC_CLASSES]) {
 if(!physical || (physical&4095) || physical>=(UINT64_C(1)<<36) || HSC_POOL_BYTES>(UINT64_C(1)<<36)-physical || !ranges)return 1;
 for(unsigned cls=0;cls<HSC_CLASSES;cls++)ranges[cls]=(ArmSmmu4KRange){iova+(uint64_t)cls*HSC_CLASS_BYTES,
  physical+(uint64_t)cls*HSC_CLASS_BYTES,HSC_CLASS_BYTES,
  ArmSmmu4KRead|((cls&1)?ArmSmmu4KPrivileged:0)|((cls&2)?ArmSmmu4KWrite:0),0,3};
 return 0;
}
static inline int hsc_pool_ranges(uint64_t physical,ArmSmmu4KRange ranges[HSC_CLASSES]) {return hsc_ranges_at(physical,HSC_POOL_IOVA,ranges);}
static inline int hsc_gpu_only_ranges(uint64_t physical,ArmSmmu4KRange ranges[HSC_CLASSES]) {return hsc_ranges_at(physical,HSC_GPU_ONLY_IOVA,ranges);}
static inline int hsc_large_range(uint64_t physical,ArmSmmu4KRange *range) {
 if(!physical || (physical&4095) || physical>=(UINT64_C(1)<<36) || HSC_LARGE_BYTES>(UINT64_C(1)<<36)-physical || !range)return 1;
 *range=(ArmSmmu4KRange){HSC_LARGE_IOVA,physical,HSC_LARGE_BYTES,ArmSmmu4KRead|ArmSmmu4KWrite|ArmSmmu4KPrivileged,0,3};return 0;
}
static inline uint32_t hsc_address(unsigned i) {
 const uint32_t a[]={0x3dc0000,0x3da1800,0x3da1000,0x3dc0010,0x3dc0030,
  0x3dc0020,0x3dc0028,0x3dc0038,0x3dc003c};return i<9?a[i]:0;
}
static inline int hsc_read(const struct hgp_io *io,struct hsc_result*r,uint32_t a,uint32_t*v) {
 r->last_address=a;r->last_operation=1;r->observed_valid=0;r->reads++;
 if(io->read32(io->cookie,a,v))return 0;r->last_value=*v;r->last_wide=*v;r->observed_valid=1;return 1;
}
static inline int hsc_write(const struct hgp_io *io,struct hsc_result*r,uint32_t a,uint64_t v,unsigned wide) {
 r->last_address=a;r->last_operation=2;r->observed_valid=0;r->last_wide=v;r->writes++;
 return !(wide?io->write64(io->cookie,a,v):io->write32(io->cookie,a,(uint32_t)v));
}
static inline int hsc_survey(const struct hgp_io *io,struct hsc_result*r,unsigned live) {
 uint32_t value;
 if(r->lpac>1 || live>1)return 0;
 if(io->power_guard(io->cookie))return 0;
 const uint32_t a[]={0x3da0020,0x3da0024,0x3da0028,0x3da0048};
 const uint32_t expected[]={0x4c017e18,0x40000016,0x5511,0};
 for(unsigned i=0;i<4;i++)if(!hsc_read(io,r,a[i],&value) || value!=expected[i])return 0;
 for(unsigned i=0;i<24;i++) {
  uint32_t smr=i==0?0x80000005u:(live && i==1?0x80000000u:
   (live && r->lpac && i==2?0x80000001u:0));
  uint32_t route=i==0?2:(live && (i==1 || (r->lpac && i==2))?0:0x20000u);
  if(!hsc_read(io,r,0x3da0800+4*i,&value) || value!=smr ||
     !hsc_read(io,r,0x3da0c00+4*i,&value) || value!=route)return 0;
 }
 for(unsigned i=0;i<22;i++) {
  if(!hsc_read(io,r,0x3dc0000+4096*i,&value))return 0;
  if(i==2 || (live && i==0)){if((value&~0x1000u)!=0x27)return 0;}
  else if(value&1)return 0;
 }
 if(!hsc_read(io,r,0x3dc0058,&r->fsr) || (r->fsr&0xc00001feu))return 0;
 return 1;
}
static inline unsigned hsc_run_profile(const struct hgp_io *io,const ArmSmmu4KConfig *config,
 const ArmSmmu4KResult *built,struct hsc_result*r,unsigned lpac) {
 if(!r)return HSC_ARGUMENT;*r=(struct hsc_result){0};r->retained=1;r->lpac=lpac;
 if(!io || !io->read32 || !io->read64 || !io->write32 || !io->write64 ||
    !io->delay || !io->power_guard || !config || !built || lpac>1 || config->input_bits!=32 ||
    config->output_bits!=36 || built->root_physical!=config->table_physical ||
    !((built->table_pages==3 && built->leaf_pages==HSC_POOL_PAGES) ||
      (built->table_pages==HSC_LARGE_TABLE_PAGES && built->leaf_pages==HSC_LARGE_LEAF_PAGES) ||
      (built->table_pages==HSC_DATA_TABLE_PAGES && built->leaf_pages==HSC_DATA_LEAF_PAGES) ||
      (built->table_pages==HSC_GPU_TABLE_PAGES && built->leaf_pages==HSC_GPU_LEAF_PAGES) ||
      (built->table_pages==HOUJI_GPU_TABLE_USED_PAGES && built->leaf_pages==HSC_GPU_LEAF_PAGES && config->leaf_budget==HOUJI_GPU_APERTURE/4096)))return r->code=HSC_ARGUMENT;
 ArmSmmu4KContextConfig cfg={config,0,built,0,0x4c017e18,0x5511,0x751,0,0,0};
 ArmSmmu4KContextImage image;uint32_t value=0;uint64_t wide=0;
 if(ArmSmmu4KEncodeContext(&cfg,&image))return r->code=HSC_ARGUMENT;
#define HSC_REQUIRE(test,error) do {if(!(test))return r->code=(error);} while(0)
 r->phase=1;HSC_REQUIRE(hsc_survey(io,r,0),HSC_GUARD);
 HSC_REQUIRE(hsc_read(io,r,0x3dc0000,&value) && !(value&0x41) &&
  hsc_read(io,r,0x3dc0004,&value) && value==3,HSC_GUARD);
 const uint64_t fields[]={0,image.cba2r,image.cbar,image.tcr2,image.tcr,
  image.ttbr0,image.ttbr1,image.mair0,image.mair1};
 r->phase=2;r->attempted=1;
 for(unsigned i=0;i<9;i++)HSC_REQUIRE(hsc_write(io,r,hsc_address(i),fields[i],i==5 || i==6),HSC_IO);
 HSC_REQUIRE(hsc_write(io,r,0x3dc0004,0x32b,0) && hsc_write(io,r,0x3dc0618,0,0) &&
  hsc_write(io,r,0x3dc07f0,0xffffffffu,0),HSC_IO);
 for(unsigned i=0;i<100;i++) {
  HSC_REQUIRE(hsc_read(io,r,0x3dc07f4,&value),HSC_IO);if(!(value&1))break;
  r->polls++;HSC_REQUIRE(i<99,HSC_TIMEOUT);HSC_REQUIRE(!io->delay(io->cookie,10),HSC_IO);
 }
 r->phase=3;
 for(unsigned i=0;i<9;i++) {
  if(i==5 || i==6) {
   r->last_address=hsc_address(i);r->last_operation=1;r->observed_valid=0;r->reads++;
   HSC_REQUIRE(!io->read64(io->cookie,hsc_address(i),&wide),HSC_IO);
   r->last_wide=wide;r->observed_valid=1;HSC_REQUIRE(wide==fields[i],HSC_VERIFY);
  } else {
   HSC_REQUIRE(hsc_read(io,r,hsc_address(i),&value) &&
    (i==3?((value^(uint32_t)fields[i])&~0x60u)==0:value==fields[i]),HSC_VERIFY);
  }
 }
 HSC_REQUIRE(hsc_read(io,r,0x3dc0004,&value) && value==0x32b,HSC_VERIFY);
 HSC_REQUIRE(hsc_survey(io,r,0),HSC_GUARD);
 r->phase=4;HSC_REQUIRE(hsc_write(io,r,0x3dc0000,0x1027,0) &&
  hsc_read(io,r,0x3dc0000,&value) && (value&~0x1000u)==0x27,HSC_VERIFY);r->enabled=1;
 HSC_REQUIRE(hsc_write(io,r,0x3da0c04,0,0) && hsc_write(io,r,0x3da0804,0x80000000u,0),HSC_IO);
 HSC_REQUIRE(hsc_read(io,r,0x3da0c04,&r->sid0_s2cr) && r->sid0_s2cr==0 &&
  hsc_read(io,r,0x3da0804,&r->sid0_smr) && r->sid0_smr==0x80000000u,HSC_VERIFY);r->sid0_attached=1;
 if(r->lpac) {
  HSC_REQUIRE(hsc_write(io,r,0x3da0c08,0,0) && hsc_write(io,r,0x3da0808,0x80000001u,0),HSC_IO);
  HSC_REQUIRE(hsc_read(io,r,0x3da0c08,&r->lpac_s2cr) && r->lpac_s2cr==0 &&
   hsc_read(io,r,0x3da0808,&r->lpac_smr) && r->lpac_smr==0x80000001u,HSC_VERIFY);r->lpac_attached=1;
 }
 r->phase=5;HSC_REQUIRE(hsc_survey(io,r,1),HSC_GUARD);
 HSC_REQUIRE(hsc_read(io,r,0x3dc0004,&value) && value==0x32b,HSC_VERIFY);
 r->attached=1;
 return r->code;
#undef HSC_REQUIRE
}
/* No caller-supplied stream IDs, routes or secure context are accepted. The
 * attachment proofs above follow actual readback; any partial failure retains
 * all pages and route changes until reset and leaves attached clear. */
static inline unsigned hsc_run(const struct hgp_io *io,const ArmSmmu4KConfig *config,
 const ArmSmmu4KResult *built,struct hsc_result*r) {
 return hsc_run_profile(io,config,built,r,0);
}
static inline unsigned hsc_run_lpac(const struct hgp_io *io,const ArmSmmu4KConfig *config,
 const ArmSmmu4KResult *built,struct hsc_result*r) {
 return hsc_run_profile(io,config,built,r,1);
}
/* The first captured v1 request only: shared privileged read/write scratch,
 * 4KiB, no secure or MMIO semantics. This is a wire check, not mapping proof.
 */
static inline int hsc_scratch_request(const uint32_t *request,unsigned words) {
 return request && words==11 && (request[0]&0xfffffu)==0xb14 && request[1]==1 &&
  !request[2] && !request[3] && request[4]==0x33f && request[5]==2 &&
  !request[6] && request[7]==2 && !request[8] && request[9]==4096 && !request[10];
}
#endif
