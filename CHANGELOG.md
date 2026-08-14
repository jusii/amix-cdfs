## 2026-08-13 — amix-kernel: mount failures stop laundering into EIO (e0ef11a)

An unregistered SCSI card now returns ENXIO (with a cmn_err naming the card),
a non-CD target ENOTTY, bad-format/unsupported discs EINVAL; EIO means genuine
I/O. New codes ODFS_ERR_NO_DEVICE / ODFS_ERR_NOT_OPTICAL; AmigaOS paths untouched.
Machine-level verified (objdump jump-table diff); all suites green. Motivated by
the 64 MB card-shift hunt: three investigations chased ghosts because ENXIO,
BAD_FORMAT and IO all read as errno 5.
