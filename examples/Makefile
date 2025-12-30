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

EXAMPLE_CFLAGS := $(COMMON_CFLAGS) $(WARN_CFLAGS) $(OPT_CFLAGS)
EXAMPLE_CFLAGS += -I../src
EXAMPLE_CFLAGS += -I$(GRAYLIB_DIR)/src
EXAMPLE_CFLAGS += -I$(GRAYLIB_DIR)/deps/raylib/src
EXAMPLE_CFLAGS += -I$(YAMLGLIB_DIR)/src
EXAMPLE_CFLAGS += $(GLIB_CFLAGS)

# Platform-specific optional dependencies
ifeq ($(TARGET_PLATFORM),windows)
    # Windows: no libdex or GIR runtime available
else
    # Linux: add optional dependencies if available
    ifneq ($(shell $(PKG_CONFIG) --exists libdex-1 2>/dev/null && echo yes),)
        EXAMPLE_CFLAGS += $(shell $(PKG_CONFIG) --cflags libdex-1)
        EXAMPLE_OPT_LIBS += $(shell $(PKG_CONFIG) --libs libdex-1)
    endif
    ifneq ($(shell $(PKG_CONFIG) --exists gobject-introspection-1.0 2>/dev/null && echo yes),)
        EXAMPLE_CFLAGS += $(shell $(PKG_CONFIG) --cflags gobject-introspection-1.0)
    endif
endif

EXAMPLE_LDFLAGS := -L../$(LIBOUTDIR)
EXAMPLE_LDFLAGS += -L$(GRAYLIB_DIR)/build/lib
EXAMPLE_LDFLAGS += -L$(YAMLGLIB_DIR)/build
ifeq ($(TARGET_PLATFORM),windows)
    # Windows: no rpath, DLLs must be in PATH or same directory
else
    EXAMPLE_LDFLAGS += -Wl,-rpath,'$$ORIGIN/../$(LIBOUTDIR)'
    EXAMPLE_LDFLAGS += -Wl,-rpath,'$$ORIGIN/../$(GRAYLIB_DIR)/build/lib'
    EXAMPLE_LDFLAGS += -Wl,-rpath,'$$ORIGIN/../$(YAMLGLIB_DIR)/build'
endif

EXAMPLE_LIBS := -l$(LIB_NAME) -lgraylib -lyaml-glib -lm
EXAMPLE_LIBS += $(GLIB_LIBS) $(EXAMPLE_OPT_LIBS)
ifeq ($(TARGET_PLATFORM),windows)
    EXAMPLE_LIBS += $(PLATFORM_LIBS)
endif

# Base examples (always built)
EXAMPLES := game-omnomagon game-taco-racing game-chocolate-chip-clicker game-tuktuk-derby render-yaml-santa render-yaml-taco-truck

# Scripted examples only on native Linux (scripting backends not available on Windows)
ifneq ($(TARGET_PLATFORM),windows)
    EXAMPLES += scripted-lua-game scripted-python-game scripted-python-gobject-game scripted-gjs-game
endif

EXAMPLE_BINARIES := $(patsubst %,$(EXAMPLES_BUILDDIR)/%$(EXE_EXT),$(EXAMPLES))

# MinGW sysroot for Windows DLLs
MINGW_BIN := /usr/x86_64-w64-mingw32/sys-root/mingw/bin

.PHONY: all clean copy-dlls

ifeq ($(TARGET_PLATFORM),windows)
all: $(EXAMPLE_BINARIES) copy-dlls
else
all: $(EXAMPLE_BINARIES)
endif

# Copy required DLLs for Windows builds
copy-dlls: | $(EXAMPLES_BUILDDIR)
ifeq ($(TARGET_PLATFORM),windows)
	$(call print_status,"Copying DLLs for Windows...")
	@cp ../$(LIBOUTDIR)/libregnum.dll $(EXAMPLES_BUILDDIR)/
	@cp $(GRAYLIB_DIR)/build/lib/graylib.dll $(EXAMPLES_BUILDDIR)/
	@cp $(YAMLGLIB_DIR)/build/yaml-glib.dll $(EXAMPLES_BUILDDIR)/
	@cp $(MINGW_BIN)/libglib-2.0-0.dll $(EXAMPLES_BUILDDIR)/
	@cp $(MINGW_BIN)/libgobject-2.0-0.dll $(EXAMPLES_BUILDDIR)/
	@cp $(MINGW_BIN)/libgio-2.0-0.dll $(EXAMPLES_BUILDDIR)/
	@cp $(MINGW_BIN)/libgmodule-2.0-0.dll $(EXAMPLES_BUILDDIR)/
	@cp $(MINGW_BIN)/libintl-8.dll $(EXAMPLES_BUILDDIR)/
	@cp $(MINGW_BIN)/iconv.dll $(EXAMPLES_BUILDDIR)/
	@cp $(MINGW_BIN)/libpcre2-8-0.dll $(EXAMPLES_BUILDDIR)/
	@cp $(MINGW_BIN)/libffi-8.dll $(EXAMPLES_BUILDDIR)/
	@cp $(MINGW_BIN)/zlib1.dll $(EXAMPLES_BUILDDIR)/
	@cp $(MINGW_BIN)/libjson-glib-1.0-0.dll $(EXAMPLES_BUILDDIR)/
	@cp $(MINGW_BIN)/libyaml-0-2.dll $(EXAMPLES_BUILDDIR)/
	@cp $(MINGW_BIN)/libgcc_s_seh-1.dll $(EXAMPLES_BUILDDIR)/
	@cp $(MINGW_BIN)/libwinpthread-1.dll $(EXAMPLES_BUILDDIR)/
endif

$(EXAMPLES_BUILDDIR)/game-omnomagon$(EXE_EXT): game-omnomagon.c | $(EXAMPLES_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(EXAMPLE_CFLAGS) -o $@ $< $(EXAMPLE_LDFLAGS) $(EXAMPLE_LIBS)

$(EXAMPLES_BUILDDIR)/game-taco-racing$(EXE_EXT): game-taco-racing.c | $(EXAMPLES_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(EXAMPLE_CFLAGS) -o $@ $< $(EXAMPLE_LDFLAGS) $(EXAMPLE_LIBS)

$(EXAMPLES_BUILDDIR)/game-chocolate-chip-clicker$(EXE_EXT): game-chocolate-chip-clicker.c | $(EXAMPLES_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(EXAMPLE_CFLAGS) -o $@ $< $(EXAMPLE_LDFLAGS) $(EXAMPLE_LIBS)

$(EXAMPLES_BUILDDIR)/game-tuktuk-derby$(EXE_EXT): game-tuktuk-derby.c | $(EXAMPLES_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(EXAMPLE_CFLAGS) -o $@ $< $(EXAMPLE_LDFLAGS) $(EXAMPLE_LIBS)

$(EXAMPLES_BUILDDIR)/render-yaml-santa$(EXE_EXT): render-yaml-santa.c | $(EXAMPLES_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(EXAMPLE_CFLAGS) -o $@ $< $(EXAMPLE_LDFLAGS) $(EXAMPLE_LIBS)

$(EXAMPLES_BUILDDIR)/render-yaml-taco-truck$(EXE_EXT): render-yaml-taco-truck.c | $(EXAMPLES_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(EXAMPLE_CFLAGS) -o $@ $< $(EXAMPLE_LDFLAGS) $(EXAMPLE_LIBS)

$(EXAMPLES_BUILDDIR)/scripted-lua-game$(EXE_EXT): scripted-lua-game.c | $(EXAMPLES_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(EXAMPLE_CFLAGS) -o $@ $< $(EXAMPLE_LDFLAGS) $(EXAMPLE_LIBS)

$(EXAMPLES_BUILDDIR)/scripted-python-game$(EXE_EXT): scripted-python-game.c | $(EXAMPLES_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(EXAMPLE_CFLAGS) -o $@ $< $(EXAMPLE_LDFLAGS) $(EXAMPLE_LIBS)

$(EXAMPLES_BUILDDIR)/scripted-python-gobject-game$(EXE_EXT): scripted-python-gobject-game.c | $(EXAMPLES_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(EXAMPLE_CFLAGS) -o $@ $< $(EXAMPLE_LDFLAGS) $(EXAMPLE_LIBS)

$(EXAMPLES_BUILDDIR)/scripted-gjs-game$(EXE_EXT): scripted-gjs-game.c | $(EXAMPLES_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(EXAMPLE_CFLAGS) -o $@ $< $(EXAMPLE_LDFLAGS) $(EXAMPLE_LIBS)

$(EXAMPLES_BUILDDIR):
	@$(MKDIR_P) $@

clean:
	$(call print_status,"Cleaning examples...")
	@$(RM) $(EXAMPLE_BINARIES)
	@$(RM) $(EXAMPLES_BUILDDIR)/*.dll
	@if [ -d "$(EXAMPLES_BUILDDIR)" ]; then rmdir --ignore-fail-on-non-empty "$(EXAMPLES_BUILDDIR)"; fi
