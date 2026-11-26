# dispatch.mk - Libregnum: does this run build here or in a build container?
#
# Copyright 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Included after target.mk by libregnum's GNUmakefile, and by the
# GNUmakefile of a project that reuses libregnum's cross builds
# (docs/cross-compile.org, "Consuming the cross builds from another
# project").  Sets:
#
#   LRG_USE_CONTAINER  1 when a cross target builds in its container: not
#                      NO_BUILD_CONTAINER=1 and not already inside it
#   LRG_DISPATCH       1 when this run hands its goals to the container:
#                      LRG_USE_CONTAINER=1 and at least one goal is not
#                      host-safe (LRG_HOST_SAFE_GOALS from target.mk, plus
#                      the including project's LRG_EXTRA_HOST_SAFE_GOALS)
#
# LRG_EXTRA_HOST_SAFE_GOALS: goals of the including project that never
# compile (its own clean, help, device goals ...); set it before this file
# is included.  Empty for libregnum itself.

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
    ifneq ($(filter-out $(LRG_HOST_SAFE_GOALS) $(LRG_EXTRA_HOST_SAFE_GOALS),$(or $(MAKECMDGOALS),all)),)
        LRG_DISPATCH := 1
    endif
endif
