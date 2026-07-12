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

#endif /* ODFS_PLATFORM_AMIX_KERNEL */

#endif /* ODFS_AMIX_KERNEL_CDFS_H */
