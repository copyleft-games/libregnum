/* lrg-animation-event.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Animation events triggered during playback.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ANIMATION_EVENT (lrg_animation_event_get_type ())

typedef struct _LrgAnimationEvent LrgAnimationEvent;

/**
 * LrgAnimationEvent:
 *
 * An event embedded in an animation clip that fires at a
 * specific time during playback.
 *
 * Animation events are useful for triggering sound effects,
 * particle systems, or game logic at precise moments.
 *
 * Since: 1.0
 */
struct _LrgAnimationEvent
{
    /*< public >*/
    gfloat       time;         /* Time in seconds when event fires */
    gchar       *name;         /* Event name/identifier */
    GVariant    *data;         /* Optional event data */
};

LRG_AVAILABLE_IN_ALL
GType                  lrg_animation_event_get_type     (void) G_GNUC_CONST;

/**
 * lrg_animation_event_new:
 * @time: The time in seconds when the event fires
 * @name: The event name
 *
 * Creates a new animation event.
 *
 * Returns: (transfer full): A new #LrgAnimationEvent
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationEvent *    lrg_animation_event_new          (gfloat        time,
                                                         const gchar  *name);

/**
 * lrg_animation_event_new_with_data:
 * @time: The time in seconds when the event fires
 * @name: The event name
 * @data: (nullable) (transfer none): Event data as a GVariant
 *
 * Creates a new animation event with attached data.
 *
 * Returns: (transfer full): A new #LrgAnimationEvent
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationEvent *    lrg_animation_event_new_with_data (gfloat        time,
                                                          const gchar  *name,
                                                          GVariant     *data);

/**
 * lrg_animation_event_copy:
 * @event: A #LrgAnimationEvent
 *
 * Creates a deep copy of the event.
 *
 * Returns: (transfer full): A new #LrgAnimationEvent
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationEvent *    lrg_animation_event_copy         (const LrgAnimationEvent *event);

/**
 * lrg_animation_event_free:
 * @event: A #LrgAnimationEvent
 *
 * Frees an animation event.
 */
LRG_AVAILABLE_IN_ALL
void                   lrg_animation_event_free         (LrgAnimationEvent *event);

/**
 * lrg_animation_event_get_time:
 * @event: A #LrgAnimationEvent
 *
 * Gets the event trigger time.
 *
 * Returns: The time in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat                 lrg_animation_event_get_time     (const LrgAnimationEvent *event);

/**
 * lrg_animation_event_get_name:
 * @event: A #LrgAnimationEvent
 *
 * Gets the event name.
 *
 * Returns: (transfer none): The event name
 */
LRG_AVAILABLE_IN_ALL
const gchar *          lrg_animation_event_get_name     (const LrgAnimationEvent *event);

/**
 * lrg_animation_event_get_data:
 * @event: A #LrgAnimationEvent
 *
 * Gets the event data.
 *
 * Returns: (transfer none) (nullable): The event data as GVariant
 */
LRG_AVAILABLE_IN_ALL
GVariant *             lrg_animation_event_get_data     (const LrgAnimationEvent *event);

/**
 * lrg_animation_event_set_data:
 * @event: A #LrgAnimationEvent
 * @data: (nullable) (transfer none): Event data
 *
 * Sets the event data.
 */
LRG_AVAILABLE_IN_ALL
void                   lrg_animation_event_set_data     (LrgAnimationEvent *event,
                                                         GVariant          *data);

/**
 * lrg_animation_event_get_int:
 * @event: A #LrgAnimationEvent
 * @key: The key to look up
 * @default_value: Default value if key not found
 *
 * Gets an integer from the event data dictionary.
 *
 * Returns: The integer value or default
 */
LRG_AVAILABLE_IN_ALL
gint                   lrg_animation_event_get_int      (const LrgAnimationEvent *event,
                                                         const gchar             *key,
                                                         gint                     default_value);

/**
 * lrg_animation_event_get_float:
 * @event: A #LrgAnimationEvent
 * @key: The key to look up
 * @default_value: Default value if key not found
 *
 * Gets a float from the event data dictionary.
 *
 * Returns: The float value or default
 */
LRG_AVAILABLE_IN_ALL
gfloat                 lrg_animation_event_get_float    (const LrgAnimationEvent *event,
                                                         const gchar             *key,
                                                         gfloat                   default_value);

/**
 * lrg_animation_event_get_string:
 * @event: A #LrgAnimationEvent
 * @key: The key to look up
 * @default_value: Default value if key not found
 *
 * Gets a string from the event data dictionary.
 *
 * Returns: (transfer none) (nullable): The string value or default
 */
LRG_AVAILABLE_IN_ALL
const gchar *          lrg_animation_event_get_string   (const LrgAnimationEvent *event,
                                                         const gchar             *key,
                                                         const gchar             *default_value);

G_END_DECLS
