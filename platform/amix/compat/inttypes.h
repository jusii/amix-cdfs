/*
 * platform/amix/compat/inttypes.h — AMIX <inttypes.h> interceptor.
 *
 * The AMIX (SVR4/m68k) sysroot has no <inttypes.h>. Resolved instead of
 * the (absent) system header for the AMIX build; supplies both the
 * fixed-width typedefs and the PRI* format macros from
 * odfs/compat_stdint.h, so the shared code's `#include <inttypes.h>`
 * keeps working unedited.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ODFS_AMIX_COMPAT_INTTYPES_H
#define ODFS_AMIX_COMPAT_INTTYPES_H

#include <odfs/compat_stdint.h>

#endif /* ODFS_AMIX_COMPAT_INTTYPES_H */
