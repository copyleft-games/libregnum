/* lrg-reel-image-clip.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelImageClip - a reel clip that draws a static GrlImage onto the canvas.
 *
 * The image is positioned inside an optional destination box (defaulting to the
 * full frame) and scaled according to the #LrgReelFit mode.  An optional tint
 * color modulates each pixel.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-reel-clip.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_IMAGE_CLIP (lrg_reel_image_clip_get_type ())

/**
 * LrgReelImageClip:
 *
 * A #LrgReelClip subclass that renders a static #GrlImage onto the canvas.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelImageClip, lrg_reel_image_clip,
                      LRG, REEL_IMAGE_CLIP, LrgReelClip)

/**
 * lrg_reel_image_clip_new_from_file:
 * @path: path to the image file to load.
 * @error: (nullable): return location for a #GError, or %NULL.
 *
 * Creates a new #LrgReelImageClip by loading an image from @path.
 * Returns %NULL and sets @error if the file cannot be loaded.
 *
 * Returns: (transfer full) (nullable): a new #LrgReelImageClip, or %NULL on
 *   error.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelImageClip *
lrg_reel_image_clip_new_from_file (const gchar  *path,
                                   GError      **error);

/**
 * lrg_reel_image_clip_new_from_image:
 * @image: a #GrlImage to use as the clip source.
 *
 * Creates a new #LrgReelImageClip that holds a reference to @image.
 * Ownership of @image is not transferred; the clip takes its own reference.
 *
 * Returns: (transfer full): a new #LrgReelImageClip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelImageClip *
lrg_reel_image_clip_new_from_image (GrlImage *image);

/**
 * lrg_reel_image_clip_get_image:
 * @self: an #LrgReelImageClip.
 *
 * Returns the #GrlImage used by this clip.
 *
 * Returns: (transfer none) (nullable): the source #GrlImage, or %NULL if none
 *   has been set.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlImage *
lrg_reel_image_clip_get_image (LrgReelImageClip *self);

/**
 * lrg_reel_image_clip_set_image:
 * @self: an #LrgReelImageClip.
 * @image: (nullable): a #GrlImage to use, or %NULL to clear.
 *
 * Replaces the source image.  The clip takes a new reference on @image and
 * drops the reference to the previous image.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_image_clip_set_image (LrgReelImageClip *self,
                                GrlImage         *image);

/**
 * lrg_reel_image_clip_get_fit:
 * @self: an #LrgReelImageClip.
 *
 * Returns the #LrgReelFit mode used when placing the image inside the
 * destination box.
 *
 * Returns: the current #LrgReelFit value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelFit
lrg_reel_image_clip_get_fit (LrgReelImageClip *self);

/**
 * lrg_reel_image_clip_set_fit:
 * @self: an #LrgReelImageClip.
 * @fit: the new #LrgReelFit value.
 *
 * Sets the fit mode used when scaling the image into the destination box.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_image_clip_set_fit (LrgReelImageClip *self,
                              LrgReelFit        fit);

/**
 * lrg_reel_image_clip_set_tint:
 * @self: an #LrgReelImageClip.
 * @tint: (nullable): a #GrlColor to apply as a tint, or %NULL to clear.
 *
 * Sets the tint color multiplied over the image pixels.  Pass %NULL to
 * disable tinting (equivalent to an opaque white tint).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_image_clip_set_tint (LrgReelImageClip *self,
                               const GrlColor   *tint);

/**
 * lrg_reel_image_clip_get_tint:
 * @self: an #LrgReelImageClip.
 *
 * Returns whether a tint is currently active.  If %TRUE, the tint color is
 * stored inside the clip; if %FALSE no tint is applied.
 *
 * Returns: %TRUE if a tint has been set.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_image_clip_get_tint (LrgReelImageClip *self);

/**
 * lrg_reel_image_clip_set_box:
 * @self: an #LrgReelImageClip.
 * @x: left edge of the destination box in pixels.
 * @y: top edge of the destination box in pixels.
 * @width: width of the destination box in pixels (must be > 0).
 * @height: height of the destination box in pixels (must be > 0).
 *
 * Constrains the rendered image to the given sub-rectangle of the frame.
 * By default the destination box covers the entire frame.  Setting a box
 * overrides that default.  Call with width and height equal to 0 to revert
 * to the full-frame default.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_image_clip_set_box (LrgReelImageClip *self,
                              gint              x,
                              gint              y,
                              gint              width,
                              gint              height);

G_END_DECLS
