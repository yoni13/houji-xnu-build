/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Fixed A750v2 secure-CX initialization only; no caller-selected SCM IDs,
 * secure buffers, PAS/ZAP loading, or GPU command submission.
 * ABI: OnePlus android_kernel_oneplus_sm8650 commit
 * d763ed47deec55451366c1187f813f2f309d88a7, drivers/firmware/qcom_scm{,-smc}.c.
 * Request: graphics-kernel adreno_gen7.c + gen7_9_1 profile at modules commit
 * 453d8875f815bf9dfad9035982861226a4823a6c.
 */
#ifndef GIULIA_GPU_SCM_H
#define GIULIA_GPU_SCM_H
#include <stdint.h>

enum { GGS_PROBE, GGS_INIT };
enum { GGS_OK, GGS_ARGUMENT, GGS_PREREQUISITE, GGS_TRANSPORT,
    GGS_CONVENTION, GGS_UNAVAILABLE, GGS_SECURE_ERROR, GGS_RETRY_LIMIT,
    GGS_WAITQ, GGS_QUARANTINED, GGS_ALREADY_INITIALIZED };
enum { GGS_PHASE_NONE, GGS_PHASE_CONVENTION, GGS_PHASE_AVAILABILITY,
    GGS_PHASE_INIT };
enum { GGS_INIT_REQUEST=0x0b, GGS_BUSY_RETRIES=20,
    GGS_INTERRUPT_RESUMES=32, GGS_BUSY_DELAY_MS=30 };
struct ggs_regs { uint64_t x[8]; };
struct ggs_ops {
    void *cookie;
    int (*call)(void *,struct ggs_regs *); /* In/out x0..x7; zero on transport success. */
    int (*delay_ms)(void *,unsigned);
    /* Owner checks boot epoch, exclusive ownership, power/clocks and reset.
     * This helper cannot infer those facts from a terminal token. */
    int (*prerequisites)(void *,unsigned operation);
};
struct ggs_state {
    unsigned convention_verified,available,init_succeeded,quarantined;
    unsigned calls,busy_retries,interrupt_resumes,phase,result;
    struct ggs_regs last_input,last_output;
};

/* Internal exchange. Retrying INTERRUPTED reuses the original arguments and
 * returned x6 cookie. A BUSY retry starts a fresh call with x6=0.
 * Bounds apply to returned statuses, never to time spent inside one SMC. */
static inline int ggs_exchange_frame(const struct ggs_ops *o,struct ggs_state *s,
    const struct ggs_regs *original)
{
    unsigned busy=0,interrupted=0;
    uint64_t function_next=original->x[0],cookie=0;
    if(s->quarantined)return GGS_QUARANTINED;
    for (;;) {
        struct ggs_regs registers=*original;
        registers.x[0]=function_next;registers.x[6]=cookie;
        int transport;
        uint64_t status;
        s->last_input=registers;
        ++s->calls;
        transport=o->call(o->cookie,&registers);
        s->last_output=registers;
        if(transport) {s->quarantined=1;return GGS_TRANSPORT;}
        /* Vendor success and positive control statuses compare full a0.
         * Its final error mapper accepts int; additionally accept the known
         * BUSY error's zero-extended encoding without truncating success. */
        status=registers.x[0];
        if(status==0)return GGS_OK;
        if(status==1) {
            if(interrupted++==GGS_INTERRUPT_RESUMES) {
                s->quarantined=1;return GGS_RETRY_LIMIT;
            }
            ++s->interrupt_resumes;
            function_next=1;cookie=registers.x[6];
            continue;
        }
        if(status==2 || status==3) {
            /* This runtime has no SCM waitqueue IRQ/resume owner. */
            s->quarantined=1;return GGS_WAITQ;
        }
        if(status==(uint64_t)(int64_t)-12 || status==UINT64_C(0xfffffff4)) {
            if(busy++==GGS_BUSY_RETRIES) {
                s->quarantined=1;return GGS_RETRY_LIMIT;
            }
            ++s->busy_retries;
            if(o->delay_ms(o->cookie,GGS_BUSY_DELAY_MS)) {
                s->quarantined=1;return GGS_TRANSPORT;
            }
            function_next=original->x[0];cookie=0;
            continue;
        }
        return GGS_SECURE_ERROR;
    }
}

static inline int ggs_exchange(const struct ggs_ops *o,struct ggs_state *s,
    uint32_t function,uint32_t argument)
{
    const struct ggs_regs original={{function,1,argument,0,0,0,0,0}};
    return ggs_exchange_frame(o,s,&original);
}

/* Zero-initialize state once for a verified boot epoch. The owner must not
 * clear quarantine merely because a host timeout elapsed or a new token was
 * supplied. Success records one call, not persistent secure-register state. */
static inline int ggs_run(const struct ggs_ops *o,struct ggs_state *s,unsigned operation)
{
    int result;
    if(!s)return GGS_ARGUMENT;
    if(!o || !o->call || !o->delay_ms || !o->prerequisites || operation>GGS_INIT)
        return s->result=GGS_ARGUMENT;
    if(s->quarantined)return s->result=GGS_QUARANTINED;
    if(operation==GGS_INIT && s->init_succeeded)
        return s->result=GGS_ALREADY_INITIALIZED;
    if(o->prerequisites(o->cookie,operation)!=1)
        return s->result=GGS_PREREQUISITE;
    if(!s->convention_verified) {
        s->phase=GGS_PHASE_CONVENTION;
        /* FAST SiP SMC64 probe of INFO_IS_CALL_AVAIL itself. */
        result=ggs_exchange(o,s,0xc2000601u,0x02000601u);
        if(result)return s->result=result;
        if(s->last_output.x[1]!=1)return s->result=GGS_CONVENTION;
        s->convention_verified=1;
    }
    s->phase=GGS_PHASE_AVAILABILITY;s->available=0;
    /* Standard SiP INFO availability query for service 0x28, command 1. */
    result=ggs_exchange(o,s,0x42000601u,0x02002801u);
    if(result)return s->result=result;
    if(!s->last_output.x[1])return s->result=GGS_UNAVAILABLE;
    s->available=1;
    if(operation==GGS_PROBE)return s->result=GGS_OK;
    if(o->prerequisites(o->cookie,operation)!=1)
        return s->result=GGS_PREREQUISITE;
    s->phase=GGS_PHASE_INIT;
    /* Standard SiP GPU INIT_REGS; ALWAYS | BCL | TSENSE, without CLX. */
    result=ggs_exchange(o,s,0x42002801u,GGS_INIT_REQUEST);
    if(!result)s->init_succeeded=1;
    return s->result=result;
}

#if defined(__aarch64__) && !defined(GG_HOST_TEST)
/* Called only by the owning worker, never by the USB parser or IRQ handler.
 * Unlike the PSCI helper, Qualcomm SCM arguments begin in x2 and x6 is an
 * in/out interrupted-call cookie. Keep full-width raw results for diagnosis. */
static inline int ggs_aarch64_call(void *unused,struct ggs_regs *registers)
{
    register uint64_t x0 __asm__("x0")=registers->x[0];
    register uint64_t x1 __asm__("x1")=registers->x[1];
    register uint64_t x2 __asm__("x2")=registers->x[2];
    register uint64_t x3 __asm__("x3")=registers->x[3];
    register uint64_t x4 __asm__("x4")=registers->x[4];
    register uint64_t x5 __asm__("x5")=registers->x[5];
    register uint64_t x6 __asm__("x6")=registers->x[6];
    register uint64_t x7 __asm__("x7")=registers->x[7];
    (void)unused;
    __asm__ volatile("smc #0" : "+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3),
        "+r"(x4),"+r"(x5),"+r"(x6),"+r"(x7) : :
        "x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","cc","memory");
    registers->x[0]=x0;registers->x[1]=x1;registers->x[2]=x2;registers->x[3]=x3;
    registers->x[4]=x4;registers->x[5]=x5;registers->x[6]=x6;registers->x[7]=x7;
    return 0;
}
#endif
#endif
