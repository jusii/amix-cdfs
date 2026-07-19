# AMIX port — known unknowns

Open questions about the AMIX (SVR4/m68k) target where we have solid *observations*
but no established *mechanism*. Kept separate from the code comments on purpose: an
unproven cause that hardens into a load-bearing comment is how a plausible guess
becomes a false fact.

Each entry states what is evidence, what is hypothesis, and what would settle it.

---

## 1. `__udivdi3` returns a wrong value in the kernel (2026-07-19, OPEN)

### What is established

On metal (A4000 + Z3660, kernel `sum -r /stand/unix` = 10842, cdfs at 31e8c3b), the
64-bit divide in `cdfs_getattr()`

```c
vap->va_nblocks = (u_long)((n->size + 2047u) / 2048u);
```

did not produce a quotient. m68k GCC 2.7.2.3 does not strength-reduce a DImode
division by a power of two, so this lowers to an out-of-line `__udivdi3` call, and
the value that came back fits

```
returned == (size + 2047) << 4
```

**exactly**, across four independent data points from one `ls -lR /cdrom`:

| observed | source | fit |
|---|---|---|
| 32848 | `SHORT.TXT`, 6 bytes | `(6 + 2047) * 16` |
| 33152 | `café.txt`, 25 bytes | `(25 + 2047) * 16` |
| 32912 | `total` of `deep/subdir` | one 10-byte entry |
| 65520 | `total` of `deep` | one 2048-byte directory extent |

`(size + 2047) << 4` is `(n0 << bm) >> 16` for libgcc `__udivdi3`'s own
normalization shift `bm = 32 - bitcount(2048) = 20` — i.e. the routine's **partial
remainder** after the first division digit, which for a 6-byte file is exactly
`0x80500000 >> 16 == 0x8050 == 32848`. The returned value is an internal
intermediate of the division loop.

The root directory's `total` printed as `4293100864`, i.e. a **negative** signed
`st_blocks` (`<sys/stat.h>` declares `long st_blocks`) that `ls` renders unsigned.

### What is NOT established

**Why.** The first hypothesis — a missing or mis-resolved link-time binding — is
**refuted**. `nm /stand/unix` on metal shows:

```
[6502] |  951104 | 1052 | FUNC | GLOB | 1 | __udivdi3
[7295] |  952156 | 1128 | FUNC | GLOB | 1 | __umoddi3
[4809] |  953284 |   84 | FUNC | GLOB | 1 | __lshrdi3
[4710] |    3723 |  256 | OBJT | LOCL | 2 | __clz_tab
[4713] |    3979 |  256 | OBJT | LOCL | 2 | __clz_tab
```

`__udivdi3` is linked, global, a real `FUNC`, at a sane text address, with a
plausible size. `__clz_tab` — the leading-zero table `__udivdi3` needs for
normalization — is present; the two copies are `LOCL` statics from two libgcc
objects, which is legal and expected, and their small values are section-relative
(normal for non-text symbols on this kernel, the same property `amix-kerntools`'
`tools/strstat.py` documents and works around).

So the mechanism is something other than a bad binding, and **as of 2026-07-19 it is
unidentified.**

Also ruled out, so nobody re-runs them:

- **Z3660 CPU emulation of `divu.l`.** The UAE core's `m68k_divl()`
  (`Z3660/z3660-firmware/Z-TURN/vitis_ide/Z3660_emu/src/uae/newcpu_common.cpp:907`)
  decodes Dq/Dr and the `0x400` 64-bit-dividend bit correctly on inspection.
- **`/usr/sys/fs/libfsgcc.a` missing on the box.** It is not there, but that is a
  build-time staging artifact that simply was not left behind — not evidence.
- **A native userland control on the box.** Not possible: metal has only AT&T `cc`
  (1991) and no `libgcc.a`, and AT&T `cc` does not emit `__udivdi3` at all.
  Settling this needs a **cross-compiled test binary pushed from the host** via
  `gcc-cross-amix`.

### Scope of the exposure

The ISO9660 / Rock Ridge **read path is empirically correct on metal** (5/5
validated, byte-identical reads). It uses only `__lshrdi3`
(`odfs_cache_read_bytes`), a different symbol, which is therefore known-good.
Whatever is wrong does not break that path.

`cdfs_getattr` was the only `__udivdi3` call site an ISO9660 mount exercised
per-file, and it no longer performs 64-bit division. The remaining call sites in
`build/amix-kernel/libcdfskern.o` **will return wrong values IF the misbehaviour is
general** — this is unproven, and none of them has been observed misbehaving:

| function | helpers | when it runs |
|---|---|---|
| `odfs_iso_merge_multi_extent` | `__udivdi3`, `__umoddi3` | multi-extent ISO files only |
| `udf_get_volume_size` | `__udivdi3` | UDF mount |
| `cdda_get_volume_size` | `__udivdi3` | audio disc |
| `hfs_probe`, `hfs_mount`, `hfs_read_node`, `hfs_get_volume_size` | `__udivdi3`, `__umoddi3` | HFS |
| `hfsp_mount`, `hfsp_read_fork`, `hfsp_get_volume_size` | `__udivdi3`, `__umoddi3` | HFS+ |

`hfs_probe` and the volume-size calls sit on the mount/probe path, so **HFS and UDF
probing is unproven on metal**, not known-bad.

To enumerate the current set after any change:

```sh
make amix-kernel
m68k-cbm-sysv4-objdump -d --reloc build/amix-kernel/libcdfskern.o \
  | awk '/^[0-9a-f]+ </{fn=$2} /__udivdi3|__umoddi3|__lshrdi3/{print fn, $NF}'
```

### What would settle it

1. Cross-compile a minimal `__udivdi3` exerciser with `gcc-cross-amix`, push it to
   the box, and run it in **userland** — does `2053ULL / 2048ULL` yield 1 there? That
   separates "libgcc/CPU is wrong" from "something about the kernel context is wrong".
2. If userland is fine, instrument the **kernel** side: a `cmn_err` in cdfs printing
   `__udivdi3(2053, 2048)` at mount time, which distinguishes a wrong callee from a
   wrong call.
3. Disassemble `__udivdi3` **as linked in `/stand/unix`** (at 951104) and diff it
   against the cross `libgcc.a` copy — this would catch a relocation applied wrongly
   at kernel-link time, which is the one binding-shaped hypothesis the `nm` output
   does not exclude.

### Standing rule until it is settled

In AMIX kernel code, **prefer shifts to 64-bit division**, and where a 64-bit divide
is genuinely needed, treat its result as suspect until validated on metal.
