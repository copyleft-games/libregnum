/* lrg-reel-shape-clip.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-shape-clip.h"
#include "../graphics/lrg-image-canvas.h"
#include "lrg-reel-context.h"
#include <math.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Instance structure
 * -------------------------------------------------------------------------- */

struct _LrgReelShapeClip
{
    LrgReelClip parent_instance;

    /* Shape kind */
    LrgReelShapeKind kind;

    /* Bounding-box / positional geometry */
    gint shape_x;
    gint shape_y;
    gint shape_width;
    gint shape_height;
    gint shape_radius;
    gint corner_radius;

    /* Line end point */
    gint shape_x2;
    gint shape_y2;

    /* Star parameters */
    gint    point_count;
    gdouble inner_radius_ratio;

    /* Polygon geometry (owned copy) */
    GrlVector2 *poly_points;
    gint         poly_n;

    /* Path geometry */
    GrlPath *path;

    /* Style */
    GrlColor fill_color;
    GrlColor stroke_color;
    gint     stroke_width;
};

G_DEFINE_FINAL_TYPE (LrgReelShapeClip, lrg_reel_shape_clip, LRG_TYPE_REEL_CLIP)

/* --------------------------------------------------------------------------
 * GObject properties
 * -------------------------------------------------------------------------- */

enum
{
    PROP_0,
    PROP_KIND,
    PROP_SHAPE_X,
    PROP_SHAPE_Y,
    PROP_SHAPE_WIDTH,
    PROP_SHAPE_HEIGHT,
    PROP_SHAPE_RADIUS,
    PROP_CORNER_RADIUS,
    PROP_SHAPE_X2,
    PROP_SHAPE_Y2,
    PROP_POINT_COUNT,
    PROP_INNER_RADIUS_RATIO,
    PROP_FILL_COLOR,
    PROP_STROKE_COLOR,
    PROP_STROKE_WIDTH,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/*
 * render_rect:
 * Fill and/or stroke an axis-aligned rectangle.  Stroke is approximated via
 * four draw_line calls so that the stroke rides on the outside of the fill
 * (as callers generally expect).
 */
static void
render_rect (LrgReelShapeClip *self,
             LrgImageCanvas   *canvas)
{
    gint sw;
    gint x;
    gint y;
    gint w;
    gint h;

    x  = self->shape_x;
    y  = self->shape_y;
    w  = self->shape_width;
    h  = self->shape_height;
    sw = self->stroke_width;

    if (self->fill_color.a > 0)
        lrg_image_canvas_fill_rect (canvas, x, y, w, h, &self->fill_color);

    if (sw > 0 && self->stroke_color.a > 0)
    {
        gint half;
        gint x0;
        gint y0;
        gint x1;
        gint y1;

        half = sw / 2;
        x0 = x - half;
        y0 = y - half;
        x1 = x + w - 1 + half;
        y1 = y + h - 1 + half;

        lrg_image_canvas_draw_line (canvas, x0,  y0,  x1,  y0,  sw, &self->stroke_color);
        lrg_image_canvas_draw_line (canvas, x1,  y0,  x1,  y1,  sw, &self->stroke_color);
        lrg_image_canvas_draw_line (canvas, x1,  y1,  x0,  y1,  sw, &self->stroke_color);
        lrg_image_canvas_draw_line (canvas, x0,  y1,  x0,  y0,  sw, &self->stroke_color);
    }
}

/*
 * render_rounded_rect:
 * Build a GrlPath that approximates a rounded rectangle using four straight
 * edges connected by quarter-circle cubic bezier arcs, then fill and/or
 * stroke via fill_path / stroke_path.
 *
 * The classic cubic bezier approximation of a quarter-circle uses a control-
 * point offset of k = (4/3) * tan(pi/8) ≈ 0.5523.
 */
static void
render_rounded_rect (LrgReelShapeClip *self,
                     LrgImageCanvas   *canvas)
{
    g_autoptr(GrlPath) path = NULL;
    gfloat             fx;
    gfloat             fy;
    gfloat             fw;
    gfloat             fh;
    gfloat             fr;
    gfloat             k;
    gint               sw;

    fx = (gfloat) self->shape_x;
    fy = (gfloat) self->shape_y;
    fw = (gfloat) self->shape_width;
    fh = (gfloat) self->shape_height;
    fr = (gfloat) self->corner_radius;
    sw = self->stroke_width;

    /* Clamp corner radius so it cannot exceed half the shorter side. */
    if (fr < 0.0f)
        fr = 0.0f;
    if (fr > fw * 0.5f)
        fr = fw * 0.5f;
    if (fr > fh * 0.5f)
        fr = fh * 0.5f;

    /* Bezier magic constant for quarter-circle approximation. */
    k = fr * 0.5523f;

    path = grl_path_new ();

    /* Start at top-left corner, just right of the top-left arc. */
    grl_path_move_to (path, fx + fr, fy);

    /* Top edge -> top-right arc */
    grl_path_line_to  (path, fx + fw - fr, fy);
    grl_path_cubic_to (path,
                       fx + fw - fr + k, fy,
                       fx + fw,          fy + fr - k,
                       fx + fw,          fy + fr);

    /* Right edge -> bottom-right arc */
    grl_path_line_to  (path, fx + fw, fy + fh - fr);
    grl_path_cubic_to (path,
                       fx + fw,          fy + fh - fr + k,
                       fx + fw - fr + k, fy + fh,
                       fx + fw - fr,     fy + fh);

    /* Bottom edge -> bottom-left arc */
    grl_path_line_to  (path, fx + fr, fy + fh);
    grl_path_cubic_to (path,
                       fx + fr - k, fy + fh,
                       fx,          fy + fh - fr + k,
                       fx,          fy + fh - fr);

    /* Left edge -> top-left arc, close */
    grl_path_line_to  (path, fx, fy + fr);
    grl_path_cubic_to (path,
                       fx,          fy + fr - k,
                       fx + fr - k, fy,
                       fx + fr,     fy);
    grl_path_close (path);

    if (self->fill_color.a > 0)
        lrg_image_canvas_fill_path (canvas, path, GRL_FILL_RULE_NONZERO,
                                    &self->fill_color);

    if (sw > 0 && self->stroke_color.a > 0)
        lrg_image_canvas_stroke_path (canvas, path, sw, &self->stroke_color);
}

/*
 * render_circle:
 */
static void
render_circle (LrgReelShapeClip *self,
               LrgImageCanvas   *canvas)
{
    gint cx;
    gint cy;
    gint r;
    gint sw;

    cx = self->shape_x;
    cy = self->shape_y;
    r  = self->shape_radius;
    sw = self->stroke_width;

    if (self->fill_color.a > 0)
        lrg_image_canvas_fill_circle (canvas, cx, cy, r, &self->fill_color);

    if (sw > 0 && self->stroke_color.a > 0)
        lrg_image_canvas_stroke_circle (canvas, cx, cy, r, sw, &self->stroke_color);
}

/*
 * render_ellipse:
 * Use grl_image_draw_ellipse / grl_image_draw_ellipse_lines on the underlying
 * image, accessed via lrg_image_canvas_get_image().
 */
static void
render_ellipse (LrgReelShapeClip *self,
                LrgImageCanvas   *canvas)
{
    GrlImage *img;
    gint      cx;
    gint      cy;
    gint      rx;
    gint      ry;
    gint      sw;

    img = lrg_image_canvas_get_image (canvas);
    cx  = self->shape_x + self->shape_width  / 2;
    cy  = self->shape_y + self->shape_height / 2;
    rx  = self->shape_width  / 2;
    ry  = self->shape_height / 2;
    sw  = self->stroke_width;

    if (self->fill_color.a > 0)
        grl_image_draw_ellipse (img, cx, cy, rx, ry, &self->fill_color);

    if (sw > 0 && self->stroke_color.a > 0)
        grl_image_draw_ellipse_lines (img, cx, cy, rx, ry, sw, &self->stroke_color);
}

/*
 * render_triangle:
 * The three vertices are derived from the bounding box:
 *   v1 = top-centre
 *   v2 = bottom-left
 *   v3 = bottom-right
 */
static void
render_triangle (LrgReelShapeClip *self,
                 LrgImageCanvas   *canvas)
{
    GrlImage   *img;
    GrlVector2  v1;
    GrlVector2  v2;
    GrlVector2  v3;
    gint        sw;

    img = lrg_image_canvas_get_image (canvas);
    sw  = self->stroke_width;

    v1.x = (gfloat) (self->shape_x + self->shape_width / 2);
    v1.y = (gfloat) self->shape_y;

    v2.x = (gfloat) self->shape_x;
    v2.y = (gfloat) (self->shape_y + self->shape_height - 1);

    v3.x = (gfloat) (self->shape_x + self->shape_width - 1);
    v3.y = (gfloat) (self->shape_y + self->shape_height - 1);

    if (self->fill_color.a > 0)
        grl_image_draw_triangle (img, &v1, &v2, &v3, &self->fill_color);

    if (sw > 0 && self->stroke_color.a > 0)
        grl_image_draw_triangle_lines (img, &v1, &v2, &v3, sw, &self->stroke_color);
}

/*
 * render_star:
 * Build a 2*point_count vertex polygon alternating outer and inner radii,
 * then fill with fill_polygon.  Stroke via stroke_path of the same outline.
 */
static void
render_star (LrgReelShapeClip *self,
             LrgImageCanvas   *canvas)
{
    GrlVector2 *verts;
    gint        n_pts;
    gint        total;
    gfloat      cx;
    gfloat      cy;
    gfloat      outer_r;
    gfloat      inner_r;
    gfloat      step;
    gfloat      angle;
    gint        i;
    gint        sw;

    n_pts   = (self->point_count >= 2) ? self->point_count : 2;
    total   = n_pts * 2;
    cx      = (gfloat) self->shape_x;
    cy      = (gfloat) self->shape_y;
    outer_r = (gfloat) self->shape_radius;
    inner_r = outer_r * (gfloat) self->inner_radius_ratio;
    step    = (gfloat) G_PI / (gfloat) n_pts;
    sw      = self->stroke_width;

    /* Start at the top (- pi/2) */
    angle = -(gfloat) G_PI_2;

    verts = g_new (GrlVector2, (gsize) total);

    for (i = 0; i < total; i++)
    {
        gfloat r;

        r = ((i % 2) == 0) ? outer_r : inner_r;

        verts[i].x = cx + r * cosf (angle);
        verts[i].y = cy + r * sinf (angle);

        angle += step;
    }

    if (self->fill_color.a > 0)
        lrg_image_canvas_fill_polygon (canvas, verts, total, &self->fill_color);

    if (sw > 0 && self->stroke_color.a > 0)
    {
        g_autoptr(GrlPath) path = NULL;
        gint               j;

        path = grl_path_new ();
        grl_path_move_to (path, verts[0].x, verts[0].y);
        for (j = 1; j < total; j++)
            grl_path_line_to (path, verts[j].x, verts[j].y);
        grl_path_close (path);

        lrg_image_canvas_stroke_path (canvas, path, sw, &self->stroke_color);
    }

    g_free (verts);
}

/*
 * render_line:
 * Uses the stroke color (defaulting stroke width to 1 when 0).
 */
static void
render_line (LrgReelShapeClip *self,
             LrgImageCanvas   *canvas)
{
    gint sw;

    sw = (self->stroke_width > 0) ? self->stroke_width : 1;

    if (self->stroke_color.a > 0)
        lrg_image_canvas_draw_line (canvas,
                                    self->shape_x,  self->shape_y,
                                    self->shape_x2, self->shape_y2,
                                    sw, &self->stroke_color);
}

/*
 * render_polygon:
 * Skip if fewer than 3 points.  Stroke via a closed path.
 */
static void
render_polygon (LrgReelShapeClip *self,
                LrgImageCanvas   *canvas)
{
    gint sw;

    if (self->poly_n < 3)
        return;

    sw = self->stroke_width;

    if (self->fill_color.a > 0)
        lrg_image_canvas_fill_polygon (canvas, self->poly_points,
                                       self->poly_n, &self->fill_color);

    if (sw > 0 && self->stroke_color.a > 0)
    {
        g_autoptr(GrlPath) path = NULL;
        gint               i;

        path = grl_path_new ();
        grl_path_move_to (path, self->poly_points[0].x, self->poly_points[0].y);
        for (i = 1; i < self->poly_n; i++)
            grl_path_line_to (path, self->poly_points[i].x, self->poly_points[i].y);
        grl_path_close (path);

        lrg_image_canvas_stroke_path (canvas, path, sw, &self->stroke_color);
    }
}

/*
 * render_path:
 * Skip if path is NULL.
 */
static void
render_path (LrgReelShapeClip *self,
             LrgImageCanvas   *canvas)
{
    gint sw;

    if (self->path == NULL)
        return;

    sw = self->stroke_width;

    if (self->fill_color.a > 0)
        lrg_image_canvas_fill_path (canvas, self->path, GRL_FILL_RULE_NONZERO,
                                    &self->fill_color);

    if (sw > 0 && self->stroke_color.a > 0)
        lrg_image_canvas_stroke_path (canvas, self->path, sw, &self->stroke_color);
}

/* --------------------------------------------------------------------------
 * Render vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_shape_clip_render (LrgReelClip    *base,
                             LrgReelContext *ctx,
                             LrgImageCanvas *canvas)
{
    LrgReelShapeClip *self;

    (void) ctx;

    self = LRG_REEL_SHAPE_CLIP (base);

    switch (self->kind)
    {
    case LRG_REEL_SHAPE_RECT:
        render_rect (self, canvas);
        break;
    case LRG_REEL_SHAPE_ROUNDED_RECT:
        render_rounded_rect (self, canvas);
        break;
    case LRG_REEL_SHAPE_CIRCLE:
        render_circle (self, canvas);
        break;
    case LRG_REEL_SHAPE_ELLIPSE:
        render_ellipse (self, canvas);
        break;
    case LRG_REEL_SHAPE_TRIANGLE:
        render_triangle (self, canvas);
        break;
    case LRG_REEL_SHAPE_STAR:
        render_star (self, canvas);
        break;
    case LRG_REEL_SHAPE_LINE:
        render_line (self, canvas);
        break;
    case LRG_REEL_SHAPE_POLYGON:
        render_polygon (self, canvas);
        break;
    case LRG_REEL_SHAPE_PATH:
        render_path (self, canvas);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * GObject property implementation
 * -------------------------------------------------------------------------- */

static void
lrg_reel_shape_clip_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgReelShapeClip *self = LRG_REEL_SHAPE_CLIP (object);
    GrlColor          tmp;

    switch (prop_id)
    {
    case PROP_KIND:
        g_value_set_enum (value, self->kind);
        break;
    case PROP_SHAPE_X:
        g_value_set_int (value, self->shape_x);
        break;
    case PROP_SHAPE_Y:
        g_value_set_int (value, self->shape_y);
        break;
    case PROP_SHAPE_WIDTH:
        g_value_set_int (value, self->shape_width);
        break;
    case PROP_SHAPE_HEIGHT:
        g_value_set_int (value, self->shape_height);
        break;
    case PROP_SHAPE_RADIUS:
        g_value_set_int (value, self->shape_radius);
        break;
    case PROP_CORNER_RADIUS:
        g_value_set_int (value, self->corner_radius);
        break;
    case PROP_SHAPE_X2:
        g_value_set_int (value, self->shape_x2);
        break;
    case PROP_SHAPE_Y2:
        g_value_set_int (value, self->shape_y2);
        break;
    case PROP_POINT_COUNT:
        g_value_set_int (value, self->point_count);
        break;
    case PROP_INNER_RADIUS_RATIO:
        g_value_set_double (value, self->inner_radius_ratio);
        break;
    case PROP_FILL_COLOR:
        tmp = self->fill_color;
        g_value_set_boxed (value, &tmp);
        break;
    case PROP_STROKE_COLOR:
        tmp = self->stroke_color;
        g_value_set_boxed (value, &tmp);
        break;
    case PROP_STROKE_WIDTH:
        g_value_set_int (value, self->stroke_width);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_shape_clip_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgReelShapeClip *self = LRG_REEL_SHAPE_CLIP (object);

    switch (prop_id)
    {
    case PROP_KIND:
        self->kind = g_value_get_enum (value);
        break;
    case PROP_SHAPE_X:
        lrg_reel_shape_clip_set_shape_x (self, g_value_get_int (value));
        break;
    case PROP_SHAPE_Y:
        lrg_reel_shape_clip_set_shape_y (self, g_value_get_int (value));
        break;
    case PROP_SHAPE_WIDTH:
        lrg_reel_shape_clip_set_shape_width (self, g_value_get_int (value));
        break;
    case PROP_SHAPE_HEIGHT:
        lrg_reel_shape_clip_set_shape_height (self, g_value_get_int (value));
        break;
    case PROP_SHAPE_RADIUS:
        lrg_reel_shape_clip_set_shape_radius (self, g_value_get_int (value));
        break;
    case PROP_CORNER_RADIUS:
        lrg_reel_shape_clip_set_corner_radius (self, g_value_get_int (value));
        break;
    case PROP_SHAPE_X2:
        lrg_reel_shape_clip_set_shape_x2 (self, g_value_get_int (value));
        break;
    case PROP_SHAPE_Y2:
        lrg_reel_shape_clip_set_shape_y2 (self, g_value_get_int (value));
        break;
    case PROP_POINT_COUNT:
        lrg_reel_shape_clip_set_point_count (self, g_value_get_int (value));
        break;
    case PROP_INNER_RADIUS_RATIO:
        lrg_reel_shape_clip_set_inner_radius_ratio (self, g_value_get_double (value));
        break;
    case PROP_FILL_COLOR:
        lrg_reel_shape_clip_set_fill_color (self,
                                             (const GrlColor *) g_value_get_boxed (value));
        break;
    case PROP_STROKE_COLOR:
        lrg_reel_shape_clip_set_stroke_color (self,
                                               (const GrlColor *) g_value_get_boxed (value));
        break;
    case PROP_STROKE_WIDTH:
        lrg_reel_shape_clip_set_stroke_width (self, g_value_get_int (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/* --------------------------------------------------------------------------
 * GObject lifecycle
 * -------------------------------------------------------------------------- */

static void
lrg_reel_shape_clip_finalize (GObject *object)
{
    LrgReelShapeClip *self = LRG_REEL_SHAPE_CLIP (object);

    g_free (self->poly_points);
    self->poly_points = NULL;
    self->poly_n      = 0;

    g_clear_object (&self->path);

    G_OBJECT_CLASS (lrg_reel_shape_clip_parent_class)->finalize (object);
}

static void
lrg_reel_shape_clip_class_init (LrgReelShapeClipClass *klass)
{
    GObjectClass     *object_class = G_OBJECT_CLASS (klass);
    LrgReelClipClass *clip_class   = LRG_REEL_CLIP_CLASS (klass);

    object_class->finalize     = lrg_reel_shape_clip_finalize;
    object_class->get_property = lrg_reel_shape_clip_get_property;
    object_class->set_property = lrg_reel_shape_clip_set_property;

    clip_class->render = lrg_reel_shape_clip_render;

    /**
     * LrgReelShapeClip:kind:
     *
     * The shape variant to draw.  Set at construction time via
     * lrg_reel_shape_clip_new().
     *
     * Since: 1.0
     */
    properties[PROP_KIND] =
        g_param_spec_enum ("kind", "Kind",
                           "Shape kind",
                           LRG_TYPE_REEL_SHAPE_KIND,
                           LRG_REEL_SHAPE_RECT,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelShapeClip:shape-x:
     *
     * X origin of the shape bounding box, or line start X.
     *
     * Since: 1.0
     */
    properties[PROP_SHAPE_X] =
        g_param_spec_int ("shape-x", "Shape X",
                          "X origin / line start X",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelShapeClip:shape-y:
     *
     * Y origin of the shape bounding box, or line start Y.
     *
     * Since: 1.0
     */
    properties[PROP_SHAPE_Y] =
        g_param_spec_int ("shape-y", "Shape Y",
                          "Y origin / line start Y",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelShapeClip:shape-width:
     *
     * Bounding box width in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_SHAPE_WIDTH] =
        g_param_spec_int ("shape-width", "Shape Width",
                          "Bounding box width in pixels",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelShapeClip:shape-height:
     *
     * Bounding box height in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_SHAPE_HEIGHT] =
        g_param_spec_int ("shape-height", "Shape Height",
                          "Bounding box height in pixels",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelShapeClip:shape-radius:
     *
     * Circle radius in pixels, or star outer radius.
     *
     * Since: 1.0
     */
    properties[PROP_SHAPE_RADIUS] =
        g_param_spec_int ("shape-radius", "Shape Radius",
                          "Circle radius / star outer radius in pixels",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelShapeClip:corner-radius:
     *
     * Corner rounding radius in pixels for rounded-rect shapes.
     *
     * Since: 1.0
     */
    properties[PROP_CORNER_RADIUS] =
        g_param_spec_int ("corner-radius", "Corner Radius",
                          "Corner rounding radius for rounded-rect",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelShapeClip:shape-x2:
     *
     * X coordinate of the line end point.
     *
     * Since: 1.0
     */
    properties[PROP_SHAPE_X2] =
        g_param_spec_int ("shape-x2", "Shape X2",
                          "Line end X coordinate",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelShapeClip:shape-y2:
     *
     * Y coordinate of the line end point.
     *
     * Since: 1.0
     */
    properties[PROP_SHAPE_Y2] =
        g_param_spec_int ("shape-y2", "Shape Y2",
                          "Line end Y coordinate",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelShapeClip:point-count:
     *
     * Number of points on a star shape.  Default is 5.
     *
     * Since: 1.0
     */
    properties[PROP_POINT_COUNT] =
        g_param_spec_int ("point-count", "Point Count",
                          "Number of star points",
                          2, G_MAXINT, 5,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelShapeClip:inner-radius-ratio:
     *
     * Ratio of the inner notch radius to the outer radius for star shapes.
     * The default of 0.5 produces a classic star.
     *
     * Since: 1.0
     */
    properties[PROP_INNER_RADIUS_RATIO] =
        g_param_spec_double ("inner-radius-ratio", "Inner Radius Ratio",
                             "Star inner-to-outer radius ratio",
                             0.0, 1.0, 0.5,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelShapeClip:fill-color:
     *
     * Fill color.  A fully transparent color (alpha 0) disables filling.
     * Default is opaque white.
     *
     * Since: 1.0
     */
    properties[PROP_FILL_COLOR] =
        g_param_spec_boxed ("fill-color", "Fill Color",
                            "Shape fill color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelShapeClip:stroke-color:
     *
     * Stroke color.  The stroke is only drawn when @stroke-width > 0 and
     * this color has alpha > 0.  Default is fully transparent (no stroke).
     *
     * Since: 1.0
     */
    properties[PROP_STROKE_COLOR] =
        g_param_spec_boxed ("stroke-color", "Stroke Color",
                            "Shape outline stroke color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelShapeClip:stroke-width:
     *
     * Stroke width in pixels.  0 (the default) means no stroke.
     *
     * Since: 1.0
     */
    properties[PROP_STROKE_WIDTH] =
        g_param_spec_int ("stroke-width", "Stroke Width",
                          "Outline stroke width in pixels (0 = no stroke)",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_shape_clip_init (LrgReelShapeClip *self)
{
    self->kind               = LRG_REEL_SHAPE_RECT;
    self->shape_x            = 0;
    self->shape_y            = 0;
    self->shape_width        = 0;
    self->shape_height       = 0;
    self->shape_radius       = 0;
    self->corner_radius      = 0;
    self->shape_x2           = 0;
    self->shape_y2           = 0;
    self->point_count        = 5;
    self->inner_radius_ratio = 0.5;

    self->poly_points        = NULL;
    self->poly_n             = 0;
    self->path               = NULL;

    /* Default fill: opaque white */
    self->fill_color.r = 255;
    self->fill_color.g = 255;
    self->fill_color.b = 255;
    self->fill_color.a = 255;

    /* Default stroke: transparent (disabled) */
    self->stroke_color.r = 0;
    self->stroke_color.g = 0;
    self->stroke_color.b = 0;
    self->stroke_color.a = 0;

    self->stroke_width = 0;
}

/* --------------------------------------------------------------------------
 * Public API: constructors
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_shape_clip_new:
 * @kind: the shape variant to draw.
 *
 * Creates a new #LrgReelShapeClip of the given @kind.  The fill color defaults
 * to opaque white; no stroke is drawn by default.
 *
 * Returns: (transfer full): a new #LrgReelShapeClip.
 *
 * Since: 1.0
 */
LrgReelShapeClip *
lrg_reel_shape_clip_new (LrgReelShapeKind kind)
{
    return g_object_new (LRG_TYPE_REEL_SHAPE_CLIP,
                         "kind", kind,
                         NULL);
}

/**
 * lrg_reel_shape_clip_new_rect:
 * @x: left edge of the rectangle in frame coordinates.
 * @y: top edge of the rectangle in frame coordinates.
 * @w: rectangle width in pixels.
 * @h: rectangle height in pixels.
 *
 * Convenience constructor.
 *
 * Returns: (transfer full): a new #LrgReelShapeClip.
 *
 * Since: 1.0
 */
LrgReelShapeClip *
lrg_reel_shape_clip_new_rect (gint x,
                               gint y,
                               gint w,
                               gint h)
{
    LrgReelShapeClip *self;

    self = lrg_reel_shape_clip_new (LRG_REEL_SHAPE_RECT);
    self->shape_x      = x;
    self->shape_y      = y;
    self->shape_width  = w;
    self->shape_height = h;

    return self;
}

/**
 * lrg_reel_shape_clip_new_circle:
 * @cx: centre X coordinate in frame coordinates.
 * @cy: centre Y coordinate in frame coordinates.
 * @radius: circle radius in pixels.
 *
 * Convenience constructor.
 *
 * Returns: (transfer full): a new #LrgReelShapeClip.
 *
 * Since: 1.0
 */
LrgReelShapeClip *
lrg_reel_shape_clip_new_circle (gint cx,
                                 gint cy,
                                 gint radius)
{
    LrgReelShapeClip *self;

    self = lrg_reel_shape_clip_new (LRG_REEL_SHAPE_CIRCLE);
    self->shape_x      = cx;
    self->shape_y      = cy;
    self->shape_radius = radius;

    return self;
}

/**
 * lrg_reel_shape_clip_new_line:
 * @x1: X coordinate of the start point.
 * @y1: Y coordinate of the start point.
 * @x2: X coordinate of the end point.
 * @y2: Y coordinate of the end point.
 *
 * Convenience constructor.  Sets the stroke color to opaque white, fill color
 * to fully transparent, and stroke width to 1.
 *
 * Returns: (transfer full): a new #LrgReelShapeClip.
 *
 * Since: 1.0
 */
LrgReelShapeClip *
lrg_reel_shape_clip_new_line (gint x1,
                               gint y1,
                               gint x2,
                               gint y2)
{
    LrgReelShapeClip *self;

    self = lrg_reel_shape_clip_new (LRG_REEL_SHAPE_LINE);
    self->shape_x  = x1;
    self->shape_y  = y1;
    self->shape_x2 = x2;
    self->shape_y2 = y2;

    /* Lines default to opaque-white stroke, transparent fill. */
    self->fill_color.a   = 0;
    self->stroke_color.r = 255;
    self->stroke_color.g = 255;
    self->stroke_color.b = 255;
    self->stroke_color.a = 255;
    self->stroke_width   = 1;

    return self;
}

/* --------------------------------------------------------------------------
 * Public API: geometry accessors
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_shape_clip_get_kind:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: the shape kind.
 *
 * Since: 1.0
 */
LrgReelShapeKind
lrg_reel_shape_clip_get_kind (LrgReelShapeClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SHAPE_CLIP (self), LRG_REEL_SHAPE_RECT);
    return self->kind;
}

/**
 * lrg_reel_shape_clip_get_shape_x:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: the shape X coordinate.
 *
 * Since: 1.0
 */
gint
lrg_reel_shape_clip_get_shape_x (LrgReelShapeClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SHAPE_CLIP (self), 0);
    return self->shape_x;
}

/**
 * lrg_reel_shape_clip_set_shape_x:
 * @self: an #LrgReelShapeClip.
 * @x: the new shape X coordinate.
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_shape_x (LrgReelShapeClip *self,
                                  gint              x)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));

    if (self->shape_x == x)
        return;

    self->shape_x = x;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHAPE_X]);
}

/**
 * lrg_reel_shape_clip_get_shape_y:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: the shape Y coordinate.
 *
 * Since: 1.0
 */
gint
lrg_reel_shape_clip_get_shape_y (LrgReelShapeClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SHAPE_CLIP (self), 0);
    return self->shape_y;
}

/**
 * lrg_reel_shape_clip_set_shape_y:
 * @self: an #LrgReelShapeClip.
 * @y: the new shape Y coordinate.
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_shape_y (LrgReelShapeClip *self,
                                  gint              y)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));

    if (self->shape_y == y)
        return;

    self->shape_y = y;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHAPE_Y]);
}

/**
 * lrg_reel_shape_clip_get_shape_width:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: the bounding-box width in pixels.
 *
 * Since: 1.0
 */
gint
lrg_reel_shape_clip_get_shape_width (LrgReelShapeClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SHAPE_CLIP (self), 0);
    return self->shape_width;
}

/**
 * lrg_reel_shape_clip_set_shape_width:
 * @self: an #LrgReelShapeClip.
 * @width: the new width in pixels.
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_shape_width (LrgReelShapeClip *self,
                                      gint              width)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));

    if (self->shape_width == width)
        return;

    self->shape_width = width;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHAPE_WIDTH]);
}

/**
 * lrg_reel_shape_clip_get_shape_height:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: the bounding-box height in pixels.
 *
 * Since: 1.0
 */
gint
lrg_reel_shape_clip_get_shape_height (LrgReelShapeClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SHAPE_CLIP (self), 0);
    return self->shape_height;
}

/**
 * lrg_reel_shape_clip_set_shape_height:
 * @self: an #LrgReelShapeClip.
 * @height: the new height in pixels.
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_shape_height (LrgReelShapeClip *self,
                                       gint              height)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));

    if (self->shape_height == height)
        return;

    self->shape_height = height;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHAPE_HEIGHT]);
}

/**
 * lrg_reel_shape_clip_get_shape_radius:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: the radius in pixels.
 *
 * Since: 1.0
 */
gint
lrg_reel_shape_clip_get_shape_radius (LrgReelShapeClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SHAPE_CLIP (self), 0);
    return self->shape_radius;
}

/**
 * lrg_reel_shape_clip_set_shape_radius:
 * @self: an #LrgReelShapeClip.
 * @radius: the new radius in pixels.
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_shape_radius (LrgReelShapeClip *self,
                                       gint              radius)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));

    if (self->shape_radius == radius)
        return;

    self->shape_radius = radius;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHAPE_RADIUS]);
}

/**
 * lrg_reel_shape_clip_get_corner_radius:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: the corner radius in pixels.
 *
 * Since: 1.0
 */
gint
lrg_reel_shape_clip_get_corner_radius (LrgReelShapeClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SHAPE_CLIP (self), 0);
    return self->corner_radius;
}

/**
 * lrg_reel_shape_clip_set_corner_radius:
 * @self: an #LrgReelShapeClip.
 * @corner_radius: the new corner radius in pixels.
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_corner_radius (LrgReelShapeClip *self,
                                        gint              corner_radius)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));

    if (self->corner_radius == corner_radius)
        return;

    self->corner_radius = corner_radius;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
}

/**
 * lrg_reel_shape_clip_get_shape_x2:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: the line end X coordinate.
 *
 * Since: 1.0
 */
gint
lrg_reel_shape_clip_get_shape_x2 (LrgReelShapeClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SHAPE_CLIP (self), 0);
    return self->shape_x2;
}

/**
 * lrg_reel_shape_clip_set_shape_x2:
 * @self: an #LrgReelShapeClip.
 * @x2: the new line end X coordinate.
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_shape_x2 (LrgReelShapeClip *self,
                                   gint              x2)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));

    if (self->shape_x2 == x2)
        return;

    self->shape_x2 = x2;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHAPE_X2]);
}

/**
 * lrg_reel_shape_clip_get_shape_y2:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: the line end Y coordinate.
 *
 * Since: 1.0
 */
gint
lrg_reel_shape_clip_get_shape_y2 (LrgReelShapeClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SHAPE_CLIP (self), 0);
    return self->shape_y2;
}

/**
 * lrg_reel_shape_clip_set_shape_y2:
 * @self: an #LrgReelShapeClip.
 * @y2: the new line end Y coordinate.
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_shape_y2 (LrgReelShapeClip *self,
                                   gint              y2)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));

    if (self->shape_y2 == y2)
        return;

    self->shape_y2 = y2;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHAPE_Y2]);
}

/**
 * lrg_reel_shape_clip_get_point_count:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: number of star points.
 *
 * Since: 1.0
 */
gint
lrg_reel_shape_clip_get_point_count (LrgReelShapeClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SHAPE_CLIP (self), 5);
    return self->point_count;
}

/**
 * lrg_reel_shape_clip_set_point_count:
 * @self: an #LrgReelShapeClip.
 * @point_count: the new number of star points (>= 2).
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_point_count (LrgReelShapeClip *self,
                                      gint              point_count)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));

    if (point_count < 2)
        point_count = 2;

    if (self->point_count == point_count)
        return;

    self->point_count = point_count;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POINT_COUNT]);
}

/**
 * lrg_reel_shape_clip_get_inner_radius_ratio:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: the inner radius ratio in [0.0, 1.0].
 *
 * Since: 1.0
 */
gdouble
lrg_reel_shape_clip_get_inner_radius_ratio (LrgReelShapeClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SHAPE_CLIP (self), 0.5);
    return self->inner_radius_ratio;
}

/**
 * lrg_reel_shape_clip_set_inner_radius_ratio:
 * @self: an #LrgReelShapeClip.
 * @ratio: the new inner radius ratio in [0.0, 1.0].
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_inner_radius_ratio (LrgReelShapeClip *self,
                                             gdouble           ratio)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));

    ratio = CLAMP (ratio, 0.0, 1.0);

    if (self->inner_radius_ratio == ratio)
        return;

    self->inner_radius_ratio = ratio;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INNER_RADIUS_RATIO]);
}

/* --------------------------------------------------------------------------
 * Public API: polygon / path setters
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_shape_clip_set_points:
 * @self: an #LrgReelShapeClip.
 * @points: (array length=n_points): vertex array for a polygon shape.
 * @n_points: number of vertices.
 *
 * Copies @points into the clip.
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_points (LrgReelShapeClip  *self,
                                 const GrlVector2  *points,
                                 gint               n_points)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));

    g_free (self->poly_points);
    self->poly_points = NULL;
    self->poly_n      = 0;

    if (points != NULL && n_points > 0)
    {
        self->poly_points = g_new (GrlVector2, (gsize) n_points);
        self->poly_n      = n_points;
        memcpy (self->poly_points, points,
                (gsize) n_points * sizeof (GrlVector2));
    }
}

/**
 * lrg_reel_shape_clip_set_path:
 * @self: an #LrgReelShapeClip.
 * @path: (nullable) (transfer none): a #GrlPath, or %NULL to clear.
 *
 * Stores a reference to @path.
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_path (LrgReelShapeClip *self,
                               GrlPath          *path)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));
    g_return_if_fail (path == NULL || GRL_IS_PATH (path));

    if (self->path == path)
        return;

    g_clear_object (&self->path);
    if (path != NULL)
        self->path = g_object_ref (path);
}

/* --------------------------------------------------------------------------
 * Public API: style accessors
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_shape_clip_get_fill_color:
 * @self: an #LrgReelShapeClip.
 * @out_color: (out caller-allocates): return location for the fill color.
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_get_fill_color (LrgReelShapeClip *self,
                                     GrlColor         *out_color)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));
    g_return_if_fail (out_color != NULL);

    *out_color = self->fill_color;
}

/**
 * lrg_reel_shape_clip_set_fill_color:
 * @self: an #LrgReelShapeClip.
 * @color: the new fill color.
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_fill_color (LrgReelShapeClip *self,
                                     const GrlColor   *color)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));
    g_return_if_fail (color != NULL);

    self->fill_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_COLOR]);
}

/**
 * lrg_reel_shape_clip_get_stroke_color:
 * @self: an #LrgReelShapeClip.
 * @out_color: (out caller-allocates): return location for the stroke color.
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_get_stroke_color (LrgReelShapeClip *self,
                                       GrlColor         *out_color)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));
    g_return_if_fail (out_color != NULL);

    *out_color = self->stroke_color;
}

/**
 * lrg_reel_shape_clip_set_stroke_color:
 * @self: an #LrgReelShapeClip.
 * @color: the new stroke color.
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_stroke_color (LrgReelShapeClip *self,
                                       const GrlColor   *color)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));
    g_return_if_fail (color != NULL);

    self->stroke_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STROKE_COLOR]);
}

/**
 * lrg_reel_shape_clip_get_stroke_width:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: the stroke width in pixels.
 *
 * Since: 1.0
 */
gint
lrg_reel_shape_clip_get_stroke_width (LrgReelShapeClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SHAPE_CLIP (self), 0);
    return self->stroke_width;
}

/**
 * lrg_reel_shape_clip_set_stroke_width:
 * @self: an #LrgReelShapeClip.
 * @stroke_width: the new stroke width in pixels (0 = no stroke).
 *
 * Since: 1.0
 */
void
lrg_reel_shape_clip_set_stroke_width (LrgReelShapeClip *self,
                                       gint              stroke_width)
{
    g_return_if_fail (LRG_IS_REEL_SHAPE_CLIP (self));

    if (stroke_width < 0)
        stroke_width = 0;

    if (self->stroke_width == stroke_width)
        return;

    self->stroke_width = stroke_width;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STROKE_WIDTH]);
}
