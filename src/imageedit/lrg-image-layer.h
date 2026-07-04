/* lrg-image-layer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgImageLayer - one layer of an LrgImageDocument.
 *
 * Wraps an RGBA8 #GrlImage plus the per-layer compositing state (name,
 * opacity, blend mode, visibility, lock, and an x/y offset within the
 * document).  Layers are composited bottom-to-top by #LrgImageDocument.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_IMAGE_LAYER (lrg_image_layer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgImageLayer, lrg_image_layer, LRG, IMAGE_LAYER, GObject)

/**
 * lrg_image_layer_new:
 * @width: layer width in pixels (> 0)
 * @height: layer height in pixels (> 0)
 * @name: (nullable): layer name, or %NULL for a default
 *
 * Creates a new fully-transparent RGBA8 layer.
 *
 * Returns: (transfer full): a new #LrgImageLayer
 */
LRG_AVAILABLE_IN_ALL
LrgImageLayer *lrg_image_layer_new (gint         width,
                                    gint         height,
                                    const gchar *name);

/**
 * lrg_image_layer_new_for_image:
 * @image: (transfer none): an existing #GrlImage (converted to RGBA8)
 * @name: (nullable): layer name, or %NULL for a default
 *
 * Creates a layer that owns a copy of @image (in RGBA8 format).
 *
 * Returns: (transfer full): a new #LrgImageLayer
 */
LRG_AVAILABLE_IN_ALL
LrgImageLayer *lrg_image_layer_new_for_image (GrlImage    *image,
                                              const gchar *name);

/**
 * lrg_image_layer_get_image:
 * @self: a #LrgImageLayer
 *
 * Returns: (transfer none): the layer's backing #GrlImage (RGBA8).  Draw onto
 * it directly (e.g. via #LrgImageCanvas) then mark the document dirty.
 */
LRG_AVAILABLE_IN_ALL
GrlImage *lrg_image_layer_get_image (LrgImageLayer *self);

/**
 * lrg_image_layer_set_image:
 * @self: a #LrgImageLayer
 * @image: (transfer none): the new backing image (converted to RGBA8)
 *
 * Replaces the layer's backing image with a copy of @image.
 */
LRG_AVAILABLE_IN_ALL
void lrg_image_layer_set_image (LrgImageLayer *self,
                                GrlImage      *image);

LRG_AVAILABLE_IN_ALL
const gchar *lrg_image_layer_get_name (LrgImageLayer *self);
LRG_AVAILABLE_IN_ALL
void lrg_image_layer_set_name (LrgImageLayer *self, const gchar *name);

LRG_AVAILABLE_IN_ALL
gfloat lrg_image_layer_get_opacity (LrgImageLayer *self);
LRG_AVAILABLE_IN_ALL
void lrg_image_layer_set_opacity (LrgImageLayer *self, gfloat opacity);

LRG_AVAILABLE_IN_ALL
GrlImageBlendMode lrg_image_layer_get_blend_mode (LrgImageLayer *self);
LRG_AVAILABLE_IN_ALL
void lrg_image_layer_set_blend_mode (LrgImageLayer     *self,
                                     GrlImageBlendMode  mode);

LRG_AVAILABLE_IN_ALL
gboolean lrg_image_layer_get_visible (LrgImageLayer *self);
LRG_AVAILABLE_IN_ALL
void lrg_image_layer_set_visible (LrgImageLayer *self, gboolean visible);

LRG_AVAILABLE_IN_ALL
gboolean lrg_image_layer_get_locked (LrgImageLayer *self);
LRG_AVAILABLE_IN_ALL
void lrg_image_layer_set_locked (LrgImageLayer *self, gboolean locked);

LRG_AVAILABLE_IN_ALL
void lrg_image_layer_get_offset (LrgImageLayer *self, gint *x, gint *y);
LRG_AVAILABLE_IN_ALL
void lrg_image_layer_set_offset (LrgImageLayer *self, gint x, gint y);

LRG_AVAILABLE_IN_ALL
gint lrg_image_layer_get_width (LrgImageLayer *self);
LRG_AVAILABLE_IN_ALL
gint lrg_image_layer_get_height (LrgImageLayer *self);

G_END_DECLS
