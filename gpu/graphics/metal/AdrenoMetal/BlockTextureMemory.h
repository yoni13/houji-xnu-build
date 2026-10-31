// SPDX-License-Identifier: MIT
// Our MTLTextureLayout's compressed block rows. This is not an AGX tile decoder.
#pragma once
#include "LinearTextureMemory.h"
namespace native_linear {
struct BlockLayout { Layout blocks; uint64_t width,height,bw,bh; };
inline bool blockPlan(uint64_t width,uint64_t height,uint64_t bw,uint64_t bh,
                      uint64_t blockBytes,uint64_t alignment,BlockLayout &out) {
    if(!width || !height || !bw || !bh)return false;
    const uint64_t across=width/bw+(width%bw!=0),rows=height/bh+(height%bh!=0);
    uint64_t row;Layout blocks;
    if(!multiply(across,blockBytes,row) || !plan(across,rows,blockBytes,row,alignment,blocks))return false;
    out={blocks,width,height,bw,bh};return true;
}
inline bool blockCopy(const BlockLayout &l,void *texture,void *linear,uint64_t offset,
                      uint64_t row,uint64_t image,uint64_t slice,uint64_t level,
                      const Region &r,bool toTexture) {
    if(r.z || r.depth!=1 || !r.width || !r.height || r.x%l.bw || r.y%l.bh ||
       r.x>l.width || r.width>l.width-r.x || r.y>l.height || r.height>l.height-r.y ||
       (r.width%l.bw && r.width!=l.width-r.x) ||
       (r.height%l.bh && r.height!=l.height-r.y))return false;
    const Region blocks={r.x/l.bw,r.y/l.bh,0,
        r.width/l.bw+(r.width%l.bw!=0),r.height/l.bh+(r.height%l.bh!=0),1};
    return copy(l.blocks,texture,linear,offset,row,image,slice,level,blocks,toTexture);
}
}
