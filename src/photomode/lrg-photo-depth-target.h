/* lrg-photo-depth-target.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhotoDepthTarget - a render target whose depth buffer can be
 * sampled, what the depth of field needs.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_PHOTO_DEPTH_TARGET (lrg_photo_depth_target_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPhotoDepthTarget, lrg_photo_depth_target, LRG, PHOTO_DEPTH_TARGET, GObject)

/**
 * lrg_photo_depth_target_new:
 * @width: the width in pixels (at least 1)
 * @height: the height in pixels (at least 1)
 * @error: (nullable): a location for an error
 *
 * Creates a render target of @width x @height with an RGBA colour
 * texture and a depth texture (a #GrlRenderTexture only has a depth
 * renderbuffer, which a shader cannot read).  Needs a window (a GL
 * context).
 *
 * Returns: (transfer full) (nullable): the target, or %NULL without a
 *   context or when the framebuffer is incomplete
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoDepthTarget *
lrg_photo_depth_target_new (gint     width,
                            gint     height,
                            GError **error);

/**
 * lrg_photo_depth_target_get_width:
 * @self: an #LrgPhotoDepthTarget
 *
 * Returns: the width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_photo_depth_target_get_width (LrgPhotoDepthTarget *self);

/**
 * lrg_photo_depth_target_get_height:
 * @self: an #LrgPhotoDepthTarget
 *
 * Returns: the height in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_photo_depth_target_get_height (LrgPhotoDepthTarget *self);

/**
 * lrg_photo_depth_target_begin:
 * @self: an #LrgPhotoDepthTarget
 *
 * Starts drawing into the target (like a render texture's begin): the
 * 3D mode of the scene is entered after this call.  The caller clears
 * the colour and the depth.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_depth_target_begin (LrgPhotoDepthTarget *self);

/**
 * lrg_photo_depth_target_end:
 * @self: an #LrgPhotoDepthTarget
 *
 * Ends drawing into the target.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_depth_target_end (LrgPhotoDepthTarget *self);

/**
 * lrg_photo_depth_target_get_color_id:
 * @self: an #LrgPhotoDepthTarget
 *
 * Returns: the GL id of the colour texture
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_photo_depth_target_get_color_id (LrgPhotoDepthTarget *self);

/**
 * lrg_photo_depth_target_get_depth_id:
 * @self: an #LrgPhotoDepthTarget
 *
 * Returns: the GL id of the depth texture
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_photo_depth_target_get_depth_id (LrgPhotoDepthTarget *self);

G_END_DECLS
