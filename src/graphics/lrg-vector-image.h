/* lrg-vector-image.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVectorImage - Load and rasterize SVG vector assets.
 *
 * Loads an SVG asset via graylib's SVG path rasterizer and renders it
 * to a GrlImage or GrlTexture at an arbitrary target size.  Games can
 * ship scalable icon and UI assets that render crisp at any resolution.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Error Domain
 * ========================================================================== */

/**
 * LRG_VECTOR_IMAGE_ERROR:
 *
 * Error domain for #LrgVectorImage errors.
 *
 * Since: 1.0
 */
#define LRG_VECTOR_IMAGE_ERROR (lrg_vector_image_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_vector_image_error_quark (void);

/**
 * LrgVectorImageError:
 * @LRG_VECTOR_IMAGE_ERROR_FAILED:  Generic failure.
 * @LRG_VECTOR_IMAGE_ERROR_LOAD:    The SVG data could not be loaded or parsed.
 * @LRG_VECTOR_IMAGE_ERROR_RENDER:  Rasterization failed (e.g., bad dimensions).
 *
 * Error codes for #LrgVectorImage.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_VECTOR_IMAGE_ERROR_FAILED,
    LRG_VECTOR_IMAGE_ERROR_LOAD,
    LRG_VECTOR_IMAGE_ERROR_RENDER
} LrgVectorImageError;

/* ==========================================================================
 * Type Declaration
 * ========================================================================== */

#define LRG_TYPE_VECTOR_IMAGE (lrg_vector_image_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVectorImage, lrg_vector_image, LRG, VECTOR_IMAGE, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_vector_image_new_from_file:
 * @filename: path to the SVG file
 * @error: (nullable): return location for a #GError
 *
 * Loads an SVG file from disk.  The parsed shapes and their union bounding
 * box are retained so that subsequent calls to lrg_vector_image_render()
 * are cheap (no re-parsing).
 *
 * Returns: (transfer full) (nullable): a new #LrgVectorImage, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVectorImage *
lrg_vector_image_new_from_file (const gchar  *filename,
                                GError      **error);

/**
 * lrg_vector_image_new_from_data:
 * @data: (array length=len): SVG source bytes
 * @len: length of @data in bytes
 * @error: (nullable): return location for a #GError
 *
 * Loads an SVG from an in-memory buffer.  Useful for embedded assets or
 * procedurally generated SVG strings.
 *
 * Returns: (transfer full) (nullable): a new #LrgVectorImage, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVectorImage *
lrg_vector_image_new_from_data (const gchar  *data,
                                gsize         len,
                                GError      **error);

/* ==========================================================================
 * Introspection
 * ========================================================================== */

/**
 * lrg_vector_image_get_source_size:
 * @self: an #LrgVectorImage
 * @out_w: (out) (nullable): return location for the intrinsic width, or %NULL
 * @out_h: (out) (nullable): return location for the intrinsic height, or %NULL
 *
 * Returns the intrinsic size of the SVG as determined by the union bounding
 * box of all parsed shapes.  This is the "natural" size of the artwork in
 * SVG user units.
 *
 * Returns: %TRUE if the size is available
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_vector_image_get_source_size (LrgVectorImage *self,
                                  gfloat         *out_w,
                                  gfloat         *out_h);

/**
 * lrg_vector_image_get_shape_count:
 * @self: an #LrgVectorImage
 *
 * Returns the number of #GrlVectorShape objects parsed from the SVG.
 *
 * Returns: number of shapes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_vector_image_get_shape_count (LrgVectorImage *self);

/* ==========================================================================
 * Rasterization
 * ========================================================================== */

/**
 * lrg_vector_image_render:
 * @self: an #LrgVectorImage
 * @width: target width in pixels (must be > 0)
 * @height: target height in pixels (must be > 0)
 * @bg_or_null: (nullable): background fill colour, or %NULL for transparent
 * @preserve_aspect: if %TRUE the SVG is letterboxed inside @width × @height
 *
 * Rasterizes the vector image into a new RGBA #GrlImage at the requested
 * pixel dimensions.  Antialiasing is enabled automatically.
 *
 * When @preserve_aspect is %FALSE the artwork is stretched to fill the
 * entire @width × @height rectangle.  When %TRUE the artwork is scaled
 * uniformly to fit within the rectangle (letterboxed); pixels outside the
 * scaled content area are filled with @bg_or_null (or transparent).
 *
 * Returns: (transfer full) (nullable): a new #GrlImage, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlImage *
lrg_vector_image_render (LrgVectorImage  *self,
                         gint             width,
                         gint             height,
                         const GrlColor  *bg_or_null,
                         gboolean         preserve_aspect);

/**
 * lrg_vector_image_render_to_texture:
 * @self: an #LrgVectorImage
 * @width: target width in pixels (must be > 0)
 * @height: target height in pixels (must be > 0)
 * @bg_or_null: (nullable): background fill colour, or %NULL for transparent
 * @preserve_aspect: if %TRUE the SVG is letterboxed inside @width × @height
 *
 * Convenience wrapper: rasterizes the vector image and uploads it to the
 * GPU as a #GrlTexture.  Equivalent to calling lrg_vector_image_render()
 * followed by grl_texture_new_from_image().
 *
 * Returns: (transfer full) (nullable): a new #GrlTexture, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlTexture *
lrg_vector_image_render_to_texture (LrgVectorImage  *self,
                                    gint             width,
                                    gint             height,
                                    const GrlColor  *bg_or_null,
                                    gboolean         preserve_aspect);

G_END_DECLS
