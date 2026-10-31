/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef HOUJI_DVFS_POLICY_H
#define HOUJI_DVFS_POLICY_H
#include <stdint.h>
/* Sheng's interaction policy, adapted to houji's four validated domains.
 * A sustained render stream holds 60% for 300 ms after its last activity;
 * P cores use their qualified cap during touch and a 50% floor until quiet.
 * This chooses indices only; board code still validates and votes the LUT. */
static inline unsigned houjiDvfsInteractiveFloor(const uint32_t *khz,unsigned n,
 unsigned busy,bool performance,bool touch,bool frame,unsigned *quiet) {
 if(!n)return 0;
 *quiet=busy<100?(*quiet<12?*quiet+1:12):0;
 unsigned pct=frame?60:0;
 if(performance){if(touch)pct=100;else if(*quiet<12 && pct<50)pct=50;}
 unsigned floor=0;while(floor+1<n && (uint64_t)khz[floor]*100<(uint64_t)khz[n-1]*pct)++floor;
 return floor;
}
/* Immediate rise; four genuinely low windows before falling. Holding the
 * current level must not clear the low-window count (ishtar fix 2033). */
static inline unsigned houjiDvfsChoose(const uint32_t *khz,unsigned n,unsigned cur,
 unsigned busy,unsigned floor,unsigned *low) {
 if(!n)return 0;
 if(cur>=n)cur=n-1;if(floor>=n)floor=n-1;if(busy>1000)busy=1000;
 unsigned want=n-1;
 if(busy<900){uint64_t need=(uint64_t)khz[cur]*busy*125/100000;
  want=floor;while(want+1<n && khz[want]<need)++want;}
 if(want>cur){*low=0;return want;}
 if(want==cur){*low=0;return cur;}
 if(++*low<4)return cur;
 *low=0;return want;
}
#endif
