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

# Test binaries are PE executables; run them through Wine
LRG_EXEC_WRAPPER ?= wine

# Build container (build-aux/containers/windows-amd64/Containerfile)
LRG_CONTAINER_PLATFORM :=
