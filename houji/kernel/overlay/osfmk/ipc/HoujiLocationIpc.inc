// SPDX-License-Identifier: BSD-2-Clause-Patent
// Observes the unbounded locationd MIG call seen in the owner's r34 stackshot.
// No messages, timeouts, rights, return values or service states are modified.
// Narrow BSD declarations avoid importing BSD typedefs into the osfmk build.
extern void proc_selfname(char *buf, int size);
extern void proc_name(int pid, char *buf, int size);

static int houji_location_ipc_enabled = -1;
static LCK_GRP_DECLARE(houji_location_ipc_group, "houji-location-ipc");
static LCK_SPIN_DECLARE(houji_location_ipc_lock, &houji_location_ipc_group);
static struct {
  uint64_t requests, replies, request_tid, reply_tid, last_time;
  int location_pid, peer_pid, reply_pid;
  uint32_t peer_name, peer_kotype, peer_queued, peer_state, host_slot;
  uintptr_t request_token;
  uint64_t receive_attempts, receive_results, freed_unreceived;
  int receive_pid;
  uint32_t receive_status;
} houji_location_ipc;

static bool houji_location_ipc_is_enabled(void) {
  int enabled = __atomic_load_n(&houji_location_ipc_enabled, __ATOMIC_RELAXED);
  if (enabled < 0) {
    int requested = 0;
    PE_parse_boot_argn("houji_location_ipc", &requested, sizeof(requested));
    enabled = requested == 1;
    __atomic_store_n(&houji_location_ipc_enabled, enabled, __ATOMIC_RELAXED);
  }
  return enabled == 1;
}

static void houji_location_ipc_observe(ipc_port_t port,
    const mach_msg_header_t *header, thread_t thread, uintptr_t token) {
  if (!houji_location_ipc_is_enabled() ||
      (header->msgh_id != 1203 && header->msgh_id != 1303)) return;
  char name[32];
  proc_selfname(name, sizeof(name));
  bool request = header->msgh_id == 1203 && strcmp(name, "locationd") == 0;
  if (!request && header->msgh_id != 1303) return;
  int peer = -1;
  uint32_t receiver_name = 0, kotype = 0, queued = 0, state = 0;
  // Same receiver inspection and queue lock used by XNU's existing IPC trace.
  ip_mq_lock(port);
  if (ip_active(port)) {
    kotype = ip_kotype(port);
    queued = port->ip_messages.imq_msgcount;
    if (port->ip_tempowner) {
      state = 1;
      if (ip_get_imp_task(port) != IIT_NULL)
        peer = task_pid(port->ip_imp_task->iit_task);
    } else if (ip_in_a_space(port)) {
      state = 2;
      ipc_space_t space = ip_get_receiver(port);
      if (space && is_active(space)) {
        peer = task_pid(space->is_task);
        receiver_name = port->ip_receiver_name;
      }
    } else {
      state = ip_in_transit(port) ? 3 : 4;
    }
  }
  ip_mq_unlock(port);
  uint32_t host_slot = 0;
  if (request) {
    // No port or observer lock held. Compare the live destination reference
    // with registered host service ports; do not copy/create any send rights.
    host_lock(&realhost);
    for (unsigned slot = 1; slot <= HOST_MAX_SPECIAL_PORT; slot++)
      if (realhost.special[slot] == port) { host_slot = slot; break; }
    host_unlock(&realhost);
  }
  int sender = task_pid(current_task());
  uint64_t time;
  clock_get_uptime(&time);
  lck_spin_lock(&houji_location_ipc_lock);
  if (request) {
    houji_location_ipc.requests++;
    houji_location_ipc.location_pid = sender;
    houji_location_ipc.peer_pid = peer;
    houji_location_ipc.peer_name = receiver_name;
    houji_location_ipc.peer_kotype = kotype;
    houji_location_ipc.peer_queued = queued;
    houji_location_ipc.peer_state = state;
    houji_location_ipc.host_slot = host_slot;
    __atomic_store_n(&houji_location_ipc.request_token, token, __ATOMIC_RELEASE);
    houji_location_ipc.request_tid = thread_tid(thread);
    houji_location_ipc.last_time = time;
  } else if (houji_location_ipc.location_pid > 0 && peer == houji_location_ipc.location_pid) {
    houji_location_ipc.replies++;
    houji_location_ipc.reply_pid = sender;
    houji_location_ipc.reply_tid = thread_tid(thread);
    houji_location_ipc.last_time = time;
  }
  lck_spin_unlock(&houji_location_ipc_lock);
}

// The kmsg pointer is an opaque comparison token, never dereferenced or retained.
// Free clears the token, so allocator reuse cannot look like a later receipt.
static void houji_location_ipc_freed(uintptr_t token) {
  if (!houji_location_ipc_is_enabled() || !token ||
      token != __atomic_load_n(&houji_location_ipc.request_token, __ATOMIC_ACQUIRE)) return;
  lck_spin_lock(&houji_location_ipc_lock);
  if (token && houji_location_ipc.request_token == token) {
    houji_location_ipc.freed_unreceived++;
    __atomic_store_n(&houji_location_ipc.request_token, 0, __ATOMIC_RELEASE);
  }
  lck_spin_unlock(&houji_location_ipc_lock);
}

static bool houji_location_ipc_receiving(uintptr_t token, int pid) {
  if (!houji_location_ipc_is_enabled() || !token ||
      token != __atomic_load_n(&houji_location_ipc.request_token, __ATOMIC_ACQUIRE)) return false;
  bool tracked = false;
  lck_spin_lock(&houji_location_ipc_lock);
  if (token && houji_location_ipc.request_token == token) {
    tracked = true;
    __atomic_store_n(&houji_location_ipc.request_token, 0, __ATOMIC_RELEASE);
    houji_location_ipc.receive_attempts++;
    houji_location_ipc.receive_pid = pid;
  }
  lck_spin_unlock(&houji_location_ipc_lock);
  return tracked;
}

static void houji_location_ipc_received(bool tracked, uint32_t status) {
  if (!tracked) return;
  lck_spin_lock(&houji_location_ipc_lock);
  houji_location_ipc.receive_results++;
  houji_location_ipc.receive_status = status;
  lck_spin_unlock(&houji_location_ipc_lock);
}

void houji_location_ipc_report(void) {
  if (!houji_location_ipc_is_enabled()) return;
  typeof(houji_location_ipc) snapshot;
  lck_spin_lock(&houji_location_ipc_lock);
  snapshot = houji_location_ipc;
  lck_spin_unlock(&houji_location_ipc_lock);
  char name[32] = "unresolved";
  if (snapshot.peer_pid > 0) proc_name(snapshot.peer_pid, name, sizeof(name));
  printf("HOUJI LOCATION IPC: requests=%llu replies=%llu locationd=%d peer=%d(%s) port-name=%x kotype=%u queued=%u request-tid=%llu reply-pid=%d reply-tid=%llu last-ticks=%llu\n",
      snapshot.requests, snapshot.replies, snapshot.location_pid, snapshot.peer_pid,
      name, snapshot.peer_name, snapshot.peer_kotype, snapshot.peer_queued,
      snapshot.request_tid, snapshot.reply_pid, snapshot.reply_tid, snapshot.last_time);
  char receiver[32] = "unresolved";
  if (snapshot.receive_pid > 0) proc_name(snapshot.receive_pid, receiver, sizeof(receiver));
  printf("HOUJI LOCATION ROUTE: state=%u host-slot=%u receive-attempts=%llu results=%llu receiver=%d(%s) status=%x freed-unreceived=%llu\n",
      snapshot.peer_state, snapshot.host_slot, snapshot.receive_attempts, snapshot.receive_results,
      snapshot.receive_pid, receiver, snapshot.receive_status, snapshot.freed_unreceived);
}
