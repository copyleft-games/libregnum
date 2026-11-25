# target.mk - Libregnum build target resolution
#
# Copyright 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Decides WHAT the build produces code for (the target) and WHERE it goes
# (the build directory suffix).  Included first by config.mk (so the root
# Makefile, tests/, examples/ and bench/ all agree) and by GNUmakefile (to
# decide whether the build runs in a container).  It must stay cheap: no
# pkg-config, no compiler probes -- only uname.
#
# Variables it sets (all documented in docs/cross-compile.org):
#
#   LRG_HOST_TARGET    the machine make runs on, <os>-<arch> (overridable,
#                      so tests can pretend to be an aarch64 host)
#   LRG_TARGET         the machine the build is for, <os>-<arch>
#   LRG_TARGET_OS      linux | windows | android | ...
#   LRG_TARGET_ARCH    amd64 | arm64 | ...
#   LRG_IS_CROSS       1 when LRG_TARGET differs from LRG_HOST_TARGET
#   LRG_TARGET_PROFILE build-aux/mk/targets/<target>.mk (cross only)
#   LRG_BUILD_SUFFIX   "" natively, else "-<target>" without "linux-":
#                      -windows-amd64, -arm64, -android-arm64
#   LRG_KNOWN_TARGETS  every target that has a profile
#   LRG_CROSS_BUILD_DIRS  build/{release,debug}<suffix> of every profile
#
# Ways to pick a target (at most one):
#
#   make LRG_TARGET=windows-amd64     the canonical spelling
#   make WINDOWS=1                    windows-amd64
#   make ARM64=1                      linux-arm64
#   make ANDROID=1                    android-arm64
#   make CROSS=x86_64-w64-mingw32     windows-amd64 (legacy spelling)
#
# Asking for the host's own target (make ARM64=1 on an aarch64 machine) is
# a plain native build: no suffix, no container.

ifndef LRG_TARGET_MK_INCLUDED
LRG_TARGET_MK_INCLUDED := 1

LRG_MK_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
LRG_TARGETS_DIR := $(LRG_MK_DIR)/targets

# Every target with a profile, e.g. "android-arm64 linux-arm64 windows-amd64"
LRG_KNOWN_TARGETS := $(sort $(patsubst %.mk,%,$(notdir $(wildcard $(LRG_TARGETS_DIR)/*.mk))))

# =============================================================================
# Host
# =============================================================================

LRG_UNAME_S := $(shell uname -s)
LRG_UNAME_M := $(shell uname -m)

ifeq ($(LRG_UNAME_S),Linux)
    LRG_HOST_OS := linux
else ifeq ($(LRG_UNAME_S),Darwin)
    LRG_HOST_OS := macos
else ifeq ($(LRG_UNAME_S),FreeBSD)
    LRG_HOST_OS := freebsd
else ifneq (,$(findstring MINGW,$(LRG_UNAME_S))$(findstring MSYS,$(LRG_UNAME_S)))
    LRG_HOST_OS := windows
else
    LRG_HOST_OS := unknown
endif

# Debian/Docker spellings (amd64, arm64) for the arch half
ifneq (,$(filter x86_64 amd64,$(LRG_UNAME_M)))
    LRG_HOST_ARCH := amd64
else ifneq (,$(filter aarch64 arm64,$(LRG_UNAME_M)))
    LRG_HOST_ARCH := arm64
else
    LRG_HOST_ARCH := $(LRG_UNAME_M)
endif

LRG_HOST_TARGET ?= $(LRG_HOST_OS)-$(LRG_HOST_ARCH)

# =============================================================================
# Requested target
# =============================================================================

# The convenience switches, each mapped to the target it names
LRG_TARGET_ALIASES :=
ifeq ($(WINDOWS),1)
    LRG_TARGET_ALIASES += windows-amd64
endif
ifeq ($(ARM64),1)
    LRG_TARGET_ALIASES += linux-arm64
endif
ifeq ($(ANDROID),1)
    LRG_TARGET_ALIASES += android-arm64
endif
# CROSS=<triple> predates the target model; only the MinGW triple was ever
# supported, so it is the only one still accepted.
ifneq ($(strip $(CROSS)),)
    ifeq ($(strip $(CROSS)),x86_64-w64-mingw32)
        LRG_TARGET_ALIASES += windows-amd64
    else
        $(error CROSS=$(CROSS) is not supported; pick a target with LRG_TARGET=<target> (one of: $(LRG_KNOWN_TARGETS)))
    endif
endif
# WINDOWS=1 CROSS=x86_64-w64-mingw32 is one request, not two
LRG_TARGET_ALIASES := $(sort $(LRG_TARGET_ALIASES))

ifneq ($(word 2,$(LRG_TARGET_ALIASES)),)
    $(error Conflicting targets requested: $(LRG_TARGET_ALIASES) -- use only one of WINDOWS=1, ARM64=1, ANDROID=1, CROSS=, LRG_TARGET=)
endif

ifdef LRG_TARGET
    ifneq ($(LRG_TARGET_ALIASES),)
        ifneq ($(LRG_TARGET_ALIASES),$(LRG_TARGET))
            $(error Conflicting targets requested: LRG_TARGET=$(LRG_TARGET) and $(LRG_TARGET_ALIASES))
        endif
    endif
else
    LRG_TARGET := $(or $(LRG_TARGET_ALIASES),$(LRG_HOST_TARGET))
endif

LRG_TARGET_OS := $(firstword $(subst -, ,$(LRG_TARGET)))
LRG_TARGET_ARCH := $(lastword $(subst -, ,$(LRG_TARGET)))

# =============================================================================
# Native or cross
# =============================================================================

ifeq ($(LRG_TARGET),$(LRG_HOST_TARGET))
    LRG_IS_CROSS := 0
    LRG_TARGET_PROFILE :=
    LRG_COMPUTED_SUFFIX :=
else
    LRG_IS_CROSS := 1
    LRG_TARGET_PROFILE := $(LRG_TARGETS_DIR)/$(LRG_TARGET).mk
    ifeq ($(wildcard $(LRG_TARGET_PROFILE)),)
        $(error Unknown target '$(LRG_TARGET)' (host is $(LRG_HOST_TARGET)); known targets: $(LRG_KNOWN_TARGETS))
    endif
    LRG_COMPUTED_SUFFIX := -$(patsubst linux-%,%,$(LRG_TARGET))
endif

# Inside a build container the outer make already decided the suffix and
# passes it in (an arm64 container would otherwise look native to itself);
# everywhere else it is derived here and an environment value is ignored.
ifeq ($(LRG_IN_CONTAINER),1)
    LRG_BUILD_SUFFIX ?= $(LRG_COMPUTED_SUFFIX)
else ifneq ($(origin LRG_BUILD_SUFFIX),command line)
    LRG_BUILD_SUFFIX := $(LRG_COMPUTED_SUFFIX)
endif

# ASan/UBSan builds are host-only (build/sanitize has no target suffix)
ifeq ($(LRG_IS_CROSS),1)
    ifeq ($(SANITIZE),1)
        $(error SANITIZE=1 is native-only; it cannot be combined with the $(LRG_TARGET) target)
    endif
endif

# Output directories of every cross target, so a native `make clean` can
# leave them alone (and `make clean-targets` can find them)
LRG_CROSS_BUILD_DIRS := $(strip $(foreach t,$(filter-out $(LRG_HOST_TARGET),$(LRG_KNOWN_TARGETS)),\
    build/release-$(patsubst linux-%,%,$(t)) build/debug-$(patsubst linux-%,%,$(t))))

# Goals that never compile anything: they run on the host even for a cross
# target, without its toolchain or build container
LRG_HOST_SAFE_GOALS := clean clean-targets distclean print-target targets help \
    container-image container-shell container-prune print-container \
    apk-install apk-run test-device

# lrg_default VAR,VALUE -- a profile's default for VAR: applied unless the
# user set VAR on the command line or in the environment.  Profiles are
# read after config.mk's own `VAR ?= x' lines, so a plain ?= would never win.
lrg_default = $(if $(filter file default undefined,$(origin $(1))),$(eval $(1) := $(2)))

endif # LRG_TARGET_MK_INCLUDED
