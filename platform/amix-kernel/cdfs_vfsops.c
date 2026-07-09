/*
 * cdfs_vfsops.c — the SVR4 vfsops vector for the AMIX in-kernel cdfs.
 *
 * Defines cdfs_vfsops POSITIONALLY, in the EXACT slot order of struct vfsops in
 * <sys/vfs.h> (vfs_mount, vfs_unmount, vfs_root, vfs_statvfs, vfs_sync,
 * vfs_vget, vfs_mountroot, vfs_swapvp), plus cdfsinit — the vsw_init routine a
 * later kerntools step wires into the static vfssw[] table. The op vector is a
 * K&R `int (*)()` array with zero compile-time checking, so the VFS_* call
 * macros in <sys/vfs.h> are the ABI and each function matches them exactly.
 *
 * v1 mount interface (flagged for review): the mount `special` argument is an
 * ASCII "<card><sep><unit>" string — <sep> is any run of non-digits, e.g.
 * "0,2", "0.2", "0:2" — naming the SCSI host-adapter index and target id the
 * in-kernel media backend opens. There is no /dev node yet; a real CD never
 * traverses /dev/dsk on stock AMIX, so addressing by (card,unit) sidesteps that
 * until the device-node story is built out.
 *
 * C89-clean for the period cross compiler (GCC 2.7.2.3).
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#if defined(ODFS_PLATFORM_AMIX_KERNEL)

#include "cdfs.h"
#include "amix_kern_media.h"   /* odfs_kern_media_open_scsi() */

#include <sys/errno.h>
#include <sys/statvfs.h>
#include <sys/cred.h>
#include <sys/kmem.h>          /* kmem_zalloc/kmem_free, KM_SLEEP */
#include <string.h>            /* memset, strncpy */

/*
 * copyinstr() copies a NUL-terminated string in from user space. Declared K&R
 * (no prototype) rather than pulling <sys/systm.h>, whose non-const strcmp/
 * strncpy prototypes clash with the compiler built-ins — the same reason the
 * media backend declares its kernel externs locally.
 */
extern int copyinstr(/* char *uaddr, char *kaddr, size_t max, size_t *done */);

/* fstype index, published by cdfsinit(), stamped into vfs_fstype at mount. */
int cdfs_fstype = 0;

/* TEMP DIAGNOSTIC -- remove: the global diag struct, defined once here and read
 * out of the running kernel via /dev/kmem. BSS-zero; populated in cdfs_mount and
 * the media backend. Declared in cdfs.h. */
struct cdfs_diag_s cdfs_diag;
/* TEMP DIAGNOSTIC -- remove */

/* ---- mount-spec parsing --------------------------------------------- */

/*
 * Parse "<card><sep><unit>" into (card, unit). <sep> is one or more non-digit
 * characters. Returns 0 on success, -1 on malformed input.
 */
static int cdfs_parse_spec(const char *s, unsigned int *card, unsigned int *unit)
{
    unsigned int c = 0;
    unsigned int u = 0;
    int          i = 0;
    int          got;

    got = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        c = c * 10u + (unsigned int)(s[i] - '0');
        i++;
        got = 1;
    }
    if (!got)
        return -1;

    while (s[i] != '\0' && (s[i] < '0' || s[i] > '9'))
        i++;

    got = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        u = u * 10u + (unsigned int)(s[i] - '0');
        i++;
        got = 1;
    }
    if (!got)
        return -1;

    *card = c;
    *unit = u;
    return 0;
}

/* ---- vfs ops (positional, matching <sys/vfs.h> struct vfsops) -------- */

/* 1: vfs_mount(vfsp, mvp, uap, cr) */
static int cdfs_mount(struct vfs *vfsp, struct vnode *mvp,
                      struct mounta *uap, cred_t *cr)
{
    char             specbuf[64];
    size_t           done;
    unsigned int     card;
    unsigned int     unit;
    struct cdfs_vfs *cvfs;
    struct vnode    *rvp;
    odfs_media_t     media;
    odfs_err_t       oe;
    int              e;

    if (uap == 0 || uap->spec == 0)
        return 210;   /* TEMP DIAGNOSTIC -- remove: was EINVAL */

    e = copyinstr(uap->spec, specbuf, sizeof(specbuf), &done);
    if (e != 0)
        return 220;   /* TEMP DIAGNOSTIC -- remove: was e */
    specbuf[sizeof(specbuf) - 1] = '\0';

    if (cdfs_parse_spec(specbuf, &card, &unit) != 0)
        return 211;   /* TEMP DIAGNOSTIC -- remove: was EINVAL */

    /* TEMP DIAGNOSTIC -- remove */
    cdfs_diag.card    = (int)card;
    cdfs_diag.unit    = (int)unit;
    cdfs_diag.reached = 1;
    /* TEMP DIAGNOSTIC -- remove */

    cvfs = (struct cdfs_vfs *)kmem_zalloc(sizeof(*cvfs), KM_SLEEP);
    if (cvfs == 0)
        return ENOMEM;
    cvfs->card = card;
    cvfs->unit = unit;

    oe = odfs_kern_media_open_scsi(card, unit, &media);
    /* TEMP DIAGNOSTIC -- remove */
    cdfs_diag.mopen_oe = (int)oe;
    /* TEMP DIAGNOSTIC -- remove */
    if (oe != ODFS_OK) {
        kmem_free((void *)cvfs, sizeof(*cvfs));
        return 130 + (int)oe;   /* TEMP DIAGNOSTIC -- remove: was cdfs_errno(oe) */
    }

    /* TEMP DIAGNOSTIC -- remove */
    cdfs_diag.scount  = (int)odfs_media_sector_count(&media);
    cdfs_diag.reached = 2;
    /* TEMP DIAGNOSTIC -- remove */

    oe = odfs_mount(&media, (const odfs_mount_opts_t *)0,
                    (odfs_log_state_t *)0, &cvfs->mnt);
    /* TEMP DIAGNOSTIC -- remove */
    cdfs_diag.mount_oe = (int)oe;
    cdfs_diag.reached  = 3;
    cdfs_diag.magic    = 0x0DF50001;   /* set LAST: struct fully populated */
    /* TEMP DIAGNOSTIC -- remove */
    if (oe != ODFS_OK) {
        odfs_media_close(&media);   /* odfs_mount does not close on failure */
        kmem_free((void *)cvfs, sizeof(*cvfs));
        return 100 + (int)oe;   /* TEMP DIAGNOSTIC -- remove: was cdfs_errno(oe) */
    }

    /* TEMP DIAGNOSTIC -- remove: the root node odfs_mount produced, captured
     * BEFORE the cdfs_getnode copy below. Compare with d1_lba/d1_size (cnp->node
     * in the first readdir) to localize a malformed root to odfs_mount vs copy. */
    cdfs_diag.m_rootlba  = (int)cvfs->mnt.root.extent.lba;
    cdfs_diag.m_rootlen  = (int)cvfs->mnt.root.extent.length;
    cdfs_diag.m_rootbk   = (int)cvfs->mnt.root.backend;
    cdfs_diag.m_rootkind = (int)cvfs->mnt.root.kind;
    /* TEMP DIAGNOSTIC -- remove */

    /* VFS_INIT clears vfs_flag, so OR the read-only flags AFTER it, and set
     * vfs_data before cdfs_getnode (which reaches it through VFSTOCDFS). */
    VFS_INIT(vfsp, &cdfs_vfsops, (caddr_t)cvfs);
    vfsp->vfs_flag |= VFS_RDONLY | VFS_NOTRUNC;
    vfsp->vfs_bsize = 2048;
    vfsp->vfs_fstype = cdfs_fstype;

    e = cdfs_getnode(vfsp, &cvfs->mnt.root,
                     (const odfs_node_t *)0, (const char *)0, &rvp);
    if (e != 0) {
        odfs_unmount(&cvfs->mnt);
        kmem_free((void *)cvfs, sizeof(*cvfs));
        return e;
    }
    rvp->v_flag |= VROOT;
    cvfs->rootvp = rvp;   /* the mount owns this reference */

    return 0;
}

/* 2: vfs_unmount(vfsp, cr) */
static int cdfs_unmount(struct vfs *vfsp, cred_t *cr)
{
    struct cdfs_vfs *cvfs = VFSTOCDFS(vfsp);
    struct cdfsnode *cnp;
    unsigned int     h;

    /* Busy check: the only live vnode may be the root, held once by the mount.
     * Any other node in the hash, or an extra root hold, means EBUSY. */
    for (h = 0; h < CDFS_NHASH; h++) {
        for (cnp = cvfs->hash[h]; cnp != 0; cnp = cnp->hash_next) {
            struct vnode *vp = CDFSTOV(cnp);
            if (vp == cvfs->rootvp) {
                if (vp->v_count > 1)
                    return EBUSY;
            } else {
                return EBUSY;
            }
        }
    }

    /* Drop the mount's root hold; v_count hits 0, vn_rele calls vop_inactive,
     * which unlinks + frees the root node. Do this BEFORE freeing cvfs, since
     * inactive reaches cvfs through vp->v_vfsp. */
    if (cvfs->rootvp != 0)
        VN_RELE(cvfs->rootvp);

    odfs_unmount(&cvfs->mnt);

    if (cvfs->devvp != 0)
        VN_RELE(cvfs->devvp);

    kmem_free((void *)cvfs, sizeof(*cvfs));
    return 0;
}

/* 3: vfs_root(vfsp, vpp) */
static int cdfs_root(struct vfs *vfsp, struct vnode **vpp)
{
    struct cdfs_vfs *cvfs = VFSTOCDFS(vfsp);
    struct vnode    *vp = cvfs->rootvp;

    if (vp == 0)
        return EIO;
    VN_HOLD(vp);
    *vpp = vp;
    return 0;
}

/* 4: vfs_statvfs(vfsp, sp) */
static int cdfs_statvfs(struct vfs *vfsp, struct statvfs *sp)
{
    struct cdfs_vfs *cvfs = VFSTOCDFS(vfsp);
    odfs_mount_t    *mnt = &cvfs->mnt;

    (void)memset((void *)sp, 0, sizeof(*sp));

    sp->f_bsize  = 2048;
    sp->f_frsize = 2048;
    sp->f_blocks = (u_long)mnt->total_blocks;   /* 2048-byte logical blocks */
    sp->f_bfree  = 0;
    sp->f_bavail = 0;

    /* No true inode count on an optical volume; total_blocks is a safe upper
     * bound, and the medium is full (no free nodes). */
    sp->f_files  = (u_long)mnt->total_blocks;
    sp->f_ffree  = 0;
    sp->f_favail = 0;

    sp->f_fsid = vfsp->vfs_dev;
    (void)strncpy(sp->f_basetype, "cdfs", sizeof(sp->f_basetype) - 1);
    sp->f_flag = ST_RDONLY | ST_NOTRUNC;
    sp->f_namemax = 255;
    (void)strncpy(sp->f_fstr, mnt->volume_name, sizeof(sp->f_fstr) - 1);

    return 0;
}

/* 5: vfs_sync(vfsp, flag, cr) — read-only: nothing to flush. */
static int cdfs_sync(struct vfs *vfsp, int flag, cred_t *cr)
{
    return 0;
}

/* 6: vfs_vget(vfsp, vpp, fidp) — resolve a fid whose payload is extent.lba.
 * v1 resolves only currently-cached nodes; stateless handle synthesis for NFS
 * export is out of scope. */
static int cdfs_vget(struct vfs *vfsp, struct vnode **vpp, struct fid *fidp)
{
    struct cdfs_vfs *cvfs = VFSTOCDFS(vfsp);
    struct cdfs_fid *cfid = (struct cdfs_fid *)fidp;
    struct cdfsnode *cnp;
    uint32_t         lba = cfid->cfid_lba;
    unsigned int     h = (unsigned int)(lba % (uint32_t)CDFS_NHASH);

    for (cnp = cvfs->hash[h]; cnp != 0; cnp = cnp->hash_next) {
        if (cnp->node.extent.lba == lba) {
            struct vnode *vp = CDFSTOV(cnp);
            VN_HOLD(vp);
            *vpp = vp;
            return 0;
        }
    }

    *vpp = 0;
    return EINVAL;
}

/* 7: vfs_mountroot(vfsp, why) — cdfs can never be the root filesystem. */
static int cdfs_mountroot(struct vfs *vfsp, enum whymountroot why)
{
    return EROFS;
}

/* 8: vfs_swapvp(vfsp, vpp, nm) — no swap on a read-only optical volume. */
static int cdfs_swapvp(struct vfs *vfsp, struct vnode **vpp, char *nm)
{
    return EINVAL;
}

/* ---- vfssw init routine --------------------------------------------- */

/*
 * cdfsinit() — the vsw_init routine, called once at boot as the kernel walks
 * vfssw[] initializing each filesystem type. ROBUST to the kernel's vsw_init
 * calling convention: rather than trusting a passed (vswp, fstype) — the AMIX
 * boot loop invokes (*vsw_init)() with no/other arguments, so a declared vswp
 * would be garbage and writing through it corrupts an unrelated slot, leaving
 * the real "cdfs" row's vsw_vfsops NULL (mount(2) then EINVALs before dispatch)
 * — it SELF-LOCATES its own row in the kernel's global vfssw[] table by name,
 * installs the op vector there, and records the row index as the fstype.
 *
 * Declared with empty parens (C89 "unspecified arguments") so it binds cleanly
 * however the kernel invokes it; any arguments the kernel pushes are ignored.
 *
 * The vfssw[] row it locates,
 *   { "cdfs", cdfsinit, 0, 0 }
 * is injected by the kerntools step (out of scope). vfssw[]/nfstype come from
 * <sys/vfs.h>; strcmp is the kernel's own copy (declared in <string.h>). All
 * three stay undefined externs in libcdfskern.o, resolved at the kernel link.
 */
int cdfsinit()
{
    int i;

    for (i = 0; i < nfstype; i++) {
        if (vfssw[i].vsw_name != 0 &&
            strcmp(vfssw[i].vsw_name, "cdfs") == 0) {
            vfssw[i].vsw_vfsops = &cdfs_vfsops;
            cdfs_fstype = i;
            return 0;
        }
    }
    return 0;
}

/* ---- the vector: positional, EXACT <sys/vfs.h> struct vfsops order --- */

struct vfsops cdfs_vfsops = {
    cdfs_mount,      /* 1 vfs_mount     */
    cdfs_unmount,    /* 2 vfs_unmount   */
    cdfs_root,       /* 3 vfs_root      */
    cdfs_statvfs,    /* 4 vfs_statvfs   */
    cdfs_sync,       /* 5 vfs_sync      */
    cdfs_vget,       /* 6 vfs_vget      */
    cdfs_mountroot,  /* 7 vfs_mountroot */
    cdfs_swapvp      /* 8 vfs_swapvp    */
    /* vfs_filler[8] left zero-initialized */
};

#endif /* ODFS_PLATFORM_AMIX_KERNEL */
