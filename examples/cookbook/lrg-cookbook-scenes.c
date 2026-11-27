/* lrg-cookbook-scenes.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The cookbook scenes.  Every scene follows one of two recipes:
 *
 *   8-bit:  lrg_image_canvas_new_supersampled ()  ->  draw (canvas
 *           primitives, lrg_image_canvas_fill_sdf_rect (), text)  ->
 *           lrg_image_canvas_resolve ()  ->  optional CPU post stack
 *           (lrg_post_processor_apply_to_image ())
 *
 *   HDR:    lrg_hdr_canvas_new ()  ->  lrg_hdr_canvas_shade () with
 *           stratified samples (SDFs, band-limited noise)  ->  optional
 *           post on the float buffer (bloom, blur)  ->
 *           lrg_hdr_canvas_to_image () with a tone-map operator  ->
 *           optional 8-bit post (vignette, grain)
 *
 * Elements that must not collide are placed with LrgFrameLayout and
 * checked with lrg_frame_layout_validate () before anything is drawn.
 *
 * The helpers at the top are the small per-scene glue the guide
 * mentions (colours, a look-at camera, text fitting, running a post
 * stack); everything heavy (distance fields, raymarching, shadows,
 * noise, blending, resampling, tone mapping, post effects) is the
 * library's.
 */

#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <glib.h>
#include <libregnum.h>

#include "lrg-cookbook-scenes.h"

#ifndef G_PI_F
#define G_PI_F ((gfloat) G_PI)
#endif

/* The design size: scenes are composed for 640 x 400 and scaled. */
#define CB_DESIGN_W (640.0f)
#define CB_DESIGN_H (400.0f)

/* The smallest image a scene renders. */
#define CB_MIN_SIZE (16)

/* ==========================================================================
 * Small maths
 * ========================================================================== */

static gfloat
cb_clampf (gfloat v,
           gfloat lo,
           gfloat hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static gfloat
cb_mixf (gfloat a,
         gfloat b,
         gfloat t)
{
	return a + (b - a) * t;
}

static gfloat
cb_smoothstep (gfloat e0,
               gfloat e1,
               gfloat x)
{
	gfloat t;

	t = cb_clampf ((x - e0) / (e1 - e0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

static void
v3_set (gfloat o[3],
        gfloat x,
        gfloat y,
        gfloat z)
{
	o[0] = x;
	o[1] = y;
	o[2] = z;
}

static gfloat
v3_dot (const gfloat a[3],
        const gfloat b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void
v3_normalize (gfloat a[3])
{
	gfloat l;

	l = sqrtf (v3_dot (a, a));
	if (l > 0.0f)
	{
		a[0] /= l;
		a[1] /= l;
		a[2] /= l;
	}
}

/* o = a + b * s (o may alias a) */
static void
v3_madd (gfloat       o[3],
         const gfloat a[3],
         const gfloat b[3],
         gfloat       s)
{
	o[0] = a[0] + b[0] * s;
	o[1] = a[1] + b[1] * s;
	o[2] = a[2] + b[2] * s;
}

/* o = mix (a, b, t), per channel (o may alias a) */
static void
v3_mix (gfloat       o[3],
        const gfloat a[3],
        const gfloat b[3],
        gfloat       t)
{
	o[0] = cb_mixf (a[0], b[0], t);
	o[1] = cb_mixf (a[1], b[1], t);
	o[2] = cb_mixf (a[2], b[2], t);
}

/* reflect d about the unit normal n */
static void
v3_reflect (gfloat       o[3],
            const gfloat d[3],
            const gfloat n[3])
{
	gfloat k;

	k = 2.0f * v3_dot (d, n);
	o[0] = d[0] - k * n[0];
	o[1] = d[1] - k * n[1];
	o[2] = d[2] - k * n[2];
}

static void
cb_color (GrlColor *c,
          guint8    r,
          guint8    g,
          guint8    b,
          guint8    a)
{
	c->r = r;
	c->g = g;
	c->b = b;
	c->a = a;
}

/* the uniform scale from the 640 x 400 design size */
static gfloat
cb_unit (guint width,
         guint height)
{
	return MIN ((gfloat) width / CB_DESIGN_W, (gfloat) height / CB_DESIGN_H);
}

static gboolean
cb_size_ok (guint width,
            guint height)
{
	return width >= CB_MIN_SIZE && height >= CB_MIN_SIZE;
}

/* ==========================================================================
 * Camera: a pinhole looking from eye to target
 * ========================================================================== */

typedef struct
{
	gfloat eye[3];
	gfloat fwd[3];
	gfloat right[3];
	gfloat up[3];
	gfloat focal;        /* 1 / tan (fov / 2) */
	gfloat width;
	gfloat height;
	gfloat pixel_angle;  /* angular size of one pixel, radians */
} CbCamera;

static void
cb_camera_init (CbCamera     *cam,
                const gfloat  eye[3],
                const gfloat  target[3],
                gfloat        fov_y_deg,
                guint         width,
                guint         height)
{
	gfloat world_up[3] = { 0.0f, 1.0f, 0.0f };

	memcpy (cam->eye, eye, sizeof (cam->eye));

	/* forward, then right = fwd x up and up = right x fwd */
	v3_set (cam->fwd, target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]);
	v3_normalize (cam->fwd);
	v3_set (cam->right,
	        cam->fwd[1] * world_up[2] - cam->fwd[2] * world_up[1],
	        cam->fwd[2] * world_up[0] - cam->fwd[0] * world_up[2],
	        cam->fwd[0] * world_up[1] - cam->fwd[1] * world_up[0]);
	v3_normalize (cam->right);
	v3_set (cam->up,
	        cam->right[1] * cam->fwd[2] - cam->right[2] * cam->fwd[1],
	        cam->right[2] * cam->fwd[0] - cam->right[0] * cam->fwd[2],
	        cam->right[0] * cam->fwd[1] - cam->right[1] * cam->fwd[0]);

	cam->focal = 1.0f / tanf (fov_y_deg * G_PI_F / 360.0f);
	cam->width = (gfloat) width;
	cam->height = (gfloat) height;
	cam->pixel_angle = 2.0f / (cam->focal * cam->height);
}

/* the normalised ray direction through image position (x, y) */
static void
cb_camera_ray (const CbCamera *cam,
               gfloat          x,
               gfloat          y,
               gfloat          rd[3])
{
	gfloat nx;
	gfloat ny;
	gint   i;

	nx = (2.0f * x - cam->width) / cam->height;
	ny = (cam->height - 2.0f * y) / cam->height;
	for (i = 0; i < 3; i++)
		rd[i] = cam->fwd[i] * cam->focal + cam->right[i] * nx + cam->up[i] * ny;
	v3_normalize (rd);
}

/* ==========================================================================
 * Fonts and text
 * ========================================================================== */

GrlImageFont *
lrg_cookbook_load_font (gboolean bold)
{
	static const gchar *const bold_files[] = {
		"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
		"/usr/share/fonts/liberation-sans-fonts/LiberationSans-Bold.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
		"/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
		NULL
	};
	static const gchar *const regular_files[] = {
		"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
		"/usr/share/fonts/abattis-cantarell-fonts/Cantarell-Regular.otf",
		"/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
		NULL
	};
	const gchar *const *files;
	const gchar        *env;
	guint               i;

	/* an explicit choice wins */
	env = g_getenv ("LRG_COOKBOOK_FONT");
	if (env != NULL && *env != '\0')
	{
		GrlImageFont *font = grl_image_font_new_from_file (env, NULL);

		if (font != NULL)
			return font;
	}

	files = bold ? bold_files : regular_files;
	for (i = 0; files[i] != NULL; i++)
	{
		if (g_file_test (files[i], G_FILE_TEST_IS_REGULAR))
		{
			GrlImageFont *font = grl_image_font_new_from_file (files[i], NULL);

			if (font != NULL)
				return font;
		}
	}
	return NULL;
}

/* measured width and height of text at a size; the bitmap font when
 * font is NULL */
static void
cb_text_measure (GrlImageFont *font,
                 const gchar  *text,
                 gfloat        size,
                 gfloat       *out_w,
                 gfloat       *out_h)
{
	g_autoptr(GrlVector2) v = NULL;

	if (font != NULL)
		v = grl_image_measure_text_ttf (font, text, size);
	else
		v = grl_image_measure_text_bitmap (text, MAX (1, (gint) size));
	*out_w = v != NULL ? v->x : 0.0f;
	*out_h = v != NULL ? v->y : 0.0f;
}

/* the largest size (up to max_size) at which text fits a w x h box */
static gfloat
cb_text_fit (GrlImageFont *font,
             const gchar  *text,
             gfloat        box_w,
             gfloat        box_h,
             gfloat        max_size)
{
	gfloat w;
	gfloat h;
	gfloat size;

	/* measurements scale linearly with the size: measure once */
	cb_text_measure (font, text, 100.0f, &w, &h);
	if (w <= 0.0f || h <= 0.0f)
		return max_size;
	size = MIN (100.0f * box_w / w, 100.0f * box_h / h);
	return MIN (size, max_size);
}

/*
 * cb_text_in_box:
 * Draws text centred (align 0.5) or left-aligned (align 0) in a box
 * given in final-image pixels on a canvas supersampled by f.  The
 * size is fitted to the box (never above max_size).
 */
static void
cb_text_in_box (LrgImageCanvas     *canvas,
                GrlImageFont       *font,
                const gchar        *text,
                const GrlRectangle *box,
                gfloat              f,
                gfloat              max_size,
                gfloat              align,
                const GrlColor     *color)
{
	gfloat size;
	gfloat w;
	gfloat h;
	gfloat x;
	gfloat y;

	size = cb_text_fit (font, text, box->width, box->height, max_size);
	cb_text_measure (font, text, size, &w, &h);
	x = box->x + (box->width - w) * align;
	y = box->y + (box->height - h) * 0.5f;

	/* text ignores the transform stack: draw in the large image's pixels */
	if (font != NULL)
		lrg_image_canvas_draw_text (canvas, font, text, (gint) lroundf (x * f),
		                            (gint) lroundf (y * f), size * f, color);
	else
		grl_image_draw_text_bitmap (lrg_image_canvas_get_image (canvas), text,
		                            (gint) lroundf (x * f), (gint) lroundf (y * f),
		                            MAX (1, (gint) (size * f)), color);
}

/* ==========================================================================
 * 2D shapes for lrg_image_canvas_fill_sdf_rect ()
 *
 * One small struct describes a shape in final-image pixels; the shared
 * callback maps the large (supersampled) pixel back, evaluates the
 * library SDF and returns the distance in large pixels divided by the
 * feather, so feather 1 is a crisp anti-aliased edge and feather 20 a
 * soft 20-pixel glow.
 * ========================================================================== */

typedef enum
{
	CB_SHAPE_CIRCLE,
	CB_SHAPE_ELLIPSE,
	CB_SHAPE_ROUND_BOX,
	CB_SHAPE_CAPSULE,
	CB_SHAPE_TRIANGLE,
	CB_SHAPE_BAND_CIRCLE    /* circle intersected with a horizontal band */
} CbShapeKind;

typedef struct
{
	CbShapeKind kind;
	gfloat      a[2];       /* centre, or first point */
	gfloat      b[2];       /* half size / radii, or second point */
	gfloat      c[2];       /* third point; band: y0, y1 */
	gfloat      r;          /* radius / corner radius */
	gfloat      f;          /* supersampling factor */
	gfloat      feather;    /* edge width in final pixels (1 = crisp) */
} CbShape;

static gfloat
cb_shape_distance (const CbShape *s,
                   const gfloat   q[2])
{
	gfloat p[2];

	p[0] = q[0] - s->a[0];
	p[1] = q[1] - s->a[1];
	switch (s->kind)
	{
	case CB_SHAPE_CIRCLE:
		return lrg_sdf2_circle (p, s->r);
	case CB_SHAPE_ELLIPSE:
		return lrg_sdf2_ellipse (p, s->b);
	case CB_SHAPE_ROUND_BOX:
		return lrg_sdf2_round_box (p, s->b, s->r);
	case CB_SHAPE_CAPSULE:
		return lrg_sdf2_capsule (q, s->a, s->b, s->r);
	case CB_SHAPE_TRIANGLE:
		return lrg_sdf2_triangle (q, s->a, s->b, s->c);
	case CB_SHAPE_BAND_CIRCLE:
	{
		gfloat half[2];
		gfloat band[2];

		half[0] = s->r + 2.0f;
		half[1] = (s->c[1] - s->c[0]) * 0.5f;
		band[0] = p[0];
		band[1] = q[1] - (s->c[0] + s->c[1]) * 0.5f;
		return lrg_sdf_intersect (lrg_sdf2_circle (p, s->r), lrg_sdf2_box (band, half));
	}
	default:
		return 1e9f;
	}
}

static gfloat
cb_shape_sdf (const gfloat p[2],
              gpointer     user_data)
{
	const CbShape *s = user_data;
	gfloat         q[2];

	/* large-image pixel -> final pixel, distance back in large pixels */
	q[0] = p[0] / s->f;
	q[1] = p[1] / s->f;
	return cb_shape_distance (s, q) * s->f / MAX (s->feather, 1e-3f);
}

/* fill a shape over its bounding box (in final pixels) */
static void
cb_fill (LrgImageCanvas *canvas,
         CbShape        *s,
         gfloat          f,
         gfloat          x0,
         gfloat          y0,
         gfloat          x1,
         gfloat          y1,
         const GrlColor *color)
{
	gfloat pad;

	s->f = f;
	if (s->feather <= 0.0f)
		s->feather = 1.0f;
	pad = s->feather + 2.0f;
	lrg_image_canvas_fill_sdf_rect (canvas,
	                                (gint) floorf ((x0 - pad) * f), (gint) floorf ((y0 - pad) * f),
	                                (gint) ceilf ((x1 - x0 + 2.0f * pad) * f),
	                                (gint) ceilf ((y1 - y0 + 2.0f * pad) * f),
	                                cb_shape_sdf, s, color);
}

static void
cb_circle (LrgImageCanvas *canvas,
           gfloat          f,
           gfloat          cx,
           gfloat          cy,
           gfloat          r,
           gfloat          feather,
           const GrlColor *color)
{
	CbShape s;

	memset (&s, 0, sizeof (s));
	s.kind = CB_SHAPE_CIRCLE;
	s.a[0] = cx;
	s.a[1] = cy;
	s.r = r;
	s.feather = feather;
	cb_fill (canvas, &s, f, cx - r, cy - r, cx + r, cy + r, color);
}

static void
cb_ellipse (LrgImageCanvas *canvas,
            gfloat          f,
            gfloat          cx,
            gfloat          cy,
            gfloat          rx,
            gfloat          ry,
            gfloat          feather,
            const GrlColor *color)
{
	CbShape s;

	memset (&s, 0, sizeof (s));
	s.kind = CB_SHAPE_ELLIPSE;
	s.a[0] = cx;
	s.a[1] = cy;
	s.b[0] = rx;
	s.b[1] = ry;
	s.feather = feather;
	cb_fill (canvas, &s, f, cx - rx, cy - ry, cx + rx, cy + ry, color);
}

static void
cb_round_box (LrgImageCanvas *canvas,
              gfloat          f,
              gfloat          x,
              gfloat          y,
              gfloat          w,
              gfloat          h,
              gfloat          radius,
              gfloat          feather,
              const GrlColor *color)
{
	CbShape s;

	memset (&s, 0, sizeof (s));
	s.kind = CB_SHAPE_ROUND_BOX;
	s.a[0] = x + w * 0.5f;
	s.a[1] = y + h * 0.5f;
	s.b[0] = w * 0.5f;
	s.b[1] = h * 0.5f;
	s.r = radius;
	s.feather = feather;
	cb_fill (canvas, &s, f, x, y, x + w, y + h, color);
}

static void
cb_capsule (LrgImageCanvas *canvas,
            gfloat          f,
            gfloat          ax,
            gfloat          ay,
            gfloat          bx,
            gfloat          by,
            gfloat          r,
            const GrlColor *color)
{
	CbShape s;

	memset (&s, 0, sizeof (s));
	s.kind = CB_SHAPE_CAPSULE;
	s.a[0] = ax;
	s.a[1] = ay;
	s.b[0] = bx;
	s.b[1] = by;
	s.r = r;
	s.feather = 1.0f;
	cb_fill (canvas, &s, f, MIN (ax, bx) - r, MIN (ay, by) - r, MAX (ax, bx) + r,
	         MAX (ay, by) + r, color);
}

/* ==========================================================================
 * Post stacks
 * ========================================================================== */

/*
 * cb_post_new:
 * Builds a processor from a NULL-terminated list of effects (borrowed),
 * giving them rising priorities in the order passed, which is the
 * order they run in.
 */
static LrgPostProcessor *
cb_post_new (LrgPostEffect *first,
             ...)
{
	LrgPostProcessor *post;
	LrgPostEffect    *effect;
	va_list           ap;
	gint              priority;

	post = lrg_post_processor_new (1, 1);
	priority = 10;
	va_start (ap, first);
	for (effect = first; effect != NULL; effect = va_arg (ap, LrgPostEffect *))
	{
		lrg_post_effect_set_priority (effect, priority);
		lrg_post_processor_add_effect (post, effect);
		priority += 10;
	}
	va_end (ap);
	return post;
}

/* run a processor on an 8-bit image, warning on failure */
static void
cb_post_image (LrgPostProcessor *post,
               GrlImage         *image)
{
	g_autoptr(GError) error = NULL;

	if (!lrg_post_processor_apply_to_image (post, image, 0.0f, &error))
		g_warning ("cookbook: post stack failed: %s", error->message);
}

/*
 * cb_post_hdr:
 * Runs a processor on an HDR canvas' float data in place.  The canvas
 * stores premultiplied alpha and the post stack expects straight
 * alpha; for an opaque canvas (every cookbook HDR scene) the two are
 * the same, so the data goes in as it is.
 */
static void
cb_post_hdr (LrgPostProcessor *post,
             LrgHdrCanvas     *canvas)
{
	g_autoptr(GError) error = NULL;
	gfloat           *data;

	data = lrg_hdr_canvas_get_data (canvas, NULL);
	if (!lrg_post_processor_apply_to_buffer (post, data, lrg_hdr_canvas_get_width (canvas),
	                                         lrg_hdr_canvas_get_height (canvas), 0.0f, &error))
		g_warning ("cookbook: post stack failed: %s", error->message);
}

/* the finishing pass most 8-bit scenes share: vignette and grain */
static void
cb_finish (GrlImage *image,
           gfloat    vignette,
           gfloat    grain)
{
	g_autoptr(LrgVignette)      vig = lrg_vignette_new ();
	g_autoptr(LrgFilmGrain)     gr = lrg_film_grain_new ();
	g_autoptr(LrgPostProcessor) post = NULL;

	lrg_vignette_set_intensity (vig, vignette);
	lrg_vignette_set_radius (vig, 0.45f);
	lrg_vignette_set_smoothness (vig, 0.6f);
	lrg_film_grain_set_intensity (gr, grain);
	lrg_film_grain_set_size (gr, 1.0f);
	post = cb_post_new (LRG_POST_EFFECT (vig), LRG_POST_EFFECT (gr), NULL);
	cb_post_image (post, image);
}

/* bloom on an HDR canvas (linear light, threshold in linear units) */
static void
cb_bloom_hdr (LrgHdrCanvas *canvas,
              gfloat        threshold,
              gfloat        intensity,
              gfloat        blur_size)
{
	g_autoptr(LrgBloom)         bloom = lrg_bloom_new ();
	g_autoptr(LrgPostProcessor) post = NULL;

	lrg_bloom_set_threshold (bloom, threshold);
	lrg_bloom_set_intensity (bloom, intensity);
	lrg_bloom_set_blur_size (bloom, blur_size);
	lrg_bloom_set_iterations (bloom, 6);
	post = cb_post_new (LRG_POST_EFFECT (bloom), NULL);
	cb_post_hdr (post, canvas);
}

/* ==========================================================================
 * Image helpers
 * ========================================================================== */

/* copy a w x h block of src at (sx, sy) to dst at (dx, dy); RGBA8 */
static void
cb_blit (GrlImage *dst,
         GrlImage *src,
         gint      sx,
         gint      sy,
         gint      w,
         gint      h,
         gint      dx,
         gint      dy)
{
	guint8 *d;
	guint8 *s;
	gint    dw;
	gint    sw;
	gint    row;

	d = grl_image_get_pixels (dst, NULL);
	s = grl_image_get_pixels (src, NULL);
	dw = grl_image_get_width (dst);
	sw = grl_image_get_width (src);
	if (d == NULL || s == NULL)
		return;
	for (row = 0; row < h; row++)
		memcpy (d + ((gsize) (dy + row) * dw + dx) * 4, s + ((gsize) (sy + row) * sw + sx) * 4,
		        (gsize) w * 4);
}

/* a dark rounded label with centred text, inside box (final pixels) */
static void
cb_label (LrgImageCanvas     *canvas,
          GrlImageFont       *font,
          const gchar        *text,
          const GrlRectangle *box,
          gfloat              f)
{
	GrlColor     pill;
	GrlColor     ink;
	GrlRectangle inner;

	cb_color (&pill, 8, 10, 20, 170);
	cb_color (&ink, 245, 245, 250, 255);
	cb_round_box (canvas, f, box->x, box->y, box->width, box->height, box->height * 0.5f, 1.0f,
	              &pill);
	inner.x = box->x + box->height * 0.4f;
	inner.y = box->y + box->height * 0.18f;
	inner.width = box->width - box->height * 0.8f;
	inner.height = box->height * 0.64f;
	cb_text_in_box (canvas, font, text, &inner, f, 1000.0f, 0.5f, &ink);
}

/* store or drop the layout */
static void
cb_return_layout (LrgFrameLayout  *layout,
                  LrgFrameLayout **out_layout)
{
	g_autoptr(GError) error = NULL;

	if (!lrg_frame_layout_validate (layout, &error))
		g_warning ("cookbook: layout problems:\n%s", error->message);
	if (out_layout != NULL)
		*out_layout = g_object_ref (layout);
}

static void
cb_no_layout (LrgFrameLayout **out_layout)
{
	if (out_layout != NULL)
		*out_layout = NULL;
}

/* SCENES */
/* ==========================================================================
 * 1. aa-shapes: the quality preset and supersampling, nothing else
 * ========================================================================== */

/* a rounded rectangle as a path (final-image coordinates) */
static GrlPath *
cb_path_round_rect (gfloat x,
                    gfloat y,
                    gfloat w,
                    gfloat h,
                    gfloat r)
{
	GrlPath *path = grl_path_new ();

	grl_path_move_to (path, x + r, y);
	grl_path_line_to (path, x + w - r, y);
	grl_path_quad_to (path, x + w, y, x + w, y + r);
	grl_path_line_to (path, x + w, y + h - r);
	grl_path_quad_to (path, x + w, y + h, x + w - r, y + h);
	grl_path_line_to (path, x + r, y + h);
	grl_path_quad_to (path, x, y + h, x, y + h - r);
	grl_path_line_to (path, x, y + r);
	grl_path_quad_to (path, x, y, x + r, y);
	grl_path_close (path);
	return path;
}

GrlImage *
lrg_cookbook_aa_shapes (guint            width,
                        guint            height,
                        LrgFrameLayout **out_layout)
{
	g_autoptr(LrgImageCanvas) canvas = NULL;
	g_autoptr(GrlPath)        card = NULL;
	g_autoptr(GrlPath)        star = NULL;
	GrlColor                  bg;
	GrlColor                  top;
	GrlColor                  bottom;
	GrlColor                  glow;
	GrlColor                  clear;
	GrlColor                  c;
	gfloat                    u;
	gfloat                    cx;
	gfloat                    cy;
	gint                      f;
	gint                      i;

	cb_no_layout (out_layout);
	if (!cb_size_ok (width, height))
		return NULL;
	u = cb_unit (width, height);

	/* --- 1. a 3x supersampled canvas with the quality preset --- */
	cb_color (&bg, 18, 20, 38, 255);
	canvas = lrg_image_canvas_new_supersampled ((gint) width, (gint) height, 3, &bg);
	f = (gint) lrg_image_canvas_get_supersample (canvas);

	/* --- 2. background: gradients ignore the transform, so large pixels --- */
	cb_color (&top, 28, 34, 72, 255);
	cb_color (&bottom, 10, 10, 22, 255);
	cb_color (&glow, 120, 140, 255, 70);
	cb_color (&clear, 120, 140, 255, 0);
	lrg_image_canvas_draw_gradient_rect (canvas, 0, 0, (gint) width * f, (gint) height * f,
	                                     &top, &bottom, GRL_GRADIENT_AXIS_VERTICAL);
	lrg_image_canvas_draw_gradient_radial (canvas, (gint) (width * 0.3f) * f,
	                                       (gint) (height * 0.35f) * f,
	                                       (gint) (300.0f * u) * f, &glow, &clear);

	/* --- 3. everything else in final-size coordinates --- */
	lrg_image_canvas_scale (canvas, (gfloat) f, (gfloat) f);
	cx = width * 0.5f;
	cy = height * 0.5f;

	/* a translucent card behind the circles (path with rounded corners) */
	card = cb_path_round_rect (cx - 250.0f * u, cy - 150.0f * u, 500.0f * u, 300.0f * u, 28.0f * u);
	cb_color (&c, 255, 255, 255, 14);
	lrg_image_canvas_fill_path (canvas, card, GRL_FILL_RULE_NONZERO, &c);
	cb_color (&c, 255, 255, 255, 90);
	lrg_image_canvas_stroke_path (canvas, card, 1, &c);

	/* three translucent circles: overlaps blend in linear light */
	cb_color (&c, 255, 70, 90, 150);
	lrg_image_canvas_fill_circle (canvas, (gint) (cx - 95.0f * u), (gint) (cy - 20.0f * u),
	                              (gint) (78.0f * u), &c);
	cb_color (&c, 60, 220, 140, 150);
	lrg_image_canvas_fill_circle (canvas, (gint) (cx - 35.0f * u), (gint) (cy + 60.0f * u),
	                              (gint) (78.0f * u), &c);
	cb_color (&c, 70, 140, 255, 150);
	lrg_image_canvas_fill_circle (canvas, (gint) (cx + 25.0f * u), (gint) (cy - 20.0f * u),
	                              (gint) (78.0f * u), &c);

	/* a five-pointed star (non-convex path) */
	star = grl_path_new ();
	for (i = 0; i < 10; i++)
	{
		gfloat a = -G_PI_F * 0.5f + (gfloat) i * G_PI_F / 5.0f;
		gfloat r = (i % 2 == 0 ? 62.0f : 26.0f) * u;
		gfloat px = cx + 165.0f * u + cosf (a) * r;
		gfloat py = cy + 10.0f * u + sinf (a) * r;

		if (i == 0)
			grl_path_move_to (star, px, py);
		else
			grl_path_line_to (star, px, py);
	}
	grl_path_close (star);
	cb_color (&c, 255, 200, 70, 230);
	lrg_image_canvas_fill_path (canvas, star, GRL_FILL_RULE_NONZERO, &c);

	/* string art from hairlines, and thin rings: the hardest case for aliasing */
	for (i = 0; i < 28; i++)
	{
		gfloat a = (gfloat) i / 27.0f;

		cb_color (&c, (guint8) (120 + 135 * a), (guint8) (210 - 90 * a), 255, 210);
		lrg_image_canvas_draw_line (canvas, (gint) (cx - 232.0f * u + 150.0f * u * a),
		                            (gint) (cy + 135.0f * u),
		                            (gint) (cx - 232.0f * u),
		                            (gint) (cy + 135.0f * u - 95.0f * u * (1.0f - a)), 1, &c);
	}
	for (i = 0; i < 3; i++)
	{
		cb_color (&c, 255, 255, 255, (guint8) (190 - 50 * i));
		lrg_image_canvas_stroke_circle (canvas, (gint) (cx + 165.0f * u), (gint) (cy + 10.0f * u),
		                                (gint) ((72.0f + 7.0f * i) * u), 1, &c);
	}

	/* --- 4. one downsample: gamma-correct, premultiplied, Lanczos-3 --- */
	return lrg_image_canvas_resolve (canvas, LRG_RESAMPLE_FILTER_LANCZOS3);
}

/* ==========================================================================
 * 2. glow-title: text, bloom, tone map
 * ========================================================================== */

GrlImage *
lrg_cookbook_glow_title (guint            width,
                         guint            height,
                         LrgFrameLayout **out_layout)
{
	g_autoptr(LrgFrameLayout)   layout = NULL;
	g_autoptr(LrgImageCanvas)   canvas = NULL;
	g_autoptr(GrlImageFont)     bold = NULL;
	g_autoptr(GrlImageFont)     regular = NULL;
	g_autoptr(LrgBloom)         bloom = NULL;
	g_autoptr(LrgTonemap)       tonemap = NULL;
	g_autoptr(LrgVignette)      vignette = NULL;
	g_autoptr(LrgFilmGrain)     grain = NULL;
	g_autoptr(LrgPostProcessor) post = NULL;
	GrlImage                   *image;
	GrlRectangle                title;
	GrlRectangle                subtitle;
	GrlColor                    c;
	GrlColor                    c2;
	gfloat                      w;
	gfloat                      h;
	gfloat                      horizon;
	gint                        f;
	gint                        i;

	if (out_layout != NULL)
		*out_layout = NULL;
	if (!cb_size_ok (width, height))
		return NULL;
	w = (gfloat) width;
	h = (gfloat) height;
	horizon = h * 0.64f;

	/* --- 1. layout: title centred, subtitle below it --- */
	layout = lrg_frame_layout_new (w, h);
	lrg_frame_layout_set_safe_area_percent (layout, 0.08f);
	lrg_frame_layout_add_box (layout, "title", LRG_HUD_ANCHOR_CENTER, 0.0f, -h * 0.12f,
	                          w * 0.76f, h * 0.24f);
	lrg_frame_layout_add_box_relative (layout, "subtitle", "title", LRG_FRAME_SIDE_BELOW,
	                                   h * 0.03f, w * 0.56f, h * 0.075f, LRG_FRAME_ALIGN_CENTER);
	lrg_frame_layout_get_box (layout, "title", &title);
	lrg_frame_layout_get_box (layout, "subtitle", &subtitle);

	/* --- 2. background on a 3x canvas: night sky, horizon glow, grid floor --- */
	cb_color (&c, 3, 2, 12, 255);
	canvas = lrg_image_canvas_new_supersampled ((gint) width, (gint) height, 3, &c);
	f = (gint) lrg_image_canvas_get_supersample (canvas);
	cb_color (&c2, 34, 8, 52, 255);
	lrg_image_canvas_draw_gradient_rect (canvas, 0, 0, (gint) width * f, (gint) horizon * f,
	                                     &c, &c2, GRL_GRADIENT_AXIS_VERTICAL);
	cb_color (&c, 255, 50, 160, 40);
	cb_color (&c2, 255, 50, 160, 0);
	lrg_image_canvas_draw_gradient_radial (canvas, (gint) (w * 0.5f) * f, (gint) horizon * f,
	                                       (gint) (w * 0.42f) * f, &c, &c2);

	lrg_image_canvas_scale (canvas, (gfloat) f, (gfloat) f);
	for (i = 1; i <= 9; i++)
	{
		gfloat t = (gfloat) i / 9.0f;
		gfloat y = horizon + (h - horizon) * t * t;

		cb_color (&c, 255, 60, 190, (guint8) (40 + 120 * t));
		lrg_image_canvas_draw_line (canvas, 0, (gint) y, (gint) w, (gint) y, 1, &c);
	}
	for (i = -12; i <= 12; i++)
	{
		cb_color (&c, 255, 60, 190, 110);
		lrg_image_canvas_draw_line (canvas, (gint) (w * 0.5f + i * w * 0.012f), (gint) horizon,
		                            (gint) (w * 0.5f + i * w * 0.11f), (gint) h, 1, &c);
	}
	cb_color (&c, 255, 150, 230, 255);
	lrg_image_canvas_draw_line (canvas, 0, (gint) horizon, (gint) w, (gint) horizon, 1, &c);

	/* --- 3. text in its boxes (large pixels; fitted to the box) --- */
	bold = lrg_cookbook_load_font (TRUE);
	regular = lrg_cookbook_load_font (FALSE);
	cb_color (&c, 255, 90, 215, 255);
	cb_text_in_box (canvas, bold, "NEON NIGHTS", &title, (gfloat) f, 1000.0f, 0.5f, &c);
	cb_color (&c, 90, 230, 255, 255);
	cb_text_in_box (canvas, regular, "bloom \xc2\xb7 tonemap \xc2\xb7 vignette \xc2\xb7 grain",
	                &subtitle, (gfloat) f, 1000.0f, 0.5f, &c);
	image = lrg_image_canvas_resolve (canvas, LRG_RESAMPLE_FILTER_LANCZOS3);

	/* --- 4. the post stack on the 8-bit image, in this order --- */
	bloom = lrg_bloom_new ();
	lrg_bloom_set_threshold (bloom, 0.45f);
	lrg_bloom_set_intensity (bloom, 2.2f);
	lrg_bloom_set_blur_size (bloom, 5.0f);
	lrg_bloom_set_iterations (bloom, 6);
	tonemap = lrg_tonemap_new ();
	lrg_tonemap_set_operator (tonemap, LRG_TONEMAP_OPERATOR_ACES);
	lrg_tonemap_set_exposure (tonemap, 0.2f);
	vignette = lrg_vignette_new ();
	lrg_vignette_set_intensity (vignette, 0.55f);
	lrg_vignette_set_smoothness (vignette, 0.6f);
	grain = lrg_film_grain_new ();
	lrg_film_grain_set_intensity (grain, 0.05f);
	lrg_film_grain_set_size (grain, 1.0f);
	post = cb_post_new (LRG_POST_EFFECT (bloom), LRG_POST_EFFECT (tonemap),
	                    LRG_POST_EFFECT (vignette), LRG_POST_EFFECT (grain), NULL);
	if (image != NULL)
		cb_post_image (post, image);

	cb_return_layout (layout, out_layout);
	return image;
}

/* ==========================================================================
 * 3. sdf-character-bust: 3D SDFs raymarched into an HDR canvas
 * ========================================================================== */

typedef enum
{
	BUST_SKIN,
	BUST_LIPS,
	BUST_HAIR,
	BUST_EYE,
	BUST_SHIRT,
	BUST_WALL
} BustMaterial;

/* the bust: distance and material at p (mat may be NULL) */
static gfloat
bust_map (const gfloat  p[3],
          BustMaterial *mat)
{
	static const gfloat head_r[3] = { 0.34f, 0.42f, 0.37f };
	static const gfloat jaw_r[3] = { 0.26f, 0.2f, 0.29f };
	static const gfloat ear_r[3] = { 0.055f, 0.1f, 0.045f };
	static const gfloat hair_r[3] = { 0.37f, 0.29f, 0.40f };
	static const gfloat nose_r[3] = { 0.045f, 0.07f, 0.05f };
	static const gfloat mouth_a[3] = { 0.0f, 0.29f, 0.366f };
	static const gfloat mouth_b[3] = { 0.07f, 0.30f, 0.345f };
	static const gfloat brow_a[3] = { 0.07f, 0.605f, 0.33f };
	static const gfloat brow_b[3] = { 0.18f, 0.615f, 0.29f };
	static const gfloat torso_r[3] = { 0.66f, 0.36f, 0.34f };
	static const gfloat neck_a[3] = { 0.0f, -0.1f, 0.0f };
	static const gfloat neck_b[3] = { 0.0f, 0.2f, 0.0f };
	gfloat q[3];
	gfloat skin;
	gfloat hair;
	gfloat eye;
	gfloat torso;
	gfloat wall;
	gfloat mouth;
	gfloat d;

	/* the wall behind the figure */
	wall = p[2] + 0.95f;

	/* skin: head, jaw, nose, ears and neck, smoothly joined */
	v3_set (q, p[0], p[1] - 0.48f, p[2]);
	skin = lrg_sdf3_ellipsoid (q, head_r);
	v3_set (q, p[0], p[1] - 0.27f, p[2] - 0.05f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_ellipsoid (q, jaw_r), 0.14f);
	v3_set (q, p[0], p[1] - 0.41f, p[2] - 0.355f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_ellipsoid (q, nose_r), 0.05f);
	v3_set (q, fabsf (p[0]) - 0.335f, p[1] - 0.45f, p[2] + 0.01f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_ellipsoid (q, ear_r), 0.04f);
	skin = lrg_sdf_smooth_union (skin, lrg_sdf3_capsule (p, neck_a, neck_b, 0.13f), 0.08f);

	/* a smile carved into the skin with a smooth subtraction */
	v3_set (q, fabsf (p[0]), p[1], p[2]);
	mouth = lrg_sdf3_capsule (q, mouth_a, mouth_b, 0.015f);
	skin = lrg_sdf_smooth_subtract (skin, mouth, 0.01f);

	/* hair cap and brows, eyes (mirrored), shirt */
	v3_set (q, p[0], p[1] - 0.69f, p[2] + 0.07f);
	hair = lrg_sdf3_ellipsoid (q, hair_r);
	v3_set (q, fabsf (p[0]), p[1], p[2]);
	hair = lrg_sdf_union (hair, lrg_sdf3_capsule (q, brow_a, brow_b, 0.019f));
	v3_set (q, fabsf (p[0]) - 0.125f, p[1] - 0.52f, p[2] - 0.29f);
	eye = lrg_sdf3_sphere (q, 0.065f);
	v3_set (q, p[0], p[1] + 0.42f, p[2]);
	torso = lrg_sdf3_ellipsoid (q, torso_r);

	d = lrg_sdf_smooth_union (skin, torso, 0.06f);
	d = lrg_sdf_smooth_union (d, hair, 0.03f);
	d = lrg_sdf_union (d, eye);
	d = lrg_sdf_union (d, wall);

	/* the material is the nearest part */
	if (mat != NULL)
	{
		gfloat best = skin;

		*mat = mouth < 0.012f ? BUST_LIPS : BUST_SKIN;
		if (hair < best) { best = hair; *mat = BUST_HAIR; }
		if (torso < best) { best = torso; *mat = BUST_SHIRT; }
		if (eye < best) { best = eye; *mat = BUST_EYE; }
		if (wall < best) { *mat = BUST_WALL; }
	}
	return d;
}

static gfloat
bust_sdf (const gfloat p[3],
          gpointer     user_data)
{
	/*
	 * Ellipsoids and smooth unions give distance estimates, not exact
	 * distances; scaling by 0.8 keeps the marcher and the soft shadow
	 * from overshooting (which shows as contour lines).
	 */
	return bust_map (p, NULL) * 0.8f;
}

/*
 * bust_shade:
 * One sample: raymarch, then key light with a soft shadow, sky fill
 * with ambient occlusion, a cool rim light and a little specular.
 */
static void
bust_shade (gfloat   x,
            gfloat   y,
            gfloat  *out,
            gpointer user_data)
{
	const CbCamera *cam = user_data;
	gfloat          rd[3];
	gfloat          p[3];
	gfloat          n[3];
	gfloat          key[3];
	gfloat          rim[3];
	gfloat          half[3];
	gfloat          albedo[3];
	gfloat          lifted[3];
	gfloat          t;
	gfloat          ndl;
	gfloat          shadow;
	gfloat          ao;
	gfloat          fres;
	gfloat          spec;
	gfloat          gloss;
	gfloat          sky;
	BustMaterial    mat;
	gint            i;

	cb_camera_ray (cam, x, y, rd);
	out[3] = 1.0f;
	if (!lrg_sdf3_raymarch (bust_sdf, NULL, cam->eye, rd, 12.0f, 200, &t))
	{
		v3_set (out, 0.05f, 0.05f, 0.06f);
		return;
	}
	v3_madd (p, cam->eye, rd, t);
	lrg_sdf3_normal (bust_sdf, NULL, p, 1e-3f, n);
	bust_map (p, &mat);

	/* material */
	gloss = 0.0f;
	switch (mat)
	{
	case BUST_SKIN:  v3_set (albedo, 0.72f, 0.42f, 0.30f); gloss = 0.25f; break;
	case BUST_LIPS:  v3_set (albedo, 0.42f, 0.13f, 0.11f); gloss = 0.3f; break;
	case BUST_HAIR:  v3_set (albedo, 0.16f, 0.08f, 0.05f); gloss = 0.4f; break;
	case BUST_SHIRT: v3_set (albedo, 0.05f, 0.11f, 0.22f); gloss = 0.1f; break;
	case BUST_EYE:
	{
		/* iris and pupil from the angle to the eye's forward axis */
		gfloat ez = (p[2] - 0.29f) / 0.065f;

		if (ez > 0.93f)
			v3_set (albedo, 0.02f, 0.02f, 0.02f);
		else if (ez > 0.78f)
			v3_set (albedo, 0.10f, 0.28f, 0.35f);
		else
			v3_set (albedo, 0.85f, 0.85f, 0.82f);
		gloss = 1.0f;
		break;
	}
	case BUST_WALL:
	default:
		v3_set (albedo, 0.42f, 0.36f, 0.33f);
		break;
	}

	/* key light: wrapped diffuse for a soft terminator, soft shadow */
	v3_set (key, -0.55f, 0.55f, 0.65f);
	v3_normalize (key);
	ndl = cb_clampf ((v3_dot (n, key) + 0.2f) / 1.2f, 0.0f, 1.0f);
	v3_madd (lifted, p, n, 0.01f);
	shadow = ndl > 0.0f ? lrg_sdf3_soft_shadow (bust_sdf, NULL, lifted, key, 0.02f, 4.0f, 8.0f) : 0.0f;
	ao = lrg_sdf3_ambient_occlusion (bust_sdf, NULL, p, n);
	sky = 0.55f + 0.45f * n[1];

	/* rim light from behind-right, strongest at grazing angles */
	v3_set (rim, 0.8f, 0.35f, -0.5f);
	v3_normalize (rim);
	fres = powf (1.0f - cb_clampf (-v3_dot (n, rd), 0.0f, 1.0f), 3.0f);

	/* Blinn-Phong highlight of the key light */
	for (i = 0; i < 3; i++)
		half[i] = key[i] - rd[i];
	v3_normalize (half);
	spec = gloss * powf (cb_clampf (v3_dot (n, half), 0.0f, 1.0f), 10.0f + 90.0f * gloss) * shadow;

	for (i = 0; i < 3; i++)
	{
		static const gfloat key_c[3] = { 2.1f, 1.9f, 1.6f };
		static const gfloat sky_c[3] = { 0.30f, 0.36f, 0.50f };
		static const gfloat rim_c[3] = { 1.4f, 2.0f, 3.2f };

		out[i] = albedo[i] * (key_c[i] * ndl * shadow + sky_c[i] * sky * ao)
		       + rim_c[i] * fres * cb_clampf (v3_dot (n, rim) + 0.3f, 0.0f, 1.0f) * ao
		         * (mat == BUST_WALL ? 0.0f : 1.0f)
		       + key_c[i] * spec;
	}

	/* a pool of light on the wall behind the head */
	if (mat == BUST_WALL)
	{
		gfloat dx = p[0] + 0.25f;
		gfloat dy = p[1] - 0.45f;
		gfloat pool = expf (-(dx * dx + dy * dy) * 1.4f);

		for (i = 0; i < 3; i++)
			out[i] *= 0.45f + 0.9f * pool;
	}
}

GrlImage *
lrg_cookbook_sdf_character_bust (guint            width,
                                 guint            height,
                                 LrgFrameLayout **out_layout)
{
	g_autoptr(LrgHdrCanvas) canvas = NULL;
	CbCamera                cam;
	gfloat                  eye[3] = { 0.55f, 0.42f, 3.1f };
	gfloat                  target[3] = { 0.0f, 0.15f, 0.0f };

	cb_no_layout (out_layout);
	if (!cb_size_ok (width, height))
		return NULL;

	/* shade with 2 x 2 stratified samples, ACES to 8 bits with dither */
	cb_camera_init (&cam, eye, target, 34.0f, width, height);
	canvas = lrg_hdr_canvas_new (width, height);
	lrg_hdr_canvas_shade (canvas, bust_shade, &cam, 2);
	return lrg_hdr_canvas_to_image (canvas, LRG_TONEMAP_OPERATOR_ACES, 0.0f, TRUE);
}

/* ==========================================================================
 * 4. product-shot: an SDF object on a studio floor
 * ========================================================================== */

typedef enum
{
	SHOT_FLOOR,
	SHOT_GLASS,
	SHOT_GOLD,
	SHOT_PEARL
} ShotMaterial;

static gfloat
shot_map (const gfloat  p[3],
          ShotMaterial *mat)
{
	static const gfloat body_b[3] = { 0.30f, 0.42f, 0.15f };
	static const gfloat cap_b[3] = { 0.12f, 0.10f, 0.12f };
	gfloat              q[3];
	gfloat              ca;
	gfloat              sa;
	gfloat              glass;
	gfloat              gold;
	gfloat              pearl;
	gfloat              floor_d;
	gfloat              d;

	/* the bottle stands turned 25 degrees about y */
	ca = 0.906f;
	sa = 0.423f;
	v3_set (q, ca * (p[0] + 0.2f) - sa * p[2], p[1] - 0.42f, sa * (p[0] + 0.2f) + ca * p[2]);
	glass = lrg_sdf3_round_box (q, body_b, 0.09f);
	q[1] = p[1] - 0.80f;
	glass = lrg_sdf_smooth_union (glass, lrg_sdf3_round_cone (q, 0.075f, 0.06f, 0.12f), 0.05f);
	q[1] = p[1] - 1.03f;
	gold = lrg_sdf3_round_box (q, cap_b, 0.03f);

	/* two pearls in front */
	v3_set (q, p[0] - 0.42f, p[1] - 0.12f, p[2] - 0.32f);
	pearl = lrg_sdf3_sphere (q, 0.12f);
	v3_set (q, p[0] - 0.66f, p[1] - 0.075f, p[2] - 0.06f);
	pearl = lrg_sdf_union (pearl, lrg_sdf3_sphere (q, 0.075f));

	floor_d = p[1];
	d = lrg_sdf_union (lrg_sdf_union (glass, gold), lrg_sdf_union (pearl, floor_d));
	if (mat != NULL)
	{
		*mat = SHOT_FLOOR;
		if (glass <= d + 1e-5f)
			*mat = SHOT_GLASS;
		else if (gold <= d + 1e-5f)
			*mat = SHOT_GOLD;
		else if (pearl <= d + 1e-5f)
			*mat = SHOT_PEARL;
	}
	return d;
}

static gfloat
shot_sdf (const gfloat p[3],
          gpointer     user_data)
{
	return shot_map (p, NULL);
}

/* the studio around the object: a soft gradient with a large softbox */
static void
shot_env (const gfloat dir[3],
          gfloat       out[3])
{
	gfloat up;
	gfloat box;

	up = cb_clampf (dir[1] * 0.5f + 0.5f, 0.0f, 1.0f);
	box = cb_smoothstep (0.55f, 0.75f, -dir[0]) * cb_smoothstep (0.05f, 0.25f, dir[1]);
	out[0] = 0.10f + 0.55f * up + 5.0f * box;
	out[1] = 0.10f + 0.52f * up + 5.0f * box;
	out[2] = 0.12f + 0.50f * up + 5.0f * box;
}

static void
shot_shade (gfloat   x,
            gfloat   y,
            gfloat  *out,
            gpointer user_data)
{
	const CbCamera *cam = user_data;
	gfloat          rd[3];
	gfloat          p[3];
	gfloat          n[3];
	gfloat          key[3];
	gfloat          refl[3];
	gfloat          env[3];
	gfloat          lifted[3];
	gfloat          bg[3];
	gfloat          top[3];
	gfloat          half[3];
	gfloat          albedo[3];
	gfloat          pool;
	gfloat          glint;
	gfloat          t;
	gfloat          ndl;
	gfloat          shadow;
	gfloat          ao;
	gfloat          fres;
	gfloat          spec_k;
	gfloat          fog;
	ShotMaterial    mat;
	gint            i;

	/* the backdrop colour behind everything */
	cb_camera_ray (cam, x, y, rd);
	out[3] = 1.0f;
	v3_set (bg, 0.075f, 0.075f, 0.085f);
	v3_set (top, 0.02f, 0.02f, 0.025f);
	v3_mix (bg, bg, top, cb_clampf (rd[1] * 3.0f, 0.0f, 1.0f));
	if (!lrg_sdf3_raymarch (shot_sdf, NULL, cam->eye, rd, 30.0f, 200, &t))
	{
		memcpy (out, bg, sizeof (bg));
		return;
	}
	v3_madd (p, cam->eye, rd, t);
	lrg_sdf3_normal (shot_sdf, NULL, p, 1e-3f, n);
	shot_map (p, &mat);

	/* the key: a big soft light from the upper left */
	v3_set (key, -0.55f, 0.75f, 0.45f);
	v3_normalize (key);
	ndl = cb_clampf (v3_dot (n, key), 0.0f, 1.0f);
	v3_madd (lifted, p, n, 0.005f);
	shadow = ndl > 0.0f ? lrg_sdf3_soft_shadow (shot_sdf, NULL, lifted, key, 0.01f, 6.0f, 6.0f) : 0.0f;
	ao = lrg_sdf3_ambient_occlusion (shot_sdf, NULL, p, n);

	/* reflection of the studio, weighted by Schlick's Fresnel */
	v3_reflect (refl, rd, n);
	shot_env (refl, env);
	fres = powf (1.0f - cb_clampf (-v3_dot (n, rd), 0.0f, 1.0f), 5.0f);

	switch (mat)
	{
	case SHOT_GLASS:
		v3_set (albedo, 0.006f, 0.05f, 0.055f);
		spec_k = 0.08f + 0.92f * fres;
		break;
	case SHOT_GOLD:
		v3_set (albedo, 0.05f, 0.035f, 0.01f);
		spec_k = 1.0f;
		env[0] *= 1.0f;
		env[1] *= 0.72f;
		env[2] *= 0.32f;
		break;
	case SHOT_PEARL:
		v3_set (albedo, 0.75f, 0.72f, 0.70f);
		spec_k = 0.04f + 0.6f * fres;
		break;
	case SHOT_FLOOR:
	default:
		v3_set (albedo, 0.42f, 0.41f, 0.42f);
		spec_k = 0.0f;
		break;
	}
	/* the key is a spot: its pool of light falls off around the object */
	pool = 0.08f + 0.92f * expf (-(p[0] * p[0] + p[2] * p[2]) * 0.35f);
	for (i = 0; i < 3; i++)
		half[i] = key[i] - rd[i];
	v3_normalize (half);
	glint = mat == SHOT_FLOOR ? 0.0f
	      : 4.0f * powf (cb_clampf (v3_dot (n, half), 0.0f, 1.0f), 160.0f) * shadow;
	for (i = 0; i < 3; i++)
		out[i] = albedo[i] * (2.2f * ndl * shadow * pool + 0.35f * ao * pool)
		       + spec_k * env[i] * ao + glint;

	/* the floor melts into the backdrop with distance */
	fog = cb_smoothstep (4.0f, 12.0f, t);
	v3_mix (out, out, bg, fog);
}

GrlImage *
lrg_cookbook_product_shot (guint            width,
                           guint            height,
                           LrgFrameLayout **out_layout)
{
	g_autoptr(LrgHdrCanvas) canvas = NULL;
	CbCamera                cam;
	gfloat                  eye[3] = { 0.35f, 1.05f, 3.6f };
	gfloat                  target[3] = { 0.08f, 0.45f, 0.0f };

	cb_no_layout (out_layout);
	if (!cb_size_ok (width, height))
		return NULL;

	/* 3 x 3 stratified samples: the supersampling of the HDR path */
	cb_camera_init (&cam, eye, target, 30.0f, width, height);
	canvas = lrg_hdr_canvas_new (width, height);
	lrg_hdr_canvas_shade (canvas, shot_shade, &cam, 3);
	return lrg_hdr_canvas_to_image (canvas, LRG_TONEMAP_OPERATOR_FILMIC, 0.25f, TRUE);
}

/* ==========================================================================
 * 5. band-limited-texture: fBm with a pixel footprint
 * ========================================================================== */

typedef struct
{
	CbCamera cam;
	gfloat   split_x;   /* pixels left of this use footprint 0 (pitfall) */
} FloorScene;

/* sky radiance in a direction */
static void
floor_sky (const gfloat rd[3],
           gfloat       out[3])
{
	static const gfloat horizon[3] = { 0.95f, 0.55f, 0.32f };
	static const gfloat zenith[3] = { 0.06f, 0.10f, 0.30f };
	gfloat              sun[3] = { 0.35f, 0.25f, -0.9f };
	gfloat              s;

	v3_mix (out, horizon, zenith, powf (cb_clampf (rd[1], 0.0f, 1.0f), 0.5f));
	v3_normalize (sun);
	s = cb_clampf (v3_dot (rd, sun), 0.0f, 1.0f);
	out[0] += 2.5f * powf (s, 200.0f) + 0.35f * powf (s, 8.0f);
	out[1] += 2.2f * powf (s, 200.0f) + 0.25f * powf (s, 8.0f);
	out[2] += 1.8f * powf (s, 200.0f) + 0.12f * powf (s, 8.0f);
}

static void
floor_shade (gfloat   x,
             gfloat   y,
             gfloat  *out,
             gpointer user_data)
{
	const FloorScene *fs = user_data;
	static const gfloat cream[3] = { 0.62f, 0.60f, 0.56f };
	static const gfloat slate[3] = { 0.05f, 0.06f, 0.08f };
	static const gfloat rose[3] = { 0.62f, 0.46f, 0.40f };
	gfloat              rd[3];
	gfloat              sky[3];
	gfloat              refl[3];
	gfloat              albedo[3];
	gfloat              t;
	gfloat              u;
	gfloat              v;
	gfloat              footprint;
	gfloat              n1;
	gfloat              n2;
	gfloat              vein;
	gfloat              fade;
	gfloat              fres;
	gfloat              fog;
	gint                i;

	cb_camera_ray (&fs->cam, x, y, rd);
	out[3] = 1.0f;
	floor_sky (rd, sky);
	if (rd[1] > -1e-4f)
	{
		memcpy (out, sky, sizeof (sky));
		return;
	}

	/* hit the floor plane y = 0; texture coordinates in marble units */
	t = -fs->cam.eye[1] / rd[1];
	u = (fs->cam.eye[0] + rd[0] * t) * 2.5f;
	v = (fs->cam.eye[2] + rd[2] * t) * 2.5f;

	/*
	 * The footprint: how much texture one pixel covers.  Along the
	 * view direction the floor is stretched by 1 / |rd.y| (grazing),
	 * so take that, the larger of the two screen derivatives.
	 */
	footprint = 2.5f * t * fs->cam.pixel_angle / MAX (-rd[1], 1e-3f);
	if (x < fs->split_x)
		footprint = 0.0f;

	/*
	 * Marble: veins are a sine warped by fBm.  The fBm is band-limited
	 * by the library; the sine carrier (about 1.6 cycles per unit) gets
	 * the same fade by hand: full contrast while a cycle spans 4 pixels
	 * or more, none at 2 pixels, where it falls back to its average.
	 */
	n1 = lrg_noise_fbm2 (u, v, 9, 2.0f, 0.5f, footprint, 7u);
	n2 = lrg_noise_fbm2 (u * 0.35f + 3.1f, v * 0.35f, 6, 2.0f, 0.5f, footprint * 0.35f, 11u);
	vein = cb_smoothstep (0.55f, 0.98f, 0.5f + 0.5f * sinf (2.2f * u + 0.8f * v + 5.0f * n1));
	fade = footprint > 0.0f ? 1.0f - cb_smoothstep (0.25f, 0.5f, 1.6f * footprint) : 1.0f;
	vein = cb_mixf (0.22f, vein, fade);
	v3_mix (albedo, cream, rose, cb_clampf (0.5f + n2, 0.0f, 1.0f) * 0.7f);
	v3_mix (albedo, albedo, slate, vein);

	/* diffuse sky light plus a Fresnel reflection of the sky */
	v3_set (refl, rd[0], -rd[1], rd[2]);
	floor_sky (refl, refl);
	fres = 0.04f + 0.96f * powf (1.0f - cb_clampf (-rd[1], 0.0f, 1.0f), 5.0f);
	for (i = 0; i < 3; i++)
		out[i] = albedo[i] * 0.95f * (1.0f - fres) + refl[i] * fres;

	/* aerial perspective towards the horizon */
	fog = 1.0f - expf (-t * 0.012f);
	v3_mix (out, out, sky, fog);
}

static GrlImage *
floor_render (guint  width,
              guint  height,
              gfloat split_x)
{
	g_autoptr(LrgHdrCanvas) canvas = NULL;
	FloorScene              fs;
	gfloat                  eye[3] = { 0.0f, 1.1f, 0.0f };
	gfloat                  target[3] = { 0.0f, 0.62f, -4.0f };

	cb_camera_init (&fs.cam, eye, target, 55.0f, width, height);
	fs.split_x = split_x;
	canvas = lrg_hdr_canvas_new (width, height);
	lrg_hdr_canvas_shade (canvas, floor_shade, &fs, 2);
	return lrg_hdr_canvas_to_image (canvas, LRG_TONEMAP_OPERATOR_ACES, -1.6f, TRUE);
}

GrlImage *
lrg_cookbook_band_limited_texture (guint            width,
                                   guint            height,
                                   LrgFrameLayout **out_layout)
{
	cb_no_layout (out_layout);
	if (!cb_size_ok (width, height))
		return NULL;
	return floor_render (width, height, -1.0f);
}

GrlImage *
lrg_cookbook_pitfall_moire (guint            width,
                            guint            height,
                            LrgFrameLayout **out_layout)
{
	g_autoptr(LrgImageCanvas) canvas = NULL;
	g_autoptr(GrlImageFont)   font = NULL;
	GrlImage                 *image;
	GrlRectangle              box;
	GrlColor                  white;
	gfloat                    u;

	cb_no_layout (out_layout);
	if (!cb_size_ok (width, height))
		return NULL;
	u = cb_unit (width, height);

	/* the left half with footprint 0, the right half band-limited */
	image = floor_render (width, height, (gfloat) width * 0.5f);
	canvas = lrg_image_canvas_new_for_image (image);
	lrg_image_canvas_set_quality (canvas);
	cb_color (&white, 255, 255, 255, 200);
	lrg_image_canvas_fill_rect (canvas, (gint) (width / 2), 0, MAX (1, (gint) u), (gint) height, &white);

	font = lrg_cookbook_load_font (TRUE);
	box.width = 190.0f * u;
	box.height = 30.0f * u;
	box.y = 16.0f * u;
	box.x = width * 0.25f - box.width * 0.5f;
	cb_label (canvas, font, "footprint 0: moire", &box, 1.0f);
	box.x = width * 0.75f - box.width * 0.5f;
	cb_label (canvas, font, "pixel footprint", &box, 1.0f);
	return image;
}

/* ==========================================================================
 * 6. sky-gradient-vignette: a dusk sky in linear light
 * ========================================================================== */

typedef struct
{
	gfloat width;
	gfloat height;
	gfloat sun_x;
	gfloat sun_y;
	gfloat sun_r;
} DuskScene;

static void
dusk_shade (gfloat   x,
            gfloat   y,
            gfloat  *out,
            gpointer user_data)
{
	const DuskScene     *ds = user_data;
	static const gfloat  zenith[3] = { 0.015f, 0.025f, 0.09f };
	static const gfloat  mid[3] = { 0.16f, 0.07f, 0.20f };
	static const gfloat  low[3] = { 1.50f, 0.50f, 0.16f };
	static const gfloat  hills[3][3] = {
		{ 0.32f, 0.13f, 0.20f },
		{ 0.12f, 0.05f, 0.10f },
		{ 0.025f, 0.012f, 0.03f }
	};
	gfloat               t;
	gfloat               p[2];
	gfloat               d;
	gfloat               glow;
	gfloat               disc;
	gint                 layer;
	gint                 i;

	/* the sky: zenith -> violet -> orange at the horizon (y = 0.72 h) */
	t = cb_clampf (y / (ds->height * 0.72f), 0.0f, 1.0f);
	if (t < 0.6f)
		v3_mix (out, zenith, mid, cb_smoothstep (0.0f, 0.6f, t));
	else
		v3_mix (out, mid, low, cb_smoothstep (0.6f, 1.0f, t));

	/* the sun: a hard disc far brighter than white, and its glow */
	p[0] = x - ds->sun_x;
	p[1] = y - ds->sun_y;
	d = lrg_sdf2_circle (p, ds->sun_r);
	disc = lrg_sdf2_coverage (d, 1.0f);
	glow = 1.0f / (1.0f + (d > 0.0f ? d * d : 0.0f) / (ds->sun_r * ds->sun_r * 1.5f));
	out[0] += 60.0f * disc + 1.6f * glow;
	out[1] += 42.0f * disc + 0.8f * glow;
	out[2] += 22.0f * disc + 0.30f * glow;

	/* three hill ridges from 1D band-limited fBm, far to near */
	for (layer = 0; layer < 3; layer++)
	{
		gfloat freq = 2.5f + 2.0f * layer;
		gfloat nx = x / ds->width * freq;
		gfloat ridge;
		gfloat cov;

		ridge = ds->height * (0.70f + 0.06f * layer)
		      - ds->height * (0.07f + 0.02f * layer)
		        * (0.6f + lrg_noise_fbm2 (nx, 3.7f * layer, 6, 2.0f, 0.5f,
		                                  freq / ds->width, 31u + (guint32) layer));
		cov = lrg_sdf2_coverage (ridge - y, 1.0f);
		for (i = 0; i < 3; i++)
			out[i] = cb_mixf (out[i], hills[layer][i], cov);
	}
	out[3] = 1.0f;
}

GrlImage *
lrg_cookbook_sky_gradient_vignette (guint            width,
                                    guint            height,
                                    LrgFrameLayout **out_layout)
{
	g_autoptr(LrgHdrCanvas) canvas = NULL;
	GrlImage               *image;
	DuskScene               ds;

	cb_no_layout (out_layout);
	if (!cb_size_ok (width, height))
		return NULL;

	ds.width = (gfloat) width;
	ds.height = (gfloat) height;
	ds.sun_x = ds.width * 0.66f;
	ds.sun_y = ds.height * 0.60f;
	ds.sun_r = ds.height * 0.045f;

	/* shade, bloom the float buffer, tone-map, then vignette and grain */
	canvas = lrg_hdr_canvas_new (width, height);
	lrg_hdr_canvas_shade (canvas, dusk_shade, &ds, 2);
	cb_bloom_hdr (canvas, 2.0f, 0.3f, 4.0f);
	image = lrg_hdr_canvas_to_image (canvas, LRG_TONEMAP_OPERATOR_ACES, 0.0f, TRUE);
	if (image != NULL)
		cb_finish (image, 0.55f, 0.045f);
	return image;
}

/* ==========================================================================
 * 7. speech-bubble-layout: LrgFrameLayout first, pixels second
 * ========================================================================== */

/* a friendly character drawn inside its box (final pixels) */
static void
cb_draw_character (LrgImageCanvas     *canvas,
                   const GrlRectangle *box,
                   gfloat              f)
{
	GrlColor c;
	gfloat   bx = box->x;
	gfloat   by = box->y;
	gfloat   bw = box->width;
	gfloat   bh = box->height;
	gfloat   cx = bx + bw * 0.5f;
	gfloat   hy = by + bh * 0.34f;
	gfloat   hr = MIN (bw * 0.30f, bh * 0.20f);
	gint     side;

	/* contact shadow, then the body with a soft highlight */
	cb_color (&c, 20, 30, 20, 110);
	cb_ellipse (canvas, f, cx, by + bh * 0.955f, bw * 0.36f, bh * 0.03f, 6.0f, &c);
	cb_color (&c, 236, 112, 58, 255);
	cb_round_box (canvas, f, cx - bw * 0.30f, by + bh * 0.50f, bw * 0.60f, bh * 0.45f, bw * 0.22f,
	              1.0f, &c);
	cb_color (&c, 255, 190, 140, 120);
	cb_ellipse (canvas, f, cx - bw * 0.10f, by + bh * 0.62f, bw * 0.10f, bh * 0.08f, bw * 0.12f, &c);

	/* the arm points at the bubble */
	cb_color (&c, 214, 96, 46, 255);
	cb_capsule (canvas, f, cx + bw * 0.22f, by + bh * 0.62f, bx + bw * 0.93f, by + bh * 0.50f,
	            bw * 0.055f, &c);

	/* antenna and head */
	cb_color (&c, 70, 60, 80, 255);
	cb_capsule (canvas, f, cx, hy - hr, cx + bw * 0.06f, by + bh * 0.07f, MAX (1.0f, bw * 0.015f), &c);
	cb_color (&c, 255, 80, 110, 255);
	cb_circle (canvas, f, cx + bw * 0.06f, by + bh * 0.06f, bw * 0.04f, 1.0f, &c);
	cb_color (&c, 252, 206, 112, 255);
	cb_circle (canvas, f, cx, hy, hr, 1.0f, &c);
	cb_color (&c, 255, 240, 200, 140);
	cb_circle (canvas, f, cx - hr * 0.35f, hy - hr * 0.4f, hr * 0.35f, hr * 0.45f, &c);

	/* eyes looking right, cheeks, smile */
	for (side = -1; side <= 1; side += 2)
	{
		gfloat ex = cx + side * hr * 0.38f;

		cb_color (&c, 255, 255, 255, 255);
		cb_ellipse (canvas, f, ex, hy - hr * 0.08f, hr * 0.22f, hr * 0.27f, 1.0f, &c);
		cb_color (&c, 30, 30, 50, 255);
		cb_circle (canvas, f, ex + hr * 0.08f, hy - hr * 0.04f, hr * 0.12f, 1.0f, &c);
		cb_color (&c, 255, 120, 120, 110);
		cb_circle (canvas, f, ex + side * hr * 0.1f, hy + hr * 0.38f, hr * 0.16f, hr * 0.12f, &c);
	}
	cb_color (&c, 120, 50, 40, 255);
	cb_capsule (canvas, f, cx - hr * 0.2f, hy + hr * 0.45f, cx + hr * 0.22f, hy + hr * 0.42f,
	            MAX (0.8f, hr * 0.06f), &c);
}

/* a speech bubble filling its box; the tail tip touches the box's left edge */
static void
cb_draw_bubble (LrgImageCanvas     *canvas,
                GrlImageFont       *font,
                const gchar        *text,
                const GrlRectangle *box,
                gfloat              f)
{
	CbShape      tail;
	GrlColor     c;
	GrlRectangle inner;
	gfloat       tail_w = box->width * 0.10f;
	gfloat       x0 = box->x + tail_w;
	gfloat       w = box->width - tail_w;
	gfloat       r = box->height * 0.22f;

	/* soft drop shadow, body, tail */
	cb_color (&c, 10, 20, 40, 80);
	cb_round_box (canvas, f, x0 + 4.0f, box->y + 6.0f, w - 6.0f, box->height - 8.0f, r, 8.0f, &c);
	cb_color (&c, 255, 255, 252, 255);
	cb_round_box (canvas, f, x0, box->y, w - 2.0f, box->height - 4.0f, r, 1.0f, &c);
	memset (&tail, 0, sizeof (tail));
	tail.kind = CB_SHAPE_TRIANGLE;
	tail.a[0] = box->x;
	tail.a[1] = box->y + box->height * 0.64f;
	tail.b[0] = x0 + r;
	tail.b[1] = box->y + box->height * 0.36f;
	tail.c[0] = x0 + r;
	tail.c[1] = box->y + box->height * 0.60f;
	tail.feather = 1.0f;
	cb_fill (canvas, &tail, f, box->x, box->y, x0 + r, box->y + box->height, &c);

	/* the text, fitted with padding */
	inner.x = x0 + r * 0.7f;
	inner.y = box->y + box->height * 0.16f;
	inner.width = w - r * 1.4f;
	inner.height = box->height * 0.62f;
	cb_color (&c, 30, 34, 52, 255);
	cb_text_in_box (canvas, font, text, &inner, f, 1000.0f, 0.5f, &c);
}

GrlImage *
lrg_cookbook_speech_bubble_layout (guint            width,
                                   guint            height,
                                   LrgFrameLayout **out_layout)
{
	g_autoptr(LrgFrameLayout) layout = NULL;
	g_autoptr(LrgImageCanvas) canvas = NULL;
	g_autoptr(GrlImageFont)   bold = NULL;
	g_autoptr(GError)         error = NULL;
	GrlImage                 *image;
	GrlRectangle              full;
	GrlRectangle              title;
	GrlRectangle              character;
	GrlRectangle              bubble;
	GrlRectangle              shadow;
	GrlColor                  c;
	GrlColor                  c2;
	gfloat                    w;
	gfloat                    h;
	gint                      f;

	if (out_layout != NULL)
		*out_layout = NULL;
	if (!cb_size_ok (width, height))
		return NULL;
	w = (gfloat) width;
	h = (gfloat) height;

	/* --- 1. the layout: every element gets a named box --- */
	layout = lrg_frame_layout_new (w, h);
	lrg_frame_layout_set_safe_area_percent (layout, 0.06f);
	full.x = 0.0f;
	full.y = 0.0f;
	full.width = w;
	full.height = h;
	lrg_frame_layout_set_box_rect (layout, "backdrop", &full);
	lrg_frame_layout_set_backdrop (layout, "backdrop", TRUE);
	lrg_frame_layout_add_box (layout, "title", LRG_HUD_ANCHOR_TOP, 0.0f, 0.0f, w * 0.70f, h * 0.13f);
	lrg_frame_layout_add_box (layout, "character", LRG_HUD_ANCHOR_BOTTOM_LEFT, w * 0.04f, 0.0f,
	                          w * 0.30f, h * 0.62f);
	lrg_frame_layout_add_box_relative (layout, "bubble", "character", LRG_FRAME_SIDE_RIGHT,
	                                   w * 0.03f, w * 0.50f, h * 0.36f, LRG_FRAME_ALIGN_START);

	/* --- 2. check before drawing: a failure names every problem --- */
	if (!lrg_frame_layout_validate (layout, &error))
		g_warning ("speech-bubble-layout: %s", error->message);
	lrg_frame_layout_get_box (layout, "title", &title);
	lrg_frame_layout_get_box (layout, "character", &character);
	lrg_frame_layout_get_box (layout, "bubble", &bubble);

	/* --- 3. backdrop: sky, sun glow, two hills --- */
	cb_color (&c, 52, 112, 196, 255);
	canvas = lrg_image_canvas_new_supersampled ((gint) width, (gint) height, 3, &c);
	f = (gint) lrg_image_canvas_get_supersample (canvas);
	cb_color (&c2, 246, 214, 178, 255);
	lrg_image_canvas_draw_gradient_rect (canvas, 0, 0, (gint) width * f, (gint) height * f, &c, &c2,
	                                     GRL_GRADIENT_AXIS_VERTICAL);
	cb_color (&c, 255, 250, 220, 200);
	cb_color (&c2, 255, 250, 220, 0);
	lrg_image_canvas_draw_gradient_radial (canvas, (gint) (w * 0.85f) * f, (gint) (h * 0.12f) * f,
	                                       (gint) (h * 0.5f) * f, &c, &c2);
	cb_color (&c, 140, 196, 120, 255);
	cb_ellipse (canvas, (gfloat) f, w * 0.85f, h * 1.25f, w * 0.7f, h * 0.48f, 1.0f, &c);
	cb_color (&c, 104, 170, 96, 255);
	cb_ellipse (canvas, (gfloat) f, w * 0.2f, h * 1.3f, w * 0.75f, h * 0.45f, 1.0f, &c);

	/* --- 4. the elements, each inside its own box --- */
	cb_draw_character (canvas, &character, (gfloat) f);
	bold = lrg_cookbook_load_font (TRUE);
	cb_draw_bubble (canvas, bold, "Boxes first,\nthen pixels!", &bubble, (gfloat) f);
	shadow = title;
	shadow.x += MAX (1.0f, h * 0.005f);
	shadow.y += MAX (1.0f, h * 0.007f);
	cb_color (&c, 20, 40, 80, 120);
	cb_text_in_box (canvas, bold, "The Layout Checks Out", &shadow, (gfloat) f, 1000.0f, 0.5f, &c);
	cb_color (&c, 255, 255, 255, 255);
	cb_text_in_box (canvas, bold, "The Layout Checks Out", &title, (gfloat) f, 1000.0f, 0.5f, &c);

	image = lrg_image_canvas_resolve (canvas, LRG_RESAMPLE_FILTER_LANCZOS3);
	if (image != NULL)
		cb_finish (image, 0.25f, 0.025f);
	cb_return_layout (layout, out_layout);
	return image;
}

/* ==========================================================================
 * 8. hdr-sunset: shade once, tone-map three ways
 * ========================================================================== */

typedef struct
{
	gfloat width;
	gfloat height;
	gfloat horizon;
} SunsetScene;

/* the sky's radiance at a panel position (no sea) */
static void
sunset_sky (const SunsetScene *ss,
            gfloat             x,
            gfloat             y,
            gfloat             out[3])
{
	static const gfloat top[3] = { 0.03f, 0.05f, 0.16f };
	static const gfloat low[3] = { 2.2f, 0.75f, 0.25f };
	gfloat              p[2];
	gfloat              d;
	gfloat              r;
	gfloat              glow;
	gfloat              disc;

	v3_mix (out, top, low, powf (cb_clampf (y / ss->horizon, 0.0f, 1.0f), 2.2f));

	/* the sun sits just above the horizon: 50x white at its core */
	r = ss->height * 0.06f;
	p[0] = x - ss->width * 0.5f;
	p[1] = y - (ss->horizon - r * 2.8f);
	d = lrg_sdf2_circle (p, r);
	disc = lrg_sdf2_coverage (d, 1.0f);
	glow = 1.0f / (1.0f + MAX (d, 0.0f) * MAX (d, 0.0f) / (r * r * 0.8f));
	out[0] += 50.0f * disc + 3.0f * glow;
	out[1] += 34.0f * disc + 1.4f * glow;
	out[2] += 16.0f * disc + 0.45f * glow;
}

static void
sunset_shade (gfloat   x,
              gfloat   y,
              gfloat  *out,
              gpointer user_data)
{
	const SunsetScene *ss = user_data;
	gfloat             depth;
	gfloat             scale;
	gfloat             n;
	gfloat             my;
	gint               i;

	out[3] = 1.0f;
	if (y < ss->horizon)
	{
		sunset_sky (ss, x, y, out);
		return;
	}

	/*
	 * The sea mirrors the sky about the horizon, broken up by ripples:
	 * band-limited fBm stretched by perspective (finer towards the
	 * horizon), whose footprint is its own per-pixel scale.
	 */
	depth = (y - ss->horizon) / (ss->height - ss->horizon);
	scale = 0.02f / (depth + 0.04f);
	n = lrg_noise_fbm2 (x * scale * 0.3f, y * scale * 6.0f, 6, 2.0f, 0.55f, scale * 6.0f, 5u);
	my = 2.0f * ss->horizon - y + n * ss->height * (0.10f + 0.45f * depth);
	sunset_sky (ss, x, MIN (my, ss->horizon - 1.0f), out);
	for (i = 0; i < 3; i++)
		out[i] = out[i] * 0.55f + 0.004f;
}

GrlImage *
lrg_cookbook_hdr_sunset (guint            width,
                         guint            height,
                         LrgFrameLayout **out_layout)
{
	static const LrgTonemapOperator ops[3] = {
		LRG_TONEMAP_OPERATOR_NONE, LRG_TONEMAP_OPERATOR_REINHARD, LRG_TONEMAP_OPERATOR_ACES
	};
	static const gchar *const labels[3] = { "clamp (none)", "Reinhard", "ACES" };
	static const gchar *const panel_names[3] = { "panel-clamp", "panel-reinhard", "panel-aces" };
	static const gchar *const label_names[3] = { "label-clamp", "label-reinhard", "label-aces" };
	g_autoptr(LrgFrameLayout) layout = NULL;
	g_autoptr(LrgHdrCanvas)   hdr = NULL;
	g_autoptr(LrgImageCanvas) canvas = NULL;
	g_autoptr(GrlImageFont)   font = NULL;
	GrlImage                 *image;
	GrlColor                  black;
	GrlRectangle              r;
	SunsetScene               ss;
	guint                     pw;
	gfloat                    u;
	gint                      i;

	if (out_layout != NULL)
		*out_layout = NULL;
	if (!cb_size_ok (width, height) || width < 48)
		return NULL;
	u = cb_unit (width, height);

	/* --- 1. shade one panel-sized HDR image (4 x 4 samples) --- */
	pw = (width + 2) / 3;
	ss.width = (gfloat) pw;
	ss.height = (gfloat) height;
	ss.horizon = (gfloat) height * 0.62f;
	hdr = lrg_hdr_canvas_new (pw, height);
	lrg_hdr_canvas_shade (hdr, sunset_shade, &ss, 2);

	/* --- 2. layout: three panels (backdrops) and a label on each --- */
	layout = lrg_frame_layout_new ((gfloat) width, (gfloat) height);
	lrg_frame_layout_set_safe_area_percent (layout, 0.04f);
	cb_color (&black, 0, 0, 0, 255);
	image = grl_image_new_color ((gint) width, (gint) height, &black);
	for (i = 0; i < 3; i++)
	{
		g_autoptr(GrlImage) mapped = NULL;
		gint                x0 = (gint) (width * i / 3);
		gint                x1 = (gint) (width * (i + 1) / 3);

		r.x = (gfloat) x0;
		r.y = 0.0f;
		r.width = (gfloat) (x1 - x0);
		r.height = (gfloat) height;
		lrg_frame_layout_set_box_rect (layout, panel_names[i], &r);
		lrg_frame_layout_set_backdrop (layout, panel_names[i], TRUE);
		r.width = MIN (150.0f * u, (gfloat) (x1 - x0) * 0.86f);
		r.height = 28.0f * u;
		r.x = (gfloat) x0 + ((gfloat) (x1 - x0) - r.width) * 0.5f;
		r.y = (gfloat) height * 0.96f - r.height;
		lrg_frame_layout_set_box_rect (layout, label_names[i], &r);

		/* --- 3. the same floats through each operator --- */
		mapped = lrg_hdr_canvas_to_image (hdr, ops[i], 0.0f, TRUE);
		cb_blit (image, mapped, 0, 0, x1 - x0, (gint) height, x0, 0);
	}

	/* --- 4. separators and labels on top --- */
	canvas = lrg_image_canvas_new_for_image (image);
	lrg_image_canvas_set_quality (canvas);
	font = lrg_cookbook_load_font (TRUE);
	cb_color (&black, 0, 0, 0, 255);
	for (i = 0; i < 3; i++)
	{
		if (i > 0)
			lrg_image_canvas_fill_rect (canvas, (gint) (width * i / 3), 0, MAX (1, (gint) u),
			                            (gint) height, &black);
		lrg_frame_layout_get_box (layout, label_names[i], &r);
		cb_label (canvas, font, labels[i], &r, 1.0f);
	}
	g_object_unref (canvas);
	canvas = NULL;

	cb_return_layout (layout, out_layout);
	return image;
}

/* ==========================================================================
 * 9. supersampled-line-art: hairlines at 4x
 * ========================================================================== */

/* a saturated colour from a hue in 0..1 */
static void
cb_hue (GrlColor *c,
        gfloat    hue,
        guint8    alpha)
{
	gfloat r = cb_clampf (fabsf (hue * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
	gfloat g = cb_clampf (2.0f - fabsf (hue * 6.0f - 2.0f), 0.0f, 1.0f);
	gfloat b = cb_clampf (2.0f - fabsf (hue * 6.0f - 4.0f), 0.0f, 1.0f);

	cb_color (c, (guint8) (60 + 195 * r), (guint8) (60 + 195 * g), (guint8) (60 + 195 * b), alpha);
}

GrlImage *
lrg_cookbook_supersampled_line_art (guint            width,
                                    guint            height,
                                    LrgFrameLayout **out_layout)
{
	g_autoptr(LrgImageCanvas) canvas = NULL;
	GrlColor                  c;
	GrlColor                  c2;
	gfloat                    w;
	gfloat                    h;
	gfloat                    u;
	gfloat                    sx;
	gfloat                    sy;
	gfloat                    sr;
	gint                      f;
	gint                      i;
	gint                      seg;

	cb_no_layout (out_layout);
	if (!cb_size_ok (width, height))
		return NULL;
	w = (gfloat) width;
	h = (gfloat) height;
	u = cb_unit (width, height);

	/* --- 1. a 4x canvas on dark blue paper --- */
	cb_color (&c, 16, 22, 44, 255);
	canvas = lrg_image_canvas_new_supersampled ((gint) width, (gint) height, 4, &c);
	f = (gint) lrg_image_canvas_get_supersample (canvas);
	cb_color (&c2, 8, 10, 20, 255);
	lrg_image_canvas_draw_gradient_rect (canvas, 0, 0, (gint) width * f, (gint) height * f, &c, &c2,
	                                     GRL_GRADIENT_AXIS_VERTICAL);
	lrg_image_canvas_scale (canvas, (gfloat) f, (gfloat) f);

	/* --- 2. a faint fan of cubic Beziers across the page --- */
	for (i = 0; i < 26; i++)
	{
		g_autoptr(GrlPath) path = grl_path_new ();
		gfloat             t = (gfloat) i / 25.0f;

		grl_path_move_to (path, 0.0f, h * (0.15f + 0.8f * t));
		grl_path_cubic_to (path, w * 0.35f, h * (1.1f - 0.4f * t), w * 0.6f, h * 0.1f * t,
		                   w, h * (0.92f - 0.5f * t));
		cb_color (&c, 120, 170, 255, 46);
		lrg_image_canvas_stroke_path (canvas, path, 1, &c);
	}

	/* --- 3. a hypotrochoid in twelve hues (one path per hue) --- */
	sx = w * 0.30f;
	sy = h * 0.5f;
	sr = h * 0.40f / 13.0f;
	for (seg = 0; seg < 12; seg++)
	{
		g_autoptr(GrlPath) path = grl_path_new ();
		gint               steps = 200;

		for (i = 0; i <= steps; i++)
		{
			/* R = 11, r = 4, d = 6 closes after t = 8 pi */
			gfloat t = 8.0f * G_PI_F * ((gfloat) seg + (gfloat) i / (gfloat) steps) / 12.0f;
			gfloat px = sx + sr * (7.0f * cosf (t) + 6.0f * cosf (7.0f * t / 4.0f));
			gfloat py = sy + sr * (7.0f * sinf (t) - 6.0f * sinf (7.0f * t / 4.0f));

			if (i == 0)
				grl_path_move_to (path, px, py);
			else
				grl_path_line_to (path, px, py);
		}
		cb_hue (&c, (gfloat) seg / 12.0f, 230);
		lrg_image_canvas_stroke_path (canvas, path, 1, &c);
	}

	/* --- 4. tightly spaced rings: moire bait at 1x, clean at 4x --- */
	for (i = 0; i < 28; i++)
	{
		gfloat t = (gfloat) i / 27.0f;

		cb_hue (&c, 0.08f + 0.5f * t, (guint8) (240 - 120 * t));
		lrg_image_canvas_stroke_circle (canvas, (gint) (w * 0.74f), (gint) (h * 0.5f),
		                                (gint) ((14.0f + 5.0f * i) * u), 1, &c);
	}

	/* --- 5. resolve with Lanczos-3: sharp, ringing clamped --- */
	return lrg_image_canvas_resolve (canvas, LRG_RESAMPLE_FILTER_LANCZOS3);
}

/* ==========================================================================
 * 10. poster-typography: text fitted into checked boxes
 * ========================================================================== */

GrlImage *
lrg_cookbook_poster_typography (guint            width,
                                guint            height,
                                LrgFrameLayout **out_layout)
{
	g_autoptr(LrgFrameLayout) layout = NULL;
	g_autoptr(LrgImageCanvas) canvas = NULL;
	g_autoptr(GrlImageFont)   bold = NULL;
	g_autoptr(GrlImageFont)   regular = NULL;
	g_autoptr(GError)         error = NULL;
	GrlImage                 *image;
	GrlRectangle              full;
	GrlRectangle              kicker;
	GrlRectangle              title;
	GrlRectangle              subtitle;
	GrlRectangle              art;
	GrlRectangle              footer;
	GrlRectangle              rule;
	GrlColor                  paper;
	GrlColor                  paper2;
	GrlColor                  ink;
	GrlColor                  accent;
	GrlColor                  c;
	CbShape                   band;
	gfloat                    w;
	gfloat                    h;
	gfloat                    acx;
	gfloat                    acy;
	gfloat                    ar;
	gint                      f;
	gint                      i;

	if (out_layout != NULL)
		*out_layout = NULL;
	if (!cb_size_ok (width, height))
		return NULL;
	w = (gfloat) width;
	h = (gfloat) height;

	/* --- 1. the grid: a text column on the left, the art on the right --- */
	layout = lrg_frame_layout_new (w, h);
	lrg_frame_layout_set_safe_area_percent (layout, 0.06f);
	full.x = 0.0f;
	full.y = 0.0f;
	full.width = w;
	full.height = h;
	lrg_frame_layout_set_box_rect (layout, "backdrop", &full);
	lrg_frame_layout_set_backdrop (layout, "backdrop", TRUE);
	lrg_frame_layout_add_box (layout, "kicker", LRG_HUD_ANCHOR_TOP_LEFT, 0.0f, 0.0f,
	                          w * 0.40f, h * 0.055f);
	lrg_frame_layout_add_box_relative (layout, "title", "kicker", LRG_FRAME_SIDE_BELOW, h * 0.03f,
	                                   w * 0.50f, h * 0.40f, LRG_FRAME_ALIGN_START);
	lrg_frame_layout_add_box_relative (layout, "subtitle", "title", LRG_FRAME_SIDE_BELOW, h * 0.035f,
	                                   w * 0.46f, h * 0.11f, LRG_FRAME_ALIGN_START);
	lrg_frame_layout_add_box (layout, "art", LRG_HUD_ANCHOR_RIGHT, 0.0f, -h * 0.04f,
	                          w * 0.35f, h * 0.68f);
	lrg_frame_layout_add_box (layout, "footer", LRG_HUD_ANCHOR_BOTTOM_LEFT, 0.0f, 0.0f,
	                          w * 0.88f, h * 0.06f);
	if (!lrg_frame_layout_validate (layout, &error))
		g_warning ("poster-typography: %s", error->message);
	lrg_frame_layout_get_box (layout, "kicker", &kicker);
	lrg_frame_layout_get_box (layout, "title", &title);
	lrg_frame_layout_get_box (layout, "subtitle", &subtitle);
	lrg_frame_layout_get_box (layout, "art", &art);
	lrg_frame_layout_get_box (layout, "footer", &footer);

	/* --- 2. paper --- */
	cb_color (&paper, 244, 236, 220, 255);
	cb_color (&paper2, 226, 212, 188, 255);
	cb_color (&ink, 26, 28, 44, 255);
	cb_color (&accent, 222, 86, 48, 255);
	canvas = lrg_image_canvas_new_supersampled ((gint) width, (gint) height, 3, &paper);
	f = (gint) lrg_image_canvas_get_supersample (canvas);
	lrg_image_canvas_draw_gradient_rect (canvas, 0, 0, (gint) width * f, (gint) height * f, &paper,
	                                     &paper2, GRL_GRADIENT_AXIS_VERTICAL);

	/* --- 3. the art: a striped retro sun with a soft shadow --- */
	acx = art.x + art.width * 0.5f;
	acy = art.y + art.height * 0.5f;
	ar = MIN (art.width, art.height) * 0.46f;
	cb_color (&c, 90, 50, 30, 70);
	cb_circle (canvas, (gfloat) f, acx + ar * 0.05f, acy + ar * 0.08f, ar, ar * 0.12f, &c);
	cb_circle (canvas, (gfloat) f, acx, acy, ar, 1.0f, &accent);
	cb_color (&c, 250, 170, 60, 255);
	cb_circle (canvas, (gfloat) f, acx, acy - ar * 0.12f, ar * 0.78f, ar * 0.5f, &c);
	memset (&band, 0, sizeof (band));
	band.kind = CB_SHAPE_BAND_CIRCLE;
	band.a[0] = acx;
	band.a[1] = acy;
	band.r = ar + 1.0f;
	band.feather = 1.0f;
	for (i = 0; i < 5; i++)
	{
		gfloat y0 = acy + ar * (0.12f + 0.18f * i);
		gfloat thick = ar * (0.025f + 0.022f * i);

		band.c[0] = y0;
		band.c[1] = y0 + thick;
		cb_fill (canvas, &band, (gfloat) f, acx - ar, y0, acx + ar, y0 + thick, &paper);
	}

	/* --- 4. the text, each block fitted into its box --- */
	bold = lrg_cookbook_load_font (TRUE);
	regular = lrg_cookbook_load_font (FALSE);
	cb_text_in_box (canvas, bold, "LIBREGNUM \xc2\xb7 ISSUE 7", &kicker, (gfloat) f, 1000.0f, 0.0f,
	                &accent);
	cb_text_in_box (canvas, bold, "LIGHT\nWITHOUT\nGUESSWORK", &title, (gfloat) f, 1000.0f, 0.0f, &ink);
	cb_text_in_box (canvas, regular, "HDR shading, SDFs, band-limited\nnoise and layouts that check out",
	                &subtitle, (gfloat) f, 1000.0f, 0.0f, &ink);
	rule = footer;
	rule.height = MAX (1.0f, h * 0.003f);
	cb_round_box (canvas, (gfloat) f, rule.x, rule.y, rule.width, rule.height, 0.0f, 1.0f, &ink);
	footer.y += rule.height * 3.0f;
	footer.height -= rule.height * 3.0f;
	cb_text_in_box (canvas, regular, "CPU ONLY  \xc2\xb7  NO DISPLAY  \xc2\xb7  ZERO OVERLAPS", &footer,
	                (gfloat) f, 1000.0f, 0.0f, &ink);

	image = lrg_image_canvas_resolve (canvas, LRG_RESAMPLE_FILTER_LANCZOS3);
	if (image != NULL)
		cb_finish (image, 0.2f, 0.04f);
	cb_return_layout (layout, out_layout);
	return image;
}

/* ==========================================================================
 * 11. noise-terrain: an fBm heightfield as a distance field
 * ========================================================================== */

#define TERRAIN_SCALE  (0.32f)   /* noise cycles per world unit */
#define TERRAIN_HEIGHT (1.6f)    /* world units from the lowest to the highest point */
#define TERRAIN_WATER  (0.38f)   /* water level */

typedef struct
{
	CbCamera cam;
	gfloat   sun[3];
} TerrainScene;

/*
 * terrain_height:
 * The heightfield.  The footprint grows with the distance from the
 * camera, so far terrain loses its fine octaves instead of shimmering
 * (and is cheaper to evaluate).
 */
static gfloat
terrain_height (gfloat x,
                gfloat z,
                gfloat footprint)
{
	gfloat n;

	n = lrg_noise_fbm2 (x * TERRAIN_SCALE, z * TERRAIN_SCALE, 6, 2.0f, 0.48f, footprint, 2024u);
	n = 0.5f + 0.5f * n;
	return TERRAIN_HEIGHT * n * n * 1.6f;
}

static gfloat
terrain_sdf (const gfloat p[3],
             gpointer     user_data)
{
	const TerrainScene *ts = user_data;
	gfloat              dx;
	gfloat              dz;
	gfloat              dist;
	gfloat              ground;

	/* above the highest peak the field is just the height difference */
	if (p[1] > TERRAIN_HEIGHT * 1.7f)
		return p[1] - TERRAIN_HEIGHT * 1.6f;

	dx = p[0] - ts->cam.eye[0];
	dz = p[2] - ts->cam.eye[2];
	dist = sqrtf (dx * dx + dz * dz);

	/*
	 * Vertical distance over-estimates the true distance on slopes;
	 * 0.5 keeps the marcher from stepping through ridges.
	 */
	ground = (p[1] - terrain_height (p[0], p[2], TERRAIN_SCALE * dist * ts->cam.pixel_angle)) * 0.5f;
	return MIN (ground, p[1] - TERRAIN_WATER);
}

static void
terrain_shade (gfloat   x,
               gfloat   y,
               gfloat  *out,
               gpointer user_data)
{
	TerrainScene       *ts = user_data;
	static const gfloat haze[3] = { 0.70f, 0.58f, 0.52f };
	static const gfloat grass[3] = { 0.10f, 0.17f, 0.06f };
	static const gfloat rock[3] = { 0.20f, 0.17f, 0.15f };
	static const gfloat snow[3] = { 0.80f, 0.82f, 0.86f };
	static const gfloat sand[3] = { 0.42f, 0.36f, 0.25f };
	static const gfloat water[3] = { 0.02f, 0.05f, 0.08f };
	gfloat              rd[3];
	gfloat              p[3];
	gfloat              n[3];
	gfloat              lifted[3];
	gfloat              albedo[3];
	gfloat              sky[3];
	gfloat              t;
	gfloat              ndl;
	gfloat              shadow;
	gfloat              fog;
	gfloat              s;
	gfloat              hgt;
	gint                i;

	cb_camera_ray (&ts->cam, x, y, rd);
	out[3] = 1.0f;

	/* the sky: haze at the horizon, blue above, a sun glow */
	s = cb_clampf (v3_dot (rd, ts->sun), 0.0f, 1.0f);
	v3_set (sky, cb_mixf (haze[0], 0.10f, cb_clampf (rd[1] * 3.0f, 0.0f, 1.0f)),
	        cb_mixf (haze[1], 0.20f, cb_clampf (rd[1] * 3.0f, 0.0f, 1.0f)),
	        cb_mixf (haze[2], 0.48f, cb_clampf (rd[1] * 3.0f, 0.0f, 1.0f)));
	sky[0] += 1.2f * powf (s, 12.0f) + 20.0f * powf (s, 900.0f);
	sky[1] += 0.9f * powf (s, 12.0f) + 18.0f * powf (s, 900.0f);
	sky[2] += 0.5f * powf (s, 12.0f) + 14.0f * powf (s, 900.0f);

	if (!lrg_sdf3_raymarch (terrain_sdf, ts, ts->cam.eye, rd, 40.0f, 220, &t))
	{
		memcpy (out, sky, sizeof (sky));
		return;
	}
	v3_madd (p, ts->cam.eye, rd, t);
	lrg_sdf3_normal (terrain_sdf, ts, p, 0.002f * (1.0f + t), n);

	/* materials by height and slope */
	hgt = p[1];
	if (hgt < TERRAIN_WATER + 0.002f)
		memcpy (albedo, water, sizeof (albedo));
	else
	{
		v3_mix (albedo, sand, grass, cb_smoothstep (TERRAIN_WATER + 0.02f, TERRAIN_WATER + 0.12f, hgt));
		v3_mix (albedo, albedo, rock, cb_smoothstep (0.72f, 0.55f, n[1]));
		v3_mix (albedo, albedo, snow,
		        cb_smoothstep (1.55f, 1.85f, hgt + 0.25f * n[1]) * cb_smoothstep (0.55f, 0.75f, n[1]));
	}

	/* sun with a soft shadow, sky fill, water reflection */
	ndl = cb_clampf (v3_dot (n, ts->sun), 0.0f, 1.0f);
	v3_madd (lifted, p, n, 0.01f);
	shadow = ndl > 0.0f ? lrg_sdf3_soft_shadow (terrain_sdf, ts, lifted, ts->sun, 0.02f, 12.0f, 10.0f)
	                    : 0.0f;
	for (i = 0; i < 3; i++)
	{
		static const gfloat sun_c[3] = { 3.6f, 2.6f, 1.7f };
		static const gfloat fill_c[3] = { 0.30f, 0.38f, 0.55f };

		out[i] = albedo[i] * (sun_c[i] * ndl * shadow + fill_c[i] * (0.5f + 0.5f * n[1]));
	}
	if (hgt < TERRAIN_WATER + 0.002f)
	{
		gfloat fres = 0.02f + 0.98f * powf (1.0f - cb_clampf (-rd[1], 0.0f, 1.0f), 5.0f);

		for (i = 0; i < 3; i++)
			out[i] += fres * haze[i] * 0.8f;
	}

	/* aerial perspective */
	fog = 1.0f - expf (-t * 0.035f);
	v3_mix (out, out, sky, fog);
}

GrlImage *
lrg_cookbook_noise_terrain (guint            width,
                            guint            height,
                            LrgFrameLayout **out_layout)
{
	g_autoptr(LrgHdrCanvas) canvas = NULL;
	TerrainScene            ts;
	gfloat                  eye[3] = { 0.0f, 2.6f, 6.0f };
	gfloat                  target[3] = { 0.6f, 1.3f, -2.0f };

	cb_no_layout (out_layout);
	if (!cb_size_ok (width, height))
		return NULL;

	cb_camera_init (&ts.cam, eye, target, 50.0f, width, height);
	v3_set (ts.sun, 0.75f, 0.24f, -0.55f);
	v3_normalize (ts.sun);
	canvas = lrg_hdr_canvas_new (width, height);
	lrg_hdr_canvas_shade (canvas, terrain_shade, &ts, 2);
	return lrg_hdr_canvas_to_image (canvas, LRG_TONEMAP_OPERATOR_ACES, -0.4f, TRUE);
}

/* ==========================================================================
 * 12. particles-glow: additive light, blur, bloom
 * ========================================================================== */

/*
 * cb_add_dot:
 * Adds a Gaussian dot of light (linear radiance, peak at the centre)
 * to an HDR canvas.  Additive light needs no sorting and never clips:
 * the tone map compresses where dots pile up.
 */
static void
cb_add_dot (LrgHdrCanvas *canvas,
            gfloat        cx,
            gfloat        cy,
            gfloat        sigma,
            const gfloat  rgb[3])
{
	gint   x0;
	gint   y0;
	gint   x1;
	gint   y1;
	gint   x;
	gint   y;
	gfloat k;

	x0 = (gint) floorf (cx - 3.0f * sigma);
	x1 = (gint) ceilf (cx + 3.0f * sigma);
	y0 = (gint) floorf (cy - 3.0f * sigma);
	y1 = (gint) ceilf (cy + 3.0f * sigma);
	k = -0.5f / (sigma * sigma);
	for (y = y0; y <= y1; y++)
	{
		for (x = x0; x <= x1; x++)
		{
			gfloat dx = (gfloat) x + 0.5f - cx;
			gfloat dy = (gfloat) y + 0.5f - cy;
			gfloat g = expf ((dx * dx + dy * dy) * k);

			/* add () ignores positions outside the canvas */
			lrg_hdr_canvas_add (canvas, x, y, rgb[0] * g, rgb[1] * g, rgb[2] * g);
		}
	}
}

/* a particle on a three-armed spiral; returns its position and colour */
static void
cb_spiral_particle (GRand  *rng,
                    gfloat  w,
                    gfloat  h,
                    gfloat *out_x,
                    gfloat *out_y,
                    gfloat  rgb[3])
{
	static const gfloat palette[3][3] = {
		{ 0.25f, 0.75f, 1.0f },
		{ 1.0f, 0.30f, 0.75f },
		{ 1.0f, 0.75f, 0.30f }
	};
	gint   arm;
	gfloat r;
	gfloat a;
	gfloat jitter;

	arm = g_rand_int_range (rng, 0, 3);
	r = powf ((gfloat) g_rand_double (rng), 0.7f);
	jitter = (gfloat) g_rand_double_range (rng, -0.35, 0.35) * (0.3f + r);
	a = (gfloat) arm * 2.0f * G_PI_F / 3.0f + r * 4.2f + jitter;
	*out_x = w * 0.5f + cosf (a) * r * h * 0.62f + (gfloat) g_rand_double_range (rng, -1.0, 1.0) * h * 0.02f;
	*out_y = h * 0.5f + sinf (a) * r * h * 0.40f + (gfloat) g_rand_double_range (rng, -1.0, 1.0) * h * 0.02f;
	v3_mix (rgb, palette[arm], palette[(arm + 1) % 3], (gfloat) g_rand_double (rng) * 0.4f);
}

GrlImage *
lrg_cookbook_particles_glow (guint            width,
                             guint            height,
                             LrgFrameLayout **out_layout)
{
	g_autoptr(LrgHdrCanvas)     canvas = NULL;
	g_autoptr(LrgBlur)          blur = NULL;
	g_autoptr(LrgPostProcessor) post = NULL;
	GRand                      *rng;
	GrlImage                   *image;
	gfloat                      w;
	gfloat                      h;
	gfloat                      u;
	gint                        i;

	cb_no_layout (out_layout);
	if (!cb_size_ok (width, height))
		return NULL;
	w = (gfloat) width;
	h = (gfloat) height;
	u = cb_unit (width, height);
	rng = g_rand_new_with_seed (7u);

	/* --- 1. a deep background and a faint core glow --- */
	canvas = lrg_hdr_canvas_new (width, height);
	lrg_hdr_canvas_clear (canvas, 0.004f, 0.004f, 0.012f, 1.0f);
	{
		gfloat core[3] = { 0.10f, 0.06f, 0.14f };

		cb_add_dot (canvas, w * 0.5f, h * 0.5f, h * 0.22f, core);
	}

	/* --- 2. the far layer: many dim dots, softened with LrgBlur --- */
	for (i = 0; i < 2400; i++)
	{
		gfloat x;
		gfloat y;
		gfloat rgb[3];
		gfloat e = (gfloat) g_rand_double_range (rng, 0.15, 0.6);

		cb_spiral_particle (rng, w, h, &x, &y, rgb);
		rgb[0] *= e;
		rgb[1] *= e;
		rgb[2] *= e;
		cb_add_dot (canvas, x, y, MAX (0.6f, 1.2f * u), rgb);
	}
	blur = lrg_blur_new ();
	lrg_blur_set_radius (blur, (guint) MAX (1.0f, 3.0f * u));
	post = cb_post_new (LRG_POST_EFFECT (blur), NULL);
	cb_post_hdr (post, canvas);

	/* --- 3. the near layer: fewer, brighter, sharp dots --- */
	for (i = 0; i < 500; i++)
	{
		gfloat x;
		gfloat y;
		gfloat rgb[3];
		gfloat e = (gfloat) g_rand_double_range (rng, 0.6, 4.0);

		cb_spiral_particle (rng, w, h, &x, &y, rgb);
		rgb[0] *= e;
		rgb[1] *= e;
		rgb[2] *= e;
		cb_add_dot (canvas, x, y, MAX (0.5f, (gfloat) g_rand_double_range (rng, 0.6, 1.4) * u), rgb);
	}
	g_rand_free (rng);

	/* --- 4. bloom the bright dots, tone-map, a touch of grain --- */
	cb_bloom_hdr (canvas, 0.8f, 0.9f, 4.0f);
	image = lrg_hdr_canvas_to_image (canvas, LRG_TONEMAP_OPERATOR_ACES, 0.0f, TRUE);
	if (image != NULL)
		cb_finish (image, 0.3f, 0.03f);
	return image;
}

/* ==========================================================================
 * Pitfall: thin dark detail under the tone map
 * ========================================================================== */

typedef struct
{
	gfloat   width;
	gfloat   height;
	gboolean mustache;
} FaceScene;

/* the mustache: two thin capsules in panel pixels */
static gfloat
face_mustache (const FaceScene *fs,
               const gfloat     p[2])
{
	gfloat cx = fs->width * 0.5f;
	gfloat cy = fs->height * 0.5f;
	gfloat r = MIN (fs->height * 0.28f, fs->width * 0.38f);
	gfloat a[2];
	gfloat b[2];
	gfloat q[2];

	/* mirror x so one capsule draws both halves */
	q[0] = cx + fabsf (p[0] - cx);
	q[1] = p[1];
	a[0] = cx + r * 0.03f;
	a[1] = cy + r * 0.30f;
	b[0] = cx + r * 0.42f;
	b[1] = cy + r * 0.22f;
	return lrg_sdf2_capsule (q, a, b, MAX (0.6f, fs->height * 0.0035f));
}

static gfloat
face_mustache_sdf (const gfloat p[2],
                   gpointer     user_data)
{
	return face_mustache (user_data, p);
}

static void
face_shade (gfloat   x,
            gfloat   y,
            gfloat  *out,
            gpointer user_data)
{
	const FaceScene *fs = user_data;
	gfloat           cx = fs->width * 0.5f;
	gfloat           cy = fs->height * 0.5f;
	gfloat           r = MIN (fs->height * 0.28f, fs->width * 0.38f);
	gfloat           p[2];
	gfloat           d;
	gfloat           cov;
	gfloat           nz;
	gfloat           light;
	gfloat           back;
	gfloat           eye[2];

	/* a bright backlight behind the head */
	p[0] = x - cx;
	p[1] = y - cy * 0.8f;
	back = 6.0f / (1.0f + (p[0] * p[0] + p[1] * p[1]) / (r * r * 1.5f));
	out[0] = 0.04f + back;
	out[1] = 0.05f + back * 0.8f;
	out[2] = 0.09f + back * 0.6f;
	out[3] = 1.0f;

	/* the head: a disc lit like a sphere from the front-left */
	p[0] = x - cx;
	p[1] = y - cy;
	d = lrg_sdf2_circle (p, r);
	cov = lrg_sdf2_coverage (d, 1.0f);
	if (cov <= 0.0f)
		return;
	nz = sqrtf (cb_clampf (1.0f - (p[0] * p[0] + p[1] * p[1]) / (r * r), 0.0f, 1.0f));
	light = 3.0f * cb_clampf (-0.35f * p[0] / r - 0.3f * p[1] / r + 0.88f * nz, 0.0f, 1.0f) + 0.15f;
	{
		gfloat skin[3] = { 0.72f, 0.48f, 0.36f };
		gint   i;

		/* the eyes, and the thin mustache when it is part of the shading */
		eye[0] = cx + fabsf (x - cx);
		eye[1] = y;
		eye[0] -= cx + r * 0.36f;
		eye[1] -= cy - r * 0.12f;
		if (lrg_sdf2_circle (eye, r * 0.07f) < 0.0f)
			v3_set (skin, 0.03f, 0.03f, 0.04f);
		if (fs->mustache)
		{
			gfloat q[2] = { x, y };
			gfloat m = lrg_sdf2_coverage (face_mustache (fs, q), 1.0f);

			skin[0] = cb_mixf (skin[0], 0.22f, m);
			skin[1] = cb_mixf (skin[1], 0.12f, m);
			skin[2] = cb_mixf (skin[2], 0.08f, m);
		}
		for (i = 0; i < 3; i++)
			out[i] = cb_mixf (out[i], skin[i] * light, cov);
	}
}

GrlImage *
lrg_cookbook_pitfall_thin_detail (guint            width,
                                  guint            height,
                                  LrgFrameLayout **out_layout)
{
	static const gchar *const labels[3] = { "ACES +1.5 EV: lost", "Reinhard 0 EV", "drawn after tone map" };
	g_autoptr(LrgHdrCanvas)   with_detail = NULL;
	g_autoptr(LrgHdrCanvas)   without_detail = NULL;
	g_autoptr(GrlImage)       panel_a = NULL;
	g_autoptr(GrlImage)       panel_b = NULL;
	g_autoptr(GrlImage)       panel_c = NULL;
	g_autoptr(LrgImageCanvas) canvas = NULL;
	g_autoptr(GrlImageFont)   font = NULL;
	GrlImage                 *image;
	GrlImage                 *panels[3];
	GrlColor                  black;
	GrlColor                  brown;
	GrlRectangle              r;
	FaceScene                 fs;
	guint                     pw;
	gfloat                    u;
	gint                      i;

	cb_no_layout (out_layout);
	if (!cb_size_ok (width, height) || width < 48)
		return NULL;
	u = cb_unit (width, height);
	pw = (width + 2) / 3;
	fs.width = (gfloat) pw;
	fs.height = (gfloat) height;

	/* --- 1. the face with and without the detail in the shading --- */
	fs.mustache = TRUE;
	with_detail = lrg_hdr_canvas_new (pw, height);
	lrg_hdr_canvas_shade (with_detail, face_shade, &fs, 3);
	fs.mustache = FALSE;
	without_detail = lrg_hdr_canvas_new (pw, height);
	lrg_hdr_canvas_shade (without_detail, face_shade, &fs, 3);

	/* --- 2. (a) pushed exposure, (b) gentler curve, (c) detail after --- */
	panel_a = lrg_hdr_canvas_to_image (with_detail, LRG_TONEMAP_OPERATOR_ACES, 1.5f, TRUE);
	panel_b = lrg_hdr_canvas_to_image (with_detail, LRG_TONEMAP_OPERATOR_REINHARD, 0.0f, TRUE);
	panel_c = lrg_hdr_canvas_to_image (without_detail, LRG_TONEMAP_OPERATOR_ACES, 1.5f, TRUE);
	{
		g_autoptr(LrgImageCanvas) pc = lrg_image_canvas_new_for_image (panel_c);

		lrg_image_canvas_set_quality (pc);
		cb_color (&brown, 70, 40, 28, 255);
		fs.mustache = TRUE;
		lrg_image_canvas_fill_sdf (pc, face_mustache_sdf, &fs, &brown);
	}

	/* --- 3. side by side with labels --- */
	cb_color (&black, 0, 0, 0, 255);
	image = grl_image_new_color ((gint) width, (gint) height, &black);
	panels[0] = panel_a;
	panels[1] = panel_b;
	panels[2] = panel_c;
	canvas = lrg_image_canvas_new_for_image (image);
	lrg_image_canvas_set_quality (canvas);
	font = lrg_cookbook_load_font (TRUE);
	for (i = 0; i < 3; i++)
	{
		gint x0 = (gint) (width * i / 3);
		gint x1 = (gint) (width * (i + 1) / 3);

		cb_blit (image, panels[i], 0, 0, x1 - x0, (gint) height, x0, 0);
		if (i > 0)
			lrg_image_canvas_fill_rect (canvas, x0, 0, MAX (1, (gint) u), (gint) height, &black);
		r.width = MIN (180.0f * u, (gfloat) (x1 - x0) * 0.9f);
		r.height = 26.0f * u;
		r.x = (gfloat) x0 + ((gfloat) (x1 - x0) - r.width) * 0.5f;
		r.y = (gfloat) height * 0.95f - r.height;
		cb_label (canvas, font, labels[i], &r, 1.0f);
	}
	return image;
}

/* ==========================================================================
 * The scene table
 * ========================================================================== */

static const LrgCookbookScene cookbook_scenes[] = {
	{ "aa-shapes",
	  "Translucent shapes, hairlines and a star: quality preset + 3x supersampling only",
	  lrg_cookbook_aa_shapes, FALSE },
	{ "glow-title",
	  "Neon title: TrueType text, bloom, ACES tonemap, vignette, grain",
	  lrg_cookbook_glow_title, TRUE },
	{ "sdf-character-bust",
	  "3D SDF bust raymarched into an HDR canvas: soft shadow, AO, rim light, ACES",
	  lrg_cookbook_sdf_character_bust, FALSE },
	{ "product-shot",
	  "SDF bottle on a studio floor: soft contact shadow, 3x3 samples, filmic",
	  lrg_cookbook_product_shot, FALSE },
	{ "band-limited-texture",
	  "Marble floor to the horizon with band-limited fBm: no moire",
	  lrg_cookbook_band_limited_texture, FALSE },
	{ "sky-gradient-vignette",
	  "Dusk sky in linear light: sun with bloom, hills, vignette and grain",
	  lrg_cookbook_sky_gradient_vignette, FALSE },
	{ "speech-bubble-layout",
	  "Character, bubble and title placed with LrgFrameLayout and validated",
	  lrg_cookbook_speech_bubble_layout, TRUE },
	{ "hdr-sunset",
	  "One HDR sunset tone-mapped three ways: clamp, Reinhard, ACES",
	  lrg_cookbook_hdr_sunset, TRUE },
	{ "supersampled-line-art",
	  "Hairline spirograph, rings and Bezier fan at 4x, Lanczos-3 resolve",
	  lrg_cookbook_supersampled_line_art, FALSE },
	{ "poster-typography",
	  "Poster: text fitted into layout boxes inside a safe area",
	  lrg_cookbook_poster_typography, TRUE },
	{ "noise-terrain",
	  "fBm heightfield raymarched with soft shadows, materials and haze",
	  lrg_cookbook_noise_terrain, FALSE },
	{ "particles-glow",
	  "Thousands of additive light dots: blurred far layer, bloom, ACES",
	  lrg_cookbook_particles_glow, FALSE },
};

static const LrgCookbookScene cookbook_pitfalls[] = {
	{ "pitfall-moire",
	  "Noise without a footprint (left) aliases into moire; with it (right) it fades",
	  lrg_cookbook_pitfall_moire, FALSE },
	{ "pitfall-thin-detail",
	  "Thin dark detail: lost under ACES +1.5 EV, kept by Reinhard, exact when drawn after",
	  lrg_cookbook_pitfall_thin_detail, FALSE },
};

const LrgCookbookScene *
lrg_cookbook_get_scenes (guint *n_scenes)
{
	*n_scenes = G_N_ELEMENTS (cookbook_scenes);
	return cookbook_scenes;
}

const LrgCookbookScene *
lrg_cookbook_get_pitfalls (guint *n_pitfalls)
{
	*n_pitfalls = G_N_ELEMENTS (cookbook_pitfalls);
	return cookbook_pitfalls;
}

const LrgCookbookScene *
lrg_cookbook_find_scene (const gchar *name)
{
	guint i;

	g_return_val_if_fail (name != NULL, NULL);

	for (i = 0; i < G_N_ELEMENTS (cookbook_scenes); i++)
		if (g_str_equal (cookbook_scenes[i].name, name))
			return &cookbook_scenes[i];
	for (i = 0; i < G_N_ELEMENTS (cookbook_pitfalls); i++)
		if (g_str_equal (cookbook_pitfalls[i].name, name))
			return &cookbook_pitfalls[i];
	return NULL;
}
