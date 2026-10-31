/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef GIULIA_GPU_POWER_REPORT_H
#define GIULIA_GPU_POWER_REPORT_H
#if !defined(GPP_FIRMWARE_TYPES)
#include <stdint.h>
typedef uint32_t GPP_U32;
typedef uint64_t GPP_U64;
#else
typedef UINT32 GPP_U32;
typedef UINT64 GPP_U64;
#endif

#define GPP_REPORT_MAGIC 0x31505047u /* GPP1 */
#define GPP_REPORT_VERSION 1u
#define GPP_REPORT_BYTES 256u
#define GPP_REQUEST_PREPARE 1u
#define GPP_PROTOCOL_VERSION 1u
#define GPP_PROTOCOL_GUID {0xf5b737f6,0x3b39,0x438d,{0x84,0xc0,0x17,0xcb,0x75,0x00,0x00,0x01}}

enum gpp_code { GPP_OK, GPP_ERR_ARGUMENT, GPP_ERR_RESOLVE, GPP_ERR_READ,
    GPP_ERR_INITIAL, GPP_ERR_ACD, GPP_ERR_RESOURCE, GPP_ERR_RATE,
    GPP_ERR_TIMEOUT, GPP_ERR_RESET, GPP_ERR_REUSED, GPP_ERR_PROTOCOL };
enum gpp_phase { GPP_PHASE_NONE, GPP_PHASE_RESOLVE, GPP_PHASE_CFG,
    GPP_PHASE_INITIAL_CX, GPP_PHASE_ACD, GPP_PHASE_CX, GPP_PHASE_GMU_RATE,
    GPP_PHASE_HUB_RATE, GPP_PHASE_CLOCKS, GPP_PHASE_READY,
    GPP_PHASE_RESET, GPP_PHASE_DONE, GPP_PHASE_LOADER=0x100 };
enum gpp_operation { GPP_OP_NONE, GPP_OP_RESOLVE, GPP_OP_READ,
    GPP_OP_ENABLE, GPP_OP_SET_RATE, GPP_OP_GET_RATE, GPP_OP_ACD,
    GPP_OP_DELAY, GPP_OP_VALIDATE };
enum gpp_flags { GPP_CFG_INHERITED=1, GPP_READY=2, GPP_ACD_CONSUMED=4,
    GPP_PROTOCOL_UNCERTAIN=8, GPP_QMP_UNCERTAIN=16, GPP_PROVIDER_UNBOUNDED=32,
    GPP_EXPECTED_VALID=64, GPP_OBSERVED_VALID=128, GPP_MASK_VALID=256 };

/* Fixed-width handoff only: no live Boot Services handles or pointers. An
 * unset valid_reads bit means unknown, including when values[] contains zero.
 * attempted is set BEFORE a potentially mutating callback; confirmed is set
 * only after its independent readback. Resources are retained until reset.
 */
struct gpp_report {
    GPP_U32 magic, version, bytes, request;
    GPP_U32 code, phase, operation, resource;
    GPP_U32 attempted, confirmed, valid_reads, polls;
    GPP_U32 retained, flags, last_read, failed_read;
    GPP_U64 firmware_status, last_firmware_status, protocol_status;
    GPP_U64 expected, observed, mask;
    GPP_U32 values[16];
    GPP_U32 failure_phase, failure_operation, failure_resource;
    GPP_U32 reserved[16];
    GPP_U32 crc32;
};
typedef char gpp_report_size_check[sizeof(struct gpp_report)==GPP_REPORT_BYTES?1:-1];

static inline GPP_U32 gpp_crc32(const void *data, unsigned bytes)
{
    const unsigned char *p=(const unsigned char *)data;
    GPP_U32 crc=0xffffffffu;
    unsigned i,j;
    for(i=0;i<bytes;i++) {
        crc^=p[i];
        for(j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
    }
    return ~crc;
}
static inline void gpp_report_seal(struct gpp_report *r)
{ if(r)r->crc32=gpp_crc32(r,GPP_REPORT_BYTES-4); }
static inline void gpp_report_init(struct gpp_report *r)
{
    unsigned i;
    if(!r)return;
    for(i=0;i<sizeof(*r);i++)((unsigned char *)r)[i]=0;
    r->magic=GPP_REPORT_MAGIC;r->version=GPP_REPORT_VERSION;
    r->bytes=GPP_REPORT_BYTES;r->request=GPP_REQUEST_PREPARE;
}
static inline int gpp_report_valid(const struct gpp_report *r)
{
    unsigned i;
    if(!r || r->magic!=GPP_REPORT_MAGIC || r->version!=GPP_REPORT_VERSION ||
       r->bytes!=GPP_REPORT_BYTES || r->request!=GPP_REQUEST_PREPARE ||
       !r->phase ||
       r->crc32!=gpp_crc32(r,GPP_REPORT_BYTES-4))return 0;
    for(i=0;i<16;i++)if(r->reserved[i])return 0;
    return 1;
}
static inline int gpp_report_requested(const struct gpp_report *r)
{
    struct gpp_report pristine;
    const unsigned char *a=(const unsigned char *)r,*b=(const unsigned char *)&pristine;
    unsigned i;
    if(!r)return 0;
    gpp_report_init(&pristine);
    for(i=0;i<sizeof(pristine);i++)if(a[i]!=b[i])return 0;
    return 1;
}
#endif
