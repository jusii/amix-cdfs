/*
 * amix_file_media.c — AMIX (SVR4/m68k) raw-device / image media backend.
 *
 * Userspace "ISO on a slice" backend for the AMIX port: it opens a raw
 * disk slice or a plain .iso file and serves 2048-byte logical blocks
 * out of it. Modelled on platform/host/file_media.c, but:
 *   - uses raw open()/lseek()/read() rather than stdio FILE*, because a
 *     raw SVR4 device rejects stdio's arbitrary-size buffered reads
 *     (2048 is a multiple of the 512-byte device block, so aligned
 *     reads are always valid);
 *   - single-session data only: no .cue parsing, no CDDA/TOC. The three
 *     mandatory media ops (read_sectors/sector_size/sector_count) are
 *     implemented; the optical-only ops are left NULL and the core
 *     treats them as ODFS_ERR_UNSUPPORTED.
 *
 * SCSI passthrough for a real drive (/dev/scsi GSIO) is a separate
 * backend (amix_scsi_media.c): this file owns the AMIX media-open seam
 * and routes a "scsi:" device spec there, but the CDB/ioctl logic lives
 * entirely in that file.
 *
 * Written in C89 for the period AMIX cross compiler (GCC 2.7.2.3):
 * declarations at block scope, no C99 for-init or mixed declarations.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "odfs/media.h"
#include "amix_scsi_media.h"

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define AMIX_SECTOR_SIZE 2048

typedef struct amix_media_ctx {
    int      fd;
    uint32_t sector_count;
} amix_media_ctx_t;

static const odfs_media_ops_t amix_file_media_ops;

static odfs_err_t amix_read_sectors(void *ctx, uint32_t lba,
                                    uint32_t count, void *buf)
{
    amix_media_ctx_t *fm = (amix_media_ctx_t *)ctx;
    off_t   offset;
    size_t  remaining;
    char   *out;

    if (fm == 0 || fm->fd < 0)
        return ODFS_ERR_IO;

    if (count == 0)
        return ODFS_OK;

    /* Guard the 32-bit off_t byte offset against overflow. */
    if (lba > (uint32_t)(0x7FFFFFFFL / AMIX_SECTOR_SIZE))
        return ODFS_ERR_RANGE;

    offset = (off_t)lba * (off_t)AMIX_SECTOR_SIZE;
    if (lseek(fm->fd, offset, SEEK_SET) == (off_t)-1)
        return ODFS_ERR_IO;

    remaining = (size_t)count * (size_t)AMIX_SECTOR_SIZE;
    out = (char *)buf;

    while (remaining != 0) {
        int got = read(fm->fd, out, (unsigned int)remaining);

        if (got < 0)
            return ODFS_ERR_IO;
        if (got == 0)
            return ODFS_ERR_EOF;

        out += got;
        remaining -= (size_t)got;
    }

    return ODFS_OK;
}

static uint32_t amix_sector_size(void *ctx)
{
    (void)ctx;
    return AMIX_SECTOR_SIZE;
}

static uint32_t amix_sector_count(void *ctx)
{
    amix_media_ctx_t *fm = (amix_media_ctx_t *)ctx;
    return fm->sector_count;
}

static void amix_close(void *ctx)
{
    amix_media_ctx_t *fm = (amix_media_ctx_t *)ctx;

    if (fm) {
        if (fm->fd >= 0)
            close(fm->fd);
        free(fm);
    }
}

static const odfs_media_ops_t amix_file_media_ops = {
    amix_read_sectors,  /* read_sectors          */
    amix_sector_size,   /* sector_size           */
    amix_sector_count,  /* sector_count          */
    0,                  /* read_toc              */
    0,                  /* read_last_session_lba */
    0,                  /* read_audio            */
    0,                  /* read_cdtext           */
    amix_close          /* close                 */
};

odfs_err_t odfs_media_open_image(const char *path, odfs_media_t *out)
{
    amix_media_ctx_t *fm;
    int               fd;
    off_t             end;

    if (path == 0 || out == 0)
        return ODFS_ERR_INVAL;

    /*
     * Device-spec routing: a "scsi:<card>:<unit>[:<devpath>]" spec names a
     * real optical drive reached through the SVR4 SCSI passthrough, not a
     * seekable file. Hand it to the GSIO backend; every other spec is a raw
     * slice or .iso path served by the file backend below.
     */
    if (strncmp(path, "scsi:", 5) == 0)
        return odfs_media_open_scsi(path, out);

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return ODFS_ERR_IO;

    end = lseek(fd, (off_t)0, SEEK_END);
    if (end == (off_t)-1) {
        close(fd);
        return ODFS_ERR_IO;
    }

    fm = (amix_media_ctx_t *)calloc(1, sizeof(*fm));
    if (fm == 0) {
        close(fd);
        return ODFS_ERR_NOMEM;
    }

    fm->fd = fd;
    fm->sector_count = (uint32_t)(end / AMIX_SECTOR_SIZE);

    out->ops = &amix_file_media_ops;
    out->ctx = fm;
    return ODFS_OK;
}
