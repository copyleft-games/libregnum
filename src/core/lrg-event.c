/* lrg-event.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the LrgEvent interface.
 */

#include "lrg-event.h"
#include "../lrg-log.h"

G_DEFINE_INTERFACE (LrgEvent, lrg_event, G_TYPE_OBJECT)

static void
lrg_event_default_init (LrgEventInterface *iface)
{
    /* No default implementations - all methods must be provided */
}

/**
 * lrg_event_get_type_mask:
 * @self: an #LrgEvent
 *
 * Gets the event type as a bitmask value for listener matching.
 *
 * Returns: the event type bitmask
 *
 * Since: 1.0
 */
guint64
lrg_event_get_type_mask (LrgEvent *self)
{
    LrgEventInterface *iface;

    g_return_val_if_fail (LRG_IS_EVENT (self), 0);

    iface = LRG_EVENT_GET_IFACE (self);

    g_return_val_if_fail (iface->get_type_mask != NULL, 0);

    return iface->get_type_mask (self);
}

/**
 * lrg_event_is_cancelled:
 * @self: an #LrgEvent
 *
 * Checks if the event has been cancelled.
 *
 * Returns: %TRUE if cancelled
 *
 * Since: 1.0
 */
gboolean
lrg_event_is_cancelled (LrgEvent *self)
{
    LrgEventInterface *iface;

    g_return_val_if_fail (LRG_IS_EVENT (self), FALSE);

    iface = LRG_EVENT_GET_IFACE (self);

    g_return_val_if_fail (iface->is_cancelled != NULL, FALSE);

    return iface->is_cancelled (self);
}

/**
 * lrg_event_cancel:
 * @self: an #LrgEvent
 *
 * Cancels the event. This prevents further propagation to listeners.
 *
 * Since: 1.0
 */
void
lrg_event_cancel (LrgEvent *self)
{
    LrgEventInterface *iface;

    g_return_if_fail (LRG_IS_EVENT (self));

    iface = LRG_EVENT_GET_IFACE (self);

    g_return_if_fail (iface->cancel != NULL);

    iface->cancel (self);
}
