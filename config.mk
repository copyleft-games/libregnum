# config.mk - Libregnum Build Configuration
#
# Copyright 2025 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# This file contains all configurable build options for Libregnum.
# Override any option on the command line: make DEBUG=1

# =============================================================================
# Project Root Path
# =============================================================================

# Determine the project root based on where config.mk is located
# This ensures paths work correctly when make is run from subdirectories
CONFIG_MK_PATH := $(dir $(lastword $(MAKEFILE_LIST)))
PROJECT_ROOT := $(realpath $(CONFIG_MK_PATH))

# =============================================================================
# Version Information
# =============================================================================

VERSION_MAJOR := 0
VERSION_MINOR := 2
VERSION_MICRO := 0
VERSION := $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_MICRO)

# API version for parallel installability (bump on ABI breaks)
API_VERSION := 1

# Shared library versioning (libtool-style: current:revision:age)
SO_VERSION := 0
SO_MINOR := 0
SO_RELEASE := 0

# =============================================================================
# Installation Directories
# =============================================================================

PREFIX ?= /usr/local
EXEC_PREFIX ?= $(PREFIX)
BINDIR ?= $(EXEC_PREFIX)/bin
LIBDIR ?= $(EXEC_PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
DATADIR ?= $(PREFIX)/share
DOCDIR ?= $(DATADIR)/doc/libregnum
GIRDIR ?= $(DATADIR)/gir-1.0
TYPELIBDIR ?= $(LIBDIR)/girepository-1.0
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig

# =============================================================================
# Build Options
# =============================================================================

# Build shared library (liblibregnum.so)
BUILD_SHARED ?= 1

# Build static library (liblibregnum.a)
BUILD_STATIC ?= 1

# Build GObject Introspection data (.gir and .typelib)
BUILD_GIR ?= 1

# Build unit tests
BUILD_TESTS ?= 1

# Build example programs
BUILD_EXAMPLES ?= 1

# Build documentation (requires gi-docgen)
BUILD_DOCS ?= 0

# Build the editor/level authoring module (src/editor/). Off-able for headless
# or minimal builds; the rest of the engine builds without it.
BUILD_EDITOR ?= 1

# Build the standalone engine-drawn editor UI (src/editor/ui/, needs BUILD_EDITOR).
# Off by default: embedded hosts (e.g. cmacs) provide their own panels.
BUILD_EDITOR_UI ?= 0

# =============================================================================
# Dependency Paths
# =============================================================================

# Paths to submodule dependencies (use PROJECT_ROOT for consistency across subdirs)
GRAYLIB_DIR ?= $(PROJECT_ROOT)/deps/graylib
YAMLGLIB_DIR ?= $(PROJECT_ROOT)/deps/yaml-glib
CRISPY_DIR ?= $(PROJECT_ROOT)/deps/crispy

# =============================================================================
# Debug Configuration
# =============================================================================

# Debug build mode:
#   0 = Release build (-O2, no debug symbols)
#   1 = Debug build (-g3 -O0, full debug info for gdb)
DEBUG ?= 0

# Enable AddressSanitizer (requires DEBUG=1)
ASAN ?= 0

# Enable UndefinedBehaviorSanitizer (requires DEBUG=1)
UBSAN ?= 0

# Convenience flag: SANITIZE=1 implies DEBUG=1, ASAN=1, UBSAN=1 and builds
# into a separate build/sanitize/ directory so sanitized objects never collide
# with a normal debug build (avoids stale-object mis-links).
SANITIZE ?= 0

# Enable trace logging at compile time
ENABLE_TRACE ?= 0

# =============================================================================
# Compiler and Tools
# =============================================================================

CC ?= gcc
AR ?= ar
RANLIB ?= ranlib
PKG_CONFIG ?= pkg-config
INSTALL ?= install
INSTALL_DATA ?= $(INSTALL) -m 644
INSTALL_PROGRAM ?= $(INSTALL) -m 755
SED ?= sed
MKDIR_P ?= mkdir -p
RM ?= rm -f
RMDIR ?= rm -rf

# GObject Introspection tools
GIR_SCANNER ?= g-ir-scanner
GIR_COMPILER ?= g-ir-compiler
GLIB_MKENUMS ?= glib-mkenums

# Documentation tools
GI_DOCGEN ?= gi-docgen

# =============================================================================
# Compiler Flags
# =============================================================================

# C standard (gnu89, NO -pedantic)
CSTD := gnu89

# Warning flags (ZERO tolerance - warnings are errors)
WARN_CFLAGS := -Wall -Wextra -Werror
WARN_CFLAGS += -Wformat=2 -Wformat-security
WARN_CFLAGS += -Wnull-dereference
WARN_CFLAGS += -Wstack-protector
WARN_CFLAGS += -Wstrict-prototypes
WARN_CFLAGS += -Wmissing-prototypes
WARN_CFLAGS += -Wold-style-definition
WARN_CFLAGS += -Wdeclaration-after-statement
WARN_CFLAGS += -Wno-unused-parameter

# Feature test macros
FEATURE_CFLAGS := -D_GNU_SOURCE

# Visibility for shared libraries
VISIBILITY_CFLAGS := -fvisibility=hidden

# Position independent code (required for shared libraries)
PIC_CFLAGS := -fPIC

# =============================================================================
# Game Module Flags
# =============================================================================

# Flags for building a libregnum game as a loadable module (.so). A game's
# main translation unit uses LRG_DEFINE_GAME_MODULE; building it with
# -DLRG_GAME_BUILD_MODULE emits the exported entry symbol instead of a main().
# The version script keeps everything but that entry symbol module-local.
GAME_MODULE_MAP := $(PROJECT_ROOT)/build-aux/lrg-game-module.map
GAME_MODULE_CFLAGS := $(PIC_CFLAGS) -DLRG_GAME_BUILD_MODULE=1
GAME_MODULE_LDFLAGS := -shared

# =============================================================================
# Platform Detection
# =============================================================================

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_S),Linux)
    PLATFORM := linux
    PLATFORM_LINUX := 1
else ifeq ($(UNAME_S),FreeBSD)
    PLATFORM := freebsd
    PLATFORM_FREEBSD := 1
else ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
    PLATFORM_MACOS := 1
else ifneq (,$(findstring MINGW,$(UNAME_S)))
    PLATFORM := windows
    PLATFORM_WINDOWS := 1
else ifneq (,$(findstring MSYS,$(UNAME_S)))
    PLATFORM := windows
    PLATFORM_WINDOWS := 1
else
    PLATFORM := unknown
endif

# =============================================================================
# Windows Cross-Compilation Support
# =============================================================================
#
# Usage: make WINDOWS=1
#        make CROSS=x86_64-w64-mingw32  (equivalent, for advanced users)
#
# Requires: mingw64-glib2, mingw64-gcc, mingw64-pkg-config (on Fedora)

WINDOWS ?= 0
CROSS ?=

# Set CROSS based on convenience variable
ifeq ($(WINDOWS),1)
    CROSS := x86_64-w64-mingw32
endif

# Apply cross-compilation settings when CROSS is set
ifneq ($(CROSS),)
    CC := $(CROSS)-gcc
    AR := $(CROSS)-ar
    RANLIB := $(CROSS)-ranlib
    PKG_CONFIG := $(CROSS)-pkg-config
    TARGET_PLATFORM := windows
    # GIR not supported for cross-compilation (requires host compiler)
    BUILD_GIR := 0
else
    TARGET_PLATFORM := $(PLATFORM)
endif

# =============================================================================
# Steam SDK Integration (Opt-In)
# =============================================================================
#
# Usage: make STEAM=1
#
# Requires: Steamworks SDK in deps/steamworks_sdk
#           (git submodule: https://github.com/rlabrecque/SteamworksSDK)
#
# Uses the pure C flat API (steam_api_flat.h) - NO C++ required.

STEAM ?= 0

ifeq ($(STEAM),1)
    STEAM_SDK_PATH := $(PROJECT_ROOT)/deps/steamworks_sdk/sdk
    STEAM_CFLAGS := -I$(STEAM_SDK_PATH)/public -DLRG_ENABLE_STEAM=1
    ifeq ($(TARGET_PLATFORM),windows)
        STEAM_LIBS := -L$(STEAM_SDK_PATH)/redistributable_bin/win64 -lsteam_api64
    else
        STEAM_LIBS := -L$(STEAM_SDK_PATH)/redistributable_bin/linux64 -lsteam_api
    endif
else
    STEAM_CFLAGS :=
    STEAM_LIBS :=
endif

# =============================================================================
# MCP Support (Opt-In) - For AI-assisted debugging
# =============================================================================
#
# Usage: make MCP=1
#
# Requires: mcp-glib in deps/mcp-glib
# Enables: MCP server for Claude Code / AI debugging integration
#
# Features:
#   - Input injection (keyboard, mouse, gamepad)
#   - Screenshot capture (base64 PNG)
#   - Engine state inspection
#   - ECS/World manipulation
#   - Save/Load triggering
#
# Note: HTTP transport requires libsoup-3.0 (already a dependency)

# =============================================================================
# CAD Integration (cad-glib: parametric CAD parts as scene nodes)
# =============================================================================
# Opt-in (CAD=1).  cad-glib is ALWAYS linked statically (its generated .pc
# Libs.private supplies the OpenCASCADE toolkits + libstdc++, so the OCCT
# probe lives in exactly one place).  It is located, in order:
#   1. CAD_GLIB_DIR set explicitly -- how a cmacs build points at ITS
#      canonical copy ($(abs_top_srcdir)/deps/cad-glib), which wins.
#   2. the bundled deps/cad-glib submodule -- so a standalone libregnum
#      checkout builds CAD parts independently (`make cad-glib` builds it).
#   3. a system install via pkg-config.

CAD ?= 0

CAD_GLIB_BUNDLED := $(PROJECT_ROOT)/deps/cad-glib

ifeq ($(CAD),1)
    # Standalone fallback: a cmacs build supplies CAD_GLIB_DIR and wins.
    ifeq ($(CAD_GLIB_DIR),)
        ifneq ($(wildcard $(CAD_GLIB_BUNDLED)/src/cad-glib.h),)
            CAD_GLIB_DIR := $(CAD_GLIB_BUNDLED)
        endif
    endif
    ifneq ($(CAD_GLIB_DIR),)
        CAD_CFLAGS := -I$(CAD_GLIB_DIR)/src -I$(CAD_GLIB_DIR)/build/release -DLRG_ENABLE_CAD=1
        CAD_LIBS := $(CAD_GLIB_DIR)/build/release/libcad-glib-1.0.a
        CAD_LIBS += $(shell sed -n 's/^Libs.private: //p' $(CAD_GLIB_DIR)/cad-glib-1.0.pc 2>/dev/null)
        CAD_LIBS += -lstdc++
    else
        CAD_CFLAGS := $(shell $(PKG_CONFIG) --cflags cad-glib-1.0) -DLRG_ENABLE_CAD=1
        CAD_LIBS := $(shell $(PKG_CONFIG) --libs cad-glib-1.0)
    endif
else
    CAD_CFLAGS :=
    CAD_LIBS :=
endif

MCP ?= 0

ifeq ($(MCP),1)
    MCP_GLIB_DIR := $(PROJECT_ROOT)/deps/mcp-glib
    MCP_CFLAGS := -I$(MCP_GLIB_DIR)/src -DLRG_ENABLE_MCP=1
    MCP_LIBS := -L$(MCP_GLIB_DIR)/build -lmcp-glib-1.0
else
    MCP_CFLAGS :=
    MCP_LIBS :=
endif

# =============================================================================
# Library Names (Platform-Specific)
# =============================================================================

LIB_NAME := libregnum

ifeq ($(TARGET_PLATFORM),windows)
    # Windows: DLL naming convention (no lib prefix for DLL)
    LIB_STATIC := lib$(LIB_NAME).a
    LIB_SHARED := $(LIB_NAME).dll
    LIB_IMPORT := lib$(LIB_NAME).dll.a
    EXE_EXT := .exe
    # Platform-specific system libraries
    PLATFORM_LIBS := -lopengl32 -lgdi32 -lwinmm -lshell32
    # Windows shared library linker flags (use = for deferred expansion of LIBOUTDIR)
    LIB_LDFLAGS_PLATFORM = -shared -Wl,--out-implib,$(LIBOUTDIR)/$(LIB_IMPORT)
else
    # Unix: standard .so naming with versioning
    LIB_STATIC := lib$(LIB_NAME).a
    LIB_SHARED := lib$(LIB_NAME).so
    LIB_SHARED_VERSION := $(LIB_SHARED).$(SO_VERSION).$(SO_MINOR).$(SO_RELEASE)
    LIB_SHARED_SONAME := $(LIB_SHARED).$(SO_VERSION)
    EXE_EXT :=
    # Platform-specific system libraries
    PLATFORM_LIBS := -lGL -lm -lpthread -ldl -lrt -lX11
    # Unix shared library linker flags
    LIB_LDFLAGS_PLATFORM = -shared -Wl,-soname,$(LIB_SHARED_SONAME)
endif

# GIR output names
GIR_NAME := Libregnum-$(API_VERSION).gir
TYPELIB_NAME := Libregnum-$(API_VERSION).typelib

# pkg-config file
PC_FILE := libregnum-$(API_VERSION).pc

# =============================================================================
# Build Directories
# =============================================================================

ifeq ($(SANITIZE),1)
    # Sanitized build: implies debug + both sanitizers, isolated output dir.
    DEBUG := 1
    ASAN := 1
    UBSAN := 1
    BUILDDIR := build/sanitize
else ifeq ($(DEBUG),1)
    BUILDDIR := build/debug
else
    BUILDDIR := build/release
endif

OBJDIR := $(BUILDDIR)/obj
LIBOUTDIR := $(BUILDDIR)/lib
GIROUTDIR := $(BUILDDIR)/gir

# =============================================================================
# Dependency pkg-config
# =============================================================================

# Core GLib dependencies (required)
GLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0 gobject-2.0 gio-2.0 gmodule-2.0)
GLIB_LIBS := $(shell $(PKG_CONFIG) --libs glib-2.0 gobject-2.0 gio-2.0 gmodule-2.0)

# json-glib (for yaml-glib interop, required)
JSON_CFLAGS := $(shell $(PKG_CONFIG) --cflags json-glib-1.0)
JSON_LIBS := $(shell $(PKG_CONFIG) --libs json-glib-1.0)

# libsoup3 (for HTTP analytics backend, required)
SOUP_CFLAGS := $(shell $(PKG_CONFIG) --cflags libsoup-3.0)
SOUP_LIBS := $(shell $(PKG_CONFIG) --libs libsoup-3.0)

# libyaml (for yaml-glib, required)
YAML_CFLAGS := $(shell $(PKG_CONFIG) --cflags yaml-0.1)
YAML_LIBS := $(shell $(PKG_CONFIG) --libs yaml-0.1)

# libdex for async (not available for Windows cross-compile)
ifeq ($(TARGET_PLATFORM),windows)
    DEX_CFLAGS :=
    DEX_LIBS :=
    HAS_LIBDEX := 0
else
    DEX_CFLAGS := $(shell $(PKG_CONFIG) --cflags libdex-1 2>/dev/null)
    DEX_LIBS := $(shell $(PKG_CONFIG) --libs libdex-1 2>/dev/null)
    ifneq ($(DEX_CFLAGS),)
        HAS_LIBDEX := 1
    else
        HAS_LIBDEX := 0
    endif
endif

# LuaJIT for scripting (optional)
ifeq ($(TARGET_PLATFORM),windows)
    LUAJIT_CFLAGS :=
    LUAJIT_LIBS :=
    HAS_LUAJIT := 0
else
    LUAJIT_CFLAGS := $(shell $(PKG_CONFIG) --cflags luajit 2>/dev/null)
    LUAJIT_LIBS := $(shell $(PKG_CONFIG) --libs luajit 2>/dev/null)
    ifneq ($(LUAJIT_CFLAGS),)
        HAS_LUAJIT := 1
    else
        HAS_LUAJIT := 0
    endif
endif

# Python 3 for scripting (optional, not for cross-compile)
ifeq ($(TARGET_PLATFORM),windows)
    PYTHON_CFLAGS :=
    PYTHON_LIBS :=
    HAS_PYTHON := 0
else
    PYTHON_CFLAGS := $(shell $(PKG_CONFIG) --cflags python3-embed 2>/dev/null)
    PYTHON_LIBS := $(shell $(PKG_CONFIG) --libs python3-embed 2>/dev/null)
    ifneq ($(PYTHON_CFLAGS),)
        HAS_PYTHON := 1
    else
        HAS_PYTHON := 0
    endif
endif

# GObject Introspection runtime (only for native builds with GIR)
ifeq ($(BUILD_GIR),1)
    # Use GLib's integrated GIRepository 3.0 (girepository-2.0), not the legacy
    # libgirepository-1.0 (gobject-introspection-1.0).  On GLib >= 2.80 the two
    # both register a "GIRepository" GType, so linking the legacy one alongside
    # gjs / modern pygobject aborts with a duplicate-registration fatal.
    GI_RUNTIME_CFLAGS := $(shell $(PKG_CONFIG) --cflags girepository-2.0 2>/dev/null)
    GI_RUNTIME_LIBS := $(shell $(PKG_CONFIG) --libs girepository-2.0 2>/dev/null)
    ifneq ($(GI_RUNTIME_CFLAGS),)
        HAS_GI := 1
    else
        HAS_GI := 0
    endif
else
    GI_RUNTIME_CFLAGS :=
    GI_RUNTIME_LIBS :=
    HAS_GI := 0
endif

# Python's pygobject bridge (lrg-scripting-pygobject.c) includes the GI
# scripting base (girepository.h), so the Python backend requires the GI
# runtime.  This gate runs after HAS_GI is known (the Python block above runs
# before it).  Without GI (e.g. BUILD_GIR=0 and no GI_RUNTIME override) disable
# Python so the build doesn't fail on girepository.h.  (Gjs is gated the same
# way in its own block below.)  cmacs passes HAS_GI=1 + GI_RUNTIME_*, so both
# stay enabled there.
ifneq ($(HAS_GI),1)
    ifeq ($(HAS_PYTHON),1)
        PYTHON_CFLAGS :=
        PYTHON_LIBS :=
        HAS_PYTHON := 0
    endif
endif

# Gjs (GNOME JavaScript) for scripting (optional, not for cross-compile)
ifeq ($(TARGET_PLATFORM),windows)
    GJS_CFLAGS :=
    GJS_LIBS :=
    HAS_GJS := 0
else
    GJS_CFLAGS := $(shell $(PKG_CONFIG) --cflags gjs-1.0 2>/dev/null)
    GJS_LIBS := $(shell $(PKG_CONFIG) --libs gjs-1.0 2>/dev/null)
    # The gjs backend (LrgScriptingGjs) extends LrgScriptingGI, which needs the
    # GObject-Introspection runtime (girepository.h).  Require HAS_GI: without it
    # (e.g. BUILD_GIR=0 without GI_RUNTIME flags) gjs cannot compile, so disable
    # it rather than break the build when gjs-1.0 is installed but GI is not set.
    ifneq ($(GJS_CFLAGS),)
        ifeq ($(HAS_GI),1)
            HAS_GJS := 1
        else
            GJS_CFLAGS :=
            GJS_LIBS :=
            HAS_GJS := 0
        endif
    else
        HAS_GJS := 0
    endif
endif

# Crispy (compiled-C) scripting backend.  Vendored as the deps/crispy git
# submodule and built by libregnum itself -- no system crispy / pkg-config
# needed, so libregnum builds standalone with its own dependency.  On by
# default (native only); CRISPY=0 disables it.  Gated on the submodule being
# present so a checkout without it still builds (just without crispy).  Crispy's
# own deps (glib/gobject/gio/gmodule) are already in GLIB_LIBS, and its static
# archive is whole-archived via ALL_LIBS + bundled into liblibregnum.a, so
# CRISPY_LIBS stays empty.
CRISPY ?= 1
ifeq ($(TARGET_PLATFORM),windows)
    CRISPY_CFLAGS :=
    CRISPY_LIBS :=
    HAS_CRISPY := 0
else ifeq ($(CRISPY),1)
    ifneq ($(wildcard $(CRISPY_DIR)/src/crispy.h),)
        CRISPY_CFLAGS := -I$(CRISPY_DIR)/src
        CRISPY_LIBS :=
        HAS_CRISPY := 1
    else
        CRISPY_CFLAGS :=
        CRISPY_LIBS :=
        HAS_CRISPY := 0
    endif
else
    CRISPY_CFLAGS :=
    CRISPY_LIBS :=
    HAS_CRISPY := 0
endif

# Combined dependency flags (use -isystem to suppress warnings from deps)
DEP_CFLAGS := $(GLIB_CFLAGS) $(DEX_CFLAGS) $(JSON_CFLAGS) $(YAML_CFLAGS) $(SOUP_CFLAGS) $(LUAJIT_CFLAGS) $(PYTHON_CFLAGS) $(GI_RUNTIME_CFLAGS) $(GJS_CFLAGS) $(CRISPY_CFLAGS)
DEP_LIBS := $(GLIB_LIBS) $(DEX_LIBS) $(JSON_LIBS) $(YAML_LIBS) $(SOUP_LIBS) $(LUAJIT_LIBS) $(PYTHON_LIBS) $(GI_RUNTIME_LIBS) $(GJS_LIBS) $(CRISPY_LIBS)

# Graylib and yaml-glib (built from submodules)
# Also include raylib headers for rlgl.h etc.
GRAYLIB_CFLAGS := -I$(GRAYLIB_DIR)/src -I$(GRAYLIB_DIR)/deps/raylib/src
YAMLGLIB_CFLAGS := -I$(YAMLGLIB_DIR)/src

# Static archives for embedding graylib, raylib, and yaml-glib into libregnum
GRAYLIB_STATIC  := $(GRAYLIB_DIR)/build/lib/libgraylib.a
RAYLIB_STATIC   := $(GRAYLIB_DIR)/deps/raylib/src/libraylib.a
YAMLGLIB_STATIC := $(YAMLGLIB_DIR)/build/libyaml-glib.a
CRISPY_STATIC   := $(CRISPY_DIR)/build/release/libcrispy.a

# =============================================================================
# Composite Flags
# =============================================================================

# Base CFLAGS
BASE_CFLAGS := -std=$(CSTD) $(WARN_CFLAGS) $(FEATURE_CFLAGS)

# Debug/Release flags
ifeq ($(DEBUG),1)
    OPT_CFLAGS := -g3 -O0 -DLRG_DEBUG=1
else
    OPT_CFLAGS := -O2 -DNDEBUG
endif

# Trace flag
ifeq ($(ENABLE_TRACE),1)
    OPT_CFLAGS += -DLRG_ENABLE_TRACE=1
endif

# Optional dependency availability flags
ifeq ($(HAS_LIBDEX),1)
    OPT_CFLAGS += -DLRG_HAS_LIBDEX=1
endif
ifeq ($(HAS_LUAJIT),1)
    OPT_CFLAGS += -DLRG_HAS_LUAJIT=1
endif
ifeq ($(HAS_PYTHON),1)
    OPT_CFLAGS += -DLRG_HAS_PYTHON=1
endif
ifeq ($(HAS_GJS),1)
    OPT_CFLAGS += -DLRG_HAS_GJS=1
endif
ifeq ($(HAS_GI),1)
    OPT_CFLAGS += -DLRG_HAS_GI=1
endif
ifeq ($(BUILD_EDITOR),1)
    OPT_CFLAGS += -DLRG_BUILD_EDITOR=1
endif
ifeq ($(HAS_CRISPY),1)
    OPT_CFLAGS += -DLRG_HAS_CRISPY=1
endif
ifeq ($(BUILD_EDITOR_UI),1)
    OPT_CFLAGS += -DLRG_BUILD_EDITOR_UI=1
endif

# Windows cross-compile marker
ifeq ($(TARGET_PLATFORM),windows)
    OPT_CFLAGS += -DLRG_PLATFORM_WINDOWS=1
else
    OPT_CFLAGS += -DLRG_PLATFORM_LINUX=1
endif

# Sanitizer flags
ifeq ($(ASAN),1)
ifeq ($(DEBUG),1)
    OPT_CFLAGS += -fsanitize=address -fno-omit-frame-pointer
    SANITIZER_LIBS := -fsanitize=address
endif
endif

ifeq ($(UBSAN),1)
ifeq ($(DEBUG),1)
    OPT_CFLAGS += -fsanitize=undefined
    SANITIZER_LIBS += -fsanitize=undefined
endif
endif

# Library compilation flags
LIB_CFLAGS := $(BASE_CFLAGS) $(OPT_CFLAGS) $(PIC_CFLAGS) $(VISIBILITY_CFLAGS)
LIB_CFLAGS += -DLIBREGNUM_COMPILATION
LIB_CFLAGS += -isystem $(GRAYLIB_DIR)/src
LIB_CFLAGS += -isystem $(GRAYLIB_DIR)/deps/raylib/src
LIB_CFLAGS += -isystem $(YAMLGLIB_DIR)/src
LIB_CFLAGS += $(subst -I,-isystem ,$(DEP_CFLAGS))
LIB_CFLAGS += -I$(CURDIR)/src
LIB_CFLAGS += $(STEAM_CFLAGS)
LIB_CFLAGS += $(MCP_CFLAGS)
LIB_CFLAGS += $(CAD_CFLAGS)

# Library link flags (use platform-specific flags)
LIB_LDFLAGS := $(LIB_LDFLAGS_PLATFORM)

# All libraries to link (include platform-specific libs)
ALL_LIBS := $(DEP_LIBS) $(PLATFORM_LIBS) $(SANITIZER_LIBS)
ifeq ($(TARGET_PLATFORM),windows)
    # Windows: link against import libraries for DLLs
    # Note: libregnum.dll requires graylib.dll and yaml-glib.dll at runtime
    ALL_LIBS += -L$(GRAYLIB_DIR)/build/lib -lgraylib
    ALL_LIBS += -L$(YAMLGLIB_DIR)/build -lyaml-glib
else
    # Unix: embed graylib, raylib, and yaml-glib directly into libregnum (self-contained).
    # --whole-archive ensures all graylib/yaml-glib symbols are included.
    # raylib is linked normally (not --whole-archive) to avoid pulling in its entire
    # rcore.o, but --allow-multiple-definition is needed because raylib's inline math
    # functions (Vector2*, Matrix*, etc.) get compiled into both libregnum's .o files
    # (via raymath.h includes) and into rcore.o -- all definitions are identical.
    ALL_LIBS += -Wl,--whole-archive $(GRAYLIB_STATIC) $(YAMLGLIB_STATIC) -Wl,--no-whole-archive
    # Vendored crispy: whole-archive its static lib so LrgScriptingCrispy resolves
    # (its glib/gio/gmodule deps come from GLIB_LIBS).
    ifeq ($(HAS_CRISPY),1)
        ALL_LIBS += -Wl,--whole-archive $(CRISPY_STATIC) -Wl,--no-whole-archive
        # crispy-repl.c pulls in readline; resolve those symbols here since the
        # whole-archived crispy objects bring them into liblibregnum.  Without
        # this the GIR scanner's executable link fails on undefined readline
        # symbols (a shared-lib link tolerates them, an executable does not).
        ALL_LIBS += $(shell $(PKG_CONFIG) --libs readline 2>/dev/null || echo -lreadline)
    endif
    ALL_LIBS += -Wl,--allow-multiple-definition $(RAYLIB_STATIC)
endif
ALL_LIBS += $(STEAM_LIBS)
ALL_LIBS += $(MCP_LIBS)
ALL_LIBS += $(CAD_LIBS)

# =============================================================================
# GIR Scanner Flags
# =============================================================================

GIR_SCANNER_FLAGS := \
    --warn-all \
    --namespace=Libregnum \
    --nsversion=$(API_VERSION) \
    --identifier-prefix=Lrg \
    --symbol-prefix=lrg \
    --c-include="libregnum.h" \
    --pkg=glib-2.0 \
    --pkg=gobject-2.0 \
    --pkg=gio-2.0 \
    --pkg=libdex-1 \
    --pkg=json-glib-1.0 \
    --pkg=girepository-2.0 \
    --include=GLib-2.0 \
    --include=GObject-2.0 \
    --include=Gio-2.0 \
    --include=Dex-1 \
    --include=Json-1.0 \
    --include=Graylib-1 \
    -DLIBREGNUM_COMPILATION \
    -I$(CURDIR)/src \
    -I$(GRAYLIB_DIR)/src \
    -I$(YAMLGLIB_DIR)/src \
    --add-include-path=$(GRAYLIB_DIR)/build/gir \
    --add-include-path=$(YAMLGLIB_DIR)/build/release/gir

# MCP GIR additions (when MCP=1)
ifeq ($(MCP),1)
GIR_SCANNER_FLAGS += \
    -I$(MCP_GLIB_DIR)/src \
    --pkg=libsoup-3.0 \
    -DLRG_ENABLE_MCP=1
endif

GIR_COMPILER_FLAGS := \
    --includedir=$(GRAYLIB_DIR)/build/gir \
    --includedir=$(YAMLGLIB_DIR)/build/release/gir
