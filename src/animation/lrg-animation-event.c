/* lrg-animation-event.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-animation-event.h"

/**
 * SECTION:lrg-animation-event
 * @Title: LrgAnimationEvent
 * @Short_description: Animation event data
 *
 * #LrgAnimationEvent represents an event that fires at a specific
 * time during animation playback. Events can carry arbitrary data
 * and are useful for synchronizing game logic with animations.
 *
 * Common uses:
 * - Footstep sounds at precise walk cycle moments
 * - Particle effects when weapons hit
 * - Game state changes during cutscenes
 */

G_DEFINE_BOXED_TYPE (LrgAnimationEvent, lrg_animation_event,
                     lrg_animation_event_copy,
                     lrg_animation_event_free)

LrgAnimationEvent *
lrg_animation_event_new (gfloat        time,
                         const gchar  *name)
{
    LrgAnimationEvent *event;

    g_return_val_if_fail (name != NULL, NULL);

    event = g_slice_new0 (LrgAnimationEvent);
    event->time = time;
    event->name = g_strdup (name);
    event->data = NULL;

    return event;
}

LrgAnimationEvent *
lrg_animation_event_new_with_data (gfloat        time,
                                   const gchar  *name,
                                   GVariant     *data)
{
    LrgAnimationEvent *event;

    g_return_val_if_fail (name != NULL, NULL);

    event = g_slice_new0 (LrgAnimationEvent);
    event->time = time;
    event->name = g_strdup (name);

    if (data != NULL)
        event->data = g_variant_ref_sink (data);
    else
        event->data = NULL;

    return event;
}

LrgAnimationEvent *
lrg_animation_event_copy (const LrgAnimationEvent *event)
{
    LrgAnimationEvent *copy;

    g_return_val_if_fail (event != NULL, NULL);

    copy = g_slice_new0 (LrgAnimationEvent);
    copy->time = event->time;
    copy->name = g_strdup (event->name);

    if (event->data != NULL)
        copy->data = g_variant_ref (event->data);
    else
        copy->data = NULL;

    return copy;
}

void
lrg_animation_event_free (LrgAnimationEvent *event)
{
    g_return_if_fail (event != NULL);

    g_free (event->name);
    g_clear_pointer (&event->data, g_variant_unref);
    g_slice_free (LrgAnimationEvent, event);
}

gfloat
lrg_animation_event_get_time (const LrgAnimationEvent *event)
{
    g_return_val_if_fail (event != NULL, 0.0f);

    return event->time;
}

const gchar *
lrg_animation_event_get_name (const LrgAnimationEvent *event)
{
    g_return_val_if_fail (event != NULL, NULL);

    return event->name;
}

GVariant *
lrg_animation_event_get_data (const LrgAnimationEvent *event)
{
    g_return_val_if_fail (event != NULL, NULL);

    return event->data;
}

void
lrg_animation_event_set_data (LrgAnimationEvent *event,
                              GVariant          *data)
{
    g_return_if_fail (event != NULL);

    g_clear_pointer (&event->data, g_variant_unref);

    if (data != NULL)
        event->data = g_variant_ref_sink (data);
}

gint
lrg_animation_event_get_int (const LrgAnimationEvent *event,
                             const gchar             *key,
                             gint                     default_value)
{
    GVariant *value;

    g_return_val_if_fail (event != NULL, default_value);
    g_return_val_if_fail (key != NULL, default_value);

    if (event->data == NULL)
        return default_value;

    if (!g_variant_is_of_type (event->data, G_VARIANT_TYPE_VARDICT))
        return default_value;

    if (!g_variant_lookup (event->data, key, "i", &value))
        return default_value;

    return g_variant_get_int32 (value);
}

gfloat
lrg_animation_event_get_float (const LrgAnimationEvent *event,
                               const gchar             *key,
                               gfloat                   default_value)
{
    GVariant *value;

    g_return_val_if_fail (event != NULL, default_value);
    g_return_val_if_fail (key != NULL, default_value);

    if (event->data == NULL)
        return default_value;

    if (!g_variant_is_of_type (event->data, G_VARIANT_TYPE_VARDICT))
        return default_value;

    if (!g_variant_lookup (event->data, key, "d", &value))
        return default_value;

    return (gfloat)g_variant_get_double (value);
}

const gchar *
lrg_animation_event_get_string (const LrgAnimationEvent *event,
                                const gchar             *key,
                                const gchar             *default_value)
{
    const gchar *value;

    g_return_val_if_fail (event != NULL, default_value);
    g_return_val_if_fail (key != NULL, default_value);

    if (event->data == NULL)
        return default_value;

    if (!g_variant_is_of_type (event->data, G_VARIANT_TYPE_VARDICT))
        return default_value;

    if (!g_variant_lookup (event->data, key, "&s", &value))
        return default_value;

    return value;
}
