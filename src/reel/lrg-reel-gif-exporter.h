/* lrg-reel-gif-exporter.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelGifExporter - reel exporter that writes an animated GIF file.
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

#define LRG_TYPE_REEL_GIF_EXPORTER (lrg_reel_gif_exporter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelGifExporter, lrg_reel_gif_exporter,
                      LRG, REEL_GIF_EXPORTER, LrgReelExporter)

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
LRG_AVAILABLE_IN_ALL
LrgReelGifExporter *
lrg_reel_gif_exporter_new (const gchar  *path,
                            GError      **error);

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
LRG_AVAILABLE_IN_ALL
void
lrg_reel_gif_exporter_set_quality (LrgReelGifExporter *self,
                                    gboolean            adaptive_palette,
                                    gboolean            dither);

G_END_DECLS
