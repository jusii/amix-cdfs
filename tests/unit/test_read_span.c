/*
 * test_read_span.c — file reads that span a 4 KiB boundary never come back
 * short before end of file.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * A read-only filesystem must behave like a stock SVR4 one for a regular
 * file: a read returns min(request, size - offset) bytes, never fewer, so a
 * reader that takes a short count as end of file still sees the whole file.
 * Readers in the field use every shape of request: ftpd issues one 8 KiB
 * read() per buffer, dd uses its bs, stdio its BUFSIZ. The 68040/68060
 * kernels run 4 KiB pages while the 68030 ones run 2 KiB pages, so the
 * requests here deliberately straddle offset 4096 from both sides.
 *
 * The media mock models the AMIX in-kernel SCSI backend
 * (platform/amix-kernel/amix_kern_media.c): one 2048-byte sector per
 * transfer, a bounce sector for a target that is not aligned to the
 * compile-time NBPP (2048), and a check that each modeled DMA stays inside
 * one page of the RUNTIME page size. Running it at 2048 and at 4096 shows
 * that the 2048-granular no-cross rule the object is built with also holds
 * on a 4 KiB-page kernel (a 2048-aligned sector never crosses a 4 KiB page).
 */

#include "iso9660/iso9660.h"
#include "odfs/api.h"
#include "odfs/error.h"
#include "test_harness.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RS_SECTORS      96
#define RS_ROOT_LBA     20
#define RS_FIRST_LBA    21
#define RS_NBPP_BUILD   2048u   /* the NBPP the kernel object compiles with */
#define RS_RDBUF        8192u   /* cdfs_read's kmem staging buffer */

/* sizes around the 4 KiB boundary, plus one either side of 8 KiB */
static const uint32_t rs_sizes[] = {
    4095, 4096, 4097, 4152, 4448, 4744, 5040, 6144, 8191, 8192, 8193, 12289
};
#define RS_NFILES (sizeof(rs_sizes) / sizeof(rs_sizes[0]))

typedef struct rs_media {
    uint8_t  sectors[RS_SECTORS][ISO_SECTOR_SIZE];
    uint32_t page;          /* runtime page size for the DMA check */
    int      violations;    /* modeled DMAs that crossed a runtime page */
} rs_media_t;

static uint8_t rs_byte(uint32_t file, uint32_t pos)
{
    return (uint8_t)(pos * 131u + (pos >> 8) + file * 7u + 1u);
}

static odfs_err_t rs_read_sectors(void *ctx, uint32_t lba, uint32_t count,
                                  void *buf)
{
    rs_media_t *m = ctx;
    uint8_t    *out = buf;
    uint8_t    *bounce = NULL;

    if (lba + count > RS_SECTORS)
        return ODFS_ERR_IO;

    while (count != 0) {
        uint8_t *dst = out;

        if (((uintptr_t)out & (RS_NBPP_BUILD - 1u)) != 0) {
            if (!bounce)
                bounce = aligned_alloc(RS_NBPP_BUILD, ISO_SECTOR_SIZE);
            if (!bounce)
                return ODFS_ERR_NOMEM;
            dst = bounce;
        }
        if (((uintptr_t)dst & (m->page - 1u)) + ISO_SECTOR_SIZE > m->page)
            m->violations++;
        memcpy(dst, m->sectors[lba], ISO_SECTOR_SIZE);
        if (dst == bounce)
            memcpy(out, bounce, ISO_SECTOR_SIZE);
        out += ISO_SECTOR_SIZE;
        lba++;
        count--;
    }
    free(bounce);
    return ODFS_OK;
}

static uint32_t rs_sector_size(void *ctx)
{
    (void)ctx;
    return ISO_SECTOR_SIZE;
}

static uint32_t rs_sector_count(void *ctx)
{
    (void)ctx;
    return RS_SECTORS;
}

static const odfs_media_ops_t rs_media_ops = {
    .read_sectors = rs_read_sectors,
    .sector_size = rs_sector_size,
    .sector_count = rs_sector_count,
};

static void rs_wr_bb32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
    p[4] = (uint8_t)(v >> 24);
    p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);
    p[7] = (uint8_t)v;
}

static void rs_wr_bb16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static size_t rs_dir_record(uint8_t *p, uint32_t lba, uint32_t size,
                            uint8_t flags, const char *name, size_t name_len)
{
    size_t rec_len = 33 + name_len + ((name_len & 1) == 0 ? 1 : 0);

    memset(p, 0, rec_len);
    p[0] = (uint8_t)rec_len;
    rs_wr_bb32(&p[ISO_DR_EXTENT_LBA], lba);
    rs_wr_bb32(&p[ISO_DR_DATA_LENGTH], size);
    p[ISO_DR_DATE + 0] = 95;
    p[ISO_DR_DATE + 1] = 6;
    p[ISO_DR_DATE + 2] = 1;
    p[ISO_DR_FLAGS] = flags;
    rs_wr_bb16(&p[ISO_DR_VOLUME_SEQ_NUM], 1);
    p[ISO_DR_NAME_LEN] = (uint8_t)name_len;
    memcpy(&p[ISO_DR_NAME], name, name_len);
    return rec_len;
}

static void rs_name(char *out, uint32_t i)
{
    /* "F00.DAT;1" .. "F11.DAT;1" */
    memcpy(out, "F00.DAT;1", 10);
    out[1] = (char)('0' + i / 10u);
    out[2] = (char)('0' + i % 10u);
}

static void rs_build_image(rs_media_t *m, uint32_t page)
{
    uint8_t *pvd = m->sectors[ISO_VD_START_LBA];
    uint8_t *term = m->sectors[ISO_VD_START_LBA + 1];
    uint8_t *root = m->sectors[RS_ROOT_LBA];
    uint32_t lba = RS_FIRST_LBA;
    size_t   off = 0;
    uint32_t i;
    uint32_t pos;
    char     name[10];

    memset(m, 0, sizeof(*m));
    m->page = page;

    pvd[ISO_PVD_TYPE] = ISO_VD_TYPE_PRIMARY;
    memcpy(&pvd[ISO_PVD_ID], ISO_STANDARD_ID, ISO_STANDARD_ID_LEN);
    pvd[ISO_PVD_VERSION] = 1;
    memset(&pvd[ISO_PVD_SYSTEM_ID], ' ', 32);
    memset(&pvd[ISO_PVD_VOLUME_ID], ' ', 32);
    memcpy(&pvd[ISO_PVD_VOLUME_ID], "RSTEST", 6);
    rs_wr_bb32(&pvd[ISO_PVD_VOLUME_SPACE_SIZE], RS_SECTORS);
    rs_wr_bb16(&pvd[ISO_PVD_LOGICAL_BLK_SIZE], ISO_SECTOR_SIZE);
    rs_dir_record(&pvd[ISO_PVD_ROOT_DIR_RECORD], RS_ROOT_LBA,
                  ISO_SECTOR_SIZE, ISO_DR_FLAG_DIRECTORY, "\x00", 1);

    term[ISO_PVD_TYPE] = ISO_VD_TYPE_TERM;
    memcpy(&term[ISO_PVD_ID], ISO_STANDARD_ID, ISO_STANDARD_ID_LEN);
    term[ISO_PVD_VERSION] = 1;

    off += rs_dir_record(&root[off], RS_ROOT_LBA, ISO_SECTOR_SIZE,
                         ISO_DR_FLAG_DIRECTORY, "\x00", 1);
    off += rs_dir_record(&root[off], RS_ROOT_LBA, ISO_SECTOR_SIZE,
                         ISO_DR_FLAG_DIRECTORY, "\x01", 1);

    for (i = 0; i < RS_NFILES; i++) {
        rs_name(name, i);
        off += rs_dir_record(&root[off], lba, rs_sizes[i], 0, name, 9);
        for (pos = 0; pos < rs_sizes[i]; pos++)
            m->sectors[lba + pos / ISO_SECTOR_SIZE][pos % ISO_SECTOR_SIZE] =
                rs_byte(i, pos);
        lba += (rs_sizes[i] + ISO_SECTOR_SIZE - 1) / ISO_SECTOR_SIZE;
    }
}

static uint32_t rs_expect(uint32_t size, uint32_t off, uint32_t len)
{
    if (off >= size)
        return 0;
    return (size - off < len) ? size - off : len;
}

/* bytes [off, off+len) of file i must match the image pattern */
static int rs_content_ok(uint32_t i, uint32_t off, const uint8_t *buf,
                         uint32_t len)
{
    uint32_t k;

    for (k = 0; k < len; k++) {
        if (buf[k] != rs_byte(i, off + k))
            return 0;
    }
    return 1;
}

/*
 * One read(2) of `len` bytes at `off`, modeled on cdfs_read(): stage through
 * an RS_RDBUF kernel buffer (page-aligned, like kmem_alloc of 8 KiB, or
 * shifted by `skew` bytes to force the bounce path), calling odfs_read until
 * the request is satisfied or odfs_read reports end of file. Returns the
 * byte count read(2) would return, or -1 on an error. Each odfs_read call
 * must itself return exactly min(request, size - offset): a short return
 * before EOF is reported through *short_seen.
 */
static long rs_read(odfs_mount_t *mnt, const odfs_node_t *file,
                    uint32_t off, uint8_t *user, uint32_t len,
                    uint8_t *kbuf, int *short_seen)
{
    uint32_t done = 0;

    while (done < len) {
        size_t     chunk = len - done;
        uint32_t   want;
        odfs_err_t err;

        if (chunk > RS_RDBUF)
            chunk = RS_RDBUF;
        want = rs_expect((uint32_t)file->size, off + done, (uint32_t)chunk);
        err = odfs_read(mnt, file, off + done, kbuf, &chunk);
        if (err != ODFS_OK && err != ODFS_ERR_EOF)
            return -1;
        if (chunk != want)
            (*short_seen)++;
        if (chunk == 0)
            break;
        memcpy(user + done, kbuf, chunk);
        done += (uint32_t)chunk;
    }
    return (long)done;
}

static void rs_run(int *_fail_count, uint32_t page, size_t skew)
{
    static rs_media_t media;
    static const uint32_t bss[] = { 512, 1024, 2048, 4096, 8192 };
    static const uint32_t ds[] = { 1, 7, 648, 2047, 2048, 2049 };
    odfs_media_t m;
    odfs_mount_t mnt;
    uint8_t     *kraw;
    uint8_t     *kbuf;
    uint8_t     *user;
    uint32_t     i;
    int          short_seen = 0;

    rs_build_image(&media, page);
    m.ops = &rs_media_ops;
    m.ctx = &media;
    ASSERT_OK(odfs_mount(&m, NULL, NULL, &mnt));

    kraw = aligned_alloc(4096, RS_RDBUF + 4096);
    user = malloc(16384);
    ASSERT(kraw != NULL && user != NULL);
    kbuf = kraw + skew;

    for (i = 0; i < RS_NFILES; i++) {
        odfs_node_t file;
        char        path[10];
        uint32_t    size = rs_sizes[i];
        uint32_t    off;
        uint32_t    b;
        long        got;

        rs_name(path, i);
        path[7] = '\0';                 /* drop ";1" */
        ASSERT_OK(odfs_resolve_path(&mnt, path, &file));
        ASSERT_EQ(file.size, size);

        /* ftpd's RETR: read(fd, buf, 8192) until it returns 0 */
        off = 0;
        for (;;) {
            got = rs_read(&mnt, &file, off, user, 8192, kbuf, &short_seen);
            ASSERT_EQ(got, rs_expect(size, off, 8192));
            ASSERT(rs_content_ok(i, off, user, (uint32_t)got));
            if (got == 0)
                break;
            off += (uint32_t)got;
        }
        ASSERT_EQ(off, size);

        /* dd at every bs: each read is full until the last */
        for (b = 0; b < sizeof(bss) / sizeof(bss[0]); b++) {
            off = 0;
            for (;;) {
                got = rs_read(&mnt, &file, off, user, bss[b], kbuf,
                              &short_seen);
                ASSERT_EQ(got, rs_expect(size, off, bss[b]));
                ASSERT(rs_content_ok(i, off, user, (uint32_t)got));
                if (got == 0)
                    break;
                off += (uint32_t)got;
            }
            ASSERT_EQ(off, size);
        }

        /* reads straddling offset 4096 from both sides */
        for (b = 0; b < sizeof(ds) / sizeof(ds[0]); b++) {
            off = 4096u - ds[b];
            got = rs_read(&mnt, &file, off, user, 2u * ds[b], kbuf,
                          &short_seen);
            ASSERT_EQ(got, rs_expect(size, off, 2u * ds[b]));
            ASSERT(rs_content_ok(i, off, user, (uint32_t)got));
        }

        /* an 8 KiB read from every offset near 4096 */
        for (off = 4090; off <= 4102; off++) {
            got = rs_read(&mnt, &file, off, user, 8192, kbuf, &short_seen);
            ASSERT_EQ(got, rs_expect(size, off, 8192));
            ASSERT(rs_content_ok(i, off, user, (uint32_t)got));
        }
    }

    ASSERT_EQ(short_seen, 0);
    ASSERT_EQ(media.violations, 0);

    free(user);
    free(kraw);
    odfs_unmount(&mnt);
}

TEST(read_span_2k_pages_aligned)   { rs_run(_fail_count, 2048, 0); }
TEST(read_span_2k_pages_skewed)    { rs_run(_fail_count, 2048, 8); }
TEST(read_span_4k_pages_aligned)   { rs_run(_fail_count, 4096, 0); }
TEST(read_span_4k_pages_skewed)    { rs_run(_fail_count, 4096, 8); }
TEST(read_span_4k_pages_half_page) { rs_run(_fail_count, 4096, 2048); }

TEST_MAIN()
