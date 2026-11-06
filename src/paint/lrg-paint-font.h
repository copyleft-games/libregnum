/* lrg-paint-font.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * SDF font atlases, font fallback chains and bidirectional text.
 *
 * An #LrgPaintFont is a TrueType/OpenType face whose glyphs are turned
 * into signed distance fields on demand and packed into a shared,
 * growable atlas. The paint shader draws each glyph as one instance
 * (%LRG_PAINT_PRIM_GLYPH) with screen-space anti-aliasing, so text is
 * crisp at any scale and batches with every other UI shape.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-paint-types.h"
#include "lrg-paint-list.h"

G_BEGIN_DECLS

/**
 * LRG_PAINT_FONT_DEFAULT_BASE_SIZE:
 *
 * Pixel height (ascent minus descent) glyph distance fields are
 * generated at unless another base size is requested.
 */
#define LRG_PAINT_FONT_DEFAULT_BASE_SIZE (48)

/**
 * LRG_PAINT_FONT_SDF_SPREAD:
 *
 * Reach of the distance field around each glyph, in base-size pixels.
 * Outline plus glow (or shadow softness) cannot extend further than
 * this, scaled to the drawn size.
 */
#define LRG_PAINT_FONT_SDF_SPREAD (10)

#define LRG_TYPE_PAINT_FONT (lrg_paint_font_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPaintFont, lrg_paint_font, LRG, PAINT_FONT, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_paint_font_new_from_file:
 * @path: a TTF, OTF or TTC file
 * @error: return location for a #GError
 *
 * Opens a font file. The file is memory-mapped and its tables parsed
 * now; glyphs are rasterised only when first drawn. For a collection
 * (TTC) the first face is used, see lrg_paint_font_new_from_file_index().
 * Works without a window.
 *
 * Returns: (transfer full) (nullable): the font, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgPaintFont * lrg_paint_font_new_from_file (const gchar  *path,
                                             GError      **error);

/**
 * lrg_paint_font_new_from_file_index:
 * @path: a TTF, OTF or TTC file
 * @face_index: face of a collection, 0 for plain font files
 * @base_size: pixel size distance fields are generated at, 0 for
 *   %LRG_PAINT_FONT_DEFAULT_BASE_SIZE
 * @error: return location for a #GError
 *
 * Like lrg_paint_font_new_from_file() with a face index and base size.
 * Larger base sizes keep sharper corners when text is drawn very large
 * at the cost of atlas space.
 *
 * Returns: (transfer full) (nullable): the font, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgPaintFont * lrg_paint_font_new_from_file_index (const gchar  *path,
                                                   gint          face_index,
                                                   gint          base_size,
                                                   GError      **error);

/**
 * lrg_paint_font_new_from_bytes:
 * @data: the contents of a TTF, OTF or TTC file
 * @error: return location for a #GError
 *
 * Opens a font held in memory (for example a GResource). @data is
 * referenced, not copied.
 *
 * Returns: (transfer full) (nullable): the font, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgPaintFont * lrg_paint_font_new_from_bytes (GBytes  *data,
                                              GError **error);

/**
 * lrg_paint_font_new_for_family:
 * @family: a family name such as "Noto Sans" or a fontconfig pattern
 * @error: return location for a #GError
 *
 * Opens the installed font best matching @family, found through
 * fontconfig when libfontconfig is available at run time and otherwise
 * through a list of well-known paths. See lrg_paint_font_find_file().
 *
 * Returns: (transfer full) (nullable): the font, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgPaintFont * lrg_paint_font_new_for_family (const gchar  *family,
                                              GError      **error);

/**
 * lrg_paint_font_find_file:
 * @family: a family name ("Noto Sans", "DejaVu Sans", "sans-serif",
 *   "Noto Emoji", ...)
 * @face_index: (out) (optional): face index inside the file (TTC)
 *
 * Locates an installed font file. libfontconfig is loaded with
 * #GModule when present (no build dependency); without it a built-in
 * table of Fedora/Debian/Arch paths is searched. Colour-only emoji
 * fonts (COLR/CBDT) are never returned, since only outline glyphs can
 * become distance fields.
 *
 * Returns: (transfer full) (nullable): the file path, or %NULL
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_paint_font_find_file (const gchar *family,
                                  gint        *face_index);

/**
 * lrg_paint_font_get_system_default:
 *
 * The shared system UI font: the fontconfig "sans-serif" match (or the
 * first of Noto Sans, DejaVu Sans, Adwaita Sans, Cantarell, Liberation
 * Sans found on disk) with lrg_paint_font_add_system_fallbacks()
 * applied. Created on first call and kept for the process; do not add
 * fallbacks to it, create your own font instead.
 *
 * Returns: (transfer none) (nullable): the font, or %NULL when no
 *   usable font is installed (text then uses raylib's bitmap font)
 */
LRG_AVAILABLE_IN_ALL
LrgPaintFont * lrg_paint_font_get_system_default (void);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_paint_font_get_path:
 * @self: an #LrgPaintFont
 *
 * Returns: (nullable): the file the font was opened from, %NULL for
 *   fonts made from bytes
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_paint_font_get_path (LrgPaintFont *self);

/**
 * lrg_paint_font_get_base_size:
 * @self: an #LrgPaintFont
 *
 * Returns: the pixel size the glyph distance fields are generated at
 */
LRG_AVAILABLE_IN_ALL
gint lrg_paint_font_get_base_size (LrgPaintFont *self);

/**
 * lrg_paint_font_get_units_per_em:
 * @self: an #LrgPaintFont
 *
 * Returns: design units per em of the face (from its `head` table)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_paint_font_get_units_per_em (LrgPaintFont *self);

/**
 * lrg_paint_font_has_glyph:
 * @self: an #LrgPaintFont
 * @codepoint: a Unicode codepoint
 *
 * Whether this face itself (not its fallbacks) maps @codepoint to a
 * glyph. Reads the `cmap` table only; nothing is rasterised.
 *
 * Returns: %TRUE when the face has the glyph
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_paint_font_has_glyph (LrgPaintFont *self,
                                   gunichar      codepoint);

/* ==========================================================================
 * Fallback chain
 * ========================================================================== */

/**
 * lrg_paint_font_add_fallback:
 * @self: an #LrgPaintFont
 * @fallback: a font tried, in order of addition, for codepoints @self
 *   lacks
 *
 * Appends @fallback to the chain. Fallbacks of @fallback itself are not
 * consulted. Adding @self or a font already in the chain does nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_font_add_fallback (LrgPaintFont *self,
                                  LrgPaintFont *fallback);

/**
 * lrg_paint_font_clear_fallbacks:
 * @self: an #LrgPaintFont
 *
 * Empties the fallback chain.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_font_clear_fallbacks (LrgPaintFont *self);

/**
 * lrg_paint_font_get_fallbacks:
 * @self: an #LrgPaintFont
 *
 * Returns: (transfer none) (element-type LrgPaintFont): the fallback
 *   chain, in lookup order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_paint_font_get_fallbacks (LrgPaintFont *self);

/**
 * lrg_paint_font_add_system_fallbacks:
 * @self: an #LrgPaintFont
 *
 * Appends whichever of these are installed: DejaVu Sans (wide symbol,
 * Hebrew and Arabic presentation-form coverage), Noto Sans Symbols 2,
 * Noto Sans Symbols, Noto Sans Arabic, Noto Sans Hebrew, the
 * monochrome Noto Emoji, Noto Sans CJK and Symbola. Fallback faces
 * are shared between fonts and their files are only opened when a
 * codepoint first needs them.
 *
 * Returns: the number of fonts added
 */
LRG_AVAILABLE_IN_ALL
guint lrg_paint_font_add_system_fallbacks (LrgPaintFont *self);

/**
 * lrg_paint_font_resolve:
 * @self: an #LrgPaintFont
 * @codepoint: a Unicode codepoint
 *
 * Picks the font that draws @codepoint: @self when it has the glyph,
 * else the first fallback that does. Results are cached.
 *
 * Returns: (transfer none) (nullable): the font, or %NULL when no font
 *   in the chain has the glyph (it is drawn as an empty box)
 */
LRG_AVAILABLE_IN_ALL
LrgPaintFont * lrg_paint_font_resolve (LrgPaintFont *self,
                                       gunichar      codepoint);

/* ==========================================================================
 * Metrics
 * ========================================================================== */

/**
 * lrg_paint_font_get_ascent:
 * @self: an #LrgPaintFont
 * @size: font size (ascent plus descent, as raylib font sizes)
 *
 * Returns: distance from the top of a line to the baseline at @size
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_paint_font_get_ascent (LrgPaintFont *self,
                                  gfloat        size);

/**
 * lrg_paint_font_get_descent:
 * @self: an #LrgPaintFont
 * @size: font size
 *
 * Returns: distance from the baseline down to the lowest descender at
 *   @size (positive)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_paint_font_get_descent (LrgPaintFont *self,
                                   gfloat        size);

/**
 * lrg_paint_font_get_line_height:
 * @self: an #LrgPaintFont
 * @size: font size
 *
 * The face's natural line spacing: ascent + descent + line gap. Text
 * drawn by the paint list advances lines by 1.2 × @size instead, the
 * factor every paint text path shares, so widget layout stays the same
 * whichever font is in use.
 *
 * Returns: the natural line height at @size
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_paint_font_get_line_height (LrgPaintFont *self,
                                       gfloat        size);

/**
 * lrg_paint_font_get_advance:
 * @self: an #LrgPaintFont
 * @codepoint: a Unicode codepoint
 * @size: font size
 *
 * Horizontal advance of @codepoint at @size through the fallback
 * chain, unrounded (from the `hmtx` table).
 *
 * Returns: the advance in canvas units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_paint_font_get_advance (LrgPaintFont *self,
                                   gunichar      codepoint,
                                   gfloat        size);

/**
 * lrg_paint_font_get_kerning:
 * @self: an #LrgPaintFont
 * @left: codepoint on the left (visual order)
 * @right: codepoint on the right
 * @size: font size
 *
 * Pair adjustment from the face's GPOS `kern` feature (pair positioning
 * lookups, formats 1 and 2) or, without GPOS, its legacy `kern` table.
 * Usually negative ("AV", "To").
 *
 * Returns: the adjustment in canvas units, 0 when the pair is not kerned
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_paint_font_get_kerning (LrgPaintFont *self,
                                   gunichar      left,
                                   gunichar      right,
                                   gfloat        size);

/**
 * lrg_paint_font_get_max_effect:
 * @self: an #LrgPaintFont
 * @size: font size
 *
 * Returns: the farthest outline + glow (or shadow softness) can reach
 *   outside a glyph at @size, in canvas units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_paint_font_get_max_effect (LrgPaintFont *self,
                                      gfloat        size);

/**
 * lrg_paint_font_measure_text:
 * @self: an #LrgPaintFont
 * @text: UTF-8 text; '\n' starts a new line
 * @length: byte length of @text, or -1 when nul-terminated
 * @size: font size
 * @spacing: extra space after every glyph
 * @direction: base direction for bidi reordering
 * @width: (out) (optional): width of the widest line
 * @height: (out) (optional): height of all lines
 *
 * Measures text exactly as lrg_paint_font_draw_text() lays it out:
 * same bidi reordering and Arabic shaping, fallback fonts, advances and
 * kerning. Needs no window and rasterises nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_font_measure_text (LrgPaintFont     *self,
                                  const gchar      *text,
                                  gssize            length,
                                  gfloat            size,
                                  gfloat            spacing,
                                  LrgTextDirection  direction,
                                  gfloat           *width,
                                  gfloat           *height);

/* ==========================================================================
 * Drawing
 * ========================================================================== */

/**
 * lrg_paint_font_draw_text:
 * @self: an #LrgPaintFont
 * @list: the paint list to record into
 * @text: UTF-8 text; '\n' starts a new line
 * @length: byte length of @text, or -1 when nul-terminated
 * @x: left edge
 * @y: top edge
 * @size: font size in canvas units
 * @style: (nullable): colour and effects, %NULL for opaque white
 *
 * Records @text as %LRG_PAINT_PRIM_GLYPH instances (plus one shadow
 * instance per glyph when the style has a shadow). Missing glyphs are
 * rasterised into the shared atlas first; with a window they are
 * uploaded at once, headless the instances carry texture 0 and the
 * layout can still be inspected. The list keeps a reference to @self
 * until cleared.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_font_draw_text (LrgPaintFont            *self,
                               LrgPaintList            *list,
                               const gchar             *text,
                               gssize                   length,
                               gfloat                   x,
                               gfloat                   y,
                               gfloat                   size,
                               const LrgPaintTextStyle *style);

/**
 * lrg_paint_font_prepare:
 * @self: an #LrgPaintFont
 * @text: UTF-8 text whose glyphs to rasterise
 *
 * Rasterises the glyphs @text needs (through the fallback chain) into
 * the atlas ahead of time, for example during a loading screen, so the
 * first frame that shows them does no distance-field work.
 *
 * Returns: the number of glyphs newly rasterised
 */
LRG_AVAILABLE_IN_ALL
guint lrg_paint_font_prepare (LrgPaintFont *self,
                              const gchar  *text);

/**
 * lrg_paint_font_get_n_glyphs:
 * @self: an #LrgPaintFont
 *
 * Returns: how many of this face's glyphs are rasterised in the atlas
 */
LRG_AVAILABLE_IN_ALL
guint lrg_paint_font_get_n_glyphs (LrgPaintFont *self);

/* ==========================================================================
 * Shared glyph atlas
 * ========================================================================== */

/**
 * lrg_paint_font_atlas_get_n_pages:
 *
 * Glyphs of every #LrgPaintFont share one atlas, so text in several
 * fonts (fallbacks included) still draws in one batch. A page starts at
 * 512×512 single-channel pixels and doubles up to 2048×2048 when full;
 * only then is another page (another texture) added.
 *
 * Returns: the number of atlas pages
 */
LRG_AVAILABLE_IN_ALL
guint lrg_paint_font_atlas_get_n_pages (void);

/**
 * lrg_paint_font_atlas_get_page_size:
 * @page: page index
 * @width: (out) (optional): width in pixels
 * @height: (out) (optional): height in pixels
 *
 * Returns: %TRUE when @page exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_paint_font_atlas_get_page_size (guint  page,
                                             gint  *width,
                                             gint  *height);

/**
 * lrg_paint_font_atlas_get_n_glyphs:
 *
 * Returns: glyphs rasterised into the atlas so far, all fonts together
 */
LRG_AVAILABLE_IN_ALL
guint lrg_paint_font_atlas_get_n_glyphs (void);

/* ==========================================================================
 * Defaults and GrlFont binding
 * ========================================================================== */

/**
 * lrg_paint_font_get_default:
 *
 * Returns: (transfer none) (nullable): the font lrg_paint_list_text()
 *   uses when given a %NULL #GrlFont, %NULL (raylib's bitmap font) until
 *   lrg_paint_font_set_default() is called
 */
LRG_AVAILABLE_IN_ALL
LrgPaintFont * lrg_paint_font_get_default (void);

/**
 * lrg_paint_font_set_default:
 * @font: (nullable): the font, or %NULL for raylib's bitmap font
 *
 * Sets the font %NULL-font text draws and measures with.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_font_set_default (LrgPaintFont *font);

/**
 * lrg_paint_font_bind:
 * @self: (nullable): an #LrgPaintFont, or %NULL to unbind
 * @font: a #GrlFont
 *
 * Makes lrg_paint_list_text() and lrg_paint_measure_text() use @self
 * whenever they are given @font. This is how existing widgets, which
 * pass the theme's #GrlFont, get SDF text: #LrgTheme binds its SDF font
 * to its UI font. @font keeps a reference to @self.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_font_bind (LrgPaintFont *self,
                          GrlFont      *font);

/**
 * lrg_paint_font_lookup:
 * @font: (nullable): a #GrlFont
 *
 * Returns: (transfer none) (nullable): the #LrgPaintFont bound to
 *   @font, or for %NULL the default font; %NULL means bitmap text
 */
LRG_AVAILABLE_IN_ALL
LrgPaintFont * lrg_paint_font_lookup (GrlFont *font);

/* ==========================================================================
 * Bidirectional text
 * ========================================================================== */

/**
 * lrg_paint_text_has_bidi:
 *
 * Returns: %TRUE when libregnum was built with fribidi, so right-to-left
 *   text is reordered and Arabic is shaped
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_paint_text_has_bidi (void);

/**
 * lrg_paint_text_reorder:
 * @text: one line of UTF-8 text in logical (typing) order
 * @length: byte length, or -1 when nul-terminated
 * @direction: base direction; %LRG_TEXT_DIRECTION_AUTO uses the first
 *   strong character (left-to-right when there is none)
 * @resolved: (out) (optional): the resolved base direction
 *
 * The text in visual (left-to-right drawing) order, as the paint text
 * functions lay it out. With fribidi this runs the Unicode
 * Bidirectional Algorithm (embedding levels, bracket pairs, mirroring)
 * and Arabic joining and shaping to presentation forms; without it the
 * text is returned unchanged and only the direction is resolved.
 *
 * Returns: (transfer full): the visual-order text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_paint_text_reorder (const gchar      *text,
                                gssize            length,
                                LrgTextDirection  direction,
                                LrgTextDirection *resolved);

G_END_DECLS
