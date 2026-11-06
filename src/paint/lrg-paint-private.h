/* lrg-paint-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal paint entry points for backends that hold raw raylib fonts
 * (the immediate-mode GUI loads its fonts with LoadFontEx()).
 */

#pragma once

#include <raylib.h>
#include "lrg-paint-list.h"

G_BEGIN_DECLS

/*
 * _lrg_paint_list_text_font:
 * @self: an #LrgPaintList
 * @font: a raylib font (borrowed; must outlive the frame)
 * @text: UTF-8 text, @length bytes or nul-terminated when -1
 * @length: byte length, or -1
 * @x: left edge
 * @y: top edge
 * @size: font size
 * @spacing: extra space between characters
 * @color: text colour
 *
 * lrg_paint_list_text() for a raw raylib Font.
 */
void _lrg_paint_list_text_font (LrgPaintList   *self,
                                const Font     *font,
                                const gchar    *text,
                                gssize          length,
                                gfloat          x,
                                gfloat          y,
                                gfloat          size,
                                gfloat          spacing,
                                const GrlColor *color);

/*
 * _lrg_paint_list_glyph:
 * @self: an #LrgPaintList
 * @rect: x0, y0, x1, y1 of the glyph quad (distance field padding included)
 * @atlas: x0, y0, x1, y1 of the glyph's field in the atlas, in pixels
 * @texture: GL texture of the atlas page (0 headless)
 * @dist_scale: canvas units spanned by the field's half range (spread)
 * @softness: extra edge blur in canvas units (shadows), 0 for crisp
 * @dilate: grows the glyph by this many canvas units (shadow of an
 *   outlined glyph)
 * @outline: outline width in canvas units
 * @glow: glow radius in canvas units
 * @color: fill colour
 * @outline_color: (nullable): outline colour
 * @glow_color: (nullable): glow colour
 *
 * Records one %LRG_PAINT_PRIM_GLYPH instance with the current clip and
 * opacity. Used by #LrgPaintFont.
 */
void _lrg_paint_list_glyph (LrgPaintList   *self,
                            const gfloat   *rect,
                            const gfloat   *atlas,
                            guint           texture,
                            gfloat          dist_scale,
                            gfloat          softness,
                            gfloat          dilate,
                            gfloat          outline,
                            gfloat          glow,
                            const GrlColor *color,
                            const GrlColor *outline_color,
                            const GrlColor *glow_color);

/*
 * _lrg_paint_list_keep:
 * @self: an #LrgPaintList
 * @object: a #GObject to keep alive until the list is cleared
 */
void _lrg_paint_list_keep (LrgPaintList *self,
                           gpointer      object);

/*
 * _lrg_paint_instance_get_bounds:
 * @inst: an instance
 * @x0: (out): left
 * @y0: (out): top
 * @x1: (out): right
 * @y1: (out): bottom
 *
 * Box of every pixel @inst can touch (shape, anti-aliasing margin,
 * shadow blur, curve glow), cut to its clip.
 */
void _lrg_paint_instance_get_bounds (const LrgPaintInstance *inst,
                                     gfloat                 *x0,
                                     gfloat                 *y0,
                                     gfloat                 *x1,
                                     gfloat                 *y1);

/*
 * _lrg_paint_instance_get_backdrop_reach:
 * @inst: a blur or glass instance
 * @x0: (out): left
 * @y0: (out): top
 * @x1: (out): right
 * @y1: (out): bottom
 *
 * Box of the backdrop @inst reads: its shape grown by the blur and
 * refraction reach.
 */
void _lrg_paint_instance_get_backdrop_reach (const LrgPaintInstance *inst,
                                             gfloat                 *x0,
                                             gfloat                 *y0,
                                             gfloat                 *x1,
                                             gfloat                 *y1);

/*
 * _lrg_paint_effect_get_serial:
 *
 * Returns: a counter bumped by every lrg_paint_effect_register(); the
 *   renderer rebuilds its user-effect shader variant when it changes
 */
guint _lrg_paint_effect_get_serial (void);

/*
 * _lrg_paint_effect_build_user_source:
 *
 * Returns: (transfer full) (nullable): GLSL defining every user effect
 *   and `vec4 lrg_user_effect (int id, LrgEffect fx)`, or %NULL when no
 *   user effect is registered
 */
gchar * _lrg_paint_effect_build_user_source (void);

G_END_DECLS
