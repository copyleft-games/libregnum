/* lrg-rating.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRating: A row of stars (or other shapes) for a 0..N rating, with halves.
 *
 * Every symbol is drawn twice: once in the TRACK colour (the empty
 * symbol) and once in the MARK colour clipped to the filled fraction,
 * which is how half symbols and the gliding fill work. The fill follows
 * a spring towards the value (or the hover preview while the pointer is
 * over the row), and every symbol owns a scale spring: the symbol under
 * the pointer lifts a little and the symbol that was just set "pops".
 * Stars are triangle fans, hearts two circles over a triangle, dots one
 * circle, all SDF shapes from the paint list.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>

#include "lrg-rating.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* Gap between symbols as a fraction of the symbol size */
#define RATING_GAP (0.22f)

/* Scale of the symbol under the pointer */
#define RATING_HOVER_SCALE (1.12f)

/* Scale a freshly set symbol jumps to before settling back */
#define RATING_POP_SCALE (1.45f)

/* Star inner radius as a fraction of the outer radius */
#define RATING_STAR_INNER (0.47f)

/* Triangles are pushed out by this much so fan seams overlap */
#define RATING_SEAM_INFLATE (0.6f)

struct _LrgRating
{
	LrgWidget       parent_instance;

	gdouble         value;
	guint           max;
	gboolean        allow_half;
	gboolean        read_only;
	LrgRatingShape  shape;
	gfloat          symbol_size;    /* 0 = theme */

	GrlColor        fill_color;
	gboolean        fill_set;

	/* Pointer preview, -1 when the pointer is elsewhere */
	gdouble         hover_value;

	/* Displayed fill (value or preview) and one scale per symbol */
	LrgSpring       fill;
	GArray         *scales;         /* LrgSpring */
	gboolean        stepped;
};

G_DEFINE_FINAL_TYPE (LrgRating, lrg_rating, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_VALUE,
	PROP_MAX,
	PROP_ALLOW_HALF,
	PROP_READ_ONLY,
	PROP_SHAPE,
	PROP_SYMBOL_SIZE,
	PROP_FILL_COLOR,
	N_PROPS
};

enum
{
	SIGNAL_VALUE_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

GType
lrg_rating_shape_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_RATING_SHAPE_STAR, "LRG_RATING_SHAPE_STAR", "star" },
			{ LRG_RATING_SHAPE_HEART, "LRG_RATING_SHAPE_HEART", "heart" },
			{ LRG_RATING_SHAPE_CIRCLE, "LRG_RATING_SHAPE_CIRCLE", "circle" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgRatingShape"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

/* ==========================================================================
 * Geometry helpers
 * ========================================================================== */

/* symbol_size: edge length of one symbol */
static gfloat
symbol_size (LrgRating *self)
{
	LrgTheme *theme;

	if (self->symbol_size > 0.0f)
		return self->symbol_size;
	theme = lrg_widget_get_theme (LRG_WIDGET (self));
	return roundf (lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT) * 0.75f);
}

/* symbol_pitch: distance between the left edges of two symbols */
static gfloat
symbol_pitch (LrgRating *self)
{
	gfloat size = symbol_size (self);

	return size + roundf (size * RATING_GAP);
}

/* row_width: width of all symbols and the gaps between them */
static gfloat
row_width (LrgRating *self)
{
	return symbol_pitch (self) * (gfloat) self->max - roundf (symbol_size (self) * RATING_GAP);
}

/* round_value: clamp to 0..max and snap to whole or half steps */
static gdouble
round_value (LrgRating *self,
             gdouble    value)
{
	value = CLAMP (value, 0.0, (gdouble) self->max);
	if (self->allow_half)
		return round (value * 2.0) / 2.0;
	return round (value);
}

/*
 * value_at:
 *
 * The value a press at canvas x @px picks: the symbol under it (or the
 * last one left of it, over a gap), and with halves the left half of
 * a symbol picks n - 0.5. Never 0, so a click always rates something.
 */
static gdouble
value_at (LrgRating *self,
          gfloat     px)
{
	gfloat  size = symbol_size (self);
	gfloat  pitch = symbol_pitch (self);
	gfloat  rel = px - lrg_widget_get_world_x (LRG_WIDGET (self));
	gint    i;
	gfloat  frac;

	if (rel <= 0.0f)
		return self->allow_half ? 0.5 : 1.0;

	i = (gint) floorf (rel / pitch);
	if (i >= (gint) self->max)
		return (gdouble) self->max;

	frac = (rel - (gfloat) i * pitch) / size;
	if (self->allow_half && frac < 0.5f)
		return (gdouble) i + 0.5;
	return (gdouble) i + 1.0;
}

/* hover_index: the symbol under the preview, -1 for none */
static gint
hover_index (LrgRating *self)
{
	if (self->hover_value < 0.0)
		return -1;
	return (gint) ceil (self->hover_value) - 1;
}

/* fill_target: what the fill shows: the preview, else the value */
static gfloat
fill_target (LrgRating *self)
{
	return (gfloat) (self->hover_value >= 0.0 ? self->hover_value : self->value);
}

/* ensure_scales: one scale spring per symbol */
static void
ensure_scales (LrgRating *self)
{
	guint old = self->scales->len;
	guint i;

	g_array_set_size (self->scales, self->max);
	for (i = old; i < self->max; i++)
		lrg_spring_init (&g_array_index (self->scales, LrgSpring, i), 1.0f, LRG_MOTION_BOUNCY);
}

/* symbol_scale: the current scale of symbol @i */
static gfloat
symbol_scale (LrgRating *self,
              guint      i)
{
	if (!self->stepped || i >= self->scales->len)
		return 1.0f;
	return g_array_index (self->scales, LrgSpring, i).value;
}

/* ==========================================================================
 * Drawing the shapes
 * ========================================================================== */

/*
 * fan_triangle:
 *
 * Records one triangle pushed out from its centroid by a fraction of a
 * pixel, so neighbouring triangles of a fan overlap instead of leaving
 * an anti-aliased seam between them.
 */
static void
fan_triangle (LrgPaintList   *list,
              gfloat          x0,
              gfloat          y0,
              gfloat          x1,
              gfloat          y1,
              gfloat          x2,
              gfloat          y2,
              const GrlColor *color)
{
	gfloat cx = (x0 + x1 + x2) / 3.0f;
	gfloat cy = (y0 + y1 + y2) / 3.0f;
	gfloat px[3] = { x0, x1, x2 };
	gfloat py[3] = { y0, y1, y2 };
	guint  k;

	for (k = 0; k < 3; k++)
	{
		gfloat dx = px[k] - cx;
		gfloat dy = py[k] - cy;
		gfloat d = sqrtf (dx * dx + dy * dy);

		if (d > 0.0f)
		{
			px[k] += dx / d * RATING_SEAM_INFLATE;
			py[k] += dy / d * RATING_SEAM_INFLATE;
		}
	}
	lrg_paint_list_triangle (list, px[0], py[0], px[1], py[1], px[2], py[2], color);
}

/*
 * paint_shape:
 *
 * Records one symbol of @shape centred on (@cx, @cy) whose bounding
 * box is @size across.
 */
static void
paint_shape (LrgPaintList   *list,
             LrgRatingShape  shape,
             gfloat          cx,
             gfloat          cy,
             gfloat          size,
             const GrlColor *color)
{
	gfloat r = size * 0.5f;

	switch (shape)
	{
	case LRG_RATING_SHAPE_HEART:
		{
			/* Two lobes and a point; the lobes cover the triangle's
			 * top corners so the outline stays smooth */
			gfloat lr = r * 0.52f;

			lrg_paint_list_circle (list, cx - r * 0.47f, cy - r * 0.28f, lr, color);
			lrg_paint_list_circle (list, cx + r * 0.47f, cy - r * 0.28f, lr, color);
			fan_triangle (list, cx - r * 0.95f, cy - r * 0.12f, cx + r * 0.95f, cy - r * 0.12f,
			              cx, cy + r * 0.88f, color);
		}
		break;

	case LRG_RATING_SHAPE_CIRCLE:
		lrg_paint_list_circle (list, cx, cy, r * 0.8f, color);
		break;

	case LRG_RATING_SHAPE_STAR:
	default:
		{
			/* Ten points alternating outer and inner radius, from
			 * straight up; a fan of ten triangles from the centre.
			 * The star is nudged down so it looks optically centred. */
			gfloat px[10];
			gfloat py[10];
			gfloat oy = cy + r * 0.06f;
			guint  k;

			for (k = 0; k < 10; k++)
			{
				gfloat a = (gfloat) G_PI * ((gfloat) k / 5.0f) - (gfloat) G_PI * 0.5f;
				gfloat rad = (k % 2 == 0) ? r : r * RATING_STAR_INNER;

				px[k] = cx + cosf (a) * rad;
				py[k] = oy + sinf (a) * rad;
			}
			for (k = 0; k < 10; k++)
				fan_triangle (list, cx, oy, px[k], py[k], px[(k + 1) % 10], py[(k + 1) % 10], color);
		}
		break;
	}
}

/* ==========================================================================
 * LrgWidget vfuncs
 * ========================================================================== */

/* shape_color: the theme role that fills set symbols of @shape */
static LrgThemeColor
shape_color (LrgRatingShape shape)
{
	switch (shape)
	{
	case LRG_RATING_SHAPE_HEART:
		return LRG_THEME_COLOR_DANGER;
	case LRG_RATING_SHAPE_CIRCLE:
		return LRG_THEME_COLOR_ACCENT;
	case LRG_RATING_SHAPE_STAR:
	default:
		return LRG_THEME_COLOR_WARNING;
	}
}

static void
lrg_rating_style_defaults (LrgWidget      *widget,
                           LrgUIPart       part,
                           LrgWidgetState  state,
                           LrgPartLook    *look)
{
	LrgRating *self = LRG_RATING (widget);
	LrgTheme  *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_MARK:
		{
			GrlColor c = *lrg_theme_get_color (theme, shape_color (self->shape));

			/* Set symbols brighten while previewing under the pointer */
			if ((state & LRG_WIDGET_STATE_HOVERED) && !self->read_only)
				lrg_paint_color_shade (&c, 0.12f, &c);
			lrg_part_look_set_foreground (look, &c);
			if (state & LRG_WIDGET_STATE_DISABLED)
				lrg_part_look_set_opacity (look, 0.45f);
		}
		break;

	case LRG_UI_PART_TRACK:
		{
			GrlColor c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);

			if ((state & LRG_WIDGET_STATE_HOVERED) && !self->read_only)
				lrg_paint_color_shade (&c, 0.06f, &c);
			lrg_part_look_set_foreground (look, &c);
			if (state & LRG_WIDGET_STATE_DISABLED)
				lrg_part_look_set_opacity (look, 0.45f);
		}
		break;

	default:
		LRG_WIDGET_CLASS (lrg_rating_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_rating_style_overrides (LrgWidget      *widget,
                            LrgUIPart       part,
                            LrgWidgetState  state,
                            LrgPartLook    *look)
{
	LrgRating *self = LRG_RATING (widget);

	/* An explicit fill colour beats the theme (still brightened on hover) */
	if (part == LRG_UI_PART_MARK && self->fill_set)
	{
		GrlColor c = self->fill_color;

		if ((state & LRG_WIDGET_STATE_HOVERED) && !self->read_only)
			lrg_paint_color_shade (&c, 0.12f, &c);
		lrg_part_look_set_foreground (look, &c);
	}
}

static void
lrg_rating_measure (LrgWidget *widget,
                    gfloat    *width,
                    gfloat    *height)
{
	LrgRating *self = LRG_RATING (widget);

	/* A little vertical room so the pop does not get clipped by
	 * neighbours' backgrounds */
	*width = row_width (self);
	*height = roundf (symbol_size (self) * 1.25f);
}

/*
 * lrg_rating_step:
 *
 * Moves the fill towards the value / preview and each symbol's scale
 * towards resting (or lifted under the pointer). The first step snaps.
 */
static gboolean
lrg_rating_step (LrgWidget *widget,
                 gfloat     delta_time)
{
	LrgRating *self = LRG_RATING (widget);
	LrgMotion  motion = lrg_widget_get_motion (widget);
	gint       hi = hover_index (self);
	gboolean   busy = FALSE;
	guint      i;

	ensure_scales (self);
	lrg_spring_set_motion (&self->fill, motion == LRG_MOTION_NONE ? LRG_MOTION_NONE : LRG_MOTION_SNAPPY);
	lrg_spring_set_target (&self->fill, fill_target (self));

	for (i = 0; i < self->scales->len; i++)
	{
		LrgSpring *s = &g_array_index (self->scales, LrgSpring, i);

		lrg_spring_set_motion (s, motion == LRG_MOTION_NONE ? LRG_MOTION_NONE : LRG_MOTION_BOUNCY);
		lrg_spring_set_target (s, (gint) i == hi ? RATING_HOVER_SCALE : 1.0f);
	}

	if (!self->stepped || motion == LRG_MOTION_NONE)
	{
		lrg_spring_snap (&self->fill, self->fill.target);
		for (i = 0; i < self->scales->len; i++)
		{
			LrgSpring *s = &g_array_index (self->scales, LrgSpring, i);

			lrg_spring_snap (s, s->target);
		}
		self->stepped = TRUE;
		return FALSE;
	}

	busy |= lrg_spring_step (&self->fill, delta_time);
	for (i = 0; i < self->scales->len; i++)
		busy |= lrg_spring_step (&g_array_index (self->scales, LrgSpring, i), delta_time);
	return busy;
}

static void
lrg_rating_paint (LrgWidget    *widget,
                  LrgPaintList *list)
{
	LrgRating   *self = LRG_RATING (widget);
	LrgPartLook  mark;
	LrgPartLook  track;
	gfloat       x = lrg_widget_get_world_x (widget);
	gfloat       y = lrg_widget_get_world_y (widget);
	gfloat       h = lrg_widget_get_height (widget);
	gfloat       size = symbol_size (self);
	gfloat       pitch = symbol_pitch (self);
	gfloat       cy = y + h * 0.5f;
	gfloat       fill = self->stepped ? self->fill.value : fill_target (self);
	gfloat       opacity;
	guint        i;

	lrg_widget_resolve_look (widget, LRG_UI_PART_MARK, &mark);
	lrg_widget_resolve_look (widget, LRG_UI_PART_TRACK, &track);

	opacity = MIN (mark.opacity, track.opacity);
	if (opacity < 1.0f)
		lrg_paint_list_push_opacity (list, opacity);

	for (i = 0; i < self->max; i++)
	{
		gfloat scale = symbol_scale (self, i);
		gfloat s = size * scale;
		gfloat left = x + (gfloat) i * pitch;
		gfloat cx = left + size * 0.5f;
		gfloat frac = CLAMP (fill - (gfloat) i, 0.0f, 1.0f);

		/* Empty symbol underneath */
		if (frac < 1.0f)
			paint_shape (list, self->shape, cx, cy, s, &track.foreground);

		/* Filled part: a clip over the left @frac of the symbol */
		if (frac >= 1.0f)
			paint_shape (list, self->shape, cx, cy, s, &mark.foreground);
		else if (frac > 0.0f)
		{
			gfloat sx = cx - s * 0.5f - 1.0f;

			lrg_paint_list_push_clip (list, sx, cy - s, (s + 2.0f) * frac, s * 2.0f);
			paint_shape (list, self->shape, cx, cy, s, &mark.foreground);
			lrg_paint_list_pop_clip (list);
		}
	}

	if (opacity < 1.0f)
		lrg_paint_list_pop_opacity (list);
}

/* set_hover: change the preview value */
static void
set_hover (LrgRating *self,
           gdouble    value)
{
	self->hover_value = value;
}

/*
 * pick: the user chose @value. The symbol that holds the new value
 * pops, then the value is set (emitting value-changed).
 */
static void
pick (LrgRating *self,
      gdouble    value)
{
	gint k;

	value = round_value (self, value);
	k = (gint) ceil (value) - 1;
	ensure_scales (self);
	if (k >= 0 && k < (gint) self->scales->len && self->stepped &&
	    lrg_widget_get_motion (LRG_WIDGET (self)) != LRG_MOTION_NONE)
	{
		LrgSpring *s = &g_array_index (self->scales, LrgSpring, k);

		s->value = RATING_POP_SCALE;
		s->velocity = 0.0f;
	}
	lrg_rating_set_value (self, value);
}

static gboolean
lrg_rating_handle_event (LrgWidget        *widget,
                         const LrgUIEvent *event)
{
	LrgRating *self = LRG_RATING (widget);
	gfloat     ex = lrg_ui_event_get_x (event);
	gfloat     ey = lrg_ui_event_get_y (event);
	gdouble    step = self->allow_half ? 0.5 : 1.0;

	if (self->read_only || !lrg_widget_get_enabled (widget))
		return FALSE;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		/* Preview follows the pointer; a held button drags the value */
		if (lrg_widget_contains_point (widget, ex, ey))
		{
			set_hover (self, value_at (self, ex));
			if (lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_PRESSED)
				lrg_rating_set_value (self, value_at (self, ex));
		}
		else
			set_hover (self, -1.0);
		return TRUE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		set_hover (self, -1.0);
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0 || !lrg_widget_contains_point (widget, ex, ey))
			return FALSE;
		set_hover (self, value_at (self, ex));
		pick (self, value_at (self, ex));
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		return lrg_ui_event_get_button (event) == 0;

	case LRG_UI_EVENT_KEY_DOWN:
		switch (lrg_ui_event_get_key (event))
		{
		case GRL_KEY_LEFT:
			pick (self, self->value - step);
			return TRUE;
		case GRL_KEY_RIGHT:
			pick (self, self->value + step);
			return TRUE;
		case GRL_KEY_HOME:
			lrg_rating_set_value (self, 0.0);
			return TRUE;
		case GRL_KEY_END:
			pick (self, (gdouble) self->max);
			return TRUE;
		default:
			return FALSE;
		}

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_rating_finalize (GObject *object)
{
	LrgRating *self = LRG_RATING (object);

	g_clear_pointer (&self->scales, g_array_unref);

	G_OBJECT_CLASS (lrg_rating_parent_class)->finalize (object);
}

static void
lrg_rating_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	LrgRating *self = LRG_RATING (object);

	switch (prop_id)
	{
	case PROP_VALUE:
		g_value_set_double (value, self->value);
		break;
	case PROP_MAX:
		g_value_set_uint (value, self->max);
		break;
	case PROP_ALLOW_HALF:
		g_value_set_boolean (value, self->allow_half);
		break;
	case PROP_READ_ONLY:
		g_value_set_boolean (value, self->read_only);
		break;
	case PROP_SHAPE:
		g_value_set_enum (value, self->shape);
		break;
	case PROP_SYMBOL_SIZE:
		g_value_set_float (value, self->symbol_size);
		break;
	case PROP_FILL_COLOR:
		g_value_set_boxed (value, self->fill_set ? &self->fill_color : NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_rating_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	LrgRating *self = LRG_RATING (object);

	switch (prop_id)
	{
	case PROP_VALUE:
		lrg_rating_set_value (self, g_value_get_double (value));
		break;
	case PROP_MAX:
		lrg_rating_set_max (self, g_value_get_uint (value));
		break;
	case PROP_ALLOW_HALF:
		lrg_rating_set_allow_half (self, g_value_get_boolean (value));
		break;
	case PROP_READ_ONLY:
		lrg_rating_set_read_only (self, g_value_get_boolean (value));
		break;
	case PROP_SHAPE:
		lrg_rating_set_shape (self, (LrgRatingShape) g_value_get_enum (value));
		break;
	case PROP_SYMBOL_SIZE:
		lrg_rating_set_symbol_size (self, g_value_get_float (value));
		break;
	case PROP_FILL_COLOR:
		lrg_rating_set_fill_color (self, g_value_get_boxed (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_rating_class_init (LrgRatingClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->finalize = lrg_rating_finalize;
	object_class->get_property = lrg_rating_get_property;
	object_class->set_property = lrg_rating_set_property;

	widget_class->style_defaults = lrg_rating_style_defaults;
	widget_class->style_overrides = lrg_rating_style_overrides;
	widget_class->measure = lrg_rating_measure;
	widget_class->paint = lrg_rating_paint;
	widget_class->step = lrg_rating_step;
	widget_class->handle_event = lrg_rating_handle_event;

	/**
	 * LrgRating:value:
	 *
	 * The rating, 0 to #LrgRating:max, in whole or half steps.
	 */
	properties[PROP_VALUE] =
		g_param_spec_double ("value", NULL, NULL, 0.0, G_MAXDOUBLE, 0.0,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgRating:max:
	 *
	 * Number of symbols.
	 */
	properties[PROP_MAX] =
		g_param_spec_uint ("max", NULL, NULL, 1, 100, 5,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgRating:allow-half:
	 *
	 * Whether half symbols can be picked.
	 */
	properties[PROP_ALLOW_HALF] =
		g_param_spec_boolean ("allow-half", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgRating:read-only:
	 *
	 * Display only: no focus, preview, clicks or keys.
	 */
	properties[PROP_READ_ONLY] =
		g_param_spec_boolean ("read-only", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgRating:shape:
	 *
	 * The symbol drawn for each step.
	 */
	properties[PROP_SHAPE] =
		g_param_spec_enum ("shape", NULL, NULL, LRG_TYPE_RATING_SHAPE, LRG_RATING_SHAPE_STAR,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgRating:symbol-size:
	 *
	 * Edge length of one symbol; 0 follows the theme.
	 */
	properties[PROP_SYMBOL_SIZE] =
		g_param_spec_float ("symbol-size", NULL, NULL, 0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgRating:fill-color:
	 *
	 * Explicit colour of set symbols, or %NULL for the theme's.
	 */
	properties[PROP_FILL_COLOR] =
		g_param_spec_boxed ("fill-color", NULL, NULL, GRL_TYPE_COLOR,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgRating::value-changed:
	 * @self: the rating
	 *
	 * Emitted when #LrgRating:value changes, by the user or by
	 * lrg_rating_set_value().
	 */
	signals[SIGNAL_VALUE_CHANGED] =
		g_signal_new ("value-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_rating_init (LrgRating *self)
{
	self->max = 5;
	self->hover_value = -1.0;
	self->shape = LRG_RATING_SHAPE_STAR;
	self->scales = g_array_new (FALSE, TRUE, sizeof (LrgSpring));
	lrg_spring_init (&self->fill, 0.0f, LRG_MOTION_SNAPPY);
	ensure_scales (self);

	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgRating *
lrg_rating_new (void)
{
	return g_object_new (LRG_TYPE_RATING, NULL);
}

gdouble
lrg_rating_get_value (LrgRating *self)
{
	g_return_val_if_fail (LRG_IS_RATING (self), 0.0);

	return self->value;
}

void
lrg_rating_set_value (LrgRating *self,
                      gdouble    value)
{
	g_return_if_fail (LRG_IS_RATING (self));

	value = round_value (self, value);
	if (value == self->value)
		return;

	self->value = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALUE]);
	g_signal_emit (self, signals[SIGNAL_VALUE_CHANGED], 0);
}

guint
lrg_rating_get_max (LrgRating *self)
{
	g_return_val_if_fail (LRG_IS_RATING (self), 0);

	return self->max;
}

void
lrg_rating_set_max (LrgRating *self,
                    guint      max)
{
	g_return_if_fail (LRG_IS_RATING (self));

	max = MAX (max, 1u);
	if (max == self->max)
		return;

	self->max = max;
	ensure_scales (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX]);
	lrg_rating_set_value (self, self->value);
}

gboolean
lrg_rating_get_allow_half (LrgRating *self)
{
	g_return_val_if_fail (LRG_IS_RATING (self), FALSE);

	return self->allow_half;
}

void
lrg_rating_set_allow_half (LrgRating *self,
                           gboolean   allow_half)
{
	g_return_if_fail (LRG_IS_RATING (self));

	allow_half = !!allow_half;
	if (allow_half == self->allow_half)
		return;

	self->allow_half = allow_half;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ALLOW_HALF]);
	lrg_rating_set_value (self, self->value);
}

gboolean
lrg_rating_get_read_only (LrgRating *self)
{
	g_return_val_if_fail (LRG_IS_RATING (self), FALSE);

	return self->read_only;
}

void
lrg_rating_set_read_only (LrgRating *self,
                          gboolean   read_only)
{
	g_return_if_fail (LRG_IS_RATING (self));

	read_only = !!read_only;
	if (read_only == self->read_only)
		return;

	self->read_only = read_only;
	lrg_widget_set_focusable (LRG_WIDGET (self), !read_only);
	if (read_only)
		set_hover (self, -1.0);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_READ_ONLY]);
}

LrgRatingShape
lrg_rating_get_shape (LrgRating *self)
{
	g_return_val_if_fail (LRG_IS_RATING (self), LRG_RATING_SHAPE_STAR);

	return self->shape;
}

void
lrg_rating_set_shape (LrgRating      *self,
                      LrgRatingShape  shape)
{
	g_return_if_fail (LRG_IS_RATING (self));

	if (shape == self->shape)
		return;

	self->shape = shape;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHAPE]);
}

gfloat
lrg_rating_get_symbol_size (LrgRating *self)
{
	g_return_val_if_fail (LRG_IS_RATING (self), 0.0f);

	return self->symbol_size;
}

void
lrg_rating_set_symbol_size (LrgRating *self,
                            gfloat     size)
{
	g_return_if_fail (LRG_IS_RATING (self));

	size = MAX (size, 0.0f);
	if (size == self->symbol_size)
		return;

	self->symbol_size = size;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SYMBOL_SIZE]);
}

const GrlColor *
lrg_rating_get_fill_color (LrgRating *self)
{
	g_return_val_if_fail (LRG_IS_RATING (self), NULL);

	return self->fill_set ? &self->fill_color : NULL;
}

void
lrg_rating_set_fill_color (LrgRating      *self,
                           const GrlColor *color)
{
	g_return_if_fail (LRG_IS_RATING (self));

	if (color == NULL && !self->fill_set)
		return;
	if (color != NULL && self->fill_set && memcmp (color, &self->fill_color, sizeof (GrlColor)) == 0)
		return;

	self->fill_set = color != NULL;
	if (color != NULL)
		self->fill_color = *color;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_COLOR]);
}

gdouble
lrg_rating_get_hover_value (LrgRating *self)
{
	g_return_val_if_fail (LRG_IS_RATING (self), -1.0);

	return self->hover_value;
}
