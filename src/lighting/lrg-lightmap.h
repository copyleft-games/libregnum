/* lrg-lightmap.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Baked lightmap for static lighting.
 *
 * Pre-computed lighting stored as a texture.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_LIGHTMAP (lrg_lightmap_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLightmap, lrg_lightmap, LRG, LIGHTMAP, GObject)

LRG_AVAILABLE_IN_ALL
LrgLightmap * lrg_lightmap_new (guint width, guint height);

LRG_AVAILABLE_IN_ALL
LrgLightmap * lrg_lightmap_load (const gchar *path, GError **error);

LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap_save (LrgLightmap *self, const gchar *path, GError **error);

LRG_AVAILABLE_IN_ALL
guint lrg_lightmap_get_width (LrgLightmap *self);

LRG_AVAILABLE_IN_ALL
guint lrg_lightmap_get_height (LrgLightmap *self);

LRG_AVAILABLE_IN_ALL
guint lrg_lightmap_get_texture_id (LrgLightmap *self);

LRG_AVAILABLE_IN_ALL
void lrg_lightmap_set_pixel (LrgLightmap *self, guint x, guint y, guint8 r, guint8 g, guint8 b);

LRG_AVAILABLE_IN_ALL
void lrg_lightmap_get_pixel (LrgLightmap *self, guint x, guint y, guint8 *r, guint8 *g, guint8 *b);

LRG_AVAILABLE_IN_ALL
void lrg_lightmap_clear (LrgLightmap *self, guint8 r, guint8 g, guint8 b);

LRG_AVAILABLE_IN_ALL
void lrg_lightmap_upload (LrgLightmap *self);

G_END_DECLS
