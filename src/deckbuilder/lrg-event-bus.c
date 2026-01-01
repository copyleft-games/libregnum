/* lrg-event-bus.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEventBus - Central event dispatch system implementation.
 */

#include "lrg-event-bus.h"
#include "../lrg-log.h"

struct _LrgEventBus
{
    GObject    parent_instance;

    GPtrArray *listeners;       /* Array of LrgTriggerListener */
    gboolean   listeners_dirty; /* TRUE if needs re-sorting */
};

G_DEFINE_FINAL_TYPE (LrgEventBus, lrg_event_bus, G_TYPE_OBJECT)

enum
{
    SIGNAL_EVENT_EMITTED,
    SIGNAL_EVENT_CANCELLED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Singleton
 * ========================================================================== */

static LrgEventBus *default_event_bus = NULL;

/**
 * lrg_event_bus_get_default:
 *
 * Gets the default event bus singleton.
 *
 * Returns: (transfer none): the default #LrgEventBus
 *
 * Since: 1.0
 */
LrgEventBus *
lrg_event_bus_get_default (void)
{
    static gsize init = 0;

    if (g_once_init_enter (&init))
    {
        default_event_bus = lrg_event_bus_new ();
        g_once_init_leave (&init, 1);
    }

    return default_event_bus;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_event_bus_finalize (GObject *object)
{
    LrgEventBus *self = LRG_EVENT_BUS (object);

    g_clear_pointer (&self->listeners, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_event_bus_parent_class)->finalize (object);
}

static void
lrg_event_bus_class_init (LrgEventBusClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_event_bus_finalize;

    /**
     * LrgEventBus::event-emitted:
     * @self: the #LrgEventBus
     * @event: the event that was emitted
     *
     * Emitted after an event has been dispatched to all listeners.
     *
     * Since: 1.0
     */
    signals[SIGNAL_EVENT_EMITTED] =
        g_signal_new ("event-emitted",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_CARD_EVENT);

    /**
     * LrgEventBus::event-cancelled:
     * @self: the #LrgEventBus
     * @event: the event that was cancelled
     * @listener: the listener that cancelled it
     *
     * Emitted when an event is cancelled by a listener.
     *
     * Since: 1.0
     */
    signals[SIGNAL_EVENT_CANCELLED] =
        g_signal_new ("event-cancelled",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      LRG_TYPE_CARD_EVENT,
                      LRG_TYPE_TRIGGER_LISTENER);
}

static void
lrg_event_bus_init (LrgEventBus *self)
{
    self->listeners = g_ptr_array_new_with_free_func (g_object_unref);
    self->listeners_dirty = FALSE;
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_event_bus_new:
 *
 * Creates a new event bus. Use this for isolated combat contexts
 * rather than the global singleton.
 *
 * Returns: (transfer full): a new #LrgEventBus
 *
 * Since: 1.0
 */
LrgEventBus *
lrg_event_bus_new (void)
{
    return g_object_new (LRG_TYPE_EVENT_BUS, NULL);
}

/* ==========================================================================
 * Internal Helpers
 * ========================================================================== */

/* Sort comparison: higher priority comes first */
static gint
compare_listener_priority (gconstpointer a, gconstpointer b)
{
    LrgTriggerListener *listener_a = *(LrgTriggerListener **)a;
    LrgTriggerListener *listener_b = *(LrgTriggerListener **)b;
    gint priority_a = lrg_trigger_listener_get_priority (listener_a);
    gint priority_b = lrg_trigger_listener_get_priority (listener_b);

    /* Descending order (higher priority first) */
    return priority_b - priority_a;
}

static void
ensure_sorted (LrgEventBus *self)
{
    if (self->listeners_dirty && self->listeners->len > 1)
    {
        g_ptr_array_sort (self->listeners, compare_listener_priority);
        self->listeners_dirty = FALSE;
    }
}

/* ==========================================================================
 * Listener Management
 * ========================================================================== */

/**
 * lrg_event_bus_register:
 * @self: an #LrgEventBus
 * @listener: (transfer none): the listener to register
 *
 * Registers a trigger listener with the event bus. The listener
 * will be notified of matching events.
 *
 * Since: 1.0
 */
void
lrg_event_bus_register (LrgEventBus        *self,
                        LrgTriggerListener *listener)
{
    g_return_if_fail (LRG_IS_EVENT_BUS (self));
    g_return_if_fail (LRG_IS_TRIGGER_LISTENER (listener));

    g_ptr_array_add (self->listeners, g_object_ref (listener));
    self->listeners_dirty = TRUE;
}

/**
 * lrg_event_bus_unregister:
 * @self: an #LrgEventBus
 * @listener: the listener to unregister
 *
 * Unregisters a trigger listener from the event bus.
 *
 * Since: 1.0
 */
void
lrg_event_bus_unregister (LrgEventBus        *self,
                          LrgTriggerListener *listener)
{
    g_return_if_fail (LRG_IS_EVENT_BUS (self));
    g_return_if_fail (LRG_IS_TRIGGER_LISTENER (listener));

    g_ptr_array_remove (self->listeners, listener);
}

/**
 * lrg_event_bus_unregister_by_id:
 * @self: an #LrgEventBus
 * @trigger_id: the trigger ID to unregister
 *
 * Unregisters all listeners with the given trigger ID.
 *
 * Since: 1.0
 */
void
lrg_event_bus_unregister_by_id (LrgEventBus *self,
                                const gchar *trigger_id)
{
    guint i;

    g_return_if_fail (LRG_IS_EVENT_BUS (self));
    g_return_if_fail (trigger_id != NULL);

    /* Iterate in reverse to safely remove during iteration */
    for (i = self->listeners->len; i > 0; i--)
    {
        LrgTriggerListener *listener;
        const gchar *id;

        listener = g_ptr_array_index (self->listeners, i - 1);
        id = lrg_trigger_listener_get_trigger_id (listener);

        if (g_strcmp0 (id, trigger_id) == 0)
            g_ptr_array_remove_index (self->listeners, i - 1);
    }
}

/**
 * lrg_event_bus_clear:
 * @self: an #LrgEventBus
 *
 * Removes all registered listeners.
 *
 * Since: 1.0
 */
void
lrg_event_bus_clear (LrgEventBus *self)
{
    g_return_if_fail (LRG_IS_EVENT_BUS (self));

    g_ptr_array_set_size (self->listeners, 0);
    self->listeners_dirty = FALSE;
}

/**
 * lrg_event_bus_get_listener_count:
 * @self: an #LrgEventBus
 *
 * Gets the number of registered listeners.
 *
 * Returns: the listener count
 *
 * Since: 1.0
 */
guint
lrg_event_bus_get_listener_count (LrgEventBus *self)
{
    g_return_val_if_fail (LRG_IS_EVENT_BUS (self), 0);
    return self->listeners->len;
}

/* ==========================================================================
 * Event Dispatch
 * ========================================================================== */

/**
 * lrg_event_bus_emit:
 * @self: an #LrgEventBus
 * @event: (transfer full): the event to emit
 * @context: (nullable): the combat/game context
 *
 * Emits an event to all registered listeners. Listeners are notified
 * in priority order (highest first). If a listener cancels the event,
 * subsequent listeners are not notified.
 *
 * The event bus takes ownership of the event and frees it after dispatch.
 *
 * Returns: %TRUE if the event completed (not cancelled), %FALSE if cancelled
 *
 * Since: 1.0
 */
gboolean
lrg_event_bus_emit (LrgEventBus  *self,
                    LrgCardEvent *event,
                    gpointer      context)
{
    LrgCardEventType event_type;
    guint64 event_mask;
    guint i;
    gboolean result;

    g_return_val_if_fail (LRG_IS_EVENT_BUS (self), TRUE);
    g_return_val_if_fail (event != NULL, TRUE);

    ensure_sorted (self);

    event_type = lrg_card_event_get_event_type (event);
    event_mask = lrg_trigger_listener_event_type_to_mask (event_type);
    result = TRUE;

    /* Dispatch to all matching listeners in priority order */
    for (i = 0; i < self->listeners->len; i++)
    {
        LrgTriggerListener *listener;
        guint64 listener_mask;

        listener = g_ptr_array_index (self->listeners, i);
        listener_mask = lrg_trigger_listener_get_event_mask (listener);

        /* Skip if listener doesn't care about this event type */
        if ((listener_mask & event_mask) == 0)
            continue;

        /* Notify the listener */
        if (!lrg_trigger_listener_on_event (listener, event, context))
        {
            /* Listener cancelled the event */
            lrg_card_event_cancel (event);
            g_signal_emit (self, signals[SIGNAL_EVENT_CANCELLED], 0,
                           event, listener);
            result = FALSE;
            break;
        }

        /* Check if event was cancelled during processing */
        if (lrg_card_event_is_cancelled (event))
        {
            g_signal_emit (self, signals[SIGNAL_EVENT_CANCELLED], 0,
                           event, listener);
            result = FALSE;
            break;
        }
    }

    g_signal_emit (self, signals[SIGNAL_EVENT_EMITTED], 0, event);
    lrg_card_event_free (event);

    return result;
}

/**
 * lrg_event_bus_emit_copy:
 * @self: an #LrgEventBus
 * @event: (transfer none): the event to emit (copied)
 * @context: (nullable): the combat/game context
 *
 * Emits a copy of an event to all registered listeners. The original
 * event is not modified.
 *
 * Returns: %TRUE if the event completed (not cancelled), %FALSE if cancelled
 *
 * Since: 1.0
 */
gboolean
lrg_event_bus_emit_copy (LrgEventBus  *self,
                         LrgCardEvent *event,
                         gpointer      context)
{
    LrgCardEvent *copy;

    g_return_val_if_fail (LRG_IS_EVENT_BUS (self), TRUE);
    g_return_val_if_fail (event != NULL, TRUE);

    copy = lrg_card_event_copy (event);
    return lrg_event_bus_emit (self, copy, context);
}
