# android-arm64.mk - target profile: Android on arm64-v8a (NDK clang)
#
# Copyright 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# description: Android arm64-v8a (API 29+), NDK clang; libraries and APKs
#
# Read by config.mk when LRG_TARGET=android-arm64 (`make ANDROID=1').
# Platform notes: docs/platforms/android.org.  Layout and rationale:
# docs/cross-compile.org.
#
# Output: build/{release,debug}-android-arm64/

LRG_TARGET_TRIPLE := aarch64-linux-android

TARGET_PLATFORM := android

# GObject Introspection runs the library it scans, which a cross build
# cannot do
BUILD_GIR := 0

# Build container (build-aux/containers/android-arm64/Containerfile).  The
# NDK ships x86_64 Linux host tools only, so the container is always amd64
# (emulated on an aarch64 host).
LRG_CONTAINER_PLATFORM := linux/amd64
