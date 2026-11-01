/* lrg-game-object-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private header for LrgGameObject internals.
 * Only include this from ECS module implementation files.
 */

#pragma once

#include "lrg-game-object.h"

G_BEGIN_DECLS

/*
 * _lrg_game_object_world_update:
 * @self: an #LrgGameObject (not checked)
 * @delta: time since the last frame, in seconds
 *
 * What #LrgWorld does per object per update, with the same result as
 *   if (grl_entity_get_active (entity))
 *       grl_updatable_update (GRL_UPDATABLE (entity), delta);
 * but through the object's #GrlUpdatableInterface, looked up once per
 * object (g_type_interface_peek() is a type-system call) instead of on
 * every call. For the world's own objects, validated when added.
 */
void _lrg_game_object_world_update (LrgGameObject *self,
                                    gfloat         delta);

/*
 * _lrg_game_object_world_draw:
 * @self: an #LrgGameObject (not checked)
 *
 * What #LrgWorld does per object per draw, with the same result as
 *   if (grl_entity_get_visible (entity))
 *       grl_drawable_draw (GRL_DRAWABLE (entity));
 * through the object's cached #GrlDrawableInterface (see
 * _lrg_game_object_world_update()).
 */
void _lrg_game_object_world_draw   (LrgGameObject *self);

G_END_DECLS
