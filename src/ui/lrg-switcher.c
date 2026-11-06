/* lrg-switcher.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSwitcher: shows one child at a time, with spring-driven transitions.
 *
 * Page visibility is the switcher's business: the visible page has its
 * widget "visible" flag on and every other page has it off, which is
 * exactly what keeps hidden pages out of layout, painting, stepping of
 * focus navigation and hit-testing in the core. Children added with
 * lrg_container_add_child() are noticed in layout_children() (the
 * container calls it on every add and remove) and tracked in a
 * reference-holding array so removals can be detected too.
 *
 * Transitions: one spring runs from 0 to 1. While it runs, the page
 * being left (previous) is already hidden, so the canvas never
 * hit-tests it, but paint() still records it by calling its paint
 * method directly inside a transform group, then records the incoming
 * page in another group: crossfade animates opacity, slides translate,
 * zoom scales around the centre. Reversing a running transition
 * (switching back to the page being left) flips the spring around so
 * the motion continues smoothly instead of restarting.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-switcher.h"
#include "lrg-canvas.h"
#include "lrg-widget-private.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* How far a zoom transition scales pages (fraction of their size) */
#define ZOOM_AMOUNT (0.08f)

struct _LrgSwitcher
{
	LrgContainer          parent_instance;

	GPtrArray            *pages;          /* LrgWidget*, owned refs, child order not kept */
	LrgWidget            *visible;        /* shown / incoming page (ref in @pages) */
	LrgWidget            *previous;       /* page being left during a transition */

	LrgSwitcherTransition transition;     /* default for switches */
	LrgSwitcherTransition active;         /* resolved kind of the running transition */
	gboolean              reversed;       /* slide / zoom geometry mirrored */
	gboolean              running;
	LrgSpring             progress;       /* 0 -> 1 */

	gboolean              homogeneous;
	gboolean              interpolate_size;
	gboolean              size_ready;     /* size springs hold a real value */
	LrgSpring             size_w;
	LrgSpring             size_h;

	gboolean              stepped;        /* stepped at least once: may animate */
};

G_DEFINE_FINAL_TYPE (LrgSwitcher, lrg_switcher, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_VISIBLE_CHILD,
	PROP_VISIBLE_CHILD_NAME,
	PROP_VISIBLE_INDEX,
	PROP_TRANSITION,
	PROP_HOMOGENEOUS,
	PROP_INTERPOLATE_SIZE,
	PROP_TRANSITION_RUNNING,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Object-data key of a page's name */
static GQuark name_quark;

/* ==========================================================================
 * LrgSwitcherTransition
 * ========================================================================== */

GType
lrg_switcher_transition_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SWITCHER_TRANSITION_NONE, "LRG_SWITCHER_TRANSITION_NONE", "none" },
			{ LRG_SWITCHER_TRANSITION_CROSSFADE, "LRG_SWITCHER_TRANSITION_CROSSFADE", "crossfade" },
			{ LRG_SWITCHER_TRANSITION_SLIDE_LEFT, "LRG_SWITCHER_TRANSITION_SLIDE_LEFT", "slide-left" },
			{ LRG_SWITCHER_TRANSITION_SLIDE_RIGHT, "LRG_SWITCHER_TRANSITION_SLIDE_RIGHT", "slide-right" },
			{ LRG_SWITCHER_TRANSITION_SLIDE_UP, "LRG_SWITCHER_TRANSITION_SLIDE_UP", "slide-up" },
			{ LRG_SWITCHER_TRANSITION_SLIDE_DOWN, "LRG_SWITCHER_TRANSITION_SLIDE_DOWN", "slide-down" },
			{ LRG_SWITCHER_TRANSITION_SLIDE, "LRG_SWITCHER_TRANSITION_SLIDE", "slide" },
			{ LRG_SWITCHER_TRANSITION_ZOOM, "LRG_SWITCHER_TRANSITION_ZOOM", "zoom" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgSwitcherTransition"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void
notify_visible (LrgSwitcher *self)
{
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE_CHILD]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE_CHILD_NAME]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE_INDEX]);
}

/* set_running: update the flag and its read-only property */
static void
set_running (LrgSwitcher *self,
             gboolean     running)
{
	if (self->running == running)
		return;
	self->running = running;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRANSITION_RUNNING]);
}

/* finish_transition: drop the outgoing page and rest the spring at 1 */
static void
finish_transition (LrgSwitcher *self)
{
	self->previous = NULL;
	lrg_spring_snap (&self->progress, 1.0f);
	self->reversed = FALSE;
	set_running (self, FALSE);
}

/*
 * resolve_transition:
 *
 * Turns SLIDE into a concrete direction: towards the left (new page
 * from the right) when moving to a later page, mirrored right to left.
 */
static LrgSwitcherTransition
resolve_transition (LrgSwitcher           *self,
                    LrgSwitcherTransition  transition,
                    LrgWidget             *from,
                    LrgWidget             *to)
{
	GList   *children;
	gint     i_from;
	gint     i_to;
	gboolean forward;

	if (transition != LRG_SWITCHER_TRANSITION_SLIDE)
		return transition;

	children = lrg_container_get_children (LRG_CONTAINER (self));
	i_from = g_list_index (children, from);
	i_to = g_list_index (children, to);
	forward = i_to > i_from;
	if (lrg_widget_is_rtl (LRG_WIDGET (self)))
		forward = !forward;
	return forward ? LRG_SWITCHER_TRANSITION_SLIDE_LEFT : LRG_SWITCHER_TRANSITION_SLIDE_RIGHT;
}

/*
 * forget_focus:
 *
 * A page being hidden must not keep the keyboard focus (the focused
 * widget would be invisible and still receive keys): clear it.
 */
static void
forget_focus (LrgSwitcher *self,
              LrgWidget   *page)
{
	LrgWidget *root = lrg_widget_get_root (LRG_WIDGET (self));
	LrgWidget *focused;

	if (page == NULL || root == NULL || !LRG_IS_CANVAS (root))
		return;
	focused = lrg_canvas_get_focused_widget (LRG_CANVAS (root));
	if (focused != NULL && (focused == page || lrg_widget_is_ancestor (focused, page)))
		lrg_canvas_set_focused_widget (LRG_CANVAS (root), NULL);
}

/* ==========================================================================
 * Page bookkeeping
 * ========================================================================== */

/*
 * sync_pages:
 *
 * Brings @pages in line with the container's children: pages that were
 * removed get their visible flag back (they may be added elsewhere) and
 * their name dropped; new children are hidden, unless the switcher has
 * no visible page yet, in which case the first one becomes visible.
 */
static void
sync_pages (LrgSwitcher *self)
{
	GList    *children = lrg_container_get_children (LRG_CONTAINER (self));
	gboolean  changed = FALSE;
	GList    *l;
	guint     i;

	/* Removed pages */
	for (i = self->pages->len; i > 0; i--)
	{
		LrgWidget *page = g_ptr_array_index (self->pages, i - 1);

		if (g_list_find (children, page) != NULL)
			continue;
		if (page == self->previous)
			finish_transition (self);
		if (page == self->visible)
		{
			self->visible = NULL;
			changed = TRUE;
		}
		g_object_set_qdata (G_OBJECT (page), name_quark, NULL);
		lrg_widget_set_visible (page, TRUE);
		g_ptr_array_remove_index (self->pages, i - 1);
	}

	/* New children */
	for (l = children; l != NULL; l = l->next)
	{
		LrgWidget *child = LRG_WIDGET (l->data);

		if (g_ptr_array_find (self->pages, child, NULL))
			continue;
		g_ptr_array_add (self->pages, g_object_ref (child));
		if (self->visible == NULL && !changed)
		{
			self->visible = child;
			lrg_widget_set_visible (child, TRUE);
			changed = TRUE;
		}
		else
		{
			lrg_widget_set_visible (child, FALSE);
		}
	}

	/* The visible page went away: show the first remaining one */
	if (self->visible == NULL && children != NULL)
	{
		self->visible = LRG_WIDGET (children->data);
		lrg_widget_set_visible (self->visible, TRUE);
		changed = TRUE;
	}

	if (changed)
	{
		self->size_ready = FALSE;
		notify_visible (self);
	}
}

/* ==========================================================================
 * Switching
 * ========================================================================== */

static void
switch_to (LrgSwitcher           *self,
           LrgWidget             *child,
           LrgSwitcherTransition  transition)
{
	LrgWidget *old = self->visible;
	gboolean   instant;

	if (child == old)
		return;

	/* Instant when nothing to animate from, on the very first frame,
	 * under reduced motion, without motion or without a size */
	instant = old == NULL || transition == LRG_SWITCHER_TRANSITION_NONE || !self->stepped ||
	          lrg_motion_get_reduced () || lrg_widget_get_motion (LRG_WIDGET (self)) == LRG_MOTION_NONE ||
	          lrg_widget_get_width (LRG_WIDGET (self)) <= 0.0f ||
	          lrg_widget_get_height (LRG_WIDGET (self)) <= 0.0f;

	if (!instant && self->running && child == self->previous)
	{
		/*
		 * Going back to the page being left: swap the pages and mirror
		 * the spring (p -> 1 - p, velocity negated) so every page keeps
		 * its on-screen position and speed. The geometry is reversed so
		 * the incoming page leaves the way the old one came.
		 */
		self->previous = old;
		self->progress.value = 1.0f - self->progress.value;
		self->progress.velocity = -self->progress.velocity;
		lrg_spring_set_target (&self->progress, 1.0f);
		self->reversed = !self->reversed;
	}
	else if (!instant)
	{
		/* A new transition (or a third page interrupting one): start
		 * over from the page that is currently coming in */
		self->active = resolve_transition (self, transition, old, child);
		self->previous = old;
		self->reversed = FALSE;
		lrg_spring_snap (&self->progress, 0.0f);
		lrg_spring_set_target (&self->progress, 1.0f);
		set_running (self, TRUE);
	}

	/* Only the incoming page is visible to layout and hit-testing; the
	 * old one is painted by paint() until the spring settles */
	forget_focus (self, old);
	if (old != NULL)
		lrg_widget_set_visible (old, FALSE);
	self->visible = child;
	lrg_widget_set_visible (child, TRUE);

	if (instant)
		finish_transition (self);

	notify_visible (self);
	lrg_container_layout_children (LRG_CONTAINER (self));
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static void
lrg_switcher_layout_children (LrgContainer *container)
{
	LrgSwitcher *self = LRG_SWITCHER (container);
	LrgWidget   *widget = LRG_WIDGET (container);
	gfloat       pad = lrg_container_get_padding (container);
	gfloat       w;
	gfloat       h;

	sync_pages (self);

	/* Every page gets the whole content rectangle; hidden pages are not
	 * laid out, except the one still painting during a transition */
	w = MAX (0.0f, lrg_widget_get_width (widget) - pad * 2.0f);
	h = MAX (0.0f, lrg_widget_get_height (widget) - pad * 2.0f);
	if (self->visible != NULL)
		lrg_widget_place_in_slot (self->visible, pad, pad, w, h);
	if (self->previous != NULL)
		lrg_widget_place_in_slot (self->previous, pad, pad, w, h);
}

static void
lrg_switcher_measure (LrgWidget *widget,
                      gfloat    *preferred_width,
                      gfloat    *preferred_height)
{
	LrgSwitcher *self = LRG_SWITCHER (widget);
	gfloat       pad = lrg_container_get_padding (LRG_CONTAINER (widget));
	gfloat       w = 0.0f;
	gfloat       h = 0.0f;
	guint        i;

	if (self->homogeneous)
	{
		/* The largest page, whether shown or not */
		for (i = 0; i < self->pages->len; i++)
		{
			gfloat pw;
			gfloat ph;

			_lrg_widget_outer_size (g_ptr_array_index (self->pages, i), &pw, &ph);
			w = MAX (w, pw);
			h = MAX (h, ph);
		}
	}
	else
	{
		if (self->visible != NULL)
			_lrg_widget_outer_size (self->visible, &w, &h);

		/* Interpolated: report the springs, which head for the page's
		 * size; they start at it when there is nothing to glide from */
		if (self->interpolate_size)
		{
			if (!self->size_ready || !self->stepped)
			{
				lrg_spring_snap (&self->size_w, w);
				lrg_spring_snap (&self->size_h, h);
				self->size_ready = TRUE;
			}
			lrg_spring_set_target (&self->size_w, w);
			lrg_spring_set_target (&self->size_h, h);
			w = ceilf (MAX (0.0f, self->size_w.value));
			h = ceilf (MAX (0.0f, self->size_h.value));
		}
	}

	*preferred_width = w + pad * 2.0f;
	*preferred_height = h + pad * 2.0f;
}

/* ==========================================================================
 * Animation
 * ========================================================================== */

static gboolean
lrg_switcher_step (LrgWidget *widget,
                   gfloat     delta_time)
{
	LrgSwitcher *self = LRG_SWITCHER (widget);
	LrgMotion    motion = lrg_widget_get_motion (widget);
	gboolean     busy = FALSE;

	self->stepped = TRUE;

	/* Page transition */
	if (self->running)
	{
		lrg_spring_set_motion (&self->progress, motion);
		busy |= lrg_spring_step (&self->progress, delta_time);
		if (lrg_spring_is_settled (&self->progress))
			finish_transition (self);
	}

	/* Size interpolation: a moving size needs a new layout */
	if (self->interpolate_size && !self->homogeneous && self->size_ready)
	{
		gboolean moved = FALSE;

		lrg_spring_set_motion (&self->size_w, motion);
		lrg_spring_set_motion (&self->size_h, motion);
		moved |= lrg_spring_step (&self->size_w, delta_time);
		moved |= lrg_spring_step (&self->size_h, delta_time);
		if (moved)
		{
			lrg_widget_queue_layout (widget);
			busy = TRUE;
		}
	}

	return busy;
}

/* ==========================================================================
 * Painting
 * ========================================================================== */

/*
 * paint_hidden_page:
 *
 * Records a page whose visible flag is off (lrg_widget_paint() would
 * skip it) through its paint method, with its own opacity. Legacy
 * draw-only widgets cannot be recorded this way and simply vanish at
 * the start of the transition.
 */
static void
paint_hidden_page (LrgWidget    *page,
                   LrgPaintList *list)
{
	LrgWidgetClass *klass = LRG_WIDGET_GET_CLASS (page);
	gfloat          opacity = lrg_widget_get_opacity (page);

	if (klass->paint == NULL || klass->paint == _lrg_widget_real_paint)
		return;
	if (opacity < 1.0f)
		lrg_paint_list_push_opacity (list, opacity);
	klass->paint (page, list);
	if (opacity < 1.0f)
		lrg_paint_list_pop_opacity (list);
}

/*
 * transition_params:
 *
 * Scale, translation and opacity of a page at progress @p (0..1, may
 * overshoot). @incoming selects the page coming in; @reversed mirrors
 * the geometry (a running transition was turned around).
 */
static void
transition_params (LrgSwitcherTransition  kind,
                   gboolean               reversed,
                   gboolean               incoming,
                   gfloat                 p,
                   gfloat                 w,
                   gfloat                 h,
                   gfloat                *scale,
                   gfloat                *dx,
                   gfloat                *dy,
                   gfloat                *alpha)
{
	gfloat a = CLAMP (p, 0.0f, 1.0f);
	gfloat dir = reversed ? -1.0f : 1.0f;

	*scale = 1.0f;
	*dx = 0.0f;
	*dy = 0.0f;
	*alpha = 1.0f;

	switch (kind)
	{
	case LRG_SWITCHER_TRANSITION_CROSSFADE:
		*alpha = incoming ? a : 1.0f - a;
		break;

	/* Slides: the incoming page travels from one side of the switcher
	 * to its place while the old one leaves through the other side */
	case LRG_SWITCHER_TRANSITION_SLIDE_LEFT:
		*dx = dir * (incoming ? w * (1.0f - p) : -w * p);
		break;
	case LRG_SWITCHER_TRANSITION_SLIDE_RIGHT:
		*dx = -dir * (incoming ? w * (1.0f - p) : -w * p);
		break;
	case LRG_SWITCHER_TRANSITION_SLIDE_UP:
		*dy = dir * (incoming ? h * (1.0f - p) : -h * p);
		break;
	case LRG_SWITCHER_TRANSITION_SLIDE_DOWN:
		*dy = -dir * (incoming ? h * (1.0f - p) : -h * p);
		break;

	/* Zoom: the new page grows into place from slightly small while the
	 * old one grows on and fades; reversed, pages come from large and
	 * leave by shrinking, so turning around never jumps */
	case LRG_SWITCHER_TRANSITION_ZOOM:
		if (incoming)
			*scale = 1.0f - dir * ZOOM_AMOUNT * (1.0f - p);
		else
			*scale = 1.0f + dir * ZOOM_AMOUNT * p;
		*alpha = incoming ? a : 1.0f - a;
		break;

	case LRG_SWITCHER_TRANSITION_NONE:
	case LRG_SWITCHER_TRANSITION_SLIDE:
	default:
		*alpha = incoming ? 1.0f : 0.0f;
		break;
	}
}

static void
lrg_switcher_paint (LrgWidget    *widget,
                    LrgPaintList *list)
{
	LrgSwitcher *self = LRG_SWITCHER (widget);
	gfloat       x;
	gfloat       y;
	gfloat       w;
	gfloat       h;
	gfloat       p;
	gfloat       scale;
	gfloat       dx;
	gfloat       dy;
	gfloat       alpha;
	gboolean     clip;
	guint        mark;

	/* At rest: just the visible page */
	if (!self->running || self->previous == NULL)
	{
		if (self->visible != NULL)
			lrg_widget_paint (self->visible, list);
		return;
	}

	x = lrg_widget_get_world_x (widget);
	y = lrg_widget_get_world_y (widget);
	w = lrg_widget_get_width (widget);
	h = lrg_widget_get_height (widget);
	p = self->progress.value;

	/* Moving pages are cut to the switcher; a crossfade stays in place
	 * and keeps the pages' shadows */
	clip = self->active != LRG_SWITCHER_TRANSITION_CROSSFADE;
	if (clip)
		lrg_paint_list_push_clip (list, x, y, w, h);

	/* The page being left, underneath */
	mark = lrg_paint_list_begin_group (list);
	paint_hidden_page (self->previous, list);
	transition_params (self->active, self->reversed, FALSE, p, w, h, &scale, &dx, &dy, &alpha);
	lrg_paint_list_transform_group (list, mark, scale, x + w * 0.5f, y + h * 0.5f, dx, dy, alpha);

	/* The incoming page on top */
	mark = lrg_paint_list_begin_group (list);
	if (self->visible != NULL)
		lrg_widget_paint (self->visible, list);
	transition_params (self->active, self->reversed, TRUE, p, w, h, &scale, &dx, &dy, &alpha);
	lrg_paint_list_transform_group (list, mark, scale, x + w * 0.5f, y + h * 0.5f, dx, dy, alpha);

	if (clip)
		lrg_paint_list_pop_clip (list);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_switcher_dispose (GObject *object)
{
	LrgSwitcher *self = LRG_SWITCHER (object);

	self->visible = NULL;
	self->previous = NULL;
	if (self->pages != NULL)
		g_ptr_array_set_size (self->pages, 0);

	G_OBJECT_CLASS (lrg_switcher_parent_class)->dispose (object);
}

static void
lrg_switcher_finalize (GObject *object)
{
	LrgSwitcher *self = LRG_SWITCHER (object);

	g_clear_pointer (&self->pages, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_switcher_parent_class)->finalize (object);
}

static void
lrg_switcher_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgSwitcher *self = LRG_SWITCHER (object);

	switch (prop_id)
	{
	case PROP_VISIBLE_CHILD:
		g_value_set_object (value, self->visible);
		break;
	case PROP_VISIBLE_CHILD_NAME:
		g_value_set_string (value, lrg_switcher_get_visible_child_name (self));
		break;
	case PROP_VISIBLE_INDEX:
		g_value_set_int (value, lrg_switcher_get_visible_index (self));
		break;
	case PROP_TRANSITION:
		g_value_set_enum (value, self->transition);
		break;
	case PROP_HOMOGENEOUS:
		g_value_set_boolean (value, self->homogeneous);
		break;
	case PROP_INTERPOLATE_SIZE:
		g_value_set_boolean (value, self->interpolate_size);
		break;
	case PROP_TRANSITION_RUNNING:
		g_value_set_boolean (value, self->running);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_switcher_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgSwitcher *self = LRG_SWITCHER (object);

	switch (prop_id)
	{
	case PROP_VISIBLE_CHILD:
		lrg_switcher_set_visible_child (self, g_value_get_object (value));
		break;
	case PROP_VISIBLE_CHILD_NAME:
		lrg_switcher_set_visible_child_name (self, g_value_get_string (value));
		break;
	case PROP_VISIBLE_INDEX:
		lrg_switcher_set_visible_index (self, g_value_get_int (value));
		break;
	case PROP_TRANSITION:
		lrg_switcher_set_transition (self, g_value_get_enum (value));
		break;
	case PROP_HOMOGENEOUS:
		lrg_switcher_set_homogeneous (self, g_value_get_boolean (value));
		break;
	case PROP_INTERPOLATE_SIZE:
		lrg_switcher_set_interpolate_size (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_switcher_class_init (LrgSwitcherClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	name_quark = g_quark_from_static_string ("lrg-switcher-page-name");

	object_class->dispose = lrg_switcher_dispose;
	object_class->finalize = lrg_switcher_finalize;
	object_class->get_property = lrg_switcher_get_property;
	object_class->set_property = lrg_switcher_set_property;

	widget_class->measure = lrg_switcher_measure;
	widget_class->paint = lrg_switcher_paint;
	widget_class->step = lrg_switcher_step;
	container_class->layout_children = lrg_switcher_layout_children;

	/**
	 * LrgSwitcher:visible-child:
	 *
	 * The page shown, or being switched to during a transition.
	 */
	properties[PROP_VISIBLE_CHILD] =
		g_param_spec_object ("visible-child", "Visible child", "The page shown",
		                     LRG_TYPE_WIDGET,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSwitcher:visible-child-name:
	 *
	 * Name of the page shown. Setting it switches pages; this is the
	 * property the "ui.page" action sets.
	 */
	properties[PROP_VISIBLE_CHILD_NAME] =
		g_param_spec_string ("visible-child-name", "Visible child name", "Name of the page shown",
		                     NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSwitcher:visible-index:
	 *
	 * Position of the page shown among the children, -1 for none.
	 */
	properties[PROP_VISIBLE_INDEX] =
		g_param_spec_int ("visible-index", "Visible index", "Position of the page shown",
		                  -1, G_MAXINT, -1,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSwitcher:transition:
	 *
	 * How page switches animate.
	 */
	properties[PROP_TRANSITION] =
		g_param_spec_enum ("transition", "Transition", "How page switches animate",
		                   LRG_TYPE_SWITCHER_TRANSITION, LRG_SWITCHER_TRANSITION_CROSSFADE,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSwitcher:homogeneous:
	 *
	 * Request the size of the largest page instead of the visible one.
	 */
	properties[PROP_HOMOGENEOUS] =
		g_param_spec_boolean ("homogeneous", "Homogeneous", "Size for the largest page",
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSwitcher:interpolate-size:
	 *
	 * When not homogeneous, glide the requested size between pages on
	 * a spring.
	 */
	properties[PROP_INTERPOLATE_SIZE] =
		g_param_spec_boolean ("interpolate-size", "Interpolate size", "Animate the requested size",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSwitcher:transition-running:
	 *
	 * Whether a page transition is animating.
	 */
	properties[PROP_TRANSITION_RUNNING] =
		g_param_spec_boolean ("transition-running", "Transition running", "A transition is animating",
		                      FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_switcher_init (LrgSwitcher *self)
{
	self->pages = g_ptr_array_new_with_free_func (g_object_unref);
	self->transition = LRG_SWITCHER_TRANSITION_CROSSFADE;
	self->homogeneous = TRUE;
	lrg_spring_init (&self->progress, 1.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&self->size_w, 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&self->size_h, 0.0f, LRG_MOTION_SMOOTH);

	/* Like a plain stack: clicks on empty space reach what is beneath */
	lrg_widget_set_pointer_transparent (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgSwitcher *
lrg_switcher_new (void)
{
	return g_object_new (LRG_TYPE_SWITCHER, NULL);
}

void
lrg_switcher_add_named (LrgSwitcher *self,
                        LrgWidget   *child,
                        const gchar *name)
{
	g_return_if_fail (LRG_IS_SWITCHER (self));
	g_return_if_fail (LRG_IS_WIDGET (child));
	g_return_if_fail (lrg_widget_get_parent (child) == NULL);

	/* Name first: adding may make it the visible page and notify */
	g_object_set_qdata_full (G_OBJECT (child), name_quark, g_strdup (name), g_free);
	lrg_container_add_child (LRG_CONTAINER (self), child);
}

const gchar *
lrg_switcher_get_child_name (LrgSwitcher *self,
                             LrgWidget   *child)
{
	g_return_val_if_fail (LRG_IS_SWITCHER (self), NULL);
	g_return_val_if_fail (LRG_IS_WIDGET (child), NULL);

	return g_object_get_qdata (G_OBJECT (child), name_quark);
}

void
lrg_switcher_set_child_name (LrgSwitcher *self,
                             LrgWidget   *child,
                             const gchar *name)
{
	g_return_if_fail (LRG_IS_SWITCHER (self));
	g_return_if_fail (LRG_IS_WIDGET (child));
	g_return_if_fail (lrg_widget_get_parent (child) == LRG_CONTAINER (self));

	if (g_strcmp0 (name, g_object_get_qdata (G_OBJECT (child), name_quark)) == 0)
		return;
	g_object_set_qdata_full (G_OBJECT (child), name_quark, g_strdup (name), g_free);
	if (child == self->visible)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE_CHILD_NAME]);
}

LrgWidget *
lrg_switcher_get_child_by_name (LrgSwitcher *self,
                                const gchar *name)
{
	GList *l;

	g_return_val_if_fail (LRG_IS_SWITCHER (self), NULL);

	if (name == NULL)
		return NULL;
	for (l = lrg_container_get_children (LRG_CONTAINER (self)); l != NULL; l = l->next)
		if (g_strcmp0 (g_object_get_qdata (G_OBJECT (l->data), name_quark), name) == 0)
			return LRG_WIDGET (l->data);
	return NULL;
}

LrgWidget *
lrg_switcher_get_visible_child (LrgSwitcher *self)
{
	g_return_val_if_fail (LRG_IS_SWITCHER (self), NULL);
	return self->visible;
}

void
lrg_switcher_set_visible_child (LrgSwitcher *self,
                                LrgWidget   *child)
{
	g_return_if_fail (LRG_IS_SWITCHER (self));
	lrg_switcher_set_visible_child_full (self, child, self->transition);
}

void
lrg_switcher_set_visible_child_full (LrgSwitcher           *self,
                                     LrgWidget             *child,
                                     LrgSwitcherTransition  transition)
{
	g_return_if_fail (LRG_IS_SWITCHER (self));
	g_return_if_fail (LRG_IS_WIDGET (child));
	g_return_if_fail (lrg_widget_get_parent (child) == LRG_CONTAINER (self));

	switch_to (self, child, transition);
}

const gchar *
lrg_switcher_get_visible_child_name (LrgSwitcher *self)
{
	g_return_val_if_fail (LRG_IS_SWITCHER (self), NULL);

	if (self->visible == NULL)
		return NULL;
	return g_object_get_qdata (G_OBJECT (self->visible), name_quark);
}

void
lrg_switcher_set_visible_child_name (LrgSwitcher *self,
                                     const gchar *name)
{
	LrgWidget *child;

	g_return_if_fail (LRG_IS_SWITCHER (self));

	child = lrg_switcher_get_child_by_name (self, name);
	if (child == NULL)
	{
		lrg_warning (LRG_LOG_DOMAIN, "LrgSwitcher: no page named '%s'", name != NULL ? name : "(null)");
		return;
	}
	switch_to (self, child, self->transition);
}

gint
lrg_switcher_get_visible_index (LrgSwitcher *self)
{
	g_return_val_if_fail (LRG_IS_SWITCHER (self), -1);

	if (self->visible == NULL)
		return -1;
	return g_list_index (lrg_container_get_children (LRG_CONTAINER (self)), self->visible);
}

void
lrg_switcher_set_visible_index (LrgSwitcher *self,
                                gint         index)
{
	LrgWidget *child;

	g_return_if_fail (LRG_IS_SWITCHER (self));

	if (index < 0)
		return;
	child = lrg_container_get_child (LRG_CONTAINER (self), (guint) index);
	if (child != NULL)
		switch_to (self, child, self->transition);
}

LrgSwitcherTransition
lrg_switcher_get_transition (LrgSwitcher *self)
{
	g_return_val_if_fail (LRG_IS_SWITCHER (self), LRG_SWITCHER_TRANSITION_NONE);
	return self->transition;
}

void
lrg_switcher_set_transition (LrgSwitcher           *self,
                             LrgSwitcherTransition  transition)
{
	g_return_if_fail (LRG_IS_SWITCHER (self));

	if (self->transition == transition)
		return;
	self->transition = transition;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRANSITION]);
}

gboolean
lrg_switcher_get_homogeneous (LrgSwitcher *self)
{
	g_return_val_if_fail (LRG_IS_SWITCHER (self), TRUE);
	return self->homogeneous;
}

void
lrg_switcher_set_homogeneous (LrgSwitcher *self,
                              gboolean     homogeneous)
{
	g_return_if_fail (LRG_IS_SWITCHER (self));

	homogeneous = !!homogeneous;
	if (self->homogeneous == homogeneous)
		return;
	self->homogeneous = homogeneous;
	self->size_ready = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOMOGENEOUS]);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

gboolean
lrg_switcher_get_interpolate_size (LrgSwitcher *self)
{
	g_return_val_if_fail (LRG_IS_SWITCHER (self), FALSE);
	return self->interpolate_size;
}

void
lrg_switcher_set_interpolate_size (LrgSwitcher *self,
                                   gboolean     interpolate_size)
{
	g_return_if_fail (LRG_IS_SWITCHER (self));

	interpolate_size = !!interpolate_size;
	if (self->interpolate_size == interpolate_size)
		return;
	self->interpolate_size = interpolate_size;
	self->size_ready = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTERPOLATE_SIZE]);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

gboolean
lrg_switcher_get_transition_running (LrgSwitcher *self)
{
	g_return_val_if_fail (LRG_IS_SWITCHER (self), FALSE);
	return self->running;
}

gfloat
lrg_switcher_get_transition_progress (LrgSwitcher *self)
{
	g_return_val_if_fail (LRG_IS_SWITCHER (self), 1.0f);
	return self->running ? self->progress.value : 1.0f;
}

LrgWidget *
lrg_switcher_get_previous_child (LrgSwitcher *self)
{
	g_return_val_if_fail (LRG_IS_SWITCHER (self), NULL);
	return self->running ? self->previous : NULL;
}
