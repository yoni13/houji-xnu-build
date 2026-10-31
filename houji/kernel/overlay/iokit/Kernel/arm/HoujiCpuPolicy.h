// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef HOUJI_CPU_POLICY_H
#define HOUJI_CPU_POLICY_H
#include <stdint.h>

static inline bool houjiCpuNodeAllowed(unsigned logical, uint32_t affinity,
    unsigned total, bool enabled) {
  if (total == 1) return logical == 0 && affinity == 0;
  return enabled && total >= 2 && total <= 8 && logical < total &&
      affinity == logical * 0x100U;
}

// Arm part IDs, checked against Linux cputype.h and the captured SM8650 layout.
// The MIDR is checked on the executing core; an ADT string is not evidence.
static inline unsigned houjiCpuPart(unsigned logical) {
  return logical < 2 ? 0xd80U : logical < 7 ? 0xd81U : 0xd82U;
}

// Group 1 SGI1, exact MPIDR affinity; no broadcast/range selector.
static inline bool houjiSgiValue(uint64_t affinity, uint64_t *value) {
  if (!value || (affinity & ~0xff00ffffffULL) || (affinity & 0xff) >= 16)
    return false;
  *value = (((affinity >> 32) & 0xff) << 48) |
      (((affinity >> 16) & 0xff) << 32) |
      (((affinity >> 8) & 0xff) << 16) |
      (1ULL << 24) | (1ULL << (affinity & 0xff));
  return true;
}
#endif
