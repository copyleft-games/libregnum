/* lrg-achievement-progress.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAchievementProgress - Boxed type for achievement progress tracking.
 */

#include "config.h"

#include "lrg-achievement-progress.h"

struct _LrgAchievementProgress
{
    gint ref_count;
    gint64 current;
    gint64 target;
};

G_DEFINE_BOXED_TYPE (LrgAchievementProgress,
                     lrg_achievement_progress,
                     lrg_achievement_progress_copy,
                     lrg_achievement_progress_free)

LrgAchievementProgress *
lrg_achievement_progress_new (gint64 current,
                              gint64 target)
{
    LrgAchievementProgress *self;

    self = g_slice_new0 (LrgAchievementProgress);
    self->ref_count = 1;
    self->current = current;
    self->target = target > 0 ? target : 1;  /* Avoid division by zero */

    return self;
}

LrgAchievementProgress *
lrg_achievement_progress_copy (const LrgAchievementProgress *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return lrg_achievement_progress_new (self->current, self->target);
}

void
lrg_achievement_progress_free (LrgAchievementProgress *self)
{
    if (self == NULL)
        return;

    g_slice_free (LrgAchievementProgress, self);
}

gint64
lrg_achievement_progress_get_current (const LrgAchievementProgress *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return self->current;
}

void
lrg_achievement_progress_set_current (LrgAchievementProgress *self,
                                      gint64                  current)
{
    g_return_if_fail (self != NULL);

    self->current = current;
}

gint64
lrg_achievement_progress_get_target (const LrgAchievementProgress *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return self->target;
}

void
lrg_achievement_progress_set_target (LrgAchievementProgress *self,
                                     gint64                  target)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (target > 0);

    self->target = target;
}

gboolean
lrg_achievement_progress_is_complete (const LrgAchievementProgress *self)
{
    g_return_val_if_fail (self != NULL, FALSE);

    return self->current >= self->target;
}

gdouble
lrg_achievement_progress_get_percentage (const LrgAchievementProgress *self)
{
    gdouble percentage;

    g_return_val_if_fail (self != NULL, 0.0);

    if (self->target <= 0)
        return 0.0;

    percentage = (gdouble)self->current / (gdouble)self->target;

    return CLAMP (percentage, 0.0, 1.0);
}

void
lrg_achievement_progress_increment (LrgAchievementProgress *self,
                                    gint64                  amount)
{
    g_return_if_fail (self != NULL);

    self->current += amount;
}

void
lrg_achievement_progress_reset (LrgAchievementProgress *self)
{
    g_return_if_fail (self != NULL);

    self->current = 0;
}
