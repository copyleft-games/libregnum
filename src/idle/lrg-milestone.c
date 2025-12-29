/* lrg-milestone.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-milestone.h"

/* Forward declarations for G_DEFINE_BOXED_TYPE */
LrgMilestone *lrg_milestone_copy (const LrgMilestone *src);
void lrg_milestone_free (LrgMilestone *self);

G_DEFINE_BOXED_TYPE (LrgMilestone, lrg_milestone,
                     lrg_milestone_copy,
                     lrg_milestone_free)

LrgMilestone *
lrg_milestone_new (const gchar        *id,
                   const gchar        *name,
                   const LrgBigNumber *threshold)
{
    LrgMilestone *self;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (threshold != NULL, NULL);

    self = g_slice_new0 (LrgMilestone);
    self->id = g_strdup (id);
    self->name = g_strdup (name);
    self->description = NULL;
    self->icon = NULL;
    self->threshold = lrg_big_number_copy (threshold);
    self->achieved = FALSE;
    self->achieved_time = 0;
    self->reward_multiplier = 1.0;

    return self;
}

LrgMilestone *
lrg_milestone_new_simple (const gchar *id,
                          const gchar *name,
                          gdouble      threshold)
{
    g_autoptr(LrgBigNumber) thresh = NULL;

    thresh = lrg_big_number_new (threshold);
    return lrg_milestone_new (id, name, thresh);
}

LrgMilestone *
lrg_milestone_copy (const LrgMilestone *self)
{
    LrgMilestone *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_slice_new0 (LrgMilestone);
    copy->id = g_strdup (self->id);
    copy->name = g_strdup (self->name);
    copy->description = g_strdup (self->description);
    copy->icon = g_strdup (self->icon);
    copy->threshold = lrg_big_number_copy (self->threshold);
    copy->achieved = self->achieved;
    copy->achieved_time = self->achieved_time;
    copy->reward_multiplier = self->reward_multiplier;

    return copy;
}

void
lrg_milestone_free (LrgMilestone *self)
{
    if (self == NULL)
        return;

    g_free (self->id);
    g_free (self->name);
    g_free (self->description);
    g_free (self->icon);
    lrg_big_number_free (self->threshold);
    g_slice_free (LrgMilestone, self);
}

const gchar *
lrg_milestone_get_id (const LrgMilestone *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->id;
}

const gchar *
lrg_milestone_get_name (const LrgMilestone *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->name;
}

const gchar *
lrg_milestone_get_description (const LrgMilestone *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->description;
}

void
lrg_milestone_set_description (LrgMilestone *self,
                               const gchar  *description)
{
    g_return_if_fail (self != NULL);

    g_free (self->description);
    self->description = g_strdup (description);
}

const LrgBigNumber *
lrg_milestone_get_threshold (const LrgMilestone *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->threshold;
}

const gchar *
lrg_milestone_get_icon (const LrgMilestone *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->icon;
}

void
lrg_milestone_set_icon (LrgMilestone *self,
                        const gchar  *icon)
{
    g_return_if_fail (self != NULL);

    g_free (self->icon);
    self->icon = g_strdup (icon);
}

gboolean
lrg_milestone_is_achieved (const LrgMilestone *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->achieved;
}

gint64
lrg_milestone_get_achieved_time (const LrgMilestone *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->achieved_time;
}

gboolean
lrg_milestone_check (LrgMilestone       *self,
                     const LrgBigNumber *value)
{
    g_autoptr(GDateTime) now = NULL;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

    /* Already achieved */
    if (self->achieved)
        return FALSE;

    /* Check if value >= threshold */
    if (lrg_big_number_compare (value, self->threshold) >= 0)
    {
        self->achieved = TRUE;
        now = g_date_time_new_now_utc ();
        self->achieved_time = g_date_time_to_unix (now);
        return TRUE;
    }

    return FALSE;
}

void
lrg_milestone_reset (LrgMilestone *self)
{
    g_return_if_fail (self != NULL);

    self->achieved = FALSE;
    self->achieved_time = 0;
}

gdouble
lrg_milestone_get_progress (const LrgMilestone *self,
                            const LrgBigNumber *current)
{
    gdouble current_val;
    gdouble threshold_val;

    g_return_val_if_fail (self != NULL, 0.0);
    g_return_val_if_fail (current != NULL, 0.0);

    if (self->achieved)
        return 1.0;

    current_val = lrg_big_number_to_double (current);
    threshold_val = lrg_big_number_to_double (self->threshold);

    if (threshold_val <= 0.0)
        return 1.0;

    return CLAMP (current_val / threshold_val, 0.0, 1.0);
}

gdouble
lrg_milestone_get_reward_multiplier (const LrgMilestone *self)
{
    g_return_val_if_fail (self != NULL, 1.0);
    return self->reward_multiplier;
}

void
lrg_milestone_set_reward_multiplier (LrgMilestone *self,
                                     gdouble       multiplier)
{
    g_return_if_fail (self != NULL);
    self->reward_multiplier = multiplier;
}
