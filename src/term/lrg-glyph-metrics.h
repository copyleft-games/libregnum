/* lrg-glyph-metrics.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Atlas placement + glyph metrics for one cached glyph.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_GLYPH_METRICS (lrg_glyph_metrics_get_type ())

/**
 * LrgGlyphMetrics:
 *
 * Where a glyph lives in a #LrgGlyphAtlas and how to place it.
 *
 * Carries the atlas page index, the pixel rectangle within that page, the
 * normalised UV rectangle (for texture sampling), the glyph ink size, the
 * bearings (offset from the pen origin to the top-left of the ink box), the
 * horizontal advance, and whether the glyph is colour (BGRA emoji, drawn
 * untinted) versus an alpha-coverage mask (drawn tinted by the face fg).
 *
 * Since: 1.0
 */
typedef struct _LrgGlyphMetrics LrgGlyphMetrics;

LRG_AVAILABLE_IN_ALL
GType lrg_glyph_metrics_get_type (void) G_GNUC_CONST;

/**
 * lrg_glyph_metrics_new:
 * @page: atlas page index
 * @px: x of the glyph ink box within the page (pixels)
 * @py: y of the glyph ink box within the page (pixels)
 * @w: ink width (pixels)
 * @h: ink height (pixels)
 * @bearing_x: x offset from pen origin to ink left (pixels)
 * @bearing_y: y offset from pen baseline up to ink top (pixels)
 * @advance: horizontal advance (pixels)
 * @is_color: %TRUE for a colour glyph drawn untinted
 *
 * Returns: (transfer full): a new #LrgGlyphMetrics with UVs computed from the
 *   page size supplied later via lrg_glyph_metrics_set_uv(), or zeroed UVs.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgGlyphMetrics * lrg_glyph_metrics_new (guint    page,
                                         gint     px,
                                         gint     py,
                                         gint     w,
                                         gint     h,
                                         gint     bearing_x,
                                         gint     bearing_y,
                                         gint     advance,
                                         gboolean is_color);

LRG_AVAILABLE_IN_ALL
LrgGlyphMetrics * lrg_glyph_metrics_copy (const LrgGlyphMetrics *self);

LRG_AVAILABLE_IN_ALL
void lrg_glyph_metrics_free (LrgGlyphMetrics *self);

/**
 * lrg_glyph_metrics_set_uv:
 * @self: a #LrgGlyphMetrics
 * @u0: left UV
 * @v0: top UV
 * @u1: right UV
 * @v1: bottom UV
 *
 * Sets the normalised texture coordinates. Called by #LrgGlyphAtlas once the
 * glyph is placed and the page size is known.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_glyph_metrics_set_uv (LrgGlyphMetrics *self,
                               gfloat           u0,
                               gfloat           v0,
                               gfloat           u1,
                               gfloat           v1);

LRG_AVAILABLE_IN_ALL
guint lrg_glyph_metrics_get_page (const LrgGlyphMetrics *self);

LRG_AVAILABLE_IN_ALL
void lrg_glyph_metrics_get_rect (const LrgGlyphMetrics *self,
                                 gint                  *out_x,
                                 gint                  *out_y,
                                 gint                  *out_w,
                                 gint                  *out_h);

LRG_AVAILABLE_IN_ALL
void lrg_glyph_metrics_get_uv (const LrgGlyphMetrics *self,
                               gfloat                *out_u0,
                               gfloat                *out_v0,
                               gfloat                *out_u1,
                               gfloat                *out_v1);

LRG_AVAILABLE_IN_ALL
gint lrg_glyph_metrics_get_bearing_x (const LrgGlyphMetrics *self);

LRG_AVAILABLE_IN_ALL
gint lrg_glyph_metrics_get_bearing_y (const LrgGlyphMetrics *self);

LRG_AVAILABLE_IN_ALL
gint lrg_glyph_metrics_get_advance (const LrgGlyphMetrics *self);

LRG_AVAILABLE_IN_ALL
gboolean lrg_glyph_metrics_get_is_color (const LrgGlyphMetrics *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgGlyphMetrics, lrg_glyph_metrics_free)

G_END_DECLS
