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

G_END_DECLS
