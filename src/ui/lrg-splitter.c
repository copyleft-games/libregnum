/* lrg-splitter.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSplitter: Two panes separated by a draggable divider.
 *
 * The start pane gets "position" units along the orientation, the
 * handle "handle-size" units, the end pane the rest. The position is
 * either absolute (lrg_splitter_set_position(): the start pane keeps
 * its size when the splitter is resized) or proportional
 * (lrg_splitter_set_fraction()). Either way it is clamped so both panes
 * keep their minimum sizes.
 *
 * The laid-out position is a spring: programmatic changes with
 * animate=TRUE glide (step() advances the spring and queues a layout
 * every frame); drags and plain changes snap.
 *
 * The handle is a private focusable child (a plain LrgWidget, painted
 * by the splitter as the THUMB part), so clicks inside the panes never
 * give the splitter keyboard focus; arrows on the focused handle move
 * it by 10 units, Home / End collapse the start / end pane.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-splitter.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* Units moved by one arrow key */
#define KEY_STEP (10.0f)

/* Two presses on the handle within this time (canvas clock) reset it */
#define DOUBLE_CLICK_TIME (0.4)

/* Length of the grip pill in the middle of the handle */
#define GRIP_LENGTH (28.0f)

struct _LrgSplitter
{
	LrgContainer    parent_instance;

	LrgOrientation  orientation;
	LrgWidget      *start;          /* not owned (container holds it) */
	LrgWidget      *end;
	LrgWidget      *handle;         /* owned */

	gboolean        use_fraction;
	gfloat          position;       /* absolute mode */
	gfloat          fraction;       /* proportional mode */
	gfloat          default_fraction;
	gfloat          min_start;
	gfloat          min_end;
	gfloat          handle_size;

	LrgSpring       shown;          /* laid-out start pane size */
	gboolean        animating;
	gboolean        laid_out;       /* shown holds a real value */

	gboolean        dragging;
	gfloat          grab;           /* pointer offset inside the handle */

	gdouble         clock;
	gboolean        click_armed;
	gdouble         last_click_time;
	gboolean        in_layout;
};

G_DEFINE_FINAL_TYPE (LrgSplitter, lrg_splitter, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_ORIENTATION,
	PROP_POSITION,
	PROP_FRACTION,
	PROP_DEFAULT_FRACTION,
	PROP_MIN_START_SIZE,
	PROP_MIN_END_SIZE,
	PROP_HANDLE_SIZE,
	N_PROPS
};

enum
{
	SIGNAL_DRAG_BEGIN,
	SIGNAL_DRAG_END,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Geometry
 * ========================================================================== */

static gboolean
horizontal (LrgSplitter *self)
{
	return self->orientation == LRG_ORIENTATION_HORIZONTAL;
}

/* avail: main-axis space for both panes (handle excluded) */
static gfloat
avail (LrgSplitter *self)
{
	LrgWidget *w = LRG_WIDGET (self);
	gfloat     len = horizontal (self) ? lrg_widget_get_width (w) : lrg_widget_get_height (w);

	return MAX (0.0f, len - self->handle_size);
}

/* clamp_position: keep both minimum sizes, the start one winning */
static gfloat
clamp_position (LrgSplitter *self,
                gfloat       p)
{
	gfloat a = avail (self);
	gfloat hi = MAX (MIN (self->min_start, a), a - self->min_end);

	return roundf (CLAMP (p, MIN (self->min_start, a), hi));
}

/* target: the clamped start pane size of the current mode */
static gfloat
target (LrgSplitter *self)
{
	gfloat raw = self->use_fraction ? self->fraction * avail (self) : self->position;

	return clamp_position (self, raw);
}

static gboolean
smooth (LrgSplitter *self)
{
	return lrg_widget_get_motion (LRG_WIDGET (self)) != LRG_MOTION_NONE;
}

/*
 * move_to:
 * @p: wanted start pane size (unclamped)
 * @animate: glide with the spring
 *
 * Stores @p in the current mode (as a fraction in proportional mode),
 * aims the spring and notifies.
 */
static void
move_to (LrgSplitter *self,
         gfloat       p,
         gboolean     animate)
{
	gfloat a = avail (self);
	gfloat old = target (self);

	p = clamp_position (self, p);
	if (self->use_fraction)
		self->fraction = a > 0.0f ? CLAMP (p / a, 0.0f, 1.0f) : self->fraction;
	else
		self->position = p;

	lrg_spring_set_target (&self->shown, target (self));
	self->animating = animate && smooth (self) && self->laid_out;
	if (!self->animating)
		lrg_spring_snap (&self->shown, self->shown.target);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	lrg_container_layout_children (LRG_CONTAINER (self));

	if (old != target (self))
	{
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POSITION]);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRACTION]);
	}
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

/*
 * lrg_splitter_layout_children:
 *
 * Start pane, handle, end pane along the orientation. Without an
 * animation running the displayed position snaps to the target (so
 * resizes and mode changes apply at once); during one the spring is
 * retargeted and keeps gliding.
 */
static void
lrg_splitter_layout_children (LrgContainer *container)
{
	LrgSplitter *self = LRG_SPLITTER (container);
	LrgWidget   *w = LRG_WIDGET (self);
	gfloat       width = lrg_widget_get_width (w);
	gfloat       height = lrg_widget_get_height (w);
	gfloat       t;
	gfloat       p;
	gfloat       a;

	if (self->in_layout || self->handle == NULL)
		return;
	self->in_layout = TRUE;

	t = target (self);
	lrg_spring_set_target (&self->shown, t);
	if (!self->animating || !self->laid_out)
	{
		lrg_spring_snap (&self->shown, t);
		self->animating = FALSE;
	}
	self->laid_out = width > 0.0f && height > 0.0f;

	a = avail (self);
	p = roundf (CLAMP (self->shown.value, 0.0f, a));

	if (horizontal (self))
	{
		if (self->start != NULL)
			lrg_widget_place_in_slot (self->start, 0.0f, 0.0f, p, height);
		lrg_widget_allocate (self->handle, p, 0.0f, self->handle_size, height);
		if (self->end != NULL)
			lrg_widget_place_in_slot (self->end, p + self->handle_size, 0.0f, MAX (0.0f, a - p), height);
	}
	else
	{
		if (self->start != NULL)
			lrg_widget_place_in_slot (self->start, 0.0f, 0.0f, width, p);
		lrg_widget_allocate (self->handle, 0.0f, p, width, self->handle_size);
		if (self->end != NULL)
			lrg_widget_place_in_slot (self->end, 0.0f, p + self->handle_size, width, MAX (0.0f, a - p));
	}

	self->in_layout = FALSE;
}

static void
lrg_splitter_measure (LrgWidget *widget,
                      gfloat    *preferred_width,
                      gfloat    *preferred_height)
{
	LrgSplitter *self = LRG_SPLITTER (widget);
	gfloat       sw = 0.0f;
	gfloat       sh = 0.0f;
	gfloat       ew = 0.0f;
	gfloat       eh = 0.0f;

	if (self->start != NULL && lrg_widget_get_visible (self->start))
		lrg_widget_get_preferred_size (self->start, &sw, &sh);
	if (self->end != NULL && lrg_widget_get_visible (self->end))
		lrg_widget_get_preferred_size (self->end, &ew, &eh);

	/* Both panes at least their minimum, side by side or stacked */
	if (horizontal (self))
	{
		*preferred_width = MAX (sw, self->min_start) + self->handle_size + MAX (ew, self->min_end);
		*preferred_height = MAX (sh, eh);
	}
	else
	{
		*preferred_width = MAX (sw, ew);
		*preferred_height = MAX (sh, self->min_start) + self->handle_size + MAX (eh, self->min_end);
	}
}

/* ==========================================================================
 * Looks and painting
 * ========================================================================== */

static void
lrg_splitter_style_defaults (LrgWidget      *widget,
                             LrgUIPart       part,
                             LrgWidgetState  state,
                             LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	GrlColor  c;

	if (part == LRG_UI_PART_THUMB)
	{
		/* The divider: a hairline in BORDER at rest, a thicker
		 * translucent accent when hovered, solid ACCENT while dragged */
		if (state & LRG_WIDGET_STATE_PRESSED)
		{
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
			lrg_part_look_set_size (look, 4.0f);
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.35f, &c);
			lrg_part_look_set_glow (look, &c, 6.0f);
		}
		else if (state & LRG_WIDGET_STATE_HOVERED)
		{
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.7f, &c);
			lrg_part_look_set_background (look, &c);
			lrg_part_look_set_size (look, 4.0f);
		}
		else
		{
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
			lrg_part_look_set_size (look, MAX (1.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH)));
		}
		/* Grip pill colour */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                              (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED))
		                              ? LRG_THEME_COLOR_ACCENT : LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_radius (look, G_MAXFLOAT);
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;
	}

	LRG_WIDGET_CLASS (lrg_splitter_parent_class)->style_defaults (widget, part, state, look);
}

/*
 * paint_handle:
 *
 * The THUMB look blended by the handle widget's hover spring and
 * pressed fully while dragging: a line along the handle, thicker as
 * it highlights, plus a grip pill in the middle.
 */
static void
paint_handle (LrgSplitter  *self,
              LrgPaintList *list)
{
	LrgWidget   *widget = LRG_WIDGET (self);
	LrgPartLook  look;
	LrgPartLook  variant;
	gfloat       hover = CLAMP (lrg_widget_get_hover_progress (self->handle), 0.0f, 1.0f);
	gfloat       press = self->dragging ? 1.0f : CLAMP (lrg_widget_get_press_progress (self->handle), 0.0f, 1.0f);
	gfloat       hx = lrg_widget_get_world_x (self->handle);
	gfloat       hy = lrg_widget_get_world_y (self->handle);
	gfloat       hw = lrg_widget_get_width (self->handle);
	gfloat       hh = lrg_widget_get_height (self->handle);
	gfloat       t;
	gfloat       gl;

	hover = MAX (hover, press);
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_THUMB, LRG_WIDGET_STATE_NONE, &look);
	if (hover > 0.001f)
	{
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_THUMB, LRG_WIDGET_STATE_HOVERED, &variant);
		lrg_part_look_lerp (&look, &variant, hover, &look);
	}
	if (press > 0.001f)
	{
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_THUMB,
		                                   LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED, &variant);
		lrg_part_look_lerp (&look, &variant, press, &look);
	}

	t = MAX (1.0f, look.size > 0.0f ? look.size : 1.0f);
	gl = MIN (GRIP_LENGTH, horizontal (self) ? hh : hw);
	if (horizontal (self))
	{
		gfloat cx = hx + hw * 0.5f;

		lrg_widget_paint_box (widget, list, &look, roundf (cx - t * 0.5f), hy, t, hh);
		lrg_paint_list_rect (list, roundf (cx - (t + 2.0f) * 0.5f), hy + (hh - gl) * 0.5f, t + 2.0f, gl,
		                     G_MAXFLOAT, &look.foreground);
	}
	else
	{
		gfloat cy = hy + hh * 0.5f;

		lrg_widget_paint_box (widget, list, &look, hx, roundf (cy - t * 0.5f), hw, t);
		lrg_paint_list_rect (list, hx + (hw - gl) * 0.5f, roundf (cy - (t + 2.0f) * 0.5f), gl, t + 2.0f,
		                     G_MAXFLOAT, &look.foreground);
	}
}

static void
lrg_splitter_paint (LrgWidget    *widget,
                    LrgPaintList *list)
{
	LrgSplitter *self = LRG_SPLITTER (widget);
	LrgWidget   *panes[2] = { self->start, self->end };
	guint        i;

	/* Panes, each clipped to its slot so a pane bigger than its share
	 * never draws under the other */
	for (i = 0; i < 2; i++)
	{
		LrgWidget *p = panes[i];
		gfloat     x;
		gfloat     y;
		gfloat     w;
		gfloat     h;
		gfloat     a = avail (self);
		gfloat     pos = roundf (CLAMP (self->shown.value, 0.0f, a));

		if (p == NULL || !lrg_widget_get_visible (p))
			continue;
		x = lrg_widget_get_world_x (widget);
		y = lrg_widget_get_world_y (widget);
		w = lrg_widget_get_width (widget);
		h = lrg_widget_get_height (widget);
		if (horizontal (self))
		{
			if (i == 0)
				w = pos;
			else
			{
				x += pos + self->handle_size;
				w = MAX (0.0f, a - pos);
			}
		}
		else
		{
			if (i == 0)
				h = pos;
			else
			{
				y += pos + self->handle_size;
				h = MAX (0.0f, a - pos);
			}
		}
		lrg_paint_list_push_clip (list, x, y, w, h);
		lrg_widget_paint (p, list);
		lrg_paint_list_pop_clip (list);
	}

	paint_handle (self, list);

	/* The handle widget itself paints nothing but the core focus ring */
	lrg_widget_paint (self->handle, list);
}

/*
 * lrg_splitter_step:
 *
 * Advances the canvas clock and the position spring while an animated
 * change runs, queueing a layout each frame so the panes follow.
 *
 * Returns: %TRUE while the divider glides
 */
static gboolean
lrg_splitter_step (LrgWidget *widget,
                   gfloat     delta_time)
{
	LrgSplitter *self = LRG_SPLITTER (widget);
	gboolean     moving;

	if (delta_time > 0.0f)
		self->clock += (gdouble) delta_time;
	if (!self->animating)
		return FALSE;

	lrg_spring_set_motion (&self->shown, lrg_widget_get_motion (widget));
	moving = lrg_spring_step (&self->shown, delta_time);
	if (!moving)
	{
		lrg_spring_snap (&self->shown, self->shown.target);
		self->animating = FALSE;
	}
	lrg_widget_queue_layout (widget);
	return moving;
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/* main_coord: the event coordinate along the orientation */
static gfloat
main_coord (LrgSplitter      *self,
            const LrgUIEvent *event)
{
	return horizontal (self) ? lrg_ui_event_get_x (event) : lrg_ui_event_get_y (event);
}

/* origin: the splitter's world coordinate along the orientation */
static gfloat
origin (LrgSplitter *self)
{
	return horizontal (self) ? lrg_widget_get_world_x (LRG_WIDGET (self))
	                         : lrg_widget_get_world_y (LRG_WIDGET (self));
}

static gboolean
handle_key (LrgSplitter *self,
            GrlKey       key)
{
	gboolean h = horizontal (self);
	gfloat   cur = target (self);

	if ((h && key == GRL_KEY_LEFT) || (!h && key == GRL_KEY_UP))
		move_to (self, cur - KEY_STEP, TRUE);
	else if ((h && key == GRL_KEY_RIGHT) || (!h && key == GRL_KEY_DOWN))
		move_to (self, cur + KEY_STEP, TRUE);
	else if (key == GRL_KEY_HOME)
		move_to (self, 0.0f, TRUE);
	else if (key == GRL_KEY_END)
		move_to (self, G_MAXFLOAT, TRUE);
	else
		return FALSE;
	return TRUE;
}

static gboolean
lrg_splitter_handle_event (LrgWidget        *widget,
                           const LrgUIEvent *event)
{
	LrgSplitter *self = LRG_SPLITTER (widget);
	LrgWidget   *target_w = lrg_ui_event_get_target (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (target_w != self->handle || lrg_ui_event_get_button (event) != 0)
			return FALSE;

		/* Double click resets; otherwise start a drag */
		if (self->click_armed && self->clock - self->last_click_time <= DOUBLE_CLICK_TIME)
		{
			self->click_armed = FALSE;
			lrg_splitter_reset (self, TRUE);
			return TRUE;
		}
		self->click_armed = TRUE;
		self->last_click_time = self->clock;

		self->dragging = TRUE;
		self->animating = FALSE;
		lrg_spring_snap (&self->shown, target (self));
		self->grab = main_coord (self, event) - (origin (self) + self->shown.value);
		g_signal_emit (self, signals[SIGNAL_DRAG_BEGIN], 0);
		return TRUE;

	case LRG_UI_EVENT_MOUSE_MOVE:
		if (!self->dragging)
			return FALSE;
		move_to (self, main_coord (self, event) - origin (self) - self->grab, FALSE);
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (!self->dragging || lrg_ui_event_get_button (event) != 0)
			return FALSE;
		self->dragging = FALSE;
		g_signal_emit (self, signals[SIGNAL_DRAG_END], 0);
		return TRUE;

	case LRG_UI_EVENT_KEY_DOWN:
		if (target_w != self->handle)
			return FALSE;
		return handle_key (self, lrg_ui_event_get_key (event));

	case LRG_UI_EVENT_ACTIVATE:
		/* Enter / gamepad A on the handle: back to the default split */
		if (target_w != self->handle)
			return FALSE;
		lrg_splitter_reset (self, TRUE);
		return TRUE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_splitter_constructed (GObject *object)
{
	LrgSplitter *self = LRG_SPLITTER (object);

	G_OBJECT_CLASS (lrg_splitter_parent_class)->constructed (object);

	self->in_layout = TRUE;
	self->handle = g_object_new (LRG_TYPE_WIDGET, NULL);
	lrg_widget_set_name (self->handle, "handle");
	lrg_widget_set_focusable (self->handle, TRUE);
	lrg_container_add_child (LRG_CONTAINER (self), self->handle);
	self->in_layout = FALSE;
}

static void
lrg_splitter_dispose (GObject *object)
{
	LrgSplitter *self = LRG_SPLITTER (object);

	self->start = NULL;
	self->end = NULL;
	g_clear_object (&self->handle);

	G_OBJECT_CLASS (lrg_splitter_parent_class)->dispose (object);
}

static void
lrg_splitter_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgSplitter *self = LRG_SPLITTER (object);

	switch (prop_id)
	{
	case PROP_ORIENTATION:
		g_value_set_enum (value, self->orientation);
		break;
	case PROP_POSITION:
		g_value_set_float (value, lrg_splitter_get_position (self));
		break;
	case PROP_FRACTION:
		g_value_set_float (value, lrg_splitter_get_fraction (self));
		break;
	case PROP_DEFAULT_FRACTION:
		g_value_set_float (value, self->default_fraction);
		break;
	case PROP_MIN_START_SIZE:
		g_value_set_float (value, self->min_start);
		break;
	case PROP_MIN_END_SIZE:
		g_value_set_float (value, self->min_end);
		break;
	case PROP_HANDLE_SIZE:
		g_value_set_float (value, self->handle_size);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_splitter_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgSplitter *self = LRG_SPLITTER (object);

	switch (prop_id)
	{
	case PROP_ORIENTATION:
		lrg_splitter_set_orientation (self, (LrgOrientation) g_value_get_enum (value));
		break;
	case PROP_POSITION:
		lrg_splitter_set_position (self, g_value_get_float (value), FALSE);
		break;
	case PROP_FRACTION:
		lrg_splitter_set_fraction (self, g_value_get_float (value), FALSE);
		break;
	case PROP_DEFAULT_FRACTION:
		lrg_splitter_set_default_fraction (self, g_value_get_float (value));
		break;
	case PROP_MIN_START_SIZE:
		lrg_splitter_set_min_sizes (self, g_value_get_float (value), self->min_end);
		break;
	case PROP_MIN_END_SIZE:
		lrg_splitter_set_min_sizes (self, self->min_start, g_value_get_float (value));
		break;
	case PROP_HANDLE_SIZE:
		lrg_splitter_set_handle_size (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_splitter_class_init (LrgSplitterClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->constructed = lrg_splitter_constructed;
	object_class->dispose = lrg_splitter_dispose;
	object_class->get_property = lrg_splitter_get_property;
	object_class->set_property = lrg_splitter_set_property;

	widget_class->measure = lrg_splitter_measure;
	widget_class->paint = lrg_splitter_paint;
	widget_class->step = lrg_splitter_step;
	widget_class->style_defaults = lrg_splitter_style_defaults;
	widget_class->handle_event = lrg_splitter_handle_event;

	container_class->layout_children = lrg_splitter_layout_children;

	/**
	 * LrgSplitter:orientation:
	 *
	 * Horizontal: panes side by side with a vertical handle; vertical:
	 * panes stacked.
	 */
	properties[PROP_ORIENTATION] =
		g_param_spec_enum ("orientation", "Orientation", "Direction of the panes",
		                   LRG_TYPE_ORIENTATION, LRG_ORIENTATION_HORIZONTAL,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSplitter:position:
	 *
	 * Size of the start pane (clamped target). Setting it switches to
	 * absolute mode.
	 */
	properties[PROP_POSITION] =
		g_param_spec_float ("position", "Position", "Size of the start pane",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSplitter:fraction:
	 *
	 * Share of the space for the start pane. Setting it switches to
	 * proportional mode.
	 */
	properties[PROP_FRACTION] =
		g_param_spec_float ("fraction", "Fraction", "Share of the start pane",
		                    0.0f, 1.0f, 0.5f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSplitter:default-fraction:
	 *
	 * Fraction restored by a double click on the handle, Enter on the
	 * focused handle, or lrg_splitter_reset().
	 */
	properties[PROP_DEFAULT_FRACTION] =
		g_param_spec_float ("default-fraction", "Default fraction", "Fraction restored on reset",
		                    0.0f, 1.0f, 0.5f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSplitter:min-start-size:
	 *
	 * Smallest size of the start pane.
	 */
	properties[PROP_MIN_START_SIZE] =
		g_param_spec_float ("min-start-size", "Min start size", "Smallest start pane",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSplitter:min-end-size:
	 *
	 * Smallest size of the end pane.
	 */
	properties[PROP_MIN_END_SIZE] =
		g_param_spec_float ("min-end-size", "Min end size", "Smallest end pane",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSplitter:handle-size:
	 *
	 * Thickness of the handle between the panes (its grab zone).
	 */
	properties[PROP_HANDLE_SIZE] =
		g_param_spec_float ("handle-size", "Handle size", "Thickness of the handle",
		                    1.0f, G_MAXFLOAT, 8.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgSplitter::drag-begin:
	 * @self: the splitter
	 *
	 * Emitted when the handle is grabbed with the pointer.
	 */
	signals[SIGNAL_DRAG_BEGIN] =
		g_signal_new ("drag-begin",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);

	/**
	 * LrgSplitter::drag-end:
	 * @self: the splitter
	 *
	 * Emitted when the handle is released after a drag.
	 */
	signals[SIGNAL_DRAG_END] =
		g_signal_new ("drag-end",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);
}

static void
lrg_splitter_init (LrgSplitter *self)
{
	self->orientation = LRG_ORIENTATION_HORIZONTAL;
	self->use_fraction = TRUE;
	self->fraction = 0.5f;
	self->default_fraction = 0.5f;
	self->handle_size = 8.0f;
	lrg_spring_init (&self->shown, 0.0f, LRG_MOTION_SMOOTH);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgSplitter *
lrg_splitter_new (LrgOrientation orientation)
{
	return g_object_new (LRG_TYPE_SPLITTER, "orientation", orientation, NULL);
}

LrgOrientation
lrg_splitter_get_orientation (LrgSplitter *self)
{
	g_return_val_if_fail (LRG_IS_SPLITTER (self), LRG_ORIENTATION_HORIZONTAL);
	return self->orientation;
}

void
lrg_splitter_set_orientation (LrgSplitter    *self,
                              LrgOrientation  orientation)
{
	g_return_if_fail (LRG_IS_SPLITTER (self));

	if (self->orientation == orientation)
		return;
	self->orientation = orientation;
	self->animating = FALSE;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORIENTATION]);
}

/* set_pane: swap one pane, keeping the handle the last child */
static void
set_pane (LrgSplitter  *self,
          LrgWidget   **slot,
          LrgWidget    *child)
{
	LrgContainer *c = LRG_CONTAINER (self);

	if (*slot == child)
		return;

	self->in_layout = TRUE;
	if (*slot != NULL)
		lrg_container_remove_child (c, *slot);
	*slot = child;
	lrg_container_remove_child (c, self->handle);
	if (child != NULL)
		lrg_container_add_child (c, child);
	lrg_container_add_child (c, self->handle);
	self->in_layout = FALSE;

	lrg_widget_queue_layout (LRG_WIDGET (self));
}

void
lrg_splitter_set_start_child (LrgSplitter *self,
                              LrgWidget   *child)
{
	g_return_if_fail (LRG_IS_SPLITTER (self));
	g_return_if_fail (child == NULL || LRG_IS_WIDGET (child));
	set_pane (self, &self->start, child);
}

LrgWidget *
lrg_splitter_get_start_child (LrgSplitter *self)
{
	g_return_val_if_fail (LRG_IS_SPLITTER (self), NULL);
	return self->start;
}

void
lrg_splitter_set_end_child (LrgSplitter *self,
                            LrgWidget   *child)
{
	g_return_if_fail (LRG_IS_SPLITTER (self));
	g_return_if_fail (child == NULL || LRG_IS_WIDGET (child));
	set_pane (self, &self->end, child);
}

LrgWidget *
lrg_splitter_get_end_child (LrgSplitter *self)
{
	g_return_val_if_fail (LRG_IS_SPLITTER (self), NULL);
	return self->end;
}

gfloat
lrg_splitter_get_position (LrgSplitter *self)
{
	g_return_val_if_fail (LRG_IS_SPLITTER (self), 0.0f);
	return target (self);
}

void
lrg_splitter_set_position (LrgSplitter *self,
                           gfloat       position,
                           gboolean     animate)
{
	g_return_if_fail (LRG_IS_SPLITTER (self));

	self->use_fraction = FALSE;
	self->position = MAX (0.0f, position);
	move_to (self, self->position, animate);
}

gfloat
lrg_splitter_get_displayed_position (LrgSplitter *self)
{
	g_return_val_if_fail (LRG_IS_SPLITTER (self), 0.0f);
	return self->shown.value;
}

gfloat
lrg_splitter_get_fraction (LrgSplitter *self)
{
	gfloat a;

	g_return_val_if_fail (LRG_IS_SPLITTER (self), 0.0f);

	if (self->use_fraction)
		return self->fraction;
	a = avail (self);
	return a > 0.0f ? CLAMP (target (self) / a, 0.0f, 1.0f) : 0.0f;
}

void
lrg_splitter_set_fraction (LrgSplitter *self,
                           gfloat       fraction,
                           gboolean     animate)
{
	g_return_if_fail (LRG_IS_SPLITTER (self));

	self->use_fraction = TRUE;
	self->fraction = CLAMP (fraction, 0.0f, 1.0f);
	move_to (self, self->fraction * avail (self), animate);
}

gfloat
lrg_splitter_get_default_fraction (LrgSplitter *self)
{
	g_return_val_if_fail (LRG_IS_SPLITTER (self), 0.5f);
	return self->default_fraction;
}

void
lrg_splitter_set_default_fraction (LrgSplitter *self,
                                   gfloat       fraction)
{
	g_return_if_fail (LRG_IS_SPLITTER (self));

	fraction = CLAMP (fraction, 0.0f, 1.0f);
	if (self->default_fraction == fraction)
		return;
	self->default_fraction = fraction;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEFAULT_FRACTION]);
}

void
lrg_splitter_reset (LrgSplitter *self,
                    gboolean     animate)
{
	g_return_if_fail (LRG_IS_SPLITTER (self));
	lrg_splitter_set_fraction (self, self->default_fraction, animate);
}

void
lrg_splitter_set_min_sizes (LrgSplitter *self,
                            gfloat       start,
                            gfloat       end)
{
	gboolean sc;
	gboolean ec;

	g_return_if_fail (LRG_IS_SPLITTER (self));

	start = MAX (0.0f, start);
	end = MAX (0.0f, end);
	sc = self->min_start != start;
	ec = self->min_end != end;
	if (!sc && !ec)
		return;
	self->min_start = start;
	self->min_end = end;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	if (sc)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_START_SIZE]);
	if (ec)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_END_SIZE]);
}

void
lrg_splitter_get_min_sizes (LrgSplitter *self,
                            gfloat      *start,
                            gfloat      *end)
{
	g_return_if_fail (LRG_IS_SPLITTER (self));

	if (start != NULL)
		*start = self->min_start;
	if (end != NULL)
		*end = self->min_end;
}

gfloat
lrg_splitter_get_handle_size (LrgSplitter *self)
{
	g_return_val_if_fail (LRG_IS_SPLITTER (self), 0.0f);
	return self->handle_size;
}

void
lrg_splitter_set_handle_size (LrgSplitter *self,
                              gfloat       size)
{
	g_return_if_fail (LRG_IS_SPLITTER (self));

	size = MAX (1.0f, roundf (size));
	if (self->handle_size == size)
		return;
	self->handle_size = size;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HANDLE_SIZE]);
}

LrgWidget *
lrg_splitter_get_handle (LrgSplitter *self)
{
	g_return_val_if_fail (LRG_IS_SPLITTER (self), NULL);
	return self->handle;
}

gboolean
lrg_splitter_get_dragging (LrgSplitter *self)
{
	g_return_val_if_fail (LRG_IS_SPLITTER (self), FALSE);
	return self->dragging;
}
