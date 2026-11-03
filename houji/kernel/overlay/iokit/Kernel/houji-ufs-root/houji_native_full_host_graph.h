/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef HOUJI_NATIVE_FULL_HOST_GRAPH_H
#define HOUJI_NATIVE_FULL_HOST_GRAPH_H
#include <stddef.h>
#include <stdint.h>
#include "HoujiGpuAddressLayout.h"
/* Independent fixed CPU-only graph. No old HHI/HSN storage is accepted. */
#define FHG_ABI_VERSION 1u
#define FHG_STORAGE_BYTES 32768u
#define FHG_STORAGE_ALIGNMENT 16u
#define FHG_ROWS 15u
#define FHG_GMU_RANGES 11u
#define FHG_GPU_RANGES 12u
#define FHG_LIVE_GPU_TABLE_PAGES HOUJI_GPU_TABLE_OWNED_PAGES
#define FHG_LIVE_GPU_USED_PAGES HOUJI_GPU_TABLE_USED_PAGES
#define FHG_LIVE_GPU_APERTURE HOUJI_GPU_APERTURE
#define FHG_GMU_BYTES 78588u
#define FHG_SQE_BYTES 85300u
#define FHG_AQE_BYTES 33176u
#define FHG_ALL 32767u
enum FhgStatus {FHG_OK=0,FHG_ARGUMENT=1,FHG_ALIGNMENT=2,FHG_UNINITIALIZED=3,FHG_BUSY=4,FHG_ALREADY_PREPARED=5,FHG_NOT_READY=6};
enum FhgCode {FHG_MATCH=0,FHG_IDENTITY=1,FHG_FIRMWARE=2,FHG_ALLOCATION=3,FHG_BACKING=4,FHG_VALIDATION=5,FHG_CONTENTS=6,FHG_PUBLICATION=7,FHG_TABLES=8,FHG_CALLBACK_MUTATION=9};
enum FhgPhase {FHG_INITIALIZED=1,FHG_PREPARING=2,FHG_PREPARED=3};
enum FhgKind {FHG_FW=1,FHG_HFI,FHG_LOG,FHG_VRB,FHG_SHARED,FHG_GMU_ONLY,FHG_LARGE,FHG_GMU_TABLES,FHG_GPU_POOL,FHG_CODE,FHG_GPU_TABLES};
enum FhgSlot {FHG_SHARED_SLOT=8,FHG_GMU_ONLY_SLOT=9,FHG_LARGE_SLOT=10,FHG_GMU_TABLE_SLOT=11,FHG_GPU_POOL_SLOT=12,FHG_CODE_SLOT=13,FHG_GPU_TABLE_SLOT=14};
typedef struct FhgStorage {
#ifdef __cplusplus
 alignas(FHG_STORAGE_ALIGNMENT) unsigned char bytes[FHG_STORAGE_BYTES];
#else
 _Alignas(FHG_STORAGE_ALIGNMENT) unsigned char bytes[FHG_STORAGE_BYTES];
#endif
} FhgStorage;
typedef struct FhgConfig {uint32_t abi_version,cpu_page_bytes,reserved[2];} FhgConfig;
typedef struct FhgRequest {
 uint32_t slot,kind,gpu4k_pages,normal_nc;
 uint64_t logical_bytes,owned_bytes;
 uint32_t cpu_page_bytes,reserved;
} FhgRequest;
typedef struct FhgBacking {
 uint64_t physical;void *cpu;
 uint64_t owned_bytes,owned_physical,owned_span_bytes;
 uint32_t cpu_page_bytes,owned_span_count;
} FhgBacking;
typedef struct FhgOps {
 uint32_t abi_version,reserved;void *cookie;
 /* sha256 is the real CPU digest boundary. Bridge compares all32 bytes with
  * fixed GMU/SQE/AQE identities before allocation; never approve by name/CRC. */
 int (*sha256)(void *,const uint8_t *,size_t,uint8_t digest[32]);
 /* Each successful callback transfers one exclusive, zero-device-ref backing
  * until platform reset. Allocate the requested full CPU4K/16K owned span;
  * expose only logical GPU4K bytes. Failed nonzero outputs are uncertain and
  * must stay retained in the Native ledger; they are separately reported. */
 int (*allocate)(void *,const FhgRequest *,FhgBacking *);
 /* Prove descriptor identity, full-owned span, current attributes and CPU
  * inactive exclusive access. NC descriptor is required for normal_nc=1;
  * known WB/NC aliases may be AT-observed only, with matching PA, no alias data
  * access. This does not qualify DMA ownership or firmware execution. */
 int (*validate)(void *,const FhgRequest *,const FhgBacking *);
 /* CPU publication only: full-owned WB cache maintenance, explicit NC
  * ordering. No refs acquisition, MMIO, SMC, device start, FS, release/unmap. */
 int (*publish)(void *,const FhgRequest *,const FhgBacking *);
} FhgOps;
typedef struct FhgTrace {uint32_t attempted,success,failure,calls;} FhgTrace;
typedef struct FhgFailure {uint32_t code,stage,slot,callback_kind;int32_t callback_status;uint32_t reserved;} FhgFailure;
typedef struct FhgRange {uint64_t iova,physical,bytes;uint32_t permissions,writeback,shareability,reserved;} FhgRange;
typedef struct FhgRow {FhgRequest request;FhgBacking backing;uint32_t allocated,validated,published,uncertain;} FhgRow;
typedef struct FhgGraph {
 uint32_t graph,input_bits,output_bits,range_count;
 uint64_t table_physical,leaf_budget,root_physical,mair,table_pages,leaf_pages,descriptor_reads;
 uint32_t start_level,upper_region,audit_complete,build_status,audit_status,reserved;
} FhgGraph;
typedef struct FhgSummary {
 uint32_t abi_version,phase,prepare_called,ready,code,stage;
 uint32_t allocated,validated,published,uncertain_outputs,tail_verified,retained;
 uint32_t hfm_code,hfm_first_error,hfm_return_code,hfm_records,hfm_reservations,hfm_count;
 uint32_t hfm_allocated,hfm_validated,hfm_released,hfm_retained,hfm_published;
 uint32_t firmware_bytes_loaded,tcm_bytes_deferred,hfi_queues,hfi_initialized;
 uint32_t cpu_page_bytes,resources_enabled,reserved[2];
 FhgFailure first_failure;FhgTrace allocate_trace,validate_trace,publish_trace,retention_trace;
} FhgSummary;
#ifdef __cplusplus
extern "C" {
#endif
size_t fhg_actual_state_size(void);
size_t fhg_actual_state_alignment(void);
/* Zero before first init. Its address is identity; no copying, moving, manual
 * clearing, repeated init or release API. Externally serialize all calls.
 * Callback reentry is BUSY. Every outcome retains all transferred resources. */
int fhg_init(void *,size_t,const FhgConfig *,size_t,const FhgOps *,size_t);
/* Fixed assets must remain immutable/alive until platform reset. Identity is
 * checked before and after callback boundaries via the real sha256 callback.
 * FHG_OK means one
 * prepare attempt ran; out_code describes it. API rejection leaves out_code
 * unchanged. A callback-created overlap with out_code also returns ARGUMENT
 * after consuming the attempt and retaining its partial allocations; inspect
 * summary using a separate output object. No success bit grants refs,
 * execution, FS or resource responses. */
int fhg_prepare(void *,size_t,const uint8_t *,size_t,const uint8_t *,size_t,const uint8_t *,size_t,uint32_t *out_code);
int fhg_summary(const void *,size_t,FhgSummary *,size_t);
int fhg_row(const void *,size_t,uint32_t,FhgRow *,size_t);
/* graph0 = real HFM1 GMU graph, graph1 = real fixed GPU scratch/code graph.
 * Snapshots are diagnostic POD copies, never replayable upstream objects. */
int fhg_graph(const void *,size_t,uint32_t,FhgGraph *,size_t);
int fhg_range(const void *,size_t,uint32_t,uint32_t,FhgRange *,size_t);
/* Recheck actual retained contents, table RAM and CPU-owned padding without
 * rewriting. Caller repeats exclusive zero-ref inactive lease first. */
int fhg_verify(void *,size_t,uint32_t *out_code);
#ifdef __cplusplus
}
#endif
#endif
