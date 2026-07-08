/*
 * platform/amix-kernel/compat/string.h — AMIX in-kernel <string.h> interceptor.
 *
 * The portable core/backends `#include <string.h>` and use memcpy/memmove/
 * memset/memcmp/strlen. The SVR4 kernel runtime (see <sys/systm.h>) exports
 * only bcopy/bzero/bcmp/strcmp/strncmp/strncpy/strncat — it has NO memcpy/
 * memset/memcmp/strlen. So the kernel-compiled tree has nothing to resolve
 * those four against.
 *
 * This directory is placed on the include search path AHEAD of the sysroot for
 * the AMIX in-kernel build only (same trick as platform/amix/compat/stdint.h),
 * so `#include <string.h>` resolves here. Under the kernel guard it hands the
 * portable code a minimal, const-correct set of prototypes and NOTHING else
 * (no <string.h> #pragma int_to_unsigned / strtok / strerror the kernel never
 * links against). The actual bodies for the cdfs-provided functions live in
 * platform/amix-kernel/kstring.c.
 *
 * IMPORTANT — why real symbols, not a `#define memcpy odfs_kmemcpy` redirect:
 * a preprocessor #define only catches SOURCE-level calls. GCC lowers aggregate
 * copies/zero-inits (e.g. `*out = mnt->virtual_root_map[i];` where the operand
 * is a ~540-byte odfs_node_t, and cdfs_getnode's `cnp->node = *n;`) to DIRECT
 * memcpy/memset calls at codegen time, which no macro can rewrite. Those force
 * the real symbols to exist. Since the SVR4 kernel defines none of them, cdfs
 * supplies the real memcpy/memmove/memset/memcmp/strlen with no link clash.
 * strcmp/strncmp/strncpy are left undeclared-here-as-cdfs and resolve to the
 * kernel's own copies.
 *
 * Inert everywhere else: without ODFS_PLATFORM_AMIX_KERNEL it falls through to
 * the next <string.h> on the path. This header is only ever on the search path
 * of the in-kernel build, so that branch is a safety net, not a normal path.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ODFS_AMIX_KERNEL_COMPAT_STRING_H
#define ODFS_AMIX_KERNEL_COMPAT_STRING_H

#if defined(ODFS_PLATFORM_AMIX_KERNEL)

/* size_t / NULL under the sysroot's shared guards so a later <sys/types.h> or
 * compiler <stddef.h> cooperates instead of colliding. */
#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

#ifndef NULL
#define NULL 0
#endif

/* cdfs-provided (bodies in kstring.c); prototypes match the GCC builtins. */
extern void  *memcpy(void *, const void *, size_t);
extern void  *memmove(void *, const void *, size_t);
extern void  *memset(void *, int, size_t);
extern int    memcmp(const void *, const void *, size_t);
extern size_t strlen(const char *);

/* Kernel-provided; declared here so the portable code that uses them compiles
 * without pulling the full sysroot <string.h>. They stay undefined externs in
 * libcdfskern.o, resolved at the final kernel link. */
extern int    strcmp(const char *, const char *);
extern int    strncmp(const char *, const char *, size_t);
extern char  *strncpy(char *, const char *, size_t);

#else /* not the in-kernel build: defer to the real <string.h> */

#include_next <string.h>

#endif /* ODFS_PLATFORM_AMIX_KERNEL */

#endif /* ODFS_AMIX_KERNEL_COMPAT_STRING_H */
