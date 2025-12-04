# SPDX-FileCopyrightText: 2018 Frank Hunleth
#
# SPDX-License-Identifier: Apache-2.0
#
VERSION=2.4.0

EXTRA_CFLAGS=-Wall -Wextra -DPROGRAM_VERSION=$(VERSION)

ifeq ($(shell uname),Darwin)
EXTRA_CFLAGS+=-Isrc/compat
EXTRA_SRC=
endif

all: heart

heart: src/heart.c src/elog.c $(EXTRA_SRC)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -o $@ $^

test: check
check: heart
	$(MAKE) -C tests

clean:
	$(RM) heart
	$(MAKE) -C tests clean

.PHONY: all test check clean
