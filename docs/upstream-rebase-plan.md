# Upstream rebase plan — reinauer/ODFileSystem, 39 commits ahead

**Written 2026-07-26; re-measured 2026-09-01 against `upstream/main` 441d765. Analysis only:
nothing was rebased, cherry-picked, merged or branched; no source file was modified — the
conflict numbers come from `git merge-tree --write-tree`, which writes an object and touches
no ref and no working file.**

Every number in the July version has moved, most of them by roughly a factor of two, and two
files it listed as safe now conflict. The plan was also partly *executed* in the interim, on a
branch, and that attempt failed on metal and was then exonerated — §5.0 records it, because a
reader of `main` alone would conclude the sync has not been started.

This fork is explicitly *upstreamable* — the Amix (SVR4/m68k) platform layer is meant to become
one more target beside AmigaOS 3/4. Drift therefore costs us twice: once when we take upstream's
fixes, once when upstream takes our port. This document measures the current gap, names what
actually collides, and proposes how to close it.

Everything below is derived from commands recorded in the appendix, so it can be re-run and
checked rather than believed.

---

## 0. State

| | |
|---|---|
| Fork point (merge base) | `00ae148` (2026-07-06) |
| `upstream/main` | `441d765` (2026-07-31) — **39** commits ahead |
| Our `main` | `3c537a6` (2026-08-14) — **36** commits ahead |
| Topology | Strictly linear on both sides. No merges on `main`. `441d765` is **not** an ancestor of our HEAD. |
| Upstream releases since the fork | `v0.7.0` at `9afca35` (2026-07-28) — **not** on our `main`; our tag set is upstream's `v0.1`…`v0.7.0` |
| Host baseline today | `make check` at `3c537a6`: **22 suites, 111 tests, 0 failures** |
| Upstream's own baseline | `make check` at `441d765`: **20 suites, 112 tests, 0 failures** |
| Upstream remote | fetch-only; nothing pushed, no `origin` |

Files upstream changed since the fork: 43. Files **we** changed since the fork: 50.
Their intersection is **14** — the `Makefile` plus 13 sources — of which **10 conflict**.
The `Makefile` auto-merges as before; of the remaining three sources, `core/cache_block.c`,
`core/charset.c` and `backends/iso9660/iso9660.h` auto-merge **silently**, which is now the
sharpest finding in this document (§2.6).

> **As measured 2026-07-26:** `upstream/main` `4adecbb`, 20 ahead; our `main` `8de83cf`,
> 32 ahead; 23 / 46 files changed, intersection 10, of which 7 conflicted.

---

## 1. Classification of the 39 upstream commits

Impact is judged against **our** build arms — `make amix` (userspace, `-DODFS_PLATFORM_AMIX`)
and `make amix-kernel` (in-kernel cdfs, `-DODFS_PLATFORM_AMIX_KERNEL -D_KERNEL`). Note that
`CORE_SRCS` puts **every backend, including `cdda.c` and `udf.c`, into the in-kernel module**,
so "CD-audio feature we don't use" is not automatically "code we don't compile".

### 1.1 The original 20 (`00ae148..4adecbb`) — classification unchanged

| # | Commit | Subject | Class | Why |
|---|--------|---------|-------|-----|
| 1 | `340f620` | udf: walk the full allocation-descriptor chain | **WANT + BUILD-RISK + CONFLICT** | Real silent-corruption fix (>1 GiB and fragmented files read adjacent sectors as data). But `udf_read_object()` puts **`uint8_t ad_buf[2048]` on the stack**, and it is now on *every* UDF readdir/lookup/parent-resolve path — the exact defect class `702b2cb` fixed. Conflicts with our C89 hoisting in `udf_read`. |
| 2 | `2ffe5fb` | amiga: read past 4 GiB without TD64 | NO-IMPACT | `platform/amiga/` only — a tree we have never touched. |
| 3 | `72557d8` | Add RomTag | NO-IMPACT | `platform/amiga/resident.c` + Makefile. Makefile auto-merges. |
| 4 | `7f1df7d` | cdda: CD-Text track titles as file comments | CONFLICT-RISK + BUILD-RISK | `cdda.c` is in the kernel module. Adds 4 C99 for-init declarations. |
| 5 | `0d14de6` | **joliet: reuse the ISO 9660 machinery** | **CONFLICT-RISK (headline)** | Structural refactor of `iso9660.c/.h` + `joliet.c/.h`. See §2. |
| 6 | `346c937` | cdda: render CD-TEXT.txt one line per string | CONFLICT-RISK + BUILD-RISK | Adds 4 more C99 for-init decls and two 256-byte stack arrays (`cur`, `clean`) in kernel-compiled code. |
| 7 | `2518e8e` | host: mount audio cue/wav images with CD-Text | NO-IMPACT | `platform/host/` + tests — untouched by us; `platform/host/file_media.c` is our *template*, not our code. |
| 8 | `cbd6611` | tools: fall back to CDDA backend for audio images | CONFLICT-RISK (trivial) | Adds `tools/imgmount.h` and includes it from `imgcat.c`/`imgls.c`, where we added `odfs/printf.h`. 5-line include-block conflicts. |
| 9 | `6af04f9` | cdda: name audio volumes after CD-Text disc title | NO-IMPACT (build) | `cdda.h` gains `char album_title[32]` in the context struct — heap-allocated context, not stack. No C89 issue. |
| 10 | `7732531` | tools: report volume name of audio images | CONFLICT-RISK (trivial) | Same include-block collision in `imginfo.c`. |
| 11 | `3a6634b` | tests: size collector buffers to node fields | **NO-IMPACT** | Sweep flagged this as build-risk; it is not. `tests/` is not in any `amix*` Makefile target. On host `ODFS_NAME_MAX` is 512, so `test_collect_t collect;` becomes ~4.7 KB of *host* test stack — fine. It is a genuine `-Werror=format-truncation` fix for host CI: take it. |
| 12 | `713bb56` | node: widen the name buffer on AmigaOS 4 | **NO-IMPACT** | Also flagged by the sweep; also not a risk. The guard becomes `#if ODFS_PLATFORM_AMIGA && defined(__amigaos4__)` → 256, `#elif ODFS_PLATFORM_AMIGA` → 128, `#else` → 512. Amix defines neither Amiga macro, so `ODFS_NAME_MAX` stays **512** and `sizeof(odfs_node_t)` stays **672** — byte-identical to today. We never touched `include/odfs/node.h`, so it does not even conflict. |
| 13 | `a57f711` | amiga: keep mounting when stack swap cannot allocate | NO-IMPACT | `platform/amiga/startup.S`. |
| 14 | `7b3409a` | amiga: soft links through the OS4 vector port | NO-IMPACT | `platform/amiga/`. |
| 15 | `ba7568c` | amiga: device I/O on the calling task's request | NO-IMPACT | `platform/amiga/`. |
| 16 | `e37ddd3` | amiga: consume separator after virtual CDDA dir | NO-IMPACT | `platform/amiga/`. |
| 17 | `8e7e464` | amiga: empty drive as no-disk, not out-of-memory | NO-IMPACT | `platform/amiga/`. |
| 18 | `86b739e` | amiga: grant write/delete on read-only volume files | NO-IMPACT | `platform/amiga/`. |
| 19 | `2e93252` | amiga: honor DismountDevice keep-device on OS4 | NO-IMPACT | `platform/amiga/`. |
| 20 | `4adecbb` | amiga: retain stale volume nodes for backdrop locks | NO-IMPACT | `platform/amiga/`. |

**Tally:** 13 NO-IMPACT (all `platform/amiga`, `platform/host`, `tests/`, `include/odfs/node.h`),
1 WANT-with-caveats (`340f620`), 6 CONFLICT-RISK (`0d14de6`, `7f1df7d`, `346c937`, `cbd6611`,
`7732531`, and `340f620` again).

### 1.2 The 19 added since (`4adecbb..441d765`, 2026-07-26 … 2026-07-31)

Upstream spent this week on two things: a sharing/refactoring pass that hoists duplicated logic
into shared helpers, and a two-commit sweep making every integer conversion explicit. The second
is what re-shaped the conflict set — it edits the same lines our C89 sweep and stack-hoisting
edited, with the opposite intent.

| # | Commit | Subject | Class | Why |
|---|--------|---------|-------|-----|
| 21 | `2e4cad9` | amiga: include version in filesystem creator | NO-IMPACT | `platform/amiga/resident.c`. |
| 22 | `ca64df3` | amiga: retry deferred volume publication | NO-IMPACT | `platform/amiga/` only. |
| 23 | `a24cb35` | build: add distclean target | NO-IMPACT | `Makefile` only; auto-merges. |
| 24 | `b9898b1` | build: derive resident version from release tag | NO-IMPACT (build) + **PUBLISH INTERACTION** | `Makefile` + `resident.c`. The resident version string is now derived from the repository's release tag, so **a fork's tag set feeds the build's version**. Interacts directly with the do-not-push-tags recommendation in §5.0. |
| 25 | `d326fa3` | amiga: add configurable audio CD disk icon | CONFLICT-RISK + BUILD-RISK | Not Amiga-only despite the subject: it also edits `backends/cdda/cdda.{c,h}`, which are in the in-kernel module. |
| 26 | `ea88b9d` | amiga: share pure audio mount finalization | NO-IMPACT | `platform/amiga/`. |
| 27 | `c48fe5e` | cdda: share virtual node initialization | **CONFLICT + KERNEL** | A shared node-init helper in `cdda.c`, which compiles into `libcdfskern.o`. Exactly the shape `5c34112`'s stack rules apply to; needs a hygiene pass on arrival. |
| 28 | `5666e09` | amiga: reuse lock constructor for duplicate locks | NO-IMPACT | `platform/amiga/`. |
| 29 | `51e115f` | **udf: share file identifier traversal** | **CONFLICT (major) + KERNEL** | Replaces the per-call FID locals with a shared `udf_fid_info_t` walk. Four of `udf.c`'s nine hunks are this commit meeting `b28a9a2`/`702b2cb`. Lands in the kernel module. |
| 30 | `a33674a` | **core: share Macintosh timestamp conversion** | **CONFLICT (structural)** | Adds `core/timestamp.c` + `include/odfs/timestamp.h` and *deletes* `hfs_parse_mac_date` and `hfsp_parse_date`. This is what pulls `hfs.c` and `hfsplus.c` into the conflict set — and it adds a **new file to `CORE_SRCS`**, i.e. to the in-kernel module, which therefore needs its own stack/C89 pass. |
| 31 | `c0ec147` | amiga: share read-only SCSI command setup | NO-IMPACT | `platform/amiga/`. |
| 32 | `e676bf5` | **cache: share victim installation bookkeeping** | **SILENT OVERLAP — the dangerous one** | Rewrites the victim-installation path in `core/cache_block.c` (+44/−54) — the file that carries `fa9db1a`. Produces **no conflict at all**. See §2.6. |
| 33 | `2f17be3` | tests: split file-handle checks by ISO format | NO-IMPACT | `tests/integration/` only. |
| 34 | `d67a02c` | amiga: avoid obsolete Seek API on OS4 | NO-IMPACT | `platform/amiga/`. |
| 35 | `9afca35` | release: update release builder to gcc v13.4 | NO-IMPACT | `.github/workflows/release.yml`. This is the commit **`v0.7.0`** points at. |
| 36 | `e5807e9` | **core: make integer conversions explicit** | **CONFLICT** | `core/charset.c`, `core/log.c`, `core/timestamp.c`. Brings `core/log.c` into the conflict set, and `core/charset.c` into the silent-merge set. |
| 37 | `e39c68a` | **backends: make disk field conversions explicit** | **CONFLICT (broadest)** | All six backends plus three headers. With `e5807e9` this is the new conflict driver: our hoisted declarations against their widened types, in the same lines. |
| 38 | `3a1b719` | amiga: make ABI conversions explicit | NO-IMPACT | `platform/amiga/` only. |
| 39 | `441d765` | build: enable stricter Amiga compiler warnings | NO-IMPACT | `Makefile` only. |

**Tally for the 19:** 12 NO-IMPACT, 6 CONFLICT (`d326fa3`, `c48fe5e`, `51e115f`, `a33674a`,
`e5807e9`, `e39c68a`), 1 SILENT OVERLAP (`e676bf5`).

**Across all 39:** nothing upstream is a *behavioural* threat to the Amix port. The cost is
still mechanical — one structural refactor, one stack-hygiene regression, an explicit-conversion
sweep to re-resolve, and a small C89 re-sweep — but there is now one commit (`e676bf5`) whose
risk is precisely that it is **not** mechanical-looking, because git resolves it without asking.

---

## 2. Conflict analysis — measured, not guessed

`git merge-tree --write-tree main upstream/main` produces tree `03c5a77` and exits 1, reporting
**10 conflicted files**. Measured per file by extracting the three merge stages and running
`git merge-file -p --diff3` on them: **28 hunks, ~1307 conflicted lines** (conflict-marker lines
excluded from the count).

| File | Hunks | Conflicted lines | Our file | Severity |
|---|---|---|---|---|
| `backends/joliet/joliet.c` | 5 | **847** | 639 | **Total rewrite** — take theirs wholesale |
| `backends/udf/udf.c` | 9 | 160 | 1392 | Substantial (was 1 hunk / 43 lines) |
| `backends/hfs/hfs.c` | 3 | 101 | 936 | **NEW** — one deleted function plus two mechanical |
| `backends/hfsplus/hfsplus.c` | 2 | 83 | 1008 | **NEW** — same shape |
| `backends/cdda/cdda.c` | 3 | 49 | 1180 | Mechanical (was 1 / 18) |
| `backends/iso9660/iso9660.c` | 2 | 42 | 1155 | Mechanical |
| `core/log.c` | 1 | 13 | 163 | **NEW** — trivial |
| `tools/imgls/imgls.c` | 1 | 4 | 133 | Trivial |
| `tools/imgcat/imgcat.c` | 1 | 4 | 99 | Trivial |
| `tools/imginfo/imginfo.c` | 1 | 4 | 72 | Trivial |

`Makefile`, `backends/joliet/joliet.h`, `include/odfs/node.h`, `backends/iso9660/iso9660.h`,
`core/cache_block.c` and `core/charset.c` auto-merge — **the last three of those without a
conflict but not without risk**; see §2.6.

**None of the conflicts touch `platform/amix/` or `platform/amix-kernel/`.** Upstream has no such
trees, so all 22 of our platform files replay untouched. Every conflict is one of our two
portable sweeps (`b28a9a2` C89, `702b2cb`/`5c34112` stack-hoisting) meeting one of upstream's
refactors or its `e5807e9`/`e39c68a` explicit-conversion sweeps in the same lines — same intent,
opposite direction. That is why the count is large and the resolutions are nevertheless
mechanical.

> **As measured 2026-07-26:** tree `7f53977`, 9 hunks across 7 files —
> joliet.c 2/461, udf.c 1/43, iso9660.c 2/29, cdda.c 1/18, the three tools 1/5 each.
> `hfs.c`, `hfsplus.c` and `core/log.c` did not conflict then and do now.

### 2.1 `0d14de6` — the Joliet refactor (headline)

**What upstream did.** `joliet.c` drops from 604 to 108 lines. `joliet_context_t` is deleted;
a Joliet mount is now an `iso_context_t` with a new `int joliet` flag. Everything except
`probe` and `mount` becomes the shared `odfs_iso_*` ops, and mounting goes through a new
`odfs_iso_mount_vd(cache, log, session_start, vd_lba, joliet, root_out, backend_ctx)`.
`iso_parse_dir_record()` loses four parameters — `session_start`, `next_id`, `lowercase`,
`high_sierra` are read off `ctx` instead.

**Which of our work sits on it.** Six of our 36 commits touch these files:

| Our commit | What it does to the collision zone |
|---|---|
| `b28a9a2` C89 sweep | for-init and mixed declarations across `iso9660.c`, `joliet.c`, `cdda.c`, `udf.c`, `hfs.c`, `hfsplus.c`, `core/log.c`, `imgcat.c`, `imgls.c`, `imginfo.c` — i.e. **all ten conflicted files** |
| `702b2cb` stack #1 | heap-allocates sector-sized locals in `iso9660.c`, `udf.c`, `cdda.c`, `hfs`, `hfsplus`, `core/` |
| `5c34112` stack #2 | heap-allocates the readdir/lookup/parent walk scratch in `iso9660.c` **and `joliet.c`**; changes `odfs_iso_dir_stub()` from by-value return to out-param |
| `3420559` | root Rock-Ridge scratch, `iso9660.c` |
| `ad09035` | kernel `memset`, touches `iso9660.c` |
| `2b57f88` | `%llu` removal in the three tools |

**What specifically collides.**

1. **`joliet_resolve_parent` — our work is deleted, not merged.** `5c34112` heap-allocated
   `grandparent` and `great` (672 B each) inside `joliet_resolve_parent`, and reworked its exits
   onto a `gp_done` label. Upstream deletes that whole function; Joliet now dispatches to
   `odfs_iso_resolve_parent`. Same for `joliet_readdir`/`joliet_read`/`joliet_lookup`/
   `joliet_unmount`/`joliet_get_volume_*`. **Roughly 64 lines of our `joliet.c` hardening plus
   6 of our C89 fixes evaporate.** This is a *good* outcome — see §2.7 — but it means
   "resolve the conflict" is wrong framing for this file. The correct action is **take upstream's
   `joliet.c` wholesale (`git checkout --theirs`) and discard ours**.

2. **`iso_parse_dir_record` call sites — a clean, mechanical collision.** The *definition*
   auto-merges to upstream's new 6-parameter form (our sweep changed the body, not the signature).
   Only the two call sites conflict, because we hoisted their declarations and made `node` a heap
   pointer. Merged-tree lines 502-515 and 777-791. Resolution is to keep our hoisted C89
   declarations and heap `node`, and adopt upstream's argument list:

   ```c
   /* ours (HEAD side)                  -> resolution */
   consumed = iso_parse_dir_record(rec, avail, ctx->session_start,
                                   &ctx->next_node_id, lowercase,
                                   ctx->high_sierra, node, &sua, &sua_len);
   /* becomes */
   consumed = iso_parse_dir_record(rec, avail, ctx, node, &sua, &sua_len);
   ```
   Two sites, ~14 lines. No judgement required.

3. **`odfs_iso_dir_stub` — a silent semantic seam that git resolves *in our favour*.**
   `backends/iso9660/iso9660.h` auto-merges: upstream's added block of `odfs_iso_*` declarations,
   the new `joliet` context field (`0d14de6`) and its widened field types (`e39c68a`) land
   alongside our changed `odfs_iso_dir_stub` signature, because the two sides touched different
   lines. The `.c` call sites at upstream lines 1007/1034
   are *not* in upstream's changed range, so our out-param versions win there too. Result is
   consistent — but this is exactly the shape of merge that compiles by luck rather than by
   review. **It must be verified by building, not by reading the merge output.**

4. **The shared readdir now needs our stack fix, and Joliet inherits it free.** Upstream's
   post-refactor `iso9660.c` still has `odfs_node_t node;` (672 B) at lines 473 and 693, and
   `rr_info_t rr;` (**1184 B**) at lines 383, 517 and 704 — all on the stack. Our HEAD has none.
   After the merge, our `5c34112` heap-ification must be re-applied to `odfs_iso_readdir`,
   `odfs_iso_readlink` and `odfs_iso_resolve_parent` — and because those are now *shared*, Joliet
   gets kernel-stack safety without a second patch.

### 2.2 `340f620` / `51e115f` / `e39c68a` — UDF

**Nine hunks now, not one** — `340f620` has been joined by `51e115f` (share file identifier
traversal) and `e39c68a` (explicit conversions), so `udf.c` went from 1 hunk / 43 lines to
9 / 160. They fall into three groups:

- **`udf_read()` (`340f620`)** — the original hunk: we hoisted its declarations for C89, upstream
  replaced the body with a call to the new `udf_read_object()`. **Take upstream's side
  unconditionally**; the old single-extent assumption is a silent-corruption bug.
- **The FID walk (`51e115f`)** — upstream replaces the per-entry locals (`tag`, `fid_version`,
  `fid_flags`, `name_len`, `icb_lba`, `icb_part`, `impl_len`, `fid_len`, `node`, `name_off`) with
  a shared `udf_fid_info_t`, and adds a `dir_size_out` out-parameter. Our side had hoisted the
  same locals to block top for C89. Take upstream's structure; the hoisting problem disappears
  with the locals. **But the shared struct is now on the readdir/lookup/parent path in the kernel
  module — measure its frame** (Packet B).
- **Type widenings (`e39c68a`)** — `uint8_t sum` → `uint32_t`, `uint8_t used` → `size_t`, and
  similar. Keep our hoisted declaration, adopt upstream's wider type. No judgement.

Two follow-ups are mandatory before this ships to metal:

- **`uint8_t ad_buf[2048]` is a stack local in `udf_read_object()`.** `702b2cb` established that
  the SVR4/Amix kernel stack is "a page or two" and that a 2048-byte stack sector buffer on the
  mount path overran it into the u-area (`Bad vp 0x4F532058`, `kmem_free lookup failed`).
  `udf_read_object()` is now on the file-read path *and* every directory FID-stream walk
  (readdir, lookup, parent resolution, child scan). This is a straight re-introduction of the
  defect we already paid for. It must be heap-allocated or moved into `udf_context_t` before any
  Amix kernel build is trusted. Upstream should be told; it is a latent AmigaOS risk too.
- **`odfs_cache_read_bytes(cache, lba, skip, out + done, &n)` feeds an arbitrarily-offset caller
  buffer into the cache's streaming path.** `core/cache_block.c:592` hands `out` *directly* to
  `odfs_media_read()` once the run reaches `ODFS_CACHE_STREAM_MIN_SECTORS` (= 2), so any UDF read
  of 4 KB or more becomes a DMA into `buf + done`. **This is already safe under `31e8c3b`**: the
  Amix kernel backend bounces any non-page-aligned target through a per-call page-aligned bounce
  sector, precisely for "callers that stream full sectors into the middle of their own buffer".
  So the new walker takes the bounce path rather than the direct path — correct, slower, and
  worth an explicit bench measurement rather than an assumption.

### 2.3 `346c937` / `7f1df7d` / `d326fa3` / `c48fe5e` — CDDA

Three hunks in `cdda.c` now. The original: our hoisted `for (i = ...)` against upstream's new
nested `for (uint8_t type = 0x80; ...) { for (uint8_t block = 0; ...) }`. Take upstream, re-hoist.
`346c937` also adds `char cur[256]; char clean[256];` — 512 bytes of stack in kernel-compiled
code. Since July, `d326fa3` (audio CD disk icon — not Amiga-only, it edits `cdda.c`/`cdda.h`) and
`c48fe5e` (share virtual node initialization) have landed on the same file.

`cdda.c` carries the largest single block of arriving C89 debt (29 sites at `441d765`, §3), so
this file wants the re-sweep and the stack pass in one pass. Lower priority than the UDF buffer —
the CDDA backend does not mount on the kernel arm today — but it belongs on the same hygiene run.

### 2.4 `cbd6611` / `7732531` — the tools

Three identical 4-line conflicts in the include block, unchanged since July. Both includes are
wanted:

```c
#include "odfs/printf.h"   /* ours  */
#include "../imgmount.h"   /* theirs */
```

Union-merge, done.

### 2.5 `hfs.c`, `hfsplus.c` and `core/log.c` — new to the conflict set

All three were listed as non-colliding in July. They are not any more.

**`hfs.c` (3 hunks / 101 lines) and `hfsplus.c` (2 / 83)** conflict for the same reason, and it
is `a33674a`, not `e39c68a`: upstream hoisted the Mac-epoch date conversion into a shared
`core/timestamp.c` and **deleted** `hfs_parse_mac_date()` and `hfsp_parse_date()` from the two
backends. Our side has the same two functions with `b28a9a2`'s hoisted declarations, so the
whole deleted body shows up as a conflict — that is where ~85 % of those 184 lines are. This is
the `joliet.c` pattern again at smaller scale: **take upstream and let our hoisting go with the
function**, then check that the shared version in `core/timestamp.c` is itself C89-clean and
stack-cheap (it is a new file in `CORE_SRCS`, so it compiles into `libcdfskern.o`).

The remaining hunks are pure `e39c68a` type widenings meeting our hoisting —
`uint16_t data_off` → `uint32_t`, `for (int off = ...)` → `for (uint32_t off = ...)`. Keep the
hoisted declaration, adopt the wider type.

**`core/log.c` (1 hunk / 13 lines)** is trivial: our `ODFS_LOG_BUF_SIZE` form of the bounded
formatter call against upstream's `sizeof(buf) - (size_t)hdr` cast from `e5807e9`. Keep ours,
adopt their cast. It is listed only because a file that used not to conflict now does, and the
July document said otherwise.

### 2.6 Silent overlaps — auto-merged, and therefore unreviewed

**This is the sharpest finding of the re-measurement.** Fourteen files were changed by both
sides. Ten conflict and will therefore be read by a human. Three sources auto-merge without a
murmur — and one of them is the file carrying the fix that stopped cdfs corrupting binaries.

| File | Our side | Upstream side | Why it does not conflict |
|---|---|---|---|
| **`core/cache_block.c`** | `fa9db1a` (sector-align every buffer a media backend DMAs into), `3ed0285` (one data slab), `b28a9a2` (C89) | `e676bf5` **cache: share victim installation bookkeeping** (+44/−54) | The hunks do not overlap textually: ours are in the allocation and streaming paths, theirs is the victim-installation path |
| `core/charset.c` | `b28a9a2` | `e5807e9` | Different lines |
| `backends/iso9660/iso9660.h` | `b28a9a2`, `5c34112` | `0d14de6`, `e39c68a` | Different lines — already flagged in §2.1.3 |

§2.1.3 named this hazard in the abstract — "compiles by luck rather than by review" — and then
the July §2.5 (now §2.7) asserted that `fa9db1a` had "zero upstream overlap". **That was true
against `4adecbb` and is false against `441d765`.** `e676bf5` rewrites the same file. Doing the
merge against the July text is how a proven DMA guarantee gets lost to a clean-looking
auto-merge.

**Measured in advance, read-only** (`git show 03c5a77:core/cache_block.c` diffed against both
parents): the auto-merged result differs from our side in exactly **two hunks**, both inside the
victim-installation path, and all three `odfs_malloc_aligned` call sites — the one data slab, the
per-entry fallback buffer, and the read-ahead stream buffer — survive **textually intact**, as
does the slab-mode free path. So the merge is very probably correct. "Very probably correct" is
not the standard for this particular guarantee, which is why §5.1 carries it as a named gate
rather than an assumption: text surviving is not behaviour surviving when the surrounding
bookkeeping has been rewritten.

The same check on the other two is cheap and clean: neither auto-merged file carries any arriving
C89 debt (0 for-init sites in the merged `cache_block.c` and `charset.c`, against 3 and 1 in
upstream's — our swept lines won).

### 2.7 What genuinely does not collide

Only one side changed each of these, so the merge takes that side whole:

**Ours, untouched by upstream:**

- **`31e8c3b`** (`platform/amix-kernel/amix_kern_media.c`) — the DMA page-cross contract.
  **Zero upstream overlap**: upstream has no `platform/amix-kernel/` at all.
- All of `platform/amix/` and `platform/amix-kernel/` — 22 files.
- `core/mount.c`, `core/cache_meta.c`, `core/namefix.c`, `core/ancestry.c`, `core/node.c`,
  `core/error.c`, `core/session.c`,
  `include/odfs/{alloc,api,backend,cache,log,printf,config,ancestry,compat_stdint}.h`.
- `tests/unit/test_remount.c`, `tests/unit/test_cdfs_blocks.c`, `docs/`.

**Theirs, untouched by us:**

- `backends/rock_ridge/rock_ridge.c` (`e39c68a`) and the new `core/timestamp.c` +
  `include/odfs/timestamp.h` (`a33674a`) — both arrive wholesale, and both compile into the
  in-kernel module, so both want a stack/C89 pass on arrival.
- **`platform/amiga/` end to end** — `git diff 00ae148 main -- platform/amiga` is empty, so every
  one of upstream's Amiga commits arrives unopposed.

**The `31e8c3b` page-cross contract is structurally out of the blast radius.** It cannot be lost
to a conflict resolution; it can only be lost to a *rewrite* of history (see §4).
`fa9db1a` no longer has that property — see §2.6.

---

## 3. C89 debt — measured

Two constructs matter for `m68k-cbm-sysv4-gcc` (gcc 2.7.2.3): for-loop initial declarations
(hard **errors** under C89) and declaration-after-statement. Designated initializers
(121 sites tree-wide) are accepted as a GNU extension and are *not* an issue — our sweep
deliberately left them.

Actionable sites in the shared build (`core/` + `backends/` + the three cross-built tools),
re-counted 2026-09-01 with the appendix method:

| Tree | for-init | mixed-decl | total |
|---|---|---|---|
| fork point `00ae148` | 43 | 92 | **135** |
| `upstream/main` `4adecbb` (the July figure) | 51 | 86 | **137** |
| `upstream/main` `441d765` (today) | 51 | 75 | **126** |
| our `main` `3c537a6` | 0 | 0 | **0** |

**The 19 new upstream commits are net −11, and they added no new for-init sites at all** — the
count is 51 at both `4adecbb` and `441d765`. The whole change is mixed declarations removed as a
side effect of `e39c68a`'s explicit conversions: `hfs.c` −3, `hfsplus.c` −3, `udf.c` −5. So the
July re-sweep estimate stands rather than growing.

Where **upstream's** 126 sites sit, per file at `441d765` (for-init + mixed-decl) — and how much
of each file the merge can carry in:

| File | Upstream sites | How the merge reaches it |
|---|---:|---|
| `backends/hfs/hfs.c` | 31 | Conflicts (3 hunks) — every arriving line is read |
| `backends/cdda/cdda.c` | 29 | Conflicts (3 hunks) |
| `backends/hfsplus/hfsplus.c` | 21 | Conflicts (2 hunks) |
| `backends/udf/udf.c` | 19 | Conflicts (9 hunks) |
| `backends/iso9660/iso9660.c` | 9 | Conflicts (2 hunks) |
| `core/session.c` | 4 | Upstream never touched it — **our swept version wins whole** |
| `core/cache_block.c` | 3 | Silent auto-merge (§2.6) |
| `tools/imgcat.c` 3, `tools/imgls.c` 2 | 5 | Conflicts, but only in the include block |
| `core/mount.c`, `core/namefix.c` | 1 each | Upstream never touched them — ours win whole |
| `core/charset.c`, `backends/joliet/joliet.c`, `backends/rock_ridge/rock_ridge.c` | 1 each | Auto-merge or take-theirs |

**Measured on the merged tree** (`git show 03c5a77:<file>`, read-only): every file that does *not*
conflict comes out of the merge with **zero for-init sites** — `session.c`, `mount.c`,
`namefix.c`, `charset.c`, `cache_block.c`, `joliet.c` and `rock_ridge.c` alike. Nothing sneaks in
through the quiet path. What arrives in the ten conflicting files depends on how each hunk is
resolved, which is exactly why they conflict.

So the post-merge re-sweep is still small, and it is now precisely locatable: **the upstream
lines taken wholesale in `joliet.c`, `hfs.c`, `hfsplus.c`, `udf.c` and `cdda.c`, plus
`core/timestamp.c`**, which is a new file in `CORE_SRCS` that has never been through our sweep at
all. The 119 anonymous-variadic-macro sites cost nothing: our fix is a single `__STDC_VERSION__`
gate in `include/odfs/log.h`, a file upstream did not touch.

(Our tree shows 3 residual for-init sites in `tools/imgbench`, `imgdump`, `imgstat` — host-only
tools, not in any `amix*` target. Harmless, but they are why upstream should get a C89 gate; see
§6.)

---

## 4. Recommended strategy

**Do not rebase our 36 commits onto `upstream/main`. Merge `upstream/main` into `main`.**

Three reasons, in order of weight:

1. **Rebase invalidates the metal proof chain.** The cdfs port's evidence is anchored to specific
   hashes: `fa9db1a` + `31e8c3b` are cited as the page-crossing-DMA fix in `docs/amix-known-unknowns.md`
   (`§1`, "cdfs at `31e8c3b`"), in the grimoire briefs, and in the workspace record of the
   15/15-cycle metal validation and the 8 byte-identical canary binaries. `9360542` and `e0ef11a`
   have since been added to that set of cited anchors. A rebase rewrites all 36 hashes, and every
   one of those citations silently becomes a dangling reference to a commit that no longer
   exists. The proof would have to be re-anchored by hand, or re-earned on metal.

2. **Rebase replays five commits that were deliberately deleted.** `3beda3b`, `3fd67c1`,
   `aa41ea4`, `51540aa` and `9aadea5` are `TEMP`/`TEMP DIAG` instrumentation, removed again by
   `139aabb` and `a7da26a`. Under rebase each is replayed against the moving upstream base, so we
   would resolve conflicts in `cdfs_vfsops.c`/`cdfs_vnodeops.c` scaffolding that is then thrown
   away two commits later. Pure waste, and each resolution is a chance to corrupt the diagnostic
   narrative those commits preserve.

3. **A merge is exactly one conflict event, and it is already fully characterized.** §2 above
   *is* the resolution plan: 28 hunks across 10 files, of which `joliet.c` is `--theirs`
   wholesale, the two Mac-date functions in `hfs.c`/`hfsplus.c` go the same way, three (`tools/`)
   are union-merges, and most of the rest are "keep our hoisted declaration, adopt their wider
   type". Rebase would spread the same collisions across 6 of our commits (`b28a9a2`, `702b2cb`,
   `5c34112`, `3420559`, `ad09035`, `2b57f88`), several of them repeatedly, because each replays
   against a base that already contains the refactor.

   The count grew from 9 hunks to 28 in five weeks of upstream activity. That is the cost of
   waiting, stated as a number.

**Cherry-pick-forward is the wrong tool here** (it is a rebase with extra steps and the same hash
churn), but it *is* the right tool for the eventual upstream PR — see §6. Keep the two concerns
separate: `main` gets a merge and keeps its provenance; the PR branch gets a curated replay onto
`upstream/main` at PR time, when the shape of the offer is known.

---

## 5. Staged plan

### 5.0 What already happened — Packets A–C were run, and Packet D failed

**This plan is not un-started.** Between 2026-07-26 and 2026-08-07 it was executed as far as
Packet D, on the branch **`upstream-sync-2026-07`**, which still exists locally and is **not**
merged into `main`. A reader of `main` alone sees no trace of it, which is why it is recorded
here.

| | |
|---|---|
| Branch | `upstream-sync-2026-07`, head `5b9828d` (2026-08-07) |
| Base | branched from `main` at `8de83cf`; merge commit `68d24a5` (2026-07-26) took `upstream/main` **`4adecbb`** — i.e. the 20-commit gap, not today's 39 |
| Contents | the merge, then 12 commits: **5 code** (`7a5df7d`, `b1e3fa3`, `2c2a22f`, `3dabf1a`, `8ce66ca` — the Packet B stack-hygiene work on `udf.c`, `hfs.c`, `hfsplus.c`) and **7 bench/metal record docs** |
| Packets A–C | completed on that branch |
| Packet D | **FAILED on metal 2026-08-06** — recorded in `e2d4484`, "the upstream-sync line must not ship" |
| Retraction | **2026-08-07, `5b9828d`**: the metal panic was **not** a sync-line regression. It was the stack-allocated `sdcom` — an independent, pre-existing cdfs defect, fixed on `main` as `9360542`. The sync line was exonerated. |

Two consequences for the next attempt:

1. **The branch predates `9360542` and `e0ef11a`**, the two `main` commits that came out of that
   metal session. It is behind `main`, not ahead of it.
2. **It was merged against `4adecbb`**, so it is also 19 upstream commits stale. Its merge
   resolution does not cover `hfs.c`, `hfsplus.c`, `core/log.c` or `core/timestamp.c`, and it was
   made before `e676bf5` created the `core/cache_block.c` silent overlap.

So the branch is **not** a shortcut to redoing this work, but its **five code commits are** —
they are the Packet B stack-hygiene patches, already written and already bench-validated, and
they apply to the same functions the new merge will produce. Salvage those by cherry-pick after
the merge; leave the seven bench-record docs commits behind (they are workspace-local lab records
of the kind §6.1 lists as do-not-offer, and they have no place in a published tree).

### Packet A — mechanical merge, host green
1. Branch from `main` (`upstream-sync-2026-07`); `git merge upstream/main`.
2. Resolve in this order, cheapest-first so the tree compiles early:
   1. `core/log.c` — 1 hunk: keep ours, adopt their `(size_t)hdr` cast.
   2. `tools/imgcat|imgls|imginfo` — union the include blocks (3 × 4 lines).
   3. `backends/joliet/joliet.c` — **`--theirs`, wholesale.** Do not hand-merge.
   4. `backends/hfs/hfs.c`, `backends/hfsplus/hfsplus.c` — `--theirs` for the deleted
      `hfs_parse_mac_date`/`hfsp_parse_date` (they move to `core/timestamp.c`); for the remaining
      hunks keep our hoisted declaration and adopt their wider type.
   5. `backends/iso9660/iso9660.c` — 2 call sites onto upstream's
      `iso_parse_dir_record(rec, avail, ctx, ...)`, keeping our hoisted declarations and heap
      `node`.
   6. `backends/udf/udf.c` — 9 hunks in three groups (§2.2): `--theirs` for the `udf_read()` body
      and the `udf_fid_info_t` FID walk; keep-ours-plus-their-type for the widenings.
   7. `backends/cdda/cdda.c` — `--theirs`, then re-hoist the loop variables.
3. Re-apply the C89 sweep to whatever upstream lines won (§3), **including the new
   `core/timestamp.c`**, which enters `CORE_SRCS` — and therefore `libcdfskern.o` — for the first
   time.
4. `make host && make check` must return to **22 suites / 121 tests / 0 failures**. Derivation,
   measured 2026-09-01 by running `make check` on both trees: ours is 22 suites / 111 tests;
   upstream's at `441d765` is 20 suites / 112 tests; the per-suite deltas upstream holds over us
   are `test_udf` **+5**, `test_cache` **+2**, `test_cdda` **+2**, `test_file_media` **+1** = +10,
   and upstream adds no new suite *file*, while our `test_remount` (2) and `test_cdfs_blocks` (7)
   are ours alone. 111 + 10 = 121 = 112 + 9. **A count below 121 means a case was lost in
   resolution.**
5. **Explicitly verify `odfs_iso_dir_stub`** — the header/`.c` seam that auto-merged (§2.1.3).
   A clean compile is the proof; a clean-looking diff is not.
6. **Run the `core/cache_block.c` gate before anything else is trusted** — see §5.1. It is the
   one file that merges silently over a proven guarantee.

**Acceptance:** host builds and the full suite is green; `git diff upstream/main -- backends/ core/`
contains only changes we can each name.

### Packet B — re-harden the new upstream code, cross-build
1. Heap-allocate (or move into `udf_context_t`) the `ad_buf[2048]` in `udf_read_object()`, in the
   style of `702b2cb`. Measure the m68k frame before and after, as `702b2cb`/`5c34112` did.
2. Re-apply `5c34112`'s heap-ification to the now-shared `odfs_iso_readdir`, `odfs_iso_readlink`,
   `odfs_iso_resolve_parent` (`odfs_node_t` 672 B, `rr_info_t` 1184 B). Confirm Joliet inherits it.
3. Consider the two 256-byte CD-Text stack arrays (lower priority).
4. `make amix` and `make amix-kernel` with `m68k-cbm-sysv4-gcc`; whole-kernel link via
   `amix-kerntools`.

**Acceptance:** both cross arms build; no function on the mount/readdir/lookup path has a frame
larger than its pre-merge measurement.

### Packet C — bench validation
Full bench cycle on the Amiberry box: `mount -F cdfs`, `ls -la /cdrom`, byte-compare a read-back
file against the host original, unmount, **remount** (the `3ed0285` heap-churn path), and a UDF
image exercising multi-extent reads — which is new coverage the old code could not have passed.

### Packet D — metal validation
See §5.1. This is image-invalidating and belongs in a batched metal session.

**Effort estimate: 3 dispatch packets (A, B, C) + 1 metal session (D).** Packet A is the only one
with real uncertainty, and §2 bounds it: 28 hunks across 10 files, of which **10** are `--theirs`
or union (`joliet.c` ×5, the three tools ×1, the `hfs.c`/`hfsplus.c` Mac-date deletions ×1 each),
**2** are the named two-line `iso_parse_dir_record` signature change, and the remaining **16** are
"keep our hoisted declaration, adopt their wider type".
Packet A is bigger than it was in July by about the ratio the gap grew.

### 5.1 Regression evidence required

The port has bench **and** metal proof today. The merge touches `iso9660.c`, `udf.c` and `cdda.c`
— all three in the in-kernel module — so the following must be **re-proven**, not assumed:

| What | Why it is at risk | Evidence required |
|---|---|---|
| `mount -F cdfs 0,2 /cdrom` on metal | shared readdir/mount path rewritten by `0d14de6` | mount succeeds; `ls -la /cdrom` enumerates; a read-back file is byte-identical to the host original |
| **`core/cache_block.c` survived the silent merge (`fa9db1a` + `3ed0285`)** | `e676bf5` rewrites the victim path in the same file and **auto-merges with no conflict** (§2.6) — the July text's "zero upstream overlap" is no longer true | **Before any cross build:** diff the merged `core/cache_block.c` against *both* parents and confirm by line that all three `odfs_malloc_aligned` sites (slab, per-entry fallback, read-ahead stream buffer) and the slab-mode free path are intact and still on the paths that feed `odfs_media_read()`. The pre-merge measurement says they survive textually; this gate is what turns that into a verified fact. Then compile — a clean-looking diff is not proof. |
| **Page-aligned DMA guarantees (`fa9db1a` + `31e8c3b`)** | `31e8c3b` is not conflicted (§2.7), but `udf_read_object()` newly feeds arbitrarily-offset buffers into the streaming path at `cache_block.c:592` — and `fa9db1a`'s own file is now a silent auto-merge | ≥15 mount/read/unmount cycles with the 8 canary binaries byte-identical, as in the original `31e8c3b` validation. **This is the one non-negotiable gate** — it is the fix that stopped cdfs corrupting `/usr/sbin/umount` and libc. |
| Kernel-stack headroom | `ad_buf[2048]` + shared-readdir stack nodes | m68k frame sizes on mount/readdir/lookup/readlink/resolve_parent, compared against the numbers in `702b2cb` and `5c34112` |
| Remount / heap churn | `3ed0285` single-slab cache untouched but the callers moved | remount cycle clean, no wild-pointer frees |
| `st_blocks` (`12b5a97`, `8de83cf`) | `__udivdi3` path in `cdfs_getattr` | `ls -s` sane on metal, per the closed known-unknown §1 |
| UDF multi-extent (**new capability**) | `340f620` is why we want this merge | a >1 GiB or deliberately fragmented UDF image read correctly — coverage we have never had |

| `core/timestamp.c` as a new kernel-compiled file | `a33674a` adds it to `CORE_SRCS`, so it enters `libcdfskern.o` without ever having been through our C89 or stack sweeps | compiles under `make amix-kernel`; frame measured like any other kernel-path function |

Until Packet D passes, the merged branch is **not** a replacement for the proven kernel on metal.
Note that the metal anchor has moved since July: the proven line is now `main` at `9360542` /
`e0ef11a`, not `8de83cf` — see §5.0.

---

## 6. Upstreamability

### 6.1 Our commits, by upstream candidacy

**Strong candidates — portable, and they fix real defects on upstream's own targets:**

| Commit | Offer |
|---|---|
| `fa9db1a` core: sector-align every buffer a media backend DMAs into | Generic `odfs_malloc_aligned`. AmigaOS DMA has the same class of constraint; upstream currently has no alignment guarantee at all. |
| `702b2cb` + `5c34112` core: move sector-sized / node scratch off the stack | AmigaOS handler tasks also run small stacks (upstream even added `a57f711` for a failed stack swap). Measured frame reductions make the case. After `0d14de6` this patch is **~64 lines smaller** and covers both backends at once. |
| `3ed0285` cache: one data slab | Portable robustness + fewer allocator round-trips. |
| The `ad_buf[2048]` fix from Packet B | Should be offered as a **bug report against `340f620`** regardless of whether we PR anything else. |

**The port itself — the actual deliverable:** `platform/amix/`, `platform/amix-kernel/`, the
`amix`/`amix-kernel` Makefile targets, `include/odfs/compat_stdint.h`, and the
`ODFS_PLATFORM_AMIX` branches in `config.h`/`alloc.h`/`log.h`/`printf.h`. This is additive and
sits cleanly on the two documented vtable seams (`odfs_media_ops_t`, `odfs_backend_ops_t`) — the
layering has held.

**Medium — needs a policy decision from upstream, not just a patch:** `b28a9a2` (C89 sweep) and
`737457e` (bounded formatter). A one-off sweep will rot: upstream wrote 8 new for-init
declarations in a fortnight without noticing, because nothing enforces it. **The ask that makes
this durable is a CI job, not a diff** — a `-std=c89 -pedantic-errors` compile of
`core/` + `backends/` + `tools/`. Offer the sweep *and* the gate together, or expect to re-sweep
every sync.

**Workspace-local by nature — do not offer:**
`docs/amix-known-unknowns.md` (cites our metal box, kernel `sum -r` values, the Z3660 firmware);
the five `TEMP`/`TEMP DIAG` commits (already reverted); `12b5a97`/`4041037`/`8de83cf`
(`st_blocks` + the `__udivdi3` toolchain narrative — a `gcc-cross-amix` story, not an ODFileSystem
one); `6f727b1`/`d3beca7`/`844c2ed` (SVR4 `mount` helper and `/etc/mnttab` — SVR4-specific, ships
inside `platform/amix/` if at all); and the seven bench-record docs commits on
`upstream-sync-2026-07` (§5.0), which are lab records of a specific box.

**Added since July, and both strong:** `9360542` (never hand `sdqueue()` a stack-allocated
request) is SVR4-specific in its detail but the *class* — a driver completing in a foreign
context dereferencing a caller's stack object — is worth reporting wherever it applies; and
`e0ef11a` (`ODFS_ERR_NO_DEVICE` / `ODFS_ERR_NOT_OPTICAL`) is **portable and additive**: two new
`odfs_err_t` codes with the AmigaOS `odfs_err_to_dos()` default arm already covering them, so the
AmigaOS paths do not change. That one should go with the port.

**One publish hazard, from `b9898b1`:** upstream now derives the resident version string from the
repository's release tag. A fork carrying upstream's `v0.1`…`v0.7.0` tags therefore feeds them
into its own build's version — and, if those tags were ever pushed to a fork's own remote,
`.github/workflows/release.yml` and `aminet-release.yml` both fire on `v*`. Push branches, not
tags.

### 6.2 Does the refactor make upstreaming easier or harder?

**Easier, clearly.** `0d14de6` removes the duplicated backend, so our stack-hardening argument has
to be made once against `odfs_iso_readdir`/`odfs_iso_resolve_parent` instead of twice against two
near-identical implementations, and there is one less file to keep C89-clean. Upstream also
demonstrated (32,688 → 31,252 bytes on the ROM profile) that it cares about code size on
constrained targets — which is the same argument that motivates our heap-vs-stack patches. That
is a useful shared premise to open a PR on.

The *only* thing that gets harder is timing: upstream is actively refactoring shared parsers, so
every month of drift costs another conflict of this shape. **Sync now, and sync on a cadence.**

### 6.3 LIV2's ACTION_PARENT `..`-entry speedup (upstream PR #6)

**Still not in `upstream/main`** — re-checked 2026-09-01: none of the 39 commits touches
`action_parent` (`git log -S'action_parent' 00ae148..upstream/main -- platform/amiga/` is empty).
We could not inspect the PR itself: only `upstream/main` and `upstream/amigaos4.1fe` are fetched,
and this task was scoped no-fetch. So what follows is about *where it would land*, not what it
contains.

**Do we diverge around it? No.** We have never touched `platform/amiga/` — `git diff 00ae148 main
-- platform/amiga` is still empty at `3c537a6`. Our `..` handling lives in `platform/amix-kernel/cdfs_vnodeops.c`,
an entirely separate frontend. So as an AmigaDOS packet-handler change it is **NO-IMPACT**.

**The forward-looking risk is if it migrates below the seam.** "Use the `..` directory entry
instead of re-walking" is a *backend* optimization at heart, and the natural place for it is
`odfs_iso_resolve_parent` — which is (a) the function `5c34112` heap-ified, and (b) as of
`0d14de6`, shared by ISO 9660 *and* Joliet, so a change there now moves twice the code. If that
PR (or a follow-up) is ever pushed down into `resolve_parent`, it collides head-on with our stack
work. **Recommendation: get our `resolve_parent` heap-ification upstream *before* that happens**,
so the optimization is written on top of it rather than against it. Worth 10 minutes with the PR
diff at the next sync to confirm which side of the seam it sits on.

---

## Appendix — commands behind every number here

Run from a checkout of this repository. Nothing below writes into the working tree;
`merge-tree --write-tree` creates an object and touches no ref and no file.

```sh
R=$(git rev-parse --show-toplevel)
T=$(mktemp -d)                 # scratch for the read-only tree extracts

# topology (§0)
git -C $R merge-base main upstream/main                        # 00ae148
git -C $R rev-list --count --left-right upstream/main...main   # 39  36
git -C $R log --reverse --format='%h %ad %s' --date=short --name-only 4adecbb..upstream/main
comm -12 <(git -C $R diff --name-only 00ae148 upstream/main | LC_ALL=C sort) \
         <(git -C $R diff --name-only 00ae148 main          | LC_ALL=C sort)   # the 14

# conflict evidence (§2) — tree 03c5a77, exit 1, 10 conflicted files
git -C $R merge-tree --write-tree main upstream/main > $T/mt.txt; echo $?
head -1 $T/mt.txt                                              # 03c5a77...

# per-file hunks and conflicted lines: extract the three stages, then diff3.
# Conflicted lines are counted EXCLUDING the <<<<<<< ||||||| ======= >>>>>>> markers.
awk 'NF==4 && $3 ~ /^[123]$/ {print $2, $3, $4}' $T/mt.txt      # stage blobs per file
git -C $R cat-file blob <stage2> > $T/f.ours                    # ours
git -C $R cat-file blob <stage1> > $T/f.base                    # merge base
git -C $R cat-file blob <stage3> > $T/f.theirs                  # upstream
git merge-file -p --diff3 $T/f.ours $T/f.base $T/f.theirs > $T/f.merged
grep -c '^<<<<<<<' $T/f.merged                                  # hunks -> 28 total

# the silent auto-merges (§2.6) — read the merged blob straight out of the tree
git -C $R show 03c5a77:core/cache_block.c > $T/cb.merged
diff -u $R/core/cache_block.c $T/cb.merged                      # 2 hunks, victim path only
grep -c odfs_malloc_aligned $T/cb.merged $R/core/cache_block.c  # equal on both sides

# C89 debt (§3) — read-only extracts, nothing written into the repo
for r in 00ae148 4adecbb upstream/main main; do
  mkdir -p $T/$r && git -C $R archive $r | tar -x -C $T/$r; done
grep -rhoE 'for *\( *(const +)?(unsigned +|signed +)?(u?int[0-9_a-z]*|size_t|char|long|short|struct )' \
    $T/<tree>/core/*.c $T/<tree>/backends/*/*.c $T/<tree>/tools/img{cat,info,ls}/*.c | wc -l
gcc -std=gnu89 -Wdeclaration-after-statement -fsyntax-only \
    -I $T/<tree>/include -I $T/<tree>/backends -I $T/<tree>/tools $T/<tree>/backends/cdda/cdda.c \
  2>&1 | grep -c 'ISO C90 forbids mixed declarations'
# -> 135 (00ae148) / 137 (4adecbb) / 126 (441d765) / 0 (3c537a6)

# struct sizes (§1, §2.1)
# ODFS_NAME_MAX=512  sizeof(odfs_node_t)=672  sizeof(rr_info_t)=1184

# baselines (§0, Packet A step 4)
make -C $R host && make -C $R check          # 22 suites, 111 tests, 0 failures
make -C $T/upstream/main check               # 20 suites, 112 tests, 0 failures
```
