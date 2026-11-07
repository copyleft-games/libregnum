/* lrg-drop-target.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Drag and drop, drop side: the LrgDropTarget controller. It stores the
 * accepted content types and actions and emits accept / enter / motion
 * / leave / drop when the canvas session (lrg-canvas.c) asks it to.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-drop-target.h"
#include "lrg-canvas-private.h"
#include "../lrg-log.h"

struct _LrgDropTarget
{
	GObject          parent_instance;

	GStrv            types;             /* owned, NULL = anything */
	LrgDragAction    actions;
	LrgDragAction    preferred_action;
	gboolean         paint_highlight;
	LrgWidget       *widget;            /* weak */

	/* Session state, maintained by the canvas through the hooks below */
	LrgDragPayload  *current_drop;      /* owned while hovering */
	LrgDragAction    current_action;
	LrgDragAction    proposed_action;   /* for the motion class handler */
};

G_DEFINE_FINAL_TYPE (LrgDropTarget, lrg_drop_target, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_TYPES,
	PROP_ACTIONS,
	PROP_PREFERRED_ACTION,
	PROP_PAINT_HIGHLIGHT,
	PROP_CURRENT_DROP,
	PROP_CURRENT_ACTION,
	PROP_WIDGET,
	N_PROPS
};

enum
{
	SIGNAL_ACCEPT,
	SIGNAL_ENTER,
	SIGNAL_MOTION,
	SIGNAL_LEAVE,
	SIGNAL_DROP,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* qdata key for the GPtrArray of targets a widget owns */
static GQuark
drop_targets_quark (void)
{
	static GQuark q = 0;

	if (G_UNLIKELY (q == 0))
		q = g_quark_from_static_string ("lrg-drop-targets");
	return q;
}

/*
 * Class handlers. They run after handlers connected with
 * g_signal_connect() (the signals are RUN_LAST) and, with the
 * first-wins accumulator, only decide the result when no user handler
 * ran: accept says yes, motion takes the proposed action, drop says no.
 */
static gboolean
accept_default (LrgDropTarget  *self,
                LrgDragPayload *payload)
{
	return TRUE;
}

static LrgDragAction
motion_default (LrgDropTarget  *self,
                LrgDragPayload *payload,
                gfloat          x,
                gfloat          y)
{
	return self->proposed_action;
}

static gboolean
drop_default (LrgDropTarget  *self,
              LrgDragPayload *payload,
              gfloat          x,
              gfloat          y)
{
	return FALSE;
}

static void
lrg_drop_target_dispose (GObject *object)
{
	LrgDropTarget *self = LRG_DROP_TARGET (object);

	g_clear_pointer (&self->current_drop, lrg_drag_payload_unref);
	g_clear_weak_pointer (&self->widget);

	G_OBJECT_CLASS (lrg_drop_target_parent_class)->dispose (object);
}

static void
lrg_drop_target_finalize (GObject *object)
{
	LrgDropTarget *self = LRG_DROP_TARGET (object);

	g_clear_pointer (&self->types, g_strfreev);

	G_OBJECT_CLASS (lrg_drop_target_parent_class)->finalize (object);
}

static void
lrg_drop_target_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgDropTarget *self = LRG_DROP_TARGET (object);

	switch (prop_id)
	{
	case PROP_TYPES:
		g_value_set_boxed (value, self->types);
		break;
	case PROP_ACTIONS:
		g_value_set_flags (value, self->actions);
		break;
	case PROP_PREFERRED_ACTION:
		g_value_set_flags (value, self->preferred_action);
		break;
	case PROP_PAINT_HIGHLIGHT:
		g_value_set_boolean (value, self->paint_highlight);
		break;
	case PROP_CURRENT_DROP:
		g_value_set_boxed (value, self->current_drop);
		break;
	case PROP_CURRENT_ACTION:
		g_value_set_flags (value, self->current_action);
		break;
	case PROP_WIDGET:
		g_value_set_object (value, self->widget);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_drop_target_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgDropTarget *self = LRG_DROP_TARGET (object);

	switch (prop_id)
	{
	case PROP_TYPES:
		lrg_drop_target_set_types (self, g_value_get_boxed (value));
		break;
	case PROP_ACTIONS:
		lrg_drop_target_set_actions (self, (LrgDragAction) g_value_get_flags (value));
		break;
	case PROP_PREFERRED_ACTION:
		lrg_drop_target_set_preferred_action (self, (LrgDragAction) g_value_get_flags (value));
		break;
	case PROP_PAINT_HIGHLIGHT:
		lrg_drop_target_set_paint_highlight (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_drop_target_class_init (LrgDropTargetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_drop_target_dispose;
	object_class->finalize = lrg_drop_target_finalize;
	object_class->get_property = lrg_drop_target_get_property;
	object_class->set_property = lrg_drop_target_set_property;

	/**
	 * LrgDropTarget:types:
	 *
	 * Content types the target takes; "major/&ast;" matches any subtype and
	 * "&ast;" anything. %NULL (the default) takes every type.
	 */
	properties[PROP_TYPES] =
		g_param_spec_boxed ("types", NULL, NULL, G_TYPE_STRV,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDropTarget:actions:
	 *
	 * Actions the target supports. A drop needs at least one action in
	 * common with the payload.
	 */
	properties[PROP_ACTIONS] =
		g_param_spec_flags ("actions", NULL, NULL, LRG_TYPE_DRAG_ACTION, LRG_DRAG_ACTION_ALL,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDropTarget:preferred-action:
	 *
	 * The action used when no modifier key is held, %LRG_DRAG_ACTION_NONE
	 * for the built-in order (MOVE, COPY, LINK).
	 */
	properties[PROP_PREFERRED_ACTION] =
		g_param_spec_flags ("preferred-action", NULL, NULL, LRG_TYPE_DRAG_ACTION,
		                    LRG_DRAG_ACTION_NONE,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDropTarget:paint-highlight:
	 *
	 * Whether the canvas outlines the widget in the theme's accent colour
	 * while a drop onto it would be accepted. Turn it off to paint custom
	 * feedback from #LrgDropTarget:current-drop.
	 */
	properties[PROP_PAINT_HIGHLIGHT] =
		g_param_spec_boolean ("paint-highlight", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDropTarget:current-drop:
	 *
	 * The payload hovering over the target (between
	 * #LrgDropTarget::enter and #LrgDropTarget::leave), or %NULL.
	 */
	properties[PROP_CURRENT_DROP] =
		g_param_spec_boxed ("current-drop", NULL, NULL, LRG_TYPE_DRAG_PAYLOAD,
		                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDropTarget:current-action:
	 *
	 * The action the hovering drag would perform, as settled by the last
	 * #LrgDropTarget::motion; %LRG_DRAG_ACTION_NONE when it would be
	 * refused or nothing hovers.
	 */
	properties[PROP_CURRENT_ACTION] =
		g_param_spec_flags ("current-action", NULL, NULL, LRG_TYPE_DRAG_ACTION,
		                    LRG_DRAG_ACTION_NONE,
		                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDropTarget:widget:
	 *
	 * The widget the target is attached to.
	 */
	properties[PROP_WIDGET] =
		g_param_spec_object ("widget", NULL, NULL, LRG_TYPE_WIDGET,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgDropTarget::accept:
	 * @target: the drop target
	 * @payload: the dragged payload (its type already matched)
	 *
	 * Emitted when a matching drag first reaches the target. Return
	 * %FALSE to veto it: the session then looks further up the widget
	 * tree. The first handler's answer wins; without handlers the
	 * target accepts.
	 *
	 * Returns: %TRUE to accept the payload
	 */
	signals[SIGNAL_ACCEPT] =
		g_signal_new_class_handler ("accept", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		                            G_CALLBACK (accept_default), g_signal_accumulator_first_wins,
		                            NULL, NULL, G_TYPE_BOOLEAN, 1,
		                            LRG_TYPE_DRAG_PAYLOAD | G_SIGNAL_TYPE_STATIC_SCOPE);

	/**
	 * LrgDropTarget::enter:
	 * @target: the drop target
	 * @payload: the dragged payload
	 * @x: pointer X (canvas coordinates)
	 * @y: pointer Y (canvas coordinates)
	 *
	 * Emitted when an accepted drag moves onto the target. A
	 * #LrgDropTarget::motion follows at once.
	 */
	signals[SIGNAL_ENTER] =
		g_signal_new ("enter", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 3,
		              LRG_TYPE_DRAG_PAYLOAD | G_SIGNAL_TYPE_STATIC_SCOPE,
		              G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgDropTarget::motion:
	 * @target: the drop target
	 * @payload: the dragged payload
	 * @x: pointer X (canvas coordinates)
	 * @y: pointer Y (canvas coordinates)
	 *
	 * Emitted after #LrgDropTarget::enter and whenever the pointer or
	 * the modifier keys change while over the target. Return the action
	 * a drop here would perform, usually
	 * lrg_drop_target_get_proposed_action(), or %LRG_DRAG_ACTION_NONE
	 * to refuse a drop at this spot (a full inventory slot). The first
	 * handler's answer wins; without handlers the proposed action is
	 * used.
	 *
	 * Returns: the action for a drop at (@x, @y)
	 */
	signals[SIGNAL_MOTION] =
		g_signal_new_class_handler ("motion", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		                            G_CALLBACK (motion_default), g_signal_accumulator_first_wins,
		                            NULL, NULL, LRG_TYPE_DRAG_ACTION, 3,
		                            LRG_TYPE_DRAG_PAYLOAD | G_SIGNAL_TYPE_STATIC_SCOPE,
		                            G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgDropTarget::leave:
	 * @target: the drop target
	 *
	 * Emitted when the drag leaves the target, is cancelled, or right
	 * after #LrgDropTarget::drop. #LrgDropTarget:current-drop is %NULL
	 * again afterwards.
	 */
	signals[SIGNAL_LEAVE] =
		g_signal_new ("leave", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgDropTarget::drop:
	 * @target: the drop target
	 * @payload: the dropped payload
	 * @x: pointer X (canvas coordinates)
	 * @y: pointer Y (canvas coordinates)
	 *
	 * Emitted when the button is released over the target and the last
	 * #LrgDropTarget::motion settled on an action (read it with
	 * lrg_drop_target_get_current_action()). Perform the drop and return
	 * %TRUE; %FALSE reports the drop as refused to the source.
	 *
	 * Returns: %TRUE when the drop was performed
	 */
	signals[SIGNAL_DROP] =
		g_signal_new_class_handler ("drop", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		                            G_CALLBACK (drop_default), g_signal_accumulator_true_handled,
		                            NULL, NULL, G_TYPE_BOOLEAN, 3,
		                            LRG_TYPE_DRAG_PAYLOAD | G_SIGNAL_TYPE_STATIC_SCOPE,
		                            G_TYPE_FLOAT, G_TYPE_FLOAT);
}

static void
lrg_drop_target_init (LrgDropTarget *self)
{
	self->actions = LRG_DRAG_ACTION_ALL;
	self->preferred_action = LRG_DRAG_ACTION_NONE;
	self->paint_highlight = TRUE;
}

LrgDropTarget *
lrg_drop_target_new (const gchar * const *types,
                     LrgDragAction        actions)
{
	LrgDropTarget *self = g_object_new (LRG_TYPE_DROP_TARGET, NULL);

	self->types = types != NULL ? g_strdupv ((gchar **) types) : NULL;
	self->actions = (LrgDragAction) (actions & LRG_DRAG_ACTION_ALL);
	return self;
}

const gchar * const *
lrg_drop_target_get_types (LrgDropTarget *self)
{
	g_return_val_if_fail (LRG_IS_DROP_TARGET (self), NULL);

	return (const gchar * const *) self->types;
}

void
lrg_drop_target_set_types (LrgDropTarget       *self,
                           const gchar * const *types)
{
	g_return_if_fail (LRG_IS_DROP_TARGET (self));

	if (self->types == NULL && types == NULL)
		return;
	if (self->types != NULL && types != NULL &&
	    g_strv_equal ((const gchar * const *) self->types, types))
		return;

	g_strfreev (self->types);
	self->types = types != NULL ? g_strdupv ((gchar **) types) : NULL;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TYPES]);
}

LrgDragAction
lrg_drop_target_get_actions (LrgDropTarget *self)
{
	g_return_val_if_fail (LRG_IS_DROP_TARGET (self), LRG_DRAG_ACTION_NONE);

	return self->actions;
}

void
lrg_drop_target_set_actions (LrgDropTarget *self,
                             LrgDragAction  actions)
{
	g_return_if_fail (LRG_IS_DROP_TARGET (self));

	actions = (LrgDragAction) (actions & LRG_DRAG_ACTION_ALL);
	if (self->actions == actions)
		return;
	self->actions = actions;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIONS]);
}

LrgDragAction
lrg_drop_target_get_preferred_action (LrgDropTarget *self)
{
	g_return_val_if_fail (LRG_IS_DROP_TARGET (self), LRG_DRAG_ACTION_NONE);

	return self->preferred_action;
}

void
lrg_drop_target_set_preferred_action (LrgDropTarget *self,
                                      LrgDragAction  action)
{
	g_return_if_fail (LRG_IS_DROP_TARGET (self));

	action = (LrgDragAction) (action & LRG_DRAG_ACTION_ALL);
	if (self->preferred_action == action)
		return;
	self->preferred_action = action;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PREFERRED_ACTION]);
}

gboolean
lrg_drop_target_get_paint_highlight (LrgDropTarget *self)
{
	g_return_val_if_fail (LRG_IS_DROP_TARGET (self), FALSE);

	return self->paint_highlight;
}

void
lrg_drop_target_set_paint_highlight (LrgDropTarget *self,
                                     gboolean       paint_highlight)
{
	g_return_if_fail (LRG_IS_DROP_TARGET (self));

	paint_highlight = !!paint_highlight;
	if (self->paint_highlight == paint_highlight)
		return;
	self->paint_highlight = paint_highlight;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAINT_HIGHLIGHT]);
}

LrgDragPayload *
lrg_drop_target_get_current_drop (LrgDropTarget *self)
{
	g_return_val_if_fail (LRG_IS_DROP_TARGET (self), NULL);

	return self->current_drop;
}

LrgDragAction
lrg_drop_target_get_current_action (LrgDropTarget *self)
{
	g_return_val_if_fail (LRG_IS_DROP_TARGET (self), LRG_DRAG_ACTION_NONE);

	return self->current_action;
}

LrgDragAction
lrg_drop_target_get_proposed_action (LrgDropTarget *self)
{
	g_return_val_if_fail (LRG_IS_DROP_TARGET (self), LRG_DRAG_ACTION_NONE);

	return self->proposed_action;
}

LrgWidget *
lrg_drop_target_get_widget (LrgDropTarget *self)
{
	g_return_val_if_fail (LRG_IS_DROP_TARGET (self), NULL);

	return self->widget;
}

gboolean
lrg_drop_target_matches (LrgDropTarget  *self,
                         LrgDragPayload *payload)
{
	guint i;

	g_return_val_if_fail (LRG_IS_DROP_TARGET (self), FALSE);
	g_return_val_if_fail (payload != NULL, FALSE);

	/* At least one action in common */
	if ((self->actions & lrg_drag_payload_get_actions (payload)) == 0)
		return FALSE;

	/* NULL types take anything; otherwise one pattern must match */
	if (self->types == NULL)
		return TRUE;
	for (i = 0; self->types[i] != NULL; i++)
		if (lrg_drag_payload_matches (payload, self->types[i]))
			return TRUE;
	return FALSE;
}

/* ==========================================================================
 * Attaching to widgets
 * ========================================================================== */

void
lrg_widget_add_drop_target (LrgWidget     *self,
                            LrgDropTarget *target)
{
	GPtrArray *targets;

	g_return_if_fail (LRG_IS_WIDGET (self));
	g_return_if_fail (LRG_IS_DROP_TARGET (target));
	g_return_if_fail (target->widget == NULL);

	targets = g_object_get_qdata (G_OBJECT (self), drop_targets_quark ());
	if (targets == NULL)
	{
		targets = g_ptr_array_new_with_free_func (g_object_unref);
		g_object_set_qdata_full (G_OBJECT (self), drop_targets_quark (), targets,
		                         (GDestroyNotify) g_ptr_array_unref);
	}
	g_ptr_array_add (targets, target);   /* takes the caller's reference */
	g_set_weak_pointer (&target->widget, self);
	g_object_notify_by_pspec (G_OBJECT (target), properties[PROP_WIDGET]);
}

void
lrg_widget_remove_drop_target (LrgWidget     *self,
                               LrgDropTarget *target)
{
	GPtrArray *targets;

	g_return_if_fail (LRG_IS_WIDGET (self));
	g_return_if_fail (LRG_IS_DROP_TARGET (target));

	targets = g_object_get_qdata (G_OBJECT (self), drop_targets_quark ());
	if (targets == NULL || !g_ptr_array_find (targets, target, NULL))
	{
		g_warning ("%s: drop target %p is not attached to this %s", G_STRFUNC,
		           (gpointer) target, G_OBJECT_TYPE_NAME (self));
		return;
	}

	g_object_ref (target);
	g_clear_weak_pointer (&target->widget);
	g_ptr_array_remove (targets, target);
	g_object_notify_by_pspec (G_OBJECT (target), properties[PROP_WIDGET]);
	g_object_unref (target);
}

GList *
lrg_widget_list_drop_targets (LrgWidget *self)
{
	GPtrArray *targets;
	GList     *list = NULL;
	guint      i;

	g_return_val_if_fail (LRG_IS_WIDGET (self), NULL);

	targets = g_object_get_qdata (G_OBJECT (self), drop_targets_quark ());
	for (i = 0; targets != NULL && i < targets->len; i++)
		list = g_list_prepend (list, g_ptr_array_index (targets, i));
	return g_list_reverse (list);
}

/* ==========================================================================
 * Canvas session hooks (lrg-canvas-private.h)
 * ========================================================================== */

GPtrArray *
_lrg_widget_peek_drop_targets (LrgWidget *widget)
{
	return g_object_get_qdata (G_OBJECT (widget), drop_targets_quark ());
}

gboolean
_lrg_drop_target_accept (LrgDropTarget  *self,
                         LrgDragPayload *payload)
{
	gboolean ok = FALSE;

	g_signal_emit (self, signals[SIGNAL_ACCEPT], 0, payload, &ok);
	return ok;
}

/* set_current: track the hovering payload and action, with notifies */
static void
set_current (LrgDropTarget  *self,
             LrgDragPayload *payload,
             LrgDragAction   action)
{
	if (self->current_drop != payload)
	{
		if (payload != NULL)
			lrg_drag_payload_ref (payload);
		g_clear_pointer (&self->current_drop, lrg_drag_payload_unref);
		self->current_drop = payload;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_DROP]);
	}
	if (self->current_action != action)
	{
		self->current_action = action;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_ACTION]);
	}
}

void
_lrg_drop_target_enter (LrgDropTarget  *self,
                        LrgDragPayload *payload,
                        gfloat          x,
                        gfloat          y)
{
	set_current (self, payload, LRG_DRAG_ACTION_NONE);
	g_signal_emit (self, signals[SIGNAL_ENTER], 0, payload, x, y);
}

guint
_lrg_drop_target_motion (LrgDropTarget  *self,
                         LrgDragPayload *payload,
                         gfloat          x,
                         gfloat          y,
                         guint           proposed_in)
{
	LrgDragAction proposed = (LrgDragAction) proposed_in;
	LrgDragAction action = LRG_DRAG_ACTION_NONE;
	LrgDragAction allowed;

	self->proposed_action = proposed;
	g_signal_emit (self, signals[SIGNAL_MOTION], 0, payload, x, y, &action);

	/* A handler may only pick an action both sides allow */
	allowed = (LrgDragAction) (self->actions & lrg_drag_payload_get_actions (payload));
	if ((action & allowed) != action)
		action = LRG_DRAG_ACTION_NONE;

	/* Several bits: settle on one (the proposed action when it is among them) */
	if (action != LRG_DRAG_ACTION_NONE && (action & (action - 1)) != 0)
		action = lrg_drag_action_from_modifiers (LRG_INPUT_MODIFIER_NONE, action, proposed);

	/* The target may have been left (or the drag cancelled) from inside
	 * a handler: only record the action while the payload still hovers */
	if (self->current_drop == payload)
		set_current (self, payload, action);
	return (guint) action;
}

void
_lrg_drop_target_leave (LrgDropTarget *self)
{
	g_object_ref (self);
	g_signal_emit (self, signals[SIGNAL_LEAVE], 0);
	set_current (self, NULL, LRG_DRAG_ACTION_NONE);
	self->proposed_action = LRG_DRAG_ACTION_NONE;
	g_object_unref (self);
}

gboolean
_lrg_drop_target_drop (LrgDropTarget  *self,
                       LrgDragPayload *payload,
                       gfloat          x,
                       gfloat          y)
{
	gboolean ok = FALSE;

	g_signal_emit (self, signals[SIGNAL_DROP], 0, payload, x, y, &ok);
	return ok;
}
