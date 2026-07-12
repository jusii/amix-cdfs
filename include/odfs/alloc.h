/*
 * odfs/alloc.h — small allocation wrapper for shared code
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ODFS_ALLOC_H
#define ODFS_ALLOC_H

#include "odfs/config.h"

#include <stddef.h>

#if defined(ODFS_PLATFORM_AMIX)
/*
 * AMIX (SVR4/m68k) userspace uses the plain libc malloc/free path in the
 * #else branch below — never the Amiga AllocVec/FreeVec path. This holds
 * because ODFS_PLATFORM_AMIGA is 0 on AMIX; the guard here only documents
 * that deliberate choice and emits nothing.
 */
#endif

#if defined(ODFS_PLATFORM_AMIX_KERNEL)

/*
 * AMIX in-kernel context: no libc. Allocate through the SVR4 kernel memory
 * allocator (<sys/kmem.h>). The mismatch this arm has to bridge: the core's
 * odfs_free(ptr) is handed no size, but SVR4 kmem_free(cp, size) REQUIRES
 * the original length. So odfs_malloc/odfs_calloc over-allocate an aligned
 * size_t header, stash the total (header + payload) length there, and hand
 * back the payload pointer; odfs_free reads the length back for kmem_free.
 * KM_SLEEP: the filesystem runs in process context, so blocking to obtain
 * memory is allowed.
 */
#include <sys/types.h>   /* size_t + _VOID, needed by <sys/kmem.h> */
#include <sys/kmem.h>    /* kmem_alloc/kmem_zalloc/kmem_free, KM_SLEEP */

/*
 * Header size: one size_t, rounded up to 8-byte alignment so the returned
 * payload keeps the 8-byte alignment the core's 64-bit fields expect. kmem
 * hands back at least word-aligned memory, so base + 8 stays 8-aligned.
 */
#define ODFS_KMEM_HDR ((size_t)8)

static inline void *odfs_malloc(size_t size)
{
    char   *base;
    size_t  total;

    if (size == 0)
        size = 1;
    total = size + ODFS_KMEM_HDR;
    base = (char *)kmem_alloc(total, KM_SLEEP);
    if (base == 0)
        return 0;
    *(size_t *)base = total;
    return (void *)(base + ODFS_KMEM_HDR);
}

static inline void *odfs_calloc(size_t count, size_t size)
{
    char   *base;
    size_t  total;

    if (size != 0 && count > ((size_t)-1) / size)
        return 0;

    total = count * size;
    if (total == 0)
        total = 1;
    total += ODFS_KMEM_HDR;

    base = (char *)kmem_zalloc(total, KM_SLEEP);
    if (base == 0)
        return 0;
    *(size_t *)base = total;
    return (void *)(base + ODFS_KMEM_HDR);
}

static inline void odfs_free(void *ptr)
{
    char *base;

    if (ptr == 0)
        return;
    base = (char *)ptr - ODFS_KMEM_HDR;
    kmem_free((void *)base, *(size_t *)base);
}

#elif ODFS_PLATFORM_AMIGA

#include "amiga_target_compat.h"

#include <exec/memory.h>
#include <exec/types.h>
#if ODFS_AMIGA_OS4
#include <exec/exectags.h>
#include <utility/tagitem.h>
#endif
#include <proto/exec.h>

static inline void *odfs_malloc(size_t size)
{
    if (size == 0)
        size = 1;
#if ODFS_AMIGA_OS4
    return AllocVecTags((ULONG)size, AVT_Type, MEMF_SHARED, TAG_END);
#else
    return AllocVec((ULONG)size, MEMF_PUBLIC);
#endif
}

static inline void *odfs_calloc(size_t count, size_t size)
{
    size_t total;

    if (size != 0 && count > ((size_t)-1) / size)
        return NULL;

    total = count * size;
    if (total == 0)
        total = 1;

#if ODFS_AMIGA_OS4
    return AllocVecTags((ULONG)total,
                        AVT_Type, MEMF_SHARED,
                        AVT_ClearWithValue, 0,
                        TAG_END);
#else
    return AllocVec((ULONG)total, MEMF_PUBLIC | MEMF_CLEAR);
#endif
}

static inline void odfs_free(void *ptr)
{
    if (ptr)
        FreeVec(ptr);
}

#else

#include <stdlib.h>

static inline void *odfs_malloc(size_t size)
{
    return malloc(size);
}

static inline void *odfs_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

static inline void odfs_free(void *ptr)
{
    free(ptr);
}

#endif

#endif /* ODFS_ALLOC_H */
