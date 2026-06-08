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
#include <gio/gio.h>

struct _LrgReelRenderer
{
    GObject parent_instance;

    LrgReel        *reel;
    GrlColor        background;
    gboolean        has_background;
    gint            motion_blur_samples;  /* 1 = off */

    /* Reused render state (created lazily, sized to the reel).  Per-clip
     * compositing (opacity/blend/transform) is handled by the clip dispatcher
     * via the context's scratch pool. */
    LrgImageCanvas *canvas;
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
    self->motion_blur_samples = 1;
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

/**
 * lrg_reel_renderer_set_motion_blur:
 * @self: an #LrgReelRenderer
 * @samples: sub-frame samples per output frame (1 disables motion blur).
 *
 * Enables motion blur by averaging @samples renders taken across each frame's
 * exposure window.  Animations driven by seconds (e.g. interpolating on
 * lrg_reel_context_get_seconds()) smear; integer-frame content does not.
 *
 * Since: 1.0
 */
void
lrg_reel_renderer_set_motion_blur (LrgReelRenderer *self,
                                   gint             samples)
{
    g_return_if_fail (LRG_IS_REEL_RENDERER (self));

    self->motion_blur_samples = MAX (1, samples);
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

/* Lazily create the reusable canvas and context. */
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
    self->ctx = lrg_reel_context_new (0,
                                      lrg_reel_get_fps (self->reel),
                                      w, h,
                                      lrg_reel_get_duration_in_frames (self->reel));
}

/* Render one pass of @frame into the reusable canvas; returns the live image. */
static GrlImage *
reel_renderer_render_pass (LrgReelRenderer *self,
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

        if (!lrg_reel_clip_get_visible (clip))
            continue;

        /* Push the clip's own window: get_frame() becomes clip-relative. */
        lrg_reel_context_push_offset (self->ctx,
                                      lrg_reel_clip_get_from_frame (clip),
                                      lrg_reel_clip_get_duration_in_frames (clip));

        /* The clip dispatcher applies opacity / blend / transform itself
         * (compositing through the context's scratch pool when needed). */
        if (lrg_reel_context_is_active (self->ctx))
            lrg_reel_clip_render (clip, self->ctx, self->canvas);

        lrg_reel_context_pop_offset (self->ctx);
    }

    return canvas_image;
}

/* Render one frame, accumulating sub-frame passes when motion blur is on. */
static GrlImage *
reel_renderer_render_into_canvas (LrgReelRenderer *self,
                                  gint             frame)
{
    GrlImage *image;
    gdouble  *accum;
    gint      n;
    gint      k;
    gint      w;
    gint      h;
    gint      x;
    gint      y;

    if (self->motion_blur_samples <= 1)
        return reel_renderer_render_pass (self, frame);

    reel_renderer_ensure_state (self);  /* self->ctx must exist before set_subframe */

    n = self->motion_blur_samples;
    w = lrg_reel_get_width (self->reel);
    h = lrg_reel_get_height (self->reel);
    accum = g_new0 (gdouble, (gsize) w * h * 4);
    image = NULL;

    /* Sweep the exposure window [frame, frame+1); time-based animation moves. */
    for (k = 0; k < n; k++)
    {
        lrg_reel_context_set_subframe (self->ctx, (gdouble) k / (gdouble) n);
        image = reel_renderer_render_pass (self, frame);

        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
            {
                g_autoptr(GrlColor) c = grl_image_get_pixel (image, x, y);
                gsize idx = ((gsize) y * w + x) * 4;

                accum[idx] += c->r;
                accum[idx + 1] += c->g;
                accum[idx + 2] += c->b;
                accum[idx + 3] += c->a;
            }
    }
    lrg_reel_context_set_subframe (self->ctx, 0.0);

    /* Write the averaged frame back into the (last pass's) canvas image. */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            gsize    idx = ((gsize) y * w + x) * 4;
            GrlColor c;

            c.r = (guint8) (accum[idx] / n);
            c.g = (guint8) (accum[idx + 1] / n);
            c.b = (guint8) (accum[idx + 2] / n);
            c.a = (guint8) (accum[idx + 3] / n);
            grl_image_draw_pixel (image, x, y, &c);
        }

    g_free (accum);
    return image;
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

/**
 * lrg_reel_renderer_render_still:
 * @self: an #LrgReelRenderer
 * @frame: the frame index to render.
 * @path: (type filename): the output image path (format chosen by extension,
 *   e.g. .png/.jpg).  PNG preserves the composition's alpha channel.
 * @error: (nullable): return location for a #GError.
 *
 * Renders a single frame and writes it to an image file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
gboolean
lrg_reel_renderer_render_still (LrgReelRenderer *self,
                                gint             frame,
                                const gchar     *path,
                                GError         **error)
{
    g_autoptr(GrlImage) image = NULL;

    g_return_val_if_fail (LRG_IS_REEL_RENDERER (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    image = lrg_reel_renderer_render_frame (self, frame);
    if (image == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "failed to render frame %d", frame);
        return FALSE;
    }

    if (!grl_image_export (image, path))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "failed to write still image to '%s'", path);
        return FALSE;
    }

    return TRUE;
}

/**
 * lrg_reel_renderer_render_range:
 * @self: an #LrgReelRenderer
 * @start_frame: first frame (inclusive, clamped to 0).
 * @end_frame: last frame (exclusive, clamped to the reel length).
 * @exporter: the #LrgReelExporter sink.
 * @error: (nullable): return location for a #GError.
 *
 * Renders only the frames in [@start_frame, @end_frame) to @exporter.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
gboolean
lrg_reel_renderer_render_range (LrgReelRenderer *self,
                                gint             start_frame,
                                gint             end_frame,
                                LrgReelExporter *exporter,
                                GError         **error)
{
    gint total;
    gint w;
    gint h;
    gint f;

    g_return_val_if_fail (LRG_IS_REEL_RENDERER (self), FALSE);
    g_return_val_if_fail (LRG_IS_REEL_EXPORTER (exporter), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    total = lrg_reel_get_duration_in_frames (self->reel);
    if (start_frame < 0)
        start_frame = 0;
    if (end_frame > total)
        end_frame = total;
    if (end_frame < start_frame)
        end_frame = start_frame;

    w = lrg_reel_get_width (self->reel);
    h = lrg_reel_get_height (self->reel);

    if (!lrg_reel_exporter_begin (exporter, w, h,
                                  lrg_reel_get_fps (self->reel), error))
        return FALSE;

    for (f = start_frame; f < end_frame; f++)
    {
        GrlImage *image = reel_renderer_render_into_canvas (self, f);

        if (!lrg_reel_exporter_add_frame (exporter, image, error))
        {
            lrg_reel_exporter_finish (exporter, NULL);
            return FALSE;
        }

        if (self->progress_cb != NULL)
            self->progress_cb ((guint) (f - start_frame),
                               (guint) (end_frame - start_frame),
                               self->progress_data);
    }

    return lrg_reel_exporter_finish (exporter, error);
}

typedef struct
{
    LrgReelRenderer *renderer;
    gint             mod;       /* this worker renders frames mod, mod+n, ... */
    gint             n;
    gint             total;
    GrlImage       **results;   /* shared; each worker writes disjoint indices */
} ReelParallelWorker;

static gpointer
reel_parallel_worker (gpointer data)
{
    ReelParallelWorker *w = data;
    gint                f;

    for (f = w->mod; f < w->total; f += w->n)
        w->results[f] = lrg_reel_renderer_render_frame (w->renderer, f);

    return NULL;
}

/**
 * lrg_reel_renderer_render_parallel:
 * @self: an #LrgReelRenderer
 * @n_threads: worker thread count, or 0 to use the CPU count.
 * @exporter: the #LrgReelExporter sink.
 * @error: (nullable): return location for a #GError.
 *
 * Renders every frame across @n_threads worker threads (each with its own
 * canvas/context), then feeds the frames to @exporter strictly in order — so
 * the output is identical to a sequential render.  Each frame is independent
 * and deterministic, which is what makes this safe.  (Clips that share mutable
 * state across threads — e.g. a video source's frame cache — are the exception
 * and should be rendered sequentially.)
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
gboolean
lrg_reel_renderer_render_parallel (LrgReelRenderer *self,
                                   gint             n_threads,
                                   LrgReelExporter *exporter,
                                   GError         **error)
{
    gint               total;
    gint               w;
    gint               h;
    gint               f;
    gint               t;
    GrlImage         **results;
    GThread          **threads;
    LrgReelRenderer  **renderers;
    ReelParallelWorker *workers;
    gboolean           ok = TRUE;

    g_return_val_if_fail (LRG_IS_REEL_RENDERER (self), FALSE);
    g_return_val_if_fail (LRG_IS_REEL_EXPORTER (exporter), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    total = lrg_reel_get_duration_in_frames (self->reel);
    if (total <= 0)
        return lrg_reel_exporter_begin (exporter, lrg_reel_get_width (self->reel),
                                        lrg_reel_get_height (self->reel),
                                        lrg_reel_get_fps (self->reel), error) &&
               lrg_reel_exporter_finish (exporter, error);

    if (n_threads < 1)
        n_threads = (gint) g_get_num_processors ();
    if (n_threads > total)
        n_threads = total;
    if (n_threads < 1)
        n_threads = 1;

    w = lrg_reel_get_width (self->reel);
    h = lrg_reel_get_height (self->reel);

    results = g_new0 (GrlImage *, total);
    threads = g_new0 (GThread *, n_threads);
    renderers = g_new0 (LrgReelRenderer *, n_threads);
    workers = g_new0 (ReelParallelWorker, n_threads);

    for (t = 0; t < n_threads; t++)
    {
        renderers[t] = lrg_reel_renderer_new (self->reel);
        if (self->has_background)
            lrg_reel_renderer_set_background (renderers[t], &self->background);

        workers[t].renderer = renderers[t];
        workers[t].mod = t;
        workers[t].n = n_threads;
        workers[t].total = total;
        workers[t].results = results;
        threads[t] = g_thread_new ("reel-render", reel_parallel_worker, &workers[t]);
    }

    for (t = 0; t < n_threads; t++)
        g_thread_join (threads[t]);

    /* Feed the rendered frames to the exporter strictly in order. */
    if (!lrg_reel_exporter_begin (exporter, w, h,
                                  lrg_reel_get_fps (self->reel), error))
        ok = FALSE;

    for (f = 0; ok && f < total; f++)
    {
        if (!lrg_reel_exporter_add_frame (exporter, results[f], error))
        {
            lrg_reel_exporter_finish (exporter, NULL);
            ok = FALSE;
            break;
        }

        if (self->progress_cb != NULL)
            self->progress_cb ((guint) f, (guint) total, self->progress_data);
    }

    if (ok)
        ok = lrg_reel_exporter_finish (exporter, error);

    for (f = 0; f < total; f++)
        g_clear_object (&results[f]);
    for (t = 0; t < n_threads; t++)
        g_object_unref (renderers[t]);

    g_free (results);
    g_free (threads);
    g_free (renderers);
    g_free (workers);

    return ok;
}
