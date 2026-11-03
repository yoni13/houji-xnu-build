/* SPDX-License-Identifier: MIT */
#ifndef NATIVE_ADRENO_COHERENCE_POLICY_H
#define NATIVE_ADRENO_COHERENCE_POLICY_H
enum {NativeAdrenoCoherenceOff=0,NativeAdrenoCoherenceReadOnly=1,
      NativeAdrenoCoherenceReadWrite=2};
static inline bool NativeAdrenoCoherentMapping(unsigned mode,unsigned writeback,unsigned shareability)
{
    return (mode==NativeAdrenoCoherenceReadOnly || mode==NativeAdrenoCoherenceReadWrite) &&
           writeback==1 && shareability==3;
}
static inline bool NativeAdrenoCoherentImport(unsigned mode,unsigned permissions)
{
    return mode==NativeAdrenoCoherenceReadWrite ||
           (mode==NativeAdrenoCoherenceReadOnly && !(permissions&2u));
}
#endif
