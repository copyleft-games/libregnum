/* lrg-animation-clip.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-animation-clip.h"
#include <math.h>
#include <string.h>
#include <gio/gio.h>

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
 * Channel (internal): keys for one pose component (or one morph weight
 * vector) with their own times and interpolation, laid out as glTF
 * stores them. comps is the width of one value (3 translation/scale,
 * 4 rotation, n weights); cubic spline keys hold in-tangent, value,
 * out-tangent, so values has 3 * comps floats per key.
 */
typedef struct
{
    LrgAnimationInterpolation interpolation;
    guint                     n_keys;
    guint                     comps;
    gboolean                  is_rotation;
    gfloat                   *times;
    gfloat                   *values;
    gchar                    *target;   /* weight channels: morph group */
} LrgAnimationChannel;

/*
 * Animation track structure (internal)
 */
typedef struct
{
    gchar      *bone_name;
    GArray     *keyframes;  /* Array of LrgAnimationKeyframe */
    gboolean    sorted;     /* keyframe times never decrease (binary search ok) */

    /* channel tracks: indexed by LrgAnimationChannelPath, nullable */
    LrgAnimationChannel *channels[3];
    LrgBonePose          base_pose;  /* components without a channel */
} LrgAnimationTrack;

typedef struct
{
    gchar               *name;
    gfloat               duration;
    LrgAnimationLoopMode loop_mode;
    GPtrArray           *tracks;     /* Array of LrgAnimationTrack* */
    GArray              *events;     /* Array of LrgAnimationEvent */
    GPtrArray           *weights;    /* LrgAnimationChannel*, morph weights */
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

/* ------------------------------------------------------------------------ */
/* Enum types                                                               */
/* ------------------------------------------------------------------------ */

GType
lrg_animation_interpolation_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        static const GEnumValue values[] = {
            { LRG_ANIMATION_INTERPOLATION_STEP, "LRG_ANIMATION_INTERPOLATION_STEP", "step" },
            { LRG_ANIMATION_INTERPOLATION_LINEAR, "LRG_ANIMATION_INTERPOLATION_LINEAR", "linear" },
            { LRG_ANIMATION_INTERPOLATION_CUBIC_SPLINE, "LRG_ANIMATION_INTERPOLATION_CUBIC_SPLINE", "cubic-spline" },
            { 0, NULL, NULL }
        };
        GType id = g_enum_register_static (g_intern_static_string ("LrgAnimationInterpolation"), values);

        g_once_init_leave (&type_id, id);
    }
    return (GType)type_id;
}

GType
lrg_animation_channel_path_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        static const GEnumValue values[] = {
            { LRG_ANIMATION_CHANNEL_TRANSLATION, "LRG_ANIMATION_CHANNEL_TRANSLATION", "translation" },
            { LRG_ANIMATION_CHANNEL_ROTATION, "LRG_ANIMATION_CHANNEL_ROTATION", "rotation" },
            { LRG_ANIMATION_CHANNEL_SCALE, "LRG_ANIMATION_CHANNEL_SCALE", "scale" },
            { 0, NULL, NULL }
        };
        GType id = g_enum_register_static (g_intern_static_string ("LrgAnimationChannelPath"), values);

        g_once_init_leave (&type_id, id);
    }
    return (GType)type_id;
}

/* ------------------------------------------------------------------------ */
/* Channels                                                                 */
/* ------------------------------------------------------------------------ */

static void
animation_channel_free (LrgAnimationChannel *channel)
{
    if (channel == NULL)
        return;
    g_free (channel->times);
    g_free (channel->values);
    g_free (channel->target);
    g_free (channel);
}

/*
 * animation_channel_new:
 * Validates and copies the keys of a channel. Times must be finite and
 * not decreasing; values are copied verbatim (comps per key, three
 * times as many for cubic spline).
 */
static LrgAnimationChannel *
animation_channel_new (LrgAnimationInterpolation   interpolation,
                       guint                       comps,
                       guint                       n_keys,
                       const gfloat               *times,
                       const gfloat               *values,
                       GError                    **error)
{
    LrgAnimationChannel *channel;
    guint                per_key;
    guint                k;

    if (n_keys == 0 || times == NULL || values == NULL || comps == 0 ||
        (guint)interpolation > (guint)LRG_ANIMATION_INTERPOLATION_CUBIC_SPLINE)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "An animation channel needs at least one key, times and values");
        return NULL;
    }
    for (k = 0; k < n_keys; k++)
    {
        if (!isfinite (times[k]) || (k > 0 && times[k] < times[k - 1]))
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                         "Animation channel key %u has a bad or decreasing time", k);
            return NULL;
        }
    }

    per_key = interpolation == LRG_ANIMATION_INTERPOLATION_CUBIC_SPLINE ? 3 * comps : comps;
    channel = g_new0 (LrgAnimationChannel, 1);
    channel->interpolation = interpolation;
    channel->n_keys = n_keys;
    channel->comps = comps;
    channel->times = g_memdup2 (times, n_keys * sizeof (gfloat));
    channel->values = g_memdup2 (values, (gsize)n_keys * per_key * sizeof (gfloat));
    return channel;
}

/* channel_value: the value (not a tangent) of key k */
static const gfloat *
channel_value (const LrgAnimationChannel *channel,
               guint                      k)
{
    if (channel->interpolation == LRG_ANIMATION_INTERPOLATION_CUBIC_SPLINE)
        return channel->values + ((gsize)k * 3 + 1) * channel->comps;
    return channel->values + (gsize)k * channel->comps;
}

/* normalise a quaternion in place (left alone when degenerate) */
static void
quat_normalize4 (gfloat *q)
{
    gfloat len = sqrtf (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);

    if (len > 1e-12f)
    {
        q[0] /= len;
        q[1] /= len;
        q[2] /= len;
        q[3] /= len;
    }
}

/* shortest-arc spherical interpolation of unit quaternions a -> b */
static void
quat_slerp4 (const gfloat *a,
             const gfloat *b,
             gfloat        t,
             gfloat       *out)
{
    gfloat bb[4];
    gfloat d;
    gfloat wa;
    gfloat wb;
    guint  i;

    d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    for (i = 0; i < 4; i++)
        bb[i] = d < 0.0f ? -b[i] : b[i];
    d = fabsf (d);
    if (d > 0.9995f)
    {
        /* nearly parallel: normalised lerp avoids dividing by sin ~ 0 */
        wa = 1.0f - t;
        wb = t;
    }
    else
    {
        gfloat theta = acosf (d);
        gfloat sin_theta = sinf (theta);

        wa = sinf ((1.0f - t) * theta) / sin_theta;
        wb = sinf (t * theta) / sin_theta;
    }
    for (i = 0; i < 4; i++)
        out[i] = wa * a[i] + wb * bb[i];
    quat_normalize4 (out);
}

/*
 * animation_channel_sample:
 * Evaluates the channel at local time t into out (comps floats). Times
 * before the first key take the first value, after the last the last
 * value. Between keys k and k+1: STEP holds k, LINEAR lerps (slerps a
 * rotation), CUBIC_SPLINE evaluates the glTF Hermite form
 *   p = (2s^3 - 3s^2 + 1) v_k + d (s^3 - 2s^2 + s) b_k
 *     + (-2s^3 + 3s^2) v_k1 + d (s^3 - s^2) a_k1
 * with d the key interval, b_k the out-tangent of k and a_k1 the
 * in-tangent of k+1; a rotation result is renormalised.
 */
static void
animation_channel_sample (const LrgAnimationChannel *channel,
                          gfloat                     t,
                          gfloat                    *out)
{
    guint  lo;
    guint  hi;
    guint  k;
    guint  c;
    gfloat d;
    gfloat s;

    if (channel->n_keys == 1 || t <= channel->times[0])
    {
        memcpy (out, channel_value (channel, 0), channel->comps * sizeof (gfloat));
        return;
    }
    if (t >= channel->times[channel->n_keys - 1])
    {
        memcpy (out, channel_value (channel, channel->n_keys - 1), channel->comps * sizeof (gfloat));
        return;
    }

    /* upper bound: first key later than t; k is the one before it */
    lo = 0;
    hi = channel->n_keys;
    while (lo < hi)
    {
        guint mid = lo + (hi - lo) / 2;

        if (channel->times[mid] <= t)
            lo = mid + 1;
        else
            hi = mid;
    }
    k = lo - 1;
    d = channel->times[k + 1] - channel->times[k];
    s = d > 0.0f ? (t - channel->times[k]) / d : 0.0f;

    switch (channel->interpolation)
    {
    case LRG_ANIMATION_INTERPOLATION_STEP:
        memcpy (out, channel_value (channel, k), channel->comps * sizeof (gfloat));
        break;

    case LRG_ANIMATION_INTERPOLATION_LINEAR:
        if (channel->is_rotation)
            quat_slerp4 (channel_value (channel, k), channel_value (channel, k + 1), s, out);
        else
        {
            const gfloat *a = channel_value (channel, k);
            const gfloat *b = channel_value (channel, k + 1);

            for (c = 0; c < channel->comps; c++)
                out[c] = a[c] + (b[c] - a[c]) * s;
        }
        break;

    case LRG_ANIMATION_INTERPOLATION_CUBIC_SPLINE:
    default:
        {
            const gfloat *vk = channel_value (channel, k);
            const gfloat *bk = vk + channel->comps;                       /* out-tangent of k */
            const gfloat *vk1 = channel_value (channel, k + 1);
            const gfloat *ak1 = vk1 - channel->comps;                     /* in-tangent of k+1 */
            gfloat s2 = s * s;
            gfloat s3 = s2 * s;
            gfloat h00 = 2.0f * s3 - 3.0f * s2 + 1.0f;
            gfloat h10 = s3 - 2.0f * s2 + s;
            gfloat h01 = -2.0f * s3 + 3.0f * s2;
            gfloat h11 = s3 - s2;

            for (c = 0; c < channel->comps; c++)
                out[c] = h00 * vk[c] + d * h10 * bk[c] + h01 * vk1[c] + d * h11 * ak1[c];
            if (channel->is_rotation)
                quat_normalize4 (out);
        }
        break;
    }
}

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
    for (i = 0; i < 3; i++)
        animation_channel_free (track->channels[i]);

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
    g_clear_pointer (&priv->weights, g_ptr_array_unref);

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
    priv->weights = g_ptr_array_new_with_free_func ((GDestroyNotify)animation_channel_free);
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
    track->sorted = TRUE;
    lrg_bone_pose_set_identity (&track->base_pose);

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

    /* Keyframes are kept in insertion order; remember whether that is
     * also time order (NaN times count as out of order). */
    if (track->keyframes->len > 0)
    {
        const LrgAnimationKeyframe *last;

        last = &g_array_index (track->keyframes, LrgAnimationKeyframe,
                               track->keyframes->len - 1);
        if (!(kf_copy.time >= last->time))
            track->sorted = FALSE;
    }
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

/*
 * clip_local_time:
 * Maps a playback time into the clip by its loop mode (unchanged when
 * the clip has no duration).
 */
static gfloat
clip_local_time (LrgAnimationClipPrivate *priv,
                 gfloat                   time)
{
    gfloat local_time = time;

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

    return local_time;
}

/*
 * track_sample_channels:
 * A channel track's pose: each component from its channel when it has
 * one, else from the track's base pose.
 */
static void
track_sample_channels (LrgAnimationTrack *track,
                       gfloat             local_time,
                       LrgBonePose       *out_pose)
{
    gfloat v[4];

    *out_pose = track->base_pose;
    if (track->channels[LRG_ANIMATION_CHANNEL_TRANSLATION] != NULL)
    {
        animation_channel_sample (track->channels[LRG_ANIMATION_CHANNEL_TRANSLATION], local_time, v);
        out_pose->position_x = v[0];
        out_pose->position_y = v[1];
        out_pose->position_z = v[2];
    }
    if (track->channels[LRG_ANIMATION_CHANNEL_ROTATION] != NULL)
    {
        animation_channel_sample (track->channels[LRG_ANIMATION_CHANNEL_ROTATION], local_time, v);
        out_pose->rotation_x = v[0];
        out_pose->rotation_y = v[1];
        out_pose->rotation_z = v[2];
        out_pose->rotation_w = v[3];
    }
    if (track->channels[LRG_ANIMATION_CHANNEL_SCALE] != NULL)
    {
        animation_channel_sample (track->channels[LRG_ANIMATION_CHANNEL_SCALE], local_time, v);
        out_pose->scale_x = v[0];
        out_pose->scale_y = v[1];
        out_pose->scale_z = v[2];
    }
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

    if (track->keyframes->len == 0 && track->channels[0] == NULL &&
        track->channels[1] == NULL && track->channels[2] == NULL)
    {
        lrg_bone_pose_set_identity (out_pose);
        return;
    }

    local_time = clip_local_time (priv, time);

    /* channel tracks: each component from its own channel */
    if (track->channels[0] != NULL || track->channels[1] != NULL || track->channels[2] != NULL)
    {
        track_sample_channels (track, local_time, out_pose);
        return;
    }

    /* Single keyframe case */
    if (track->keyframes->len == 1)
    {
        LrgAnimationKeyframe *kf;

        kf = &g_array_index (track->keyframes, LrgAnimationKeyframe, 0);
        *out_pose = kf->pose;
        return;
    }

    /*
     * Find surrounding keyframes: next is the first keyframe later than
     * local_time, prev the last one before it that is not. On a time
     * sorted track that is the upper bound, found by bisection; an
     * unsorted track keeps the linear scan, whose answer differs there.
     */
    prev_kf = NULL;
    next_kf = NULL;

    if (track->sorted)
    {
        guint lo = 0;
        guint hi = track->keyframes->len;

        while (lo < hi)
        {
            guint mid = lo + (hi - lo) / 2;

            if (g_array_index (track->keyframes, LrgAnimationKeyframe, mid).time <= local_time)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo > 0)
            prev_kf = &g_array_index (track->keyframes, LrgAnimationKeyframe, lo - 1);
        if (lo < track->keyframes->len)
            next_kf = &g_array_index (track->keyframes, LrgAnimationKeyframe, lo);
    }
    else
    {
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

/* ------------------------------------------------------------------------ */
/* Channels API                                                             */
/* ------------------------------------------------------------------------ */

gint
lrg_animation_clip_find_track (LrgAnimationClip *self,
                               const gchar      *bone_name)
{
    LrgAnimationClipPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), -1);
    g_return_val_if_fail (bone_name != NULL, -1);

    priv = lrg_animation_clip_get_instance_private (self);
    for (i = 0; i < priv->tracks->len; i++)
    {
        LrgAnimationTrack *track = g_ptr_array_index (priv->tracks, i);

        if (g_strcmp0 (track->bone_name, bone_name) == 0)
            return (gint)i;
    }
    return -1;
}

/* grow the clip's duration to a channel's last key */
static void
clip_extend_duration (LrgAnimationClip          *self,
                      const LrgAnimationChannel *channel)
{
    gfloat last = channel->times[channel->n_keys - 1];

    if (last > lrg_animation_clip_get_duration (self))
        lrg_animation_clip_set_duration (self, last);
}

gboolean
lrg_animation_clip_set_track_channel (LrgAnimationClip         *self,
                                      guint                     track_index,
                                      LrgAnimationChannelPath   path,
                                      LrgAnimationInterpolation interpolation,
                                      guint                     n_keys,
                                      const gfloat             *times,
                                      const gfloat             *values,
                                      GError                  **error)
{
    LrgAnimationClipPrivate *priv;
    LrgAnimationTrack       *track;
    LrgAnimationChannel     *channel;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    priv = lrg_animation_clip_get_instance_private (self);
    if (track_index >= priv->tracks->len || (guint)path > (guint)LRG_ANIMATION_CHANNEL_SCALE)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "No animation track %u (or bad channel path)", track_index);
        return FALSE;
    }

    channel = animation_channel_new (interpolation,
                                     path == LRG_ANIMATION_CHANNEL_ROTATION ? 4 : 3,
                                     n_keys, times, values, error);
    if (channel == NULL)
        return FALSE;
    channel->is_rotation = path == LRG_ANIMATION_CHANNEL_ROTATION;

    track = g_ptr_array_index (priv->tracks, track_index);
    animation_channel_free (track->channels[path]);
    track->channels[path] = channel;
    clip_extend_duration (self, channel);
    return TRUE;
}

gboolean
lrg_animation_clip_get_track_channel (LrgAnimationClip           *self,
                                      guint                       track_index,
                                      LrgAnimationChannelPath     path,
                                      LrgAnimationInterpolation  *out_interpolation,
                                      guint                      *out_n_keys,
                                      const gfloat              **out_times,
                                      const gfloat              **out_values)
{
    LrgAnimationClipPrivate *priv;
    LrgAnimationTrack       *track;
    LrgAnimationChannel     *channel;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), FALSE);

    priv = lrg_animation_clip_get_instance_private (self);
    if (track_index >= priv->tracks->len || (guint)path > (guint)LRG_ANIMATION_CHANNEL_SCALE)
        return FALSE;
    track = g_ptr_array_index (priv->tracks, track_index);
    channel = track->channels[path];
    if (channel == NULL)
        return FALSE;
    if (out_interpolation != NULL)
        *out_interpolation = channel->interpolation;
    if (out_n_keys != NULL)
        *out_n_keys = channel->n_keys;
    if (out_times != NULL)
        *out_times = channel->times;
    if (out_values != NULL)
        *out_values = channel->values;
    return TRUE;
}

gboolean
lrg_animation_clip_track_has_channels (LrgAnimationClip *self,
                                       guint             track_index)
{
    LrgAnimationClipPrivate *priv;
    LrgAnimationTrack       *track;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), FALSE);

    priv = lrg_animation_clip_get_instance_private (self);
    if (track_index >= priv->tracks->len)
        return FALSE;
    track = g_ptr_array_index (priv->tracks, track_index);
    return track->channels[0] != NULL || track->channels[1] != NULL || track->channels[2] != NULL;
}

void
lrg_animation_clip_set_track_base_pose (LrgAnimationClip  *self,
                                        guint              track_index,
                                        const LrgBonePose *pose)
{
    LrgAnimationClipPrivate *priv;
    LrgAnimationTrack       *track;

    g_return_if_fail (LRG_IS_ANIMATION_CLIP (self));
    g_return_if_fail (pose != NULL);

    priv = lrg_animation_clip_get_instance_private (self);
    if (track_index >= priv->tracks->len)
        return;
    track = g_ptr_array_index (priv->tracks, track_index);
    track->base_pose = *pose;
}

gboolean
lrg_animation_clip_get_track_base_pose (LrgAnimationClip *self,
                                        guint             track_index,
                                        LrgBonePose      *out_pose)
{
    LrgAnimationClipPrivate *priv;
    LrgAnimationTrack       *track;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), FALSE);
    g_return_val_if_fail (out_pose != NULL, FALSE);

    priv = lrg_animation_clip_get_instance_private (self);
    if (track_index >= priv->tracks->len)
    {
        lrg_bone_pose_set_identity (out_pose);
        return FALSE;
    }
    track = g_ptr_array_index (priv->tracks, track_index);
    *out_pose = track->base_pose;
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Morph weight channels                                                    */
/* ------------------------------------------------------------------------ */

gint
lrg_animation_clip_add_weights_channel (LrgAnimationClip         *self,
                                        const gchar              *target_name,
                                        guint                     n_weights,
                                        LrgAnimationInterpolation interpolation,
                                        guint                     n_keys,
                                        const gfloat             *times,
                                        const gfloat             *values,
                                        GError                  **error)
{
    LrgAnimationClipPrivate *priv;
    LrgAnimationChannel     *channel;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), -1);
    g_return_val_if_fail (target_name != NULL, -1);
    g_return_val_if_fail (error == NULL || *error == NULL, -1);

    priv = lrg_animation_clip_get_instance_private (self);
    channel = animation_channel_new (interpolation, n_weights, n_keys, times, values, error);
    if (channel == NULL)
        return -1;
    channel->target = g_strdup (target_name);
    g_ptr_array_add (priv->weights, channel);
    clip_extend_duration (self, channel);
    return (gint)priv->weights->len - 1;
}

guint
lrg_animation_clip_get_weights_channel_count (LrgAnimationClip *self)
{
    LrgAnimationClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), 0);
    priv = lrg_animation_clip_get_instance_private (self);
    return priv->weights->len;
}

const gchar *
lrg_animation_clip_get_weights_channel_target (LrgAnimationClip *self,
                                               guint             channel)
{
    LrgAnimationClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), NULL);
    priv = lrg_animation_clip_get_instance_private (self);
    if (channel >= priv->weights->len)
        return NULL;
    return ((LrgAnimationChannel *)g_ptr_array_index (priv->weights, channel))->target;
}

guint
lrg_animation_clip_get_weights_channel_width (LrgAnimationClip *self,
                                              guint             channel)
{
    LrgAnimationClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), 0);
    priv = lrg_animation_clip_get_instance_private (self);
    if (channel >= priv->weights->len)
        return 0;
    return ((LrgAnimationChannel *)g_ptr_array_index (priv->weights, channel))->comps;
}

gboolean
lrg_animation_clip_sample_weights (LrgAnimationClip *self,
                                   guint             channel,
                                   gfloat            time,
                                   gfloat           *out_weights)
{
    LrgAnimationClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (self), FALSE);
    g_return_val_if_fail (out_weights != NULL, FALSE);

    priv = lrg_animation_clip_get_instance_private (self);
    if (channel >= priv->weights->len)
        return FALSE;
    animation_channel_sample (g_ptr_array_index (priv->weights, channel),
                              clip_local_time (priv, time), out_weights);
    return TRUE;
}
