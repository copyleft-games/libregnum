/* lrg-tutorial-step.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tutorial step definition.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_TUTORIAL_STEP (lrg_tutorial_step_get_type ())

/**
 * LrgTutorialStep:
 *
 * A single step in a tutorial sequence.
 *
 * Tutorial steps define what happens at each point in the tutorial,
 * such as displaying text, highlighting UI elements, or waiting for
 * player input.
 *
 * Since: 1.0
 */
typedef struct _LrgTutorialStep LrgTutorialStep;

LRG_AVAILABLE_IN_ALL
GType lrg_tutorial_step_get_type (void) G_GNUC_CONST;

/**
 * lrg_tutorial_step_new:
 * @step_type: The type of step
 *
 * Creates a new tutorial step.
 *
 * Returns: (transfer full): A new #LrgTutorialStep
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorialStep *   lrg_tutorial_step_new                   (LrgTutorialStepType step_type);

/**
 * lrg_tutorial_step_new_text:
 * @text: The text to display
 * @speaker: (nullable): Name of the speaker (for dialog-style)
 *
 * Creates a text display step.
 *
 * Returns: (transfer full): A new #LrgTutorialStep
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorialStep *   lrg_tutorial_step_new_text              (const gchar *text,
                                                             const gchar *speaker);

/**
 * lrg_tutorial_step_new_highlight:
 * @target_id: ID of the widget/element to highlight
 * @style: The highlight style
 *
 * Creates a highlight step.
 *
 * Returns: (transfer full): A new #LrgTutorialStep
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorialStep *   lrg_tutorial_step_new_highlight         (const gchar       *target_id,
                                                             LrgHighlightStyle  style);

/**
 * lrg_tutorial_step_new_input:
 * @action_name: Name of the input action to wait for
 * @show_prompt: Whether to show an input prompt
 *
 * Creates an input wait step.
 *
 * Returns: (transfer full): A new #LrgTutorialStep
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorialStep *   lrg_tutorial_step_new_input             (const gchar *action_name,
                                                             gboolean     show_prompt);

/**
 * lrg_tutorial_step_new_condition:
 * @condition_id: ID of the condition to check
 *
 * Creates a condition wait step.
 *
 * Returns: (transfer full): A new #LrgTutorialStep
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorialStep *   lrg_tutorial_step_new_condition         (const gchar *condition_id);

/**
 * lrg_tutorial_step_new_delay:
 * @duration: Delay duration in seconds
 *
 * Creates a delay step.
 *
 * Returns: (transfer full): A new #LrgTutorialStep
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorialStep *   lrg_tutorial_step_new_delay             (gfloat duration);

/**
 * lrg_tutorial_step_copy:
 * @self: A #LrgTutorialStep
 *
 * Creates a copy of a tutorial step.
 *
 * Returns: (transfer full): A copy of the step
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorialStep *   lrg_tutorial_step_copy                  (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_free:
 * @self: A #LrgTutorialStep
 *
 * Frees a tutorial step.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_free                  (LrgTutorialStep *self);

/* Accessors */

/**
 * lrg_tutorial_step_get_step_type:
 * @self: A #LrgTutorialStep
 *
 * Gets the step type.
 *
 * Returns: The step type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorialStepType lrg_tutorial_step_get_step_type         (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_get_id:
 * @self: A #LrgTutorialStep
 *
 * Gets the step ID.
 *
 * Returns: (transfer none) (nullable): The step ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_tutorial_step_get_id                (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_set_id:
 * @self: A #LrgTutorialStep
 * @id: The step ID
 *
 * Sets the step ID.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_set_id                (LrgTutorialStep *self,
                                                             const gchar     *id);

/* Text step properties */

/**
 * lrg_tutorial_step_get_text:
 * @self: A #LrgTutorialStep
 *
 * Gets the text to display (for text steps).
 *
 * Returns: (transfer none) (nullable): The text
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_tutorial_step_get_text              (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_set_text:
 * @self: A #LrgTutorialStep
 * @text: The text to display
 *
 * Sets the text to display.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_set_text              (LrgTutorialStep *self,
                                                             const gchar     *text);

/**
 * lrg_tutorial_step_get_speaker:
 * @self: A #LrgTutorialStep
 *
 * Gets the speaker name (for dialog-style text).
 *
 * Returns: (transfer none) (nullable): The speaker name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_tutorial_step_get_speaker           (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_set_speaker:
 * @self: A #LrgTutorialStep
 * @speaker: The speaker name
 *
 * Sets the speaker name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_set_speaker           (LrgTutorialStep *self,
                                                             const gchar     *speaker);

/* Highlight step properties */

/**
 * lrg_tutorial_step_get_target_id:
 * @self: A #LrgTutorialStep
 *
 * Gets the target element ID (for highlight steps).
 *
 * Returns: (transfer none) (nullable): The target ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_tutorial_step_get_target_id         (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_set_target_id:
 * @self: A #LrgTutorialStep
 * @target_id: The target element ID
 *
 * Sets the target element ID.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_set_target_id         (LrgTutorialStep *self,
                                                             const gchar     *target_id);

/**
 * lrg_tutorial_step_get_highlight_style:
 * @self: A #LrgTutorialStep
 *
 * Gets the highlight style.
 *
 * Returns: The highlight style
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHighlightStyle   lrg_tutorial_step_get_highlight_style   (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_set_highlight_style:
 * @self: A #LrgTutorialStep
 * @style: The highlight style
 *
 * Sets the highlight style.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_set_highlight_style   (LrgTutorialStep  *self,
                                                             LrgHighlightStyle style);

/* Input step properties */

/**
 * lrg_tutorial_step_get_action_name:
 * @self: A #LrgTutorialStep
 *
 * Gets the input action name (for input steps).
 *
 * Returns: (transfer none) (nullable): The action name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_tutorial_step_get_action_name       (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_set_action_name:
 * @self: A #LrgTutorialStep
 * @action_name: The input action name
 *
 * Sets the input action name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_set_action_name       (LrgTutorialStep *self,
                                                             const gchar     *action_name);

/**
 * lrg_tutorial_step_get_show_prompt:
 * @self: A #LrgTutorialStep
 *
 * Gets whether to show an input prompt.
 *
 * Returns: %TRUE if prompt should be shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_step_get_show_prompt       (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_set_show_prompt:
 * @self: A #LrgTutorialStep
 * @show: Whether to show the prompt
 *
 * Sets whether to show an input prompt.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_set_show_prompt       (LrgTutorialStep *self,
                                                             gboolean         show);

/* Condition step properties */

/**
 * lrg_tutorial_step_get_condition_id:
 * @self: A #LrgTutorialStep
 *
 * Gets the condition ID (for condition steps).
 *
 * Returns: (transfer none) (nullable): The condition ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_tutorial_step_get_condition_id      (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_set_condition_id:
 * @self: A #LrgTutorialStep
 * @condition_id: The condition ID
 *
 * Sets the condition ID.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_set_condition_id      (LrgTutorialStep *self,
                                                             const gchar     *condition_id);

/* Delay step properties */

/**
 * lrg_tutorial_step_get_duration:
 * @self: A #LrgTutorialStep
 *
 * Gets the delay duration (for delay steps).
 *
 * Returns: Duration in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_tutorial_step_get_duration          (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_set_duration:
 * @self: A #LrgTutorialStep
 * @duration: Duration in seconds
 *
 * Sets the delay duration.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_set_duration          (LrgTutorialStep *self,
                                                             gfloat           duration);

/* Common properties */

/**
 * lrg_tutorial_step_get_can_skip:
 * @self: A #LrgTutorialStep
 *
 * Gets whether this step can be skipped.
 *
 * Returns: %TRUE if skippable
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_step_get_can_skip          (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_set_can_skip:
 * @self: A #LrgTutorialStep
 * @can_skip: Whether the step can be skipped
 *
 * Sets whether this step can be skipped.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_set_can_skip          (LrgTutorialStep *self,
                                                             gboolean         can_skip);

/**
 * lrg_tutorial_step_get_blocks_input:
 * @self: A #LrgTutorialStep
 *
 * Gets whether this step blocks game input.
 *
 * Returns: %TRUE if input is blocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_step_get_blocks_input      (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_set_blocks_input:
 * @self: A #LrgTutorialStep
 * @blocks: Whether to block game input
 *
 * Sets whether this step blocks game input.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_set_blocks_input      (LrgTutorialStep *self,
                                                             gboolean         blocks);

/**
 * lrg_tutorial_step_get_auto_advance:
 * @self: A #LrgTutorialStep
 *
 * Gets whether this step auto-advances after its action completes.
 *
 * Returns: %TRUE if auto-advancing
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_step_get_auto_advance      (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_set_auto_advance:
 * @self: A #LrgTutorialStep
 * @auto_advance: Whether to auto-advance
 *
 * Sets whether this step auto-advances.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_set_auto_advance      (LrgTutorialStep *self,
                                                             gboolean         auto_advance);

/* Position for text/arrows */

/**
 * lrg_tutorial_step_get_position:
 * @self: A #LrgTutorialStep
 * @out_x: (out) (nullable): Return location for X position
 * @out_y: (out) (nullable): Return location for Y position
 *
 * Gets the position for text/prompt display.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_get_position          (const LrgTutorialStep *self,
                                                             gfloat                *out_x,
                                                             gfloat                *out_y);

/**
 * lrg_tutorial_step_set_position:
 * @self: A #LrgTutorialStep
 * @x: X position
 * @y: Y position
 *
 * Sets the position for text/prompt display.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_set_position          (LrgTutorialStep *self,
                                                             gfloat           x,
                                                             gfloat           y);

/**
 * lrg_tutorial_step_get_arrow_direction:
 * @self: A #LrgTutorialStep
 *
 * Gets the arrow direction for this step.
 *
 * Returns: The arrow direction
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgArrowDirection   lrg_tutorial_step_get_arrow_direction   (const LrgTutorialStep *self);

/**
 * lrg_tutorial_step_set_arrow_direction:
 * @self: A #LrgTutorialStep
 * @direction: The arrow direction
 *
 * Sets the arrow direction for this step.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_step_set_arrow_direction   (LrgTutorialStep  *self,
                                                             LrgArrowDirection direction);

G_END_DECLS
