# container.mk - Libregnum cross builds inside a build container
#
# Copyright 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Included by GNUmakefile for a cross target unless NO_BUILD_CONTAINER=1 or
# make is already running inside the container.  Two parts:
#
#   1. Image management (always): the image is built from
#      build-aux/containers/<target>/Containerfile and tagged with a hash of
#      its inputs, so editing a recipe rebuilds the image on the next build
#      and an unchanged recipe reuses it.  Goals: container-image,
#      container-shell, container-prune.
#   2. Dispatch (LRG_DISPATCH=1): every goal on the command line runs as ONE
#      `make <goals>` inside the container, with the checkout mounted at the
#      same path and the caller's MAKEFLAGS (-j, DEBUG=1, every VAR=value)
#      forwarded minus the jobserver, which cannot cross the boundary.
#
# Variables (docs/cross-compile.org, "Container images"):
#
#   CONTAINER_ENGINE      podman (default) or a compatible CLI
#   BUILD_CONTAINER_BASE  base image the Containerfile starts FROM
#                         (default registry.fedoraproject.org/fedora:45)
#   BUILD_CONTAINER_IMAGE use this image as-is (prebuilt or pushed to a
#                         registry); nothing is built locally
#   BUILD_CONTAINER_ARGS  extra `run' arguments, e.g. a ccache volume
#   NO_BUILD_CONTAINER=1  build on the host instead (target.mk, GNUmakefile)

CONTAINER_ENGINE ?= podman
BUILD_CONTAINER_BASE ?= registry.fedoraproject.org/fedora:45
BUILD_CONTAINER_IMAGE ?=
BUILD_CONTAINER_ARGS ?=

# The build context is build-aux/: the recipes (containers/) and the cross
# wrappers the images install (cross/)
LRG_CONTAINER_CONTEXT := $(PROJECT_ROOT)/build-aux
LRG_CONTAINER_DIR := $(LRG_CONTAINER_CONTEXT)/containers/$(LRG_TARGET)
LRG_CONTAINER_LABEL := org.libregnum.build-target=$(LRG_TARGET)

ifeq ($(wildcard $(LRG_CONTAINER_DIR)/Containerfile),)
    $(error No build container for $(LRG_TARGET): $(LRG_CONTAINER_DIR)/Containerfile is missing (or build on the host with NO_BUILD_CONTAINER=1))
endif

# --platform for a target whose toolchain only exists for one host arch
# (the Android NDK is x86_64-only); empty means the host's arch
LRG_CONTAINER_PLATFORM_ARG := $(if $(LRG_CONTAINER_PLATFORM),--platform $(LRG_CONTAINER_PLATFORM))

# Content hash of everything the image is built from: the shared scripts,
# this target's directory, the cross wrappers (file names and contents),
# the base image and the platform.  12 hex digits are plenty to tell
# recipes apart.
LRG_CONTAINER_HASH := $(shell { \
        printf '%s\n' '$(BUILD_CONTAINER_BASE)' '$(LRG_CONTAINER_PLATFORM)'; \
        CDPATH= cd '$(LRG_CONTAINER_CONTEXT)' && find containers/common 'containers/$(LRG_TARGET)' cross \
            -type f 2>/dev/null | LC_ALL=C sort | xargs sha256sum; \
    } | sha256sum | cut -c1-12)
LRG_CONTAINER_TAG := localhost/libregnum-build-$(LRG_TARGET):$(LRG_CONTAINER_HASH)
LRG_CONTAINER_REF := $(or $(BUILD_CONTAINER_IMAGE),$(LRG_CONTAINER_TAG))

# The git directory shared by every worktree.  The build asks git about the
# raylib submodule (graylib's rules.mk), and in a worktree the submodules'
# .git files point into the main checkout's .git, which is outside the
# mounted tree; mount it too, read-only, at the same path.
LRG_GIT_COMMON_DIR := $(shell git -C '$(PROJECT_ROOT)' rev-parse --path-format=absolute --git-common-dir 2>/dev/null)
LRG_CONTAINER_GIT_MOUNT := $(if $(filter-out $(PROJECT_ROOT)/%,$(LRG_GIT_COMMON_DIR)),\
    -v $(LRG_GIT_COMMON_DIR):$(LRG_GIT_COMMON_DIR):ro)

# How every container starts.  Same-path mount: .d files, rpaths and
# compile_commands entries stay valid on the host.  keep-id: the files the
# build writes belong to the caller.  label=disable: no SELinux relabelling
# of the checkout.  HOME=/tmp: keep-id's home directory does not exist in
# the image.
LRG_CONTAINER_RUN = $(CONTAINER_ENGINE) run --rm \
    $(LRG_CONTAINER_PLATFORM_ARG) \
    --userns=keep-id \
    --security-opt label=disable \
    -v $(PROJECT_ROOT):$(PROJECT_ROOT) \
    $(LRG_CONTAINER_GIT_MOUNT) \
    -w $(CURDIR) \
    -e HOME=/tmp \
    -e TERM \
    -e LRG_IN_CONTAINER=1 \
    -e LRG_BUILD_SUFFIX=$(LRG_BUILD_SUFFIX)

# =============================================================================
# Image management
# =============================================================================

# Build the image unless it already exists (or a ready image was named)
.PHONY: container-image
container-image:
ifeq ($(BUILD_CONTAINER_IMAGE),)
	@if ! $(CONTAINER_ENGINE) image exists '$(LRG_CONTAINER_TAG)'; then \
		printf '==> Building the %s build container %s (from %s)\n' \
			'$(LRG_TARGET)' '$(LRG_CONTAINER_TAG)' '$(BUILD_CONTAINER_BASE)'; \
		$(CONTAINER_ENGINE) build $(LRG_CONTAINER_PLATFORM_ARG) \
			--build-arg BASE_IMAGE='$(BUILD_CONTAINER_BASE)' \
			--label '$(LRG_CONTAINER_LABEL)' \
			-t '$(LRG_CONTAINER_TAG)' \
			-f '$(LRG_CONTAINER_DIR)/Containerfile' \
			'$(LRG_CONTAINER_CONTEXT)' || exit $$?; \
	fi
else
	@printf '==> Using the %s build container %s\n' '$(LRG_TARGET)' '$(BUILD_CONTAINER_IMAGE)'
endif

# The image this target builds in, and what its tag is derived from
.PHONY: print-container
print-container:
	@echo "Target:           $(LRG_TARGET)"
	@echo "Engine:           $(CONTAINER_ENGINE)"
	@echo "Image:            $(LRG_CONTAINER_REF)"
	@echo "Recipe:           $(LRG_CONTAINER_DIR)/Containerfile"
	@echo "Base image:       $(BUILD_CONTAINER_BASE)"
	@echo "Platform:         $(or $(LRG_CONTAINER_PLATFORM),host)"
	@echo "Recipe hash:      $(LRG_CONTAINER_HASH)"
	@echo "Prebuilt image:   $(or $(BUILD_CONTAINER_IMAGE),no)"

# An interactive shell in the build container, set up like a build
.PHONY: container-shell
container-shell: container-image
	$(LRG_CONTAINER_RUN) -it $(BUILD_CONTAINER_ARGS) $(LRG_CONTAINER_REF) bash

# Remove this target's images except the current one
.PHONY: container-prune
container-prune:
	@$(CONTAINER_ENGINE) images --filter 'label=$(LRG_CONTAINER_LABEL)' \
		--format '{{.Repository}}:{{.Tag}}' | while read -r image; do \
		if [ "$$image" != '$(LRG_CONTAINER_TAG)' ]; then \
			echo "removing $$image"; \
			$(CONTAINER_ENGINE) rmi "$$image" || exit $$?; \
		fi; \
	done

# =============================================================================
# Dispatch
# =============================================================================

ifeq ($(LRG_DISPATCH),1)

# What runs in the container: the command-line goals, or the default
LRG_CONTAINER_GOALS := $(or $(MAKECMDGOALS),all)

# One container run carries every goal; each goal only waits for it
.PHONY: lrg-container-run $(LRG_CONTAINER_GOALS)
$(LRG_CONTAINER_GOALS): lrg-container-run
	@:

# MAKEFLAGS minus the jobserver (a host fifo/fd the container cannot see);
# -j<N> stays, so the inner make starts its own jobserver with N slots.
# Taken from the environment, where make has already escaped every
# VAR=value for re-reading.
lrg-container-run: container-image
	@printf '==> %s: make %s in %s\n' '$(LRG_TARGET)' '$(LRG_CONTAINER_GOALS)' '$(LRG_CONTAINER_REF)'
	@flags="$$(printf '%s' "$$MAKEFLAGS" | sed -E 's/ ?--jobserver-(auth|fds)=[^ ]*//g')"; \
	$(LRG_CONTAINER_RUN) -e MAKEFLAGS="$$flags" $(BUILD_CONTAINER_ARGS) \
		$(LRG_CONTAINER_REF) make $(LRG_CONTAINER_GOALS)

endif # LRG_DISPATCH
