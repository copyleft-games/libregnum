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
VERSION_MINOR := 1
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

# =============================================================================
# Dependency Paths
# =============================================================================

# Paths to submodule dependencies (use PROJECT_ROOT for consistency across subdirs)
GRAYLIB_DIR ?= $(PROJECT_ROOT)/deps/graylib
YAMLGLIB_DIR ?= $(PROJECT_ROOT)/deps/yaml-glib.git

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

ifeq ($(DEBUG),1)
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
    GI_RUNTIME_CFLAGS := $(shell $(PKG_CONFIG) --cflags gobject-introspection-1.0 2>/dev/null)
    GI_RUNTIME_LIBS := $(shell $(PKG_CONFIG) --libs gobject-introspection-1.0 2>/dev/null)
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

# Gjs (GNOME JavaScript) for scripting (optional, not for cross-compile)
ifeq ($(TARGET_PLATFORM),windows)
    GJS_CFLAGS :=
    GJS_LIBS :=
    HAS_GJS := 0
else
    GJS_CFLAGS := $(shell $(PKG_CONFIG) --cflags gjs-1.0 2>/dev/null)
    GJS_LIBS := $(shell $(PKG_CONFIG) --libs gjs-1.0 2>/dev/null)
    ifneq ($(GJS_CFLAGS),)
        HAS_GJS := 1
    else
        HAS_GJS := 0
    endif
endif

# Combined dependency flags (use -isystem to suppress warnings from deps)
DEP_CFLAGS := $(GLIB_CFLAGS) $(DEX_CFLAGS) $(JSON_CFLAGS) $(YAML_CFLAGS) $(SOUP_CFLAGS) $(LUAJIT_CFLAGS) $(PYTHON_CFLAGS) $(GI_RUNTIME_CFLAGS) $(GJS_CFLAGS)
DEP_LIBS := $(GLIB_LIBS) $(DEX_LIBS) $(JSON_LIBS) $(YAML_LIBS) $(SOUP_LIBS) $(LUAJIT_LIBS) $(PYTHON_LIBS) $(GI_RUNTIME_LIBS) $(GJS_LIBS)

# Graylib and yaml-glib (built from submodules)
# Also include raylib headers for rlgl.h etc.
GRAYLIB_CFLAGS := -I$(GRAYLIB_DIR)/src -I$(GRAYLIB_DIR)/deps/raylib/src
YAMLGLIB_CFLAGS := -I$(YAMLGLIB_DIR)/src

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

# Library link flags (use platform-specific flags)
LIB_LDFLAGS := $(LIB_LDFLAGS_PLATFORM)

# All libraries to link (include platform-specific libs)
ALL_LIBS := $(DEP_LIBS) $(PLATFORM_LIBS) $(SANITIZER_LIBS)
ifeq ($(TARGET_PLATFORM),windows)
    # For Windows, link against import libraries for DLLs
    # Note: libregnum.dll requires graylib.dll and yaml-glib.dll at runtime
    ALL_LIBS += -L$(GRAYLIB_DIR)/build/lib -lgraylib
    ALL_LIBS += -L$(YAMLGLIB_DIR)/build -lyaml-glib
else
    ALL_LIBS += -L$(GRAYLIB_DIR)/build/lib -lgraylib
    ALL_LIBS += -L$(YAMLGLIB_DIR)/build -lyaml-glib
endif
ALL_LIBS += $(STEAM_LIBS)

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
    --pkg=gobject-introspection-1.0 \
    --include=GLib-2.0 \
    --include=GObject-2.0 \
    --include=Gio-2.0 \
    --include=Dex-1 \
    --include=GIRepository-2.0 \
    -DLIBREGNUM_COMPILATION \
    -I$(CURDIR)/src \
    -I$(GRAYLIB_DIR)/src \
    -I$(YAMLGLIB_DIR)/src \
    --add-include-path=$(GRAYLIB_DIR)/build/release/gir \
    --add-include-path=$(YAMLGLIB_DIR)/build/release/gir

GIR_COMPILER_FLAGS := \
    --includedir=$(GRAYLIB_DIR)/build/release/gir \
    --includedir=$(YAMLGLIB_DIR)/build/release/gir
