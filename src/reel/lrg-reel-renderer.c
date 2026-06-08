/* lrg-reel-renderer.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-renderer.h"
#include "lrg-reel.h"
#include "lrg-reel-clip.h"
#include "lrg-reel-context.h"
#include "lrg-reel-exporter.h"
#include "../graphics/lrg-image-canvas.h"

struct _LrgReelRenderer
{
    GObject parent_instance;

    LrgReel        *reel;
    GrlColor        background;
    gboolean        has_background;

    /* Reused render state (created lazily, sized to the reel). */
    LrgImageCanvas *canvas;
    GrlLayer       *scratch_layer;
    LrgImageCanvas *scratch_canvas;
    LrgReelContext *ctx;

    LrgReelProgressFunc progress_cb;
    gpointer            progress_data;
    GDestroyNotify      progress_destroy;
};

G_DEFINE_FINAL_TYPE (LrgReelRenderer, lrg_reel_renderer, G_TYPE_OBJECT)

static void
lrg_reel_renderer_dispose (GObject *object)
{
    LrgReelRenderer *self = LRG_REEL_RENDERER (object);

    g_clear_object (&self->canvas);
    g_clear_object (&self->scratch_canvas);
    g_clear_pointer (&self->scratch_layer, grl_layer_unref);
    g_clear_object (&self->ctx);
    g_clear_object (&self->reel);

    G_OBJECT_CLASS (lrg_reel_renderer_parent_class)->dispose (object);
}

static void
lrg_reel_renderer_finalize (GObject *object)
{
    LrgReelRenderer *self = LRG_REEL_RENDERER (object);

    if (self->progress_destroy != NULL && self->progress_data != NULL)
        self->progress_destroy (self->progress_data);

    G_OBJECT_CLASS (lrg_reel_renderer_parent_class)->finalize (object);
}

static void
lrg_reel_renderer_class_init (LrgReelRendererClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_reel_renderer_dispose;
    object_class->finalize = lrg_reel_renderer_finalize;
}

static void
lrg_reel_renderer_init (LrgReelRenderer *self)
{
    self->has_background = FALSE;
}

LrgReelRenderer *
lrg_reel_renderer_new (LrgReel *reel)
{
    LrgReelRenderer *self;

    g_return_val_if_fail (LRG_IS_REEL (reel), NULL);

    self = g_object_new (LRG_TYPE_REEL_RENDERER, NULL);
    self->reel = g_object_ref (reel);

    return self;
}

void
lrg_reel_renderer_set_background (LrgReelRenderer *self,
                                  const GrlColor  *background)
{
    g_return_if_fail (LRG_IS_REEL_RENDERER (self));

    if (background != NULL)
    {
        self->background = *background;
        self->has_background = TRUE;
    }
    else
    {
        self->has_background = FALSE;
    }
}

void
lrg_reel_renderer_set_progress_callback (LrgReelRenderer     *self,
                                         LrgReelProgressFunc  callback,
                                         gpointer             user_data,
                                         GDestroyNotify       destroy)
{
    g_return_if_fail (LRG_IS_REEL_RENDERER (self));

    if (self->progress_destroy != NULL && self->progress_data != NULL)
        self->progress_destroy (self->progress_data);

    self->progress_cb = callback;
    self->progress_data = user_data;
    self->progress_destroy = destroy;
}

/* Lazily create the reusable canvas, scratch layer and context. */
static void
reel_renderer_ensure_state (LrgReelRenderer *self)
{
    gint w;
    gint h;

    if (self->canvas != NULL)
        return;

    w = lrg_reel_get_width (self->reel);
    h = lrg_reel_get_height (self->reel);

    self->canvas = lrg_image_canvas_new (w, h, NULL);
    self->scratch_layer = grl_layer_new (w, h);
    self->scratch_canvas =
        lrg_image_canvas_new_for_image (grl_layer_get_image (self->scratch_layer));
    self->ctx = lrg_reel_context_new (0,
                                      lrg_reel_get_fps (self->reel),
                                      w, h,
                                      lrg_reel_get_duration_in_frames (self->reel));
}

/* Render one frame into the reusable canvas; returns the live canvas image. */
static GrlImage *
reel_renderer_render_into_canvas (LrgReelRenderer *self,
                                  gint             frame)
{
    GrlColor   transparent = { 0, 0, 0, 0 };
    GrlImage  *canvas_image;
    GPtrArray *clips;
    guint      i;

    reel_renderer_ensure_state (self);

    /* Cheapest correct per-frame reset. */
    if (self->has_background)
        lrg_image_canvas_clear (self->canvas, &self->background);
    else
        lrg_image_canvas_clear (self->canvas, &transparent);

    canvas_image = lrg_image_canvas_get_image (self->canvas);

    lrg_reel_context_set_absolute_frame (self->ctx, frame);

    clips = lrg_reel_get_clips (self->reel);
    for (i = 0; i < clips->len; i++)
    {
        LrgReelClip *clip = g_ptr_array_index (clips, i);
        gdouble      opacity;

        if (!lrg_reel_clip_get_visible (clip))
            continue;

        /* Push the clip's own window: get_frame() becomes clip-relative. */
        lrg_reel_context_push_offset (self->ctx,
                                      lrg_reel_clip_get_from_frame (clip),
                                      lrg_reel_clip_get_duration_in_frames (clip));

        if (lrg_reel_context_is_active (self->ctx))
        {
            opacity = lrg_reel_clip_get_opacity (clip);

            if (opacity >= 0.999)
            {
                /* Draw straight onto the shared canvas. */
                lrg_reel_clip_render (clip, self->ctx, self->canvas);
            }
            else
            {
                /* Draw onto a transparent scratch layer, then composite the
                 * whole layer over the canvas at reduced opacity. */
                lrg_image_canvas_clear (self->scratch_canvas, &transparent);
                lrg_reel_clip_render (clip, self->ctx, self->scratch_canvas);
                grl_image_composite_layer (canvas_image, self->scratch_layer,
                                           0, 0, GRL_LAYER_BLEND_NORMAL,
                                           (gfloat) opacity);
            }
        }

        lrg_reel_context_pop_offset (self->ctx);
    }

    return canvas_image;
}

GrlImage *
lrg_reel_renderer_get_canvas_image (LrgReelRenderer *self,
                                    gint             frame)
{
    g_return_val_if_fail (LRG_IS_REEL_RENDERER (self), NULL);

    return reel_renderer_render_into_canvas (self, frame);
}

GrlImage *
lrg_reel_renderer_render_frame (LrgReelRenderer *self,
                                gint             frame)
{
    GrlImage *live;

    g_return_val_if_fail (LRG_IS_REEL_RENDERER (self), NULL);

    live = reel_renderer_render_into_canvas (self, frame);

    /* Return an independent copy the caller owns. */
    return grl_image_copy (live);
}

gboolean
lrg_reel_renderer_render_to_exporter (LrgReelRenderer *self,
                                      LrgReelExporter *exporter,
                                      GError         **error)
{
    gint  total;
    gint  w;
    gint  h;
    gint  f;

    g_return_val_if_fail (LRG_IS_REEL_RENDERER (self), FALSE);
    g_return_val_if_fail (LRG_IS_REEL_EXPORTER (exporter), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    w = lrg_reel_get_width (self->reel);
    h = lrg_reel_get_height (self->reel);
    total = lrg_reel_get_duration_in_frames (self->reel);

    if (!lrg_reel_exporter_begin (exporter, w, h,
                                  lrg_reel_get_fps (self->reel), error))
        return FALSE;

    for (f = 0; f < total; f++)
    {
        GrlImage *image = reel_renderer_render_into_canvas (self, f);

        if (!lrg_reel_exporter_add_frame (exporter, image, error))
        {
            /* Let the sink clean up, but report the original error. */
            lrg_reel_exporter_finish (exporter, NULL);
            return FALSE;
        }

        if (self->progress_cb != NULL)
            self->progress_cb ((guint) f, (guint) total, self->progress_data);
    }

    return lrg_reel_exporter_finish (exporter, error);
}
