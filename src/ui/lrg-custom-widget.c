/* lrg-custom-widget.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCustomWidget: a widget whose measure, paint, step and event
 * handling are supplied by the application.
 *
 * Two ways to plug in, usable together:
 *
 *  - a C function table (lrg_custom_widget_set_funcs()), called
 *    directly with no signal overhead, for hot paths;
 *  - signals ("measure", "paint", "step", "event"), for bindings and
 *    loosely coupled code.
 *
 * For each hook the table function runs first, then the signal. The
 * measure size is a boxed in/out struct prefilled with the
 * preferred-width/-height properties, so a callback that only knows
 * one dimension leaves the other alone. step results are OR-ed (every
 * handler runs); an event consumed by the table function or a handler
 * stops there.
 *
 * Like every widget, paint() must be a pure function of state: keep
 * time-based state in step, which follows the canvas clock.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <string.h>

#include "lrg-custom-widget.h"
#include "../lrg-log.h"

struct _LrgCustomWidget
{
	LrgWidget             parent_instance;

	gfloat                preferred_width;
	gfloat                preferred_height;
	gboolean              paint_box;

	gboolean              has_funcs;
	LrgCustomWidgetFuncs  funcs;
	gpointer              funcs_data;
	GDestroyNotify        funcs_destroy;
};

G_DEFINE_FINAL_TYPE (LrgCustomWidget, lrg_custom_widget, LRG_TYPE_WIDGET)

G_DEFINE_BOXED_TYPE (LrgCustomWidgetSize, lrg_custom_widget_size,
                     lrg_custom_widget_size_copy, lrg_custom_widget_size_free)

enum
{
	PROP_0,
	PROP_PREFERRED_WIDTH,
	PROP_PREFERRED_HEIGHT,
	PROP_PAINT_BOX,
	N_PROPS
};

enum
{
	SIGNAL_MEASURE,
	SIGNAL_PAINT,
	SIGNAL_STEP,
	SIGNAL_EVENT,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * LrgCustomWidgetSize
 * ========================================================================== */

LrgCustomWidgetSize *
lrg_custom_widget_size_copy (const LrgCustomWidgetSize *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return g_memdup2 (self, sizeof (LrgCustomWidgetSize));
}

void
lrg_custom_widget_size_free (LrgCustomWidgetSize *self)
{
	g_free (self);
}

/* ==========================================================================
 * Signal accumulator
 * ========================================================================== */

/*
 * accumulate_or:
 *
 * "step": TRUE if any handler is still animating. Unlike
 * g_signal_accumulator_true_handled() every handler runs, so two
 * independent animations both advance.
 */
static gboolean
accumulate_or (GSignalInvocationHint *ihint,
               GValue                *return_accu,
               const GValue          *handler_return,
               gpointer               data)
{
	gboolean busy = g_value_get_boolean (return_accu) || g_value_get_boolean (handler_return);

	g_value_set_boolean (return_accu, busy);
	return TRUE;
}

/* ==========================================================================
 * LrgWidget
 * ========================================================================== */

static void
lrg_custom_widget_measure (LrgWidget *widget,
                           gfloat    *preferred_width,
                           gfloat    *preferred_height)
{
	LrgCustomWidget     *self = LRG_CUSTOM_WIDGET (widget);
	LrgCustomWidgetSize  size;
	gfloat               avail_w = lrg_widget_get_width (widget);
	gfloat               avail_h = lrg_widget_get_height (widget);

	/* Base size from the properties, then the table, then handlers */
	size.width = self->preferred_width;
	size.height = self->preferred_height;
	if (self->has_funcs && self->funcs.measure != NULL)
		self->funcs.measure (self, avail_w, avail_h, &size, self->funcs_data);
	g_signal_emit (self, signals[SIGNAL_MEASURE], 0, avail_w, avail_h, &size);

	*preferred_width = MAX (0.0f, size.width);
	*preferred_height = MAX (0.0f, size.height);
}

static void
lrg_custom_widget_style_defaults (LrgWidget      *widget,
                                  LrgUIPart       part,
                                  LrgWidgetState  state,
                                  LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	if (part != LRG_UI_PART_BOX)
	{
		LRG_WIDGET_CLASS (lrg_custom_widget_parent_class)->style_defaults (widget, part, state, look);
		return;
	}

	/* An optional card-like surface; interactive custom widgets get
	 * the CONTROL hover / press tints for free */
	if (state & LRG_WIDGET_STATE_PRESSED)
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_PRESSED));
	else if (state & LRG_WIDGET_STATE_HOVERED)
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER));
	else
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
	                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
	lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
	if (state & LRG_WIDGET_STATE_DISABLED)
		lrg_part_look_set_opacity (look, 0.45f);
}

static void
lrg_custom_widget_paint (LrgWidget    *widget,
                         LrgPaintList *list)
{
	LrgCustomWidget *self = LRG_CUSTOM_WIDGET (widget);
	gfloat           x = lrg_widget_get_world_x (widget);
	gfloat           y = lrg_widget_get_world_y (widget);
	gfloat           w = lrg_widget_get_width (widget);
	gfloat           h = lrg_widget_get_height (widget);

	/* 1. Optional themed surface */
	if (self->paint_box)
	{
		LrgPartLook look;

		lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &look);
		lrg_widget_paint_box (widget, list, &look, x, y, w, h);
	}

	/* 2. Application paint: table, then handlers */
	if (self->has_funcs && self->funcs.paint != NULL)
		self->funcs.paint (self, list, x, y, w, h, self->funcs_data);
	g_signal_emit (self, signals[SIGNAL_PAINT], 0, list, x, y, w, h);
}

static gboolean
lrg_custom_widget_step (LrgWidget *widget,
                        gfloat     delta_time)
{
	LrgCustomWidget *self = LRG_CUSTOM_WIDGET (widget);
	gboolean         busy = FALSE;
	gboolean         signalled = FALSE;

	if (self->has_funcs && self->funcs.step != NULL)
		busy = self->funcs.step (self, delta_time, self->funcs_data);
	g_signal_emit (self, signals[SIGNAL_STEP], 0, delta_time, &signalled);
	return busy || signalled;
}

static gboolean
lrg_custom_widget_handle_event (LrgWidget        *widget,
                                const LrgUIEvent *event)
{
	LrgCustomWidget *self = LRG_CUSTOM_WIDGET (widget);
	gboolean         handled = FALSE;

	if (self->has_funcs && self->funcs.event != NULL &&
	    self->funcs.event (self, event, self->funcs_data))
		return TRUE;

	g_signal_emit (self, signals[SIGNAL_EVENT], 0, event, &handled);
	return handled;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

/* clear_funcs: drop the table, freeing its data last */
static void
clear_funcs (LrgCustomWidget *self)
{
	GDestroyNotify destroy = self->funcs_destroy;
	gpointer       data = self->funcs_data;

	self->has_funcs = FALSE;
	memset (&self->funcs, 0, sizeof (self->funcs));
	self->funcs_data = NULL;
	self->funcs_destroy = NULL;
	if (destroy != NULL)
		destroy (data);
}

static void
lrg_custom_widget_dispose (GObject *object)
{
	clear_funcs (LRG_CUSTOM_WIDGET (object));

	G_OBJECT_CLASS (lrg_custom_widget_parent_class)->dispose (object);
}

static void
lrg_custom_widget_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgCustomWidget *self = LRG_CUSTOM_WIDGET (object);

	switch (prop_id)
	{
	case PROP_PREFERRED_WIDTH:
		g_value_set_float (value, self->preferred_width);
		break;
	case PROP_PREFERRED_HEIGHT:
		g_value_set_float (value, self->preferred_height);
		break;
	case PROP_PAINT_BOX:
		g_value_set_boolean (value, self->paint_box);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_custom_widget_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgCustomWidget *self = LRG_CUSTOM_WIDGET (object);

	switch (prop_id)
	{
	case PROP_PREFERRED_WIDTH:
		lrg_custom_widget_set_preferred_width (self, g_value_get_float (value));
		break;
	case PROP_PREFERRED_HEIGHT:
		lrg_custom_widget_set_preferred_height (self, g_value_get_float (value));
		break;
	case PROP_PAINT_BOX:
		lrg_custom_widget_set_paint_box (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_custom_widget_class_init (LrgCustomWidgetClass *klass)
{
	GObjectClass     *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass   *widget_class = LRG_WIDGET_CLASS (klass);
	const GParamFlags rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_custom_widget_dispose;
	object_class->get_property = lrg_custom_widget_get_property;
	object_class->set_property = lrg_custom_widget_set_property;

	widget_class->measure = lrg_custom_widget_measure;
	widget_class->paint = lrg_custom_widget_paint;
	widget_class->step = lrg_custom_widget_step;
	widget_class->style_defaults = lrg_custom_widget_style_defaults;
	widget_class->handle_event = lrg_custom_widget_handle_event;

	/**
	 * LrgCustomWidget:preferred-width:
	 *
	 * Width the widget asks for unless a measure callback changes it.
	 */
	properties[PROP_PREFERRED_WIDTH] =
		g_param_spec_float ("preferred-width", "Preferred width", "Base preferred width",
		                    0.0f, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgCustomWidget:preferred-height:
	 *
	 * Height the widget asks for unless a measure callback changes it.
	 */
	properties[PROP_PREFERRED_HEIGHT] =
		g_param_spec_float ("preferred-height", "Preferred height", "Base preferred height",
		                    0.0f, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgCustomWidget:paint-box:
	 *
	 * Paint the themed BOX surface before the paint callbacks.
	 */
	properties[PROP_PAINT_BOX] =
		g_param_spec_boolean ("paint-box", "Paint box", "Paint the themed BOX surface",
		                      FALSE, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgCustomWidget::measure:
	 * @self: the widget
	 * @avail_width: current allocated width (0 before the first layout),
	 *   a hint for height-for-width content
	 * @avail_height: current allocated height
	 * @size: (inout): the preferred size, prefilled; overwrite it
	 *
	 * Asked whenever the parent measures the widget. Call
	 * lrg_widget_queue_layout() when what you measure changes.
	 */
	signals[SIGNAL_MEASURE] =
		g_signal_new ("measure", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 3, G_TYPE_FLOAT, G_TYPE_FLOAT,
		              LRG_TYPE_CUSTOM_WIDGET_SIZE | G_SIGNAL_TYPE_STATIC_SCOPE);

	/**
	 * LrgCustomWidget::paint:
	 * @self: the widget
	 * @list: the paint list to record into
	 * @x: world X of the widget
	 * @y: world Y of the widget
	 * @width: widget width
	 * @height: widget height
	 *
	 * Record the widget's appearance. Use lrg_widget_resolve_look() and
	 * lrg_widget_paint_box() for themed surfaces and
	 * lrg_widget_get_hover_progress() for state-driven motion.
	 */
	signals[SIGNAL_PAINT] =
		g_signal_new ("paint", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 5, LRG_TYPE_PAINT_LIST, G_TYPE_FLOAT, G_TYPE_FLOAT,
		              G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgCustomWidget::step:
	 * @self: the widget
	 * @delta_time: seconds since the previous frame (canvas clock)
	 *
	 * Advance your own animation. Results of all handlers are OR-ed.
	 *
	 * Returns: %TRUE while still animating
	 */
	signals[SIGNAL_STEP] =
		g_signal_new ("step", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, accumulate_or, NULL, NULL,
		              G_TYPE_BOOLEAN, 1, G_TYPE_FLOAT);

	/**
	 * LrgCustomWidget::event:
	 * @self: the widget
	 * @event: the UI event (positions in canvas units)
	 *
	 * Handle input. Events bubble to the parent unless consumed.
	 *
	 * Returns: %TRUE to consume @event
	 */
	signals[SIGNAL_EVENT] =
		g_signal_new ("event", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, g_signal_accumulator_true_handled, NULL, NULL,
		              G_TYPE_BOOLEAN, 1, LRG_TYPE_UI_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
}

static void
lrg_custom_widget_init (LrgCustomWidget *self)
{
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgCustomWidget *
lrg_custom_widget_new (void)
{
	return g_object_new (LRG_TYPE_CUSTOM_WIDGET, NULL);
}

void
lrg_custom_widget_set_funcs (LrgCustomWidget            *self,
                             const LrgCustomWidgetFuncs *funcs,
                             gpointer                    user_data,
                             GDestroyNotify              destroy)
{
	g_return_if_fail (LRG_IS_CUSTOM_WIDGET (self));

	clear_funcs (self);
	if (funcs != NULL)
	{
		self->funcs = *funcs;
		self->funcs_data = user_data;
		self->funcs_destroy = destroy;
		self->has_funcs = TRUE;
	}
	else if (destroy != NULL)
		destroy (user_data);

	lrg_widget_queue_layout (LRG_WIDGET (self));
}

gfloat
lrg_custom_widget_get_preferred_width (LrgCustomWidget *self)
{
	g_return_val_if_fail (LRG_IS_CUSTOM_WIDGET (self), 0.0f);

	return self->preferred_width;
}

void
lrg_custom_widget_set_preferred_width (LrgCustomWidget *self,
                                       gfloat           width)
{
	g_return_if_fail (LRG_IS_CUSTOM_WIDGET (self));

	width = MAX (0.0f, width);
	if (self->preferred_width == width)
		return;
	self->preferred_width = width;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PREFERRED_WIDTH]);
}

gfloat
lrg_custom_widget_get_preferred_height (LrgCustomWidget *self)
{
	g_return_val_if_fail (LRG_IS_CUSTOM_WIDGET (self), 0.0f);

	return self->preferred_height;
}

void
lrg_custom_widget_set_preferred_height (LrgCustomWidget *self,
                                        gfloat           height)
{
	g_return_if_fail (LRG_IS_CUSTOM_WIDGET (self));

	height = MAX (0.0f, height);
	if (self->preferred_height == height)
		return;
	self->preferred_height = height;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PREFERRED_HEIGHT]);
}

gboolean
lrg_custom_widget_get_paint_box (LrgCustomWidget *self)
{
	g_return_val_if_fail (LRG_IS_CUSTOM_WIDGET (self), FALSE);

	return self->paint_box;
}

void
lrg_custom_widget_set_paint_box (LrgCustomWidget *self,
                                 gboolean         paint_box)
{
	g_return_if_fail (LRG_IS_CUSTOM_WIDGET (self));

	paint_box = !!paint_box;
	if (self->paint_box == paint_box)
		return;
	self->paint_box = paint_box;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAINT_BOX]);
}
