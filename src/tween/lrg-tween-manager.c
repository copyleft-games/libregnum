/* lrg-tween-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manages active tweens and updates them each frame.
 */

#include "lrg-tween-manager.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TWEEN

/**
 * LrgTweenManager:
 *
 * Manages a collection of active tweens.
 *
 * The #LrgTweenManager is responsible for updating all registered tweens
 * each frame. It provides factory methods for creating tweens and handles
 * automatic cleanup of finished tweens.
 *
 * Typically, you'll get the tween manager from the engine:
 * |[<!-- language="C" -->
 * LrgEngine *engine = lrg_engine_get_default ();
 * LrgTweenManager *tweens = lrg_engine_get_tween_manager (engine);
 *
 * // Create and configure a tween
 * LrgTween *tween = lrg_tween_manager_create_tween (tweens, sprite, "x", 1.0);
 * lrg_tween_set_to_float (tween, 100.0);
 * lrg_tween_base_start (LRG_TWEEN_BASE (tween));
 * ]|
 *
 * Since: 1.0
 */

struct _LrgTweenManager
{
    GObject     parent_instance;

    GPtrArray  *tweens;
    GPtrArray  *pending_add;
    GPtrArray  *pending_remove;

    gfloat      time_scale;
    gboolean    auto_remove_finished;
    gboolean    is_updating;
};

enum
{
    PROP_0,
    PROP_TIME_SCALE,
    PROP_AUTO_REMOVE_FINISHED,
    N_PROPS
};

enum
{
    SIGNAL_TWEEN_ADDED,
    SIGNAL_TWEEN_REMOVED,
    SIGNAL_TWEEN_COMPLETED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgTweenManager, lrg_tween_manager, G_TYPE_OBJECT)

/*
 * Process pending additions and removals.
 * Called after update loop to avoid modifying the collection while iterating.
 */
static void
process_pending (LrgTweenManager *self)
{
    guint i;

    /* Process removals first */
    for (i = 0; i < self->pending_remove->len; i++)
    {
        LrgTweenBase *tween;

        tween = g_ptr_array_index (self->pending_remove, i);
        g_ptr_array_remove (self->tweens, tween);
    }
    g_ptr_array_set_size (self->pending_remove, 0);

    /* Process additions */
    for (i = 0; i < self->pending_add->len; i++)
    {
        LrgTweenBase *tween;

        tween = g_ptr_array_index (self->pending_add, i);
        g_ptr_array_add (self->tweens, g_object_ref (tween));

        /* Auto-start if configured */
        if (lrg_tween_base_get_auto_start (tween))
        {
            lrg_tween_base_start (tween);
        }

        g_signal_emit (self, signals[SIGNAL_TWEEN_ADDED], 0, tween);
    }
    g_ptr_array_set_size (self->pending_add, 0);
}

/*
 * GObject virtual methods
 */

static void
lrg_tween_manager_dispose (GObject *object)
{
    LrgTweenManager *self;

    self = LRG_TWEEN_MANAGER (object);

    g_ptr_array_set_size (self->tweens, 0);
    g_ptr_array_set_size (self->pending_add, 0);
    g_ptr_array_set_size (self->pending_remove, 0);

    G_OBJECT_CLASS (lrg_tween_manager_parent_class)->dispose (object);
}

static void
lrg_tween_manager_finalize (GObject *object)
{
    LrgTweenManager *self;

    self = LRG_TWEEN_MANAGER (object);

    g_clear_pointer (&self->tweens, g_ptr_array_unref);
    g_clear_pointer (&self->pending_add, g_ptr_array_unref);
    g_clear_pointer (&self->pending_remove, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_tween_manager_parent_class)->finalize (object);
}

static void
lrg_tween_manager_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgTweenManager *self;

    self = LRG_TWEEN_MANAGER (object);

    switch (prop_id)
    {
    case PROP_TIME_SCALE:
        g_value_set_float (value, self->time_scale);
        break;

    case PROP_AUTO_REMOVE_FINISHED:
        g_value_set_boolean (value, self->auto_remove_finished);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_tween_manager_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgTweenManager *self;

    self = LRG_TWEEN_MANAGER (object);

    switch (prop_id)
    {
    case PROP_TIME_SCALE:
        self->time_scale = g_value_get_float (value);
        break;

    case PROP_AUTO_REMOVE_FINISHED:
        self->auto_remove_finished = g_value_get_boolean (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_tween_manager_class_init (LrgTweenManagerClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->dispose = lrg_tween_manager_dispose;
    object_class->finalize = lrg_tween_manager_finalize;
    object_class->get_property = lrg_tween_manager_get_property;
    object_class->set_property = lrg_tween_manager_set_property;

    /**
     * LrgTweenManager:time-scale:
     *
     * Global time scale applied to all tweens.
     * 1.0 = normal speed, 0.5 = half speed, 2.0 = double speed.
     *
     * Since: 1.0
     */
    properties[PROP_TIME_SCALE] =
        g_param_spec_float ("time-scale",
                            "Time Scale",
                            "Global time scale multiplier",
                            0.0f,
                            G_MAXFLOAT,
                            1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgTweenManager:auto-remove-finished:
     *
     * Whether to automatically remove finished tweens.
     *
     * Since: 1.0
     */
    properties[PROP_AUTO_REMOVE_FINISHED] =
        g_param_spec_boolean ("auto-remove-finished",
                              "Auto Remove Finished",
                              "Whether to auto-remove finished tweens",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgTweenManager::tween-added:
     * @self: The manager
     * @tween: The added tween
     *
     * Emitted when a tween is added to the manager.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TWEEN_ADDED] =
        g_signal_new ("tween-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_TWEEN_BASE);

    /**
     * LrgTweenManager::tween-removed:
     * @self: The manager
     * @tween: The removed tween
     *
     * Emitted when a tween is removed from the manager.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TWEEN_REMOVED] =
        g_signal_new ("tween-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_TWEEN_BASE);

    /**
     * LrgTweenManager::tween-completed:
     * @self: The manager
     * @tween: The completed tween
     *
     * Emitted when a tween completes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TWEEN_COMPLETED] =
        g_signal_new ("tween-completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_TWEEN_BASE);
}

static void
lrg_tween_manager_init (LrgTweenManager *self)
{
    self->tweens = g_ptr_array_new_with_free_func (g_object_unref);
    self->pending_add = g_ptr_array_new_with_free_func (g_object_unref);
    self->pending_remove = g_ptr_array_new ();  /* No free func - we don't own refs here */

    self->time_scale = 1.0f;
    self->auto_remove_finished = TRUE;
    self->is_updating = FALSE;
}

/*
 * Public API
 */

/**
 * lrg_tween_manager_new:
 *
 * Creates a new tween manager.
 *
 * Returns: (transfer full): A new #LrgTweenManager
 *
 * Since: 1.0
 */
LrgTweenManager *
lrg_tween_manager_new (void)
{
    return g_object_new (LRG_TYPE_TWEEN_MANAGER, NULL);
}

/**
 * lrg_tween_manager_add:
 * @self: A #LrgTweenManager
 * @tween: (transfer none): The tween to manage
 *
 * Adds a tween to the manager.
 * If the tween has auto-start enabled, it will be started immediately.
 *
 * Since: 1.0
 */
void
lrg_tween_manager_add (LrgTweenManager *self,
                       LrgTweenBase    *tween)
{
    g_return_if_fail (LRG_IS_TWEEN_MANAGER (self));
    g_return_if_fail (LRG_IS_TWEEN_BASE (tween));

    if (self->is_updating)
    {
        /* Defer addition until after update loop */
        g_ptr_array_add (self->pending_add, g_object_ref (tween));
    }
    else
    {
        g_ptr_array_add (self->tweens, g_object_ref (tween));

        if (lrg_tween_base_get_auto_start (tween))
        {
            lrg_tween_base_start (tween);
        }

        g_signal_emit (self, signals[SIGNAL_TWEEN_ADDED], 0, tween);
    }
}

/**
 * lrg_tween_manager_remove:
 * @self: A #LrgTweenManager
 * @tween: The tween to remove
 *
 * Removes a tween from the manager.
 *
 * Returns: %TRUE if the tween was found and removed
 *
 * Since: 1.0
 */
gboolean
lrg_tween_manager_remove (LrgTweenManager *self,
                          LrgTweenBase    *tween)
{
    g_return_val_if_fail (LRG_IS_TWEEN_MANAGER (self), FALSE);
    g_return_val_if_fail (LRG_IS_TWEEN_BASE (tween), FALSE);

    if (self->is_updating)
    {
        /* Defer removal until after update loop */
        g_ptr_array_add (self->pending_remove, tween);
        return TRUE;  /* Assume it exists */
    }
    else
    {
        gboolean removed;

        removed = g_ptr_array_remove (self->tweens, tween);

        if (removed)
        {
            g_signal_emit (self, signals[SIGNAL_TWEEN_REMOVED], 0, tween);
        }

        return removed;
    }
}

/**
 * lrg_tween_manager_clear:
 * @self: A #LrgTweenManager
 *
 * Removes all tweens from the manager.
 *
 * Since: 1.0
 */
void
lrg_tween_manager_clear (LrgTweenManager *self)
{
    guint i;

    g_return_if_fail (LRG_IS_TWEEN_MANAGER (self));

    /* Emit removed signal for each tween */
    for (i = 0; i < self->tweens->len; i++)
    {
        LrgTweenBase *tween;

        tween = g_ptr_array_index (self->tweens, i);
        g_signal_emit (self, signals[SIGNAL_TWEEN_REMOVED], 0, tween);
    }

    g_ptr_array_set_size (self->tweens, 0);
    g_ptr_array_set_size (self->pending_add, 0);
    g_ptr_array_set_size (self->pending_remove, 0);
}

/**
 * lrg_tween_manager_update:
 * @self: A #LrgTweenManager
 * @delta_time: Time elapsed since last update in seconds
 *
 * Updates all managed tweens. Should be called every frame.
 * Finished tweens are automatically removed unless configured otherwise.
 *
 * Since: 1.0
 */
void
lrg_tween_manager_update (LrgTweenManager *self,
                          gfloat           delta_time)
{
    gfloat scaled_time;
    guint i;

    g_return_if_fail (LRG_IS_TWEEN_MANAGER (self));

    self->is_updating = TRUE;

    scaled_time = delta_time * self->time_scale;

    /* Update all tweens */
    for (i = 0; i < self->tweens->len; i++)
    {
        LrgTweenBase *tween;
        gboolean was_finished;

        tween = g_ptr_array_index (self->tweens, i);
        was_finished = lrg_tween_base_is_finished (tween);

        /* Only update running tweens */
        if (lrg_tween_base_is_running (tween))
        {
            lrg_tween_base_update (tween, scaled_time);
        }

        /* Check if just finished */
        if (!was_finished && lrg_tween_base_is_finished (tween))
        {
            g_signal_emit (self, signals[SIGNAL_TWEEN_COMPLETED], 0, tween);

            if (self->auto_remove_finished)
            {
                g_ptr_array_add (self->pending_remove, tween);
            }
        }
    }

    self->is_updating = FALSE;

    /* Process any deferred additions/removals */
    process_pending (self);
}

/**
 * lrg_tween_manager_get_tween_count:
 * @self: A #LrgTweenManager
 *
 * Gets the number of active tweens.
 *
 * Returns: The number of managed tweens
 *
 * Since: 1.0
 */
guint
lrg_tween_manager_get_tween_count (LrgTweenManager *self)
{
    g_return_val_if_fail (LRG_IS_TWEEN_MANAGER (self), 0);

    return self->tweens->len;
}

/**
 * lrg_tween_manager_get_tweens:
 * @self: A #LrgTweenManager
 *
 * Gets the list of active tweens.
 *
 * Returns: (transfer none) (element-type LrgTweenBase): The tweens
 *
 * Since: 1.0
 */
GPtrArray *
lrg_tween_manager_get_tweens (LrgTweenManager *self)
{
    g_return_val_if_fail (LRG_IS_TWEEN_MANAGER (self), NULL);

    return self->tweens;
}

/**
 * lrg_tween_manager_pause_all:
 * @self: A #LrgTweenManager
 *
 * Pauses all managed tweens.
 *
 * Since: 1.0
 */
void
lrg_tween_manager_pause_all (LrgTweenManager *self)
{
    guint i;

    g_return_if_fail (LRG_IS_TWEEN_MANAGER (self));

    for (i = 0; i < self->tweens->len; i++)
    {
        LrgTweenBase *tween;

        tween = g_ptr_array_index (self->tweens, i);
        lrg_tween_base_pause (tween);
    }
}

/**
 * lrg_tween_manager_resume_all:
 * @self: A #LrgTweenManager
 *
 * Resumes all paused tweens.
 *
 * Since: 1.0
 */
void
lrg_tween_manager_resume_all (LrgTweenManager *self)
{
    guint i;

    g_return_if_fail (LRG_IS_TWEEN_MANAGER (self));

    for (i = 0; i < self->tweens->len; i++)
    {
        LrgTweenBase *tween;

        tween = g_ptr_array_index (self->tweens, i);
        if (lrg_tween_base_is_paused (tween))
        {
            lrg_tween_base_resume (tween);
        }
    }
}

/**
 * lrg_tween_manager_stop_all:
 * @self: A #LrgTweenManager
 *
 * Stops all managed tweens. They remain in the manager but are reset.
 *
 * Since: 1.0
 */
void
lrg_tween_manager_stop_all (LrgTweenManager *self)
{
    guint i;

    g_return_if_fail (LRG_IS_TWEEN_MANAGER (self));

    for (i = 0; i < self->tweens->len; i++)
    {
        LrgTweenBase *tween;

        tween = g_ptr_array_index (self->tweens, i);
        lrg_tween_base_stop (tween);
    }
}

/**
 * lrg_tween_manager_create_tween:
 * @self: A #LrgTweenManager
 * @target: (transfer none): The target object
 * @property_name: The property to animate
 * @duration: Duration in seconds
 *
 * Creates and registers a new property tween.
 * The tween is added to the manager and will auto-start.
 *
 * Returns: (transfer none): The new tween (owned by manager)
 *
 * Since: 1.0
 */
LrgTween *
lrg_tween_manager_create_tween (LrgTweenManager *self,
                                GObject         *target,
                                const gchar     *property_name,
                                gfloat           duration)
{
    LrgTween *tween;

    g_return_val_if_fail (LRG_IS_TWEEN_MANAGER (self), NULL);
    g_return_val_if_fail (G_IS_OBJECT (target), NULL);
    g_return_val_if_fail (property_name != NULL, NULL);

    tween = lrg_tween_new (target, property_name, duration);
    lrg_tween_base_set_auto_start (LRG_TWEEN_BASE (tween), TRUE);

    lrg_tween_manager_add (self, LRG_TWEEN_BASE (tween));
    g_object_unref (tween);  /* Manager now owns it */

    return tween;
}

/**
 * lrg_tween_manager_create_sequence:
 * @self: A #LrgTweenManager
 *
 * Creates and registers a new tween sequence.
 * The sequence is added to the manager.
 *
 * Returns: (transfer none): The new sequence (owned by manager)
 *
 * Since: 1.0
 */
LrgTweenSequence *
lrg_tween_manager_create_sequence (LrgTweenManager *self)
{
    LrgTweenSequence *sequence;

    g_return_val_if_fail (LRG_IS_TWEEN_MANAGER (self), NULL);

    sequence = lrg_tween_sequence_new ();

    lrg_tween_manager_add (self, LRG_TWEEN_BASE (sequence));
    g_object_unref (sequence);  /* Manager now owns it */

    return sequence;
}

/**
 * lrg_tween_manager_create_parallel:
 * @self: A #LrgTweenManager
 *
 * Creates and registers a new parallel tween group.
 * The group is added to the manager.
 *
 * Returns: (transfer none): The new parallel group (owned by manager)
 *
 * Since: 1.0
 */
LrgTweenParallel *
lrg_tween_manager_create_parallel (LrgTweenManager *self)
{
    LrgTweenParallel *parallel;

    g_return_val_if_fail (LRG_IS_TWEEN_MANAGER (self), NULL);

    parallel = lrg_tween_parallel_new ();

    lrg_tween_manager_add (self, LRG_TWEEN_BASE (parallel));
    g_object_unref (parallel);  /* Manager now owns it */

    return parallel;
}

/**
 * lrg_tween_manager_get_auto_remove_finished:
 * @self: A #LrgTweenManager
 *
 * Gets whether finished tweens are automatically removed.
 *
 * Returns: %TRUE if finished tweens are auto-removed
 *
 * Since: 1.0
 */
gboolean
lrg_tween_manager_get_auto_remove_finished (LrgTweenManager *self)
{
    g_return_val_if_fail (LRG_IS_TWEEN_MANAGER (self), TRUE);

    return self->auto_remove_finished;
}

/**
 * lrg_tween_manager_set_auto_remove_finished:
 * @self: A #LrgTweenManager
 * @auto_remove: Whether to auto-remove finished tweens
 *
 * Sets whether finished tweens should be automatically removed
 * from the manager after completion.
 *
 * Since: 1.0
 */
void
lrg_tween_manager_set_auto_remove_finished (LrgTweenManager *self,
                                            gboolean         auto_remove)
{
    g_return_if_fail (LRG_IS_TWEEN_MANAGER (self));

    if (self->auto_remove_finished != auto_remove)
    {
        self->auto_remove_finished = auto_remove;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTO_REMOVE_FINISHED]);
    }
}

/**
 * lrg_tween_manager_get_time_scale:
 * @self: A #LrgTweenManager
 *
 * Gets the global time scale applied to all tweens.
 *
 * Returns: The time scale multiplier (1.0 = normal speed)
 *
 * Since: 1.0
 */
gfloat
lrg_tween_manager_get_time_scale (LrgTweenManager *self)
{
    g_return_val_if_fail (LRG_IS_TWEEN_MANAGER (self), 1.0f);

    return self->time_scale;
}

/**
 * lrg_tween_manager_set_time_scale:
 * @self: A #LrgTweenManager
 * @scale: The time scale multiplier
 *
 * Sets a global time scale applied to all managed tweens.
 * Use 0.5 for half speed, 2.0 for double speed, etc.
 *
 * Since: 1.0
 */
void
lrg_tween_manager_set_time_scale (LrgTweenManager *self,
                                  gfloat           scale)
{
    g_return_if_fail (LRG_IS_TWEEN_MANAGER (self));
    g_return_if_fail (scale >= 0.0f);

    if (self->time_scale != scale)
    {
        self->time_scale = scale;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME_SCALE]);
    }
}
