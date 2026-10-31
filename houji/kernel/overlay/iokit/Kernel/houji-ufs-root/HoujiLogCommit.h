// SPDX-License-Identifier: BSD-2-Clause-Patent
// Header-last single-record transaction. Caller verifies identity/allocation and
// initial run ID first; the transport gate independently enforces exact LBAs.
template<class Write,class Flush,class Read>
static const char *houjiCommitLog(const uint8_t *image,unsigned bytes,
                                 Write write,Flush flush,Read read) {
  if(!image || bytes!=65536)return "log image size";
  for(unsigned i=1;i<16;i++) if(!write(i,image+i*4096))return "log payload write failed";
  if(!flush())return "log payload flush failed";
  if(!write(0,image))return "log header commit failed";
  if(!flush())return "log header flush failed";
  uint8_t block[4096];
  for(unsigned i=0;i<16;i++) {
    if(!read(i,block))return "committed log readback failed";
    for(unsigned j=0;j<4096;j++) if(block[j]!=image[i*4096+j])return "committed log bytes differ";
  }
  return nullptr;
}
