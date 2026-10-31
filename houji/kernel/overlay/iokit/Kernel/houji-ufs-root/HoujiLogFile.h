// SPDX-License-Identifier: BSD-2-Clause-Patent
// Pure FAT12 validation; no allocations or I/O. HoujiGptCheck.h supplies LE/CRC.
static unsigned houjiLe16(const uint8_t *p) {return unsigned(p[0])|(unsigned(p[1])<<8);}
static unsigned houjiFatNext(const uint8_t *fat,unsigned c) {
  unsigned p=houjiLe16(fat+c+c/2);return c&1?p>>4:p&0xfff;
}
static bool houjiLogCluster(unsigned c) {
  for(unsigned i=0;i<16;i++) if(c==kHoujiLogClusters[i]) return true;
  return false;
}
static const char *houjiLogFatError(const uint8_t *m,unsigned n) {
  if(!m || n!=7*4096 || m[510]!=0x55 || m[511]!=0xaa) return "logfs boot signature";
  if(houjiLe16(m+11)!=4096 || m[13]!=1 || houjiLe16(m+14)!=1 || m[16]!=2 ||
     houjiLe16(m+17)!=512 || houjiLe16(m+19)!=1280 || m[21]!=0xf8 ||
     houjiLe16(m+22)!=1 || houjiLe32(m+32)!=0) return "logfs FAT12 geometry";
  const uint8_t *fat=m+4096;
  for(unsigned i=0;i<4096;i++) if(fat[i]!=fat[4096+i]) return "logfs FAT copies differ";
  for(unsigned i=0;i<16;i++) {
    unsigned c=kHoujiLogClusters[i];
    if(c<2 || c>=1275 || kHoujiLogLbas[i]!=4096+7+c-2) return "log allocation bounds";
    for(unsigned j=0;j<i;j++) if(kHoujiLogClusters[j]==c) return "log allocation duplicate";
    unsigned next=houjiFatNext(fat,c);
    if(i<15 ? next!=kHoujiLogClusters[i+1] : next<0xff8) return "native log FAT chain changed";
  }
  static const uint8_t name[11]={'X','N','U','L','O','G',' ',' ','B','I','N'};
  unsigned found=0;
  for(unsigned at=3*4096;at<7*4096;at+=32) {
    const uint8_t *e=m+at;
    if(!e[0]) break;
    if(e[0]==0xe5 || e[11]==15 || (e[11]&8)) continue;
    if((e[11]&0x10) || houjiLe16(e+20)) return "unexpected FAT directory/cluster";
    bool own=true;for(unsigned i=0;i<11;i++) if(e[i]!=name[i]) own=false;
    unsigned size=houjiLe32(e+28),start=houjiLe16(e+26);
    if(own) {
      if(++found!=1 || size!=65536 || start!=kHoujiLogClusters[0] || (e[11]&1))
        return "native log directory entry changed";
      continue;
    }
    // OEM log names may alias each other; none may alias our reserved file.
    if(size>1280U*4096U) return "other log size outside FAT";
    unsigned c=start,count=(size+4095)/4096;
    for(unsigned i=0;i<count;i++) {
      if(c<2 || c>=1275 || houjiLogCluster(c)) return "native log overlaps another file";
      c=houjiFatNext(fat,c);
    }
    if(count && c<0xff8) return "other file chain malformed";
  }
  return found==1 ? nullptr : "native log missing";
}
static void houjiStore32(uint8_t *p,uint32_t v) {
  for(unsigned i=0;i<4;i++) p[i]=uint8_t(v>>(i*8));
}
static bool houjiLogInitialValid(const uint8_t *p,unsigned n) {
  return p && n==65536 && houjiCrc32(p,n)==kHoujiLogInitialCrc;
}
static void houjiLogHeader(uint8_t *h,const uint8_t *payload,unsigned bytes) {
  for(unsigned i=0;i<4096;i++) h[i]=0;
  static const uint8_t magic[8]={'H','J','X','N','U','0','0','1'};
  for(unsigned i=0;i<8;i++) h[i]=magic[i];
  houjiStore32(h+8,1);houjiStore32(h+12,1);
  for(unsigned i=0;i<16;i++) h[16+i]=kHoujiLogRunId[i];
  houjiStore32(h+32,bytes);houjiStore32(h+36,houjiCrc32(payload,bytes));
  houjiStore32(h+40,houjiCrc32(h,4096));
}
