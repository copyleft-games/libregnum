/* lrg-text-baker.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTextBaker - Headless rasterization of TTF/OTF text to CPU images,
 * GPU textures, and glyph atlases using graylib's GrlImageFont.
 *
 * Unlike the GPU-only LrgFontManager / GrlFont path, LrgTextBaker works
 * entirely in software and requires no display or GL context.  It is
 * intended for use in asset-pipeline baking, procedural texture generation,
 * and any other context where a windowed renderer is unavailable.
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
 * LRG_TEXT_BAKER_ERROR:
 *
 * Error domain for #LrgTextBaker errors.
 */
#define LRG_TEXT_BAKER_ERROR (lrg_text_baker_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_text_baker_error_quark (void);

/**
 * LrgTextBakerError:
 * @LRG_TEXT_BAKER_ERROR_FAILED: Generic failure.
 * @LRG_TEXT_BAKER_ERROR_FONT_LOAD: Font file could not be loaded.
 * @LRG_TEXT_BAKER_ERROR_INVALID_SIZE: The requested pixel size is invalid
 *   (must be > 0).
 * @LRG_TEXT_BAKER_ERROR_RENDER: An error occurred during image rendering.
 *
 * Error codes for #LrgTextBaker operations.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TEXT_BAKER_ERROR_FAILED,
    LRG_TEXT_BAKER_ERROR_FONT_LOAD,
    LRG_TEXT_BAKER_ERROR_INVALID_SIZE,
    LRG_TEXT_BAKER_ERROR_RENDER
} LrgTextBakerError;

/* ==========================================================================
 * Type Declaration
 * ========================================================================== */

#define LRG_TYPE_TEXT_BAKER (lrg_text_baker_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTextBaker, lrg_text_baker, LRG, TEXT_BAKER, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_text_baker_new_from_file:
 * @font_path: path to a TTF or OTF font file
 * @error: (nullable): return location for a #GError
 *
 * Creates a new #LrgTextBaker by loading a font from @font_path.
 *
 * The font is loaded headlessly via grl_image_font_new_from_file().  No
 * display or GL context is required.
 *
 * Returns: (transfer full) (nullable): a new #LrgTextBaker, or %NULL on
 *   error with @error set.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTextBaker *
lrg_text_baker_new_from_file (const gchar  *font_path,
                               GError      **error);

/**
 * lrg_text_baker_new_for_font:
 * @font: a #GrlImageFont to use
 *
 * Creates a new #LrgTextBaker that holds a reference to an already-loaded
 * #GrlImageFont.  This is useful when the font is shared or was loaded from
 * memory.
 *
 * Returns: (transfer full): a new #LrgTextBaker
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTextBaker *
lrg_text_baker_new_for_font (GrlImageFont *font);

/* ==========================================================================
 * Font Access
 * ========================================================================== */

/**
 * lrg_text_baker_get_font:
 * @self: an #LrgTextBaker
 *
 * Returns the #GrlImageFont held by this baker.
 *
 * Returns: (transfer none): the underlying #GrlImageFont
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlImageFont *
lrg_text_baker_get_font (LrgTextBaker *self);

/* ==========================================================================
 * Measurement
 * ========================================================================== */

/**
 * lrg_text_baker_measure:
 * @self: an #LrgTextBaker
 * @text: the UTF-8 text to measure
 * @px_size: the desired font size in pixels (must be > 0)
 * @out_width: (out) (nullable): return location for the pixel width
 * @out_height: (out) (nullable): return location for the pixel height
 *
 * Measures the bounding box of @text rendered at @px_size.  Multi-line
 * text (containing `\n`) is supported; the returned height covers all
 * lines.
 *
 * Returns: %TRUE on success, %FALSE if @text is empty or @px_size <= 0.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_text_baker_measure (LrgTextBaker *self,
                        const gchar  *text,
                        gfloat        px_size,
                        gint         *out_width,
                        gint         *out_height);

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/**
 * lrg_text_baker_render_to_image:
 * @self: an #LrgTextBaker
 * @text: the UTF-8 text to render (may contain `\n` for multiple lines)
 * @px_size: the desired font size in pixels (must be > 0)
 * @color: the foreground text colour
 * @bg_or_null: (nullable): background colour to fill the image with, or
 *   %NULL for a transparent (RGBA) background
 *
 * Rasterizes @text into a tightly-sized RGBA #GrlImage.
 *
 * The image dimensions are determined by lrg_text_baker_measure().
 * Anti-aliasing is enabled on the output image.  The text is drawn with its
 * top-left glyph origin at (0, 0); the ascender of the first line is
 * therefore at y = 0 within the image.
 *
 * Text is composited using SRC_OVER onto the background.
 *
 * Returns: (transfer full) (nullable): a new #GrlImage containing the
 *   rasterized text, or %NULL if @text is empty, @px_size <= 0, or image
 *   allocation fails.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlImage *
lrg_text_baker_render_to_image (LrgTextBaker    *self,
                                const gchar     *text,
                                gfloat           px_size,
                                const GrlColor  *color,
                                const GrlColor  *bg_or_null);

/**
 * lrg_text_baker_render_to_texture:
 * @self: an #LrgTextBaker
 * @text: the UTF-8 text to render
 * @px_size: the desired font size in pixels (must be > 0)
 * @color: the foreground text colour
 * @bg_or_null: (nullable): background colour, or %NULL for transparent
 *
 * Convenience wrapper that calls lrg_text_baker_render_to_image() and
 * uploads the result to a GPU #GrlTexture via grl_texture_new_from_image().
 *
 * A display / GL context must be available at call time for the texture
 * upload step.
 *
 * Returns: (transfer full) (nullable): a new #GrlTexture, or %NULL on
 *   failure.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlTexture *
lrg_text_baker_render_to_texture (LrgTextBaker    *self,
                                  const gchar     *text,
                                  gfloat           px_size,
                                  const GrlColor  *color,
                                  const GrlColor  *bg_or_null);

/* ==========================================================================
 * Glyph Atlas
 * ========================================================================== */

/**
 * lrg_text_baker_bake_atlas:
 * @self: an #LrgTextBaker
 * @codepoints_utf8: a UTF-8 string whose unique codepoints define the
 *   glyph set to bake into the atlas
 * @px_size: the desired font size in pixels (must be > 0)
 * @color: the foreground glyph colour
 * @padding: number of transparent pixels to insert between glyphs
 * @out_regions: (out) (array length=out_n) (transfer full) (nullable):
 *   return location for an array of #GrlRectangle values describing each
 *   glyph's pixel rect in the atlas image, in the same order as the
 *   codepoints encountered in @codepoints_utf8 (after deduplication).
 *   Free the array with g_free() when no longer needed.  Set to %NULL if
 *   the caller does not need the region data.
 * @out_n: (out) (nullable): return location for the number of entries in
 *   @out_regions.  Set to %NULL if not needed.
 *
 * Renders every unique codepoint found in @codepoints_utf8 into a single
 * shelf-packed atlas #GrlImage.
 *
 * Codepoints for which the font has no glyph (as reported by
 * grl_image_font_has_glyph()) are silently skipped and do not appear in
 * @out_regions.
 *
 * The atlas is packed left-to-right in rows of up to
 * %LRG_TEXT_BAKER_ATLAS_MAX_WIDTH pixels; a new row is started whenever
 * the next glyph would overflow the current row.
 *
 * Returns: (transfer full) (nullable): a new #GrlImage containing all
 *   baked glyphs, or %NULL if no renderable codepoints were found or an
 *   error occurred.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlImage *
lrg_text_baker_bake_atlas (LrgTextBaker   *self,
                           const gchar    *codepoints_utf8,
                           gfloat          px_size,
                           const GrlColor *color,
                           gint            padding,
                           GrlRectangle  **out_regions,
                           guint          *out_n);

/**
 * LRG_TEXT_BAKER_ATLAS_MAX_WIDTH:
 *
 * Maximum width in pixels of a generated glyph atlas image row.
 * Rows wrap to the next shelf when the cursor would exceed this limit.
 *
 * Since: 1.0
 */
#define LRG_TEXT_BAKER_ATLAS_MAX_WIDTH (1024)

G_END_DECLS
