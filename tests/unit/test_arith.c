/*
 * test_arith.c - the ODFS_NO_MUL64 arithmetic helpers against the
 * native operators
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ODFS_NO_MUL64
#define ODFS_NO_MUL64 1
#endif

#include "odfs/arith.h"
#include "test_harness.h"

static uint32_t rng_state = 0x2545f491u;

static uint32_t rng_next(void)
{
    /* xorshift32 */
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static const uint32_t edge[] = {
    0u, 1u, 2u, 3u, 9u, 10u, 11u, 75u, 255u, 2047u, 2048u, 2352u,
    0xffffu, 0x10000u, 0x10001u, 86399u, 86400u, 0x7fffffffu,
    0x80000000u, 0xfffffffeu, 0xffffffffu
};

#define N_EDGE (sizeof(edge) / sizeof(edge[0]))

TEST(umul64_edges)
{
    size_t i, j;

    for (i = 0; i < N_EDGE; i++)
        for (j = 0; j < N_EDGE; j++)
            ASSERT(odfs_umul64(edge[i], edge[j]) ==
                   (uint64_t)edge[i] * edge[j]);
}

TEST(umul64_random)
{
    int i;

    for (i = 0; i < 200000; i++) {
        uint32_t a = rng_next();
        uint32_t b = rng_next() >> (i & 31);
        ASSERT(odfs_umul64(a, b) == (uint64_t)a * b);
    }
}

TEST(udiv32_umod32)
{
    static const uint32_t divisors[] = {
        3u, 5u, 10u, 18u, 60u, 61u, 75u, 100u, 255u, 2352u, 3600u, 86400u
    };
    size_t i, k;
    int r;

    for (k = 0; k < sizeof(divisors) / sizeof(divisors[0]); k++) {
        uint32_t d = divisors[k];
        for (i = 0; i < N_EDGE; i++) {
            ASSERT_EQ(odfs_udiv32(edge[i], d), edge[i] / d);
            ASSERT_EQ(odfs_umod32(edge[i], d), edge[i] % d);
        }
        for (r = 0; r < 20000; r++) {
            uint32_t n = rng_next();
            ASSERT_EQ(odfs_udiv32(n, d), n / d);
            ASSERT_EQ(odfs_umod32(n, d), n % d);
        }
    }
}

TEST(sdiv32_smod32)
{
    static const int32_t values[] = {
        0, 1, -1, 9, -9, 10, -10, 99, -99, 100, -100, 399, -399, 400, -401,
        1970, 2000, 2100, 2400, INT32_MAX, INT32_MIN + 1
    };
    static const int32_t divisors[] = { 10, 100, 400, -7 };
    size_t i, k;

    for (k = 0; k < sizeof(divisors) / sizeof(divisors[0]); k++) {
        for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
            ASSERT_EQ(odfs_sdiv32(values[i], divisors[k]),
                      values[i] / divisors[k]);
            ASSERT_EQ(odfs_smod32(values[i], divisors[k]),
                      values[i] % divisors[k]);
        }
    }
}

TEST(udivsz)
{
    size_t i;

    for (i = 0; i < N_EDGE; i++)
        ASSERT_EQ(odfs_udivsz((size_t)edge[i], (size_t)24),
                  (size_t)edge[i] / 24u);
    ASSERT(odfs_udivsz((size_t)-1, (size_t)18) == (size_t)-1 / 18u);
}

TEST_MAIN()
