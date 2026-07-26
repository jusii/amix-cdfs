# Upstream rebase plan — reinauer/ODFileSystem, 20 commits ahead

**Written 2026-07-26. Analysis only: nothing was rebased, cherry-picked, merged or branched;
no source file was modified.**

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
| `upstream/main` | `4adecbb` (2026-07-20) — 20 commits ahead |
| Our `main` | `8de83cf` (2026-07-21) — 32 commits ahead |
| Topology | Strictly linear on both sides. No merges. `4adecbb` is **not** an ancestor of our HEAD. |
| Host baseline today | `make check`: **22 suites, 111 tests, 0 failures** |
| Upstream remote | fetch-only; nothing pushed, no `origin` |

Files upstream changed since the fork: 23. Files **we** changed since the fork: 46.
Their intersection is 10, of which 7 conflict.

---

## 1. Classification of the 20 upstream commits

Impact is judged against **our** build arms — `make amix` (userspace, `-DODFS_PLATFORM_AMIX`)
and `make amix-kernel` (in-kernel cdfs, `-DODFS_PLATFORM_AMIX_KERNEL -D_KERNEL`). Note that
`CORE_SRCS` puts **every backend, including `cdda.c` and `udf.c`, into the in-kernel module**,
so "CD-audio feature we don't use" is not automatically "code we don't compile".

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

Nothing upstream is a *behavioural* threat to the Amix port. The whole cost is mechanical:
one structural refactor, one stack-hygiene regression, and a small C89 re-sweep.

---

## 2. Conflict analysis — measured, not guessed

`git merge-tree --write-tree HEAD upstream/main` produces tree `7f53977`. It reports
**9 conflict hunks across 7 files**; `Makefile`, `backends/iso9660/iso9660.h`,
`backends/joliet/joliet.h`, `include/odfs/node.h` and everything else auto-merge.

| File | Hunks | Conflicted lines | File size | Severity |
|---|---|---|---|---|
| `backends/joliet/joliet.c` | 2 | **461** | 565 | **Total rewrite** |
| `backends/udf/udf.c` | 1 | 43 | 1564 | Moderate |
| `backends/iso9660/iso9660.c` | 2 | 29 | 1202 | Mechanical |
| `backends/cdda/cdda.c` | 1 | 18 | 1405 | Mechanical |
| `tools/imgls/imgls.c` | 1 | 5 | 137 | Trivial |
| `tools/imgcat/imgcat.c` | 1 | 5 | 103 | Trivial |
| `tools/imginfo/imginfo.c` | 1 | 5 | 76 | Trivial |

### 2.1 `0d14de6` — the Joliet refactor (headline)

**What upstream did.** `joliet.c` drops from 604 to 108 lines. `joliet_context_t` is deleted;
a Joliet mount is now an `iso_context_t` with a new `int joliet` flag. Everything except
`probe` and `mount` becomes the shared `odfs_iso_*` ops, and mounting goes through a new
`odfs_iso_mount_vd(cache, log, session_start, vd_lba, joliet, root_out, backend_ctx)`.
`iso_parse_dir_record()` loses four parameters — `session_start`, `next_id`, `lowercase`,
`high_sierra` are read off `ctx` instead.

**Which of our work sits on it.** Six of our 32 commits touch these files:

| Our commit | What it does to the collision zone |
|---|---|
| `b28a9a2` C89 sweep | for-init and mixed declarations across `iso9660.c`, `joliet.c`, `cdda.c`, `udf.c`, `imgcat.c`, `imgls.c` — i.e. **all seven conflicted files** |
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
   6 of our C89 fixes evaporate.** This is a *good* outcome — see §2.5 — but it means
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
   `backends/iso9660/iso9660.h` auto-merges: upstream's added block of `odfs_iso_*` declarations
   and the new `joliet` context field land alongside our changed `odfs_iso_dir_stub` signature,
   because the two sides touched different lines. The `.c` call sites at upstream lines 1007/1034
   are *not* in upstream's changed range, so our out-param versions win there too. Result is
   consistent — but this is exactly the shape of merge that compiles by luck rather than by
   review. **It must be verified by building, not by reading the merge output.**

4. **The shared readdir now needs our stack fix, and Joliet inherits it free.** Upstream's
   post-refactor `iso9660.c` still has `odfs_node_t node;` (672 B) at lines 473 and 693, and
   `rr_info_t rr;` (**1184 B**) at lines 383, 517 and 704 — all on the stack. Our HEAD has none.
   After the merge, our `5c34112` heap-ification must be re-applied to `odfs_iso_readdir`,
   `odfs_iso_readlink` and `odfs_iso_resolve_parent` — and because those are now *shared*, Joliet
   gets kernel-stack safety without a second patch.

### 2.2 `340f620` — UDF allocation-descriptor chain

One conflict hunk, and it is the whole body of `udf_read()`: we hoisted its declarations for C89,
upstream replaced the body with a call to the new `udf_read_object()`. **Take upstream's side
unconditionally** — the old single-extent assumption is a silent-corruption bug.

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

### 2.3 `346c937` / `7f1df7d` — CD-Text

One conflict hunk in `cdda.c`: our hoisted `for (i = ...)` against upstream's new nested
`for (uint8_t type = 0x80; ...) { for (uint8_t block = 0; ...) }`. Take upstream, re-hoist.
Also adds `char cur[256]; char clean[256];` — 512 bytes of stack in kernel-compiled code.
Lower priority than the UDF buffer (the CDDA backend does not mount on the kernel arm today),
but it belongs on the same hygiene pass.

### 2.4 `cbd6611` / `7732531` — the tools

Three identical 5-line conflicts in the include block. Both includes are wanted:

```c
#include "odfs/printf.h"   /* ours  */
#include "../imgmount.h"   /* theirs */
```

Union-merge, done.

### 2.5 What does **not** collide — including everything load-bearing

Upstream touched none of these, so our work in them replays untouched:

- **`fa9db1a`** (`core/cache_block.c`, `core/session.c`, `include/odfs/alloc.h`) — the
  sector-aligned DMA buffers. **Zero upstream overlap.**
- **`31e8c3b`** (`platform/amix-kernel/amix_kern_media.c`) — the DMA page-cross contract.
  **Zero upstream overlap** (upstream has no `platform/amix-kernel/`).
- All of `platform/amix/` and `platform/amix-kernel/` (13 files).
- `core/mount.c`, `core/cache_meta.c`, `core/log.c`, `core/charset.c`, `core/namefix.c`,
  `include/odfs/{alloc,api,backend,cache,log,printf,config,ancestry,compat_stdint}.h`.
- `backends/hfs/`, `backends/hfsplus/`, `backends/rock_ridge/`.
- `tests/unit/test_remount.c`, `tests/unit/test_cdfs_blocks.c`, `docs/`.

**The page-aligned-DMA guarantees are structurally out of the blast radius.** They cannot be
lost to a conflict resolution; they can only be lost to a *rewrite* of history (see §4).

---

## 3. C89 debt — measured

Two constructs matter for `m68k-cbm-sysv4-gcc` (gcc 2.7.2.3): for-loop initial declarations
(hard **errors** under C89) and declaration-after-statement. Designated initializers
(121 sites tree-wide) are accepted as a GNU extension and are *not* an issue — our sweep
deliberately left them.

Actionable sites in the shared build (`core/` + `backends/` + the three cross-built tools):

| Tree | for-init + mixed-decl sites |
|---|---|
| fork point `00ae148` | 135 |
| `upstream/main` | 137 |
| our HEAD | **0** |

Per-file delta introduced by the 20 upstream commits: `cdda.c` **+8**, `joliet.c` **−5**
(the file shrinks), `udf.c` **−1**. **Net +2.**

So the post-merge C89 re-sweep is roughly **8 for-init hoists in the new CD-Text code plus a
handful of mixed declarations in `udf_read_object()`** — perhaps 30 minutes. The 119
anonymous-variadic-macro sites cost nothing: our fix is a single `__STDC_VERSION__` gate in
`include/odfs/log.h`, a file upstream did not touch.

(Our tree shows 3 residual for-init sites in `tools/imgbench`, `imgdump`, `imgstat` — host-only
tools, not in any `amix*` target. Harmless, but they are why upstream should get a C89 gate; see
§6.)

---

## 4. Recommended strategy

**Do not rebase our 32 commits onto `upstream/main`. Merge `upstream/main` into `main`.**

Three reasons, in order of weight:

1. **Rebase invalidates the metal proof chain.** The cdfs port's evidence is anchored to specific
   hashes: `fa9db1a` + `31e8c3b` are cited as the page-crossing-DMA fix in `docs/amix-known-unknowns.md`
   (`§1`, "cdfs at `31e8c3b`"), in the grimoire briefs, and in the workspace record of the
   15/15-cycle metal validation and the 8 byte-identical canary binaries. A rebase rewrites all 32
   hashes, and every one of those citations silently becomes a dangling reference to a commit that
   no longer exists. The proof would have to be re-anchored by hand, or re-earned on metal.

2. **Rebase replays five commits that were deliberately deleted.** `3beda3b`, `3fd67c1`,
   `aa41ea4`, `51540aa` and `9aadea5` are `TEMP`/`TEMP DIAG` instrumentation, removed again by
   `139aabb` and `a7da26a`. Under rebase each is replayed against the moving upstream base, so we
   would resolve conflicts in `cdfs_vfsops.c`/`cdfs_vnodeops.c` scaffolding that is then thrown
   away two commits later. Pure waste, and each resolution is a chance to corrupt the diagnostic
   narrative those commits preserve.

3. **A merge is exactly one conflict event, and it is already fully characterized.** §2 above
   *is* the resolution plan: 9 hunks, 7 files, one of which (`joliet.c`) is `--theirs` wholesale
   and three of which (`tools/`) are union-merges. Rebase would spread the same collisions across
   6 of our commits (`b28a9a2`, `702b2cb`, `5c34112`, `3420559`, `ad09035`, `2b57f88`), several of
   them repeatedly, because each replays against a base that already contains the refactor.

**Cherry-pick-forward is the wrong tool here** (it is a rebase with extra steps and the same hash
churn), but it *is* the right tool for the eventual upstream PR — see §6. Keep the two concerns
separate: `main` gets a merge and keeps its provenance; the PR branch gets a curated replay onto
`upstream/main` at PR time, when the shape of the offer is known.

---

## 5. Staged plan

### Packet A — mechanical merge, host green
1. Branch from `main` (`upstream-sync-2026-07`); `git merge upstream/main`.
2. Resolve in this order, cheapest-first so the tree compiles early:
   1. `tools/imgcat|imgls|imginfo` — union the include blocks (3 × 5 lines).
   2. `backends/joliet/joliet.c` — **`--theirs`, wholesale.** Do not hand-merge.
   3. `backends/iso9660/iso9660.c` — 2 call sites onto upstream's `iso_parse_dir_record(rec, avail, ctx, ...)`, keeping our hoisted declarations and heap `node`.
   4. `backends/udf/udf.c` — `--theirs` for the `udf_read()` body.
   5. `backends/cdda/cdda.c` — `--theirs`, then re-hoist the loop variables.
3. Re-apply the C89 sweep to whatever upstream lines won (§3: ~8 sites, `cdda.c` + `udf.c`).
4. `make host && make check` must return to **22 suites / 117 tests / 0 failures** — our 111
   today plus the 6 cases upstream adds to existing suites (`test_udf.c` +4, `test_cdda.c` +1,
   `test_file_media.c` +1). Upstream adds no new suite *files*, and the two suites we added
   (`test_remount.c`, `test_cdfs_blocks.c`) are ours alone, so the file set merges without
   conflict. A count below 117 means a case was lost in resolution.
5. **Explicitly verify `odfs_iso_dir_stub`** — the header/`.c` seam that auto-merged (§2.1.3).
   A clean compile is the proof; a clean-looking diff is not.

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
with real uncertainty, and §2 bounds it: 9 hunks, of which 4 are `--theirs` or union and 2 are
a named two-line signature change.

### 5.1 Regression evidence required

The port has bench **and** metal proof today. The merge touches `iso9660.c`, `udf.c` and `cdda.c`
— all three in the in-kernel module — so the following must be **re-proven**, not assumed:

| What | Why it is at risk | Evidence required |
|---|---|---|
| `mount -F cdfs 0,2 /cdrom` on metal | shared readdir/mount path rewritten by `0d14de6` | mount succeeds; `ls -la /cdrom` enumerates; a read-back file is byte-identical to the host original |
| **Page-aligned DMA guarantees (`fa9db1a` + `31e8c3b`)** | not conflicted (§2.5), but `udf_read_object()` newly feeds arbitrarily-offset buffers into the streaming path at `cache_block.c:592` | ≥15 mount/read/unmount cycles with the 8 canary binaries byte-identical, as in the original `31e8c3b` validation. **This is the one non-negotiable gate** — it is the fix that stopped cdfs corrupting `/usr/sbin/umount` and libc. |
| Kernel-stack headroom | `ad_buf[2048]` + shared-readdir stack nodes | m68k frame sizes on mount/readdir/lookup/readlink/resolve_parent, compared against the numbers in `702b2cb` and `5c34112` |
| Remount / heap churn | `3ed0285` single-slab cache untouched but the callers moved | remount cycle clean, no wild-pointer frees |
| `st_blocks` (`12b5a97`, `8de83cf`) | `__udivdi3` path in `cdfs_getattr` | `ls -s` sane on metal, per the closed known-unknown §1 |
| UDF multi-extent (**new capability**) | `340f620` is why we want this merge | a >1 GiB or deliberately fragmented UDF image read correctly — coverage we have never had |

Until Packet D passes, the merged branch is **not** a replacement for the proven `8de83cf`
kernel on metal.

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
inside `platform/amix/` if at all).

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

**Not in `upstream/main`** — none of the 20 commits touches `action_parent`, and upstream's
`platform/amiga/handler_main.c` still dispatches it unchanged at line 4439. We could not inspect
the PR itself: only `upstream/main` and `upstream/amigaos4.1fe` are fetched, and this task was
scoped no-fetch. So what follows is about *where it would land*, not what it contains.

**Do we diverge around it? No.** We have never touched `platform/amiga/` — `git diff 00ae148 HEAD
-- platform/amiga` is empty. Our `..` handling lives in `platform/amix-kernel/cdfs_vnodeops.c`,
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

```sh
R=/home/jalanara/Devel/Omat/Amiga/Amix/amix-cdfs

# topology
git -C $R merge-base HEAD upstream/main                  # 00ae148
git -C $R rev-list --count --left-right upstream/main...HEAD   # 20  32
git -C $R log --reverse --format='%h %ad %s' --date=short --name-only 00ae148..upstream/main

# conflict evidence (§2) — writes a tree object, touches no working file
git -C $R merge-tree --write-tree --name-only HEAD upstream/main   # -> 7f53977
git -C $R show 7f53977:backends/joliet/joliet.c | grep -c '^<<<<<<<'

# C89 debt (§3) — three read-only extracts, nothing written into the repo
git -C $R archive 00ae148     | tar -x -C /tmp/base
git -C $R archive upstream/main | tar -x -C /tmp/up
git -C $R archive HEAD        | tar -x -C /tmp/ours
gcc -std=gnu89 -Wdeclaration-after-statement -fsyntax-only \
    -I <tree>/include -I <tree>/backends -I <tree>/tools <tree>/backends/cdda/cdda.c
grep -cE 'for *\( *(u?int|size_t|char|long|unsigned)' <tree>/backends/*/*.c

# struct sizes (§1, §2.1)
# ODFS_NAME_MAX=512  sizeof(odfs_node_t)=672  sizeof(rr_info_t)=1184

# host baseline (§0)
make -C $R host && make -C $R check    # 22 suites, 111 tests, 0 failures
```
