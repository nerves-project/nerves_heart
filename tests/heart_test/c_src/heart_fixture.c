#define _GNU_SOURCE // for RTLD_NEXT
#include <stdio.h>
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h>
#include <err.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/watchdog.h>

#ifndef __APPLE__
#define ORIGINAL(name) original_##name
#define REPLACEMENT(name) name
#define OVERRIDE(ret, name, args) \
    static ret (*original_##name) args; \
    __attribute__((constructor)) void init_##name() { ORIGINAL(name) = dlsym(RTLD_NEXT, #name); } \
    ret REPLACEMENT(name) args

#define REPLACE(ret, name, args) \
    ret REPLACEMENT(name) args
#else
#define ORIGINAL(name) name
#define REPLACEMENT(name) new_##name
#define OVERRIDE(ret, name, args) \
    ret REPLACEMENT(name) args; \
    __attribute__((used)) static struct { const void *original; const void *replacement; } _interpose_##name \
    __attribute__ ((section ("__DATA,__interpose"))) = { (const void*)(unsigned long)&REPLACEMENT(name), (const void*)(unsigned long)&ORIGINAL(name) }; \
    ret REPLACEMENT(name) args

#define REPLACE(ret, name, args) OVERRIDE(ret, name, args)
#endif

// Special file handle for watchdog operations
#define WATCHDOG_FILENO 9999

static int to_elixir_fd = -1;
static int open_tries = 0;
static int wdt_timeout = 0;

static void flog(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);

    char buffer[256];
    int count = vsnprintf(buffer, sizeof(buffer), format, ap);
    write(to_elixir_fd, buffer, count);

    va_end(ap);
}

__attribute__((constructor)) void fixture_init()
{
    char *report_path = getenv("HEART_REPORT_PATH");
    open_tries = atoi(getenv("HEART_WATCHDOG_OPEN_TRIES"));
    wdt_timeout = atoi(getenv("WDT_TIMEOUT"));

    to_elixir_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (to_elixir_fd < 0)
        err(EXIT_FAILURE, "socket");

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, report_path, sizeof(addr.sun_path) - 1);

    if (connect(to_elixir_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        err(EXIT_FAILURE, "fixture can't connect to %s", report_path);

    // Don't wrap child processes
    unsetenv("LD_PRELOAD");
    unsetenv("DYLD_INSERT_LIBRARIES");
}

REPLACE(void, sync, ())
{
    flog("sync()");
}

REPLACE(int, reboot, (int cmd))
{
    flog("reboot(0x%08x)", cmd);
    exit(0);
}

REPLACE(int, kill, (pid_t pid, int sig))
{
    flog("kill(%d, %d)", pid, sig);
    return 0;
}

OVERRIDE(ssize_t, write, (int fildes, const void *buf, size_t nbyte))
{
    if (fildes == WATCHDOG_FILENO) {
        flog("pet(%d)", (int) nbyte);
        return nbyte;
    }

    return ORIGINAL(write)(fildes, buf, nbyte);
}

OVERRIDE(int, select, (int nfds, fd_set *readfds, fd_set *writefds, fd_set *errorfds, struct timeval *timeout))
{
    return ORIGINAL(select)(nfds, readfds, writefds, errorfds, timeout);
}

OVERRIDE(int, open, (const char *pathname, int flags, ...))
{
    int mode;

    va_list ap;
    va_start(ap, flags);
    if (flags & O_CREAT)
        mode = va_arg(ap, int);
    else
        mode = 0;
    va_end(ap);

    // Log to stderr if opened for write (read is stubbed)
    if (strcmp(pathname, "/dev/kmsg") == 0 && (flags & (O_RDWR|O_WRONLY)))
        return dup(STDERR_FILENO);

    if (strncmp(pathname, "/dev/watchdog", 13) == 0) {
        if (open_tries <= 0) {
            flog("open(%s) succeeded", pathname);
            return WATCHDOG_FILENO;
        } else {
            flog("open(%s) failed", pathname);
            open_tries--;
            return -1;
        }
    }

    return ORIGINAL(open)(pathname, flags, mode);
}

OVERRIDE(unsigned int, sleep, (unsigned int seconds))
{
    if (seconds >= 2) {
        // This is from the emulated sigtimedwait
        return ORIGINAL(sleep)(seconds);
    } else {
        flog("sleep(%u)", seconds);
        return 0;
    }
}

REPLACE(int, ioctl, (int fd, unsigned long request, ...))
{
    (void) fd;
    va_list ap;
    va_start(ap, request);

    switch (request) {
    case WDIOC_GETSUPPORT:
        {
            struct watchdog_info *info = va_arg(ap, struct watchdog_info *);

            info->options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING;
            info->firmware_version = 0;
            strcpy((char *) info->identity, "OMAP Watchdog");

            break;
        }
    case WDIOC_GETSTATUS:
        {
            int *v = va_arg(ap, int *);
            *v = 0;
            break;
        }
    case WDIOC_GETBOOTSTATUS:
        {
            int *v = va_arg(ap, int *);
            *v = 0;
            break;
        }
    case WDIOC_GETTEMP:
        {
            int *v = va_arg(ap, int *);
            *v = 0;
            break;
        }
    case WDIOC_SETOPTIONS:
        {
            int *v = va_arg(ap, int *);
            *v = 0;
            break;
        }
    case WDIOC_KEEPALIVE:
        {
            int *v = va_arg(ap, int *);
            *v = 0;
            break;
        }
    case WDIOC_SETTIMEOUT:
        {
            int *v = va_arg(ap, int *);
            *v = 0;
            break;
        }
    case WDIOC_GETTIMEOUT:
        {
            int *v = va_arg(ap, int *);
            *v = wdt_timeout;
            break;
        }
    case WDIOC_SETPRETIMEOUT:
        {
            int *v = va_arg(ap, int *);
            *v = 0;
            break;
        }
    case WDIOC_GETPRETIMEOUT:
        {
            int *v = va_arg(ap, int *);
            *v = 0;
            break;
        }
    case WDIOC_GETTIMELEFT:
        {
            int *v = va_arg(ap, int *);
            *v = wdt_timeout - 4;
            break;
        }

    default:
        flog("unknown ioctl(0x%08lx)", request);
        break;
    }
    va_end(ap);
    return 0;
}

