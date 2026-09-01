# Architecture Overview

## Layers

### A. Platform Frontends
Each frontend owns one host operating system's object model and calls into the
same core. All three are kept as small and policy-light as practical.

- **AmigaDOS handler** (`platform/amiga/`) — packet dispatch, mount lifecycle,
  locks, file handles; translation from the internal node model to AmigaDOS
  structures. OS3 and OS4 variants.
- **AMIX in-kernel SVR4 filesystem** (`platform/amix-kernel/`) — the `cdfs`
  vfsops/vnodeops vectors, mount lifecycle, and a per-mount vnode cache that
  gives the core's value-type nodes a kernel lifecycle.
- **Host and AMIX tools** (`tools/`, cross-built by `make amix`) — `imginfo`,
  `imgls`, `imgcat` over image files; no mount lifecycle of their own.

### B. Core Common Layer
- Internal node model (`odfs_node_t`)
- Path resolution
- Cache manager coordination
- Error mapping, memory policy helpers
- Configuration/profile support

### C. Device / Media Access Layer
- Sector reads via the `odfs_media_t` vtable (`odfs_media_ops_t`)
- Media geometry, session/TOC discovery
- Block size abstraction, retry policy
- Isolates hardware from parser logic

Implementations behind that one vtable:

| Implementation | Transport |
|---|---|
| `platform/amiga/handler_main.c` | Exec device I/O (`scsi.device` and friends) |
| `platform/host/file_media.c` | Host `stdio` over an image file |
| `platform/amix/amix_file_media.c` | AMIX raw `open`/`lseek`/`read` over an image or a disk slice |
| `platform/amix/amix_scsi_media.c` | AMIX userspace SCSI passthrough — the `/dev/scsi` GSIO ioctl |
| `platform/amix-kernel/amix_kern_media.c` | AMIX in-kernel SCSI — fill a `struct sdcom`, `sdqueue()`, sleep for the completion interrupt |

Two constraints the AMIX arms forced the seam to absorb, both of them
platform-neutral improvements rather than special cases:

- **DMA targets must be sector-aligned and must not cross a page.** A media
  backend that DMAs receives a buffer the core allocated, so the guarantee has
  to be made in the core (`odfs_malloc_aligned`), not per backend. The in-kernel
  AMIX backend then enforces it: it refuses an out-of-contract request rather
  than firing a wild DMA, and bounces an unaligned caller through an aligned
  sector.
- **No stdio and no libc on the kernel arm.** The seam is a plain vtable of
  function pointers over `void *` context, with allocation routed through
  `odfs_alloc.h`, so an implementation that has neither is expressible without
  changing the interface.

### D. Parser Backends
Pluggable readers implementing `odfs_backend_ops_t`:
- iso9660, rock_ridge, joliet
- udf, hfs, hfsplus
- cdda, multisession

For HFS-family media, the current frontend contract is data-fork-only.
Resource forks and Finder metadata are intentionally not surfaced through the
AmigaDOS view.

### E. Auxiliary Services
- Logging (`odfs_log`)
- Charset conversion
- Cache statistics
- Debug tracing

## Internal Object Model

All backends produce `odfs_node_t` entries. The handler frontend never needs to know which backend produced a node.

## Format Precedence

### ISO-Family Hybrids

Priority, highest first:

1. Rock Ridge
2. Joliet
3. Plain ISO 9660

The ISO9660 backend is probed first because Rock Ridge detection happens during ISO-family mount evaluation. If Rock Ridge is present, it wins. If not, Joliet is preferred over plain ISO 9660 unless Joliet has been disabled.

### Bridge Discs (ISO + UDF)

- Default policy: prefer ISO-family content, including Rock Ridge or Joliet when available
- `prefer_udf`: prefer UDF instead
- `force_backend`: force a specific backend

### Hybrid HFS/ISO Discs

- Default policy: prefer ISO-family content on Amiga
- `prefer_hfs`: prefer HFS instead

### HFS-Family Limits

- HFS and HFS+ expose data forks only
- Some classic Mac media may therefore lose resource-fork content in the
  AmigaDOS view

### Mount Options That Affect Selection

- `force_backend`: force a specific backend
- `force_session`: force a specific session number
- `disable_rr`: disable Rock Ridge
- `disable_joliet`: disable Joliet
- `prefer_udf`: prefer UDF on bridge discs
- `prefer_hfs`: prefer HFS on hybrid discs
