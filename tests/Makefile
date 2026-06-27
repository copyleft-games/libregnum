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
TEST_CFLAGS += $(shell $(PKG_CONFIG) --cflags glib-2.0 gobject-2.0 gio-2.0 libdex-1 json-glib-1.0 girepository-2.0)

# Add dependency include paths
TEST_CFLAGS += -I$(GRAYLIB_DIR)/src
TEST_CFLAGS += -I$(GRAYLIB_DIR)/deps/raylib/src
TEST_CFLAGS += -I$(YAMLGLIB_DIR)/src

# Test LDFLAGS
TEST_LDFLAGS := -L../$(LIBOUTDIR)
TEST_LDFLAGS += -Wl,-rpath,'$$ORIGIN/../$(LIBOUTDIR)'

# Test libraries (graylib and yaml-glib are embedded in libregnum)
TEST_LIBS := -l$(LIB_NAME)
TEST_LIBS += $(shell $(PKG_CONFIG) --libs glib-2.0 gobject-2.0 gio-2.0 libdex-1 girepository-2.0)
TEST_LIBS += $(PYTHON_LIBS) $(GJS_LIBS)
TEST_LIBS += -lm
# Link sanitizers into the test binaries when SANITIZE/ASAN/UBSAN is active
# (SANITIZER_LIBS is empty for a normal build, so this is a no-op then).
TEST_LIBS += $(SANITIZER_LIBS)

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
	test-font-manager.c \
	test-video.c \
	test-tween.c \
	test-keyframe-curve.c \
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
	test-template-deckbuilder.c \
	test-chart.c \
	test-game-module.c \
	test-configurable.c \
	test-lrgldr.c \
	test-standalone-host.c \
	test-editor.c \
	test-gif-recorder.c \
	test-text-baker.c \
	test-image-canvas.c \
	test-vector-image.c \
	test-reel.c \
	test-term.c

# CAD test sources (conditional on CAD=1)
ifeq ($(CAD),1)
TEST_SOURCES += \
	test-cad.c

TEST_CFLAGS += $(CAD_CFLAGS)
TEST_LIBS += $(CAD_LIBS)
endif

# MCP test sources (conditional on MCP=1)
ifeq ($(MCP),1)
TEST_SOURCES += \
	test-mcp-server.c \
	test-mcp-tools.c \
	test-mcp-resources.c

TEST_CFLAGS += $(MCP_CFLAGS)
TEST_CFLAGS += $(shell pkg-config --cflags libsoup-3.0)
TEST_LDFLAGS += -L$(MCP_GLIB_DIR)/build
TEST_LDFLAGS += -Wl,-rpath,$(MCP_GLIB_DIR)/build
TEST_LIBS += $(MCP_LIBS)
TEST_LIBS += $(shell pkg-config --libs libsoup-3.0)
endif

# Test executables
TEST_BINARIES := $(patsubst %.c,$(TEST_BUILDDIR)/%,$(TEST_SOURCES))

# The lrgldr loader binary (built by the root) and the fixture game module used
# by test-lrgldr. Their absolute paths are baked into the test at compile time.
LRGLDR_BIN     := $(abspath ../$(BUILDDIR)/lrgldr$(EXE_EXT))
FIXTURE_SO     := $(TEST_BUILDDIR)/test-args-module.so
FIXTURE_SO_ABS := $(abspath $(FIXTURE_SO))

# Flags for the fixture module: a libregnum *consumer* (no LIBREGNUM_COMPILATION),
# built as a position-independent shared object whose lrg_*/g_* symbols resolve
# from the host (lrgldr) at load time.
FIXTURE_CFLAGS := $(BASE_CFLAGS) $(OPT_CFLAGS) -fPIC -I../src -DLRG_GAME_BUILD_MODULE=1
FIXTURE_CFLAGS += $(shell $(PKG_CONFIG) --cflags glib-2.0 gobject-2.0 gio-2.0 libdex-1 json-glib-1.0 girepository-2.0)
FIXTURE_CFLAGS += -I$(GRAYLIB_DIR)/src -I$(GRAYLIB_DIR)/deps/raylib/src -I$(YAMLGLIB_DIR)/src

# Binaries to run under valgrind. The test-scripting* binaries dlopen CPython
# and GJS, whose runtimes emit large benign leak footprints that drown the
# signal, so they are excluded by default. Override with VALGRIND_TESTS to
# run a custom set (e.g. VALGRIND_TESTS="$(TEST_BINARIES)" for everything).
VALGRIND_BINARIES ?= $(filter-out $(TEST_BUILDDIR)/test-scripting%,$(TEST_BINARIES))

# Binaries to run under ASan+UBSan (make test-sanitize / run-sanitize). The
# test-scripting* binaries are excluded for the same reason as valgrind above
# -- the dlopen'd CPython/GJS runtimes leak heavily (observed ~840 KB and
# ~2.4 MB) and drown any real libregnum finding. Override with SANITIZE_TESTS
# to run a custom set (e.g. SANITIZE_TESTS="$(TEST_BINARIES)" for everything).
SANITIZE_BINARIES ?= $(filter-out $(TEST_BUILDDIR)/test-scripting%,$(TEST_BINARIES))

# =============================================================================
# Targets
# =============================================================================

.PHONY: all run run-sanitize run-valgrind clean

all: $(TEST_BINARIES)

# MCP library path (conditional)
ifeq ($(MCP),1)
MCP_LD_PATH := $(MCP_GLIB_DIR)/build:
else
MCP_LD_PATH :=
endif

run: all
	$(call print_status,"Running tests...")
	@failed=0; \
	for test in $(TEST_BINARIES); do \
		LD_LIBRARY_PATH="$(MCP_LD_PATH)../$(LIBOUTDIR):$$LD_LIBRARY_PATH" \
		G_TEST_SRCDIR="$(shell pwd)" \
		$$test --tap || failed=1; \
	done; \
	if [ $$failed -eq 0 ]; then \
		printf "\033[32m\033[1m==>\033[0m All tests passed!\n"; \
	else \
		printf "\033[31m\033[1mERROR:\033[0m Some tests failed!\n"; \
		exit 1; \
	fi

# Run the suite under ASan+UBSan. Requires the library and tests to have
# been built with SANITIZE=1 (the build/sanitize/ directory), which sets
# G_SLICE=always-malloc + G_DEBUG=gc-friendly so GLib's slice allocator does
# not hide individual allocations from the tools. The test-scripting* binaries
# are excluded (see SANITIZE_BINARIES) and confirmed-benign leaks are filtered
# via tests/lsan.supp (see LSAN_OPTIONS=suppressions= below). Note: leak
# suppressions go in LSAN_OPTIONS (the `leak:` format), NOT ASAN_OPTIONS --
# ASan's suppressions= only accepts interceptor types and rejects `leak:`.
run-sanitize: all
	$(call print_status,"Running tests under ASan+UBSan...")
	@failed=0; \
	for test in $(SANITIZE_BINARIES); do \
		ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:print_summary=1" \
		LSAN_OPTIONS="suppressions=$(CURDIR)/lsan.supp" \
		UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0" \
		G_SLICE=always-malloc \
		G_DEBUG=gc-friendly \
		LD_LIBRARY_PATH="$(MCP_LD_PATH)../$(LIBOUTDIR):$$LD_LIBRARY_PATH" \
		G_TEST_SRCDIR="$(shell pwd)" \
		$$test --tap || failed=1; \
	done; \
	if [ $$failed -eq 0 ]; then \
		printf "\033[32m\033[1m==>\033[0m All sanitized tests passed!\n"; \
	else \
		printf "\033[31m\033[1mERROR:\033[0m Some sanitized tests failed!\n"; \
		exit 1; \
	fi

# Run the suite under valgrind. Expects a plain (non-sanitized) DEBUG=1
# build -- ASan-instrumented binaries conflict with valgrind. The
# test-scripting* binaries are excluded by default (see VALGRIND_BINARIES).
run-valgrind: all
	$(call print_status,"Running tests under valgrind...")
	@command -v valgrind >/dev/null 2>&1 || { \
		printf "\033[31m\033[1mERROR:\033[0m valgrind not found on PATH\n"; exit 1; }; \
	failed=0; \
	for test in $(VALGRIND_BINARIES); do \
		printf "\n\033[1m==>\033[0m %s\n" "$$test"; \
		G_SLICE=always-malloc \
		G_DEBUG=gc-friendly \
		LD_LIBRARY_PATH="$(MCP_LD_PATH)../$(LIBOUTDIR):$$LD_LIBRARY_PATH" \
		G_TEST_SRCDIR="$(shell pwd)" \
		valgrind --error-exitcode=1 --leak-check=full \
			--show-leak-kinds=definite --run-libc-freeres=yes --num-callers=30 \
			--suppressions=$(CURDIR)/valgrind.supp \
			$$test --tap || failed=1; \
	done; \
	if [ $$failed -eq 0 ]; then \
		printf "\n\033[32m\033[1m==>\033[0m All valgrind runs clean!\n"; \
	else \
		printf "\n\033[31m\033[1mERROR:\033[0m valgrind reported errors or definite leaks!\n"; \
		exit 1; \
	fi

# Generated headers (produced by the root build). Test binaries bake in the
# LRG_VERSION_* macros via <libregnum.h>, so a version/config bump must force a
# recompile -- otherwise a stale macro mismatches the freshly-built library at
# runtime (e.g. test-engine's version assertion). The fallback rule lets a
# standalone `make -C tests` regenerate them when no root build ran first.
GENERATED_HEADERS := ../src/lrg-version.h ../src/config.h
$(GENERATED_HEADERS):
	@$(MAKE) -C .. generate

# Pattern rule for test compilation
$(TEST_BUILDDIR)/test-%: test-%.c $(GENERATED_HEADERS) | $(TEST_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(TEST_CFLAGS) -o $@ $< $(TEST_LDFLAGS) $(TEST_LIBS)

# Build lrgldr by recursing into the root (it links the shared library).
.PHONY: $(LRGLDR_BIN)
$(LRGLDR_BIN):
	@$(MAKE) -C .. lrgldr

# Fixture game module (.so) loaded by the lrgldr subprocess test.
$(FIXTURE_SO): fixtures/test-args-module.c | $(TEST_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(FIXTURE_CFLAGS) -shared -o $@ $<

# test-lrgldr needs the loader binary and the fixture module, and has their
# absolute paths baked in (so it can spawn/load them regardless of CWD).
$(TEST_BUILDDIR)/test-lrgldr: test-lrgldr.c $(GENERATED_HEADERS) $(LRGLDR_BIN) $(FIXTURE_SO) | $(TEST_BUILDDIR)
	$(call print_compile,$<)
	@$(CC) $(TEST_CFLAGS) \
		-DLRGLDR_BIN_PATH='"$(LRGLDR_BIN)"' \
		-DTEST_ARGS_MODULE_PATH='"$(FIXTURE_SO_ABS)"' \
		-o $@ $< $(TEST_LDFLAGS) $(TEST_LIBS)

# Create test build directory
$(TEST_BUILDDIR):
	@$(MKDIR_P) $@

clean:
	$(call print_status,"Cleaning test artifacts...")
	$(RM) $(TEST_BINARIES) $(FIXTURE_SO)
	@if [ -d "$(TEST_BUILDDIR)" ]; then rmdir --ignore-fail-on-non-empty "$(TEST_BUILDDIR)"; fi
