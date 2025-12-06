// SPDX-FileCopyrightText: 1996-2020 Ericsson AB. All Rights Reserved.
// SPDX-FileCopyrightText: 2018 Frank Hunleth
// SPDX-FileCopyrightText: 2020 Alan Jackson
// SPDX-FileCopyrightText: 2020 Jon Thacker
// SPDX-FileCopyrightText: 2024 Jon Ringle
//
// SPDX-License-Identifier: Apache-2.0

/*
 * %CopyrightBegin%
 *
 * Copyright Ericsson AB 1996-2020. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * %CopyrightEnd%
 */
/**
 *
 *  File:     heart.c
 *  Purpose:  Portprogram for supervision of the Erlang emulator.
 *
 *  Synopsis: heart
 *
 *  SPAWNING FROM ERLANG
 *
 *  This program is started from Erlang as follows,
 *
 *      Port = open_port({spawn, 'heart'}, [{packet, 2}]),
 *
 *  ROLE OF THIS PORT PROGRAM
 *
 *  This program is started by the Erlang emulator. It  communicates
 *  with the emulator through file descriptor 0 (standard input).
 *
 *  MESSAGE FORMAT
 *
 *  All messages have the following format (a value in parentheses
 *  indicate field length in bytes),
 *
 *      {Length(2), Operation(1)}
 *
 *  START ACK
 *
 *  When this program has started it sends an START ACK message to Erlang.
 *
 *  HEART_BEATING
 *
 *  This program expects a heart beat message. If it does not receive a
 *  heart beat message from Erlang within heart_beat_timeout seconds, it
 *  reboots the system.
 *
 *  BLOCKING DESCRIPTORS
 *
 *  All file descriptors in this program are blocking. This can lead
 *  to deadlocks. The emulator reads and writes are blocking.
 *
 *  STANDARD INPUT, OUTPUT AND ERROR
 *
 *  This program communicates with Erlang through the standard
 *  input and output file descriptors (0 and 1). These descriptors
 *  (and the standard error descriptor 2) must NOT be closed
 *  explicitly by this program at termination (in UNIX it is
 *  taken care of by the operating system itself).
 *
 *  END OF FILE
 *
 *  If a read from a file descriptor returns zero (0), it means
 *  that there is no process at the other end of the connection
 *  having the connection open for writing (end-of-file).
 *
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include <stdarg.h>

#include <string.h>
#include <time.h>
#include <errno.h>

#include <signal.h>
#include <unistd.h>
#include <linux/reboot.h>
#include <sys/reboot.h>
#include <arpa/inet.h>
#include <linux/watchdog.h>
#include <sys/ioctl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define PROGRAM_NAME "nerves_heart"
#ifndef PROGRAM_VERSION
#error PROGRAM_VERSION is undefined
#endif

#define xstr(s) str(s)
#define str(s) #s
#define PROGRAM_VERSION_STR xstr(PROGRAM_VERSION)

#ifdef __clang_analyzer__
   /* CodeChecker does not seem to understand inline asm in FD_ZERO */
#  undef FD_ZERO
#  define FD_ZERO(FD_SET_PTR) memset(FD_SET_PTR, 0, sizeof(fd_set))
#endif

#define HEART_INIT_GRACE_TIME_ENV  "HEART_INIT_GRACE_TIME"
#define HEART_INIT_TIMEOUT_ENV     "HEART_INIT_TIMEOUT"
#define HEART_KERNEL_TIMEOUT_ENV   "HEART_KERNEL_TIMEOUT"
#define ERL_CRASH_DUMP_SECONDS_ENV "ERL_CRASH_DUMP_SECONDS"
#define HEART_KILL_SIGNAL          "HEART_KILL_SIGNAL"
#define HEART_WATCHDOG_PATH        "HEART_WATCHDOG_PATH"
#define HEART_NO_KILL              "HEART_NO_KILL"
#define HEART_VERBOSE              "HEART_VERBOSE"

#define MSG_HDR_SIZE         (2)
#define MSG_HDR_PLUS_OP_SIZE (3)
#define MSG_BODY_SIZE        (2048)
#define MSG_TOTAL_SIZE       (2050)

struct msg {
  unsigned short len;
  unsigned char op;
  unsigned char fill[MSG_BODY_SIZE]; /* one too many */
};

/* operations */
#define  HEART_ACK       (1)
#define  HEART_BEAT      (2)
#define  SHUT_DOWN       (3)
#define  SET_CMD         (4)
#define  CLEAR_CMD       (5)
#define  GET_CMD         (6)
#define  HEART_CMD       (7)
#define  PREPARING_CRASH (8)


/*  Maybe interesting to change */

/* Times in seconds */
#define  DEFAULT_HEART_BEAT_TIMEOUT 60 /* Expect a message at least every 60 seconds from Erlang. */
#define  DEFAULT_WDT_TIMEOUT        10
#define  WDT_PET_TIMEOUT_BUFFER     10 /* Pet the watchdog 10 seconds before it would expire (or half its timeout) */
#define  DEFAULT_WDT_PET_TIMEOUT    (DEFAULT_WDT_TIMEOUT / 2)
#define  MIN_WDT_PET_TIMEOUT        2 /* Limited by pet timer's resolution in seconds */
#define  MAX_WDT_PET_TIMEOUT        120
#define  MIN_RUN_TIME               60
#define  MAX_MIN_RUN_TIME           600 /* Don't allow the heart to be disabled indefinitely */

static int wdt_pet_timeout = DEFAULT_WDT_PET_TIMEOUT;

/* heart_beat_timeout is the maximum gap in seconds between two
   consecutive heart beat messages from Erlang. */
static int heart_beat_timeout = DEFAULT_HEART_BEAT_TIMEOUT;

/* last_heart_beat_time is the absolute time that the previous heart beat was received */
static time_t last_heart_beat_time = 0;

/* wdt_timeout is the maximum gap in seconds between two consecutive heart beat
 * messages before the hardware watchdog times out.
 */
static int wdt_timeout = DEFAULT_WDT_TIMEOUT;

/* last_wdt_pet_time is the absolute time that hardware watchdog was pet */
static time_t last_wdt_pet_time = 0;

/* Timeout on receiving a handshake message from the application that the heart callback was set. 0=unused */
static time_t init_handshake_timeout = 0;

/* Set to 1 if the initialization handshake message came in */
static int init_handshake_happened = 0;

/* If !init_handshake_happened, then this is the end time */
static time_t init_handshake_end_time = 0;

/* Keep the system running for this many seconds from the start */
static time_t init_grace_time = 0;

/* End timestamp for not crashing the system on an issue */
static time_t init_grace_end_time = 0;

/* All current platforms have a process identifier that
   fits in an unsigned long and where 0 is an impossible or invalid value */
static pid_t heart_beat_kill_pid = 0;

/* When snooze is enabled, this is when it's over. */
static time_t snooze_end_time = 0;

/* Set to one when either the SIGUSR1 is received */
static int snooze_requested = 0;

/* reasons for reboot */
#define  R_TIMEOUT          (1)
#define  R_CLOSED           (2)
#define  R_ERROR            (3)
#define  R_SHUT_DOWN        (4)
#define  R_CRASHING         (5) /* Doing a crash dump and we will wait for it */

/*  macros */

#define  NULLFDS  ((fd_set *) NULL)

/*  prototypes */

static int message_loop(void);
static void do_terminate(int);
static int notify_ack(void);
static int heart_cmd_info_reply(time_t now);
static int write_message(int, const struct msg *);
static int read_message(int, struct msg *);
static int read_skip(int, char *, int, int);
static int read_fill(int, char *, int);
static time_t timestamp_seconds();
static int  wait_until_close_write_or_env_tmo(int);

/*  static variables */

static char * const watchdog_path_default = "/dev/watchdog0";
static int watchdog_open_retries = 10;
static int watchdog_fd = -1;
static int verbose_level = 1; // 0 = no prints, 1 = important prints, 2 = informational prints

static int is_env_set(char *key)
{
    return getenv(key) != NULL;
}

static char *get_env(char *key)
{
    return getenv(key);
}

// See /usr/include/syslog.h for values. They're also standardized in RFC5424.
#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERROR   3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7
#define LOG_DAEMON  3

static int kmsg_format(int severity, char **strp, const char *msg)
{
    int prival = (LOG_DAEMON << 3) | severity; // facility=daemon(3)
    return asprintf(strp, "<%d>heart: %s\n", prival, msg);
}

static int stderr_format(char **strp, const char *msg)
{
    return asprintf(strp, "heart: %s\n", msg);
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

// Enable compiler checks for printf-style format strings.
static void elog(int severity, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void elog(int severity, const char *fmt, ...)
{
    if (severity <= verbose_level) {
        va_list ap;
        va_start(ap, fmt);

        char *msg;
        if (vasprintf(&msg, fmt, ap) > 0) {
            log_write(severity, msg);
            free(msg);
        }

        va_end(ap);
    }
}

static void set_default_logging_verbosity()
{
    // If logging to kmsg, then log more since kmsg supports log levels.
    if (access("/dev/kmsg", W_OK) == 0)
        verbose_level = LOG_INFO;
    else
        verbose_level = LOG_ERROR;
 }

static void set_logging_verbosity()
{
    const char *verbose_value = get_env(HEART_VERBOSE);
    if (!verbose_value) {
        set_default_logging_verbosity();
        return;
    }

    // Per Erlang heart documentation:
    // 0 = no prints, 1 = important prints, 2 = informational prints
    switch (atoi(verbose_value)) {
    case 0:
        verbose_level = LOG_EMERG;
        break;
    case 1:
        set_default_logging_verbosity();
        break;
    default:
        verbose_level = LOG_DEBUG;
        break;
    }
}

static void try_open_watchdog()
{
    /* The watchdog device sometimes takes a bit to appear, so give it a few tries. */
    if (watchdog_fd >= 0)
       return;

    if (watchdog_open_retries <= 0)
       return;

    char *watchdog_path = get_env(HEART_WATCHDOG_PATH);
    if (watchdog_path == NULL)
        watchdog_path = watchdog_path_default;

    watchdog_fd = open(watchdog_path, O_WRONLY);
    if (watchdog_fd >= 0) {
        int real_wdt_timeout;
        int set_wdt_timeout = 0;
        int ret = 0;
        char *kernel_timeout_env = get_env(HEART_KERNEL_TIMEOUT_ENV);

        if (kernel_timeout_env != NULL) {
            struct watchdog_info info;
            if (ioctl(watchdog_fd, WDIOC_GETSUPPORT, &info) == 0 &&
                info.options & WDIOF_SETTIMEOUT) {

                set_wdt_timeout = atoi(kernel_timeout_env);
                if (set_wdt_timeout >= MIN_WDT_PET_TIMEOUT &&
                    set_wdt_timeout <= MAX_WDT_PET_TIMEOUT) {
                    ret = ioctl(watchdog_fd, WDIOC_SETTIMEOUT, &set_wdt_timeout);
                    if (ret == 0) {
                        elog(LOG_INFO, "kernel WDT timeout set to %ds", set_wdt_timeout);
                    } else {
                        elog(LOG_ERROR, "Failed to set kernel WDT timeout to %ds (ioctl ret %d, errno %d: %s)",
                                  set_wdt_timeout, ret, errno, strerror(errno));
                    }
                } else {
                    elog(LOG_ERROR, "Failed to set kernel WDT timeout to %ds (invalid range %d-%d)",
                              set_wdt_timeout, MIN_WDT_PET_TIMEOUT, MAX_WDT_PET_TIMEOUT);
                }
            } else {
                elog(LOG_ERROR, "Failed to set kernel WDT timeout to %ss (not supported)", kernel_timeout_env);
            }
        }

        ret = ioctl(watchdog_fd, WDIOC_GETTIMEOUT, &real_wdt_timeout);
        if (ret == 0 && real_wdt_timeout >= MIN_WDT_PET_TIMEOUT) {
            wdt_timeout = real_wdt_timeout;
            /* Most of the time, pet WDT_PET_TIMEOUT_BUFFER seconds before the timeout,
             * but if it's really short, then pet half the timeout.
             */
            if (real_wdt_timeout > 2*WDT_PET_TIMEOUT_BUFFER)
                wdt_pet_timeout = real_wdt_timeout - WDT_PET_TIMEOUT_BUFFER;
            else
                wdt_pet_timeout = real_wdt_timeout / 2;
        } else if (ret != 0) {
            elog(LOG_ERROR, "error or too short WDT timeout so using defaults!");
        }

        elog(LOG_INFO, "kernel watchdog activated. WDT timeout %ds, WDT pet interval %ds, VM timeout %ds, initial grace period %ds", wdt_timeout, wdt_pet_timeout, heart_beat_timeout, (int) init_grace_time);
    } else {
        watchdog_open_retries--;
        if (watchdog_open_retries <= 0) {
            elog(LOG_ERROR, "can't open '%s'. Running without kernel watchdog: %s", watchdog_path, strerror(errno));
            wdt_timeout = wdt_pet_timeout = 60*60*24*365;
        }
        return;
    }
}

static void pet_watchdog(time_t now)
{
    try_open_watchdog();

    if (watchdog_fd >= 0) {
        if (write(watchdog_fd, "\0", 1) >= 0) {
            last_wdt_pet_time = now;
        } else {
            elog(LOG_ERROR, "error petting watchdog: %s", strerror(errno));

            // Retry next time if there is a next time.
            close(watchdog_fd);
            watchdog_fd = -1;
        }
    }
}

/*
 *  main
 */
static void get_arguments(int argc, char **argv)
{
    int i = 1;
    int h = -1;
    unsigned long p = 0;

    while (i < argc) {
        switch (argv[i][0]) {
        case '-':
            switch (argv[i][1]) {
            case 'h':
                if (strcmp(argv[i], "-ht") == 0)
                    if (sscanf(argv[i + 1], "%i", &h) == 1)
                        if ((h > 10) && (h <= 65535)) {
                            heart_beat_timeout = h;
                            i++;
                        }
                break;
            case 'p':
                if (strcmp(argv[i], "-pid") == 0)
                    if (sscanf(argv[i + 1], "%lu", &p) == 1) {
                        heart_beat_kill_pid = p;
                        i++;
                    }
                break;
            default:
                ;
            }
            break;
        default:
            ;
        }
        i++;
    }
}

static void stop_petting_watchdog()
{
    // Stop petting of the hardware watchdog by forgetting the
    // file handle and marking that there are no retries left to
    // open it. Do not close the file handle since that might
    // tell Linux to disable the watchdog if the kernel doesn't
    // have CONFIG_WDT_NOWAYOUT=y.
    watchdog_open_retries = 0;
    watchdog_fd = -1;

    // Set the pet timeout out really long so that if control
    // ends up in the select loop, the WDT pet timeout won't
    // exit select early.
    wdt_pet_timeout = 86400;
}

static void snooze_signal_handler(int sig)
{
    (void) sig;
    snooze_requested = 1;
}

int main(int argc, char **argv)
{
    set_logging_verbosity();

    elog(LOG_INFO, "" PROGRAM_NAME " v" PROGRAM_VERSION_STR " started.");

    // Assume that the handshake happened and this fixes it if a timeout was specified
    init_handshake_happened = 1;
    if (is_env_set(HEART_INIT_TIMEOUT_ENV)) {
        const char *init_tmo_env = get_env(HEART_INIT_TIMEOUT_ENV);
        init_handshake_timeout = atoi(init_tmo_env);
        if (init_handshake_timeout > 0)
            init_handshake_happened = 0;
    }
    if (is_env_set(HEART_INIT_GRACE_TIME_ENV)) {
        const char *init_grace_time_env = get_env(HEART_INIT_GRACE_TIME_ENV);
        init_grace_time = atoi(init_grace_time_env);
        if (init_grace_time < 0)
            init_grace_time = 0;
        else if (init_grace_time > MAX_MIN_RUN_TIME)
            init_grace_time = MAX_MIN_RUN_TIME;

        // Check that the initialization handshake timeout, if any, doesn't
        // come before the initial grace period time out and introduce another
        // way to exit too soon.
        if (init_handshake_timeout > 0 && init_handshake_timeout < init_grace_time)
            init_handshake_timeout = init_grace_time;
    }

    signal(SIGUSR1, snooze_signal_handler);

    get_arguments(argc, argv);
    notify_ack();

    do_terminate(message_loop());

    return 0;
}

static inline int max(int x, int y) { if (x > y) return x; else return y; }
static inline int min(int x, int y) { if (x > y) return y; else return x; }

/*
 * message loop
 */
static int message_loop()
{
    int   i;
    time_t now;
    fd_set read_fds;
    int   max_fd;
    struct timeval timeout;
    int   tlen;           /* total message length */
    struct msg m;

    // Initialize timestamps
    now = last_heart_beat_time = last_wdt_pet_time = snooze_end_time = timestamp_seconds();
    init_handshake_end_time = now + init_handshake_timeout;
    init_grace_end_time = now + init_grace_time;
    last_heart_beat_time = init_grace_end_time;

    // Pet the hw watchdog on start since we don't know how long it has been
    pet_watchdog(now);

    max_fd = STDIN_FILENO;

    while (1) {
        if (snooze_requested) {
            /* Don't time out for the next 15 minutes no matter what */
            pet_watchdog(now);
            init_handshake_happened = 1;
            last_heart_beat_time = snooze_end_time = now + 15 * 60;
            snooze_requested = 0;
        }

        /* Prepare to block on select */
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        timeout.tv_sec = max(1, min(last_heart_beat_time + heart_beat_timeout - now, last_wdt_pet_time + wdt_pet_timeout - now));
        timeout.tv_usec = 0;

        if (!init_handshake_happened)
            timeout.tv_sec = min(timeout.tv_sec, init_handshake_end_time - now);

        if ((i = select(max_fd + 1, &read_fds, NULLFDS, NULLFDS, &timeout)) < 0) {
            if (errno == EINTR)
                continue;

            elog(LOG_ERROR, "select failed: %s", strerror(errno));
            return R_ERROR;
        }

        now = timestamp_seconds();

        if (now >= last_heart_beat_time + heart_beat_timeout) {
            elog(LOG_ERROR, "heartbeat timeout -> no activity for %lu seconds",
                  (unsigned long) (now - last_heart_beat_time));
            return R_TIMEOUT;
        }

        if (!init_handshake_happened && now >= init_handshake_end_time) {
            elog(LOG_ERROR, "init handshake never happened -> not received in %lu seconds",
                  (unsigned long) init_handshake_timeout);
            return R_TIMEOUT;
        }

        /*
         * Do not check fd-bits if select timeout
         */
        if (i == 0) {
            pet_watchdog(now);
            continue;
        }

        if (now < snooze_end_time || now < init_grace_end_time) {
            // If snoozing or keeping the device alive for a minimum amount of time, unconditionally pet the hardware watchdog.
            pet_watchdog(now);
        }

        /*
         * Message from ERLANG
         */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if ((tlen = read_message(STDIN_FILENO, &m)) < 0) {
                elog(LOG_ERROR, "error from read_message:  %s", strerror(errno));
                return R_ERROR;
            }
            if ((tlen > MSG_HDR_SIZE) && (tlen <= MSG_TOTAL_SIZE)) {
                int mp_len = htons(m.len);

                switch (m.op) {
                case HEART_BEAT:
                    pet_watchdog(now);
                    // Snoozing and the initial grace period set
                    // last_heart_beat_time to a future time.
                    if (last_heart_beat_time < now)
                        last_heart_beat_time = now;
                    break;
                case SHUT_DOWN:
                    return R_SHUT_DOWN;
                case SET_CMD:
                    if ((mp_len == 8 && memcmp(m.fill, "disable", 7) == 0) ||
                        (mp_len == 11 && memcmp(m.fill, "disable_hw", 10) == 0)) {
                        /* If the user specifies "disable" or "disable_hw", turn off the hw watchdog
                         * petter to verify that the system reboots.
                         */
                        elog(LOG_ERROR, "Received 'disable_hw' so no longer petting the hardware watchdog. System should reboot momentarily.");

                        stop_petting_watchdog();
                    } else if (mp_len == 11 && memcmp(m.fill, "disable_vm", 10) == 0) {
                        /* If the user specifies "disable_vm", return like there was a timeout */
                        elog(LOG_ERROR, "Received 'disable_vm' so exiting with a timeout. System should reboot momentarily.");

                        notify_ack();
                        return R_TIMEOUT;
                    } else if (mp_len == 15 && memcmp(m.fill, "guarded_reboot", 14) == 0) {
                        pet_watchdog(now);
                        stop_petting_watchdog();
                        kill(1, SIGTERM); // SIGTERM signals "reboot" to PID 1

                        elog(LOG_ERROR, "Guarded reboot requested. No longer petting the WDT");
                        sync();
                    } else if (mp_len == 25 && memcmp(m.fill, "guarded_immediate_reboot", 24) == 0) {
                        stop_petting_watchdog();
                        reboot(LINUX_REBOOT_CMD_RESTART);

                        elog(LOG_ERROR, "Guarded immediate reboot requested. No longer petting the WDT");
                     } else if (mp_len == 17 && memcmp(m.fill, "guarded_poweroff", 16) == 0) {
                        pet_watchdog(now);
                        stop_petting_watchdog();
                        kill(1, SIGUSR2); // SIGUSR2 signals "poweroff" to PID 1

                        elog(LOG_ERROR, "Guarded poweroff requested. No longer petting the WDT");
                        sync();
                    } else if (mp_len == 27 && memcmp(m.fill, "guarded_immediate_poweroff", 26) == 0) {
                        stop_petting_watchdog();
                        reboot(LINUX_REBOOT_CMD_POWER_OFF);

                        elog(LOG_ERROR, "Guarded immediate poweroff requested. No longer petting the WDT");
                    } else if (mp_len == 13 && memcmp(m.fill, "guarded_halt", 12) == 0) {
                        pet_watchdog(now);
                        stop_petting_watchdog();
                        kill(1, SIGUSR1); // SIGUSR1 signals "halt" to PID 1

                        elog(LOG_ERROR, "Guarded halt requested. No longer petting the WDT");
                        sync();
                    } else if (mp_len == 15 && memcmp(m.fill, "init_handshake", 14) == 0) {
                        /* Application has said that it's completed initialization */
                        elog(LOG_ERROR, "Received init handshake");
                        init_handshake_happened = 1;
                    } else if (mp_len == 7 && memcmp(m.fill, "snooze", 6) == 0) {
                        elog(LOG_ERROR, "Snoozing heart keepalive checks for 15 minutes");
                        snooze_requested = 1;
                    }
                    notify_ack();
                    break;
                case CLEAR_CMD:
                    /* Not supported */
                    notify_ack();
                    break;
                case GET_CMD:
                    /* Return information about heart */
                    heart_cmd_info_reply(now);
                    break;
                case PREPARING_CRASH:
                    /* Erlang has reached a crash dump point (is crashing for sure) */
                    elog(LOG_ERROR, "Erlang is crashing .. (waiting for crash dump file)");
                    return R_CRASHING;
                default:
                    /* ignore all other messages */
                    break;
                }
            } else if (tlen == 0) {
                /* Erlang has closed its end */
                elog(LOG_ERROR, "Erlang has closed.");
                return R_CLOSED;
            }
            /* Junk erroneous messages */
        }
    }
}

static void
kill_old_erlang(int reason)
{
    int i, res;
    int sig = SIGKILL;
    char *envvar = NULL;

    envvar = get_env(HEART_NO_KILL);
    if (envvar && strcmp(envvar, "TRUE") == 0)
      return;

    if (heart_beat_kill_pid != 0) {
        if (reason == R_CLOSED) {
            elog(LOG_INFO, "Wait 5 seconds for Erlang to terminate nicely");
            for (i=0; i < 5; ++i) {
               res = kill(heart_beat_kill_pid, 0); /* check if alive */
               if (res < 0 && errno == ESRCH)
                  return;
              sleep(1);
            }
           elog(LOG_ERROR, "Erlang still alive, kill it");
        }

        envvar = get_env(HEART_KILL_SIGNAL);
        if (envvar && strcmp(envvar, "SIGABRT") == 0) {
            elog(LOG_ERROR, "kill signal SIGABRT requested");
            sig = SIGABRT;
        }

        res = kill(heart_beat_kill_pid, sig);
        for (i = 0; i < 5 && res == 0; ++i) {
            sleep(1);
            res = kill(heart_beat_kill_pid, sig);
        }
        if (errno != ESRCH) {
            elog(LOG_ERROR, "Unable to kill old process, "
                 "kill failed (tried multiple times):  %s", strerror(errno));
        }
    }
}

/*
 * do_terminate
 */
static void
do_terminate(int reason)
{
    switch (reason) {
    case R_SHUT_DOWN:
        // Pet watchdog to give remainder of graceful shutdown code time to run
        pet_watchdog(0);
        break;
    case R_CRASHING:
        // Pet watchdog to avoid unintended WDT reset during crash
        pet_watchdog(0);
        if (is_env_set(ERL_CRASH_DUMP_SECONDS_ENV)) {
            const char *tmo_env = get_env(ERL_CRASH_DUMP_SECONDS_ENV);
            int tmo = atoi(tmo_env);
            elog(LOG_ERROR, "waiting for dump - timeout set to %d seconds.", tmo);
            wait_until_close_write_or_env_tmo(tmo);
        }
    /* fall through */
    case R_TIMEOUT:
    case R_CLOSED:
    case R_ERROR:
    default:
        sync();
        kill_old_erlang(reason);
        reboot(LINUX_REBOOT_CMD_RESTART);
        break;
    } /* switch(reason) */
}


/* Waits until something happens on socket or handle
 *
 * Uses global variables STDIN_FILENO or hevent_dataready
 */
int wait_until_close_write_or_env_tmo(int tmo)
{
    int i = 0;

    fd_set read_fds;
    int   max_fd;
    struct timeval timeout;
    struct timeval *tptr = NULL;

    max_fd = STDIN_FILENO; /* global */

    if (tmo >= 0) {
        timeout.tv_sec  = tmo;  /* On Linux timeout is modified by select */
        timeout.tv_usec = 0;
        tptr = &timeout;
    }

    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    if ((i = select(max_fd + 1, &read_fds, NULLFDS, NULLFDS, tptr)) < 0) {
        elog(LOG_ERROR, "select failed:  %s", strerror(errno));
        return -1;
    }
    return i;
}


/*
 * notify_ack
 *
 * Sends an HEART_ACK.
 */
static int notify_ack()
{
    struct msg m;

    m.op = HEART_ACK;
    m.len = htons(1);
    return write_message(STDOUT_FILENO, &m);
}


/*
 *  write_message
 *
 *  Writes a message to a blocking file descriptor. Returns the total
 *  size of the message written (always > 0), or -1 if error.
 *
 *  A message which is too short or too long, is not written. The return
 *  value is then MSG_HDR_SIZE (2), as if the message had been written.
 *  Is this really necessary? Can't we assume that the length is ok?
 *  FIXME.
 */
static int write_message(int fd, const struct msg *mp)
{
    int len = ntohs(mp->len);

    if ((len == 0) || (len > MSG_BODY_SIZE)) {
        return MSG_HDR_SIZE;
    }             /* cc68k wants (char *) */
    if (write(fd, (const char *) mp, len + MSG_HDR_SIZE) != len + MSG_HDR_SIZE) {
        return -1;
    }
    return len + MSG_HDR_SIZE;
}

/*
 *  read_message
 *
 *  Reads a message from a blocking file descriptor. Returns the total
 *  size of the message read (> 0), 0 if eof, and < 0 if error.
 *
 *  Note: The return value MSG_HDR_SIZE means a message of total size
 *  MSG_HDR_SIZE, i.e. without even an operation field.
 *
 *  If the size of the message is larger than MSG_TOTAL_SIZE, the total
 *  number of bytes read is returned, but the buffer contains a truncated
 *  message.
 */
static int read_message(int fd, struct msg *mp)
{
    int   rlen, i;
    unsigned char *tmp;

    if ((i = read_fill(fd, (char *) mp, MSG_HDR_SIZE)) != MSG_HDR_SIZE) {
        /* < 0 is an error; = 0 is eof */
        return i;
    }

    tmp = (unsigned char *) & (mp->len);
    rlen = (*tmp * 256) + *(tmp + 1);
    if (rlen == 0) {
        return MSG_HDR_SIZE;
    }
    if (rlen > MSG_BODY_SIZE) {
        if ((i = read_skip(fd, (((char *) mp) + MSG_HDR_SIZE),
                           MSG_BODY_SIZE, rlen)) != rlen) {
            return i;
        } else {
            return rlen + MSG_HDR_SIZE;
        }
    }
    if ((i = read_fill(fd, ((char *) mp + MSG_HDR_SIZE), rlen)) != rlen) {
        return i;
    }
    return rlen + MSG_HDR_SIZE;
}

/*
 *  read_fill
 *
 *  Reads len bytes into buf from a blocking fd. Returns total number of
 *  bytes read (i.e. len) , 0 if eof, or < 0 if error. len must be > 0.
 */
static int read_fill(int fd, char *buf, int len)
{
    int   i, got = 0;

    do {
        if ((i = read(fd, buf + got, len - got)) <= 0) {
            return i;
        }
        got += i;
    } while (got < len);
    return len;
}

/*
 *  read_skip
 *
 *  Reads len bytes into buf from a blocking fd, but puts not more than
 *  maxlen bytes in buf. Returns total number of bytes read ( > 0),
 *  0 if eof, or < 0 if error. len > maxlen > 0 must hold.
 */
static int read_skip(int fd, char *buf, int maxlen, int len)
{
    int   i, got = 0;
    char  c;

    if ((i = read_fill(fd, buf, maxlen)) <= 0) {
        return i;
    }
    do {
        if ((i = read(fd, &c, 1)) <= 0) {
            return i;
        }
        got += i;
    } while (got < len - maxlen);
    return len;
}

time_t timestamp_seconds()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        elog(LOG_ERROR, "fatal, could not get clock_monotonic value, terminating! %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return ts.tv_sec;
}

static int heart_cmd_info_reply(time_t now)
{
    struct msg m;
    struct watchdog_info info;
    char *p = (char *) m.fill;
    int ret;
    int flags;

    int heartbeat_time_left = last_heart_beat_time + heart_beat_timeout - now;
    int wdt_pet_time_left = last_wdt_pet_time + wdt_pet_timeout - now;
    int init_handshake_time_left = init_handshake_end_time - now;
    if (init_handshake_happened || init_handshake_time_left < 0)
        init_handshake_time_left = 0;
    int init_grace_time_time_left = init_grace_end_time > now ? init_grace_end_time - now : 0;
    int snooze_time_left = snooze_end_time > now ? snooze_end_time - now : 0;

    /* The reply format is:
     *  <KEY>=<VALUE> NEWLINE
     *  ...
     */
    p += sprintf(p, "program_name=" PROGRAM_NAME "\nprogram_version=" PROGRAM_VERSION_STR "\n"
        "heartbeat_timeout=%d\n"
        "heartbeat_time_left=%d\n"
        "init_grace_time_left=%d\n"
        "snooze_time_left=%d\n"
        "wdt_pet_time_left=%d\n"
        "init_handshake_happened=%d\n"
        "init_handshake_timeout=%d\n"
        "init_handshake_time_left=%d\n",
        heart_beat_timeout, heartbeat_time_left, init_grace_time_time_left, snooze_time_left, wdt_pet_time_left,
        init_handshake_happened, (int) init_handshake_timeout, init_handshake_time_left);

    ret = ioctl(watchdog_fd, WDIOC_GETSUPPORT, &info);
    if (ret == 0) {
        p += sprintf(p, "wdt_identity=%s\n", info.identity);
        p += sprintf(p, "wdt_firmware_version=%u\n", info.firmware_version);
        p += sprintf(p, "wdt_options=");
        if (info.options & WDIOF_OVERHEAT) p += sprintf(p, "overheat,");
        if (info.options & WDIOF_FANFAULT) p += sprintf(p, "fanfault,");
        if (info.options & WDIOF_EXTERN1) p += sprintf(p, "extern1,");
        if (info.options & WDIOF_EXTERN2) p += sprintf(p, "extern2,");
        if (info.options & WDIOF_POWERUNDER) p += sprintf(p, "powerunder,");
        if (info.options & WDIOF_CARDRESET) p += sprintf(p, "cardreset,");
        if (info.options & WDIOF_POWEROVER) p += sprintf(p, "powerover,");
        if (info.options & WDIOF_SETTIMEOUT) p += sprintf(p, "settimeout,");
        if (info.options & WDIOF_MAGICCLOSE) p += sprintf(p, "magicclose,");
        if (info.options & WDIOF_PRETIMEOUT) p += sprintf(p, "pretimeout,");
        if (info.options & WDIOF_ALARMONLY) p += sprintf(p, "alarmonly,");
        if (info.options & WDIOF_KEEPALIVEPING) p += sprintf(p, "keepaliveping,");
        p += sprintf(p, "\n");
    } else {
        p += sprintf(p, "wdt_identity=none\nwdt_firmware_version=0\nwdt_options=\n");
    }

    ret = ioctl(watchdog_fd, WDIOC_GETTIMELEFT, &flags);
    if (ret != 0)
        flags = 0;
    p += sprintf(p, "wdt_time_left=%u\n", flags);

    ret = ioctl(watchdog_fd, WDIOC_GETPRETIMEOUT, &flags);
    if (ret != 0)
        flags = 0;
    p += sprintf(p, "wdt_pre_timeout=%u\n", flags);

    p += sprintf(p, "wdt_timeout=%u\n", wdt_timeout);

    flags = 0;
    ret = ioctl(watchdog_fd, WDIOC_GETBOOTSTATUS, &flags);
    if (ret != 0)
        flags = 0;
    p += sprintf(p, "wdt_last_boot=%s\n", (flags != 0 ? "watchdog" : "power_on"));

    size_t len = p - (char *) m.fill;
    m.op = HEART_CMD;
    m.len = htons(len + 1);   /* Include Op */

    return write_message(STDOUT_FILENO, &m);
}
