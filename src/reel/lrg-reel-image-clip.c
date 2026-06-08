/* lrg-reel-image-clip.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-image-clip.h"
#include "../graphics/lrg-image-canvas.h"
#include "lrg-reel-context.h"
#include <graylib.h>

struct _LrgReelImageClip
{
    LrgReelClip  parent_instance;

    GrlImage    *image;
    LrgReelFit   fit;

    /* Tint: has_tint guards use of the color struct. */
    gboolean     has_tint;
    GrlColor     tint;

    /* Destination box; box_set == FALSE means "use full frame". */
    gboolean     box_set;
    gint         box_x;
    gint         box_y;
    gint         box_w;
    gint         box_h;
};

G_DEFINE_FINAL_TYPE (LrgReelImageClip, lrg_reel_image_clip, LRG_TYPE_REEL_CLIP)

enum
{
    PROP_0,
    PROP_IMAGE,
    PROP_FIT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * Fit helpers
 * -------------------------------------------------------------------------- */

/*
 * compute_contain_rect:
 *
 * Scale @src_w x @src_h to fit INSIDE @box_w x @box_h preserving aspect,
 * and centre the result.  Populates @out (image coords).
 */
static void
compute_contain_rect (gint          src_w,
                      gint          src_h,
                      gint          box_x,
                      gint          box_y,
                      gint          box_w,
                      gint          box_h,
                      GrlRectangle *out)
{
    gfloat scale_x;
    gfloat scale_y;
    gfloat scale;
    gfloat dw;
    gfloat dh;

    scale_x = (gfloat) box_w / (gfloat) src_w;
    scale_y = (gfloat) box_h / (gfloat) src_h;
    scale   = (scale_x < scale_y) ? scale_x : scale_y;

    dw = (gfloat) src_w * scale;
    dh = (gfloat) src_h * scale;

    out->x      = (gfloat) box_x + ((gfloat) box_w - dw) * 0.5f;
    out->y      = (gfloat) box_y + ((gfloat) box_h - dh) * 0.5f;
    out->width  = dw;
    out->height = dh;
}

/*
 * compute_cover_dst_rect:
 *
 * Scale @src_w x @src_h to COVER @box_w x @box_h preserving aspect, and
 * centre (overflows the box on one axis).  Populates @out (image coords).
 * The caller is responsible for setting a clip rect before drawing.
 */
static void
compute_cover_dst_rect (gint          src_w,
                        gint          src_h,
                        gint          box_x,
                        gint          box_y,
                        gint          box_w,
                        gint          box_h,
                        GrlRectangle *out)
{
    gfloat scale_x;
    gfloat scale_y;
    gfloat scale;
    gfloat dw;
    gfloat dh;

    scale_x = (gfloat) box_w / (gfloat) src_w;
    scale_y = (gfloat) box_h / (gfloat) src_h;
    scale   = (scale_x > scale_y) ? scale_x : scale_y;

    dw = (gfloat) src_w * scale;
    dh = (gfloat) src_h * scale;

    out->x      = (gfloat) box_x + ((gfloat) box_w - dw) * 0.5f;
    out->y      = (gfloat) box_y + ((gfloat) box_h - dh) * 0.5f;
    out->width  = dw;
    out->height = dh;
}

/* --------------------------------------------------------------------------
 * Render vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_image_clip_render (LrgReelClip    *base,
                             LrgReelContext *ctx,
                             LrgImageCanvas *canvas)
{
    LrgReelImageClip *self;
    GrlImage         *dst_img;
    GrlRectangle      dst_rect;
    GrlRectangle      clip_rect;
    const GrlColor   *tint_ptr;
    gint              fw;
    gint              fh;
    gint              src_w;
    gint              src_h;
    gint              box_x;
    gint              box_y;
    gint              box_w;
    gint              box_h;
    gboolean          needs_clip;

    self = LRG_REEL_IMAGE_CLIP (base);

    if (self->image == NULL)
        return;

    dst_img = lrg_image_canvas_get_image (canvas);

    fw = lrg_reel_context_get_width (ctx);
    fh = lrg_reel_context_get_height (ctx);

    /* Resolve destination box. */
    if (self->box_set)
    {
        box_x = self->box_x;
        box_y = self->box_y;
        box_w = self->box_w;
        box_h = self->box_h;
    }
    else
    {
        box_x = 0;
        box_y = 0;
        box_w = fw;
        box_h = fh;
    }

    /* Guard against degenerate box. */
    if (box_w <= 0 || box_h <= 0)
        return;

    src_w = grl_image_get_width (self->image);
    src_h = grl_image_get_height (self->image);

    if (src_w <= 0 || src_h <= 0)
        return;

    tint_ptr   = self->has_tint ? &self->tint : NULL;
    needs_clip = FALSE;

    switch (self->fit)
    {
    case LRG_REEL_FIT_FILL:
    case LRG_REEL_FIT_STRETCH:
        /* Stretch to exactly fill the box, ignoring aspect ratio. */
        dst_rect.x      = (gfloat) box_x;
        dst_rect.y      = (gfloat) box_y;
        dst_rect.width  = (gfloat) box_w;
        dst_rect.height = (gfloat) box_h;
        break;

    case LRG_REEL_FIT_CONTAIN:
        /* Scale to fit inside the box preserving aspect; letterbox. */
        compute_contain_rect (src_w, src_h, box_x, box_y, box_w, box_h,
                              &dst_rect);
        break;

    case LRG_REEL_FIT_COVER:
        /* Scale to cover the box preserving aspect; crop excess. */
        compute_cover_dst_rect (src_w, src_h, box_x, box_y, box_w, box_h,
                                &dst_rect);
        clip_rect.x      = (gfloat) box_x;
        clip_rect.y      = (gfloat) box_y;
        clip_rect.width  = (gfloat) box_w;
        clip_rect.height = (gfloat) box_h;
        needs_clip       = TRUE;
        break;

    case LRG_REEL_FIT_NONE:
    default:
        /* Draw at native size, top-left of the box. */
        dst_rect.x      = (gfloat) box_x;
        dst_rect.y      = (gfloat) box_y;
        dst_rect.width  = (gfloat) src_w;
        dst_rect.height = (gfloat) src_h;
        break;
    }

    if (needs_clip)
        lrg_image_canvas_set_clip_rect (canvas, &clip_rect);

    grl_image_draw_image (dst_img, self->image, NULL, &dst_rect, tint_ptr);

    if (needs_clip)
        lrg_image_canvas_set_clip_rect (canvas, NULL);
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_image_clip_finalize (GObject *object)
{
    LrgReelImageClip *self = LRG_REEL_IMAGE_CLIP (object);

    g_clear_object (&self->image);

    G_OBJECT_CLASS (lrg_reel_image_clip_parent_class)->finalize (object);
}

static void
lrg_reel_image_clip_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgReelImageClip *self = LRG_REEL_IMAGE_CLIP (object);

    switch (prop_id)
    {
    case PROP_IMAGE:
        g_value_set_object (value, self->image);
        break;
    case PROP_FIT:
        g_value_set_enum (value, (gint) self->fit);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_image_clip_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgReelImageClip *self = LRG_REEL_IMAGE_CLIP (object);

    switch (prop_id)
    {
    case PROP_IMAGE:
        lrg_reel_image_clip_set_image (self, g_value_get_object (value));
        break;
    case PROP_FIT:
        lrg_reel_image_clip_set_fit (self, (LrgReelFit) g_value_get_enum (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_image_clip_class_init (LrgReelImageClipClass *klass)
{
    GObjectClass    *object_class = G_OBJECT_CLASS (klass);
    LrgReelClipClass *clip_class  = LRG_REEL_CLIP_CLASS (klass);

    object_class->finalize     = lrg_reel_image_clip_finalize;
    object_class->get_property = lrg_reel_image_clip_get_property;
    object_class->set_property = lrg_reel_image_clip_set_property;

    clip_class->render = lrg_reel_image_clip_render;

    /**
     * LrgReelImageClip:image:
     *
     * The #GrlImage drawn by this clip.
     *
     * Since: 1.0
     */
    properties[PROP_IMAGE] =
        g_param_spec_object ("image", "Image",
                             "The GrlImage drawn by this clip",
                             GRL_TYPE_IMAGE,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelImageClip:fit:
     *
     * How the image is scaled and positioned inside the destination box.
     *
     * Since: 1.0
     */
    properties[PROP_FIT] =
        g_param_spec_enum ("fit", "Fit",
                           "How the image is fitted inside the destination box",
                           LRG_TYPE_REEL_FIT, LRG_REEL_FIT_CONTAIN,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_image_clip_init (LrgReelImageClip *self)
{
    self->image    = NULL;
    self->fit      = LRG_REEL_FIT_CONTAIN;
    self->has_tint = FALSE;
    self->box_set  = FALSE;
    self->box_x    = 0;
    self->box_y    = 0;
    self->box_w    = 0;
    self->box_h    = 0;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_image_clip_new_from_file:
 * @path: path to the image file to load.
 * @error: (nullable): return location for a #GError, or %NULL.
 *
 * Creates a new #LrgReelImageClip by loading an image from @path.
 * Returns %NULL and sets @error (domain %G_FILE_ERROR) if the file cannot
 * be loaded.
 *
 * Returns: (transfer full) (nullable): a new #LrgReelImageClip, or %NULL on
 *   error.
 *
 * Since: 1.0
 */
LrgReelImageClip *
lrg_reel_image_clip_new_from_file (const gchar  *path,
                                   GError      **error)
{
    LrgReelImageClip *self;
    GrlImage         *image;

    g_return_val_if_fail (path != NULL, NULL);

    image = grl_image_new_from_file (path);
    if (image == NULL)
    {
        g_set_error (error,
                     G_FILE_ERROR,
                     G_FILE_ERROR_FAILED,
                     "Failed to load image from file: %s",
                     path);
        return NULL;
    }

    self = g_object_new (LRG_TYPE_REEL_IMAGE_CLIP, NULL);
    self->image = image; /* transfer: grl_image_new_from_file is (transfer full) */

    return self;
}

/**
 * lrg_reel_image_clip_new_from_image:
 * @image: a #GrlImage to use as the clip source.
 *
 * Creates a new #LrgReelImageClip that holds a reference to @image.
 *
 * Returns: (transfer full): a new #LrgReelImageClip.
 *
 * Since: 1.0
 */
LrgReelImageClip *
lrg_reel_image_clip_new_from_image (GrlImage *image)
{
    LrgReelImageClip *self;

    g_return_val_if_fail (GRL_IS_IMAGE (image), NULL);

    self        = g_object_new (LRG_TYPE_REEL_IMAGE_CLIP, NULL);
    self->image = g_object_ref (image);

    return self;
}

/**
 * lrg_reel_image_clip_get_image:
 * @self: an #LrgReelImageClip.
 *
 * Returns: (transfer none) (nullable): the source #GrlImage, or %NULL.
 *
 * Since: 1.0
 */
GrlImage *
lrg_reel_image_clip_get_image (LrgReelImageClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_IMAGE_CLIP (self), NULL);

    return self->image;
}

/**
 * lrg_reel_image_clip_set_image:
 * @self: an #LrgReelImageClip.
 * @image: (nullable): a #GrlImage to use, or %NULL to clear.
 *
 * Replaces the source image.
 *
 * Since: 1.0
 */
void
lrg_reel_image_clip_set_image (LrgReelImageClip *self,
                                GrlImage         *image)
{
    g_return_if_fail (LRG_IS_REEL_IMAGE_CLIP (self));
    g_return_if_fail (image == NULL || GRL_IS_IMAGE (image));

    if (self->image == image)
        return;

    g_clear_object (&self->image);
    if (image != NULL)
        self->image = g_object_ref (image);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IMAGE]);
}

/**
 * lrg_reel_image_clip_get_fit:
 * @self: an #LrgReelImageClip.
 *
 * Returns: the current #LrgReelFit value.
 *
 * Since: 1.0
 */
LrgReelFit
lrg_reel_image_clip_get_fit (LrgReelImageClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_IMAGE_CLIP (self), LRG_REEL_FIT_CONTAIN);

    return self->fit;
}

/**
 * lrg_reel_image_clip_set_fit:
 * @self: an #LrgReelImageClip.
 * @fit: the new #LrgReelFit value.
 *
 * Sets the fit mode used when scaling the image into the destination box.
 *
 * Since: 1.0
 */
void
lrg_reel_image_clip_set_fit (LrgReelImageClip *self,
                              LrgReelFit        fit)
{
    g_return_if_fail (LRG_IS_REEL_IMAGE_CLIP (self));

    if (self->fit == fit)
        return;

    self->fit = fit;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FIT]);
}

/**
 * lrg_reel_image_clip_set_tint:
 * @self: an #LrgReelImageClip.
 * @tint: (nullable): a #GrlColor to apply as a tint, or %NULL to clear.
 *
 * Sets the tint color multiplied over the image pixels.
 *
 * Since: 1.0
 */
void
lrg_reel_image_clip_set_tint (LrgReelImageClip *self,
                               const GrlColor   *tint)
{
    g_return_if_fail (LRG_IS_REEL_IMAGE_CLIP (self));

    if (tint == NULL)
    {
        self->has_tint = FALSE;
    }
    else
    {
        self->has_tint = TRUE;
        self->tint     = *tint;
    }
}

/**
 * lrg_reel_image_clip_get_tint:
 * @self: an #LrgReelImageClip.
 *
 * Returns: %TRUE if a tint has been set.
 *
 * Since: 1.0
 */
gboolean
lrg_reel_image_clip_get_tint (LrgReelImageClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_IMAGE_CLIP (self), FALSE);

    return self->has_tint;
}

/**
 * lrg_reel_image_clip_set_box:
 * @self: an #LrgReelImageClip.
 * @x: left edge of the destination box in pixels.
 * @y: top edge of the destination box in pixels.
 * @width: width of the destination box in pixels.
 * @height: height of the destination box in pixels.
 *
 * Constrains rendering to a sub-rectangle of the frame.  Pass width and
 * height both equal to 0 to revert to the full-frame default.
 *
 * Since: 1.0
 */
void
lrg_reel_image_clip_set_box (LrgReelImageClip *self,
                              gint              x,
                              gint              y,
                              gint              width,
                              gint              height)
{
    g_return_if_fail (LRG_IS_REEL_IMAGE_CLIP (self));

    if (width <= 0 || height <= 0)
    {
        self->box_set = FALSE;
        return;
    }

    self->box_set = TRUE;
    self->box_x   = x;
    self->box_y   = y;
    self->box_w   = width;
    self->box_h   = height;
}
