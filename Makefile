VERSION=2.0.1

EXTRA_CFLAGS=-Wall -Wextra -DPROGRAM_VERSION=$(VERSION)

ifeq ($(shell uname),Darwin)
EXTRA_CFLAGS+=-Isrc/compat
EXTRA_SRC=src/compat/compat.c
endif

all: heart

heart: src/heart.c $(EXTRA_SRC)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -o $@ $^

test: check
check: heart
	$(MAKE) -C tests

clean:
	$(RM) heart
	$(MAKE) -C tests clean

.PHONY: all test check clean
