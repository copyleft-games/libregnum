/* lrg-gif-recorder.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGifRecorder - Animated GIF recording from a sequence of frames.
 *
 * Records a sequence of #GrlImage frames to an animated GIF file using
 * graylib's #GrlGifWriter. Optionally applies motion blur by averaging
 * multiple sub-frames per output frame via #GrlImageAccumulator.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_GIF_RECORDER (lrg_gif_recorder_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGifRecorder, lrg_gif_recorder, LRG, GIF_RECORDER, GObject)

/* ==========================================================================
 * Error Domain
 * ========================================================================== */

/**
 * LRG_GIF_RECORDER_ERROR:
 *
 * Error domain for GIF recorder errors.
 *
 * Since: 1.0
 */
#define LRG_GIF_RECORDER_ERROR (lrg_gif_recorder_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_gif_recorder_error_quark (void);

/**
 * LrgGifRecorderError:
 * @LRG_GIF_RECORDER_ERROR_FAILED: Generic failure.
 * @LRG_GIF_RECORDER_ERROR_OPEN: Could not open the output file.
 * @LRG_GIF_RECORDER_ERROR_WRITE: Failed to write a frame or close the file.
 * @LRG_GIF_RECORDER_ERROR_ALREADY_CLOSED: The recorder has already been finished.
 *
 * Error codes for the GIF recorder.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_GIF_RECORDER_ERROR_FAILED,
    LRG_GIF_RECORDER_ERROR_OPEN,
    LRG_GIF_RECORDER_ERROR_WRITE,
    LRG_GIF_RECORDER_ERROR_ALREADY_CLOSED
} LrgGifRecorderError;

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_gif_recorder_new:
 * @filename: (type filename): Path of the GIF file to create.
 * @width: Canvas width in pixels (must be > 0).
 * @height: Canvas height in pixels (must be > 0).
 * @fps: Target frames per second (must be >= 1). The per-frame delay is
 *   derived as @delay_cs = MAX(1, ROUND(100.0 / @fps)) centiseconds.
 * @error: (nullable): Return location for a #GError, or %NULL.
 *
 * Creates a new #LrgGifRecorder and opens @filename for writing.  The GIF
 * header is emitted immediately; the file is left open until
 * lrg_gif_recorder_finish() is called (or the object is finalized).
 *
 * The GIF loops forever (loop count 0).
 *
 * Returns: (transfer full) (nullable): A new #LrgGifRecorder, or %NULL on
 *   error.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgGifRecorder *
lrg_gif_recorder_new (const gchar  *filename,
                      gint          width,
                      gint          height,
                      gint          fps,
                      GError      **error);

/* ==========================================================================
 * Quality Configuration
 * ========================================================================== */

/**
 * lrg_gif_recorder_set_quality:
 * @self: an #LrgGifRecorder.
 * @adaptive_palette: If %TRUE, use median-cut quantization for an adaptive
 *   per-image palette instead of the fixed web-safe palette.
 * @dither: If %TRUE (and @adaptive_palette is also %TRUE), apply
 *   Floyd-Steinberg error-diffusion dithering.
 *
 * Configures colour quantization quality for the GIF encoder.  Must be
 * called before the first frame is added.  The defaults (both %FALSE) use
 * the web-safe palette which produces the smallest output and is compatible
 * with all viewers.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gif_recorder_set_quality (LrgGifRecorder *self,
                               gboolean        adaptive_palette,
                               gboolean        dither);

/* ==========================================================================
 * Motion Blur
 * ========================================================================== */

/**
 * lrg_gif_recorder_set_motion_blur:
 * @self: an #LrgGifRecorder.
 * @samples: Number of sub-frames to average per output frame.  Values <= 1
 *   disable motion blur (the default).  When > 1 a #GrlImageAccumulator is
 *   created lazily on the first call to lrg_gif_recorder_begin_frame().
 *
 * Enables or disables motion blur.  When enabled, callers use the
 * begin_frame / add_subframe / end_frame triplet instead of add_frame for
 * each output frame.  Each output frame is the linear-light average of
 * @samples sub-frames.
 *
 * Must be called before the first frame is added.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gif_recorder_set_motion_blur (LrgGifRecorder *self,
                                   gint            samples);

/* ==========================================================================
 * Frame Recording
 * ========================================================================== */

/**
 * lrg_gif_recorder_add_frame:
 * @self: an #LrgGifRecorder.
 * @frame: a #GrlImage containing the frame to append.  If its dimensions
 *   differ from the recorder canvas, graylib scales it automatically.
 * @error: (nullable): Return location for a #GError, or %NULL.
 *
 * Appends a single fully-rendered frame to the GIF.  Use this function when
 * motion blur is disabled; use the begin_frame / add_subframe / end_frame
 * triplet when motion blur is enabled.
 *
 * Returns: %TRUE on success, %FALSE on error.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gif_recorder_add_frame (LrgGifRecorder  *self,
                             GrlImage        *frame,
                             GError         **error);

/**
 * lrg_gif_recorder_capture_frame:
 * @self: an #LrgGifRecorder.
 * @error: (nullable): Return location for a #GError, or %NULL.
 *
 * Convenience function: captures the current GL frame buffer with
 * grl_image_new_from_screen() and appends it as the next frame.
 *
 * A live OpenGL context (i.e., an open #GrlWindow) must be present when
 * this function is called.  Use lrg_gif_recorder_add_frame() to record
 * frames from an off-screen source or in a headless context.
 *
 * Returns: %TRUE on success, %FALSE on error.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gif_recorder_capture_frame (LrgGifRecorder  *self,
                                 GError         **error);

/* ==========================================================================
 * Motion-Blur Sub-Frame Flow
 * ========================================================================== */

/**
 * lrg_gif_recorder_begin_frame:
 * @self: an #LrgGifRecorder.
 *
 * Resets the internal #GrlImageAccumulator, preparing it to receive
 * sub-frames for the next output frame.  Must be paired with a subsequent
 * call to lrg_gif_recorder_end_frame().
 *
 * If motion blur is disabled (samples <= 1), this function still resets the
 * accumulator so that lrg_gif_recorder_end_frame() can resolve a single
 * sub-frame identically to lrg_gif_recorder_add_frame().
 *
 * The accumulator is created lazily on the first call to this function.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gif_recorder_begin_frame (LrgGifRecorder *self);

/**
 * lrg_gif_recorder_add_subframe:
 * @self: an #LrgGifRecorder.
 * @subframe: a #GrlImage sub-frame to accumulate with equal weight 1.0.
 *
 * Adds @subframe to the accumulation buffer.  Call this function @samples
 * times between lrg_gif_recorder_begin_frame() and
 * lrg_gif_recorder_end_frame() to blend multiple renders into one output
 * frame.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gif_recorder_add_subframe (LrgGifRecorder *self,
                                GrlImage       *subframe);

/**
 * lrg_gif_recorder_end_frame:
 * @self: an #LrgGifRecorder.
 * @error: (nullable): Return location for a #GError, or %NULL.
 *
 * Resolves the accumulated sub-frames into a single averaged image and
 * appends it to the GIF.  Must be called after lrg_gif_recorder_begin_frame()
 * and one or more calls to lrg_gif_recorder_add_subframe().
 *
 * If no sub-frames were added (total weight zero), this function does
 * nothing and returns %TRUE.
 *
 * Returns: %TRUE on success, %FALSE on error.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gif_recorder_end_frame (LrgGifRecorder  *self,
                             GError         **error);

/* ==========================================================================
 * Finish
 * ========================================================================== */

/**
 * lrg_gif_recorder_finish:
 * @self: an #LrgGifRecorder.
 * @error: (nullable): Return location for a #GError, or %NULL.
 *
 * Writes the GIF trailer and closes the output file.  This function is
 * idempotent: calling it more than once returns %TRUE without touching the
 * file on subsequent calls.
 *
 * The GIF file is also closed during finalization if lrg_gif_recorder_finish()
 * has not been called explicitly.
 *
 * Returns: %TRUE on success, %FALSE on error.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gif_recorder_finish (LrgGifRecorder  *self,
                          GError         **error);

/* ==========================================================================
 * Accessors
 * ========================================================================== */

/**
 * lrg_gif_recorder_get_frame_count:
 * @self: an #LrgGifRecorder.
 *
 * Returns the number of output frames that have been appended to the GIF so
 * far.
 *
 * Returns: the frame count.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gif_recorder_get_frame_count (LrgGifRecorder *self);

G_END_DECLS
