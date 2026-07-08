/*
 * amix_kern_media.h — AMIX (SVR4/m68k) IN-KERNEL SCSI media backend.
 *
 * Private to the AMIX in-kernel platform: declares the entry point a future
 * cdfs_vfs_mount() calls to bind a genuine optical drive to an odfs_media_t.
 * The transport is the SVR4 kernel SCSI path (fill a `struct sdcom`, call
 * sdqueue(), sleep for the completion interrupt) — NOT the userspace GSIO
 * ioctl the sibling platform/amix backend uses.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ODFS_AMIX_KERN_MEDIA_H
#define ODFS_AMIX_KERN_MEDIA_H

#include "odfs/media.h"

/*
 * Open a genuine optical drive over the in-kernel SVR4 SCSI transport.
 *
 *   card — host-adapter index   (0 .. SDCARDS-1)
 *   unit — SCSI target id        (0 .. SDUNITS-1)
 *
 * INQUIRY confirms the target is a CD-ROM and READ CAPACITY(10) fills the
 * sector count; on success *out is a media handle backed by the kernel ops.
 * The optical-only ops (read_toc/read_last_session_lba/read_audio/
 * read_cdtext) are left NULL — the media.h wrappers map those to
 * ODFS_ERR_UNSUPPORTED, exactly as the userspace SCSI backend does.
 */
odfs_err_t odfs_kern_media_open_scsi(unsigned int card, unsigned int unit,
                                     odfs_media_t *out);

#endif /* ODFS_AMIX_KERN_MEDIA_H */
