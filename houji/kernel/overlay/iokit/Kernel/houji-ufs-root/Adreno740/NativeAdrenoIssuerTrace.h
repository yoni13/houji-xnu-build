/* SPDX-License-Identifier: MIT
 * CPU mutex observations only. Writers already own the issuer mutex. No
 * allocation, MMIO, waiting, or resource lifetime changes. Readers never lock.
 */
#ifndef NATIVE_ADRENO_ISSUER_TRACE_H
#define NATIVE_ADRENO_ISSUER_TRACE_H
#include <stdint.h>

struct NativeAdrenoIssuerTrace {
    // requested/acquired/released, source line, ABI method (or UINT32_MAX),
    // client slot (or UINT32_MAX), subject, last issued fence, observed fence.
    struct Entry {uint64_t sequence=0, values[9]{};};
    Entry recent[64]{}, longestHold[3]{}, longestWait[3]{};
    uint64_t active[9]{}, calls=0, records=0, invalid=0;
    bool enabled=false; // Set before publication; immutable while in use.

    static uint64_t load(const uint64_t *p) {return __atomic_load_n(p,__ATOMIC_RELAXED);}
    static void write(Entry &entry,uint64_t seq,const uint64_t values[9]) {
        __atomic_store_n(&entry.sequence,0ull,__ATOMIC_SEQ_CST);
        for(unsigned i=0;i<9;++i)__atomic_store_n(&entry.values[i],values[i],__ATOMIC_RELAXED);
        __atomic_store_n(&entry.sequence,seq,__ATOMIC_RELEASE);
    }
    static bool read(const Entry &entry,uint64_t *seq,uint64_t values[9]) {
        const uint64_t before=__atomic_load_n(&entry.sequence,__ATOMIC_ACQUIRE);
        if(!before)return false;
        for(unsigned i=0;i<9;++i)values[i]=load(&entry.values[i]);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if(before!=__atomic_load_n(&entry.sequence,__ATOMIC_ACQUIRE))return false;
        *seq=before;return true;
    }
    void begin(uint64_t requested,uint64_t acquired,unsigned site) {
        if(!enabled)return;
        const uint64_t values[9]={requested,acquired,0,site,UINT32_MAX,UINT32_MAX,0,0,0};
        for(unsigned i=0;i<9;++i)active[i]=values[i];
    }
    void context(unsigned method,unsigned client,uint64_t subject) {
        if(!enabled)return;
        active[4]=method;active[5]=client;active[6]=subject;
    }
    void subject(uint64_t value) {if(enabled)active[6]=value;}
    static uint64_t duration(const Entry &entry,bool hold) {
        return load(&entry.values[hold?2:1])-load(&entry.values[hold?1:0]);
    }
    void end(uint64_t released,uint32_t issued,uint32_t observed) {
        if(!enabled)return;
        __atomic_fetch_add(&calls,1ull,__ATOMIC_RELAXED);
        if(active[1]<active[0] || released<active[1]) {
            __atomic_fetch_add(&invalid,1ull,__ATOMIC_RELAXED);return;
        }
        active[2]=released;active[7]=issued;active[8]=observed;
        const uint64_t held=released-active[1],waited=active[1]-active[0];
        if(held<1000 && waited<1000)return;
        const uint64_t seq=load(&records)+1;
        write(recent[(seq-1)%64],seq,active);
        // Retain three longest holds and waits even after later activity laps
        // the recent ring. Equal durations keep the earlier observation.
        for(unsigned kind=0;kind<2;++kind) {
            Entry *entries=kind?longestHold:longestWait;
            unsigned at=0;
            for(unsigned i=1;i<3;++i)if(duration(entries[i],kind)<duration(entries[at],kind))at=i;
            if((kind?held:waited)>duration(entries[at],kind))write(entries[at],seq,active);
        }
        __atomic_store_n(&records,seq,__ATOMIC_RELEASE);
    }
    template<class Append> void snapshot(Append append) const {
        if(!enabled)return;
        append("HOUJI ISSUER: calls=%llu slow=%llu invalid=%llu threshold-us=1000; site=NativeAdrenoPublish.cpp line; method/client=4294967295 when unspecified; CPU intervals, not GPU timestamps\n",load(&calls),load(&records),load(&invalid));
        uint64_t printed[12]{},count=0;
        auto emit=[&](const Entry &entry) {
            uint64_t seq=0,v[9];if(!read(entry,&seq,v))return;
            for(unsigned i=0;i<count;++i)if(printed[i]==seq)return;
            if(count>=12)return;printed[count++]=seq;
            append("HOUJI LOCK: seq=%llu site=%llu method=%llu client=%llu subject=%llu request=%llu acquired=%llu released=%llu issued=%llu seen=%llu\n",seq,v[3],v[4],v[5],v[6],v[0],v[1],v[2],v[7],v[8]);
        };
        for(const auto &entry:longestHold)emit(entry);
        for(const auto &entry:longestWait)emit(entry);
        const uint64_t latest=__atomic_load_n(&records,__ATOMIC_ACQUIRE);
        for(unsigned i=0;i<6 && latest>i;++i)emit(recent[(latest-i-1)%64]);
    }
};
#endif
