# examples/Makefile
#
# Copyright 2025 Zach Podbielniak
#
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Makefile for building libregnum example programs.

include ../config.mk
include ../rules.mk

EXAMPLES_BUILDDIR := $(BUILDDIR)/examples
LIBREGNUM_LIB := ../$(LIBOUTDIR)/$(LIB_SHARED)

EXAMPLE_CFLAGS := $(COMMON_CFLAGS) $(WARN_CFLAGS)
EXAMPLE_CFLAGS += -I../src
EXAMPLE_CFLAGS += -I$(GRAYLIB_DIR)/src
EXAMPLE_CFLAGS += -I$(YAMLGLIB_DIR)/src
EXAMPLE_CFLAGS += $(shell $(PKG_CONFIG) --cflags glib-2.0 gobject-2.0 gio-2.0 libdex-1)

EXAMPLE_LDFLAGS := -L../$(LIBOUTDIR)
EXAMPLE_LDFLAGS += -L$(GRAYLIB_DIR)/build/lib
EXAMPLE_LDFLAGS += -L$(YAMLGLIB_DIR)/build
EXAMPLE_LDFLAGS += -Wl,-rpath,'$$ORIGIN/../$(LIBOUTDIR)'
EXAMPLE_LDFLAGS += -Wl,-rpath,'$$ORIGIN/../$(GRAYLIB_DIR)/build/lib'
EXAMPLE_LDFLAGS += -Wl,-rpath,'$$ORIGIN/../$(YAMLGLIB_DIR)/build'

EXAMPLE_LIBS := -l$(LIB_NAME) -lgraylib -lyaml-glib -lm
EXAMPLE_LIBS += $(shell $(PKG_CONFIG) --libs glib-2.0 gobject-2.0 gio-2.0 libdex-1)

EXAMPLES := game-omnomagon

EXAMPLE_BINARIES := $(patsubst %,$(EXAMPLES_BUILDDIR)/%,$(EXAMPLES))

.PHONY: all clean

all: $(EXAMPLE_BINARIES)

$(EXAMPLES_BUILDDIR)/game-omnomagon: game-omnomagon.c | $(EXAMPLES_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(EXAMPLE_CFLAGS) -o $@ $< $(EXAMPLE_LDFLAGS) $(EXAMPLE_LIBS)

$(EXAMPLES_BUILDDIR):
	@$(MKDIR_P) $@

clean:
	$(call print_status,"Cleaning examples...")
	@$(RM) $(EXAMPLE_BINARIES)
	@if [ -d "$(EXAMPLES_BUILDDIR)" ]; then rmdir --ignore-fail-on-non-empty "$(EXAMPLES_BUILDDIR)"; fi
