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
