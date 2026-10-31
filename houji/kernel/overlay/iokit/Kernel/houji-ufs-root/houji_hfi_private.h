/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef HOUJI_HFI_PRIVATE_H
#define HOUJI_HFI_PRIVATE_H
#include "houji_hfi_inactive.h"
#include <stddef.h>
#define HFM_WITH_SCRATCH 0
#include "GpuFirmwareMemory.h"

#define HHI_MAGIC UINT64_C(0x484849494e414354)
struct HhiState {
    _Alignas(HHI_STORAGE_ALIGNMENT) uint64_t magic;
    uint64_t magic_inverse;
    uintptr_t self;
    uint32_t abi_version, busy, phase, prepare_called, release_calls;
    uint32_t first_prepare_error, prepare_return_code, release_return_code;
    uint32_t uncertain_allocate_outputs, allocate_index, release_cursor;
    HhiCallbacks callbacks;
    HhiCallbackTrace traces[4];
    struct hfm_result result;
};
_Static_assert(sizeof(struct HhiState)<=HHI_STORAGE_BYTES,"opaque state envelope");
_Static_assert(_Alignof(struct HhiState)==HHI_STORAGE_ALIGNMENT,"opaque state alignment");
_Static_assert(HFM_MAX==8 && HFM_TABLE_PAGES==10,"exact inactive houji profile");
_Static_assert(sizeof(unsigned)==sizeof(uint32_t),"upstream unsigned ABI");

static inline void hhi_private_summary(const struct HhiState *s,HhiSummary *out) {
    const struct hfm_result *r=&s->result;HhiSummary v={0};
    v.abi_version=HHI_ABI_VERSION;v.phase=s->phase;v.prepare_called=s->prepare_called;v.release_calls=s->release_calls;
#define COPY(f) v.f=r->f
    v.hfm_code=r->code;COPY(records);COPY(reservations);COPY(count);COPY(allocated);COPY(validated);COPY(released);COPY(retained);
    COPY(bytes_loaded);COPY(tcm_bytes_deferred);COPY(hfi_queues);COPY(hfi_initialized);COPY(published);COPY(build_status);COPY(audit_status);
#undef COPY
    v.first_prepare_error=s->first_prepare_error;v.prepare_return_code=s->prepare_return_code;
    v.release_return_code=s->release_return_code;v.uncertain_allocate_outputs=s->uncertain_allocate_outputs;
    v.input_bits=r->config.input_bits;v.output_bits=r->config.output_bits;v.table_physical=r->config.table_physical;v.leaf_budget=r->config.leaf_budget;
    v.root_physical=r->built.root_physical;v.mair=r->built.mair;v.built_table_pages=r->built.table_pages;v.built_leaf_pages=r->built.leaf_pages;
    v.start_level=r->built.start_level;v.upper_region=r->built.upper_region;
    v.audit_descriptor_reads=r->audit.descriptor_reads;v.audit_table_pages=r->audit.table_pages;v.audit_leaf_pages=r->audit.leaf_pages;
    v.audit_descriptor_physical=r->audit.descriptor_physical;v.audit_descriptor=r->audit.descriptor;
    v.audit_iova=r->audit.iova;v.audit_expected_descriptor=r->audit.expected_descriptor;v.audit_level=r->audit.level;v.audit_complete=r->audit.complete;
    v.allocate_trace=s->traces[0];v.validate_trace=s->traces[1];v.publish_trace=s->traces[2];v.release_trace=s->traces[3];
    *out=v;
}
static inline void hhi_private_row(const struct HhiState *s,uint32_t index,HhiRow *out) {
    const struct hfm_result *r=&s->result;const struct hfm_allocation *a=&r->memory[index];
    HhiRow v={0};v.index=index;v.planned=(r->count==HHI_GMU_RANGES && a->pages!=0);v.is_table=v.planned && index==r->count;
    v.allocated=(r->allocated>>index)&1;v.validated=(r->validated>>index)&1;v.released=(r->released>>index)&1;v.retained=(r->retained>>index)&1;
    v.allocation=(HhiAllocation){a->physical,a->cpu,a->pages,a->nc,a->iova,0};
    if(index<r->count && index<HHI_GMU_RANGES) {
        const ArmSmmu4KRange *q=&r->ranges[index];v.range_initialized=v.validated;
        v.range=(HhiRange){q->iova,q->physical,q->bytes,q->permissions,q->writeback,q->shareability,0};
    }
    *out=v;
}
_Static_assert(sizeof(struct HhiState)==992,"HHI private size unchanged");
_Static_assert(offsetof(struct HhiState,callbacks)==72,"HHI callbacks ABI");
_Static_assert(offsetof(struct HhiState,traces)==120,"HHI traces ABI");
_Static_assert(offsetof(struct HhiState,result)==184,"HHI result ABI");
#endif
