/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Fixed houji stock-GMU protocol ONLY. Pure comparison: no MMIO/SMC/IO.
 * Source bindings:
 * GpuStockGmu.h SHA256 c96fae9333b6220ccb7a14386fcb05cce3b6511158d393376d4c11d4140077ad
 * GpuSmmuContext.h SHA256 05142b91bdb2d3ec5b012e043666c8acade76383072df793d31c25472aab86ec
 * firmware 78588B SHA256 9685f62c42543befcaee68d25355e16bf55cdb0d573bddcc9f6633f59eec3f72
 * TCM literals are the actual 135 host hgf writes from that pinned firmware.
 * This is an address/value protocol filter, not owner/cache/FS qualification.
 * Native MUST bind encoded[5] to its actual tableBO and zero_physical to its
 * retained PRR-zero BO, and separately qualify every callback/phase/lease.
 */
#ifndef HOUJI_STOCK_MMIO_POLICY_H
#define HOUJI_STOCK_MMIO_POLICY_H
#include <stdint.h>
#include <stddef.h>
enum {HSNP_WIDTH32=32,HSNP_WIDTH64=64};
typedef struct HsnpBindings {uint64_t encoded[9];uint64_t zero_physical;} HsnpBindings;
static inline uint32_t hsnp_gate_address(unsigned i){
 const uint32_t a[7]={0x171004u,0x171010u,0x171018u,0x3d97000u,0x3d99108u,0x3d99168u,0x3d9911cu};return i<7?a[i]:0;
}
static inline uint32_t hsnp_gate_mask(unsigned i){
 const uint32_t a[7]={0x80000003u,0x80000003u,0x80000001u,0x80000001u,1u,0x80000000u,0x80000000u};return i<7?a[i]:0;
}
static inline uint32_t hsnp_gate_value(unsigned i){
 const uint32_t a[7]={1,1,1,1,0,0x80000000u,0};return i<7?a[i]:0;
}
static inline uint32_t hsnp_field_address(unsigned i){
 const uint32_t a[9]={0x3dc2000u,0x3da1808u,0x3da1008u,0x3dc2010u,0x3dc2030u,0x3dc2020u,0x3dc2028u,0x3dc2038u,0x3dc203cu};return i<9?a[i]:0;
}
static inline int hsnp_bindings_valid(const HsnpBindings*b){
 const uint64_t tag=UINT64_C(0x0750000000000000),mask=UINT64_C(0x0000fffffffff000);
 if(!b || !b->zero_physical || (b->zero_physical&4095) || b->zero_physical>=(UINT64_C(1)<<36))return 0;
 const uint64_t root=b->encoded[5]&mask;
 return b->encoded[0]==0 && b->encoded[1]==1 && b->encoded[2]==0x1f300u && b->encoded[3]==0x38011u &&
  b->encoded[4]==0x80802020u && root && root<(UINT64_C(1)<<36) && b->encoded[5]==(tag|root) &&
  b->encoded[6]==tag && b->encoded[7]==0xff44u && b->encoded[8]==0;
}
static inline int hsnp_allowed_read(uint32_t a,unsigned width){
 if(width==HSNP_WIDTH64)return a==0x3dc2020u || a==0x3dc2028u || a==0x3dc2060u;
 if(width!=HSNP_WIDTH32 || (a&3))return 0;
 if(a>=0x3d6d000u && a<0x3d75000u)return 1; /* exact 8192-word TCM backup */
 if(a>=0x3da0800u && a<0x3da0860u)return 1; /* actual 24 SMRs */
 if(a>=0x3da0c00u && a<0x3da0c60u)return 1; /* actual 24 S2CRs */
 if(a>=0x3dc0000u && a<0x3dd6000u && !((a-0x3dc0000u)&4095))return 1; /* SCTLR only */
 switch(a){
 case 0x171004u:
 case 0x171010u:
 case 0x171018u:
 case 0x3d0f114u:
 case 0x3d50010u:
 case 0x3d50020u:
 case 0x3d50024u:
 case 0x3d50028u:
 case 0x3d5002cu:
 case 0x3d50400u:
 case 0x3d50404u:
 case 0x3d50550u:
 case 0x3d50554u:
 case 0x3d50558u:
 case 0x3d5055cu:
 case 0x3d50560u:
 case 0x3d7d000u:
 case 0x3d7d004u:
 case 0x3d7d03cu:
 case 0x3d7e000u:
 case 0x3d7e004u:
 case 0x3d7e070u:
 case 0x3d7e0b4u:
 case 0x3d7e200u:
 case 0x3d7e3c0u:
 case 0x3d7e3c4u:
 case 0x3d7e600u:
 case 0x3d7e610u:
 case 0x3d7e614u:
 case 0x3d7e618u:
 case 0x3d7e648u:
 case 0x3d7e64cu:
 case 0x3d7e734u:
 case 0x3d7e738u:
 case 0x3d7e73cu:
 case 0x3d7e740u:
 case 0x3d8ec14u:
 case 0x3d8ec18u:
 case 0x3d8ec44u:
 case 0x3d97000u:
 case 0x3d99108u:
 case 0x3d9911cu:
 case 0x3d99168u:
 case 0x3da0048u:
 case 0x3da1008u:
 case 0x3da1808u:
 case 0x3da6008u:
 case 0x3da600cu:
 case 0x3dc2004u:
 case 0x3dc2010u:
 case 0x3dc2030u:
 case 0x3dc2038u:
 case 0x3dc203cu:
 case 0x3dc2058u:
 case 0x3dc2068u:
 case 0x3dc27f4u:
 case 0xb284500u:
 case 0xb284520u:
 return 1;default:return 0;
 }
}
struct HsnpTcmWord {uint32_t address,value;};
static const struct HsnpTcmWord hsnp_tcm_words[135]={
 {0x3d6d000u,0x10007f80u},
 {0x3d6d004u,0x00000105u},
 {0x3d6d008u,0x00000145u},
 {0x3d6d00cu,0x00000159u},
 {0x3d6d010u,0xf802f000u},
 {0x3d6d014u,0xfff8f003u},
 {0x3d6d018u,0xe890a00au},
 {0x3d6d01cu,0x44820c00u},
 {0x3d6d020u,0xf1aa4483u},
 {0x3d6d024u,0x45da0701u},
 {0x3d6d028u,0xf003d101u},
 {0x3d6d02cu,0xf2afffedu},
 {0x3d6d030u,0xe8ba0e09u},
 {0x3d6d034u,0xf013000fu},
 {0x3d6d038u,0xbf180f01u},
 {0x3d6d03cu,0xf0431afbu},
 {0x3d6d040u,0x47180301u},
 {0x3d6d044u,0x00000128u},
 {0x3d6d048u,0x00000198u},
 {0x3d6d04cu,0xbf243a10u},
 {0x3d6d050u,0xc178c878u},
 {0x3d6d054u,0x0752d8fau},
 {0x3d6d058u,0xc830bf24u},
 {0x3d6d05cu,0xbf44c130u},
 {0x3d6d060u,0x600c6804u},
 {0x3d6d064u,0x00004770u},
 {0x3d6d068u,0x24002300u},
 {0x3d6d06cu,0x26002500u},
 {0x3d6d070u,0xbf283a10u},
 {0x3d6d074u,0xd8fbc178u},
 {0x3d6d078u,0xbf280752u},
 {0x3d6d07cu,0xbf48c130u},
 {0x3d6d080u,0x4770600bu},
 {0x3d6d084u,0x4c1db5f0u},
 {0x3d6d088u,0x68202114u},
 {0x3d6d08cu,0x2000eb01u},
 {0x3d6d090u,0x4d1b6020u},
 {0x3d6d094u,0x606e2600u},
 {0x3d6d098u,0xf3bf602eu},
 {0x3d6d09cu,0xf3bf8f4fu},
 {0x3d6d0a0u,0x27038f6fu},
 {0x3d6d0a4u,0x2301616fu},
 {0x3d6d0a8u,0x4a154619u},
 {0x3d6d0acu,0x68103228u},
 {0x3d6d0b0u,0x42884018u},
 {0x3d6d0b4u,0x616ed1fbu},
 {0x3d6d0b8u,0x8f4ff3bfu},
 {0x3d6d0bcu,0x21156820u},
 {0x3d6d0c0u,0x2000eb01u},
 {0x3d6d0c4u,0x612f6020u},
 {0x3d6d0c8u,0x460b2101u},
 {0x3d6d0ccu,0x32244a0cu},
 {0x3d6d0d0u,0x40086810u},
 {0x3d6d0d4u,0xd1fb4298u},
 {0x3d6d0d8u,0xf3bf612eu},
 {0x3d6d0dcu,0xf3bf8f4fu},
 {0x3d6d0e0u,0xbf008f6fu},
 {0x3d6d0e4u,0xbf00bf00u},
 {0x3d6d0e8u,0xbf00bf00u},
 {0x3d6d0ecu,0x2116bf00u},
 {0x3d6d0f0u,0xeb016820u},
 {0x3d6d0f4u,0x60202000u},
 {0x3d6d0f8u,0x0000bdf0u},
 {0x3d6d0fcu,0x10007fdcu},
 {0x3d6d100u,0x1807d000u},
 {0x3d6d104u,0x68014809u},
 {0x3d6d108u,0x2101ea4fu},
 {0x3d6d10cu,0x0101f101u},
 {0x3d6d110u,0x48076001u},
 {0x3d6d114u,0x8808f380u},
 {0x3d6d118u,0xf3804806u},
 {0x3d6d11cu,0x48068809u},
 {0x3d6d120u,0x60084906u},
 {0x3d6d124u,0xffaef7ffu},
 {0x3d6d128u,0xbf72f7ffu},
 {0x3d6d12cu,0x10007fdcu},
 {0x3d6d130u,0x10007f80u},
 {0x3d6d134u,0xdeadbeefu},
 {0x3d6d138u,0x00001000u},
 {0x3d6d13cu,0xe000ed08u},
 {0x3d6d140u,0xe7fdbf30u},
 {0x3d6d144u,0x68014803u},
 {0x3d6d148u,0x2101ea4fu},
 {0x3d6d14cu,0x0103f101u},
 {0x3d6d150u,0xe7f56001u},
 {0x3d6d154u,0x10007fdcu},
 {0x3d6d158u,0x68014803u},
 {0x3d6d15cu,0x2101ea4fu},
 {0x3d6d160u,0x0104f101u},
 {0x3d6d164u,0xe7eb6001u},
 {0x3d6d168u,0x10007fdcu},
 {0x3d6d16cu,0x000110ecu},
 {0x3d6d170u,0x00000200u},
 {0x3d6d174u,0x0000383cu},
 {0x3d6d178u,0x0000004cu},
 {0x3d6d17cu,0x00014928u},
 {0x3d6d180u,0x00100000u},
 {0x3d6d184u,0x00000ec8u},
 {0x3d6d188u,0x0000004cu},
 {0x3d6d18cu,0x000157f0u},
 {0x3d6d190u,0x00200000u},
 {0x3d6d194u,0x00000080u},
 {0x3d6d198u,0x0000004cu},
 {0x3d6d19cu,0x00015870u},
 {0x3d6d1a0u,0x10004000u},
 {0x3d6d1a4u,0x00001780u},
 {0x3d6d1a8u,0x0000004cu},
 {0x3d6d1acu,0x000157f0u},
 {0x3d6d1b0u,0x00100ec8u},
 {0x3d6d1b4u,0x00019cb0u},
 {0x3d6d1b8u,0x00000068u},
 {0x3d6d1bcu,0x00015870u},
 {0x3d6d1c0u,0x00300000u},
 {0x3d6d1c4u,0x00024bf8u},
 {0x3d6d1c8u,0x00000068u},
 {0x3d6d1ccu,0x00016ff0u},
 {0x3d6d1d0u,0x10005780u},
 {0x3d6d1d4u,0x00001738u},
 {0x3d6d1d8u,0x00000068u},
 {0x3d74f98u,0x00000000u},
 {0x3d74f9cu,0x00000000u},
 {0x3d74fa0u,0x00000000u},
 {0x3d74fa4u,0x00000000u},
 {0x3d74fa8u,0x00000000u},
 {0x3d74facu,0x00000000u},
 {0x3d74fb0u,0x00000000u},
 {0x3d74fb4u,0x00000000u},
 {0x3d74fb8u,0x00000000u},
 {0x3d74fbcu,0x00000000u},
 {0x3d74fdcu,0xffffffffu},
 {0x3d74fe0u,0x40020014u},
 {0x3d74fe4u,0xffffffffu},
 {0x3d74fe8u,0x40020010u},
 {0x3d74fecu,0x00000000u},
 {0x3d74ff0u,0x20060001u},
};
struct HsnpWriteRule {uint32_t address,width,value,phase_mask;};
static const struct HsnpWriteRule hsnp_fixed_writes[47]={
 {0x3d0f114u,32u,0x00000000u,0x10u},
 {0x3d50010u,32u,0x01000000u,0x8u},
 {0x3d50020u,32u,0x00004520u,0x8u},
 {0x3d50024u,32u,0x00004510u,0x8u},
 {0x3d50028u,32u,0x00004514u,0x8u},
 {0x3d5002cu,32u,0x00000001u,0x8u},
 {0x3d50034u,32u,0x00000000u,0x8u},
 {0x3d50038u,32u,0x00000000u,0x8u},
 {0x3d5003cu,32u,0x00000000u,0x8u},
 {0x3d50040u,32u,0x00000000u,0x8u},
 {0x3d50044u,32u,0x00000000u,0x8u},
 {0x3d50048u,32u,0x80000021u,0x8u},
 {0x3d50400u,32u,0x00000000u,0x8u},
 {0x3d50550u,32u,0xeaaae5a0u,0x8u},
 {0x3d50554u,32u,0xe1a1ebabu,0x8u},
 {0x3d50558u,32u,0xa2e0a581u,0x8u},
 {0x3d5055cu,32u,0xecac82e2u,0x8u},
 {0x3d50560u,32u,0x0020edadu,0x8u},
 {0x3d7d000u,32u,0x00000001u,0x10u},
 {0x3d7d004u,32u,0x00000001u,0x10u},
 {0x3d7d03cu,32u,0x00000001u,0x10u},
 {0x3d7e000u,32u,0x00000000u,0x80u},
 {0x3d7e000u,32u,0x00000001u,0x780u},
 {0x3d7e004u,32u,0x00000002u,0x10u},
 {0x3d7e070u,32u,0x00000000u,0x10u},
 {0x3d7e0b4u,32u,0x00004052u,0x10u},
 {0x3d7e3c0u,32u,0x00000001u,0x10u},
 {0x3d7e3c4u,32u,0x00000001u,0x10u},
 {0x3d7e610u,32u,0x00000001u,0x10u},
 {0x3d7e614u,32u,0x60010000u,0x10u},
 {0x3d7e618u,32u,0x00000000u,0x10u},
 {0x3d7e618u,32u,0x00000001u,0x100u},
 {0x3d7e64cu,32u,0xffffffffu,0x10u},
 {0x3d7e650u,32u,0x00000001u,0x200u},
 {0x3d7e734u,32u,0x6003000fu,0x10u},
 {0x3d7e738u,32u,0x00000000u,0x10u},
 {0x3d7e73cu,32u,0x07090100u,0x10u},
 {0x3d7e740u,32u,0x60040000u,0x10u},
 {0x3d8ec18u,32u,0xffffffffu,0x10u},
 {0x3d8ec44u,32u,0x80c808a0u,0x10u},
 {0x3da0800u,32u,0x80000005u,0x40u},
 {0x3da0c00u,32u,0x00000002u,0x40u},
 {0x3dc2004u,32u,0x0000032bu,0x20u},
 {0x3dc2618u,32u,0x00000000u,0x20u},
 {0x3dc27f0u,32u,0xffffffffu,0x20u},
 {0xb284500u,32u,0x80000001u,0x8u},
 {0xb284520u,32u,0x00000000u,0x8u},
};
static inline int hsnp_allowed_write_impl(const HsnpBindings*b,uint32_t a,unsigned width,uint64_t value,unsigned phase){
 if(!hsnp_bindings_valid(b) || (width!=HSNP_WIDTH32 && width!=HSNP_WIDTH64) || (width==HSNP_WIDTH32 && (value>>32)) || (a&3) || phase>10)return 0;
 for(unsigned i=0;i<9;i++)if(a==hsnp_field_address(i)){
  if(width!=(i==5 || i==6?HSNP_WIDTH64:HSNP_WIDTH32))return 0;
  if(value==b->encoded[i] && (!phase || phase==5))return 1;
  return i==0 && value==0x1027u && (!phase || phase==6);
 }
 if(width!=HSNP_WIDTH32)return 0;
 if(a==0x3da6008u || a==0x3da600cu)return (!phase || phase==5) && value==(a==0x3da6008u?(uint32_t)b->zero_physical:(uint32_t)(b->zero_physical>>32));
 for(unsigned i=0;i<135;i++)if(a==hsnp_tcm_words[i].address)return value==hsnp_tcm_words[i].value && (!phase || phase==4);
 for(unsigned i=0;i<47;i++){const struct HsnpWriteRule*r=&hsnp_fixed_writes[i];if(a==r->address && width==r->width && value==r->value && (!phase || (r->phase_mask&(1u<<phase))))return 1;}
 return 0;
}
static inline int hsnp_allowed_write(const HsnpBindings*b,uint32_t a,unsigned width,uint64_t value){return hsnp_allowed_write_impl(b,a,width,value,0);}
static inline int hsnp_allowed_write_phase(const HsnpBindings*b,uint32_t a,unsigned width,uint64_t value,unsigned phase){return phase && hsnp_allowed_write_impl(b,a,width,value,phase);}
#endif
