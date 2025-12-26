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
# Library Names
# =============================================================================

LIB_NAME := libregnum
LIB_STATIC := lib$(LIB_NAME).a
LIB_SHARED := lib$(LIB_NAME).so
LIB_SHARED_VERSION := $(LIB_SHARED).$(SO_VERSION).$(SO_MINOR).$(SO_RELEASE)
LIB_SHARED_SONAME := $(LIB_SHARED).$(SO_VERSION)

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

# Core GLib dependencies
GLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0 gobject-2.0 gio-2.0)
GLIB_LIBS := $(shell $(PKG_CONFIG) --libs glib-2.0 gobject-2.0 gio-2.0)

# libdex for async
DEX_CFLAGS := $(shell $(PKG_CONFIG) --cflags libdex-1)
DEX_LIBS := $(shell $(PKG_CONFIG) --libs libdex-1)

# json-glib (for yaml-glib interop)
JSON_CFLAGS := $(shell $(PKG_CONFIG) --cflags json-glib-1.0)
JSON_LIBS := $(shell $(PKG_CONFIG) --libs json-glib-1.0)

# libyaml (for yaml-glib)
YAML_CFLAGS := $(shell $(PKG_CONFIG) --cflags yaml-0.1)
YAML_LIBS := $(shell $(PKG_CONFIG) --libs yaml-0.1)

# Combined dependency flags (use -isystem to suppress warnings from deps)
DEP_CFLAGS := $(GLIB_CFLAGS) $(DEX_CFLAGS) $(JSON_CFLAGS) $(YAML_CFLAGS)
DEP_LIBS := $(GLIB_LIBS) $(DEX_LIBS) $(JSON_LIBS) $(YAML_LIBS)

# Graylib and yaml-glib (built from submodules)
GRAYLIB_CFLAGS := -I$(GRAYLIB_DIR)/src
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
LIB_CFLAGS += -isystem $(YAMLGLIB_DIR)/src
LIB_CFLAGS += $(subst -I,-isystem ,$(DEP_CFLAGS))
LIB_CFLAGS += -I$(CURDIR)/src

# Library link flags
LIB_LDFLAGS := -shared -Wl,-soname,$(LIB_SHARED_SONAME)

# All libraries to link
ALL_LIBS := $(DEP_LIBS) $(SANITIZER_LIBS)
ALL_LIBS += -L$(GRAYLIB_DIR)/build/lib -lgraylib
ALL_LIBS += -L$(YAMLGLIB_DIR)/build -lyaml-glib

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
    --include=GLib-2.0 \
    --include=GObject-2.0 \
    --include=Gio-2.0 \
    --include=Dex-1 \
    -I$(CURDIR)/src \
    -I$(GRAYLIB_DIR)/src \
    -I$(YAMLGLIB_DIR)/src \
    --add-include-path=$(GRAYLIB_DIR)/build/release/gir \
    --add-include-path=$(YAMLGLIB_DIR)/build/release/gir

GIR_COMPILER_FLAGS := \
    --includedir=$(GRAYLIB_DIR)/build/release/gir \
    --includedir=$(YAMLGLIB_DIR)/build/release/gir
