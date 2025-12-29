# Makefile - Libregnum Root Build File
#
# Copyright 2025 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Main build orchestration for Libregnum - a GObject game engine.
#
# Usage:
#   make                     - Build the library
#   make DEBUG=1             - Build with debug symbols
#   make test                - Run unit tests
#   make install             - Install to PREFIX
#   make help                - Show all targets

# Set default goal before including other files
.DEFAULT_GOAL := all

# Include configuration
include config.mk
include rules.mk

# =============================================================================
# Source Files
# =============================================================================

# Public headers (for installation and GIR)
PUBLIC_HEADERS := \
	src/libregnum.h \
	src/lrg-version.h \
	src/lrg-types.h \
	src/lrg-enums.h \
	src/lrg-log.h \
	src/core/lrg-engine.h \
	src/core/lrg-registry.h \
	src/core/lrg-data-loader.h \
	src/core/lrg-asset-manager.h \
	src/graphics/lrg-drawable.h \
	src/graphics/lrg-window.h \
	src/graphics/lrg-grl-window.h \
	src/graphics/lrg-camera.h \
	src/graphics/lrg-camera2d.h \
	src/graphics/lrg-camera3d.h \
	src/graphics/lrg-camera-isometric.h \
	src/graphics/lrg-camera-topdown.h \
	src/graphics/lrg-camera-sideon.h \
	src/graphics/lrg-camera-firstperson.h \
	src/graphics/lrg-camera-thirdperson.h \
	src/graphics/lrg-renderer.h \
	src/ecs/lrg-component.h \
	src/ecs/lrg-game-object.h \
	src/ecs/lrg-world.h \
	src/ecs/components/lrg-sprite-component.h \
	src/ecs/components/lrg-collider-component.h \
	src/ecs/components/lrg-transform-component.h \
	src/ecs/components/lrg-animator-component.h \
	src/input/lrg-input.h \
	src/input/lrg-input-manager.h \
	src/input/lrg-input-keyboard.h \
	src/input/lrg-input-mouse.h \
	src/input/lrg-input-gamepad.h \
	src/input/lrg-input-mock.h \
	src/input/lrg-input-software.h \
	src/input/lrg-input-binding.h \
	src/input/lrg-input-action.h \
	src/input/lrg-input-map.h \
	src/shapes/lrg-shape.h \
	src/shapes/lrg-shape2d.h \
	src/shapes/lrg-shape3d.h \
	src/shapes/lrg-sphere3d.h \
	src/shapes/lrg-cube3d.h \
	src/shapes/lrg-line3d.h \
	src/shapes/lrg-text2d.h \
	src/shapes/lrg-rectangle2d.h \
	src/shapes/lrg-circle2d.h \
	src/shapes/lrg-cylinder3d.h \
	src/shapes/lrg-cone3d.h \
	src/shapes/lrg-plane3d.h \
	src/shapes/lrg-grid3d.h \
	src/shapes/lrg-circle3d.h \
	src/shapes/lrg-torus3d.h \
	src/shapes/lrg-icosphere3d.h \
	src/ui/lrg-ui-event.h \
	src/ui/lrg-widget.h \
	src/ui/lrg-container.h \
	src/ui/lrg-label.h \
	src/ui/lrg-button.h \
	src/ui/lrg-panel.h \
	src/ui/lrg-vbox.h \
	src/ui/lrg-hbox.h \
	src/ui/lrg-grid.h \
	src/ui/lrg-canvas.h \
	src/ui/lrg-checkbox.h \
	src/ui/lrg-progress-bar.h \
	src/ui/lrg-image.h \
	src/ui/lrg-slider.h \
	src/ui/lrg-text-input.h \
	src/ui/lrg-theme.h \
	src/tilemap/lrg-tileset.h \
	src/tilemap/lrg-tilemap-layer.h \
	src/tilemap/lrg-tilemap.h \
	src/save/lrg-saveable.h \
	src/save/lrg-save-context.h \
	src/save/lrg-save-game.h \
	src/save/lrg-save-manager.h \
	src/dialog/lrg-dialog-response.h \
	src/dialog/lrg-dialog-node.h \
	src/dialog/lrg-dialog-tree.h \
	src/dialog/lrg-dialog-runner.h \
	src/quest/lrg-quest-objective.h \
	src/quest/lrg-quest-def.h \
	src/quest/lrg-quest-instance.h \
	src/quest/lrg-quest-log.h \
	src/audio/lrg-sound-bank.h \
	src/audio/lrg-music-track.h \
	src/audio/lrg-audio-manager.h \
	src/inventory/lrg-item-def.h \
	src/inventory/lrg-item-stack.h \
	src/inventory/lrg-inventory.h \
	src/inventory/lrg-equipment.h \
	src/i18n/lrg-locale.h \
	src/i18n/lrg-localization.h \
	src/pathfinding/lrg-nav-cell.h \
	src/pathfinding/lrg-path.h \
	src/pathfinding/lrg-nav-grid.h \
	src/pathfinding/lrg-pathfinder.h \
	src/ai/lrg-blackboard.h \
	src/ai/lrg-bt-node.h \
	src/ai/lrg-bt-composite.h \
	src/ai/lrg-bt-decorator.h \
	src/ai/lrg-bt-leaf.h \
	src/ai/lrg-behavior-tree.h \
	src/physics/lrg-collision-info.h \
	src/physics/lrg-rigid-body.h \
	src/physics/lrg-physics-world.h \
	src/debug/lrg-profiler.h \
	src/debug/lrg-debug-console.h \
	src/debug/lrg-debug-overlay.h \
	src/debug/lrg-inspector.h \
	src/mod/lrg-mod-manifest.h \
	src/mod/lrg-mod.h \
	src/mod/lrg-mod-loader.h \
	src/mod/lrg-mod-manager.h \
	src/mod/lrg-modable.h \
	src/mod/lrg-providers.h \
	src/net/lrg-net-message.h \
	src/net/lrg-net-peer.h \
	src/net/lrg-net-server.h \
	src/net/lrg-net-client.h \
	src/world3d/lrg-bounding-box3d.h \
	src/world3d/lrg-spawn-point3d.h \
	src/world3d/lrg-trigger3d.h \
	src/world3d/lrg-octree.h \
	src/world3d/lrg-portal.h \
	src/world3d/lrg-sector.h \
	src/world3d/lrg-level3d.h \
	src/world3d/lrg-portal-system.h \
	src/scene/lrg-material3d.h \
	src/scene/lrg-scene-object.h \
	src/scene/lrg-scene-entity.h \
	src/scene/lrg-scene.h \
	src/scene/lrg-scene-serializer.h \
	src/scene/lrg-scene-serializer-yaml.h \
	src/scene/lrg-scene-serializer-blender.h \
	src/scene/lrg-mesh-data.h \
	src/scripting/lrg-scripting.h \
	src/settings/lrg-settings-group.h \
	src/settings/lrg-graphics-settings.h \
	src/settings/lrg-audio-settings.h \
	src/settings/lrg-settings.h \
	src/gamestate/lrg-game-state.h \
	src/gamestate/lrg-game-state-manager.h \
	src/crash/lrg-crash-dialog.h \
	src/crash/lrg-crash-dialog-terminal.h \
	src/crash/lrg-crash-reporter.h \
	src/accessibility/lrg-color-filter.h \
	src/accessibility/lrg-accessibility-settings.h \
	src/steam/lrg-steam-service.h \
	src/steam/lrg-steam-stub.h \
	src/steam/lrg-steam-client.h \
	src/steam/lrg-steam-achievements.h \
	src/steam/lrg-steam-cloud.h \
	src/steam/lrg-steam-stats.h \
	src/steam/lrg-steam-presence.h

# Source files
SOURCES := \
	src/lrg-enums.c \
	src/core/lrg-engine.c \
	src/core/lrg-registry.c \
	src/core/lrg-data-loader.c \
	src/core/lrg-asset-manager.c \
	src/graphics/lrg-drawable.c \
	src/graphics/lrg-window.c \
	src/graphics/lrg-grl-window.c \
	src/graphics/lrg-camera.c \
	src/graphics/lrg-camera2d.c \
	src/graphics/lrg-camera3d.c \
	src/graphics/lrg-camera-isometric.c \
	src/graphics/lrg-camera-topdown.c \
	src/graphics/lrg-camera-sideon.c \
	src/graphics/lrg-camera-firstperson.c \
	src/graphics/lrg-camera-thirdperson.c \
	src/graphics/lrg-renderer.c \
	src/ecs/lrg-component.c \
	src/ecs/lrg-game-object.c \
	src/ecs/lrg-world.c \
	src/ecs/components/lrg-sprite-component.c \
	src/ecs/components/lrg-collider-component.c \
	src/ecs/components/lrg-transform-component.c \
	src/ecs/components/lrg-animator-component.c \
	src/input/lrg-input.c \
	src/input/lrg-input-manager.c \
	src/input/lrg-input-keyboard.c \
	src/input/lrg-input-mouse.c \
	src/input/lrg-input-gamepad.c \
	src/input/lrg-input-mock.c \
	src/input/lrg-input-software.c \
	src/input/lrg-input-binding.c \
	src/input/lrg-input-action.c \
	src/input/lrg-input-map.c \
	src/shapes/lrg-shape.c \
	src/shapes/lrg-shape2d.c \
	src/shapes/lrg-shape3d.c \
	src/shapes/lrg-sphere3d.c \
	src/shapes/lrg-cube3d.c \
	src/shapes/lrg-line3d.c \
	src/shapes/lrg-text2d.c \
	src/shapes/lrg-rectangle2d.c \
	src/shapes/lrg-circle2d.c \
	src/shapes/lrg-cylinder3d.c \
	src/shapes/lrg-cone3d.c \
	src/shapes/lrg-plane3d.c \
	src/shapes/lrg-grid3d.c \
	src/shapes/lrg-circle3d.c \
	src/shapes/lrg-torus3d.c \
	src/shapes/lrg-icosphere3d.c \
	src/ui/lrg-ui-event.c \
	src/ui/lrg-widget.c \
	src/ui/lrg-container.c \
	src/ui/lrg-label.c \
	src/ui/lrg-button.c \
	src/ui/lrg-panel.c \
	src/ui/lrg-vbox.c \
	src/ui/lrg-hbox.c \
	src/ui/lrg-grid.c \
	src/ui/lrg-canvas.c \
	src/ui/lrg-checkbox.c \
	src/ui/lrg-progress-bar.c \
	src/ui/lrg-image.c \
	src/ui/lrg-slider.c \
	src/ui/lrg-text-input.c \
	src/ui/lrg-theme.c \
	src/tilemap/lrg-tileset.c \
	src/tilemap/lrg-tilemap-layer.c \
	src/tilemap/lrg-tilemap.c \
	src/save/lrg-saveable.c \
	src/save/lrg-save-context.c \
	src/save/lrg-save-game.c \
	src/save/lrg-save-manager.c \
	src/dialog/lrg-dialog-response.c \
	src/dialog/lrg-dialog-node.c \
	src/dialog/lrg-dialog-tree.c \
	src/dialog/lrg-dialog-runner.c \
	src/quest/lrg-quest-objective.c \
	src/quest/lrg-quest-def.c \
	src/quest/lrg-quest-instance.c \
	src/quest/lrg-quest-log.c \
	src/audio/lrg-sound-bank.c \
	src/audio/lrg-music-track.c \
	src/audio/lrg-audio-manager.c \
	src/inventory/lrg-item-def.c \
	src/inventory/lrg-item-stack.c \
	src/inventory/lrg-inventory.c \
	src/inventory/lrg-equipment.c \
	src/i18n/lrg-locale.c \
	src/i18n/lrg-localization.c \
	src/pathfinding/lrg-nav-cell.c \
	src/pathfinding/lrg-path.c \
	src/pathfinding/lrg-nav-grid.c \
	src/pathfinding/lrg-pathfinder.c \
	src/ai/lrg-blackboard.c \
	src/ai/lrg-bt-node.c \
	src/ai/lrg-bt-composite.c \
	src/ai/lrg-bt-decorator.c \
	src/ai/lrg-bt-leaf.c \
	src/ai/lrg-behavior-tree.c \
	src/physics/lrg-collision-info.c \
	src/physics/lrg-rigid-body.c \
	src/physics/lrg-physics-world.c \
	src/debug/lrg-profiler.c \
	src/debug/lrg-debug-console.c \
	src/debug/lrg-debug-overlay.c \
	src/debug/lrg-inspector.c \
	src/mod/lrg-mod-manifest.c \
	src/mod/lrg-mod.c \
	src/mod/lrg-mod-loader.c \
	src/mod/lrg-mod-manager.c \
	src/mod/lrg-modable.c \
	src/mod/lrg-providers.c \
	src/net/lrg-net-message.c \
	src/net/lrg-net-peer.c \
	src/net/lrg-net-server.c \
	src/net/lrg-net-client.c \
	src/world3d/lrg-bounding-box3d.c \
	src/world3d/lrg-spawn-point3d.c \
	src/world3d/lrg-trigger3d.c \
	src/world3d/lrg-octree.c \
	src/world3d/lrg-portal.c \
	src/world3d/lrg-sector.c \
	src/world3d/lrg-level3d.c \
	src/world3d/lrg-portal-system.c \
	src/scene/lrg-material3d.c \
	src/scene/lrg-scene-object.c \
	src/scene/lrg-scene-entity.c \
	src/scene/lrg-scene.c \
	src/scene/lrg-scene-serializer.c \
	src/scene/lrg-scene-serializer-yaml.c \
	src/scene/lrg-scene-serializer-blender.c \
	src/scene/lrg-mesh-data.c \
	src/scripting/lrg-scripting.c \
	src/scripting/lrg-scriptable.c \
	src/settings/lrg-settings-group.c \
	src/settings/lrg-graphics-settings.c \
	src/settings/lrg-audio-settings.c \
	src/settings/lrg-settings.c \
	src/gamestate/lrg-game-state.c \
	src/gamestate/lrg-game-state-manager.c \
	src/crash/lrg-crash-dialog.c \
	src/crash/lrg-crash-dialog-terminal.c \
	src/crash/lrg-crash-reporter.c \
	src/accessibility/lrg-color-filter.c \
	src/accessibility/lrg-accessibility-settings.c \
	src/steam/lrg-steam-service.c \
	src/steam/lrg-steam-stub.c \
	src/steam/lrg-steam-client.c \
	src/steam/lrg-steam-achievements.c \
	src/steam/lrg-steam-cloud.c \
	src/steam/lrg-steam-stats.c \
	src/steam/lrg-steam-presence.c

# Conditional scripting backends
ifeq ($(HAS_LUAJIT),1)
PUBLIC_HEADERS += \
	src/scripting/lrg-scripting-lua.h
SOURCES += \
	src/scripting/lrg-scripting-lua.c \
	src/scripting/lrg-lua-bridge.c \
	src/scripting/lrg-lua-api.c
endif

ifeq ($(HAS_PYTHON),1)
PUBLIC_HEADERS += \
	src/scripting/lrg-scripting-python.h \
	src/scripting/lrg-scripting-pygobject.h
SOURCES += \
	src/scripting/lrg-scripting-python.c \
	src/scripting/lrg-python-bridge.c \
	src/scripting/lrg-python-api.c \
	src/scripting/lrg-scripting-pygobject.c
endif

ifeq ($(HAS_GI),1)
PUBLIC_HEADERS += \
	src/scripting/lrg-scripting-gi.h
SOURCES += \
	src/scripting/lrg-scripting-gi.c
endif

ifeq ($(HAS_GJS),1)
PUBLIC_HEADERS += \
	src/scripting/lrg-scripting-gjs.h
SOURCES += \
	src/scripting/lrg-scripting-gjs.c
endif

# Object files
OBJECTS := $(patsubst %.c,$(OBJDIR)/%.o,$(SOURCES))
DEPENDS := $(patsubst %.c,$(OBJDIR)/%.d,$(SOURCES))

# =============================================================================
# Generated Headers (must be built first)
# =============================================================================

GENERATED_HEADERS := src/lrg-version.h src/config.h

.PHONY: generate
generate: $(GENERATED_HEADERS)

# =============================================================================
# Platform Marker (auto-clean on platform switch)
# =============================================================================

PLATFORM_MARKER := $(BUILDDIR)/.platform-marker

.PHONY: platform-check
platform-check:
	@$(MKDIR_P) $(BUILDDIR)
	@if [ -f "$(PLATFORM_MARKER)" ] && \
	   [ "$$(cat $(PLATFORM_MARKER))" != "$(TARGET_PLATFORM)" ]; then \
		echo "Platform changed from $$(cat $(PLATFORM_MARKER)) to $(TARGET_PLATFORM), cleaning..."; \
		$(RMDIR) $(OBJDIR); \
		$(RMDIR) $(LIBOUTDIR); \
	fi
	@echo "$(TARGET_PLATFORM)" > $(PLATFORM_MARKER)

# =============================================================================
# Default Target
# =============================================================================

# Use recursive make to ensure generate completes before lib starts
all: check-deps platform-check deps generate
	@$(MAKE) --no-print-directory _lib
ifeq ($(BUILD_GIR),1)
	@$(MAKE) --no-print-directory gir
endif

# Internal target for actual library build (called after generate)
.PHONY: _lib
_lib: lib-static lib-shared

# =============================================================================
# Dependencies (Submodules)
# =============================================================================

.PHONY: deps deps-graylib deps-yamlglib

deps: deps-graylib deps-yamlglib

# Pass cross-compilation settings to dependencies
ifeq ($(TARGET_PLATFORM),windows)
    GRAYLIB_LIB := $(GRAYLIB_DIR)/build/lib/graylib.dll
    YAMLGLIB_LIB := $(YAMLGLIB_DIR)/build/yaml-glib.dll
    DEP_BUILD_FLAGS := WINDOWS=1
else
    GRAYLIB_LIB := $(GRAYLIB_DIR)/build/lib/libgraylib.so
    YAMLGLIB_LIB := $(YAMLGLIB_DIR)/build/libyaml-glib.so
    DEP_BUILD_FLAGS :=
endif

deps-graylib:
	@if [ ! -f "$(GRAYLIB_LIB)" ]; then \
		$(call print_status,"Building graylib ($(TARGET_PLATFORM))..."); \
		$(MAKE) -C $(GRAYLIB_DIR) $(DEP_BUILD_FLAGS); \
	fi

deps-yamlglib:
	@if [ ! -f "$(YAMLGLIB_LIB)" ]; then \
		$(call print_status,"Building yaml-glib ($(TARGET_PLATFORM))..."); \
		$(MAKE) -C $(YAMLGLIB_DIR) $(DEP_BUILD_FLAGS); \
	fi

deps-clean:
	$(MAKE) -C $(GRAYLIB_DIR) clean
	$(MAKE) -C $(YAMLGLIB_DIR) clean

# =============================================================================
# Library Targets
# =============================================================================

# Note: 'lib' target uses recursive make to ensure generate runs first
lib: generate
	@$(MAKE) --no-print-directory _lib

lib-static: $(LIBOUTDIR)/$(LIB_STATIC)

lib-shared: $(LIBOUTDIR)/$(LIB_SHARED)

# Static library
$(LIBOUTDIR)/$(LIB_STATIC): $(OBJECTS) | $(LIBOUTDIR)
	$(call print_archive,$(LIB_STATIC))
	@$(AR) rcs $@ $(OBJECTS)
	@$(RANLIB) $@

# Shared library (platform-specific build)
ifeq ($(TARGET_PLATFORM),windows)
$(LIBOUTDIR)/$(LIB_SHARED): $(OBJECTS) | $(LIBOUTDIR)
	$(call print_link,$(LIB_SHARED))
	@$(CC) $(LIB_LDFLAGS) -o $(LIBOUTDIR)/$(LIB_SHARED) $(OBJECTS) $(ALL_LIBS)
	@echo "Built: $(LIB_SHARED) and $(LIB_IMPORT)"
else
$(LIBOUTDIR)/$(LIB_SHARED): $(OBJECTS) | $(LIBOUTDIR)
	$(call print_link,$(LIB_SHARED))
	@$(CC) $(LIB_LDFLAGS) -o $(LIBOUTDIR)/$(LIB_SHARED_VERSION) $(OBJECTS) $(ALL_LIBS)
	@cd $(LIBOUTDIR) && ln -sf $(LIB_SHARED_VERSION) $(LIB_SHARED_SONAME)
	@cd $(LIBOUTDIR) && ln -sf $(LIB_SHARED_SONAME) $(LIB_SHARED)
endif

# =============================================================================
# Generated Files
# =============================================================================

# Generate version header from template
src/lrg-version.h: src/lrg-version.h.in config.mk
	$(call print_status,"Generating lrg-version.h")
	@$(SED) -e 's/@VERSION_MAJOR@/$(VERSION_MAJOR)/g' \
	        -e 's/@VERSION_MINOR@/$(VERSION_MINOR)/g' \
	        -e 's/@VERSION_MICRO@/$(VERSION_MICRO)/g' \
	        -e 's/@VERSION@/$(VERSION)/g' \
	        -e 's/@API_VERSION@/$(API_VERSION)/g' \
	        $< > $@

# Generate config header from template
src/config.h: src/config.h.in config.mk
	$(call print_status,"Generating config.h")
	@$(SED) -e 's/@VERSION_MAJOR@/$(VERSION_MAJOR)/g' \
	        -e 's/@VERSION_MINOR@/$(VERSION_MINOR)/g' \
	        -e 's/@VERSION_MICRO@/$(VERSION_MICRO)/g' \
	        -e 's/@VERSION@/$(VERSION)/g' \
	        $< > $@

# Generate pkg-config file
$(BUILDDIR)/$(PC_FILE): libregnum.pc.in config.mk | $(BUILDDIR)
	$(call print_status,"Generating pkg-config file")
	@$(SED) -e 's|@PREFIX@|$(PREFIX)|g' \
	        -e 's|@EXEC_PREFIX@|$(EXEC_PREFIX)|g' \
	        -e 's|@LIBDIR@|$(LIBDIR)|g' \
	        -e 's|@INCLUDEDIR@|$(INCLUDEDIR)|g' \
	        -e 's|@VERSION@|$(VERSION)|g' \
	        -e 's|@API_VERSION@|$(API_VERSION)|g' \
	        $< > $@

# =============================================================================
# GObject Introspection
# =============================================================================

gir: $(GIROUTDIR)/$(GIR_NAME) $(GIROUTDIR)/$(TYPELIB_NAME)

$(GIROUTDIR)/$(GIR_NAME): $(LIBOUTDIR)/$(LIB_SHARED) $(PUBLIC_HEADERS) $(SOURCES) | $(GIROUTDIR)
	$(call print_gir,$(GIR_NAME))
	@$(GIR_SCANNER) $(GIR_SCANNER_FLAGS) \
		--library=$(LIB_NAME) \
		--library-path=$(LIBOUTDIR) \
		--library-path=$(GRAYLIB_DIR)/build/lib \
		--library-path=$(YAMLGLIB_DIR)/build \
		--output=$@ \
		$(PUBLIC_HEADERS) $(SOURCES)

$(GIROUTDIR)/$(TYPELIB_NAME): $(GIROUTDIR)/$(GIR_NAME)
	$(call print_gir,$(TYPELIB_NAME))
	@$(GIR_COMPILER) $(GIR_COMPILER_FLAGS) \
		--output=$@ $<

# =============================================================================
# Tests
# =============================================================================

test tests check: lib
ifeq ($(TARGET_PLATFORM),windows)
	$(call print_warning,"Cross-compiled tests cannot be run on Linux.")
	$(call print_info,"Use Wine or copy to Windows to run tests.")
	$(call print_status,"Building tests only...")
	@$(MAKE) -C tests
else ifeq ($(BUILD_TESTS),1)
	$(call print_status,"Building and running tests...")
	@$(MAKE) -C tests run
else
	$(call print_warning,"Tests disabled (BUILD_TESTS=0)")
endif

# =============================================================================
# Examples
# =============================================================================

examples: lib
ifeq ($(BUILD_EXAMPLES),1)
	$(call print_status,"Building examples...")
	@$(MAKE) -C examples
else
	$(call print_warning,"Examples disabled (BUILD_EXAMPLES=0)")
endif

# =============================================================================
# Documentation
# =============================================================================

docs:
ifeq ($(BUILD_DOCS),1)
	$(call print_status,"Building documentation...")
	@$(GI_DOCGEN) generate --config docs/libregnum.toml $(GIROUTDIR)/$(GIR_NAME)
else
	$(call print_warning,"Documentation build disabled (BUILD_DOCS=0)")
endif

# =============================================================================
# Installation
# =============================================================================

install: lib $(BUILDDIR)/$(PC_FILE)
ifeq ($(TARGET_PLATFORM),windows)
	$(call print_warning,"Install target is for native Linux builds only.")
	$(call print_info,"For Windows, copy the DLL and headers manually:")
	$(call print_info,"  DLL: $(LIBOUTDIR)/$(LIB_SHARED)")
	$(call print_info,"  Import lib: $(LIBOUTDIR)/$(LIB_IMPORT)")
	$(call print_info,"  Headers: src/*.h src/*/*.h")
	@exit 0
else
	$(call print_status,"Installing to $(PREFIX)...")
	# Create directories
	@$(MKDIR_P) $(DESTDIR)$(LIBDIR)
	@$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/libregnum
	@$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/libregnum/core
	@$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/libregnum/graphics
	@$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/libregnum/ecs
	@$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/libregnum/ecs/components
	@$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/libregnum/input
	@$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/libregnum/shapes
	@$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/libregnum/ui
	@$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/libregnum/inventory
	@$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/libregnum/debug
	@$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/libregnum/net
	@$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/libregnum/world3d
	@$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/libregnum/scene
	@$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/libregnum/scripting
	@$(MKDIR_P) $(DESTDIR)$(PKGCONFIGDIR)
ifeq ($(BUILD_GIR),1)
	@$(MKDIR_P) $(DESTDIR)$(GIRDIR)
	@$(MKDIR_P) $(DESTDIR)$(TYPELIBDIR)
endif
	# Install libraries
ifeq ($(BUILD_STATIC),1)
	$(INSTALL_DATA) $(LIBOUTDIR)/$(LIB_STATIC) $(DESTDIR)$(LIBDIR)/
endif
ifeq ($(BUILD_SHARED),1)
	$(INSTALL_PROGRAM) $(LIBOUTDIR)/$(LIB_SHARED_VERSION) $(DESTDIR)$(LIBDIR)/
	cd $(DESTDIR)$(LIBDIR) && ln -sf $(LIB_SHARED_VERSION) $(LIB_SHARED_SONAME)
	cd $(DESTDIR)$(LIBDIR) && ln -sf $(LIB_SHARED_SONAME) $(LIB_SHARED)
endif
	# Install headers
	$(INSTALL_DATA) src/libregnum.h $(DESTDIR)$(INCLUDEDIR)/libregnum/
	$(INSTALL_DATA) src/lrg-version.h $(DESTDIR)$(INCLUDEDIR)/libregnum/
	$(INSTALL_DATA) src/lrg-types.h $(DESTDIR)$(INCLUDEDIR)/libregnum/
	$(INSTALL_DATA) src/lrg-enums.h $(DESTDIR)$(INCLUDEDIR)/libregnum/
	$(INSTALL_DATA) src/lrg-log.h $(DESTDIR)$(INCLUDEDIR)/libregnum/
	$(INSTALL_DATA) src/core/lrg-engine.h $(DESTDIR)$(INCLUDEDIR)/libregnum/core/
	$(INSTALL_DATA) src/core/lrg-registry.h $(DESTDIR)$(INCLUDEDIR)/libregnum/core/
	$(INSTALL_DATA) src/core/lrg-data-loader.h $(DESTDIR)$(INCLUDEDIR)/libregnum/core/
	$(INSTALL_DATA) src/core/lrg-asset-manager.h $(DESTDIR)$(INCLUDEDIR)/libregnum/core/
	$(INSTALL_DATA) src/graphics/lrg-drawable.h $(DESTDIR)$(INCLUDEDIR)/libregnum/graphics/
	$(INSTALL_DATA) src/graphics/lrg-window.h $(DESTDIR)$(INCLUDEDIR)/libregnum/graphics/
	$(INSTALL_DATA) src/graphics/lrg-grl-window.h $(DESTDIR)$(INCLUDEDIR)/libregnum/graphics/
	$(INSTALL_DATA) src/graphics/lrg-camera.h $(DESTDIR)$(INCLUDEDIR)/libregnum/graphics/
	$(INSTALL_DATA) src/graphics/lrg-camera2d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/graphics/
	$(INSTALL_DATA) src/graphics/lrg-camera3d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/graphics/
	$(INSTALL_DATA) src/graphics/lrg-camera-isometric.h $(DESTDIR)$(INCLUDEDIR)/libregnum/graphics/
	$(INSTALL_DATA) src/graphics/lrg-camera-topdown.h $(DESTDIR)$(INCLUDEDIR)/libregnum/graphics/
	$(INSTALL_DATA) src/graphics/lrg-camera-sideon.h $(DESTDIR)$(INCLUDEDIR)/libregnum/graphics/
	$(INSTALL_DATA) src/graphics/lrg-camera-firstperson.h $(DESTDIR)$(INCLUDEDIR)/libregnum/graphics/
	$(INSTALL_DATA) src/graphics/lrg-camera-thirdperson.h $(DESTDIR)$(INCLUDEDIR)/libregnum/graphics/
	$(INSTALL_DATA) src/graphics/lrg-renderer.h $(DESTDIR)$(INCLUDEDIR)/libregnum/graphics/
	$(INSTALL_DATA) src/ecs/lrg-component.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ecs/
	$(INSTALL_DATA) src/ecs/lrg-game-object.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ecs/
	$(INSTALL_DATA) src/ecs/lrg-world.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ecs/
	$(INSTALL_DATA) src/ecs/components/lrg-sprite-component.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ecs/components/
	$(INSTALL_DATA) src/ecs/components/lrg-collider-component.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ecs/components/
	$(INSTALL_DATA) src/ecs/components/lrg-transform-component.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ecs/components/
	$(INSTALL_DATA) src/ecs/components/lrg-animator-component.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ecs/components/
	$(INSTALL_DATA) src/input/lrg-input.h $(DESTDIR)$(INCLUDEDIR)/libregnum/input/
	$(INSTALL_DATA) src/input/lrg-input-manager.h $(DESTDIR)$(INCLUDEDIR)/libregnum/input/
	$(INSTALL_DATA) src/input/lrg-input-keyboard.h $(DESTDIR)$(INCLUDEDIR)/libregnum/input/
	$(INSTALL_DATA) src/input/lrg-input-mouse.h $(DESTDIR)$(INCLUDEDIR)/libregnum/input/
	$(INSTALL_DATA) src/input/lrg-input-gamepad.h $(DESTDIR)$(INCLUDEDIR)/libregnum/input/
	$(INSTALL_DATA) src/input/lrg-input-mock.h $(DESTDIR)$(INCLUDEDIR)/libregnum/input/
	$(INSTALL_DATA) src/input/lrg-input-software.h $(DESTDIR)$(INCLUDEDIR)/libregnum/input/
	$(INSTALL_DATA) src/input/lrg-input-binding.h $(DESTDIR)$(INCLUDEDIR)/libregnum/input/
	$(INSTALL_DATA) src/input/lrg-input-action.h $(DESTDIR)$(INCLUDEDIR)/libregnum/input/
	$(INSTALL_DATA) src/input/lrg-input-map.h $(DESTDIR)$(INCLUDEDIR)/libregnum/input/
	$(INSTALL_DATA) src/shapes/lrg-shape.h $(DESTDIR)$(INCLUDEDIR)/libregnum/shapes/
	$(INSTALL_DATA) src/shapes/lrg-shape2d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/shapes/
	$(INSTALL_DATA) src/shapes/lrg-shape3d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/shapes/
	$(INSTALL_DATA) src/shapes/lrg-sphere3d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/shapes/
	$(INSTALL_DATA) src/shapes/lrg-cube3d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/shapes/
	$(INSTALL_DATA) src/shapes/lrg-line3d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/shapes/
	$(INSTALL_DATA) src/shapes/lrg-text2d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/shapes/
	$(INSTALL_DATA) src/shapes/lrg-rectangle2d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/shapes/
	$(INSTALL_DATA) src/shapes/lrg-circle2d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/shapes/
	$(INSTALL_DATA) src/ui/lrg-ui-event.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-widget.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-container.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-label.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-button.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-panel.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-vbox.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-hbox.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-grid.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-canvas.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-checkbox.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-progress-bar.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-image.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-slider.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-text-input.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/ui/lrg-theme.h $(DESTDIR)$(INCLUDEDIR)/libregnum/ui/
	$(INSTALL_DATA) src/inventory/lrg-item-def.h $(DESTDIR)$(INCLUDEDIR)/libregnum/inventory/
	$(INSTALL_DATA) src/inventory/lrg-item-stack.h $(DESTDIR)$(INCLUDEDIR)/libregnum/inventory/
	$(INSTALL_DATA) src/inventory/lrg-inventory.h $(DESTDIR)$(INCLUDEDIR)/libregnum/inventory/
	$(INSTALL_DATA) src/inventory/lrg-equipment.h $(DESTDIR)$(INCLUDEDIR)/libregnum/inventory/
	$(INSTALL_DATA) src/debug/lrg-profiler.h $(DESTDIR)$(INCLUDEDIR)/libregnum/debug/
	$(INSTALL_DATA) src/debug/lrg-debug-console.h $(DESTDIR)$(INCLUDEDIR)/libregnum/debug/
	$(INSTALL_DATA) src/debug/lrg-debug-overlay.h $(DESTDIR)$(INCLUDEDIR)/libregnum/debug/
	$(INSTALL_DATA) src/debug/lrg-inspector.h $(DESTDIR)$(INCLUDEDIR)/libregnum/debug/
	$(INSTALL_DATA) src/net/lrg-net-message.h $(DESTDIR)$(INCLUDEDIR)/libregnum/net/
	$(INSTALL_DATA) src/net/lrg-net-peer.h $(DESTDIR)$(INCLUDEDIR)/libregnum/net/
	$(INSTALL_DATA) src/net/lrg-net-server.h $(DESTDIR)$(INCLUDEDIR)/libregnum/net/
	$(INSTALL_DATA) src/net/lrg-net-client.h $(DESTDIR)$(INCLUDEDIR)/libregnum/net/
	$(INSTALL_DATA) src/world3d/lrg-bounding-box3d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/world3d/
	$(INSTALL_DATA) src/world3d/lrg-spawn-point3d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/world3d/
	$(INSTALL_DATA) src/world3d/lrg-trigger3d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/world3d/
	$(INSTALL_DATA) src/world3d/lrg-octree.h $(DESTDIR)$(INCLUDEDIR)/libregnum/world3d/
	$(INSTALL_DATA) src/world3d/lrg-portal.h $(DESTDIR)$(INCLUDEDIR)/libregnum/world3d/
	$(INSTALL_DATA) src/world3d/lrg-sector.h $(DESTDIR)$(INCLUDEDIR)/libregnum/world3d/
	$(INSTALL_DATA) src/world3d/lrg-level3d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/world3d/
	$(INSTALL_DATA) src/world3d/lrg-portal-system.h $(DESTDIR)$(INCLUDEDIR)/libregnum/world3d/
	$(INSTALL_DATA) src/scene/lrg-material3d.h $(DESTDIR)$(INCLUDEDIR)/libregnum/scene/
	$(INSTALL_DATA) src/scene/lrg-scene-object.h $(DESTDIR)$(INCLUDEDIR)/libregnum/scene/
	$(INSTALL_DATA) src/scene/lrg-scene-entity.h $(DESTDIR)$(INCLUDEDIR)/libregnum/scene/
	$(INSTALL_DATA) src/scene/lrg-scene.h $(DESTDIR)$(INCLUDEDIR)/libregnum/scene/
	$(INSTALL_DATA) src/scene/lrg-scene-serializer.h $(DESTDIR)$(INCLUDEDIR)/libregnum/scene/
	$(INSTALL_DATA) src/scene/lrg-scene-serializer-yaml.h $(DESTDIR)$(INCLUDEDIR)/libregnum/scene/
	$(INSTALL_DATA) src/scene/lrg-scene-serializer-blender.h $(DESTDIR)$(INCLUDEDIR)/libregnum/scene/
	$(INSTALL_DATA) src/scene/lrg-mesh-data.h $(DESTDIR)$(INCLUDEDIR)/libregnum/scene/
	$(INSTALL_DATA) src/scripting/lrg-scripting.h $(DESTDIR)$(INCLUDEDIR)/libregnum/scripting/
	$(INSTALL_DATA) src/scripting/lrg-scripting-lua.h $(DESTDIR)$(INCLUDEDIR)/libregnum/scripting/
	$(INSTALL_DATA) src/scripting/lrg-scripting-gi.h $(DESTDIR)$(INCLUDEDIR)/libregnum/scripting/
	$(INSTALL_DATA) src/scripting/lrg-scripting-python.h $(DESTDIR)$(INCLUDEDIR)/libregnum/scripting/
	$(INSTALL_DATA) src/scripting/lrg-scripting-pygobject.h $(DESTDIR)$(INCLUDEDIR)/libregnum/scripting/
	$(INSTALL_DATA) src/scripting/lrg-scripting-gjs.h $(DESTDIR)$(INCLUDEDIR)/libregnum/scripting/
	# Install pkg-config
	$(INSTALL_DATA) $(BUILDDIR)/$(PC_FILE) $(DESTDIR)$(PKGCONFIGDIR)/
	# Install GIR
ifeq ($(BUILD_GIR),1)
	$(INSTALL_DATA) $(GIROUTDIR)/$(GIR_NAME) $(DESTDIR)$(GIRDIR)/
	$(INSTALL_DATA) $(GIROUTDIR)/$(TYPELIB_NAME) $(DESTDIR)$(TYPELIBDIR)/
endif
	$(call print_status,"Installation complete!")
endif
# End of ifeq ($(TARGET_PLATFORM),windows) else block

uninstall:
	$(call print_status,"Uninstalling from $(PREFIX)...")
	$(RM) $(DESTDIR)$(LIBDIR)/$(LIB_STATIC)
	$(RM) $(DESTDIR)$(LIBDIR)/$(LIB_SHARED)
	$(RM) $(DESTDIR)$(LIBDIR)/$(LIB_SHARED_SONAME)
	$(RM) $(DESTDIR)$(LIBDIR)/$(LIB_SHARED_VERSION)
	$(RMDIR) $(DESTDIR)$(INCLUDEDIR)/libregnum
	$(RM) $(DESTDIR)$(PKGCONFIGDIR)/$(PC_FILE)
ifeq ($(BUILD_GIR),1)
	$(RM) $(DESTDIR)$(GIRDIR)/$(GIR_NAME)
	$(RM) $(DESTDIR)$(TYPELIBDIR)/$(TYPELIB_NAME)
endif

# =============================================================================
# Cleanup
# =============================================================================

clean:
	$(call print_status,"Cleaning build artifacts...")
	$(RMDIR) build
	$(RM) src/lrg-version.h
	$(RM) src/config.h

distclean: clean deps-clean
	$(call print_status,"Cleaning all generated files...")

# =============================================================================
# Debug Target (shorthand for DEBUG=1)
# =============================================================================

debug-build:
	$(MAKE) DEBUG=1 all

# =============================================================================
# Dependencies
# =============================================================================

# Source files depend on generated headers
$(OBJECTS): src/lrg-version.h src/config.h

# =============================================================================
# Object File Rules
# =============================================================================

# Pattern rule for source compilation
$(OBJDIR)/src/%.o: src/%.c | $(OBJDIR)
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# Explicit rules for each source file (for proper dependency tracking)

# Core module
$(OBJDIR)/src/lrg-enums.o: src/lrg-enums.c src/lrg-enums.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/core/lrg-engine.o: src/core/lrg-engine.c src/core/lrg-engine.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/core/lrg-registry.o: src/core/lrg-registry.c src/core/lrg-registry.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/core/lrg-data-loader.o: src/core/lrg-data-loader.c src/core/lrg-data-loader.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/core/lrg-asset-manager.o: src/core/lrg-asset-manager.c src/core/lrg-asset-manager.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# Graphics module
$(OBJDIR)/src/graphics/lrg-drawable.o: src/graphics/lrg-drawable.c src/graphics/lrg-drawable.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/graphics/lrg-window.o: src/graphics/lrg-window.c src/graphics/lrg-window.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/graphics/lrg-grl-window.o: src/graphics/lrg-grl-window.c src/graphics/lrg-grl-window.h src/graphics/lrg-window.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/graphics/lrg-camera.o: src/graphics/lrg-camera.c src/graphics/lrg-camera.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/graphics/lrg-camera2d.o: src/graphics/lrg-camera2d.c src/graphics/lrg-camera2d.h src/graphics/lrg-camera.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/graphics/lrg-camera3d.o: src/graphics/lrg-camera3d.c src/graphics/lrg-camera3d.h src/graphics/lrg-camera.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/graphics/lrg-camera-isometric.o: src/graphics/lrg-camera-isometric.c src/graphics/lrg-camera-isometric.h src/graphics/lrg-camera3d.h src/graphics/lrg-camera.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/graphics/lrg-camera-topdown.o: src/graphics/lrg-camera-topdown.c src/graphics/lrg-camera-topdown.h src/graphics/lrg-camera2d.h src/graphics/lrg-camera.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/graphics/lrg-camera-sideon.o: src/graphics/lrg-camera-sideon.c src/graphics/lrg-camera-sideon.h src/graphics/lrg-camera2d.h src/graphics/lrg-camera.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/graphics/lrg-camera-firstperson.o: src/graphics/lrg-camera-firstperson.c src/graphics/lrg-camera-firstperson.h src/graphics/lrg-camera3d.h src/graphics/lrg-camera.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/graphics/lrg-camera-thirdperson.o: src/graphics/lrg-camera-thirdperson.c src/graphics/lrg-camera-thirdperson.h src/graphics/lrg-camera3d.h src/graphics/lrg-camera.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/graphics/lrg-renderer.o: src/graphics/lrg-renderer.c src/graphics/lrg-renderer.h src/graphics/lrg-window.h src/graphics/lrg-camera.h src/graphics/lrg-drawable.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# ECS module
$(OBJDIR)/src/ecs/lrg-component.o: src/ecs/lrg-component.c src/ecs/lrg-component.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ecs/lrg-game-object.o: src/ecs/lrg-game-object.c src/ecs/lrg-game-object.h src/ecs/lrg-component.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ecs/lrg-world.o: src/ecs/lrg-world.c src/ecs/lrg-world.h src/ecs/lrg-game-object.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ecs/components/lrg-sprite-component.o: src/ecs/components/lrg-sprite-component.c src/ecs/components/lrg-sprite-component.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ecs/components/lrg-collider-component.o: src/ecs/components/lrg-collider-component.c src/ecs/components/lrg-collider-component.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ecs/components/lrg-transform-component.o: src/ecs/components/lrg-transform-component.c src/ecs/components/lrg-transform-component.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ecs/components/lrg-animator-component.o: src/ecs/components/lrg-animator-component.c src/ecs/components/lrg-animator-component.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# Input module
$(OBJDIR)/src/input/lrg-input.o: src/input/lrg-input.c src/input/lrg-input.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/input/lrg-input-manager.o: src/input/lrg-input-manager.c src/input/lrg-input-manager.h src/input/lrg-input.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/input/lrg-input-keyboard.o: src/input/lrg-input-keyboard.c src/input/lrg-input-keyboard.h src/input/lrg-input.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/input/lrg-input-mouse.o: src/input/lrg-input-mouse.c src/input/lrg-input-mouse.h src/input/lrg-input.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/input/lrg-input-gamepad.o: src/input/lrg-input-gamepad.c src/input/lrg-input-gamepad.h src/input/lrg-input.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/input/lrg-input-mock.o: src/input/lrg-input-mock.c src/input/lrg-input-mock.h src/input/lrg-input.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/input/lrg-input-software.o: src/input/lrg-input-software.c src/input/lrg-input-software.h src/input/lrg-input.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/input/lrg-input-binding.o: src/input/lrg-input-binding.c src/input/lrg-input-binding.h src/input/lrg-input-manager.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/input/lrg-input-action.o: src/input/lrg-input-action.c src/input/lrg-input-action.h src/input/lrg-input-binding.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/input/lrg-input-map.o: src/input/lrg-input-map.c src/input/lrg-input-map.h src/input/lrg-input-action.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# Shapes module
$(OBJDIR)/src/shapes/lrg-shape.o: src/shapes/lrg-shape.c src/shapes/lrg-shape.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-shape2d.o: src/shapes/lrg-shape2d.c src/shapes/lrg-shape2d.h src/shapes/lrg-shape.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-shape3d.o: src/shapes/lrg-shape3d.c src/shapes/lrg-shape3d.h src/shapes/lrg-shape.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-sphere3d.o: src/shapes/lrg-sphere3d.c src/shapes/lrg-sphere3d.h src/shapes/lrg-shape3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-cube3d.o: src/shapes/lrg-cube3d.c src/shapes/lrg-cube3d.h src/shapes/lrg-shape3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-line3d.o: src/shapes/lrg-line3d.c src/shapes/lrg-line3d.h src/shapes/lrg-shape3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-text2d.o: src/shapes/lrg-text2d.c src/shapes/lrg-text2d.h src/shapes/lrg-shape2d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-rectangle2d.o: src/shapes/lrg-rectangle2d.c src/shapes/lrg-rectangle2d.h src/shapes/lrg-shape2d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-circle2d.o: src/shapes/lrg-circle2d.c src/shapes/lrg-circle2d.h src/shapes/lrg-shape2d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-cylinder3d.o: src/shapes/lrg-cylinder3d.c src/shapes/lrg-cylinder3d.h src/shapes/lrg-shape3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-cone3d.o: src/shapes/lrg-cone3d.c src/shapes/lrg-cone3d.h src/shapes/lrg-shape3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-plane3d.o: src/shapes/lrg-plane3d.c src/shapes/lrg-plane3d.h src/shapes/lrg-shape3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-grid3d.o: src/shapes/lrg-grid3d.c src/shapes/lrg-grid3d.h src/shapes/lrg-shape3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-circle3d.o: src/shapes/lrg-circle3d.c src/shapes/lrg-circle3d.h src/shapes/lrg-shape3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-torus3d.o: src/shapes/lrg-torus3d.c src/shapes/lrg-torus3d.h src/shapes/lrg-shape3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/shapes/lrg-icosphere3d.o: src/shapes/lrg-icosphere3d.c src/shapes/lrg-icosphere3d.h src/shapes/lrg-shape3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# UI module
$(OBJDIR)/src/ui/lrg-ui-event.o: src/ui/lrg-ui-event.c src/ui/lrg-ui-event.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-widget.o: src/ui/lrg-widget.c src/ui/lrg-widget.h src/ui/lrg-widget-private.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-container.o: src/ui/lrg-container.c src/ui/lrg-container.h src/ui/lrg-widget.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-label.o: src/ui/lrg-label.c src/ui/lrg-label.h src/ui/lrg-widget.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-button.o: src/ui/lrg-button.c src/ui/lrg-button.h src/ui/lrg-widget.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-panel.o: src/ui/lrg-panel.c src/ui/lrg-panel.h src/ui/lrg-container.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-vbox.o: src/ui/lrg-vbox.c src/ui/lrg-vbox.h src/ui/lrg-container.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-hbox.o: src/ui/lrg-hbox.c src/ui/lrg-hbox.h src/ui/lrg-container.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-grid.o: src/ui/lrg-grid.c src/ui/lrg-grid.h src/ui/lrg-container.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-canvas.o: src/ui/lrg-canvas.c src/ui/lrg-canvas.h src/ui/lrg-container.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-checkbox.o: src/ui/lrg-checkbox.c src/ui/lrg-checkbox.h src/ui/lrg-widget.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-progress-bar.o: src/ui/lrg-progress-bar.c src/ui/lrg-progress-bar.h src/ui/lrg-widget.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-image.o: src/ui/lrg-image.c src/ui/lrg-image.h src/ui/lrg-widget.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-slider.o: src/ui/lrg-slider.c src/ui/lrg-slider.h src/ui/lrg-widget.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-text-input.o: src/ui/lrg-text-input.c src/ui/lrg-text-input.h src/ui/lrg-widget.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/ui/lrg-theme.o: src/ui/lrg-theme.c src/ui/lrg-theme.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# I18N module
$(OBJDIR)/src/i18n/lrg-locale.o: src/i18n/lrg-locale.c src/i18n/lrg-locale.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/i18n/lrg-localization.o: src/i18n/lrg-localization.c src/i18n/lrg-localization.h src/i18n/lrg-locale.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# Networking module
$(OBJDIR)/src/net/lrg-net-message.o: src/net/lrg-net-message.c src/net/lrg-net-message.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/net/lrg-net-peer.o: src/net/lrg-net-peer.c src/net/lrg-net-peer.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/net/lrg-net-server.o: src/net/lrg-net-server.c src/net/lrg-net-server.h src/net/lrg-net-peer.h src/net/lrg-net-message.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/net/lrg-net-client.o: src/net/lrg-net-client.c src/net/lrg-net-client.h src/net/lrg-net-message.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# World3D module
$(OBJDIR)/src/world3d/lrg-bounding-box3d.o: src/world3d/lrg-bounding-box3d.c src/world3d/lrg-bounding-box3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/world3d/lrg-spawn-point3d.o: src/world3d/lrg-spawn-point3d.c src/world3d/lrg-spawn-point3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/world3d/lrg-trigger3d.o: src/world3d/lrg-trigger3d.c src/world3d/lrg-trigger3d.h src/world3d/lrg-bounding-box3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/world3d/lrg-octree.o: src/world3d/lrg-octree.c src/world3d/lrg-octree.h src/world3d/lrg-bounding-box3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/world3d/lrg-portal.o: src/world3d/lrg-portal.c src/world3d/lrg-portal.h src/world3d/lrg-bounding-box3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/world3d/lrg-sector.o: src/world3d/lrg-sector.c src/world3d/lrg-sector.h src/world3d/lrg-bounding-box3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/world3d/lrg-level3d.o: src/world3d/lrg-level3d.c src/world3d/lrg-level3d.h src/world3d/lrg-octree.h src/world3d/lrg-spawn-point3d.h src/world3d/lrg-trigger3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/world3d/lrg-portal-system.o: src/world3d/lrg-portal-system.c src/world3d/lrg-portal-system.h src/world3d/lrg-portal.h src/world3d/lrg-sector.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# Scene module
$(OBJDIR)/src/scene/lrg-material3d.o: src/scene/lrg-material3d.c src/scene/lrg-material3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scene/lrg-scene-object.o: src/scene/lrg-scene-object.c src/scene/lrg-scene-object.h src/scene/lrg-material3d.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scene/lrg-scene-entity.o: src/scene/lrg-scene-entity.c src/scene/lrg-scene-entity.h src/scene/lrg-scene-object.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scene/lrg-scene.o: src/scene/lrg-scene.c src/scene/lrg-scene.h src/scene/lrg-scene-entity.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scene/lrg-scene-serializer.o: src/scene/lrg-scene-serializer.c src/scene/lrg-scene-serializer.h src/scene/lrg-scene.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scene/lrg-scene-serializer-yaml.o: src/scene/lrg-scene-serializer-yaml.c src/scene/lrg-scene-serializer-yaml.h src/scene/lrg-scene-serializer.h src/scene/lrg-scene.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scene/lrg-scene-serializer-blender.o: src/scene/lrg-scene-serializer-blender.c src/scene/lrg-scene-serializer-blender.h src/scene/lrg-scene-serializer-yaml.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scene/lrg-mesh-data.o: src/scene/lrg-mesh-data.c src/scene/lrg-mesh-data.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# Scripting module
$(OBJDIR)/src/scripting/lrg-scripting.o: src/scripting/lrg-scripting.c src/scripting/lrg-scripting.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scripting/lrg-scriptable.o: src/scripting/lrg-scriptable.c src/scripting/lrg-scriptable.h src/scripting/lrg-scripting.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scripting/lrg-scripting-lua.o: src/scripting/lrg-scripting-lua.c src/scripting/lrg-scripting-lua.h src/scripting/lrg-scripting-lua-private.h src/scripting/lrg-lua-bridge.h src/scripting/lrg-lua-api.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scripting/lrg-lua-bridge.o: src/scripting/lrg-lua-bridge.c src/scripting/lrg-lua-bridge.h src/scripting/lrg-scripting-lua-private.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scripting/lrg-lua-api.o: src/scripting/lrg-lua-api.c src/scripting/lrg-lua-api.h src/scripting/lrg-lua-bridge.h src/scripting/lrg-scripting-lua-private.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scripting/lrg-scripting-gi.o: src/scripting/lrg-scripting-gi.c src/scripting/lrg-scripting-gi.h src/scripting/lrg-scripting-gi-private.h src/scripting/lrg-scripting.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scripting/lrg-scripting-python.o: src/scripting/lrg-scripting-python.c src/scripting/lrg-scripting-python.h src/scripting/lrg-scripting-python-private.h src/scripting/lrg-python-bridge.h src/scripting/lrg-python-api.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scripting/lrg-python-bridge.o: src/scripting/lrg-python-bridge.c src/scripting/lrg-python-bridge.h src/scripting/lrg-scripting-python-private.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scripting/lrg-python-api.o: src/scripting/lrg-python-api.c src/scripting/lrg-python-api.h src/scripting/lrg-python-bridge.h src/scripting/lrg-scripting-python-private.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scripting/lrg-scripting-pygobject.o: src/scripting/lrg-scripting-pygobject.c src/scripting/lrg-scripting-pygobject.h src/scripting/lrg-scripting-gi.h src/scripting/lrg-scripting-gi-private.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/scripting/lrg-scripting-gjs.o: src/scripting/lrg-scripting-gjs.c src/scripting/lrg-scripting-gjs.h src/scripting/lrg-scripting-gi.h src/scripting/lrg-scripting-gi-private.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# Game State module
$(OBJDIR)/src/gamestate/lrg-game-state.o: src/gamestate/lrg-game-state.c src/gamestate/lrg-game-state.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/gamestate/lrg-game-state-manager.o: src/gamestate/lrg-game-state-manager.c src/gamestate/lrg-game-state-manager.h src/gamestate/lrg-game-state.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# Crash module
$(OBJDIR)/src/crash/lrg-crash-dialog.o: src/crash/lrg-crash-dialog.c src/crash/lrg-crash-dialog.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/crash/lrg-crash-dialog-terminal.o: src/crash/lrg-crash-dialog-terminal.c src/crash/lrg-crash-dialog-terminal.h src/crash/lrg-crash-dialog.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/crash/lrg-crash-reporter.o: src/crash/lrg-crash-reporter.c src/crash/lrg-crash-reporter.h src/crash/lrg-crash-dialog.h src/crash/lrg-crash-dialog-terminal.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# Accessibility module
$(OBJDIR)/src/accessibility/lrg-color-filter.o: src/accessibility/lrg-color-filter.c src/accessibility/lrg-color-filter.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/accessibility/lrg-accessibility-settings.o: src/accessibility/lrg-accessibility-settings.c src/accessibility/lrg-accessibility-settings.h src/accessibility/lrg-color-filter.h src/settings/lrg-settings-group.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) -c -o $@ $<

# Steam module
$(OBJDIR)/src/steam/lrg-steam-service.o: src/steam/lrg-steam-service.c src/steam/lrg-steam-service.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) $(STEAM_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/steam/lrg-steam-stub.o: src/steam/lrg-steam-stub.c src/steam/lrg-steam-stub.h src/steam/lrg-steam-service.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) $(STEAM_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/steam/lrg-steam-client.o: src/steam/lrg-steam-client.c src/steam/lrg-steam-client.h src/steam/lrg-steam-service.h src/steam/lrg-steam-types.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) $(STEAM_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/steam/lrg-steam-achievements.o: src/steam/lrg-steam-achievements.c src/steam/lrg-steam-achievements.h src/steam/lrg-steam-client.h src/steam/lrg-steam-types.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) $(STEAM_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/steam/lrg-steam-cloud.o: src/steam/lrg-steam-cloud.c src/steam/lrg-steam-cloud.h src/steam/lrg-steam-client.h src/steam/lrg-steam-types.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) $(STEAM_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/steam/lrg-steam-stats.o: src/steam/lrg-steam-stats.c src/steam/lrg-steam-stats.h src/steam/lrg-steam-client.h src/steam/lrg-steam-types.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) $(STEAM_CFLAGS) -c -o $@ $<

$(OBJDIR)/src/steam/lrg-steam-presence.o: src/steam/lrg-steam-presence.c src/steam/lrg-steam-presence.h src/steam/lrg-steam-client.h src/steam/lrg-steam-types.h
	@$(MKDIR_P) $(dir $@)
	$(call print_compile,$<)
	@$(CC) $(LIB_CFLAGS) $(STEAM_CFLAGS) -c -o $@ $<

# =============================================================================
# Phony Targets
# =============================================================================

.PHONY: all lib lib-static lib-shared gir test tests check examples docs
.PHONY: install uninstall clean distclean debug-build generate
.PHONY: deps deps-graylib deps-yamlglib deps-clean check-deps
