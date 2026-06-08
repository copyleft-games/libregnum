/* lrg-reel-clip.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-clip.h"
#include "lrg-reel-context.h"

typedef struct
{
    gint     from_frame;
    gint     duration_in_frames;
    gdouble  opacity;
    gboolean visible;
    gchar   *name;

    /* Functional render callback (base clips). */
    LrgReelRenderFunc func;
    gpointer          user_data;
    GDestroyNotify    destroy;
} LrgReelClipPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgReelClip, lrg_reel_clip, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_FROM_FRAME,
    PROP_DURATION_IN_FRAMES,
    PROP_OPACITY,
    PROP_VISIBLE,
    PROP_NAME,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_reel_clip_real_render (LrgReelClip    *self,
                           LrgReelContext *ctx,
                           LrgImageCanvas *canvas)
{
    LrgReelClipPrivate *priv = lrg_reel_clip_get_instance_private (self);

    /* Default behaviour: invoke the functional callback if present. */
    if (priv->func != NULL)
        priv->func (self, ctx, canvas, priv->user_data);
}

static void
lrg_reel_clip_finalize (GObject *object)
{
    LrgReelClip        *self = LRG_REEL_CLIP (object);
    LrgReelClipPrivate *priv = lrg_reel_clip_get_instance_private (self);

    if (priv->destroy != NULL && priv->user_data != NULL)
        priv->destroy (priv->user_data);

    g_clear_pointer (&priv->name, g_free);

    G_OBJECT_CLASS (lrg_reel_clip_parent_class)->finalize (object);
}

static void
lrg_reel_clip_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgReelClip *self = LRG_REEL_CLIP (object);

    switch (prop_id)
    {
    case PROP_FROM_FRAME:
        g_value_set_int (value, lrg_reel_clip_get_from_frame (self));
        break;
    case PROP_DURATION_IN_FRAMES:
        g_value_set_int (value, lrg_reel_clip_get_duration_in_frames (self));
        break;
    case PROP_OPACITY:
        g_value_set_double (value, lrg_reel_clip_get_opacity (self));
        break;
    case PROP_VISIBLE:
        g_value_set_boolean (value, lrg_reel_clip_get_visible (self));
        break;
    case PROP_NAME:
        g_value_set_string (value, lrg_reel_clip_get_name (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_clip_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgReelClip *self = LRG_REEL_CLIP (object);

    switch (prop_id)
    {
    case PROP_FROM_FRAME:
        lrg_reel_clip_set_from_frame (self, g_value_get_int (value));
        break;
    case PROP_DURATION_IN_FRAMES:
        lrg_reel_clip_set_duration_in_frames (self, g_value_get_int (value));
        break;
    case PROP_OPACITY:
        lrg_reel_clip_set_opacity (self, g_value_get_double (value));
        break;
    case PROP_VISIBLE:
        lrg_reel_clip_set_visible (self, g_value_get_boolean (value));
        break;
    case PROP_NAME:
        lrg_reel_clip_set_name (self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_clip_class_init (LrgReelClipClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_reel_clip_finalize;
    object_class->get_property = lrg_reel_clip_get_property;
    object_class->set_property = lrg_reel_clip_set_property;

    klass->render = lrg_reel_clip_real_render;

    /**
     * LrgReelClip:from-frame:
     *
     * Frame, relative to the clip's parent, at which the clip becomes active.
     */
    properties[PROP_FROM_FRAME] =
        g_param_spec_int ("from-frame", "From Frame",
                          "Parent-relative start frame",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelClip:duration-in-frames:
     *
     * Number of frames the clip is active.  %LRG_REEL_DURATION_INFINITE
     * (the default) means it lasts until the end of its parent.
     */
    properties[PROP_DURATION_IN_FRAMES] =
        g_param_spec_int ("duration-in-frames", "Duration In Frames",
                          "Active duration in frames (infinite by default)",
                          G_MININT, G_MAXINT, LRG_REEL_DURATION_INFINITE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelClip:opacity:
     *
     * Layer opacity in the range [0.0, 1.0].
     */
    properties[PROP_OPACITY] =
        g_param_spec_double ("opacity", "Opacity",
                             "Layer opacity",
                             0.0, 1.0, 1.0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelClip:visible:
     *
     * Whether the clip is drawn at all.
     */
    properties[PROP_VISIBLE] =
        g_param_spec_boolean ("visible", "Visible",
                              "Whether the clip is drawn",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelClip:name:
     *
     * Optional human-readable name for the clip.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name", "Name",
                             "Optional clip name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_clip_init (LrgReelClip *self)
{
    LrgReelClipPrivate *priv = lrg_reel_clip_get_instance_private (self);

    priv->from_frame = 0;
    priv->duration_in_frames = LRG_REEL_DURATION_INFINITE;
    priv->opacity = 1.0;
    priv->visible = TRUE;
    priv->name = NULL;
    priv->func = NULL;
    priv->user_data = NULL;
    priv->destroy = NULL;
}

LrgReelClip *
lrg_reel_clip_new_with_func (LrgReelRenderFunc func,
                             gpointer          user_data,
                             GDestroyNotify    destroy)
{
    LrgReelClip        *self;
    LrgReelClipPrivate *priv;

    g_return_val_if_fail (func != NULL, NULL);

    self = g_object_new (LRG_TYPE_REEL_CLIP, NULL);
    priv = lrg_reel_clip_get_instance_private (self);
    priv->func = func;
    priv->user_data = user_data;
    priv->destroy = destroy;

    return self;
}

void
lrg_reel_clip_render (LrgReelClip    *self,
                      LrgReelContext *ctx,
                      LrgImageCanvas *canvas)
{
    g_return_if_fail (LRG_IS_REEL_CLIP (self));

    LRG_REEL_CLIP_GET_CLASS (self)->render (self, ctx, canvas);
}

gboolean
lrg_reel_clip_is_active_at (LrgReelClip *self,
                            gint         frame)
{
    LrgReelClipPrivate *priv;
    gint relative;

    g_return_val_if_fail (LRG_IS_REEL_CLIP (self), FALSE);

    priv = lrg_reel_clip_get_instance_private (self);

    if (!priv->visible)
        return FALSE;

    relative = frame - priv->from_frame;
    if (relative < 0)
        return FALSE;

    /* Infinite or negative duration: active until the end. */
    if (priv->duration_in_frames < 0 ||
        priv->duration_in_frames >= LRG_REEL_DURATION_INFINITE)
        return TRUE;

    return relative < priv->duration_in_frames;
}

gint
lrg_reel_clip_get_from_frame (LrgReelClip *self)
{
    LrgReelClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_REEL_CLIP (self), 0);

    priv = lrg_reel_clip_get_instance_private (self);
    return priv->from_frame;
}

void
lrg_reel_clip_set_from_frame (LrgReelClip *self,
                              gint         from_frame)
{
    LrgReelClipPrivate *priv;

    g_return_if_fail (LRG_IS_REEL_CLIP (self));

    priv = lrg_reel_clip_get_instance_private (self);
    if (priv->from_frame == from_frame)
        return;

    priv->from_frame = from_frame;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FROM_FRAME]);
}

gint
lrg_reel_clip_get_duration_in_frames (LrgReelClip *self)
{
    LrgReelClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_REEL_CLIP (self), 0);

    priv = lrg_reel_clip_get_instance_private (self);
    return priv->duration_in_frames;
}

void
lrg_reel_clip_set_duration_in_frames (LrgReelClip *self,
                                      gint         duration_in_frames)
{
    LrgReelClipPrivate *priv;

    g_return_if_fail (LRG_IS_REEL_CLIP (self));

    priv = lrg_reel_clip_get_instance_private (self);
    if (priv->duration_in_frames == duration_in_frames)
        return;

    priv->duration_in_frames = duration_in_frames;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DURATION_IN_FRAMES]);
}

gdouble
lrg_reel_clip_get_opacity (LrgReelClip *self)
{
    LrgReelClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_REEL_CLIP (self), 1.0);

    priv = lrg_reel_clip_get_instance_private (self);
    return priv->opacity;
}

void
lrg_reel_clip_set_opacity (LrgReelClip *self,
                           gdouble      opacity)
{
    LrgReelClipPrivate *priv;

    g_return_if_fail (LRG_IS_REEL_CLIP (self));

    priv = lrg_reel_clip_get_instance_private (self);
    opacity = CLAMP (opacity, 0.0, 1.0);
    if (priv->opacity == opacity)
        return;

    priv->opacity = opacity;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OPACITY]);
}

gboolean
lrg_reel_clip_get_visible (LrgReelClip *self)
{
    LrgReelClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_REEL_CLIP (self), FALSE);

    priv = lrg_reel_clip_get_instance_private (self);
    return priv->visible;
}

void
lrg_reel_clip_set_visible (LrgReelClip *self,
                           gboolean     visible)
{
    LrgReelClipPrivate *priv;

    g_return_if_fail (LRG_IS_REEL_CLIP (self));

    priv = lrg_reel_clip_get_instance_private (self);
    visible = !!visible;
    if (priv->visible == visible)
        return;

    priv->visible = visible;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
}

const gchar *
lrg_reel_clip_get_name (LrgReelClip *self)
{
    LrgReelClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_REEL_CLIP (self), NULL);

    priv = lrg_reel_clip_get_instance_private (self);
    return priv->name;
}

void
lrg_reel_clip_set_name (LrgReelClip *self,
                        const gchar *name)
{
    LrgReelClipPrivate *priv;

    g_return_if_fail (LRG_IS_REEL_CLIP (self));

    priv = lrg_reel_clip_get_instance_private (self);
    if (g_strcmp0 (priv->name, name) == 0)
        return;

    g_free (priv->name);
    priv->name = g_strdup (name);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}
