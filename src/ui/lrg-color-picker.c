/* lrg-color-picker.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgColorPicker: Saturation/value square, hue and alpha strips, hex entry and swatches.
 *
 * The full picker is a container with four internal children:
 *
 *  - three LrgColorArea widgets (private type): the saturation / value
 *    square, the hue strip and the alpha strip. Each is focusable, so
 *    Tab / the gamepad reach it and the arrow keys nudge its cursor;
 *    each owns a knob whose position follows the colour on springs.
 *  - an LrgTextInput for the hex value.
 *
 * The picker paints its own surface, a preview swatch and the swatch
 * rows (palette, then recent colours) and handles clicks on swatches.
 *
 * The colour is kept as HSV plus alpha (doubles) so the hue survives
 * greys and the square does not jump; the 8-bit GrlColor is derived.
 *
 * In popup mode the picker is a compact swatch button instead: its
 * children are hidden and a click opens a second, full picker in a
 * canvas popup whose changes are mirrored back.
 *
 * Drawing: the square is two gradient rectangles (white to the pure
 * hue left to right, then transparent to black top to bottom), the hue
 * strip six gradient segments through the primaries and secondaries,
 * the alpha strip a checkerboard under a transparent-to-colour
 * gradient.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>

#include "lrg-color-picker.h"
#include "lrg-canvas.h"
#include "lrg-text-input.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private types
 * ========================================================================== */

#define LRG_TYPE_COLOR_AREA (lrg_color_area_get_type ())
G_DECLARE_FINAL_TYPE (LrgColorArea, lrg_color_area, LRG, COLOR_AREA, LrgWidget)

struct _LrgColorArea
{
	LrgWidget           parent_instance;

	LrgColorPickerArea  kind;      /* SV, HUE or ALPHA */
	gboolean            dragging;

	/* Knob position, 0..1 across / down the area */
	LrgSpring           knob_x;
	LrgSpring           knob_y;
	gboolean            stepped;
};

typedef struct
{
	GrlRectangle sv;
	GrlRectangle hue;
	GrlRectangle alpha;
	GrlRectangle preview;
	GrlRectangle hex;
	GrlRectangle swatches;
	gfloat       swatch;      /* swatch edge */
	gfloat       gap;         /* swatch gap */
	guint        per_row;
	gfloat       recent_y;    /* top of the recent row(s), relative */
	gfloat       height;      /* total */
} Layout;

struct _LrgColorPicker
{
	LrgContainer    parent_instance;

	/* Colour */
	gdouble         h;
	gdouble         s;
	gdouble         v;
	gdouble         a;
	GrlColor        color;

	gboolean        show_alpha;
	gboolean        popup_mode;

	GArray         *palette;   /* GrlColor */
	GArray         *recent;    /* GrlColor, newest first */

	/* Children (owned by the container) */
	LrgColorArea   *sv_area;
	LrgColorArea   *hue_area;
	LrgColorArea   *alpha_area;
	LrgTextInput   *hex_entry;
	gboolean        syncing_hex;

	/* Swatches */
	gint            hover_swatch;
	gint            press_swatch;

	/* Popup mode */
	LrgColorPicker *popup;     /* the full picker shown in the popup */
	LrgCanvas      *canvas;    /* weak */
	gulong          dismissed_id;
	gboolean        swallow_press;
	LrgColorPicker *owner;     /* weak: set on the picker inside a popup */
};

G_DEFINE_FINAL_TYPE (LrgColorArea, lrg_color_area, LRG_TYPE_WIDGET)
G_DEFINE_FINAL_TYPE (LrgColorPicker, lrg_color_picker, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_COLOR,
	PROP_SHOW_ALPHA,
	PROP_POPUP_MODE,
	PROP_POPUP_OPEN,
	N_PROPS
};

enum
{
	SIGNAL_COLOR_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

static void set_hsva (LrgColorPicker *self,
                      gdouble         h,
                      gdouble         s,
                      gdouble         v,
                      gdouble         a,
                      gboolean        from_hex);

GType
lrg_color_picker_area_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_COLOR_PICKER_AREA_SV, "LRG_COLOR_PICKER_AREA_SV", "sv" },
			{ LRG_COLOR_PICKER_AREA_HUE, "LRG_COLOR_PICKER_AREA_HUE", "hue" },
			{ LRG_COLOR_PICKER_AREA_ALPHA, "LRG_COLOR_PICKER_AREA_ALPHA", "alpha" },
			{ LRG_COLOR_PICKER_AREA_HEX, "LRG_COLOR_PICKER_AREA_HEX", "hex" },
			{ LRG_COLOR_PICKER_AREA_PREVIEW, "LRG_COLOR_PICKER_AREA_PREVIEW", "preview" },
			{ LRG_COLOR_PICKER_AREA_SWATCHES, "LRG_COLOR_PICKER_AREA_SWATCHES", "swatches" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgColorPickerArea"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

/* ==========================================================================
 * Colour maths
 * ========================================================================== */

void
lrg_color_picker_rgb_to_hsv (const GrlColor *color,
                             gdouble        *hue,
                             gdouble        *saturation,
                             gdouble        *value)
{
	gdouble r;
	gdouble g;
	gdouble b;
	gdouble mx;
	gdouble mn;
	gdouble d;
	gdouble h = 0.0;

	g_return_if_fail (color != NULL);

	r = color->r / 255.0;
	g = color->g / 255.0;
	b = color->b / 255.0;
	mx = MAX (r, MAX (g, b));
	mn = MIN (r, MIN (g, b));
	d = mx - mn;

	/* Hue from whichever channel is largest */
	if (d > 0.0)
	{
		if (mx == r)
			h = 60.0 * fmod ((g - b) / d, 6.0);
		else if (mx == g)
			h = 60.0 * ((b - r) / d + 2.0);
		else
			h = 60.0 * ((r - g) / d + 4.0);
		if (h < 0.0)
			h += 360.0;
	}

	if (hue != NULL)
		*hue = h;
	if (saturation != NULL)
		*saturation = mx > 0.0 ? d / mx : 0.0;
	if (value != NULL)
		*value = mx;
}

void
lrg_color_picker_hsv_to_rgb (gdouble   hue,
                             gdouble   saturation,
                             gdouble   value,
                             gdouble   alpha,
                             GrlColor *color)
{
	gdouble c;
	gdouble x;
	gdouble m;
	gdouble r = 0.0;
	gdouble g = 0.0;
	gdouble b = 0.0;
	gdouble hp;

	g_return_if_fail (color != NULL);

	hue = fmod (hue, 360.0);
	if (hue < 0.0)
		hue += 360.0;
	saturation = CLAMP (saturation, 0.0, 1.0);
	value = CLAMP (value, 0.0, 1.0);
	alpha = CLAMP (alpha, 0.0, 1.0);

	/* Chroma, the second-largest component and the shared minimum */
	c = value * saturation;
	hp = hue / 60.0;
	x = c * (1.0 - fabs (fmod (hp, 2.0) - 1.0));
	m = value - c;

	if (hp < 1.0)
	{
		r = c; g = x;
	}
	else if (hp < 2.0)
	{
		r = x; g = c;
	}
	else if (hp < 3.0)
	{
		g = c; b = x;
	}
	else if (hp < 4.0)
	{
		g = x; b = c;
	}
	else if (hp < 5.0)
	{
		r = x; b = c;
	}
	else
	{
		r = c; b = x;
	}

	color->r = (guint8) lround ((r + m) * 255.0);
	color->g = (guint8) lround ((g + m) * 255.0);
	color->b = (guint8) lround ((b + m) * 255.0);
	color->a = (guint8) lround (alpha * 255.0);
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgCanvas *
find_canvas (LrgWidget *widget)
{
	LrgWidget *root = lrg_widget_get_root (widget);

	return LRG_IS_CANVAS (root) ? LRG_CANVAS (root) : NULL;
}

static gfloat
metric (LrgWidget      *widget,
        LrgThemeMetric  m)
{
	return lrg_theme_get_metric (lrg_widget_get_theme (widget), m);
}

static gboolean
color_equal (const GrlColor *a,
             const GrlColor *b)
{
	return a->r == b->r && a->g == b->g && a->b == b->b && a->a == b->a;
}

static gboolean
rect_contains (const GrlRectangle *r,
               gfloat              x,
               gfloat              y)
{
	return x >= r->x && x < r->x + r->width && y >= r->y && y < r->y + r->height;
}

/*
 * paint_checker:
 *
 * A light / dark checkerboard behind translucent colours: a light
 * rounded base and dark cells, clipped to the rectangle.
 */
static void
paint_checker (LrgPaintList *list,
               gfloat        x,
               gfloat        y,
               gfloat        w,
               gfloat        h,
               gfloat        radius,
               gfloat        cell)
{
	static const GrlColor light = { 236, 236, 236, 255 };
	static const GrlColor dark = { 190, 190, 190, 255 };
	gint                  cols = (gint) ceilf (w / cell);
	gint                  rows = (gint) ceilf (h / cell);
	gint                  i;
	gint                  j;

	lrg_paint_list_rect (list, x, y, w, h, radius, &light);
	lrg_paint_list_push_clip (list, x, y, w, h);
	for (j = 0; j < rows; j++)
		for (i = (j % 2); i < cols; i += 2)
			lrg_paint_list_rect (list, x + (gfloat) i * cell, y + (gfloat) j * cell, cell, cell, 0.0f, &dark);
	lrg_paint_list_pop_clip (list);
}

/* hue_color: the fully saturated, full value colour of @hue */
static GrlColor
hue_color (gdouble hue)
{
	GrlColor c;

	lrg_color_picker_hsv_to_rgb (hue, 1.0, 1.0, 1.0, &c);
	return c;
}

/* ==========================================================================
 * Layout of the full picker
 * ========================================================================== */

static gfloat
picker_pad (LrgWidget *widget)
{
	return roundf (metric (widget, LRG_THEME_METRIC_SPACING) * 1.5f);
}

static gfloat
strip_height (LrgWidget *widget)
{
	return roundf (MAX (10.0f, metric (widget, LRG_THEME_METRIC_FONT_SIZE) * 0.85f));
}

/*
 * compute_layout:
 *
 * Every region of the full picker for @width, relative to the picker:
 * square, hue strip, optional alpha strip, preview + hex row, swatch
 * rows. The square is @sv_height tall (see compute_layout()).
 */
static void
compute_layout_with (LrgColorPicker *self,
                     gfloat          width,
                     gfloat          sv_height,
                     Layout         *out)
{
	LrgWidget *widget = LRG_WIDGET (self);
	gfloat     pad = picker_pad (widget);
	gfloat     gap = metric (widget, LRG_THEME_METRIC_SPACING);
	gfloat     iw = MAX (40.0f, width - pad * 2.0f);
	gfloat     strip = strip_height (widget);
	gfloat     ch = metric (widget, LRG_THEME_METRIC_CONTROL_HEIGHT);
	gfloat     y = pad;
	guint      np = self->palette->len;
	guint      nr = self->recent->len;
	guint      rows;

	memset (out, 0, sizeof (Layout));

	out->sv = (GrlRectangle) { pad, y, iw, sv_height };
	y += out->sv.height + gap + 2.0f;

	out->hue = (GrlRectangle) { pad, y, iw, strip };
	y += strip + gap + 2.0f;

	if (self->show_alpha)
	{
		out->alpha = (GrlRectangle) { pad, y, iw, strip };
		y += strip + gap + 2.0f;
	}

	out->preview = (GrlRectangle) { pad, y, ch, ch };
	out->hex = (GrlRectangle) { pad + ch + gap, y, MAX (0.0f, iw - ch - gap), ch };
	y += ch;

	/* Swatch grid */
	out->swatch = roundf (ch * 0.6f);
	out->gap = roundf (gap * 0.75f);
	out->per_row = (guint) MAX (1.0f, floorf ((iw + out->gap) / (out->swatch + out->gap)));
	if (np + nr > 0)
	{
		y += gap + 2.0f;
		out->swatches.x = pad;
		out->swatches.y = y;
		out->swatches.width = iw;
		rows = (np + out->per_row - 1) / out->per_row;
		y += (gfloat) rows * (out->swatch + out->gap);
		out->recent_y = y + (np > 0 && nr > 0 ? gap * 0.5f : 0.0f);
		if (nr > 0)
		{
			rows = (nr + out->per_row - 1) / out->per_row;
			y = out->recent_y + (gfloat) rows * (out->swatch + out->gap);
		}
		y -= out->gap;
		out->swatches.height = y - out->swatches.y;
	}

	out->height = y + pad;
}

/*
 * compute_layout:
 *
 * The layout for @width. The square is two thirds as tall as it is
 * wide, but when @max_height is positive (the allocated height) and the
 * picker would not fit, the square gives up the difference (down to 40
 * units), so a picker allocated wider than it measured never spills
 * over what lies below it.
 */
static void
compute_layout (LrgColorPicker *self,
                gfloat          width,
                gfloat          max_height,
                Layout         *out)
{
	gfloat pad = picker_pad (LRG_WIDGET (self));
	gfloat sv_height = roundf (MAX (40.0f, width - pad * 2.0f) * 0.66f);

	compute_layout_with (self, width, sv_height, out);
	if (max_height > 0.0f && out->height > max_height && sv_height > 40.0f)
		compute_layout_with (self, width, MAX (40.0f, sv_height - (out->height - max_height)), out);
}

/* preferred_width: width of a full picker */
static gfloat
preferred_width (LrgWidget *widget)
{
	return roundf (metric (widget, LRG_THEME_METRIC_CONTROL_HEIGHT) * 7.5f);
}

/* current_layout: layout for the allocated (or preferred) width */
static void
current_layout (LrgColorPicker *self,
                Layout         *out)
{
	gfloat w = lrg_widget_get_width (LRG_WIDGET (self));

	compute_layout (self, w > 0.0f ? w : preferred_width (LRG_WIDGET (self)),
	                w > 0.0f ? lrg_widget_get_height (LRG_WIDGET (self)) : 0.0f, out);
}

/* swatch_rect: swatch @index (palette, then recent), relative */
static gboolean
swatch_rect (LrgColorPicker *self,
             const Layout   *lay,
             guint           index,
             GrlRectangle   *r)
{
	guint  np = self->palette->len;
	guint  k;
	gfloat top;

	if (index >= np + self->recent->len)
		return FALSE;
	if (index < np)
	{
		k = index;
		top = lay->swatches.y;
	}
	else
	{
		k = index - np;
		top = lay->recent_y;
	}
	r->x = lay->swatches.x + (gfloat) (k % lay->per_row) * (lay->swatch + lay->gap);
	r->y = top + (gfloat) (k / lay->per_row) * (lay->swatch + lay->gap);
	r->width = lay->swatch;
	r->height = lay->swatch;
	return TRUE;
}

/* swatch_color: colour of swatch @index */
static const GrlColor *
swatch_color (LrgColorPicker *self,
              guint           index)
{
	if (index < self->palette->len)
		return &g_array_index (self->palette, GrlColor, index);
	return &g_array_index (self->recent, GrlColor, index - self->palette->len);
}

/* swatch_at: swatch under the canvas point, -1 for none */
static gint
swatch_at (LrgColorPicker *self,
           gfloat          px,
           gfloat          py)
{
	Layout lay;
	gfloat rx = px - lrg_widget_get_world_x (LRG_WIDGET (self));
	gfloat ry = py - lrg_widget_get_world_y (LRG_WIDGET (self));
	guint  i;

	if (self->popup_mode)
		return -1;
	current_layout (self, &lay);
	for (i = 0; i < self->palette->len + self->recent->len; i++)
	{
		GrlRectangle r;

		swatch_rect (self, &lay, i, &r);
		if (rect_contains (&r, rx, ry))
			return (gint) i;
	}
	return -1;
}

/* ==========================================================================
 * LrgColorArea: square and strips
 * ========================================================================== */

static LrgColorPicker *
area_picker (LrgColorArea *area)
{
	LrgContainer *parent = lrg_widget_get_parent (LRG_WIDGET (area));

	return LRG_IS_COLOR_PICKER (parent) ? LRG_COLOR_PICKER (parent) : NULL;
}

/* knob_target: where the knob belongs for the picker's colour */
static void
knob_target (LrgColorArea *self,
             gfloat       *x,
             gfloat       *y)
{
	LrgColorPicker *picker = area_picker (self);

	*x = 0.0f;
	*y = 0.5f;
	if (picker == NULL)
		return;
	switch (self->kind)
	{
	case LRG_COLOR_PICKER_AREA_SV:
		*x = (gfloat) picker->s;
		*y = (gfloat) (1.0 - picker->v);
		break;
	case LRG_COLOR_PICKER_AREA_HUE:
		*x = (gfloat) (picker->h / 360.0);
		break;
	default:
		*x = (gfloat) picker->a;
		break;
	}
}

/* area_knob_radius: base knob radius */
static gfloat
area_knob_radius (LrgColorArea *self)
{
	LrgWidget *widget = LRG_WIDGET (self);

	if (self->kind == LRG_COLOR_PICKER_AREA_SV)
		return roundf (MAX (5.0f, metric (widget, LRG_THEME_METRIC_HANDLE_SIZE) * 0.42f));
	return roundf (lrg_widget_get_height (widget) * 0.5f + 2.0f);
}

static void
lrg_color_area_style_defaults (LrgWidget      *widget,
                               LrgUIPart       part,
                               LrgWidgetState  state,
                               LrgPartLook    *look)
{
	LrgColorArea *self = LRG_COLOR_AREA (widget);
	LrgTheme     *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_TRACK:
		{
			GrlColor edge;

			/* A hairline to set the gradients off the surface */
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.8f, &edge);
			lrg_part_look_set_border (look, &edge, 1.0f);
			lrg_part_look_set_radius (look, self->kind == LRG_COLOR_PICKER_AREA_SV
			                                ? lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS)
			                                : G_MAXFLOAT);
			if (state & LRG_WIDGET_STATE_DISABLED)
				lrg_part_look_set_opacity (look, 0.45f);
		}
		break;

	case LRG_UI_PART_THUMB:
		{
			static const GrlColor white = { 255, 255, 255, 255 };
			gfloat                r = area_knob_radius (self);

			/* White ring with a soft shadow; grows under the pointer
			 * and while dragged */
			if (state & LRG_WIDGET_STATE_PRESSED)
				r += 2.5f;
			else if (state & LRG_WIDGET_STATE_HOVERED)
				r += 1.5f;
			lrg_part_look_set_border (look, &white, (state & LRG_WIDGET_STATE_PRESSED) ? 3.0f : 2.5f);
			lrg_part_look_set_size (look, r);
			lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
			                          (state & LRG_WIDGET_STATE_PRESSED) ? 10.0f : 6.0f, 1.0f);
		}
		break;

	default:
		LRG_WIDGET_CLASS (lrg_color_area_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static gboolean
lrg_color_area_step (LrgWidget *widget,
                     gfloat     delta_time)
{
	LrgColorArea *self = LRG_COLOR_AREA (widget);
	LrgMotion     motion = lrg_widget_get_motion (widget);
	gfloat        tx;
	gfloat        ty;
	gboolean      busy = FALSE;

	knob_target (self, &tx, &ty);
	lrg_spring_set_motion (&self->knob_x, motion == LRG_MOTION_NONE ? LRG_MOTION_NONE : LRG_MOTION_SNAPPY);
	lrg_spring_set_motion (&self->knob_y, motion == LRG_MOTION_NONE ? LRG_MOTION_NONE : LRG_MOTION_SNAPPY);
	lrg_spring_set_target (&self->knob_x, tx);
	lrg_spring_set_target (&self->knob_y, ty);
	if (!self->stepped || motion == LRG_MOTION_NONE)
	{
		lrg_spring_snap (&self->knob_x, tx);
		lrg_spring_snap (&self->knob_y, ty);
		self->stepped = TRUE;
		return FALSE;
	}
	busy |= lrg_spring_step (&self->knob_x, delta_time);
	busy |= lrg_spring_step (&self->knob_y, delta_time);
	return busy;
}

/* paint_hue_strip: six gradient segments red .. red */
static void
paint_hue_strip (LrgPaintList *list,
                 gfloat        x,
                 gfloat        y,
                 gfloat        w,
                 gfloat        h,
                 gfloat        radius)
{
	gfloat seg = w / 6.0f;
	gint   k;

	for (k = 0; k < 6; k++)
	{
		LrgPaintGradient grad;
		LrgCornerRadii   radii;
		GrlColor         c0 = hue_color (60.0 * k);
		GrlColor         c1 = hue_color (60.0 * (k + 1));
		gfloat           sx = x + seg * (gfloat) k;

		/* Only the outer ends are rounded; segments overlap by half a
		 * pixel so no seam shows */
		lrg_corner_radii_init_uniform (&radii, 0.0f);
		if (k == 0)
		{
			radii.top_left = radius;
			radii.bottom_left = radius;
		}
		if (k == 5)
		{
			radii.top_right = radius;
			radii.bottom_right = radius;
		}
		lrg_paint_gradient_init_linear (&grad, &c0, &c1, 0.0f);
		lrg_paint_list_rect_ex (list, sx - (k > 0 ? 0.5f : 0.0f), y, seg + (k > 0 && k < 5 ? 1.0f : 0.5f), h,
		                        &radii, NULL, &grad, 0.0f, NULL);
	}
}

static void
lrg_color_area_paint (LrgWidget    *widget,
                      LrgPaintList *list)
{
	LrgColorArea   *self = LRG_COLOR_AREA (widget);
	LrgColorPicker *picker = area_picker (self);
	LrgPartLook     track;
	LrgPartLook     thumb;
	gfloat          x = lrg_widget_get_world_x (widget);
	gfloat          y = lrg_widget_get_world_y (widget);
	gfloat          w = lrg_widget_get_width (widget);
	gfloat          h = lrg_widget_get_height (widget);
	gfloat          radius;
	gfloat          kx;
	gfloat          ky;
	GrlColor        fill;

	if (picker == NULL)
		return;

	lrg_widget_resolve_look (widget, LRG_UI_PART_TRACK, &track);
	radius = MIN (track.radii.top_left, MIN (w, h) * 0.5f);
	if (track.opacity < 1.0f)
		lrg_paint_list_push_opacity (list, track.opacity);

	switch (self->kind)
	{
	case LRG_COLOR_PICKER_AREA_SV:
		{
			LrgPaintGradient grad;
			GrlColor         white = { 255, 255, 255, 255 };
			GrlColor         clear = { 0, 0, 0, 0 };
			GrlColor         black = { 0, 0, 0, 255 };
			GrlColor         pure = hue_color (picker->h);

			/* Saturation left to right, then value top to bottom */
			lrg_paint_gradient_init_linear (&grad, &white, &pure, 0.0f);
			lrg_paint_list_rect_ex (list, x, y, w, h, &track.radii, NULL, &grad, 0.0f, NULL);
			lrg_paint_gradient_init_linear (&grad, &clear, &black, 90.0f);
			lrg_paint_list_rect_ex (list, x, y, w, h, &track.radii, NULL, &grad, 0.0f, NULL);
			lrg_color_picker_hsv_to_rgb (picker->h, picker->s, picker->v, 1.0, &fill);
		}
		break;

	case LRG_COLOR_PICKER_AREA_HUE:
		paint_hue_strip (list, x, y, w, h, radius);
		fill = hue_color (picker->h);
		break;

	default:
		{
			LrgPaintGradient grad;
			GrlColor         c0;
			GrlColor         c1;

			lrg_color_picker_hsv_to_rgb (picker->h, picker->s, picker->v, 0.0, &c0);
			lrg_color_picker_hsv_to_rgb (picker->h, picker->s, picker->v, 1.0, &c1);
			paint_checker (list, x, y, w, h, radius, roundf (h * 0.5f));
			lrg_paint_gradient_init_linear (&grad, &c0, &c1, 0.0f);
			lrg_paint_list_rect_ex (list, x, y, w, h, &track.radii, NULL, &grad, 0.0f, NULL);
			fill = picker->color;
		}
		break;
	}

	/* Frame */
	if (track.border_width > 0.0f)
		lrg_paint_list_rect_ex (list, x, y, w, h, &track.radii, NULL, NULL, track.border_width, &track.border);

	/* Knob: shadow, white ring, the colour it stands on */
	knob_target (self, &kx, &ky);
	if (self->stepped)
	{
		kx = self->knob_x.value;
		ky = self->knob_y.value;
	}
	lrg_widget_resolve_look (widget, LRG_UI_PART_THUMB, &thumb);
	{
		gfloat r = thumb.size > 0.0f ? thumb.size : area_knob_radius (self);
		gfloat cx = x + CLAMP (kx, 0.0f, 1.0f) * w;
		gfloat cy = y + CLAMP (ky, 0.0f, 1.0f) * h;

		/* Strip knobs stay inside the rounded ends */
		if (self->kind != LRG_COLOR_PICKER_AREA_SV)
			cx = x + h * 0.5f + CLAMP (kx, 0.0f, 1.0f) * MAX (0.0f, w - h);

		if (thumb.shadow.a > 0)
			lrg_paint_list_shadow (list, cx - r, cy - r, r * 2.0f, r * 2.0f, r, thumb.shadow_blur, 0.0f, 0.0f,
			                       thumb.shadow_offset_y, &thumb.shadow);
		lrg_paint_list_circle (list, cx, cy, r, &thumb.border);
		if (fill.a < 255)
			paint_checker (list, cx - r + thumb.border_width, cy - r + thumb.border_width,
			               (r - thumb.border_width) * 2.0f, (r - thumb.border_width) * 2.0f, r, r * 0.5f);
		lrg_paint_list_circle (list, cx, cy, MAX (1.0f, r - thumb.border_width), &fill);
	}

	if (track.opacity < 1.0f)
		lrg_paint_list_pop_opacity (list);
}

/* area_apply: pick the colour under canvas point (px, py) */
static void
area_apply (LrgColorArea *self,
            gfloat        px,
            gfloat        py)
{
	LrgColorPicker *picker = area_picker (self);
	LrgWidget      *widget = LRG_WIDGET (self);
	gfloat          w = lrg_widget_get_width (widget);
	gfloat          h = lrg_widget_get_height (widget);
	gdouble         fx;
	gdouble         fy;

	if (picker == NULL || w <= 0.0f || h <= 0.0f)
		return;
	fx = CLAMP ((px - lrg_widget_get_world_x (widget)) / w, 0.0, 1.0);
	fy = CLAMP ((py - lrg_widget_get_world_y (widget)) / h, 0.0, 1.0);

	switch (self->kind)
	{
	case LRG_COLOR_PICKER_AREA_SV:
		set_hsva (picker, picker->h, fx, 1.0 - fy, picker->a, FALSE);
		break;
	case LRG_COLOR_PICKER_AREA_HUE:
		/* Same inset as the knob: the ends are half a strip in */
		if (w > h)
			fx = CLAMP ((px - lrg_widget_get_world_x (widget) - h * 0.5f) / (w - h), 0.0, 1.0);
		set_hsva (picker, fx * 360.0, picker->s, picker->v, picker->a, FALSE);
		break;
	default:
		if (w > h)
			fx = CLAMP ((px - lrg_widget_get_world_x (widget) - h * 0.5f) / (w - h), 0.0, 1.0);
		set_hsva (picker, picker->h, picker->s, picker->v, fx, FALSE);
		break;
	}
}

/* area_nudge: keyboard step; returns FALSE for keys the area ignores */
static gboolean
area_nudge (LrgColorArea *self,
            GrlKey        key,
            gboolean      big)
{
	LrgColorPicker *picker = area_picker (self);
	gdouble         step = big ? 0.1 : 0.01;
	gdouble         h;
	gdouble         s;
	gdouble         v;
	gdouble         a;

	if (picker == NULL)
		return FALSE;
	h = picker->h;
	s = picker->s;
	v = picker->v;
	a = picker->a;

	switch (self->kind)
	{
	case LRG_COLOR_PICKER_AREA_SV:
		if (key == GRL_KEY_LEFT)
			s -= step;
		else if (key == GRL_KEY_RIGHT)
			s += step;
		else if (key == GRL_KEY_UP)
			v += step;
		else if (key == GRL_KEY_DOWN)
			v -= step;
		else
			return FALSE;
		break;

	case LRG_COLOR_PICKER_AREA_HUE:
		/* Degrees, clamped (no wrap) so the knob never jumps across */
		if (key == GRL_KEY_LEFT)
			h = MAX (0.0, h - (big ? 10.0 : 1.0));
		else if (key == GRL_KEY_RIGHT)
			h = MIN (360.0, h + (big ? 10.0 : 1.0));
		else if (key == GRL_KEY_HOME)
			h = 0.0;
		else if (key == GRL_KEY_END)
			h = 360.0;
		else
			return FALSE;
		break;

	default:
		if (key == GRL_KEY_LEFT)
			a -= step;
		else if (key == GRL_KEY_RIGHT)
			a += step;
		else if (key == GRL_KEY_HOME)
			a = 0.0;
		else if (key == GRL_KEY_END)
			a = 1.0;
		else
			return FALSE;
		break;
	}
	set_hsva (picker, h, CLAMP (s, 0.0, 1.0), CLAMP (v, 0.0, 1.0), CLAMP (a, 0.0, 1.0), FALSE);
	return TRUE;
}

static gboolean
lrg_color_area_handle_event (LrgWidget        *widget,
                             const LrgUIEvent *event)
{
	LrgColorArea   *self = LRG_COLOR_AREA (widget);
	LrgColorPicker *picker = area_picker (self);

	if (picker == NULL)
		return FALSE;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		self->dragging = TRUE;
		area_apply (self, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event));
		return TRUE;

	case LRG_UI_EVENT_MOUSE_MOVE:
		if (!self->dragging)
			return FALSE;
		area_apply (self, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event));
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0 || !self->dragging)
			return FALSE;
		self->dragging = FALSE;
		lrg_color_picker_add_recent (picker, &picker->color);
		return TRUE;

	case LRG_UI_EVENT_KEY_DOWN:
		return area_nudge (self, lrg_ui_event_get_key (event),
		                   (lrg_ui_event_get_modifiers (event) & LRG_INPUT_MODIFIER_SHIFT) != 0);

	default:
		return FALSE;
	}
}

static void
lrg_color_area_class_init (LrgColorAreaClass *klass)
{
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	widget_class->style_defaults = lrg_color_area_style_defaults;
	widget_class->paint = lrg_color_area_paint;
	widget_class->step = lrg_color_area_step;
	widget_class->handle_event = lrg_color_area_handle_event;
}

static void
lrg_color_area_init (LrgColorArea *self)
{
	lrg_spring_init (&self->knob_x, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->knob_y, 0.0f, LRG_MOTION_SNAPPY);
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

static LrgColorArea *
color_area_new (LrgColorPickerArea kind)
{
	LrgColorArea *area = g_object_new (LRG_TYPE_COLOR_AREA, NULL);

	area->kind = kind;
	return area;
}

/* ==========================================================================
 * Colour state
 * ========================================================================== */

/* sync_hex: show the current colour in the hex field */
static void
sync_hex (LrgColorPicker *self)
{
	g_autofree gchar *hex = NULL;

	if (self->hex_entry == NULL)
		return;
	hex = lrg_color_picker_get_hex (self);
	if (g_strcmp0 (hex, lrg_text_input_get_text (self->hex_entry)) == 0)
		return;
	self->syncing_hex = TRUE;
	lrg_text_input_set_text (self->hex_entry, hex);
	self->syncing_hex = FALSE;
}

/*
 * set_hsva:
 *
 * The one place the colour changes. Stores HSV + alpha, derives the
 * 8-bit colour and, if that changed, notifies and emits color-changed.
 * The hex field is rewritten unless the change came from it (so the
 * caret does not jump while typing).
 */
static void
set_hsva (LrgColorPicker *self,
          gdouble         h,
          gdouble         s,
          gdouble         v,
          gdouble         a,
          gboolean        from_hex)
{
	GrlColor old = self->color;

	if (h < 0.0 || h > 360.0)
	{
		h = fmod (h, 360.0);
		if (h < 0.0)
			h += 360.0;
	}
	self->h = h;
	self->s = CLAMP (s, 0.0, 1.0);
	self->v = CLAMP (v, 0.0, 1.0);
	self->a = CLAMP (a, 0.0, 1.0);
	lrg_color_picker_hsv_to_rgb (self->h, self->s, self->v, self->a, &self->color);

	if (!from_hex)
		sync_hex (self);
	if (color_equal (&old, &self->color))
		return;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
	g_signal_emit (self, signals[SIGNAL_COLOR_CHANGED], 0);
}

/*
 * set_rgba:
 *
 * Sets an 8-bit colour, keeping the hue for greys (and the saturation
 * for black) so the cursors do not jump to the corner.
 */
static void
set_rgba (LrgColorPicker *self,
          const GrlColor *c,
          gboolean        keep_alpha,
          gboolean        from_hex)
{
	gdouble h;
	gdouble s;
	gdouble v;

	lrg_color_picker_rgb_to_hsv (c, &h, &s, &v);
	if (s <= 0.0 || v <= 0.0)
		h = self->h;
	if (v <= 0.0)
		s = self->s;
	set_hsva (self, h, s, v, keep_alpha ? self->a : c->a / 255.0, from_hex);
}

static void
on_hex_changed (LrgTextInput *entry,
                gpointer      user_data)
{
	LrgColorPicker *self = LRG_COLOR_PICKER (user_data);
	const gchar    *text = lrg_text_input_get_text (entry);
	const gchar    *p = text != NULL ? text : "";
	GrlColor        c;
	gsize           len;

	if (self->syncing_hex)
		return;

	/* Live: only complete 6 / 8 digit values while typing */
	while (*p == ' ' || *p == '#')
		p++;
	len = strlen (p);
	if ((len == 6 || (len == 8 && self->show_alpha)) && lrg_color_parse (p, &c))
		set_rgba (self, &c, len == 6 || !self->show_alpha, TRUE);
}

static void
on_hex_submitted (LrgTextInput *entry,
                  gpointer      user_data)
{
	LrgColorPicker *self = LRG_COLOR_PICKER (user_data);

	/* Any valid form is accepted on Enter; then the canonical text */
	if (lrg_color_picker_set_hex (self, lrg_text_input_get_text (entry)))
		lrg_color_picker_add_recent (self, &self->color);
	sync_hex (self);
}

/* ==========================================================================
 * Popup mode
 * ========================================================================== */

static void
forget_popup (LrgColorPicker *self)
{
	if (self->canvas != NULL && self->dismissed_id != 0)
		g_signal_handler_disconnect (self->canvas, self->dismissed_id);
	self->dismissed_id = 0;
	g_clear_weak_pointer (&self->canvas);

	if (self->popup != NULL)
	{
		guint i;

		/* Take the popup's recent colours, plus the final colour */
		g_array_set_size (self->recent, 0);
		for (i = 0; i < self->popup->recent->len; i++)
			g_array_append_val (self->recent, g_array_index (self->popup->recent, GrlColor, i));
		g_signal_handlers_disconnect_by_data (self->popup, self);
		g_clear_object (&self->popup);
		lrg_color_picker_add_recent (self, &self->color);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POPUP_OPEN]);
	}
}

static void
on_popup_dismissed (LrgCanvas *canvas,
                    LrgWidget *popup,
                    gpointer   user_data)
{
	LrgColorPicker *self = LRG_COLOR_PICKER (user_data);

	if (self->popup != NULL && popup == LRG_WIDGET (self->popup))
		forget_popup (self);
}

static void
on_state_changed (LrgWidget      *widget,
                  LrgWidgetState  previous,
                  gpointer        user_data)
{
	LrgColorPicker *self = LRG_COLOR_PICKER (widget);
	LrgWidgetState  now = lrg_widget_get_state_flags (widget);

	/* A press on the button that closed the popup must not reopen it */
	if ((previous & LRG_WIDGET_STATE_OPEN) && !(now & LRG_WIDGET_STATE_OPEN) &&
	    (now & LRG_WIDGET_STATE_HOVERED))
		self->swallow_press = TRUE;
}

/* on_popup_color_changed: mirror the popup picker into the button */
static void
on_popup_color_changed (LrgColorPicker *popup,
                        gpointer        user_data)
{
	LrgColorPicker *self = LRG_COLOR_PICKER (user_data);

	set_hsva (self, popup->h, popup->s, popup->v, popup->a, FALSE);
}

static void
open_popup (LrgColorPicker *self,
            gboolean        keyboard)
{
	LrgColorPicker *inner;
	LrgCanvas      *canvas;
	const GQuark   *classes;
	guint           n_classes;
	guint           c;

	if (self->popup != NULL || !self->popup_mode)
		return;
	canvas = find_canvas (LRG_WIDGET (self));
	if (canvas == NULL)
	{
		lrg_debug (LRG_LOG_DOMAIN_UI, "colour picker is not on a canvas; cannot open its popup");
		return;
	}

	/* A full picker with our colour, palette and history */
	inner = lrg_color_picker_new ();
	g_set_weak_pointer (&inner->owner, self);
	lrg_widget_set_theme (LRG_WIDGET (inner), lrg_widget_get_theme (LRG_WIDGET (self)));
	lrg_widget_set_appear (LRG_WIDGET (inner), LRG_APPEAR_FLY_DOWN, 0.0f);
	classes = lrg_widget_get_classes (LRG_WIDGET (self), &n_classes);
	for (c = 0; c < n_classes; c++)
		lrg_widget_add_class (LRG_WIDGET (inner), g_quark_to_string (classes[c]));
	lrg_color_picker_set_show_alpha (inner, self->show_alpha);
	lrg_color_picker_set_palette (inner, (const GrlColor *) (gpointer) self->palette->data, self->palette->len);
	g_array_set_size (inner->recent, 0);
	g_array_append_vals (inner->recent, self->recent->data, self->recent->len);
	set_hsva (inner, self->h, self->s, self->v, self->a, FALSE);
	g_signal_connect (inner, "color-changed", G_CALLBACK (on_popup_color_changed), self);

	self->popup = inner;
	g_set_weak_pointer (&self->canvas, canvas);
	self->dismissed_id = g_signal_connect_object (canvas, "popup-dismissed",
	                                              G_CALLBACK (on_popup_dismissed), self, 0);
	lrg_canvas_show_popup (canvas, LRG_WIDGET (inner), LRG_WIDGET (self), LRG_POPUP_PLACEMENT_BELOW, FALSE);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POPUP_OPEN]);

	/* Focus lands on the square, so the arrows work at once */
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (inner->sv_area));
	if (keyboard)
		lrg_widget_set_state_flags (LRG_WIDGET (inner->sv_area), LRG_WIDGET_STATE_FOCUS_VISIBLE);
}

/* ==========================================================================
 * LrgWidget / LrgContainer vfuncs
 * ========================================================================== */

static void
lrg_color_picker_style_defaults (LrgWidget      *widget,
                                 LrgUIPart       part,
                                 LrgWidgetState  state,
                                 LrgPartLook    *look)
{
	LrgColorPicker *self = LRG_COLOR_PICKER (widget);
	LrgTheme       *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_BOX:
		if (self->popup_mode)
		{
			LrgThemeColor bg = LRG_THEME_COLOR_CONTROL;

			/* Compact: a button around the colour swatch */
			if (state & LRG_WIDGET_STATE_PRESSED)
				bg = LRG_THEME_COLOR_CONTROL_PRESSED;
			else if (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_OPEN))
				bg = LRG_THEME_COLOR_CONTROL_HOVER;
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, bg));
			lrg_part_look_set_border (look,
			                          lrg_theme_get_color (theme, (state & LRG_WIDGET_STATE_OPEN)
			                                                      ? LRG_THEME_COLOR_ACCENT
			                                                      : LRG_THEME_COLOR_BORDER),
			                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
			lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
		}
		else
		{
			GrlColor shadow;

			/* Full: a card */
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
			                            ((gfloat) lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW)->a / 255.0f) *
			                            0.5f, &shadow);
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
			lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
			                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
			lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_POPUP_RADIUS));
			lrg_part_look_set_shadow (look, &shadow, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR) * 0.5f,
			                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y) * 0.5f);
		}
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		break;

	case LRG_UI_PART_POPUP:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_POPUP_RADIUS));
		lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y));
		break;

	case LRG_UI_PART_ITEM:
		{
			GrlColor none = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);

			/* Swatch ring: none, muted when hovered, accent when it is
			 * the current colour (SELECTED) */
			none.a = 0;
			if (state & LRG_WIDGET_STATE_SELECTED)
				lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 2.0f);
			else if (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED))
				lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED), 1.5f);
			else
				lrg_part_look_set_border (look, &none, 1.5f);
			lrg_part_look_set_radius (look, MAX (2.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS) * 0.6f));
		}
		break;

	case LRG_UI_PART_ARROW:
		lrg_part_look_set_foreground (look,
		                              lrg_theme_get_color (theme,
		                                                   (state & LRG_WIDGET_STATE_OPEN)
		                                                   ? LRG_THEME_COLOR_ACCENT
		                                                   : (state & LRG_WIDGET_STATE_HOVERED)
		                                                     ? LRG_THEME_COLOR_TEXT
		                                                     : LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_size (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE) * 0.5f);
		break;

	default:
		LRG_WIDGET_CLASS (lrg_color_picker_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_color_picker_measure (LrgWidget *widget,
                          gfloat    *width,
                          gfloat    *height)
{
	LrgColorPicker *self = LRG_COLOR_PICKER (widget);
	Layout          lay;
	gfloat          ch = metric (widget, LRG_THEME_METRIC_CONTROL_HEIGHT);

	if (self->popup_mode)
	{
		*width = roundf (ch * 1.6f + metric (widget, LRG_THEME_METRIC_FONT_SIZE));
		*height = ch;
		return;
	}
	compute_layout (self, preferred_width (widget), 0.0f, &lay);
	*width = preferred_width (widget);
	*height = lay.height;
}

static void
lrg_color_picker_layout_children (LrgContainer *container)
{
	LrgColorPicker *self = LRG_COLOR_PICKER (container);
	Layout          lay;

	if (self->popup_mode)
		return;
	current_layout (self, &lay);
	lrg_widget_allocate (LRG_WIDGET (self->sv_area), lay.sv.x, lay.sv.y, lay.sv.width, lay.sv.height);
	lrg_widget_allocate (LRG_WIDGET (self->hue_area), lay.hue.x, lay.hue.y, lay.hue.width, lay.hue.height);
	if (self->show_alpha)
		lrg_widget_allocate (LRG_WIDGET (self->alpha_area), lay.alpha.x, lay.alpha.y, lay.alpha.width,
		                     lay.alpha.height);
	lrg_widget_allocate (LRG_WIDGET (self->hex_entry), lay.hex.x, lay.hex.y, lay.hex.width, lay.hex.height);
	if (lrg_text_input_get_font_size (self->hex_entry) != metric (LRG_WIDGET (self), LRG_THEME_METRIC_FONT_SIZE))
		lrg_text_input_set_font_size (self->hex_entry, metric (LRG_WIDGET (self), LRG_THEME_METRIC_FONT_SIZE));
}

/* paint_compact: the swatch button of popup mode */
static void
paint_compact (LrgColorPicker *self,
               LrgPaintList   *list)
{
	LrgWidget   *widget = LRG_WIDGET (self);
	LrgPartLook  box;
	LrgPartLook  arrow;
	gfloat       x = lrg_widget_get_world_x (widget);
	gfloat       y = lrg_widget_get_world_y (widget);
	gfloat       w = lrg_widget_get_width (widget);
	gfloat       h = lrg_widget_get_height (widget);
	gfloat       size = metric (widget, LRG_THEME_METRIC_FONT_SIZE);
	gfloat       inset = roundf (h * 0.2f);
	gfloat       sw = MAX (0.0f, w - inset * 2.0f - size);
	gfloat       r = MAX (2.0f, metric (widget, LRG_THEME_METRIC_CORNER_RADIUS) * 0.6f);

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, x, y, w, h);
	if (box.opacity < 1.0f)
		lrg_paint_list_push_opacity (list, box.opacity);

	/* The colour, over a checkerboard when translucent */
	if (self->color.a < 255)
		paint_checker (list, x + inset, y + inset, sw, h - inset * 2.0f, r, roundf ((h - inset * 2.0f) * 0.5f));
	lrg_paint_list_rect (list, x + inset, y + inset, sw, h - inset * 2.0f, r, &self->color);

	/* Chevron */
	lrg_widget_resolve_look (widget, LRG_UI_PART_ARROW, &arrow);
	{
		gfloat s = arrow.size > 0.0f ? arrow.size : size * 0.5f;
		gfloat ax = x + w - inset * 0.5f - size * 0.5f;
		gfloat cy = y + h * 0.5f;
		gfloat t = MAX (1.5f, s * 0.2f);
		gfloat flip = (lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_OPEN) ? -1.0f : 1.0f;

		lrg_paint_list_line (list, ax - s * 0.5f, cy - s * 0.25f * flip, ax, cy + s * 0.25f * flip, t,
		                     &arrow.foreground);
		lrg_paint_list_line (list, ax, cy + s * 0.25f * flip, ax + s * 0.5f, cy - s * 0.25f * flip, t,
		                     &arrow.foreground);
	}

	if (box.opacity < 1.0f)
		lrg_paint_list_pop_opacity (list);
}

static void
lrg_color_picker_paint (LrgWidget    *widget,
                        LrgPaintList *list)
{
	LrgColorPicker *self = LRG_COLOR_PICKER (widget);
	LrgPartLook     surface;
	Layout          lay;
	gfloat          x = lrg_widget_get_world_x (widget);
	gfloat          y = lrg_widget_get_world_y (widget);
	gfloat          r = MAX (2.0f, metric (widget, LRG_THEME_METRIC_CORNER_RADIUS) * 0.6f);
	GrlColor        edge;
	guint           i;

	if (self->popup_mode)
	{
		paint_compact (self, list);
		return;
	}

	/* Card surface (popup surface when shown in a popup) */
	lrg_widget_resolve_look (widget, self->owner != NULL ? LRG_UI_PART_POPUP : LRG_UI_PART_BOX, &surface);
	lrg_widget_paint_box (widget, list, &surface, x, y, lrg_widget_get_width (widget),
	                      lrg_widget_get_height (widget));
	if (surface.opacity < 1.0f)
		lrg_paint_list_push_opacity (list, surface.opacity);

	/* Square, strips, hex field */
	LRG_WIDGET_CLASS (lrg_color_picker_parent_class)->paint (widget, list);

	current_layout (self, &lay);

	/* Preview of the current colour */
	paint_checker (list, x + lay.preview.x, y + lay.preview.y, lay.preview.width, lay.preview.height, r,
	               roundf (lay.preview.height * 0.25f));
	lrg_paint_list_rect (list, x + lay.preview.x, y + lay.preview.y, lay.preview.width, lay.preview.height, r,
	                     &self->color);
	lrg_paint_list_border (list, x + lay.preview.x, y + lay.preview.y, lay.preview.width, lay.preview.height, r,
	                       1.0f, lrg_theme_get_color (lrg_widget_get_theme (widget), LRG_THEME_COLOR_BORDER));

	/* Swatches: palette, then recent */
	lrg_paint_color_with_alpha (lrg_theme_get_color (lrg_widget_get_theme (widget), LRG_THEME_COLOR_BORDER), 0.7f,
	                            &edge);
	for (i = 0; i < self->palette->len + self->recent->len; i++)
	{
		GrlRectangle    sr;
		LrgPartLook     ring;
		LrgWidgetState  st = LRG_WIDGET_STATE_NONE;
		const GrlColor *c = swatch_color (self, i);

		swatch_rect (self, &lay, i, &sr);
		sr.x += x;
		sr.y += y;
		if ((gint) i == self->hover_swatch)
			st |= LRG_WIDGET_STATE_HOVERED;
		if ((gint) i == self->press_swatch && (gint) i == self->hover_swatch)
			st |= LRG_WIDGET_STATE_PRESSED;
		if (color_equal (c, &self->color))
			st |= LRG_WIDGET_STATE_SELECTED;
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, st, &ring);

		if (c->a < 255)
			paint_checker (list, sr.x, sr.y, sr.width, sr.height, ring.radii.top_left, roundf (sr.width * 0.5f));
		/* A faint hairline so swatches matching the surface still show */
		lrg_paint_list_rect_ex (list, sr.x, sr.y, sr.width, sr.height, &ring.radii, c, NULL, 1.0f, &edge);
		if (ring.border.a > 0 && ring.border_width > 0.0f)
		{
			LrgCornerRadii outer = ring.radii;
			gfloat         g = ring.border_width + 1.0f;

			outer.top_left += g;
			outer.top_right += g;
			outer.bottom_left += g;
			outer.bottom_right += g;
			lrg_paint_list_rect_ex (list, sr.x - g, sr.y - g, sr.width + g * 2.0f, sr.height + g * 2.0f, &outer,
			                        NULL, NULL, ring.border_width, &ring.border);
		}
	}

	if (surface.opacity < 1.0f)
		lrg_paint_list_pop_opacity (list);
}

static gboolean
lrg_color_picker_handle_event (LrgWidget        *widget,
                               const LrgUIEvent *event)
{
	LrgColorPicker *self = LRG_COLOR_PICKER (widget);
	gfloat          ex = lrg_ui_event_get_x (event);
	gfloat          ey = lrg_ui_event_get_y (event);

	if (self->popup_mode)
	{
		switch (lrg_ui_event_get_event_type (event))
		{
		case LRG_UI_EVENT_POINTER_ENTER:
			self->swallow_press = FALSE;
			return FALSE;

		case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
			if (lrg_ui_event_get_button (event) != 0 || !lrg_widget_contains_point (widget, ex, ey))
				return FALSE;
			if (self->swallow_press)
				self->swallow_press = FALSE;
			else if (self->popup != NULL)
				lrg_color_picker_popdown (self);
			else
				open_popup (self, FALSE);
			return TRUE;

		case LRG_UI_EVENT_MOUSE_BUTTON_UP:
			return lrg_ui_event_get_button (event) == 0;

		case LRG_UI_EVENT_ACTIVATE:
			if (self->popup != NULL)
				lrg_color_picker_popdown (self);
			else
				open_popup (self, TRUE);
			return TRUE;

		default:
			return FALSE;
		}
	}

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		self->hover_swatch = lrg_widget_contains_point (widget, ex, ey) ? swatch_at (self, ex, ey) : -1;
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hover_swatch = -1;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		self->press_swatch = swatch_at (self, ex, ey);
		/* The surface swallows clicks (a popup must not pass them on) */
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		{
			gint pressed = self->press_swatch;

			if (lrg_ui_event_get_button (event) != 0)
				return FALSE;
			self->press_swatch = -1;
			if (pressed >= 0 && swatch_at (self, ex, ey) == pressed)
			{
				GrlColor c = *swatch_color (self, (guint) pressed);

				set_rgba (self, &c, !self->show_alpha, FALSE);
				if ((guint) pressed < self->palette->len)
					lrg_color_picker_add_recent (self, &self->color);
			}
			return TRUE;
		}

	case LRG_UI_EVENT_CANCEL:
		/* Escape inside a popup picker closes the popup */
		if (self->owner != NULL)
		{
			LrgColorPicker *owner = self->owner;
			LrgCanvas      *canvas = find_canvas (widget);

			lrg_color_picker_popdown (owner);
			if (canvas != NULL && lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (owner))
				lrg_widget_set_state_flags (LRG_WIDGET (owner), LRG_WIDGET_STATE_FOCUS_VISIBLE);
			return TRUE;
		}
		return FALSE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_color_picker_dispose (GObject *object)
{
	LrgColorPicker *self = LRG_COLOR_PICKER (object);

	if (self->popup != NULL && self->canvas != NULL)
	{
		g_autoptr(LrgCanvas) canvas = g_object_ref (self->canvas);
		g_autoptr(LrgWidget) popup = LRG_WIDGET (g_object_ref (self->popup));

		forget_popup (self);
		lrg_canvas_dismiss_popup (canvas, popup);
	}
	forget_popup (self);
	g_clear_weak_pointer (&self->owner);

	G_OBJECT_CLASS (lrg_color_picker_parent_class)->dispose (object);
}

static void
lrg_color_picker_finalize (GObject *object)
{
	LrgColorPicker *self = LRG_COLOR_PICKER (object);

	g_clear_pointer (&self->palette, g_array_unref);
	g_clear_pointer (&self->recent, g_array_unref);

	G_OBJECT_CLASS (lrg_color_picker_parent_class)->finalize (object);
}

static void
lrg_color_picker_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	LrgColorPicker *self = LRG_COLOR_PICKER (object);

	switch (prop_id)
	{
	case PROP_COLOR:
		g_value_set_boxed (value, &self->color);
		break;
	case PROP_SHOW_ALPHA:
		g_value_set_boolean (value, self->show_alpha);
		break;
	case PROP_POPUP_MODE:
		g_value_set_boolean (value, self->popup_mode);
		break;
	case PROP_POPUP_OPEN:
		g_value_set_boolean (value, self->popup != NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_color_picker_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	LrgColorPicker *self = LRG_COLOR_PICKER (object);

	switch (prop_id)
	{
	case PROP_COLOR:
		if (g_value_get_boxed (value) != NULL)
			lrg_color_picker_set_color (self, g_value_get_boxed (value));
		break;
	case PROP_SHOW_ALPHA:
		lrg_color_picker_set_show_alpha (self, g_value_get_boolean (value));
		break;
	case PROP_POPUP_MODE:
		lrg_color_picker_set_popup_mode (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_color_picker_class_init (LrgColorPickerClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_color_picker_dispose;
	object_class->finalize = lrg_color_picker_finalize;
	object_class->get_property = lrg_color_picker_get_property;
	object_class->set_property = lrg_color_picker_set_property;

	widget_class->style_defaults = lrg_color_picker_style_defaults;
	widget_class->measure = lrg_color_picker_measure;
	widget_class->paint = lrg_color_picker_paint;
	widget_class->handle_event = lrg_color_picker_handle_event;

	container_class->layout_children = lrg_color_picker_layout_children;

	/**
	 * LrgColorPicker:color:
	 *
	 * The current colour (#GrlColor).
	 */
	properties[PROP_COLOR] =
		g_param_spec_boxed ("color", NULL, NULL, GRL_TYPE_COLOR,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgColorPicker:show-alpha:
	 *
	 * Whether the alpha strip is shown and the hex field has 8 digits.
	 */
	properties[PROP_SHOW_ALPHA] =
		g_param_spec_boolean ("show-alpha", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgColorPicker:popup-mode:
	 *
	 * %TRUE shows a compact swatch button that opens the full picker in
	 * a popup.
	 */
	properties[PROP_POPUP_MODE] =
		g_param_spec_boolean ("popup-mode", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgColorPicker:popup-open:
	 *
	 * Whether the popup picker is showing.
	 */
	properties[PROP_POPUP_OPEN] =
		g_param_spec_boolean ("popup-open", NULL, NULL, FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgColorPicker::color-changed:
	 * @self: the colour picker
	 *
	 * Emitted whenever the colour changes: while dragging, on keys,
	 * hex entry, swatch clicks, or from code.
	 */
	signals[SIGNAL_COLOR_CHANGED] =
		g_signal_new ("color-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_color_picker_init (LrgColorPicker *self)
{
	static const guint32 defaults[] = {
		0xef4444ff, 0xf97316ff, 0xeab308ff, 0x22c55eff, 0x14b8a6ff, 0x3b82f6ff,
		0x6366f1ff, 0xa855f7ff, 0xec4899ff, 0xffffffff, 0x808080ff, 0x000000ff
	};
	guint i;

	self->h = 0.0;
	self->s = 0.0;
	self->v = 1.0;
	self->a = 1.0;
	lrg_color_picker_hsv_to_rgb (self->h, self->s, self->v, self->a, &self->color);
	self->show_alpha = TRUE;
	self->hover_swatch = -1;
	self->press_swatch = -1;

	self->palette = g_array_new (FALSE, TRUE, sizeof (GrlColor));
	self->recent = g_array_new (FALSE, TRUE, sizeof (GrlColor));
	for (i = 0; i < G_N_ELEMENTS (defaults); i++)
	{
		GrlColor c;

		c.r = (guint8) (defaults[i] >> 24);
		c.g = (guint8) (defaults[i] >> 16);
		c.b = (guint8) (defaults[i] >> 8);
		c.a = (guint8) defaults[i];
		g_array_append_val (self->palette, c);
	}

	/* Children: square, strips, hex field */
	self->sv_area = color_area_new (LRG_COLOR_PICKER_AREA_SV);
	self->hue_area = color_area_new (LRG_COLOR_PICKER_AREA_HUE);
	self->alpha_area = color_area_new (LRG_COLOR_PICKER_AREA_ALPHA);
	self->hex_entry = lrg_text_input_new ();
	lrg_text_input_set_max_length (self->hex_entry, 9);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->sv_area));
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->hue_area));
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->alpha_area));
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->hex_entry));
	g_object_unref (self->sv_area);
	g_object_unref (self->hue_area);
	g_object_unref (self->alpha_area);
	g_object_unref (self->hex_entry);
	sync_hex (self);
	g_signal_connect_object (self->hex_entry, "text-changed", G_CALLBACK (on_hex_changed), self, 0);
	g_signal_connect_object (self->hex_entry, "submitted", G_CALLBACK (on_hex_submitted), self, 0);

	g_signal_connect (self, "state-changed", G_CALLBACK (on_state_changed), NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgColorPicker *
lrg_color_picker_new (void)
{
	return g_object_new (LRG_TYPE_COLOR_PICKER, NULL);
}

void
lrg_color_picker_get_color (LrgColorPicker *self,
                            GrlColor       *color)
{
	g_return_if_fail (LRG_IS_COLOR_PICKER (self));
	g_return_if_fail (color != NULL);

	*color = self->color;
}

void
lrg_color_picker_set_color (LrgColorPicker *self,
                            const GrlColor *color)
{
	g_return_if_fail (LRG_IS_COLOR_PICKER (self));
	g_return_if_fail (color != NULL);

	set_rgba (self, color, FALSE, FALSE);
}

void
lrg_color_picker_get_hsv (LrgColorPicker *self,
                          gdouble        *hue,
                          gdouble        *saturation,
                          gdouble        *value)
{
	g_return_if_fail (LRG_IS_COLOR_PICKER (self));

	if (hue != NULL)
		*hue = self->h;
	if (saturation != NULL)
		*saturation = self->s;
	if (value != NULL)
		*value = self->v;
}

void
lrg_color_picker_set_hsv (LrgColorPicker *self,
                          gdouble         hue,
                          gdouble         saturation,
                          gdouble         value)
{
	g_return_if_fail (LRG_IS_COLOR_PICKER (self));

	set_hsva (self, hue, saturation, value, self->a, FALSE);
}

gdouble
lrg_color_picker_get_alpha (LrgColorPicker *self)
{
	g_return_val_if_fail (LRG_IS_COLOR_PICKER (self), 1.0);

	return self->a;
}

void
lrg_color_picker_set_alpha (LrgColorPicker *self,
                            gdouble         alpha)
{
	g_return_if_fail (LRG_IS_COLOR_PICKER (self));

	set_hsva (self, self->h, self->s, self->v, alpha, FALSE);
}

gchar *
lrg_color_picker_get_hex (LrgColorPicker *self)
{
	g_return_val_if_fail (LRG_IS_COLOR_PICKER (self), NULL);

	if (self->show_alpha)
		return g_strdup_printf ("#%02X%02X%02X%02X", self->color.r, self->color.g, self->color.b, self->color.a);
	return g_strdup_printf ("#%02X%02X%02X", self->color.r, self->color.g, self->color.b);
}

gboolean
lrg_color_picker_set_hex (LrgColorPicker *self,
                          const gchar    *hex)
{
	GrlColor     c;
	const gchar *p;
	gsize        len;

	g_return_val_if_fail (LRG_IS_COLOR_PICKER (self), FALSE);

	if (hex == NULL || !lrg_color_parse (hex, &c))
		return FALSE;

	/* Forms without alpha keep the current alpha */
	p = hex;
	while (*p == ' ' || *p == '#')
		p++;
	len = strlen (p);
	while (len > 0 && g_ascii_isspace (p[len - 1]))
		len--;
	set_rgba (self, &c, (len == 3 || len == 6) || !self->show_alpha, FALSE);
	return TRUE;
}

gboolean
lrg_color_picker_get_show_alpha (LrgColorPicker *self)
{
	g_return_val_if_fail (LRG_IS_COLOR_PICKER (self), TRUE);

	return self->show_alpha;
}

void
lrg_color_picker_set_show_alpha (LrgColorPicker *self,
                                 gboolean        show_alpha)
{
	g_return_if_fail (LRG_IS_COLOR_PICKER (self));

	show_alpha = !!show_alpha;
	if (show_alpha == self->show_alpha)
		return;
	self->show_alpha = show_alpha;
	lrg_widget_set_visible (LRG_WIDGET (self->alpha_area), show_alpha && !self->popup_mode);
	lrg_text_input_set_max_length (self->hex_entry, show_alpha ? 9 : 7);
	sync_hex (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_ALPHA]);
}

gboolean
lrg_color_picker_get_popup_mode (LrgColorPicker *self)
{
	g_return_val_if_fail (LRG_IS_COLOR_PICKER (self), FALSE);

	return self->popup_mode;
}

void
lrg_color_picker_set_popup_mode (LrgColorPicker *self,
                                 gboolean        popup_mode)
{
	g_return_if_fail (LRG_IS_COLOR_PICKER (self));

	popup_mode = !!popup_mode;
	if (popup_mode == self->popup_mode)
		return;
	if (!popup_mode)
		lrg_color_picker_popdown (self);
	self->popup_mode = popup_mode;

	/* The compact button is one focusable widget; the full picker's
	 * focus lives in its children */
	lrg_widget_set_visible (LRG_WIDGET (self->sv_area), !popup_mode);
	lrg_widget_set_visible (LRG_WIDGET (self->hue_area), !popup_mode);
	lrg_widget_set_visible (LRG_WIDGET (self->alpha_area), !popup_mode && self->show_alpha);
	lrg_widget_set_visible (LRG_WIDGET (self->hex_entry), !popup_mode);
	lrg_widget_set_focusable (LRG_WIDGET (self), popup_mode);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POPUP_MODE]);
}

void
lrg_color_picker_popup (LrgColorPicker *self)
{
	g_return_if_fail (LRG_IS_COLOR_PICKER (self));

	open_popup (self, FALSE);
}

void
lrg_color_picker_popdown (LrgColorPicker *self)
{
	g_return_if_fail (LRG_IS_COLOR_PICKER (self));

	if (self->popup == NULL)
		return;
	if (self->canvas != NULL)
	{
		g_autoptr(LrgCanvas) canvas = g_object_ref (self->canvas);
		g_autoptr(LrgWidget) popup = LRG_WIDGET (g_object_ref (self->popup));

		lrg_canvas_dismiss_popup (canvas, popup);
	}
	forget_popup (self);
}

gboolean
lrg_color_picker_get_popup_open (LrgColorPicker *self)
{
	g_return_val_if_fail (LRG_IS_COLOR_PICKER (self), FALSE);

	return self->popup != NULL;
}

LrgColorPicker *
lrg_color_picker_get_popup_picker (LrgColorPicker *self)
{
	g_return_val_if_fail (LRG_IS_COLOR_PICKER (self), NULL);

	return self->popup;
}

void
lrg_color_picker_set_palette (LrgColorPicker *self,
                              const GrlColor *colors,
                              guint           n_colors)
{
	g_return_if_fail (LRG_IS_COLOR_PICKER (self));
	g_return_if_fail (colors != NULL || n_colors == 0);

	g_array_set_size (self->palette, 0);
	if (n_colors > 0)
		g_array_append_vals (self->palette, colors, n_colors);
	self->hover_swatch = -1;
	self->press_swatch = -1;
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

const GrlColor *
lrg_color_picker_get_palette (LrgColorPicker *self,
                              guint          *n_colors)
{
	g_return_val_if_fail (LRG_IS_COLOR_PICKER (self), NULL);
	g_return_val_if_fail (n_colors != NULL, NULL);

	*n_colors = self->palette->len;
	return (const GrlColor *) (gpointer) self->palette->data;
}

void
lrg_color_picker_add_recent (LrgColorPicker *self,
                             const GrlColor *color)
{
	GrlColor c;
	guint    old;
	guint    i;

	g_return_if_fail (LRG_IS_COLOR_PICKER (self));
	g_return_if_fail (color != NULL);

	c = *color;
	old = self->recent->len;
	for (i = 0; i < self->recent->len; i++)
	{
		if (color_equal (&g_array_index (self->recent, GrlColor, i), &c))
		{
			g_array_remove_index (self->recent, i);
			break;
		}
	}
	g_array_prepend_val (self->recent, c);
	if (self->recent->len > LRG_COLOR_PICKER_MAX_RECENT)
		g_array_set_size (self->recent, LRG_COLOR_PICKER_MAX_RECENT);

	/* A new row can change the height */
	if (self->recent->len != old)
		lrg_widget_queue_layout (LRG_WIDGET (self));
}

const GrlColor *
lrg_color_picker_get_recent (LrgColorPicker *self,
                             guint          *n_colors)
{
	g_return_val_if_fail (LRG_IS_COLOR_PICKER (self), NULL);
	g_return_val_if_fail (n_colors != NULL, NULL);

	*n_colors = self->recent->len;
	return (const GrlColor *) (gpointer) self->recent->data;
}

void
lrg_color_picker_clear_recent (LrgColorPicker *self)
{
	g_return_if_fail (LRG_IS_COLOR_PICKER (self));

	if (self->recent->len == 0)
		return;
	g_array_set_size (self->recent, 0);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

gboolean
lrg_color_picker_get_area_bounds (LrgColorPicker     *self,
                                  LrgColorPickerArea  area,
                                  GrlRectangle       *bounds)
{
	Layout             lay;
	const GrlRectangle *r;

	g_return_val_if_fail (LRG_IS_COLOR_PICKER (self), FALSE);
	g_return_val_if_fail (bounds != NULL, FALSE);

	if (self->popup_mode)
		return FALSE;
	current_layout (self, &lay);
	switch (area)
	{
	case LRG_COLOR_PICKER_AREA_SV:
		r = &lay.sv;
		break;
	case LRG_COLOR_PICKER_AREA_HUE:
		r = &lay.hue;
		break;
	case LRG_COLOR_PICKER_AREA_ALPHA:
		if (!self->show_alpha)
			return FALSE;
		r = &lay.alpha;
		break;
	case LRG_COLOR_PICKER_AREA_HEX:
		r = &lay.hex;
		break;
	case LRG_COLOR_PICKER_AREA_PREVIEW:
		r = &lay.preview;
		break;
	case LRG_COLOR_PICKER_AREA_SWATCHES:
		if (lay.swatches.height <= 0.0f)
			return FALSE;
		r = &lay.swatches;
		break;
	default:
		return FALSE;
	}
	bounds->x = lrg_widget_get_world_x (LRG_WIDGET (self)) + r->x;
	bounds->y = lrg_widget_get_world_y (LRG_WIDGET (self)) + r->y;
	bounds->width = r->width;
	bounds->height = r->height;
	return TRUE;
}

gboolean
lrg_color_picker_get_swatch_bounds (LrgColorPicker *self,
                                    guint           index,
                                    GrlRectangle   *bounds)
{
	Layout lay;

	g_return_val_if_fail (LRG_IS_COLOR_PICKER (self), FALSE);
	g_return_val_if_fail (bounds != NULL, FALSE);

	if (self->popup_mode)
		return FALSE;
	current_layout (self, &lay);
	if (!swatch_rect (self, &lay, index, bounds))
		return FALSE;
	bounds->x += lrg_widget_get_world_x (LRG_WIDGET (self));
	bounds->y += lrg_widget_get_world_y (LRG_WIDGET (self));
	return TRUE;
}
