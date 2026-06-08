/* lrg-gif-recorder.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGifRecorder - Animated GIF recording from a sequence of frames.
 */

#include "config.h"

#include "lrg-gif-recorder.h"
#include "../lrg-log.h"

#include <math.h>

struct _LrgGifRecorder
{
    GObject parent_instance;

    /* Output parameters */
    gchar  *filename;
    gint    width;
    gint    height;
    gint    fps;
    gint    delay_cs;        /* per-frame delay in centiseconds, derived from fps */

    /* Underlying graylib writer (NULL after finish) */
    GrlGifWriter *writer;

    /* Quality settings (applied before the first frame) */
    gboolean adaptive_palette;
    gboolean dither;
    gboolean quality_applied; /* TRUE once set on the writer */

    /* Motion-blur settings */
    gint                  motion_blur_samples; /* <= 1 means disabled */
    GrlImageAccumulator  *accumulator;         /* created lazily */

    /* Statistics */
    guint frame_count;

    /* State */
    gboolean finished;
};

G_DEFINE_TYPE (LrgGifRecorder, lrg_gif_recorder, G_TYPE_OBJECT)

/* ==========================================================================
 * Error Quark
 * ========================================================================== */

/**
 * lrg_gif_recorder_error_quark:
 *
 * Returns the error quark for GIF recorder errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_gif_recorder_error_quark (void)
{
    return g_quark_from_static_string ("lrg-gif-recorder-error-quark");
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_gif_recorder_finalize (GObject *object)
{
    LrgGifRecorder *self = LRG_GIF_RECORDER (object);

    /* Close the writer if the caller never called finish() */
    if (self->writer != NULL && !self->finished)
    {
        g_autoptr(GError) error = NULL;

        if (!grl_gif_writer_close (self->writer, &error))
        {
            lrg_debug (LRG_LOG_DOMAIN_PHOTOMODE,
                       "GIF writer close during finalize failed: %s",
                       error->message);
        }
    }

    g_clear_object (&self->writer);
    g_clear_object (&self->accumulator);
    g_clear_pointer (&self->filename, g_free);

    G_OBJECT_CLASS (lrg_gif_recorder_parent_class)->finalize (object);
}

static void
lrg_gif_recorder_class_init (LrgGifRecorderClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_gif_recorder_finalize;
}

static void
lrg_gif_recorder_init (LrgGifRecorder *self)
{
    self->filename             = NULL;
    self->width                = 0;
    self->height               = 0;
    self->fps                  = 0;
    self->delay_cs             = 4; /* ~25 fps default */
    self->writer               = NULL;
    self->adaptive_palette     = FALSE;
    self->dither               = FALSE;
    self->quality_applied      = FALSE;
    self->motion_blur_samples  = 1;
    self->accumulator          = NULL;
    self->frame_count          = 0;
    self->finished             = FALSE;
}

/* ==========================================================================
 * Internal Helpers
 * ========================================================================== */

/*
 * ensure_quality_applied:
 *
 * Configures the underlying GrlGifWriter with the chosen quality settings.
 * Called once, lazily, before the first frame is written.
 */
static void
ensure_quality_applied (LrgGifRecorder *self)
{
    if (self->quality_applied)
        return;

    if (self->adaptive_palette)
    {
        grl_gif_writer_set_quantizer (self->writer,
                                      GRL_GIF_QUANTIZER_MEDIAN_CUT);

        if (self->dither)
            grl_gif_writer_set_dither (self->writer,
                                       GRL_GIF_DITHER_FLOYD_STEINBERG);
    }

    self->quality_applied = TRUE;
}

/*
 * ensure_accumulator:
 *
 * Creates the GrlImageAccumulator lazily when motion-blur is first used.
 */
static void
ensure_accumulator (LrgGifRecorder *self)
{
    if (self->accumulator != NULL)
        return;

    self->accumulator = grl_image_accumulator_new (self->width,
                                                   self->height,
                                                   TRUE /* linear */);
}

/*
 * append_image_to_gif:
 *
 * Lower-level helper: applies quality settings once, then delegates to
 * grl_gif_writer_add_frame().
 */
static gboolean
append_image_to_gif (LrgGifRecorder  *self,
                     GrlImage        *image,
                     GError         **error)
{
    ensure_quality_applied (self);

    if (!grl_gif_writer_add_frame (self->writer, image, self->delay_cs, error))
        return FALSE;

    self->frame_count++;
    return TRUE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgGifRecorder *
lrg_gif_recorder_new (const gchar  *filename,
                      gint          width,
                      gint          height,
                      gint          fps,
                      GError      **error)
{
    LrgGifRecorder *self;
    GrlGifWriter   *writer;
    gint            delay_cs;

    g_return_val_if_fail (filename != NULL, NULL);
    g_return_val_if_fail (width > 0, NULL);
    g_return_val_if_fail (height > 0, NULL);
    g_return_val_if_fail (fps >= 1, NULL);

    /* Derive per-frame delay: round(100.0 / fps), minimum 1 centisecond */
    delay_cs = (gint) round (100.0 / fps);
    if (delay_cs < 1)
        delay_cs = 1;

    /* Open the underlying GIF writer immediately */
    writer = grl_gif_writer_new (filename, width, height, 0 /* loop forever */, error);
    if (writer == NULL)
        return NULL;

    self = g_object_new (LRG_TYPE_GIF_RECORDER, NULL);

    self->filename  = g_strdup (filename);
    self->width     = width;
    self->height    = height;
    self->fps       = fps;
    self->delay_cs  = delay_cs;
    self->writer    = writer; /* takes ownership */

    lrg_debug (LRG_LOG_DOMAIN_PHOTOMODE,
               "GIF recorder opened: %s (%dx%d @ %d fps, delay %d cs)",
               filename, width, height, fps, delay_cs);

    return self;
}

void
lrg_gif_recorder_set_quality (LrgGifRecorder *self,
                               gboolean        adaptive_palette,
                               gboolean        dither)
{
    g_return_if_fail (LRG_IS_GIF_RECORDER (self));

    /* Warn if quality has already been flushed to the writer */
    if (self->quality_applied)
    {
        lrg_debug (LRG_LOG_DOMAIN_PHOTOMODE,
                   "lrg_gif_recorder_set_quality called after first frame; "
                   "settings will have no effect");
        return;
    }

    self->adaptive_palette = adaptive_palette;
    self->dither           = dither;
}

void
lrg_gif_recorder_set_motion_blur (LrgGifRecorder *self,
                                   gint            samples)
{
    g_return_if_fail (LRG_IS_GIF_RECORDER (self));

    /* Normalise: any value <= 1 means disabled (treat as 1 sample) */
    self->motion_blur_samples = (samples > 1) ? samples : 1;
}

gboolean
lrg_gif_recorder_add_frame (LrgGifRecorder  *self,
                             GrlImage        *frame,
                             GError         **error)
{
    g_return_val_if_fail (LRG_IS_GIF_RECORDER (self), FALSE);
    g_return_val_if_fail (GRL_IS_IMAGE (frame), FALSE);

    if (self->finished)
    {
        g_set_error (error,
                     LRG_GIF_RECORDER_ERROR,
                     LRG_GIF_RECORDER_ERROR_ALREADY_CLOSED,
                     "Cannot add frame: recorder has already been finished");
        return FALSE;
    }

    return append_image_to_gif (self, frame, error);
}

gboolean
lrg_gif_recorder_capture_frame (LrgGifRecorder  *self,
                                 GError         **error)
{
    g_autoptr(GrlImage) frame = NULL;
    gboolean            result;

    g_return_val_if_fail (LRG_IS_GIF_RECORDER (self), FALSE);

    if (self->finished)
    {
        g_set_error (error,
                     LRG_GIF_RECORDER_ERROR,
                     LRG_GIF_RECORDER_ERROR_ALREADY_CLOSED,
                     "Cannot capture frame: recorder has already been finished");
        return FALSE;
    }

    /*
     * Capture the current GL frame buffer.  Requires a live OpenGL context
     * (i.e., an open GrlWindow).
     */
    frame = grl_image_new_from_screen ();
    if (frame == NULL)
    {
        g_set_error (error,
                     LRG_GIF_RECORDER_ERROR,
                     LRG_GIF_RECORDER_ERROR_FAILED,
                     "Failed to capture frame from screen "
                     "(is a GrlWindow open?)");
        return FALSE;
    }

    result = append_image_to_gif (self, frame, error);

    return result;
}

void
lrg_gif_recorder_begin_frame (LrgGifRecorder *self)
{
    g_return_if_fail (LRG_IS_GIF_RECORDER (self));

    ensure_accumulator (self);
    grl_image_accumulator_reset (self->accumulator);
}

void
lrg_gif_recorder_add_subframe (LrgGifRecorder *self,
                                GrlImage       *subframe)
{
    g_return_if_fail (LRG_IS_GIF_RECORDER (self));
    g_return_if_fail (GRL_IS_IMAGE (subframe));
    g_return_if_fail (self->accumulator != NULL);

    grl_image_accumulator_add (self->accumulator, subframe, 1.0f);
}

gboolean
lrg_gif_recorder_end_frame (LrgGifRecorder  *self,
                             GError         **error)
{
    g_autoptr(GrlImage) resolved = NULL;

    g_return_val_if_fail (LRG_IS_GIF_RECORDER (self), FALSE);

    if (self->finished)
    {
        g_set_error (error,
                     LRG_GIF_RECORDER_ERROR,
                     LRG_GIF_RECORDER_ERROR_ALREADY_CLOSED,
                     "Cannot end frame: recorder has already been finished");
        return FALSE;
    }

    if (self->accumulator == NULL)
    {
        /*
         * begin_frame was never called; nothing to resolve.  This is a
         * programming error but we treat it as a no-op to be robust.
         */
        lrg_debug (LRG_LOG_DOMAIN_PHOTOMODE,
                   "lrg_gif_recorder_end_frame called without a preceding "
                   "begin_frame — skipping");
        return TRUE;
    }

    /* Resolve the accumulated sub-frames into a single averaged image */
    resolved = grl_image_accumulator_resolve (self->accumulator);
    if (resolved == NULL)
    {
        /* No sub-frames were added (total weight zero); nothing to append */
        lrg_debug (LRG_LOG_DOMAIN_PHOTOMODE,
                   "lrg_gif_recorder_end_frame: accumulator has zero weight "
                   "(no sub-frames added) — skipping frame");
        return TRUE;
    }

    return append_image_to_gif (self, resolved, error);
}

gboolean
lrg_gif_recorder_finish (LrgGifRecorder  *self,
                          GError         **error)
{
    g_return_val_if_fail (LRG_IS_GIF_RECORDER (self), FALSE);

    /* Idempotent: do nothing if already finished */
    if (self->finished)
        return TRUE;

    if (!grl_gif_writer_close (self->writer, error))
        return FALSE;

    self->finished = TRUE;

    lrg_debug (LRG_LOG_DOMAIN_PHOTOMODE,
               "GIF recorder finished: %s (%u frames)",
               self->filename, self->frame_count);

    return TRUE;
}

guint
lrg_gif_recorder_get_frame_count (LrgGifRecorder *self)
{
    g_return_val_if_fail (LRG_IS_GIF_RECORDER (self), 0);

    return self->frame_count;
}
