VERSION=0.3.0

CFLAGS+=-Wall -Wextra -O2 -DPROGRAM_VERSION=$(VERSION)

all: heart

heart: src/heart.c
	$(CC) $(CFLAGS) -o $@ $^

install: heart
	cp $^ $(PREFIX)/lib/erlang/erts-9.3/bin/heart

clean:
	-rm -f heart

.PHONY: all clean
