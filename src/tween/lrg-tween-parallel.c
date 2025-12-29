/* lrg-tween-parallel.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Parallel tween group that plays all tweens simultaneously.
 */

#include "lrg-tween-parallel.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TWEEN

/**
 * LrgTweenParallel:
 *
 * A tween group that plays all tweens simultaneously.
 *
 * When started, a #LrgTweenParallel starts all its child tweens at once.
 * The parallel group completes when ALL child tweens have finished.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * LrgTweenParallel *parallel = lrg_tween_parallel_new ();
 *
 * // Animate position and opacity at the same time
 * lrg_tween_parallel_add (parallel, position_tween);
 * lrg_tween_parallel_add (parallel, opacity_tween);
 *
 * lrg_tween_base_start (LRG_TWEEN_BASE (parallel));
 * ]|
 *
 * Since: 1.0
 */

struct _LrgTweenParallel
{
    LrgTweenGroup  parent_instance;

    gboolean       has_started;
};

G_DEFINE_FINAL_TYPE (LrgTweenParallel, lrg_tween_parallel, LRG_TYPE_TWEEN_GROUP)

/*
 * Helper to count finished tweens
 */
static guint
count_finished_tweens (LrgTweenParallel *self)
{
    LrgTweenGroup *group;
    GPtrArray *tweens;
    guint count;
    guint i;

    group = LRG_TWEEN_GROUP (self);
    tweens = lrg_tween_group_get_tweens (group);
    count = 0;

    for (i = 0; i < tweens->len; i++)
    {
        LrgTweenBase *tween;

        tween = g_ptr_array_index (tweens, i);
        if (lrg_tween_base_is_finished (tween))
        {
            count++;
        }
    }

    return count;
}

/*
 * LrgTweenBase virtual method overrides
 */

static void
lrg_tween_parallel_start (LrgTweenBase *base)
{
    LrgTweenParallel *self;
    LrgTweenGroup *group;
    LrgTweenBaseClass *parent_class;
    GPtrArray *tweens;
    guint i;

    self = LRG_TWEEN_PARALLEL (base);
    group = LRG_TWEEN_GROUP (self);

    self->has_started = TRUE;

    /* Start all tweens */
    tweens = lrg_tween_group_get_tweens (group);
    for (i = 0; i < tweens->len; i++)
    {
        LrgTweenBase *tween;

        tween = g_ptr_array_index (tweens, i);
        lrg_tween_base_start (tween);
    }

    /* Chain up to parent */
    parent_class = LRG_TWEEN_BASE_CLASS (lrg_tween_parallel_parent_class);
    if (parent_class->start != NULL)
    {
        parent_class->start (base);
    }
}

static void
lrg_tween_parallel_update (LrgTweenBase *base,
                           gfloat        delta_time)
{
    LrgTweenParallel *self;
    LrgTweenGroup *group;
    LrgTweenBaseClass *parent_class;
    GPtrArray *tweens;
    guint i;

    self = LRG_TWEEN_PARALLEL (base);
    group = LRG_TWEEN_GROUP (self);

    if (!self->has_started)
    {
        return;
    }

    /* Update all running tweens */
    tweens = lrg_tween_group_get_tweens (group);
    for (i = 0; i < tweens->len; i++)
    {
        LrgTweenBase *tween;

        tween = g_ptr_array_index (tweens, i);

        /* Only update tweens that haven't finished */
        if (!lrg_tween_base_is_finished (tween))
        {
            lrg_tween_base_update (tween, delta_time);
        }
    }

    /* Chain up to parent for base state updates */
    parent_class = LRG_TWEEN_BASE_CLASS (lrg_tween_parallel_parent_class);
    if (parent_class->update != NULL)
    {
        parent_class->update (base, delta_time);
    }
}

static void
lrg_tween_parallel_reset (LrgTweenBase *base)
{
    LrgTweenParallel *self;
    LrgTweenBaseClass *parent_class;

    self = LRG_TWEEN_PARALLEL (base);

    self->has_started = FALSE;

    /* Chain up resets all child tweens */
    parent_class = LRG_TWEEN_BASE_CLASS (lrg_tween_parallel_parent_class);
    if (parent_class->reset != NULL)
    {
        parent_class->reset (base);
    }
}

static gboolean
lrg_tween_parallel_is_finished (LrgTweenBase *base)
{
    LrgTweenParallel *self;
    LrgTweenGroup *group;
    GPtrArray *tweens;
    guint i;

    self = LRG_TWEEN_PARALLEL (base);
    group = LRG_TWEEN_GROUP (self);
    tweens = lrg_tween_group_get_tweens (group);

    if (tweens->len == 0)
    {
        return TRUE;
    }

    /* Finished when ALL tweens are finished */
    for (i = 0; i < tweens->len; i++)
    {
        LrgTweenBase *tween;

        tween = g_ptr_array_index (tweens, i);
        if (!lrg_tween_base_is_finished (tween))
        {
            return FALSE;
        }
    }

    return TRUE;
}

/*
 * GObject virtual methods
 */

static void
lrg_tween_parallel_class_init (LrgTweenParallelClass *klass)
{
    LrgTweenBaseClass *tween_base_class;

    tween_base_class = LRG_TWEEN_BASE_CLASS (klass);
    tween_base_class->start = lrg_tween_parallel_start;
    tween_base_class->update = lrg_tween_parallel_update;
    tween_base_class->reset = lrg_tween_parallel_reset;
    tween_base_class->is_finished = lrg_tween_parallel_is_finished;
}

static void
lrg_tween_parallel_init (LrgTweenParallel *self)
{
    self->has_started = FALSE;
}

/*
 * Public API
 */

/**
 * lrg_tween_parallel_new:
 *
 * Creates a new parallel tween group.
 * All tweens added to a parallel group play simultaneously.
 *
 * Returns: (transfer full): A new #LrgTweenParallel
 *
 * Since: 1.0
 */
LrgTweenParallel *
lrg_tween_parallel_new (void)
{
    return g_object_new (LRG_TYPE_TWEEN_PARALLEL, NULL);
}

/**
 * lrg_tween_parallel_add:
 * @self: A #LrgTweenParallel
 * @tween: (transfer none): The tween to add
 *
 * Adds a tween to the parallel group.
 * This is equivalent to lrg_tween_group_add_tween().
 *
 * Returns: (transfer none): @self for method chaining
 *
 * Since: 1.0
 */
LrgTweenParallel *
lrg_tween_parallel_add (LrgTweenParallel *self,
                        LrgTweenBase     *tween)
{
    g_return_val_if_fail (LRG_IS_TWEEN_PARALLEL (self), NULL);
    g_return_val_if_fail (LRG_IS_TWEEN_BASE (tween), NULL);

    lrg_tween_group_add_tween (LRG_TWEEN_GROUP (self), tween);

    return self;
}

/**
 * lrg_tween_parallel_get_finished_count:
 * @self: A #LrgTweenParallel
 *
 * Gets the number of tweens that have finished.
 *
 * Returns: The number of finished tweens
 *
 * Since: 1.0
 */
guint
lrg_tween_parallel_get_finished_count (LrgTweenParallel *self)
{
    g_return_val_if_fail (LRG_IS_TWEEN_PARALLEL (self), 0);

    return count_finished_tweens (self);
}

/**
 * lrg_tween_parallel_get_running_count:
 * @self: A #LrgTweenParallel
 *
 * Gets the number of tweens that are still running.
 *
 * Returns: The number of running tweens
 *
 * Since: 1.0
 */
guint
lrg_tween_parallel_get_running_count (LrgTweenParallel *self)
{
    LrgTweenGroup *group;
    guint total;
    guint finished;

    g_return_val_if_fail (LRG_IS_TWEEN_PARALLEL (self), 0);

    group = LRG_TWEEN_GROUP (self);
    total = lrg_tween_group_get_tween_count (group);
    finished = count_finished_tweens (self);

    return total - finished;
}
