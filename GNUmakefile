# GNUmakefile - Libregnum build entry point
#
# Copyright 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# GNU make reads this before Makefile.  It decides whether this run builds
# here or hands the goals to a cross target's build container, and then
# includes the real build (Makefile) or the container dispatch
# (build-aux/mk/container.mk).  The native build is exactly Makefile.
#
#   make                        native, on the host
#   make WINDOWS=1              cross: in the windows-amd64 container
#   make WINDOWS=1 NO_BUILD_CONTAINER=1
#                               cross: on the host, toolchain installed here
#   make -f Makefile ...        bypass this file (native goals only)
#
# docs/cross-compile.org, "How dispatch works".

LRG_ENTRY_ROOT := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))

include $(LRG_ENTRY_ROOT)/build-aux/mk/target.mk

# Tells config.mk this run came through here (a cross build that bypasses
# it, e.g. `make -C tests WINDOWS=1', is refused there)
LRG_VIA_GNUMAKEFILE := 1

# A cross target builds in its container unless told otherwise or already
# inside it
LRG_USE_CONTAINER := 0
ifeq ($(LRG_IS_CROSS),1)
    ifneq ($(NO_BUILD_CONTAINER),1)
        ifneq ($(LRG_IN_CONTAINER),1)
            LRG_USE_CONTAINER := 1
        endif
    endif
endif

# Dispatch unless every goal is one that never compiles (clean, help, ...)
LRG_DISPATCH := 0
ifeq ($(LRG_USE_CONTAINER),1)
    ifneq ($(filter-out $(LRG_HOST_SAFE_GOALS),$(or $(MAKECMDGOALS),all)),)
        LRG_DISPATCH := 1
    endif
endif

ifeq ($(LRG_DISPATCH),1)
    # The whole build happens in the container; only the dispatch is read
    # here (no host pkg-config probes for a toolchain the host lacks)
    .DEFAULT_GOAL := all
    PROJECT_ROOT := $(LRG_ENTRY_ROOT)
    include $(LRG_TARGET_PROFILE)
    include $(LRG_ENTRY_ROOT)/build-aux/mk/container.mk
else
    include $(LRG_ENTRY_ROOT)/Makefile
    # container-image, container-shell, container-prune, print-container
    ifeq ($(LRG_USE_CONTAINER),1)
        include $(LRG_ENTRY_ROOT)/build-aux/mk/container.mk
    endif
endif
