/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef HOUJI_FULL_HOST_RUNTIME_H
#define HOUJI_FULL_HOST_RUNTIME_H
#include "houji_native_full_host_graph.h"
#include "ArmSmmu4KLive.h"
#define FHR_ABI_VERSION 3u
#define FHR_STORAGE_BYTES 327680u
#define FHR_STORAGE_ALIGNMENT 16u
#define FHR_RX_RECORDS 128u
#define FHR_MESSAGE_WORDS 255u
#define FHR_GEN7_OPS 102u
#define FHR_GEN7_PAIRS 75u
typedef struct FhrStorage {
#ifdef __cplusplus
 alignas(FHR_STORAGE_ALIGNMENT) unsigned char bytes[FHR_STORAGE_BYTES];
#else
 _Alignas(FHR_STORAGE_ALIGNMENT) unsigned char bytes[FHR_STORAGE_BYTES];
#endif
} FhrStorage;
enum FhrStatus {FHR_OK=0,FHR_ARGUMENT=1,FHR_ALIGNMENT=2,FHR_UNINITIALIZED=3,FHR_BUSY=4,FHR_ALREADY_RUN=5,FHR_GRAPH_NOT_READY=6,FHR_NOT_AVAILABLE=7};
enum FhrStage {FHR_PREFLIGHT=0,FHR_STOCK=1,FHR_HOST=2,FHR_BANK0=3,FHR_GX=4,FHR_CODE=5,FHR_GEN7=6,FHR_PRE_CP_BOUNDARY=7,FHR_RETURNED=8,FHR_RETAINED_LIVE=9};
enum FhrCode {FHR_SUCCESS=0,FHR_CLAIM=1,FHR_IDENTITY=2,FHR_GRAPH=3,FHR_BINDING=4,FHR_HOST_FAILED=5,FHR_BANK0_FAILED=6,FHR_GX_FAILED=7,FHR_CODE_FAILED=8,FHR_GEN7_FAILED=9,FHR_RECORD_FAILED=10};
typedef struct FhrOps {
 uint32_t abi_version,reserved;void *cookie;
 /* Independent runtime owner; NEVER invoke the retired CPU adapter's ops. */
 int (*sha256)(void *,const uint8_t *,size_t,uint8_t digest[32]);
 /* Fresh exclusive 15BO refs0/descriptor/NC-WB/PAR/table claim before any
  * core RAM reads and before the permanent FHG busy seal. Return0. */
 int (*prepare_claim)(void *);
 int (*read32)(void *,uint32_t,uint32_t *);
 int (*read64)(void *,uint32_t,uint64_t *);
 int (*write32)(void *,uint32_t,uint32_t);
 int (*write64)(void *,uint32_t,uint64_t);
 int (*barrier)(void *);
 int (*delay_us)(void *,unsigned);
 /* Exactly1: real original HGF prerequisite. First call after fresh cold
  * proof acquires15refs + retained PRRzero extra ref; all later outcomes retain. */
 int (*prerequisite)(void *);
 int (*publish_graph)(void *);
 /* Original HGF stages1/2 before release ONLY. Stage1 may save after Native
  * cold proof; stage2 is RAM-only. No callback of this kind after possibleexec. */
 int (*checkpoint)(void *,unsigned);
 int (*stopped)(void *);
 /* Fresh independent owner/whole-graph lease/power/context/fault policy for
  * current fixed stage. Return0; must not save, free, unmap or replay oldowner. */
 int (*stage_guard)(void *,unsigned stage);
 /* After core writes/verifies the exact154DW gen7 record in real NC codeBO:
  * order NC writes, revalidate runtime lease; no attr/ref/FS operation. */
 int (*publish_record)(void *);
 /* Fixed optional native CP_INIT/two-writeback continuation, same live graph. */
 int (*after_prepare)(void *);
 int (*retain_live)(void *); /* Optional exact live handoff, never aliases stopped. */
} FhrOps;
typedef struct FhrFailure {uint32_t attempted,stage,kind,address,width;int32_t status;uint64_t value;} FhrFailure;
typedef struct FhrRx {uint32_t sequence,stage,queue,status,word_count;uint32_t words[FHR_MESSAGE_WORDS];} FhrRx;
typedef struct FhrTcmWord {uint32_t index,address,value,valid;} FhrTcmWord;
typedef struct FhrHostStep {uint32_t attempted,sent,ack,word_count,polls,status_present,legacy_contract,header,value,status;uint32_t reply[FHR_MESSAGE_WORDS];} FhrHostStep;
typedef struct FhrGen7Op {uint32_t index,address,value,mask,verify,kind;} FhrGen7Op;
typedef struct FhrGen7Record {FhrGen7Op op;uint32_t before,expected,after,valid,written,polls;} FhrGen7Record;
typedef struct FhrSummary {
 uint32_t abi_version,busy,run_called,graph_sealed,stage,completed_stage,code,retained_live;
 uint32_t possible_execution,stock_code,stock_phase,reset_held,release_attempted;
 uint32_t fw_ready,hfi_ready,query_sent,query_ack,query_value,query_status;
 uint32_t tcm_backup_words,tcm_loaded_words,rsc_attempted,boot_attempted;
 uint32_t host_attempted,host_code,host_stage,host_completed,host_qualified;
 uint32_t resource_count,resource_acks,next_sequence;
 uint32_t bank0_code,bank0_phase,bank0_attempted,bank0_enabled,bank0_attached,bank0_lpac,sid0_attached,sid1_attached;
 uint32_t sid0_smr,sid0_s2cr,sid1_smr,sid1_s2cr;
 uint32_t gx_code,gx_phase,gx_requested,gx_acked,gx_clear_attempted,gx_clear_confirmed,gx_ready,gx_valid;
 uint32_t gx_before,gx_after,gx_irq_before,gx_irq_ack,gx_irq_after,gx_fence,gx_busy,gx_rbbm,gx_rbbm3,gx_sqe;
 uint32_t code_bind_code,code_bind_phase,code_bind_attempted,code_bind_verified;
 uint32_t gen7_code,gen7_phase,gen7_step,gen7_completed,gen7_record_ready,gen7_pairs,gen7_record_crc;
 /* Explicit bounded result: preparation completed; no CP/ZAP/2D attempted. */
 uint32_t pre_cp_boundary,cp_attempted,zap_attempted,graphics_attempted;
 uint32_t rx_calls,rx_records,rx_empty[2],rx_overflow;
 uint32_t fsr,fsynr,gfsr,irq,ao_irq,diagnostic_mask;uint64_t far;
 FhrFailure first_callback;
 uint32_t failed_phase,failed_address,failed_value,failed_operation,failed_observed_valid;
} FhrSummary;
#ifdef __cplusplus
extern "C" {
#endif
size_t fhr_actual_state_size(void);
int fhr_init(void *,size_t,const FhrOps *,size_t);
/* Both stable-address storages, assets and all BOs must survive until reset.
 * No old HHI/HSN object is accepted/restarted. The exact ready FHG hfm_result
 * is used directly and FHG.busy is permanently sealed before first device op.
 * FHR_OK means original HGF ran; out_code is untouched hgf code. Summary.code
 * is the bridge's distinct lower-stage cause. All outcomes remain no-release.
 * Calls externally serialized; mutation reentryBUSY, same-thread readonly
 * getters allowed for Native guard/diagnostics. No active-FS permission. */
int fhr_run(void *,size_t,FhgStorage *,size_t,uint64_t zero_pa,uint32_t *out_code);
/* Trusted same-owner binding to the actual retained arena/state, not POD
 * snapshots used to reconstruct tables. Only inside PRE_CP continuation. */
typedef struct FhrGpuLiveBinding {const ArmSmmu4KLiveArena *arena;ArmSmmu4KLiveState *state;const ArmSmmu4KRange *boot_ranges;size_t boot_count;} FhrGpuLiveBinding;
typedef struct FhrRuntimeIo {void *cookie;int (*live)(void*);int (*doorbell)(void*);int (*barrier)(void*);int (*delay)(void*,unsigned);} FhrRuntimeIo;
int fhr_runtime_vote(void *,size_t,const FhrRuntimeIo*,uint32_t level,uint32_t out[6]);
int fhr_gpu_live_binding(void *,size_t,FhrGpuLiveBinding *,size_t);
int fhr_summary(const void *,size_t,FhrSummary *,size_t);
int fhr_tcm_backup(const void *,size_t,uint32_t,FhrTcmWord *,size_t);
int fhr_rx(const void *,size_t,uint32_t,FhrRx *,size_t);
int fhr_host_step(const void *,size_t,uint32_t,FhrHostStep *,size_t);
int fhr_gen7_record(const void *,size_t,uint32_t,FhrGen7Record *,size_t);
/* Stateless source-bound policy facts, no MMIO or permission side effect. */
int fhr_gen7_op(uint32_t,FhrGen7Op *,size_t);
int fhr_gen7_power_address(uint32_t,uint32_t *);
#ifdef __cplusplus
}
#endif
#endif
