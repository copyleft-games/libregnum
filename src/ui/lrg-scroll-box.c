/* lrg-scroll-box.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScrollBox: A clipped, scrollable viewport onto one child with spring-smoothed scrolling.
 *
 * The content child is laid out once at its preferred size and then
 * only moved while scrolling (no relayout per frame). Offsets are
 * springs stepped by step() with the canvas frame time (so
 * lrg_canvas_update() reports TRUE while a scroll glides or the bars
 * fade): the wheel moves a target and the
 * displayed offset glides after it, overshooting a little at the ends
 * (rubber band). Overlay scrollbars fade in on activity; their hit
 * zones are two invisible child widgets kept on top of the content so
 * that presses on a bar never reach the content underneath.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-scroll-box.h"
#include "lrg-canvas.h"
#include "lrg-widget-private.h"
#include "lrg-scrollbar-private.h"
#include "../lrg-log.h"

/* Largest preferred size along a scrolling axis (users usually size
 * scroll boxes with a fill weight, an explicit size or a max size) */
#define SCROLL_BOX_NATURAL_MAX (240.0f)

struct _LrgScrollBox
{
	LrgContainer   parent_instance;

	LrgWidget     *child;       /* borrowed: owned by the children list */
	LrgScrollAxis  ax;          /* horizontal; ax.bar owned */
	LrgScrollAxis  ay;          /* vertical; ay.bar owned */
	LrgScrollFx    fx;

	gboolean       hscroll;
	gboolean       vscroll;
	gboolean       kinetic;

	gfloat         child_x0;    /* child position at zero offset */
	gfloat         child_y0;
	gfloat         shown_x;     /* offsets last applied and notified */
	gfloat         shown_y;

	gconstpointer  last_focus;  /* compared only, never dereferenced */
	gboolean       in_layout;   /* guards re-entry from add/remove */
};

G_DEFINE_FINAL_TYPE (LrgScrollBox, lrg_scroll_box, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_CHILD,
	PROP_SCROLL_X,
	PROP_SCROLL_Y,
	PROP_HSCROLL,
	PROP_VSCROLL,
	PROP_KINETIC,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* smooth: glide (spring) or jump, from the kinetic flag and the motion */
static gboolean
smooth (LrgScrollBox *self)
{
	return self->kinetic && lrg_widget_get_motion (LRG_WIDGET (self)) != LRG_MOTION_NONE;
}

/* viewport: the content rectangle (size minus padding), relative */
static void
viewport (LrgScrollBox *self,
          gfloat       *pad,
          gfloat       *vw,
          gfloat       *vh)
{
	LrgWidget *w = LRG_WIDGET (self);

	*pad = lrg_container_get_padding (LRG_CONTAINER (self));
	*vw = MAX (0.0f, lrg_widget_get_width (w) - *pad * 2.0f);
	*vh = MAX (0.0f, lrg_widget_get_height (w) - *pad * 2.0f);
}

/*
 * restack:
 *
 * Re-adds the two bar hit zones so they are the last children (hit
 * first). Layout is suppressed while doing it.
 */
static void
restack (LrgScrollBox *self)
{
	LrgContainer *c = LRG_CONTAINER (self);
	gboolean      was = self->in_layout;

	self->in_layout = TRUE;
	lrg_container_remove_child (c, self->ax.bar);
	lrg_container_remove_child (c, self->ay.bar);
	lrg_container_add_child (c, self->ax.bar);
	lrg_container_add_child (c, self->ay.bar);
	self->in_layout = was;
}

/*
 * apply_offsets:
 *
 * Moves the content to the displayed offsets (whole units for crisp
 * text) and notifies scroll-x / scroll-y when they changed.
 */
static void
apply_offsets (LrgScrollBox *self)
{
	gfloat sx = self->hscroll ? lrg_scroll_axis_get_value (&self->ax) : 0.0f;
	gfloat sy = self->vscroll ? lrg_scroll_axis_get_value (&self->ay) : 0.0f;

	if (self->child != NULL)
		lrg_widget_set_position (self->child, self->child_x0 - sx, self->child_y0 - sy);

	if (sx != self->shown_x)
	{
		self->shown_x = sx;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCROLL_X]);
	}
	if (sy != self->shown_y)
	{
		self->shown_y = sy;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCROLL_Y]);
	}
}

/* ensure_layout: bring the extents up to date before using them */
static void
ensure_layout (LrgScrollBox *self)
{
	if (lrg_widget_get_needs_layout (LRG_WIDGET (self)))
		lrg_container_layout_children (LRG_CONTAINER (self));
}

/*
 * adopt_stray_child:
 *
 * A child added with lrg_container_add_child() instead of
 * lrg_scroll_box_set_child() becomes the content (the first one wins).
 */
static void
adopt_stray_child (LrgScrollBox *self)
{
	GList *l;

	if (self->child != NULL)
		return;

	for (l = lrg_container_get_children (LRG_CONTAINER (self)); l != NULL; l = l->next)
	{
		LrgWidget *w = LRG_WIDGET (l->data);

		if (w != self->ax.bar && w != self->ay.bar)
		{
			self->child = w;
			restack (self);
			g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CHILD]);
			return;
		}
	}
}

/*
 * follow_focus:
 *
 * When keyboard / gamepad focus moved onto a widget inside the
 * content since the last frame, scroll it into view.
 */
static void
follow_focus (LrgScrollBox *self)
{
	LrgWidget *root = lrg_widget_get_root (LRG_WIDGET (self));
	LrgWidget *focus;

	if (!LRG_IS_CANVAS (root))
		return;

	focus = lrg_canvas_get_focused_widget (LRG_CANVAS (root));
	if ((gconstpointer) focus == self->last_focus)
		return;
	self->last_focus = focus;

	if (focus == NULL || self->child == NULL)
		return;
	if (focus != self->child && !lrg_widget_is_ancestor (focus, self->child))
		return;
	if ((lrg_widget_get_state_flags (focus) & LRG_WIDGET_STATE_FOCUS_VISIBLE) == 0)
		return;

	lrg_scroll_box_scroll_to_widget (self, focus);
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static void
lrg_scroll_box_layout_children (LrgContainer *container)
{
	LrgScrollBox *self = LRG_SCROLL_BOX (container);
	gfloat        pad;
	gfloat        vw;
	gfloat        vh;
	gfloat        zone;
	gboolean      vshow;
	gboolean      hshow;

	if (self->in_layout || self->ax.bar == NULL)
		return;

	adopt_stray_child (self);
	self->in_layout = TRUE;
	viewport (self, &pad, &vw, &vh);

	/* Content: preferred size, stretched to the viewport on axes that
	 * do not scroll (and when smaller than the viewport) */
	if (self->child != NULL && lrg_widget_get_visible (self->child))
	{
		gfloat ow;
		gfloat oh;
		gfloat cw;
		gfloat ch;

		_lrg_widget_outer_size (self->child, &ow, &oh);
		cw = self->hscroll ? MAX (ow, vw) : vw;
		ch = self->vscroll ? MAX (oh, vh) : vh;

		lrg_scroll_axis_set_extent (&self->ax, cw, vw);
		lrg_scroll_axis_set_extent (&self->ay, ch, vh);

		lrg_widget_place_in_slot (self->child, pad, pad, cw, ch);
		self->child_x0 = lrg_widget_get_x (self->child);
		self->child_y0 = lrg_widget_get_y (self->child);
	}
	else
	{
		lrg_scroll_axis_set_extent (&self->ax, 0.0f, vw);
		lrg_scroll_axis_set_extent (&self->ay, 0.0f, vh);
	}

	/* Bar hit zones along the right and bottom edges of the viewport,
	 * present only when that axis can actually scroll */
	zone = lrg_scroll_bar_zone (LRG_WIDGET (self));
	vshow = self->vscroll && lrg_scroll_axis_get_max (&self->ay) > 0.0f;
	hshow = self->hscroll && lrg_scroll_axis_get_max (&self->ax) > 0.0f;

	lrg_widget_set_visible (self->ay.bar, vshow);
	lrg_widget_set_visible (self->ax.bar, hshow);
	lrg_widget_allocate (self->ay.bar, pad + vw - zone, pad, zone, MAX (0.0f, vh - (hshow ? zone : 0.0f)));
	lrg_widget_allocate (self->ax.bar, pad, pad + vh - zone, MAX (0.0f, vw - (vshow ? zone : 0.0f)), zone);

	apply_offsets (self);
	self->in_layout = FALSE;
}

static void
lrg_scroll_box_measure (LrgWidget *widget,
                        gfloat    *preferred_width,
                        gfloat    *preferred_height)
{
	LrgScrollBox *self = LRG_SCROLL_BOX (widget);
	gfloat        pad = lrg_container_get_padding (LRG_CONTAINER (self));
	gfloat        w = 0.0f;
	gfloat        h = 0.0f;

	/* The child's size; capped along scrolling axes, since the point
	 * of a scroll box is to be smaller than its content */
	if (self->child != NULL && lrg_widget_get_visible (self->child))
	{
		_lrg_widget_outer_size (self->child, &w, &h);
		if (self->hscroll)
			w = MIN (w, SCROLL_BOX_NATURAL_MAX);
		if (self->vscroll)
			h = MIN (h, SCROLL_BOX_NATURAL_MAX);
	}

	*preferred_width = w + pad * 2.0f;
	*preferred_height = h + pad * 2.0f;
}

/* ==========================================================================
 * Painting
 * ========================================================================== */

static void
lrg_scroll_box_style_defaults (LrgWidget      *widget,
                               LrgUIPart       part,
                               LrgWidgetState  state,
                               LrgPartLook    *look)
{
	/* The box itself is transparent; theme rules may give it a body */
	if (lrg_scroll_style_defaults (lrg_widget_get_theme (widget), part, state, look))
		return;

	LRG_WIDGET_CLASS (lrg_scroll_box_parent_class)->style_defaults (widget, part, state, look);
}

/*
 * lrg_scroll_box_step:
 *
 * Advances time by the canvas frame: scroll newly focused content
 * into view, step the offset springs (keeping the bars shown while
 * they move), move the content, and fade the bars.
 *
 * Returns: %TRUE while an offset glides / bounces or the bars fade
 *   (or wait to fade out)
 */
static gboolean
lrg_scroll_box_step (LrgWidget *widget,
                     gfloat     delta_time)
{
	LrgScrollBox *self = LRG_SCROLL_BOX (widget);
	gboolean      moving = FALSE;
	gboolean      keep;

	if (self->ax.bar == NULL)
		return FALSE;

	follow_focus (self);
	if (self->hscroll)
		moving |= lrg_scroll_axis_step (&self->ax, delta_time);
	if (self->vscroll)
		moving |= lrg_scroll_axis_step (&self->ay, delta_time);
	if (moving)
		lrg_scroll_fx_poke (&self->fx);
	apply_offsets (self);

	keep = self->ax.dragging || self->ay.dragging ||
	       (lrg_widget_get_state_flags (self->ay.bar) & LRG_WIDGET_STATE_HOVERED) ||
	       (lrg_widget_get_state_flags (self->ax.bar) & LRG_WIDGET_STATE_HOVERED);

	return lrg_scroll_fx_update (&self->fx, keep, delta_time) || moving;
}

static void
lrg_scroll_box_paint (LrgWidget    *widget,
                      LrgPaintList *list)
{
	LrgScrollBox *self = LRG_SCROLL_BOX (widget);
	LrgPartLook   box;
	gfloat        wx = lrg_widget_get_world_x (widget);
	gfloat        wy = lrg_widget_get_world_y (widget);
	gfloat        pad;
	gfloat        vw;
	gfloat        vh;

	/* 1. Content follows the displayed offsets. Springs move only in
	 *    step(); this just syncs jumps made since (idempotent). */
	apply_offsets (self);

	/* 2. Optional body (transparent unless a theme rule styles it) */
	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, wx, wy,
	                      lrg_widget_get_width (widget), lrg_widget_get_height (widget));

	/* 3. Content, cut to the viewport */
	viewport (self, &pad, &vw, &vh);
	if (self->child != NULL)
	{
		lrg_paint_list_push_clip (list, wx + pad, wy + pad, vw, vh);
		lrg_widget_paint (self->child, list);
		lrg_paint_list_pop_clip (list);
	}

	/* 4. Overlay scrollbars */
	lrg_scroll_bar_paint (widget, list, &self->ay, TRUE, self->fx.fade.value);
	lrg_scroll_bar_paint (widget, list, &self->ax, FALSE, self->fx.fade.value);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/* page_keys: PageUp/PageDown/Home/End (and arrows when @own) */
static gboolean
handle_key (LrgScrollBox *self,
            GrlKey        key,
            gboolean      own)
{
	LrgScrollAxis *ay = &self->ay;
	LrgScrollAxis *ax = &self->ax;
	gboolean       can_y = self->vscroll && lrg_scroll_axis_get_max (ay) > 0.0f;
	gboolean       can_x = self->hscroll && lrg_scroll_axis_get_max (ax) > 0.0f;
	gfloat         page = MAX (1.0f, ay->viewport * 0.9f);
	gboolean       s = smooth (self);

	switch (key)
	{
	case GRL_KEY_PAGE_UP:
		if (!can_y)
			return FALSE;
		lrg_scroll_axis_scroll_to (ay, ay->pos.target - page, s);
		break;
	case GRL_KEY_PAGE_DOWN:
		if (!can_y)
			return FALSE;
		lrg_scroll_axis_scroll_to (ay, ay->pos.target + page, s);
		break;
	case GRL_KEY_HOME:
		if (!can_y && !can_x)
			return FALSE;
		lrg_scroll_axis_scroll_to (can_y ? ay : ax, 0.0f, s);
		break;
	case GRL_KEY_END:
		if (!can_y && !can_x)
			return FALSE;
		lrg_scroll_axis_scroll_to (can_y ? ay : ax, G_MAXFLOAT, s);
		break;
	case GRL_KEY_UP:
	case GRL_KEY_DOWN:
		/* Arrows belong to focus navigation unless the box itself is
		 * focused and can scroll that way */
		if (!own || !can_y)
			return FALSE;
		lrg_scroll_axis_scroll_to (ay, ay->pos.target +
		                           (key == GRL_KEY_UP ? -LRG_SCROLL_WHEEL_STEP : LRG_SCROLL_WHEEL_STEP), s);
		break;
	case GRL_KEY_LEFT:
	case GRL_KEY_RIGHT:
		if (!own || !can_x)
			return FALSE;
		lrg_scroll_axis_scroll_to (ax, ax->pos.target +
		                           (key == GRL_KEY_LEFT ? -LRG_SCROLL_WHEEL_STEP : LRG_SCROLL_WHEEL_STEP), s);
		break;
	default:
		return FALSE;
	}

	lrg_scroll_fx_poke (&self->fx);
	return TRUE;
}

static gboolean
lrg_scroll_box_handle_event (LrgWidget        *widget,
                             const LrgUIEvent *event)
{
	LrgScrollBox *self = LRG_SCROLL_BOX (widget);
	LrgWidget    *target = lrg_ui_event_get_target (event);
	gfloat        x = lrg_ui_event_get_x (event);
	gfloat        y = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_POINTER_ENTER:
		lrg_scroll_fx_poke (&self->fx);
		return FALSE;

	case LRG_UI_EVENT_MOUSE_MOVE:
		/* Moving over the box shows the bars; a captured drag follows */
		lrg_scroll_fx_poke (&self->fx);
		if (self->ay.dragging)
		{
			lrg_scroll_bar_drag (&self->ay, TRUE, y);
			return TRUE;
		}
		if (self->ax.dragging)
		{
			lrg_scroll_bar_drag (&self->ax, FALSE, x);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		if (target == self->ay.bar)
		{
			lrg_scroll_bar_press (&self->ay, TRUE, y, smooth (self));
			lrg_scroll_fx_poke (&self->fx);
			return TRUE;
		}
		if (target == self->ax.bar)
		{
			lrg_scroll_bar_press (&self->ax, FALSE, x, smooth (self));
			lrg_scroll_fx_poke (&self->fx);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (self->ax.dragging || self->ay.dragging)
		{
			self->ax.dragging = FALSE;
			self->ay.dragging = FALSE;
			lrg_scroll_fx_poke (&self->fx);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_SCROLL:
		{
			gfloat   dy = -lrg_ui_event_get_scroll_y (event) * LRG_SCROLL_WHEEL_STEP;
			gfloat   dx = -lrg_ui_event_get_scroll_x (event) * LRG_SCROLL_WHEEL_STEP;
			gboolean handled = FALSE;

			ensure_layout (self);

			/* Shift+wheel, or a box that only scrolls sideways, turns
			 * the vertical wheel into horizontal scrolling */
			if ((lrg_ui_event_get_modifiers (event) & LRG_INPUT_MODIFIER_SHIFT) ||
			    (!self->vscroll && self->hscroll))
			{
				dx += dy;
				dy = 0.0f;
			}

			if (self->vscroll && dy != 0.0f)
				handled |= lrg_scroll_axis_wheel (&self->ay, dy, smooth (self));
			if (self->hscroll && dx != 0.0f)
				handled |= lrg_scroll_axis_wheel (&self->ax, dx, smooth (self));
			if (handled)
				lrg_scroll_fx_poke (&self->fx);
			return handled;
		}

	case LRG_UI_EVENT_KEY_DOWN:
		ensure_layout (self);
		return handle_key (self, lrg_ui_event_get_key (event), target == widget);

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_scroll_box_constructed (GObject *object)
{
	LrgScrollBox *self = LRG_SCROLL_BOX (object);

	G_OBJECT_CLASS (lrg_scroll_box_parent_class)->constructed (object);

	/* The bar hit zones are permanent children, kept last */
	self->in_layout = TRUE;
	self->ax.bar = lrg_scroll_bar_new ("hscrollbar");
	self->ay.bar = lrg_scroll_bar_new ("vscrollbar");
	lrg_container_add_child (LRG_CONTAINER (self), self->ax.bar);
	lrg_container_add_child (LRG_CONTAINER (self), self->ay.bar);
	self->in_layout = FALSE;
}

static void
lrg_scroll_box_dispose (GObject *object)
{
	LrgScrollBox *self = LRG_SCROLL_BOX (object);

	self->child = NULL;
	g_clear_object (&self->ax.bar);
	g_clear_object (&self->ay.bar);

	G_OBJECT_CLASS (lrg_scroll_box_parent_class)->dispose (object);
}

static void
lrg_scroll_box_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgScrollBox *self = LRG_SCROLL_BOX (object);

	switch (prop_id)
	{
	case PROP_CHILD:
		g_value_set_object (value, self->child);
		break;
	case PROP_SCROLL_X:
		g_value_set_float (value, lrg_scroll_box_get_scroll_x (self));
		break;
	case PROP_SCROLL_Y:
		g_value_set_float (value, lrg_scroll_box_get_scroll_y (self));
		break;
	case PROP_HSCROLL:
		g_value_set_boolean (value, self->hscroll);
		break;
	case PROP_VSCROLL:
		g_value_set_boolean (value, self->vscroll);
		break;
	case PROP_KINETIC:
		g_value_set_boolean (value, self->kinetic);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_scroll_box_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgScrollBox *self = LRG_SCROLL_BOX (object);

	switch (prop_id)
	{
	case PROP_CHILD:
		lrg_scroll_box_set_child (self, g_value_get_object (value));
		break;
	case PROP_SCROLL_X:
		lrg_scroll_box_set_scroll_x (self, g_value_get_float (value));
		break;
	case PROP_SCROLL_Y:
		lrg_scroll_box_set_scroll_y (self, g_value_get_float (value));
		break;
	case PROP_HSCROLL:
		lrg_scroll_box_set_hscroll (self, g_value_get_boolean (value));
		break;
	case PROP_VSCROLL:
		lrg_scroll_box_set_vscroll (self, g_value_get_boolean (value));
		break;
	case PROP_KINETIC:
		lrg_scroll_box_set_kinetic (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_scroll_box_class_init (LrgScrollBoxClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->constructed = lrg_scroll_box_constructed;
	object_class->dispose = lrg_scroll_box_dispose;
	object_class->get_property = lrg_scroll_box_get_property;
	object_class->set_property = lrg_scroll_box_set_property;

	widget_class->measure = lrg_scroll_box_measure;
	widget_class->paint = lrg_scroll_box_paint;
	widget_class->step = lrg_scroll_box_step;
	widget_class->style_defaults = lrg_scroll_box_style_defaults;
	widget_class->handle_event = lrg_scroll_box_handle_event;

	container_class->layout_children = lrg_scroll_box_layout_children;

	/**
	 * LrgScrollBox:child:
	 *
	 * The content widget shown through the viewport.
	 */
	properties[PROP_CHILD] =
		g_param_spec_object ("child", "Child", "The content widget",
		                     LRG_TYPE_WIDGET,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgScrollBox:scroll-x:
	 *
	 * Horizontal offset currently displayed. Setting it jumps there.
	 */
	properties[PROP_SCROLL_X] =
		g_param_spec_float ("scroll-x", "Scroll X", "Displayed horizontal offset",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgScrollBox:scroll-y:
	 *
	 * Vertical offset currently displayed. Setting it jumps there.
	 * While a rubber band plays it briefly leaves the scroll range.
	 */
	properties[PROP_SCROLL_Y] =
		g_param_spec_float ("scroll-y", "Scroll Y", "Displayed vertical offset",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgScrollBox:hscroll:
	 *
	 * Whether the content scrolls horizontally. When %FALSE the child
	 * is exactly as wide as the viewport.
	 */
	properties[PROP_HSCROLL] =
		g_param_spec_boolean ("hscroll", "Horizontal scrolling", "Scroll horizontally",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgScrollBox:vscroll:
	 *
	 * Whether the content scrolls vertically. When %FALSE the child is
	 * exactly as tall as the viewport.
	 */
	properties[PROP_VSCROLL] =
		g_param_spec_boolean ("vscroll", "Vertical scrolling", "Scroll vertically",
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgScrollBox:kinetic:
	 *
	 * Spring-smoothed scrolling with a rubber-band overshoot at the
	 * ends. When %FALSE (or when the theme's motion is "none") the
	 * wheel, keys and track clicks jump straight to the new offset.
	 */
	properties[PROP_KINETIC] =
		g_param_spec_boolean ("kinetic", "Kinetic", "Smooth, rubber-banded scrolling",
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_scroll_box_init (LrgScrollBox *self)
{
	lrg_scroll_axis_init (&self->ax);
	lrg_scroll_axis_init (&self->ay);
	lrg_scroll_fx_init (&self->fx);
	self->hscroll = FALSE;
	self->vscroll = TRUE;
	self->kinetic = TRUE;

	/* Focusable so PageUp/PageDown/Home/End and arrows reach it even
	 * when the content has nothing focusable */
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgScrollBox *
lrg_scroll_box_new (void)
{
	return g_object_new (LRG_TYPE_SCROLL_BOX, NULL);
}

void
lrg_scroll_box_set_child (LrgScrollBox *self,
                          LrgWidget    *child)
{
	LrgContainer *c;

	g_return_if_fail (LRG_IS_SCROLL_BOX (self));
	g_return_if_fail (child == NULL || LRG_IS_WIDGET (child));

	if (self->child == child)
		return;
	c = LRG_CONTAINER (self);

	/* Swap the content without intermediate layouts, then put the bars
	 * back on top */
	self->in_layout = TRUE;
	if (self->child != NULL)
		lrg_container_remove_child (c, self->child);
	self->child = NULL;
	if (child != NULL)
	{
		lrg_container_add_child (c, child);
		self->child = child;
		restack (self);
	}
	self->in_layout = FALSE;

	/* New content starts at the top */
	lrg_scroll_axis_scroll_to (&self->ax, 0.0f, FALSE);
	lrg_scroll_axis_scroll_to (&self->ay, 0.0f, FALSE);

	lrg_widget_queue_layout (LRG_WIDGET (self));
	lrg_container_layout_children (c);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CHILD]);
}

LrgWidget *
lrg_scroll_box_get_child (LrgScrollBox *self)
{
	g_return_val_if_fail (LRG_IS_SCROLL_BOX (self), NULL);
	return self->child;
}

gfloat
lrg_scroll_box_get_scroll_x (LrgScrollBox *self)
{
	g_return_val_if_fail (LRG_IS_SCROLL_BOX (self), 0.0f);
	return self->hscroll ? lrg_scroll_axis_get_value (&self->ax) : 0.0f;
}

void
lrg_scroll_box_set_scroll_x (LrgScrollBox *self,
                             gfloat        scroll_x)
{
	g_return_if_fail (LRG_IS_SCROLL_BOX (self));
	lrg_scroll_box_scroll_to (self, scroll_x, self->ay.pos.target, FALSE);
}

gfloat
lrg_scroll_box_get_scroll_y (LrgScrollBox *self)
{
	g_return_val_if_fail (LRG_IS_SCROLL_BOX (self), 0.0f);
	return self->vscroll ? lrg_scroll_axis_get_value (&self->ay) : 0.0f;
}

void
lrg_scroll_box_set_scroll_y (LrgScrollBox *self,
                             gfloat        scroll_y)
{
	g_return_if_fail (LRG_IS_SCROLL_BOX (self));
	lrg_scroll_box_scroll_to (self, self->ax.pos.target, scroll_y, FALSE);
}

gboolean
lrg_scroll_box_get_hscroll (LrgScrollBox *self)
{
	g_return_val_if_fail (LRG_IS_SCROLL_BOX (self), FALSE);
	return self->hscroll;
}

void
lrg_scroll_box_set_hscroll (LrgScrollBox *self,
                            gboolean      enabled)
{
	g_return_if_fail (LRG_IS_SCROLL_BOX (self));

	enabled = !!enabled;
	if (self->hscroll == enabled)
		return;
	self->hscroll = enabled;
	if (!enabled)
		lrg_scroll_axis_scroll_to (&self->ax, 0.0f, FALSE);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HSCROLL]);
}

gboolean
lrg_scroll_box_get_vscroll (LrgScrollBox *self)
{
	g_return_val_if_fail (LRG_IS_SCROLL_BOX (self), FALSE);
	return self->vscroll;
}

void
lrg_scroll_box_set_vscroll (LrgScrollBox *self,
                            gboolean      enabled)
{
	g_return_if_fail (LRG_IS_SCROLL_BOX (self));

	enabled = !!enabled;
	if (self->vscroll == enabled)
		return;
	self->vscroll = enabled;
	if (!enabled)
		lrg_scroll_axis_scroll_to (&self->ay, 0.0f, FALSE);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VSCROLL]);
}

gboolean
lrg_scroll_box_get_kinetic (LrgScrollBox *self)
{
	g_return_val_if_fail (LRG_IS_SCROLL_BOX (self), FALSE);
	return self->kinetic;
}

void
lrg_scroll_box_set_kinetic (LrgScrollBox *self,
                            gboolean      kinetic)
{
	g_return_if_fail (LRG_IS_SCROLL_BOX (self));

	kinetic = !!kinetic;
	if (self->kinetic == kinetic)
		return;
	self->kinetic = kinetic;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KINETIC]);
}

void
lrg_scroll_box_scroll_to (LrgScrollBox *self,
                          gfloat        x,
                          gfloat        y,
                          gboolean      animate)
{
	g_return_if_fail (LRG_IS_SCROLL_BOX (self));

	ensure_layout (self);
	animate = animate && smooth (self);

	if (self->hscroll)
		lrg_scroll_axis_scroll_to (&self->ax, x, animate);
	if (self->vscroll)
		lrg_scroll_axis_scroll_to (&self->ay, y, animate);
	lrg_scroll_fx_poke (&self->fx);

	/* A jump is visible at once (and in the properties) */
	if (!animate)
		apply_offsets (self);
}

void
lrg_scroll_box_get_max_scroll (LrgScrollBox *self,
                               gfloat       *x,
                               gfloat       *y)
{
	g_return_if_fail (LRG_IS_SCROLL_BOX (self));

	ensure_layout (self);
	if (x != NULL)
		*x = self->hscroll ? lrg_scroll_axis_get_max (&self->ax) : 0.0f;
	if (y != NULL)
		*y = self->vscroll ? lrg_scroll_axis_get_max (&self->ay) : 0.0f;
}

void
lrg_scroll_box_scroll_to_widget (LrgScrollBox *self,
                                 LrgWidget    *descendant)
{
	gfloat pad;
	gfloat vw;
	gfloat vh;
	gfloat cx;
	gfloat cy;
	gfloat cw;
	gfloat ch;
	gfloat tx;
	gfloat ty;

	g_return_if_fail (LRG_IS_SCROLL_BOX (self));
	g_return_if_fail (LRG_IS_WIDGET (descendant));
	g_return_if_fail (lrg_widget_is_ancestor (descendant, LRG_WIDGET (self)));

	ensure_layout (self);
	viewport (self, &pad, &vw, &vh);

	/* The descendant's rectangle in content coordinates (0,0 = the
	 * content origin at zero offset) */
	cx = lrg_widget_get_world_x (descendant) - (lrg_widget_get_world_x (LRG_WIDGET (self)) + pad) +
	     self->shown_x;
	cy = lrg_widget_get_world_y (descendant) - (lrg_widget_get_world_y (LRG_WIDGET (self)) + pad) +
	     self->shown_y;
	cw = lrg_widget_get_width (descendant);
	ch = lrg_widget_get_height (descendant);

	/* Smallest move: bring the far edge in, then make sure the near
	 * edge wins when the widget is bigger than the viewport */
	tx = self->ax.bounce ? lrg_scroll_axis_clamp (&self->ax, self->ax.pos.target) : self->ax.pos.target;
	ty = self->ay.bounce ? lrg_scroll_axis_clamp (&self->ay, self->ay.pos.target) : self->ay.pos.target;
	if (cx + cw > tx + vw)
		tx = cx + cw - vw;
	if (cx < tx)
		tx = cx;
	if (cy + ch > ty + vh)
		ty = cy + ch - vh;
	if (cy < ty)
		ty = cy;

	lrg_scroll_box_scroll_to (self, tx, ty, TRUE);
}
