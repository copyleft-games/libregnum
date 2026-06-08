/* lrg-reel-exporter.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelExporter - abstract sink for rendered frames.
 *
 * An exporter consumes a stream of rendered #GrlImage frames: begin() once,
 * then add_frame() per frame in order, then finish() once.  Concrete
 * subclasses write animated GIFs, image sequences, or muxed video.
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

#define LRG_TYPE_REEL_EXPORTER (lrg_reel_exporter_get_type ())

/**
 * LRG_REEL_EXPORTER_ERROR:
 *
 * Error domain for reel exporters.
 *
 * Since: 1.0
 */
#define LRG_REEL_EXPORTER_ERROR (lrg_reel_exporter_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_reel_exporter_error_quark (void);

/**
 * LrgReelExporterError:
 * @LRG_REEL_EXPORTER_ERROR_FAILED: generic failure.
 * @LRG_REEL_EXPORTER_ERROR_OPEN: could not open/create the output.
 * @LRG_REEL_EXPORTER_ERROR_WRITE: failed while writing a frame.
 * @LRG_REEL_EXPORTER_ERROR_FFMPEG_NOT_FOUND: the ffmpeg program was not found.
 * @LRG_REEL_EXPORTER_ERROR_SPAWN: failed to spawn or run the encoder.
 * @LRG_REEL_EXPORTER_ERROR_UNSUPPORTED: operation not implemented by the sink.
 *
 * Error codes for #LrgReelExporter.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_REEL_EXPORTER_ERROR_FAILED,
    LRG_REEL_EXPORTER_ERROR_OPEN,
    LRG_REEL_EXPORTER_ERROR_WRITE,
    LRG_REEL_EXPORTER_ERROR_FFMPEG_NOT_FOUND,
    LRG_REEL_EXPORTER_ERROR_SPAWN,
    LRG_REEL_EXPORTER_ERROR_UNSUPPORTED
} LrgReelExporterError;

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgReelExporter, lrg_reel_exporter, LRG, REEL_EXPORTER, GObject)

/**
 * LrgReelExporterClass:
 * @parent_class: the parent class.
 * @begin: open the sink for a stream of @width x @height frames at @fps.
 * @add_frame: append one frame.
 * @finish: flush and close the sink.
 *
 * Class structure for #LrgReelExporter.
 *
 * Since: 1.0
 */
struct _LrgReelExporterClass
{
    GObjectClass parent_class;

    gboolean (*begin)     (LrgReelExporter *self,
                           gint             width,
                           gint             height,
                           gdouble          fps,
                           GError         **error);
    gboolean (*add_frame) (LrgReelExporter *self,
                           GrlImage        *frame,
                           GError         **error);
    gboolean (*finish)    (LrgReelExporter *self,
                           GError         **error);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_reel_exporter_begin:
 * @self: a #LrgReelExporter
 * @width: frame width in pixels.
 * @height: frame height in pixels.
 * @fps: frames per second.
 * @error: (nullable): return location for a #GError.
 *
 * Opens the sink.  Called once before any frames.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_exporter_begin (LrgReelExporter *self,
                         gint             width,
                         gint             height,
                         gdouble          fps,
                         GError         **error);

/**
 * lrg_reel_exporter_add_frame:
 * @self: a #LrgReelExporter
 * @frame: the rendered frame (RGBA8 #GrlImage).
 * @error: (nullable): return location for a #GError.
 *
 * Appends one frame.  Frames must be added in order between begin() and
 * finish().
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_exporter_add_frame (LrgReelExporter *self,
                             GrlImage        *frame,
                             GError         **error);

/**
 * lrg_reel_exporter_finish:
 * @self: a #LrgReelExporter
 * @error: (nullable): return location for a #GError.
 *
 * Flushes and closes the sink.  Called once after the last frame.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_exporter_finish (LrgReelExporter *self,
                          GError         **error);

G_END_DECLS
