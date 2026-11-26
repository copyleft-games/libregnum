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
# The build container (build-aux/containers/android-arm64/) provides:
#
#   /opt/android/ndk          the NDK (clang, sysroot, llvm-ar, ...)
#   /opt/android/sdk          build-tools (aapt2, zipalign, apksigner) and
#                             platforms/android-<api>/android.jar
#   /opt/android/arm64-v8a    the GLib stack as static libraries + .pc files
#
# On a host (NO_BUILD_CONTAINER=1) point ANDROID_NDK, ANDROID_SDK and
# ANDROID_SYSROOT at the same things.
#
# The NDK only has clang.  WARN_CFLAGS stays the project's list; config.mk
# drops the flags this clang does not know (LRG_CC_IS_GCC=0) and keeps
# every warning it does know an error.
#
# Output: build/{release,debug}-android-arm64/

LRG_TARGET_TRIPLE := aarch64-linux-android

ANDROID_NDK ?= /opt/android/ndk
ANDROID_SDK ?= /opt/android/sdk
ANDROID_SYSROOT ?= /opt/android/arm64-v8a
ANDROID_MIN_API ?= 29

LRG_NDK_BIN := $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin

# Toolchain (a command-line CC=... etc. still wins over these)
CC := $(LRG_NDK_BIN)/aarch64-linux-android$(ANDROID_MIN_API)-clang
AR := $(LRG_NDK_BIN)/llvm-ar
RANLIB := $(LRG_NDK_BIN)/llvm-ranlib
PKG_CONFIG := $(LRG_ROOT)/build-aux/cross/lrg-sysroot-pkg-config
LRG_CC_IS_GCC := 0

# The one gcc/clang dialect gap the code meets.  Types are forward-declared
# in lrg-types.h (typedef struct _LrgWidget LrgWidget;) and declared again by
# G_DECLARE_*_TYPE; an identical typedef redefinition is valid GNU C in
# gnu89 (gcc never warns) and valid C11, but clang reports it as a C11
# extension in gnu89 mode.  It is not a bug, and removing the forward
# declarations would touch every header, so clang is told to accept what
# gcc accepts.  Every other warning stays an error.  (Decided 2026-11-24;
# docs/platforms/android.org, "Compiler".)
LRG_DIALECT_WARN_CFLAGS := -Wno-typedef-redefinition

# The GLib stack is installed with --prefix=$(ANDROID_SYSROOT) (absolute
# paths in its .pc files) and is all static archives: query with --static
export LRG_SYSROOT := $(ANDROID_SYSROOT)
export LRG_SYSROOT_LIBDIRS := lib/pkgconfig:share/pkgconfig
export LRG_SYSROOT_PREFIXED := 0
export LRG_PKG_CONFIG_STATIC := 1

TARGET_PLATFORM := android

# GObject Introspection runs the library it scans, which a cross build
# cannot do; the editor app and its CLI are desktop tools; crispy compiles
# C at run time
BUILD_GIR := 0
$(call lrg_default,BUILD_EDITOR_UI,0)
$(call lrg_default,CRISPY,0)
# No desktop tools on Android: `all' builds the libraries (and `apk' the app)
$(call lrg_default,BUILD_TOOLS,0)
# Not in the Android GLib stack: libsoup (HTTP analytics) and libpq (the MMO
# server's PostgreSQL store); both features build as documented stubs
$(call lrg_default,SOUP,0)
$(call lrg_default,LIBPQ,0)

# Android links with bionic's libc, liblog (logcat), libandroid (assets,
# NativeActivity) and GLES 3 through EGL; -z max-page-size keeps every
# LOAD segment loadable on 16 KB-page devices (Android 15+)
LRG_PLATFORM_LIBS := -lEGL -lGLESv3 -landroid -llog -lm -ldl
LRG_PLATFORM_LDFLAGS := -Wl,-z,max-page-size=16384

# How a game's libmain.so links (the apk goal, and a parent project that
# links its own; docs/platforms/android.org, "Another project's APK"):
# -shared; --no-undefined: every symbol resolved now, not at load time on
# a phone; 16 KB pages for Android 15+; -u ANativeActivity_onCreate: keep
# the glue's entry point Android calls; --wrap=fopen: raylib's Android
# layer reads files from the APK's assets first (rcore_android.c)
LRG_APK_LDFLAGS := -shared -Wl,--no-undefined -Wl,-z,max-page-size=16384 \
    -u ANativeActivity_onCreate -Wl,--wrap=fopen
# The packager: build-aux/android/mkapk.sh --help
LRG_MKAPK := $(LRG_ROOT)/build-aux/android/mkapk.sh

# The NativeActivity glue raylib is built with (src/core/lrg-platform.c
# reads the app's internal storage path and asset manager through it)
LRG_TARGET_CFLAGS := -isystem $(ANDROID_NDK)/sources/android/native_app_glue

# The dependencies build with the same toolchain.  raylib builds its own
# Android platform layer (rcore_android.c, android_native_app_glue) against
# GLES 3.
LRG_DEP_FLAGS := CC=$(CC) AR=$(AR) RANLIB=$(RANLIB) PKG_CONFIG=$(PKG_CONFIG)
LRG_GRAYLIB_FLAGS := RAYLIB_PLATFORM=PLATFORM_ANDROID \
    RAYLIB_EXTRA_FLAGS="ANDROID_NDK=$(ANDROID_NDK) ANDROID_ARCH=arm64 ANDROID_API_VERSION=$(ANDROID_MIN_API) GRAPHICS=GRAPHICS_API_OPENGL_ES3"

# Android binaries do not run in the build container; tests run on a
# device (make ANDROID=1 test-device)
LRG_EXEC_WRAPPER ?=

# binutils for tests/test-cross-artifacts.sh (make test-cross-artifacts)
LRG_READELF ?= $(LRG_NDK_BIN)/llvm-readelf

# What must exist for a host build (NO_BUILD_CONTAINER=1; config.mk): the
# NDK's clang, pkg-config and the GLib stack
LRG_TOOLCHAIN_CHECK := test -x $(CC) && command -v pkg-config && test -d $(ANDROID_SYSROOT)/lib/pkgconfig

# Build container (build-aux/containers/android-arm64/Containerfile).  The
# NDK ships x86_64 Linux host tools only, so the container is always amd64
# (emulated on an aarch64 host).
LRG_CONTAINER_PLATFORM := linux/amd64
