// SPDX-License-Identifier: BSD-2-Clause-Patent
// Passive exit status for boot services. Never holds a process reference or
// changes exit status/reason; the exiting process remains live at the hook.
extern uint64_t houji_task_mach_header(task_t);
static LCK_GRP_DECLARE(houji_exit_group, "houji-service-exit");
static LCK_SPIN_DECLARE(houji_exit_lock, &houji_exit_group);
static int houji_exit_enabled = -1;
static const char *const houji_exit_names[] = {
  "fairplayd.H2", "locationd", "backboardd", "SpringBoard", "com.apple.migra"
};
struct HoujiExitCall { int code, error; uint32_t rv0, rv1; };
static struct HoujiExitCall houji_exit_calls[24], houji_exit_last_calls[24];
struct HoujiExitOpen { char path[160]; int error, copy_error; };
static struct HoujiExitOpen houji_exit_opens[8], houji_exit_last_opens[8];
static unsigned houji_exit_open_count, houji_exit_last_open_count;
static unsigned houji_exit_call_count, houji_exit_last_call_count;
static int houji_exit_call_pid;
static struct {
  uint64_t count, time, reason_code, mach_header, pc, lr;
  int pid, status;
  uint32_t reason_namespace;
} houji_exit_records[5];
static bool houji_exit_is_enabled(void) {
  int enabled = __atomic_load_n(&houji_exit_enabled, __ATOMIC_RELAXED);
  if (enabled < 0) {
    int requested = 0;
    PE_parse_boot_argn("houji_location_ipc", &requested, sizeof(requested));
    enabled = requested == 1;
    __atomic_store_n(&houji_exit_enabled, enabled, __ATOMIC_RELAXED);
  }
  return enabled == 1;
}
void houji_location_syscall_observe(proc_t p, int code, int error, uint32_t rv0, uint32_t rv1, uint64_t arg0) {
  if (!houji_exit_is_enabled() || strcmp(p->p_comm, "fairplayd.H2")) return;
  int pid = proc_getpid(p);
  struct HoujiExitOpen opened = {.error = error};
  if (code == 5) {
    size_t length = 0;
    opened.copy_error = copyinstr((user_addr_t)arg0, opened.path, sizeof(opened.path), &length);
    opened.path[sizeof(opened.path)-1] = 0;
  }
  lck_spin_lock(&houji_exit_lock);
  if (houji_exit_call_pid != pid) { houji_exit_call_pid = pid; houji_exit_call_count = 0; houji_exit_open_count = 0; }
  houji_exit_calls[houji_exit_call_count++ % 24] = (struct HoujiExitCall){code,error,rv0,rv1};
  if (code == 5) houji_exit_opens[houji_exit_open_count++ % 8] = opened;
  lck_spin_unlock(&houji_exit_lock);
}
static void houji_location_exit_observe(proc_t p, int status,
    const struct os_reason *reason) {
  if (!houji_exit_is_enabled()) return;
  unsigned id;
  for (id = 0; id < 5; id++)
    if (!strncmp(p->p_comm, houji_exit_names[id], strlen(houji_exit_names[id]))) break;
  if (id == 5) return;
  uint64_t time, mh = 0, pc = 0, lr = 0;
  task_t task = proc_task(p);
  if (task == current_task()) {
    mh = houji_task_mach_header(task);
    arm_saved_state_t *regs = find_user_regs(current_thread());
    if (regs && is_saved_state64(regs)) { pc = get_saved_state_pc(regs); lr = get_saved_state_lr(regs); }
  }
  clock_get_uptime(&time);
  lck_spin_lock(&houji_exit_lock);
  houji_exit_records[id].count++;
  houji_exit_records[id].pid = proc_getpid(p);
  houji_exit_records[id].status = status;
  houji_exit_records[id].time = time;
  houji_exit_records[id].mach_header = mh;
  houji_exit_records[id].pc = pc; houji_exit_records[id].lr = lr;
  if (!id && houji_exit_call_pid == proc_getpid(p)) {
    houji_exit_last_call_count = houji_exit_call_count < 24 ? houji_exit_call_count : 24;
    houji_exit_last_open_count = houji_exit_open_count < 8 ? houji_exit_open_count : 8;
    unsigned first_open = houji_exit_open_count - houji_exit_last_open_count;
    for (unsigned i = 0; i < houji_exit_last_open_count; i++)
      houji_exit_last_opens[i] = houji_exit_opens[(first_open+i)%8];
    unsigned first = houji_exit_call_count - houji_exit_last_call_count;
    for (unsigned i = 0; i < houji_exit_last_call_count; i++)
      houji_exit_last_calls[i] = houji_exit_calls[(first + i) % 24];
  }
  houji_exit_records[id].reason_namespace = reason ? reason->osr_namespace : 0;
  houji_exit_records[id].reason_code = reason ? reason->osr_code : 0;
  lck_spin_unlock(&houji_exit_lock);
}
void houji_location_exit_report(void) {
  if (!houji_exit_is_enabled()) return;
  typeof(houji_exit_records) snapshot;
  struct HoujiExitCall calls[24]; unsigned count, open_count;
  struct HoujiExitOpen opens[8];
  lck_spin_lock(&houji_exit_lock);
  memcpy(snapshot, houji_exit_records, sizeof(snapshot));
  memcpy(calls, houji_exit_last_calls, sizeof(calls)); count = houji_exit_last_call_count;
  memcpy(opens, houji_exit_last_opens, sizeof(opens)); open_count = houji_exit_last_open_count;
  lck_spin_unlock(&houji_exit_lock);
  for (unsigned id = 0; id < 5; id++) {
    if (!snapshot[id].count) continue;
    printf("HOUJI SERVICE EXIT: %s count=%llu last-pid=%d status=%x namespace=%u reason=%llx time=%llu mh=%llx pc=%llx lr=%llx\n",
        houji_exit_names[id], snapshot[id].count, snapshot[id].pid,
        snapshot[id].status, snapshot[id].reason_namespace,
        snapshot[id].reason_code, snapshot[id].time, snapshot[id].mach_header, snapshot[id].pc, snapshot[id].lr);
  }
  for (unsigned i = 0; i < count; i++)
    printf("HOUJI FAIRPLAY BSD: %u code=%d error=%d rv0=%x rv1=%x\n",i,calls[i].code,calls[i].error,calls[i].rv0,calls[i].rv1);
  for (unsigned i = 0; i < open_count; i++)
    printf("HOUJI FAIRPLAY OPEN: %u error=%d copy=%d path=%s\n", i, opens[i].error, opens[i].copy_error, opens[i].path);
}
