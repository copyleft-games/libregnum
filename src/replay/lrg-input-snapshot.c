/* lrg-input-snapshot.c - One tick of action input for one player
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#include "lrg-input-snapshot.h"
#include "../input/lrg-input-action.h"

#include <string.h>

struct _LrgInputSnapshot
{
    guint64 down;        /* bit N: action N held */
    guint64 axis_mask;   /* bit N: axes[N] holds an explicit value */
    gfloat  axes[LRG_INPUT_SNAPSHOT_MAX_ACTIONS];
};

G_DEFINE_BOXED_TYPE (LrgInputSnapshot, lrg_input_snapshot,
                     lrg_input_snapshot_copy, lrg_input_snapshot_free)

LrgInputSnapshot *
lrg_input_snapshot_new (void)
{
    return g_new0 (LrgInputSnapshot, 1);
}

LrgInputSnapshot *
lrg_input_snapshot_copy (const LrgInputSnapshot *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return g_memdup2 (self, sizeof (LrgInputSnapshot));
}

void
lrg_input_snapshot_free (LrgInputSnapshot *self)
{
    g_free (self);
}

void
lrg_input_snapshot_assign (LrgInputSnapshot       *self,
                           const LrgInputSnapshot *other)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (other != NULL);

    memcpy (self, other, sizeof (LrgInputSnapshot));
}

void
lrg_input_snapshot_clear (LrgInputSnapshot *self)
{
    g_return_if_fail (self != NULL);

    memset (self, 0, sizeof (LrgInputSnapshot));
}

gboolean
lrg_input_snapshot_equal (const LrgInputSnapshot *a,
                          const LrgInputSnapshot *b)
{
    guint i;

    g_return_val_if_fail (a != NULL, FALSE);
    g_return_val_if_fail (b != NULL, FALSE);

    if (a->down != b->down || a->axis_mask != b->axis_mask)
        return FALSE;

    /* Compare bit patterns: NaN == NaN and -0 != +0 here, which is what
     * determinism needs (the simulation sees exactly these bits) */
    for (i = 0; i < LRG_INPUT_SNAPSHOT_MAX_ACTIONS; i++)
    {
        if ((a->axis_mask & (G_GUINT64_CONSTANT (1) << i)) != 0 &&
            memcmp (&a->axes[i], &b->axes[i], sizeof (gfloat)) != 0)
            return FALSE;
    }
    return TRUE;
}

guint64
lrg_input_snapshot_get_down_mask (const LrgInputSnapshot *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return self->down;
}

void
lrg_input_snapshot_set_down_mask (LrgInputSnapshot *self,
                                  guint64           mask)
{
    g_return_if_fail (self != NULL);

    self->down = mask;
}

gboolean
lrg_input_snapshot_get_down (const LrgInputSnapshot *self,
                             guint                   action)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (action < LRG_INPUT_SNAPSHOT_MAX_ACTIONS, FALSE);

    return (self->down & (G_GUINT64_CONSTANT (1) << action)) != 0;
}

void
lrg_input_snapshot_set_down (LrgInputSnapshot *self,
                             guint             action,
                             gboolean          down)
{
    guint64 bit;

    g_return_if_fail (self != NULL);
    g_return_if_fail (action < LRG_INPUT_SNAPSHOT_MAX_ACTIONS);

    bit = G_GUINT64_CONSTANT (1) << action;
    if (down)
        self->down |= bit;
    else
        self->down &= ~bit;
}

gfloat
lrg_input_snapshot_get_axis (const LrgInputSnapshot *self,
                             guint                   action)
{
    guint64 bit;

    g_return_val_if_fail (self != NULL, 0.0f);
    g_return_val_if_fail (action < LRG_INPUT_SNAPSHOT_MAX_ACTIONS, 0.0f);

    bit = G_GUINT64_CONSTANT (1) << action;
    if ((self->axis_mask & bit) != 0)
        return self->axes[action];

    /* No explicit value: the held bit implies it */
    return (self->down & bit) != 0 ? 1.0f : 0.0f;
}

void
lrg_input_snapshot_set_axis (LrgInputSnapshot *self,
                             guint             action,
                             gfloat            value)
{
    guint64 bit;
    gfloat implied;

    g_return_if_fail (self != NULL);
    g_return_if_fail (action < LRG_INPUT_SNAPSHOT_MAX_ACTIONS);

    bit = G_GUINT64_CONSTANT (1) << action;
    implied = (self->down & bit) != 0 ? 1.0f : 0.0f;

    /* Store only values the held bit does not already imply, so digital
     * actions cost nothing in the encoding */
    if (memcmp (&value, &implied, sizeof (gfloat)) == 0)
    {
        self->axis_mask &= ~bit;
        self->axes[action] = 0.0f;
    }
    else
    {
        self->axis_mask |= bit;
        self->axes[action] = value;
    }
}

LrgInputSnapshot *
lrg_input_snapshot_capture (LrgInputMap        *map,
                            const gchar *const *actions)
{
    LrgInputSnapshot *snapshot;
    guint i;

    g_return_val_if_fail (LRG_IS_INPUT_MAP (map), NULL);
    g_return_val_if_fail (actions != NULL, NULL);

    snapshot = lrg_input_snapshot_new ();
    for (i = 0; actions[i] != NULL && i < LRG_INPUT_SNAPSHOT_MAX_ACTIONS; i++)
    {
        LrgInputAction *action;

        action = lrg_input_map_get_action (map, actions[i]);
        if (action == NULL)
            continue;

        /* A tap that began and ended between two polls is still a press */
        lrg_input_snapshot_set_down (snapshot, i,
                                     lrg_input_action_is_down (action) ||
                                     lrg_input_action_is_pressed (action));
        lrg_input_snapshot_set_axis (snapshot, i, lrg_input_action_get_axis (action));
    }
    return snapshot;
}

void
lrg_input_snapshot_apply (const LrgInputSnapshot *self,
                          const LrgInputSnapshot *previous,
                          LrgInputMap            *map,
                          const gchar *const     *actions)
{
    guint64 before;
    guint i;

    g_return_if_fail (self != NULL);
    g_return_if_fail (LRG_IS_INPUT_MAP (map));
    g_return_if_fail (actions != NULL);

    before = previous != NULL ? previous->down : 0;
    for (i = 0; actions[i] != NULL && i < LRG_INPUT_SNAPSHOT_MAX_ACTIONS; i++)
    {
        LrgInputAction *action;
        guint64 bit;
        gboolean down;
        gboolean was_down;

        action = lrg_input_map_get_action (map, actions[i]);
        if (action == NULL)
            continue;

        bit = G_GUINT64_CONSTANT (1) << i;
        down = (self->down & bit) != 0;
        was_down = (before & bit) != 0;
        lrg_input_action_set_override (action, down,
                                       down && !was_down,
                                       !down && was_down,
                                       lrg_input_snapshot_get_axis (self, i));
    }
}

void
lrg_input_snapshot_write (const LrgInputSnapshot *self,
                          LrgStateBuffer         *buffer)
{
    guint i;

    g_return_if_fail (self != NULL);
    g_return_if_fail (LRG_IS_STATE_BUFFER (buffer));

    lrg_state_buffer_write_uint64 (buffer, self->down);
    lrg_state_buffer_write_uint64 (buffer, self->axis_mask);
    for (i = 0; i < LRG_INPUT_SNAPSHOT_MAX_ACTIONS; i++)
    {
        if ((self->axis_mask & (G_GUINT64_CONSTANT (1) << i)) != 0)
            lrg_state_buffer_write_float (buffer, self->axes[i]);
    }
}

gboolean
lrg_input_snapshot_read (LrgInputSnapshot *self,
                         LrgStateBuffer   *buffer)
{
    guint i;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (LRG_IS_STATE_BUFFER (buffer), FALSE);

    lrg_input_snapshot_clear (self);
    self->down = lrg_state_buffer_read_uint64 (buffer);
    self->axis_mask = lrg_state_buffer_read_uint64 (buffer);
    for (i = 0; i < LRG_INPUT_SNAPSHOT_MAX_ACTIONS; i++)
    {
        if ((self->axis_mask & (G_GUINT64_CONSTANT (1) << i)) != 0)
            self->axes[i] = lrg_state_buffer_read_float (buffer);
    }
    return lrg_state_buffer_check (buffer, NULL);
}
