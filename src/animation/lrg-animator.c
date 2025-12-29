/* lrg-animator.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-animator.h"

/**
 * SECTION:lrg-animator
 * @Title: LrgAnimator
 * @Short_description: Animation playback controller
 *
 * #LrgAnimator manages animation playback for a skeleton.
 * It supports playing clips, crossfading between animations,
 * and fires events at the correct times.
 */

typedef struct
{
    LrgSkeleton         *skeleton;
    GHashTable          *clips;         /* name -> LrgAnimationClip */
    gchar               *current_clip;
    gchar               *blend_clip;    /* Target clip for crossfade */
    gfloat               time;
    gfloat               blend_time;
    gfloat               blend_duration;
    gfloat               blend_progress;
    gfloat               speed;
    gfloat               prev_time;     /* For event detection */
    LrgAnimatorState     state;
} LrgAnimatorPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgAnimator, lrg_animator, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_SKELETON,
    PROP_SPEED,
    N_PROPS
};

enum
{
    SIGNAL_EVENT,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

static void
lrg_animator_real_update (LrgAnimator *self,
                          gfloat       delta_time)
{
    LrgAnimatorPrivate *priv;
    LrgAnimationClip *clip;
    LrgAnimationClip *blend_clip;
    gfloat adjusted_time;
    guint i;
    GList *events;
    GList *l;

    priv = lrg_animator_get_instance_private (self);

    if (priv->state != LRG_ANIMATOR_PLAYING)
        return;

    if (priv->current_clip == NULL)
        return;

    clip = g_hash_table_lookup (priv->clips, priv->current_clip);
    if (clip == NULL)
        return;

    /* Store previous time for event detection */
    priv->prev_time = priv->time;

    /* Update time */
    adjusted_time = delta_time * priv->speed;
    priv->time += adjusted_time;

    /* Fire events in range */
    events = lrg_animation_clip_get_events_in_range (clip, priv->prev_time, priv->time);
    for (l = events; l != NULL; l = l->next)
    {
        LrgAnimationEvent *event = l->data;
        g_signal_emit (self, signals[SIGNAL_EVENT], 0, event);
    }
    g_list_free (events);

    /* Update blend progress */
    if (priv->blend_clip != NULL && priv->blend_duration > 0.0f)
    {
        priv->blend_progress += delta_time / priv->blend_duration;

        if (priv->blend_progress >= 1.0f)
        {
            /* Crossfade complete */
            g_free (priv->current_clip);
            priv->current_clip = priv->blend_clip;
            priv->blend_clip = NULL;
            priv->time = priv->blend_time;
            priv->blend_progress = 0.0f;
        }
        else
        {
            priv->blend_time += adjusted_time;
        }
    }

    /* Apply animation to skeleton */
    if (priv->skeleton == NULL)
        return;

    (void)lrg_skeleton_get_bone_count (priv->skeleton);

    /* Sample current clip */
    for (i = 0; i < lrg_animation_clip_get_track_count (clip); i++)
    {
        const gchar *bone_name;
        gint bone_index;
        LrgBonePose pose;

        bone_name = lrg_animation_clip_get_track_bone_name (clip, i);
        if (bone_name == NULL)
            continue;

        {
            LrgBone *bone;
            bone = lrg_skeleton_get_bone_by_name (priv->skeleton, bone_name);
            if (bone == NULL)
                continue;
            bone_index = lrg_bone_get_index (bone);
        }

        lrg_animation_clip_sample_track (clip, i, priv->time, &pose);

        /* Blend with second clip if crossfading */
        if (priv->blend_clip != NULL && priv->blend_progress > 0.0f)
        {
            LrgBonePose blend_pose;
            guint j;

            blend_clip = g_hash_table_lookup (priv->clips, priv->blend_clip);
            if (blend_clip != NULL)
            {
                /* Find matching track in blend clip */
                for (j = 0; j < lrg_animation_clip_get_track_count (blend_clip); j++)
                {
                    const gchar *blend_bone_name;

                    blend_bone_name = lrg_animation_clip_get_track_bone_name (blend_clip, j);
                    if (blend_bone_name != NULL && g_strcmp0 (blend_bone_name, bone_name) == 0)
                    {
                        lrg_animation_clip_sample_track (blend_clip, j, priv->blend_time, &blend_pose);
                        lrg_bone_pose_lerp_to (&pose, &blend_pose, priv->blend_progress, &pose);
                        break;
                    }
                }
            }
        }

        lrg_skeleton_set_pose (priv->skeleton, bone_index, &pose);
    }

    /* Calculate world transforms */
    lrg_skeleton_calculate_world_poses (priv->skeleton);
}

static void
lrg_animator_finalize (GObject *object)
{
    LrgAnimator *self = LRG_ANIMATOR (object);
    LrgAnimatorPrivate *priv = lrg_animator_get_instance_private (self);

    g_clear_object (&priv->skeleton);
    g_clear_pointer (&priv->clips, g_hash_table_unref);
    g_free (priv->current_clip);
    g_free (priv->blend_clip);

    G_OBJECT_CLASS (lrg_animator_parent_class)->finalize (object);
}

static void
lrg_animator_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgAnimator *self = LRG_ANIMATOR (object);
    LrgAnimatorPrivate *priv = lrg_animator_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_SKELETON:
        g_value_set_object (value, priv->skeleton);
        break;
    case PROP_SPEED:
        g_value_set_float (value, priv->speed);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_animator_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgAnimator *self = LRG_ANIMATOR (object);

    switch (prop_id)
    {
    case PROP_SKELETON:
        lrg_animator_set_skeleton (self, g_value_get_object (value));
        break;
    case PROP_SPEED:
        lrg_animator_set_speed (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_animator_class_init (LrgAnimatorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_animator_finalize;
    object_class->get_property = lrg_animator_get_property;
    object_class->set_property = lrg_animator_set_property;

    klass->update = lrg_animator_real_update;

    properties[PROP_SKELETON] =
        g_param_spec_object ("skeleton",
                             "Skeleton",
                             "The skeleton to animate",
                             LRG_TYPE_SKELETON,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_SPEED] =
        g_param_spec_float ("speed",
                            "Speed",
                            "Playback speed multiplier",
                            -10.0f, 10.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgAnimator::event:
     * @self: The animator
     * @event: The animation event
     *
     * Emitted when an animation event fires.
     */
    signals[SIGNAL_EVENT] =
        g_signal_new ("event",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgAnimatorClass, event),
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_ANIMATION_EVENT);
}

static void
lrg_animator_init (LrgAnimator *self)
{
    LrgAnimatorPrivate *priv = lrg_animator_get_instance_private (self);

    priv->skeleton = NULL;
    priv->clips = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, g_object_unref);
    priv->current_clip = NULL;
    priv->blend_clip = NULL;
    priv->time = 0.0f;
    priv->blend_time = 0.0f;
    priv->blend_duration = 0.0f;
    priv->blend_progress = 0.0f;
    priv->speed = 1.0f;
    priv->prev_time = 0.0f;
    priv->state = LRG_ANIMATOR_STOPPED;
}

LrgAnimator *
lrg_animator_new (LrgSkeleton *skeleton)
{
    return g_object_new (LRG_TYPE_ANIMATOR,
                         "skeleton", skeleton,
                         NULL);
}

LrgSkeleton *
lrg_animator_get_skeleton (LrgAnimator *self)
{
    LrgAnimatorPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR (self), NULL);

    priv = lrg_animator_get_instance_private (self);

    return priv->skeleton;
}

void
lrg_animator_set_skeleton (LrgAnimator *self,
                           LrgSkeleton *skeleton)
{
    LrgAnimatorPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR (self));

    priv = lrg_animator_get_instance_private (self);

    if (priv->skeleton == skeleton)
        return;

    g_clear_object (&priv->skeleton);
    if (skeleton != NULL)
        priv->skeleton = g_object_ref (skeleton);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SKELETON]);
}

void
lrg_animator_add_clip (LrgAnimator      *self,
                       const gchar      *name,
                       LrgAnimationClip *clip)
{
    LrgAnimatorPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR (self));
    g_return_if_fail (name != NULL);
    g_return_if_fail (LRG_IS_ANIMATION_CLIP (clip));

    priv = lrg_animator_get_instance_private (self);

    g_hash_table_insert (priv->clips, g_strdup (name), g_object_ref (clip));
}

void
lrg_animator_remove_clip (LrgAnimator *self,
                          const gchar *name)
{
    LrgAnimatorPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR (self));
    g_return_if_fail (name != NULL);

    priv = lrg_animator_get_instance_private (self);

    g_hash_table_remove (priv->clips, name);
}

LrgAnimationClip *
lrg_animator_get_clip (LrgAnimator *self,
                       const gchar *name)
{
    LrgAnimatorPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    priv = lrg_animator_get_instance_private (self);

    return g_hash_table_lookup (priv->clips, name);
}

void
lrg_animator_play (LrgAnimator *self,
                   const gchar *name)
{
    LrgAnimatorPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR (self));
    g_return_if_fail (name != NULL);

    priv = lrg_animator_get_instance_private (self);

    g_free (priv->current_clip);
    priv->current_clip = g_strdup (name);
    priv->time = 0.0f;
    priv->prev_time = 0.0f;
    priv->state = LRG_ANIMATOR_PLAYING;

    /* Cancel any crossfade */
    g_free (priv->blend_clip);
    priv->blend_clip = NULL;
    priv->blend_progress = 0.0f;
}

void
lrg_animator_crossfade (LrgAnimator *self,
                        const gchar *name,
                        gfloat       duration)
{
    LrgAnimatorPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR (self));
    g_return_if_fail (name != NULL);

    priv = lrg_animator_get_instance_private (self);

    /* If nothing is playing, just play directly */
    if (priv->current_clip == NULL || priv->state != LRG_ANIMATOR_PLAYING)
    {
        lrg_animator_play (self, name);
        return;
    }

    g_free (priv->blend_clip);
    priv->blend_clip = g_strdup (name);
    priv->blend_time = 0.0f;
    priv->blend_duration = MAX (duration, 0.001f);
    priv->blend_progress = 0.0f;
}

void
lrg_animator_stop (LrgAnimator *self)
{
    LrgAnimatorPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR (self));

    priv = lrg_animator_get_instance_private (self);

    priv->state = LRG_ANIMATOR_STOPPED;
    priv->time = 0.0f;
    priv->prev_time = 0.0f;

    g_free (priv->blend_clip);
    priv->blend_clip = NULL;
}

void
lrg_animator_pause (LrgAnimator *self)
{
    LrgAnimatorPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR (self));

    priv = lrg_animator_get_instance_private (self);

    if (priv->state == LRG_ANIMATOR_PLAYING)
        priv->state = LRG_ANIMATOR_PAUSED;
}

void
lrg_animator_resume (LrgAnimator *self)
{
    LrgAnimatorPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR (self));

    priv = lrg_animator_get_instance_private (self);

    if (priv->state == LRG_ANIMATOR_PAUSED)
        priv->state = LRG_ANIMATOR_PLAYING;
}

LrgAnimatorState
lrg_animator_get_state (LrgAnimator *self)
{
    LrgAnimatorPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR (self), LRG_ANIMATOR_STOPPED);

    priv = lrg_animator_get_instance_private (self);

    return priv->state;
}

const gchar *
lrg_animator_get_current_clip (LrgAnimator *self)
{
    LrgAnimatorPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR (self), NULL);

    priv = lrg_animator_get_instance_private (self);

    return priv->current_clip;
}

gfloat
lrg_animator_get_time (LrgAnimator *self)
{
    LrgAnimatorPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR (self), 0.0f);

    priv = lrg_animator_get_instance_private (self);

    return priv->time;
}

void
lrg_animator_set_time (LrgAnimator *self,
                       gfloat       time)
{
    LrgAnimatorPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR (self));

    priv = lrg_animator_get_instance_private (self);

    priv->time = MAX (0.0f, time);
    priv->prev_time = priv->time;
}

gfloat
lrg_animator_get_speed (LrgAnimator *self)
{
    LrgAnimatorPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR (self), 1.0f);

    priv = lrg_animator_get_instance_private (self);

    return priv->speed;
}

void
lrg_animator_set_speed (LrgAnimator *self,
                        gfloat       speed)
{
    LrgAnimatorPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR (self));

    priv = lrg_animator_get_instance_private (self);

    if (priv->speed == speed)
        return;

    priv->speed = speed;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPEED]);
}

void
lrg_animator_update (LrgAnimator *self,
                     gfloat       delta_time)
{
    LrgAnimatorClass *klass;

    g_return_if_fail (LRG_IS_ANIMATOR (self));

    klass = LRG_ANIMATOR_GET_CLASS (self);

    if (klass->update != NULL)
        klass->update (self, delta_time);
}
