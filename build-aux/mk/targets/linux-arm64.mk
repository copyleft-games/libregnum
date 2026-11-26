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

LRG_CROSS_DIR := $(LRG_ROOT)/build-aux/cross

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

# binutils for tests/test-cross-artifacts.sh (make test-cross-artifacts)
LRG_READELF ?= $(LRG_TARGET_TRIPLE)-readelf
# fontconfig in the sysroot does not find its default config through qemu's
# path mapping; point it there (its font directories then map as usual)
export FONTCONFIG_FILE ?= $(ARM64_SYSROOT)/etc/fonts/fonts.conf

# Test paths that cannot run under that emulation (tests/Makefile passes
# each to every binary as -s PATH -- exact paths: GTest's prefix skipping
# does not work -- and `make ARM64=1 test' lists them), one group per cause:

# g_test_trap_subprocess() re-executes the aarch64 test binary, which the
# host kernel cannot start without binfmt registration
LRG_ARM64_SKIP_SUBPROCESS := \
    /billboard-batch/invalid-size /paint/list/measure-headless \
    /paint/renderer/headless /paint/list/unbalanced-pops \
    /template/base/max-frames-in-flight-env

# start another aarch64 program (lrgldr, lrgeditor --merge): binfmt again
LRG_ARM64_SKIP_SPAWN := \
    /lrgldr/forward-args /lrgldr/forward-error /lrgldr/info \
    /lrgldr/missing-module /lrgldr/no-module /lrgldr/version \
    /workflows/merge-files

# compile C at test time with the container's (x86_64) gcc: crispy, plugins
LRG_ARM64_SKIP_COMPILE := \
    /scripting-crispy/compile-error /scripting-crispy/fixture \
    /scripting-crispy/load-string-main /scripting-crispy/manager \
    /scripting-crispy/search-path-and-cflags /script-reload/crispy \
    /pack-writer/plugin /editor-app/dialogs/plugin-dock

# user-mode qemu implements neither Landlock nor seccomp for its guest
LRG_ARM64_SKIP_SANDBOX := \
    /sandbox/apply/allow-exec /sandbox/apply/filesystem \
    /sandbox/apply/network-client /sandbox/apply/network-server \
    /sandbox/apply/seccomp-only

# an audio output device; the build container has none
LRG_ARM64_SKIP_AUDIO := \
    /audio/manager/procedural-add /audio/manager/procedural-remove \
    /audio/manager/procedural-get-names /audio/manager/procedural-play-stop \
    /audio/manager/procedural-stop-all

# a frame-time budget, meaningless under emulation
LRG_ARM64_SKIP_TIMING := /broadphase/perf/10k

LRG_TEST_SKIP_PATHS := $(LRG_ARM64_SKIP_SUBPROCESS) $(LRG_ARM64_SKIP_SPAWN) \
    $(LRG_ARM64_SKIP_COMPILE) $(LRG_ARM64_SKIP_SANDBOX) $(LRG_ARM64_SKIP_AUDIO) \
    $(LRG_ARM64_SKIP_TIMING)

# Real aarch64 failures the cross build found (2026-11-24), skipped so the
# rest of their binaries run and named at the end of every test run until
# fixed (docs/platforms/linux-arm64.org, "Known aarch64 bugs"):
#   results that must be bit-identical across architectures and are not
#   (docs/modules/replay/determinism.org):
#     /replay/golden/physics-timers-rng  the golden replay diverges
#     /collide/random-consistency        a contact depth misses its tolerance
#     /foley-patch/pinned-bytes          synthesised audio hashes differently
LRG_TEST_KNOWN_FAILURES := \
    /replay/golden/physics-timers-rng /collide/random-consistency \
    /foley-patch/pinned-bytes

# What must exist for a host build (NO_BUILD_CONTAINER=1; config.mk): the
# cross gcc, pkg-config and the sysroot
LRG_TOOLCHAIN_CHECK := command -v aarch64-linux-gnu-gcc && command -v pkg-config && test -d $(ARM64_SYSROOT)/usr/lib64

# Build container (build-aux/containers/linux-arm64/Containerfile).  The
# container is the host's arch; only the compiler and sysroot are aarch64.
LRG_CONTAINER_PLATFORM :=
