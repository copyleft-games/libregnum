# windows-amd64.mk - target profile: Windows 10+ on x86_64 (MinGW-w64)
#
# Copyright 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# description: Windows 10+ x86_64, MinGW-w64 gcc; produces .dll and .exe
#
# Read by config.mk (after its tool defaults) when LRG_TARGET=windows-amd64,
# i.e. `make WINDOWS=1'.  A profile is the single place a target is defined:
# toolchain, platform name, forced feature defaults, how to run its test
# binaries and which build container provides the toolchain.  The layout
# and the checklist for adding a target are in docs/cross-compile.org,
# "Adding a new target".
#
# Output: build/{release,debug}-windows-amd64/

LRG_TARGET_TRIPLE := x86_64-w64-mingw32

# The legacy name; rules.mk's `help' still prints it
CROSS := $(LRG_TARGET_TRIPLE)

# Toolchain (a command-line CC=... etc. still wins over these)
CC := $(LRG_TARGET_TRIPLE)-gcc
AR := $(LRG_TARGET_TRIPLE)-ar
RANLIB := $(LRG_TARGET_TRIPLE)-ranlib
PKG_CONFIG := $(LRG_TARGET_TRIPLE)-pkg-config

TARGET_PLATFORM := windows

# GObject Introspection runs the library it scans, which a cross build
# cannot do
BUILD_GIR := 0

# Test binaries are PE executables; run them through Wine (the build
# container has wine-core), on a pseudo-terminal so they get a console as
# on Windows (build-aux/cross/lrg-wine-run says why).  Wine finds DLLs on
# WINEPATH: libregnum's, graylib's, yaml-glib's and the MinGW runtime (GLib
# and friends).  Its prefix lives in the build directory: Wine refuses a
# new prefix whose parent the user does not own (the container's /tmp), and
# it is reused.
LRG_EXEC_WRAPPER ?= $(LRG_ROOT)/build-aux/cross/lrg-wine-run

# Test paths Wine cannot run (tests/Makefile passes each as -s PATH -- exact
# paths -- and `make WINDOWS=1 test' lists them), one group per cause:

# Wine has no Recycle Bin: g_file_trash() fails ("Unknown error"); a real
# Windows trashes the file
LRG_WINDOWS_SKIP_TRASH := /asset-meta/trash

# an audio output device; the build container has none
LRG_WINDOWS_SKIP_AUDIO := \
    /audio/manager/procedural-add /audio/manager/procedural-remove \
    /audio/manager/procedural-get-names /audio/manager/procedural-play-stop \
    /audio/manager/procedural-stop-all

LRG_TEST_SKIP_PATHS := $(LRG_WINDOWS_SKIP_TRASH) $(LRG_WINDOWS_SKIP_AUDIO)

# binutils for tests/test-cross-artifacts.sh (make test-cross-artifacts)
LRG_OBJDUMP ?= $(LRG_TARGET_TRIPLE)-objdump
LRG_MINGW_BIN ?= /usr/x86_64-w64-mingw32/sys-root/mingw/bin
export WINEPATH = $(PROJECT_ROOT)/$(LIBOUTDIR);$(GRAYLIB_BUILDDIR)/lib;$(YAMLGLIB_BUILDDIR)/$(DEP_BUILD_TYPE);$(LRG_MINGW_BIN)
export WINEPREFIX ?= $(PROJECT_ROOT)/$(BUILDDIR)/wine
# The prefix is copied from the one the image made (/opt/lrg-wine) the first
# time, which is much faster than letting wineboot create it
LRG_TEST_SETUP = test -d "$(WINEPREFIX)/drive_c" || { mkdir -p "$(WINEPREFIX)" && cp -a /opt/lrg-wine/. "$(WINEPREFIX)/"; }
export WINEDEBUG ?= -all
# no .NET (Mono) or HTML (Gecko) runtime: tests are plain console programs,
# and Wine would otherwise try to install them when it creates the prefix
export WINEDLLOVERRIDES ?= mscoree,mshtml=

# Build container (build-aux/containers/windows-amd64/Containerfile)
LRG_CONTAINER_PLATFORM :=
