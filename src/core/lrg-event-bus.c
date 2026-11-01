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
    gboolean mask_stale;                /* re-read the mask before the next emission */
} Registration;

/*
 * MaskSlot:
 *
 * One registration's entry in the bus's mask array. @cached is set for
 * listeners that opted into caching (has_static_mask()); for the others
 * @mask is unused and the listener is asked on every emission.
 */
typedef struct
{
    guint64  mask;
    gboolean cached;
} MaskSlot;

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

    /*
     * MaskSlot of each registration, parallel to listeners (same
     * index). For listeners that opted into caching, an emission tests
     * the cached mask instead of calling get_event_mask(); the entry is
     * (re)read at the start of the first emission after the listener
     * was registered or refreshed (Registration.mask_stale), and
     * masks_stale says whether any entry needs that. Other listeners
     * are asked on every emission, as before caching existed.
     */
    GArray *masks;
    gboolean masks_stale;
    guint n_cached;             /* registrations with MaskSlot.cached set */
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
    g_clear_pointer (&self->masks, g_array_unref);

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
    self->masks = g_array_new (FALSE, FALSE, sizeof (MaskSlot));
    self->masks_stale = FALSE;
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

/*
 * registration_read_mask:
 *
 * The listener's current mask, through the registration's cached
 * vtable (the public wrapper, and its critical, only for a missing
 * vfunc).
 */
static inline guint64
registration_read_mask (Registration *registration)
{
    if (G_LIKELY (registration->iface->get_event_mask != NULL))
        return registration->iface->get_event_mask (registration->listener);
    return lrg_event_listener_get_event_mask (registration->listener);
}

/*
 * remove_registration_at:
 *
 * Removes the registration at @index, keeping the mask cache aligned.
 * The registration record itself lives on while a dispatch snapshot
 * holds it; inactive, it is skipped there.
 */
static void
remove_registration_at (LrgEventBus *self,
                        guint        index)
{
    Registration *registration = g_ptr_array_index (self->listeners, index);

    registration->active = FALSE;
    if (g_array_index (self->masks, MaskSlot, index).cached)
        self->n_cached--;
    g_array_remove_index (self->masks, index);
    g_ptr_array_remove_index (self->listeners, index);
}

/*
 * refresh_stale_masks:
 *
 * Re-reads the masks of registrations added or refreshed since the
 * last emission. Runs at the start of an emission, before its
 * snapshot, so a listener configured after registering is seen as it
 * is when the event is emitted.
 */
static void
refresh_stale_masks (LrgEventBus *self)
{
    guint i;

    /* Cleared first: a listener accessor that (against the contract)
     * refreshes again only marks entries for the next emission. */
    self->masks_stale = FALSE;
    for (i = 0; i < self->listeners->len; i++)
    {
        Registration *registration = g_ptr_array_index (self->listeners, i);

        if (registration->mask_stale)
        {
            guint64 mask;

            registration->mask_stale = FALSE;
            mask = registration_read_mask (registration);

            /* An accessor that mutated the bus (against the contract)
             * may have moved this registration: store only into its own
             * slot, otherwise leave it for the next emission. */
            if (i < self->listeners->len &&
                g_ptr_array_index (self->listeners, i) == registration)
            {
                g_array_index (self->masks, MaskSlot, i).mask = mask;
            }
            else
            {
                registration->mask_stale = TRUE;
                self->masks_stale = TRUE;
            }
        }
    }
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
    MaskSlot slot;

    g_return_if_fail (LRG_IS_EVENT_BUS (self));
    g_return_if_fail (LRG_IS_EVENT_LISTENER (listener));

    registration = g_rc_box_new0 (Registration);
    registration->listener = g_object_ref (listener);
    registration->iface = LRG_EVENT_LISTENER_GET_IFACE (listener);
    registration->active = TRUE;

    /* A listener that opted into caching has its mask read when the
     * next emission starts (it may still be configured after
     * registering); the others are asked on every emission. */
    slot.mask = 0;
    slot.cached = registration->iface->has_static_mask != NULL &&
                  registration->iface->has_static_mask (listener);
    registration->mask_stale = slot.cached;
    if (slot.cached)
    {
        self->masks_stale = TRUE;
        self->n_cached++;
    }
    g_ptr_array_add (self->listeners, registration);
    g_array_append_val (self->masks, slot);
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
            remove_registration_at (self, i);
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
            remove_registration_at (self, i - 1);
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
    g_array_set_size (self->masks, 0);
    self->masks_stale = FALSE;
    self->n_cached = 0;
}

/*
 * lrg_event_bus_refresh_listener:
 * @self: an #LrgEventBus
 * @listener: the listener whose event mask changed
 *
 * Marks every registration of @listener so its mask is read again at
 * the start of the next emission. See the header for the contract.
 */
void
lrg_event_bus_refresh_listener (LrgEventBus      *self,
                                LrgEventListener *listener)
{
    guint i;

    g_return_if_fail (LRG_IS_EVENT_BUS (self));
    g_return_if_fail (LRG_IS_EVENT_LISTENER (listener));

    for (i = 0; i < self->listeners->len; i++)
    {
        Registration *registration = g_ptr_array_index (self->listeners, i);

        /* Only cached masks need re-reading; the rest are live */
        if (registration->listener == listener &&
            g_array_index (self->masks, MaskSlot, i).cached)
        {
            registration->mask_stale = TRUE;
            self->masks_stale = TRUE;
        }
    }
}

/*
 * lrg_event_bus_refresh_listeners:
 * @self: an #LrgEventBus
 *
 * Marks every registration so all masks are read again at the start of
 * the next emission.
 */
void
lrg_event_bus_refresh_listeners (LrgEventBus *self)
{
    guint i;

    g_return_if_fail (LRG_IS_EVENT_BUS (self));

    for (i = 0; i < self->listeners->len; i++)
    {
        Registration *registration = g_ptr_array_index (self->listeners, i);

        if (g_array_index (self->masks, MaskSlot, i).cached)
        {
            registration->mask_stale = TRUE;
            self->masks_stale = TRUE;
        }
    }
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
 * event_is_cancelled:
 *
 * lrg_event_is_cancelled() through @event's already looked-up vtable.
 */
static inline gboolean
event_is_cancelled (LrgEvent          *event,
                    LrgEventInterface *iface)
{
    if (G_LIKELY (iface->is_cancelled != NULL))
        return iface->is_cancelled (event);
    return lrg_event_is_cancelled (event);
}

/*
 * snapshot_append:
 *
 * Appends @registration (at registration index @order) to an emission's
 * snapshot, growing it from the on-stack array to the heap as needed.
 * The snapshot holds a reference on the registration record.
 */
static inline void
snapshot_append (DispatchEntry **entries,
                 guint          *n_entries,
                 guint          *capacity,
                 DispatchEntry  *stack_entries,
                 Registration   *registration,
                 guint           order)
{
    if (*n_entries == *capacity)
    {
        *capacity *= 2;
        if (*entries == stack_entries)
        {
            *entries = g_new (DispatchEntry, *capacity);
            memcpy (*entries, stack_entries, sizeof (DispatchEntry) * DISPATCH_STACK_ENTRIES);
        }
        else
        {
            *entries = g_renew (DispatchEntry, *entries, *capacity);
        }
    }
    (*entries)[*n_entries].registration = g_rc_box_acquire (registration);
    (*entries)[*n_entries].priority = 0;
    (*entries)[*n_entries].order = order;
    (*n_entries)++;
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
 * Event masks are captured at the start of each emission: only listeners
 * whose mask matches the event then take part. A listener whose mask
 * stops matching before its turn is skipped; one whose mask starts
 * matching during the emission waits for the next one. A listener that
 * opts into caching (has_static_mask()) has its mask read at the first
 * emission after registering and after lrg_event_bus_refresh_listener()
 * or lrg_event_bus_refresh_listeners() only; its live mask is still
 * checked at its turn. Priorities are captured at the start of each emission. Equal
 * priorities use registration order. Priority changes during callbacks
 * take effect on the next emission; nested emissions have independent
 * snapshots.
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
    LrgEventInterface *event_iface;
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

    /* The event's vtable, looked up once: it is asked whether it was
     * cancelled after every listener. */
    event_iface = LRG_EVENT_GET_IFACE (event);

    event_mask = lrg_event_get_type_mask (event);
    result = !event_is_cancelled (event, event_iface);

    /* Cached masks of new and refreshed opted-in registrations, read
     * before the snapshot like the live ones */
    if (result && self->masks_stale)
        refresh_stale_masks (self);

    /* Snapshot the registrations whose listener wants this event type.
     * Registration identities, not just listener identities: removing and
     * re-registering a listener must not revive its old dispatch entry.
     * A non-matching listener costs one mask read: a load from the
     * cache for opted-in listeners, a get_event_mask() call otherwise. */
    entries = stack_entries;
    n_entries = 0;
    capacity = DISPATCH_STACK_ENTRIES;
    if (result && self->n_cached == 0)
    {
        /* No opted-in listener: every mask is live, the cache is not
         * consulted at all (a separate loop, so this one stays as
         * tight as before caching existed) */
        for (i = 0; i < self->listeners->len; i++)
        {
            Registration *registration = g_ptr_array_index (self->listeners, i);

            if ((registration_read_mask (registration) & event_mask) == 0)
                continue;
            snapshot_append (&entries, &n_entries, &capacity, stack_entries,
                             registration, i);
        }
    }
    else if (result)
    {
        for (i = 0; i < self->listeners->len; i++)
        {
            Registration *registration = g_ptr_array_index (self->listeners, i);
            const MaskSlot *slot = &g_array_index (self->masks, MaskSlot, i);
            guint64 listener_mask;

            if (slot->cached)
                listener_mask = slot->mask;
            else
                listener_mask = registration_read_mask (registration);
            if ((listener_mask & event_mask) == 0)
                continue;
            snapshot_append (&entries, &n_entries, &capacity, stack_entries,
                             registration, i);
        }
    }
    for (i = 0; i < n_entries; i++)
    {
        Registration *registration = entries[i].registration;

        if (G_LIKELY (registration->iface->get_priority != NULL))
            entries[i].priority = registration->iface->get_priority (registration->listener);
        else
            entries[i].priority = lrg_event_listener_get_priority (registration->listener);
    }
    sort_dispatch_entries (entries, n_entries);

    /* Dispatch to all matching listeners in priority order */
    for (i = 0; result && i < n_entries; i++)
    {
        DispatchEntry *entry = &entries[i];
        LrgEventListener *listener = entry->registration->listener;
        LrgEventListenerInterface *iface = entry->registration->iface;
        guint64 listener_mask;
        gboolean keep_going;

        if (!entry->registration->active)
            continue;

        /* The live mask (lrg_event_listener_get_event_mask() is the
         * source of truth): a listener that stopped caring about this
         * event type since its mask was cached is skipped */
        listener_mask = registration_read_mask (entry->registration);

        /* Skip if listener stopped caring about this event type */
        if ((listener_mask & event_mask) == 0)
            continue;

        /* Notify the listener */
        if (G_LIKELY (iface->on_event != NULL))
            keep_going = iface->on_event (listener, event, context);
        else
            keep_going = lrg_event_listener_on_event (listener, event, context);
        if (!keep_going)
        {
            /* Listener cancelled the event */
            lrg_event_cancel (event);
            g_signal_emit (self, signals[SIGNAL_EVENT_CANCELLED], 0,
                           event, listener);
            result = FALSE;
            break;
        }

        /* Check if event was cancelled during processing */
        if (event_is_cancelled (event, event_iface))
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
