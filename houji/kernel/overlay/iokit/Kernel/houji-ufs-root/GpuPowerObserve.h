/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef GIULIA_GPU_POWER_H
#define GIULIA_GPU_POWER_H
#include <stdint.h>
/* SM8650 offsets, from captured DTS and pinned gcc/gpucc-pineapple sources.
 * In particular CX hw-control is 0x9168, NOT sheng's 0x953c.
 * This stage only observes inherited state. It never enables a resource.
 */
enum { GGP_GCC, GGP_GPUCC, GGP_SMMU, GGP_WINDOWS };
enum { GGP_START, GGP_CFG_OFF, GGP_CX_OFF, GGP_CLOCK_OFF, GGP_OBSERVED, GGP_IO_ERROR };
struct ggp_io { void *cookie; int (*read)(void *, unsigned, uint32_t, uint32_t *); };
struct ggp_report {
    uint32_t stage, window, offset;
    uint32_t cfg, memnoc, dvm, cx, cx_hw, ahb, smmu_clock, gmu_clock;
    uint32_t scr0, id0, id1, id2, fault;
    uint32_t expected, observed, mask, compared, valid_fields;
    int io_error;
};
static inline int ggp_read(const struct ggp_io *io, struct ggp_report *r,
    unsigned window, uint32_t offset, uint32_t *value)
{
    r->window=window; r->offset=offset;
    r->io_error=io->read(io->cookie,window,offset,value);
    if(!r->io_error) {
        unsigned bit=32;
        if(window==GGP_GCC)bit=offset==4?0:offset==0x10?1:offset==0x18?2:32;
        else if(window==GGP_GPUCC)bit=offset==0x9108?3:offset==0x9168?4:offset==0x911c?5:offset==0x7000?6:offset==0x913c?7:32;
        else if(window==GGP_SMMU)bit=offset==0?8:offset==0x20?9:offset==0x24?10:offset==0x28?11:offset==0x48?12:32;
        if(bit<32)r->valid_fields|=1u<<bit;
    }
    if (r->io_error || *value==UINT32_MAX) {
        r->observed=*value;
        r->stage=GGP_IO_ERROR; return 0;
    }
    return 1;
}
static inline int ggp_running(uint32_t value) { return (value & 1) && !(value & (1u<<31)); }
static inline int ggp_expect(struct ggp_report *r,unsigned stage,unsigned window,unsigned offset,
    uint32_t observed,uint32_t mask,uint32_t expected)
{
    if ((observed&mask)==expected) return 1;
    r->stage=stage;r->window=window;r->offset=offset;
    r->observed=observed;r->mask=mask;r->expected=expected;r->compared=1;
    return 0;
}
static inline void ggp_observe(const struct ggp_io *io, struct ggp_report *r)
{
    unsigned i;
    if (!r) return;
    for (i=0;i<sizeof(*r);++i) ((unsigned char *)r)[i]=0;
    if (!io || !io->read) { r->stage=GGP_IO_ERROR; return; }
    if (!ggp_read(io,r,GGP_GCC,0x4,&r->cfg) ||
        !ggp_read(io,r,GGP_GCC,0x10,&r->memnoc) ||
        !ggp_read(io,r,GGP_GCC,0x18,&r->dvm)) return;
    /* GCC CFG branch may be hardware gated while idle: require enable. */
    if (!ggp_expect(r,GGP_CFG_OFF,GGP_GCC,4,r->cfg,1,1)) return;
    if (!ggp_read(io,r,GGP_GPUCC,0x9108,&r->cx) ||
        !ggp_read(io,r,GGP_GPUCC,0x9168,&r->cx_hw)) return;
    if (!ggp_expect(r,GGP_CX_OFF,GGP_GPUCC,0x9168,r->cx_hw,1u<<31,1u<<31) ||
        !ggp_expect(r,GGP_CX_OFF,GGP_GPUCC,0x9108,r->cx,1,0)) return;
    if (!ggp_read(io,r,GGP_GPUCC,0x911c,&r->ahb) ||
        !ggp_read(io,r,GGP_GPUCC,0x7000,&r->smmu_clock) ||
        !ggp_read(io,r,GGP_GPUCC,0x913c,&r->gmu_clock)) return;
    /* Pineapple AHB is BRANCH_HALT_DELAY. Ace 5 reads CBCR=0 even after
     * an enable write while SMMU/TCM accesses work; bit 0 is not an access
     * prerequisite. Keep halt, CX and the independently voted gates checked. */
    if (!ggp_expect(r,GGP_CLOCK_OFF,GGP_GPUCC,0x911c,r->ahb,0x80000000,0) ||
        !ggp_expect(r,GGP_CLOCK_OFF,GGP_GPUCC,0x7000,r->smmu_clock,0x80000001,1) ||
        !ggp_expect(r,GGP_CLOCK_OFF,GGP_GCC,0x10,r->memnoc,0x80000001,1) ||
        !ggp_expect(r,GGP_CLOCK_OFF,GGP_GCC,0x18,r->dvm,0x80000001,1)) return;
    if (!ggp_read(io,r,GGP_SMMU,0,&r->scr0) ||
        !ggp_read(io,r,GGP_SMMU,0x20,&r->id0) ||
        !ggp_read(io,r,GGP_SMMU,0x24,&r->id1) ||
        !ggp_read(io,r,GGP_SMMU,0x28,&r->id2) ||
        !ggp_read(io,r,GGP_SMMU,0x48,&r->fault)) return;
    r->stage=GGP_OBSERVED;
}
#endif
