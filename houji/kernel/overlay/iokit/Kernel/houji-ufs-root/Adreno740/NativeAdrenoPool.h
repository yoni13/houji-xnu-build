/* SPDX-License-Identifier: MIT */
/* Best-fit/adjacent-merge policy adapted from Adreno740 5c8c1453.
 * Offsets always own complete CPU pages; callers serialize all operations. */
#ifndef NATIVE_ADRENO_POOL_H
#define NATIVE_ADRENO_POOL_H
#include <stddef.h>
struct NativeAdrenoPoolExtents {
    struct Extent {size_t offset,bytes;};
    static constexpr unsigned Capacity=512;
    Extent free[Capacity]{};
    size_t bytes=0,page=0,used=0,peak=0;
    unsigned count=0;
    bool initialize(size_t size,size_t granule) {
        if(bytes || (granule!=4096 && granule!=16384) || !size || (size&(granule-1)))return false;
        bytes=size;page=granule;free[0]={0,size};count=1;return true;
    }
    bool take(size_t size,size_t *offset) {
        if(!offset || !page || !size || (size&(page-1)) || size>bytes-used)return false;
        unsigned best=count;
        for(unsigned i=0;i<count;++i)
            if(free[i].bytes>=size && (best==count || free[i].bytes<free[best].bytes))best=i;
        if(best==count)return false;
        *offset=free[best].offset;free[best].offset+=size;free[best].bytes-=size;
        if(!free[best].bytes) {for(unsigned i=best;i+1<count;++i)free[i]=free[i+1];--count;}
        used+=size;if(used>peak)peak=used;return true;
    }
    bool give(size_t offset,size_t size) {
        if(!page || !size || ((offset|size)&(page-1)) || offset>bytes || size>bytes-offset || size>used)return false;
        unsigned at=0;while(at<count && free[at].offset<offset)++at;
        if((at && free[at-1].offset+free[at-1].bytes>offset) ||
            (at<count && offset+size>free[at].offset))return false;
        const bool left=at && free[at-1].offset+free[at-1].bytes==offset;
        const bool right=at<count && offset+size==free[at].offset;
        if(left) {
            free[at-1].bytes+=size;
            if(right) {
                free[at-1].bytes+=free[at].bytes;
                for(unsigned i=at;i+1<count;++i)free[i]=free[i+1];--count;
            }
        } else if(right) {free[at].offset=offset;free[at].bytes+=size;}
        else {
            /* Keep ownership on metadata exhaustion, never lose/reuse pages. */
            if(count==Capacity)return false;
            for(unsigned i=count;i>at;--i)free[i]=free[i-1];free[at]={offset,size};++count;
        }
        used-=size;return true;
    }
};
#endif
