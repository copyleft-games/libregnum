/* lrg-island.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgIsland: a pill-shaped panel that morphs between a compact and an
 * expanded size.
 *
 * Three springs drive it: width and height chase the preferred size of
 * the child for the current state, and a morph value runs 0 -> 1. The
 * compact child fades out over the first half of the morph and the
 * expanded child fades in over the second half, so they never overlap
 * and only one of them is hit-testable at a time. The expanded child is
 * laid out at its full size from the start and clipped by the growing
 * pill, so its text does not reflow while the island opens.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-island.h"
#include "lrg-widget-private.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* Padding between the pill edge and its content */
#define ISLAND_PADDING (10.0f)

/* Smallest compact height, so an empty island is still a pill */
#define ISLAND_MIN_HEIGHT (36.0f)

struct _LrgIsland
{
	LrgContainer  parent_instance;

	LrgWidget    *compact;        /* borrowed: owned as a container child */
	LrgWidget    *expanded_child; /* borrowed: owned as a container child */
	gboolean      expanded;
	gboolean      toggle_on_click;
	gboolean      pressed_inside;

	LrgSpring     width;
	LrgSpring     height;
	LrgSpring     morph;
	gboolean      stepped;        /* snapped to the first target yet */

	/* Dark twin of the surrounding theme for light themes */
	LrgTheme     *twin;           /* owned, NULL while not needed */
	LrgTheme     *twin_source;    /* weak */
	guint         twin_generation;
};

G_DEFINE_FINAL_TYPE (LrgIsland, lrg_island, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_EXPANDED,
	PROP_TOGGLE_ON_CLICK,
	N_PROPS
};

enum
{
	SIGNAL_TOGGLED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Geometry
 * ========================================================================== */

/* child_size: preferred size of a child including its margin */
static void
child_size (LrgWidget *child,
            gfloat    *w,
            gfloat    *h)
{
	LrgInsets m;

	*w = 0.0f;
	*h = 0.0f;
	if (child == NULL)
		return;

	lrg_widget_get_preferred_size (child, w, h);
	lrg_widget_get_margin (child, &m);
	*w += m.left + m.right;
	*h += m.top + m.bottom;
}

/* target_size: the island size for a state */
static void
target_size (LrgIsland *self,
             gboolean   expanded,
             gfloat    *w,
             gfloat    *h)
{
	gfloat pad = lrg_container_get_padding (LRG_CONTAINER (self));

	child_size (expanded ? self->expanded_child : self->compact, w, h);
	*w += pad * 2.0f;
	*h += pad * 2.0f;
	*h = MAX (*h, ISLAND_MIN_HEIGHT);
	*w = MAX (*w, *h);   /* never narrower than a circle */
}

static void
lrg_island_measure (LrgWidget *widget,
                    gfloat    *preferred_width,
                    gfloat    *preferred_height)
{
	LrgIsland *self = LRG_ISLAND (widget);
	gfloat     w;
	gfloat     h;

	/* While animating, the island asks for its current (spring) size
	 * so parents re-flow smoothly around it */
	if (self->stepped)
	{
		w = self->width.value;
		h = self->height.value;
	}
	else
	{
		target_size (self, self->expanded, &w, &h);
	}

	if (preferred_width != NULL)
		*preferred_width = w;
	if (preferred_height != NULL)
		*preferred_height = h;
}

static void
lrg_island_layout_children (LrgContainer *container)
{
	LrgIsland *self = LRG_ISLAND (container);
	gfloat     pad = lrg_container_get_padding (container);
	gfloat     w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat     h = lrg_widget_get_height (LRG_WIDGET (self));

	/* Compact content: centred at its own size */
	if (self->compact != NULL)
	{
		gfloat cw;
		gfloat ch;

		lrg_widget_get_preferred_size (self->compact, &cw, &ch);
		lrg_widget_allocate (self->compact, roundf ((w - cw) * 0.5f), roundf ((h - ch) * 0.5f),
		                     cw, ch);
	}

	/* Expanded content: full size from the top-left, clipped by the
	 * pill while it grows, so nothing reflows mid-animation */
	if (self->expanded_child != NULL)
	{
		gfloat ew;
		gfloat eh;

		target_size (self, TRUE, &ew, &eh);
		lrg_widget_place_in_slot (self->expanded_child, pad, pad,
		                          MAX (ew - pad * 2.0f, 0.0f), MAX (eh - pad * 2.0f, 0.0f));
	}
}

/* ==========================================================================
 * Dark twin theme
 * ========================================================================== */

/*
 * update_twin:
 *
 * On light themes the island gives its subtree a copy of the midnight
 * preset carrying the surrounding accent, so labels inside are light on
 * the island's dark surface. Rebuilt when the surrounding theme changes.
 */
static void
update_twin (LrgIsland *self)
{
	LrgContainer *parent = lrg_widget_get_parent (LRG_WIDGET (self));
	LrgTheme     *outer = parent != NULL ? lrg_widget_get_theme (LRG_WIDGET (parent))
	                                     : lrg_theme_get_default ();

	if (outer == self->twin_source && lrg_theme_get_generation (outer) == self->twin_generation)
		return;

	g_set_weak_pointer (&self->twin_source, outer);
	self->twin_generation = lrg_theme_get_generation (outer);

	if (lrg_theme_get_dark (outer))
	{
		g_clear_object (&self->twin);
		lrg_widget_set_theme (LRG_WIDGET (self), NULL);
		return;
	}

	g_clear_object (&self->twin);
	self->twin = lrg_theme_new_preset ("midnight");
	lrg_theme_set_accent (self->twin, lrg_theme_get_color (outer, LRG_THEME_COLOR_ACCENT));
	lrg_theme_set_motion (self->twin, lrg_theme_get_motion (outer));
	lrg_widget_set_theme (LRG_WIDGET (self), self->twin);
}

/* ==========================================================================
 * Animation
 * ========================================================================== */

/* apply_morph: cross-fade the children for the current morph value */
static void
apply_morph (LrgIsland *self)
{
	gfloat p = CLAMP (self->morph.value, 0.0f, 1.0f);

	if (self->compact != NULL)
	{
		lrg_widget_set_opacity (self->compact, CLAMP (1.0f - p * 2.0f, 0.0f, 1.0f));
		lrg_widget_set_visible (self->compact, p < 0.5f);
	}
	if (self->expanded_child != NULL)
	{
		lrg_widget_set_opacity (self->expanded_child, CLAMP (p * 2.0f - 1.0f, 0.0f, 1.0f));
		lrg_widget_set_visible (self->expanded_child, p >= 0.5f);
	}
}

static gboolean
lrg_island_step (LrgWidget *widget,
                 gfloat     delta_time)
{
	LrgIsland *self = LRG_ISLAND (widget);
	LrgMotion  motion;
	gfloat     tw;
	gfloat     th;
	gboolean   busy = FALSE;

	update_twin (self);

	target_size (self, self->expanded, &tw, &th);
	motion = lrg_widget_get_motion (widget);

	if (!self->stepped)
	{
		/* First frame: show the current state, don't animate in */
		lrg_spring_init (&self->width, tw, motion);
		lrg_spring_init (&self->height, th, motion);
		lrg_spring_init (&self->morph, self->expanded ? 1.0f : 0.0f, motion);
		self->stepped = TRUE;
		apply_morph (self);
		lrg_widget_queue_layout (widget);
		return FALSE;
	}

	lrg_spring_set_motion (&self->width, motion);
	lrg_spring_set_motion (&self->height, motion);
	lrg_spring_set_motion (&self->morph, motion);
	lrg_spring_set_target (&self->width, tw);
	lrg_spring_set_target (&self->height, th);
	lrg_spring_set_target (&self->morph, self->expanded ? 1.0f : 0.0f);

	busy |= lrg_spring_step (&self->width, delta_time);
	busy |= lrg_spring_step (&self->height, delta_time);
	busy |= lrg_spring_step (&self->morph, delta_time);

	apply_morph (self);

	/* The size changed: let the parent place us again */
	if (busy)
		lrg_widget_queue_layout (widget);

	return busy;
}

/* ==========================================================================
 * Looks and painting
 * ========================================================================== */

static void
lrg_island_style_defaults (LrgWidget      *widget,
                           LrgUIPart       part,
                           LrgWidgetState  state,
                           LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	if (part == LRG_UI_PART_BOX)
	{
		GrlColor rim = { 255, 255, 255, 22 };

		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TOOLTIP));
		lrg_part_look_set_border (look, &rim, 1.0f);
		lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR) * 1.4f,
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y));
		if (state & LRG_WIDGET_STATE_HOVERED)
		{
			GrlColor glow;

			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT),
			                            0.35f, &glow);
			lrg_part_look_set_glow (look, &glow, 18.0f);
		}
		return;
	}

	LRG_WIDGET_CLASS (lrg_island_parent_class)->style_defaults (widget, part, state, look);
}

static void
lrg_island_paint (LrgWidget    *widget,
                  LrgPaintList *list)
{
	LrgIsland   *self = LRG_ISLAND (widget);
	LrgTheme    *theme = lrg_widget_get_theme (widget);
	LrgPartLook  look;
	gfloat       x = lrg_widget_get_world_x (widget);
	gfloat       y = lrg_widget_get_world_y (widget);
	gfloat       w = lrg_widget_get_width (widget);
	gfloat       h = lrg_widget_get_height (widget);
	gfloat       p = CLAMP (self->morph.value, 0.0f, 1.0f);
	gfloat       radius;

	/* A full pill when compact, a rounded card when expanded */
	radius = h * 0.5f + (lrg_theme_get_metric (theme, LRG_THEME_METRIC_POPUP_RADIUS) * 1.6f - h * 0.5f) * p;

	/* The corner radius is the island's shape, so it always comes from
	 * the morph; colours, border, shadow and glow follow the looks */
	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &look);
	lrg_corner_radii_init_uniform (&look.radii, radius);
	lrg_widget_paint_box (widget, list, &look, x, y, w, h);

	/* Content, clipped to the pill */
	lrg_paint_list_push_clip (list, x, y, w, h);
	LRG_WIDGET_CLASS (lrg_island_parent_class)->paint (widget, list);
	lrg_paint_list_pop_clip (list);
}

/* ==========================================================================
 * Input
 * ========================================================================== */

static gboolean
lrg_island_handle_event (LrgWidget        *widget,
                         const LrgUIEvent *event)
{
	LrgIsland *self = LRG_ISLAND (widget);
	gfloat     x = lrg_ui_event_get_x (event);
	gfloat     y = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0 || !self->toggle_on_click)
			return FALSE;
		self->pressed_inside = lrg_widget_contains_point (widget, x, y);
		return self->pressed_inside;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0 || !self->pressed_inside)
			return FALSE;
		self->pressed_inside = FALSE;
		if (lrg_widget_contains_point (widget, x, y))
			lrg_island_toggle (self);
		return TRUE;

	case LRG_UI_EVENT_ACTIVATE:
		if (!self->toggle_on_click)
			return FALSE;
		lrg_island_toggle (self);
		return TRUE;

	case LRG_UI_EVENT_CANCEL:
		/* Escape / gamepad B folds an open island */
		if (!self->expanded)
			return FALSE;
		lrg_island_set_expanded (self, FALSE);
		return TRUE;

	default:
		break;
	}

	/* Not the container default: children already saw bubbling events */
	return FALSE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_island_dispose (GObject *object)
{
	LrgIsland *self = LRG_ISLAND (object);

	self->compact = NULL;
	self->expanded_child = NULL;
	g_clear_weak_pointer (&self->twin_source);
	g_clear_object (&self->twin);

	G_OBJECT_CLASS (lrg_island_parent_class)->dispose (object);
}

static void
lrg_island_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	LrgIsland *self = LRG_ISLAND (object);

	switch (prop_id)
	{
	case PROP_EXPANDED:
		g_value_set_boolean (value, self->expanded);
		break;
	case PROP_TOGGLE_ON_CLICK:
		g_value_set_boolean (value, self->toggle_on_click);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_island_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	LrgIsland *self = LRG_ISLAND (object);

	switch (prop_id)
	{
	case PROP_EXPANDED:
		lrg_island_set_expanded (self, g_value_get_boolean (value));
		break;
	case PROP_TOGGLE_ON_CLICK:
		lrg_island_set_toggle_on_click (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_island_class_init (LrgIslandClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_island_dispose;
	object_class->get_property = lrg_island_get_property;
	object_class->set_property = lrg_island_set_property;

	widget_class->measure = lrg_island_measure;
	widget_class->paint = lrg_island_paint;
	widget_class->draw = _lrg_widget_real_draw;
	widget_class->style_defaults = lrg_island_style_defaults;
	widget_class->handle_event = lrg_island_handle_event;
	widget_class->step = lrg_island_step;
	container_class->layout_children = lrg_island_layout_children;

	/**
	 * LrgIsland:expanded:
	 *
	 * Whether the island is (or is morphing towards) its expanded form.
	 */
	properties[PROP_EXPANDED] =
		g_param_spec_boolean ("expanded", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);
	/**
	 * LrgIsland:toggle-on-click:
	 *
	 * Whether clicks and activation on the island toggle it.
	 */
	properties[PROP_TOGGLE_ON_CLICK] =
		g_param_spec_boolean ("toggle-on-click", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgIsland::toggled:
	 * @island: the island
	 * @expanded: the new state
	 *
	 * Emitted when the island starts morphing to the other form.
	 */
	signals[SIGNAL_TOGGLED] =
		g_signal_new ("toggled", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
lrg_island_init (LrgIsland *self)
{
	self->toggle_on_click = TRUE;
	lrg_container_set_padding (LRG_CONTAINER (self), ISLAND_PADDING);
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
	lrg_spring_init (&self->width, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_init (&self->height, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_init (&self->morph, 0.0f, LRG_MOTION_BOUNCY);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgIsland *
lrg_island_new (void)
{
	return g_object_new (LRG_TYPE_ISLAND, NULL);
}

/* replace_child: swap one of the two slots */
static void
replace_child (LrgIsland  *self,
               LrgWidget **slot,
               LrgWidget  *child)
{
	if (*slot == child)
		return;

	if (*slot != NULL)
		lrg_container_remove_child (LRG_CONTAINER (self), *slot);
	*slot = child;
	if (child != NULL)
		lrg_container_add_child (LRG_CONTAINER (self), child);

	if (self->stepped)
		apply_morph (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

void
lrg_island_set_compact_child (LrgIsland *self,
                              LrgWidget *child)
{
	g_return_if_fail (LRG_IS_ISLAND (self));
	g_return_if_fail (child == NULL || LRG_IS_WIDGET (child));

	replace_child (self, &self->compact, child);
}

LrgWidget *
lrg_island_get_compact_child (LrgIsland *self)
{
	g_return_val_if_fail (LRG_IS_ISLAND (self), NULL);

	return self->compact;
}

void
lrg_island_set_expanded_child (LrgIsland *self,
                               LrgWidget *child)
{
	g_return_if_fail (LRG_IS_ISLAND (self));
	g_return_if_fail (child == NULL || LRG_IS_WIDGET (child));

	replace_child (self, &self->expanded_child, child);
}

LrgWidget *
lrg_island_get_expanded_child (LrgIsland *self)
{
	g_return_val_if_fail (LRG_IS_ISLAND (self), NULL);

	return self->expanded_child;
}

gboolean
lrg_island_get_expanded (LrgIsland *self)
{
	g_return_val_if_fail (LRG_IS_ISLAND (self), FALSE);

	return self->expanded;
}

void
lrg_island_set_expanded (LrgIsland *self,
                         gboolean   expanded)
{
	g_return_if_fail (LRG_IS_ISLAND (self));

	expanded = !!expanded;
	if (self->expanded == expanded)
		return;

	self->expanded = expanded;
	if (expanded)
		lrg_widget_set_state_flags (LRG_WIDGET (self), LRG_WIDGET_STATE_OPEN);
	else
		lrg_widget_unset_state_flags (LRG_WIDGET (self), LRG_WIDGET_STATE_OPEN);
	lrg_widget_queue_layout (LRG_WIDGET (self));

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXPANDED]);
	g_signal_emit (self, signals[SIGNAL_TOGGLED], 0, expanded);
}

void
lrg_island_toggle (LrgIsland *self)
{
	g_return_if_fail (LRG_IS_ISLAND (self));

	lrg_island_set_expanded (self, !self->expanded);
}

gboolean
lrg_island_get_toggle_on_click (LrgIsland *self)
{
	g_return_val_if_fail (LRG_IS_ISLAND (self), FALSE);

	return self->toggle_on_click;
}

void
lrg_island_set_toggle_on_click (LrgIsland *self,
                                gboolean   toggle)
{
	g_return_if_fail (LRG_IS_ISLAND (self));

	toggle = !!toggle;
	if (self->toggle_on_click == toggle)
		return;
	self->toggle_on_click = toggle;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TOGGLE_ON_CLICK]);
}

gfloat
lrg_island_get_progress (LrgIsland *self)
{
	g_return_val_if_fail (LRG_IS_ISLAND (self), 0.0f);

	return self->stepped ? self->morph.value : (self->expanded ? 1.0f : 0.0f);
}
