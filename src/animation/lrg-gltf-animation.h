/* lrg-gltf-animation.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGltfAnimation: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_GLTF_ANIMATION (lrg_gltf_animation_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGltfAnimation, lrg_gltf_animation, LRG, GLTF_ANIMATION, GObject)

/**
 * lrg_gltf_animation_new:
 *
 * Returns: (transfer full): a new #LrgGltfAnimation
 */
LRG_AVAILABLE_IN_ALL
LrgGltfAnimation * lrg_gltf_animation_new (void);

G_END_DECLS
