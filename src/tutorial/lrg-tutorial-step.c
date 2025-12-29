/* lrg-tutorial-step.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tutorial step definition.
 */

#include "lrg-tutorial-step.h"

struct _LrgTutorialStep
{
    LrgTutorialStepType step_type;
    gchar              *id;

    /* Text step data */
    gchar *text;
    gchar *speaker;

    /* Highlight step data */
    gchar            *target_id;
    LrgHighlightStyle highlight_style;

    /* Input step data */
    gchar   *action_name;
    gboolean show_prompt;

    /* Condition step data */
    gchar *condition_id;

    /* Delay step data */
    gfloat duration;

    /* Common properties */
    gboolean          can_skip;
    gboolean          blocks_input;
    gboolean          auto_advance;
    gfloat            position_x;
    gfloat            position_y;
    LrgArrowDirection arrow_direction;
};

G_DEFINE_BOXED_TYPE (LrgTutorialStep, lrg_tutorial_step,
                     lrg_tutorial_step_copy, lrg_tutorial_step_free)

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
LrgTutorialStep *
lrg_tutorial_step_new (LrgTutorialStepType step_type)
{
    LrgTutorialStep *self;

    self = g_new0 (LrgTutorialStep, 1);
    self->step_type = step_type;
    self->can_skip = TRUE;
    self->blocks_input = FALSE;
    self->auto_advance = FALSE;
    self->arrow_direction = LRG_ARROW_DIRECTION_AUTO;
    self->highlight_style = LRG_HIGHLIGHT_STYLE_OUTLINE;

    return self;
}

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
LrgTutorialStep *
lrg_tutorial_step_new_text (const gchar *text,
                            const gchar *speaker)
{
    LrgTutorialStep *self;

    self = lrg_tutorial_step_new (LRG_TUTORIAL_STEP_TEXT);
    self->text = g_strdup (text);
    self->speaker = g_strdup (speaker);

    return self;
}

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
LrgTutorialStep *
lrg_tutorial_step_new_highlight (const gchar       *target_id,
                                 LrgHighlightStyle  style)
{
    LrgTutorialStep *self;

    self = lrg_tutorial_step_new (LRG_TUTORIAL_STEP_HIGHLIGHT);
    self->target_id = g_strdup (target_id);
    self->highlight_style = style;

    return self;
}

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
LrgTutorialStep *
lrg_tutorial_step_new_input (const gchar *action_name,
                             gboolean     show_prompt)
{
    LrgTutorialStep *self;

    self = lrg_tutorial_step_new (LRG_TUTORIAL_STEP_INPUT);
    self->action_name = g_strdup (action_name);
    self->show_prompt = show_prompt;

    return self;
}

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
LrgTutorialStep *
lrg_tutorial_step_new_condition (const gchar *condition_id)
{
    LrgTutorialStep *self;

    self = lrg_tutorial_step_new (LRG_TUTORIAL_STEP_CONDITION);
    self->condition_id = g_strdup (condition_id);

    return self;
}

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
LrgTutorialStep *
lrg_tutorial_step_new_delay (gfloat duration)
{
    LrgTutorialStep *self;

    self = lrg_tutorial_step_new (LRG_TUTORIAL_STEP_DELAY);
    self->duration = duration;
    self->auto_advance = TRUE;  /* Delay steps auto-advance by default */

    return self;
}

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
LrgTutorialStep *
lrg_tutorial_step_copy (const LrgTutorialStep *self)
{
    LrgTutorialStep *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgTutorialStep, 1);

    copy->step_type = self->step_type;
    copy->id = g_strdup (self->id);

    copy->text = g_strdup (self->text);
    copy->speaker = g_strdup (self->speaker);

    copy->target_id = g_strdup (self->target_id);
    copy->highlight_style = self->highlight_style;

    copy->action_name = g_strdup (self->action_name);
    copy->show_prompt = self->show_prompt;

    copy->condition_id = g_strdup (self->condition_id);

    copy->duration = self->duration;

    copy->can_skip = self->can_skip;
    copy->blocks_input = self->blocks_input;
    copy->auto_advance = self->auto_advance;
    copy->position_x = self->position_x;
    copy->position_y = self->position_y;
    copy->arrow_direction = self->arrow_direction;

    return copy;
}

/**
 * lrg_tutorial_step_free:
 * @self: A #LrgTutorialStep
 *
 * Frees a tutorial step.
 *
 * Since: 1.0
 */
void
lrg_tutorial_step_free (LrgTutorialStep *self)
{
    if (self == NULL)
        return;

    g_free (self->id);
    g_free (self->text);
    g_free (self->speaker);
    g_free (self->target_id);
    g_free (self->action_name);
    g_free (self->condition_id);
    g_free (self);
}

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
LrgTutorialStepType
lrg_tutorial_step_get_step_type (const LrgTutorialStep *self)
{
    g_return_val_if_fail (self != NULL, LRG_TUTORIAL_STEP_TEXT);
    return self->step_type;
}

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
const gchar *
lrg_tutorial_step_get_id (const LrgTutorialStep *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->id;
}

/**
 * lrg_tutorial_step_set_id:
 * @self: A #LrgTutorialStep
 * @id: The step ID
 *
 * Sets the step ID.
 *
 * Since: 1.0
 */
void
lrg_tutorial_step_set_id (LrgTutorialStep *self,
                          const gchar     *id)
{
    g_return_if_fail (self != NULL);

    g_free (self->id);
    self->id = g_strdup (id);
}

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
const gchar *
lrg_tutorial_step_get_text (const LrgTutorialStep *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->text;
}

/**
 * lrg_tutorial_step_set_text:
 * @self: A #LrgTutorialStep
 * @text: The text to display
 *
 * Sets the text to display.
 *
 * Since: 1.0
 */
void
lrg_tutorial_step_set_text (LrgTutorialStep *self,
                            const gchar     *text)
{
    g_return_if_fail (self != NULL);

    g_free (self->text);
    self->text = g_strdup (text);
}

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
const gchar *
lrg_tutorial_step_get_speaker (const LrgTutorialStep *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->speaker;
}

/**
 * lrg_tutorial_step_set_speaker:
 * @self: A #LrgTutorialStep
 * @speaker: The speaker name
 *
 * Sets the speaker name.
 *
 * Since: 1.0
 */
void
lrg_tutorial_step_set_speaker (LrgTutorialStep *self,
                               const gchar     *speaker)
{
    g_return_if_fail (self != NULL);

    g_free (self->speaker);
    self->speaker = g_strdup (speaker);
}

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
const gchar *
lrg_tutorial_step_get_target_id (const LrgTutorialStep *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->target_id;
}

/**
 * lrg_tutorial_step_set_target_id:
 * @self: A #LrgTutorialStep
 * @target_id: The target element ID
 *
 * Sets the target element ID.
 *
 * Since: 1.0
 */
void
lrg_tutorial_step_set_target_id (LrgTutorialStep *self,
                                 const gchar     *target_id)
{
    g_return_if_fail (self != NULL);

    g_free (self->target_id);
    self->target_id = g_strdup (target_id);
}

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
LrgHighlightStyle
lrg_tutorial_step_get_highlight_style (const LrgTutorialStep *self)
{
    g_return_val_if_fail (self != NULL, LRG_HIGHLIGHT_STYLE_OUTLINE);
    return self->highlight_style;
}

/**
 * lrg_tutorial_step_set_highlight_style:
 * @self: A #LrgTutorialStep
 * @style: The highlight style
 *
 * Sets the highlight style.
 *
 * Since: 1.0
 */
void
lrg_tutorial_step_set_highlight_style (LrgTutorialStep  *self,
                                       LrgHighlightStyle style)
{
    g_return_if_fail (self != NULL);
    self->highlight_style = style;
}

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
const gchar *
lrg_tutorial_step_get_action_name (const LrgTutorialStep *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->action_name;
}

/**
 * lrg_tutorial_step_set_action_name:
 * @self: A #LrgTutorialStep
 * @action_name: The input action name
 *
 * Sets the input action name.
 *
 * Since: 1.0
 */
void
lrg_tutorial_step_set_action_name (LrgTutorialStep *self,
                                   const gchar     *action_name)
{
    g_return_if_fail (self != NULL);

    g_free (self->action_name);
    self->action_name = g_strdup (action_name);
}

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
gboolean
lrg_tutorial_step_get_show_prompt (const LrgTutorialStep *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->show_prompt;
}

/**
 * lrg_tutorial_step_set_show_prompt:
 * @self: A #LrgTutorialStep
 * @show: Whether to show the prompt
 *
 * Sets whether to show an input prompt.
 *
 * Since: 1.0
 */
void
lrg_tutorial_step_set_show_prompt (LrgTutorialStep *self,
                                   gboolean         show)
{
    g_return_if_fail (self != NULL);
    self->show_prompt = show;
}

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
const gchar *
lrg_tutorial_step_get_condition_id (const LrgTutorialStep *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->condition_id;
}

/**
 * lrg_tutorial_step_set_condition_id:
 * @self: A #LrgTutorialStep
 * @condition_id: The condition ID
 *
 * Sets the condition ID.
 *
 * Since: 1.0
 */
void
lrg_tutorial_step_set_condition_id (LrgTutorialStep *self,
                                    const gchar     *condition_id)
{
    g_return_if_fail (self != NULL);

    g_free (self->condition_id);
    self->condition_id = g_strdup (condition_id);
}

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
gfloat
lrg_tutorial_step_get_duration (const LrgTutorialStep *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->duration;
}

/**
 * lrg_tutorial_step_set_duration:
 * @self: A #LrgTutorialStep
 * @duration: Duration in seconds
 *
 * Sets the delay duration.
 *
 * Since: 1.0
 */
void
lrg_tutorial_step_set_duration (LrgTutorialStep *self,
                                gfloat           duration)
{
    g_return_if_fail (self != NULL);
    self->duration = duration;
}

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
gboolean
lrg_tutorial_step_get_can_skip (const LrgTutorialStep *self)
{
    g_return_val_if_fail (self != NULL, TRUE);
    return self->can_skip;
}

/**
 * lrg_tutorial_step_set_can_skip:
 * @self: A #LrgTutorialStep
 * @can_skip: Whether the step can be skipped
 *
 * Sets whether this step can be skipped.
 *
 * Since: 1.0
 */
void
lrg_tutorial_step_set_can_skip (LrgTutorialStep *self,
                                gboolean         can_skip)
{
    g_return_if_fail (self != NULL);
    self->can_skip = can_skip;
}

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
gboolean
lrg_tutorial_step_get_blocks_input (const LrgTutorialStep *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->blocks_input;
}

/**
 * lrg_tutorial_step_set_blocks_input:
 * @self: A #LrgTutorialStep
 * @blocks: Whether to block game input
 *
 * Sets whether this step blocks game input.
 *
 * Since: 1.0
 */
void
lrg_tutorial_step_set_blocks_input (LrgTutorialStep *self,
                                    gboolean         blocks)
{
    g_return_if_fail (self != NULL);
    self->blocks_input = blocks;
}

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
gboolean
lrg_tutorial_step_get_auto_advance (const LrgTutorialStep *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->auto_advance;
}

/**
 * lrg_tutorial_step_set_auto_advance:
 * @self: A #LrgTutorialStep
 * @auto_advance: Whether to auto-advance
 *
 * Sets whether this step auto-advances.
 *
 * Since: 1.0
 */
void
lrg_tutorial_step_set_auto_advance (LrgTutorialStep *self,
                                    gboolean         auto_advance)
{
    g_return_if_fail (self != NULL);
    self->auto_advance = auto_advance;
}

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
void
lrg_tutorial_step_get_position (const LrgTutorialStep *self,
                                gfloat                *out_x,
                                gfloat                *out_y)
{
    g_return_if_fail (self != NULL);

    if (out_x != NULL)
        *out_x = self->position_x;
    if (out_y != NULL)
        *out_y = self->position_y;
}

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
void
lrg_tutorial_step_set_position (LrgTutorialStep *self,
                                gfloat           x,
                                gfloat           y)
{
    g_return_if_fail (self != NULL);

    self->position_x = x;
    self->position_y = y;
}

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
LrgArrowDirection
lrg_tutorial_step_get_arrow_direction (const LrgTutorialStep *self)
{
    g_return_val_if_fail (self != NULL, LRG_ARROW_DIRECTION_AUTO);
    return self->arrow_direction;
}

/**
 * lrg_tutorial_step_set_arrow_direction:
 * @self: A #LrgTutorialStep
 * @direction: The arrow direction
 *
 * Sets the arrow direction for this step.
 *
 * Since: 1.0
 */
void
lrg_tutorial_step_set_arrow_direction (LrgTutorialStep  *self,
                                       LrgArrowDirection direction)
{
    g_return_if_fail (self != NULL);
    self->arrow_direction = direction;
}
