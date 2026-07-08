/*
 * kstring.c — the mem primitives the AMIX kernel does NOT already export.
 *
 * The portable core/backends — and GCC's own lowering of aggregate struct
 * copies/zero-inits — reference memcpy/memmove/memset/memcmp/strlen. But of
 * those the kernel's ml/exp module ALREADY exports GLOBAL memcpy and strlen,
 * so cdfs must NOT define them: a second definition makes the whole-kernel
 * `ld -o unix -r` fail with "multiple definition of 'memcpy'"/"'strlen'".
 * memcpy and strlen are therefore left as undefined externs (declared in
 * compat/string.h) and resolve to the kernel's ml/exp copies at the final
 * link — the standard ABI every other kernel module relies on.
 *
 * The kernel exports NEITHER memmove NOR memset, and its only memcmp lives in
 * netinet as a static/local symbol (no clash), so cdfs supplies exactly those
 * three here, layered on the kernel's bcopy/bzero where possible.
 *
 * MUST be compiled with -fno-builtin (see the Makefile per-object override):
 * otherwise GCC recognizes the byte loops below as the very memset/memcmp
 * idioms and rewrites them into self-calls (infinite recursion) — the same
 * freestanding trap the OS4 handler's freestanding.o guards against.
 *
 * The kernel routines are spelled K&R (no prototype), matching the period
 * kernel's own <sys/systm.h> declarations, and <sys/systm.h> is deliberately
 * NOT included: it would drag in non-const strcmp/strncpy prototypes that clash
 * with the compiler built-ins. bcopy's argument order is (src, dst, n) — the
 * OPPOSITE of memcpy(dst, src, n); the wrappers swap accordingly.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#if defined(ODFS_PLATFORM_AMIX_KERNEL)

#include <sys/types.h>   /* size_t, caddr_t */

/* Kernel primitives, declared K&R (whatever the kernel provides). */
extern void bcopy(/* caddr_t src, caddr_t dst, size_t n */);
extern void bzero(/* caddr_t s, size_t n */);

void *memmove(dst, src, n)
    void *dst;
    const void *src;
    size_t n;
{
    /* SVR4 bcopy tolerates overlapping regions, so it doubles as memmove. */
    bcopy((caddr_t)src, (caddr_t)dst, n);
    return dst;
}

void *memset(s, c, n)
    void *s;
    int c;
    size_t n;
{
    unsigned char *p = (unsigned char *)s;
    unsigned char  v = (unsigned char)c;
    size_t         i;

    if (v == 0) {
        bzero((caddr_t)s, n);
        return s;
    }
    for (i = 0; i < n; i++)
        p[i] = v;
    return s;
}

int memcmp(a, b, n)
    const void *a;
    const void *b;
    size_t n;
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    size_t               i;

    for (i = 0; i < n; i++) {
        if (pa[i] != pb[i])
            return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

#endif /* ODFS_PLATFORM_AMIX_KERNEL */
