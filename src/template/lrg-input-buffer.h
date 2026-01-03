/* lrg-input-buffer.h - Input buffering for action games
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_INPUT_BUFFER_H
#define LRG_INPUT_BUFFER_H

#include <glib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LrgInputBuffer:
 *
 * An input buffer for frame-perfect action games.
 *
 * The input buffer stores recent inputs for a configurable number
 * of frames, allowing the game to be more forgiving with timing.
 * This is commonly used in fighting games, action games, and
 * platformers to make inputs feel responsive even when the player
 * presses a button slightly before it would be valid.
 *
 * Since: 1.0
 */
typedef struct _LrgInputBuffer LrgInputBuffer;

/**
 * LrgBufferedInput:
 * @action_name: the name of the action
 * @timestamp_usec: timestamp when the input was recorded
 * @frames_remaining: frames until this input expires
 *
 * A single buffered input entry.
 *
 * Since: 1.0
 */
typedef struct _LrgBufferedInput
{
    gchar  *action_name;
    gint64  timestamp_usec;
    gint    frames_remaining;
} LrgBufferedInput;

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
LRG_AVAILABLE_IN_ALL
LrgInputBuffer *
lrg_input_buffer_new (gint buffer_frames);

/**
 * lrg_input_buffer_free:
 * @self: (nullable): an #LrgInputBuffer
 *
 * Frees an input buffer.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_input_buffer_free (LrgInputBuffer *self);

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
LRG_AVAILABLE_IN_ALL
gint
lrg_input_buffer_get_buffer_frames (LrgInputBuffer *self);

/**
 * lrg_input_buffer_set_buffer_frames:
 * @self: an #LrgInputBuffer
 * @buffer_frames: number of frames to buffer inputs
 *
 * Sets the number of frames inputs are buffered for.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_input_buffer_set_buffer_frames (LrgInputBuffer *self,
                                     gint            buffer_frames);

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
LRG_AVAILABLE_IN_ALL
gboolean
lrg_input_buffer_is_enabled (LrgInputBuffer *self);

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
LRG_AVAILABLE_IN_ALL
void
lrg_input_buffer_set_enabled (LrgInputBuffer *self,
                               gboolean        enabled);

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
LRG_AVAILABLE_IN_ALL
LrgInputContext
lrg_input_buffer_get_context (LrgInputBuffer *self);

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
LRG_AVAILABLE_IN_ALL
void
lrg_input_buffer_set_context (LrgInputBuffer  *self,
                               LrgInputContext  context);

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
LRG_AVAILABLE_IN_ALL
void
lrg_input_buffer_update (LrgInputBuffer *self);

/**
 * lrg_input_buffer_record:
 * @self: an #LrgInputBuffer
 * @action: the action name to record
 *
 * Records an action press into the buffer.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_input_buffer_record (LrgInputBuffer *self,
                          const gchar    *action);

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
LRG_AVAILABLE_IN_ALL
gboolean
lrg_input_buffer_consume (LrgInputBuffer  *self,
                           const gchar     *action,
                           LrgInputContext  required_context);

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
LRG_AVAILABLE_IN_ALL
gboolean
lrg_input_buffer_has_action (LrgInputBuffer *self,
                              const gchar    *action);

/**
 * lrg_input_buffer_clear:
 * @self: an #LrgInputBuffer
 *
 * Clears all buffered inputs.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_input_buffer_clear (LrgInputBuffer *self);

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
LRG_AVAILABLE_IN_ALL
guint
lrg_input_buffer_get_length (LrgInputBuffer *self);

G_END_DECLS

#endif /* LRG_INPUT_BUFFER_H */
