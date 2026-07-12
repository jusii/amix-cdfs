# AMIX (SVR4/m68k) — userspace side of cdfs

`cdfs` is the AMIX port of ODFileSystem: a read-only optical filesystem
(ISO9660 / High Sierra / Rock Ridge / Joliet / UDF) that lives *in the kernel*
(`platform/amix-kernel/`). This directory holds the userspace half — the media
backends used by the cross-built tools, and the **SVR4 mount helper** that makes
`mount -F cdfs` work at all.

## The mount helper — `/usr/lib/fs/cdfs/mount`

**Source:** [`mount.c`](mount.c) · **Build:** `make amix-mount` · **Artifact:** `build/amix/mount`

### Why it exists

SVR4's generic `mount(1M)` does not know how to mount anything itself. It parses
its arguments and then **execs `/usr/lib/fs/<fstype>/mount`**, handing that binary
the special and the mount point as the **last two positional arguments** (any
`-o` options precede them):

```
mount -F cdfs 0,2 /cdrom   →   exec /usr/lib/fs/cdfs/mount [-o ...] 0,2 /cdrom
```

So without this binary installed, `mount -F cdfs` **fails outright** — the kernel
filesystem is unreachable no matter how correct it is. It is a small file, but
the whole CD stack rests on it.

The helper does exactly two things:

1. **`mount(2)`**, the 6-argument `MS_DATA` form, fstype `"cdfs"`, always
   read-only:
   `mount(spec, dir, MS_DATA | MS_RDONLY, "cdfs", (char *)0, 0)`.
2. **Appends the `/etc/mnttab` entry** (see below).

### The mount spec is *not* a `/dev` path

cdfs addresses the drive directly, as `"<card><sep><unit>"` — `<sep>` is any run
of non-digit characters, so `0,2` / `0.2` / `0:2` are equivalent. `card` is the
SCSI host-adapter index, `unit` the target id; `cdfs_mount()` parses it kernel-side
(`platform/amix-kernel/cdfs_vfsops.c`, max 64 bytes).

There is no `/dev` node because a real CD never traverses `/dev/dsk` on stock
AMIX: `dd.c` hard-codes 512-byte blocks + an RDB, and stock `gsio`'s `iobuf[1024]`
is smaller than a 2048-byte CD block. Addressing by `(card,unit)` sidesteps that
entirely. Give the mount point as an **absolute** path — the entry records the
path as passed, exactly as the stock SVR4 helpers do.

### It writes `/etc/mnttab` — because nothing else will

**The SVR4 kernel does not maintain `/etc/mnttab`; the mount commands do.** This
is the trap: skip the bookkeeping and the CD mounts and reads perfectly, yet is
invisible to `mount` (no args) and `df` (no args), and `umount` refuses it with
*"not in mnttab"*. The filesystem looks broken when it is not.

So on success the helper appends the standard TAB-separated, newline-terminated
5-field line:

```
special \t mountp \t fstype \t options \t time
0,2	/cdrom	cdfs	ro	1783866823
```

(alongside the box's own `/dev/dsk/c6d0s1  /  ufs  rw,suid  252647335`). fstype is
`cdfs`, options are always `ro`, and `time` is `time(0)` in decimal seconds. Once
the entry exists, the **generic `umount(1M)` works natively and removes it** — no
cdfs-specific umount helper is needed.

If the mnttab append fails, the filesystem **is still mounted**: the helper warns
on stderr and **exits 0**. Never unmount a working filesystem because bookkeeping
failed.

### Exit status

| Status | Meaning |
|--------|---------|
| 0 | mounted (possibly with an mnttab warning on stderr) |
| 1 | `mount(2)` failed — message on stderr with the errno |
| 2 | usage error (fewer than 2 operands) |

### Building

Cross-build (the normal path — needs the `gcc-cross-amix` toolchain):

```sh
make amix-mount          # just the helper  → build/amix/mount
make amix                # libodfs.a + imginfo/imgls/imgcat + mount
```

The result is an `ELF 32-bit MSB executable, Motorola m68k, 68020, (SYSV)`.

**On-box fallback** (the recovery route after an image rebuild — no cross
toolchain, no repo, just `mount.c` copied to the box and its own 1991 `cc`):

```sh
cc -O -o /usr/lib/fs/cdfs/mount mount.c
```

That fallback is *load-bearing*, and it is why `mount.c` is written in old-style
**K&R C** with no C99 and — deliberately — **no dependency on anything in this
repo**: no odfs headers, no `libodfs`, system headers only. The Makefile enforces
that mechanically by building it with neither `$(AMIX_INCLUDES)` nor
`$(AMIX_DEFS)`; the moment `mount.c` grows a repo dependency, that rule stops
compiling. Keep it standalone.

### Installing

```sh
mkdir -p /usr/lib/fs/cdfs
cp mount /usr/lib/fs/cdfs/mount
chmod 755 /usr/lib/fs/cdfs/mount

mkdir -p /cdrom
mount -F cdfs 0,2 /cdrom        # SCSI card 0, target 2
mount                           # the CD now appears here …
df                              # … and here
umount /cdrom                   # … and this removes the mnttab entry
```

The `cdfs` row in the kernel's `vfssw[]` is injected separately, by
`amix-kerntools`' `fstypes.conf` staging.
