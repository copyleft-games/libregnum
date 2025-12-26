/* lrg-text-input.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Text input widget for single-line text entry.
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

#define LRG_TYPE_TEXT_INPUT (lrg_text_input_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTextInput, lrg_text_input, LRG, TEXT_INPUT, LrgWidget)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_text_input_new:
 *
 * Creates a new text input widget.
 *
 * Returns: (transfer full): A new #LrgTextInput
 */
LRG_AVAILABLE_IN_ALL
LrgTextInput * lrg_text_input_new (void);

/**
 * lrg_text_input_new_with_placeholder:
 * @placeholder: (nullable): placeholder text to display when empty
 *
 * Creates a new text input widget with placeholder text.
 *
 * Returns: (transfer full): A new #LrgTextInput
 */
LRG_AVAILABLE_IN_ALL
LrgTextInput * lrg_text_input_new_with_placeholder (const gchar *placeholder);

/* ==========================================================================
 * Text
 * ========================================================================== */

/**
 * lrg_text_input_get_text:
 * @self: an #LrgTextInput
 *
 * Gets the current input text.
 *
 * Returns: (transfer none): The current text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_text_input_get_text (LrgTextInput *self);

/**
 * lrg_text_input_set_text:
 * @self: an #LrgTextInput
 * @text: (nullable): the text to set
 *
 * Sets the input text.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_input_set_text (LrgTextInput *self,
                              const gchar  *text);

/**
 * lrg_text_input_get_placeholder:
 * @self: an #LrgTextInput
 *
 * Gets the placeholder text.
 *
 * Returns: (transfer none) (nullable): The placeholder text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_text_input_get_placeholder (LrgTextInput *self);

/**
 * lrg_text_input_set_placeholder:
 * @self: an #LrgTextInput
 * @placeholder: (nullable): the placeholder text
 *
 * Sets the placeholder text shown when the input is empty.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_input_set_placeholder (LrgTextInput *self,
                                     const gchar  *placeholder);

/* ==========================================================================
 * Input Behavior
 * ========================================================================== */

/**
 * lrg_text_input_get_max_length:
 * @self: an #LrgTextInput
 *
 * Gets the maximum text length.
 *
 * Returns: Maximum length (0 = unlimited)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_text_input_get_max_length (LrgTextInput *self);

/**
 * lrg_text_input_set_max_length:
 * @self: an #LrgTextInput
 * @max_length: maximum length (0 = unlimited)
 *
 * Sets the maximum text length.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_input_set_max_length (LrgTextInput *self,
                                    guint         max_length);

/**
 * lrg_text_input_get_password_mode:
 * @self: an #LrgTextInput
 *
 * Gets whether password mode is enabled.
 *
 * Returns: %TRUE if password mode is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_input_get_password_mode (LrgTextInput *self);

/**
 * lrg_text_input_set_password_mode:
 * @self: an #LrgTextInput
 * @password_mode: whether to mask input
 *
 * Sets whether to mask input with asterisks.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_input_set_password_mode (LrgTextInput *self,
                                       gboolean      password_mode);

/* ==========================================================================
 * Cursor
 * ========================================================================== */

/**
 * lrg_text_input_get_cursor_position:
 * @self: an #LrgTextInput
 *
 * Gets the cursor position (in UTF-8 characters).
 *
 * Returns: The cursor position
 */
LRG_AVAILABLE_IN_ALL
gint lrg_text_input_get_cursor_position (LrgTextInput *self);

/**
 * lrg_text_input_set_cursor_position:
 * @self: an #LrgTextInput
 * @position: the cursor position
 *
 * Sets the cursor position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_input_set_cursor_position (LrgTextInput *self,
                                         gint          position);

/* ==========================================================================
 * Focus
 * ========================================================================== */

/**
 * lrg_text_input_get_focused:
 * @self: an #LrgTextInput
 *
 * Gets whether the input has keyboard focus.
 *
 * Returns: %TRUE if focused
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_text_input_get_focused (LrgTextInput *self);

/**
 * lrg_text_input_set_focused:
 * @self: an #LrgTextInput
 * @focused: whether to focus
 *
 * Sets the focus state. When focused, the input receives keyboard events.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_input_set_focused (LrgTextInput *self,
                                 gboolean      focused);

/* ==========================================================================
 * Appearance
 * ========================================================================== */

/**
 * lrg_text_input_get_font_size:
 * @self: an #LrgTextInput
 *
 * Gets the font size.
 *
 * Returns: The font size
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_text_input_get_font_size (LrgTextInput *self);

/**
 * lrg_text_input_set_font_size:
 * @self: an #LrgTextInput
 * @size: the font size
 *
 * Sets the font size.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_input_set_font_size (LrgTextInput *self,
                                   gfloat        size);

/**
 * lrg_text_input_get_text_color:
 * @self: an #LrgTextInput
 *
 * Gets the text color.
 *
 * Returns: (transfer none): The text color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_text_input_get_text_color (LrgTextInput *self);

/**
 * lrg_text_input_set_text_color:
 * @self: an #LrgTextInput
 * @color: the text color
 *
 * Sets the text color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_input_set_text_color (LrgTextInput   *self,
                                    const GrlColor *color);

/**
 * lrg_text_input_get_background_color:
 * @self: an #LrgTextInput
 *
 * Gets the background color.
 *
 * Returns: (transfer none): The background color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_text_input_get_background_color (LrgTextInput *self);

/**
 * lrg_text_input_set_background_color:
 * @self: an #LrgTextInput
 * @color: the background color
 *
 * Sets the background color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_input_set_background_color (LrgTextInput   *self,
                                          const GrlColor *color);

/**
 * lrg_text_input_get_border_color:
 * @self: an #LrgTextInput
 *
 * Gets the border color.
 *
 * Returns: (transfer none): The border color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_text_input_get_border_color (LrgTextInput *self);

/**
 * lrg_text_input_set_border_color:
 * @self: an #LrgTextInput
 * @color: the border color
 *
 * Sets the border color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_input_set_border_color (LrgTextInput   *self,
                                      const GrlColor *color);

/**
 * lrg_text_input_get_placeholder_color:
 * @self: an #LrgTextInput
 *
 * Gets the placeholder text color.
 *
 * Returns: (transfer none): The placeholder color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_text_input_get_placeholder_color (LrgTextInput *self);

/**
 * lrg_text_input_set_placeholder_color:
 * @self: an #LrgTextInput
 * @color: the placeholder color
 *
 * Sets the placeholder text color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_input_set_placeholder_color (LrgTextInput   *self,
                                           const GrlColor *color);

/**
 * lrg_text_input_get_corner_radius:
 * @self: an #LrgTextInput
 *
 * Gets the corner radius.
 *
 * Returns: The corner radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_text_input_get_corner_radius (LrgTextInput *self);

/**
 * lrg_text_input_set_corner_radius:
 * @self: an #LrgTextInput
 * @radius: the corner radius
 *
 * Sets the corner radius for rounded corners.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_input_set_corner_radius (LrgTextInput *self,
                                       gfloat        radius);

/**
 * lrg_text_input_get_padding:
 * @self: an #LrgTextInput
 *
 * Gets the text padding from the edges.
 *
 * Returns: The padding
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_text_input_get_padding (LrgTextInput *self);

/**
 * lrg_text_input_set_padding:
 * @self: an #LrgTextInput
 * @padding: the padding
 *
 * Sets the text padding from the edges.
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_input_set_padding (LrgTextInput *self,
                                 gfloat        padding);

G_END_DECLS
