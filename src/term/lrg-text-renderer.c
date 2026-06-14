/* lrg-text-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-text-renderer.h"

G_DEFINE_INTERFACE (LrgTextRenderer, lrg_text_renderer, G_TYPE_OBJECT)

static void
lrg_text_renderer_default_init (LrgTextRendererInterface *iface)
{
    (void) iface;
}

void
lrg_text_renderer_draw_glyph (LrgTextRenderer   *self,
                              LrgGlyphAtlas     *atlas,
                              const LrgGlyphKey *key,
                              gfloat             x,
                              gfloat             y,
                              const GrlColor    *fg)
{
    LrgTextRendererInterface *iface;

    g_return_if_fail (LRG_IS_TEXT_RENDERER (self));

    iface = LRG_TEXT_RENDERER_GET_IFACE (self);
    if (iface->draw_glyph != NULL)
        iface->draw_glyph (self, atlas, key, x, y, fg);
}
