/* SPDX-License-Identifier: MIT
 * CPU wall-time observations only. No allocation, waiting, MMIO or GPU access.
 * Atomic fields let the checkpoint read without taking the GPU issuer lock.
 */
#ifndef NATIVE_ADRENO_LATENCY_H
#define NATIVE_ADRENO_LATENCY_H
#include <stdint.h>

struct NativeAdrenoLatency {
    struct Method { uint64_t calls,total,maximum,phase[4],phaseMax[4]; } methods[20]{};
    struct Slow { uint64_t sequence,values[9]; } slow[8]{};
    uint64_t records=0,dropped=0;
    uint32_t writing=0;

    static uint64_t load(const uint64_t *p) { return __atomic_load_n(p,__ATOMIC_RELAXED); }
    static void maximum(uint64_t *p,uint64_t value) {
        uint64_t old=load(p);
        while(value>old && !__atomic_compare_exchange_n(p,&old,value,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {}
    }
    void record(unsigned method,uint64_t start,uint64_t elapsed,const uint64_t phases[4],uint32_t status) {
        if(method>=20)return;
        auto &m=methods[method];
        __atomic_fetch_add(&m.calls,1ull,__ATOMIC_RELAXED);
        __atomic_fetch_add(&m.total,elapsed,__ATOMIC_RELAXED);
        maximum(&m.maximum,elapsed);
        for(unsigned i=0;i<4;i++) {
            __atomic_fetch_add(&m.phase[i],phases[i],__ATOMIC_RELAXED);
            maximum(&m.phaseMax[i],phases[i]);
        }
        if(elapsed<20000)return;
        if(__atomic_exchange_n(&writing,1u,__ATOMIC_ACQUIRE)) {
            __atomic_fetch_add(&dropped,1ull,__ATOMIC_RELAXED);return;
        }
        uint64_t seq=load(&records)+1;
        auto &r=slow[(seq-1)%8];
        __atomic_store_n(&r.sequence,0ull,__ATOMIC_SEQ_CST);
        uint64_t v[9]={method,start,elapsed,phases[0],phases[1],phases[2],phases[3],status,seq};
        for(unsigned i=0;i<9;i++)__atomic_store_n(&r.values[i],v[i],__ATOMIC_RELAXED);
        __atomic_store_n(&r.sequence,seq,__ATOMIC_RELEASE);
        __atomic_store_n(&records,seq,__ATOMIC_RELEASE);
        __atomic_store_n(&writing,0u,__ATOMIC_RELEASE);
    }
    template<class Append> void snapshot(Append append) const {
        append("HOUJI LATENCY: slow-count=%llu dropped=%llu threshold-us=20000; phases=lock/gate/core/other; CPU wall time, not GPU time\n",load(&records),load(&dropped));
        for(unsigned i=0;i<20;i++) {
            const auto &m=methods[i];if(!load(&m.calls))continue;
            append("HOUJI METHOD: id=%u n=%llu us=%llu max=%llu phases=%llu/%llu/%llu/%llu peaks=%llu/%llu/%llu/%llu\n",i,
                load(&m.calls),load(&m.total),load(&m.maximum),load(&m.phase[0]),load(&m.phase[1]),load(&m.phase[2]),load(&m.phase[3]),
                load(&m.phaseMax[0]),load(&m.phaseMax[1]),load(&m.phaseMax[2]),load(&m.phaseMax[3]));
        }
        uint64_t latest=__atomic_load_n(&records,__ATOMIC_ACQUIRE);
        for(unsigned i=0;i<8 && latest>i;i++) {
            uint64_t seq=latest-i;const auto &r=slow[(seq-1)%8];uint64_t v[9];
            if(__atomic_load_n(&r.sequence,__ATOMIC_ACQUIRE)!=seq)continue;
            for(unsigned j=0;j<9;j++)v[j]=load(&r.values[j]);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            if(__atomic_load_n(&r.sequence,__ATOMIC_ACQUIRE)!=seq || v[8]!=seq)continue;
            append("HOUJI SLOW: seq=%llu id=%llu start-us=%llu us=%llu lock=%llu gate=%llu core=%llu other=%llu status=%llx\n",
                seq,v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]);
        }
    }
};
#endif
