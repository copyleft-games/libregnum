/* lrg-collider-component3d.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgColliderComponent3D: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ecs/lrg-component.h"

G_BEGIN_DECLS

#define LRG_TYPE_COLLIDER_COMPONENT3D (lrg_collider_component3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgColliderComponent3D, lrg_collider_component3d, LRG, COLLIDER_COMPONENT3D, LrgComponent)

/**
 * lrg_collider_component3d_new:
 *
 * Returns: (transfer full): a new #LrgColliderComponent3D
 */
LRG_AVAILABLE_IN_ALL
LrgColliderComponent3D * lrg_collider_component3d_new (void);

G_END_DECLS
