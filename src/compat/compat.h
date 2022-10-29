#ifndef COMPAT_H
#define COMPAT_H

#include <signal.h>
#include <time.h>

// Missing SOCK_CLOEXEC
#define SOCK_CLOEXEC  02000000

#endif
