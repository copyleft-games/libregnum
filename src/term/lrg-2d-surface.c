/* lrg-2d-surface.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-2d-surface.h"
#include "lrg-text-renderer.h"

typedef struct
{
    gint x;
    gint y;
    gint w;
    gint h;
} ClipRect;

struct _Lrg2DSurface
{
    LrgFrameSurface parent_instance;
    GrlWindow      *window;
    GArray         *clip_stack;  /* ClipRect */
};

static void lrg_2d_surface_text_renderer_init (LrgTextRendererInterface *iface);

G_DEFINE_TYPE_WITH_CODE (Lrg2DSurface, lrg_2d_surface, LRG_TYPE_FRAME_SURFACE,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_TEXT_RENDERER,
                                                lrg_2d_surface_text_renderer_init))

static void
clip_intersect (const ClipRect *a,
                const ClipRect *b,
                ClipRect       *out)
{
    gint x0 = MAX (a->x, b->x);
    gint y0 = MAX (a->y, b->y);
    gint x1 = MIN (a->x + a->w, b->x + b->w);
    gint y1 = MIN (a->y + a->h, b->y + b->h);

    out->x = x0;
    out->y = y0;
    out->w = MAX (0, x1 - x0);
    out->h = MAX (0, y1 - y0);
}

static void
lrg_2d_surface_begin_frame (LrgFrameSurface *surface)
{
    Lrg2DSurface *self = LRG_2D_SURFACE (surface);
    gint ww, wh;

    if (self->window == NULL)
        return;

    /* Reconcile the surface geometry with the live window size on EVERY frame,
       not only while grl_window_is_resized () (raylib's IsWindowResized ()) is
       set.  That flag is one-shot -- cleared at the next PollInputEvents () --
       so a present that runs a cycle after the resize poll (always the case for
       the 3D surface, whose present is driven by a later redisplay) would miss
       it and keep stale dimensions: the panel/capture stay the old size while
       the GL viewport is already the new one, so the scene renders at the wrong
       size/offset ("the UI drags along on resize").  Comparing actual sizes is
       robust to that timing and is a cheap no-op when nothing changed.  */
    ww = grl_window_get_width (self->window);
    wh = grl_window_get_height (self->window);
    if (ww > 0 && wh > 0
        && (ww != lrg_frame_surface_get_width (surface)
            || wh != lrg_frame_surface_get_height (surface)))
    {
        g_autoptr(GrlVector2) dpi = grl_window_get_scale_dpi (self->window);
        gfloat scale = (dpi != NULL) ? dpi->x : 1.0f;

        lrg_frame_surface_set_geometry (surface, ww, wh, scale);
    }

    grl_window_begin_drawing (self->window);
}

static void
lrg_2d_surface_end_frame (LrgFrameSurface *surface)
{
    Lrg2DSurface *self = LRG_2D_SURFACE (surface);

    if (self->window == NULL)
        return;

    /* Present via swap_buffers (flush + SwapScreenBuffer), NOT end_drawing:
       end_drawing == EndDrawing() also calls PollInputEvents(), which would
       consume the input queues a second time per cycle and drop keystrokes.
       The lrg backend owns a single poll point in its read_socket hook
       (grl_window_poll_events).  */
    grl_window_swap_buffers (self->window);
}

static void
lrg_2d_surface_clear (LrgFrameSurface *surface,
                      const GrlColor  *color)
{
    Lrg2DSurface *self = LRG_2D_SURFACE (surface);

    if (self->window == NULL || color == NULL)
        return;

    grl_window_clear_background (self->window, color);
}

static void
lrg_2d_surface_fill_rect (LrgFrameSurface *surface,
                          gint             x,
                          gint             y,
                          gint             width,
                          gint             height,
                          const GrlColor  *color)
{
    (void) surface;

    if (color == NULL)
        return;

    grl_draw_rectangle (x, y, width, height, color);
}

static void
lrg_2d_surface_draw_rect_outline (LrgFrameSurface *surface,
                                  gint             x,
                                  gint             y,
                                  gint             width,
                                  gint             height,
                                  gfloat           thickness,
                                  const GrlColor  *color)
{
    g_autoptr(GrlRectangle) rect = NULL;

    (void) surface;

    if (color == NULL)
        return;

    rect = grl_rectangle_new (x, y, width, height);
    grl_draw_rectangle_lines_ex (rect, thickness, color);
}

static void
lrg_2d_surface_draw_line (LrgFrameSurface *surface,
                          gint             x1,
                          gint             y1,
                          gint             x2,
                          gint             y2,
                          gfloat           thickness,
                          const GrlColor  *color)
{
    g_autoptr(GrlVector2) a = NULL;
    g_autoptr(GrlVector2) b = NULL;

    (void) surface;

    if (color == NULL)
        return;

    a = grl_vector2_new ((gfloat) x1, (gfloat) y1);
    b = grl_vector2_new ((gfloat) x2, (gfloat) y2);
    grl_draw_line_ex (a, b, thickness, color);
}

static void
lrg_2d_surface_push_clip (LrgFrameSurface *surface,
                          gint             x,
                          gint             y,
                          gint             width,
                          gint             height)
{
    Lrg2DSurface *self = LRG_2D_SURFACE (surface);
    ClipRect nr;

    nr.x = x;
    nr.y = y;
    nr.w = width;
    nr.h = height;

    if (self->clip_stack->len > 0)
    {
        ClipRect top = g_array_index (self->clip_stack, ClipRect,
                                      self->clip_stack->len - 1);

        /* Only one scissor is active at a time; replace it with the
         * intersection of the parent clip and the new rect. */
        grl_draw_end_scissor_mode ();
        clip_intersect (&top, &nr, &nr);
    }

    g_array_append_val (self->clip_stack, nr);
    grl_draw_begin_scissor_mode (nr.x, nr.y, nr.w, nr.h);
}

static void
lrg_2d_surface_pop_clip (LrgFrameSurface *surface)
{
    Lrg2DSurface *self = LRG_2D_SURFACE (surface);

    if (self->clip_stack->len == 0)
        return;

    grl_draw_end_scissor_mode ();
    g_array_remove_index (self->clip_stack, self->clip_stack->len - 1);

    if (self->clip_stack->len > 0)
    {
        ClipRect top = g_array_index (self->clip_stack, ClipRect,
                                      self->clip_stack->len - 1);
        grl_draw_begin_scissor_mode (top.x, top.y, top.w, top.h);
    }
}

static void
lrg_2d_surface_draw_glyph (LrgFrameSurface   *surface,
                           LrgGlyphAtlas     *atlas,
                           const LrgGlyphKey *key,
                           gfloat             x,
                           gfloat             y,
                           const GrlColor    *fg)
{
    LrgGlyphMetrics *m;
    GrlTexture *tex;
    gint px = 0;
    gint py = 0;
    gint gw = 0;
    gint gh = 0;

    (void) surface;

    if (atlas == NULL || key == NULL)
        return;

    m = lrg_glyph_atlas_lookup (atlas, key);
    if (m == NULL)
        return;

    lrg_glyph_metrics_get_rect (m, &px, &py, &gw, &gh);
    if (gw <= 0 || gh <= 0)
        return;  /* zero-size glyph (e.g. space) */

    tex = lrg_glyph_atlas_get_page_texture (atlas, lrg_glyph_metrics_get_page (m));
    if (tex == NULL)
        return;

    {
        gint bx = lrg_glyph_metrics_get_bearing_x (m);
        gint by = lrg_glyph_metrics_get_bearing_y (m);
        g_autoptr(GrlRectangle) src = grl_rectangle_new (px, py, gw, gh);
        g_autoptr(GrlRectangle) dst =
            grl_rectangle_new (x + (gfloat) bx, y - (gfloat) by,
                               (gfloat) gw, (gfloat) gh);
        g_autoptr(GrlVector2) origin = grl_vector2_new (0.0f, 0.0f);
        g_autoptr(GrlColor) white = NULL;
        const GrlColor *tint;

        if (lrg_glyph_metrics_get_is_color (m) || fg == NULL)
        {
            white = grl_color_new_white ();
            tint = white;
        }
        else
        {
            tint = fg;
        }

        grl_draw_begin_blend_mode (GRL_BLEND_ALPHA);
        grl_draw_texture_pro (tex, src, dst, origin, 0.0f, tint);
        grl_draw_end_blend_mode ();
    }
}

static void
lrg_2d_surface_draw_texture_region (LrgFrameSurface    *surface,
                                    GrlTexture         *texture,
                                    const GrlRectangle *src,
                                    gfloat              dx,
                                    gfloat              dy,
                                    gfloat              dw,
                                    gfloat              dh,
                                    const GrlColor     *tint)
{
    g_autoptr(GrlRectangle) dst = NULL;
    g_autoptr(GrlVector2) origin = NULL;
    g_autoptr(GrlColor) white = NULL;
    const GrlColor *t = tint;

    (void) surface;

    if (texture == NULL || src == NULL)
        return;

    dst = grl_rectangle_new (dx, dy, dw, dh);
    origin = grl_vector2_new (0.0f, 0.0f);
    if (t == NULL)
    {
        white = grl_color_new_white ();
        t = white;
    }

    grl_draw_begin_blend_mode (GRL_BLEND_ALPHA);
    grl_draw_texture_pro (texture, src, dst, origin, 0.0f, t);
    grl_draw_end_blend_mode ();
}

static GrlWindow *
lrg_2d_surface_real_get_window (LrgFrameSurface *surface)
{
    return LRG_2D_SURFACE (surface)->window;
}

static void
lrg_2d_surface_text_renderer_draw_glyph (LrgTextRenderer   *renderer,
                                         LrgGlyphAtlas     *atlas,
                                         const LrgGlyphKey *key,
                                         gfloat             x,
                                         gfloat             y,
                                         const GrlColor    *fg)
{
    lrg_frame_surface_draw_glyph (LRG_FRAME_SURFACE (renderer),
                                  atlas, key, x, y, fg);
}

static void
lrg_2d_surface_text_renderer_init (LrgTextRendererInterface *iface)
{
    iface->draw_glyph = lrg_2d_surface_text_renderer_draw_glyph;
}

Lrg2DSurface *
lrg_2d_surface_new (gint         width,
                    gint         height,
                    const gchar *title)
{
    Lrg2DSurface *self = g_object_new (LRG_TYPE_2D_SURFACE, NULL);

    g_return_val_if_fail (width > 0 && height > 0, self);

    self->window = grl_window_new (width, height,
                                   title != NULL ? title : "cmacs");
    lrg_frame_surface_set_render_mode (LRG_FRAME_SURFACE (self),
                                       LRG_RENDER_MODE_2D);

    if (self->window != NULL)
    {
        g_autoptr(GrlVector2) dpi = grl_window_get_scale_dpi (self->window);
        gfloat scale = (dpi != NULL) ? dpi->x : 1.0f;

        lrg_frame_surface_set_geometry (LRG_FRAME_SURFACE (self),
                                        grl_window_get_width (self->window),
                                        grl_window_get_height (self->window),
                                        scale);
    }
    else
    {
        lrg_frame_surface_set_geometry (LRG_FRAME_SURFACE (self),
                                        width, height, 1.0f);
    }

    return self;
}

GrlWindow *
lrg_2d_surface_get_window (Lrg2DSurface *self)
{
    g_return_val_if_fail (LRG_IS_2D_SURFACE (self), NULL);
    return self->window;
}

static void
lrg_2d_surface_finalize (GObject *object)
{
    Lrg2DSurface *self = LRG_2D_SURFACE (object);

    g_clear_object (&self->window);
    g_clear_pointer (&self->clip_stack, g_array_unref);

    G_OBJECT_CLASS (lrg_2d_surface_parent_class)->finalize (object);
}

static void
lrg_2d_surface_class_init (Lrg2DSurfaceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgFrameSurfaceClass *surface_class = LRG_FRAME_SURFACE_CLASS (klass);

    object_class->finalize = lrg_2d_surface_finalize;

    surface_class->begin_frame = lrg_2d_surface_begin_frame;
    surface_class->end_frame = lrg_2d_surface_end_frame;
    surface_class->clear = lrg_2d_surface_clear;
    surface_class->fill_rect = lrg_2d_surface_fill_rect;
    surface_class->draw_rect_outline = lrg_2d_surface_draw_rect_outline;
    surface_class->draw_line = lrg_2d_surface_draw_line;
    surface_class->push_clip = lrg_2d_surface_push_clip;
    surface_class->pop_clip = lrg_2d_surface_pop_clip;
    surface_class->draw_glyph = lrg_2d_surface_draw_glyph;
    surface_class->draw_texture_region = lrg_2d_surface_draw_texture_region;
    surface_class->get_window = lrg_2d_surface_real_get_window;
    /* pick: inherit base identity mapping */
}

static void
lrg_2d_surface_init (Lrg2DSurface *self)
{
    self->window = NULL;
    self->clip_stack = g_array_new (FALSE, FALSE, sizeof (ClipRect));
}
