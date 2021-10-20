VERSION=1.0.0

ADDITIONAL_CFLAGS=-Wall -Wextra -DPROGRAM_VERSION=$(VERSION)

all: heart

heart: src/heart.c
	$(CC) $(CFLAGS) $(ADDITIONAL_CFLAGS) -o $@ $^

install: heart
	cp $^ $(PREFIX)/lib/erlang/erts-9.3/bin/heart

clean:
	-rm -f heart

.PHONY: all clean
