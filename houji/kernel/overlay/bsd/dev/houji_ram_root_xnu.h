/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef HOUJI_RAM_ROOT_XNU_H
#define HOUJI_RAM_ROOT_XNU_H
#include "houji_ram_root.h"
#include <sys/types.h>
#define HRR_XNU_MAX_SPANS 64u
struct hrr_physical_span { uint64_t physical_address, bytes; };
struct hrr_xnu;

/* Private trusted binder API, not an ioctl/wire/bootarg address importer.
 * spans are consecutive pieces of the packed file in listed order, each backed
 * by a disjoint, PAGE_SIZE-aligned, permanently owned UEFI allocation. All but
 * the final allocation are fully used; only final page padding may be unused.
 * Before calling, the binder must exclude these allocations from the XNU VM
 * allocator and install matching normal-WB pmap-io-ranges. No device MMIO, UFS,
 * Android memory, reserved firmware memory, or kernel managed RAM is permitted.
 * validate_owned_ram must enforce those facts using the trusted allocation
 * manifest; accepting an arbitrary caller-supplied PA is prohibited.
 * expected digests are selected by the trusted controller, not the upload.
 * create additionally rehashes all received packed bytes before opening.
 * Call after VM, locks, SHA-256 and memdev init, from a sleepable thread.
 */
int hrr_xnu_create(const struct hrr_physical_span *, unsigned int span_count,
    uint64_t packed_bytes, const uint8_t expected_header_sha256[32],
    const uint8_t expected_packed_sha256[32],
    int (*validate_owned_ram)(void *, uint64_t, uint64_t), void *ownership,
    uint64_t cow_budget, unsigned int cache_chunks, struct hrr_xnu **);
/* Explicit separate registration; normally devid=0 with the existing rd=md0.
 * Once registered, backend/span ownership lasts until reboot. destroy refuses
 * a registered instance; there is no unsafe removal or buffer reuse API.
 */
int hrr_xnu_register(struct hrr_xnu *, int devid, dev_t *);
int hrr_xnu_destroy(struct hrr_xnu *);
#endif
