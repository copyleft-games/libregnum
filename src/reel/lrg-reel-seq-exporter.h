/* lrg-reel-seq-exporter.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelSeqExporter - reel exporter that writes a numbered image sequence.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-reel-exporter.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_SEQ_EXPORTER (lrg_reel_seq_exporter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelSeqExporter, lrg_reel_seq_exporter,
                      LRG, REEL_SEQ_EXPORTER, LrgReelExporter)

/**
 * lrg_reel_seq_exporter_new:
 * @dir: (type filename): the output directory; created with g_mkdir_with_parents()
 *   if it does not yet exist.
 * @pattern: a printf integer format string (without extension), e.g.
 *   %<literal>"frame_%05d"</literal>.  The frame index is substituted at
 *   each call to lrg_reel_exporter_add_frame().
 * @format: the image format to write (%LRG_REEL_IMAGE_FORMAT_PNG or
 *   %LRG_REEL_IMAGE_FORMAT_JPEG).
 *
 * Creates a new #LrgReelSeqExporter.  Each frame is written as a separate
 * image file inside @dir.  The output directory is not created until
 * lrg_reel_exporter_begin() is called.
 *
 * Returns: (transfer full): a new #LrgReelSeqExporter.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelSeqExporter *
lrg_reel_seq_exporter_new (const gchar        *dir,
                            const gchar        *pattern,
                            LrgReelImageFormat  format);

/**
 * lrg_reel_seq_exporter_get_frame_count:
 * @self: an #LrgReelSeqExporter.
 *
 * Returns the number of frames that have been written so far.  This counter
 * is reset to zero each time lrg_reel_exporter_begin() is called.
 *
 * Returns: the number of frames written.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_reel_seq_exporter_get_frame_count (LrgReelSeqExporter *self);

G_END_DECLS
