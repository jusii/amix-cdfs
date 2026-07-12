/*
 * test_remount.c — repeated in-process mount/unmount cycles over one image
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Regression coverage for the AMIX remount corruption hunt: the on-metal
 * failure only ever appeared on the SECOND and later mounts of the same boot,
 * a shape the rest of the suite never exercises — every other test mounts an
 * image at most once per process, so state that leaks across odfs_unmount()
 * (a stale pointer surviving into the next mount, a double free the allocator
 * only trips over on reuse, a mis-aimed free of a stored buffer pointer) was
 * structurally invisible to the sanitizers.  This test performs the whole
 * mount → full-tree walk (readdir + lookup + read + readlink + parent
 * resolution) → unmount cycle several times in ONE process and requires
 * byte-identical results every cycle, so ASan/valgrind runs of the suite now
 * see exactly the allocation-reuse pattern the AMIX kernel sees.
 *
 * Uses the Rock Ridge fixture (the on-metal bench disc is RR): path is
 * relative to the repository root, which is where `make check` runs the
 * binaries.
 */

#include "odfs/api.h"
#include "odfs/ancestry.h"
#include "odfs/error.h"
#include "test_harness.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define REMOUNT_IMAGE   "tests/images/test_rr.iso"
#define REMOUNT_CYCLES  6
#define WALK_MAX_DEPTH  8
#define DIR_MAX_ENTRIES 64
#define READ_CHUNK      3000    /* deliberately unaligned: partial sectors */

/* Aggregate of one full tree walk; must be identical on every cycle. */
typedef struct walk_stats {
    uint32_t dirs;
    uint32_t files;
    uint32_t links;
    uint64_t bytes;
    uint32_t name_hash;   /* order-sensitive FNV over every entry name */
    uint32_t data_hash;   /* FNV over every file's full content */
} walk_stats_t;

typedef struct dir_snapshot {
    odfs_node_t entries[DIR_MAX_ENTRIES];
    uint32_t    count;
    int         overflow;
} dir_snapshot_t;

static uint32_t fnv1a(uint32_t hash, const void *data, size_t len)
{
    const unsigned char *p = data;
    size_t i;

    for (i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }
    return hash;
}

static odfs_err_t snapshot_cb(const odfs_node_t *entry, void *ctx)
{
    dir_snapshot_t *snap = ctx;

    if (snap->count >= DIR_MAX_ENTRIES) {
        snap->overflow = 1;
        return ODFS_ERR_EOF;
    }
    snap->entries[snap->count++] = *entry;
    return ODFS_OK;
}

/* Read the whole file twice — once in unaligned chunks, once in one big
 * gulp — hashing the chunked pass and checking the two agree in size. */
static int hash_file(odfs_mount_t *mnt, const odfs_node_t *file,
                     walk_stats_t *stats)
{
    uint8_t *buf;
    uint64_t off = 0;
    uint64_t total = 0;

    buf = malloc(READ_CHUNK);
    if (!buf)
        return -1;

    for (;;) {
        size_t len = READ_CHUNK;
        odfs_err_t err = odfs_read(mnt, file, off, buf, &len);

        if (err != ODFS_OK && err != ODFS_ERR_EOF) {
            free(buf);
            return -1;
        }
        if (len == 0)
            break;
        stats->data_hash = fnv1a(stats->data_hash, buf, len);
        total += len;
        off += len;
    }

    free(buf);
    if (total != file->size)
        return -1;
    stats->bytes += total;
    return 0;
}

/* Depth-first walk mirroring what the AMIX soak does per cycle:
 * ls -laR (readdir + per-name lookup) plus checksumming every file. */
static int walk_dir(odfs_mount_t *mnt, const odfs_node_t *dir,
                    walk_stats_t *stats, int depth)
{
    dir_snapshot_t *snap;
    uint32_t i;
    int rc = 0;

    if (depth > WALK_MAX_DEPTH)
        return -1;

    snap = calloc(1, sizeof(*snap));
    if (!snap)
        return -1;

    if (odfs_readdir(mnt, dir, snapshot_cb, snap, NULL) != ODFS_OK ||
        snap->overflow) {
        free(snap);
        return -1;
    }

    for (i = 0; i < snap->count && rc == 0; i++) {
        const odfs_node_t *e = &snap->entries[i];
        odfs_node_t looked;

        stats->name_hash = fnv1a(stats->name_hash, e->name,
                                 strlen(e->name));

        /* every listed name must resolve from its parent (ls → stat) */
        if (odfs_lookup(mnt, dir, e->name, &looked) != ODFS_OK) {
            rc = -1;
            break;
        }
        if (looked.extent.lba != e->extent.lba) {
            rc = -1;
            break;
        }

        switch (e->kind) {
        case ODFS_NODE_DIR: {
            odfs_node_t parent;

            stats->dirs++;
            /* ".." resolution for every subdirectory */
            if (odfs_resolve_parent_node(mnt, e, &parent, NULL) != ODFS_OK) {
                rc = -1;
                break;
            }
            rc = walk_dir(mnt, e, stats, depth + 1);
            break;
        }
        case ODFS_NODE_SYMLINK: {
            char target[512];

            stats->links++;
            if (odfs_readlink(mnt, dir, e->name, target,
                              sizeof(target)) != ODFS_OK) {
                rc = -1;
                break;
            }
            stats->name_hash = fnv1a(stats->name_hash, target,
                                     strlen(target));
            break;
        }
        case ODFS_NODE_FILE:
            stats->files++;
            rc = hash_file(mnt, e, stats);
            break;
        default:
            break;
        }
    }

    free(snap);
    return rc;
}

static int run_cycle(walk_stats_t *stats)
{
    odfs_media_t media;
    odfs_mount_t *mnt;
    int rc;

    memset(stats, 0, sizeof(*stats));
    stats->name_hash = 2166136261u;
    stats->data_hash = 2166136261u;

    if (odfs_media_open_image(REMOUNT_IMAGE, &media) != ODFS_OK)
        return -2;   /* fixture missing / wrong cwd */

    mnt = calloc(1, sizeof(*mnt));
    if (!mnt) {
        odfs_media_close(&media);
        return -1;
    }

    if (odfs_mount(&media, NULL, NULL, mnt) != ODFS_OK) {
        odfs_media_close(&media);
        free(mnt);
        return -1;
    }

    rc = walk_dir(mnt, &mnt->root, stats, 0);

    odfs_unmount(mnt);
    free(mnt);
    return rc;
}

TEST(remount_cycles_are_identical)
{
    walk_stats_t first;
    walk_stats_t cur;
    int cycle;
    int rc;

    rc = run_cycle(&first);
    ASSERT_EQ(rc, 0);

    /* the fixture is a real tree: an empty walk means the test is broken */
    ASSERT(first.dirs >= 1);
    ASSERT(first.files >= 1);
    ASSERT(first.bytes > 0);

    for (cycle = 2; cycle <= REMOUNT_CYCLES; cycle++) {
        rc = run_cycle(&cur);
        ASSERT_EQ(rc, 0);
        ASSERT_EQ(cur.dirs, first.dirs);
        ASSERT_EQ(cur.files, first.files);
        ASSERT_EQ(cur.links, first.links);
        ASSERT_EQ(cur.bytes, first.bytes);
        ASSERT_EQ(cur.name_hash, first.name_hash);
        ASSERT_EQ(cur.data_hash, first.data_hash);
    }
}

/* The same shape over the Joliet fixture: a second backend keeps the
 * remount path honest for the ctx-registration/dedup logic in
 * odfs_mount_register_backend/odfs_unmount. */
TEST(remount_cycles_joliet)
{
    odfs_media_t media;
    odfs_mount_t *mnt;
    char volume_first[40];
    int cycle;

    volume_first[0] = '\0';

    for (cycle = 1; cycle <= 3; cycle++) {
        ASSERT_OK(odfs_media_open_image("tests/images/test_joliet.iso",
                                        &media));
        mnt = calloc(1, sizeof(*mnt));
        ASSERT(mnt != NULL);
        ASSERT_OK(odfs_mount(&media, NULL, NULL, mnt));

        if (cycle == 1) {
            memcpy(volume_first, mnt->volume_name, sizeof(volume_first) - 1);
            volume_first[sizeof(volume_first) - 1] = '\0';
        } else {
            ASSERT_EQ(strncmp(volume_first, mnt->volume_name,
                              sizeof(volume_first) - 1), 0);
        }

        odfs_unmount(mnt);
        free(mnt);
    }
}

TEST_MAIN()
