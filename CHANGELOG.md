## 2026-10-02 — the AMIX kernel object carries no 68060-trapping 64-bit multiplies (b383d14)

include/odfs/arith.h provides odfs_udiv32/umod32/sdiv32/smod32/udivsz/umul64, switched on by ODFS_NO_MUL64,
which only the ODFS_PLATFORM_AMIX_KERNEL arm sets; used at every site that produced a mulul/mulsl candidate
(cdda, hfs, hfsplus, udf, namefix, cache_block, the amix-kernel glue). scan060 on libcdfskern.o 40 -> 0
(md5 673cd9f4 -> 83955331); AmigaOS, host and AMIX userspace builds unchanged; new unit test test_arith;
host 116/116, golden 51/51, malformed 36/36.

## 2026-09-01 — docs: the AMIX port documented at the front door (063dc1d, 72ba3d1, 6514e3b, b6ca4fa, 4f3bb51, 81c8721, 446ec77, 1c4779a)

README gains a "Building for AMIX (SVR4/m68k)" chapter (two build arms, the GCC 2.7.2.3
toolchain and its override variables, the vfssw[]/relink boundary that belongs to amix-kerntools,
the mount -F cdfs syntax, known limits) and repo-relative links. platform/amix-kernel/README.md is
new: file inventory, the ld -r build, the four hard contracts (0fcf308 no VFS_INIT, 6446103
self-locating cdfsinit, fa9db1a+31e8c3b page-aligned DMA, 9360542 no stack sdcom), the mount errno
table. platform/amix/README.md gets the errno-diagnosis table e0ef11a made possible.
docs/architecture.md: three frontends, five media backends behind odfs_media_ops_t. docs/testing.md:
the five AMIX targets are validated out of band, not in CI; host baseline 22 suites / 111 tests.
docs/upstream-rebase-plan.md re-measured against upstream/main 441d765: 39/36 commits, 28 hunks in
10 files; core/cache_block.c is a silent auto-merge over fa9db1a and now carries a named §5.1 gate.

## 2026-09-01 — build: neutral AMIX cross-prefix default, amix-kernel phony (e9abd8d)

AMIX_CROSS_PREFIX ?= /opt/amix-cross replaces a home-directory default; the amix-kernel target is
declared .PHONY.

## 2026-08-13 — amix-kernel: mount failures stop laundering into EIO (e0ef11a)

An unregistered SCSI card now returns ENXIO (with a cmn_err naming the card),
a non-CD target ENOTTY, bad-format/unsupported discs EINVAL; EIO means genuine
I/O. New codes ODFS_ERR_NO_DEVICE / ODFS_ERR_NOT_OPTICAL; AmigaOS paths untouched.
Machine-level verified (objdump jump-table diff); all suites green. Motivated by
the 64 MB card-shift hunt: three investigations chased ghosts because ENXIO,
BAD_FORMAT and IO all read as errno 5.

## 2026-08-07 — amix-kernel: the sdcom moves off the per-process kernel stack (9360542)

The M-RUN4 panic was this defect, not an upstream-sync regression: the SCSI command block lived on
the per-process kernel stack and was DMA'd after the frame was gone. Backfilled at the 2026-09-26
consolidation review.
