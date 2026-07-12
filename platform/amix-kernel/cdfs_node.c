/*
 * cdfs_node.c — vnode object cache for the AMIX in-kernel cdfs.
 *
 * The portable ODFS core returns VALUE-type odfs_node_t with no lifecycle; cdfs
 * owns vnode identity. Each mount keeps a hash of live cdfsnode objects keyed by
 * extent.lba (the disc-authoritative inode analog). cdfs_getnode() finds an
 * existing live vnode and VN_HOLDs it, or allocates and initializes a fresh one;
 * cdfs_node_inactive() (driven from vop_inactive when v_count reaches 0) unlinks
 * and frees it. This mirrors the SVR4 s5 in-core inode idiom, minus the
 * reuse-freelist: a read-only optical volume never dirties a node.
 *
 * v1 is single-threaded with respect to this hash (no bucket locking); the
 * on-box bench is a later step and SMP hardening with it.
 *
 * C89-clean for the period cross compiler (GCC 2.7.2.3).
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#if defined(ODFS_PLATFORM_AMIX_KERNEL)

#include "cdfs.h"
#include "cdfs_guard.h"  /* TEMP DIAG: guarded alloc/free */

#include <sys/kmem.h>    /* kmem_alloc/kmem_zalloc/kmem_free, KM_SLEEP */
#include <sys/errno.h>   /* ENOMEM, EIO, ... */
#include <string.h>      /* strncpy (kernel), + the compiler's aggregate ops */

/* Bucket for a node identity. extent.lba is the on-disc block address. */
static unsigned int cdfs_hash(uint32_t lba)
{
    return (unsigned int)(lba % (uint32_t)CDFS_NHASH);
}

vtype_t cdfs_vtype(odfs_node_kind_t kind)
{
    switch (kind) {
    case ODFS_NODE_DIR:
        return VDIR;
    case ODFS_NODE_SYMLINK:
        return VLNK;
    case ODFS_NODE_FILE:
        return VREG;
    case ODFS_NODE_VIRTUAL:
    default:
        /* Virtual nodes (e.g. a CDDA track) are file-like. The in-kernel media
         * backend exposes no audio, so CDDA is not expected to activate here. */
        return VREG;
    }
}

/* Days from 1970-01-01 to the given civil date (Howard Hinnant's algorithm,
 * valid for the proleptic Gregorian calendar). */
static long cdfs_days_from_civil(long y, unsigned int m, unsigned int d)
{
    long         era;
    unsigned int yoe;
    unsigned int doy;
    unsigned int doe;

    y -= (m <= 2);
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned int)(y - era * 400);
    doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097L + (long)doe - 719468L;
}

void cdfs_time_to_timestruc(const odfs_timestamp_t *ts, timestruc_t *out)
{
    long days;
    long secs;

    out->tv_nsec = 0;

    if (ts == 0 || ts->year <= 0 || ts->month < 1 || ts->month > 12) {
        out->tv_sec = 0;
        return;
    }

    days = cdfs_days_from_civil((long)ts->year,
                                (unsigned int)ts->month,
                                (unsigned int)ts->day);
    secs = days * 86400L
         + (long)ts->hour * 3600L
         + (long)ts->minute * 60L
         + (long)ts->second;

    /* Broken-down time is local; tz_offset (minutes east of UTC) maps it back
     * to the epoch. INT16_MIN marks an unknown zone — treat as UTC. */
    if (ts->tz_offset != (int16_t)0x8000)
        secs -= (long)ts->tz_offset * 60L;

    out->tv_sec = secs;
}

int cdfs_errno(odfs_err_t err)
{
    switch (err) {
    case ODFS_OK:
    case ODFS_ERR_EOF:
        return 0;
    case ODFS_ERR_NOMEM:
        return ENOMEM;
    case ODFS_ERR_INVAL:
    case ODFS_ERR_RANGE:
    case ODFS_ERR_OVERFLOW:
        return EINVAL;
    case ODFS_ERR_NOT_FOUND:
        return ENOENT;
    case ODFS_ERR_NOT_DIR:
        return ENOTDIR;
    case ODFS_ERR_IS_DIR:
        return EISDIR;
    case ODFS_ERR_READ_ONLY:
        return EROFS;
    case ODFS_ERR_NAME_TOO_LONG:
        return ENAMETOOLONG;
    default:
        /* IO / BAD_SECTOR / NO_MEDIA / MEDIA_CHANGED / BAD_FORMAT / etc. */
        return EIO;
    }
}

int cdfs_getnode(struct vfs *vfsp, const odfs_node_t *n,
                 const odfs_node_t *parent, const char *name,
                 struct vnode **vpp)
{
    struct cdfs_vfs *cvfs = VFSTOCDFS(vfsp);
    struct cdfsnode *cnp;
    struct vnode    *vp;
    unsigned int     h;

    h = cdfs_hash(n->extent.lba);

    /* Cache hit: reuse the live vnode. */
    for (cnp = cvfs->hash[h]; cnp != 0; cnp = cnp->hash_next) {
        if (odfs_node_matches_identity(&cnp->node, n)) {
            vp = CDFSTOV(cnp);
            VN_HOLD(vp);
            *vpp = vp;
            return 0;
        }
    }

    /* Miss: allocate + initialize a fresh node.
     * TEMP DIAG: guarded (head magic + 0xC5 tail canary), tag "node". */
    cnp = (struct cdfsnode *)cdfs_guard_calloc(1, sizeof(*cnp), "node");
    if (cnp == 0)
        return ENOMEM;

    cnp->node = *n;
    cnp->slink = 0;

    vp = CDFSTOV(cnp);
    VN_INIT(vp, vfsp, cdfs_vtype(n->kind), (dev_t)0);
    vp->v_op = &cdfs_vnodeops;
    vp->v_data = (caddr_t)cnp;
    vp->v_flag |= VNOMAP;

    /* A symlink needs its parent + entry name for odfs_readlink().
     * TEMP DIAG: guarded, tag "slink". */
    if (n->kind == ODFS_NODE_SYMLINK && parent != 0) {
        cnp->slink = (struct cdfs_symlink *)
            cdfs_guard_calloc(1, sizeof(struct cdfs_symlink), "slink");
        if (cnp->slink != 0) {
            cnp->slink->parent = *parent;
            if (name != 0) {
                (void)strncpy(cnp->slink->name, name,
                              sizeof(cnp->slink->name) - 1);
                cnp->slink->name[sizeof(cnp->slink->name) - 1] = '\0';
            }
        }
    }

    cnp->hash_next = cvfs->hash[h];
    cvfs->hash[h] = cnp;

    *vpp = vp;
    return 0;
}

void cdfs_node_inactive(struct vnode *vp)
{
    struct cdfs_vfs  *cvfs = VFSTOCDFS(vp->v_vfsp);
    struct cdfsnode  *cnp = VTOCDFS(vp);
    struct cdfsnode **pp;
    unsigned int      h;

    h = cdfs_hash(cnp->node.extent.lba);

    for (pp = &cvfs->hash[h]; *pp != 0; pp = &(*pp)->hash_next) {
        if (*pp == cnp) {
            *pp = cnp->hash_next;
            break;
        }
    }

    if (cnp->slink != 0)
        cdfs_guard_free((void *)cnp->slink, "slink");
    cdfs_guard_free((void *)cnp, "node");
}

#endif /* ODFS_PLATFORM_AMIX_KERNEL */
