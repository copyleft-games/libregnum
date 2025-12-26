/* lrg-button.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Clickable button widget with visual feedback.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_BUTTON (lrg_button_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgButton, lrg_button, LRG, BUTTON, LrgWidget)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_button_new:
 * @text: (nullable): the button text
 *
 * Creates a new button widget.
 *
 * Returns: (transfer full): A new #LrgButton
 */
LRG_AVAILABLE_IN_ALL
LrgButton * lrg_button_new (const gchar *text);

/* ==========================================================================
 * Text
 * ========================================================================== */

/**
 * lrg_button_get_text:
 * @self: an #LrgButton
 *
 * Gets the button's text.
 *
 * Returns: (transfer none) (nullable): The text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_button_get_text (LrgButton *self);

/**
 * lrg_button_set_text:
 * @self: an #LrgButton
 * @text: (nullable): the button text
 *
 * Sets the button's text.
 */
LRG_AVAILABLE_IN_ALL
void lrg_button_set_text (LrgButton   *self,
                          const gchar *text);

/* ==========================================================================
 * Colors
 * ========================================================================== */

/**
 * lrg_button_get_normal_color:
 * @self: an #LrgButton
 *
 * Gets the button's normal (idle) background color.
 *
 * Returns: (transfer none): The color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_button_get_normal_color (LrgButton *self);

/**
 * lrg_button_set_normal_color:
 * @self: an #LrgButton
 * @color: the color
 *
 * Sets the button's normal (idle) background color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_button_set_normal_color (LrgButton      *self,
                                  const GrlColor *color);

/**
 * lrg_button_get_hover_color:
 * @self: an #LrgButton
 *
 * Gets the button's hover background color.
 *
 * Returns: (transfer none): The color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_button_get_hover_color (LrgButton *self);

/**
 * lrg_button_set_hover_color:
 * @self: an #LrgButton
 * @color: the color
 *
 * Sets the button's hover background color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_button_set_hover_color (LrgButton      *self,
                                 const GrlColor *color);

/**
 * lrg_button_get_pressed_color:
 * @self: an #LrgButton
 *
 * Gets the button's pressed background color.
 *
 * Returns: (transfer none): The color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_button_get_pressed_color (LrgButton *self);

/**
 * lrg_button_set_pressed_color:
 * @self: an #LrgButton
 * @color: the color
 *
 * Sets the button's pressed background color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_button_set_pressed_color (LrgButton      *self,
                                   const GrlColor *color);

/**
 * lrg_button_get_text_color:
 * @self: an #LrgButton
 *
 * Gets the button's text color.
 *
 * Returns: (transfer none): The color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_button_get_text_color (LrgButton *self);

/**
 * lrg_button_set_text_color:
 * @self: an #LrgButton
 * @color: the color
 *
 * Sets the button's text color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_button_set_text_color (LrgButton      *self,
                                const GrlColor *color);

/* ==========================================================================
 * Appearance
 * ========================================================================== */

/**
 * lrg_button_get_corner_radius:
 * @self: an #LrgButton
 *
 * Gets the button's corner radius.
 *
 * Returns: The corner radius in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_button_get_corner_radius (LrgButton *self);

/**
 * lrg_button_set_corner_radius:
 * @self: an #LrgButton
 * @radius: the corner radius in pixels
 *
 * Sets the button's corner radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_button_set_corner_radius (LrgButton *self,
                                   gfloat     radius);

/* ==========================================================================
 * State
 * ========================================================================== */

/**
 * lrg_button_get_is_hovered:
 * @self: an #LrgButton
 *
 * Gets whether the button is currently hovered.
 *
 * Returns: %TRUE if hovered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_button_get_is_hovered (LrgButton *self);

/**
 * lrg_button_get_is_pressed:
 * @self: an #LrgButton
 *
 * Gets whether the button is currently pressed.
 *
 * Returns: %TRUE if pressed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_button_get_is_pressed (LrgButton *self);

G_END_DECLS
