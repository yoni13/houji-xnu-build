/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Fixed one-shot C ABI around real upstream hgf_run; Native owns hardware. */
#ifndef HOUJI_STOCK_NATIVE_H
#define HOUJI_STOCK_NATIVE_H
#include <stddef.h>
#include <stdint.h>
#include "houji_hfi_inactive.h"
#define HSN_ABI_VERSION 2u
#define HSN_STORAGE_BYTES 196608u
#define HSN_STORAGE_ALIGNMENT 16u
#define HSN_TCM_WORDS 8192u
#define HSN_RECEIVE_RECORDS 80u
#define HSN_MESSAGE_WORDS 255u
#define HSN_HOST_PROFILE_STEPS 5u

typedef struct HsnStorage {
#ifdef __cplusplus
 alignas(HSN_STORAGE_ALIGNMENT) unsigned char bytes[HSN_STORAGE_BYTES];
#else
 _Alignas(HSN_STORAGE_ALIGNMENT) unsigned char bytes[HSN_STORAGE_BYTES];
#endif
} HsnStorage;
/* Zero before first init; address identity, no copying/moving/reinitializing.
 * One hgf attempt; no release/reset/unbind API. Both storages, all BOs and the
 * Native owner's references must outlive every outcome until platform reset.
 * Calls are externally serialized. Reentrant mutation is rejected. */
enum HsnStatus {HSN_OK=0,HSN_ARGUMENT=1,HSN_ALIGNMENT=2,HSN_UNINITIALIZED=3,
 HSN_BUSY=4,HSN_ALREADY_RUN=5,HSN_HHI_NOT_READY=6,HSN_NOT_AVAILABLE=7};
enum HsnCallbackKind {HSN_CB_READ32=1,HSN_CB_READ64,HSN_CB_WRITE32,HSN_CB_WRITE64,HSN_CB_BARRIER,HSN_CB_DELAY,HSN_CB_PREREQUISITE,HSN_CB_PUBLISH,HSN_CB_CHECKPOINT,HSN_CB_STOPPED,HSN_CB_BINDING};
typedef struct HsnCallbacks {
 uint32_t abi_version,reserved;void *cookie;
 int (*read32)(void *,uint32_t,uint32_t *);
 int (*read64)(void *,uint32_t,uint64_t *);
 int (*write32)(void *,uint32_t,uint32_t);
 int (*write64)(void *,uint32_t,uint64_t);
 int (*barrier)(void *);
 int (*delay_us)(void *,unsigned);
 /* Exactly 1 authorizes the real upstream prerequisite; all else refuses.
  * Native owns fresh gates, secure-CX proof, backing/alias/cache/lease checks.
  * Read/write/delay/barrier/publish/checkpoint use 0=success. */
 int (*prerequisite)(void *);
 int (*publish)(void *);
 /* Stage1 and stage2 are upstream progress points, not FS eligibility. The
  * bridge has no safe_to_save bit; Native must authorize any storage action. */
 int (*checkpoint)(void *,unsigned);
 /* Optional post-reset diagnostic boundary; reset1 never permits release. */
 int (*stopped)(void *);
} HsnCallbacks;
typedef struct HsnCallbackFailure {
 uint32_t attempted,sequence,kind,phase,address,width;
 uint64_t value;int32_t callback_status;uint32_t reserved;
} HsnCallbackFailure;
typedef struct HsnFailure {
 uint32_t hgf_code,failed_phase,failed_address,failed_value;
 uint32_t failed_operation,failed_observed_valid,reserved[2];
 HsnCallbackFailure first_callback;
} HsnFailure;
typedef struct HsnSummary {
 uint32_t abi_version,run_called,bridge_phase,hhi_sealed;
 uint32_t hgf_code,hgf_phase,attempted,retained,reads,writes,polls;
 uint32_t reset_held,release_attempted,tcm_backup_words,tcm_loaded_words;
 uint32_t rsc_attempted,boot_attempted,stream_attached,fw_ready,hfi_ready;
 uint32_t query_sent,query_ack,query_header,query_value,query_status;
 uint32_t reply_count,debug_count,diagnostic_mask,fw_status,hfi_status;
 uint32_t fsr,fsynr,gfsr,irq,ao_irq,counter_before,counter_after;
 uint32_t receive_calls,receive_records,receive_empty[2],receive_overflow;
 uint64_t far;
} HsnSummary;
typedef struct HsnTcmWord {uint32_t index,address,value,valid;} HsnTcmWord;
/* Every nonempty receive and receive error is retained in original order.
 * Empty receives are counted per queue in HsnSummary, not called messages.
 * Base mode fixes bootstrap=NULL. Host-profile mode adds the original five
 * ordinary messages;80 slots cover base33 + host38 conservative records.
 * No writable upstream pointer, queue or replayable hfm_result is exported. */
typedef struct HsnReceive {
 uint32_t sequence,queue,status,word_count;
 uint32_t words[HSN_MESSAGE_WORDS];
} HsnReceive;
typedef struct HsnHostProfileSummary {
 uint32_t requested,attempted,code,return_code,stage,completed,debug_count;
 uint32_t resource_request,resource_words,resource_count,resource_acks,next_sequence,qualified;
} HsnHostProfileSummary;
typedef struct HsnHostProfileStep {
 uint32_t attempted,sent,ack,word_count,polls,status_present,legacy_contract;
 uint32_t header,value,status,reply[HSN_MESSAGE_WORDS];
} HsnHostProfileStep;
#ifdef __cplusplus
extern "C" {
#endif
size_t hsn_actual_state_size(void);
size_t hsn_actual_state_alignment(void);
int hsn_init(void *,size_t,const HsnCallbacks *,size_t);
/* Caller first pins exact FW SHA/length. Private C code validates the actual
 * self-bound HhiState, successful prepare/no release/all9 owners/audit/content,
 * then irreversibly seals its busy flag before any Native callback. hgf_run
 * receives &that_HhiState.result directly. Never rebuild from public snapshots.
 * HSN_OK means hgf_run returned; out_hgf_code is its untouched result code.
 * zero_pa denotes a separately owned/qualified PRR-zero allocation from Native.
 * This API grants no filesystem eligibility or resource-release permission.
 * Rejected API/preflight inputs do not consume the single upstream attempt;
 * once sealed, every outcome keeps the HHI busy word set until platform reset. */
int hsn_run(void *,size_t,HhiStorage *,size_t,const uint8_t *,size_t,
 uint64_t zero_pa,uint32_t *out_hgf_code);
/* Same one-shot signature and retained graph; invokes untouched hhp_run through
 * real hgf bootstrap. Fixed PERF/BW/CORE/START/QUERY only, resource=NULL; no
 * HWSCHED/AQE, scratch, additional MMIO range, FS or retirement permission. */
int hsn_run_host_profile(void *,size_t,HhiStorage *,size_t,const uint8_t *,size_t,
 uint64_t zero_pa,uint32_t *out_hgf_code);
/* Read-only CPU snapshots only. Same-thread callbacks may use these for
 * diagnostics under the external owner serialization; mutation remains busy.
 * They grant no permission to access a BO or to save storage. */
int hsn_summary(const void *,size_t,HsnSummary *,size_t);
int hsn_failure(const void *,size_t,HsnFailure *,size_t);
int hsn_tcm_backup(const void *,size_t,uint32_t,HsnTcmWord *,size_t);
int hsn_receive_log(const void *,size_t,uint32_t,HsnReceive *,size_t);
int hsn_memory_summary(const void *,size_t,HhiSummary *,size_t);
int hsn_memory_row(const void *,size_t,uint32_t,HhiRow *,size_t);
int hsn_host_profile_summary(const void *,size_t,HsnHostProfileSummary *,size_t);
int hsn_host_profile_step(const void *,size_t,uint32_t,HsnHostProfileStep *,size_t);
#ifdef __cplusplus
}
#endif
#endif
