
LDFLAGS += -fPIC -shared -pedantic -ldl
CFLAGS ?= -fPIC -O2 -Wall -Wextra -Wno-unused-parameter

ifeq ($(shell uname),Darwin)
CFLAGS+=-I../../src/compat
endif

TARGET=heart_fixture.so

SRC=$(wildcard c_src/*.c)
OBJ=$(SRC:.c=.o)

all: $(TARGET)

$(OBJ): $(wildcard c_src/*.h)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(TARGET): $(OBJ)
	$(CC) $^ $(LDFLAGS) -o $@

clean:
	$(RM) $(TARGET) $(OBJ)

.PHONY: all clean
