/* lrg-tween-sequence.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sequential tween group that plays tweens one after another.
 */

#include "lrg-tween-sequence.h"
#include "lrg-tween.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TWEEN

/**
 * LrgTweenSequence:
 *
 * A tween group that plays its tweens sequentially.
 *
 * When started, a #LrgTweenSequence plays each tween one after another
 * in the order they were added. The sequence completes when the last
 * tween finishes.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * LrgTweenSequence *seq = lrg_tween_sequence_new ();
 *
 * // Fade in, wait, then fade out
 * lrg_tween_sequence_append (seq, fade_in_tween);
 * lrg_tween_sequence_append_interval (seq, 2.0); // Wait 2 seconds
 * lrg_tween_sequence_append (seq, fade_out_tween);
 *
 * lrg_tween_base_start (LRG_TWEEN_BASE (seq));
 * ]|
 *
 * Since: 1.0
 */

struct _LrgTweenSequence
{
    LrgTweenGroup  parent_instance;

    gint           current_index;
    gboolean       has_started;
};

G_DEFINE_FINAL_TYPE (LrgTweenSequence, lrg_tween_sequence, LRG_TYPE_TWEEN_GROUP)

/*
 * Forward to next tween in sequence.
 * Returns TRUE if there is a next tween, FALSE if sequence is complete.
 */
static gboolean
advance_to_next (LrgTweenSequence *self)
{
    LrgTweenGroup *group;
    guint count;

    group = LRG_TWEEN_GROUP (self);
    count = lrg_tween_group_get_tween_count (group);

    self->current_index++;

    if ((guint)self->current_index >= count)
    {
        return FALSE;
    }

    /* Start the next tween */
    {
        LrgTweenBase *next_tween;

        next_tween = lrg_tween_group_get_tween_at (group, (guint)self->current_index);
        if (next_tween != NULL)
        {
            lrg_tween_base_start (next_tween);
        }
    }

    return TRUE;
}

/*
 * LrgTweenBase virtual method overrides
 */

static void
lrg_tween_sequence_start (LrgTweenBase *base)
{
    LrgTweenSequence *self;
    LrgTweenGroup *group;
    LrgTweenBaseClass *parent_class;
    LrgTweenBase *first_tween;

    self = LRG_TWEEN_SEQUENCE (base);
    group = LRG_TWEEN_GROUP (self);

    self->current_index = 0;
    self->has_started = TRUE;

    /* Start the first tween */
    first_tween = lrg_tween_group_get_tween_at (group, 0);
    if (first_tween != NULL)
    {
        lrg_tween_base_start (first_tween);
    }

    /* Chain up to parent */
    parent_class = LRG_TWEEN_BASE_CLASS (lrg_tween_sequence_parent_class);
    if (parent_class->start != NULL)
    {
        parent_class->start (base);
    }
}

static void
lrg_tween_sequence_update (LrgTweenBase *base,
                           gfloat        delta_time)
{
    LrgTweenSequence *self;
    LrgTweenGroup *group;
    LrgTweenBase *current_tween;
    LrgTweenBaseClass *parent_class;

    self = LRG_TWEEN_SEQUENCE (base);
    group = LRG_TWEEN_GROUP (self);

    if (!self->has_started)
    {
        return;
    }

    /* Get current tween */
    current_tween = lrg_tween_group_get_tween_at (group, (guint)self->current_index);
    if (current_tween == NULL)
    {
        return;
    }

    /* Update current tween */
    lrg_tween_base_update (current_tween, delta_time);

    /* Check if current tween is finished */
    if (lrg_tween_base_is_finished (current_tween))
    {
        /* Try to advance to next */
        if (!advance_to_next (self))
        {
            /* Sequence is complete - but don't set state directly,
             * let parent class handle it via is_finished check */
        }
    }

    /* Chain up to parent for base state updates */
    parent_class = LRG_TWEEN_BASE_CLASS (lrg_tween_sequence_parent_class);
    if (parent_class->update != NULL)
    {
        parent_class->update (base, delta_time);
    }
}

static void
lrg_tween_sequence_reset (LrgTweenBase *base)
{
    LrgTweenSequence *self;
    LrgTweenBaseClass *parent_class;

    self = LRG_TWEEN_SEQUENCE (base);

    self->current_index = 0;
    self->has_started = FALSE;

    /* Chain up resets all child tweens */
    parent_class = LRG_TWEEN_BASE_CLASS (lrg_tween_sequence_parent_class);
    if (parent_class->reset != NULL)
    {
        parent_class->reset (base);
    }
}

static gboolean
lrg_tween_sequence_is_finished (LrgTweenBase *base)
{
    LrgTweenSequence *self;
    LrgTweenGroup *group;
    guint count;

    self = LRG_TWEEN_SEQUENCE (base);
    group = LRG_TWEEN_GROUP (self);
    count = lrg_tween_group_get_tween_count (group);

    if (count == 0)
    {
        return TRUE;
    }

    /* Finished when past the last tween */
    if ((guint)self->current_index >= count)
    {
        return TRUE;
    }

    /* Check if on last tween and it's finished */
    if ((guint)self->current_index == count - 1)
    {
        LrgTweenBase *last_tween;

        last_tween = lrg_tween_group_get_tween_at (group, (guint)self->current_index);
        if (last_tween != NULL && lrg_tween_base_is_finished (last_tween))
        {
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * GObject virtual methods
 */

static void
lrg_tween_sequence_class_init (LrgTweenSequenceClass *klass)
{
    LrgTweenBaseClass *tween_base_class;

    tween_base_class = LRG_TWEEN_BASE_CLASS (klass);
    tween_base_class->start = lrg_tween_sequence_start;
    tween_base_class->update = lrg_tween_sequence_update;
    tween_base_class->reset = lrg_tween_sequence_reset;
    tween_base_class->is_finished = lrg_tween_sequence_is_finished;
}

static void
lrg_tween_sequence_init (LrgTweenSequence *self)
{
    self->current_index = 0;
    self->has_started = FALSE;
}

/*
 * Public API
 */

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
LrgTweenSequence *
lrg_tween_sequence_new (void)
{
    return g_object_new (LRG_TYPE_TWEEN_SEQUENCE, NULL);
}

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
LrgTweenSequence *
lrg_tween_sequence_append (LrgTweenSequence *self,
                           LrgTweenBase     *tween)
{
    g_return_val_if_fail (LRG_IS_TWEEN_SEQUENCE (self), NULL);
    g_return_val_if_fail (LRG_IS_TWEEN_BASE (tween), NULL);

    lrg_tween_group_add_tween (LRG_TWEEN_GROUP (self), tween);

    return self;
}

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
LrgTweenSequence *
lrg_tween_sequence_append_interval (LrgTweenSequence *self,
                                    gfloat            duration)
{
    LrgTweenBase *interval;

    g_return_val_if_fail (LRG_IS_TWEEN_SEQUENCE (self), NULL);
    g_return_val_if_fail (duration >= 0.0f, NULL);

    /*
     * Create a simple "interval" tween using the concrete LrgTween type.
     * It has all the timing logic but does nothing if no target/property
     * is set - perfect for a delay.
     */
    interval = g_object_new (LRG_TYPE_TWEEN,
                             "duration", duration,
                             NULL);

    lrg_tween_group_add_tween (LRG_TWEEN_GROUP (self), interval);
    g_object_unref (interval);

    return self;
}

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
gint
lrg_tween_sequence_get_current_index (LrgTweenSequence *self)
{
    g_return_val_if_fail (LRG_IS_TWEEN_SEQUENCE (self), -1);

    if (!self->has_started)
    {
        return -1;
    }

    return self->current_index;
}

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
LrgTweenBase *
lrg_tween_sequence_get_current_tween (LrgTweenSequence *self)
{
    g_return_val_if_fail (LRG_IS_TWEEN_SEQUENCE (self), NULL);

    if (!self->has_started || self->current_index < 0)
    {
        return NULL;
    }

    return lrg_tween_group_get_tween_at (LRG_TWEEN_GROUP (self),
                                         (guint)self->current_index);
}
