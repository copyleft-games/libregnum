/* lrg-animation-state.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-animation-state.h"

/**
 * SECTION:lrg-animation-state
 * @Title: LrgAnimationState
 * @Short_description: Animation state for state machines
 *
 * #LrgAnimationState represents a single state in an animation
 * state machine. Each state has an associated animation clip,
 * playback settings, and callbacks for enter/exit/update.
 */

typedef struct
{
    gchar            *name;
    LrgAnimationClip *clip;
    gfloat            speed;
    gboolean          mirror;
    gfloat            time;
} LrgAnimationStatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgAnimationState, lrg_animation_state, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_NAME,
    PROP_CLIP,
    PROP_SPEED,
    PROP_MIRROR,
    PROP_TIME,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/*
 * Virtual method implementations
 */

static void
lrg_animation_state_real_enter (LrgAnimationState *self)
{
    LrgAnimationStatePrivate *priv;

    priv = lrg_animation_state_get_instance_private (self);

    /* Reset time on enter */
    priv->time = 0.0f;
}

static void
lrg_animation_state_real_exit (LrgAnimationState *self)
{
    (void)self;
    /* Default implementation does nothing */
}

static void
lrg_animation_state_real_update (LrgAnimationState *self,
                                  gfloat             delta_time)
{
    LrgAnimationStatePrivate *priv;

    priv = lrg_animation_state_get_instance_private (self);

    /* Update time */
    priv->time += delta_time * priv->speed;
}

static void
lrg_animation_state_real_sample (LrgAnimationState *self,
                                  LrgBonePose       *out_pose,
                                  const gchar       *bone_name)
{
    LrgAnimationStatePrivate *priv;
    guint track_index;
    guint i;

    priv = lrg_animation_state_get_instance_private (self);

    if (priv->clip == NULL)
    {
        lrg_bone_pose_set_identity (out_pose);
        return;
    }

    /* Find track by bone name */
    track_index = 0;
    for (i = 0; i < lrg_animation_clip_get_track_count (priv->clip); i++)
    {
        const gchar *track_bone;

        track_bone = lrg_animation_clip_get_track_bone_name (priv->clip, i);
        if (track_bone != NULL && g_strcmp0 (track_bone, bone_name) == 0)
        {
            track_index = i;
            break;
        }
    }

    lrg_animation_clip_sample_track (priv->clip, track_index, priv->time, out_pose);

    /* Apply mirroring if enabled (swap left/right bones, negate X) */
    if (priv->mirror)
    {
        out_pose->position_x = -out_pose->position_x;
        out_pose->rotation_y = -out_pose->rotation_y;
        out_pose->rotation_z = -out_pose->rotation_z;
    }
}

/*
 * GObject virtual methods
 */

static void
lrg_animation_state_finalize (GObject *object)
{
    LrgAnimationState *self = LRG_ANIMATION_STATE (object);
    LrgAnimationStatePrivate *priv = lrg_animation_state_get_instance_private (self);

    g_free (priv->name);
    g_clear_object (&priv->clip);

    G_OBJECT_CLASS (lrg_animation_state_parent_class)->finalize (object);
}

static void
lrg_animation_state_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgAnimationState *self = LRG_ANIMATION_STATE (object);
    LrgAnimationStatePrivate *priv = lrg_animation_state_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_CLIP:
        g_value_set_object (value, priv->clip);
        break;
    case PROP_SPEED:
        g_value_set_float (value, priv->speed);
        break;
    case PROP_MIRROR:
        g_value_set_boolean (value, priv->mirror);
        break;
    case PROP_TIME:
        g_value_set_float (value, priv->time);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_animation_state_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgAnimationState *self = LRG_ANIMATION_STATE (object);
    LrgAnimationStatePrivate *priv = lrg_animation_state_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_free (priv->name);
        priv->name = g_value_dup_string (value);
        break;
    case PROP_CLIP:
        g_set_object (&priv->clip, g_value_get_object (value));
        break;
    case PROP_SPEED:
        priv->speed = g_value_get_float (value);
        break;
    case PROP_MIRROR:
        priv->mirror = g_value_get_boolean (value);
        break;
    case PROP_TIME:
        priv->time = g_value_get_float (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_animation_state_class_init (LrgAnimationStateClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_animation_state_finalize;
    object_class->get_property = lrg_animation_state_get_property;
    object_class->set_property = lrg_animation_state_set_property;

    /* Virtual methods */
    klass->enter = lrg_animation_state_real_enter;
    klass->exit = lrg_animation_state_real_exit;
    klass->update = lrg_animation_state_real_update;
    klass->sample = lrg_animation_state_real_sample;

    /**
     * LrgAnimationState:name:
     *
     * The state name.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "State name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgAnimationState:clip:
     *
     * The animation clip.
     */
    properties[PROP_CLIP] =
        g_param_spec_object ("clip",
                             "Clip",
                             "Animation clip",
                             LRG_TYPE_ANIMATION_CLIP,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgAnimationState:speed:
     *
     * The playback speed multiplier.
     */
    properties[PROP_SPEED] =
        g_param_spec_float ("speed",
                            "Speed",
                            "Playback speed",
                            -10.0f, 10.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgAnimationState:mirror:
     *
     * Whether the animation is mirrored.
     */
    properties[PROP_MIRROR] =
        g_param_spec_boolean ("mirror",
                              "Mirror",
                              "Mirror animation",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgAnimationState:time:
     *
     * The current playback time.
     */
    properties[PROP_TIME] =
        g_param_spec_float ("time",
                            "Time",
                            "Playback time",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_animation_state_init (LrgAnimationState *self)
{
    LrgAnimationStatePrivate *priv = lrg_animation_state_get_instance_private (self);

    priv->name = NULL;
    priv->clip = NULL;
    priv->speed = 1.0f;
    priv->mirror = FALSE;
    priv->time = 0.0f;
}

/*
 * Public API
 */

LrgAnimationState *
lrg_animation_state_new (const gchar *name)
{
    return g_object_new (LRG_TYPE_ANIMATION_STATE,
                         "name", name,
                         NULL);
}

const gchar *
lrg_animation_state_get_name (LrgAnimationState *self)
{
    LrgAnimationStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), NULL);

    priv = lrg_animation_state_get_instance_private (self);
    return priv->name;
}

LrgAnimationClip *
lrg_animation_state_get_clip (LrgAnimationState *self)
{
    LrgAnimationStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), NULL);

    priv = lrg_animation_state_get_instance_private (self);
    return priv->clip;
}

void
lrg_animation_state_set_clip (LrgAnimationState *self,
                               LrgAnimationClip  *clip)
{
    LrgAnimationStatePrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATION_STATE (self));

    priv = lrg_animation_state_get_instance_private (self);

    if (g_set_object (&priv->clip, clip))
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CLIP]);
}

gfloat
lrg_animation_state_get_speed (LrgAnimationState *self)
{
    LrgAnimationStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), 1.0f);

    priv = lrg_animation_state_get_instance_private (self);
    return priv->speed;
}

void
lrg_animation_state_set_speed (LrgAnimationState *self,
                                gfloat             speed)
{
    LrgAnimationStatePrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATION_STATE (self));

    priv = lrg_animation_state_get_instance_private (self);

    if (priv->speed != speed)
    {
        priv->speed = speed;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPEED]);
    }
}

gboolean
lrg_animation_state_get_mirror (LrgAnimationState *self)
{
    LrgAnimationStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), FALSE);

    priv = lrg_animation_state_get_instance_private (self);
    return priv->mirror;
}

void
lrg_animation_state_set_mirror (LrgAnimationState *self,
                                 gboolean           mirror)
{
    LrgAnimationStatePrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATION_STATE (self));

    priv = lrg_animation_state_get_instance_private (self);

    if (priv->mirror != mirror)
    {
        priv->mirror = mirror;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIRROR]);
    }
}

gfloat
lrg_animation_state_get_time (LrgAnimationState *self)
{
    LrgAnimationStatePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), 0.0f);

    priv = lrg_animation_state_get_instance_private (self);
    return priv->time;
}

void
lrg_animation_state_set_time (LrgAnimationState *self,
                               gfloat             time)
{
    LrgAnimationStatePrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATION_STATE (self));

    priv = lrg_animation_state_get_instance_private (self);

    if (priv->time != time)
    {
        priv->time = time;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME]);
    }
}

gfloat
lrg_animation_state_get_normalized_time (LrgAnimationState *self)
{
    LrgAnimationStatePrivate *priv;
    gfloat duration;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), 0.0f);

    priv = lrg_animation_state_get_instance_private (self);

    if (priv->clip == NULL)
        return 0.0f;

    duration = lrg_animation_clip_get_duration (priv->clip);
    if (duration <= 0.0f)
        return 0.0f;

    return priv->time / duration;
}

void
lrg_animation_state_enter (LrgAnimationState *self)
{
    LrgAnimationStateClass *klass;

    g_return_if_fail (LRG_IS_ANIMATION_STATE (self));

    klass = LRG_ANIMATION_STATE_GET_CLASS (self);
    if (klass->enter != NULL)
        klass->enter (self);
}

void
lrg_animation_state_exit (LrgAnimationState *self)
{
    LrgAnimationStateClass *klass;

    g_return_if_fail (LRG_IS_ANIMATION_STATE (self));

    klass = LRG_ANIMATION_STATE_GET_CLASS (self);
    if (klass->exit != NULL)
        klass->exit (self);
}

void
lrg_animation_state_update (LrgAnimationState *self,
                             gfloat             delta_time)
{
    LrgAnimationStateClass *klass;

    g_return_if_fail (LRG_IS_ANIMATION_STATE (self));

    klass = LRG_ANIMATION_STATE_GET_CLASS (self);
    if (klass->update != NULL)
        klass->update (self, delta_time);
}

void
lrg_animation_state_sample (LrgAnimationState *self,
                             LrgBonePose       *out_pose,
                             const gchar       *bone_name)
{
    LrgAnimationStateClass *klass;

    g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
    g_return_if_fail (out_pose != NULL);

    klass = LRG_ANIMATION_STATE_GET_CLASS (self);
    if (klass->sample != NULL)
        klass->sample (self, out_pose, bone_name);
}
