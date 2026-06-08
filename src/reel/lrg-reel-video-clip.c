/* lrg-reel-video-clip.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-video-clip.h"
#include "lrg-reel-context.h"
#include "../graphics/lrg-image-canvas.h"

struct _LrgReelVideoClip
{
    LrgReelClip parent_instance;

    LrgReelVideoSource *source;
    LrgReelFit          fit;
    gdouble             trim_start;
    gdouble             playback_rate;
    gboolean            loop;
};

G_DEFINE_FINAL_TYPE (LrgReelVideoClip, lrg_reel_video_clip, LRG_TYPE_REEL_CLIP)

/* Compute the destination rect for fitting a sw x sh image into fw x fh. */
static void
reel_video_compute_fit (LrgReelFit    fit,
                        gint          sw,
                        gint          sh,
                        gint          fw,
                        gint          fh,
                        GrlRectangle *dst)
{
    gdouble s;
    gdouble fitw;
    gdouble fith;

    if (sw <= 0 || sh <= 0)
    {
        dst->x = 0.0f;
        dst->y = 0.0f;
        dst->width = (gfloat) fw;
        dst->height = (gfloat) fh;
        return;
    }

    switch (fit)
    {
    case LRG_REEL_FIT_CONTAIN:
        s = MIN ((gdouble) fw / sw, (gdouble) fh / sh);
        fitw = sw * s;
        fith = sh * s;
        dst->x = (gfloat) ((fw - fitw) / 2.0);
        dst->y = (gfloat) ((fh - fith) / 2.0);
        dst->width = (gfloat) fitw;
        dst->height = (gfloat) fith;
        break;
    case LRG_REEL_FIT_COVER:
        s = MAX ((gdouble) fw / sw, (gdouble) fh / sh);
        fitw = sw * s;
        fith = sh * s;
        dst->x = (gfloat) ((fw - fitw) / 2.0);
        dst->y = (gfloat) ((fh - fith) / 2.0);
        dst->width = (gfloat) fitw;
        dst->height = (gfloat) fith;
        break;
    case LRG_REEL_FIT_NONE:
        dst->x = 0.0f;
        dst->y = 0.0f;
        dst->width = (gfloat) sw;
        dst->height = (gfloat) sh;
        break;
    case LRG_REEL_FIT_FILL:
    case LRG_REEL_FIT_STRETCH:
    default:
        dst->x = 0.0f;
        dst->y = 0.0f;
        dst->width = (gfloat) fw;
        dst->height = (gfloat) fh;
        break;
    }
}

static void
lrg_reel_video_clip_render (LrgReelClip    *clip,
                            LrgReelContext *ctx,
                            LrgImageCanvas *canvas)
{
    LrgReelVideoClip   *self = LRG_REEL_VIDEO_CLIP (clip);
    g_autoptr(GError)   error = NULL;
    GrlImage           *frame;
    GrlRectangle        dst;
    gint                clip_frame;
    gint                fw;
    gint                fh;
    gint                src_count;
    gint                src_index;
    gdouble             reel_fps;
    gdouble             src_fps;
    gdouble             t;
    gdouble             src_time;

    if (self->source == NULL)
        return;

    src_count = lrg_reel_video_source_get_frame_count (self->source);
    if (src_count <= 0)
        return;

    clip_frame = lrg_reel_context_get_frame (ctx);
    reel_fps = lrg_reel_context_get_fps (ctx);
    fw = lrg_reel_context_get_width (ctx);
    fh = lrg_reel_context_get_height (ctx);
    src_fps = lrg_reel_video_source_get_fps (self->source);

    t = (gdouble) clip_frame / ((reel_fps > 0.0) ? reel_fps : 30.0);
    src_time = self->trim_start + t * self->playback_rate;
    src_index = (gint) (src_time * src_fps + 0.5);

    if (self->loop)
        src_index = ((src_index % src_count) + src_count) % src_count;

    frame = lrg_reel_video_source_get_frame (self->source, src_index, &error);
    if (frame == NULL)
        return;

    reel_video_compute_fit (self->fit,
                            grl_image_get_width (frame),
                            grl_image_get_height (frame),
                            fw, fh, &dst);

    grl_image_draw_image (lrg_image_canvas_get_image (canvas), frame, NULL, &dst, NULL);
}

static void
lrg_reel_video_clip_finalize (GObject *object)
{
    LrgReelVideoClip *self = LRG_REEL_VIDEO_CLIP (object);

    g_clear_object (&self->source);

    G_OBJECT_CLASS (lrg_reel_video_clip_parent_class)->finalize (object);
}

static void
lrg_reel_video_clip_class_init (LrgReelVideoClipClass *klass)
{
    GObjectClass     *object_class = G_OBJECT_CLASS (klass);
    LrgReelClipClass *clip_class = LRG_REEL_CLIP_CLASS (klass);

    object_class->finalize = lrg_reel_video_clip_finalize;
    clip_class->render = lrg_reel_video_clip_render;
}

static void
lrg_reel_video_clip_init (LrgReelVideoClip *self)
{
    self->fit = LRG_REEL_FIT_COVER;
    self->trim_start = 0.0;
    self->playback_rate = 1.0;
    self->loop = FALSE;
}

LrgReelVideoClip *
lrg_reel_video_clip_new_from_source (LrgReelVideoSource *source)
{
    LrgReelVideoClip *self;

    g_return_val_if_fail (LRG_IS_REEL_VIDEO_SOURCE (source), NULL);

    self = g_object_new (LRG_TYPE_REEL_VIDEO_CLIP, NULL);
    self->source = g_object_ref (source);

    return self;
}

LrgReelVideoClip *
lrg_reel_video_clip_new_from_file (const gchar  *path,
                                   GError      **error)
{
    g_autoptr(LrgReelVideoSource) source = NULL;

    g_return_val_if_fail (path != NULL, NULL);

    source = lrg_reel_video_source_new_from_file (path, error);
    if (source == NULL)
        return NULL;

    return lrg_reel_video_clip_new_from_source (source);
}

LrgReelVideoSource *
lrg_reel_video_clip_get_source (LrgReelVideoClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_VIDEO_CLIP (self), NULL);
    return self->source;
}

LrgReelFit
lrg_reel_video_clip_get_fit (LrgReelVideoClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_VIDEO_CLIP (self), LRG_REEL_FIT_COVER);
    return self->fit;
}

void
lrg_reel_video_clip_set_fit (LrgReelVideoClip *self,
                             LrgReelFit        fit)
{
    g_return_if_fail (LRG_IS_REEL_VIDEO_CLIP (self));
    self->fit = fit;
}

gdouble
lrg_reel_video_clip_get_trim_start (LrgReelVideoClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_VIDEO_CLIP (self), 0.0);
    return self->trim_start;
}

void
lrg_reel_video_clip_set_trim_start (LrgReelVideoClip *self,
                                    gdouble           seconds)
{
    g_return_if_fail (LRG_IS_REEL_VIDEO_CLIP (self));
    self->trim_start = seconds;
}

gdouble
lrg_reel_video_clip_get_playback_rate (LrgReelVideoClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_VIDEO_CLIP (self), 1.0);
    return self->playback_rate;
}

void
lrg_reel_video_clip_set_playback_rate (LrgReelVideoClip *self,
                                       gdouble           rate)
{
    g_return_if_fail (LRG_IS_REEL_VIDEO_CLIP (self));
    self->playback_rate = rate;
}

gboolean
lrg_reel_video_clip_get_loop (LrgReelVideoClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_VIDEO_CLIP (self), FALSE);
    return self->loop;
}

void
lrg_reel_video_clip_set_loop (LrgReelVideoClip *self,
                              gboolean          loop)
{
    g_return_if_fail (LRG_IS_REEL_VIDEO_CLIP (self));
    self->loop = loop;
}
