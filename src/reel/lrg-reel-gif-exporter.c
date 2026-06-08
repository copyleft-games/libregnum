/* lrg-reel-gif-exporter.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-gif-exporter.h"
#include "../photomode/lrg-gif-recorder.h"

struct _LrgReelGifExporter
{
    LrgReelExporter  parent_instance;

    gchar           *path;
    LrgGifRecorder  *recorder;
    gboolean         adaptive_palette;
    gboolean         dither;
    gboolean         quality_set;
};

G_DEFINE_FINAL_TYPE (LrgReelGifExporter, lrg_reel_gif_exporter,
                     LRG_TYPE_REEL_EXPORTER)

/* --------------------------------------------------------------------------
 * vfunc implementations
 * -------------------------------------------------------------------------- */

static gboolean
lrg_reel_gif_exporter_begin (LrgReelExporter *base,
                              gint             width,
                              gint             height,
                              gdouble          fps,
                              GError         **error)
{
    LrgReelGifExporter *self = LRG_REEL_GIF_EXPORTER (base);
    LrgGifRecorder *rec;
    gint fps_int;
    GError *local_error = NULL;

    fps_int = (gint) (fps + 0.5);

    rec = lrg_gif_recorder_new (self->path, width, height, fps_int, &local_error);
    if (rec == NULL)
    {
        g_set_error (error,
                     LRG_REEL_EXPORTER_ERROR,
                     LRG_REEL_EXPORTER_ERROR_OPEN,
                     "%s", local_error->message);
        g_error_free (local_error);
        return FALSE;
    }

    if (self->quality_set)
        lrg_gif_recorder_set_quality (rec, self->adaptive_palette, self->dither);

    self->recorder = rec;
    return TRUE;
}

static gboolean
lrg_reel_gif_exporter_add_frame (LrgReelExporter *base,
                                  GrlImage        *frame,
                                  GError         **error)
{
    LrgReelGifExporter *self = LRG_REEL_GIF_EXPORTER (base);
    gboolean ok;
    GError *local_error = NULL;

    ok = lrg_gif_recorder_add_frame (self->recorder, frame, &local_error);
    if (!ok)
    {
        if (local_error != NULL)
        {
            g_set_error (error,
                         LRG_REEL_EXPORTER_ERROR,
                         LRG_REEL_EXPORTER_ERROR_WRITE,
                         "%s", local_error->message);
            g_error_free (local_error);
        }
        else
        {
            g_set_error_literal (error,
                                 LRG_REEL_EXPORTER_ERROR,
                                 LRG_REEL_EXPORTER_ERROR_WRITE,
                                 "failed to add frame to GIF");
        }
        return FALSE;
    }

    return TRUE;
}

static gboolean
lrg_reel_gif_exporter_finish (LrgReelExporter *base,
                               GError         **error)
{
    LrgReelGifExporter *self = LRG_REEL_GIF_EXPORTER (base);
    gboolean ok;
    GError *local_error = NULL;

    if (self->recorder == NULL)
    {
        g_set_error_literal (error,
                             LRG_REEL_EXPORTER_ERROR,
                             LRG_REEL_EXPORTER_ERROR_FAILED,
                             "begin() was never called");
        return FALSE;
    }

    ok = lrg_gif_recorder_finish (self->recorder, &local_error);
    if (!ok)
    {
        g_propagate_error (error, local_error);
        return FALSE;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_gif_exporter_finalize (GObject *object)
{
    LrgReelGifExporter *self = LRG_REEL_GIF_EXPORTER (object);

    g_clear_object (&self->recorder);
    g_clear_pointer (&self->path, g_free);

    G_OBJECT_CLASS (lrg_reel_gif_exporter_parent_class)->finalize (object);
}

static void
lrg_reel_gif_exporter_class_init (LrgReelGifExporterClass *klass)
{
    GObjectClass        *object_class   = G_OBJECT_CLASS (klass);
    LrgReelExporterClass *exporter_class = LRG_REEL_EXPORTER_CLASS (klass);

    object_class->finalize = lrg_reel_gif_exporter_finalize;

    exporter_class->begin     = lrg_reel_gif_exporter_begin;
    exporter_class->add_frame = lrg_reel_gif_exporter_add_frame;
    exporter_class->finish    = lrg_reel_gif_exporter_finish;
}

static void
lrg_reel_gif_exporter_init (LrgReelGifExporter *self)
{
    self->path             = NULL;
    self->recorder         = NULL;
    self->adaptive_palette = FALSE;
    self->dither           = FALSE;
    self->quality_set      = FALSE;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_gif_exporter_new:
 * @path: (type filename): path of the GIF file to create.
 * @error: (nullable): return location for a #GError, or %NULL.
 *
 * Creates a new #LrgReelGifExporter that will write an animated GIF to @path.
 * The output file is not opened until lrg_reel_exporter_begin() is called.
 *
 * Returns: (transfer full) (nullable): a new #LrgReelGifExporter, or %NULL on
 *   error.
 *
 * Since: 1.0
 */
LrgReelGifExporter *
lrg_reel_gif_exporter_new (const gchar  *path,
                            GError      **error)
{
    LrgReelGifExporter *self;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    self = g_object_new (LRG_TYPE_REEL_GIF_EXPORTER, NULL);
    self->path = g_strdup (path);

    return self;
}

/**
 * lrg_reel_gif_exporter_set_quality:
 * @self: an #LrgReelGifExporter.
 * @adaptive_palette: if %TRUE, use median-cut quantisation for an adaptive
 *   per-image palette instead of the fixed web-safe palette.
 * @dither: if %TRUE (and @adaptive_palette is also %TRUE), apply
 *   Floyd-Steinberg error-diffusion dithering.
 *
 * Configures colour quantisation quality for the GIF encoder.  Must be called
 * before lrg_reel_exporter_begin() (or before the first frame is added).
 * The defaults (both %FALSE) use the web-safe palette which produces the
 * smallest files and is compatible with all viewers.
 *
 * Since: 1.0
 */
void
lrg_reel_gif_exporter_set_quality (LrgReelGifExporter *self,
                                    gboolean            adaptive_palette,
                                    gboolean            dither)
{
    g_return_if_fail (LRG_IS_REEL_GIF_EXPORTER (self));

    self->adaptive_palette = adaptive_palette;
    self->dither           = dither;
    self->quality_set      = TRUE;
}
