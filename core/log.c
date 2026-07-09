/*
 * log.c — structured logging subsystem
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "odfs/log.h"
#include "odfs/printf.h"
#include "odfs/alloc.h"
#if !defined(ODFS_PLATFORM_AMIX_KERNEL)
#include <stdio.h>   /* not present / not used in the AMIX kernel build */
#endif
#include <string.h>

/* Formatting buffer size for a single emitted log line. */
#define ODFS_LOG_BUF_SIZE 512

static const char *level_names[] = {
    [ODFS_LOG_FATAL] = "FATAL",
    [ODFS_LOG_ERROR] = "ERROR",
    [ODFS_LOG_WARN]  = "WARN",
    [ODFS_LOG_INFO]  = "INFO",
    [ODFS_LOG_DEBUG] = "DEBUG",
    [ODFS_LOG_TRACE] = "TRACE",
};

static const char *subsys_names[] = {
    [ODFS_SUB_NONE]         = "",
    [ODFS_SUB_CORE]         = "core",
    [ODFS_SUB_DOS]          = "dos",
    [ODFS_SUB_MOUNT]        = "mount",
    [ODFS_SUB_IO]           = "io",
    [ODFS_SUB_ISO]          = "iso",
    [ODFS_SUB_RR]           = "rr",
    [ODFS_SUB_JOLIET]       = "joliet",
    [ODFS_SUB_UDF]          = "udf",
    [ODFS_SUB_HFS]          = "hfs",
    [ODFS_SUB_HFSPLUS]      = "hfsplus",
    [ODFS_SUB_MULTISESSION] = "multisession",
    [ODFS_SUB_CDDA]         = "cdda",
    [ODFS_SUB_CACHE]        = "cache",
    [ODFS_SUB_CHARSET]      = "charset",
};

const char *odfs_log_level_name(odfs_log_level_t level)
{
    if (level >= 0 && level < ODFS_LOG__COUNT)
        return level_names[level];
    return "???";
}

const char *odfs_log_subsys_name(odfs_log_subsys_t subsys)
{
    if (subsys >= 0 && subsys < ODFS_SUB__COUNT)
        return subsys_names[subsys];
    return "???";
}

void odfs_log_init(odfs_log_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->max_level = ODFS_LOG_MAX_LEVEL;
    state->subsys_mask = ~0u; /* all subsystems enabled */
    state->sink.write = NULL;
    state->sink.ctx = NULL;
}

void odfs_log_set_sink(odfs_log_state_t *state, odfs_log_sink_fn fn, void *ctx)
{
    state->sink.write = fn;
    state->sink.ctx = ctx;
}

void odfs_log_set_level(odfs_log_state_t *state, odfs_log_level_t level)
{
    state->max_level = level;
}

void odfs_log_set_subsys_mask(odfs_log_state_t *state, unsigned int mask)
{
    state->subsys_mask = mask;
}

static void odfs_vlog(odfs_log_state_t *state,
                      odfs_log_level_t level,
                      odfs_log_subsys_t subsys,
                      const char *fmt, va_list ap)
{
    char *buf;
    int hdr;

    if (!odfs_log_enabled(state, level, subsys))
        return;

    /*
     * Allocate the 512-byte line buffer on the heap, and only after the
     * message has passed the level/subsystem filter — the filtered-out path
     * (the common case, and the ONLY case in the AMIX kernel where the sink
     * is NULL) then carries no sector-fraction frame at all. This matters on
     * the small SVR4/AMIX kernel stack, where log calls sit deep in the
     * mount/read call chain.
     */
    buf = odfs_malloc(ODFS_LOG_BUF_SIZE);
    if (!buf)
        return;

    if (subsys == ODFS_SUB_NONE) {
        hdr = odfs_snprintf(buf, ODFS_LOG_BUF_SIZE, "[%s] ",
                            odfs_log_level_name(level));
    } else {
        hdr = odfs_snprintf(buf, ODFS_LOG_BUF_SIZE, "[%s] %s: ",
                            odfs_log_level_name(level),
                            odfs_log_subsys_name(subsys));
    }
    if (hdr < 0)
        hdr = 0;

    if ((size_t)hdr < (size_t)ODFS_LOG_BUF_SIZE)
        odfs_vsnprintf(buf + hdr, (size_t)ODFS_LOG_BUF_SIZE - (size_t)hdr,
                       fmt, ap);

    state->sink.write(level, subsys, buf, state->sink.ctx);
    odfs_free(buf);
}

void odfs_log(odfs_log_state_t *state,
               odfs_log_level_t level,
               odfs_log_subsys_t subsys,
               const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    odfs_vlog(state, level, subsys, fmt, ap);
    va_end(ap);
}

/*
 * C89 fallback helpers. A C89 compiler cannot use the variadic ODFS_LOG
 * macros (see odfs/log.h), so the per-level shorthands route here instead,
 * with the level baked in. Compiled out on C99+ where the macros are used.
 */
#if !(defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)

#define ODFS_DEFINE_LEVEL_LOGGER(fn, lvl)                                  \
    void fn(odfs_log_state_t *state, odfs_log_subsys_t subsys,             \
            const char *fmt, ...)                                          \
    {                                                                      \
        va_list ap;                                                        \
        va_start(ap, fmt);                                                 \
        odfs_vlog(state, lvl, subsys, fmt, ap);                            \
        va_end(ap);                                                        \
    }

ODFS_DEFINE_LEVEL_LOGGER(odfs_log_fatal, ODFS_LOG_FATAL)
ODFS_DEFINE_LEVEL_LOGGER(odfs_log_error, ODFS_LOG_ERROR)
ODFS_DEFINE_LEVEL_LOGGER(odfs_log_warn,  ODFS_LOG_WARN)
ODFS_DEFINE_LEVEL_LOGGER(odfs_log_info,  ODFS_LOG_INFO)
ODFS_DEFINE_LEVEL_LOGGER(odfs_log_debug, ODFS_LOG_DEBUG)
ODFS_DEFINE_LEVEL_LOGGER(odfs_log_trace, ODFS_LOG_TRACE)

#undef ODFS_DEFINE_LEVEL_LOGGER

#endif /* !C99 */
