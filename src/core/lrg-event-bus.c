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

#include <stdlib.h>
#include <string.h>

typedef struct
{
    LrgEventListener *listener;
    LrgEventListenerInterface *iface;   /* the listener's vtable, looked up once */
    gboolean active;
} Registration;

typedef struct
{
    Registration *registration;
    gint priority;
    guint order;
} DispatchEntry;

struct _LrgEventBus
{
    GObject parent_instance;
    GPtrArray *listeners;       /* Owned Registration records, in registration order */
};

static void
registration_free (gpointer data)
{
    Registration *registration = data;

    g_object_unref (registration->listener);
}

static void
registration_unref (gpointer data)
{
    g_rc_box_release_full (data, registration_free);
}

static void
dispatch_entry_clear (gpointer data)
{
    DispatchEntry *entry = data;

    registration_unref (entry->registration);
}

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

/*
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
                      LRG_TYPE_EVENT);

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
                      LRG_TYPE_EVENT,
                      LRG_TYPE_EVENT_LISTENER);
}

static void
lrg_event_bus_init (LrgEventBus *self)
{
    self->listeners = g_ptr_array_new_with_free_func (registration_unref);
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/*
 * lrg_event_bus_new:
 *
 * Creates a new event bus. Use this for isolated contexts
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

/* Snapshot priorities and registration order make nested dispatch independent. */
static gint
compare_listener_priority (gconstpointer a,
                           gconstpointer b)
{
    const DispatchEntry *left = a;
    const DispatchEntry *right = b;

    if (left->priority != right->priority)
        return (right->priority > left->priority) - (right->priority < left->priority);
    return (left->order > right->order) - (left->order < right->order);
}

/* ==========================================================================
 * Listener Management
 * ========================================================================== */

/*
 * lrg_event_bus_register:
 * @self: an #LrgEventBus
 * @listener: (transfer none): the listener to register
 *
 * Registers an event listener with the event bus. The listener
 * will be notified of matching events. Each call adds a separate registration,
 * even for the same listener, and holds a reference until it is removed.
 *
 * Since: 1.0
 */
void
lrg_event_bus_register (LrgEventBus      *self,
                        LrgEventListener *listener)
{
    Registration *registration;

    g_return_if_fail (LRG_IS_EVENT_BUS (self));
    g_return_if_fail (LRG_IS_EVENT_LISTENER (listener));

    registration = g_rc_box_new0 (Registration);
    registration->listener = g_object_ref (listener);
    registration->iface = LRG_EVENT_LISTENER_GET_IFACE (listener);
    registration->active = TRUE;
    g_ptr_array_add (self->listeners, registration);
}

/*
 * lrg_event_bus_unregister:
 * @self: an #LrgEventBus
 * @listener: the listener to unregister
 *
 * Removes the oldest registration for this listener from the event bus.
 * Other registrations of the same listener remain active.
 *
 * Since: 1.0
 */
void
lrg_event_bus_unregister (LrgEventBus      *self,
                          LrgEventListener *listener)
{
    guint i;

    g_return_if_fail (LRG_IS_EVENT_BUS (self));
    g_return_if_fail (LRG_IS_EVENT_LISTENER (listener));

    for (i = 0; i < self->listeners->len; i++)
    {
        Registration *registration = g_ptr_array_index (self->listeners, i);

        if (registration->listener == listener)
        {
            registration->active = FALSE;
            g_ptr_array_remove_index (self->listeners, i);
            break;
        }
    }
}

/*
 * lrg_event_bus_unregister_by_id:
 * @self: an #LrgEventBus
 * @listener_id: the listener ID to unregister
 *
 * Unregisters all listeners with the given ID.
 *
 * Since: 1.0
 */
void
lrg_event_bus_unregister_by_id (LrgEventBus *self,
                                const gchar *listener_id)
{
    guint i;

    g_return_if_fail (LRG_IS_EVENT_BUS (self));
    g_return_if_fail (listener_id != NULL);

    /* Iterate in reverse to safely remove during iteration */
    for (i = self->listeners->len; i > 0; i--)
    {
        Registration *registration;
        const gchar *id;

        registration = g_ptr_array_index (self->listeners, i - 1);
        id = lrg_event_listener_get_id (registration->listener);

        if (g_strcmp0 (id, listener_id) == 0)
        {
            registration->active = FALSE;
            g_ptr_array_remove_index (self->listeners, i - 1);
        }
    }
}

/*
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
    guint i;

    g_return_if_fail (LRG_IS_EVENT_BUS (self));

    for (i = 0; i < self->listeners->len; i++)
    {
        Registration *registration = g_ptr_array_index (self->listeners, i);

        registration->active = FALSE;
    }
    g_ptr_array_set_size (self->listeners, 0);
}

/*
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

/*
 * Dispatch snapshots up to this many listeners live on the stack; larger
 * ones spill to the heap.
 */
#define DISPATCH_STACK_ENTRIES (32)

/*
 * sort_dispatch_entries:
 *
 * Orders a snapshot by priority (highest first), then registration order.
 * Snapshots are built in registration order, so a stable insertion sort on
 * priority alone gives exactly compare_listener_priority()'s order, in
 * linear time when priorities are already non-increasing (the common
 * case: all equal). Large snapshots use the general sort.
 */
static void
sort_dispatch_entries (DispatchEntry *entries,
                       guint          n_entries)
{
    guint i;

    if (n_entries > 64)
    {
        qsort (entries, n_entries, sizeof (DispatchEntry), compare_listener_priority);
        return;
    }

    for (i = 1; i < n_entries; i++)
    {
        DispatchEntry key = entries[i];
        guint j = i;

        while (j > 0 && entries[j - 1].priority < key.priority)
        {
            entries[j] = entries[j - 1];
            j--;
        }
        entries[j] = key;
    }
}

/*
 * lrg_event_bus_emit:
 * @self: an #LrgEventBus
 * @event: (transfer none): the event to emit
 * @context: (nullable): optional context data
 *
 * Emits an event to all registered listeners. Listeners are notified
 * in priority order (highest first). If a listener cancels the event,
 * subsequent listeners are not notified.
 *
 * Listeners may unregister during dispatch and remain alive until dispatch
 * finishes. Listeners removed before their turn are skipped. Newly registered
 * listeners participate in subsequent emissions, including nested emissions.
 * Removing and re-registering the same listener does not revive the removed
 * registration in a dispatch already in progress.
 *
 * Event masks and priorities are captured at the start of each emission:
 * only listeners whose mask matches the event then take part. A listener
 * whose mask stops matching before its turn is skipped; one whose mask
 * starts matching during the emission waits for the next one. Equal
 * priorities use registration order. Priority and mask changes during
 * callbacks take effect on the next emission; nested emissions have
 * independent snapshots.
 *
 * The bus, event, and snapshotted listeners remain alive through completion
 * signals even if callbacks release their owners' references. Access must be
 * confined to one thread or externally serialized. Listener accessors should
 * not mutate the bus.
 *
 * Returns: %TRUE if the event completed (not cancelled), %FALSE if cancelled
 *
 * Since: 1.0
 */
gboolean
lrg_event_bus_emit (LrgEventBus *self,
                    LrgEvent    *event,
                    gpointer     context)
{
    g_autoptr(LrgEventBus) bus_ref = NULL;
    g_autoptr(LrgEvent) event_ref = NULL;
    DispatchEntry stack_entries[DISPATCH_STACK_ENTRIES];
    DispatchEntry *entries;
    guint n_entries;
    guint capacity;
    guint64 event_mask;
    guint i;
    gboolean result;

    g_return_val_if_fail (LRG_IS_EVENT_BUS (self), TRUE);
    g_return_val_if_fail (LRG_IS_EVENT (event), TRUE);

    /* Protect both objects through listeners and completion signal handlers. */
    bus_ref = g_object_ref (self);
    event_ref = g_object_ref (event);

    event_mask = lrg_event_get_type_mask (event);
    result = !lrg_event_is_cancelled (event);

    /* Snapshot the registrations whose listener wants this event type.
     * Registration identities, not just listener identities: removing and
     * re-registering a listener must not revive its old dispatch entry.
     * Non-matching listeners cost one mask read and nothing else. */
    entries = stack_entries;
    n_entries = 0;
    capacity = DISPATCH_STACK_ENTRIES;
    for (i = 0; result && i < self->listeners->len; i++)
    {
        Registration *registration = g_ptr_array_index (self->listeners, i);
        guint64 listener_mask;

        /* Straight through the cached vtable: this runs for every
         * registration on every emission. */
        if (G_LIKELY (registration->iface->get_event_mask != NULL))
            listener_mask = registration->iface->get_event_mask (registration->listener);
        else
            listener_mask = lrg_event_listener_get_event_mask (registration->listener);
        if ((listener_mask & event_mask) == 0)
            continue;

        if (n_entries == capacity)
        {
            capacity *= 2;
            if (entries == stack_entries)
            {
                entries = g_new (DispatchEntry, capacity);
                memcpy (entries, stack_entries, sizeof (stack_entries));
            }
            else
            {
                entries = g_renew (DispatchEntry, entries, capacity);
            }
        }
        entries[n_entries].registration = g_rc_box_acquire (registration);
        entries[n_entries].priority = 0;
        entries[n_entries].order = i;
        n_entries++;
    }
    for (i = 0; i < n_entries; i++)
        entries[i].priority = lrg_event_listener_get_priority (entries[i].registration->listener);
    sort_dispatch_entries (entries, n_entries);

    /* Dispatch to all matching listeners in priority order */
    for (i = 0; result && i < n_entries; i++)
    {
        DispatchEntry *entry = &entries[i];
        LrgEventListener *listener = entry->registration->listener;
        guint64 listener_mask;

        if (!entry->registration->active)
            continue;

        listener_mask = lrg_event_listener_get_event_mask (listener);

        /* Skip if listener stopped caring about this event type */
        if ((listener_mask & event_mask) == 0)
            continue;

        /* Notify the listener */
        if (!lrg_event_listener_on_event (listener, event, context))
        {
            /* Listener cancelled the event */
            lrg_event_cancel (event);
            g_signal_emit (self, signals[SIGNAL_EVENT_CANCELLED], 0,
                           event, listener);
            result = FALSE;
            break;
        }

        /* Check if event was cancelled during processing */
        if (lrg_event_is_cancelled (event))
        {
            g_signal_emit (self, signals[SIGNAL_EVENT_CANCELLED], 0,
                           event, listener);
            result = FALSE;
            break;
        }
    }

    g_signal_emit (self, signals[SIGNAL_EVENT_EMITTED], 0, event);

    /* Release the snapshot only now: listeners stay alive through the
     * completion signal. */
    for (i = 0; i < n_entries; i++)
        dispatch_entry_clear (&entries[i]);
    if (entries != stack_entries)
        g_free (entries);

    return result;
}
