# SPDX-FileCopyrightText: 2022 Nerves Project Developers
#
# SPDX-License-Identifier: Apache-2.0

# Makefile for building the test fixture shared library
#
# Makefile targets:
#
# all/install   build and install the NIF
# clean         clean build products and intermediates
#
# Variables to override:
#
# MIX_APP_PATH  path to the build directory
# MIX_ENV       Mix build environment - "test" forces use of the stub
#
# CC            C compiler
# CROSSCOMPILE	crosscompiler prefix, if any
# CFLAGS	compiler flags for compiling all C files
# ERL_CFLAGS	additional compiler flags for files using Erlang header files
# ERL_EI_INCLUDE_DIR include path to ei.h (Required for crosscompile)
# ERL_EI_LIBDIR path to libei.a (Required for crosscompile)
# LDFLAGS	linker flags for linking all binaries
# ERL_LDFLAGS	additional linker flags for projects referencing Erlang libraries

PREFIX = $(MIX_APP_PATH)/priv
BUILD  = $(MIX_APP_PATH)/obj

TARGET = $(PREFIX)/heart_fixture.so

CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter -pedantic

# Check that we're on a supported build platform
ifeq ($(CROSSCOMPILE),)
    # Not crosscompiling, so check if MacOS or Linux.
    ifeq ($(shell uname),Darwin)
	CFLAGS+=-I../../src/compat
        LDFLAGS += -dynamiclib
    else
        CFLAGS += -fPIC
        LDFLAGS += -fPIC -shared -ldl
    endif
else
# Crosscompiled build
$(error Crosscompilation of regression tests not supported)
endif

SRC = $(wildcard c_src/*.c)
OBJ = $(SRC:c_src/%.c=$(BUILD)/%.o)

calling_from_make:
	mix compile

all: install

install: $(PREFIX) $(BUILD) $(TARGET)

$(OBJ): $(wildcard c_src/*.h)

$(BUILD)/%.o: c_src/%.c
	@echo " CC $(notdir $@)"
	$(CC) -c $(CFLAGS) -o $@ $<

$(TARGET): $(OBJ)
	@echo " LD $(notdir $@)"
	$(CC) $^ $(LDFLAGS) -o $@

$(PREFIX) $(BUILD):
	mkdir -p $@

clean:
	mix clean

mix_clean:
	$(RM) $(TARGET) $(OBJ)

.PHONY: all clean mix_clean calling_from_make install

# Don't echo commands unless the caller exports "V=1"
${V}.SILENT:
