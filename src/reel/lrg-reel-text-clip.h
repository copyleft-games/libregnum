/* lrg-reel-text-clip.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelTextClip - a reel clip that renders a text block onto the frame.
 *
 * Supports both a headless-safe embedded bitmap font (the default, zero
 * external dependencies) and an optional TTF/OTF #GrlImageFont for display
 * quality output.  Text may be left-, center-, or right-aligned, optionally
 * word-wrapped to a maximum pixel width, and decorated with a drop shadow.
 *
 * Layout utilities lrg_reel_measure_text() and lrg_reel_fit_text() are
 * provided as free functions in this same translation unit.
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

#define LRG_TYPE_REEL_TEXT_CLIP (lrg_reel_text_clip_get_type ())

/**
 * LrgReelTextClip:
 *
 * A #LrgReelClip subclass that renders a block of (optionally word-wrapped)
 * text at a fixed position within the frame.
 *
 * When no #GrlImageFont is set the clip uses the built-in 8×8 bitmap font,
 * which works fully headless (no GL context required).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelTextClip, lrg_reel_text_clip,
                      LRG, REEL_TEXT_CLIP, LrgReelClip)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_reel_text_clip_new:
 * @text: (nullable): UTF-8 text to display, or %NULL for an empty clip.
 *
 * Creates a new #LrgReelTextClip using the built-in 8×8 bitmap font.  The
 * clip works headless (no GL context / font file required).
 *
 * Returns: (transfer full): a new #LrgReelTextClip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelTextClip *
lrg_reel_text_clip_new (const gchar *text);

/**
 * lrg_reel_text_clip_new_with_font:
 * @text: (nullable): UTF-8 text to display, or %NULL for an empty clip.
 * @font: (nullable): a #GrlImageFont to use for rendering, or %NULL to fall
 *   back to the bitmap font.
 *
 * Creates a new #LrgReelTextClip that renders with @font when non-%NULL.
 * The clip takes a reference to @font; the caller may unref it after this
 * call.
 *
 * Returns: (transfer full): a new #LrgReelTextClip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelTextClip *
lrg_reel_text_clip_new_with_font (const gchar  *text,
                                  GrlImageFont *font);

/**
 * lrg_reel_text_clip_set_font_from_file:
 * @self: an #LrgReelTextClip.
 * @path: (type filename): path to a TTF or OTF font file.
 * @error: (nullable): return location for error, or %NULL.
 *
 * Loads a font from @path and sets it as the active font for this clip,
 * replacing any previously set font.
 *
 * Returns: %TRUE on success; %FALSE on error (see @error).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_text_clip_set_font_from_file (LrgReelTextClip  *self,
                                       const gchar      *path,
                                       GError          **error);

/* ==========================================================================
 * Text
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_text:
 * @self: an #LrgReelTextClip.
 *
 * Returns: (transfer none) (nullable): the current text string, owned by the clip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_reel_text_clip_get_text (LrgReelTextClip *self);

/**
 * lrg_reel_text_clip_set_text:
 * @self: an #LrgReelTextClip.
 * @text: (nullable): new UTF-8 text string, or %NULL to clear.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_set_text (LrgReelTextClip *self,
                             const gchar     *text);

/* ==========================================================================
 * Font
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_font:
 * @self: an #LrgReelTextClip.
 *
 * Returns: (transfer none) (nullable): the current #GrlImageFont, or %NULL if
 *   using the bitmap font.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlImageFont *
lrg_reel_text_clip_get_font (LrgReelTextClip *self);

/**
 * lrg_reel_text_clip_set_font:
 * @self: an #LrgReelTextClip.
 * @font: (nullable): a #GrlImageFont, or %NULL to switch back to the bitmap font.
 *
 * The clip takes a reference to @font; the caller retains its own reference.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_set_font (LrgReelTextClip *self,
                             GrlImageFont    *font);

/* ==========================================================================
 * Font size
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_font_size:
 * @self: an #LrgReelTextClip.
 *
 * Returns: the font size in pixels (default 32).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_text_clip_get_font_size (LrgReelTextClip *self);

/**
 * lrg_reel_text_clip_set_font_size:
 * @self: an #LrgReelTextClip.
 * @font_size: font size in pixels (must be > 0).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_set_font_size (LrgReelTextClip *self,
                                  gdouble          font_size);

/* ==========================================================================
 * Color
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_color:
 * @self: an #LrgReelTextClip.
 * @out_color: (out caller-allocates): return location for the text color.
 *
 * Copies the current text color into @out_color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_get_color (LrgReelTextClip *self,
                               GrlColor        *out_color);

/**
 * lrg_reel_text_clip_set_color:
 * @self: an #LrgReelTextClip.
 * @color: new text color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_set_color (LrgReelTextClip *self,
                               const GrlColor  *color);

/* ==========================================================================
 * Alignment
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_align:
 * @self: an #LrgReelTextClip.
 *
 * Returns: the current #LrgReelTextAlign value (default %LRG_REEL_TEXT_ALIGN_LEFT).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelTextAlign
lrg_reel_text_clip_get_align (LrgReelTextClip *self);

/**
 * lrg_reel_text_clip_set_align:
 * @self: an #LrgReelTextClip.
 * @align: the desired text alignment.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_set_align (LrgReelTextClip  *self,
                               LrgReelTextAlign  align);

/* ==========================================================================
 * Position
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_text_x:
 * @self: an #LrgReelTextClip.
 *
 * Returns: the X pixel coordinate of the text block's top-left corner (default 0).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_text_clip_get_text_x (LrgReelTextClip *self);

/**
 * lrg_reel_text_clip_set_text_x:
 * @self: an #LrgReelTextClip.
 * @text_x: X coordinate in frame pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_set_text_x (LrgReelTextClip *self,
                                gint             text_x);

/**
 * lrg_reel_text_clip_get_text_y:
 * @self: an #LrgReelTextClip.
 *
 * Returns: the Y pixel coordinate of the text block's top edge (default 0).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_text_clip_get_text_y (LrgReelTextClip *self);

/**
 * lrg_reel_text_clip_set_text_y:
 * @self: an #LrgReelTextClip.
 * @text_y: Y coordinate in frame pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_set_text_y (LrgReelTextClip *self,
                                gint             text_y);

/* ==========================================================================
 * Word-wrap width
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_max_width:
 * @self: an #LrgReelTextClip.
 *
 * Returns: the word-wrap column in pixels, or 0 if wrapping is disabled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_text_clip_get_max_width (LrgReelTextClip *self);

/**
 * lrg_reel_text_clip_set_max_width:
 * @self: an #LrgReelTextClip.
 * @max_width: maximum line width in pixels, or 0 to disable word-wrapping.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_set_max_width (LrgReelTextClip *self,
                                   gint             max_width);

/* ==========================================================================
 * Line height
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_line_height:
 * @self: an #LrgReelTextClip.
 *
 * Returns: the line-height multiplier applied to the font's natural line
 *   height (default 1.2).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_text_clip_get_line_height (LrgReelTextClip *self);

/**
 * lrg_reel_text_clip_set_line_height:
 * @self: an #LrgReelTextClip.
 * @line_height: line-height multiplier (must be > 0; default 1.2).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_set_line_height (LrgReelTextClip *self,
                                    gdouble          line_height);

/* ==========================================================================
 * Shadow
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_shadow:
 * @self: an #LrgReelTextClip.
 *
 * Returns: %TRUE if drop-shadow rendering is enabled (default %FALSE).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_text_clip_get_shadow (LrgReelTextClip *self);

/**
 * lrg_reel_text_clip_set_shadow:
 * @self: an #LrgReelTextClip.
 * @shadow: %TRUE to enable drop-shadow rendering.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_set_shadow (LrgReelTextClip *self,
                                gboolean         shadow);

/**
 * lrg_reel_text_clip_get_shadow_color:
 * @self: an #LrgReelTextClip.
 * @out_color: (out caller-allocates): return location for the shadow color.
 *
 * Copies the current shadow color into @out_color (default opaque black).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_get_shadow_color (LrgReelTextClip *self,
                                      GrlColor        *out_color);

/**
 * lrg_reel_text_clip_set_shadow_color:
 * @self: an #LrgReelTextClip.
 * @color: the new shadow color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_set_shadow_color (LrgReelTextClip *self,
                                      const GrlColor  *color);

/**
 * lrg_reel_text_clip_get_shadow_offset:
 * @self: an #LrgReelTextClip.
 * @out_dx: (out) (optional): return location for the horizontal offset in pixels.
 * @out_dy: (out) (optional): return location for the vertical offset in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_get_shadow_offset (LrgReelTextClip *self,
                                       gint            *out_dx,
                                       gint            *out_dy);

/**
 * lrg_reel_text_clip_set_shadow_offset:
 * @self: an #LrgReelTextClip.
 * @dx: horizontal shadow offset in pixels (positive = right; default 2).
 * @dy: vertical shadow offset in pixels (positive = down; default 2).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_text_clip_set_shadow_offset (LrgReelTextClip *self,
                                       gint             dx,
                                       gint             dy);

/* ==========================================================================
 * Layout utilities (free functions)
 * ========================================================================== */

/**
 * lrg_reel_measure_text:
 * @font: (nullable): a #GrlImageFont to measure with, or %NULL for the
 *   bitmap font.
 * @text: (nullable): UTF-8 text to measure.
 * @font_size: font size in pixels.
 * @out_width: (out) (optional): return location for the measured width in
 *   pixels.
 * @out_height: (out) (optional): return location for the measured height in
 *   pixels.
 *
 * Measures the bounding box of @text when rendered at @font_size with @font.
 *
 * Multi-line text (lines separated by '\n') is measured as a whole: @out_width
 * receives the width of the widest line and @out_height receives n_lines times
 * the natural line height of @font at @font_size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_measure_text (GrlImageFont *font,
                       const gchar  *text,
                       gdouble       font_size,
                       gint         *out_width,
                       gint         *out_height);

/**
 * lrg_reel_fit_text:
 * @font: (nullable): a #GrlImageFont, or %NULL for the bitmap font.
 * @text: (nullable): UTF-8 text (single logical line; newlines are ignored
 *   for the purpose of fitting).
 * @box_width: available width in pixels (must be > 0).
 * @box_height: available height in pixels (must be > 0).
 * @max_font_size: upper bound for the returned size (must be >= 1).
 *
 * Returns the largest font size ≤ @max_font_size (and ≥ 1) at which the
 * single-line rendering of @text fits within @box_width × @box_height.
 *
 * The search is performed in 0.5-pixel increments.  If @text is %NULL or
 * empty the function returns @max_font_size unchanged.
 *
 * Returns: the largest fitting font size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_fit_text (GrlImageFont *font,
                   const gchar  *text,
                   gint          box_width,
                   gint          box_height,
                   gdouble       max_font_size);

G_END_DECLS
