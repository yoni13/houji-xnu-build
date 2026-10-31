/* SPDX-License-Identifier: MIT
 * Exact read-only namespace of the existing owned MMU-500 bank0 observer.
 * IDR0 reports 24 stream slots. No bank programming, route write or global
 * reset is admitted by this predicate. Both board adapter gates share it.
 */
#ifndef HOUJI_NATIVE_SMMU_READ_POLICY_H
#define HOUJI_NATIVE_SMMU_READ_POLICY_H
#include <stdint.h>
#include <stddef.h>
static inline bool houjiNativeSmmuRead32Offset(size_t at) {
 const uint32_t fields[]={0,0x20,0x24,0x28,0x48,0x1000,0x1800,
  0x20000,0x20010,0x20030,0x20038,0x2003c,0x20058,0x207f4};
 for(auto v:fields)if(at==v)return true;
 return !(at&3) && ((at>=0x800 && at<0x860) || (at>=0xc00 && at<0xc60));
}
#endif
