/* lrg-prewarm.c - A queue of GPU-resource work run ahead of first use
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-prewarm.h"

#include <raylib.h>
#include <rlgl.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CORE
#include "../lrg-log.h"

/**
 * SECTION:lrg-prewarm
 * @title: LrgPrewarm
 * @short_description: Builds GPU resources behind a loading screen
 *
 * A first-in, first-out queue of "build your GPU resources now" work:
 * #LrgPrewarmable objects (held weakly) and plain functions. A loading
 * screen calls lrg_prewarm_run() once per frame with a time budget, so the
 * shader compiles and buffer uploads that would otherwise freeze the
 * game when a feature first appears happen while the screen keeps
 * animating; lrg_prewarm_run_all() does it all at once.
 *
 * The engine's renderers add themselves to the default queue
 * (lrg_prewarm_get_default(), also lrg_engine_get_prewarm()) when they are
 * created on the main thread, so a level built behind a
 * #LrgTemplateLoadingState is warm when the loading screen finishes.
 * Everything is idempotent: running an entry twice, or drawing after it
 * ran, builds nothing again.
 *
 * |[<!-- language="C" -->
 * LrgPrewarm *prewarm = lrg_engine_get_prewarm (lrg_engine_get_default ());
 *
 * lrg_prewarm_add_object (prewarm, LRG_PREWARMABLE (my_post_effect));
 * lrg_prewarm_add_func (prewarm, "my shaders", load_my_shaders, game, NULL);
 *
 * (each loading-screen frame)
 * if (!lrg_prewarm_run (prewarm, 8000))
 *     finish_loading ();
 * ]|
 *
 * Queue operations are thread safe; running the queue is not (it makes
 * GL calls) and belongs to the thread that owns the GL context.
 */

/* the default queue sweeps freed objects out once it holds this many */
#define SWEEP_MINIMUM (256)

typedef struct
{
    gchar          *name;
    LrgPrewarmFunc  func;       /* NULL: an object entry */
    gpointer        user_data;
    GDestroyNotify  destroy;
    GWeakRef        object;
} Entry;

struct _LrgPrewarm
{
    GObject   parent_instance;

    GMutex    lock;
    GQueue    entries;          /* Entry *, run from the head */
    guint     sweep_at;         /* queue length that triggers a sweep */
    gboolean  auto_track;
    GThread  *owner;            /* the thread whose objects are tracked */

    guint64   completed;
    gint64    elapsed_us;
};

enum
{
    PROP_0,
    PROP_AUTO_TRACK,
    PROP_PENDING,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (LrgPrewarm, lrg_prewarm, G_TYPE_OBJECT)

/* ==========================================================================
 * Entries
 * ========================================================================== */

static void
entry_free (Entry *entry)
{
    if (entry->destroy != NULL)
        entry->destroy (entry->user_data);
    g_weak_ref_clear (&entry->object);
    g_free (entry->name);
    g_free (entry);
}

/*
 * sweep_locked:
 * Drops the object entries whose object is gone, so a queue nobody runs
 * (a game without a loading screen) stays as long as the live objects it
 * tracks, not as long as every object ever made. Called with the lock
 * held; the next sweep waits until the queue doubled again.
 */
static void
sweep_locked (LrgPrewarm *self)
{
    GList *link;
    GList *next;

    for (link = self->entries.head; link != NULL; link = next)
    {
        Entry   *entry = link->data;
        GObject *object;

        next = link->next;
        if (entry->func != NULL)
            continue;
        object = g_weak_ref_get (&entry->object);
        if (object != NULL)
        {
            g_object_unref (object);
            continue;
        }
        g_queue_delete_link (&self->entries, link);
        entry_free (entry);
    }
    self->sweep_at = MAX (SWEEP_MINIMUM, self->entries.length * 2);
}

/* push: appends @entry, sweeping first when the queue grew large */
static void
push (LrgPrewarm *self,
      Entry      *entry)
{
    g_mutex_lock (&self->lock);
    if (self->entries.length >= self->sweep_at)
        sweep_locked (self);
    g_queue_push_tail (&self->entries, entry);
    g_mutex_unlock (&self->lock);
}

/*
 * run_entry:
 * Runs one entry; TRUE when it is finished (an object freed before its
 * turn is finished too). Runs without the lock: the work may queue more.
 */
static gboolean
run_entry (Entry *entry)
{
    GObject  *object;
    gboolean  done;

    if (entry->func != NULL)
        return entry->func (entry->user_data);

    object = g_weak_ref_get (&entry->object);
    if (object == NULL)
        return TRUE;
    done = lrg_prewarmable_prewarm (LRG_PREWARMABLE (object));
    g_object_unref (object);
    return done;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_prewarm_finalize (GObject *object)
{
    LrgPrewarm *self = LRG_PREWARM (object);

    lrg_prewarm_clear (self);
    g_mutex_clear (&self->lock);

    G_OBJECT_CLASS (lrg_prewarm_parent_class)->finalize (object);
}

static void
lrg_prewarm_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgPrewarm *self = LRG_PREWARM (object);

    switch (prop_id)
    {
    case PROP_AUTO_TRACK:
        g_value_set_boolean (value, self->auto_track);
        break;
    case PROP_PENDING:
        g_value_set_uint (value, lrg_prewarm_get_pending (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_prewarm_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgPrewarm *self = LRG_PREWARM (object);

    switch (prop_id)
    {
    case PROP_AUTO_TRACK:
        lrg_prewarm_set_auto_track (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_prewarm_class_init (LrgPrewarmClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_prewarm_finalize;
    object_class->get_property = lrg_prewarm_get_property;
    object_class->set_property = lrg_prewarm_set_property;

    /**
     * LrgPrewarm:auto-track:
     *
     * Whether lrg_prewarm_track() adds newly created renderers to this
     * queue. Only the default queue is tracked into; on by default.
     */
    properties[PROP_AUTO_TRACK] =
        g_param_spec_boolean ("auto-track", "Auto track",
                              "Whether new renderers add themselves to the queue",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgPrewarm:pending:
     *
     * The number of queued entries (see lrg_prewarm_get_pending()).
     * Not notified.
     */
    properties[PROP_PENDING] =
        g_param_spec_uint ("pending", "Pending",
                           "The number of queued entries",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_prewarm_init (LrgPrewarm *self)
{
    g_mutex_init (&self->lock);
    g_queue_init (&self->entries);
    self->sweep_at = SWEEP_MINIMUM;
    self->auto_track = TRUE;
    self->owner = g_thread_self ();
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgPrewarm *
lrg_prewarm_new (void)
{
    return g_object_new (LRG_TYPE_PREWARM, NULL);
}

LrgPrewarm *
lrg_prewarm_get_default (void)
{
    static gsize default_prewarm = 0;

    if (g_once_init_enter (&default_prewarm))
        g_once_init_leave (&default_prewarm, (gsize) lrg_prewarm_new ());
    return (LrgPrewarm *) default_prewarm;
}

void
lrg_prewarm_track (LrgPrewarmable *object)
{
    LrgPrewarm *prewarm;

    g_return_if_fail (LRG_IS_PREWARMABLE (object));

    prewarm = lrg_prewarm_get_default ();
    if (!prewarm->auto_track || g_thread_self () != prewarm->owner)
        return;
    lrg_prewarm_add_object (prewarm, object);
}

void
lrg_prewarm_add_object (LrgPrewarm     *self,
                        LrgPrewarmable *object)
{
    Entry *entry;

    g_return_if_fail (LRG_IS_PREWARM (self));
    g_return_if_fail (LRG_IS_PREWARMABLE (object));

    entry = g_new0 (Entry, 1);
    g_weak_ref_init (&entry->object, object);
    push (self, entry);
}

void
lrg_prewarm_add_func (LrgPrewarm     *self,
                      const gchar    *name,
                      LrgPrewarmFunc  func,
                      gpointer        user_data,
                      GDestroyNotify  destroy)
{
    Entry *entry;

    g_return_if_fail (LRG_IS_PREWARM (self));
    g_return_if_fail (func != NULL);

    entry = g_new0 (Entry, 1);
    entry->name = g_strdup (name);
    entry->func = func;
    entry->user_data = user_data;
    entry->destroy = destroy;
    g_weak_ref_init (&entry->object, NULL);
    push (self, entry);
}

gboolean
lrg_prewarm_run (LrgPrewarm *self,
                 gint64      budget_us)
{
    gint64   start;
    gint64   now;
    gboolean more;

    g_return_val_if_fail (LRG_IS_PREWARM (self), FALSE);

    /* GL work needs the context; the queue waits for a window */
    if (!IsWindowReady ())
        return FALSE;

    start = g_get_monotonic_time ();
    now = start;
    for (;;)
    {
        Entry   *entry;
        gboolean done;
        gint64   t0;

        g_mutex_lock (&self->lock);
        entry = g_queue_pop_head (&self->entries);
        g_mutex_unlock (&self->lock);
        if (entry == NULL)
            break;

        t0 = now;
        done = run_entry (entry);
        now = g_get_monotonic_time ();
        if (entry->name != NULL)
            lrg_debug (LRG_LOG_DOMAIN_CORE, "prewarm: '%s' %s in %" G_GINT64_FORMAT " us",
                       entry->name, done ? "finished" : "stepped", now - t0);

        if (done)
        {
            entry_free (entry);
            self->completed++;
        }
        else
        {
            /* not finished: back of the line, the rest go first */
            g_mutex_lock (&self->lock);
            g_queue_push_tail (&self->entries, entry);
            g_mutex_unlock (&self->lock);
        }

        if (now - start >= budget_us)
            break;
    }
    self->elapsed_us += now - start;

    g_mutex_lock (&self->lock);
    more = self->entries.length > 0;
    g_mutex_unlock (&self->lock);
    return more;
}

void
lrg_prewarm_run_all (LrgPrewarm *self)
{
    g_return_if_fail (LRG_IS_PREWARM (self));

    while (lrg_prewarm_run (self, G_MAXINT64))
        ;
}

guint
lrg_prewarm_get_pending (LrgPrewarm *self)
{
    guint pending;

    g_return_val_if_fail (LRG_IS_PREWARM (self), 0);

    g_mutex_lock (&self->lock);
    pending = self->entries.length;
    g_mutex_unlock (&self->lock);
    return pending;
}

guint64
lrg_prewarm_get_completed (LrgPrewarm *self)
{
    g_return_val_if_fail (LRG_IS_PREWARM (self), 0);
    return self->completed;
}

gint64
lrg_prewarm_get_elapsed_us (LrgPrewarm *self)
{
    g_return_val_if_fail (LRG_IS_PREWARM (self), 0);
    return self->elapsed_us;
}

void
lrg_prewarm_clear (LrgPrewarm *self)
{
    GQueue entries = G_QUEUE_INIT;

    g_return_if_fail (LRG_IS_PREWARM (self));

    /* detach under the lock, free outside it (a destroy notify may
     * queue work of its own) */
    g_mutex_lock (&self->lock);
    entries = self->entries;
    g_queue_init (&self->entries);
    self->sweep_at = SWEEP_MINIMUM;
    g_mutex_unlock (&self->lock);
    g_queue_clear_full (&entries, (GDestroyNotify) entry_free);
}

gboolean
lrg_prewarm_get_auto_track (LrgPrewarm *self)
{
    g_return_val_if_fail (LRG_IS_PREWARM (self), FALSE);
    return self->auto_track;
}

void
lrg_prewarm_set_auto_track (LrgPrewarm *self,
                            gboolean    auto_track)
{
    g_return_if_fail (LRG_IS_PREWARM (self));

    auto_track = !!auto_track;
    if (self->auto_track == auto_track)
        return;
    self->auto_track = auto_track;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTO_TRACK]);
}

void
lrg_prewarm_begin_hidden_draw (void)
{
    if (!IsWindowReady ())
        return;

    /* what was queued before is drawn as usual; then nothing passes the
     * scissor test, colour, depth and clears alike */
    rlDrawRenderBatchActive ();
    rlEnableScissorTest ();
    rlScissor (0, 0, 0, 0);
}

void
lrg_prewarm_end_hidden_draw (void)
{
    if (!IsWindowReady ())
        return;

    /* rlgl batches: the hidden draws must reach the GPU while the
     * scissor still hides them */
    rlDrawRenderBatchActive ();
    rlDisableScissorTest ();
}
