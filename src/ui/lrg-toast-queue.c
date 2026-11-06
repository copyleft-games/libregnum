/* lrg-toast-queue.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgToastQueue: Shows LrgToast messages in a corner, one at a time or stacked.
 *
 * Every shown toast lives in a slot with two springs:
 *
 *  - enter: 0 -> 1 as the card rises ~14 units and fades in; when the
 *    toast is dismissed it runs back to 0 (fade out in place) and the
 *    slot is removed once it is nearly invisible.
 *  - place: the card's distance from the anchor edge. Targets are
 *    recomputed whenever the set of slots changes, so the stack re-flows
 *    smoothly when a toast arrives or leaves.
 *
 * Time comes from lrg_toast_queue_advance(). With auto-advance on (the
 * default) the queue's step() calls it with the canvas frame time, so
 * toasts follow lrg_canvas_update() and the canvas reports itself busy
 * while a toast is entering, counting down, leaving or waiting. Explicit
 * lrg_toast_queue_advance() calls add to that (skip ahead); turn
 * auto-advance off to drive time only by hand. paint() never advances
 * anything, so drawing twice in a frame cannot double the clock.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-toast-queue.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* How far a toast rises while it enters */
#define TOAST_RISE (14.0f)

/* Below this the leaving card is gone */
#define TOAST_GONE (0.01f)

typedef struct
{
	LrgToast  *toast;     /* owned; also a child of the queue */
	LrgSpring  enter;     /* 0 hidden .. 1 shown */
	LrgSpring  place;     /* offset from the anchor edge */
	gboolean   placed;    /* place spring has a starting value */
	gboolean   leaving;
} ToastSlot;

struct _LrgToastQueue
{
	LrgContainer  parent_instance;

	LrgToastMode  mode;
	guint         max_visible;
	LrgAlign      halign;
	LrgAlign      valign;
	gboolean      auto_advance;

	GPtrArray    *slots;      /* ToastSlot *, oldest first */
	GQueue        pending;    /* LrgToast *, owned */
	gboolean      auto_fill;  /* sized from the parent */
};

G_DEFINE_FINAL_TYPE (LrgToastQueue, lrg_toast_queue, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_MODE,
	PROP_MAX_VISIBLE,
	PROP_TOAST_HALIGN,
	PROP_TOAST_VALIGN,
	PROP_AUTO_ADVANCE,
	PROP_N_VISIBLE,
	PROP_N_PENDING,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void on_toast_dismissed (LrgToast      *toast,
                                LrgToastQueue *self);

/* ==========================================================================
 * Slots
 * ========================================================================== */

/* release_toast: stop listening to @toast and drop our reference */
static void
release_toast (LrgToastQueue *self,
               LrgToast      *toast)
{
	g_signal_handlers_disconnect_by_func (toast, G_CALLBACK (on_toast_dismissed), self);
	g_object_unref (toast);
}

/* motion: the queue's spring preset */
static LrgMotion
motion (LrgToastQueue *self)
{
	return lrg_widget_get_motion (LRG_WIDGET (self));
}

/* find_slot: the slot showing @toast, or -1 */
static gint
find_slot (LrgToastQueue *self,
           LrgToast      *toast)
{
	guint i;

	for (i = 0; i < self->slots->len; i++)
		if (((ToastSlot *) g_ptr_array_index (self->slots, i))->toast == toast)
			return (gint) i;
	return -1;
}

/* count_live: slots that are not leaving */
static guint
count_live (LrgToastQueue *self)
{
	guint i;
	guint n = 0;

	for (i = 0; i < self->slots->len; i++)
		if (!((ToastSlot *) g_ptr_array_index (self->slots, i))->leaving)
			n++;
	return n;
}

/* ensure_fill: with no size of its own, cover the parent */
static void
ensure_fill (LrgToastQueue *self)
{
	LrgWidget *widget = LRG_WIDGET (self);
	LrgWidget *parent = (LrgWidget *) lrg_widget_get_parent (widget);
	gfloat     pw;
	gfloat     ph;

	if (parent == NULL)
		return;
	if (!self->auto_fill && lrg_widget_get_width (widget) > 0.0f && lrg_widget_get_height (widget) > 0.0f)
		return;

	self->auto_fill = TRUE;
	pw = lrg_widget_get_width (parent);
	ph = lrg_widget_get_height (parent);
	if (lrg_widget_get_x (widget) != 0.0f || lrg_widget_get_y (widget) != 0.0f)
		lrg_widget_set_position (widget, 0.0f, 0.0f);
	if (lrg_widget_get_width (widget) != pw || lrg_widget_get_height (widget) != ph)
		lrg_widget_set_size (widget, pw, ph);
}

/*
 * reflow:
 *
 * Gives every slot its place target: newest nearest the anchor edge,
 * older ones further out. Leaving cards keep their slot in the stack
 * (they fade where they are) except in replace mode, where the old and
 * new card cross-fade at the same spot. New slots snap to their target
 * so only the enter spring moves them.
 */
static void
reflow (LrgToastQueue *self)
{
	gfloat spacing = lrg_container_get_spacing (LRG_CONTAINER (self));
	gfloat offset = 0.0f;
	gint   i;

	for (i = (gint) self->slots->len - 1; i >= 0; i--)
	{
		ToastSlot *slot = g_ptr_array_index (self->slots, (guint) i);
		gfloat     h;

		if (slot->leaving && self->mode == LRG_TOAST_MODE_REPLACE)
			continue;

		lrg_widget_get_preferred_size (LRG_WIDGET (slot->toast), NULL, &h);
		if (!slot->placed)
		{
			lrg_spring_init (&slot->place, offset, motion (self));
			slot->placed = TRUE;
		}
		lrg_spring_set_target (&slot->place, offset);
		offset += h + spacing;
	}
}

/*
 * position_toasts:
 *
 * Allocates every card from its place spring: horizontally by the
 * toast halign inside the padding, vertically from the anchor edge.
 */
static void
position_toasts (LrgToastQueue *self)
{
	LrgWidget *widget = LRG_WIDGET (self);
	gfloat     pad = lrg_container_get_padding (LRG_CONTAINER (self));
	gfloat     spacing = lrg_container_get_spacing (LRG_CONTAINER (self));
	gfloat     qw = lrg_widget_get_width (widget);
	gfloat     qh = lrg_widget_get_height (widget);
	gfloat     total = 0.0f;
	guint      i;

	/* Centred stacks need their total height */
	if (self->valign == LRG_ALIGN_CENTER)
		for (i = 0; i < self->slots->len; i++)
		{
			ToastSlot *slot = g_ptr_array_index (self->slots, i);
			gfloat     h;

			if (slot->leaving && self->mode == LRG_TOAST_MODE_REPLACE)
				continue;
			lrg_widget_get_preferred_size (LRG_WIDGET (slot->toast), NULL, &h);
			total += h + (total > 0.0f ? spacing : 0.0f);
		}

	for (i = 0; i < self->slots->len; i++)
	{
		ToastSlot *slot = g_ptr_array_index (self->slots, i);
		gfloat     w;
		gfloat     h;
		gfloat     x;
		gfloat     y;

		lrg_widget_get_preferred_size (LRG_WIDGET (slot->toast), &w, &h);
		if (self->halign == LRG_ALIGN_FILL)
			w = MAX (qw - pad * 2.0f, 0.0f);
		else
			w = MIN (w, MAX (qw - pad * 2.0f, 0.0f));

		switch (self->halign)
		{
		case LRG_ALIGN_START:
		case LRG_ALIGN_FILL:
			x = pad;
			break;
		case LRG_ALIGN_CENTER:
			x = (qw - w) * 0.5f;
			break;
		case LRG_ALIGN_END:
		default:
			x = qw - pad - w;
			break;
		}

		switch (self->valign)
		{
		case LRG_ALIGN_START:
			y = pad + slot->place.value;
			break;
		case LRG_ALIGN_CENTER:
			y = (qh - total) * 0.5f + slot->place.value;
			break;
		case LRG_ALIGN_END:
		case LRG_ALIGN_FILL:
		default:
			y = qh - pad - h - slot->place.value;
			break;
		}

		lrg_widget_allocate (LRG_WIDGET (slot->toast), roundf (x), roundf (y), roundf (w), roundf (h));
	}
}

/* add_slot: put @toast (already referenced by us) on screen */
static void
add_slot (LrgToastQueue *self,
          LrgToast      *toast)
{
	ToastSlot *slot = g_new0 (ToastSlot, 1);

	slot->toast = toast;
	lrg_spring_init (&slot->enter, 0.0f, motion (self));
	lrg_spring_set_target (&slot->enter, 1.0f);
	if (motion (self) == LRG_MOTION_NONE)
		lrg_spring_snap (&slot->enter, 1.0f);
	g_ptr_array_add (self->slots, slot);

	lrg_widget_set_pointer_transparent (LRG_WIDGET (toast), FALSE);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (toast));
	reflow (self);
	position_toasts (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_VISIBLE]);
}

/* begin_leave: fade the slot out where it is */
static void
begin_leave (LrgToastQueue *self,
             ToastSlot     *slot)
{
	if (slot->leaving)
		return;
	slot->leaving = TRUE;
	lrg_spring_set_target (&slot->enter, 0.0f);
	if (motion (self) == LRG_MOTION_NONE)
		lrg_spring_snap (&slot->enter, 0.0f);

	/* Clicks pass through a card on its way out */
	lrg_widget_set_pointer_transparent (LRG_WIDGET (slot->toast), TRUE);
	if (self->mode == LRG_TOAST_MODE_REPLACE)
		reflow (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_VISIBLE]);
}

/* remove_slot: take slot @index off screen for good */
static void
remove_slot (LrgToastQueue *self,
             guint          index)
{
	ToastSlot *slot = g_ptr_array_index (self->slots, index);
	gboolean   live = !slot->leaving;

	g_ptr_array_remove_index (self->slots, index);
	lrg_container_remove_child (LRG_CONTAINER (self), LRG_WIDGET (slot->toast));
	release_toast (self, slot->toast);
	g_free (slot);
	if (live)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_VISIBLE]);
}

/* replace_with: replace mode; everything showing leaves, @toast enters */
static void
replace_with (LrgToastQueue *self,
              LrgToast      *toast)
{
	guint n = self->slots->len;
	guint i;

	/* Old cards leave first, so they keep their spot while @toast takes
	 * the front place over them */
	for (i = 0; i < n && i < self->slots->len; i++)
	{
		ToastSlot *slot = g_ptr_array_index (self->slots, i);

		if (!slot->leaving)
			lrg_toast_dismiss (slot->toast);
	}
	add_slot (self, toast);
}

/*
 * promote_pending:
 *
 * Brings waiting toasts on screen as the mode allows. Returns TRUE if
 * anything was shown.
 */
static gboolean
promote_pending (LrgToastQueue *self)
{
	gboolean shown = FALSE;

	while (!g_queue_is_empty (&self->pending))
	{
		LrgToast *next;

		switch (self->mode)
		{
		case LRG_TOAST_MODE_SEQUENCE:
			/* Strictly one at a time: wait for the last to vanish */
			if (self->slots->len > 0)
				return shown;
			break;
		case LRG_TOAST_MODE_STACK:
			if (count_live (self) >= self->max_visible)
				return shown;
			break;
		case LRG_TOAST_MODE_REPLACE:
		default:
			break;
		}

		next = g_queue_pop_head (&self->pending);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_PENDING]);
		if (self->mode == LRG_TOAST_MODE_REPLACE)
			replace_with (self, next);
		else
			add_slot (self, next);
		shown = TRUE;
	}
	return shown;
}

/* on_toast_dismissed: a toast expired, was clicked or dismissed */
static void
on_toast_dismissed (LrgToast      *toast,
                    LrgToastQueue *self)
{
	GList *link = g_queue_find (&self->pending, toast);
	gint   index;

	/* Dismissed before its turn: just drop it */
	if (link != NULL)
	{
		g_queue_delete_link (&self->pending, link);
		release_toast (self, toast);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_PENDING]);
		return;
	}

	index = find_slot (self, toast);
	if (index >= 0)
		begin_leave (self, g_ptr_array_index (self->slots, (guint) index));
}

/* ==========================================================================
 * Widget vfuncs
 * ========================================================================== */

static void
lrg_toast_queue_layout_children (LrgContainer *container)
{
	position_toasts (LRG_TOAST_QUEUE (container));
}

static void
lrg_toast_queue_measure (LrgWidget *widget,
                         gfloat    *width,
                         gfloat    *height)
{
	/* A full-size layer: it takes whatever it is given */
	*width = lrg_widget_get_width (widget);
	*height = lrg_widget_get_height (widget);
}

/*
 * is_busy:
 *
 * Whether the queue still needs frames: a spring moving, a visible
 * toast whose timer is counting down (a toast without a duration only
 * waits for a click), a card fading out, or toasts waiting their turn.
 */
static gboolean
is_busy (LrgToastQueue *self)
{
	guint i;

	if (!g_queue_is_empty (&self->pending))
		return TRUE;

	for (i = 0; i < self->slots->len; i++)
	{
		ToastSlot *slot = g_ptr_array_index (self->slots, i);

		if (slot->leaving || !lrg_spring_is_settled (&slot->enter) ||
		    !lrg_spring_is_settled (&slot->place))
			return TRUE;
		if (lrg_toast_get_duration (slot->toast) > 0.0)
			return TRUE;
	}
	return FALSE;
}

/*
 * lrg_toast_queue_step:
 *
 * Auto-advance: runs the toast clocks and springs by the canvas frame
 * time. With auto-advance off nothing moves here (time only passes
 * through lrg_toast_queue_advance()), so the queue never reports
 * itself busy.
 *
 * Returns: %TRUE while a toast animates, counts down or waits
 */
static gboolean
lrg_toast_queue_step (LrgWidget *widget,
                      gfloat     delta_time)
{
	LrgToastQueue *self = LRG_TOAST_QUEUE (widget);

	if (!self->auto_advance)
		return FALSE;

	lrg_toast_queue_advance (self, (gdouble) delta_time);
	return is_busy (self);
}

static void
lrg_toast_queue_paint (LrgWidget    *widget,
                       LrgPaintList *list)
{
	LrgToastQueue *self = LRG_TOAST_QUEUE (widget);
	gfloat         dir;
	guint          i;

	ensure_fill (self);

	/* Entering cards rise towards their edge: up from below for bottom
	 * stacks, down from above for top stacks */
	dir = self->valign == LRG_ALIGN_START ? -1.0f : 1.0f;

	for (i = 0; i < self->slots->len; i++)
	{
		ToastSlot *slot = g_ptr_array_index (self->slots, i);
		LrgWidget *toast = LRG_WIDGET (slot->toast);
		gfloat     e = slot->enter.value;
		gfloat     alpha = CLAMP (e, 0.0f, 1.0f);
		gfloat     dy = slot->leaving ? 0.0f : (1.0f - e) * TOAST_RISE * dir;
		gfloat     scale = slot->leaving ? 0.96f + 0.04f * alpha : 1.0f;
		guint      mark;

		if (alpha <= 0.0f)
			continue;
		mark = lrg_paint_list_begin_group (list);
		lrg_widget_paint (toast, list);
		lrg_paint_list_transform_group (list, mark, scale,
		                                lrg_widget_get_world_x (toast) + lrg_widget_get_width (toast) * 0.5f,
		                                lrg_widget_get_world_y (toast) + lrg_widget_get_height (toast) * 0.5f,
		                                0.0f, dy, alpha);
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_toast_queue_dispose (GObject *object)
{
	LrgToastQueue *self = LRG_TOAST_QUEUE (object);
	LrgToast      *toast;

	if (self->slots != NULL)
		while (self->slots->len > 0)
			remove_slot (self, self->slots->len - 1);
	while ((toast = g_queue_pop_head (&self->pending)) != NULL)
		release_toast (self, toast);

	G_OBJECT_CLASS (lrg_toast_queue_parent_class)->dispose (object);
}

static void
lrg_toast_queue_finalize (GObject *object)
{
	LrgToastQueue *self = LRG_TOAST_QUEUE (object);

	g_clear_pointer (&self->slots, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_toast_queue_parent_class)->finalize (object);
}

static void
lrg_toast_queue_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgToastQueue *self = LRG_TOAST_QUEUE (object);

	switch (prop_id)
	{
	case PROP_MODE:
		g_value_set_enum (value, self->mode);
		break;
	case PROP_MAX_VISIBLE:
		g_value_set_uint (value, self->max_visible);
		break;
	case PROP_TOAST_HALIGN:
		g_value_set_enum (value, self->halign);
		break;
	case PROP_TOAST_VALIGN:
		g_value_set_enum (value, self->valign);
		break;
	case PROP_AUTO_ADVANCE:
		g_value_set_boolean (value, self->auto_advance);
		break;
	case PROP_N_VISIBLE:
		g_value_set_uint (value, count_live (self));
		break;
	case PROP_N_PENDING:
		g_value_set_uint (value, self->pending.length);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_toast_queue_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgToastQueue *self = LRG_TOAST_QUEUE (object);

	switch (prop_id)
	{
	case PROP_MODE:
		lrg_toast_queue_set_mode (self, (LrgToastMode) g_value_get_enum (value));
		break;
	case PROP_MAX_VISIBLE:
		lrg_toast_queue_set_max_visible (self, g_value_get_uint (value));
		break;
	case PROP_TOAST_HALIGN:
		lrg_toast_queue_set_toast_halign (self, (LrgAlign) g_value_get_enum (value));
		break;
	case PROP_TOAST_VALIGN:
		lrg_toast_queue_set_toast_valign (self, (LrgAlign) g_value_get_enum (value));
		break;
	case PROP_AUTO_ADVANCE:
		lrg_toast_queue_set_auto_advance (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_toast_queue_class_init (LrgToastQueueClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_toast_queue_dispose;
	object_class->finalize = lrg_toast_queue_finalize;
	object_class->get_property = lrg_toast_queue_get_property;
	object_class->set_property = lrg_toast_queue_set_property;

	widget_class->measure = lrg_toast_queue_measure;
	widget_class->paint = lrg_toast_queue_paint;
	widget_class->step = lrg_toast_queue_step;

	container_class->layout_children = lrg_toast_queue_layout_children;

	/**
	 * LrgToastQueue:mode:
	 *
	 * How new toasts are shown: replacing the current one, one after
	 * another, or stacked.
	 */
	properties[PROP_MODE] =
		g_param_spec_enum ("mode", NULL, NULL, LRG_TYPE_TOAST_MODE, LRG_TOAST_MODE_STACK,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgToastQueue:max-visible:
	 *
	 * Most toasts on screen at once in stack mode.
	 */
	properties[PROP_MAX_VISIBLE] =
		g_param_spec_uint ("max-visible", NULL, NULL, 1, G_MAXUINT, 3,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgToastQueue:toast-halign:
	 *
	 * Horizontal placement of the cards.
	 */
	properties[PROP_TOAST_HALIGN] =
		g_param_spec_enum ("toast-halign", NULL, NULL, LRG_TYPE_ALIGN, LRG_ALIGN_END,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgToastQueue:toast-valign:
	 *
	 * Vertical edge the stack grows from.
	 */
	properties[PROP_TOAST_VALIGN] =
		g_param_spec_enum ("toast-valign", NULL, NULL, LRG_TYPE_ALIGN, LRG_ALIGN_END,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgToastQueue:auto-advance:
	 *
	 * Whether the canvas frame clock (lrg_canvas_update(), through the
	 * widget's step) advances the toasts. When %FALSE, time only
	 * passes through lrg_toast_queue_advance().
	 */
	properties[PROP_AUTO_ADVANCE] =
		g_param_spec_boolean ("auto-advance", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgToastQueue:n-visible:
	 *
	 * Toasts on screen that are not on their way out.
	 */
	properties[PROP_N_VISIBLE] =
		g_param_spec_uint ("n-visible", NULL, NULL, 0, G_MAXUINT, 0,
		                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgToastQueue:n-pending:
	 *
	 * Toasts waiting for their turn.
	 */
	properties[PROP_N_PENDING] =
		g_param_spec_uint ("n-pending", NULL, NULL, 0, G_MAXUINT, 0,
		                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_toast_queue_init (LrgToastQueue *self)
{
	self->mode = LRG_TOAST_MODE_STACK;
	self->max_visible = 3;
	self->halign = LRG_ALIGN_END;
	self->valign = LRG_ALIGN_END;
	self->auto_advance = TRUE;
	self->slots = g_ptr_array_new ();
	g_queue_init (&self->pending);

	/* A see-through layer: only the cards catch the pointer */
	lrg_widget_set_pointer_transparent (LRG_WIDGET (self), TRUE);
	lrg_container_set_spacing (LRG_CONTAINER (self), 10.0f);
	lrg_container_set_padding (LRG_CONTAINER (self), 16.0f);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgToastQueue *
lrg_toast_queue_new (void)
{
	return g_object_new (LRG_TYPE_TOAST_QUEUE, NULL);
}

LrgToastMode
lrg_toast_queue_get_mode (LrgToastQueue *self)
{
	g_return_val_if_fail (LRG_IS_TOAST_QUEUE (self), LRG_TOAST_MODE_STACK);

	return self->mode;
}

void
lrg_toast_queue_set_mode (LrgToastQueue *self,
                          LrgToastMode   mode)
{
	g_return_if_fail (LRG_IS_TOAST_QUEUE (self));

	if (self->mode == mode)
		return;
	self->mode = mode;
	reflow (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
}

guint
lrg_toast_queue_get_max_visible (LrgToastQueue *self)
{
	g_return_val_if_fail (LRG_IS_TOAST_QUEUE (self), 3);

	return self->max_visible;
}

void
lrg_toast_queue_set_max_visible (LrgToastQueue *self,
                                 guint          max_visible)
{
	g_return_if_fail (LRG_IS_TOAST_QUEUE (self));

	max_visible = MAX (max_visible, 1u);
	if (self->max_visible == max_visible)
		return;
	self->max_visible = max_visible;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_VISIBLE]);
}

LrgAlign
lrg_toast_queue_get_toast_halign (LrgToastQueue *self)
{
	g_return_val_if_fail (LRG_IS_TOAST_QUEUE (self), LRG_ALIGN_END);

	return self->halign;
}

void
lrg_toast_queue_set_toast_halign (LrgToastQueue *self,
                                  LrgAlign       align)
{
	g_return_if_fail (LRG_IS_TOAST_QUEUE (self));

	if (self->halign == align)
		return;
	self->halign = align;
	position_toasts (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TOAST_HALIGN]);
}

LrgAlign
lrg_toast_queue_get_toast_valign (LrgToastQueue *self)
{
	g_return_val_if_fail (LRG_IS_TOAST_QUEUE (self), LRG_ALIGN_END);

	return self->valign;
}

void
lrg_toast_queue_set_toast_valign (LrgToastQueue *self,
                                  LrgAlign       align)
{
	g_return_if_fail (LRG_IS_TOAST_QUEUE (self));

	if (self->valign == align)
		return;
	self->valign = align;
	position_toasts (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TOAST_VALIGN]);
}

gboolean
lrg_toast_queue_get_auto_advance (LrgToastQueue *self)
{
	g_return_val_if_fail (LRG_IS_TOAST_QUEUE (self), TRUE);

	return self->auto_advance;
}

void
lrg_toast_queue_set_auto_advance (LrgToastQueue *self,
                                  gboolean       auto_advance)
{
	g_return_if_fail (LRG_IS_TOAST_QUEUE (self));

	auto_advance = !!auto_advance;
	if (self->auto_advance == auto_advance)
		return;
	self->auto_advance = auto_advance;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTO_ADVANCE]);
}

void
lrg_toast_queue_show (LrgToastQueue *self,
                      LrgToast      *toast)
{
	g_return_if_fail (LRG_IS_TOAST_QUEUE (self));
	g_return_if_fail (LRG_IS_TOAST (toast));
	g_return_if_fail (lrg_widget_get_parent (LRG_WIDGET (toast)) == NULL);
	g_return_if_fail (g_queue_find (&self->pending, toast) == NULL);

	if (lrg_toast_get_dismissed (toast))
		return;

	ensure_fill (self);
	g_object_ref (toast);
	g_signal_connect (toast, "dismissed", G_CALLBACK (on_toast_dismissed), self);

	switch (self->mode)
	{
	case LRG_TOAST_MODE_REPLACE:
		replace_with (self, toast);
		break;

	case LRG_TOAST_MODE_SEQUENCE:
		if (self->slots->len == 0 && g_queue_is_empty (&self->pending))
			add_slot (self, toast);
		else
		{
			g_queue_push_tail (&self->pending, toast);
			g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_PENDING]);
		}
		break;

	case LRG_TOAST_MODE_STACK:
	default:
		if (count_live (self) < self->max_visible && g_queue_is_empty (&self->pending))
			add_slot (self, toast);
		else
		{
			g_queue_push_tail (&self->pending, toast);
			g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_PENDING]);
		}
		break;
	}
}

void
lrg_toast_queue_advance (LrgToastQueue *self,
                         gdouble        seconds)
{
	gboolean changed = FALSE;
	guint    i;
	gfloat   dt;

	g_return_if_fail (LRG_IS_TOAST_QUEUE (self));

	seconds = MAX (seconds, 0.0);
	dt = (gfloat) seconds;
	g_object_ref (self);
	ensure_fill (self);

	/* 1. Clocks: expiring toasts dismiss themselves, which only flags
	 *    their slot as leaving (the array is not edited here) */
	for (i = 0; i < self->slots->len; i++)
	{
		ToastSlot *slot = g_ptr_array_index (self->slots, i);

		if (!slot->leaving)
			lrg_toast_advance (slot->toast, seconds);
	}

	/* 2. Springs */
	for (i = 0; i < self->slots->len; i++)
	{
		ToastSlot *slot = g_ptr_array_index (self->slots, i);

		lrg_spring_set_motion (&slot->enter, motion (self));
		lrg_spring_set_motion (&slot->place, motion (self));
		if (motion (self) == LRG_MOTION_NONE)
		{
			lrg_spring_snap (&slot->enter, slot->enter.target);
			lrg_spring_snap (&slot->place, slot->place.target);
		}
		else
		{
			lrg_spring_step (&slot->enter, dt);
			lrg_spring_step (&slot->place, dt);
		}
	}

	/* 3. Faded-out cards leave for good */
	for (i = 0; i < self->slots->len;)
	{
		ToastSlot *slot = g_ptr_array_index (self->slots, i);

		if (slot->leaving && slot->enter.value <= TOAST_GONE)
		{
			remove_slot (self, i);
			changed = TRUE;
			continue;
		}
		i++;
	}

	/* 4. Waiting toasts take free places; the stack re-flows */
	changed |= promote_pending (self);
	if (changed)
		reflow (self);

	position_toasts (self);
	g_object_unref (self);
}

void
lrg_toast_queue_clear (LrgToastQueue *self)
{
	g_autoptr(GPtrArray) all = g_ptr_array_new_with_free_func (g_object_unref);
	GList               *l;
	guint                i;

	g_return_if_fail (LRG_IS_TOAST_QUEUE (self));

	/* Snapshot first: dismissing edits the lists */
	for (l = self->pending.head; l != NULL; l = l->next)
		g_ptr_array_add (all, g_object_ref (l->data));
	for (i = 0; i < self->slots->len; i++)
		g_ptr_array_add (all, g_object_ref (((ToastSlot *) g_ptr_array_index (self->slots, i))->toast));

	for (i = 0; i < all->len; i++)
		lrg_toast_dismiss (g_ptr_array_index (all, i));
}

guint
lrg_toast_queue_get_n_visible (LrgToastQueue *self)
{
	g_return_val_if_fail (LRG_IS_TOAST_QUEUE (self), 0);

	return count_live (self);
}

guint
lrg_toast_queue_get_n_pending (LrgToastQueue *self)
{
	g_return_val_if_fail (LRG_IS_TOAST_QUEUE (self), 0);

	return self->pending.length;
}

LrgToast *
lrg_toast_queue_get_visible_toast (LrgToastQueue *self,
                                   guint          index)
{
	guint i;

	g_return_val_if_fail (LRG_IS_TOAST_QUEUE (self), NULL);

	for (i = 0; i < self->slots->len; i++)
	{
		ToastSlot *slot = g_ptr_array_index (self->slots, i);

		if (slot->leaving)
			continue;
		if (index == 0)
			return slot->toast;
		index--;
	}
	return NULL;
}
