/* lrg-shadow-map.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shadow map for 2D lighting.
 *
 * Manages shadow texture generation for a light.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-shadow-caster.h"

G_BEGIN_DECLS

#define LRG_TYPE_SHADOW_MAP (lrg_shadow_map_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgShadowMap, lrg_shadow_map, LRG, SHADOW_MAP, GObject)

LRG_AVAILABLE_IN_ALL
LrgShadowMap * lrg_shadow_map_new (guint width, guint height);

LRG_AVAILABLE_IN_ALL
guint lrg_shadow_map_get_width (LrgShadowMap *self);

LRG_AVAILABLE_IN_ALL
guint lrg_shadow_map_get_height (LrgShadowMap *self);

LRG_AVAILABLE_IN_ALL
void lrg_shadow_map_resize (LrgShadowMap *self, guint width, guint height);

LRG_AVAILABLE_IN_ALL
void lrg_shadow_map_clear (LrgShadowMap *self);

LRG_AVAILABLE_IN_ALL
void lrg_shadow_map_render_shadows (LrgShadowMap *self, gfloat light_x, gfloat light_y, GPtrArray *casters);

LRG_AVAILABLE_IN_ALL
guint lrg_shadow_map_get_texture_id (LrgShadowMap *self);

G_END_DECLS
