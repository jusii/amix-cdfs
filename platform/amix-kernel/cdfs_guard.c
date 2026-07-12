/*
 * cdfs_guard.c — TEMP DIAG (THROWAWAY — revert with the whole commit).
 *
 * Bodies for the guard-band allocator declared in cdfs_guard.h. See that header
 * for the block layout, the leak-on-bad-header safety rule, and why every guard
 * access is byte-wise.
 *
 * This is the ONLY TU in the diagnostic that calls cmn_err, so the one header
 * whose declarations could collide with the kernel's own (<sys/cmn_err.h> also
 * declares printf/panic) is confined to a single file.
 *
 * Console output is deliberately restricted to the %s / %d / %x / %c
 * conversions. The AMIX kernel's prf() is binary-only here (no source to check),
 * and the in-kernel a4091 driver — proven on this exact kernel — uses nothing
 * wider. %p, %z, %ll and the `l` modifier are NOT used: a conversion the kernel
 * formatter does not know would mis-consume the varargs and every later argument
 * in the line would be garbage — a diagnostic that lies is worse than none. All
 * arguments are therefore cast explicitly to int / unsigned int (both 32-bit on
 * m68k, as are size_t and every pointer, so nothing is truncated).
 *
 * C89-clean for the period cross compiler (GCC 2.7.2.3).
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#if defined(ODFS_PLATFORM_AMIX_KERNEL)

#include "cdfs_guard.h"

/* <sys/types.h> FIRST: <sys/cmn_err.h> needs _VOID, <sys/kmem.h> needs size_t. */
#include <sys/types.h>
#include <sys/kmem.h>     /* kmem_alloc/kmem_zalloc/kmem_free, KM_SLEEP */
#include <sys/cmn_err.h>  /* cmn_err, CE_NOTE                           */

struct cdfs_guard_stats cdfs_guard;   /* BSS-zero; adb-readable via /dev/kmem */

/* ---- byte-wise guard access (no alignment assumptions anywhere) ------- */

static unsigned int cdfs_guard_ld32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16)
         | ((unsigned int)p[2] <<  8) | ((unsigned int)p[3]);
}

static void cdfs_guard_st32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)((v >> 24) & 0xFFu);
    p[1] = (unsigned char)((v >> 16) & 0xFFu);
    p[2] = (unsigned char)((v >>  8) & 0xFFu);
    p[3] = (unsigned char)( v        & 0xFFu);
}

/* ---- sticky first-corruption record ---------------------------------- */

/*
 * Captured ONCE (f_set is the latch) so the very first corruption survives both
 * a scrolled console and any later hits, and can be read back with adb even if
 * the box wedges before anything is printed.
 */
static void cdfs_guard_record(unsigned int which, const char *tag,
                              unsigned int size, const void *ptr,
                              const unsigned char *bytes)
{
    unsigned int i;

    if (cdfs_guard.f_set)
        return;

    /* Copy the tag, STOPPING at its NUL: `tag` is a short string literal, so
     * walking a fixed 7 bytes would read past the end of it. */
    for (i = 0; i < 7u; i++) {
        if (tag == 0 || tag[i] == '\0')
            break;
        cdfs_guard.f_tag[i] = tag[i];
    }
    for (; i < 8u; i++)
        cdfs_guard.f_tag[i] = '\0';

    cdfs_guard.f_size = size;
    cdfs_guard.f_ptr  = (unsigned int)ptr;
    for (i = 0; i < 8u; i++)
        cdfs_guard.f_bytes[i] = (bytes != 0) ? bytes[i] : 0;

    cdfs_guard.f_which = which;
    cdfs_guard.f_set   = 1u;   /* written LAST: non-zero => fully populated */
}

/* ---- stamp / verify --------------------------------------------------- */

static void cdfs_guard_stamp(unsigned char *base, unsigned int total,
                             size_t size)
{
    unsigned char *tail;
    unsigned int   i;

    cdfs_guard_st32(base, total);
    cdfs_guard_st32(base + 4, CDFS_GUARD_HEAD_MAGIC);

    tail = base + CDFS_GUARD_HDR + size;
    for (i = 0; i < CDFS_GUARD_TAIL; i++)
        tail[i] = (unsigned char)CDFS_GUARD_FILL;
}

static int cdfs_guard_tail_ok(const unsigned char *tail)
{
    unsigned int i;

    for (i = 0; i < CDFS_GUARD_TAIL; i++) {
        if (tail[i] != (unsigned char)CDFS_GUARD_FILL)
            return 0;
    }
    return 1;
}

/*
 * The shared verify. Returns:
 *    0  clean
 *   -1  head guard bad  (*totalp is NOT trustworthy — caller must not free)
 *    1  tail guard bad  (*totalp IS trustworthy — caller may free)
 * and reports loudly in both bad cases. No sleeping, no allocation, no locks.
 */
static int cdfs_guard_verify(const void *ptr, const char *tag,
                             unsigned int *totalp)
{
    unsigned char *base;
    unsigned char *tail;
    unsigned int   total;
    unsigned int   magic;
    unsigned int   size;

    /*
     * Alignment triage FIRST, before touching base-8 at all.  Every pointer
     * cdfs_guard_alloc can ever have returned is kmem_alloc(total)+8, and on
     * this kernel every kmem result is at least 16-byte aligned (disassembly
     * of os/exp kmem_alloc: small pool classes 16..256 and big pool classes
     * 512..4096 are size-aligned buddies carved from 4096-aligned pool
     * regions — kmem_allocbpool rounds its 8-page chunk UP to a 4096
     * boundary; the >4096 path returns whole pages).  A legitimate payload
     * is therefore ALWAYS == 8 (mod 16).
     *
     * So a free arriving with any other alignment is NOT a scribbled header
     * on a real block: the STORED POINTER ITSELF was overwritten between
     * allocation and free (the observed soak hit: ptr=0x10302000, == 0 mod
     * 2048 — the shape of a kmem chunk base, i.e. a freelist-link value,
     * not of any payload).  Classify it separately: this distinction is the
     * difference between hunting a buffer overrun and hunting a wild
     * pointer-sized write into live cache state.  Do not dereference base:
     * a truly wild value could fault, and the diagnostic must never be the
     * thing that panics.
     */
    if ((((unsigned int)ptr) & 15u) != 8u) {
        cdfs_guard.wild_ptr++;
        cdfs_guard_record(4u, tag, 0u, ptr, (const unsigned char *)0);

        cmn_err(CE_NOTE,
                "cdfs-guard: WILD PTR tag=%s ptr=0x%x n=%d",
                (char *)tag, (unsigned int)ptr,
                (int)cdfs_guard.wild_ptr);
        return -1;
    }

    base  = (unsigned char *)ptr - CDFS_GUARD_HDR;
    total = cdfs_guard_ld32(base);
    magic = cdfs_guard_ld32(base + 4);

    /*
     * BOTH header words must be sane. Checking the magic alone is not enough:
     * an overrun from the block below lands in the length word first, and a
     * garbage length would make the tail check dereference a wild pointer and
     * kmem_free() free a garbage-sized region. A bad length is therefore a
     * corrupt header too — reported, and the caller must NOT free.
     */
    if (magic != CDFS_GUARD_HEAD_MAGIC ||
        total <= (CDFS_GUARD_HDR + CDFS_GUARD_TAIL) ||
        total > CDFS_GUARD_MAX_TOTAL) {
        int is_dfree = (magic == CDFS_GUARD_DEAD_MAGIC);

        if (is_dfree)
            cdfs_guard.dfree++;
        else
            cdfs_guard.head_bad++;
        cdfs_guard_record(is_dfree ? 3u : 1u, tag, 0u, ptr, base);

        cmn_err(CE_NOTE,
                "cdfs-guard: CORRUPT %s tag=%s ptr=0x%x len=0x%x magic=0x%x n=%d",
                is_dfree ? (char *)"DFREE" : (char *)"HEAD",
                (char *)tag,
                (unsigned int)ptr, total, magic,
                (int)(is_dfree ? cdfs_guard.dfree : cdfs_guard.head_bad));
        return -1;
    }

    /* Header intact => the length word is good => the tail is where we think. */
    size = total - CDFS_GUARD_HDR - CDFS_GUARD_TAIL;
    *totalp = total;

    tail = base + CDFS_GUARD_HDR + size;
    if (!cdfs_guard_tail_ok(tail)) {
        cdfs_guard.tail_bad++;
        cdfs_guard_record(2u, tag, size, ptr, tail);

        cmn_err(CE_NOTE,
                "cdfs-guard: CORRUPT TAIL tag=%s size=%d ptr=0x%x b=%x %x %x %x %x %x %x %x n=%d",
                (char *)tag, (int)size, (unsigned int)ptr,
                (unsigned int)tail[0], (unsigned int)tail[1],
                (unsigned int)tail[2], (unsigned int)tail[3],
                (unsigned int)tail[4], (unsigned int)tail[5],
                (unsigned int)tail[6], (unsigned int)tail[7],
                (int)cdfs_guard.tail_bad);
        return 1;
    }

    return 0;
}

/* ---- the allocator ---------------------------------------------------- */

void *cdfs_guard_alloc(size_t size, const char *tag)
{
    unsigned char *base;
    size_t         total;

    (void)tag;   /* the tag is reported on corruption, not on a clean alloc */

    if (size == 0)
        size = 1;
    /* Refuse a size whose guarded total would wrap. */
    if (size > ((size_t)-1) - (size_t)(CDFS_GUARD_HDR + CDFS_GUARD_TAIL)) {
        cdfs_guard.nomem++;
        return 0;
    }
    total = size + (size_t)CDFS_GUARD_HDR + (size_t)CDFS_GUARD_TAIL;

    base = (unsigned char *)kmem_alloc(total, KM_SLEEP);
    if (base == 0) {
        cdfs_guard.nomem++;
        return 0;
    }

    cdfs_guard_stamp(base, (unsigned int)total, size);

    cdfs_guard.allocs++;
    cdfs_guard.live++;
    cdfs_guard.bytes_live += (unsigned int)size;

    return (void *)(base + CDFS_GUARD_HDR);
}

void *cdfs_guard_calloc(size_t count, size_t size, const char *tag)
{
    unsigned char *base;
    size_t         payload;
    size_t         total;

    (void)tag;   /* the tag is reported on corruption, not on a clean alloc */

    if (size != 0 && count > ((size_t)-1) / size) {
        cdfs_guard.nomem++;
        return 0;
    }

    payload = count * size;
    if (payload == 0)
        payload = 1;
    if (payload > ((size_t)-1) - (size_t)(CDFS_GUARD_HDR + CDFS_GUARD_TAIL)) {
        cdfs_guard.nomem++;
        return 0;
    }
    total = payload + (size_t)CDFS_GUARD_HDR + (size_t)CDFS_GUARD_TAIL;

    /* kmem_zalloc clears the whole block; the stamp then writes the guards
     * back over the zeroed header/tail, leaving exactly the payload zeroed. */
    base = (unsigned char *)kmem_zalloc(total, KM_SLEEP);
    if (base == 0) {
        cdfs_guard.nomem++;
        return 0;
    }

    cdfs_guard_stamp(base, (unsigned int)total, payload);

    cdfs_guard.allocs++;
    cdfs_guard.live++;
    cdfs_guard.bytes_live += (unsigned int)payload;

    return (void *)(base + CDFS_GUARD_HDR);
}

int cdfs_guard_check(const void *ptr, const char *tag)
{
    unsigned int total = 0;

    if (ptr == 0)
        return 0;
    return cdfs_guard_verify(ptr, tag, &total);
}

void cdfs_guard_free(void *ptr, const char *tag)
{
    unsigned char *base;
    unsigned int   total = 0;
    unsigned int   size;
    int            v;

    if (ptr == 0)
        return;

    base = (unsigned char *)ptr - CDFS_GUARD_HDR;
    v = cdfs_guard_verify(ptr, tag, &total);

    if (v < 0) {
        /*
         * Head guard gone => the kmem_free length in those same 8 bytes is
         * garbage. kmem_free(base, garbage) would corrupt the allocator far
         * worse than the bug we are chasing, so LEAK the block instead. The
         * box keeps running and keeps reporting; a soak leaks a handful of
         * blocks at worst.
         */
        cdfs_guard.leaked++;
        return;
    }

    size = total - CDFS_GUARD_HDR - CDFS_GUARD_TAIL;

    cdfs_guard.frees++;
    if (cdfs_guard.live != 0u)
        cdfs_guard.live--;
    if (cdfs_guard.bytes_live >= size)
        cdfs_guard.bytes_live -= size;

    /* Poison the head magic so a double free is caught as DFREE rather than
     * silently mis-sized. Best effort: kmem may reuse these bytes for its own
     * freelist link, in which case the second free reports plain HEAD instead. */
    cdfs_guard_st32(base + 4, CDFS_GUARD_DEAD_MAGIC);

    /* v > 0 (tail scribbled) still frees: the header — and therefore the
     * length — is intact, so this kmem_free is the correct one. */
    kmem_free((void *)base, (size_t)total);
}

/* ---- reporting -------------------------------------------------------- */

void cdfs_guard_report(void)
{
    cmn_err(CE_NOTE, "cdfs-guard: SUM alloc=%d free=%d live=%d bytes=%d",
            (int)cdfs_guard.allocs, (int)cdfs_guard.frees,
            (int)cdfs_guard.live,   (int)cdfs_guard.bytes_live);

    cmn_err(CE_NOTE,
            "cdfs-guard: BAD head=%d tail=%d dfree=%d wild=%d leak=%d nomem=%d dover=%d dunterm=%d",
            (int)cdfs_guard.head_bad, (int)cdfs_guard.tail_bad,
            (int)cdfs_guard.dfree,    (int)cdfs_guard.wild_ptr,
            (int)cdfs_guard.leaked,   (int)cdfs_guard.nomem,
            (int)cdfs_guard.dirent_over,
            (int)cdfs_guard.dirent_unterm);
}

void cdfs_guard_dirent_bad(int unterminated, unsigned int scratchsz,
                           unsigned int hdr, unsigned int namelen,
                           unsigned int need, const char *name)
{
    char          shown[17];
    unsigned char raw[8];
    unsigned int  i;

    if (unterminated)
        cdfs_guard.dirent_unterm++;
    if (need > scratchsz)
        cdfs_guard.dirent_over++;

    /*
     * BOUNDED copy of the head of the name — never strlen'd, never printed
     * straight through %s. An unterminated name is exactly one of the things
     * this reports, so treating it as a C string here would be the same bug.
     * Non-printables become '.' so the console line stays parseable.
     */
    for (i = 0; i < 16u && i < namelen; i++) {
        unsigned char c = (unsigned char)name[i];

        shown[i] = (c >= 0x20u && c < 0x7Fu) ? (char)c : '.';
    }
    shown[i] = '\0';     /* i <= 16, and shown[] is 17 bytes */

    for (i = 0; i < 8u; i++)
        raw[i] = (i < namelen) ? (unsigned char)name[i] : 0u;

    cmn_err(CE_NOTE,
            "cdfs-guard: DIRENT %s scratchsz=%d hdr=%d namelen=%d need=%d clamped over=%d unterm=%d",
            unterminated ? (char *)"UNTERM" : (char *)"OVERRUN",
            (int)scratchsz, (int)hdr, (int)namelen, (int)need,
            (int)cdfs_guard.dirent_over, (int)cdfs_guard.dirent_unterm);

    cmn_err(CE_NOTE,
            "cdfs-guard: DIRENT name=\"%s\" b=%x %x %x %x %x %x %x %x",
            shown,
            (unsigned int)raw[0], (unsigned int)raw[1],
            (unsigned int)raw[2], (unsigned int)raw[3],
            (unsigned int)raw[4], (unsigned int)raw[5],
            (unsigned int)raw[6], (unsigned int)raw[7]);
}

#endif /* ODFS_PLATFORM_AMIX_KERNEL */
