/*
 * cdfs.h — AMIX (SVR4/m68k) in-kernel read-only optical filesystem: shared
 * declarations for the vfsops/vnodeops/node-cache glue that turns the portable
 * ODFS core into a mountable Amix filesystem.
 *
 * The portable core (core/ + backends/) hands back VALUE-type odfs_node_t with
 * no lifecycle of their own. This layer owns vnode identity: a per-mount hash
 * of live cdfsnode objects keyed by the disc-authoritative inode analog
 * (extent.lba). The design mirrors the SVR4 s5 idiom — the vnode is EMBEDDED in
 * the node object (ITOV/VTOI), and v_data back-points to the containing node.
 *
 * Everything here is gated on ODFS_PLATFORM_AMIX_KERNEL and written C89-clean
 * for the period cross compiler (GCC 2.7.2.3).
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ODFS_AMIX_KERNEL_CDFS_H
#define ODFS_AMIX_KERNEL_CDFS_H

#if defined(ODFS_PLATFORM_AMIX_KERNEL)

/*
 * SVR4 kernel types + object model. sys/types.h first so the fixed-width
 * scalar typedefs (u_short/caddr_t/dev_t/ino_t/off_t) are in place before the
 * vfs/vnode headers that use them. sys/vnode.h drags in sys/time.h, which is
 * where timestruc_t (used by cdfs_time_to_timestruc) comes from.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/vfs.h>
#include <sys/vnode.h>

/* Portable ODFS core: the mount handle + value-node model this layer wraps. */
#include "odfs/api.h"

/*
 * Vnode-cache hash width. A small prime; the key is odfs_node_t.extent.lba —
 * the on-disc block address that stands in for an inode number (see the
 * identity predicate in node.h). Collisions are chained and disambiguated with
 * odfs_node_matches_identity().
 */
#define CDFS_NHASH 61

/*
 * Per-symlink side data, captured at lookup time. odfs_readlink() resolves a
 * link from its PARENT directory node PLUS the entry name (not from the link
 * node itself — see api.h), so a symlink cdfsnode stashes both. Allocated only
 * for ODFS_NODE_SYMLINK nodes; NULL otherwise.
 */
struct cdfs_symlink {
    odfs_node_t parent;                 /* containing directory node */
    char        name[ODFS_NAME_MAX];    /* entry name within parent */
};

/*
 * In-core node. The struct vnode is embedded (s5 ITOV/VTOI idiom); v_data
 * back-points here so VTOCDFS(vp) is a plain cast. `node` is a by-value copy of
 * the disc node the core produced.
 */
struct cdfsnode {
    struct vnode         vnode;         /* MUST allow &cnp->vnode == CDFSTOV */
    odfs_node_t          node;          /* by-value copy of the disc node */
    struct cdfsnode     *hash_next;     /* per-bucket chain */
    struct cdfs_symlink *slink;         /* symlink side data, or NULL */
};

/*
 * Per-mount private data, hung off vfs_data. Holds the ODFS mount handle, the
 * cached root vnode, the live-node hash, and the (card,unit) the media backend
 * was opened with. devvp is reserved for a future specvp-backed device vnode;
 * the v1 SCSI backend addresses the drive by (card,unit), not a dev node.
 */
struct cdfs_vfs {
    odfs_mount_t     mnt;               /* portable ODFS mount handle */
    struct vnode    *rootvp;            /* cached VROOT vnode (mount holds it) */
    struct vnode    *devvp;             /* optional device vnode (v1: NULL) */
    struct cdfsnode *hash[CDFS_NHASH];  /* live-node hash buckets */
    unsigned int     card;              /* SCSI host-adapter index */
    unsigned int     unit;             /* SCSI target id */
};

/*
 * fid overlay (mirrors s5 struct ufid / bfs_fid_overlay). The handle payload is
 * the node's extent.lba, symmetric between vop_fid (emit) and vfs_vget (decode).
 */
struct cdfs_fid {
    u_short  cfid_len;                  /* length of payload after this field */
    uint32_t cfid_lba;                  /* node identity = extent.lba */
};

/* vnode <-> cdfsnode / vfs -> cdfs_vfs accessors (s5 VTOI/ITOV shape). */
#define VTOCDFS(vp)    ((struct cdfsnode *)(vp)->v_data)
#define CDFSTOV(cnp)   (&(cnp)->vnode)
#define VFSTOCDFS(vfsp) ((struct cdfs_vfs *)(vfsp)->vfs_data)

/* The two op vectors, defined positionally in cdfs_vfsops.c / cdfs_vnodeops.c. */
extern struct vfsops    cdfs_vfsops;
extern struct vnodeops  cdfs_vnodeops;

/*
 * fstype index, stashed by cdfsinit() (the vsw_init routine) and stamped into
 * vfs_fstype at mount. Defined in cdfs_vfsops.c.
 */
extern int cdfs_fstype;

/* ---- node cache (cdfs_node.c) ---------------------------------------- */

/*
 * Find-or-create the vnode for a disc node.
 *   parent/name — for a SYMLINK child, the containing dir node + entry name to
 *                 stash for readlink; pass NULL/NULL for non-symlinks and root.
 * On success *vpp holds ONE reference the caller owns (VN_HOLD on a cache hit,
 * the VN_INIT count on a fresh node). Returns an errno (0 on success).
 */
int  cdfs_getnode(struct vfs *vfsp, const odfs_node_t *n,
                  const odfs_node_t *parent, const char *name,
                  struct vnode **vpp);

/* Unlink from the hash and free (called from vop_inactive when v_count hits 0). */
void cdfs_node_inactive(struct vnode *vp);

/* odfs_node_kind_t -> SVR4 vnode type. */
vtype_t cdfs_vtype(odfs_node_kind_t kind);

/* Broken-down odfs timestamp -> SVR4 timestruc_t (seconds since the epoch). */
void cdfs_time_to_timestruc(const odfs_timestamp_t *ts, timestruc_t *out);

/* Map an ODFS error to an errno for the VFS/VOP return path. */
int  cdfs_errno(odfs_err_t err);

/* ---- TEMP diagnostic (throwaway) ------------------------------------ */

/* TEMP DIAGNOSTIC -- remove */
/*
 * Global diagnostic struct for the cdfs_mount EINVAL bench bug, read straight
 * out of the running kernel via /dev/kmem (adb resolves cdfs_diag's runtime
 * address). No console/cmn_err dependency: cdfs_mount and the media backend
 * populate it in place as each mount stage happens; the reader dumps the bytes.
 *
 * Layout is fixed and portable to a big-endian /dev/kmem parse: on m68k every
 * `int` is 4 bytes big-endian with natural (4-byte) alignment, so the struct is
 * ten contiguous 4-byte ints followed by a raw 16-byte array (no padding).
 * Zero-initialized in BSS; `magic` is written LAST so a non-zero magic proves
 * the struct is fully populated.
 */
struct cdfs_diag_s {
    int           magic;        /* set LAST to 0x0DF50001 when fully populated */
    int           reached;      /* 1=after parse, 2=after media-open OK, 3=after odfs_mount */
    int           card;
    int           unit;
    int           mopen_oe;     /* odfs_kern_media_open_scsi return (odfs_err_t as int) */
    int           scount;       /* odfs_media_sector_count after a successful open */
    int           mount_oe;     /* odfs_mount return (odfs_err_t as int) */
    int           r1_lba;       /* first amix_kern_read_sectors call: lba */
    int           r1_count;     /* ... count */
    int           r1_err;       /* ... the submit err (odfs_err_t as int) */
    unsigned char r1_bytes[16]; /* first 16 bytes of the buffer after the first read */
    /*
     * First post-mount directory read (readdir OR lookup, whichever runs
     * first). Appended AFTER r1_bytes so the mount-time layout above is
     * unchanged. d1_set is written LAST (like magic): non-zero means the
     * eight fields below are fully populated. Same big-endian /dev/kmem parse
     * rule — 4-byte ints, then a raw 16-byte array, no padding.
     */
    int           d1_set;       /* 0 until the first dir op fills the block */
    int           d1_op;        /* 1 = cdfs_readdir, 2 = cdfs_lookup */
    int           d1_lba;       /* dir->extent.lba the op enumerated (cnp->node) */
    int           d1_size;      /* dir->extent.length (dir byte size, cnp->node) */
    int           d1_ret;       /* odfs_readdir/odfs_lookup return (as int) */
    int           d1_cbcount;   /* readdir: child-callback fires; lookup: found */
    int           d1_rderr;     /* direct media read of d1_lba: err (as int).
                                 * -2 = SKIPPED (d1_lba==0 or >= scount): a bogus
                                 * LBA read sleeps forever (no timeout) — a diag
                                 * must never hang the box. */
    unsigned char d1_bytes[16]; /* first 16 bytes the media backend returns for d1_lba */
    /*
     * The root node odfs_mount produced, recorded in cdfs_mount the instant
     * odfs_mount returns OK — BEFORE any cdfs_getnode `cnp->node = *n` copy or
     * vnode round-trip. Compare against d1_lba/d1_size (from cnp->node in the
     * first readdir): equal ⇒ the copy is fine and odfs_mount itself yielded a
     * bad/garbage root; differ ⇒ the copy/round-trip drops it.
     */
    int           m_rootlba;    /* cvfs->mnt.root.extent.lba */
    int           m_rootlen;    /* cvfs->mnt.root.extent.length */
    int           m_rootbk;     /* cvfs->mnt.root.backend (5=ROCK_RIDGE) */
    int           m_rootkind;   /* cvfs->mnt.root.kind (1=DIR) */
    /*
     * The CACHED read (through the odfs block cache — what odfs_readdir/
     * odfs_lookup actually walk) of the same dir sector, captured alongside the
     * cache-bypassing d1_bytes above. Equal to d1_bytes ⇒ the cache is fine and
     * the enumeration bug is in the walk (e.g. a garbage rr_info_t tail);
     * differ ⇒ the block cache returns divergent data for that LBA.
     */
    int           d1c_err;      /* odfs_cache_read of d1_lba: err (-2 = skipped) */
    unsigned char d1c_bytes[16];/* first 16 bytes the CACHED read returns for d1_lba */
    int           d1c_ptr;      /* the buffer POINTER odfs_cache_read returned, as
                                 * int: a kernel-text-range value ⇒ a bad cache
                                 * entry pointer/index; a heap value with garbage
                                 * d1c_bytes ⇒ the buffer contents were corrupted. */
};
extern struct cdfs_diag_s cdfs_diag;
/* TEMP DIAGNOSTIC -- remove */

#endif /* ODFS_PLATFORM_AMIX_KERNEL */

#endif /* ODFS_AMIX_KERNEL_CDFS_H */
