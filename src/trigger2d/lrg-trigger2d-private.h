/* lrg-trigger2d-private.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private data structure for LrgTrigger2D.
 */

#pragma once

#include "lrg-trigger2d.h"

G_BEGIN_DECLS

/**
 * LrgTrigger2DPrivate:
 *
 * Private data for #LrgTrigger2D instances.
 * Subclasses can access this via lrg_trigger2d_get_instance_private().
 */
typedef struct _LrgTrigger2DPrivate
{
    /* Identification */
    gchar      *id;

    /* State flags */
    gboolean    enabled;
    gboolean    one_shot;
    gboolean    has_fired;

    /* Cooldown */
    gfloat      cooldown;
    gfloat      cooldown_remaining;

    /* Collision filtering */
    guint32     collision_layer;
    guint32     collision_mask;

    /* User data */
    gpointer    user_data;
    GDestroyNotify user_data_destroy;
} LrgTrigger2DPrivate;

/**
 * lrg_trigger2d_mark_fired:
 * @self: A #LrgTrigger2D
 *
 * Marks the trigger as having fired (for one-shot triggers)
 * and starts the cooldown timer.
 *
 * This is called internally when a trigger event occurs.
 */
void lrg_trigger2d_mark_fired (LrgTrigger2D *self);

G_END_DECLS
