/*
 * cdfs_guard.h — TEMP DIAG (THROWAWAY — revert with the whole commit).
 *
 * Guard-band ("canary") allocator for the AMIX in-kernel cdfs, to catch the
 * wild heap write that panics the box (pid=mount, pc=fault, addr=0x4AFC0000,
 * vector 2) after enough mount/readdir/read churn. The VFS-contract fixes ruled
 * the vfs struct out; the surviving theory is that some cdfs allocation is being
 * overrun and scribbling on a neighbouring kernel heap block.
 *
 * Every heap block the CD path touches is bracketed:
 *
 *     base -> [ 4B total len | 4B head magic ][ payload ... ][ 8 x 0xC5 ]
 *              \___ CDFS_GUARD_HDR (8) ____/                 \_ TAIL (8) _/
 *                                          ^
 *                                          the pointer handed to the caller
 *
 * The 8-byte header is NOT new: the pre-existing odfs_malloc arm already
 * over-allocated exactly 8 bytes to stash a size_t length for kmem_free (SVR4
 * kmem_free needs the original length, which odfs_free(ptr) is not given). On
 * m68k size_t is 4 bytes, so 4 of those 8 header bytes were already SLACK — the
 * head magic goes there for free. Only the 8-byte tail is new footprint, so the
 * heap layout is perturbed as little as a canary scheme can manage (the block
 * cache's 128 x 2048-byte data buffers grow by 8 bytes each = 1 KiB across a
 * ~634 KiB mount).
 *
 * Both guards are verified on free. On a corrupted guard we cmn_err LOUDLY and
 * KEEP GOING — never panic — so a long soak keeps reporting instead of dying on
 * the first hit. Counters + a sticky first-corruption record live in the global
 * `cdfs_guard`, which is also readable straight out of the running kernel with
 * adb via /dev/kmem (the same technique the earlier cdfs_diag struct used), so
 * evidence survives a scrolled console.
 *
 * SAFETY — why a bad HEAD magic LEAKS the block instead of freeing it: the
 * kmem_free length lives in the same 8 bytes as the head magic. If the magic is
 * gone, the length word is untrustworthy too, and kmem_free(base, garbage) hands
 * the allocator a wrong-sized region — a far bigger wild write than the one we
 * are hunting. So a head-guard failure is reported and the block is deliberately
 * leaked. A TAIL failure is different: the header is intact, so the length IS
 * good and the block is freed normally (only the payload tail was scribbled).
 *
 * All guard reads/writes are BYTE-WISE: the tail sits at base + 8 + size, which
 * is not 4-byte aligned for an odd payload size, and a diagnostic must never be
 * the thing that faults. Nothing here sleeps, allocates or locks on the check
 * path, so it is safe at interrupt/spl context.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ODFS_AMIX_KERNEL_CDFS_GUARD_H
#define ODFS_AMIX_KERNEL_CDFS_GUARD_H

#if defined(ODFS_PLATFORM_AMIX_KERNEL)

#include <sys/types.h>   /* size_t */

/* Header: [0..3] total kmem length, [4..7] head magic. Tail: 8 x 0xC5. */
#define CDFS_GUARD_HDR   8u
#define CDFS_GUARD_TAIL  8u
#define CDFS_GUARD_FILL  0xC5u          /* the tail poison byte           */

#define CDFS_GUARD_HEAD_MAGIC 0x0DF5C0DEu  /* live block                  */
#define CDFS_GUARD_DEAD_MAGIC 0x0DF5DEADu  /* stamped on free -> catches a
                                            * double free (best effort: kmem
                                            * may reuse the header for its own
                                            * freelist link) */

/*
 * Sanity ceiling on the stored length. The magic and the length live in the
 * SAME 8 bytes, so a small overrun from the block BELOW ours can land in the
 * length word [0..3] while leaving the magic [4..7] intact. Trusting the length
 * then would send the tail check off to a wild pointer (a faulting READ) and
 * hand kmem_free a garbage size. So the length must also look sane: anything
 * outside (HDR+TAIL, CDFS_GUARD_MAX_TOTAL] is treated as a corrupt header —
 * reported, and the block leaked rather than freed.
 *
 * The largest single block the CD path allocates is the ~96 KiB meta-cache pool;
 * 8 MiB is far above every real allocation yet far below a typical garbage word,
 * so it rejects nearly all garbage while never rejecting a live block.
 */
#define CDFS_GUARD_MAX_TOTAL  (8u * 1024u * 1024u)

/*
 * Counters + sticky first-corruption record. Global (not per-TU statics) so a
 * soak can be summarized, and BSS-zero so an adb dump of a never-touched kernel
 * reads as all zeros.
 */
struct cdfs_guard_stats {
    unsigned int allocs;         /* guarded allocations that succeeded        */
    unsigned int frees;          /* guarded frees that reached kmem_free      */
    unsigned int live;           /* allocs - frees - leaked                   */
    unsigned int bytes_live;     /* payload bytes currently live              */
    unsigned int head_bad;       /* head-magic failures (block leaked)        */
    unsigned int tail_bad;       /* tail-canary failures (block still freed)  */
    unsigned int dfree;          /* head magic was DEAD -> double free        */
    unsigned int wild_ptr;       /* freed ptr was NEVER a payload (see below) */
    unsigned int leaked;         /* blocks deliberately not freed             */
    unsigned int nomem;          /* kmem_alloc returned 0                     */
    unsigned int dirent_over;    /* cdfs_emit_dirent would have overrun       */
    unsigned int dirent_unterm;  /* entry name with no NUL in ODFS_NAME_MAX   */

    /* First corruption seen, captured once and never overwritten. */
    unsigned int  f_set;         /* 0 until the first corruption is recorded  */
    unsigned int  f_which;       /* 1 = HEAD, 2 = TAIL, 3 = DOUBLE FREE,
                                  * 4 = WILD PTR                              */
    char          f_tag[8];      /* the allocation-site tag                   */
    unsigned int  f_size;        /* payload size (0 when the header is bad)   */
    unsigned int  f_ptr;         /* the payload pointer                       */
    unsigned char f_bytes[8];    /* the clobbering bytes                      */
};

extern struct cdfs_guard_stats cdfs_guard;

/*
 * Guarded allocation. `tag` is a short literal naming the site ("odfs",
 * "scratch", "rdbuf", "namebuf", "cvfs", "node", "slink") and is reported
 * verbatim on a corruption. cdfs_guard_calloc zeroes the payload.
 */
void *cdfs_guard_alloc(size_t size, const char *tag);
void *cdfs_guard_calloc(size_t count, size_t size, const char *tag);

/* Verify both guards, then free (or leak — see the header comment). */
void  cdfs_guard_free(void *ptr, const char *tag);

/* Verify both guards WITHOUT freeing. Returns 0 when clean. */
int   cdfs_guard_check(const void *ptr, const char *tag);

/* Dump the counters to the console (called at unmount: one summary per cycle). */
void  cdfs_guard_report(void);

/*
 * Report a cdfs_emit_dirent name that does not fit the dirent scratch buffer,
 * or is not NUL-terminated inside ODFS_NAME_MAX. Lives here so every cmn_err in
 * the diagnostic sits in one TU. `name` is printed BOUNDED — never strlen'd.
 */
void  cdfs_guard_dirent_bad(int unterminated, unsigned int scratchsz,
                            unsigned int hdr, unsigned int namelen,
                            unsigned int need, const char *name);

#endif /* ODFS_PLATFORM_AMIX_KERNEL */

#endif /* ODFS_AMIX_KERNEL_CDFS_GUARD_H */
