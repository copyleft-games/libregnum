/* lrg-timer-manager.c - Frame-driven gameplay timers
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "lrg-timer-manager.h"
#include <math.h>

typedef struct
{
    guint64 id;
    gdouble interval;
    gdouble remaining;
    gboolean repeat;
    gboolean unscaled;
    gboolean paused;
} Timer;

typedef struct
{
    guint64 id;
    gdouble next_remaining;
} DueTimer;

struct _LrgTimerManager
{
    GObject parent_instance;
    GHashTable *timers;
    guint64 last_id;
    gboolean updating;
};

G_DEFINE_TYPE (LrgTimerManager, lrg_timer_manager, G_TYPE_OBJECT)

static guint timeout_signal;

static void
lrg_timer_manager_finalize (GObject *object)
{
    LrgTimerManager *self = LRG_TIMER_MANAGER (object);

    g_hash_table_unref (self->timers);
    G_OBJECT_CLASS (lrg_timer_manager_parent_class)->finalize (object);
}

static void
lrg_timer_manager_class_init (LrgTimerManagerClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_timer_manager_finalize;

    /**
     * LrgTimerManager::timeout:
     * @self: the scheduler
     * @id: the expired timer's ID
     *
     * Emitted synchronously on the updating thread. One-shot IDs are already
     * removed; repeating IDs are already rearmed and may be cancelled here.
     */
    timeout_signal = g_signal_new ("timeout", G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                   G_TYPE_NONE, 1, G_TYPE_UINT64);
}

static void
lrg_timer_manager_init (LrgTimerManager *self)
{
    /* Keys point into their owned Timer values. */
    self->timers = g_hash_table_new_full (g_int64_hash, g_int64_equal,
                                         NULL, g_free);
}

LrgTimerManager *
lrg_timer_manager_new (void)
{
    return g_object_new (LRG_TYPE_TIMER_MANAGER, NULL);
}

guint64
lrg_timer_manager_add (LrgTimerManager *self,
                       gdouble          delay,
                       gboolean         repeat,
                       gboolean         unscaled)
{
    Timer *timer;

    g_return_val_if_fail (LRG_IS_TIMER_MANAGER (self), 0);

    if (!isfinite (delay) || delay < 0.0 || (repeat && delay == 0.0) ||
        self->last_id == G_MAXUINT64)
        return 0;

    timer = g_new0 (Timer, 1);
    timer->id = ++self->last_id;
    timer->interval = delay;
    timer->remaining = delay;
    timer->repeat = repeat;
    timer->unscaled = unscaled;
    g_hash_table_insert (self->timers, &timer->id, timer);
    return timer->id;
}

gboolean
lrg_timer_manager_cancel (LrgTimerManager *self,
                          guint64          id)
{
    g_return_val_if_fail (LRG_IS_TIMER_MANAGER (self), FALSE);
    return g_hash_table_remove (self->timers, &id);
}

gboolean
lrg_timer_manager_set_paused (LrgTimerManager *self,
                              guint64          id,
                              gboolean         paused)
{
    Timer *timer;

    g_return_val_if_fail (LRG_IS_TIMER_MANAGER (self), FALSE);
    timer = g_hash_table_lookup (self->timers, &id);
    if (timer == NULL)
        return FALSE;
    timer->paused = paused;
    return TRUE;
}

gdouble
lrg_timer_manager_get_remaining (LrgTimerManager *self,
                                 guint64          id)
{
    Timer *timer;

    g_return_val_if_fail (LRG_IS_TIMER_MANAGER (self), -1.0);
    timer = g_hash_table_lookup (self->timers, &id);
    return timer != NULL ? timer->remaining : -1.0;
}

guint
lrg_timer_manager_get_count (LrgTimerManager *self)
{
    g_return_val_if_fail (LRG_IS_TIMER_MANAGER (self), 0);
    return g_hash_table_size (self->timers);
}

void
lrg_timer_manager_clear (LrgTimerManager *self)
{
    g_return_if_fail (LRG_IS_TIMER_MANAGER (self));
    g_hash_table_remove_all (self->timers);
}

static gint
compare_due (gconstpointer a,
             gconstpointer b)
{
    const DueTimer *left = a;
    const DueTimer *right = b;

    return (left->id > right->id) - (left->id < right->id);
}

gboolean
lrg_timer_manager_update (LrgTimerManager *self,
                          gdouble          scaled_delta,
                          gdouble          unscaled_delta)
{
    g_autoptr(GArray) due = NULL;
    GHashTableIter iter;
    gpointer value;
    guint i;

    g_return_val_if_fail (LRG_IS_TIMER_MANAGER (self), FALSE);
    if (self->updating || !isfinite (scaled_delta) || scaled_delta < 0.0 ||
        !isfinite (unscaled_delta) || unscaled_delta < 0.0)
        return FALSE;

    /* Keep the scheduler alive even if a handler releases its owner's ref. */
    g_object_ref (self);
    self->updating = TRUE;
    due = g_array_new (FALSE, FALSE, sizeof (DueTimer));
    g_hash_table_iter_init (&iter, self->timers);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        Timer *timer = value;
        gdouble delta = timer->unscaled ? unscaled_delta : scaled_delta;

        if (timer->paused || delta == 0.0)
            continue;
        if (delta >= timer->remaining)
        {
            DueTimer entry;

            entry.id = timer->id;
            entry.next_remaining = timer->repeat
                ? timer->interval - fmod (delta - timer->remaining, timer->interval)
                : 0.0;
            timer->remaining = 0.0;
            g_array_append_val (due, entry);
        }
        else
            timer->remaining -= delta;
    }

    g_array_sort (due, compare_due);
    for (i = 0; i < due->len; i++)
    {
        DueTimer entry = g_array_index (due, DueTimer, i);
        Timer *timer = g_hash_table_lookup (self->timers, &entry.id);

        if (timer == NULL || timer->paused)
            continue;
        if (timer->repeat)
            timer->remaining = entry.next_remaining;
        else
            g_hash_table_remove (self->timers, &entry.id);
        g_signal_emit (self, timeout_signal, 0, entry.id);
    }
    self->updating = FALSE;
    g_object_unref (self);
    return TRUE;
}
