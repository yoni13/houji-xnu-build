/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * One-shot SM8650 firmware power preparation. Native resource calls are not
 * leases and may contain provider-owned polling; the adapter must audit those
 * internals separately. Every poll owned by this policy has a fixed bound.
 * No disable, rollback, reset write, DMA, TCM or GPU command is performed.
 */
#ifndef GIULIA_GPU_POWER_POLICY_H
#define GIULIA_GPU_POWER_POLICY_H
#include "GpuPowerReport.h"

enum gpp_resource { GPP_CFG, GPP_CX, GPP_GMU_RATE, GPP_HUB_RATE,
    GPP_GMU_CLK, GPP_CXO_CLK, GPP_AXI_CLK, GPP_MEMNOC_CLK, GPP_DVM_CLK,
    GPP_AHB_CLK, GPP_HUB_CLK, GPP_SMMU_CLK, GPP_ACD, GPP_RESOURCE_COUNT };
enum gpp_resource_operation { GPP_ENABLE=1, GPP_SET_RATE, GPP_GET_RATE };
enum gpp_register { GPP_REG_CFG, GPP_REG_CX, GPP_REG_CX_HW, GPP_REG_GMU,
    GPP_REG_CXO, GPP_REG_AXI, GPP_REG_MEMNOC, GPP_REG_DVM, GPP_REG_AHB,
    GPP_REG_HUB, GPP_REG_SMMU, GPP_REG_SYSRESET, GPP_REGISTER_COUNT };
enum { GPP_VALUE_GMU_RATE=12, GPP_VALUE_HUB_RATE=13,
    GPP_POLL_LIMIT=1000, GPP_POLL_DELAY_US=10 };

struct gpp_io {
    void *cookie;
    /* Resolve/check every provider ABI, resource and prerequisite before any
     * mutation. IsClockEnabled/On are observations, never ownership tokens.
     */
    GPP_U64 (*resolve)(void *);
    /* Vendor AOP ACD prerequisite; success proves transport consumption only.
     * The adapter retains an uncertain message and its persistent owner.
     */
    GPP_U64 (*acd)(void *);
    GPP_U64 (*resource)(void *,unsigned,unsigned,GPP_U32,GPP_U32 *);
    GPP_U64 (*read)(void *,unsigned,GPP_U32 *);
    GPP_U64 (*delay)(void *,unsigned);
};

static inline const char *gpp_resource_name(unsigned resource)
{
    static const char *const names[]={"gcc_gpu_cfg_ahb_clk","gpu_cc_cx_gdsc",
        "gpu_cc_cx_gmu_clk","gpu_cc_hub_aon_clk","gpu_cc_cx_gmu_clk",
        "gpu_cc_cxo_clk","gcc_ddrss_gpu_axi_clk","gcc_gpu_memnoc_gfx_clk",
        "gcc_gpu_snoc_dvm_gfx_clk","gpu_cc_ahb_clk","gpu_cc_hub_aon_clk",
        "gpu_cc_hlos1_vote_gpu_smmu_clk","aop-acd-disable"};
    return resource<GPP_RESOURCE_COUNT?names[resource]:"unknown";
}
static inline GPP_U64 gpp_register_address(unsigned reg)
{
    static const GPP_U32 addresses[]={0x00171004u,0x03d99108u,0x03d99168u,
        0x03d9913cu,0x03d99144u,0x00171154u,0x00171010u,0x00171018u,
        0x03d9911cu,0x03d993e8u,0x03d97000u,0x03d7e000u};
    return reg<GPP_REGISTER_COUNT?addresses[reg]:0;
}
static inline unsigned gpp_fail(struct gpp_report *r,unsigned code,GPP_U64 status)
{
    if(!r->phase) {
        r->phase=GPP_PHASE_RESOLVE;r->operation=GPP_OP_VALIDATE;
        r->resource=GPP_RESOURCE_COUNT;
    }
    if(!r->code) {
        r->code=code;r->firmware_status=status;
        r->failure_phase=r->phase;r->failure_operation=r->operation;
        r->failure_resource=r->resource;
    }
    if(r->attempted)r->retained=1;
    gpp_report_seal(r);return r->code;
}
static inline void gpp_action(struct gpp_report *r,unsigned phase,unsigned operation,unsigned resource)
{
    r->phase=phase;r->operation=operation;r->resource=resource;
    r->expected=r->observed=r->mask=0;
    r->flags&=~(GPP_EXPECTED_VALID|GPP_OBSERVED_VALID|GPP_MASK_VALID);
}
static inline int gpp_read_value(const struct gpp_io *io,struct gpp_report *r,unsigned reg)
{
    GPP_U32 value=0;
    GPP_U64 status;
    r->operation=GPP_OP_READ;r->last_read=reg;
    r->flags&=~GPP_OBSERVED_VALID;
    r->valid_reads&=~(1u<<reg);
    status=io->read(io->cookie,reg,&value);r->last_firmware_status=status;
    if(status || value==0xffffffffu) {
        r->failed_read=reg;r->observed=0;
        (void)gpp_fail(r,GPP_ERR_READ,status);return 0;
    }
    r->values[reg]=value;r->valid_reads|=1u<<reg;r->observed=value;
    r->flags|=GPP_OBSERVED_VALID;
    return 1;
}
static inline int gpp_wait_register(const struct gpp_io *io,struct gpp_report *r,
    unsigned reg,GPP_U32 mask,GPP_U32 expected)
{
    unsigned poll;
    for(poll=0;poll<GPP_POLL_LIMIT;poll++) {
        if(!gpp_read_value(io,r,reg))return 0;
        r->expected=expected;r->mask=mask;
        r->flags|=GPP_EXPECTED_VALID|GPP_MASK_VALID;
        if((r->values[reg]&mask)==expected)return 1;
        r->polls++;
        if(poll+1<GPP_POLL_LIMIT) {
            GPP_U64 status;
            r->operation=GPP_OP_DELAY;
            status=io->delay(io->cookie,GPP_POLL_DELAY_US);r->last_firmware_status=status;
            if(status) { (void)gpp_fail(r,GPP_ERR_RESOURCE,status);return 0; }
        }
    }
    r->operation=GPP_OP_VALIDATE;(void)gpp_fail(r,GPP_ERR_TIMEOUT,0);return 0;
}
static inline int gpp_enable(const struct gpp_io *io,struct gpp_report *r,unsigned resource)
{
    GPP_U32 actual=0;
    GPP_U64 status;
    r->resource=resource;r->operation=GPP_OP_ENABLE;
    r->attempted|=1u<<resource;r->retained=1;
    status=io->resource(io->cookie,resource,GPP_ENABLE,1,&actual);
    r->last_firmware_status=status;
    if(status) {(void)gpp_fail(r,GPP_ERR_RESOURCE,status);return 0;}
    return 1;
}
static inline int gpp_set_rate(const struct gpp_io *io,struct gpp_report *r,
    unsigned resource,GPP_U32 requested,unsigned value_slot)
{
    GPP_U32 actual=0,readback=0;
    GPP_U64 status;
    r->resource=resource;r->operation=GPP_OP_SET_RATE;
    r->attempted|=1u<<resource;r->retained=1;r->expected=requested;
    r->flags|=GPP_EXPECTED_VALID|GPP_MASK_VALID;r->mask=0xffffffffu;
    status=io->resource(io->cookie,resource,GPP_SET_RATE,requested,&actual);
    r->last_firmware_status=status;
    if(status) {(void)gpp_fail(r,GPP_ERR_RESOURCE,status);return 0;}
    r->observed=actual;r->mask=0xffffffffu;
    r->flags|=GPP_OBSERVED_VALID;
    if(actual!=requested) {(void)gpp_fail(r,GPP_ERR_RATE,0);return 0;}
    r->operation=GPP_OP_GET_RATE;
    r->flags&=~GPP_OBSERVED_VALID;r->observed=0;
    r->valid_reads&=~(1u<<value_slot);
    status=io->resource(io->cookie,resource,GPP_GET_RATE,0,&readback);
    r->last_firmware_status=status;
    if(status) {(void)gpp_fail(r,GPP_ERR_RESOURCE,status);return 0;}
    if(readback) {
        r->values[value_slot]=readback;r->valid_reads|=1u<<value_slot;
        r->observed=readback;r->flags|=GPP_OBSERVED_VALID;
    }
    if(readback!=requested) {(void)gpp_fail(r,GPP_ERR_RATE,0);return 0;}
    r->confirmed|=1u<<resource;return 1;
}

static inline unsigned gpp_prepare(const struct gpp_io *io,struct gpp_report *r)
{
    GPP_U64 status;
    unsigned i;
    static const unsigned branches[]={GPP_CXO_CLK,GPP_AXI_CLK,
        GPP_MEMNOC_CLK,GPP_DVM_CLK,GPP_AHB_CLK,GPP_HUB_CLK,GPP_SMMU_CLK};
    static const unsigned registers[]={GPP_REG_CXO,GPP_REG_AXI,
        GPP_REG_MEMNOC,GPP_REG_DVM,GPP_REG_AHB,GPP_REG_HUB,GPP_REG_SMMU};
    if(!r)return GPP_ERR_ARGUMENT;
    if(!gpp_report_requested(r))return gpp_fail(r,GPP_ERR_REUSED,0);
    if(!io || !io->resolve || !io->acd || !io->resource || !io->read || !io->delay)
        return gpp_fail(r,GPP_ERR_ARGUMENT,0);
    gpp_action(r,GPP_PHASE_RESOLVE,GPP_OP_RESOLVE,GPP_RESOURCE_COUNT);
    status=io->resolve(io->cookie);r->last_firmware_status=status;
    if(status)return gpp_fail(r,GPP_ERR_RESOLVE,status);

    gpp_action(r,GPP_PHASE_CFG,GPP_OP_READ,GPP_CFG);
    if(!gpp_read_value(io,r,GPP_REG_CFG))return r->code;
    /* Inherited config access is never claimed as a confirmed acquisition. */
    if(r->values[GPP_REG_CFG]&1u)r->flags|=GPP_CFG_INHERITED;
    else {
        if(!gpp_enable(io,r,GPP_CFG) || !gpp_wait_register(io,r,GPP_REG_CFG,1,1))return r->code;
        r->confirmed|=1u<<GPP_CFG;
    }
    /* GPUCC is read only after the GCC config-access gate is proven enabled.
     * Refuse a live/transitioning/inconsistent inherited CX before AOP, domain,
     * frequency or GPU branch mutations. No inherited resource is disabled.
     */
    gpp_action(r,GPP_PHASE_INITIAL_CX,GPP_OP_READ,GPP_CX);
    if(!gpp_read_value(io,r,GPP_REG_CX) || !gpp_read_value(io,r,GPP_REG_CX_HW))return r->code;
    if((r->values[GPP_REG_CX]&1u)!=1 || (r->values[GPP_REG_CX_HW]&0x80000000u)) {
        r->operation=GPP_OP_VALIDATE;return gpp_fail(r,GPP_ERR_INITIAL,0);
    }
    gpp_action(r,GPP_PHASE_ACD,GPP_OP_ACD,GPP_ACD);
    r->attempted|=1u<<GPP_ACD;r->retained=1;
    status=io->acd(io->cookie);r->last_firmware_status=status;
    if(status)return gpp_fail(r,GPP_ERR_ACD,status);
    r->confirmed|=1u<<GPP_ACD;r->flags|=GPP_ACD_CONSUMED;

    gpp_action(r,GPP_PHASE_CX,GPP_OP_ENABLE,GPP_CX);
    if(!gpp_enable(io,r,GPP_CX) ||
       !gpp_wait_register(io,r,GPP_REG_CX,1,0) ||
       !gpp_wait_register(io,r,GPP_REG_CX_HW,0x80000000u,0x80000000u))return r->code;
    r->confirmed|=1u<<GPP_CX;

    /* Do not change a running/inherited GMU core clock. The CX config AHB
     * path exposes its GPUCC branch independently of the GMU core source.
     */
    gpp_action(r,GPP_PHASE_CLOCKS,GPP_OP_READ,GPP_GMU_CLK);
    if(!gpp_read_value(io,r,GPP_REG_GMU))return r->code;
    r->expected=0;r->mask=1;r->flags|=GPP_EXPECTED_VALID|GPP_MASK_VALID;
    if(r->values[GPP_REG_GMU]&1) {
        r->operation=GPP_OP_VALIDATE;return gpp_fail(r,GPP_ERR_INITIAL,0);
    }
    gpp_action(r,GPP_PHASE_HUB_RATE,GPP_OP_SET_RATE,GPP_HUB_RATE);
    if(!gpp_set_rate(io,r,GPP_HUB_RATE,200000000u,GPP_VALUE_HUB_RATE))return r->code;

    for(i=0;i<sizeof(branches)/sizeof(branches[0]);i++) {
        gpp_action(r,GPP_PHASE_CLOCKS,GPP_OP_ENABLE,branches[i]);
        if(!gpp_enable(io,r,branches[i]))return r->code;
        /* DDRSS AXI uses vendor BRANCH_HALT_SKIP: enable readback is the
         * supported proof; do not invent an active-toggling guarantee.
         */
        if(!gpp_wait_register(io,r,registers[i],
            branches[i]==GPP_AXI_CLK?1u:0x80000001u,1u))return r->code;
        r->confirmed|=1u<<branches[i];
    }
    /* GEN7_GMU_CM3_SYSRESET is the CX AHB register at GPU+0x1f800*4.
     * GPUCC defines AHB/HUB and CXO separately from the GMU core branch.
     * Only after this bus path is ready may reset be observed while the core
     * branch remains disabled. A bad read or released reset blocks both the
     * GMU rate request and core-clock enable; no reset writes are permitted.
     * This observation does not prove that outstanding GPU DMA was drained.
     */
    gpp_action(r,GPP_PHASE_RESET,GPP_OP_READ,GPP_GMU_CLK);
    if(!gpp_read_value(io,r,GPP_REG_GMU))return r->code;
    if(r->values[GPP_REG_GMU]&1) {
        r->operation=GPP_OP_VALIDATE;return gpp_fail(r,GPP_ERR_INITIAL,0);
    }
    if(!gpp_read_value(io,r,GPP_REG_SYSRESET))return r->code;
    r->mask=1;r->expected=1;r->flags|=GPP_EXPECTED_VALID|GPP_MASK_VALID;
    if(!(r->values[GPP_REG_SYSRESET]&1)) {
        r->operation=GPP_OP_VALIDATE;return gpp_fail(r,GPP_ERR_RESET,0);
    }
    gpp_action(r,GPP_PHASE_GMU_RATE,GPP_OP_SET_RATE,GPP_GMU_RATE);
    if(!gpp_set_rate(io,r,GPP_GMU_RATE,625000000u,GPP_VALUE_GMU_RATE))return r->code;
    /* Keep the reset proof adjacent to the actual clock-enable transaction. */
    gpp_action(r,GPP_PHASE_RESET,GPP_OP_READ,GPP_GMU_CLK);
    if(!gpp_read_value(io,r,GPP_REG_SYSRESET))return r->code;
    r->mask=1;r->expected=1;r->flags|=GPP_EXPECTED_VALID|GPP_MASK_VALID;
    if(!(r->values[GPP_REG_SYSRESET]&1)) {
        r->operation=GPP_OP_VALIDATE;return gpp_fail(r,GPP_ERR_RESET,0);
    }
    gpp_action(r,GPP_PHASE_CLOCKS,GPP_OP_ENABLE,GPP_GMU_CLK);
    if(!gpp_enable(io,r,GPP_GMU_CLK) ||
       !gpp_wait_register(io,r,GPP_REG_GMU,0x80000001u,1u))return r->code;
    r->confirmed|=1u<<GPP_GMU_CLK;
    gpp_action(r,GPP_PHASE_READY,GPP_OP_READ,GPP_CX);
    if(!gpp_read_value(io,r,GPP_REG_CFG))return r->code;
    if(!(r->values[GPP_REG_CFG]&1)) {
        r->operation=GPP_OP_VALIDATE;return gpp_fail(r,GPP_ERR_INITIAL,0);
    }
    if(!gpp_read_value(io,r,GPP_REG_CX) || !gpp_read_value(io,r,GPP_REG_CX_HW))return r->code;
    if((r->values[GPP_REG_CX]&1) ||
       !(r->values[GPP_REG_CX_HW]&0x80000000u)) {
        r->operation=GPP_OP_VALIDATE;return gpp_fail(r,GPP_ERR_INITIAL,0);
    }
    for(i=0;i<sizeof(branches)/sizeof(branches[0]);i++) {
        r->resource=branches[i];
        if(!gpp_wait_register(io,r,registers[i],
            branches[i]==GPP_AXI_CLK?1u:0x80000001u,1u))return r->code;
    }
    r->resource=GPP_GMU_CLK;
    if(!gpp_wait_register(io,r,GPP_REG_GMU,0x80000001u,1u))return r->code;
    gpp_action(r,GPP_PHASE_RESET,GPP_OP_READ,GPP_RESOURCE_COUNT);
    if(!gpp_read_value(io,r,GPP_REG_SYSRESET))return r->code;
    r->mask=1;r->expected=1;
    r->flags|=GPP_EXPECTED_VALID|GPP_MASK_VALID;
    if(!(r->values[GPP_REG_SYSRESET]&1)) {
        r->operation=GPP_OP_VALIDATE;return gpp_fail(r,GPP_ERR_RESET,0);
    }
    r->flags|=GPP_READY;r->phase=GPP_PHASE_DONE;r->operation=GPP_OP_NONE;
    gpp_report_seal(r);return GPP_OK;
}
#endif
