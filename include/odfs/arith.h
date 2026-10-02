/*
 * arith.h - 32-bit division and 32x32->64 multiplication helpers
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Without ODFS_NO_MUL64 every helper is the plain C expression and compiles
 * to exactly what the open-coded operator did.
 *
 * ODFS_NO_MUL64 is for targets that must never execute a 32x32->64 multiply
 * (MULU.L/MULS.L <ea>,Dh:Dl). The 68060 does not implement it and traps to
 * vector 61; inside a kernel there is no emulation package to catch that.
 * m68k gcc emits the instruction on its own for two idioms:
 *
 *   - a 32-bit division or modulo by a constant, done as a multiply by the
 *     reciprocal keeping the high word of the 64-bit product, and
 *   - (uint64_t)a * b where both operands are 32 bits wide.
 *
 * Compilers without a 68060 tuning (gcc 2.x) cannot be told to avoid either.
 * Under the knob the divisor is hidden from the optimizer, so a real 32-bit
 * DIVU.L/DIVS.L is emitted (implemented on the 68060), and the widening
 * product is assembled from 16x16->32 partial products.
 *
 * Power-of-two divisors never take the reciprocal path; write those as
 * shifts or masks and they need none of this.
 */

#ifndef ODFS_ARITH_H
#define ODFS_ARITH_H

#include "odfs/config.h"

#include <stddef.h>
#include <stdint.h>

#if defined(ODFS_NO_MUL64)

static inline uint32_t odfs_udiv32(uint32_t n, uint32_t d)
{
    volatile uint32_t vd = d;
    return n / vd;
}

static inline uint32_t odfs_umod32(uint32_t n, uint32_t d)
{
    volatile uint32_t vd = d;
    return n % vd;
}

static inline int32_t odfs_sdiv32(int32_t n, int32_t d)
{
    volatile int32_t vd = d;
    return n / vd;
}

static inline int32_t odfs_smod32(int32_t n, int32_t d)
{
    volatile int32_t vd = d;
    return n % vd;
}

static inline size_t odfs_udivsz(size_t n, size_t d)
{
    volatile size_t vd = d;
    return n / vd;
}

static inline uint64_t odfs_umul64(uint32_t a, uint32_t b)
{
    uint32_t al = a & 0xffffu, ah = a >> 16;
    uint32_t bl = b & 0xffffu, bh = b >> 16;
    uint32_t ll = al * bl;
    uint32_t lh = al * bh;
    uint32_t hl = ah * bl;
    uint32_t hh = ah * bh;
    uint32_t mid = (ll >> 16) + (lh & 0xffffu) + (hl & 0xffffu);
    uint32_t lo = (ll & 0xffffu) | (mid << 16);
    uint32_t hi = hh + (lh >> 16) + (hl >> 16) + (mid >> 16);

    return ((uint64_t)hi << 32) | lo;
}

#else

/*
 * Macros rather than inline functions, so the default build is the
 * open-coded operator with nothing left for the compiler to inline (an
 * old compiler's inliner perturbs register allocation). Each operand is
 * evaluated once and converted as a function argument would be.
 */
#define odfs_udiv32(n, d) ((uint32_t)(n) / (uint32_t)(d))
#define odfs_umod32(n, d) ((uint32_t)(n) % (uint32_t)(d))
#define odfs_sdiv32(n, d) ((int32_t)(n) / (int32_t)(d))
#define odfs_smod32(n, d) ((int32_t)(n) % (int32_t)(d))
#define odfs_udivsz(n, d) ((size_t)(n) / (size_t)(d))
#define odfs_umul64(a, b) ((uint64_t)(uint32_t)(a) * (uint32_t)(b))

#endif /* ODFS_NO_MUL64 */

#endif /* ODFS_ARITH_H */
