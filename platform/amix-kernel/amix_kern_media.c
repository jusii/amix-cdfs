/*
 * amix_kern_media.c — AMIX (SVR4/m68k) IN-KERNEL SCSI optical media backend.
 *
 * Serves 2048-byte CD logical blocks off a REAL optical drive from inside the
 * kernel, by issuing SCSI CDBs straight down the SVR4 SCSI path. It is the
 * in-kernel twin of platform/amix/amix_scsi_media.c: the CDB-building logic
 * (READ(10)/0x28, READ CAPACITY(10)/0x25, INQUIRY/0x12) is identical, but the
 * TRANSPORT is swapped. Userspace fills a `struct sdcom` and hands it to the
 * kernel via ioctl('GSIO', /dev/scsi); here we fill the same `struct sdcom`
 * and call sdqueue() directly, then sleep until the driver's completion
 * interrupt fires. Because the kernel DMAs to its own buffer, the 1024-byte
 * gsioctl bounce-buffer clamp that blocks the userspace path does NOT apply.
 *
 * The wait mirrors scsi.c:gsioctl (sdqueue(); sdspl(); until(done) sleep();
 * spl0()) but WITHOUT gsioctl's copyin/static-iobuf/copyout: each request
 * carries its own `done` flag and sleep channel, never gsioctl's file-static
 * ones, so concurrent mounts do not collide. The completion handler recovers
 * its request from the sdcom pointer the driver passes to (*cp->intr)(cp),
 * relying on the sdcom being the first member of the request struct.
 *
 * `struct sdcom` + SDCARDS/SDUNITS come verbatim from the cross sysroot's
 * kernel headers (usr/sys/amiga/alien/{rico.h,sd.h}); the ABI is never hand-
 * transcribed. Those headers are included LAST because rico.h defines
 * aggressive short-name macros (bool/uint/uchar/and/or/not/...).
 *
 * PROVIDER-FOUNDATION SCOPE: this file must COMPILE and LINK as part of the
 * relocatable kernel object. sdqueue()/vtop()/sleep()/wakeup() stay undefined
 * externs resolved at the final kernel link; spl0/spl2 (sdspl) inline from
 * <sys/inline.h>, so they emit no external symbol at all. The transport is not
 * exercised until the on-box bench.
 *
 * Written in C89 for the period AMIX cross compiler (GCC 2.7.2.3):
 * declarations at block scope, no C99 for-init, no mixed declarations.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "odfs/media.h"
#include "odfs/alloc.h"
#include "amix_kern_media.h"
#include "cdfs.h"         /* TEMP DIAGNOSTIC -- remove: cdfs_diag global. Before
                          * rico.h so its short-name macros can't touch the
                          * vfs/vnode/api headers cdfs.h drags in. */

/*
 * SVR4 kernel services. Included before the alien short-name macros so the
 * typedef'd uint/ushort/ulong in <sys/types.h> land BEFORE rico.h redefines
 * them as macros (matching amix_scsi_media.c's header ordering exactly).
 * <sys/systm.h> is deliberately NOT pulled in: it would drag its non-const
 * strcmp/strncpy prototypes (which clash with the compiler built-ins), and
 * all this backend needs from it is sleep()/wakeup(), declared K&R below —
 * exactly as the reference scsi.c:gsioctl relies on them.
 */
#include <sys/types.h>   /* caddr_t, paddr_t, uint (typedef) */
#include <sys/param.h>   /* PRIBIO                           */
#include <sys/inline.h>  /* _spl0/_spl2 inlines + spl0/spl2 macros   */

/* Kernel SCSI transport ABI — LAST: rico.h's bool/uint/uchar macros. */
#include "rico.h"        /* uchar/uint/bool, TRUE/FALSE               */
#include "sd.h"          /* struct sdcom, SDCARDS/SDUNITS, sdspl      */

/* A CD logical block is 2048 bytes (NOT the 512 of the hard-disk path). */
#define AMIX_KERN_CD_SECTOR_SIZE 2048u

/*
 * Per-request transfer cap, in BYTES. read_sectors chunks the requested
 * range so no single sdcom exceeds this. 32768 = 16 CD sectors, a clean
 * multiple of the sector size. Unlike the userspace GSIO path there is no
 * fixed bounce buffer to fit under — the kernel DMAs straight to `buf`.
 */
#define AMIX_KERN_MAX_XFER 32768u

/*
 * Kernel externs the headers above do not declare. Spelled K&R (no
 * prototype), matching the period kernel's own declarations, so the ABI is
 * whatever the kernel provides. Left undefined in the relocatable object.
 * spl0/spl2 (and thus sdspl) are NOT here: <sys/inline.h> supplies them as
 * static inline _spl0/_spl2, so the calls below inline rather than emit an
 * external symbol the whole-kernel link would have to resolve.
 */
struct proc;
extern void    sdqueue(/* struct sdcom * */);  /* scsi.c: enqueue a request  */
extern paddr_t vtop(/* caddr_t, struct proc * */); /* VA -> bus address      */
extern int     sleep(/* caddr_t, int */);      /* block on a channel         */
extern void    wakeup(/* caddr_t */);          /* wake sleepers on a channel */

typedef struct amix_kern_ctx {
    unsigned int card;
    unsigned int unit;
    uint32_t     sector_count;   /* READ CAPACITY last_lba + 1 */
} amix_kern_ctx_t;

/*
 * One in-flight request. `sc` MUST be the first member: the SCSI driver
 * completes a request by calling (*cp->intr)(cp) with the sdcom pointer, so
 * the completion handler casts that pointer straight back to this struct.
 */
typedef struct amix_kern_req {
    struct sdcom   sc;
    volatile int   done;
} amix_kern_req_t;

static const odfs_media_ops_t amix_kern_media_ops;

/* ---- transport: struct sdcom + sdqueue() + sleep loop ----------------- */

static void amix_kern_intr(cp)
    struct sdcom *cp;
{
    amix_kern_req_t *r = (amix_kern_req_t *)cp;  /* sc is first member */

    r->done = 1;
    wakeup((caddr_t)cp);
}

static odfs_err_t amix_kern_submit(amix_kern_ctx_t *ctx,
                                   const unsigned char *cdb,
                                   unsigned int cdb_len,
                                   int reading,
                                   void *data,
                                   unsigned int nbyte)
{
    amix_kern_req_t req;
    unsigned int    i;

    if (ctx == 0)
        return ODFS_ERR_IO;
    if (cdb_len > sizeof(req.sc.cdb))
        return ODFS_ERR_INVAL;

    /* Zero the whole sdcom, exactly as gsioctl does (no partial init). */
    for (i = 0; i < (unsigned int)sizeof(req.sc); i++)
        ((char *)&req.sc)[i] = 0;
    req.done = 0;

    req.sc.card    = ctx->card;
    req.sc.unit    = ctx->unit;
    req.sc.reading = (bool)(reading ? TRUE : FALSE);
    /* Bus address for the driver's DMA. Identity-mapped on AMIX, but run it
     * through vtop() anyway so the value is correct if that ever changes. */
    req.sc.addr    = (caddr_t)vtop(data, (struct proc *)0);
    req.sc.nbyte   = nbyte;
    for (i = 0; i < cdb_len; i++)
        req.sc.cdb[i] = (uchar)cdb[i];
    req.sc.intr = amix_kern_intr;

    sdqueue(&req.sc);

    /* Block the SCSI interrupt across the test-and-sleep to avoid a lost
     * wakeup, mirroring gsioctl (sdspl == spl2; drop with spl0). */
    sdspl();
    while (!req.done)
        sleep((caddr_t)&req.sc, PRIBIO);
    spl0();

    if (!req.sc.okay || req.sc.status != 0)
        return ODFS_ERR_IO;

    return ODFS_OK;
}

/* ---- media ops -------------------------------------------------------- */

static odfs_err_t amix_kern_read_sectors(void *ctx, uint32_t lba,
                                         uint32_t count, void *buf)
{
    amix_kern_ctx_t *c = (amix_kern_ctx_t *)ctx;
    unsigned char   *out = (unsigned char *)buf;
    uint32_t         max_blocks = AMIX_KERN_MAX_XFER / AMIX_KERN_CD_SECTOR_SIZE;

    if (c == 0)
        return ODFS_ERR_IO;
    if (count == 0)
        return ODFS_OK;

    while (count != 0) {
        uint32_t      chunk = (count < max_blocks) ? count : max_blocks;
        unsigned char cdb[10];
        odfs_err_t    err;

        /* READ(10): opcode, LBA in cdb[2..5], block count in cdb[7..8]. */
        cdb[0] = 0x28;
        cdb[1] = 0;
        cdb[2] = (unsigned char)((lba >> 24) & 0xFF);
        cdb[3] = (unsigned char)((lba >> 16) & 0xFF);
        cdb[4] = (unsigned char)((lba >>  8) & 0xFF);
        cdb[5] = (unsigned char)( lba        & 0xFF);
        cdb[6] = 0;
        cdb[7] = (unsigned char)((chunk >> 8) & 0xFF);
        cdb[8] = (unsigned char)( chunk       & 0xFF);
        cdb[9] = 0;

        /* Data length is blocks * 2048 (CD), NOT * 512. */
        err = amix_kern_submit(c, cdb, 10, 1, out,
                               (unsigned int)(chunk * AMIX_KERN_CD_SECTOR_SIZE));

        /* TEMP DIAGNOSTIC -- remove: record the very first data read once into
         * the global diag struct. `out` still points at the buffer start here
         * (the loop advances it below). At ISO LBA 16 the sector starts with a
         * volume-descriptor type byte followed by "CD001" (43 44 30 30 31). */
        {
            static int amix_kern_read1_done = 0;
            if (!amix_kern_read1_done) {
                int j;
                amix_kern_read1_done = 1;
                cdfs_diag.r1_lba   = (int)lba;
                cdfs_diag.r1_count = (int)count;
                cdfs_diag.r1_err   = (int)err;
                for (j = 0; j < 16; j++)
                    cdfs_diag.r1_bytes[j] = out[j];
            }
        }
        /* TEMP DIAGNOSTIC -- remove */

        if (err != ODFS_OK)
            return err;

        out   += chunk * AMIX_KERN_CD_SECTOR_SIZE;
        lba   += chunk;
        count -= chunk;
    }

    return ODFS_OK;
}

static uint32_t amix_kern_sector_size(void *ctx)
{
    (void)ctx;
    return AMIX_KERN_CD_SECTOR_SIZE;
}

static uint32_t amix_kern_sector_count(void *ctx)
{
    amix_kern_ctx_t *c = (amix_kern_ctx_t *)ctx;
    return c->sector_count;
}

static void amix_kern_close(void *ctx)
{
    amix_kern_ctx_t *c = (amix_kern_ctx_t *)ctx;

    if (c)
        odfs_free(c);
}

static const odfs_media_ops_t amix_kern_media_ops = {
    amix_kern_read_sectors, /* read_sectors                               */
    amix_kern_sector_size,  /* sector_size                                */
    amix_kern_sector_count, /* sector_count                               */
    0,                      /* read_toc              (NULL -> UNSUPPORTED) */
    0,                      /* read_last_session_lba (NULL -> UNSUPPORTED) */
    0,                      /* read_audio            (NULL -> UNSUPPORTED) */
    0,                      /* read_cdtext           (NULL -> UNSUPPORTED) */
    amix_kern_close         /* close                                      */
};

/* ---- open-time SCSI probes -------------------------------------------- */

/* Big-endian 32-bit load from a READ CAPACITY response field. */
static uint32_t amix_kern_be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8) | ((uint32_t)p[3]);
}

/* INQUIRY (0x12): confirm this really is a CD-ROM (peripheral type 0x05). */
static odfs_err_t amix_kern_check_inquiry(amix_kern_ctx_t *c)
{
    unsigned char cdb[6];
    unsigned char data[36];
    odfs_err_t    err;

    cdb[0] = 0x12;   /* INQUIRY            */
    cdb[1] = 0;      /* EVPD = 0           */
    cdb[2] = 0;      /* page code          */
    cdb[3] = 0;
    cdb[4] = (unsigned char)sizeof(data);  /* allocation length (36) */
    cdb[5] = 0;      /* control            */

    err = amix_kern_submit(c, cdb, 6, 1, data, (unsigned int)sizeof(data));
    if (err != ODFS_OK)
        return err;

    /* Peripheral device type is the low 5 bits of byte 0. 0x05 = CD-ROM. */
    if ((data[0] & 0x1f) != 0x05)
        return ODFS_ERR_UNSUPPORTED;

    return ODFS_OK;
}

/* READ CAPACITY(10) (0x25): last LBA + block length; require 2048-byte blocks. */
static odfs_err_t amix_kern_read_capacity(amix_kern_ctx_t *c)
{
    unsigned char cdb[10];
    unsigned char data[8];
    uint32_t      last_lba;
    uint32_t      block_len;
    odfs_err_t    err;
    unsigned int  i;

    for (i = 0; i < (unsigned int)sizeof(cdb); i++)
        cdb[i] = 0;
    cdb[0] = 0x25;   /* READ CAPACITY(10); LBA 0, PMI 0 */

    err = amix_kern_submit(c, cdb, 10, 1, data, (unsigned int)sizeof(data));
    if (err != ODFS_OK)
        return err;

    last_lba  = amix_kern_be32(&data[0]);
    block_len = amix_kern_be32(&data[4]);

    /* 0xFFFFFFFF means the capacity overflows READ CAPACITY(10). */
    if (last_lba == 0xFFFFFFFFu)
        return ODFS_ERR_UNSUPPORTED;
    if (block_len != AMIX_KERN_CD_SECTOR_SIZE)
        return ODFS_ERR_UNSUPPORTED;

    c->sector_count = last_lba + 1u;
    return ODFS_OK;
}

/* ---- open ------------------------------------------------------------- */

odfs_err_t odfs_kern_media_open_scsi(unsigned int card, unsigned int unit,
                                     odfs_media_t *out)
{
    amix_kern_ctx_t *c;
    odfs_err_t       err;

    if (out == 0)
        return ODFS_ERR_INVAL;
    if (card >= (unsigned int)SDCARDS || unit >= (unsigned int)SDUNITS)
        return ODFS_ERR_INVAL;

    /*
     * Validate + init the card once. sdopen() runs the controller probe that
     * populates the queue table sdqueue() dispatches through, and is
     * idempotent, so a single call at open is enough. Declared in sd.h.
     */
    if (sdopen(card) != 0)
        return ODFS_ERR_IO;

    c = (amix_kern_ctx_t *)odfs_calloc(1, sizeof(*c));
    if (c == 0)
        return ODFS_ERR_NOMEM;

    c->card         = card;
    c->unit         = unit;
    c->sector_count = 0;

    err = amix_kern_check_inquiry(c);
    if (err != ODFS_OK) {
        odfs_free(c);
        return err;
    }

    err = amix_kern_read_capacity(c);
    if (err != ODFS_OK) {
        odfs_free(c);
        return err;
    }

    out->ops = &amix_kern_media_ops;
    out->ctx = c;
    return ODFS_OK;
}
