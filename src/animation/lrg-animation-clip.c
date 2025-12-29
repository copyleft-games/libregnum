/* lrg-animation-clip.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-animation-clip.h"
#include <math.h>

/**
 * SECTION:lrg-animation-clip
 * @Title: LrgAnimationClip
 * @Short_description: Animation clip with keyframe tracks
 *
 * #LrgAnimationClip contains the animation data for one or more
 * bones. Each clip has multiple tracks, one per animated bone,
 * and each track contains keyframes with transform data.
 *
 * Clips also support animation events that fire at specific times.
 */

/*
 * Animation track structure (internal)
 */
typedef struct
{
    gchar      *bone_name;
    GArray     *keyframes;  /* Array of LrgAnimationKeyframe */
} LrgAnimationTrack;

typedef struct
{
    gchar               *name;
    gfloat               duration;
    LrgAnimationLoopMode loop_mode;
    GPtrArray           *tracks;     /* Array of LrgAnimationTrack* */
    GArray              *events;     /* Array of LrgAnimationEvent */
} LrgAnimationClipPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgAnimationClip, lrg_animation_clip, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_NAME,
    PROP_DURATION,
    PROP_LOOP_MODE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
animation_track_free (LrgAnimationTrack *track)
{
    guint i;

    if (track == NULL)
        return;

    g_free (track->bone_name);

    if (track->keyframes != NULL)
    {
        for (i = 0; i < track->keyframes->len; i++)
        {
            LrgAnimationKeyframe *kf;

            kf = &g_array_index (track->keyframes, LrgAnimationKeyframe, i);
            /* Keyframes are stored by value, no need to free individually */
            (void)kf;
        }
        g_array_free (track->keyframes, TRUE);
    }

    g_slice_free (LrgAnimationTrack, track);
}

static void
lrg_animation_clip_real_sample (LrgAnimationClip *self,
                                gfloat            time,
                                GPtrArray        *out_poses)
{
    LrgAnimationClipPrivate *priv;
    guint i;

    priv = lrg_animation_clip_get_instance_private (self);

    for (i = 0; i < priv->tracks->len && i < out_poses->len; i++)
    {
        LrgBonePose *pose;

        pose = g_ptr_array_index (out_poses, i);
        lrg_animation_clip_sample_track (self, i, time, pose);
    }
}

static void
lrg_animation_clip_finalize (GObject *object)
{
    LrgAnimationClip *self = LRG_ANIMATION_CLIP (object);
    LrgAnimationClipPrivate *priv = lrg_animation_clip_get_instance_private (self);
    guint i;

    g_free (priv->name);

    if (priv->tracks != NULL)
    {
        for (i = 0; i < priv->tracks->len; i++)
        {
            LrgAnimationTrack *track;

            track = g_ptr_array_index (priv->tracks, i);
            animation_track_free (track);
        }
        g_ptr_array_free (priv->tracks, TRUE);
    }

    if (priv->events != NULL)
    {
        for (i = 0; i < priv->events->len; i++)
        {
            LrgAnimationEvent *event;

            event = &g_array_index (priv->events, LrgAnimationEvent, i);
            g_free (event->name);
            g_clear_pointer (&event->data, g_variant_unref);
        }
        g_array_free (priv->events, TRUE);
    }

    G_OBJECT_CLASS (lrg_animation_clip_parent_class)->finalize (object);
}

static void
lrg_animation_clip_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgAnimationClip *self = LRG_ANIMATION_CLIP (object);
    LrgAnimationClipPrivate *priv = lrg_animation_clip_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_DURATION:
        g_value_set_float (value, priv->duration);
        break;
    case PROP_LOOP_MODE:
        g_value_set_enum (value, priv->loop_mode);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_animation_clip_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgAnimationClip *self = LRG_ANIMATION_CLIP (object);
    LrgAnimationClipPrivate *priv = lrg_animation_clip_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_free (priv->name);
        priv->name = g_value_dup_string (value);
        break;
    case PROP_DURATION:
        priv->duration = g_value_get_float (value);
        break;
    case PROP_LOOP_MODE:
        priv->loop_mode = g_value_get_enum (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_animation_clip_class_init (LrgAnimationClipClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_animation_clip_finalize;
    object_class->get_property = lrg_animation_clip_get_property;
    object_class->set_property = lrg_animation_clip_set_property;

    klass->sample = lrg_animation_clip_real_sample;

    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Animation clip name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_DURATION] =
        g_param_spec_float ("duration",
                            "Duration",
                            "Clip duration in seconds",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_LOOP_MODE] =
        g_param_spec_enum ("loop-mode",
                           "Loop Mode",
                           "Animation loop mode",
                           LRG_TYPE_ANIMATION_LOOP_MODE,
                           LRG_ANIMATION_LOOP_NONE,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_animation_clip_init (LrgAnimationClip *self)
{
    LrgAnimationClipPrivate *priv = lrg_animation_clip_get_instance_private (self);

    priv->name = NULL;
    priv->duration = 0.0f;
    priv->loop_mode = LRG_ANIMATION_LOOP_NONE;
    priv->tracks = g_ptr_array_new ();
    priv->events = g_array_new (FALSE, TRUE, sizeof (LrgAnimationEvent));
}

LrgAnimationClip *
lrg_animation_clip_new (const gchar *name)
{
    return g_object_new (LRG_TYPE_ANIMATION_CLIP,
                         "name", name,
                         NULL);
}

const gchar *
lrg_animation_clip_get_name (LrgAnimationClip *self)
{
    LrgAnimationClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), NULL);

    priv = lrg_animation_clip_get_instance_private (self);

    return priv->name;
}

gfloat
lrg_animation_clip_get_duration (LrgAnimationClip *self)
{
    LrgAnimationClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), 0.0f);

    priv = lrg_animation_clip_get_instance_private (self);

    return priv->duration;
}

void
lrg_animation_clip_set_duration (LrgAnimationClip *self,
                                 gfloat            duration)
{
    LrgAnimationClipPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATION_CLIP (self));

    priv = lrg_animation_clip_get_instance_private (self);

    if (priv->duration == duration)
        return;

    priv->duration = duration;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DURATION]);
}

LrgAnimationLoopMode
lrg_animation_clip_get_loop_mode (LrgAnimationClip *self)
{
    LrgAnimationClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), LRG_ANIMATION_LOOP_NONE);

    priv = lrg_animation_clip_get_instance_private (self);

    return priv->loop_mode;
}

void
lrg_animation_clip_set_loop_mode (LrgAnimationClip    *self,
                                  LrgAnimationLoopMode mode)
{
    LrgAnimationClipPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATION_CLIP (self));

    priv = lrg_animation_clip_get_instance_private (self);

    if (priv->loop_mode == mode)
        return;

    priv->loop_mode = mode;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOP_MODE]);
}

guint
lrg_animation_clip_add_track (LrgAnimationClip *self,
                              const gchar      *bone_name)
{
    LrgAnimationClipPrivate *priv;
    LrgAnimationTrack *track;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), 0);
    g_return_val_if_fail (bone_name != NULL, 0);

    priv = lrg_animation_clip_get_instance_private (self);

    track = g_slice_new0 (LrgAnimationTrack);
    track->bone_name = g_strdup (bone_name);
    track->keyframes = g_array_new (FALSE, TRUE, sizeof (LrgAnimationKeyframe));

    g_ptr_array_add (priv->tracks, track);

    return priv->tracks->len - 1;
}

guint
lrg_animation_clip_get_track_count (LrgAnimationClip *self)
{
    LrgAnimationClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), 0);

    priv = lrg_animation_clip_get_instance_private (self);

    return priv->tracks->len;
}

const gchar *
lrg_animation_clip_get_track_bone_name (LrgAnimationClip *self,
                                        guint             track_index)
{
    LrgAnimationClipPrivate *priv;
    LrgAnimationTrack *track;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), NULL);

    priv = lrg_animation_clip_get_instance_private (self);

    if (track_index >= priv->tracks->len)
        return NULL;

    track = g_ptr_array_index (priv->tracks, track_index);

    return track->bone_name;
}

void
lrg_animation_clip_add_keyframe (LrgAnimationClip           *self,
                                 guint                       track_index,
                                 const LrgAnimationKeyframe *keyframe)
{
    LrgAnimationClipPrivate *priv;
    LrgAnimationTrack *track;
    LrgAnimationKeyframe kf_copy;

    g_return_if_fail (LRG_IS_ANIMATION_CLIP (self));
    g_return_if_fail (keyframe != NULL);

    priv = lrg_animation_clip_get_instance_private (self);

    if (track_index >= priv->tracks->len)
        return;

    track = g_ptr_array_index (priv->tracks, track_index);

    kf_copy = *keyframe;
    g_array_append_val (track->keyframes, kf_copy);

    /* Update duration if needed */
    if (keyframe->time > priv->duration)
        lrg_animation_clip_set_duration (self, keyframe->time);
}

guint
lrg_animation_clip_get_keyframe_count (LrgAnimationClip *self,
                                       guint             track_index)
{
    LrgAnimationClipPrivate *priv;
    LrgAnimationTrack *track;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), 0);

    priv = lrg_animation_clip_get_instance_private (self);

    if (track_index >= priv->tracks->len)
        return 0;

    track = g_ptr_array_index (priv->tracks, track_index);

    return track->keyframes->len;
}

const LrgAnimationKeyframe *
lrg_animation_clip_get_keyframe (LrgAnimationClip *self,
                                 guint             track_index,
                                 guint             keyframe_index)
{
    LrgAnimationClipPrivate *priv;
    LrgAnimationTrack *track;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), NULL);

    priv = lrg_animation_clip_get_instance_private (self);

    if (track_index >= priv->tracks->len)
        return NULL;

    track = g_ptr_array_index (priv->tracks, track_index);

    if (keyframe_index >= track->keyframes->len)
        return NULL;

    return &g_array_index (track->keyframes, LrgAnimationKeyframe, keyframe_index);
}

void
lrg_animation_clip_add_event (LrgAnimationClip        *self,
                              const LrgAnimationEvent *event)
{
    LrgAnimationClipPrivate *priv;
    LrgAnimationEvent event_copy;

    g_return_if_fail (LRG_IS_ANIMATION_CLIP (self));
    g_return_if_fail (event != NULL);

    priv = lrg_animation_clip_get_instance_private (self);

    event_copy.time = event->time;
    event_copy.name = g_strdup (event->name);

    if (event->data != NULL)
        event_copy.data = g_variant_ref (event->data);
    else
        event_copy.data = NULL;

    g_array_append_val (priv->events, event_copy);
}

guint
lrg_animation_clip_get_event_count (LrgAnimationClip *self)
{
    LrgAnimationClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), 0);

    priv = lrg_animation_clip_get_instance_private (self);

    return priv->events->len;
}

const LrgAnimationEvent *
lrg_animation_clip_get_event (LrgAnimationClip *self,
                              guint             index)
{
    LrgAnimationClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), NULL);

    priv = lrg_animation_clip_get_instance_private (self);

    if (index >= priv->events->len)
        return NULL;

    return &g_array_index (priv->events, LrgAnimationEvent, index);
}

GList *
lrg_animation_clip_get_events_in_range (LrgAnimationClip *self,
                                        gfloat            start_time,
                                        gfloat            end_time)
{
    LrgAnimationClipPrivate *priv;
    GList *result;
    guint i;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), NULL);

    priv = lrg_animation_clip_get_instance_private (self);
    result = NULL;

    for (i = 0; i < priv->events->len; i++)
    {
        LrgAnimationEvent *event;

        event = &g_array_index (priv->events, LrgAnimationEvent, i);

        if (event->time >= start_time && event->time < end_time)
            result = g_list_append (result, event);
    }

    return result;
}

void
lrg_animation_clip_sample (LrgAnimationClip *self,
                           gfloat            time,
                           GPtrArray        *out_poses)
{
    LrgAnimationClipClass *klass;

    g_return_if_fail (LRG_IS_ANIMATION_CLIP (self));
    g_return_if_fail (out_poses != NULL);

    klass = LRG_ANIMATION_CLIP_GET_CLASS (self);

    if (klass->sample != NULL)
        klass->sample (self, time, out_poses);
}

void
lrg_animation_clip_sample_track (LrgAnimationClip *self,
                                 guint             track_index,
                                 gfloat            time,
                                 LrgBonePose      *out_pose)
{
    LrgAnimationClipPrivate *priv;
    LrgAnimationTrack *track;
    LrgAnimationKeyframe *prev_kf;
    LrgAnimationKeyframe *next_kf;
    gfloat local_time;
    gfloat t;
    guint i;

    g_return_if_fail (LRG_IS_ANIMATION_CLIP (self));
    g_return_if_fail (out_pose != NULL);

    priv = lrg_animation_clip_get_instance_private (self);

    if (track_index >= priv->tracks->len)
    {
        lrg_bone_pose_set_identity (out_pose);
        return;
    }

    track = g_ptr_array_index (priv->tracks, track_index);

    if (track->keyframes->len == 0)
    {
        lrg_bone_pose_set_identity (out_pose);
        return;
    }

    /* Handle looping */
    local_time = time;
    if (priv->duration > 0.0f)
    {
        switch (priv->loop_mode)
        {
        case LRG_ANIMATION_LOOP_NONE:
            local_time = CLAMP (time, 0.0f, priv->duration);
            break;
        case LRG_ANIMATION_LOOP_REPEAT:
            local_time = fmodf (time, priv->duration);
            if (local_time < 0.0f)
                local_time += priv->duration;
            break;
        case LRG_ANIMATION_LOOP_PINGPONG:
            local_time = fmodf (time, priv->duration * 2.0f);
            if (local_time < 0.0f)
                local_time += priv->duration * 2.0f;
            if (local_time > priv->duration)
                local_time = priv->duration * 2.0f - local_time;
            break;
        case LRG_ANIMATION_LOOP_CLAMP_FOREVER:
            local_time = CLAMP (time, 0.0f, priv->duration);
            break;
        }
    }

    /* Single keyframe case */
    if (track->keyframes->len == 1)
    {
        LrgAnimationKeyframe *kf;

        kf = &g_array_index (track->keyframes, LrgAnimationKeyframe, 0);
        *out_pose = kf->pose;
        return;
    }

    /* Find surrounding keyframes */
    prev_kf = NULL;
    next_kf = NULL;

    for (i = 0; i < track->keyframes->len; i++)
    {
        LrgAnimationKeyframe *kf;

        kf = &g_array_index (track->keyframes, LrgAnimationKeyframe, i);

        if (kf->time <= local_time)
        {
            prev_kf = kf;
        }
        else
        {
            next_kf = kf;
            break;
        }
    }

    /* Before first keyframe */
    if (prev_kf == NULL)
    {
        LrgAnimationKeyframe *kf;

        kf = &g_array_index (track->keyframes, LrgAnimationKeyframe, 0);
        *out_pose = kf->pose;
        return;
    }

    /* After last keyframe */
    if (next_kf == NULL)
    {
        *out_pose = prev_kf->pose;
        return;
    }

    /* Interpolate between keyframes */
    if (next_kf->time - prev_kf->time > 0.0001f)
        t = (local_time - prev_kf->time) / (next_kf->time - prev_kf->time);
    else
        t = 0.0f;

    /* Use cubic interpolation if tangents are non-zero */
    lrg_animation_keyframe_cubic (prev_kf, next_kf, t, out_pose);
}

void
lrg_animation_clip_calculate_smooth_tangents (LrgAnimationClip *self)
{
    LrgAnimationClipPrivate *priv;
    guint i;
    guint j;

    g_return_if_fail (LRG_IS_ANIMATION_CLIP (self));

    priv = lrg_animation_clip_get_instance_private (self);

    for (i = 0; i < priv->tracks->len; i++)
    {
        LrgAnimationTrack *track;

        track = g_ptr_array_index (priv->tracks, i);

        for (j = 0; j < track->keyframes->len; j++)
        {
            LrgAnimationKeyframe *kf;
            LrgAnimationKeyframe *prev_kf;
            LrgAnimationKeyframe *next_kf;

            kf = &g_array_index (track->keyframes, LrgAnimationKeyframe, j);
            prev_kf = (j > 0) ? &g_array_index (track->keyframes, LrgAnimationKeyframe, j - 1) : NULL;
            next_kf = (j + 1 < track->keyframes->len) ? &g_array_index (track->keyframes, LrgAnimationKeyframe, j + 1) : NULL;

            lrg_animation_keyframe_set_smooth_tangents (kf, prev_kf, next_kf);
        }
    }
}
