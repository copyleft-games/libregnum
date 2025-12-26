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
    TPUT := $(shell which tput 2>/dev/null)
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
	@echo "  examples     Build example programs"
	@echo "  docs         Generate documentation (requires gi-docgen)"
	@echo "  install      Install to PREFIX (default: /usr/local)"
	@echo "  uninstall    Remove installed files"
	@echo "  clean        Remove build artifacts"
	@echo "  distclean    Remove all generated files"
	@echo "  deps         Build dependencies (graylib, yaml-glib)"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1         Enable debug build (-g3 -O0)"
	@echo "  ASAN=1          Enable AddressSanitizer (requires DEBUG=1)"
	@echo "  UBSAN=1         Enable UndefinedBehaviorSanitizer (requires DEBUG=1)"
	@echo "  ENABLE_TRACE=1  Enable trace logging at compile time"
	@echo "  PREFIX=/path    Set installation prefix"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Release build"
	@echo "  make DEBUG=1            # Debug build"
	@echo "  make DEBUG=1 ASAN=1     # Debug with AddressSanitizer"
	@echo "  make test               # Build and run tests"
	@echo "  make install PREFIX=/opt/libregnum"
	@echo ""
	@echo "Build Configuration:"
	@echo "  Platform:     $(PLATFORM)"
	@echo "  C Standard:   $(CSTD)"
	@echo "  Debug:        $(DEBUG)"
	@echo "  Build Dir:    $(BUILDDIR)"

# =============================================================================
# Dependency Checks
# =============================================================================

.PHONY: check-deps
check-deps:
	@$(PKG_CONFIG) --exists glib-2.0 || (echo "Missing: glib2-devel" && exit 1)
	@$(PKG_CONFIG) --exists gobject-2.0 || (echo "Missing: glib2-devel" && exit 1)
	@$(PKG_CONFIG) --exists gio-2.0 || (echo "Missing: glib2-devel" && exit 1)
	@$(PKG_CONFIG) --exists libdex-1 || (echo "Missing: libdex-devel" && exit 1)
	@$(PKG_CONFIG) --exists json-glib-1.0 || (echo "Missing: json-glib-devel" && exit 1)
	@$(PKG_CONFIG) --exists yaml-0.1 || (echo "Missing: libyaml-devel" && exit 1)
ifeq ($(BUILD_GIR),1)
	@which $(GIR_SCANNER) > /dev/null || (echo "Missing: gobject-introspection-devel" && exit 1)
endif
	$(call print_status,"All dependencies found")

# =============================================================================
# Version Info Target
# =============================================================================

.PHONY: version
version:
	@echo "Libregnum $(VERSION)"
	@echo "API Version: $(API_VERSION)"
	@echo "SO Version: $(SO_VERSION).$(SO_MINOR).$(SO_RELEASE)"
