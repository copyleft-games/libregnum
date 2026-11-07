/* lrg-physics-shape.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhysicsShape: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_PHYSICS_SHAPE (lrg_physics_shape_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPhysicsShape, lrg_physics_shape, LRG, PHYSICS_SHAPE, GObject)

/**
 * lrg_physics_shape_new:
 *
 * Returns: (transfer full): a new #LrgPhysicsShape
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsShape * lrg_physics_shape_new (void);

G_END_DECLS
