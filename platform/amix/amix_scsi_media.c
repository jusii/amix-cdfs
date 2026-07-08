/*
 * amix_scsi_media.c — AMIX (SVR4/m68k) SCSI-passthrough optical media backend.
 *
 * "The de-facto CD reader": serves 2048-byte logical blocks off a REAL
 * optical drive by issuing SCSI CDBs directly through the SVR4 passthrough
 * (ioctl 'GSIO' on /dev/scsi). This is the only route to a CD on AMIX —
 * the stock block layer (amiga/alien/dd.c) hard-codes 512-byte sectors and
 * an RDB, so /dev/dsk can never present a 2048-byte CD block. The sibling
 * backend amix_file_media.c handles the "ISO on a slice / .iso file" case
 * over open()/lseek()/read(); this one handles a live drive.
 *
 * Transport (per amix-a4091/src/gsio.c, the working GSIO example): fill a
 * kernel `struct sdcom` (card/unit/reading/addr/cdb[12]/nbyte, plus the
 * driver-filled status/okay) and ioctl(fd,'GSIO',&sc). The `struct sdcom`
 * layout and the SDCARDS/SDUNITS limits come verbatim from the kernel
 * headers rico.h/sd.h in the cross sysroot (usr/sys/amiga/alien) — never
 * hand-transcribed, so the ABI matches the kernel exactly. The transport
 * is a swappable function pointer on the context (amix_submit_cdb_fn) so a
 * fake transport can drive the ops without touching real hardware.
 *
 * Ops implemented: read_sectors (READ(10)/0x28), sector_size (2048),
 * sector_count (from READ CAPACITY(10)/0x25). The optical-only ops
 * (read_toc/read_last_session_lba/read_audio/read_cdtext) are left NULL;
 * the media.h wrappers map a NULL op to ODFS_ERR_UNSUPPORTED — that is the
 * v1 stub. INQUIRY (0x12) at open confirms the target really is a CD-ROM.
 *
 * RUNTIME CAVEAT: stock scsi.c:gsioctl clamps its bounce buffer at
 * iobuf[1024], smaller than one 2048-byte CD block, so every read here
 * FAILS until the kernel iobuf is enlarged (a separate amix-a4091 change).
 * This file is correct code that builds today; it goes live when that lands.
 *
 * Written in C89 for the period AMIX cross compiler (GCC 2.7.2.3):
 * declarations at block scope, no C99 for-init or mixed declarations.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "odfs/media.h"
#include "amix_scsi_media.h"

/*
 * Standard headers first, so the kernel headers' aggressive short-name
 * macros (rico.h: bool/uint/uchar/and/or/not/...) cannot leak into the
 * portable declarations above or the libc prototypes below.
 */
#include <sys/types.h>   /* caddr_t                       */
#include <fcntl.h>       /* open(), O_RDWR                */
#include <unistd.h>      /* open/close/ioctl              */
#include <stdlib.h>      /* malloc/free/strtoul           */
#include <string.h>      /* strncmp                       */

/* Kernel SCSI-passthrough ABI — must be included LAST (macro pollution). */
#include "rico.h"        /* uchar/uint/bool, TRUE/FALSE   */
#include "sd.h"          /* struct sdcom, SDCARDS/SDUNITS */

/* The gsioctl command selector, exactly as amix-a4091/src/gsio.c spells it. */
#define AMIX_GSIO 'GSIO'

/* A CD logical block is 2048 bytes (NOT the 512 of gsio.c's hard-disk demo). */
#define AMIX_CD_SECTOR_SIZE 2048u

/*
 * Per-ioctl transfer cap, in BYTES. Each GSIO request copies through the
 * kernel's fixed bounce buffer in scsi.c:gsioctl; read_sectors chunks the
 * requested range so no single ioctl exceeds this. It MUST be a whole
 * multiple of AMIX_CD_SECTOR_SIZE and MUST be <= the kernel iobuf size.
 *
 * Stock AMIX clamps that iobuf at 1024 bytes — below one CD block — so this
 * is deliberately sized for the ENLARGED iobuf (>= 64 KiB) delivered by the
 * separate amix-a4091 kernel change. 32768 = 16 CD sectors, a clean
 * multiple of the sector size and comfortably under 64 KiB. Do NOT lower
 * this to 1024: that cannot hold even a single 2048-byte block.
 */
#define AMIX_SCSI_MAX_XFER 32768u

struct amix_scsi_ctx;

/*
 * Swappable transport. Fills and submits one CDB, returning ODFS_OK only on
 * a clean completion (okay set, status 0). Direction is `reading` (all v1
 * ops read). The GSIO implementation lives in amix_gsio_submit(); a test can
 * point ctx->submit at a fake to exercise the ops off-hardware.
 */
typedef odfs_err_t (*amix_submit_cdb_fn)(struct amix_scsi_ctx *ctx,
                                         const unsigned char *cdb,
                                         unsigned int cdb_len,
                                         int reading,
                                         void *data,
                                         unsigned int nbyte);

typedef struct amix_scsi_ctx {
    int                fd;
    unsigned int       card;
    unsigned int       unit;
    uint32_t           sector_count;   /* READ CAPACITY last_lba + 1 */
    amix_submit_cdb_fn submit;
} amix_scsi_ctx_t;

static const odfs_media_ops_t amix_scsi_media_ops;

/* ---- transport: struct sdcom + ioctl('GSIO') -------------------------- */

static odfs_err_t amix_gsio_submit(amix_scsi_ctx_t *ctx,
                                   const unsigned char *cdb,
                                   unsigned int cdb_len,
                                   int reading,
                                   void *data,
                                   unsigned int nbyte)
{
    struct sdcom sc;
    unsigned int i;

    if (ctx == 0 || ctx->fd < 0)
        return ODFS_ERR_IO;
    if (cdb_len > sizeof(sc.cdb))
        return ODFS_ERR_INVAL;

    /* Zero the whole struct, exactly as gsio.c does (no partial init). */
    for (i = 0; i < (unsigned int)sizeof(sc); i++)
        ((char *)&sc)[i] = 0;

    sc.card    = ctx->card;
    sc.unit    = ctx->unit;
    sc.reading = (bool)(reading ? TRUE : FALSE);
    sc.addr    = (caddr_t)data;
    sc.nbyte   = nbyte;
    for (i = 0; i < cdb_len; i++)
        sc.cdb[i] = (uchar)cdb[i];

    if (ioctl(ctx->fd, AMIX_GSIO, &sc) < 0)
        return ODFS_ERR_IO;
    if (!sc.okay || sc.status != 0)
        return ODFS_ERR_IO;

    return ODFS_OK;
}

/* ---- media ops -------------------------------------------------------- */

static odfs_err_t amix_scsi_read_sectors(void *ctx, uint32_t lba,
                                         uint32_t count, void *buf)
{
    amix_scsi_ctx_t *c = (amix_scsi_ctx_t *)ctx;
    unsigned char   *out = (unsigned char *)buf;
    uint32_t         max_blocks = AMIX_SCSI_MAX_XFER / AMIX_CD_SECTOR_SIZE;

    if (c == 0 || c->fd < 0 || c->submit == 0)
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
        err = c->submit(c, cdb, 10, 1, out,
                        (unsigned int)(chunk * AMIX_CD_SECTOR_SIZE));
        if (err != ODFS_OK)
            return err;

        out   += chunk * AMIX_CD_SECTOR_SIZE;
        lba   += chunk;
        count -= chunk;
    }

    return ODFS_OK;
}

static uint32_t amix_scsi_sector_size(void *ctx)
{
    (void)ctx;
    return AMIX_CD_SECTOR_SIZE;
}

static uint32_t amix_scsi_sector_count(void *ctx)
{
    amix_scsi_ctx_t *c = (amix_scsi_ctx_t *)ctx;
    return c->sector_count;
}

static void amix_scsi_close(void *ctx)
{
    amix_scsi_ctx_t *c = (amix_scsi_ctx_t *)ctx;

    if (c) {
        if (c->fd >= 0)
            close(c->fd);
        free(c);
    }
}

static const odfs_media_ops_t amix_scsi_media_ops = {
    amix_scsi_read_sectors, /* read_sectors                              */
    amix_scsi_sector_size,  /* sector_size                               */
    amix_scsi_sector_count, /* sector_count                              */
    0,                      /* read_toc              (NULL -> UNSUPPORTED)*/
    0,                      /* read_last_session_lba (NULL -> UNSUPPORTED)*/
    0,                      /* read_audio            (NULL -> UNSUPPORTED)*/
    0,                      /* read_cdtext           (NULL -> UNSUPPORTED)*/
    amix_scsi_close         /* close                                     */
};

/* ---- open-time SCSI probes -------------------------------------------- */

/* Big-endian 32-bit load from a READ CAPACITY response field. */
static uint32_t amix_be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8) | ((uint32_t)p[3]);
}

/* INQUIRY (0x12): confirm this really is a CD-ROM (peripheral type 0x05). */
static odfs_err_t amix_scsi_check_inquiry(amix_scsi_ctx_t *c)
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

    err = c->submit(c, cdb, 6, 1, data, (unsigned int)sizeof(data));
    if (err != ODFS_OK)
        return err;

    /* Peripheral device type is the low 5 bits of byte 0. 0x05 = CD-ROM. */
    if ((data[0] & 0x1f) != 0x05)
        return ODFS_ERR_UNSUPPORTED;

    return ODFS_OK;
}

/* READ CAPACITY(10) (0x25): last LBA + block length; require 2048-byte blocks. */
static odfs_err_t amix_scsi_read_capacity(amix_scsi_ctx_t *c)
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

    err = c->submit(c, cdb, 10, 1, data, (unsigned int)sizeof(data));
    if (err != ODFS_OK)
        return err;

    last_lba  = amix_be32(&data[0]);
    block_len = amix_be32(&data[4]);

    /* 0xFFFFFFFF means the capacity overflows READ CAPACITY(10). */
    if (last_lba == 0xFFFFFFFFu)
        return ODFS_ERR_UNSUPPORTED;
    if (block_len != AMIX_CD_SECTOR_SIZE)
        return ODFS_ERR_UNSUPPORTED;

    c->sector_count = last_lba + 1u;
    return ODFS_OK;
}

/* ---- open ------------------------------------------------------------- */

/*
 * Parse "scsi:<card>:<unit>[:<devpath>]". *card_out/*unit_out receive the
 * decimals; *dev_out points into `spec` at the override devpath, or is left
 * untouched (caller pre-seeds the default). Returns ODFS_OK on a well-formed
 * spec whose card/unit are within the kernel's SDCARDS/SDUNITS limits.
 */
static odfs_err_t amix_scsi_parse_spec(const char *spec,
                                       unsigned int *card_out,
                                       unsigned int *unit_out,
                                       const char **dev_out)
{
    const char   *p;
    char         *endp;
    unsigned long card_ul;
    unsigned long unit_ul;

    if (strncmp(spec, "scsi:", 5) != 0)
        return ODFS_ERR_INVAL;
    p = spec + 5;

    card_ul = strtoul(p, &endp, 10);
    if (endp == p || *endp != ':')
        return ODFS_ERR_INVAL;
    p = endp + 1;

    unit_ul = strtoul(p, &endp, 10);
    if (endp == p)
        return ODFS_ERR_INVAL;

    if (*endp == ':') {
        const char *dev = endp + 1;
        if (*dev == '\0')
            return ODFS_ERR_INVAL;
        *dev_out = dev;
    } else if (*endp != '\0') {
        return ODFS_ERR_INVAL;
    }

    if (card_ul >= (unsigned long)SDCARDS || unit_ul >= (unsigned long)SDUNITS)
        return ODFS_ERR_INVAL;

    *card_out = (unsigned int)card_ul;
    *unit_out = (unsigned int)unit_ul;
    return ODFS_OK;
}

odfs_err_t odfs_media_open_scsi(const char *spec, odfs_media_t *out)
{
    amix_scsi_ctx_t *c;
    const char      *devpath = "/dev/scsi";
    unsigned int     card = 0;
    unsigned int     unit = 0;
    int              fd;
    odfs_err_t       err;

    if (spec == 0 || out == 0)
        return ODFS_ERR_INVAL;

    err = amix_scsi_parse_spec(spec, &card, &unit, &devpath);
    if (err != ODFS_OK)
        return err;

    fd = open(devpath, O_RDWR);
    if (fd < 0)
        return ODFS_ERR_IO;

    c = (amix_scsi_ctx_t *)calloc(1, sizeof(*c));
    if (c == 0) {
        close(fd);
        return ODFS_ERR_NOMEM;
    }

    c->fd           = fd;
    c->card         = card;
    c->unit         = unit;
    c->sector_count = 0;
    c->submit       = amix_gsio_submit;

    err = amix_scsi_check_inquiry(c);
    if (err != ODFS_OK) {
        amix_scsi_close(c);
        return err;
    }

    err = amix_scsi_read_capacity(c);
    if (err != ODFS_OK) {
        amix_scsi_close(c);
        return err;
    }

    out->ops = &amix_scsi_media_ops;
    out->ctx = c;
    return ODFS_OK;
}
