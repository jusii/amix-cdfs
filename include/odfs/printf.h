/*
 * printf.h - local bounded formatting helpers
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ODFS_PRINTF_H
#define ODFS_PRINTF_H

#include <stdarg.h>
#include <stddef.h>
#if !defined(ODFS_PLATFORM_AMIX_KERNEL)
#include <stdio.h>   /* only the host #else branch below needs it */
#endif

#if defined(AMIGA) || defined(ODFS_PLATFORM_AMIX) || defined(ODFS_PLATFORM_AMIX_KERNEL)

int odfs_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
#ifdef __GNUC__
    __attribute__((format(printf, 3, 0)))
#endif
    ;

int odfs_snprintf(char *buf, size_t size, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 3, 4)))
#endif
    ;

#else

static inline int odfs_vsnprintf(char *buf, size_t size,
                                 const char *fmt, va_list ap)
{
    return vsnprintf(buf, size, fmt, ap);
}

static inline int odfs_snprintf(char *buf, size_t size, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 3, 4)))
#endif
    ;

static inline int odfs_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = odfs_vsnprintf(buf, size, fmt, ap);
    va_end(ap);

    return ret;
}

#endif

#endif /* ODFS_PRINTF_H */
