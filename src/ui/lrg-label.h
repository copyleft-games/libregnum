/* lrg-label.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Simple text display widget.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_LABEL (lrg_label_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLabel, lrg_label, LRG, LABEL, LrgWidget)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_label_new:
 * @text: (nullable): the label text
 *
 * Creates a new label widget.
 *
 * Returns: (transfer full): A new #LrgLabel
 */
LRG_AVAILABLE_IN_ALL
LrgLabel * lrg_label_new (const gchar *text);

/* ==========================================================================
 * Text
 * ========================================================================== */

/**
 * lrg_label_get_text:
 * @self: an #LrgLabel
 *
 * Gets the label's text.
 *
 * Returns: (transfer none) (nullable): The text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_label_get_text (LrgLabel *self);

/**
 * lrg_label_set_text:
 * @self: an #LrgLabel
 * @text: (nullable): the text to display
 *
 * Sets the label's text.
 */
LRG_AVAILABLE_IN_ALL
void lrg_label_set_text (LrgLabel    *self,
                         const gchar *text);

/* ==========================================================================
 * Font
 * ========================================================================== */

/**
 * lrg_label_get_font:
 * @self: an #LrgLabel
 *
 * Gets the label's font.
 *
 * Returns: (transfer none) (nullable): The font, or %NULL for default
 */
LRG_AVAILABLE_IN_ALL
GrlFont * lrg_label_get_font (LrgLabel *self);

/**
 * lrg_label_set_font:
 * @self: an #LrgLabel
 * @font: (nullable): the font to use, or %NULL for default
 *
 * Sets the label's font.
 */
LRG_AVAILABLE_IN_ALL
void lrg_label_set_font (LrgLabel *self,
                         GrlFont  *font);

/**
 * lrg_label_get_font_size:
 * @self: an #LrgLabel
 *
 * Gets the label's font size.
 *
 * Returns: The font size in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_label_get_font_size (LrgLabel *self);

/**
 * lrg_label_set_font_size:
 * @self: an #LrgLabel
 * @size: the font size in pixels
 *
 * Sets the label's font size.
 */
LRG_AVAILABLE_IN_ALL
void lrg_label_set_font_size (LrgLabel *self,
                              gfloat    size);

/* ==========================================================================
 * Appearance
 * ========================================================================== */

/**
 * lrg_label_get_color:
 * @self: an #LrgLabel
 *
 * Gets the label's text color.
 *
 * Returns: (transfer none): The color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_label_get_color (LrgLabel *self);

/**
 * lrg_label_set_color:
 * @self: an #LrgLabel
 * @color: the text color
 *
 * Sets the label's text color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_label_set_color (LrgLabel       *self,
                          const GrlColor *color);

/**
 * lrg_label_get_alignment:
 * @self: an #LrgLabel
 *
 * Gets the label's text alignment.
 *
 * Returns: The alignment
 */
LRG_AVAILABLE_IN_ALL
LrgTextAlignment lrg_label_get_alignment (LrgLabel *self);

/**
 * lrg_label_set_alignment:
 * @self: an #LrgLabel
 * @alignment: the text alignment
 *
 * Sets the label's text alignment.
 */
LRG_AVAILABLE_IN_ALL
void lrg_label_set_alignment (LrgLabel         *self,
                              LrgTextAlignment  alignment);

G_END_DECLS
