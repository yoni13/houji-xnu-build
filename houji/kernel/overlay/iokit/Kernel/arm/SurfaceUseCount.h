// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
enum class HoujiSurfaceUseResult { Updated, Underflow, Overflow };
static inline HoujiSurfaceUseResult houjiSurfaceUseAdjust(volatile uint64_t *value,
                                                         bool up,uint64_t &after) {
    uint64_t old=__atomic_load_n(value,__ATOMIC_ACQUIRE);
    for(;;){
        if(up && old==UINT64_MAX){after=old;return HoujiSurfaceUseResult::Overflow;}
        if(!up && old==0){after=0;return HoujiSurfaceUseResult::Underflow;}
        uint64_t next=up?old+1:old-1;
        if(__atomic_compare_exchange_n(value,&old,next,true,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
            after=next;return HoujiSurfaceUseResult::Updated;
        }
    }
}
