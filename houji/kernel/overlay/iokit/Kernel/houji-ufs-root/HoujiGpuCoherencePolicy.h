// SPDX-License-Identifier: BSD-2-Clause-Patent
// CPU16K layout for UniXNU-sheng GpuPowerProbe.cpp's two-way coherence proof.
// Reference: 08cde20edc3254ed6f7df8d0f5384fb887786e8f, coherenceProbe.
#ifndef HOUJI_GPU_COHERENCE_POLICY_H
#define HOUJI_GPU_COHERENCE_POLICY_H
#include <stdint.h>
#include "Adreno740/AdrenoPM4.h"
#include "GpuKnownImage2d.h"
enum : uint32_t {HoujiCoherencePage=16384, HoujiCoherenceBytes=4*HoujiCoherencePage,
                HoujiCoherenceWords=1024, HoujiCoherenceAttempts=8};
static constexpr uint64_t HoujiCoherenceVa=0x40010000;
static constexpr uint64_t HoujiCoherenceCommandVa=0x40008000;
static inline bool houjiCoherenceUncachedAttribute(unsigned attribute) {
 // Darwin 22 wimg_to_pte(VM_WIMG_IO) uses Device-GRE (0x0c) for
 // DRAM. All four architectural Device types and Normal-NC bypass CPU
 // caches; neither write-through nor partially/fully cached Normal may pass.
 return attribute==0x00 || attribute==0x04 || attribute==0x08 ||
        attribute==0x0c || attribute==0x44;
}
static inline bool houjiCoherenceCopyPlan(unsigned direction,uint32_t out[7]) {
 if(direction>1 || !out)return false;
 uint64_t source=HoujiCoherenceVa+direction*2*HoujiCoherencePage;
 if(AdrenoPM4Type7(0x75,5,&out[0])!=AdrenoPM4Success ||
    AdrenoPM4Type7(0x12,0,&out[6])!=AdrenoPM4Success)return false;
 out[1]=HoujiCoherenceWords;out[2]=(uint32_t)source;out[3]=(uint32_t)(source>>32);
 out[4]=(uint32_t)(source+HoujiCoherencePage);out[5]=(uint32_t)((source+HoujiCoherencePage)>>32);
 return true;
}
static inline bool houjiCoherenceDrawWord(unsigned direction,unsigned index,uint32_t *out) {
 if(direction<2 || direction>3 || index>=HGKI_SELECTED_COMMAND_WORDS || !out)return false;
 uint32_t word=hgki_command_word(index);
 *out=word==HGKI_OUTPUT?(uint32_t)(HoujiCoherenceVa+(direction==2?1:3)*HoujiCoherencePage):word;
 return true;
}
static inline bool houjiCoherenceProved(unsigned phase,unsigned dirty,unsigned readNew,
 unsigned readOld,unsigned writeNew,unsigned dramNew,unsigned status) {
 // Dirty-line evidence is essential: matching values alone could mean that
 // eviction, a hidden clean or an uncached alias made the experiment vacuous.
 return !status && phase==6 && dirty>=48 && dirty<=64 &&
        readNew==HoujiCoherenceWords && !readOld &&
        writeNew==HoujiCoherenceWords && dramNew==HoujiCoherenceWords;
}
#endif
