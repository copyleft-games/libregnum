/* lrg-component-private.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private header for LrgComponent internals.
 * Only include this from ECS module implementation files.
 */

#pragma once

#include "lrg-component.h"
#include "lrg-game-object.h"

G_BEGIN_DECLS

/*
 * _lrg_component_set_owner:
 * @self: an #LrgComponent
 * @owner: (nullable): the new owner, or %NULL to detach
 *
 * Sets the component's owner game object.
 *
 * This function is called by LrgGameObject when adding or removing
 * components. It handles calling the attached/detached virtual methods.
 *
 * Do not call this function directly - use lrg_game_object_add_component()
 * and lrg_game_object_remove_component() instead.
 */
void _lrg_component_set_owner (LrgComponent  *self,
                               LrgGameObject *owner);

G_END_DECLS
