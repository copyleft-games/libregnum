/* lrg-frame-surface.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract draw + present + projection facade for a display frame.
 *
 * This is the single seam that makes 3D/VR additive: the redisplay glue only
 * ever talks to this base vtable, so a new render mode is a new subclass plus a
 * new #LrgRenderMode value -- no glue changes. #Lrg2DSurface is the only concrete
 * implementation today; #Lrg3DSurface / #LrgVRSurface are future subclasses that
 * reuse the same #LrgGlyphAtlas, the #LrgTextRenderer interface, and this vtable.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-render-mode.h"
#include "lrg-glyph-atlas.h"
#include "lrg-glyph-key.h"

G_BEGIN_DECLS

#define LRG_TYPE_FRAME_SURFACE (lrg_frame_surface_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgFrameSurface, lrg_frame_surface, LRG, FRAME_SURFACE, GObject)

/**
 * LrgFrameSurfaceClass:
 * @parent_class: parent class
 * @begin_frame: start a frame (bind/clear-ready the target)
 * @end_frame: finish + present a frame
 * @begin_content: 3D modes: begin the flat content-capture pass (no-op in 2D)
 * @end_content: 3D modes: finish + cache the content pass (no-op in 2D)
 * @clear: clear the whole surface to a colour
 * @fill_rect: fill an axis-aligned rectangle
 * @draw_rect_outline: stroke a rectangle outline
 * @draw_line: draw a line segment
 * @push_clip: push an intersecting clip rectangle
 * @pop_clip: pop the last clip rectangle
 * @draw_glyph: paint a cached glyph at a pen position
 * @draw_texture_region: blit a texture sub-rect (inline images)
 * @pick: map a device point to logical coordinates (default: identity)
 *
 * Virtual table every render mode implements. Coordinates are logical pixels
 * (the 2D mode maps them 1:1 to screen pixels).
 *
 * Since: 1.0
 */
struct _LrgFrameSurfaceClass
{
    GObjectClass parent_class;

    /*< public >*/
    void     (*begin_frame)         (LrgFrameSurface    *self);
    void     (*end_frame)           (LrgFrameSurface    *self);
    void     (*begin_content)       (LrgFrameSurface    *self);
    void     (*end_content)         (LrgFrameSurface    *self);
    void     (*clear)               (LrgFrameSurface    *self,
                                     const GrlColor     *color);
    void     (*fill_rect)           (LrgFrameSurface    *self,
                                     gint                x,
                                     gint                y,
                                     gint                width,
                                     gint                height,
                                     const GrlColor     *color);
    void     (*draw_rect_outline)   (LrgFrameSurface    *self,
                                     gint                x,
                                     gint                y,
                                     gint                width,
                                     gint                height,
                                     gfloat              thickness,
                                     const GrlColor     *color);
    void     (*draw_line)           (LrgFrameSurface    *self,
                                     gint                x1,
                                     gint                y1,
                                     gint                x2,
                                     gint                y2,
                                     gfloat              thickness,
                                     const GrlColor     *color);
    void     (*push_clip)           (LrgFrameSurface    *self,
                                     gint                x,
                                     gint                y,
                                     gint                width,
                                     gint                height);
    void     (*pop_clip)            (LrgFrameSurface    *self);
    void     (*draw_glyph)          (LrgFrameSurface    *self,
                                     LrgGlyphAtlas      *atlas,
                                     const LrgGlyphKey  *key,
                                     gfloat              x,
                                     gfloat              y,
                                     const GrlColor     *fg);
    void     (*draw_texture_region) (LrgFrameSurface    *self,
                                     GrlTexture         *texture,
                                     const GrlRectangle *src,
                                     gfloat              dx,
                                     gfloat              dy,
                                     gfloat              dw,
                                     gfloat              dh,
                                     const GrlColor     *tint);
    gboolean (*pick)                (LrgFrameSurface    *self,
                                     gfloat              px,
                                     gfloat              py,
                                     gfloat             *out_x,
                                     gfloat             *out_y);
    GrlWindow * (*get_window)       (LrgFrameSurface    *self);

    /*< private >*/
    gpointer _reserved[5];
};

/* Frame lifecycle */
LRG_AVAILABLE_IN_ALL
void lrg_frame_surface_begin_frame (LrgFrameSurface *self);
LRG_AVAILABLE_IN_ALL
void lrg_frame_surface_end_frame (LrgFrameSurface *self);

/* Content-capture pass (3D modes capture the flat frame between these; 2D and
   any surface that does not override them treat both as no-ops).  */
LRG_AVAILABLE_IN_ALL
void lrg_frame_surface_begin_content (LrgFrameSurface *self);
LRG_AVAILABLE_IN_ALL
void lrg_frame_surface_end_content (LrgFrameSurface *self);

/* Primitives */
LRG_AVAILABLE_IN_ALL
void lrg_frame_surface_clear (LrgFrameSurface *self,
                              const GrlColor  *color);
LRG_AVAILABLE_IN_ALL
void lrg_frame_surface_fill_rect (LrgFrameSurface *self,
                                  gint             x,
                                  gint             y,
                                  gint             width,
                                  gint             height,
                                  const GrlColor  *color);
LRG_AVAILABLE_IN_ALL
void lrg_frame_surface_draw_rect_outline (LrgFrameSurface *self,
                                          gint             x,
                                          gint             y,
                                          gint             width,
                                          gint             height,
                                          gfloat           thickness,
                                          const GrlColor  *color);
LRG_AVAILABLE_IN_ALL
void lrg_frame_surface_draw_line (LrgFrameSurface *self,
                                  gint             x1,
                                  gint             y1,
                                  gint             x2,
                                  gint             y2,
                                  gfloat           thickness,
                                  const GrlColor  *color);

/* Clipping */
LRG_AVAILABLE_IN_ALL
void lrg_frame_surface_push_clip (LrgFrameSurface *self,
                                  gint             x,
                                  gint             y,
                                  gint             width,
                                  gint             height);
LRG_AVAILABLE_IN_ALL
void lrg_frame_surface_pop_clip (LrgFrameSurface *self);

/* Text + images */
LRG_AVAILABLE_IN_ALL
void lrg_frame_surface_draw_glyph (LrgFrameSurface   *self,
                                   LrgGlyphAtlas     *atlas,
                                   const LrgGlyphKey *key,
                                   gfloat             x,
                                   gfloat             y,
                                   const GrlColor    *fg);
LRG_AVAILABLE_IN_ALL
void lrg_frame_surface_draw_texture_region (LrgFrameSurface    *self,
                                            GrlTexture         *texture,
                                            const GrlRectangle *src,
                                            gfloat              dx,
                                            gfloat              dy,
                                            gfloat              dw,
                                            gfloat              dh,
                                            const GrlColor     *tint);

/* Input projection */
LRG_AVAILABLE_IN_ALL
gboolean lrg_frame_surface_pick (LrgFrameSurface *self,
                                 gfloat           px,
                                 gfloat           py,
                                 gfloat          *out_x,
                                 gfloat          *out_y);

/* State */
LRG_AVAILABLE_IN_ALL
gint lrg_frame_surface_get_width (LrgFrameSurface *self);
LRG_AVAILABLE_IN_ALL
gint lrg_frame_surface_get_height (LrgFrameSurface *self);
LRG_AVAILABLE_IN_ALL
gfloat lrg_frame_surface_get_scale (LrgFrameSurface *self);
LRG_AVAILABLE_IN_ALL
LrgRenderMode lrg_frame_surface_get_render_mode (LrgFrameSurface *self);

/**
 * lrg_frame_surface_get_window:
 * @self: a #LrgFrameSurface
 *
 * Returns: (transfer none) (nullable): the #GrlWindow backing this surface (for
 *   input polling / native integration by the embedding backend), or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlWindow * lrg_frame_surface_get_window (LrgFrameSurface *self);

/**
 * lrg_frame_surface_set_geometry: (skip)
 * @self: a #LrgFrameSurface
 * @width: new logical width
 * @height: new logical height
 * @scale: new DPI scale factor
 *
 * Protected: for subclasses to record the current size/scale. Emits
 * #LrgFrameSurface::resized and notifies the width/height/scale properties when
 * the values change.
 */
LRG_AVAILABLE_IN_ALL
void lrg_frame_surface_set_geometry (LrgFrameSurface *self,
                                     gint             width,
                                     gint             height,
                                     gfloat           scale);

/**
 * lrg_frame_surface_set_render_mode: (skip)
 * @self: a #LrgFrameSurface
 * @mode: the mode this surface implements
 *
 * Protected: for subclasses to declare their render mode at construction.
 */
LRG_AVAILABLE_IN_ALL
void lrg_frame_surface_set_render_mode (LrgFrameSurface *self,
                                        LrgRenderMode    mode);

G_END_DECLS
