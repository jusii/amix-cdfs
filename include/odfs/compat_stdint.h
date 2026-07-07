/*
 * odfs/compat_stdint.h — fixed-width integer types and printf format
 * macros for the AMIX (SVR4/m68k) userspace cross build.
 *
 * The AMIX sysroot ships neither <stdint.h> nor <inttypes.h>, and the
 * period cross compiler (GCC 2.7.2.3) predates them, so the portable
 * core/backends — which pull those headers in directly — have nothing
 * to resolve against on AMIX. This header supplies exactly the set the
 * tree uses. platform/amix/compat/{stdint,inttypes}.h forward here so
 * the existing `#include <stdint.h>` / `#include <inttypes.h>` lines in
 * the shared code keep working without being edited.
 *
 * Inert on every other platform: the whole body is compiled only when
 * ODFS_PLATFORM_AMIX is defined (set via -DODFS_PLATFORM_AMIX). Host and
 * AmigaOS builds never see any of it.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ODFS_COMPAT_STDINT_H
#define ODFS_COMPAT_STDINT_H

#if defined(ODFS_PLATFORM_AMIX)

/*
 * Prefer the compiler's own width builtins when it advertises them
 * (GCC >= 4.5, clang) so the typedefs are exact whatever the ABI. The
 * AMIX toolchain is GCC 2.7.2.3, which defines none of these macros, so
 * it takes the explicit fallback below, keyed to the m68k SVR4 ABI:
 * char 8, short 16, int 32, long 32, long long 64, pointer 32.
 */
#if defined(__UINT32_TYPE__) && defined(__INT32_TYPE__)

typedef __INT8_TYPE__   int8_t;
typedef __UINT8_TYPE__  uint8_t;
typedef __INT16_TYPE__  int16_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __INT32_TYPE__  int32_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __INT64_TYPE__  int64_t;
typedef __UINT64_TYPE__ uint64_t;

#else /* GCC 2.7.2.3 on m68k-cbm-sysv4: no width builtins */

typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef short              int16_t;
typedef unsigned short     uint16_t;
typedef int                int32_t;
typedef unsigned int       uint32_t;
typedef long long          int64_t;
typedef unsigned long long uint64_t;

#endif

/*
 * printf conversion macros actually referenced by the tree: PRIu16,
 * PRIu32, PRIu64, PRIx32. With the mapping above, uint16_t/uint32_t are
 * (unsigned) short/int, so they need no length modifier; uint64_t is
 * unsigned long long, hence the "ll" prefix.
 */
#ifndef PRIu16
#define PRIu16 "u"
#endif
#ifndef PRIu32
#define PRIu32 "u"
#endif
#ifndef PRIx32
#define PRIx32 "x"
#endif
#ifndef PRIu64
#define PRIu64 "llu"
#endif

/*
 * Limit macros. Fixed by width regardless of which typedef branch above is
 * taken, so they are spelled as plain integer constants of the right width
 * (int/long are 32-bit, long long is 64-bit on this ABI). The tree uses
 * UINT32_MAX and INT16_MIN; the rest complete the standard set a <stdint.h>
 * is expected to provide.
 */
#ifndef INT8_MIN
#define INT8_MIN   (-128)
#endif
#ifndef INT8_MAX
#define INT8_MAX   127
#endif
#ifndef UINT8_MAX
#define UINT8_MAX  255
#endif
#ifndef INT16_MIN
#define INT16_MIN  (-32767 - 1)
#endif
#ifndef INT16_MAX
#define INT16_MAX  32767
#endif
#ifndef UINT16_MAX
#define UINT16_MAX 65535
#endif
#ifndef INT32_MIN
#define INT32_MIN  (-2147483647 - 1)
#endif
#ifndef INT32_MAX
#define INT32_MAX  2147483647
#endif
#ifndef UINT32_MAX
#define UINT32_MAX 4294967295U
#endif
#ifndef INT64_MIN
#define INT64_MIN  (-9223372036854775807LL - 1)
#endif
#ifndef INT64_MAX
#define INT64_MAX  9223372036854775807LL
#endif
#ifndef UINT64_MAX
#define UINT64_MAX 18446744073709551615ULL
#endif

#endif /* ODFS_PLATFORM_AMIX */

#endif /* ODFS_COMPAT_STDINT_H */
