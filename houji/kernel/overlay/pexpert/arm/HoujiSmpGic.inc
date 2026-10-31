// SPDX-License-Identifier: BSD-2-Clause-Patent
// CPU-interface system registers only. Do not read redistributor SGI MMIO:
// houji r40 aborted on GICR_IGROUPR0 despite earlier configuration writes.
#if defined(SWEET)
#include "HoujiSmpGic.h"
struct HoujiGicSnapshot houji_gic_snapshot[8];
uint32_t houji_gic_snapshot_ready[8];
void houji_smp_capture_gic(void) {
  unsigned int id = ml_get_cpu_number_local();
  if (id >= 8) return;
  struct HoujiGicSnapshot *s = &houji_gic_snapshot[id];
  s->group1 = __builtin_arm_rsr64("ICC_IGRPEN1_EL1");
  s->pmr = __builtin_arm_rsr64("ICC_PMR_EL1");
  s->sre = __builtin_arm_rsr64("ICC_SRE_EL1");
  s->daif = __builtin_arm_rsr64("DAIF");
  s->vbar = __builtin_arm_rsr64("VBAR_EL1");
  s->timer_control = __builtin_arm_rsr64("CNTV_CTL_EL0");
  s->control = __builtin_arm_rsr64("ICC_CTLR_EL1");
  // HPPIR observes the highest pending interrupt without acknowledging it.
  s->highest_pending = __builtin_arm_rsr64("ICC_HPPIR1_EL1");
  __atomic_store_n(&houji_gic_snapshot_ready[id], 1U, __ATOMIC_RELEASE);
}
#endif
