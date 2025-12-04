// SPDX-FileCopyrightText: 2025 Frank Hunleth
//
// SPDX-License-Identifier: Apache-2.0
//

#include "elog.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#ifndef PROGRAM_NAME
#define PROGRAM_NAME "nerves_heart"
#endif
#ifndef ELOG_FACILITY
#define ELOG_FACILITY 3 // LOG_DAEMON
#endif

int elog_level = ELOG_LEVEL_INFO;

static int kmsg_format(int severity, char **strp, const char *msg)
{
    int prival = ELOG_FACILITY * 8 + (severity & ELOG_SEVERITY_MASK);
    return asprintf(strp, "<%d>" PROGRAM_NAME ": %s\n", prival, msg);
}

static int stderr_format(char **strp, const char *msg)
{
    return asprintf(strp, PROGRAM_NAME ": %s\n", msg);
}

static int pmsg_format(char **strp, const char *msg)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return -1;

    struct tm tm;
    if (gmtime_r(&ts.tv_sec, &tm) == NULL)
        return -1;

    long usec = ts.tv_nsec / 1000;

    // Match the RFC3339 timestamps from Erlang's logger_formatter
    // 2025-12-04T00:01:34.200744+00:00
    return asprintf(
            strp,
            "%04d-%02d-%02dT%02d:%02d:%02d.%06ld+00:00 " PROGRAM_NAME " %s\n",
            tm.tm_year + 1900,
            tm.tm_mon + 1,
            tm.tm_mday,
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec,
            usec,
            msg
        );
}

static void log_pmsg_breadcrumb(const char *msg)
{
    static int open_failed = 0;

    // Don't bother trying again on failures.
    if (open_failed)
        return;

    int pmsg_fd = open("/dev/pmsg0", O_WRONLY | O_CLOEXEC);
    if (pmsg_fd < 0) {
        open_failed = 1;
        return;
    }

    char *str;
    int len = pmsg_format(&str, msg);
    if (len > 0) {
        ssize_t ignore = write(pmsg_fd, str, len);
        (void) ignore;
        free(str);
    }
    close(pmsg_fd);
}

static void log_write(int severity, const char *msg)
{
    char *str;
    ssize_t ignore;
    int log_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (log_fd >= 0) {
        int len = kmsg_format(severity, &str, msg);
        if (len > 0) {
            ignore = write(log_fd, str, len);
            free(str);
        }
        close(log_fd);
    } else {
        int len = stderr_format(&str, msg);
        if (len > 0) {
            ignore = write(STDERR_FILENO, str, len);
            free(str);
        }
    }
    (void) ignore;
}

void elog(int severity, const char *fmt, ...)
{
    int level = severity & ELOG_SEVERITY_MASK;
    int log_pmsg = severity & ELOG_PMSG;
    if (level <= elog_level || log_pmsg) {
        va_list ap;
        va_start(ap, fmt);

        char *msg;
        if (vasprintf(&msg, fmt, ap) > 0) {
            if (log_pmsg)
                log_pmsg_breadcrumb(msg);

            if (level <= elog_level)
                log_write(severity, msg);

            free(msg);
        }

        va_end(ap);
    }
}
