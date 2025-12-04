// SPDX-FileCopyrightText: 2025 Frank Hunleth
//
// SPDX-License-Identifier: Apache-2.0
//

#ifndef ELOG_H
#define ELOG_H

// See /usr/include/syslog.h for values. They're also standardized in RFC5424.
#define ELOG_LEVEL_EMERG   0
#define ELOG_LEVEL_ALERT   1
#define ELOG_LEVEL_CRIT    2
#define ELOG_LEVEL_ERROR   3
#define ELOG_LEVEL_WARNING 4
#define ELOG_LEVEL_NOTICE  5
#define ELOG_LEVEL_INFO    6
#define ELOG_LEVEL_DEBUG   7
#define ELOG_LEVEL_DONT_LOG 8

#define ELOG_SEVERITY_MASK 0xf
#define ELOG_PMSG (1 << 4)     // Log to /dev/pmsg0 if available

// Callers normally use these macros so that errors and more severe levels (not warnings) always get to pmsg
#define ELOG_EMERG     (ELOG_LEVEL_EMERG | ELOG_PMSG)
#define ELOG_ALERT     (ELOG_LEVEL_ALERT | ELOG_PMSG)
#define ELOG_CRIT      (ELOG_LEVEL_CRIT | ELOG_PMSG)
#define ELOG_ERROR     (ELOG_LEVEL_ERROR | ELOG_PMSG)
#define ELOG_WARNING   (ELOG_LEVEL_WARNING)
#define ELOG_NOTICE    (ELOG_LEVEL_NOTICE)
#define ELOG_INFO      (ELOG_LEVEL_INFO)
#define ELOG_DEBUG     (ELOG_LEVEL_DEBUG)
#define ELOG_PMSG_ONLY (ELOG_LEVEL_DONT_LOG | ELOG_PMSG)

// Global logging level
extern int elog_level;

// Logging functions
void elog(int severity, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#endif // ELOG_H
