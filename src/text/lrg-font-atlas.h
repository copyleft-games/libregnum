/* lrg-font-atlas.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFontAtlas - a growing glyph atlas baked from a TTF/OTF font, for
 * drawing codepoints a bitmap font lacks.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-text-baker.h"

G_BEGIN_DECLS

/**
 * LRG_FONT_ATLAS_ERROR:
 *
 * Error domain of #LrgFontAtlas.
 */
#define LRG_FONT_ATLAS_ERROR (lrg_font_atlas_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_font_atlas_error_quark (void);

/**
 * LrgFontAtlasError:
 * @LRG_FONT_ATLAS_ERROR_INVALID_SIZE: the pixel size is not positive
 * @LRG_FONT_ATLAS_ERROR_NO_FONT: no usable font was found
 *
 * Error codes of #LrgFontAtlas.
 */
typedef enum
{
    LRG_FONT_ATLAS_ERROR_INVALID_SIZE,
    LRG_FONT_ATLAS_ERROR_NO_FONT
} LrgFontAtlasError;

#define LRG_TYPE_FONT_ATLAS (lrg_font_atlas_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFontAtlas, lrg_font_atlas, LRG, FONT_ATLAS, GObject)

/**
 * lrg_font_atlas_new_from_file:
 * @font_path: path of a TTF or OTF font
 * @px_size: the em height glyphs are baked at, in pixels (> 0)
 * @error: return location for a #GError, or %NULL
 *
 * Creates an empty atlas over a font.  Glyphs are baked on the CPU (no
 * graphics context needed) as white pixels whose alpha is the glyph's
 * coverage; lrg_font_atlas_add_text() adds them.
 *
 * Returns: (transfer full) (nullable): a new #LrgFontAtlas, or %NULL
 *   with @error set (the font loader's error when the font does not load)
 */
LRG_AVAILABLE_IN_ALL
LrgFontAtlas *lrg_font_atlas_new_from_file (const gchar  *font_path,
                                              gfloat        px_size,
                                              GError      **error);

/**
 * lrg_font_atlas_new_for_baker:
 * @baker: an #LrgTextBaker holding the font
 * @px_size: the em height glyphs are baked at, in pixels (> 0)
 *
 * Creates an empty atlas over @baker's font.
 *
 * Returns: (transfer full): a new #LrgFontAtlas
 */
LRG_AVAILABLE_IN_ALL
LrgFontAtlas *lrg_font_atlas_new_for_baker (LrgTextBaker *baker,
                                              gfloat        px_size);

/**
 * lrg_font_atlas_find_system_font:
 * @family: (nullable): a family name or fontconfig pattern ("sans",
 *   "DejaVu Sans:style=Book"), or %NULL for the default sans-serif face
 * @needed: (nullable): UTF-8 text whose characters the font should
 *   cover, or %NULL
 *
 * Looks for an installed TTF/OTF font: first through fontconfig's
 * `fc-match` (asking for a face covering every character of @needed),
 * then by scanning the XDG font directories (`$XDG_DATA_HOME/fonts`,
 * `~/.fonts` and every `$XDG_DATA_DIRS/fonts`) for common free faces
 * (DejaVu Sans, Noto Sans, Liberation Sans, GNU FreeSans, ...).  A file
 * is only returned when it loads and has a glyph for every character of
 * @needed.
 *
 * Returns: (transfer full) (nullable) (type filename): the font file, or
 *   %NULL when nothing suitable is installed
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_font_atlas_find_system_font (const gchar *family,
                                         const gchar *needed);

/**
 * lrg_font_atlas_get_pixel_size:
 * @self: an #LrgFontAtlas
 *
 * Returns: the em height glyphs are baked at
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_font_atlas_get_pixel_size (LrgFontAtlas *self);

/**
 * lrg_font_atlas_set_alpha_threshold:
 * @self: an #LrgFontAtlas
 * @threshold: 0 keeps antialiased coverage; 1-255 makes every pixel
 *   whose coverage is at least @threshold opaque and the rest clear
 *
 * Sets how coverage turns into alpha.  A threshold suits pixel-art
 * interfaces drawn with nearest-neighbour scaling.  Takes effect on the
 * next rebuild (the atlas is rebuilt when it changes).
 */
LRG_AVAILABLE_IN_ALL
void lrg_font_atlas_set_alpha_threshold (LrgFontAtlas *self,
                                          guint8         threshold);

/**
 * lrg_font_atlas_get_alpha_threshold:
 * @self: an #LrgFontAtlas
 *
 * Returns: the alpha threshold (0 = antialiased)
 */
LRG_AVAILABLE_IN_ALL
guint8 lrg_font_atlas_get_alpha_threshold (LrgFontAtlas *self);

/**
 * lrg_font_atlas_font_has_glyph:
 * @self: an #LrgFontAtlas
 * @codepoint: a Unicode codepoint
 *
 * Returns: %TRUE when the font has a glyph for @codepoint (baked or not)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_font_atlas_font_has_glyph (LrgFontAtlas *self,
                                         gunichar       codepoint);

/**
 * lrg_font_atlas_add_text:
 * @self: an #LrgFontAtlas
 * @text: UTF-8 text
 *
 * Adds every character of @text the font has and the atlas lacks
 * (spaces, control characters and invalid bytes are skipped).  The image
 * and texture are rebuilt lazily on the next lrg_font_atlas_get_image()
 * / lrg_font_atlas_get_texture() / lrg_font_atlas_lookup().
 *
 * Returns: how many glyphs were added
 */
LRG_AVAILABLE_IN_ALL
guint lrg_font_atlas_add_text (LrgFontAtlas *self,
                                const gchar   *text);

/**
 * lrg_font_atlas_has_glyph:
 * @self: an #LrgFontAtlas
 * @codepoint: a Unicode codepoint
 *
 * Returns: %TRUE when @codepoint is in the atlas
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_font_atlas_has_glyph (LrgFontAtlas *self,
                                    gunichar       codepoint);

/**
 * lrg_font_atlas_lookup:
 * @self: an #LrgFontAtlas
 * @codepoint: a Unicode codepoint
 * @out_rect: (out caller-allocates): the glyph's cell in the atlas image;
 *   its width is the glyph's advance, its height the line height, and
 *   the baseline lies lrg_font_atlas_get_ascent() below its top
 *
 * Finds a baked glyph, rebuilding the atlas first when glyphs were added.
 *
 * Returns: %TRUE when @codepoint is in the atlas
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_font_atlas_lookup (LrgFontAtlas *self,
                                 gunichar       codepoint,
                                 GrlRectangle  *out_rect);

/**
 * lrg_font_atlas_get_glyph_count:
 * @self: an #LrgFontAtlas
 *
 * Returns: how many glyphs the atlas holds
 */
LRG_AVAILABLE_IN_ALL
guint lrg_font_atlas_get_glyph_count (LrgFontAtlas *self);

/**
 * lrg_font_atlas_get_line_height:
 * @self: an #LrgFontAtlas
 *
 * Returns: the font's line height at the atlas size, in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_font_atlas_get_line_height (LrgFontAtlas *self);

/**
 * lrg_font_atlas_get_ascent:
 * @self: an #LrgFontAtlas
 *
 * Returns: the distance from the top of a glyph cell to the baseline
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_font_atlas_get_ascent (LrgFontAtlas *self);

/**
 * lrg_font_atlas_get_image:
 * @self: an #LrgFontAtlas
 *
 * Gets the atlas image (RGBA8, white with coverage alpha), rebuilding it
 * when glyphs were added since the last build.
 *
 * Returns: (transfer none) (nullable): the image, or %NULL while the
 *   atlas is empty
 */
LRG_AVAILABLE_IN_ALL
GrlImage *lrg_font_atlas_get_image (LrgFontAtlas *self);

/**
 * lrg_font_atlas_get_texture:
 * @self: an #LrgFontAtlas
 *
 * Gets the atlas as a texture (point filtered), uploading it when the
 * image changed.  Needs a graphics context.
 *
 * Returns: (transfer none) (nullable): the texture, or %NULL while the
 *   atlas is empty
 */
LRG_AVAILABLE_IN_ALL
GrlTexture *lrg_font_atlas_get_texture (LrgFontAtlas *self);

/**
 * lrg_font_atlas_release_texture:
 * @self: an #LrgFontAtlas
 *
 * Drops the GPU texture (before the graphics context goes away); the
 * next lrg_font_atlas_get_texture() uploads it again.
 */
LRG_AVAILABLE_IN_ALL
void lrg_font_atlas_release_texture (LrgFontAtlas *self);

G_END_DECLS
