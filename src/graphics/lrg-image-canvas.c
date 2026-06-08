/* lrg-image-canvas.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgImageCanvas - CPU-side dynamic texture canvas.
 */

#include "config.h"

#define LRG_LOG_DOMAIN "Libregnum-Graphics"
#include "lrg-image-canvas.h"
#include "../lrg-log.h"

struct _LrgImageCanvas
{
    GObject parent_instance;

    GrlImage *image;
};

G_DEFINE_TYPE (LrgImageCanvas, lrg_image_canvas, G_TYPE_OBJECT)

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_image_canvas_finalize (GObject *object)
{
    LrgImageCanvas *self = LRG_IMAGE_CANVAS (object);

    g_clear_object (&self->image);

    G_OBJECT_CLASS (lrg_image_canvas_parent_class)->finalize (object);
}

static void
lrg_image_canvas_class_init (LrgImageCanvasClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_image_canvas_finalize;
}

static void
lrg_image_canvas_init (LrgImageCanvas *self)
{
    self->image = NULL;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgImageCanvas *
lrg_image_canvas_new (gint             width,
                      gint             height,
                      const GrlColor  *bg)
{
    LrgImageCanvas *self;
    GrlColor       *transparent;

    g_return_val_if_fail (width > 0, NULL);
    g_return_val_if_fail (height > 0, NULL);

    self = g_object_new (LRG_TYPE_IMAGE_CANVAS, NULL);

    if (bg != NULL)
    {
        self->image = grl_image_new_color (width, height, bg);
    }
    else
    {
        /*
         * Create a fully transparent background.  A zero-alpha black is used
         * so that Porter-Duff OVER operations start from nothing.
         */
        transparent = grl_color_new (0, 0, 0, 0);
        self->image = grl_image_new_color (width, height, transparent);
        grl_color_free (transparent);
    }

    if (self->image == NULL)
    {
        lrg_log_debug ("Failed to create backing image (%dx%d)", width, height);
        g_object_unref (self);
        return NULL;
    }

    return self;
}

LrgImageCanvas *
lrg_image_canvas_new_for_image (GrlImage *image)
{
    LrgImageCanvas *self;

    g_return_val_if_fail (GRL_IS_IMAGE (image), NULL);

    self = g_object_new (LRG_TYPE_IMAGE_CANVAS, NULL);
    self->image = g_object_ref (image);

    return self;
}

/* ==========================================================================
 * Access
 * ========================================================================== */

GrlImage *
lrg_image_canvas_get_image (LrgImageCanvas *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_CANVAS (self), NULL);

    return self->image;
}

GrlTexture *
lrg_image_canvas_to_texture (LrgImageCanvas *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_CANVAS (self), NULL);

    if (self->image == NULL)
        return NULL;

    return grl_texture_new_from_image (self->image);
}

/* ==========================================================================
 * State Setters
 * ========================================================================== */

void
lrg_image_canvas_set_blend_mode (LrgImageCanvas    *self,
                                  GrlImageBlendMode  mode)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);

    grl_image_set_blend_mode (self->image, mode);
}

void
lrg_image_canvas_set_blend_color_space (LrgImageCanvas     *self,
                                         GrlImageColorSpace  color_space)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);

    grl_image_set_blend_color_space (self->image, color_space);
}

void
lrg_image_canvas_set_antialias (LrgImageCanvas *self,
                                 gboolean        enabled)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);

    grl_image_set_antialias (self->image, enabled);
}

void
lrg_image_canvas_set_clip_rect (LrgImageCanvas      *self,
                                 const GrlRectangle  *rect)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);

    grl_image_set_clip_rect (self->image, rect);
}

/* ==========================================================================
 * Transform Stack
 * ========================================================================== */

void
lrg_image_canvas_save (LrgImageCanvas *self)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);

    grl_image_push_matrix (self->image);
}

void
lrg_image_canvas_restore (LrgImageCanvas *self)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);

    grl_image_pop_matrix (self->image);
}

void
lrg_image_canvas_translate (LrgImageCanvas *self,
                              gfloat          tx,
                              gfloat          ty)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);

    grl_image_translate (self->image, tx, ty);
}

void
lrg_image_canvas_scale (LrgImageCanvas *self,
                         gfloat          sx,
                         gfloat          sy)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);

    grl_image_scale (self->image, sx, sy);
}

void
lrg_image_canvas_rotate (LrgImageCanvas *self,
                          gfloat          radians)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);

    grl_image_rotate_matrix (self->image, radians);
}

void
lrg_image_canvas_reset_transform (LrgImageCanvas *self)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);

    grl_image_reset_matrix (self->image);
}

/* ==========================================================================
 * Drawing Primitives
 * ========================================================================== */

void
lrg_image_canvas_clear (LrgImageCanvas *self,
                          const GrlColor *color)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (color != NULL);

    grl_image_clear_background (self->image, color);
}

void
lrg_image_canvas_fill_circle (LrgImageCanvas *self,
                               gint            cx,
                               gint            cy,
                               gint            radius,
                               const GrlColor *color)
{
    g_autoptr(GrlPath) path = NULL;

    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (color != NULL);

    /* Fill via a path so the circle honours the transform stack and blend mode
     * (grl_image_draw_circle is the raylib-backed overwrite wrapper, which
     * respects neither). The flattened path also transforms under shear/rotate,
     * not just translate/scale. */
    path = grl_path_new ();
    grl_path_add_circle (path, (gfloat)cx, (gfloat)cy, (gfloat)radius);
    grl_image_fill_path (self->image, path, GRL_FILL_RULE_NONZERO, color);
}

void
lrg_image_canvas_stroke_circle (LrgImageCanvas *self,
                                  gint            cx,
                                  gint            cy,
                                  gint            radius,
                                  gint            thickness,
                                  const GrlColor *color)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (color != NULL);

    grl_image_draw_circle_lines (self->image, cx, cy, radius, thickness, color);
}

void
lrg_image_canvas_fill_rect (LrgImageCanvas *self,
                              gint            x,
                              gint            y,
                              gint            width,
                              gint            height,
                              const GrlColor *color)
{
    GrlRectangle       rect;
    g_autoptr(GrlPath) path = NULL;

    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (color != NULL);

    rect.x      = (gfloat)x;
    rect.y      = (gfloat)y;
    rect.width  = (gfloat)width;
    rect.height = (gfloat)height;

    /* Fill via a path so the rect honours the transform stack and blend mode
     * (grl_image_draw_rectangle is the raylib-backed overwrite wrapper and
     * respects neither the CTM nor the blend mode). */
    path = grl_path_new ();
    grl_path_add_rect (path, &rect);
    grl_image_fill_path (self->image, path, GRL_FILL_RULE_NONZERO, color);
}

void
lrg_image_canvas_draw_line (LrgImageCanvas *self,
                              gint            x1,
                              gint            y1,
                              gint            x2,
                              gint            y2,
                              gint            thickness,
                              const GrlColor *color)
{
    GrlVector2 start;
    GrlVector2 end;

    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (color != NULL);

    start.x = (gfloat)x1;
    start.y = (gfloat)y1;
    end.x   = (gfloat)x2;
    end.y   = (gfloat)y2;

    grl_image_draw_line_ex (self->image, &start, &end, thickness, color);
}

void
lrg_image_canvas_fill_polygon (LrgImageCanvas   *self,
                                const GrlVector2 *points,
                                gint              n_points,
                                const GrlColor   *color)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (points != NULL);
    g_return_if_fail (n_points >= 3);
    g_return_if_fail (color != NULL);

    grl_image_draw_polygon (self->image, points, n_points, color);
}

void
lrg_image_canvas_fill_path (LrgImageCanvas *self,
                              GrlPath        *path,
                              GrlFillRule     fill_rule,
                              const GrlColor *color)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (GRL_IS_PATH (path));
    g_return_if_fail (color != NULL);

    grl_image_fill_path (self->image, path, fill_rule, color);
}

void
lrg_image_canvas_stroke_path (LrgImageCanvas *self,
                               GrlPath        *path,
                               gint            thickness,
                               const GrlColor *color)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (GRL_IS_PATH (path));
    g_return_if_fail (color != NULL);

    grl_image_stroke_path (self->image, path, thickness, color);
}

void
lrg_image_canvas_draw_text (LrgImageCanvas *self,
                              GrlImageFont   *font,
                              const gchar    *text,
                              gint            x,
                              gint            y,
                              gfloat          font_size,
                              const GrlColor *color)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (GRL_IS_IMAGE_FONT (font));
    g_return_if_fail (text != NULL);
    g_return_if_fail (color != NULL);

    grl_image_draw_text_ttf (self->image, font, text, x, y, font_size, color);
}

void
lrg_image_canvas_draw_gradient_rect (LrgImageCanvas *self,
                                      gint            x,
                                      gint            y,
                                      gint            width,
                                      gint            height,
                                      const GrlColor *color_a,
                                      const GrlColor *color_b,
                                      GrlGradientAxis axis)
{
    GrlRectangle rect;

    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (color_a != NULL);
    g_return_if_fail (color_b != NULL);

    rect.x      = (gfloat)x;
    rect.y      = (gfloat)y;
    rect.width  = (gfloat)width;
    rect.height = (gfloat)height;

    grl_image_draw_gradient_rect (self->image, &rect, color_a, color_b, axis);
}

void
lrg_image_canvas_draw_gradient_radial (LrgImageCanvas *self,
                                        gint            cx,
                                        gint            cy,
                                        gint            radius,
                                        const GrlColor *inner_color,
                                        const GrlColor *outer_color)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (inner_color != NULL);
    g_return_if_fail (outer_color != NULL);

    grl_image_draw_gradient_radial (self->image, cx, cy, radius,
                                     inner_color, outer_color);
}

/* ==========================================================================
 * Compositing
 * ========================================================================== */

void
lrg_image_canvas_composite (LrgImageCanvas   *self,
                              GrlImage         *src,
                              GrlPorterDuffOp   op,
                              gint              dx,
                              gint              dy)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (GRL_IS_IMAGE (src));

    grl_image_composite (self->image, src, op, dx, dy);
}

void
lrg_image_canvas_apply_mask (LrgImageCanvas *self,
                               GrlImage       *mask,
                               gint            ox,
                               gint            oy)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (GRL_IS_IMAGE (mask));

    grl_image_apply_mask (self->image, mask, ox, oy);
}

void
lrg_image_canvas_blur (LrgImageCanvas *self,
                        gint            radius)
{
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (radius >= 0);

    grl_image_blur_box (self->image, radius);
}

void
lrg_image_canvas_drop_shadow (LrgImageCanvas *self,
                                GrlImage       *silhouette,
                                gint            dx,
                                gint            dy,
                                gint            blur_radius,
                                const GrlColor *shadow_color)
{
    g_autoptr(GrlImage) shadow = NULL;
    gint                w;
    gint                h;

    g_return_if_fail (LRG_IS_IMAGE_CANVAS (self));
    g_return_if_fail (self->image != NULL);
    g_return_if_fail (GRL_IS_IMAGE (silhouette));
    g_return_if_fail (shadow_color != NULL);

    w = grl_image_get_width  (silhouette);
    h = grl_image_get_height (silhouette);

    /*
     * Step 1 — Create a solid shadow_color canvas the same size as the
     * silhouette.  This will become the tinted, blurred shadow layer.
     */
    shadow = grl_image_new_color (w, h, shadow_color);
    if (shadow == NULL)
    {
        lrg_log_debug ("drop_shadow: failed to allocate shadow image");
        return;
    }

    /*
     * Step 2 — Trim the solid shadow layer to the silhouette's shape using
     * GRL_PORTER_DUFF_DST_IN.  This operator computes:
     *   result = shadow_color * silhouette.alpha
     * so pixels outside the silhouette become fully transparent.
     */
    grl_image_composite (shadow, silhouette, GRL_PORTER_DUFF_DST_IN, 0, 0);

    /*
     * Step 3 — Soften the shadow.
     */
    if (blur_radius > 0)
        grl_image_blur_box (shadow, blur_radius);

    /*
     * Step 4 — Composite the shadow BEHIND the current canvas content.
     * GRL_PORTER_DUFF_DST_OVER places the source (shadow) under the
     * destination (existing canvas pixels), so the original content wins
     * wherever it is opaque.
     */
    grl_image_composite (self->image, shadow,
                          GRL_PORTER_DUFF_DST_OVER,
                          dx, dy);
}
