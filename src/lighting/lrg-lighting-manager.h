/* lrg-lighting-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lighting system manager.
 *
 * Manages all lights, shadow casters, and lighting composition.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-light2d.h"
#include "lrg-shadow-caster.h"
#include "lrg-lightmap.h"

G_BEGIN_DECLS

#define LRG_TYPE_LIGHTING_MANAGER (lrg_lighting_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLightingManager, lrg_lighting_manager, LRG, LIGHTING_MANAGER, GObject)

LRG_AVAILABLE_IN_ALL
LrgLightingManager * lrg_lighting_manager_new (void);

/* Light management */

LRG_AVAILABLE_IN_ALL
void lrg_lighting_manager_add_light (LrgLightingManager *self, LrgLight2D *light);

LRG_AVAILABLE_IN_ALL
void lrg_lighting_manager_remove_light (LrgLightingManager *self, LrgLight2D *light);

LRG_AVAILABLE_IN_ALL
GList * lrg_lighting_manager_get_lights (LrgLightingManager *self);

LRG_AVAILABLE_IN_ALL
guint lrg_lighting_manager_get_light_count (LrgLightingManager *self);

/* Shadow caster management */

LRG_AVAILABLE_IN_ALL
void lrg_lighting_manager_add_shadow_caster (LrgLightingManager *self, LrgShadowCaster *caster);

LRG_AVAILABLE_IN_ALL
void lrg_lighting_manager_remove_shadow_caster (LrgLightingManager *self, LrgShadowCaster *caster);

LRG_AVAILABLE_IN_ALL
guint lrg_lighting_manager_get_shadow_caster_count (LrgLightingManager *self);

/* Lightmap */

LRG_AVAILABLE_IN_ALL
LrgLightmap * lrg_lighting_manager_get_lightmap (LrgLightingManager *self);

LRG_AVAILABLE_IN_ALL
void lrg_lighting_manager_set_lightmap (LrgLightingManager *self, LrgLightmap *lightmap);

/* Ambient */

LRG_AVAILABLE_IN_ALL
void lrg_lighting_manager_get_ambient_color (LrgLightingManager *self, guint8 *r, guint8 *g, guint8 *b);

LRG_AVAILABLE_IN_ALL
void lrg_lighting_manager_set_ambient_color (LrgLightingManager *self, guint8 r, guint8 g, guint8 b);

LRG_AVAILABLE_IN_ALL
gfloat lrg_lighting_manager_get_ambient_intensity (LrgLightingManager *self);

LRG_AVAILABLE_IN_ALL
void lrg_lighting_manager_set_ambient_intensity (LrgLightingManager *self, gfloat intensity);

/* Settings */

LRG_AVAILABLE_IN_ALL
gboolean lrg_lighting_manager_get_shadows_enabled (LrgLightingManager *self);

LRG_AVAILABLE_IN_ALL
void lrg_lighting_manager_set_shadows_enabled (LrgLightingManager *self, gboolean enabled);

LRG_AVAILABLE_IN_ALL
LrgShadowMethod lrg_lighting_manager_get_default_shadow_method (LrgLightingManager *self);

LRG_AVAILABLE_IN_ALL
void lrg_lighting_manager_set_default_shadow_method (LrgLightingManager *self, LrgShadowMethod method);

LRG_AVAILABLE_IN_ALL
LrgLightBlendMode lrg_lighting_manager_get_blend_mode (LrgLightingManager *self);

LRG_AVAILABLE_IN_ALL
void lrg_lighting_manager_set_blend_mode (LrgLightingManager *self, LrgLightBlendMode mode);

/* Viewport */

LRG_AVAILABLE_IN_ALL
void lrg_lighting_manager_set_viewport (LrgLightingManager *self, gfloat x, gfloat y, gfloat width, gfloat height);

/* Update and render */

LRG_AVAILABLE_IN_ALL
void lrg_lighting_manager_update (LrgLightingManager *self, gfloat delta_time);

LRG_AVAILABLE_IN_ALL
void lrg_lighting_manager_render (LrgLightingManager *self);

LRG_AVAILABLE_IN_ALL
guint lrg_lighting_manager_get_light_texture_id (LrgLightingManager *self);

G_END_DECLS
