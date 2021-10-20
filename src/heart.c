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

#define ERL_CRASH_DUMP_SECONDS_ENV "ERL_CRASH_DUMP_SECONDS"
#define HEART_KILL_SIGNAL          "HEART_KILL_SIGNAL"
#define HEART_WATCHDOG_PATH        "HEART_WATCHDOG_PATH"
#define HEART_NO_KILL              "HEART_NO_KILL"

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
#define  SELECT_TIMEOUT               5  /* Every 5 seconds we reset the
					    watchdog timer */

/* heart_beat_timeout is the maximum gap in seconds between two
   consecutive heart beat messages from Erlang. */

int heart_beat_timeout = 60;
/* All current platforms have a process identifier that
   fits in an unsigned long and where 0 is an impossible or invalid value */
pid_t heart_beat_kill_pid = 0;

/* reasons for reboot */
#define  R_TIMEOUT          (1)
#define  R_CLOSED           (2)
#define  R_ERROR            (3)
#define  R_SHUT_DOWN        (4)
#define  R_CRASHING         (5) /* Doing a crash dump and we will wait for it */


/*  macros */

#define  NULLFDS  ((fd_set *) NULL)

/*  prototypes */

static int message_loop();
static void do_terminate(int);
static int notify_ack();
static int heart_cmd_info_reply();
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

static void print_log(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);

    char buffer[256];
    int len = vsnprintf(buffer, sizeof(buffer), format, ap);
    if (len > 0) {
        int ignore = write(STDERR_FILENO, buffer, len);
        (void) ignore;
    }
    va_end(ap);
}

static int is_env_set(char *key)
{
    return getenv(key) != NULL;
}

static char *get_env(char *key)
{
    return getenv(key);
}

static void pet_watchdog()
{
    /* The watchdog device sometimes takes a bit to appear, so give it a few tries. */
    if (watchdog_fd < 0) {
        if (watchdog_open_retries <= 0)
            return;

        char *watchdog_path = get_env(HEART_WATCHDOG_PATH);
        if (watchdog_path == NULL) {
            watchdog_path = watchdog_path_default;
        }
        watchdog_fd = open(watchdog_path, O_WRONLY);
        if (watchdog_fd > 0) {
            print_log("heart: kernel watchdog activated (interval %ds)", SELECT_TIMEOUT);
        } else {
            watchdog_open_retries--;
            if (watchdog_open_retries <= 0)
                print_log("heart: can't open '%s'. Running without kernel watchdog: %s", watchdog_path, strerror(errno));
            return;
        }
    }

    if (write(watchdog_fd, "\0", 1) < 0)
        print_log("heart: error petting watchdog: %s", strerror(errno));
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

int main(int argc, char **argv)
{
    // See if we can log directly to the kernel log. If so, then use it for
    // warnings and errors since that will get them to the Elixir logger via
    // Nerves.Runtime or give them the best chance of being seen if everything
    // else is broke.
    int log_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (log_fd >= 0) {
        dup2(log_fd, STDERR_FILENO);
        close(log_fd);
    }

    print_log("heart: " PROGRAM_NAME " v" PROGRAM_VERSION_STR " started.");

    get_arguments(argc, argv);
    notify_ack();
    do_terminate(message_loop());

    return 0;
}

/*
 * message loop
 */
static int message_loop()
{
    int   i;
    time_t now, last_received;
    fd_set read_fds;
    int   max_fd;
    struct timeval timeout;
    int   tlen;           /* total message length */
    struct msg m, *mp = &m;

    last_received = timestamp_seconds();
    max_fd = STDIN_FILENO;

    while (1) {
        FD_ZERO(&read_fds);         /* ZERO on each turn */
        FD_SET(STDIN_FILENO, &read_fds);
        timeout.tv_sec = SELECT_TIMEOUT;  /* On Linux timeout is modified by select */
        timeout.tv_usec = 0;
        if ((i = select(max_fd + 1, &read_fds, NULLFDS, NULLFDS, &timeout)) < 0) {
            print_log("heart: select failed: %s", strerror(errno));
            return R_ERROR;
        }

        now = timestamp_seconds();
        if (now > last_received + heart_beat_timeout) {
            print_log("heart: heartbeat timeout -> no activity for %lu seconds",
                  (unsigned long) (now - last_received));
            return R_TIMEOUT;
        }
        /*
         * Do not check fd-bits if select timeout
         */
        if (i == 0) {
            pet_watchdog();
            continue;
        }
        /*
         * Message from ERLANG
         */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if ((tlen = read_message(STDIN_FILENO, mp)) < 0) {
                print_log("heart: error from read_message:  %s", strerror(errno));
                return R_ERROR;
            }
            if ((tlen > MSG_HDR_SIZE) && (tlen <= MSG_TOTAL_SIZE)) {
                switch (mp->op) {
                case HEART_BEAT:
                    pet_watchdog();
                    last_received = timestamp_seconds();
                    break;
                case SHUT_DOWN:
                    return R_SHUT_DOWN;
                case SET_CMD:
                    /* If the user specifies "disable", turn off the hw watchdog petter to verify that the system reboots. */
                    if (mp->len > 7 && memcmp(mp->fill, "disable", 7) == 0) {
                        print_log("heart: Petting of the hardware watchdog is disabled. System should reboot momentarily.");

                        /* Disable petting of the hardware watchdog */
                        watchdog_open_retries = 0;
                        watchdog_fd = -1;
                    }
                    notify_ack();
                    break;
                case CLEAR_CMD:
                    /* Not supported */
                    notify_ack();
                    break;
                case GET_CMD:
                    /* Return information about heart */
                    heart_cmd_info_reply();
                    break;
                case PREPARING_CRASH:
                    /* Erlang has reached a crushdump point (is crashing for sure) */
                    print_log("heart: Erlang is crashing .. (waiting for crash dump file)");
                    return R_CRASHING;
                default:
                    /* ignore all other messages */
                    break;
                }
            } else if (tlen == 0) {
                /* Erlang has closed its end */
                print_log("heart: Erlang has closed.");
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
            print_log("heart: Wait 5 seconds for Erlang to terminate nicely");
            for (i=0; i < 5; ++i) {
               res = kill(heart_beat_kill_pid, 0); /* check if alive */
               if (res < 0 && errno == ESRCH)
                  return;
              sleep(1);
            }
           print_log("heart: Erlang still alive, kill it");
        }

        envvar = get_env(HEART_KILL_SIGNAL);
        if (envvar && strcmp(envvar, "SIGABRT") == 0) {
            print_log("heart: kill signal SIGABRT requested");
            sig = SIGABRT;
        }

        res = kill(heart_beat_kill_pid, sig);
        for (i = 0; i < 5 && res == 0; ++i) {
            sleep(1);
            res = kill(heart_beat_kill_pid, sig);
        }
        if (errno != ESRCH) {
            print_log("heart: Unable to kill old process, "
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
        break;
    case R_CRASHING:
        if (is_env_set(ERL_CRASH_DUMP_SECONDS_ENV)) {
            const char *tmo_env = get_env(ERL_CRASH_DUMP_SECONDS_ENV);
            int tmo = atoi(tmo_env);
            print_log("heart: waiting for dump - timeout set to %d seconds.", tmo);
            wait_until_close_write_or_env_tmo(tmo);
        }
    /* fall through */
    case R_TIMEOUT:
    case R_CLOSED:
    case R_ERROR:
    default:
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
        print_log("heart: select failed:  %s", strerror(errno));
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
        print_log("heart: fatal, could not get clock_monotonic value, terminating! %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return ts.tv_sec;
}

static int heart_cmd_info_reply()
{
    struct msg m;
    struct watchdog_info info;
    char *p = (char *) m.fill;
    int ret;
    int flags;

    /* The reply format is:
     *  <KEY>=<VALUE> NEWLINE
     *  ...
     */
    p += sprintf(p, "program_name=" PROGRAM_NAME "\nprogram_version=" PROGRAM_VERSION_STR "\n");

    ret = ioctl(watchdog_fd, WDIOC_GETSUPPORT, &info);
    if (ret == 0) {
        p += sprintf(p, "identity=%s\n", info.identity);
        p += sprintf(p, "firmware_version=%u\n", info.firmware_version);
        p += sprintf(p, "options=0x%08x\n", info.options);
    }
    ret = ioctl(watchdog_fd, WDIOC_GETTIMELEFT, &flags);
    if (ret == 0)
        p += sprintf(p, "time_left=%u\n", flags);

    ret = ioctl(watchdog_fd, WDIOC_GETPRETIMEOUT, &flags);
    if (ret == 0)
        p += sprintf(p, "pre_timeout=%u\n", flags);

    ret = ioctl(watchdog_fd, WDIOC_GETTIMEOUT, &flags);
    if (ret == 0)
        p += sprintf(p, "timeout=%u\n", flags);

    flags = 0;
    ret = ioctl(watchdog_fd, WDIOC_GETBOOTSTATUS, &flags);
    if (ret == 0)
        p += sprintf(p, "last_boot=%s\n", (flags != 0 ? "watchdog" : "power_on"));

    size_t len = p - (char *) m.fill;
    m.op = HEART_CMD;
    m.len = htons(len + 1);   /* Include Op */

    return write_message(STDOUT_FILENO, &m);
}
