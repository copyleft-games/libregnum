# tests/Makefile - Libregnum Test Suite
#
# Copyright 2025 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Test suite using GLib testing framework (GTest).
#
# Usage:
#   make           - Build all tests
#   make run       - Build and run all tests
#   make test-X    - Build specific test (e.g., test-registry)
#   make clean     - Remove test binaries

# Include parent configuration
include ../config.mk
include ../rules.mk

# =============================================================================
# Test Configuration
# =============================================================================

# Test output directory
TEST_BUILDDIR := $(BUILDDIR)/tests

# Library paths (for linking and runtime)
LIBREGNUM_LIB := ../$(LIBOUTDIR)/$(LIB_SHARED)
LIBREGNUM_STATIC := ../$(LIBOUTDIR)/$(LIB_STATIC)

# Test CFLAGS
TEST_CFLAGS := $(BASE_CFLAGS) $(OPT_CFLAGS)
TEST_CFLAGS += -I../src
TEST_CFLAGS += -DLIBREGNUM_COMPILATION
TEST_CFLAGS += $(shell $(PKG_CONFIG) --cflags glib-2.0 gobject-2.0 gio-2.0 libdex-1 json-glib-1.0 gobject-introspection-1.0)

# Add dependency include paths
TEST_CFLAGS += -I$(GRAYLIB_DIR)/src
TEST_CFLAGS += -I$(GRAYLIB_DIR)/deps/raylib/src
TEST_CFLAGS += -I$(YAMLGLIB_DIR)/src

# Test LDFLAGS
TEST_LDFLAGS := -L../$(LIBOUTDIR)
TEST_LDFLAGS += -L$(GRAYLIB_DIR)/build/lib
TEST_LDFLAGS += -L$(YAMLGLIB_DIR)/build
TEST_LDFLAGS += -Wl,-rpath,'$$ORIGIN/../$(LIBOUTDIR)'
TEST_LDFLAGS += -Wl,-rpath,'$$ORIGIN/../$(GRAYLIB_DIR)/build/lib'
TEST_LDFLAGS += -Wl,-rpath,'$$ORIGIN/../$(YAMLGLIB_DIR)/build'

# Test libraries
TEST_LIBS := -l$(LIB_NAME)
TEST_LIBS += -lgraylib -lyaml-glib
TEST_LIBS += $(shell $(PKG_CONFIG) --libs glib-2.0 gobject-2.0 gio-2.0 libdex-1 gobject-introspection-1.0)
TEST_LIBS += $(PYTHON_LIBS) $(GJS_LIBS)
TEST_LIBS += -lm

# =============================================================================
# Test Sources
# =============================================================================

# Test source files
TEST_SOURCES := \
	test-engine.c \
	test-registry.c \
	test-data-loader.c \
	test-asset-manager.c \
	test-graphics.c \
	test-ecs.c \
	test-input.c \
	test-ui.c \
	test-tilemap.c \
	test-save.c \
	test-dialog.c \
	test-quest.c \
	test-audio.c \
	test-inventory.c \
	test-i18n.c \
	test-pathfinding.c \
	test-ai.c \
	test-physics.c \
	test-debug.c \
	test-mod.c \
	test-net.c \
	test-world3d.c \
	test-scene.c \
	test-primitives.c \
	test-scripting.c \
	test-scripting-python.c \
	test-scripting-gi.c \
	test-scripting-pygobject.c \
	test-scripting-gjs.c \
	test-settings.c \
	test-gamestate.c \
	test-crash.c \
	test-accessibility.c \
	test-steam.c \
	test-economy.c \
	test-idle.c \
	test-building.c \
	test-vehicle.c \
	test-particles.c \
	test-postprocess.c \
	test-animation.c \
	test-text.c \
	test-video.c \
	test-tween.c \
	test-transition.c \
	test-trigger2d.c \
	test-atlas.c \
	test-tutorial.c \
	test-weather.c \
	test-lighting.c \
	test-wave-data.c \
	test-procedural-audio.c \
	test-asset-pack.c \
	test-analytics.c \
	test-achievement.c \
	test-photomode.c \
	test-workshop.c \
	test-demo.c \
	test-dlc.c \
	test-vr.c \
	test-deckbuilder.c \
	test-object-pool.c \
	test-template-2d.c \
	test-template-3d.c \
	test-template.c \
	test-input-buffer.c \
	test-template-states.c \
	test-template-engagement.c \
	test-template-idle.c \
	test-template-deckbuilder.c

# Test executables
TEST_BINARIES := $(patsubst %.c,$(TEST_BUILDDIR)/%,$(TEST_SOURCES))

# =============================================================================
# Targets
# =============================================================================

.PHONY: all run clean

all: $(TEST_BINARIES)

run: all
	$(call print_status,"Running tests...")
	@failed=0; \
	for test in $(TEST_BINARIES); do \
		LD_LIBRARY_PATH="../$(LIBOUTDIR):$(GRAYLIB_DIR)/build/lib:$(YAMLGLIB_DIR)/build:$$LD_LIBRARY_PATH" \
		G_TEST_SRCDIR="$(shell pwd)" \
		$$test --tap || failed=1; \
	done; \
	if [ $$failed -eq 0 ]; then \
		printf "\033[32m\033[1m==>\033[0m All tests passed!\n"; \
	else \
		printf "\033[31m\033[1mERROR:\033[0m Some tests failed!\n"; \
		exit 1; \
	fi

# Pattern rule for test compilation
$(TEST_BUILDDIR)/test-%: test-%.c | $(TEST_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(TEST_CFLAGS) -o $@ $< $(TEST_LDFLAGS) $(TEST_LIBS)

# Create test build directory
$(TEST_BUILDDIR):
	@$(MKDIR_P) $@

clean:
	$(call print_status,"Cleaning test artifacts...")
	$(RM) $(TEST_BINARIES)
	@if [ -d "$(TEST_BUILDDIR)" ]; then rmdir --ignore-fail-on-non-empty "$(TEST_BUILDDIR)"; fi
