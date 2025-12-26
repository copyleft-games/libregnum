/* lrg-text2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 2D text shape.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-shape2d.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEXT2D (lrg_text2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgText2D, lrg_text2d, LRG, TEXT2D, LrgShape2D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_text2d_new:
 *
 * Creates a new empty text at the origin.
 *
 * Returns: (transfer full): A new #LrgText2D
 */
LRG_AVAILABLE_IN_ALL
LrgText2D * lrg_text2d_new (void);

/**
 * lrg_text2d_new_with_text:
 * @text: the text to display
 *
 * Creates a new text shape at the origin with specified text.
 *
 * Returns: (transfer full): A new #LrgText2D
 */
LRG_AVAILABLE_IN_ALL
LrgText2D * lrg_text2d_new_with_text (const gchar *text);

/**
 * lrg_text2d_new_at:
 * @x: X position
 * @y: Y position
 * @text: the text to display
 *
 * Creates a new text at the specified position.
 *
 * Returns: (transfer full): A new #LrgText2D
 */
LRG_AVAILABLE_IN_ALL
LrgText2D * lrg_text2d_new_at (gfloat       x,
                               gfloat       y,
                               const gchar *text);

/**
 * lrg_text2d_new_full:
 * @x: X position
 * @y: Y position
 * @text: the text to display
 * @font_size: the font size
 * @color: (transfer none): the text color
 *
 * Creates a new text with full configuration.
 *
 * Returns: (transfer full): A new #LrgText2D
 */
LRG_AVAILABLE_IN_ALL
LrgText2D * lrg_text2d_new_full (gfloat       x,
                                 gfloat       y,
                                 const gchar *text,
                                 gfloat       font_size,
                                 GrlColor    *color);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_text2d_get_text:
 * @self: an #LrgText2D
 *
 * Gets the text string.
 *
 * Returns: (transfer none): The text string
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_text2d_get_text (LrgText2D *self);

/**
 * lrg_text2d_set_text:
 * @self: an #LrgText2D
 * @text: the text to set
 *
 * Sets the text string.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text2d_set_text (LrgText2D   *self,
                          const gchar *text);

/**
 * lrg_text2d_get_font_size:
 * @self: an #LrgText2D
 *
 * Gets the font size.
 *
 * Returns: The font size
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_text2d_get_font_size (LrgText2D *self);

/**
 * lrg_text2d_set_font_size:
 * @self: an #LrgText2D
 * @font_size: the font size
 *
 * Sets the font size.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text2d_set_font_size (LrgText2D *self,
                               gfloat     font_size);

/**
 * lrg_text2d_get_spacing:
 * @self: an #LrgText2D
 *
 * Gets the character spacing.
 *
 * Returns: The character spacing
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_text2d_get_spacing (LrgText2D *self);

/**
 * lrg_text2d_set_spacing:
 * @self: an #LrgText2D
 * @spacing: the character spacing
 *
 * Sets the character spacing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text2d_set_spacing (LrgText2D *self,
                             gfloat     spacing);

/**
 * lrg_text2d_get_font:
 * @self: an #LrgText2D
 *
 * Gets the font.
 *
 * Returns: (transfer none) (nullable): The font, or %NULL for default
 */
LRG_AVAILABLE_IN_ALL
GrlFont * lrg_text2d_get_font (LrgText2D *self);

/**
 * lrg_text2d_set_font:
 * @self: an #LrgText2D
 * @font: (nullable) (transfer none): the font, or %NULL for default
 *
 * Sets the font.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text2d_set_font (LrgText2D *self,
                          GrlFont   *font);

G_END_DECLS
