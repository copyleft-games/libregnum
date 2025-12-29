/* lrg-input-prompt.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Input prompt widget for tutorial system.
 *
 * This widget displays input hints showing which button/key
 * the player needs to press. It automatically adapts to
 * show glyphs for the current input device (keyboard vs gamepad).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../ui/lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_INPUT_PROMPT (lrg_input_prompt_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgInputPrompt, lrg_input_prompt, LRG, INPUT_PROMPT, LrgWidget)

/**
 * lrg_input_prompt_new:
 *
 * Creates a new input prompt widget.
 *
 * Returns: (transfer full): A new #LrgInputPrompt
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgInputPrompt *    lrg_input_prompt_new                (void);

/**
 * lrg_input_prompt_new_with_action:
 * @action_name: The input action to display
 *
 * Creates a new input prompt widget for the specified action.
 *
 * Returns: (transfer full): A new #LrgInputPrompt
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgInputPrompt *    lrg_input_prompt_new_with_action    (const gchar *action_name);

/* Action */

/**
 * lrg_input_prompt_get_action_name:
 * @self: An #LrgInputPrompt
 *
 * Gets the input action name being displayed.
 *
 * Returns: (transfer none) (nullable): The action name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_input_prompt_get_action_name    (LrgInputPrompt *self);

/**
 * lrg_input_prompt_set_action_name:
 * @self: An #LrgInputPrompt
 * @action_name: (nullable): The action name to display
 *
 * Sets the input action name to display.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_input_prompt_set_action_name    (LrgInputPrompt *self,
                                                         const gchar    *action_name);

/* Text */

/**
 * lrg_input_prompt_get_prompt_text:
 * @self: An #LrgInputPrompt
 *
 * Gets the optional prompt text displayed with the input glyph.
 *
 * Returns: (transfer none) (nullable): The prompt text
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_input_prompt_get_prompt_text    (LrgInputPrompt *self);

/**
 * lrg_input_prompt_set_prompt_text:
 * @self: An #LrgInputPrompt
 * @text: (nullable): The prompt text
 *
 * Sets the optional prompt text to display with the input glyph.
 * For example: "Press [A] to continue"
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_input_prompt_set_prompt_text    (LrgInputPrompt *self,
                                                         const gchar    *text);

/* Input device */

/**
 * lrg_input_prompt_get_device_type:
 * @self: An #LrgInputPrompt
 *
 * Gets the current input device type being displayed.
 *
 * Returns: The device type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgInputDeviceType  lrg_input_prompt_get_device_type    (LrgInputPrompt *self);

/**
 * lrg_input_prompt_set_device_type:
 * @self: An #LrgInputPrompt
 * @device_type: The device type to display glyphs for
 *
 * Sets the input device type to display glyphs for.
 * Normally this is set automatically by listening to the input manager.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_input_prompt_set_device_type    (LrgInputPrompt    *self,
                                                         LrgInputDeviceType device_type);

/**
 * lrg_input_prompt_get_gamepad_style:
 * @self: An #LrgInputPrompt
 *
 * Gets the gamepad button style (Xbox, PlayStation, Nintendo, etc.).
 *
 * Returns: The gamepad style
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgGamepadStyle     lrg_input_prompt_get_gamepad_style  (LrgInputPrompt *self);

/**
 * lrg_input_prompt_set_gamepad_style:
 * @self: An #LrgInputPrompt
 * @style: The gamepad style
 *
 * Sets the gamepad button style for glyph display.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_input_prompt_set_gamepad_style  (LrgInputPrompt *self,
                                                         LrgGamepadStyle style);

/* Appearance */

/**
 * lrg_input_prompt_get_font:
 * @self: An #LrgInputPrompt
 *
 * Gets the font used for text.
 *
 * Returns: (transfer none) (nullable): The font
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlFont *           lrg_input_prompt_get_font           (LrgInputPrompt *self);

/**
 * lrg_input_prompt_set_font:
 * @self: An #LrgInputPrompt
 * @font: (nullable): The font to use
 *
 * Sets the font for text display.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_input_prompt_set_font           (LrgInputPrompt *self,
                                                         GrlFont        *font);

/**
 * lrg_input_prompt_get_font_size:
 * @self: An #LrgInputPrompt
 *
 * Gets the font size.
 *
 * Returns: The font size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_input_prompt_get_font_size      (LrgInputPrompt *self);

/**
 * lrg_input_prompt_set_font_size:
 * @self: An #LrgInputPrompt
 * @size: The font size in pixels
 *
 * Sets the font size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_input_prompt_set_font_size      (LrgInputPrompt *self,
                                                         gfloat          size);

/**
 * lrg_input_prompt_get_text_color:
 * @self: An #LrgInputPrompt
 *
 * Gets the text color.
 *
 * Returns: (transfer none): The color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const GrlColor *    lrg_input_prompt_get_text_color     (LrgInputPrompt *self);

/**
 * lrg_input_prompt_set_text_color:
 * @self: An #LrgInputPrompt
 * @color: The text color
 *
 * Sets the text color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_input_prompt_set_text_color     (LrgInputPrompt *self,
                                                         const GrlColor *color);

/**
 * lrg_input_prompt_get_glyph_size:
 * @self: An #LrgInputPrompt
 *
 * Gets the input glyph size.
 *
 * Returns: The glyph size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_input_prompt_get_glyph_size     (LrgInputPrompt *self);

/**
 * lrg_input_prompt_set_glyph_size:
 * @self: An #LrgInputPrompt
 * @size: The glyph size in pixels
 *
 * Sets the input glyph size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_input_prompt_set_glyph_size     (LrgInputPrompt *self,
                                                         gfloat          size);

/* Animation */

/**
 * lrg_input_prompt_get_animated:
 * @self: An #LrgInputPrompt
 *
 * Gets whether the prompt is animated.
 *
 * Returns: %TRUE if animated
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_input_prompt_get_animated       (LrgInputPrompt *self);

/**
 * lrg_input_prompt_set_animated:
 * @self: An #LrgInputPrompt
 * @animated: Whether to animate
 *
 * Sets whether the prompt should animate (pulse, bounce, etc.).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_input_prompt_set_animated       (LrgInputPrompt *self,
                                                         gboolean        animated);

/* Update */

/**
 * lrg_input_prompt_update:
 * @self: An #LrgInputPrompt
 * @delta_time: Time since last update in seconds
 *
 * Updates the prompt animation state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_input_prompt_update             (LrgInputPrompt *self,
                                                         gfloat          delta_time);

G_END_DECLS
