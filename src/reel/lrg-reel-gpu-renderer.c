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
    GrlRenderTexture *render_texture;

    LrgReelGpuDrawFunc draw_func;
    gpointer           draw_data;
    GDestroyNotify     draw_destroy;
};

G_DEFINE_FINAL_TYPE (LrgReelGpuRenderer, lrg_reel_gpu_renderer, G_TYPE_OBJECT)

#define LRG_REEL_GPU_RENDERER_ERROR (g_quark_from_static_string ("lrg-reel-gpu-renderer"))

static void
lrg_reel_gpu_renderer_finalize (GObject *object)
{
    LrgReelGpuRenderer *self = LRG_REEL_GPU_RENDERER (object);

    if (self->draw_destroy != NULL && self->draw_data != NULL)
        self->draw_destroy (self->draw_data);

    g_clear_object (&self->render_texture);
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

    self->render_texture = grl_render_texture_new (width, height);
    if (self->render_texture == NULL)
    {
        g_set_error_literal (error, LRG_REEL_GPU_RENDERER_ERROR, 3,
                             "failed to create the off-screen framebuffer");
        g_object_unref (self);
        return NULL;
    }

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
lrg_reel_gpu_renderer_set_clear_color (LrgReelGpuRenderer *self,
                                       const GrlColor     *color)
{
    g_return_if_fail (LRG_IS_REEL_GPU_RENDERER (self));
    g_return_if_fail (color != NULL);

    self->clear_color = *color;
}

GrlImage *
lrg_reel_gpu_renderer_capture_frame (LrgReelGpuRenderer *self,
                                     gint                frame)
{
    GrlTexture *texture;
    GrlImage   *image;

    g_return_val_if_fail (LRG_IS_REEL_GPU_RENDERER (self), NULL);

    grl_render_texture_begin (self->render_texture);
    grl_draw_clear_background (&self->clear_color);

    if (self->draw_func != NULL)
        self->draw_func (self, frame, self->draw_data);

    grl_render_texture_end (self->render_texture);

    texture = grl_render_texture_get_texture (self->render_texture);
    image = grl_texture_to_image (texture);

    /* Framebuffer textures are stored bottom-up; flip to image orientation. */
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
