# linux-arm64.mk - target profile: Linux on aarch64 (gcc cross + sysroot)
#
# Copyright 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# description: Linux aarch64, gcc cross build against a Fedora aarch64 sysroot
#
# Read by config.mk when LRG_TARGET=linux-arm64 (`make ARM64=1') on a host
# that is not itself aarch64; on an aarch64 host ARM64=1 is a native build
# and this file is not read.  Layout and rationale: docs/cross-compile.org,
# "linux-arm64"; platform notes: docs/platforms/linux-arm64.org.
#
# A true cross build, no emulation.  Fedora's aarch64-linux-gnu-gcc is a
# kernel compiler (no C library), so two wrappers in build-aux/cross/ pair
# it with a Fedora aarch64 sysroot:
#
#   lrg-aarch64-linux-gnu-gcc  --sysroot plus the sysroot gcc's crt files
#                              and libgcc (-B/-L)
#   lrg-sysroot-pkg-config     the sysroot's .pc files only, paths prefixed
#
# Both read LRG_SYSROOT, exported below from ARM64_SYSROOT.  The build
# container (build-aux/containers/linux-arm64/) has the sysroot at the
# default path; on a host (NO_BUILD_CONTAINER=1) point ARM64_SYSROOT at one.
#
# Output: build/{release,debug}-arm64/

LRG_TARGET_TRIPLE := aarch64-linux-gnu

ARM64_SYSROOT ?= /opt/sysroot/aarch64
export LRG_SYSROOT := $(ARM64_SYSROOT)

LRG_CROSS_DIR := $(PROJECT_ROOT)/build-aux/cross

# Toolchain (a command-line CC=... etc. still wins over these)
CC := $(LRG_CROSS_DIR)/lrg-aarch64-linux-gnu-gcc
AR := $(LRG_TARGET_TRIPLE)-ar
RANLIB := $(LRG_TARGET_TRIPLE)-ranlib
PKG_CONFIG := $(LRG_CROSS_DIR)/lrg-sysroot-pkg-config

TARGET_PLATFORM := linux

# GObject Introspection runs the library it scans, which a cross build
# cannot do
BUILD_GIR := 0

# The dependencies build with the same toolchain: graylib, yaml-glib and
# crispy get it on their command lines (LRG_DEP_FLAGS), and graylib passes
# it on to raylib's own make (RAYLIB_EXTRA_FLAGS)
LRG_DEP_FLAGS := CC=$(CC) AR=$(AR) RANLIB=$(RANLIB) PKG_CONFIG=$(PKG_CONFIG)
LRG_GRAYLIB_FLAGS := RAYLIB_EXTRA_FLAGS="CC=$(CC) AR=$(AR)"

# Test binaries run under user-mode qemu, with the sysroot as the dynamic
# loader's root (no binfmt registration needed)
LRG_EXEC_WRAPPER ?= qemu-aarch64-static -L $(ARM64_SYSROOT)

# What must exist for a host build (NO_BUILD_CONTAINER=1; config.mk): the
# cross gcc, pkg-config and the sysroot
LRG_TOOLCHAIN_CHECK := command -v aarch64-linux-gnu-gcc && command -v pkg-config && test -d $(ARM64_SYSROOT)/usr/lib64

# Build container (build-aux/containers/linux-arm64/Containerfile).  The
# container is the host's arch; only the compiler and sysroot are aarch64.
LRG_CONTAINER_PLATFORM :=
