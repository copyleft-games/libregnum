# rules.mk - Libregnum Build Rules and Helpers
#
# Copyright 2025 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Common build rules and helper functions.

# =============================================================================
# Terminal Colors (for pretty output)
# =============================================================================

# Check if terminal supports colors
ifneq ($(TERM),)
    TPUT := $(shell command -v tput 2>/dev/null)
    ifneq ($(TPUT),)
        COLORS := $(shell $(TPUT) colors 2>/dev/null)
    endif
endif

ifeq ($(shell test $(COLORS) -ge 8 2>/dev/null && echo yes),yes)
    COLOR_RESET := $(shell $(TPUT) sgr0)
    COLOR_BOLD := $(shell $(TPUT) bold)
    COLOR_RED := $(shell $(TPUT) setaf 1)
    COLOR_GREEN := $(shell $(TPUT) setaf 2)
    COLOR_YELLOW := $(shell $(TPUT) setaf 3)
    COLOR_BLUE := $(shell $(TPUT) setaf 4)
    COLOR_CYAN := $(shell $(TPUT) setaf 6)
else
    COLOR_RESET :=
    COLOR_BOLD :=
    COLOR_RED :=
    COLOR_GREEN :=
    COLOR_YELLOW :=
    COLOR_BLUE :=
    COLOR_CYAN :=
endif

# =============================================================================
# Output Functions
# =============================================================================

# Print a status message
# Usage: $(call print_status,"Building library...")
define print_status
	@printf "$(COLOR_GREEN)$(COLOR_BOLD)==>$(COLOR_RESET) %s\n" $(1)
endef

# Print a warning message
# Usage: $(call print_warning,"Feature disabled")
define print_warning
	@printf "$(COLOR_YELLOW)$(COLOR_BOLD)Warning:$(COLOR_RESET) %s\n" $(1)
endef

# Print an error message
# Usage: $(call print_error,"Build failed")
define print_error
	@printf "$(COLOR_RED)$(COLOR_BOLD)Error:$(COLOR_RESET) %s\n" $(1)
endef

# Print info message
# Usage: $(call print_info,"Using debug build")
define print_info
	@printf "$(COLOR_CYAN)Info:$(COLOR_RESET) %s\n" $(1)
endef

# Print compile action
# Usage: $(call print_compile,"src/core/lrg-engine.c")
define print_compile
	@printf "  $(COLOR_BLUE)CC$(COLOR_RESET)      %s\n" $(1)
endef

# Print link action
# Usage: $(call print_link,"liblibregnum.so")
define print_link
	@printf "  $(COLOR_BLUE)LINK$(COLOR_RESET)    %s\n" $(1)
endef

# Print archive action
# Usage: $(call print_archive,"liblibregnum.a")
define print_archive
	@printf "  $(COLOR_BLUE)AR$(COLOR_RESET)      %s\n" $(1)
endef

# Print GIR action
# Usage: $(call print_gir,"Libregnum-1.gir")
define print_gir
	@printf "  $(COLOR_BLUE)GIR$(COLOR_RESET)     %s\n" $(1)
endef

# =============================================================================
# Directory Creation
# =============================================================================

# Create build directories
$(BUILDDIR):
	@$(MKDIR_P) $@

$(OBJDIR):
	@$(MKDIR_P) $@

$(LIBOUTDIR):
	@$(MKDIR_P) $@

$(GIROUTDIR):
	@$(MKDIR_P) $@

# =============================================================================
# Help Target
# =============================================================================

.PHONY: help
help:
	@echo "Libregnum Build System"
	@echo ""
	@echo "Usage: make [target] [options]"
	@echo ""
	@echo "Targets:"
	@echo "  all          Build the library (default)"
	@echo "  lib          Build static and shared libraries"
	@echo "  lib-static   Build static library only"
	@echo "  lib-shared   Build shared library only"
	@echo "  gir          Generate GObject Introspection files"
	@echo "  test         Build and run unit tests"
	@echo "  test-sanitize  Run tests under ASan+UBSan (own build/sanitize dir)"
	@echo "  test-valgrind  Run tests under valgrind (excludes scripting tests)"
	@echo "  examples     Build example programs"
	@echo "  docs         Generate documentation (requires gi-docgen)"
	@echo "  install      Install to PREFIX (default: /usr/local)"
	@echo "  uninstall    Remove installed files"
	@echo "  clean        Remove build artifacts"
	@echo "  distclean    Remove all generated files"
	@echo "  deps         Build dependencies (graylib, yaml-glib)"
	@echo "  clean-targets  Remove every cross target's build directories"
	@echo "  targets      List build targets and their directories"
	@echo "  print-target Show the target, build dir and toolchain in use"
	@echo "  test-build-system  Build-system tests + native no-regression gate"
	@echo "  test-minimal-features  Build without libsoup/libpq (build/minimal), test the stubs"
	@echo "  test-cross-arm64   arm64: all, test (qemu), examples, binary checks"
	@echo "  test-cross-windows Windows: all, test (Wine), examples, binary checks"
	@echo "  test-cross-android Android: all, binary checks, shaders, an APK"
	@echo "  test-cross         All three test-cross-* gates, one after another"
	@echo "  test-cross-artifacts  Check a cross build's binaries (e.g. make ARM64=1 test-cross-artifacts)"
	@echo "  test-device        Run APK_GAME's APK on an adb device (make ANDROID=1 test-device APK_GAME=...)"
	@echo "  container-image    Build a cross target's build container (e.g. make WINDOWS=1 container-image)"
	@echo "  container-shell    Shell in a cross target's build container"
	@echo "  container-prune    Remove a cross target's outdated container images"
	@echo "  print-container    Show a cross target's container image and recipe hash"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1         Enable debug build (-g3 -O0)"
	@echo "  SANITIZE=1      ASan+UBSan build (implies DEBUG=1; build/sanitize dir)"
	@echo "  MINIMAL_FEATURES=1  No optional dependencies (SOUP=0 LIBPQ=0; build/minimal dir)"
	@echo "  ASAN=1          Enable AddressSanitizer (requires DEBUG=1)"
	@echo "  UBSAN=1         Enable UndefinedBehaviorSanitizer (requires DEBUG=1)"
	@echo "  ENABLE_TRACE=1  Enable trace logging at compile time"
	@echo "  PREFIX=/path    Set installation prefix"
	@echo "  LRG_TARGET=T    Build for target T (see make targets); output in build/<type>-T"
	@echo "  WINDOWS=1       Same as LRG_TARGET=windows-amd64 (MinGW-w64)"
	@echo "  ARM64=1         Same as LRG_TARGET=linux-arm64 (gcc + aarch64 sysroot)"
	@echo "  ANDROID=1       Same as LRG_TARGET=android-arm64 (NDK clang)"
	@echo "  NO_BUILD_CONTAINER=1   Build a cross target on the host (toolchain installed)"
	@echo "  BUILD_CONTAINER_BASE=I Base image of the build containers (fedora:45)"
	@echo "  BUILD_CONTAINER_IMAGE=I Use a prebuilt build container"
	@echo "  STEAM=1         Enable Steam SDK integration (opt-in)"
	@echo "  MCP=1           Enable MCP server for AI debugging (opt-in)"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Release build"
	@echo "  make DEBUG=1            # Debug build"
	@echo "  make DEBUG=1 ASAN=1     # Debug with AddressSanitizer"
	@echo "  make test               # Build and run tests"
	@echo "  make test-sanitize      # Tests under ASan+UBSan (isolated build dir)"
	@echo "  make test-valgrind      # Tests under valgrind (excludes scripting tests)"
	@echo "  make install PREFIX=/opt/libregnum"
	@echo "  make WINDOWS=1          # Windows build in build/release-windows-amd64"
	@echo "  make ARM64=1 DEBUG=1    # aarch64 build in build/debug-arm64"
	@echo "  make ANDROID=1          # Android build in build/release-android-arm64"
	@echo "  make STEAM=1            # Build with Steam SDK support"
	@echo "  make MCP=1              # Build with MCP server for AI debugging"
	@echo ""
	@echo "Build Configuration:"
	@echo "  Host Platform:    $(PLATFORM)"
	@echo "  Target Platform:  $(TARGET_PLATFORM)"
	@echo "  C Standard:       $(CSTD)"
	@echo "  Debug:            $(DEBUG)"
	@echo "  Build Dir:        $(BUILDDIR)"
ifneq ($(CROSS),)
	@echo "  Cross Compiler:   $(CROSS)"
endif

# =============================================================================
# Build Targets (native and cross)
# =============================================================================

# Where this build goes and with what toolchain (docs/cross-compile.org)
.PHONY: print-target
print-target:
	@echo "Host target:      $(LRG_HOST_TARGET)"
	@echo "Target:           $(LRG_TARGET) ($(if $(filter 1,$(LRG_IS_CROSS)),cross,native))"
	@echo "Target platform:  $(TARGET_PLATFORM)"
	@echo "Profile:          $(or $(LRG_TARGET_PROFILE),none (native))"
	@echo "Build dir:        $(BUILDDIR)"
	@echo "CC:               $(CC)"
	@echo "AR:               $(AR)"
	@echo "PKG_CONFIG:       $(PKG_CONFIG)"
	@echo "GIR:              $(BUILD_GIR)"
	@echo "Toolchain check:  $(or $(LRG_TOOLCHAIN_CHECK),n/a (native))"
	@echo "Dropped warnings: $(or $(LRG_DROPPED_WARN_CFLAGS),none)"

# Every target with a profile, its build directories and description
.PHONY: targets
targets:
	@printf '%-16s %-38s %s\n' TARGET 'BUILD DIRS' DESCRIPTION
	@printf '%-16s %-38s %s\n' '$(LRG_HOST_TARGET)' 'build/{release,debug}' 'this machine (native)'
	@$(foreach t,$(filter-out $(LRG_HOST_TARGET),$(LRG_KNOWN_TARGETS)),\
		printf '%-16s %-38s %s\n' '$(t)' 'build/{release,debug}-$(patsubst linux-%,%,$(t))' \
			"$$(sed -n 's/^# description: //p' '$(LRG_TARGETS_DIR)/$(t).mk')";)

# =============================================================================
# Dependency Checks
# =============================================================================

.PHONY: check-deps
check-deps:
ifeq ($(LIBPQ),1)
	@$(PKG_CONFIG) --exists libpq || (echo "Missing: libpq-devel" && exit 1)
endif
	@$(PKG_CONFIG) --exists openssl || (echo "Missing: OpenSSL development package (Fedora: openssl-devel)" && exit 1)
	@$(PKG_CONFIG) --exists zlib || (echo "Missing: zlib development package (Fedora: zlib-devel)" && exit 1)
	@$(PKG_CONFIG) --exists sqlite3 || (echo "Missing: SQLite development package (Fedora: sqlite-devel)" && exit 1)
ifeq ($(TARGET_PLATFORM),windows)
	@$(PKG_CONFIG) --exists glib-2.0 || (echo "Missing: mingw64-glib2" && exit 1)
	@$(PKG_CONFIG) --exists gobject-2.0 || (echo "Missing: mingw64-glib2" && exit 1)
	@$(PKG_CONFIG) --exists gio-2.0 || (echo "Missing: mingw64-glib2" && exit 1)
	@$(PKG_CONFIG) --exists json-glib-1.0 || (echo "Missing: mingw64-json-glib" && exit 1)
	@$(PKG_CONFIG) --exists yaml-0.1 || echo "Note: mingw64-libyaml not found (will use bundled yaml-glib)"
	$(call print_status,"Windows cross-compile dependencies found")
else ifeq ($(TARGET_PLATFORM),android)
	@$(PKG_CONFIG) --exists glib-2.0 gobject-2.0 gio-2.0 gmodule-2.0 || (echo "Missing: the Android GLib stack (build-aux/containers/android-arm64)" && exit 1)
	@$(PKG_CONFIG) --exists json-glib-1.0 yaml-0.1 || (echo "Missing: json-glib or libyaml in the Android GLib stack" && exit 1)
	@$(PKG_CONFIG) --exists libdex-1 || echo "Note: no libdex for Android (async helpers built off)"
	$(call print_status,"Android cross-compile dependencies found")
else
	@$(PKG_CONFIG) --exists glib-2.0 || (echo "Missing: glib2-devel" && exit 1)
	@$(PKG_CONFIG) --exists gobject-2.0 || (echo "Missing: glib2-devel" && exit 1)
	@$(PKG_CONFIG) --exists gio-2.0 || (echo "Missing: glib2-devel" && exit 1)
	@$(PKG_CONFIG) --exists libdex-1 || (echo "Missing: libdex-devel" && exit 1)
	@$(PKG_CONFIG) --exists json-glib-1.0 || (echo "Missing: json-glib-devel" && exit 1)
	@$(PKG_CONFIG) --exists yaml-0.1 || (echo "Missing: libyaml-devel" && exit 1)
	@$(PKG_CONFIG) --exists luajit || echo "Note: luajit-devel not found (scripting optional)"
ifeq ($(BUILD_GIR),1)
	@command -v $(GIR_SCANNER) > /dev/null || (echo "Missing: gobject-introspection-devel" && exit 1)
endif
	$(call print_status,"All dependencies found")
endif

# =============================================================================
# Version Info Target
# =============================================================================

.PHONY: version
version:
	@echo "Libregnum $(VERSION)"
	@echo "API Version: $(API_VERSION)"
	@echo "SO Version: $(SO_VERSION).$(SO_MINOR).$(SO_RELEASE)"
