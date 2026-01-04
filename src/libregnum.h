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
#include "core/lrg-asset-pack.h"
#include "core/lrg-event.h"
#include "core/lrg-event-listener.h"
#include "core/lrg-event-bus.h"

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
#include "ui/lrg-tab-view.h"
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
#include "audio/lrg-wave-data.h"
#include "audio/lrg-procedural-audio.h"

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

/* DLC module */
#include "dlc/lrg-dlc-ownership.h"
#include "dlc/lrg-dlc-ownership-steam.h"
#include "dlc/lrg-dlc-ownership-license.h"
#include "dlc/lrg-dlc-ownership-manifest.h"
#include "dlc/lrg-dlc.h"
#include "dlc/lrg-expansion-pack.h"
#include "dlc/lrg-cosmetic-pack.h"
#include "dlc/lrg-quest-pack.h"
#include "dlc/lrg-item-pack.h"
#include "dlc/lrg-character-pack.h"
#include "dlc/lrg-map-pack.h"

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
#include "steam/lrg-workshop-item.h"
#include "steam/lrg-workshop-query.h"
#include "steam/lrg-workshop-manager.h"

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

/* Particles module (Phase 3) */
#include "particles/lrg-particle.h"
#include "particles/lrg-particle-pool.h"
#include "particles/lrg-particle-emitter.h"
#include "particles/lrg-particle-force.h"
#include "particles/lrg-particle-system.h"

/* Post-Processing module (Phase 3) */
#include "postprocess/lrg-post-effect.h"
#include "postprocess/lrg-post-processor.h"
#include "postprocess/effects/lrg-vignette.h"
#include "postprocess/effects/lrg-bloom.h"
#include "postprocess/effects/lrg-film-grain.h"
#include "postprocess/effects/lrg-screen-shake.h"
#include "postprocess/effects/lrg-colorblind-filter.h"
#include "postprocess/effects/lrg-color-grade.h"
#include "postprocess/effects/lrg-fxaa.h"

/* Animation module (Phase 3) */
#include "animation/lrg-bone-pose.h"
#include "animation/lrg-bone.h"
#include "animation/lrg-skeleton.h"
#include "animation/lrg-animation-keyframe.h"
#include "animation/lrg-animation-event.h"
#include "animation/lrg-animation-clip.h"
#include "animation/lrg-animator.h"
#include "animation/lrg-animation-state.h"
#include "animation/lrg-animation-transition.h"
#include "animation/lrg-animation-state-machine.h"
#include "animation/lrg-blend-tree.h"
#include "animation/lrg-animation-layer.h"
#include "animation/lrg-ik-chain.h"
#include "animation/lrg-ik-solver.h"

/* Rich Text module (Phase 3) */
#include "text/lrg-text-span.h"
#include "text/lrg-text-effect.h"
#include "text/lrg-font-manager.h"
#include "text/lrg-rich-text.h"

/* Video Playback module (Phase 3) */
#include "video/lrg-video-texture.h"
#include "video/lrg-video-subtitle-track.h"
#include "video/lrg-video-subtitles.h"
#include "video/lrg-video-player.h"

/* Tween module (Phase 4) */
#include "tween/lrg-easing.h"
#include "tween/lrg-tween-base.h"
#include "tween/lrg-tween.h"
#include "tween/lrg-tween-group.h"
#include "tween/lrg-tween-sequence.h"
#include "tween/lrg-tween-parallel.h"
#include "tween/lrg-tween-manager.h"

/* Transition module (Phase 4) */
#include "transition/lrg-transition.h"
#include "transition/lrg-fade-transition.h"
#include "transition/lrg-wipe-transition.h"
#include "transition/lrg-dissolve-transition.h"
#include "transition/lrg-slide-transition.h"
#include "transition/lrg-zoom-transition.h"
#include "transition/lrg-shader-transition.h"
#include "transition/lrg-transition-manager.h"

/* Trigger2D module (Phase 4) */
#include "trigger2d/lrg-trigger2d.h"
#include "trigger2d/lrg-trigger-rect.h"
#include "trigger2d/lrg-trigger-circle.h"
#include "trigger2d/lrg-trigger-polygon.h"
#include "trigger2d/lrg-trigger-event.h"
#include "trigger2d/lrg-trigger-manager.h"

/* Atlas module (Phase 4) */
#include "atlas/lrg-atlas-region.h"
#include "atlas/lrg-texture-atlas.h"
#include "atlas/lrg-sprite-sheet.h"
#include "atlas/lrg-nine-slice.h"
#include "atlas/lrg-atlas-packer.h"

/* Tutorial module (Phase 4) */
#include "tutorial/lrg-tutorial-step.h"
#include "tutorial/lrg-tutorial.h"
#include "tutorial/lrg-tutorial-manager.h"
#include "tutorial/lrg-highlight.h"
#include "tutorial/lrg-input-prompt.h"
#include "tutorial/lrg-tooltip-arrow.h"

/* Weather module (Phase 4) */
#include "weather/lrg-weather-effect.h"
#include "weather/lrg-rain.h"
#include "weather/lrg-snow.h"
#include "weather/lrg-fog.h"
#include "weather/lrg-lightning.h"
#include "weather/lrg-weather.h"
#include "weather/lrg-day-night-cycle.h"
#include "weather/lrg-weather-manager.h"

/* Lighting module (Phase 4) */
#include "lighting/lrg-shadow-caster.h"
#include "lighting/lrg-light2d.h"
#include "lighting/lrg-point-light2d.h"
#include "lighting/lrg-spot-light2d.h"
#include "lighting/lrg-directional-light2d.h"
#include "lighting/lrg-shadow-map.h"
#include "lighting/lrg-lightmap.h"
#include "lighting/lrg-light-probe.h"
#include "lighting/lrg-lighting-manager.h"

/* Analytics module (Phase 5) */
#include "analytics/lrg-analytics-event.h"
#include "analytics/lrg-consent.h"
#include "analytics/lrg-analytics-backend.h"
#include "analytics/lrg-analytics-backend-http.h"
#include "analytics/lrg-analytics.h"

/* Achievement module (Phase 5) */
#include "achievement/lrg-achievement-progress.h"
#include "achievement/lrg-achievement.h"
#include "achievement/lrg-achievement-manager.h"
#include "achievement/lrg-achievement-notification.h"

/* Photo Mode module (Phase 5) */
#include "photomode/lrg-screenshot.h"
#include "photomode/lrg-photo-camera-controller.h"
#include "photomode/lrg-photo-mode.h"

/* Demo module (Phase 5) */
#include "demo/lrg-demo-gatable.h"
#include "demo/lrg-demo-manager.h"

/* VR module (Phase 5) */
#include "vr/lrg-vr-service.h"
#include "vr/lrg-vr-stub.h"
#include "vr/lrg-vr-comfort.h"

/* Template module - Base game templates for rapid development */
#include "template/lrg-game-template.h"
#include "template/lrg-input-buffer.h"
#include "template/states/lrg-template-main-menu-state.h"
#include "template/states/lrg-template-pause-menu-state.h"
#include "template/states/lrg-template-settings-menu-state.h"
#include "template/states/lrg-template-loading-state.h"
#include "template/states/lrg-template-error-state.h"
#include "template/states/lrg-template-confirmation-state.h"
#include "template/lrg-template-statistics.h"
#include "template/lrg-template-daily-rewards.h"
#include "template/lrg-template-difficulty.h"
#include "template/lrg-idle-mixin.h"
#include "template/lrg-idle-template.h"
#include "template/lrg-idle-2d-template.h"
#include "template/lrg-deck-mixin.h"
#include "template/lrg-deckbuilder-template.h"
#include "template/lrg-deckbuilder-combat-template.h"
#include "template/lrg-deckbuilder-poker-template.h"
#include "template/lrg-poolable.h"
#include "template/lrg-object-pool.h"
#include "template/lrg-template-scalable.h"
#include "template/lrg-game-2d-template.h"
#include "template/lrg-game-3d-template.h"
#include "template/lrg-shooter-2d-template.h"
#include "template/lrg-twin-stick-template.h"
#include "template/lrg-shmup-template.h"
#include "template/lrg-platformer-template.h"
#include "template/lrg-top-down-template.h"
#include "template/lrg-tycoon-template.h"
#include "template/lrg-racing-2d-template.h"
#include "template/lrg-fps-template.h"
#include "template/lrg-third-person-template.h"
#include "template/lrg-racing-3d-template.h"

/* Deckbuilder module (Phase 6) */
#include "deckbuilder/lrg-card-def.h"
#include "deckbuilder/lrg-card-instance.h"
#include "deckbuilder/lrg-card-pile.h"
#include "deckbuilder/lrg-hand.h"
#include "deckbuilder/lrg-deck-def.h"
#include "deckbuilder/lrg-deck-instance.h"
#include "deckbuilder/lrg-deck-builder.h"
#include "deckbuilder/lrg-card-effect.h"
#include "deckbuilder/lrg-card-effect-executor.h"
#include "deckbuilder/lrg-card-effect-registry.h"
#include "deckbuilder/lrg-effect-stack.h"
#include "deckbuilder/lrg-card-event.h"
#include "deckbuilder/lrg-trigger-listener.h"
#include "deckbuilder/lrg-card-keyword.h"
#include "deckbuilder/lrg-card-keyword-def.h"
#include "deckbuilder/lrg-card-keyword-registry.h"
#include "deckbuilder/lrg-synergy.h"
#include "deckbuilder/lrg-status-effect-def.h"
#include "deckbuilder/lrg-status-effect-instance.h"
#include "deckbuilder/lrg-status-effect-registry.h"
#include "deckbuilder/lrg-relic-def.h"
#include "deckbuilder/lrg-relic-instance.h"
#include "deckbuilder/lrg-relic-registry.h"
#include "deckbuilder/lrg-potion-def.h"
#include "deckbuilder/lrg-potion-instance.h"
#include "deckbuilder/lrg-combatant.h"
#include "deckbuilder/lrg-combat-rules.h"
#include "deckbuilder/lrg-enemy-intent.h"
#include "deckbuilder/lrg-enemy-def.h"
#include "deckbuilder/lrg-enemy-instance.h"
#include "deckbuilder/lrg-player-combatant.h"
#include "deckbuilder/lrg-combat-context.h"
#include "deckbuilder/lrg-combat-manager.h"

/* Deckbuilder module (Phase 6.5 - Run/Map System) */
#include "deckbuilder/lrg-map-node.h"
#include "deckbuilder/lrg-run-map.h"
#include "deckbuilder/lrg-run.h"
#include "deckbuilder/lrg-run-manager.h"

/* Deckbuilder module (Phase 6.6 - Scoring System) */
#include "deckbuilder/lrg-scoring-rules.h"
#include "deckbuilder/lrg-scoring-hand.h"
#include "deckbuilder/lrg-scoring-context.h"
#include "deckbuilder/lrg-joker-def.h"
#include "deckbuilder/lrg-joker-instance.h"
#include "deckbuilder/lrg-scoring-manager.h"

/* Deckbuilder module (Phase 7 - Meta-Progression) */
#include "deckbuilder/lrg-character-def.h"
#include "deckbuilder/lrg-player-profile.h"
#include "deckbuilder/lrg-unlock-def.h"
#include "deckbuilder/lrg-ascension.h"
#include "deckbuilder/lrg-deckbuilder-manager.h"

/* Chart module */
#include "chart/lrg-chart-enums.h"
#include "chart/lrg-chart-data-point.h"
#include "chart/lrg-chart-axis-config.h"
#include "chart/lrg-chart-hit-info.h"
#include "chart/lrg-chart-data-series.h"
#include "chart/lrg-chart-legend.h"
#include "chart/lrg-chart-tooltip.h"
#include "chart/lrg-chart-color-scale.h"
#include "chart/lrg-chart.h"
#include "chart/lrg-chart2d.h"
#include "chart/lrg-chart3d.h"
#include "chart/lrg-bar-chart2d.h"
#include "chart/lrg-line-chart2d.h"
#include "chart/lrg-pie-chart2d.h"
#include "chart/lrg-area-chart2d.h"
#include "chart/lrg-scatter-chart2d.h"
#include "chart/lrg-radar-chart2d.h"
#include "chart/lrg-candlestick-chart2d.h"
#include "chart/lrg-gauge-chart2d.h"
#include "chart/lrg-heatmap-chart2d.h"
#include "chart/lrg-histogram-chart2d.h"
#include "chart/lrg-bar-chart3d.h"
#include "chart/lrg-line-chart3d.h"
#include "chart/lrg-pie-chart3d.h"
#include "chart/lrg-surface-chart3d.h"
#include "chart/lrg-scatter-chart3d.h"

#undef LIBREGNUM_INSIDE
