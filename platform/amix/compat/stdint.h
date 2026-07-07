/*
 * platform/amix/compat/stdint.h — AMIX <stdint.h> interceptor.
 *
 * The AMIX (SVR4/m68k) sysroot has no <stdint.h>. This directory is put
 * on the include search path (before the sysroot) only for the AMIX
 * build, so the portable code's `#include <stdint.h>` resolves here and
 * gets the fixed-width typedefs from odfs/compat_stdint.h — without any
 * edit to the shared headers/sources.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ODFS_AMIX_COMPAT_STDINT_H
#define ODFS_AMIX_COMPAT_STDINT_H

#include <odfs/compat_stdint.h>

#endif /* ODFS_AMIX_COMPAT_STDINT_H */
