/* SPDX-License-Identifier: MIT */
#ifndef HOUJI_GPU_CLIENT_LAYOUT_H
#define HOUJI_GPU_CLIENT_LAYOUT_H
#include "HoujiGpuAddressLayout.h"

enum : unsigned {
    NativeAdrenoGeneralClients=4,
    NativeAdrenoPreferencesSlot=4,
    NativeAdrenoMaxClients=5
};
/* Keep every existing window unchanged. The new window occupies part of the
 * former 1 GiB renderer range, vacated when slot 0 moved above 0x80000000.
 * The live board gate still checks every window against all boot mappings.
 * This is VA, not a reservation of physical RAM or an isolated page table. */
static const struct {uint64_t start,bytes;} kClientWindows[NativeAdrenoMaxClients]={
    {HOUJI_GPU_RENDER_START,HOUJI_GPU_RENDER_BYTES},
    {0x10000000ull,0x10000000ull},
    {0x64000000ull,0x0C000000ull},
    {0x70000000ull,0x0C000000ull},
    {0x20000000ull,0x10000000ull}
};

static inline unsigned houjiGpuClientSlot(bool renderer,bool preferences,const bool *used)
{
    // Preferences must not fall back to unsupported OpenGL because wallpaper
    // clients occupied every generic slot. Only that process uses this slot;
    // normal IOKit/sandbox checks and close/fence quarantine still apply.
    if(preferences)return used[NativeAdrenoPreferencesSlot]?NativeAdrenoMaxClients:NativeAdrenoPreferencesSlot;
    if(renderer && !used[0])return 0;
    for(unsigned i=1;i<NativeAdrenoGeneralClients;i++)if(!used[i])return i;
    return NativeAdrenoMaxClients;
}
#endif
