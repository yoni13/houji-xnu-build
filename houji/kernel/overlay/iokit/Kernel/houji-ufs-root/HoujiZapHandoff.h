/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Fixed wire reservation, not an EFI pointer or a secure ownership attestation.
 */
#ifndef HOUJI_ZAP_HANDOFF_H
#define HOUJI_ZAP_HANDOFF_H
#include <stddef.h>
#include <stdint.h>
#define HZR_PROPERTY "houji,gpu-zap-reservation"
#define HZR_BYTES 512u
#define HZR_MAGIC UINT64_C(0x31303050415a4a48)
#define HZR_TARGET_PAGE UINT64_C(0x9b098000)
#define HZR_TARGET_GPU UINT64_C(0x9b09a000)
#define HZR_PIL_BASE UINT64_C(0x8bc00000)
#define HZR_PIL_BYTES UINT64_C(0x16f80000)
enum HzrOffset {HZR_MAGIC_O=0,HZR_VERSION_O=8,HZR_LENGTH_O=12,HZR_TRIAL_O=16,
 HZR_FLAGS_O=32,HZR_METADATA_PA_O=40,HZR_OWNED_O=48,HZR_LOGICAL_O=56,
 HZR_TARGET_PAGE_O=64,HZR_TARGET_BYTES_O=72,HZR_GPU_BASE_O=80,HZR_GPU_BYTES_O=88,
 HZR_COPY_BASE_O=96,HZR_COPY_BYTES_O=104,HZR_RAW_BASE_O=112,HZR_RAW_BYTES_O=120,
 HZR_META_EFI_O=128,HZR_TARGET_EFI_O=132,HZR_META_GCD_O=136,HZR_TARGET_GCD_O=140,
 HZR_META_GCD_BASE_O=144,HZR_META_GCD_BYTES_O=152,HZR_META_ATTR_O=160,HZR_META_CAP_O=168,HZR_META_ORIGINAL_O=176,
 HZR_TARGET_GCD_BASE_O=184,HZR_TARGET_GCD_BYTES_O=192,HZR_TARGET_ATTR_O=200,HZR_TARGET_CAP_O=208,
 HZR_META_RPAR_O=216,HZR_META_WPAR_O=248,HZR_TARGET_RPAR_O=280,HZR_TARGET_WPAR_O=312,
 HZR_MAP_KEY_O=344,HZR_MAP_BYTES_O=352,HZR_MAP_STRIDE_O=360,HZR_MAP_VERSION_O=368,
 HZR_META_COVERAGE_O=372,HZR_TARGET_COVERAGE_O=376,HZR_TARGET_RECORDS_O=380,HZR_CRC_O=508};
enum {HZR_FLAGS_READY=31u};
typedef struct HzrRecord {uint8_t bytes[HZR_BYTES];} HzrRecord;
static inline uint32_t hzr_u32(const void*p){const uint8_t*b=(const uint8_t*)p;return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static inline uint64_t hzr_u64(const void*p){const uint8_t*b=(const uint8_t*)p;return hzr_u32(b)|((uint64_t)hzr_u32(b+4)<<32);}
static inline void hzr_w32(void*p,uint32_t v){uint8_t*b=(uint8_t*)p;for(unsigned i=0;i<4;i++)b[i]=(uint8_t)(v>>(8*i));}
static inline void hzr_w64(void*p,uint64_t v){uint8_t*b=(uint8_t*)p;for(unsigned i=0;i<8;i++)b[i]=(uint8_t)(v>>(8*i));}
static inline uint64_t hzr_get64(const HzrRecord*r,unsigned o){return hzr_u64(r->bytes+o);}
static inline uint32_t hzr_get32(const HzrRecord*r,unsigned o){return hzr_u32(r->bytes+o);}
static inline void hzr_set64(HzrRecord*r,unsigned o,uint64_t v){hzr_w64(r->bytes+o,v);}
static inline void hzr_set32(HzrRecord*r,unsigned o,uint32_t v){hzr_w32(r->bytes+o,v);}
static inline uint32_t hzr_crc(const void*p,size_t n){const uint8_t*b=(const uint8_t*)p;uint32_t c=~0u;for(size_t i=0;i<n;i++){c^=b[i];for(unsigned j=0;j<8;j++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return ~c;}
static inline int hzr_span(uint64_t a,uint64_t n){return a && n && a<=UINT64_MAX-n;}
static inline int hzr_overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a<b+m && b<a+n;}
static inline int hzr_covers(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return hzr_span(a,n)&&hzr_span(b,m)&&b>=a&&m<=n&&b-a<=n-m;}
static inline int hzr_par(uint64_t v,uint64_t pa){return !(v&1) && (v>>56)==0x44 && ((v>>7)&3)!=1 && (v&UINT64_C(0x0000fffffffff000))==pa;}
static inline int hzr_equal(const void*a,const void*b,size_t n){const uint8_t*x=(const uint8_t*)a,*y=(const uint8_t*)b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return 0;return 1;}
static inline int hzr_zero(const void*a,size_t n){const uint8_t*x=(const uint8_t*)a;for(size_t i=0;i<n;i++)if(x[i])return 0;return 1;}
static inline void hzr_finish(HzrRecord*r){hzr_set32(r,HZR_CRC_O,hzr_crc(r->bytes,HZR_CRC_O));}
static inline int hzr_valid(const void*data,size_t n,const uint8_t trial[16]){
 if(!data || !trial || n!=HZR_BYTES || !hzr_span((uintptr_t)data,n) || hzr_zero(trial,16))return 0;
 const HzrRecord*r=(const HzrRecord*)data;
 if(hzr_get64(r,0)!=HZR_MAGIC || hzr_get32(r,8)!=1 || hzr_get32(r,12)!=HZR_BYTES || !hzr_equal(r->bytes+16,trial,16) ||
  hzr_get32(r,32)!=HZR_FLAGS_READY || !hzr_zero(r->bytes+36,4) || !hzr_zero(r->bytes+384,124) || hzr_get32(r,HZR_CRC_O)!=hzr_crc(r->bytes,HZR_CRC_O))return 0;
 uint64_t p=hzr_get64(r,HZR_METADATA_PA_O),raw=hzr_get64(r,HZR_RAW_BASE_O),rawbytes=hzr_get64(r,HZR_RAW_BYTES_O);
 if(!p || (p&0x3fff) || p>UINT64_C(0xffffc000) || hzr_get64(r,HZR_OWNED_O)!=16384 || hzr_get64(r,HZR_LOGICAL_O)!=4096 ||
  !hzr_span(raw,rawbytes) || (raw&4095) || rawbytes!=7u*4096 || raw+rawbytes>UINT64_C(0x100000000) || !hzr_covers(raw,rawbytes,p,16384) ||
  hzr_overlap(raw,rawbytes,HZR_TARGET_PAGE,16384))return 0;
 if(hzr_get64(r,HZR_TARGET_PAGE_O)!=HZR_TARGET_PAGE || hzr_get64(r,HZR_TARGET_BYTES_O)!=16384 || hzr_get64(r,HZR_GPU_BASE_O)!=HZR_TARGET_GPU ||
  hzr_get64(r,HZR_GPU_BYTES_O)!=8192 || hzr_get64(r,HZR_COPY_BASE_O)!=HZR_TARGET_GPU || hzr_get64(r,HZR_COPY_BYTES_O)!=1072 ||
  hzr_get32(r,HZR_META_EFI_O)!=2 || hzr_get32(r,HZR_TARGET_EFI_O)!=0 || hzr_get32(r,HZR_META_GCD_O)!=2 || hzr_get32(r,HZR_TARGET_GCD_O)!=1)return 0;
 uint64_t cachemask=31;
 if(!hzr_covers(hzr_get64(r,HZR_META_GCD_BASE_O),hzr_get64(r,HZR_META_GCD_BYTES_O),p,16384) ||
  (hzr_get64(r,HZR_META_ORIGINAL_O)&cachemask)!=8 || (hzr_get64(r,HZR_META_ATTR_O)&cachemask)!=2 || !(hzr_get64(r,HZR_META_CAP_O)&2) ||
  ((hzr_get64(r,HZR_META_ORIGINAL_O)|hzr_get64(r,HZR_META_ATTR_O))&(UINT64_C(0x20000)|UINT64_C(0x2000))))return 0;
 if(hzr_get64(r,HZR_TARGET_GCD_BASE_O)!=HZR_PIL_BASE || hzr_get64(r,HZR_TARGET_GCD_BYTES_O)!=HZR_PIL_BYTES ||
  hzr_get64(r,HZR_TARGET_ATTR_O)!=0 || (hzr_get64(r,HZR_TARGET_CAP_O)&cachemask)!=1 ||
  hzr_get32(r,HZR_META_COVERAGE_O)!=15 || hzr_get32(r,HZR_TARGET_COVERAGE_O)!=15 || !hzr_get32(r,HZR_TARGET_RECORDS_O) ||
  !hzr_get64(r,HZR_MAP_BYTES_O) || hzr_get64(r,HZR_MAP_BYTES_O)>65536 || hzr_get64(r,HZR_MAP_STRIDE_O)<40 ||
  (hzr_get64(r,HZR_MAP_STRIDE_O)&7) || hzr_get64(r,HZR_MAP_BYTES_O)%hzr_get64(r,HZR_MAP_STRIDE_O) || hzr_get32(r,HZR_MAP_VERSION_O)!=1)return 0;
 for(unsigned i=0;i<4;i++){
  uint64_t mr=hzr_get64(r,HZR_META_RPAR_O+i*8),mw=hzr_get64(r,HZR_META_WPAR_O+i*8),tr=hzr_get64(r,HZR_TARGET_RPAR_O+i*8),tw=hzr_get64(r,HZR_TARGET_WPAR_O+i*8);
  if(mr!=mw || tr!=tw || !hzr_par(mr,p+i*4096) || !hzr_par(tr,HZR_TARGET_PAGE+i*4096))return 0;
 }
 return 1;
}
/* Strict ADT scalar slot: unique property, /chosen depth1, canonical names,
 * bounded recursion, 32-byte property names, zero padding, no flags/trailing. */
struct HzrAdt {const uint8_t*data;size_t bytes,offset,slot;unsigned matches;};
static inline int hzr_name(const uint8_t*p,size_t n,const char*s){size_t i=0;for(;i<n&&s[i];i++)if(p[i]!=(uint8_t)s[i])return 0;if(i==n || p[i])return 0;for(;i<n;i++)if(p[i])return 0;return 1;}
static inline int hzr_adt_node(struct HzrAdt*t,unsigned depth){
 if(depth>64 || t->offset>t->bytes || t->bytes-t->offset<8)return 0;
 uint32_t props=hzr_u32(t->data+t->offset),children=hzr_u32(t->data+t->offset+4);t->offset+=8;
 if(props>256 || props>(t->bytes-t->offset)/36)return 0;
 unsigned chosen=0,name_seen=0,target=0;
 for(uint32_t i=0;i<props;i++){
  if(t->bytes-t->offset<36)return 0;const uint8_t*name=t->data+t->offset;size_t stop=0;while(stop<32&&name[stop]){if(name[stop]<0x21||name[stop]>0x7e||name[stop]=='/')return 0;stop++;}
  if(!stop || stop==32 || !hzr_zero(name+stop,32-stop))return 0;
  uint32_t n=hzr_u32(name+32);t->offset+=36;if(n&0x80000000u || n>t->bytes-t->offset)return 0;
  size_t padded=((size_t)n+3)&~(size_t)3;if(padded>t->bytes-t->offset || !hzr_zero(t->data+t->offset+n,padded-n))return 0;
  if(hzr_name(name,32,"name")){if(name_seen++ || !n || !hzr_zero(t->data+t->offset+n-1,1))return 0;chosen=depth==1&&hzr_name(t->data+t->offset,n,"chosen");if(depth==0&&!hzr_name(t->data+t->offset,n,"device-tree"))return 0;}
  if(hzr_name(name,32,HZR_PROPERTY)){if(n!=HZR_BYTES || ++t->matches!=1)return 0;t->slot=t->offset;target++;}
  t->offset+=padded;
 }
 if(name_seen!=1 || (target&&!chosen) || children>(t->bytes-t->offset)/8)return 0;
 for(uint32_t i=0;i<children;i++)if(!hzr_adt_node(t,depth+1))return 0;
 return 1;
}
static inline int hzr_adt_find(const void*data,size_t n,size_t*slot){if(!data || !slot || !n || n>1048576 || !hzr_span((uintptr_t)data,n))return 0;struct HzrAdt t={(const uint8_t*)data,n,0,0,0};if(!hzr_adt_node(&t,0)||t.offset!=n||t.matches!=1)return 0;*slot=t.slot;return 1;}
#endif
