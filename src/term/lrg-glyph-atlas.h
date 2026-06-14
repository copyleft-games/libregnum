/* lrg-glyph-atlas.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * GPU glyph cache: shelf-packed RGBA atlas pages keyed by #LrgGlyphKey.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-glyph-key.h"
#include "lrg-glyph-metrics.h"

G_BEGIN_DECLS

#define LRG_TYPE_GLYPH_ATLAS (lrg_glyph_atlas_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGlyphAtlas, lrg_glyph_atlas, LRG, GLYPH_ATLAS, GObject)

/**
 * lrg_glyph_atlas_new:
 * @page_width: width of each atlas page texture in pixels (e.g. 1024)
 * @page_height: height of each atlas page texture in pixels
 *
 * Creates an empty glyph atlas. Pages and their GPU textures are allocated
 * lazily on the first upload that lands on them, so this constructor needs no
 * GL context (placement via lrg_glyph_atlas_reserve() is GL-free too).
 *
 * Returns: (transfer full): a new #LrgGlyphAtlas
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgGlyphAtlas * lrg_glyph_atlas_new (gint page_width,
                                     gint page_height);

/**
 * lrg_glyph_atlas_reserve:
 * @self: a #LrgGlyphAtlas
 * @width: glyph ink width in pixels
 * @height: glyph ink height in pixels
 * @out_page: (out): return location for the chosen page index
 * @out_x: (out): return location for the x of the placement within the page
 * @out_y: (out): return location for the y of the placement within the page
 *
 * Runs the shelf packer to choose a slot for a @width x @height glyph, growing
 * a new page if needed (emitting #LrgGlyphAtlas::page-added). This performs NO
 * GPU work, so it is safe to call headless and is the unit-testable core of the
 * packer.
 *
 * Returns: %TRUE on success, %FALSE if the glyph is larger than a whole page
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_glyph_atlas_reserve (LrgGlyphAtlas *self,
                                  gint           width,
                                  gint           height,
                                  guint         *out_page,
                                  gint          *out_x,
                                  gint          *out_y);

/**
 * lrg_glyph_atlas_upload:
 * @self: a #LrgGlyphAtlas
 * @key: the glyph key (copied)
 * @pixels: (nullable) (array): raw RGBA8 pixels, @width*@height*4 bytes, or
 *   %NULL for a zero-size (e.g. space) glyph
 * @width: ink width in pixels
 * @height: ink height in pixels
 * @bearing_x: x offset from pen origin to ink left
 * @bearing_y: y offset from pen baseline up to ink top
 * @advance: horizontal advance in pixels
 * @is_color: %TRUE for a colour glyph (drawn untinted), %FALSE for an
 *   alpha-coverage mask (drawn tinted by the face foreground)
 *
 * Packs the glyph into an atlas page, uploads its pixels to the page texture
 * (creating the texture on first use -- this step needs a GL context), records
 * its #LrgGlyphMetrics, and caches it under @key. If @key is already present the
 * existing metrics are returned unchanged.
 *
 * Returns: (transfer none): the cached #LrgGlyphMetrics owned by the atlas, or
 *   %NULL on failure
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgGlyphMetrics * lrg_glyph_atlas_upload (LrgGlyphAtlas     *self,
                                          const LrgGlyphKey *key,
                                          const guint8      *pixels,
                                          gint               width,
                                          gint               height,
                                          gint               bearing_x,
                                          gint               bearing_y,
                                          gint               advance,
                                          gboolean           is_color);

/**
 * lrg_glyph_atlas_lookup:
 * @self: a #LrgGlyphAtlas
 * @key: the glyph key
 *
 * Returns: (transfer none) (nullable): the cached #LrgGlyphMetrics, or %NULL if
 *   the glyph is not cached
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgGlyphMetrics * lrg_glyph_atlas_lookup (LrgGlyphAtlas     *self,
                                          const LrgGlyphKey *key);

/**
 * lrg_glyph_atlas_get_page_texture:
 * @self: a #LrgGlyphAtlas
 * @page: a page index
 *
 * Returns: (transfer none) (nullable): the page's #GrlTexture, or %NULL if the
 *   page index is out of range or nothing has been uploaded to it yet
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_glyph_atlas_get_page_texture (LrgGlyphAtlas *self,
                                               guint          page);

LRG_AVAILABLE_IN_ALL
guint lrg_glyph_atlas_get_page_count (LrgGlyphAtlas *self);

LRG_AVAILABLE_IN_ALL
guint lrg_glyph_atlas_get_glyph_count (LrgGlyphAtlas *self);

LRG_AVAILABLE_IN_ALL
gint lrg_glyph_atlas_get_page_width (LrgGlyphAtlas *self);

LRG_AVAILABLE_IN_ALL
gint lrg_glyph_atlas_get_page_height (LrgGlyphAtlas *self);

/**
 * lrg_glyph_atlas_evict_font:
 * @self: a #LrgGlyphAtlas
 * @font_id: the font identity whose glyphs should be dropped
 *
 * Removes all cached glyphs belonging to @font_id. Atlas page space is not
 * reclaimed (the freed shelf area is not reused), but the entries are gone so a
 * re-render re-uploads them; call when a font is closed.
 *
 * Returns: the number of glyphs removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_glyph_atlas_evict_font (LrgGlyphAtlas *self,
                                  guint64        font_id);

G_END_DECLS
