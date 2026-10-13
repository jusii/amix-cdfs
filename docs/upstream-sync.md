# Tracking upstream ODFileSystem

This repository is an independent fork of
[reinauer/ODFileSystem](https://github.com/reinauer/ODFileSystem). It adds the
AMIX target and otherwise keeps upstream's code, layout and licence.

## Where the fork stands

- **Fork point:** upstream `00ae148` (2026-07-06). Everything after it on
  `main` is AMIX work, plus the portable fixes that work needed.
- **Upstream is ahead.** As of upstream `441d765` (2026-07-31) it has 39
  commits this fork does not carry. A trial merge conflicts in 10 files:
  `backends/joliet/joliet.c` (upstream's Joliet refactor), `backends/udf/udf.c`,
  `backends/hfs/hfs.c`, `backends/hfsplus/hfsplus.c`, `backends/cdda/cdda.c`,
  `backends/iso9660/iso9660.c`, `core/log.c`, and the include blocks of
  `tools/imgls`, `tools/imgcat` and `tools/imginfo`. None of the conflicts
  touch `platform/amix/` or `platform/amix-kernel/`.
- `core/cache_block.c` merges **without** a conflict even though both sides
  changed it. It carries the sector-aligned DMA buffers the AMIX kernel arm
  depends on (`fa9db1a`), so a merge must re-check it by hand.

## How it is synced

Upstream is brought in by **merging** `upstream/main` into `main`, never by
rebasing. The AMIX documentation cites this fork's commit hashes as evidence
(`fa9db1a`, `31e8c3b`, `9360542`, `e0ef11a`, `b383d14` and others); a rebase
would rewrite every one of them.

## What a merge must keep

- **C89 in `core/`, `backends/` and the three `img*` tools.** The AMIX
  compiler is GCC 2.7.2.3: no `for`-loop initial declarations, no declarations
  after statements. Upstream lines taken in a merge need the same sweep,
  including any new file that joins `CORE_SRCS`.
- **Small stack frames on the mount, readdir and lookup paths.** Large node and
  buffer locals are heap-allocated, because the kernel stack is small.
- **Sector-aligned media buffers** via `odfs_malloc_aligned` in
  `core/cache_block.c` and `core/session.c`.
- **No 32x32->64 multiply in the kernel object.** Division by a constant and
  widening multiplies go through `include/odfs/arith.h` (`ODFS_NO_MUL64`).

## Checks after a merge

```sh
make host && make check      # host suites, golden and malformed images
make amix && make amix-kernel
m68k-cbm-sysv4-objdump -d build/amix-kernel/libcdfskern.o > cdfs.lst
scan060.py cdfs.lst          # must report 0 candidates
```

The in-kernel filesystem then needs a mount, read-back and remount cycle on
AMIX hardware, because the merge rewrites code that runs in the kernel object.
