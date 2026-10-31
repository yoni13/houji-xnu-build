/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef _SYS_MEMDEV_OPS_H_
#define _SYS_MEMDEV_OPS_H_
#include <sys/types.h>
#include <stdint.h>
#include <stddef.h>

/* Internal kernel binding only: there is no ioctl, address import, or user
 * callback registration. The binder must validate/own every backing range.
 * ops and context remain alive while the device is registered. Callbacks are
 * synchronous, receive kernel virtual buffers, and return zero or errno.
 * They must serialize their own state; writes must finish before returning.
 * The backend must not retain buffer pointers or re-enter the same mdev.
 */
struct mdev_ops {
    int (*transfer)(void *context, uint64_t offset, void *buffer,
        size_t bytes, int write);
    int (*synchronize)(void *context);
};

#ifdef XNU_KERNEL_PRIVATE
#ifdef __cplusplus
extern "C" {
#endif
dev_t mdevadd_ops(int devid, uint64_t logical_bytes,
    const struct mdev_ops *ops, void *context);
#ifdef __cplusplus
}
#endif
#endif
#endif
