/* lrg-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for scene transitions.
 */

#include "lrg-transition.h"
#include "../tween/lrg-easing.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TRANSITION

/**
 * LrgTransition:
 *
 * Abstract base class for scene transitions.
 *
 * #LrgTransition provides the framework for implementing visual effects
 * that occur when switching between scenes or game states. A transition
 * has three phases:
 *
 * 1. **OUT phase**: The current scene fades/transitions out
 * 2. **HOLD phase**: Brief pause at midpoint (this is when scene switching occurs)
 * 3. **IN phase**: The new scene fades/transitions in
 *
 * Subclasses implement specific visual effects by overriding the render()
 * virtual method.
 *
 * Since: 1.0
 */

typedef struct _LrgTransitionPrivate
{
    /* State */
    LrgTransitionState  state;
    gboolean            initialized;

    /* Timing */
    gfloat              out_duration;
    gfloat              hold_duration;
    gfloat              in_duration;
    gfloat              elapsed;

    /* Easing */
    LrgEasingType       easing;

    /* Progress tracking */
    gfloat              phase_progress;
    gboolean            midpoint_reached;
} LrgTransitionPrivate;

enum
{
    PROP_0,
    PROP_STATE,
    PROP_OUT_DURATION,
    PROP_HOLD_DURATION,
    PROP_IN_DURATION,
    PROP_EASING,
    PROP_PROGRESS,
    N_PROPS
};

enum
{
    SIGNAL_STARTED,
    SIGNAL_MIDPOINT_REACHED,
    SIGNAL_COMPLETED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgTransition, lrg_transition, G_TYPE_OBJECT)

/*
 * Calculate total duration
 */
static gfloat
get_total_duration (LrgTransition *self)
{
    LrgTransitionPrivate *priv;

    priv = lrg_transition_get_instance_private (self);

    return priv->out_duration + priv->hold_duration + priv->in_duration;
}

/*
 * GObject virtual methods
 */

static void
lrg_transition_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgTransition *self;
    LrgTransitionPrivate *priv;

    self = LRG_TRANSITION (object);
    priv = lrg_transition_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_STATE:
        g_value_set_enum (value, priv->state);
        break;

    case PROP_OUT_DURATION:
        g_value_set_float (value, priv->out_duration);
        break;

    case PROP_HOLD_DURATION:
        g_value_set_float (value, priv->hold_duration);
        break;

    case PROP_IN_DURATION:
        g_value_set_float (value, priv->in_duration);
        break;

    case PROP_EASING:
        g_value_set_enum (value, priv->easing);
        break;

    case PROP_PROGRESS:
        g_value_set_float (value, lrg_transition_get_progress (self));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_transition_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgTransition *self;
    LrgTransitionPrivate *priv;

    self = LRG_TRANSITION (object);
    priv = lrg_transition_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_OUT_DURATION:
        priv->out_duration = g_value_get_float (value);
        break;

    case PROP_HOLD_DURATION:
        priv->hold_duration = g_value_get_float (value);
        break;

    case PROP_IN_DURATION:
        priv->in_duration = g_value_get_float (value);
        break;

    case PROP_EASING:
        priv->easing = g_value_get_enum (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_transition_dispose (GObject *object)
{
    LrgTransition *self;
    LrgTransitionPrivate *priv;

    self = LRG_TRANSITION (object);
    priv = lrg_transition_get_instance_private (self);

    if (priv->initialized)
    {
        lrg_transition_shutdown (self);
    }

    G_OBJECT_CLASS (lrg_transition_parent_class)->dispose (object);
}

static void
lrg_transition_class_init (LrgTransitionClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->get_property = lrg_transition_get_property;
    object_class->set_property = lrg_transition_set_property;
    object_class->dispose = lrg_transition_dispose;

    /**
     * LrgTransition:state:
     *
     * The current state of the transition.
     *
     * Since: 1.0
     */
    properties[PROP_STATE] =
        g_param_spec_enum ("state",
                           "State",
                           "The current transition state",
                           LRG_TYPE_TRANSITION_STATE,
                           LRG_TRANSITION_STATE_IDLE,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTransition:out-duration:
     *
     * Duration of the OUT phase in seconds.
     *
     * Since: 1.0
     */
    properties[PROP_OUT_DURATION] =
        g_param_spec_float ("out-duration",
                            "Out Duration",
                            "Duration of the OUT phase",
                            0.0f,
                            G_MAXFLOAT,
                            0.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_CONSTRUCT |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgTransition:hold-duration:
     *
     * Duration of the HOLD phase in seconds.
     *
     * Since: 1.0
     */
    properties[PROP_HOLD_DURATION] =
        g_param_spec_float ("hold-duration",
                            "Hold Duration",
                            "Duration of the HOLD phase",
                            0.0f,
                            G_MAXFLOAT,
                            0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_CONSTRUCT |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgTransition:in-duration:
     *
     * Duration of the IN phase in seconds.
     *
     * Since: 1.0
     */
    properties[PROP_IN_DURATION] =
        g_param_spec_float ("in-duration",
                            "In Duration",
                            "Duration of the IN phase",
                            0.0f,
                            G_MAXFLOAT,
                            0.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_CONSTRUCT |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgTransition:easing:
     *
     * The easing function to use for the transition.
     *
     * Since: 1.0
     */
    properties[PROP_EASING] =
        g_param_spec_enum ("easing",
                           "Easing",
                           "The easing function type",
                           LRG_TYPE_EASING_TYPE,
                           LRG_EASING_LINEAR,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgTransition:progress:
     *
     * Overall progress of the transition (0.0 to 1.0).
     *
     * Since: 1.0
     */
    properties[PROP_PROGRESS] =
        g_param_spec_float ("progress",
                            "Progress",
                            "Overall transition progress",
                            0.0f,
                            1.0f,
                            0.0f,
                            G_PARAM_READABLE |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgTransition::started:
     * @self: The transition
     *
     * Emitted when the transition starts.
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
     * LrgTransition::midpoint-reached:
     * @self: The transition
     *
     * Emitted when the transition reaches its midpoint (HOLD state).
     * This is when the scene should be switched.
     *
     * Since: 1.0
     */
    signals[SIGNAL_MIDPOINT_REACHED] =
        g_signal_new ("midpoint-reached",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgTransition::completed:
     * @self: The transition
     *
     * Emitted when the transition completes.
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
}

static void
lrg_transition_init (LrgTransition *self)
{
    LrgTransitionPrivate *priv;

    priv = lrg_transition_get_instance_private (self);

    priv->state = LRG_TRANSITION_STATE_IDLE;
    priv->initialized = FALSE;
    priv->out_duration = 0.5f;
    priv->hold_duration = 0.0f;
    priv->in_duration = 0.5f;
    priv->elapsed = 0.0f;
    priv->easing = LRG_EASING_LINEAR;
    priv->phase_progress = 0.0f;
    priv->midpoint_reached = FALSE;
}

/*
 * Public API
 */

/**
 * lrg_transition_initialize:
 * @self: A #LrgTransition
 * @width: Viewport width
 * @height: Viewport height
 * @error: (nullable): Return location for error
 *
 * Initializes the transition's resources.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 1.0
 */
gboolean
lrg_transition_initialize (LrgTransition  *self,
                           guint           width,
                           guint           height,
                           GError        **error)
{
    LrgTransitionClass *klass;
    LrgTransitionPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSITION (self), FALSE);

    priv = lrg_transition_get_instance_private (self);

    if (priv->initialized)
    {
        return TRUE;
    }

    klass = LRG_TRANSITION_GET_CLASS (self);
    if (klass->initialize != NULL)
    {
        if (!klass->initialize (self, width, height, error))
        {
            return FALSE;
        }
    }

    priv->initialized = TRUE;
    return TRUE;
}

/**
 * lrg_transition_shutdown:
 * @self: A #LrgTransition
 *
 * Frees resources allocated during initialization.
 *
 * Since: 1.0
 */
void
lrg_transition_shutdown (LrgTransition *self)
{
    LrgTransitionClass *klass;
    LrgTransitionPrivate *priv;

    g_return_if_fail (LRG_IS_TRANSITION (self));

    priv = lrg_transition_get_instance_private (self);

    if (!priv->initialized)
    {
        return;
    }

    klass = LRG_TRANSITION_GET_CLASS (self);
    if (klass->shutdown != NULL)
    {
        klass->shutdown (self);
    }

    priv->initialized = FALSE;
}

/**
 * lrg_transition_start:
 * @self: A #LrgTransition
 *
 * Begins the transition. The transition will go through OUT, HOLD, and IN phases.
 *
 * Since: 1.0
 */
void
lrg_transition_start (LrgTransition *self)
{
    LrgTransitionClass *klass;
    LrgTransitionPrivate *priv;

    g_return_if_fail (LRG_IS_TRANSITION (self));

    priv = lrg_transition_get_instance_private (self);

    priv->elapsed = 0.0f;
    priv->phase_progress = 0.0f;
    priv->midpoint_reached = FALSE;
    priv->state = LRG_TRANSITION_STATE_OUT;

    klass = LRG_TRANSITION_GET_CLASS (self);
    if (klass->start != NULL)
    {
        klass->start (self);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
    g_signal_emit (self, signals[SIGNAL_STARTED], 0);
}

/**
 * lrg_transition_update:
 * @self: A #LrgTransition
 * @delta_time: Time elapsed since last update in seconds
 *
 * Updates the transition state.
 *
 * Since: 1.0
 */
void
lrg_transition_update (LrgTransition *self,
                       gfloat         delta_time)
{
    LrgTransitionClass *klass;
    LrgTransitionPrivate *priv;
    gfloat total_duration;
    LrgTransitionState old_state;

    g_return_if_fail (LRG_IS_TRANSITION (self));

    priv = lrg_transition_get_instance_private (self);

    if (priv->state == LRG_TRANSITION_STATE_IDLE ||
        priv->state == LRG_TRANSITION_STATE_COMPLETE)
    {
        return;
    }

    old_state = priv->state;
    priv->elapsed += delta_time;
    total_duration = get_total_duration (self);

    /* Determine current phase and phase progress */
    if (priv->elapsed < priv->out_duration)
    {
        /* OUT phase */
        priv->state = LRG_TRANSITION_STATE_OUT;
        priv->phase_progress = priv->out_duration > 0.0f
            ? priv->elapsed / priv->out_duration
            : 1.0f;
    }
    else if (priv->elapsed < priv->out_duration + priv->hold_duration)
    {
        /* HOLD phase */
        priv->state = LRG_TRANSITION_STATE_HOLD;
        priv->phase_progress = priv->hold_duration > 0.0f
            ? (priv->elapsed - priv->out_duration) / priv->hold_duration
            : 1.0f;

        /* Emit midpoint signal once */
        if (!priv->midpoint_reached)
        {
            priv->midpoint_reached = TRUE;
            g_signal_emit (self, signals[SIGNAL_MIDPOINT_REACHED], 0);
        }
    }
    else if (priv->elapsed < total_duration)
    {
        /* IN phase */
        priv->state = LRG_TRANSITION_STATE_IN;
        priv->phase_progress = priv->in_duration > 0.0f
            ? (priv->elapsed - priv->out_duration - priv->hold_duration) / priv->in_duration
            : 1.0f;

        /* Also emit midpoint if skipped (zero hold duration) */
        if (!priv->midpoint_reached)
        {
            priv->midpoint_reached = TRUE;
            g_signal_emit (self, signals[SIGNAL_MIDPOINT_REACHED], 0);
        }
    }
    else
    {
        /* Complete */
        priv->state = LRG_TRANSITION_STATE_COMPLETE;
        priv->phase_progress = 1.0f;

        g_signal_emit (self, signals[SIGNAL_COMPLETED], 0);
    }

    /* Clamp phase progress */
    priv->phase_progress = CLAMP (priv->phase_progress, 0.0f, 1.0f);

    /* Notify if state changed */
    if (old_state != priv->state)
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
    }

    /* Call subclass update */
    klass = LRG_TRANSITION_GET_CLASS (self);
    if (klass->update != NULL)
    {
        klass->update (self, delta_time);
    }
}

/**
 * lrg_transition_render:
 * @self: A #LrgTransition
 * @old_scene_texture: Texture ID of the outgoing scene
 * @new_scene_texture: Texture ID of the incoming scene
 * @width: Viewport width
 * @height: Viewport height
 *
 * Renders the transition effect.
 *
 * Since: 1.0
 */
void
lrg_transition_render (LrgTransition *self,
                       guint          old_scene_texture,
                       guint          new_scene_texture,
                       guint          width,
                       guint          height)
{
    LrgTransitionClass *klass;

    g_return_if_fail (LRG_IS_TRANSITION (self));

    klass = LRG_TRANSITION_GET_CLASS (self);
    if (klass->render != NULL)
    {
        klass->render (self, old_scene_texture, new_scene_texture, width, height);
    }
}

/**
 * lrg_transition_reset:
 * @self: A #LrgTransition
 *
 * Resets the transition to its initial state.
 *
 * Since: 1.0
 */
void
lrg_transition_reset (LrgTransition *self)
{
    LrgTransitionClass *klass;
    LrgTransitionPrivate *priv;

    g_return_if_fail (LRG_IS_TRANSITION (self));

    priv = lrg_transition_get_instance_private (self);

    priv->state = LRG_TRANSITION_STATE_IDLE;
    priv->elapsed = 0.0f;
    priv->phase_progress = 0.0f;
    priv->midpoint_reached = FALSE;

    klass = LRG_TRANSITION_GET_CLASS (self);
    if (klass->reset != NULL)
    {
        klass->reset (self);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
}

/**
 * lrg_transition_get_state:
 * @self: A #LrgTransition
 *
 * Gets the current transition state.
 *
 * Returns: The current #LrgTransitionState
 *
 * Since: 1.0
 */
LrgTransitionState
lrg_transition_get_state (LrgTransition *self)
{
    LrgTransitionPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSITION (self), LRG_TRANSITION_STATE_IDLE);

    priv = lrg_transition_get_instance_private (self);

    return priv->state;
}

/**
 * lrg_transition_is_complete:
 * @self: A #LrgTransition
 *
 * Checks if the transition has completed.
 *
 * Returns: %TRUE if complete
 *
 * Since: 1.0
 */
gboolean
lrg_transition_is_complete (LrgTransition *self)
{
    LrgTransitionPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSITION (self), TRUE);

    priv = lrg_transition_get_instance_private (self);

    return priv->state == LRG_TRANSITION_STATE_COMPLETE;
}

/**
 * lrg_transition_is_at_midpoint:
 * @self: A #LrgTransition
 *
 * Checks if the transition is at the midpoint (HOLD state).
 * This is when the scene should be switched.
 *
 * Returns: %TRUE if at midpoint
 *
 * Since: 1.0
 */
gboolean
lrg_transition_is_at_midpoint (LrgTransition *self)
{
    LrgTransitionPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSITION (self), FALSE);

    priv = lrg_transition_get_instance_private (self);

    return priv->state == LRG_TRANSITION_STATE_HOLD;
}

/**
 * lrg_transition_get_duration:
 * @self: A #LrgTransition
 *
 * Gets the total duration of the transition (out + hold + in).
 *
 * Returns: Duration in seconds
 *
 * Since: 1.0
 */
gfloat
lrg_transition_get_duration (LrgTransition *self)
{
    g_return_val_if_fail (LRG_IS_TRANSITION (self), 0.0f);

    return get_total_duration (self);
}

/**
 * lrg_transition_set_duration:
 * @self: A #LrgTransition
 * @duration: Duration in seconds
 *
 * Sets the total duration of the transition.
 * The out, hold, and in phases will be proportionally adjusted.
 *
 * Since: 1.0
 */
void
lrg_transition_set_duration (LrgTransition *self,
                             gfloat         duration)
{
    LrgTransitionPrivate *priv;
    gfloat scale;
    gfloat current_total;

    g_return_if_fail (LRG_IS_TRANSITION (self));
    g_return_if_fail (duration >= 0.0f);

    priv = lrg_transition_get_instance_private (self);

    current_total = get_total_duration (self);
    if (current_total <= 0.0f)
    {
        /* Set to simple 50/0/50 split if no current duration */
        priv->out_duration = duration / 2.0f;
        priv->hold_duration = 0.0f;
        priv->in_duration = duration / 2.0f;
    }
    else
    {
        scale = duration / current_total;
        priv->out_duration *= scale;
        priv->hold_duration *= scale;
        priv->in_duration *= scale;
    }
}

/**
 * lrg_transition_get_out_duration:
 * @self: A #LrgTransition
 *
 * Gets the duration of the OUT phase.
 *
 * Returns: Duration in seconds
 *
 * Since: 1.0
 */
gfloat
lrg_transition_get_out_duration (LrgTransition *self)
{
    LrgTransitionPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSITION (self), 0.0f);

    priv = lrg_transition_get_instance_private (self);

    return priv->out_duration;
}

/**
 * lrg_transition_set_out_duration:
 * @self: A #LrgTransition
 * @duration: Duration in seconds
 *
 * Sets the duration of the OUT phase.
 *
 * Since: 1.0
 */
void
lrg_transition_set_out_duration (LrgTransition *self,
                                 gfloat         duration)
{
    LrgTransitionPrivate *priv;

    g_return_if_fail (LRG_IS_TRANSITION (self));
    g_return_if_fail (duration >= 0.0f);

    priv = lrg_transition_get_instance_private (self);

    if (priv->out_duration != duration)
    {
        priv->out_duration = duration;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OUT_DURATION]);
    }
}

/**
 * lrg_transition_get_hold_duration:
 * @self: A #LrgTransition
 *
 * Gets the duration of the HOLD phase.
 *
 * Returns: Duration in seconds
 *
 * Since: 1.0
 */
gfloat
lrg_transition_get_hold_duration (LrgTransition *self)
{
    LrgTransitionPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSITION (self), 0.0f);

    priv = lrg_transition_get_instance_private (self);

    return priv->hold_duration;
}

/**
 * lrg_transition_set_hold_duration:
 * @self: A #LrgTransition
 * @duration: Duration in seconds
 *
 * Sets the duration of the HOLD phase.
 *
 * Since: 1.0
 */
void
lrg_transition_set_hold_duration (LrgTransition *self,
                                  gfloat         duration)
{
    LrgTransitionPrivate *priv;

    g_return_if_fail (LRG_IS_TRANSITION (self));
    g_return_if_fail (duration >= 0.0f);

    priv = lrg_transition_get_instance_private (self);

    if (priv->hold_duration != duration)
    {
        priv->hold_duration = duration;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOLD_DURATION]);
    }
}

/**
 * lrg_transition_get_in_duration:
 * @self: A #LrgTransition
 *
 * Gets the duration of the IN phase.
 *
 * Returns: Duration in seconds
 *
 * Since: 1.0
 */
gfloat
lrg_transition_get_in_duration (LrgTransition *self)
{
    LrgTransitionPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSITION (self), 0.0f);

    priv = lrg_transition_get_instance_private (self);

    return priv->in_duration;
}

/**
 * lrg_transition_set_in_duration:
 * @self: A #LrgTransition
 * @duration: Duration in seconds
 *
 * Sets the duration of the IN phase.
 *
 * Since: 1.0
 */
void
lrg_transition_set_in_duration (LrgTransition *self,
                                gfloat         duration)
{
    LrgTransitionPrivate *priv;

    g_return_if_fail (LRG_IS_TRANSITION (self));
    g_return_if_fail (duration >= 0.0f);

    priv = lrg_transition_get_instance_private (self);

    if (priv->in_duration != duration)
    {
        priv->in_duration = duration;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IN_DURATION]);
    }
}

/**
 * lrg_transition_get_progress:
 * @self: A #LrgTransition
 *
 * Gets the overall progress of the transition (0.0 to 1.0).
 *
 * Returns: Progress value
 *
 * Since: 1.0
 */
gfloat
lrg_transition_get_progress (LrgTransition *self)
{
    LrgTransitionPrivate *priv;
    gfloat total_duration;

    g_return_val_if_fail (LRG_IS_TRANSITION (self), 0.0f);

    priv = lrg_transition_get_instance_private (self);

    total_duration = get_total_duration (self);
    if (total_duration <= 0.0f)
    {
        return priv->state == LRG_TRANSITION_STATE_COMPLETE ? 1.0f : 0.0f;
    }

    return CLAMP (priv->elapsed / total_duration, 0.0f, 1.0f);
}

/**
 * lrg_transition_get_phase_progress:
 * @self: A #LrgTransition
 *
 * Gets the progress within the current phase (0.0 to 1.0).
 *
 * Returns: Phase progress value
 *
 * Since: 1.0
 */
gfloat
lrg_transition_get_phase_progress (LrgTransition *self)
{
    LrgTransitionPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSITION (self), 0.0f);

    priv = lrg_transition_get_instance_private (self);

    return priv->phase_progress;
}

/**
 * lrg_transition_get_easing:
 * @self: A #LrgTransition
 *
 * Gets the easing function type used for the transition.
 *
 * Returns: The #LrgEasingType
 *
 * Since: 1.0
 */
LrgEasingType
lrg_transition_get_easing (LrgTransition *self)
{
    LrgTransitionPrivate *priv;

    g_return_val_if_fail (LRG_IS_TRANSITION (self), LRG_EASING_LINEAR);

    priv = lrg_transition_get_instance_private (self);

    return priv->easing;
}

/**
 * lrg_transition_set_easing:
 * @self: A #LrgTransition
 * @easing: The easing function type
 *
 * Sets the easing function to use for the transition.
 *
 * Since: 1.0
 */
void
lrg_transition_set_easing (LrgTransition *self,
                           LrgEasingType  easing)
{
    LrgTransitionPrivate *priv;

    g_return_if_fail (LRG_IS_TRANSITION (self));

    priv = lrg_transition_get_instance_private (self);

    if (priv->easing != easing)
    {
        priv->easing = easing;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EASING]);
    }
}
