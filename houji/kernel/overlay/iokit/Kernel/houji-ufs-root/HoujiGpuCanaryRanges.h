/* SPDX-License-Identifier: MIT */
#ifndef HOUJI_GPU_CANARY_RANGES_H
#define HOUJI_GPU_CANARY_RANGES_H
#include "ArmSmmu4K.h"
/* The boot ranges have already passed the complete graph audit. Insert the
 * canary by IOVA, including above the boot mappings. The live transaction still
 * checks overlap, attributes and ownership; this only builds its metadata.
 * Inputs and output must be distinct, as required by ArmSmmu4KLiveUpdate. */
static inline int houjiGpuCanaryRanges(const ArmSmmu4KRange *boot,size_t count,
 const ArmSmmu4KRange *change,ArmSmmu4KRange *out,size_t capacity)
{
 if(!change || !out || (count && !boot) || count>=capacity)return 0;
 size_t at=0;
 while(at<count && boot[at].iova<change->iova){out[at]=boot[at];++at;}
 out[at]=*change;
 for(size_t i=at;i<count;++i)out[i+1]=boot[i];
 return 1;
}
#endif
