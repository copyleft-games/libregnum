/* lrg-particle-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgParticleComponent: plays a `.rparticles` effect on an LrgEntity3D,
 * following the entity's transform, colliding with the world's physics
 * and drawing through LrgWorld3DRenderer's transparent pass.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ecs/lrg-component.h"
#include "../../core/lrg-asset-manager.h"
#include "../../particles/lrg-particle-asset.h"

G_BEGIN_DECLS

#define LRG_TYPE_PARTICLE_COMPONENT (lrg_particle_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticleComponent, lrg_particle_component, LRG, PARTICLE_COMPONENT, LrgComponent)

/**
 * lrg_particle_component_new:
 *
 * Creates a component without an effect; set #LrgParticleComponent:asset
 * or #LrgParticleComponent:asset-path.
 *
 * Returns: (transfer full): a new #LrgParticleComponent
 */
LRG_AVAILABLE_IN_ALL
LrgParticleComponent * lrg_particle_component_new (void);

/**
 * lrg_particle_component_new_with_asset:
 * @asset: the effect to play
 *
 * Returns: (transfer full): a component playing @asset
 */
LRG_AVAILABLE_IN_ALL
LrgParticleComponent * lrg_particle_component_new_with_asset (LrgParticleAsset *asset);

/**
 * lrg_particle_component_get_asset:
 * @self: a component
 *
 * Returns: (transfer none) (nullable): the effect asset (loaded from the
 *   path on first use)
 */
LRG_AVAILABLE_IN_ALL
LrgParticleAsset * lrg_particle_component_get_asset (LrgParticleComponent *self);

/**
 * lrg_particle_component_set_asset:
 * @self: a component
 * @asset: (nullable): the effect to play
 *
 * Sets #LrgParticleComponent:asset and builds a new effect (playing it
 * when #LrgParticleComponent:auto-play is set).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_component_set_asset (LrgParticleComponent *self,
                                       LrgParticleAsset     *asset);

/**
 * lrg_particle_component_get_asset_path:
 * @self: a component
 *
 * Returns: (nullable): the `.rparticles` reference
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_particle_component_get_asset_path (LrgParticleComponent *self);

/**
 * lrg_particle_component_set_asset_path:
 * @self: a component
 * @path: (nullable): a `.rparticles` reference: a path the asset manager
 *   resolves, `guid:<uuid>`, or a file path
 *
 * Sets #LrgParticleComponent:asset-path (the level and prefab key).  The
 * file is loaded on the next update; a load error is logged once with
 * its key path and the component draws nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_component_set_asset_path (LrgParticleComponent *self,
                                            const gchar          *path);

/**
 * lrg_particle_component_set_asset_manager:
 * @self: a component
 * @manager: (nullable): resolves the asset path, textures and meshes; the
 *   default engine's manager is used without one
 *
 * Sets #LrgParticleComponent:asset-manager.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_component_set_asset_manager (LrgParticleComponent *self,
                                               LrgAssetManager      *manager);

/**
 * lrg_particle_component_get_effect:
 * @self: a component
 *
 * Returns: (transfer none) (nullable): the playing effect (for the
 *   editor's stats, seeking and live reload)
 */
LRG_AVAILABLE_IN_ALL
LrgParticleEffect * lrg_particle_component_get_effect (LrgParticleComponent *self);

/**
 * lrg_particle_component_play:
 * @self: a component
 *
 * Plays the effect (after loading it).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_component_play (LrgParticleComponent *self);

/**
 * lrg_particle_component_pause:
 * @self: a component
 *
 * Pauses the effect.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_component_pause (LrgParticleComponent *self);

/**
 * lrg_particle_component_stop:
 * @self: a component
 *
 * Stops the effect and clears its particles.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_component_stop (LrgParticleComponent *self);

/**
 * lrg_particle_component_restart:
 * @self: a component
 *
 * Plays the effect again from the start.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_component_restart (LrgParticleComponent *self);

/**
 * lrg_particle_component_get_auto_play:
 * @self: a component
 *
 * Returns: whether the effect plays as soon as it is loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_component_get_auto_play (LrgParticleComponent *self);

/**
 * lrg_particle_component_set_auto_play:
 * @self: a component
 * @auto_play: whether the effect plays as soon as it is loaded
 *
 * Sets #LrgParticleComponent:auto-play (default %TRUE).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_component_set_auto_play (LrgParticleComponent *self,
                                           gboolean              auto_play);

/**
 * lrg_particle_component_get_time_scale:
 * @self: a component
 *
 * Returns: the simulation speed factor
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_particle_component_get_time_scale (LrgParticleComponent *self);

/**
 * lrg_particle_component_set_time_scale:
 * @self: a component
 * @scale: simulation speed factor (>= 0)
 *
 * Sets #LrgParticleComponent:time-scale.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_component_set_time_scale (LrgParticleComponent *self,
                                            gfloat                scale);

G_END_DECLS
