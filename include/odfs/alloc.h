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
 * the original length. So odfs_malloc/odfs_calloc over-allocate a header,
 * stash the total (header + payload) length there, and hand back the payload
 * pointer; odfs_free reads the length back for kmem_free. KM_SLEEP: the
 * filesystem runs in process context, so blocking to obtain memory is allowed.
 *
 * TEMP DIAG (THROWAWAY — revert with the whole commit): those three bodies now
 * live in platform/amix-kernel/cdfs_guard.c, which brackets every block with a
 * head magic (in the 4 slack bytes of the header that m68k's 4-byte size_t
 * never used) and an 8-byte 0xC5 tail canary, and verifies both on free. This
 * arm is reduced to a forwarder so the diagnostic can be lifted back out in one
 * revert. Only the AMIX_KERNEL arm changes; the AmigaOS and host arms below are
 * untouched.
 */
#include <sys/types.h>   /* size_t + _VOID, needed by <sys/kmem.h> */
#include <sys/kmem.h>    /* kmem_alloc/kmem_zalloc/kmem_free, KM_SLEEP */

#include "cdfs_guard.h"  /* TEMP DIAG: -I platform/amix-kernel (see Makefile) */

static inline void *odfs_malloc(size_t size)
{
    return cdfs_guard_alloc(size, "odfs");
}

static inline void *odfs_calloc(size_t count, size_t size)
{
    return cdfs_guard_calloc(count, size, "odfs");
}

static inline void odfs_free(void *ptr)
{
    cdfs_guard_free(ptr, "odfs");
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
