VERSION=1.1.0

EXTRA_CFLAGS=-Wall -Wextra -DPROGRAM_VERSION=$(VERSION)

ifeq ($(shell uname),Darwin)
EXTRA_CFLAGS+=-Isrc/compat
EXTRA_SRC=src/compat/compat.c
endif

all: heart

heart: src/heart.c $(EXTRA_SRC)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -o $@ $^

clean:
	$(RM) heart

.PHONY: all clean
