/* lrg-input-buffer.c - Input buffering implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../config.h"
#include "lrg-input-buffer.h"

/**
 * LrgInputBuffer:
 *
 * Internal structure for input buffering.
 */
struct _LrgInputBuffer
{
    GQueue          *queue;           /* Queue of LrgBufferedInput */
    gint             buffer_frames;   /* How many frames to buffer */
    gboolean         enabled;         /* Is buffering enabled */
    LrgInputContext  current_context; /* Current input context */
};

/* ========================================================================== */
/* Private Helpers                                                            */
/* ========================================================================== */

static LrgBufferedInput *
buffered_input_new (const gchar *action_name,
                    gint         buffer_frames)
{
    LrgBufferedInput *input;

    input = g_new0 (LrgBufferedInput, 1);
    input->action_name = g_strdup (action_name);
    input->timestamp_usec = g_get_monotonic_time ();
    input->frames_remaining = buffer_frames;

    return input;
}

static void
buffered_input_free (LrgBufferedInput *input)
{
    if (input == NULL)
        return;

    g_free (input->action_name);
    g_free (input);
}

/* ========================================================================== */
/* Construction / Destruction                                                 */
/* ========================================================================== */

/**
 * lrg_input_buffer_new:
 * @buffer_frames: number of frames to buffer inputs
 *
 * Creates a new input buffer.
 *
 * Returns: (transfer full): a new #LrgInputBuffer
 *
 * Since: 1.0
 */
LrgInputBuffer *
lrg_input_buffer_new (gint buffer_frames)
{
    LrgInputBuffer *self;

    g_return_val_if_fail (buffer_frames > 0, NULL);

    self = g_new0 (LrgInputBuffer, 1);
    self->queue = g_queue_new ();
    self->buffer_frames = buffer_frames;
    self->enabled = TRUE;
    self->current_context = LRG_INPUT_CONTEXT_GAMEPLAY;

    return self;
}

/**
 * lrg_input_buffer_free:
 * @self: (nullable): an #LrgInputBuffer
 *
 * Frees an input buffer.
 *
 * Since: 1.0
 */
void
lrg_input_buffer_free (LrgInputBuffer *self)
{
    if (self == NULL)
        return;

    /* Free all queued inputs */
    if (self->queue != NULL)
    {
        g_queue_free_full (self->queue, (GDestroyNotify)buffered_input_free);
        self->queue = NULL;
    }

    g_free (self);
}

/* ========================================================================== */
/* Configuration                                                              */
/* ========================================================================== */

/**
 * lrg_input_buffer_get_buffer_frames:
 * @self: an #LrgInputBuffer
 *
 * Gets the number of frames inputs are buffered for.
 *
 * Returns: the buffer frame count
 *
 * Since: 1.0
 */
gint
lrg_input_buffer_get_buffer_frames (LrgInputBuffer *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return self->buffer_frames;
}

/**
 * lrg_input_buffer_set_buffer_frames:
 * @self: an #LrgInputBuffer
 * @buffer_frames: number of frames to buffer inputs
 *
 * Sets the number of frames inputs are buffered for.
 *
 * Since: 1.0
 */
void
lrg_input_buffer_set_buffer_frames (LrgInputBuffer *self,
                                     gint            buffer_frames)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (buffer_frames > 0);

    self->buffer_frames = buffer_frames;
}

/**
 * lrg_input_buffer_is_enabled:
 * @self: an #LrgInputBuffer
 *
 * Checks if the input buffer is enabled.
 *
 * Returns: %TRUE if enabled
 *
 * Since: 1.0
 */
gboolean
lrg_input_buffer_is_enabled (LrgInputBuffer *self)
{
    g_return_val_if_fail (self != NULL, FALSE);

    return self->enabled;
}

/**
 * lrg_input_buffer_set_enabled:
 * @self: an #LrgInputBuffer
 * @enabled: whether to enable the buffer
 *
 * Enables or disables the input buffer.
 * When disabled, record() does nothing and consume() always returns FALSE.
 *
 * Since: 1.0
 */
void
lrg_input_buffer_set_enabled (LrgInputBuffer *self,
                               gboolean        enabled)
{
    g_return_if_fail (self != NULL);

    self->enabled = enabled;

    /* Clear buffer when disabling */
    if (!enabled)
    {
        lrg_input_buffer_clear (self);
    }
}

/**
 * lrg_input_buffer_get_context:
 * @self: an #LrgInputBuffer
 *
 * Gets the current input context.
 *
 * Returns: the current #LrgInputContext
 *
 * Since: 1.0
 */
LrgInputContext
lrg_input_buffer_get_context (LrgInputBuffer *self)
{
    g_return_val_if_fail (self != NULL, LRG_INPUT_CONTEXT_GAMEPLAY);

    return self->current_context;
}

/**
 * lrg_input_buffer_set_context:
 * @self: an #LrgInputBuffer
 * @context: the new input context
 *
 * Sets the current input context.
 * Changing context clears the buffer to prevent stale inputs.
 *
 * Since: 1.0
 */
void
lrg_input_buffer_set_context (LrgInputBuffer  *self,
                               LrgInputContext  context)
{
    g_return_if_fail (self != NULL);

    if (self->current_context != context)
    {
        self->current_context = context;

        /* Clear buffer on context change to prevent stale inputs */
        lrg_input_buffer_clear (self);
    }
}

/* ========================================================================== */
/* Core Operations                                                            */
/* ========================================================================== */

/**
 * lrg_input_buffer_update:
 * @self: an #LrgInputBuffer
 *
 * Updates the input buffer. Call this once per frame to decrement
 * frame counters and remove expired inputs.
 *
 * Since: 1.0
 */
void
lrg_input_buffer_update (LrgInputBuffer *self)
{
    GList *iter;
    GList *next;

    g_return_if_fail (self != NULL);

    if (!self->enabled)
        return;

    /* Iterate through the queue and update/remove expired entries */
    iter = self->queue->head;
    while (iter != NULL)
    {
        LrgBufferedInput *input;

        input = (LrgBufferedInput *)iter->data;
        next = iter->next;

        input->frames_remaining--;

        if (input->frames_remaining <= 0)
        {
            /* Remove expired input */
            g_queue_delete_link (self->queue, iter);
            buffered_input_free (input);
        }

        iter = next;
    }
}

/**
 * lrg_input_buffer_record:
 * @self: an #LrgInputBuffer
 * @action: the action name to record
 *
 * Records an action press into the buffer.
 *
 * Since: 1.0
 */
void
lrg_input_buffer_record (LrgInputBuffer *self,
                          const gchar    *action)
{
    LrgBufferedInput *input;

    g_return_if_fail (self != NULL);
    g_return_if_fail (action != NULL);

    if (!self->enabled)
        return;

    /* Check if this action is already buffered */
    {
        GList *iter;

        for (iter = self->queue->head; iter != NULL; iter = iter->next)
        {
            LrgBufferedInput *existing;

            existing = (LrgBufferedInput *)iter->data;

            if (g_strcmp0 (existing->action_name, action) == 0)
            {
                /* Refresh the existing entry instead of adding duplicate */
                existing->frames_remaining = self->buffer_frames;
                existing->timestamp_usec = g_get_monotonic_time ();
                return;
            }
        }
    }

    /* Add new buffered input */
    input = buffered_input_new (action, self->buffer_frames);
    g_queue_push_tail (self->queue, input);
}

/**
 * lrg_input_buffer_consume:
 * @self: an #LrgInputBuffer
 * @action: the action name to consume
 * @required_context: the context required for this action
 *
 * Attempts to consume a buffered action.
 * If the action is found in the buffer and the context matches,
 * it is removed and %TRUE is returned. Otherwise %FALSE is returned.
 *
 * Returns: %TRUE if the action was consumed
 *
 * Since: 1.0
 */
gboolean
lrg_input_buffer_consume (LrgInputBuffer  *self,
                           const gchar     *action,
                           LrgInputContext  required_context)
{
    GList *iter;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (action != NULL, FALSE);

    if (!self->enabled)
        return FALSE;

    /* Check context */
    if (self->current_context != required_context)
        return FALSE;

    /* Search for the action */
    for (iter = self->queue->head; iter != NULL; iter = iter->next)
    {
        LrgBufferedInput *input;

        input = (LrgBufferedInput *)iter->data;

        if (g_strcmp0 (input->action_name, action) == 0)
        {
            /* Found it - consume and remove */
            g_queue_delete_link (self->queue, iter);
            buffered_input_free (input);
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * lrg_input_buffer_has_action:
 * @self: an #LrgInputBuffer
 * @action: the action name to check
 *
 * Checks if an action is in the buffer without consuming it.
 *
 * Returns: %TRUE if the action is buffered
 *
 * Since: 1.0
 */
gboolean
lrg_input_buffer_has_action (LrgInputBuffer *self,
                              const gchar    *action)
{
    GList *iter;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (action != NULL, FALSE);

    if (!self->enabled)
        return FALSE;

    for (iter = self->queue->head; iter != NULL; iter = iter->next)
    {
        LrgBufferedInput *input;

        input = (LrgBufferedInput *)iter->data;

        if (g_strcmp0 (input->action_name, action) == 0)
            return TRUE;
    }

    return FALSE;
}

/**
 * lrg_input_buffer_clear:
 * @self: an #LrgInputBuffer
 *
 * Clears all buffered inputs.
 *
 * Since: 1.0
 */
void
lrg_input_buffer_clear (LrgInputBuffer *self)
{
    g_return_if_fail (self != NULL);

    g_queue_free_full (self->queue, (GDestroyNotify)buffered_input_free);
    self->queue = g_queue_new ();
}

/**
 * lrg_input_buffer_get_length:
 * @self: an #LrgInputBuffer
 *
 * Gets the number of buffered inputs.
 *
 * Returns: the number of inputs in the buffer
 *
 * Since: 1.0
 */
guint
lrg_input_buffer_get_length (LrgInputBuffer *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return g_queue_get_length (self->queue);
}
