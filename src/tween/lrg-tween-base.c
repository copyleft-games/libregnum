/* lrg-tween-base.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for all tween types.
 */

#include "config.h"

#include "lrg-tween-base.h"
#include "lrg-easing.h"
#include "../lrg-log.h"

typedef struct
{
    /* Timing */
    gfloat           duration;
    gfloat           delay;
    gfloat           elapsed;
    gfloat           delay_elapsed;
    gfloat           progress;

    /* Easing */
    LrgEasingType    easing;

    /* Looping */
    gint             loop_count;
    gint             current_loop;
    LrgTweenLoopMode loop_mode;
    gboolean         reversed;      /* For ping-pong mode */

    /* State */
    LrgTweenState    state;
    gboolean         auto_start;
} LrgTweenBasePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgTweenBase, lrg_tween_base, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_DURATION,
    PROP_DELAY,
    PROP_ELAPSED,
    PROP_PROGRESS,
    PROP_EASING,
    PROP_LOOP_COUNT,
    PROP_LOOP_MODE,
    PROP_CURRENT_LOOP,
    PROP_STATE,
    PROP_AUTO_START,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_STARTED,
    SIGNAL_UPDATED,
    SIGNAL_COMPLETED,
    SIGNAL_LOOP_COMPLETED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Virtual method default implementations */

static void
lrg_tween_base_real_start (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    priv = lrg_tween_base_get_instance_private (self);

    priv->state = LRG_TWEEN_STATE_RUNNING;
    priv->elapsed = 0.0f;
    priv->delay_elapsed = 0.0f;
    priv->progress = 0.0f;
    priv->current_loop = 0;
    priv->reversed = FALSE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ELAPSED]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_LOOP]);

    g_signal_emit (self, signals[SIGNAL_STARTED], 0);
}

static void
lrg_tween_base_real_stop (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    priv = lrg_tween_base_get_instance_private (self);

    priv->state = LRG_TWEEN_STATE_IDLE;
    priv->elapsed = 0.0f;
    priv->delay_elapsed = 0.0f;
    priv->progress = 0.0f;
    priv->current_loop = 0;
    priv->reversed = FALSE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ELAPSED]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_LOOP]);
}

static void
lrg_tween_base_real_pause (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    priv = lrg_tween_base_get_instance_private (self);

    if (priv->state == LRG_TWEEN_STATE_RUNNING)
    {
        priv->state = LRG_TWEEN_STATE_PAUSED;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
    }
}

static void
lrg_tween_base_real_resume (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    priv = lrg_tween_base_get_instance_private (self);

    if (priv->state == LRG_TWEEN_STATE_PAUSED)
    {
        priv->state = LRG_TWEEN_STATE_RUNNING;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
    }
}

static void
lrg_tween_base_real_update (LrgTweenBase *self,
                            gfloat        delta_time)
{
    /* Base implementation does nothing - subclasses override */
}

static void
lrg_tween_base_real_reset (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    priv = lrg_tween_base_get_instance_private (self);

    priv->state = LRG_TWEEN_STATE_IDLE;
    priv->elapsed = 0.0f;
    priv->delay_elapsed = 0.0f;
    priv->progress = 0.0f;
    priv->current_loop = 0;
    priv->reversed = FALSE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ELAPSED]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_LOOP]);
}

static gboolean
lrg_tween_base_real_is_finished (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    priv = lrg_tween_base_get_instance_private (self);

    return priv->state == LRG_TWEEN_STATE_FINISHED;
}

/* GObject methods */

static void
lrg_tween_base_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgTweenBase *self;
    LrgTweenBasePrivate *priv;

    self = LRG_TWEEN_BASE (object);
    priv = lrg_tween_base_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_DURATION:
        g_value_set_float (value, priv->duration);
        break;
    case PROP_DELAY:
        g_value_set_float (value, priv->delay);
        break;
    case PROP_ELAPSED:
        g_value_set_float (value, priv->elapsed);
        break;
    case PROP_PROGRESS:
        g_value_set_float (value, priv->progress);
        break;
    case PROP_EASING:
        g_value_set_enum (value, priv->easing);
        break;
    case PROP_LOOP_COUNT:
        g_value_set_int (value, priv->loop_count);
        break;
    case PROP_LOOP_MODE:
        g_value_set_enum (value, priv->loop_mode);
        break;
    case PROP_CURRENT_LOOP:
        g_value_set_int (value, priv->current_loop);
        break;
    case PROP_STATE:
        g_value_set_enum (value, priv->state);
        break;
    case PROP_AUTO_START:
        g_value_set_boolean (value, priv->auto_start);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tween_base_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgTweenBase *self;
    LrgTweenBasePrivate *priv;

    self = LRG_TWEEN_BASE (object);
    priv = lrg_tween_base_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_DURATION:
        priv->duration = g_value_get_float (value);
        break;
    case PROP_DELAY:
        priv->delay = g_value_get_float (value);
        break;
    case PROP_EASING:
        priv->easing = g_value_get_enum (value);
        break;
    case PROP_LOOP_COUNT:
        priv->loop_count = g_value_get_int (value);
        break;
    case PROP_LOOP_MODE:
        priv->loop_mode = g_value_get_enum (value);
        break;
    case PROP_AUTO_START:
        priv->auto_start = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tween_base_class_init (LrgTweenBaseClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_tween_base_get_property;
    object_class->set_property = lrg_tween_base_set_property;

    /* Set virtual method defaults */
    klass->start = lrg_tween_base_real_start;
    klass->stop = lrg_tween_base_real_stop;
    klass->pause = lrg_tween_base_real_pause;
    klass->resume = lrg_tween_base_real_resume;
    klass->update = lrg_tween_base_real_update;
    klass->reset = lrg_tween_base_real_reset;
    klass->is_finished = lrg_tween_base_real_is_finished;

    /* Properties */

    /**
     * LrgTweenBase:duration:
     *
     * The total duration of the tween in seconds.
     *
     * Since: 1.0
     */
    properties[PROP_DURATION] =
        g_param_spec_float ("duration",
                            "Duration",
                            "Total duration in seconds",
                            0.0f, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTweenBase:delay:
     *
     * The delay before the tween starts in seconds.
     *
     * Since: 1.0
     */
    properties[PROP_DELAY] =
        g_param_spec_float ("delay",
                            "Delay",
                            "Start delay in seconds",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTweenBase:elapsed:
     *
     * Time elapsed since the tween started animating.
     *
     * Since: 1.0
     */
    properties[PROP_ELAPSED] =
        g_param_spec_float ("elapsed",
                            "Elapsed",
                            "Time elapsed in seconds",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTweenBase:progress:
     *
     * Normalized progress of the tween (0.0 to 1.0).
     *
     * Since: 1.0
     */
    properties[PROP_PROGRESS] =
        g_param_spec_float ("progress",
                            "Progress",
                            "Normalized progress 0.0-1.0",
                            0.0f, 1.0f, 0.0f,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTweenBase:easing:
     *
     * The easing function type.
     *
     * Since: 1.0
     */
    properties[PROP_EASING] =
        g_param_spec_enum ("easing",
                           "Easing",
                           "Easing function type",
                           LRG_TYPE_EASING_TYPE,
                           LRG_EASING_LINEAR,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTweenBase:loop-count:
     *
     * Number of times to loop (-1 = infinite, 0 = none).
     *
     * Since: 1.0
     */
    properties[PROP_LOOP_COUNT] =
        g_param_spec_int ("loop-count",
                          "Loop Count",
                          "Number of loops (-1 = infinite)",
                          -1, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTweenBase:loop-mode:
     *
     * The loop mode (restart or ping-pong).
     *
     * Since: 1.0
     */
    properties[PROP_LOOP_MODE] =
        g_param_spec_enum ("loop-mode",
                           "Loop Mode",
                           "Loop behavior mode",
                           LRG_TYPE_TWEEN_LOOP_MODE,
                           LRG_TWEEN_LOOP_RESTART,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTweenBase:current-loop:
     *
     * The current loop iteration (0-based).
     *
     * Since: 1.0
     */
    properties[PROP_CURRENT_LOOP] =
        g_param_spec_int ("current-loop",
                          "Current Loop",
                          "Current loop iteration",
                          0, G_MAXINT, 0,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTweenBase:state:
     *
     * The current state of the tween.
     *
     * Since: 1.0
     */
    properties[PROP_STATE] =
        g_param_spec_enum ("state",
                           "State",
                           "Current tween state",
                           LRG_TYPE_TWEEN_STATE,
                           LRG_TWEEN_STATE_IDLE,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTweenBase:auto-start:
     *
     * Whether to start automatically when added to a manager.
     *
     * Since: 1.0
     */
    properties[PROP_AUTO_START] =
        g_param_spec_boolean ("auto-start",
                              "Auto Start",
                              "Start when added to manager",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */

    /**
     * LrgTweenBase::started:
     * @self: The tween that emitted the signal
     *
     * Emitted when the tween starts.
     *
     * Since: 1.0
     */
    signals[SIGNAL_STARTED] =
        g_signal_new ("started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTweenBase::updated:
     * @self: The tween that emitted the signal
     * @progress: Current progress (0.0 to 1.0)
     *
     * Emitted after each update with the current progress.
     *
     * Since: 1.0
     */
    signals[SIGNAL_UPDATED] =
        g_signal_new ("updated",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_FLOAT);

    /**
     * LrgTweenBase::completed:
     * @self: The tween that emitted the signal
     *
     * Emitted when the tween finishes all iterations.
     *
     * Since: 1.0
     */
    signals[SIGNAL_COMPLETED] =
        g_signal_new ("completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTweenBase::loop-completed:
     * @self: The tween that emitted the signal
     * @loop: The loop iteration that completed (0-based)
     *
     * Emitted after each loop iteration completes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_LOOP_COMPLETED] =
        g_signal_new ("loop-completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_INT);
}

static void
lrg_tween_base_init (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    priv = lrg_tween_base_get_instance_private (self);

    priv->duration = 1.0f;
    priv->delay = 0.0f;
    priv->elapsed = 0.0f;
    priv->delay_elapsed = 0.0f;
    priv->progress = 0.0f;
    priv->easing = LRG_EASING_LINEAR;
    priv->loop_count = 0;
    priv->current_loop = 0;
    priv->loop_mode = LRG_TWEEN_LOOP_RESTART;
    priv->reversed = FALSE;
    priv->state = LRG_TWEEN_STATE_IDLE;
    priv->auto_start = TRUE;
}

/* Public API */

void
lrg_tween_base_start (LrgTweenBase *self)
{
    LrgTweenBaseClass *klass;

    g_return_if_fail (LRG_IS_TWEEN_BASE (self));

    klass = LRG_TWEEN_BASE_GET_CLASS (self);
    if (klass->start != NULL)
        klass->start (self);
}

void
lrg_tween_base_stop (LrgTweenBase *self)
{
    LrgTweenBaseClass *klass;

    g_return_if_fail (LRG_IS_TWEEN_BASE (self));

    klass = LRG_TWEEN_BASE_GET_CLASS (self);
    if (klass->stop != NULL)
        klass->stop (self);
}

void
lrg_tween_base_pause (LrgTweenBase *self)
{
    LrgTweenBaseClass *klass;

    g_return_if_fail (LRG_IS_TWEEN_BASE (self));

    klass = LRG_TWEEN_BASE_GET_CLASS (self);
    if (klass->pause != NULL)
        klass->pause (self);
}

void
lrg_tween_base_resume (LrgTweenBase *self)
{
    LrgTweenBaseClass *klass;

    g_return_if_fail (LRG_IS_TWEEN_BASE (self));

    klass = LRG_TWEEN_BASE_GET_CLASS (self);
    if (klass->resume != NULL)
        klass->resume (self);
}

void
lrg_tween_base_update (LrgTweenBase *self,
                       gfloat        delta_time)
{
    LrgTweenBasePrivate *priv;
    LrgTweenBaseClass *klass;
    gfloat raw_progress;
    gboolean loop_finished;

    g_return_if_fail (LRG_IS_TWEEN_BASE (self));
    g_return_if_fail (delta_time >= 0.0f);

    priv = lrg_tween_base_get_instance_private (self);

    /* Only update if running */
    if (priv->state != LRG_TWEEN_STATE_RUNNING)
        return;

    /* Handle delay */
    if (priv->delay_elapsed < priv->delay)
    {
        priv->delay_elapsed += delta_time;
        if (priv->delay_elapsed < priv->delay)
            return;

        /* Delay finished, apply remaining time to elapsed */
        delta_time = priv->delay_elapsed - priv->delay;
        priv->delay_elapsed = priv->delay;
    }

    /* Update elapsed time */
    priv->elapsed += delta_time;

    /* Calculate raw progress */
    if (priv->duration > 0.0f)
    {
        raw_progress = priv->elapsed / priv->duration;
    }
    else
    {
        raw_progress = 1.0f;
    }

    /* Check for loop completion */
    loop_finished = FALSE;
    if (raw_progress >= 1.0f)
    {
        loop_finished = TRUE;

        /* Check if we should loop */
        if (priv->loop_count != 0)
        {
            /* Emit loop completed signal */
            g_signal_emit (self, signals[SIGNAL_LOOP_COMPLETED], 0, priv->current_loop);

            priv->current_loop++;
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_LOOP]);

            /* Check if we've done all loops */
            if (priv->loop_count > 0 && priv->current_loop >= priv->loop_count)
            {
                /* All loops done */
                priv->elapsed = priv->duration;
                raw_progress = 1.0f;
            }
            else
            {
                /* Continue looping */
                if (priv->loop_mode == LRG_TWEEN_LOOP_PING_PONG)
                {
                    priv->reversed = !priv->reversed;
                }

                /* Reset elapsed for next loop, keeping overflow */
                priv->elapsed = priv->elapsed - priv->duration;
                raw_progress = priv->elapsed / priv->duration;
                loop_finished = FALSE;
            }
        }
        else
        {
            /* No looping, clamp to 1.0 */
            raw_progress = 1.0f;
            priv->elapsed = priv->duration;
        }
    }

    /* Apply ping-pong reversal */
    if (priv->reversed)
    {
        raw_progress = 1.0f - raw_progress;
    }

    /* Clamp progress */
    if (raw_progress < 0.0f)
        raw_progress = 0.0f;
    if (raw_progress > 1.0f)
        raw_progress = 1.0f;

    priv->progress = raw_progress;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ELAPSED]);

    /* Call subclass update */
    klass = LRG_TWEEN_BASE_GET_CLASS (self);
    if (klass->update != NULL)
        klass->update (self, delta_time);

    /* Emit updated signal */
    g_signal_emit (self, signals[SIGNAL_UPDATED], 0, priv->progress);

    /* Check if completely finished */
    if (loop_finished && (priv->loop_count == 0 ||
        (priv->loop_count > 0 && priv->current_loop >= priv->loop_count)))
    {
        priv->state = LRG_TWEEN_STATE_FINISHED;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
        g_signal_emit (self, signals[SIGNAL_COMPLETED], 0);
    }
}

void
lrg_tween_base_reset (LrgTweenBase *self)
{
    LrgTweenBaseClass *klass;

    g_return_if_fail (LRG_IS_TWEEN_BASE (self));

    klass = LRG_TWEEN_BASE_GET_CLASS (self);
    if (klass->reset != NULL)
        klass->reset (self);
}

gboolean
lrg_tween_base_is_finished (LrgTweenBase *self)
{
    LrgTweenBaseClass *klass;

    g_return_val_if_fail (LRG_IS_TWEEN_BASE (self), TRUE);

    klass = LRG_TWEEN_BASE_GET_CLASS (self);
    if (klass->is_finished != NULL)
        return klass->is_finished (self);

    return TRUE;
}

gboolean
lrg_tween_base_is_running (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWEEN_BASE (self), FALSE);

    priv = lrg_tween_base_get_instance_private (self);

    return priv->state == LRG_TWEEN_STATE_RUNNING;
}

gboolean
lrg_tween_base_is_paused (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWEEN_BASE (self), FALSE);

    priv = lrg_tween_base_get_instance_private (self);

    return priv->state == LRG_TWEEN_STATE_PAUSED;
}

LrgTweenState
lrg_tween_base_get_state (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWEEN_BASE (self), LRG_TWEEN_STATE_IDLE);

    priv = lrg_tween_base_get_instance_private (self);

    return priv->state;
}

gfloat
lrg_tween_base_get_duration (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWEEN_BASE (self), 0.0f);

    priv = lrg_tween_base_get_instance_private (self);

    return priv->duration;
}

void
lrg_tween_base_set_duration (LrgTweenBase *self,
                             gfloat        duration)
{
    LrgTweenBasePrivate *priv;

    g_return_if_fail (LRG_IS_TWEEN_BASE (self));
    g_return_if_fail (duration > 0.0f);

    priv = lrg_tween_base_get_instance_private (self);

    if (priv->duration != duration)
    {
        priv->duration = duration;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DURATION]);
    }
}

gfloat
lrg_tween_base_get_delay (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWEEN_BASE (self), 0.0f);

    priv = lrg_tween_base_get_instance_private (self);

    return priv->delay;
}

void
lrg_tween_base_set_delay (LrgTweenBase *self,
                          gfloat        delay)
{
    LrgTweenBasePrivate *priv;

    g_return_if_fail (LRG_IS_TWEEN_BASE (self));
    g_return_if_fail (delay >= 0.0f);

    priv = lrg_tween_base_get_instance_private (self);

    if (priv->delay != delay)
    {
        priv->delay = delay;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DELAY]);
    }
}

gfloat
lrg_tween_base_get_elapsed (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWEEN_BASE (self), 0.0f);

    priv = lrg_tween_base_get_instance_private (self);

    return priv->elapsed;
}

gfloat
lrg_tween_base_get_progress (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWEEN_BASE (self), 0.0f);

    priv = lrg_tween_base_get_instance_private (self);

    return priv->progress;
}

LrgEasingType
lrg_tween_base_get_easing (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWEEN_BASE (self), LRG_EASING_LINEAR);

    priv = lrg_tween_base_get_instance_private (self);

    return priv->easing;
}

void
lrg_tween_base_set_easing (LrgTweenBase  *self,
                           LrgEasingType  easing)
{
    LrgTweenBasePrivate *priv;

    g_return_if_fail (LRG_IS_TWEEN_BASE (self));

    priv = lrg_tween_base_get_instance_private (self);

    if (priv->easing != easing)
    {
        priv->easing = easing;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EASING]);
    }
}

gint
lrg_tween_base_get_loop_count (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWEEN_BASE (self), 0);

    priv = lrg_tween_base_get_instance_private (self);

    return priv->loop_count;
}

void
lrg_tween_base_set_loop_count (LrgTweenBase *self,
                               gint          count)
{
    LrgTweenBasePrivate *priv;

    g_return_if_fail (LRG_IS_TWEEN_BASE (self));
    g_return_if_fail (count >= -1);

    priv = lrg_tween_base_get_instance_private (self);

    if (priv->loop_count != count)
    {
        priv->loop_count = count;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOP_COUNT]);
    }
}

LrgTweenLoopMode
lrg_tween_base_get_loop_mode (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWEEN_BASE (self), LRG_TWEEN_LOOP_RESTART);

    priv = lrg_tween_base_get_instance_private (self);

    return priv->loop_mode;
}

void
lrg_tween_base_set_loop_mode (LrgTweenBase     *self,
                              LrgTweenLoopMode  mode)
{
    LrgTweenBasePrivate *priv;

    g_return_if_fail (LRG_IS_TWEEN_BASE (self));

    priv = lrg_tween_base_get_instance_private (self);

    if (priv->loop_mode != mode)
    {
        priv->loop_mode = mode;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOP_MODE]);
    }
}

gint
lrg_tween_base_get_current_loop (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWEEN_BASE (self), 0);

    priv = lrg_tween_base_get_instance_private (self);

    return priv->current_loop;
}

gboolean
lrg_tween_base_get_auto_start (LrgTweenBase *self)
{
    LrgTweenBasePrivate *priv;

    g_return_val_if_fail (LRG_IS_TWEEN_BASE (self), TRUE);

    priv = lrg_tween_base_get_instance_private (self);

    return priv->auto_start;
}

void
lrg_tween_base_set_auto_start (LrgTweenBase *self,
                               gboolean      auto_start)
{
    LrgTweenBasePrivate *priv;

    g_return_if_fail (LRG_IS_TWEEN_BASE (self));

    priv = lrg_tween_base_get_instance_private (self);

    if (priv->auto_start != auto_start)
    {
        priv->auto_start = auto_start;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTO_START]);
    }
}
