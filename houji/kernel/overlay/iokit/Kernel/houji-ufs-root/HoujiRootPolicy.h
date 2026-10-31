#define HOUJI_ROOT_GIB 32
// SPDX-License-Identifier: BSD-2-Clause-Patent
static const uint32_t kHoujiRootFirst=2260480U;
// Existing qualified images keep 12 GiB. A newly built, separately verified
// volume may opt into 32 GiB; this never broadens the physical partition.
#ifndef HOUJI_ROOT_GIB
#define HOUJI_ROOT_GIB 12
#endif
static_assert(HOUJI_ROOT_GIB==12 || HOUJI_ROOT_GIB==32,"Unqualified root geometry");
static const uint32_t kHoujiRootBlocks=HOUJI_ROOT_GIB*262144U;
static const uint64_t kHoujiRootBytes=uint64_t(HOUJI_ROOT_GIB)*1073741824ULL;
static bool houjiUfsRootCommandAllowed(const uint8_t *u,unsigned n,unsigned dir,uint32_t bytes,bool enabled,bool armed) {
  if(!enabled || !u || n!=32 || u[0]!=1 || u[2] || u[3])return false;
  for(unsigned i=4;i<12;i++)if(u[i])return false;
  const uint8_t *c=u+16;
  uint32_t transfer=(uint32_t(u[12])<<24)|(uint32_t(u[13])<<16)|(uint32_t(u[14])<<8)|u[15];
  if(c[0]==0x35) {
    if(!armed || dir || bytes || transfer || u[1])return false;
    for(unsigned i=1;i<16;i++)if(c[i])return false;
    return true;
  }
  if(c[0]!=0x28 && c[0]!=0x2a)return false;
  bool write=c[0]==0x2a;
  if((write && !armed) || !bytes || bytes>131072 || (bytes&4095) || transfer!=bytes ||
     u[1]!=(write?0x20:0x40) || dir!=(write?1U:2U) || c[1] || c[6])return false;
  for(unsigned i=9;i<16;i++)if(c[i])return false;
  uint32_t lba=(uint32_t(c[2])<<24)|(uint32_t(c[3])<<16)|(uint32_t(c[4])<<8)|c[5];
  unsigned blocks=(unsigned(c[7])<<8)|c[8];
  return lba>=kHoujiRootFirst && lba-kHoujiRootFirst<kHoujiRootBlocks && blocks>0 &&
    blocks<=kHoujiRootBlocks-(lba-kHoujiRootFirst) && blocks*4096U==bytes;
}
static bool houjiUfsRecoveryCommandAllowed(const uint8_t *u,unsigned n,unsigned dir,uint32_t bytes,bool enabled,bool armed) {
  if(!enabled || !u || n!=32 || u[0]!=1 || u[2] || u[3])return false;
  for(unsigned i=4;i<12;i++)if(u[i])return false;
  const uint8_t *c=u+16;
  uint32_t transfer=(uint32_t(u[12])<<24)|(uint32_t(u[13])<<16)|(uint32_t(u[14])<<8)|u[15];
  if(c[0]==0x35) {
    if(!armed || dir || bytes || transfer || u[1])return false;
    for(unsigned i=1;i<16;i++)if(c[i])return false;
    return true;
  }
  bool write=c[0]==0x2a;
  if(c[0]!=0x28 && !write)return false;
  if((write&&!armed) || dir!=(write?1U:2U) || bytes!=4096 || transfer!=4096 || u[1]!=(write?0x20:0x40))return false;
  static const uint8_t cdb[16]={0x28,0,0,0,4,0,0,0,1,0,0,0,0,0,0,0};
  for(unsigned i=1;i<16;i++)if(c[i]!=cdb[i])return false;
  return true;
}
