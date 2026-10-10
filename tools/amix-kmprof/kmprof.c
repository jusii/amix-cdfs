/*
 * kmprof.c — host-side kmem cost profile of the portable core, as the AMIX
 * kernel arm would pay it. See docs/amix-kmem-oversize.md.
 *
 * Wraps malloc/calloc/free (link with --wrap), runs mount -> full tree walk
 * (readdir, lookup, 8 KiB reads) -> unmount N times over an image, and maps
 * every request onto the kernel arm of odfs/alloc.h: odfs_malloc adds an
 * 8-byte header; odfs_malloc_aligned(s, a) asks for s + a + 4 + 8 on m68k
 * (host: s + a + 8, recognised as size % 2048 == 8). A request over 4096
 * bytes is stock-kmem "oversize": ceil(req / 4096) contiguous sptmap pages.
 * Not part of `make`; build by hand from the repository root:
 *
 *   cc -std=gnu11 -O1 -I include -I backends -o /tmp/kmprof \
 *      tools/amix-kmprof/kmprof.c $(find core backends -name '*.c') \
 *      platform/host/file_media.c -Wl,--wrap=malloc,--wrap=calloc,--wrap=free
 *   /tmp/kmprof image.iso 8 32
 *
 * The optional third argument is the mount's cache_blocks (0 or absent =
 * the core default, 128); the kernel cdfs_mount passes CDFS_CACHE_BLOCKS.
 * Exit status is nonzero if any allocation is still live after the last
 * unmount. Meta-dir blob sizes are reported with the m68k struct sizes
 * (odfs_meta_dir_t 40, odfs_meta_entry_t 72).
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "odfs/api.h"
#include "odfs/cache_meta.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

odfs_err_t odfs_media_open_image(const char *path, odfs_media_t *out);

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void  __real_free(void *);

#define NT 65536u
static struct { void *p; size_t sz; } tab[NT];
static long live_n, live_bytes;
static long ov_live_pages, ov_peak_pages, ov_max_req, ov_allocs, total_allocs;
static int  tracking;

static long kreq_of(size_t host)
{
    if (host >= 2056 && ((host - 8) % 2048) == 0)
        return (long)host + 4;      /* aligned: m68k ptr 4 (not 8) + hdr 8 */
    return (long)host + 8;          /* plain odfs_malloc header */
}

static long pages4k(long k)
{
    return k > 4096 ? (k + 4095) / 4096 : 0;
}

static unsigned slot(const void *p)
{
    return (unsigned)(((uintptr_t)p >> 4) % NT);
}

static void track_add(void *p, size_t sz)
{
    unsigned h;
    long k, pg;

    if (!p || !tracking)
        return;
    for (h = slot(p); tab[h].p; h = (h + 1) % NT)
        ;
    tab[h].p = p;
    tab[h].sz = sz;
    live_n++;
    live_bytes += (long)sz;
    total_allocs++;
    k = kreq_of(sz);
    pg = pages4k(k);
    if (pg) {
        ov_allocs++;
        ov_live_pages += pg;
        if (ov_live_pages > ov_peak_pages)
            ov_peak_pages = ov_live_pages;
        if (k > ov_max_req)
            ov_max_req = k;
    }
}

static void track_del(void *p)
{
    unsigned h, j;

    if (!p)
        return;
    for (h = slot(p); tab[h].p && tab[h].p != p; h = (h + 1) % NT)
        ;
    if (tab[h].p != p)
        return;                     /* not tracked */
    live_n--;
    live_bytes -= (long)tab[h].sz;
    ov_live_pages -= pages4k(kreq_of(tab[h].sz));
    tab[h].p = NULL;
    /* re-insert the rest of the probe cluster */
    for (j = (h + 1) % NT; tab[j].p; j = (j + 1) % NT) {
        void *q = tab[j].p;
        size_t s = tab[j].sz;
        unsigned k;

        tab[j].p = NULL;
        for (k = slot(q); tab[k].p; k = (k + 1) % NT)
            ;
        tab[k].p = q;
        tab[k].sz = s;
    }
}

void *__wrap_malloc(size_t n)
{
    void *p = __real_malloc(n);
    track_add(p, n);
    return p;
}

void *__wrap_calloc(size_t a, size_t b)
{
    void *p = __real_calloc(a, b);
    track_add(p, a * b);
    return p;
}

void __wrap_free(void *p)
{
    track_del(p);
    __real_free(p);
}

static odfs_mount_t *M;
static char rbuf[8192];             /* CDFS_RDBUF */
static long files, dirs, bytes;

typedef struct { odfs_node_t *v; int n, cap; } list_t;

static odfs_err_t collect(const odfs_node_t *e, void *ctx)
{
    list_t *l = ctx;

    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->v = realloc(l->v, sizeof(*l->v) * (size_t)l->cap);
        if (!l->v)
            return ODFS_ERR_NOMEM;
    }
    l->v[l->n++] = *e;
    return ODFS_OK;
}

static void walk(const odfs_node_t *dir, int depth)
{
    list_t l = { 0, 0, 0 };
    int i;

    odfs_readdir(M, dir, collect, &l, NULL);
    dirs++;
    for (i = 0; i < l.n; i++) {
        odfs_node_t n;

        if (odfs_lookup(M, dir, l.v[i].name, &n) != ODFS_OK)
            n = l.v[i];
        if (n.kind == ODFS_NODE_DIR) {
            if (depth < 16)
                walk(&n, depth + 1);
        } else if (n.kind == ODFS_NODE_FILE) {
            uint64_t off = 0;

            files++;
            for (;;) {
                size_t len = sizeof(rbuf);
                odfs_err_t e = odfs_read(M, &n, off, rbuf, &len);

                if ((e != ODFS_OK && e != ODFS_ERR_EOF) || len == 0)
                    break;
                off += len;
                bytes += (long)len;
            }
        }
    }
    free(l.v);
}

int main(int argc, char **argv)
{
    int cycles, c;
    odfs_mount_opts_t opts;

    if (argc < 2) {
        fprintf(stderr, "usage: %s image [cycles [cache_blocks]]\n",
                argv[0]);
        return 2;
    }
    cycles = argc > 2 ? atoi(argv[2]) : 8;
    odfs_mount_opts_default(&opts);
    opts.cache_blocks = argc > 3 ? (uint32_t)atoi(argv[3]) : 0;
    M = __real_calloc(1, sizeof(*M));

    for (c = 1; c <= cycles; c++) {
        odfs_media_t media;

        tracking = 1;
        if (odfs_media_open_image(argv[1], &media) != ODFS_OK ||
            odfs_mount(&media, &opts, NULL, M) != ODFS_OK) {
            fprintf(stderr, "open/mount failed\n");
            return 2;
        }
        if (c == 1)
            printf("after mount: live=%ld bytes=%ld oversize_pages=%ld\n",
                   live_n, live_bytes, ov_live_pages);
        files = dirs = bytes = 0;
        walk(&M->root, 0);
        if (c == 1) {
            odfs_meta_dir_t *d;
            long tot = 0;

            printf("walk: dirs=%ld files=%ld bytes=%ld\n", dirs, files, bytes);
            printf("after walk: live=%ld oversize_pages=%ld\n",
                   live_n, ov_live_pages);
            for (d = M->meta.lru_head; d; d = d->lru_next) {
                long pool = (long)d->bytes - (long)sizeof(*d) -
                            (long)d->count * (long)sizeof(odfs_meta_entry_t);
                long k = 8 + 40 + 72L * (long)d->count + pool;

                tot += k;
                printf("  dir lba=%u n=%u names=%ld m68k_req=%ld pages=%ld\n",
                       (unsigned)d->dir_lba, (unsigned)d->count, pool, k,
                       pages4k(k));
            }
            printf("  meta total m68k=%ld budget=%u\n", tot,
                   (unsigned)M->meta.budget);
        }
        odfs_unmount(M);
        printf("cycle %d after unmount: live=%ld bytes=%ld oversize_pages=%ld\n",
               c, live_n, live_bytes, ov_live_pages);
    }
    printf("allocs=%ld oversize=%ld (%.1f/cycle) peak_oversize_pages=%ld "
           "max_req=%ld\n", total_allocs, ov_allocs,
           (double)ov_allocs / cycles, ov_peak_pages, ov_max_req);
    return live_n != 0;
}
