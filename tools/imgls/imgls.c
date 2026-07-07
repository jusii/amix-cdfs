/*
 * imgls — list directories in an image
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "odfs/api.h"
#include "odfs/printf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct imgls_opts {
    int show_amiga;
    odfs_mount_t *mnt;
    const odfs_node_t *dir;
} imgls_opts_t;

static odfs_err_t print_entry(const odfs_node_t *entry, void *ctx)
{
    const imgls_opts_t *opts = ctx;
    const char *kind = odfs_node_kind_name(entry->kind);
    char sizebuf[24];

    /*
     * Render the 64-bit size through the tree's bounded formatter rather
     * than passing "%llu" straight to libc printf: the AMIX (SVR4) libc
     * printf predates C99 and does not understand the "ll" length modifier,
     * so it would consume only 4 of the argument's 8 bytes and then misread
     * the following "%s" pointer — a segfault on m68k. Behaviour-identical
     * to "%10llu" on host/Amiga (odfs_snprintf routes to vsnprintf there).
     */
    odfs_snprintf(sizebuf, sizeof(sizebuf), "%llu",
                  (unsigned long long)entry->size);

    if (entry->kind == ODFS_NODE_SYMLINK && opts && opts->mnt) {
        char target[512];

        if (odfs_readlink(opts->mnt, opts->dir, entry->name,
                          target, sizeof(target)) == ODFS_OK) {
            printf("%-7s %10s  %s -> %s\n", kind,
                   sizebuf, entry->name, target);
            return ODFS_OK;
        }
    }

    printf("%-7s %10s  %s\n", kind, sizebuf, entry->name);
    if (opts && opts->show_amiga) {
        if (entry->amiga_as.has_protection) {
            printf("                     amiga=%02x %02x %02x %02x\n",
                   entry->amiga_as.protection[0], entry->amiga_as.protection[1],
                   entry->amiga_as.protection[2], entry->amiga_as.protection[3]);
        }
        if (entry->amiga_as.has_comment && entry->amiga_as.comment[0] != '\0')
            printf("                     comment=%s\n", entry->amiga_as.comment);
    }
    return ODFS_OK;
}

int main(int argc, char **argv)
{
    odfs_media_t media;
    odfs_mount_t mnt;
    odfs_mount_opts_t opts;
    odfs_log_state_t log;
    odfs_err_t err;
    const char *image = NULL;
    const char *path = "/";
    int force_udf = 0, force_hfs = 0;
    imgls_opts_t ls_opts;
    int i;
    odfs_node_t dir;

    memset(&ls_opts, 0, sizeof(ls_opts));

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--udf") == 0)
            force_udf = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--hfs") == 0)
            force_hfs = 1;
        else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--amiga") == 0)
            ls_opts.show_amiga = 1;
        else if (!image)
            image = argv[i];
        else
            path = argv[i];
    }

    if (!image) {
        fprintf(stderr, "usage: imgls [-u|-h] [-a] <image> [path]\n");
        return 1;
    }

    err = odfs_media_open_image(image, &media);
    if (err != ODFS_OK) {
        fprintf(stderr, "error: cannot open '%s': %s\n",
                image, odfs_err_str(err));
        return 1;
    }

    odfs_log_init(&log);
    odfs_mount_opts_default(&opts);
    if (force_udf)
        opts.prefer_udf = 1;
    if (force_hfs)
        opts.prefer_hfs = 1;

    err = odfs_mount(&media, &opts, &log, &mnt);
    if (err != ODFS_OK) {
        fprintf(stderr, "error: mount failed: %s\n", odfs_err_str(err));
        odfs_media_close(&media);
        return 1;
    }

    err = odfs_resolve_path(&mnt, path, &dir);
    if (err != ODFS_OK) {
        fprintf(stderr, "error: path '%s': %s\n", path, odfs_err_str(err));
        odfs_unmount(&mnt);
        return 1;
    }

    ls_opts.mnt = &mnt;
    ls_opts.dir = &dir;
    err = odfs_readdir(&mnt, &dir, print_entry, &ls_opts, NULL);
    if (err != ODFS_OK) {
        fprintf(stderr, "error: readdir: %s\n", odfs_err_str(err));
        odfs_unmount(&mnt);
        return 1;
    }

    odfs_unmount(&mnt);
    return 0;
}
