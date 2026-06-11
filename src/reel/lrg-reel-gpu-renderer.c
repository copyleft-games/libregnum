/* lrg-reel-gpu-renderer.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-gpu-renderer.h"
#include <gio/gio.h>

struct _LrgReelGpuRenderer
{
    GObject parent_instance;

    gint     width;
    gint     height;
    gdouble  fps;
    gint     duration_in_frames;
    GrlColor clear_color;

    GrlWindow        *window;

    /* World is drawn into scene_rt; the grade/bloom/overlay composite into
     * capture_rt (you cannot sample the framebuffer you are drawing into). */
    GrlRenderTexture *scene_rt;
    GrlRenderTexture *capture_rt;

    /* Cached color-buffer wrappers (the underlying GL texture id is stable for
     * the FBO's lifetime, so fetch once instead of leaking one per frame). */
    GrlTexture *scene_tex;
    GrlTexture *capture_tex;

    /* Per-frame post state. */
    gdouble grade_contrast;
    gdouble grade_brightness;
    gdouble grade_tint_r;
    gdouble grade_tint_g;
    gdouble grade_tint_b;
    gdouble bloom_intensity;
    gdouble bloom_threshold;

    LrgReelGpuDrawFunc draw_func;
    gpointer           draw_data;
    GDestroyNotify     draw_destroy;

    LrgReelGpuOverlayFunc overlay_func;
    gpointer              overlay_data;
    GDestroyNotify        overlay_destroy;
};

G_DEFINE_FINAL_TYPE (LrgReelGpuRenderer, lrg_reel_gpu_renderer, G_TYPE_OBJECT)

#define LRG_REEL_GPU_RENDERER_ERROR (g_quark_from_static_string ("lrg-reel-gpu-renderer"))

static void
lrg_reel_gpu_renderer_finalize (GObject *object)
{
    LrgReelGpuRenderer *self = LRG_REEL_GPU_RENDERER (object);

    if (self->draw_destroy != NULL && self->draw_data != NULL)
        self->draw_destroy (self->draw_data);
    if (self->overlay_destroy != NULL && self->overlay_data != NULL)
        self->overlay_destroy (self->overlay_data);

    g_clear_object (&self->scene_tex);
    g_clear_object (&self->capture_tex);
    g_clear_object (&self->scene_rt);
    g_clear_object (&self->capture_rt);
    g_clear_object (&self->window);

    G_OBJECT_CLASS (lrg_reel_gpu_renderer_parent_class)->finalize (object);
}

static void
lrg_reel_gpu_renderer_class_init (LrgReelGpuRendererClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_reel_gpu_renderer_finalize;
}

static void
lrg_reel_gpu_renderer_init (LrgReelGpuRenderer *self)
{
    self->fps = 30.0;
    self->clear_color.r = 0;
    self->clear_color.g = 0;
    self->clear_color.b = 0;
    self->clear_color.a = 255;

    self->grade_contrast = 1.0;
    self->grade_brightness = 0.0;
    self->grade_tint_r = 1.0;
    self->grade_tint_g = 1.0;
    self->grade_tint_b = 1.0;
    self->bloom_intensity = 0.0;
    self->bloom_threshold = 0.75;
}

gboolean
lrg_reel_gpu_renderer_is_available (void)
{
    return g_getenv ("DISPLAY") != NULL || g_getenv ("WAYLAND_DISPLAY") != NULL;
}

LrgReelGpuRenderer *
lrg_reel_gpu_renderer_new (gint      width,
                           gint      height,
                           gdouble   fps,
                           gint      duration_in_frames,
                           GError  **error)
{
    LrgReelGpuRenderer *self;

    g_return_val_if_fail (width > 0 && height > 0, NULL);

    if (!lrg_reel_gpu_renderer_is_available ())
    {
        g_set_error_literal (error, LRG_REEL_GPU_RENDERER_ERROR, 1,
                             "no display available for GPU capture");
        return NULL;
    }

    self = g_object_new (LRG_TYPE_REEL_GPU_RENDERER, NULL);
    self->width = width;
    self->height = height;
    self->fps = (fps > 0.0) ? fps : 30.0;
    self->duration_in_frames = duration_in_frames;

    self->window = grl_window_new (width, height, "reel-gpu-capture");
    if (self->window == NULL)
    {
        g_set_error_literal (error, LRG_REEL_GPU_RENDERER_ERROR, 2,
                             "failed to create a GL window for capture");
        g_object_unref (self);
        return NULL;
    }

    /* Keep the capture window off-screen. */
    grl_window_set_state (self->window, GRL_FLAG_WINDOW_HIDDEN);

    self->scene_rt = grl_render_texture_new (width, height);
    self->capture_rt = grl_render_texture_new (width, height);
    if (self->scene_rt == NULL || self->capture_rt == NULL)
    {
        g_set_error_literal (error, LRG_REEL_GPU_RENDERER_ERROR, 3,
                             "failed to create the off-screen framebuffers");
        g_object_unref (self);
        return NULL;
    }

    /* Cache the color-buffer texture wrappers once (stable GL ids). */
    self->scene_tex = grl_render_texture_get_texture (self->scene_rt);
    self->capture_tex = grl_render_texture_get_texture (self->capture_rt);

    return self;
}

void
lrg_reel_gpu_renderer_set_draw_func (LrgReelGpuRenderer *self,
                                     LrgReelGpuDrawFunc  func,
                                     gpointer            user_data,
                                     GDestroyNotify      destroy)
{
    g_return_if_fail (LRG_IS_REEL_GPU_RENDERER (self));

    if (self->draw_destroy != NULL && self->draw_data != NULL)
        self->draw_destroy (self->draw_data);

    self->draw_func = func;
    self->draw_data = user_data;
    self->draw_destroy = destroy;
}

void
lrg_reel_gpu_renderer_set_overlay_func (LrgReelGpuRenderer    *self,
                                        LrgReelGpuOverlayFunc  func,
                                        gpointer               user_data,
                                        GDestroyNotify         destroy)
{
    g_return_if_fail (LRG_IS_REEL_GPU_RENDERER (self));

    if (self->overlay_destroy != NULL && self->overlay_data != NULL)
        self->overlay_destroy (self->overlay_data);

    self->overlay_func = func;
    self->overlay_data = user_data;
    self->overlay_destroy = destroy;
}

void
lrg_reel_gpu_renderer_set_clear_color (LrgReelGpuRenderer *self,
                                       const GrlColor     *color)
{
    g_return_if_fail (LRG_IS_REEL_GPU_RENDERER (self));
    g_return_if_fail (color != NULL);

    self->clear_color = *color;
}

void
lrg_reel_gpu_renderer_set_grade (LrgReelGpuRenderer *self,
                                 gdouble             contrast,
                                 gdouble             brightness,
                                 gdouble             tint_r,
                                 gdouble             tint_g,
                                 gdouble             tint_b)
{
    g_return_if_fail (LRG_IS_REEL_GPU_RENDERER (self));

    self->grade_contrast = contrast;
    self->grade_brightness = brightness;
    self->grade_tint_r = tint_r;
    self->grade_tint_g = tint_g;
    self->grade_tint_b = tint_b;
}

void
lrg_reel_gpu_renderer_set_bloom (LrgReelGpuRenderer *self,
                                 gdouble             intensity,
                                 gdouble             threshold)
{
    g_return_if_fail (LRG_IS_REEL_GPU_RENDERER (self));

    self->bloom_intensity = intensity;
    self->bloom_threshold = threshold;
}

static guint8
lrg_clamp_unit_to_255 (gdouble v)
{
    v = v * 255.0;
    if (v < 0.0) v = 0.0;
    if (v > 255.0) v = 255.0;
    return (guint8) (v + 0.5);
}

/* Composite the graded+bloomed world into capture_rt using blend operations
 * (no fragment shaders: texture sampling inside a custom shader is unreliable
 * in this graylib/raylib build, but blended texture/rectangle draws are solid).
 *
 *  - the world is drawn straight from scene_tex;
 *  - tint is a MULTIPLY full-frame solid (matching the old reel tint clip);
 *  - positive exposure is an ADDITIVE white solid, negative exposure a darkening
 *    MULTIPLY solid;
 *  - bloom is approximated by additive, slightly-upscaled redraws of the world,
 *    so bright regions glow outward while near-black regions add nothing. */
static void
lrg_reel_gpu_renderer_composite (LrgReelGpuRenderer *self)
{
    GrlColor white;

    white = grl_color_init (255, 255, 255, 255);

    /* World. */
    grl_draw_texture (self->scene_tex, 0, 0, &white);

    /* Bloom: a few additive, expanding redraws of the world. */
    if (self->bloom_intensity > 0.0)
    {
        GrlVector2   origin;
        gint         i;
        gint         n = 4;

        origin = grl_vector2_init (0.0f, 0.0f);
        grl_draw_begin_blend_mode (GRL_BLEND_ADDITIVE);
        for (i = 0; i < n; i++)
        {
            GrlRectangle src, dst;
            GrlColor     tint;
            gfloat       grow;
            guint8       a;
            gfloat       ox, oy;

            grow = 1.0f + 0.02f * (gfloat) (i + 1);
            ox = (gfloat) self->width * (grow - 1.0f) * 0.5f;
            oy = (gfloat) self->height * (grow - 1.0f) * 0.5f;
            a = lrg_clamp_unit_to_255 (self->bloom_intensity * 0.4 / (gdouble) (i + 1));
            tint = grl_color_init (a, a, a, 255);
            src = grl_rectangle_init (0.0f, 0.0f, (gfloat) self->width, (gfloat) self->height);
            dst = grl_rectangle_init (-ox, -oy,
                                      (gfloat) self->width * grow,
                                      (gfloat) self->height * grow);
            grl_draw_texture_pro (self->scene_tex, &src, &dst, &origin, 0.0f, &tint);
        }
        grl_draw_end_blend_mode ();
    }

    /* Exposure (additive for positive, darkening multiply for negative). */
    if (self->grade_brightness > 0.0)
    {
        GrlColor add;
        guint8   a = lrg_clamp_unit_to_255 (self->grade_brightness);
        add = grl_color_init (255, 255, 255, a);
        grl_draw_begin_blend_mode (GRL_BLEND_ADDITIVE);
        grl_draw_rectangle (0, 0, self->width, self->height, &add);
        grl_draw_end_blend_mode ();
    }
    else if (self->grade_brightness < 0.0)
    {
        GrlColor mul;
        guint8   v = lrg_clamp_unit_to_255 (1.0 + self->grade_brightness);
        mul = grl_color_init (v, v, v, 255);
        grl_draw_begin_blend_mode (GRL_BLEND_MULTIPLIED);
        grl_draw_rectangle (0, 0, self->width, self->height, &mul);
        grl_draw_end_blend_mode ();
    }

    /* Tint multiply (skip the identity white tint). */
    if (self->grade_tint_r < 0.999 || self->grade_tint_g < 0.999 || self->grade_tint_b < 0.999)
    {
        GrlColor tint;
        tint = grl_color_init (lrg_clamp_unit_to_255 (self->grade_tint_r),
                               lrg_clamp_unit_to_255 (self->grade_tint_g),
                               lrg_clamp_unit_to_255 (self->grade_tint_b),
                               255);
        grl_draw_begin_blend_mode (GRL_BLEND_MULTIPLIED);
        grl_draw_rectangle (0, 0, self->width, self->height, &tint);
        grl_draw_end_blend_mode ();
    }
}

GrlImage *
lrg_reel_gpu_renderer_capture_frame (LrgReelGpuRenderer *self,
                                     gint                frame)
{
    GrlImage *image;

    g_return_val_if_fail (LRG_IS_REEL_GPU_RENDERER (self), NULL);

    /* 1. draw the world into the scene framebuffer */
    grl_render_texture_begin (self->scene_rt);
    grl_draw_clear_background (&self->clear_color);
    if (self->draw_func != NULL)
        self->draw_func (self, frame, self->draw_data);
    grl_render_texture_end (self->scene_rt);

    /* 2. composite world + grade + bloom + overlays into the capture framebuffer */
    grl_render_texture_begin (self->capture_rt);
    grl_draw_clear_background (&self->clear_color);
    lrg_reel_gpu_renderer_composite (self);
    if (self->overlay_func != NULL)
        self->overlay_func (self, frame, self->overlay_data);
    grl_render_texture_end (self->capture_rt);

    /* 3. read back the composited frame.  Framebuffer textures are stored
     * bottom-up; the single flip here corrects the whole composite at once. */
    image = grl_texture_to_image (self->capture_tex);
    if (image != NULL)
        grl_image_flip_vertical (image);

    return image;
}

gboolean
lrg_reel_gpu_renderer_render_to_exporter (LrgReelGpuRenderer *self,
                                          LrgReelExporter    *exporter,
                                          GError            **error)
{
    gint f;

    g_return_val_if_fail (LRG_IS_REEL_GPU_RENDERER (self), FALSE);
    g_return_val_if_fail (LRG_IS_REEL_EXPORTER (exporter), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (!lrg_reel_exporter_begin (exporter, self->width, self->height,
                                  self->fps, error))
        return FALSE;

    for (f = 0; f < self->duration_in_frames; f++)
    {
        g_autoptr(GrlImage) image = lrg_reel_gpu_renderer_capture_frame (self, f);

        if (image == NULL)
        {
            lrg_reel_exporter_finish (exporter, NULL);
            g_set_error (error, LRG_REEL_GPU_RENDERER_ERROR, 4,
                         "failed to capture frame %d", f);
            return FALSE;
        }

        if (!lrg_reel_exporter_add_frame (exporter, image, error))
        {
            lrg_reel_exporter_finish (exporter, NULL);
            return FALSE;
        }
    }

    return lrg_reel_exporter_finish (exporter, error);
}

gint
lrg_reel_gpu_renderer_get_width (LrgReelGpuRenderer *self)
{
    g_return_val_if_fail (LRG_IS_REEL_GPU_RENDERER (self), 0);
    return self->width;
}

gint
lrg_reel_gpu_renderer_get_height (LrgReelGpuRenderer *self)
{
    g_return_val_if_fail (LRG_IS_REEL_GPU_RENDERER (self), 0);
    return self->height;
}

gdouble
lrg_reel_gpu_renderer_get_fps (LrgReelGpuRenderer *self)
{
    g_return_val_if_fail (LRG_IS_REEL_GPU_RENDERER (self), 0.0);
    return self->fps;
}
