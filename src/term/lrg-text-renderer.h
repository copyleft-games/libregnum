/* lrg-text-renderer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface: something that can paint atlas glyphs. Decouples the redisplay
 * glyph-drawing contract from the concrete #LrgFrameSurface, so an alternative
 * renderer (e.g. an SDF text renderer for crisp 3D text) can be swapped in.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-glyph-atlas.h"
#include "lrg-glyph-key.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEXT_RENDERER (lrg_text_renderer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgTextRenderer, lrg_text_renderer, LRG, TEXT_RENDERER, GObject)

/**
 * LrgTextRendererInterface:
 * @parent_iface: parent interface
 * @draw_glyph: paint one cached glyph at a pen position
 *
 * Contract for painting glyphs from a #LrgGlyphAtlas.
 *
 * Since: 1.0
 */
struct _LrgTextRendererInterface
{
    GTypeInterface parent_iface;

    /**
     * LrgTextRendererInterface::draw_glyph:
     * @self: a #LrgTextRenderer
     * @atlas: the glyph atlas holding @key
     * @key: identifies the glyph to paint
     * @x: pen origin x (logical pixels)
     * @y: pen baseline y (logical pixels)
     * @fg: (nullable): foreground tint for alpha-mask glyphs; ignored for
     *   colour glyphs. %NULL means white (no tint).
     *
     * Paints the glyph, applying its bearings relative to (@x, @y).
     */
    void (*draw_glyph) (LrgTextRenderer   *self,
                        LrgGlyphAtlas     *atlas,
                        const LrgGlyphKey *key,
                        gfloat             x,
                        gfloat             y,
                        const GrlColor    *fg);
};

/**
 * lrg_text_renderer_draw_glyph:
 * @self: a #LrgTextRenderer
 * @atlas: the glyph atlas holding @key
 * @key: identifies the glyph to paint
 * @x: pen origin x (logical pixels)
 * @y: pen baseline y (logical pixels)
 * @fg: (nullable): foreground tint for alpha-mask glyphs
 *
 * Calls the draw_glyph() vfunc.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_text_renderer_draw_glyph (LrgTextRenderer   *self,
                                   LrgGlyphAtlas     *atlas,
                                   const LrgGlyphKey *key,
                                   gfloat             x,
                                   gfloat             y,
                                   const GrlColor    *fg);

G_END_DECLS
