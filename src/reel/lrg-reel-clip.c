/* lrg-reel-clip.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-clip.h"
#include "lrg-reel-context.h"
#include "lrg-reel-context-private.h"
#include "lrg-reel-effect.h"
#include "../graphics/lrg-image-canvas.h"
#include <graylib.h>

typedef struct
{
    gint     from_frame;
    gint     duration_in_frames;
    gdouble  opacity;
    gboolean visible;
    gchar   *name;

    /* Transform + blend (composited when non-default). */
    gdouble          x;
    gdouble          y;
    gdouble          scale_x;
    gdouble          scale_y;
    gdouble          rotation;
    gdouble          anchor_x;
    gdouble          anchor_y;
    LrgReelBlendMode blend_mode;

    GPtrArray *effects;  /* of LrgReelEffect*, lazily created */

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
    PROP_X,
    PROP_Y,
    PROP_SCALE_X,
    PROP_SCALE_Y,
    PROP_ROTATION,
    PROP_ANCHOR_X,
    PROP_ANCHOR_Y,
    PROP_BLEND_MODE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Map the reel blend enum onto graylib's layer blend enum (same order). */
static GrlLayerBlendMode
reel_blend_to_grl (LrgReelBlendMode mode)
{
    switch (mode)
    {
    case LRG_REEL_BLEND_MULTIPLY:    return GRL_LAYER_BLEND_MULTIPLY;
    case LRG_REEL_BLEND_SCREEN:      return GRL_LAYER_BLEND_SCREEN;
    case LRG_REEL_BLEND_OVERLAY:     return GRL_LAYER_BLEND_OVERLAY;
    case LRG_REEL_BLEND_SOFT_LIGHT:  return GRL_LAYER_BLEND_SOFT_LIGHT;
    case LRG_REEL_BLEND_ADD:         return GRL_LAYER_BLEND_ADD;
    case LRG_REEL_BLEND_COLOR_DODGE: return GRL_LAYER_BLEND_COLOR_DODGE;
    case LRG_REEL_BLEND_COLOR_BURN:  return GRL_LAYER_BLEND_COLOR_BURN;
    case LRG_REEL_BLEND_NORMAL:
    default:                         return GRL_LAYER_BLEND_NORMAL;
    }
}

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

    g_clear_pointer (&priv->effects, g_ptr_array_unref);
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
    case PROP_X:
        g_value_set_double (value, lrg_reel_clip_get_x (self));
        break;
    case PROP_Y:
        g_value_set_double (value, lrg_reel_clip_get_y (self));
        break;
    case PROP_SCALE_X:
        g_value_set_double (value, lrg_reel_clip_get_scale_x (self));
        break;
    case PROP_SCALE_Y:
        g_value_set_double (value, lrg_reel_clip_get_scale_y (self));
        break;
    case PROP_ROTATION:
        g_value_set_double (value, lrg_reel_clip_get_rotation (self));
        break;
    case PROP_ANCHOR_X:
        g_value_set_double (value, lrg_reel_clip_get_anchor_x (self));
        break;
    case PROP_ANCHOR_Y:
        g_value_set_double (value, lrg_reel_clip_get_anchor_y (self));
        break;
    case PROP_BLEND_MODE:
        g_value_set_enum (value, lrg_reel_clip_get_blend_mode (self));
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
    case PROP_X:
        lrg_reel_clip_set_x (self, g_value_get_double (value));
        break;
    case PROP_Y:
        lrg_reel_clip_set_y (self, g_value_get_double (value));
        break;
    case PROP_SCALE_X:
        lrg_reel_clip_set_scale_x (self, g_value_get_double (value));
        break;
    case PROP_SCALE_Y:
        lrg_reel_clip_set_scale_y (self, g_value_get_double (value));
        break;
    case PROP_ROTATION:
        lrg_reel_clip_set_rotation (self, g_value_get_double (value));
        break;
    case PROP_ANCHOR_X:
        lrg_reel_clip_set_anchor (self, g_value_get_double (value),
                                  lrg_reel_clip_get_anchor_y (self));
        break;
    case PROP_ANCHOR_Y:
        lrg_reel_clip_set_anchor (self, lrg_reel_clip_get_anchor_x (self),
                                  g_value_get_double (value));
        break;
    case PROP_BLEND_MODE:
        lrg_reel_clip_set_blend_mode (self, g_value_get_enum (value));
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

    /**
     * LrgReelClip:x: (attributes org.gtk.Property.get=lrg_reel_clip_get_x)
     *
     * Translation in pixels along X.
     */
    properties[PROP_X] =
        g_param_spec_double ("x", "X", "Translation X in pixels",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);
    properties[PROP_Y] =
        g_param_spec_double ("y", "Y", "Translation Y in pixels",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);
    properties[PROP_SCALE_X] =
        g_param_spec_double ("scale-x", "Scale X", "Horizontal scale",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 1.0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);
    properties[PROP_SCALE_Y] =
        g_param_spec_double ("scale-y", "Scale Y", "Vertical scale",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 1.0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);
    properties[PROP_ROTATION] =
        g_param_spec_double ("rotation", "Rotation", "Rotation in radians",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);
    properties[PROP_ANCHOR_X] =
        g_param_spec_double ("anchor-x", "Anchor X",
                             "Pivot X as a fraction of frame width",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.5,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);
    properties[PROP_ANCHOR_Y] =
        g_param_spec_double ("anchor-y", "Anchor Y",
                             "Pivot Y as a fraction of frame height",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.5,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);
    /**
     * LrgReelClip:blend-mode:
     *
     * Layer blend mode used when the clip is composited onto the frame.
     */
    properties[PROP_BLEND_MODE] =
        g_param_spec_enum ("blend-mode", "Blend Mode",
                           "Layer blend mode",
                           LRG_TYPE_REEL_BLEND_MODE, LRG_REEL_BLEND_NORMAL,
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
    priv->x = 0.0;
    priv->y = 0.0;
    priv->scale_x = 1.0;
    priv->scale_y = 1.0;
    priv->rotation = 0.0;
    priv->anchor_x = 0.5;
    priv->anchor_y = 0.5;
    priv->blend_mode = LRG_REEL_BLEND_NORMAL;
    priv->effects = NULL;
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

/* Whether the clip needs to be composited through an off-screen layer (because
 * its opacity, blend mode, or transform is non-default). */
static gboolean
reel_clip_needs_composite (LrgReelClipPrivate *priv)
{
    return priv->opacity < 0.999 ||
           priv->blend_mode != LRG_REEL_BLEND_NORMAL ||
           priv->x != 0.0 || priv->y != 0.0 ||
           priv->scale_x != 1.0 || priv->scale_y != 1.0 ||
           priv->rotation != 0.0 ||
           (priv->effects != NULL && priv->effects->len > 0);
}

void
lrg_reel_clip_render (LrgReelClip    *self,
                      LrgReelContext *ctx,
                      LrgImageCanvas *canvas)
{
    LrgReelClipPrivate *priv;
    LrgReelScratch     *scratch;
    gint                fw;
    gint                fh;
    gfloat              pivot_x;
    gfloat              pivot_y;

    g_return_if_fail (LRG_IS_REEL_CLIP (self));

    priv = lrg_reel_clip_get_instance_private (self);

    /* Fast path: identity transform, full opacity, normal blend -> draw straight
     * onto the canvas (byte-identical to the v1 behaviour). */
    if (!reel_clip_needs_composite (priv))
    {
        LRG_REEL_CLIP_GET_CLASS (self)->render (self, ctx, canvas);
        return;
    }

    scratch = lrg_reel_context_acquire_scratch (ctx);
    if (scratch == NULL)
    {
        /* No compositor available (e.g. called without a renderer) -> draw
         * straight; transform/opacity/blend are ignored in that degenerate case. */
        LRG_REEL_CLIP_GET_CLASS (self)->render (self, ctx, canvas);
        return;
    }

    fw = lrg_reel_context_get_width (ctx);
    fh = lrg_reel_context_get_height (ctx);
    pivot_x = (gfloat) (priv->anchor_x * fw);
    pivot_y = (gfloat) (priv->anchor_y * fh);

    /* Draw the clip into the scratch layer under its transform.  Order: position,
     * then scale/rotate about the anchor pivot. */
    lrg_image_canvas_save (scratch->canvas);
    lrg_image_canvas_translate (scratch->canvas, (gfloat) priv->x, (gfloat) priv->y);
    lrg_image_canvas_translate (scratch->canvas, pivot_x, pivot_y);
    lrg_image_canvas_rotate (scratch->canvas, (gfloat) priv->rotation);
    lrg_image_canvas_scale (scratch->canvas, (gfloat) priv->scale_x,
                            (gfloat) priv->scale_y);
    lrg_image_canvas_translate (scratch->canvas, -pivot_x, -pivot_y);

    LRG_REEL_CLIP_GET_CLASS (self)->render (self, ctx, scratch->canvas);

    lrg_image_canvas_restore (scratch->canvas);

    /* Run the effect chain on the clip's own pixels (the scratch layer). */
    if (priv->effects != NULL)
    {
        GrlImage *layer_image = lrg_image_canvas_get_image (scratch->canvas);
        guint     i;

        for (i = 0; i < priv->effects->len; i++)
            lrg_reel_effect_apply (g_ptr_array_index (priv->effects, i),
                                   layer_image, ctx);
    }

    /* Composite the scratch layer onto the target with blend + opacity. */
    grl_image_composite_layer (lrg_image_canvas_get_image (canvas),
                               scratch->layer, 0, 0,
                               reel_blend_to_grl (priv->blend_mode),
                               (gfloat) priv->opacity);

    lrg_reel_context_release_scratch (ctx, scratch);
}

/* ==========================================================================
 * Effect chain
 * ========================================================================== */

void
lrg_reel_clip_add_effect (LrgReelClip   *self,
                          LrgReelEffect *effect)
{
    LrgReelClipPrivate *priv;

    g_return_if_fail (LRG_IS_REEL_CLIP (self));
    g_return_if_fail (LRG_IS_REEL_EFFECT (effect));

    priv = lrg_reel_clip_get_instance_private (self);
    if (priv->effects == NULL)
        priv->effects = g_ptr_array_new_with_free_func (g_object_unref);

    g_ptr_array_add (priv->effects, g_object_ref (effect));
}

guint
lrg_reel_clip_get_n_effects (LrgReelClip *self)
{
    LrgReelClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_REEL_CLIP (self), 0);

    priv = lrg_reel_clip_get_instance_private (self);
    return priv->effects != NULL ? priv->effects->len : 0;
}

void
lrg_reel_clip_clear_effects (LrgReelClip *self)
{
    LrgReelClipPrivate *priv;

    g_return_if_fail (LRG_IS_REEL_CLIP (self));

    priv = lrg_reel_clip_get_instance_private (self);
    if (priv->effects != NULL)
        g_ptr_array_set_size (priv->effects, 0);
}

/* ==========================================================================
 * Transform + blend accessors
 * ========================================================================== */

void
lrg_reel_clip_set_transform (LrgReelClip *self,
                             gdouble      x,
                             gdouble      y,
                             gdouble      scale_x,
                             gdouble      scale_y,
                             gdouble      rotation)
{
    g_return_if_fail (LRG_IS_REEL_CLIP (self));

    lrg_reel_clip_set_x (self, x);
    lrg_reel_clip_set_y (self, y);
    lrg_reel_clip_set_scale_x (self, scale_x);
    lrg_reel_clip_set_scale_y (self, scale_y);
    lrg_reel_clip_set_rotation (self, rotation);
}

void
lrg_reel_clip_set_anchor (LrgReelClip *self,
                          gdouble      anchor_x,
                          gdouble      anchor_y)
{
    LrgReelClipPrivate *priv;

    g_return_if_fail (LRG_IS_REEL_CLIP (self));

    priv = lrg_reel_clip_get_instance_private (self);
    priv->anchor_x = anchor_x;
    priv->anchor_y = anchor_y;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANCHOR_X]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANCHOR_Y]);
}

#define REEL_CLIP_DOUBLE_ACCESSOR(field, Prop)                                \
    gdouble                                                                   \
    lrg_reel_clip_get_##field (LrgReelClip *self)                             \
    {                                                                         \
        LrgReelClipPrivate *priv;                                             \
        g_return_val_if_fail (LRG_IS_REEL_CLIP (self), 0.0);                  \
        priv = lrg_reel_clip_get_instance_private (self);                     \
        return priv->field;                                                   \
    }                                                                         \
    void                                                                      \
    lrg_reel_clip_set_##field (LrgReelClip *self, gdouble value)              \
    {                                                                         \
        LrgReelClipPrivate *priv;                                             \
        g_return_if_fail (LRG_IS_REEL_CLIP (self));                           \
        priv = lrg_reel_clip_get_instance_private (self);                     \
        if (priv->field == value)                                             \
            return;                                                           \
        priv->field = value;                                                  \
        g_object_notify_by_pspec (G_OBJECT (self), properties[Prop]);         \
    }

REEL_CLIP_DOUBLE_ACCESSOR (x, PROP_X)
REEL_CLIP_DOUBLE_ACCESSOR (y, PROP_Y)
REEL_CLIP_DOUBLE_ACCESSOR (scale_x, PROP_SCALE_X)
REEL_CLIP_DOUBLE_ACCESSOR (scale_y, PROP_SCALE_Y)
REEL_CLIP_DOUBLE_ACCESSOR (rotation, PROP_ROTATION)

#undef REEL_CLIP_DOUBLE_ACCESSOR

gdouble
lrg_reel_clip_get_anchor_x (LrgReelClip *self)
{
    LrgReelClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_REEL_CLIP (self), 0.0);

    priv = lrg_reel_clip_get_instance_private (self);
    return priv->anchor_x;
}

gdouble
lrg_reel_clip_get_anchor_y (LrgReelClip *self)
{
    LrgReelClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_REEL_CLIP (self), 0.0);

    priv = lrg_reel_clip_get_instance_private (self);
    return priv->anchor_y;
}

LrgReelBlendMode
lrg_reel_clip_get_blend_mode (LrgReelClip *self)
{
    LrgReelClipPrivate *priv;

    g_return_val_if_fail (LRG_IS_REEL_CLIP (self), LRG_REEL_BLEND_NORMAL);

    priv = lrg_reel_clip_get_instance_private (self);
    return priv->blend_mode;
}

void
lrg_reel_clip_set_blend_mode (LrgReelClip      *self,
                              LrgReelBlendMode  blend_mode)
{
    LrgReelClipPrivate *priv;

    g_return_if_fail (LRG_IS_REEL_CLIP (self));

    priv = lrg_reel_clip_get_instance_private (self);
    if (priv->blend_mode == blend_mode)
        return;

    priv->blend_mode = blend_mode;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLEND_MODE]);
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
