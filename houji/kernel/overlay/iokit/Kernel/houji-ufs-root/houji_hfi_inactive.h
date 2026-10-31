/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef HOUJI_HFI_INACTIVE_BRIDGE_H
#define HOUJI_HFI_INACTIVE_BRIDGE_H
#include <stddef.h>
#include <stdint.h>

/* This header is C11/C++14 POD only. It deliberately exposes no EFI, IOKit,
 * C99 flow implementation, SMC, MMIO, TTBR, stream or firmware-execution API. */
#define HHI_ABI_VERSION 1u
#define HHI_STORAGE_BYTES 4096u
#define HHI_STORAGE_ALIGNMENT 16u
#define HHI_RESOURCE_ROWS 9u
#define HHI_GMU_RANGES 8u
#define HHI_FIRMWARE_BYTES 78588u

typedef struct HhiStorage {
#ifdef __cplusplus
    alignas(HHI_STORAGE_ALIGNMENT) unsigned char bytes[HHI_STORAGE_BYTES];
#else
    _Alignas(HHI_STORAGE_ALIGNMENT) unsigned char bytes[HHI_STORAGE_BYTES];
#endif
} HhiStorage;
/* First use MUST zero-initialize this storage (C: HhiStorage s={0};
 * C++14: HhiStorage s{};). After init its address is its identity: do not copy,
 * move, memcpy, or manually clear it while the lifecycle owns any resource.
 * Public APIs reject a byte-for-byte copy at another address. Single owner,
 * externally serialized calls only; the busy guard rejects callback reentry,
 * and is not a substitute for an inter-thread lock. */

enum HhiStatus { HHI_OK=0, HHI_ARGUMENT=1, HHI_ALIGNMENT=2,
    HHI_UNINITIALIZED=3, HHI_BUSY=4, HHI_ALREADY_PREPARED=5,
    HHI_OWNERS_REMAIN=6, HHI_NOT_PREPARED=7 };
enum HhiPhase { HHI_INITIALIZED=1, HHI_PREPARING=2, HHI_PREPARED=3,
    HHI_RELEASING=4, HHI_RELEASE_ATTEMPTED=5 };

typedef struct HhiAllocation {
    uint64_t physical;
    void *cpu;
    uint32_t gpu4k_pages, normal_nc, iova, reserved;
} HhiAllocation;

typedef struct HhiCallbacks {
    uint32_t abi_version, reserved;
    void *cookie;
    /* On failure, return no transferred resource (leave both outputs zero).
     * The adapter retains/quarantines any uncertain internal allocation in its
     * own ledger. Nonzero failed outputs are also recorded by this bridge;
     * they are NOT invented into upstream allocated/retained/released masks. */
    int (*allocate)(void *,uint32_t gpu4k_pages,uint64_t *,void **);
    /* Validate complete exclusive CPU backing, PA bounds and descriptor-view
     * Normal-NC/WB attributes. Qualify known aperture aliases by AT only at the
     * same PA (WB or NC); do not access their data. This inactive CPU profile
     * does not qualify alias/cache ownership for DMA or firmware execution.
     * GPU logical pages are not CPU pages. */
    int (*validate)(void *,uint64_t,void *,uint32_t gpu4k_pages,uint32_t normal_nc);
    /* CPU cache publication ONLY: clean the full owned WB backing / order NC
     * memory. This callback MUST NOT install TTBRs, activate a bank/stream,
     * touch MMIO, call SMC, or execute firmware. */
    int (*publish)(void *,const HhiAllocation *);
    /* Restore/verify complete CPU attributes before successful release.
     * Failure means the adapter still owns the allocation. */
    int (*release)(void *,uint64_t,void *,uint32_t gpu4k_pages);
} HhiCallbacks;

typedef struct HhiCallbackTrace {
    uint32_t attempted_mask, success_mask, failure_mask, calls;
} HhiCallbackTrace;
typedef struct HhiRange {
    uint64_t iova, physical, bytes;
    uint32_t permissions, writeback, shareability, reserved;
} HhiRange;
typedef struct HhiRow {
    uint32_t index, planned, is_table, range_initialized;
    uint32_t allocated, validated, released, retained;
    HhiAllocation allocation;
    HhiRange range;
} HhiRow;
typedef struct HhiSummary {
    uint32_t abi_version, phase, prepare_called, release_calls;
    /* Exact upstream result values; never normalized to fabricate cleanup. */
    uint32_t hfm_code, records, reservations, count;
    uint32_t allocated, validated, released, retained;
    uint32_t bytes_loaded, tcm_bytes_deferred, hfi_queues, hfi_initialized;
    uint32_t published, build_status, audit_status, reserved;
    /* Preserve the cause before upstream cleanup can overwrite it. */
    uint32_t first_prepare_error, prepare_return_code, release_return_code;
    uint32_t uncertain_allocate_outputs;
    uint32_t input_bits, output_bits;
    uint64_t table_physical, leaf_budget;
    uint64_t root_physical, mair, built_table_pages, built_leaf_pages;
    uint32_t start_level, upper_region;
    uint64_t audit_descriptor_reads, audit_table_pages, audit_leaf_pages;
    uint64_t audit_descriptor_physical, audit_descriptor, audit_iova, audit_expected_descriptor;
    uint32_t audit_level, audit_complete;
    HhiCallbackTrace allocate_trace, validate_trace, publish_trace, release_trace;
} HhiSummary;

#ifdef __cplusplus
extern "C" {
#endif
/* Fixed envelope for caller-owned storage; actual internal layout is diagnostic
 * only and cannot be used to allocate a smaller buffer. Every API requires
 * exactly HHI_STORAGE_BYTES and HHI_STORAGE_ALIGNMENT. */
size_t hhi_actual_state_size(void);
size_t hhi_actual_state_alignment(void);
size_t hhi_storage_size(void);
size_t hhi_storage_alignment(void);
int hhi_init(void *,size_t,const HhiCallbacks *,size_t callbacks_bytes);
/* The caller MUST validate exact firmware length AND its pinned SHA-256 before
 * this call. The bridge repeats the length check, not the identity check, and
 * uses upstream's independent parsers and planners without rewriting them.
 * One prepare attempt per init, including failure. An HHI_OK return means the
 * upstream function ran; out_hfm_code/summary state its actual result. */
int hhi_prepare(void *,size_t,const uint8_t *,size_t,uint32_t *out_hfm_code);
int hhi_summary(const void *,size_t,HhiSummary *,size_t summary_bytes);
int hhi_row(const void *,size_t,uint32_t,HhiRow *,size_t row_bytes);
/* Retry upstream release only for allocated && !released. Successful release
 * leaves stale PA/CPU metadata in the snapshot: NEVER dereference a released
 * row. Raw hfm_code can retain an earlier failure after cleanup succeeds. */
int hhi_release(void *,size_t,uint32_t *out_hfm_code);
/* Read-only verification of the successful, retained inactive RAM profile.
 * Caller first validates the pinned firmware SHA-256 and holds every backing
 * allocation exclusively, with no device references, DMA or concurrent access.
 * No ownership/cache callback is invoked; CPU attributes and rounded backing
 * tails remain the adapter's responsibility. Requires no release attempt.
 * API rejection leaves out_code unchanged. HHI_OK sets out_code: 0=match,
 * 1=planner/profile mismatch, 2=firmware payload mismatch, 3=nonzero firmware
 * hole, 4=HFI header/ring/padding mismatch, 5=nonzero log, 6=nonzero VRB.
 * Buffers and opaque layout are never rewritten. */
int hhi_verify_contents(void *,size_t,const uint8_t *,size_t,uint32_t *out_code);
#ifdef __cplusplus
}
#endif
#endif
