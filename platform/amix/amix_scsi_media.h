/*
 * amix_scsi_media.h — AMIX (SVR4/m68k) SCSI-passthrough media backend.
 *
 * Private to the AMIX platform: declares the entry point that the media
 * open seam (platform/amix/amix_file_media.c:odfs_media_open_image) routes
 * to when it is handed a "scsi:" device spec instead of a file path.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ODFS_AMIX_SCSI_MEDIA_H
#define ODFS_AMIX_SCSI_MEDIA_H

#include "odfs/media.h"

/*
 * Open a genuine optical drive through the SVR4 SCSI passthrough
 * (ioctl 'GSIO' on /dev/scsi) rather than /dev/dsk — the only path that
 * can read a 2048-byte CD block, since the stock block layer (dd.c) hard-
 * codes 512-byte sectors + RDB and never traverses a CD.
 *
 *   spec = "scsi:<card>:<unit>[:<devpath>]"
 *          card    — host-adapter index (as passed to GSIO)
 *          unit    — SCSI target id on that adapter
 *          devpath — passthrough node; defaults to "/dev/scsi"
 *
 * On success *out is a media handle backed by amix_scsi_media_ops.
 */
odfs_err_t odfs_media_open_scsi(const char *spec, odfs_media_t *out);

#endif /* ODFS_AMIX_SCSI_MEDIA_H */
