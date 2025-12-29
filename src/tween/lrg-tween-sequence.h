/* lrg-tween-sequence.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sequential tween group that plays tweens one after another.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-tween-group.h"

G_BEGIN_DECLS

#define LRG_TYPE_TWEEN_SEQUENCE (lrg_tween_sequence_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTweenSequence, lrg_tween_sequence, LRG, TWEEN_SEQUENCE, LrgTweenGroup)

/**
 * lrg_tween_sequence_new:
 *
 * Creates a new tween sequence.
 * Tweens added to a sequence play one after another in the order they were added.
 *
 * Returns: (transfer full): A new #LrgTweenSequence
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTweenSequence *  lrg_tween_sequence_new              (void);

/**
 * lrg_tween_sequence_append:
 * @self: A #LrgTweenSequence
 * @tween: (transfer none): The tween to append
 *
 * Appends a tween to the end of the sequence.
 * This is equivalent to lrg_tween_group_add_tween().
 *
 * Returns: (transfer none): @self for method chaining
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTweenSequence *  lrg_tween_sequence_append           (LrgTweenSequence    *self,
                                                         LrgTweenBase        *tween);

/**
 * lrg_tween_sequence_append_interval:
 * @self: A #LrgTweenSequence
 * @duration: Duration of the delay in seconds
 *
 * Appends a delay interval to the sequence.
 * This creates a tween that does nothing but wait.
 *
 * Returns: (transfer none): @self for method chaining
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTweenSequence *  lrg_tween_sequence_append_interval  (LrgTweenSequence    *self,
                                                         gfloat               duration);

/**
 * lrg_tween_sequence_get_current_index:
 * @self: A #LrgTweenSequence
 *
 * Gets the index of the currently playing tween.
 *
 * Returns: The current tween index, or -1 if not playing
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_tween_sequence_get_current_index (LrgTweenSequence   *self);

/**
 * lrg_tween_sequence_get_current_tween:
 * @self: A #LrgTweenSequence
 *
 * Gets the currently playing tween.
 *
 * Returns: (transfer none) (nullable): The current tween, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTweenBase *      lrg_tween_sequence_get_current_tween (LrgTweenSequence   *self);

G_END_DECLS
