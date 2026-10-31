// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef HOUJI_CPU_FREQUENCY_H
#define HOUJI_CPU_FREQUENCY_H

// One 32-bit hardware cycle counter over a short architected-timer interval.
// Unsigned subtraction permits one wrap. The interval is at most half a second;
// callers sample for 100 ms, well below one wrap at the accepted frequency.
static inline bool houjiCpuFrequencyKHz(uint32_t before, uint32_t after,
    uint64_t ticks, uint64_t referenceHz, uint32_t *khz) {
  if (!khz || !ticks || referenceHz < 1000000 || referenceHz > 1000000000 ||
      ticks > referenceHz / 2) return false;
  uint64_t cycles = uint32_t(after - before);
  uint64_t hz = cycles * referenceHz / ticks;
  if (hz < 50000000 || hz > 5000000000ULL) return false;
  *khz = uint32_t(hz / 1000);
  return true;
}
#endif
