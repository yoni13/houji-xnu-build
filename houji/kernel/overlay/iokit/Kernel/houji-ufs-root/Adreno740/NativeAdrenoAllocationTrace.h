/* SPDX-License-Identifier: MIT
 * Bounded CPU observations. No allocation, locks, MMIO or cache operations.
 */
#ifndef NATIVE_ADRENO_ALLOCATION_TRACE_H
#define NATIVE_ADRENO_ALLOCATION_TRACE_H
#include "NativeAdrenoLatency.h"

struct NativeAdrenoAllocationTrace {
    enum Route {Pool,Standalone,WriteCombine,Routes};
    struct Counter {uint64_t calls,failures,bytes,total,maximum,phase[4];} counters[Routes]{};
    struct Slow {uint64_t sequence,values[10];} slow[4]{};
    uint64_t records=0,dropped=0;
    uint32_t writing=0;
    static uint64_t load(const uint64_t *p){return __atomic_load_n(p,__ATOMIC_RELAXED);}
    void record(unsigned route,uint64_t bytes,uint64_t start,const uint64_t phases[4],uint32_t status) {
        if(route>=Routes)return;
        uint64_t elapsed=0;auto &c=counters[route];
        for(unsigned i=0;i<4;++i){elapsed+=phases[i];__atomic_fetch_add(&c.phase[i],phases[i],__ATOMIC_RELAXED);}
        __atomic_fetch_add(&c.calls,1ull,__ATOMIC_RELAXED);
        __atomic_fetch_add(&c.failures,status?1ull:0ull,__ATOMIC_RELAXED);
        __atomic_fetch_add(&c.bytes,bytes,__ATOMIC_RELAXED);
        __atomic_fetch_add(&c.total,elapsed,__ATOMIC_RELAXED);
        NativeAdrenoLatency::maximum(&c.maximum,elapsed);
        if(elapsed<8000)return;
        if(__atomic_exchange_n(&writing,1u,__ATOMIC_ACQUIRE)){
            __atomic_fetch_add(&dropped,1ull,__ATOMIC_RELAXED);return;
        }
        const uint64_t seq=load(&records)+1;auto &r=slow[(seq-1)%4];
        __atomic_store_n(&r.sequence,0ull,__ATOMIC_SEQ_CST);
        const uint64_t v[10]={route,bytes,start,elapsed,phases[0],phases[1],phases[2],phases[3],status,seq};
        for(unsigned i=0;i<10;++i)__atomic_store_n(&r.values[i],v[i],__ATOMIC_RELAXED);
        __atomic_store_n(&r.sequence,seq,__ATOMIC_RELEASE);
        __atomic_store_n(&records,seq,__ATOMIC_RELEASE);
        __atomic_store_n(&writing,0u,__ATOMIC_RELEASE);
    }
    template<class Append> void snapshot(Append append) const {
        append("HOUJI BO TIMING: slow=%llu dropped=%llu threshold-us=8000 routes=pool/standalone/WC phases=backing-other/spans/cache-mode/zero-publish; CPU time only\n",load(&records),load(&dropped));
        for(unsigned i=0;i<Routes;++i){const auto &c=counters[i];if(!load(&c.calls))continue;
            append("HOUJI BO COST: route=%u n=%llu failed=%llu bytes=%llu us=%llu max=%llu phases=%llu/%llu/%llu/%llu\n",i,
                load(&c.calls),load(&c.failures),load(&c.bytes),load(&c.total),load(&c.maximum),
                load(&c.phase[0]),load(&c.phase[1]),load(&c.phase[2]),load(&c.phase[3]));
        }
        const uint64_t latest=__atomic_load_n(&records,__ATOMIC_ACQUIRE);
        for(unsigned i=0;i<4 && latest>i;++i){const uint64_t seq=latest-i;const auto &r=slow[(seq-1)%4];uint64_t v[10];
            if(__atomic_load_n(&r.sequence,__ATOMIC_ACQUIRE)!=seq)continue;
            for(unsigned j=0;j<10;++j)v[j]=load(&r.values[j]);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            if(__atomic_load_n(&r.sequence,__ATOMIC_ACQUIRE)!=seq || v[9]!=seq)continue;
            append("HOUJI BO SLOW: seq=%llu route=%llu bytes=%llu start-us=%llu us=%llu phases=%llu/%llu/%llu/%llu status=%llx\n",
                seq,v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8]);
        }
    }
};
extern NativeAdrenoAllocationTrace nativeAdrenoAllocationTrace;
extern "C" {extern uint64_t (*native_adreno_memory_now_us)(void);}
struct NativeAdrenoAllocationTimer {
    uint64_t (*now)(void)=__atomic_load_n(&native_adreno_memory_now_us,__ATOMIC_ACQUIRE);
    uint64_t began=now?now():0,last=began,phases[4]{},bytes;
    unsigned route;
    const int &status;
    NativeAdrenoAllocationTimer(unsigned r,uint64_t b,const int &s):bytes(b),route(r),status(s){}
    void lap(unsigned phase){if(now){uint64_t t=now();phases[phase]+=t>=last?t-last:0;last=t;}}
    ~NativeAdrenoAllocationTimer(){if(now){lap(0);nativeAdrenoAllocationTrace.record(route,bytes,began,phases,(uint32_t)status);}}
};
#endif
