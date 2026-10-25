# bench/Makefile - Libregnum frame-time benchmarks
#
# Copyright 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage:
#   make            - Build lrg-bench
#   make run        - Run every scenario (starts Xvfb when DISPLAY is unset)
#   make clean      - Remove the benchmark binary

include ../config.mk
include ../rules.mk

BENCH_BUILDDIR := $(BUILDDIR)/bench
BENCH_BIN      := $(BENCH_BUILDDIR)/lrg-bench

BENCH_CFLAGS := $(BASE_CFLAGS) $(OPT_CFLAGS)
BENCH_CFLAGS += -I../src -I$(GRAYLIB_DIR)/src -I$(GRAYLIB_DIR)/deps/raylib/src -I$(YAMLGLIB_DIR)/src
BENCH_CFLAGS += $(shell $(PKG_CONFIG) --cflags glib-2.0 gobject-2.0 gio-2.0 json-glib-1.0)
BENCH_CFLAGS += $(DEX_CFLAGS)

BENCH_LDFLAGS := -L../$(LIBOUTDIR) -Wl,-rpath,$(abspath ../$(LIBOUTDIR))
BENCH_LIBS    := -l$(LIB_NAME)
BENCH_LIBS    += $(shell $(PKG_CONFIG) --libs glib-2.0 gobject-2.0 gio-2.0 json-glib-1.0)
BENCH_LIBS    += -lm $(SANITIZER_LIBS)

# lrg-bench arguments for `make run', e.g. make run BENCH_ARGS='-s gui'
BENCH_ARGS ?=

.DEFAULT_GOAL := all
.PHONY: all run clean

all: $(BENCH_BIN)

$(BENCH_BIN): lrg-bench.c ../$(LIBOUTDIR)/$(LIB_SHARED) | $(BENCH_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(BENCH_CFLAGS) -o $@ $< $(BENCH_LDFLAGS) $(BENCH_LIBS)

$(BENCH_BUILDDIR):
	@mkdir -p $@

run: $(BENCH_BIN)
	@./run-bench.sh $(BENCH_BIN) $(BENCH_ARGS)

clean:
	rm -f $(BENCH_BIN)
