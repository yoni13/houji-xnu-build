// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef HOUJI_SMP_GIC_H
#define HOUJI_SMP_GIC_H
#include <stdint.h>
struct HoujiGicSnapshot {
  uint64_t group1, pmr, sre, daif, vbar, timer_control, control;
  uint64_t highest_pending;
};
#ifdef __cplusplus
extern "C" {
#endif
extern struct HoujiGicSnapshot houji_gic_snapshot[8];
extern uint32_t houji_gic_snapshot_ready[8];
void houji_smp_capture_gic(void);
#ifdef __cplusplus
}
#endif
#endif
