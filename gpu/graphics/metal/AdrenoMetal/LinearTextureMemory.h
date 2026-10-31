// SPDX-License-Identifier: MIT
// CPU rows for our own MTLTextureLayout, never an inferred Apple tiled layout.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>

namespace native_linear {
struct Layout {uint64_t width,height,texel,row,bytes,alignment;};
struct Region {uint64_t x,y,z,width,height,depth;};
inline bool multiply(uint64_t a,uint64_t b,uint64_t &out) {
    if(b && a>UINT64_MAX/b)return false;out=a*b;return true;
}
inline bool add(uint64_t a,uint64_t b,uint64_t &out) {
    if(a>UINT64_MAX-b)return false;out=a+b;return true;
}
inline bool plan(uint64_t width,uint64_t height,uint64_t texel,uint64_t row,
                 uint64_t alignment,Layout &out) {
    uint64_t tight,bytes,rounded;
    if(!width || !height || !texel || !alignment || (alignment&(alignment-1)) ||
       !multiply(width,texel,tight) || row<tight || !multiply(row,height,bytes) ||
       !add(bytes,alignment-1,rounded))return false;
    rounded&=~(alignment-1);
    if(!rounded || rounded>SIZE_MAX)return false;
    out={width,height,texel,row,rounded,alignment};return true;
}
// The caller owns both allocations. Validate every span before copying the
// first row, including the source/destination offset and pointer arithmetic.
inline bool copy(const Layout &l,void *texture,void *linear,uint64_t linearOffset,
                 uint64_t linearRow,uint64_t linearImage,uint64_t slice,uint64_t level,
                 const Region &r,bool toTexture) {
    if(!texture || !linear || slice || level || r.z || r.depth!=1 ||
       !r.width || !r.height || r.x>l.width || r.width>l.width-r.x ||
       r.y>l.height || r.height>l.height-r.y)return false;
    uint64_t rowBytes,lastLinear,linearEnd,textureOffset,xBytes,lastTexture,textureEnd;
    if(!multiply(r.width,l.texel,rowBytes) || linearRow<rowBytes ||
       !multiply(r.height-1,linearRow,lastLinear) || !add(lastLinear,rowBytes,lastLinear) ||
       !add(linearOffset,lastLinear,linearEnd) ||
       (linearImage && lastLinear>linearImage) ||
       !multiply(r.y,l.row,textureOffset) || !multiply(r.x,l.texel,xBytes) ||
       !add(textureOffset,xBytes,textureOffset) || !multiply(r.height-1,l.row,lastTexture) ||
       !add(textureOffset,lastTexture,textureEnd) || !add(textureEnd,rowBytes,textureEnd) ||
       textureEnd>l.bytes || linearEnd>SIZE_MAX ||
       uintptr_t(texture)>UINTPTR_MAX-textureEnd || uintptr_t(linear)>UINTPTR_MAX-linearEnd)return false;
    auto *t=static_cast<uint8_t *>(texture)+textureOffset;
    auto *b=static_cast<uint8_t *>(linear)+linearOffset;
    const uintptr_t ta=uintptr_t(t),ba=uintptr_t(b);
    if(ta<uintptr_t(linear)+linearEnd && ba<uintptr_t(texture)+textureEnd)return false;
    for(uint64_t y=0;y<r.height;y++) {
        if(toTexture)memmove(t,b,size_t(rowBytes));else memmove(b,t,size_t(rowBytes));
        if(y+1<r.height){t+=l.row;b+=linearRow;}
    }
    return true;
}
} // namespace native_linear
