# linux-arm64.mk - target profile: Linux on aarch64 (gcc cross + sysroot)
#
# Copyright 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# description: Linux aarch64, gcc cross build against a Fedora aarch64 sysroot
#
# Read by config.mk when LRG_TARGET=linux-arm64 (`make ARM64=1') on a host
# that is not itself aarch64; on an aarch64 host ARM64=1 is a native build
# and this file is not read.  Layout and rationale: docs/cross-compile.org.
#
# Output: build/{release,debug}-arm64/

LRG_TARGET_TRIPLE := aarch64-linux-gnu

TARGET_PLATFORM := linux

# GObject Introspection runs the library it scans, which a cross build
# cannot do
BUILD_GIR := 0

# Build container (build-aux/containers/linux-arm64/Containerfile).  The
# container is the host's arch; only the compiler and sysroot are aarch64.
LRG_CONTAINER_PLATFORM :=
