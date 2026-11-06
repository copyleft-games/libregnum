/* lrg-canvas.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Root UI container: input dispatch (bubbling, pointer capture,
 * enter/leave, focus and gamepad navigation), the popup layer, layout,
 * animation stepping and paint-based rendering.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>
#include <raylib.h>

#include "lrg-canvas.h"
#include "lrg-canvas-private.h"
#include "lrg-widget-private.h"
#include "../paint/lrg-paint-renderer.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* Gap between an anchor and its popup */
#define POPUP_GAP (6.0f)

/* Margin popups keep from the canvas edge */
#define POPUP_EDGE (4.0f)

typedef struct
{
	LrgWidget *popup;     /* owned */
	LrgWidget *anchor;    /* owned, nullable */
	gboolean   modal;
	LrgSpring  veil;      /* modal backdrop fade, 0..1 */
	LrgSpring  close;     /* 1 while open, 0 when gone (closing ghosts) */
} PopupEntry;

struct _LrgCanvas
{
	LrgContainer  parent_instance;

	/* Focus and pointer */
	LrgWidget    *focused_widget;   /* weak */
	LrgWidget    *hovered_widget;   /* weak */
	GPtrArray    *hover_path;       /* owned refs, target first */
	LrgWidget    *capture;          /* owned while a button is held */
	LrgWidget    *pulse;            /* owned: activate press flash */
	gfloat        last_mouse_x;
	gfloat        last_mouse_y;
	gboolean      navigation_enabled;
	gboolean      paint_background;

	/* Popups */
	GArray       *popups;           /* PopupEntry, open */
	GArray       *closing;          /* PopupEntry, fading out */

	/* Rendering */
	LrgPaintList *paint;
};

G_DEFINE_FINAL_TYPE (LrgCanvas, lrg_canvas, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_FOCUSED_WIDGET,
	PROP_HOVERED_WIDGET,
	PROP_NAVIGATION_ENABLED,
	PROP_PAINT_BACKGROUND,
	N_PROPS
};

enum
{
	SIGNAL_CANCEL,
	SIGNAL_POPUP_DISMISSED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* Process-wide pointer transform (window pixels -> canvas space) */
static LrgCanvasPointerTransform default_pointer_transform = NULL;
static gpointer                  default_pointer_transform_data = NULL;

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void
popup_entry_clear (gpointer data)
{
	PopupEntry *e = data;

	g_clear_object (&e->popup);
	g_clear_object (&e->anchor);
}

/* visible_chain: the widget and every ancestor are visible */
static gboolean
visible_chain (LrgWidget *widget)
{
	LrgWidget *w;

	for (w = widget; w != NULL; w = (LrgWidget *) lrg_widget_get_parent (w))
		if (!lrg_widget_get_visible (w))
			return FALSE;
	return TRUE;
}

static LrgWidget *
find_widget_at_point_recursive (LrgWidget *widget,
                                gfloat     x,
                                gfloat     y)
{
	LrgWidget *found = NULL;

	if (!lrg_widget_get_visible (widget) || !lrg_widget_get_enabled (widget))
		return NULL;
	if (!lrg_widget_contains_point (widget, x, y))
		return NULL;

	/* Children in reverse: the last painted is on top */
	if (LRG_IS_CONTAINER (widget))
	{
		GList *l;

		for (l = g_list_last (lrg_container_get_children (LRG_CONTAINER (widget)));
		     l != NULL && found == NULL; l = l->prev)
			found = find_widget_at_point_recursive (LRG_WIDGET (l->data), x, y);
	}

	if (found == NULL && lrg_widget_get_pointer_transparent (widget))
		return NULL;
	return found != NULL ? found : widget;
}

/* top_modal_index: index of the highest modal popup, or -1 */
static gint
top_modal_index (LrgCanvas *self)
{
	gint i;

	for (i = (gint) self->popups->len - 1; i >= 0; i--)
		if (g_array_index (self->popups, PopupEntry, i).modal)
			return i;
	return -1;
}

/*
 * dispatch_event:
 *
 * Delivers @event to @widget, then bubbles it up the ancestors until
 * one handles it. Bubbling stops below the canvas.
 */
static gboolean
dispatch_event (LrgCanvas  *self,
                LrgWidget  *widget,
                LrgUIEvent *event)
{
	LrgWidget *w;

	if (widget == NULL)
		return FALSE;

	lrg_ui_event_set_target (event, widget);
	lrg_ui_event_set_bubbling (event, FALSE);

	for (w = widget; w != NULL && w != LRG_WIDGET (self);
	     w = (LrgWidget *) lrg_widget_get_parent (w))
	{
		if (lrg_widget_handle_event (w, event))
			return TRUE;
		lrg_ui_event_set_bubbling (event, TRUE);
	}
	return FALSE;
}

/* send_direct: deliver to one widget without bubbling */
static void
send_direct (LrgWidget  *widget,
             LrgUIEvent *event)
{
	lrg_ui_event_set_target (event, widget);
	lrg_ui_event_set_bubbling (event, FALSE);
	lrg_widget_handle_event (widget, event);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_canvas_dispose (GObject *object)
{
	LrgCanvas *self = LRG_CANVAS (object);

	g_clear_weak_pointer (&self->focused_widget);
	g_clear_weak_pointer (&self->hovered_widget);
	if (self->hover_path != NULL)
		g_ptr_array_set_size (self->hover_path, 0);
	g_clear_object (&self->capture);
	g_clear_object (&self->pulse);
	if (self->popups != NULL)
		g_array_set_size (self->popups, 0);
	if (self->closing != NULL)
		g_array_set_size (self->closing, 0);

	G_OBJECT_CLASS (lrg_canvas_parent_class)->dispose (object);
}

static void
lrg_canvas_finalize (GObject *object)
{
	LrgCanvas *self = LRG_CANVAS (object);

	g_clear_pointer (&self->hover_path, g_ptr_array_unref);
	g_clear_pointer (&self->popups, g_array_unref);
	g_clear_pointer (&self->closing, g_array_unref);
	g_clear_object (&self->paint);

	G_OBJECT_CLASS (lrg_canvas_parent_class)->finalize (object);
}

static void
lrg_canvas_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	LrgCanvas *self = LRG_CANVAS (object);

	switch (prop_id)
	{
	case PROP_FOCUSED_WIDGET:
		g_value_set_object (value, self->focused_widget);
		break;
	case PROP_HOVERED_WIDGET:
		g_value_set_object (value, self->hovered_widget);
		break;
	case PROP_NAVIGATION_ENABLED:
		g_value_set_boolean (value, self->navigation_enabled);
		break;
	case PROP_PAINT_BACKGROUND:
		g_value_set_boolean (value, self->paint_background);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_canvas_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	LrgCanvas *self = LRG_CANVAS (object);

	switch (prop_id)
	{
	case PROP_NAVIGATION_ENABLED:
		lrg_canvas_set_navigation_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_PAINT_BACKGROUND:
		lrg_canvas_set_paint_background (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

/*
 * lrg_canvas_draw:
 *
 * Legacy immediate path: lay out, record and render in one go so
 * lrg_widget_draw (canvas) still shows the whole UI.
 */
static void
lrg_canvas_draw (LrgWidget *widget)
{
	lrg_canvas_render (LRG_CANVAS (widget));
}

/* lrg_canvas_paint: a nested canvas paints its children */
static void
lrg_canvas_paint (LrgWidget    *widget,
                  LrgPaintList *list)
{
	_lrg_container_default_paint (widget, list);
}

static void
lrg_canvas_class_init (LrgCanvasClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_canvas_dispose;
	object_class->finalize = lrg_canvas_finalize;
	object_class->get_property = lrg_canvas_get_property;
	object_class->set_property = lrg_canvas_set_property;

	widget_class->draw = lrg_canvas_draw;
	widget_class->paint = lrg_canvas_paint;

	/**
	 * LrgCanvas:focused-widget:
	 *
	 * The widget that currently has keyboard focus.
	 */
	properties[PROP_FOCUSED_WIDGET] =
		g_param_spec_object ("focused-widget", NULL, NULL, LRG_TYPE_WIDGET,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	/**
	 * LrgCanvas:hovered-widget:
	 *
	 * The widget currently under the pointer.
	 */
	properties[PROP_HOVERED_WIDGET] =
		g_param_spec_object ("hovered-widget", NULL, NULL, LRG_TYPE_WIDGET,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
	/**
	 * LrgCanvas:navigation-enabled:
	 *
	 * Whether arrow keys, Tab and the gamepad move focus.
	 */
	properties[PROP_NAVIGATION_ENABLED] =
		g_param_spec_boolean ("navigation-enabled", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);
	/**
	 * LrgCanvas:paint-background:
	 *
	 * Whether the canvas fills itself with the theme's window colour.
	 */
	properties[PROP_PAINT_BACKGROUND] =
		g_param_spec_boolean ("paint-background", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgCanvas::cancel:
	 * @canvas: the canvas
	 *
	 * Emitted when Escape / gamepad B is pressed, no widget handled
	 * %LRG_UI_EVENT_CANCEL and no popup was open to close. Games use it
	 * for "back" / pause.
	 */
	signals[SIGNAL_CANCEL] =
		g_signal_new ("cancel", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgCanvas::popup-dismissed:
	 * @canvas: the canvas
	 * @popup: the popup that closed
	 *
	 * Emitted when a popup is dismissed.
	 */
	signals[SIGNAL_POPUP_DISMISSED] =
		g_signal_new ("popup-dismissed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_WIDGET);
}

static void
lrg_canvas_init (LrgCanvas *self)
{
	self->hover_path = g_ptr_array_new_with_free_func (g_object_unref);
	self->popups = g_array_new (FALSE, TRUE, sizeof (PopupEntry));
	g_array_set_clear_func (self->popups, popup_entry_clear);
	self->closing = g_array_new (FALSE, TRUE, sizeof (PopupEntry));
	g_array_set_clear_func (self->closing, popup_entry_clear);
	self->paint = lrg_paint_list_new ();
	self->navigation_enabled = TRUE;
	self->last_mouse_x = -1.0f;
	self->last_mouse_y = -1.0f;
}

LrgCanvas *
lrg_canvas_new (void)
{
	return g_object_new (LRG_TYPE_CANVAS, NULL);
}

void
lrg_canvas_set_default_pointer_transform (LrgCanvasPointerTransform transform,
                                          gpointer                  user_data)
{
	default_pointer_transform = transform;
	default_pointer_transform_data = user_data;
}

void
_lrg_canvas_apply_pointer_transform (gfloat *x,
                                     gfloat *y)
{
	if (default_pointer_transform != NULL)
		default_pointer_transform (x, y, default_pointer_transform_data);
}

gboolean
lrg_canvas_get_navigation_enabled (LrgCanvas *self)
{
	g_return_val_if_fail (LRG_IS_CANVAS (self), FALSE);

	return self->navigation_enabled;
}

void
lrg_canvas_set_navigation_enabled (LrgCanvas *self,
                                   gboolean   enabled)
{
	g_return_if_fail (LRG_IS_CANVAS (self));

	enabled = !!enabled;
	if (self->navigation_enabled == enabled)
		return;
	self->navigation_enabled = enabled;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAVIGATION_ENABLED]);
}

gboolean
lrg_canvas_get_paint_background (LrgCanvas *self)
{
	g_return_val_if_fail (LRG_IS_CANVAS (self), FALSE);

	return self->paint_background;
}

void
lrg_canvas_set_paint_background (LrgCanvas *self,
                                 gboolean   paint_background)
{
	g_return_if_fail (LRG_IS_CANVAS (self));

	paint_background = !!paint_background;
	if (self->paint_background == paint_background)
		return;
	self->paint_background = paint_background;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAINT_BACKGROUND]);
}

/* ==========================================================================
 * Hit testing
 * ========================================================================== */

LrgWidget *
lrg_canvas_widget_at_point (LrgCanvas *self,
                            gfloat     x,
                            gfloat     y)
{
	LrgWidget *hit;
	gint       i;
	gint       modal;

	g_return_val_if_fail (LRG_IS_CANVAS (self), NULL);

	/* Popups are on top; the newest first */
	modal = top_modal_index (self);
	for (i = (gint) self->popups->len - 1; i >= 0; i--)
	{
		PopupEntry *e = &g_array_index (self->popups, PopupEntry, i);

		hit = find_widget_at_point_recursive (e->popup, x, y);
		if (hit != NULL)
			return hit;
		if (i == modal)
			return NULL;   /* a modal popup's veil swallows the rest */
	}

	hit = find_widget_at_point_recursive (LRG_WIDGET (self), x, y);
	return hit == LRG_WIDGET (self) ? NULL : hit;
}

/* ==========================================================================
 * Focus
 * ========================================================================== */

LrgWidget *
lrg_canvas_get_focused_widget (LrgCanvas *self)
{
	g_return_val_if_fail (LRG_IS_CANVAS (self), NULL);

	return self->focused_widget;
}

LrgWidget *
lrg_canvas_get_hovered_widget (LrgCanvas *self)
{
	g_return_val_if_fail (LRG_IS_CANVAS (self), NULL);

	return self->hovered_widget;
}

/*
 * set_focus:
 *
 * Moves focus, sending FOCUS_OUT / FOCUS_IN and maintaining the
 * FOCUSED and FOCUS_VISIBLE state flags. @visible is TRUE when focus
 * moved by keyboard or gamepad (the ring shows), FALSE for clicks.
 */
static void
set_focus (LrgCanvas *self,
           LrgWidget *widget,
           gboolean   visible)
{
	if (self->focused_widget == widget)
	{
		if (widget != NULL)
		{
			if (visible)
				lrg_widget_set_state_flags (widget, LRG_WIDGET_STATE_FOCUS_VISIBLE);
			else
				lrg_widget_unset_state_flags (widget, LRG_WIDGET_STATE_FOCUS_VISIBLE);
		}
		return;
	}

	if (self->focused_widget != NULL)
	{
		g_autoptr(LrgUIEvent) ev = lrg_ui_event_new_focus_out ();
		LrgWidget            *old = self->focused_widget;

		lrg_widget_unset_state_flags (old, LRG_WIDGET_STATE_FOCUSED |
		                                   LRG_WIDGET_STATE_FOCUS_VISIBLE);
		send_direct (old, ev);
	}

	g_set_weak_pointer (&self->focused_widget, widget);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOCUSED_WIDGET]);

	if (widget != NULL)
	{
		g_autoptr(LrgUIEvent) ev = lrg_ui_event_new_focus_in ();

		lrg_widget_set_state_flags (widget, LRG_WIDGET_STATE_FOCUSED |
		                                    (visible ? LRG_WIDGET_STATE_FOCUS_VISIBLE : 0));
		send_direct (widget, ev);
	}
}

void
lrg_canvas_set_focused_widget (LrgCanvas *self,
                               LrgWidget *widget)
{
	g_return_if_fail (LRG_IS_CANVAS (self));
	g_return_if_fail (widget == NULL || LRG_IS_WIDGET (widget));

	set_focus (self, widget, FALSE);
}

/* can_focus: focusable, enabled, visible all the way up, non-empty */
static gboolean
can_focus (LrgWidget *w)
{
	return lrg_widget_get_focusable (w) && lrg_widget_get_enabled (w) &&
	       lrg_widget_get_width (w) > 0.0f && lrg_widget_get_height (w) > 0.0f &&
	       visible_chain (w);
}

static void
collect_focusable (LrgWidget *widget,
                   GPtrArray *out)
{
	GList *l;

	if (!lrg_widget_get_visible (widget) || !lrg_widget_get_enabled (widget))
		return;
	if (can_focus (widget))
		g_ptr_array_add (out, widget);
	if (LRG_IS_CONTAINER (widget))
		for (l = lrg_container_get_children (LRG_CONTAINER (widget)); l != NULL; l = l->next)
			collect_focusable (LRG_WIDGET (l->data), out);
}

/* focus_scope: everything focusable that navigation may reach */
static GPtrArray *
focus_scope (LrgCanvas *self)
{
	GPtrArray *out = g_ptr_array_new ();
	gint       modal = top_modal_index (self);
	guint      i;

	if (modal >= 0)
	{
		collect_focusable (g_array_index (self->popups, PopupEntry, modal).popup, out);
		return out;
	}

	collect_focusable (LRG_WIDGET (self), out);
	for (i = 0; i < self->popups->len; i++)
		collect_focusable (g_array_index (self->popups, PopupEntry, i).popup, out);

	/* The canvas itself is never a focus target */
	g_ptr_array_remove (out, self);
	return out;
}

static void
center_of (LrgWidget *w,
           gfloat    *cx,
           gfloat    *cy)
{
	*cx = lrg_widget_get_world_x (w) + lrg_widget_get_width (w) * 0.5f;
	*cy = lrg_widget_get_world_y (w) + lrg_widget_get_height (w) * 0.5f;
}

gboolean
lrg_canvas_move_focus (LrgCanvas       *self,
                       LrgNavDirection  direction)
{
	g_autoptr(GPtrArray) scope = NULL;
	LrgWidget           *current;
	LrgWidget           *best = NULL;
	guint                i;

	g_return_val_if_fail (LRG_IS_CANVAS (self), FALSE);

	if (direction == LRG_NAV_DIRECTION_NONE)
		return FALSE;

	scope = focus_scope (self);
	if (scope->len == 0)
		return FALSE;

	current = self->focused_widget;
	if (current != NULL && !g_ptr_array_find (scope, current, NULL))
		current = NULL;

	if (direction == LRG_NAV_DIRECTION_NEXT || direction == LRG_NAV_DIRECTION_PREVIOUS)
	{
		guint idx = 0;

		/* Tab order: depth-first, wrapping */
		if (current == NULL)
			best = direction == LRG_NAV_DIRECTION_NEXT
			       ? g_ptr_array_index (scope, 0)
			       : g_ptr_array_index (scope, scope->len - 1);
		else
		{
			g_ptr_array_find (scope, current, &idx);
			if (direction == LRG_NAV_DIRECTION_NEXT)
				best = g_ptr_array_index (scope, (idx + 1) % scope->len);
			else
				best = g_ptr_array_index (scope, (idx + scope->len - 1) % scope->len);
		}
	}
	else if (current == NULL)
	{
		/* Nothing focused yet: any direction starts at the first */
		best = g_ptr_array_index (scope, 0);
	}
	else
	{
		gfloat ox;
		gfloat oy;
		gfloat best_score = G_MAXFLOAT;

		/* Spatial: nearest widget in the direction, in-line ones
		 * preferred (sideways distance costs double) */
		center_of (current, &ox, &oy);
		for (i = 0; i < scope->len; i++)
		{
			LrgWidget *w = g_ptr_array_index (scope, i);
			gfloat     cx;
			gfloat     cy;
			gfloat     primary;
			gfloat     sideways;
			gfloat     score;

			if (w == current)
				continue;
			center_of (w, &cx, &cy);

			switch (direction)
			{
			case LRG_NAV_DIRECTION_UP:
				primary = oy - cy;
				sideways = fabsf (cx - ox);
				break;
			case LRG_NAV_DIRECTION_DOWN:
				primary = cy - oy;
				sideways = fabsf (cx - ox);
				break;
			case LRG_NAV_DIRECTION_LEFT:
				primary = ox - cx;
				sideways = fabsf (cy - oy);
				break;
			case LRG_NAV_DIRECTION_RIGHT:
			default:
				primary = cx - ox;
				sideways = fabsf (cy - oy);
				break;
			}

			if (primary < 1.0f)
				continue;
			score = primary + sideways * 2.0f;
			if (score < best_score)
			{
				best_score = score;
				best = w;
			}
		}
	}

	if (best == NULL || best == self->focused_widget)
	{
		/* Still show where focus is so the user sees the edge */
		if (self->focused_widget != NULL)
			lrg_widget_set_state_flags (self->focused_widget, LRG_WIDGET_STATE_FOCUS_VISIBLE);
		return FALSE;
	}

	set_focus (self, best, TRUE);
	return TRUE;
}

gboolean
lrg_canvas_activate_focus (LrgCanvas *self)
{
	g_autoptr(LrgUIEvent) ev = NULL;
	LrgWidget            *target;

	g_return_val_if_fail (LRG_IS_CANVAS (self), FALSE);

	target = self->focused_widget;
	if (target == NULL)
		return FALSE;

	/* A one-frame press so the activation is felt (squash, colour) */
	g_clear_object (&self->pulse);
	self->pulse = g_object_ref (target);
	lrg_widget_set_state_flags (target, LRG_WIDGET_STATE_PRESSED | LRG_WIDGET_STATE_FOCUS_VISIBLE);

	ev = lrg_ui_event_new (LRG_UI_EVENT_ACTIVATE);
	return dispatch_event (self, target, ev);
}

/* cancel: Escape / gamepad B */
static void
cancel (LrgCanvas *self)
{
	g_autoptr(LrgUIEvent) ev = lrg_ui_event_new (LRG_UI_EVENT_CANCEL);
	LrgWidget            *target = self->focused_widget;

	if (target == NULL && self->popups->len > 0)
		target = lrg_canvas_get_top_popup (self);

	if (dispatch_event (self, target, ev))
		return;

	if (self->popups->len > 0)
	{
		lrg_canvas_dismiss_popup (self, lrg_canvas_get_top_popup (self));
		return;
	}

	g_signal_emit (self, signals[SIGNAL_CANCEL], 0);
}

/* ==========================================================================
 * Pointer
 * ========================================================================== */

/*
 * update_hover:
 *
 * Diffs the old and new hover paths (target and its ancestors):
 * widgets left get POINTER_LEAVE and lose HOVERED (innermost first),
 * widgets entered gain HOVERED and get POINTER_ENTER (outermost first).
 */
static void
update_hover (LrgCanvas *self,
              LrgWidget *target)
{
	g_autoptr(GPtrArray) path = g_ptr_array_new_with_free_func (g_object_unref);
	LrgWidget           *w;
	guint                i;
	gint                 k;

	for (w = target; w != NULL && w != LRG_WIDGET (self); w = (LrgWidget *) lrg_widget_get_parent (w))
		g_ptr_array_add (path, g_object_ref (w));

	for (i = 0; i < self->hover_path->len; i++)
	{
		LrgWidget *old = g_ptr_array_index (self->hover_path, i);

		if (!g_ptr_array_find (path, old, NULL))
		{
			g_autoptr(LrgUIEvent) ev = lrg_ui_event_new (LRG_UI_EVENT_POINTER_LEAVE);

			lrg_widget_unset_state_flags (old, LRG_WIDGET_STATE_HOVERED);
			send_direct (old, ev);
		}
	}

	for (k = (gint) path->len - 1; k >= 0; k--)
	{
		LrgWidget *nw = g_ptr_array_index (path, k);

		if (!g_ptr_array_find (self->hover_path, nw, NULL))
		{
			g_autoptr(LrgUIEvent) ev = lrg_ui_event_new (LRG_UI_EVENT_POINTER_ENTER);

			lrg_widget_set_state_flags (nw, LRG_WIDGET_STATE_HOVERED);
			send_direct (nw, ev);
		}
	}

	g_ptr_array_set_size (self->hover_path, 0);
	for (i = 0; i < path->len; i++)
		g_ptr_array_add (self->hover_path, g_object_ref (g_ptr_array_index (path, i)));

	if (self->hovered_widget != target)
	{
		g_set_weak_pointer (&self->hovered_widget, target);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOVERED_WIDGET]);
	}
}

/* focus_target_for: the widget a click focuses (nearest focusable) */
static LrgWidget *
focus_target_for (LrgCanvas *self,
                  LrgWidget *target)
{
	LrgWidget *w;

	for (w = target; w != NULL && w != LRG_WIDGET (self); w = (LrgWidget *) lrg_widget_get_parent (w))
		if (lrg_widget_get_focusable (w) && lrg_widget_get_enabled (w))
			return w;
	return NULL;
}

/* popup_index_of: index of the open popup containing @widget, or -1 */
static gint
popup_index_of (LrgCanvas *self,
                LrgWidget *widget)
{
	guint i;

	for (i = 0; widget != NULL && i < self->popups->len; i++)
	{
		LrgWidget *p = g_array_index (self->popups, PopupEntry, i).popup;

		if (widget == p || lrg_widget_is_ancestor (widget, p))
			return (gint) i;
	}
	return -1;
}

/* ==========================================================================
 * Input
 * ========================================================================== */

void
lrg_canvas_handle_input (LrgCanvas *self)
{
	LrgCanvasInput input;

	g_return_if_fail (LRG_IS_CANVAS (self));

	lrg_canvas_input_poll (&input, IsWindowReady () ? GetFrameTime () : 0.0f);
	lrg_canvas_process_input (self, &input);
}

/* dispatch_button: one event per button set in @mask */
static void
dispatch_button (LrgCanvas            *self,
                 LrgWidget            *target,
                 LrgUIEventType        type,
                 guint                 mask,
                 const LrgCanvasInput *input)
{
	static const GrlMouseButton buttons[] = {
		GRL_MOUSE_BUTTON_LEFT,
		GRL_MOUSE_BUTTON_RIGHT,
		GRL_MOUSE_BUTTON_MIDDLE
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (buttons); i++)
	{
		g_autoptr(LrgUIEvent) event = NULL;

		if ((mask & LRG_CANVAS_BUTTON_MASK (buttons[i])) == 0)
			continue;

		event = lrg_ui_event_new_mouse_button (type, (gint) buttons[i],
		                                       input->pointer_x, input->pointer_y);
		lrg_ui_event_set_modifiers (event, input->modifiers);
		dispatch_event (self, target, event);
	}
}

/* arrow_direction: arrow key -> navigation direction */
static LrgNavDirection
arrow_direction (GrlKey key)
{
	switch (key)
	{
	case GRL_KEY_UP:
		return LRG_NAV_DIRECTION_UP;
	case GRL_KEY_DOWN:
		return LRG_NAV_DIRECTION_DOWN;
	case GRL_KEY_LEFT:
		return LRG_NAV_DIRECTION_LEFT;
	case GRL_KEY_RIGHT:
		return LRG_NAV_DIRECTION_RIGHT;
	default:
		return LRG_NAV_DIRECTION_NONE;
	}
}

void
lrg_canvas_process_input (LrgCanvas            *self,
                          const LrgCanvasInput *input)
{
	gfloat     mx;
	gfloat     my;
	LrgWidget *hit;
	LrgWidget *target;
	LrgWidget *key_target;
	guint      i;

	g_return_if_fail (LRG_IS_CANVAS (self));
	g_return_if_fail (input != NULL);

	/* End last frame's activation flash */
	if (self->pulse != NULL)
	{
		if (self->capture != self->pulse)
			lrg_widget_unset_state_flags (self->pulse, LRG_WIDGET_STATE_PRESSED);
		g_clear_object (&self->pulse);
	}

	mx = input->pointer_x;
	my = input->pointer_y;

	/* Pointer target: the captured widget wins while a button is held */
	hit = lrg_canvas_widget_at_point (self, mx, my);
	target = self->capture != NULL ? self->capture : hit;

	update_hover (self, hit);

	/* Movement */
	if (mx != self->last_mouse_x || my != self->last_mouse_y)
	{
		g_autoptr(LrgUIEvent) event = lrg_ui_event_new_mouse_move (mx, my);

		lrg_ui_event_set_modifiers (event, input->modifiers);
		dispatch_event (self, target, event);
		self->last_mouse_x = mx;
		self->last_mouse_y = my;

		/* A captured widget looks pressed only while the pointer is on it */
		if (self->capture != NULL)
		{
			if (lrg_widget_contains_point (self->capture, mx, my))
				lrg_widget_set_state_flags (self->capture, LRG_WIDGET_STATE_PRESSED);
			else
				lrg_widget_unset_state_flags (self->capture, LRG_WIDGET_STATE_PRESSED);
		}
	}

	/* Presses */
	if (input->buttons_pressed != 0)
	{
		gint inside_popup = popup_index_of (self, hit);

		g_debug ("canvas click at ui(%.1f, %.1f) -> %s", mx, my,
		         hit != NULL ? G_OBJECT_TYPE_NAME (hit) : "(nothing)");

		/* Clicking outside non-modal popups closes them (the click
		 * still reaches what is underneath) */
		while (self->popups->len > 0 &&
		       (gint) self->popups->len - 1 > inside_popup &&
		       !g_array_index (self->popups, PopupEntry, self->popups->len - 1).modal)
		{
			lrg_canvas_dismiss_popup (self, lrg_canvas_get_top_popup (self));
		}

		if (input->buttons_pressed & LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT))
		{
			if (hit != NULL)
				set_focus (self, focus_target_for (self, hit), FALSE);
			else if (top_modal_index (self) < 0)
				set_focus (self, NULL, FALSE);

			if (hit != NULL && self->capture == NULL)
			{
				self->capture = g_object_ref (hit);
				lrg_widget_set_state_flags (hit, LRG_WIDGET_STATE_PRESSED);
			}
		}

		target = self->capture != NULL ? self->capture : hit;
		dispatch_button (self, target, LRG_UI_EVENT_MOUSE_BUTTON_DOWN,
		                 input->buttons_pressed, input);
	}

	/* Releases go to the capture, which then lets go */
	if (input->buttons_released != 0)
	{
		g_autoptr(LrgWidget) released = self->capture != NULL ? g_object_ref (self->capture) : NULL;

		dispatch_button (self, released != NULL ? released : hit,
		                 LRG_UI_EVENT_MOUSE_BUTTON_UP, input->buttons_released, input);

		if (released != NULL &&
		    (input->buttons_released & LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT)))
		{
			lrg_widget_unset_state_flags (released, LRG_WIDGET_STATE_PRESSED);
			g_clear_object (&self->capture);
		}
	}

	/* Wheel */
	if (input->wheel_x != 0.0f || input->wheel_y != 0.0f)
	{
		g_autoptr(LrgUIEvent) event = lrg_ui_event_new_scroll (mx, my, input->wheel_x,
		                                                       input->wheel_y);

		lrg_ui_event_set_modifiers (event, input->modifiers);
		dispatch_event (self, hit, event);
	}

	/* Keys: the focused widget (or the top popup) first; unhandled
	 * arrows navigate, Enter/Space activate, Escape cancels */
	key_target = self->focused_widget != NULL ? self->focused_widget : lrg_canvas_get_top_popup (self);
	for (i = 0; i < input->n_keys; i++)
	{
		g_autoptr(LrgUIEvent) event = lrg_ui_event_new_key (LRG_UI_EVENT_KEY_DOWN, input->keys[i]);
		GrlKey                key = input->keys[i];
		LrgNavDirection       dir;

		lrg_ui_event_set_modifiers (event, input->modifiers);
		if (!dispatch_event (self, key_target, event))
		{
			dir = arrow_direction (key);
			if (dir != LRG_NAV_DIRECTION_NONE && self->navigation_enabled)
				lrg_canvas_move_focus (self, dir);
			else if (key == GRL_KEY_ENTER || key == GRL_KEY_KP_ENTER || key == GRL_KEY_SPACE)
				lrg_canvas_activate_focus (self);
			else if (key == GRL_KEY_ESCAPE)
				cancel (self);
		}

		key_target = self->focused_widget != NULL ? self->focused_widget
		                                          : lrg_canvas_get_top_popup (self);
	}

	/* Typed characters */
	for (i = 0; i < input->n_chars; i++)
	{
		g_autoptr(LrgUIEvent) event = lrg_ui_event_new_text_input (input->chars[i]);

		lrg_ui_event_set_modifiers (event, input->modifiers);
		dispatch_event (self, self->focused_widget, event);
	}

	/* Gamepad / Tab navigation. Directions are offered to the focused
	 * widget as arrow keys first (a list moves its cursor, a slider
	 * steps); only unhandled ones move focus. */
	if (input->nav != LRG_NAV_DIRECTION_NONE && self->navigation_enabled)
	{
		static const GrlKey arrows[] = {
			GRL_KEY_NULL, GRL_KEY_UP, GRL_KEY_DOWN, GRL_KEY_LEFT, GRL_KEY_RIGHT
		};
		gboolean handled = FALSE;

		if ((guint) input->nav < G_N_ELEMENTS (arrows) && self->focused_widget != NULL)
		{
			g_autoptr(LrgUIEvent) event = lrg_ui_event_new_key (LRG_UI_EVENT_KEY_DOWN,
			                                                    arrows[input->nav]);

			handled = dispatch_event (self, self->focused_widget, event);
		}
		if (!handled)
			lrg_canvas_move_focus (self, input->nav);
	}
	if (input->activate)
		lrg_canvas_activate_focus (self);
	if (input->cancel)
		cancel (self);

	lrg_canvas_update (self, input->delta_time);
}

/* ==========================================================================
 * Popups
 * ========================================================================== */

LrgPopupPlacement
lrg_canvas_place_popup (gfloat             canvas_width,
                        gfloat             canvas_height,
                        gfloat             anchor_x,
                        gfloat             anchor_y,
                        gfloat             anchor_width,
                        gfloat             anchor_height,
                        gfloat             popup_width,
                        gfloat             popup_height,
                        LrgPopupPlacement  placement,
                        gfloat            *x,
                        gfloat            *y)
{
	LrgPopupPlacement used = placement;
	gfloat            ax = anchor_x;
	gfloat            ay = anchor_y;
	gfloat            aw = anchor_width;
	gfloat            ah = anchor_height;
	gfloat            pw = popup_width;
	gfloat            ph = popup_height;
	gfloat            px = ax;
	gfloat            py = ay;

	switch (placement)
	{
	case LRG_POPUP_PLACEMENT_BELOW:
	case LRG_POPUP_PLACEMENT_ABOVE:
		{
			gboolean below_fits = ay + ah + POPUP_GAP + ph <= canvas_height - POPUP_EDGE;
			gboolean above_fits = ay - POPUP_GAP - ph >= POPUP_EDGE;

			if (placement == LRG_POPUP_PLACEMENT_BELOW)
				used = (below_fits || !above_fits) ? LRG_POPUP_PLACEMENT_BELOW
				                                   : LRG_POPUP_PLACEMENT_ABOVE;
			else
				used = (above_fits || !below_fits) ? LRG_POPUP_PLACEMENT_ABOVE
				                                   : LRG_POPUP_PLACEMENT_BELOW;
			px = ax;
			py = used == LRG_POPUP_PLACEMENT_BELOW ? ay + ah + POPUP_GAP : ay - POPUP_GAP - ph;
		}
		break;

	case LRG_POPUP_PLACEMENT_RIGHT:
	case LRG_POPUP_PLACEMENT_LEFT:
		{
			gboolean right_fits = ax + aw + POPUP_GAP + pw <= canvas_width - POPUP_EDGE;
			gboolean left_fits = ax - POPUP_GAP - pw >= POPUP_EDGE;

			if (placement == LRG_POPUP_PLACEMENT_RIGHT)
				used = (right_fits || !left_fits) ? LRG_POPUP_PLACEMENT_RIGHT
				                                  : LRG_POPUP_PLACEMENT_LEFT;
			else
				used = (left_fits || !right_fits) ? LRG_POPUP_PLACEMENT_LEFT
				                                  : LRG_POPUP_PLACEMENT_RIGHT;
			py = ay;
			px = used == LRG_POPUP_PLACEMENT_RIGHT ? ax + aw + POPUP_GAP : ax - POPUP_GAP - pw;
		}
		break;

	case LRG_POPUP_PLACEMENT_AT_POINTER:
		px = ax;
		py = ay;
		break;

	case LRG_POPUP_PLACEMENT_CENTER:
	default:
		px = (canvas_width - pw) * 0.5f;
		py = (canvas_height - ph) * 0.45f;
		break;
	}

	/* Keep it on the canvas */
	px = CLAMP (px, POPUP_EDGE, MAX (POPUP_EDGE, canvas_width - pw - POPUP_EDGE));
	py = CLAMP (py, POPUP_EDGE, MAX (POPUP_EDGE, canvas_height - ph - POPUP_EDGE));

	*x = roundf (px);
	*y = roundf (py);
	return used;
}

void
lrg_canvas_show_popup (LrgCanvas         *self,
                       LrgWidget         *popup,
                       LrgWidget         *anchor,
                       LrgPopupPlacement  placement,
                       gboolean           modal)
{
	PopupEntry e;
	gfloat     pw;
	gfloat     ph;
	gfloat     ax;
	gfloat     ay;
	gfloat     aw = 0.0f;
	gfloat     ah = 0.0f;
	gfloat     x;
	gfloat     y;

	g_return_if_fail (LRG_IS_CANVAS (self));
	g_return_if_fail (LRG_IS_WIDGET (popup));
	g_return_if_fail (anchor == NULL || LRG_IS_WIDGET (anchor));

	/* Re-opening a popup whose close animation is still running:
	 * drop the ghost so it can be shown again right away */
	{
		guint k;

		for (k = 0; k < self->closing->len; k++)
		{
			if (g_array_index (self->closing, PopupEntry, k).popup == popup)
			{
				_lrg_widget_set_parent (popup, NULL);
				g_array_remove_index (self->closing, k);
				break;
			}
		}
	}
	g_return_if_fail (lrg_widget_get_parent (popup) == NULL);

	/* Size: keep an explicit one, otherwise the preferred size */
	pw = lrg_widget_get_width (popup);
	ph = lrg_widget_get_height (popup);
	if (pw <= 0.0f || ph <= 0.0f)
	{
		gfloat mw;
		gfloat mh;

		lrg_widget_get_preferred_size (popup, &mw, &mh);
		pw = pw > 0.0f ? pw : mw;
		ph = ph > 0.0f ? ph : mh;
	}

	if (anchor != NULL)
	{
		ax = lrg_widget_get_world_x (anchor) - lrg_widget_get_world_x (LRG_WIDGET (self));
		ay = lrg_widget_get_world_y (anchor) - lrg_widget_get_world_y (LRG_WIDGET (self));
		aw = lrg_widget_get_width (anchor);
		ah = lrg_widget_get_height (anchor);
	}
	else
	{
		ax = self->last_mouse_x;
		ay = self->last_mouse_y;
		if (placement != LRG_POPUP_PLACEMENT_AT_POINTER)
			placement = LRG_POPUP_PLACEMENT_CENTER;
	}

	lrg_canvas_place_popup (lrg_widget_get_width (LRG_WIDGET (self)),
	                        lrg_widget_get_height (LRG_WIDGET (self)),
	                        ax, ay, aw, ah, pw, ph, placement, &x, &y);

	/* Parent it to the canvas for coordinates and theme, but keep it
	 * out of the child list: the popup layer paints and hit-tests it */
	_lrg_widget_set_parent (popup, LRG_CONTAINER (self));
	lrg_widget_allocate (popup, x, y, pw, ph);

	if (lrg_widget_get_appear (popup) == LRG_APPEAR_NONE)
		lrg_widget_set_appear (popup, LRG_APPEAR_POP, 0.0f);
	lrg_widget_play_appear (popup);

	memset (&e, 0, sizeof (PopupEntry));
	e.popup = g_object_ref (popup);
	e.anchor = anchor != NULL ? g_object_ref (anchor) : NULL;
	e.modal = modal;
	lrg_spring_init (&e.veil, 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_set_target (&e.veil, 1.0f);
	lrg_spring_init (&e.close, 1.0f, LRG_MOTION_SNAPPY);
	g_array_append_val (self->popups, e);

	if (anchor != NULL)
		lrg_widget_set_state_flags (anchor, LRG_WIDGET_STATE_OPEN);

	/* A modal popup takes focus; keep the ring if it came by keyboard */
	if (modal)
	{
		g_autoptr(GPtrArray) scope = focus_scope (self);
		gboolean             keyboard = self->focused_widget != NULL &&
		                                (lrg_widget_get_state_flags (self->focused_widget) &
		                                 LRG_WIDGET_STATE_FOCUS_VISIBLE);

		set_focus (self, scope->len > 0 ? g_ptr_array_index (scope, 0) : NULL, keyboard);
	}
}

void
lrg_canvas_dismiss_popup (LrgCanvas *self,
                          LrgWidget *popup)
{
	gint i;
	gint found = -1;

	g_return_if_fail (LRG_IS_CANVAS (self));
	g_return_if_fail (LRG_IS_WIDGET (popup));

	for (i = 0; i < (gint) self->popups->len; i++)
		if (g_array_index (self->popups, PopupEntry, i).popup == popup)
			found = i;
	if (found < 0)
		return;

	/* Close it and everything stacked above it, newest first */
	for (i = (gint) self->popups->len - 1; i >= found; i--)
	{
		PopupEntry e = g_array_index (self->popups, PopupEntry, i);
		LrgWidget *p = e.popup;

		/* Move the entry out without clearing its references */
		g_array_index (self->popups, PopupEntry, i).popup = NULL;
		g_array_index (self->popups, PopupEntry, i).anchor = NULL;
		g_array_remove_index (self->popups, (guint) i);

		if (e.anchor != NULL)
			lrg_widget_unset_state_flags (e.anchor, LRG_WIDGET_STATE_OPEN);

		/* Drop focus and capture inside it */
		if (self->focused_widget != NULL &&
		    (self->focused_widget == p || lrg_widget_is_ancestor (self->focused_widget, p)))
			set_focus (self, (e.anchor != NULL && lrg_widget_get_focusable (e.anchor)) ? e.anchor : NULL,
			           FALSE);
		if (self->capture != NULL &&
		    (self->capture == p || lrg_widget_is_ancestor (self->capture, p)))
			g_clear_object (&self->capture);
		update_hover (self, NULL);

		/* Fade out as a ghost, then release */
		lrg_spring_set_target (&e.close, 0.0f);
		lrg_spring_set_target (&e.veil, 0.0f);
		g_array_append_val (self->closing, e);

		g_signal_emit (self, signals[SIGNAL_POPUP_DISMISSED], 0, p);
	}
}

LrgWidget *
lrg_canvas_get_top_popup (LrgCanvas *self)
{
	g_return_val_if_fail (LRG_IS_CANVAS (self), NULL);

	if (self->popups->len == 0)
		return NULL;
	return g_array_index (self->popups, PopupEntry, self->popups->len - 1).popup;
}

guint
lrg_canvas_get_popup_count (LrgCanvas *self)
{
	g_return_val_if_fail (LRG_IS_CANVAS (self), 0);

	return self->popups->len;
}

/* ==========================================================================
 * Animation, layout and rendering
 * ========================================================================== */

gboolean
lrg_canvas_update (LrgCanvas *self,
                   gfloat     delta_time)
{
	gboolean busy;
	guint    i;

	g_return_val_if_fail (LRG_IS_CANVAS (self), FALSE);

	busy = lrg_widget_step (LRG_WIDGET (self), delta_time);

	for (i = 0; i < self->popups->len; i++)
	{
		PopupEntry *e = &g_array_index (self->popups, PopupEntry, i);

		busy |= lrg_widget_step (e->popup, delta_time);
		busy |= lrg_spring_step (&e->veil, delta_time);
	}

	/* Closing ghosts: run down, then release */
	for (i = 0; i < self->closing->len;)
	{
		PopupEntry *e = &g_array_index (self->closing, PopupEntry, i);

		lrg_spring_step (&e->veil, delta_time);
		if (lrg_spring_step (&e->close, delta_time) && e->close.value > 0.01f)
		{
			busy = TRUE;
			i++;
			continue;
		}
		_lrg_widget_set_parent (e->popup, NULL);
		g_array_remove_index (self->closing, i);
	}

	return busy;
}

/* paint_veil: dim the canvas under a modal popup */
static void
paint_veil (LrgCanvas  *self,
            PopupEntry *e)
{
	const GrlColor *veil;
	GrlColor        c;
	LrgWidget      *root = LRG_WIDGET (self);

	if (!e->modal || e->veil.value <= 0.0f)
		return;

	veil = lrg_theme_get_color (lrg_widget_get_theme (root), LRG_THEME_COLOR_VEIL);
	lrg_paint_color_with_alpha (veil, ((gfloat) veil->a / 255.0f) * CLAMP (e->veil.value, 0.0f, 1.0f),
	                            &c);
	lrg_paint_list_rect (self->paint, lrg_widget_get_world_x (root), lrg_widget_get_world_y (root),
	                     lrg_widget_get_width (root), lrg_widget_get_height (root), 0.0f, &c);
}

LrgPaintList *
lrg_canvas_record (LrgCanvas *self)
{
	LrgWidget *root;
	guint      i;

	g_return_val_if_fail (LRG_IS_CANVAS (self), NULL);
	root = LRG_WIDGET (self);

	/* Layout what changed */
	lrg_widget_layout (root);
	for (i = 0; i < self->popups->len; i++)
		lrg_widget_layout (g_array_index (self->popups, PopupEntry, i).popup);

	lrg_paint_list_clear (self->paint);
	if (!lrg_widget_get_visible (root))
		return self->paint;

	if (self->paint_background)
		lrg_paint_list_rect (self->paint, lrg_widget_get_world_x (root), lrg_widget_get_world_y (root),
		                     lrg_widget_get_width (root), lrg_widget_get_height (root), 0.0f,
		                     lrg_theme_get_color (lrg_widget_get_theme (root), LRG_THEME_COLOR_WINDOW));

	/* The tree (the canvas paints its children) */
	_lrg_container_default_paint (root, self->paint);

	/* Ghosts of closing popups shrink towards their anchor */
	for (i = 0; i < self->closing->len; i++)
	{
		PopupEntry *e = &g_array_index (self->closing, PopupEntry, i);
		gfloat      p = CLAMP (e->close.value, 0.0f, 1.0f);
		LrgWidget  *pivot = e->anchor != NULL ? e->anchor : e->popup;
		gfloat      px;
		gfloat      py;
		guint       mark;

		paint_veil (self, e);
		px = lrg_widget_get_world_x (pivot) + lrg_widget_get_width (pivot) * 0.5f;
		py = lrg_widget_get_world_y (pivot) + lrg_widget_get_height (pivot) * 0.5f;
		mark = lrg_paint_list_begin_group (self->paint);
		lrg_widget_paint (e->popup, self->paint);
		lrg_paint_list_transform_group (self->paint, mark, 0.92f + 0.08f * p, px, py, 0.0f, 0.0f, p);
	}

	/* Open popups, oldest first, each modal one with its veil */
	for (i = 0; i < self->popups->len; i++)
	{
		PopupEntry *e = &g_array_index (self->popups, PopupEntry, i);

		paint_veil (self, e);
		lrg_widget_paint (e->popup, self->paint);
	}

	return self->paint;
}

void
lrg_canvas_render (LrgCanvas *self)
{
	g_return_if_fail (LRG_IS_CANVAS (self));

	lrg_canvas_record (self);
	lrg_paint_renderer_render (lrg_paint_renderer_get_default (), self->paint);
}

LrgPaintList *
lrg_canvas_get_paint_list (LrgCanvas *self)
{
	g_return_val_if_fail (LRG_IS_CANVAS (self), NULL);

	return self->paint;
}
