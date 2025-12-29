/* libregnum.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Main public header for Libregnum game engine.
 * Include only this file to use Libregnum.
 */

#pragma once

#define LIBREGNUM_INSIDE

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

/* Version information */
#include "lrg-version.h"

/* Forward declarations */
#include "lrg-types.h"

/* Enumerations */
#include "lrg-enums.h"

/* Logging */
#include "lrg-log.h"

/* Core module */
#include "core/lrg-engine.h"
#include "core/lrg-registry.h"
#include "core/lrg-data-loader.h"
#include "core/lrg-asset-manager.h"

/* Graphics module */
#include "graphics/lrg-drawable.h"
#include "graphics/lrg-window.h"
#include "graphics/lrg-grl-window.h"
#include "graphics/lrg-camera.h"
#include "graphics/lrg-camera2d.h"
#include "graphics/lrg-camera3d.h"
#include "graphics/lrg-camera-isometric.h"
#include "graphics/lrg-camera-topdown.h"
#include "graphics/lrg-camera-sideon.h"
#include "graphics/lrg-camera-firstperson.h"
#include "graphics/lrg-camera-thirdperson.h"
#include "graphics/lrg-renderer.h"

/* ECS module */
#include "ecs/lrg-component.h"
#include "ecs/lrg-game-object.h"
#include "ecs/lrg-world.h"
#include "ecs/components/lrg-sprite-component.h"
#include "ecs/components/lrg-collider-component.h"
#include "ecs/components/lrg-transform-component.h"
#include "ecs/components/lrg-animator-component.h"

/* Input module */
#include "input/lrg-input.h"
#include "input/lrg-input-manager.h"
#include "input/lrg-input-keyboard.h"
#include "input/lrg-input-mouse.h"
#include "input/lrg-input-gamepad.h"
#include "input/lrg-input-mock.h"
#include "input/lrg-input-software.h"
#include "input/lrg-input-binding.h"
#include "input/lrg-input-action.h"
#include "input/lrg-input-map.h"

/* Shapes module */
#include "shapes/lrg-shape.h"
#include "shapes/lrg-shape2d.h"
#include "shapes/lrg-shape3d.h"
#include "shapes/lrg-sphere3d.h"
#include "shapes/lrg-cube3d.h"
#include "shapes/lrg-line3d.h"
#include "shapes/lrg-text2d.h"
#include "shapes/lrg-rectangle2d.h"
#include "shapes/lrg-circle2d.h"
#include "shapes/lrg-cylinder3d.h"
#include "shapes/lrg-cone3d.h"
#include "shapes/lrg-plane3d.h"
#include "shapes/lrg-grid3d.h"
#include "shapes/lrg-circle3d.h"
#include "shapes/lrg-torus3d.h"
#include "shapes/lrg-icosphere3d.h"

/* UI module */
#include "ui/lrg-ui-event.h"
#include "ui/lrg-widget.h"
#include "ui/lrg-container.h"
#include "ui/lrg-label.h"
#include "ui/lrg-button.h"
#include "ui/lrg-panel.h"
#include "ui/lrg-vbox.h"
#include "ui/lrg-hbox.h"
#include "ui/lrg-grid.h"
#include "ui/lrg-canvas.h"
#include "ui/lrg-checkbox.h"
#include "ui/lrg-progress-bar.h"
#include "ui/lrg-image.h"
#include "ui/lrg-slider.h"
#include "ui/lrg-text-input.h"
#include "ui/lrg-theme.h"

/* Tilemap module */
#include "tilemap/lrg-tileset.h"
#include "tilemap/lrg-tilemap-layer.h"
#include "tilemap/lrg-tilemap.h"

/* Save module */
#include "save/lrg-saveable.h"
#include "save/lrg-save-context.h"
#include "save/lrg-save-game.h"
#include "save/lrg-save-manager.h"

/* Dialog module */
#include "dialog/lrg-dialog-response.h"
#include "dialog/lrg-dialog-node.h"
#include "dialog/lrg-dialog-tree.h"
#include "dialog/lrg-dialog-runner.h"

/* Quest module */
#include "quest/lrg-quest-objective.h"
#include "quest/lrg-quest-def.h"
#include "quest/lrg-quest-instance.h"
#include "quest/lrg-quest-log.h"

/* Audio module */
#include "audio/lrg-sound-bank.h"
#include "audio/lrg-music-track.h"
#include "audio/lrg-audio-manager.h"

/* Inventory module */
#include "inventory/lrg-item-def.h"
#include "inventory/lrg-item-stack.h"
#include "inventory/lrg-inventory.h"
#include "inventory/lrg-equipment.h"

/* I18N module */
#include "i18n/lrg-locale.h"
#include "i18n/lrg-localization.h"

/* Pathfinding module */
#include "pathfinding/lrg-nav-cell.h"
#include "pathfinding/lrg-path.h"
#include "pathfinding/lrg-nav-grid.h"
#include "pathfinding/lrg-pathfinder.h"

/* AI module */
#include "ai/lrg-blackboard.h"
#include "ai/lrg-bt-node.h"
#include "ai/lrg-bt-composite.h"
#include "ai/lrg-bt-decorator.h"
#include "ai/lrg-bt-leaf.h"
#include "ai/lrg-behavior-tree.h"

/* Physics module */
#include "physics/lrg-collision-info.h"
#include "physics/lrg-rigid-body.h"
#include "physics/lrg-physics-world.h"

/* Debug module */
#include "debug/lrg-profiler.h"
#include "debug/lrg-debug-console.h"
#include "debug/lrg-debug-overlay.h"
#include "debug/lrg-inspector.h"

/* Networking module */
#include "net/lrg-net-message.h"
#include "net/lrg-net-peer.h"
#include "net/lrg-net-server.h"
#include "net/lrg-net-client.h"

/* World3D module */
#include "world3d/lrg-bounding-box3d.h"
#include "world3d/lrg-spawn-point3d.h"
#include "world3d/lrg-trigger3d.h"
#include "world3d/lrg-octree.h"
#include "world3d/lrg-portal.h"
#include "world3d/lrg-sector.h"
#include "world3d/lrg-level3d.h"
#include "world3d/lrg-portal-system.h"

/* Scene module */
#include "scene/lrg-mesh-data.h"
#include "scene/lrg-material3d.h"
#include "scene/lrg-scene-object.h"
#include "scene/lrg-scene-entity.h"
#include "scene/lrg-scene.h"
#include "scene/lrg-scene-serializer.h"
#include "scene/lrg-scene-serializer-yaml.h"
#include "scene/lrg-scene-serializer-blender.h"

/* Mod module */
#include "mod/lrg-mod-manifest.h"
#include "mod/lrg-mod.h"
#include "mod/lrg-mod-loader.h"
#include "mod/lrg-mod-manager.h"
#include "mod/lrg-modable.h"
#include "mod/lrg-providers.h"

/* Scripting module */
#include "scripting/lrg-scripting.h"
#include "scripting/lrg-scriptable.h"
#ifdef LRG_HAS_LUAJIT
#include "scripting/lrg-scripting-lua.h"
#endif
#ifdef LRG_HAS_GI
#include "scripting/lrg-scripting-gi.h"
#endif
#ifdef LRG_HAS_PYTHON
#include "scripting/lrg-scripting-python.h"
#include "scripting/lrg-scripting-pygobject.h"
#endif
#ifdef LRG_HAS_GJS
#include "scripting/lrg-scripting-gjs.h"
#endif

/* Settings module (Phase 1) */
#include "settings/lrg-settings-group.h"
#include "settings/lrg-graphics-settings.h"
#include "settings/lrg-audio-settings.h"
#include "settings/lrg-settings.h"

/* Game State module (Phase 1) */
#include "gamestate/lrg-game-state.h"
#include "gamestate/lrg-game-state-manager.h"

/* Crash module (Phase 1) */
#include "crash/lrg-crash-dialog.h"
#include "crash/lrg-crash-dialog-terminal.h"
#include "crash/lrg-crash-reporter.h"

/* Accessibility module (Phase 1) */
#include "accessibility/lrg-color-filter.h"
#include "accessibility/lrg-accessibility-settings.h"

/* Steam module (Phase 1) - conditionally included */
#include "steam/lrg-steam-service.h"
#include "steam/lrg-steam-stub.h"
#include "steam/lrg-steam-client.h"
#include "steam/lrg-steam-achievements.h"
#include "steam/lrg-steam-cloud.h"
#include "steam/lrg-steam-stats.h"
#include "steam/lrg-steam-presence.h"

/* Economy module (Phase 2) */
#include "economy/lrg-resource.h"
#include "economy/lrg-resource-pool.h"
#include "economy/lrg-production-recipe.h"
#include "economy/lrg-producer.h"
#include "economy/lrg-consumer.h"
#include "economy/lrg-market.h"
#include "economy/lrg-economy-manager.h"
#include "economy/lrg-offline-calculator.h"

/* Idle module (Phase 2) */
#include "idle/lrg-big-number.h"
#include "idle/lrg-milestone.h"
#include "idle/lrg-idle-calculator.h"
#include "idle/lrg-prestige.h"
#include "idle/lrg-unlock-tree.h"
#include "idle/lrg-automation.h"

/* Building module (Phase 2) */
#include "building/lrg-building-def.h"
#include "building/lrg-building-instance.h"
#include "building/lrg-build-grid.h"
#include "building/lrg-placement-system.h"
#include "building/lrg-placement-ghost.h"
#include "building/lrg-building-ui.h"

/* Vehicle module (Phase 2) */
#include "vehicle/lrg-wheel.h"
#include "vehicle/lrg-vehicle.h"
#include "vehicle/lrg-vehicle-controller.h"
#include "vehicle/lrg-vehicle-camera.h"
#include "vehicle/lrg-vehicle-audio.h"
#include "vehicle/lrg-road.h"
#include "vehicle/lrg-road-network.h"
#include "vehicle/lrg-traffic-agent.h"

#undef LIBREGNUM_INSIDE
