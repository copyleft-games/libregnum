/* lrg-drag.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Drag and drop, source side: LrgDragAction, the refcounted
 * LrgDragPayload boxed type and the LrgDragSource controller. The
 * session itself (threshold tracking, hit-testing, preview painting)
 * lives in lrg-canvas.c; this file only stores configuration and
 * emits the source's signals when the canvas asks it to.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <string.h>

#include "lrg-drag.h"
#include "lrg-canvas-private.h"
#include "../lrg-log.h"

/* ==========================================================================
 * LrgDragAction
 * ========================================================================== */

GType
lrg_drag_action_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_DRAG_ACTION_NONE, "LRG_DRAG_ACTION_NONE", "none" },
			{ LRG_DRAG_ACTION_COPY, "LRG_DRAG_ACTION_COPY", "copy" },
			{ LRG_DRAG_ACTION_MOVE, "LRG_DRAG_ACTION_MOVE", "move" },
			{ LRG_DRAG_ACTION_LINK, "LRG_DRAG_ACTION_LINK", "link" },
			{ 0, NULL, NULL }
		};
		GType id = g_flags_register_static (g_intern_static_string ("LrgDragAction"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

LrgDragAction
lrg_drag_action_from_modifiers (LrgInputModifiers modifiers,
                                LrgDragAction     allowed,
                                LrgDragAction     preferred)
{
	LrgDragAction wanted = LRG_DRAG_ACTION_NONE;
	gboolean      ctrl = (modifiers & LRG_INPUT_MODIFIER_CTRL) != 0;
	gboolean      shift = (modifiers & LRG_INPUT_MODIFIER_SHIFT) != 0;
	gboolean      alt = (modifiers & LRG_INPUT_MODIFIER_ALT) != 0;

	allowed = (LrgDragAction) (allowed & LRG_DRAG_ACTION_ALL);
	if (allowed == LRG_DRAG_ACTION_NONE)
		return LRG_DRAG_ACTION_NONE;

	/* The modifier request: Ctrl+Shift or Alt link, Ctrl copies, Shift moves */
	if ((ctrl && shift) || alt)
		wanted = LRG_DRAG_ACTION_LINK;
	else if (ctrl)
		wanted = LRG_DRAG_ACTION_COPY;
	else if (shift)
		wanted = LRG_DRAG_ACTION_MOVE;

	if (wanted != LRG_DRAG_ACTION_NONE && (allowed & wanted) != 0)
		return wanted;

	/* No (usable) request: the preferred action, then MOVE, COPY, LINK */
	if (preferred != LRG_DRAG_ACTION_NONE && (allowed & preferred) == preferred)
	{
		/* Several bits in @preferred: take the lowest one */
		return (LrgDragAction) (preferred & (~preferred + 1));
	}
	if (allowed & LRG_DRAG_ACTION_MOVE)
		return LRG_DRAG_ACTION_MOVE;
	if (allowed & LRG_DRAG_ACTION_COPY)
		return LRG_DRAG_ACTION_COPY;
	return LRG_DRAG_ACTION_LINK;
}

/* ==========================================================================
 * LrgDragPayload
 * ========================================================================== */

struct _LrgDragPayload
{
	gatomicrefcount  ref_count;
	gchar           *content_type;
	GValue           value;
	GDestroyNotify   pointer_destroy;   /* for new_pointer() payloads */
	LrgDragAction    actions;
	GWeakRef         source;            /* LrgWidget */
	gchar           *label;
	LrgIcon          icon;
};

G_DEFINE_BOXED_TYPE (LrgDragPayload, lrg_drag_payload,
                     lrg_drag_payload_ref, lrg_drag_payload_unref)

LrgDragPayload *
lrg_drag_payload_new (const gchar   *content_type,
                      const GValue  *value,
                      LrgDragAction  actions)
{
	LrgDragPayload *self;

	g_return_val_if_fail (content_type != NULL, NULL);

	self = g_new0 (LrgDragPayload, 1);
	g_atomic_ref_count_init (&self->ref_count);
	self->content_type = g_strdup (content_type);
	self->actions = (LrgDragAction) (actions & LRG_DRAG_ACTION_ALL);
	self->icon = LRG_ICON_NONE;
	g_weak_ref_init (&self->source, NULL);

	/* Copy the value in; an unset value stays G_TYPE_INVALID */
	if (value != NULL && G_IS_VALUE (value))
	{
		g_value_init (&self->value, G_VALUE_TYPE (value));
		g_value_copy (value, &self->value);
	}
	return self;
}

LrgDragPayload *
lrg_drag_payload_new_string (const gchar   *content_type,
                             const gchar   *string,
                             LrgDragAction  actions)
{
	LrgDragPayload *self;

	g_return_val_if_fail (content_type != NULL, NULL);

	self = lrg_drag_payload_new (content_type, NULL, actions);
	g_value_init (&self->value, G_TYPE_STRING);
	g_value_set_string (&self->value, string);
	self->label = g_strdup (string);
	return self;
}

LrgDragPayload *
lrg_drag_payload_new_object (const gchar   *content_type,
                             gpointer       object,
                             LrgDragAction  actions)
{
	LrgDragPayload *self;

	g_return_val_if_fail (content_type != NULL, NULL);
	g_return_val_if_fail (object == NULL || G_IS_OBJECT (object), NULL);

	self = lrg_drag_payload_new (content_type, NULL, actions);

	/* Typed as the object's own class so G_VALUE_HOLDS checks work */
	g_value_init (&self->value, object != NULL ? G_OBJECT_TYPE (object) : G_TYPE_OBJECT);
	g_value_set_object (&self->value, object);
	return self;
}

LrgDragPayload *
lrg_drag_payload_new_pointer (const gchar    *content_type,
                              gpointer        pointer,
                              GDestroyNotify  destroy,
                              LrgDragAction   actions)
{
	LrgDragPayload *self;

	g_return_val_if_fail (content_type != NULL, NULL);

	self = lrg_drag_payload_new (content_type, NULL, actions);
	g_value_init (&self->value, G_TYPE_POINTER);
	g_value_set_pointer (&self->value, pointer);
	self->pointer_destroy = destroy;
	return self;
}

LrgDragPayload *
lrg_drag_payload_ref (LrgDragPayload *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	g_atomic_ref_count_inc (&self->ref_count);
	return self;
}

void
lrg_drag_payload_unref (LrgDragPayload *self)
{
	g_return_if_fail (self != NULL);

	if (!g_atomic_ref_count_dec (&self->ref_count))
		return;

	/* A raw pointer payload frees its pointer through the notify */
	if (self->pointer_destroy != NULL && G_VALUE_HOLDS_POINTER (&self->value))
	{
		gpointer p = g_value_get_pointer (&self->value);

		if (p != NULL)
			self->pointer_destroy (p);
	}
	if (G_IS_VALUE (&self->value))
		g_value_unset (&self->value);
	g_weak_ref_clear (&self->source);
	g_free (self->content_type);
	g_free (self->label);
	g_free (self);
}

const gchar *
lrg_drag_payload_get_content_type (LrgDragPayload *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return self->content_type;
}

gboolean
lrg_drag_payload_matches (LrgDragPayload *self,
                          const gchar    *pattern)
{
	gsize len;

	g_return_val_if_fail (self != NULL, FALSE);

	if (pattern == NULL)
		return FALSE;
	if (strcmp (pattern, "*") == 0)
		return TRUE;

	/* "major/<anything>": same major type and a slash */
	len = strlen (pattern);
	if (len >= 2 && pattern[len - 1] == '*' && pattern[len - 2] == '/')
		return strncmp (self->content_type, pattern, len - 1) == 0;

	return strcmp (self->content_type, pattern) == 0;
}

const GValue *
lrg_drag_payload_get_value (LrgDragPayload *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return &self->value;
}

const gchar *
lrg_drag_payload_get_string (LrgDragPayload *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return G_VALUE_HOLDS_STRING (&self->value) ? g_value_get_string (&self->value) : NULL;
}

gpointer
lrg_drag_payload_get_object (LrgDragPayload *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return G_VALUE_HOLDS_OBJECT (&self->value) ? g_value_get_object (&self->value) : NULL;
}

gpointer
lrg_drag_payload_get_pointer (LrgDragPayload *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return G_VALUE_HOLDS_POINTER (&self->value) ? g_value_get_pointer (&self->value) : NULL;
}

LrgDragAction
lrg_drag_payload_get_actions (LrgDragPayload *self)
{
	g_return_val_if_fail (self != NULL, LRG_DRAG_ACTION_NONE);

	return self->actions;
}

void
lrg_drag_payload_set_actions (LrgDragPayload *self,
                              LrgDragAction   actions)
{
	g_return_if_fail (self != NULL);

	self->actions = (LrgDragAction) (actions & LRG_DRAG_ACTION_ALL);
}

LrgWidget *
lrg_drag_payload_get_source (LrgDragPayload *self)
{
	LrgWidget *source;

	g_return_val_if_fail (self != NULL, NULL);

	/* Borrowed: the weak ref hands out a strong one, give it back */
	source = g_weak_ref_get (&self->source);
	if (source != NULL)
		g_object_unref (source);
	return source;
}

void
lrg_drag_payload_set_source (LrgDragPayload *self,
                             LrgWidget      *source)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (source == NULL || LRG_IS_WIDGET (source));

	g_weak_ref_set (&self->source, source);
}

const gchar *
lrg_drag_payload_get_label (LrgDragPayload *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return self->label;
}

void
lrg_drag_payload_set_label (LrgDragPayload *self,
                            const gchar    *label)
{
	g_return_if_fail (self != NULL);

	g_free (self->label);
	self->label = g_strdup (label);
}

LrgIcon
lrg_drag_payload_get_icon (LrgDragPayload *self)
{
	g_return_val_if_fail (self != NULL, LRG_ICON_NONE);

	return self->icon;
}

void
lrg_drag_payload_set_icon (LrgDragPayload *self,
                           LrgIcon         icon)
{
	g_return_if_fail (self != NULL);

	self->icon = icon;
}

/* ==========================================================================
 * LrgDragSource
 * ========================================================================== */

struct _LrgDragSource
{
	GObject          parent_instance;

	gfloat           threshold;
	GrlMouseButton   button;
	LrgDragPayload  *payload;    /* owned, nullable: fixed payload */
	LrgWidget       *widget;     /* weak: the widget it is attached to */
	gboolean         dragging;
};

G_DEFINE_FINAL_TYPE (LrgDragSource, lrg_drag_source, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_THRESHOLD,
	PROP_BUTTON,
	PROP_PAYLOAD,
	PROP_WIDGET,
	PROP_DRAGGING,
	N_PROPS
};

enum
{
	SIGNAL_PREPARE,
	SIGNAL_DRAG_BEGIN,
	SIGNAL_DRAG_END,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* qdata key for the GPtrArray of sources a widget owns */
static GQuark
drag_sources_quark (void)
{
	static GQuark q = 0;

	if (G_UNLIKELY (q == 0))
		q = g_quark_from_static_string ("lrg-drag-sources");
	return q;
}

/*
 * prepare_accumulator:
 *
 * The first handler that returns a payload wins; NULL returns let the
 * next handler (and finally the class handler with the fixed payload)
 * try.
 */
static gboolean
prepare_accumulator (GSignalInvocationHint *ihint,
                     GValue                *return_accu,
                     const GValue          *handler_return,
                     gpointer               data)
{
	gpointer payload = g_value_get_boxed (handler_return);

	g_value_set_boxed (return_accu, payload);
	return payload == NULL;
}

/* prepare_default: the class handler returns the fixed payload */
static LrgDragPayload *
prepare_default (LrgDragSource *self,
                 gfloat         x,
                 gfloat         y)
{
	return self->payload != NULL ? lrg_drag_payload_ref (self->payload) : NULL;
}

static void
lrg_drag_source_dispose (GObject *object)
{
	LrgDragSource *self = LRG_DRAG_SOURCE (object);

	g_clear_pointer (&self->payload, lrg_drag_payload_unref);
	g_clear_weak_pointer (&self->widget);

	G_OBJECT_CLASS (lrg_drag_source_parent_class)->dispose (object);
}

static void
lrg_drag_source_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgDragSource *self = LRG_DRAG_SOURCE (object);

	switch (prop_id)
	{
	case PROP_THRESHOLD:
		g_value_set_float (value, self->threshold);
		break;
	case PROP_BUTTON:
		g_value_set_enum (value, self->button);
		break;
	case PROP_PAYLOAD:
		g_value_set_boxed (value, self->payload);
		break;
	case PROP_WIDGET:
		g_value_set_object (value, self->widget);
		break;
	case PROP_DRAGGING:
		g_value_set_boolean (value, self->dragging);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_drag_source_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgDragSource *self = LRG_DRAG_SOURCE (object);

	switch (prop_id)
	{
	case PROP_THRESHOLD:
		lrg_drag_source_set_threshold (self, g_value_get_float (value));
		break;
	case PROP_BUTTON:
		lrg_drag_source_set_button (self, (GrlMouseButton) g_value_get_enum (value));
		break;
	case PROP_PAYLOAD:
		lrg_drag_source_set_payload (self, g_value_get_boxed (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_drag_source_class_init (LrgDragSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_drag_source_dispose;
	object_class->get_property = lrg_drag_source_get_property;
	object_class->set_property = lrg_drag_source_set_property;

	/**
	 * LrgDragSource:threshold:
	 *
	 * Distance in canvas units the pointer must travel with the button
	 * held before the press becomes a drag. Below it the press stays an
	 * ordinary press (a click on release).
	 */
	properties[PROP_THRESHOLD] =
		g_param_spec_float ("threshold", NULL, NULL, 0.0f, G_MAXFLOAT, LRG_DRAG_DEFAULT_THRESHOLD,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDragSource:button:
	 *
	 * The mouse button that starts drags (left by default).
	 */
	properties[PROP_BUTTON] =
		g_param_spec_enum ("button", NULL, NULL, GRL_TYPE_MOUSE_BUTTON, GRL_MOUSE_BUTTON_LEFT,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDragSource:payload:
	 *
	 * A fixed payload: #LrgDragSource::prepare returns it when no
	 * handler built one.
	 */
	properties[PROP_PAYLOAD] =
		g_param_spec_boxed ("payload", NULL, NULL, LRG_TYPE_DRAG_PAYLOAD,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDragSource:widget:
	 *
	 * The widget the source is attached to (set by
	 * lrg_widget_add_drag_source()).
	 */
	properties[PROP_WIDGET] =
		g_param_spec_object ("widget", NULL, NULL, LRG_TYPE_WIDGET,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDragSource:dragging:
	 *
	 * %TRUE while a drag started from this source is in progress.
	 */
	properties[PROP_DRAGGING] =
		g_param_spec_boolean ("dragging", NULL, NULL, FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgDragSource::prepare:
	 * @source: the drag source
	 * @x: canvas X where the button went down
	 * @y: canvas Y where the button went down
	 *
	 * Emitted when a press moved past the threshold. Return a new
	 * payload (transfer full) to start the drag, or %NULL to let the
	 * press continue as a normal press (and give an outer source a
	 * chance). The first handler that returns a payload wins; the
	 * default handler returns #LrgDragSource:payload.
	 *
	 * Returns: (transfer full) (nullable): the payload to drag
	 */
	signals[SIGNAL_PREPARE] =
		g_signal_new_class_handler ("prepare", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		                            G_CALLBACK (prepare_default), prepare_accumulator, NULL, NULL,
		                            LRG_TYPE_DRAG_PAYLOAD, 2, G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgDragSource::drag-begin:
	 * @source: the drag source
	 * @payload: the payload being dragged
	 *
	 * Emitted once the drag has started (after the press was cancelled
	 * on the source widget). Dim the dragged row here, for example.
	 */
	signals[SIGNAL_DRAG_BEGIN] =
		g_signal_new ("drag-begin", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1,
		              LRG_TYPE_DRAG_PAYLOAD | G_SIGNAL_TYPE_STATIC_SCOPE);

	/**
	 * LrgDragSource::drag-end:
	 * @source: the drag source
	 * @payload: the payload that was dragged
	 * @dropped: %TRUE when a target took the drop
	 * @action: the action of the drop, %LRG_DRAG_ACTION_NONE when it
	 *   was cancelled or refused
	 *
	 * Emitted when the drag finished. After a MOVE the source removes
	 * the item (unless the target already did, e.g. a reorder within
	 * one list).
	 */
	signals[SIGNAL_DRAG_END] =
		g_signal_new ("drag-end", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 3,
		              LRG_TYPE_DRAG_PAYLOAD | G_SIGNAL_TYPE_STATIC_SCOPE,
		              G_TYPE_BOOLEAN, LRG_TYPE_DRAG_ACTION);
}

static void
lrg_drag_source_init (LrgDragSource *self)
{
	self->threshold = LRG_DRAG_DEFAULT_THRESHOLD;
	self->button = GRL_MOUSE_BUTTON_LEFT;
}

LrgDragSource *
lrg_drag_source_new (void)
{
	return g_object_new (LRG_TYPE_DRAG_SOURCE, NULL);
}

gfloat
lrg_drag_source_get_threshold (LrgDragSource *self)
{
	g_return_val_if_fail (LRG_IS_DRAG_SOURCE (self), 0.0f);

	return self->threshold;
}

void
lrg_drag_source_set_threshold (LrgDragSource *self,
                               gfloat         threshold)
{
	g_return_if_fail (LRG_IS_DRAG_SOURCE (self));

	threshold = MAX (threshold, 0.0f);
	if (self->threshold == threshold)
		return;
	self->threshold = threshold;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_THRESHOLD]);
}

GrlMouseButton
lrg_drag_source_get_button (LrgDragSource *self)
{
	g_return_val_if_fail (LRG_IS_DRAG_SOURCE (self), GRL_MOUSE_BUTTON_LEFT);

	return self->button;
}

void
lrg_drag_source_set_button (LrgDragSource  *self,
                            GrlMouseButton  button)
{
	g_return_if_fail (LRG_IS_DRAG_SOURCE (self));

	if (self->button == button)
		return;
	self->button = button;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUTTON]);
}

LrgDragPayload *
lrg_drag_source_get_payload (LrgDragSource *self)
{
	g_return_val_if_fail (LRG_IS_DRAG_SOURCE (self), NULL);

	return self->payload;
}

void
lrg_drag_source_set_payload (LrgDragSource  *self,
                             LrgDragPayload *payload)
{
	g_return_if_fail (LRG_IS_DRAG_SOURCE (self));

	if (self->payload == payload)
		return;
	if (payload != NULL)
		lrg_drag_payload_ref (payload);
	g_clear_pointer (&self->payload, lrg_drag_payload_unref);
	self->payload = payload;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAYLOAD]);
}

LrgWidget *
lrg_drag_source_get_widget (LrgDragSource *self)
{
	g_return_val_if_fail (LRG_IS_DRAG_SOURCE (self), NULL);

	return self->widget;
}

gboolean
lrg_drag_source_get_dragging (LrgDragSource *self)
{
	g_return_val_if_fail (LRG_IS_DRAG_SOURCE (self), FALSE);

	return self->dragging;
}

/* ==========================================================================
 * Attaching to widgets
 * ========================================================================== */

void
lrg_widget_add_drag_source (LrgWidget     *self,
                            LrgDragSource *source)
{
	GPtrArray *sources;

	g_return_if_fail (LRG_IS_WIDGET (self));
	g_return_if_fail (LRG_IS_DRAG_SOURCE (source));
	g_return_if_fail (source->widget == NULL);

	/* The widget owns its sources through qdata, freed with it */
	sources = g_object_get_qdata (G_OBJECT (self), drag_sources_quark ());
	if (sources == NULL)
	{
		sources = g_ptr_array_new_with_free_func (g_object_unref);
		g_object_set_qdata_full (G_OBJECT (self), drag_sources_quark (), sources,
		                         (GDestroyNotify) g_ptr_array_unref);
	}
	g_ptr_array_add (sources, source);   /* takes the caller's reference */
	g_set_weak_pointer (&source->widget, self);
	g_object_notify_by_pspec (G_OBJECT (source), properties[PROP_WIDGET]);
}

void
lrg_widget_remove_drag_source (LrgWidget     *self,
                               LrgDragSource *source)
{
	GPtrArray *sources;

	g_return_if_fail (LRG_IS_WIDGET (self));
	g_return_if_fail (LRG_IS_DRAG_SOURCE (source));

	sources = g_object_get_qdata (G_OBJECT (self), drag_sources_quark ());
	if (sources == NULL || !g_ptr_array_find (sources, source, NULL))
	{
		g_warning ("%s: drag source %p is not attached to this %s", G_STRFUNC,
		           (gpointer) source, G_OBJECT_TYPE_NAME (self));
		return;
	}

	/* Keep it alive through the notify, then let the array drop it */
	g_object_ref (source);
	g_clear_weak_pointer (&source->widget);
	g_ptr_array_remove (sources, source);
	g_object_notify_by_pspec (G_OBJECT (source), properties[PROP_WIDGET]);
	g_object_unref (source);
}

GList *
lrg_widget_list_drag_sources (LrgWidget *self)
{
	GPtrArray *sources;
	GList     *list = NULL;
	guint      i;

	g_return_val_if_fail (LRG_IS_WIDGET (self), NULL);

	sources = g_object_get_qdata (G_OBJECT (self), drag_sources_quark ());
	for (i = 0; sources != NULL && i < sources->len; i++)
		list = g_list_prepend (list, g_ptr_array_index (sources, i));
	return g_list_reverse (list);
}

/* ==========================================================================
 * Canvas session hooks (lrg-canvas-private.h)
 * ========================================================================== */

GPtrArray *
_lrg_widget_peek_drag_sources (LrgWidget *widget)
{
	return g_object_get_qdata (G_OBJECT (widget), drag_sources_quark ());
}

LrgDragPayload *
_lrg_drag_source_prepare (LrgDragSource *self,
                          gfloat         x,
                          gfloat         y)
{
	LrgDragPayload *payload = NULL;

	g_signal_emit (self, signals[SIGNAL_PREPARE], 0, x, y, &payload);
	return payload;
}

void
_lrg_drag_source_begin (LrgDragSource  *self,
                        LrgDragPayload *payload)
{
	if (!self->dragging)
	{
		self->dragging = TRUE;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DRAGGING]);
	}
	g_signal_emit (self, signals[SIGNAL_DRAG_BEGIN], 0, payload);
}

void
_lrg_drag_source_end (LrgDragSource  *self,
                      LrgDragPayload *payload,
                      gboolean        dropped,
                      guint           action)
{
	if (self->dragging)
	{
		self->dragging = FALSE;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DRAGGING]);
	}
	g_signal_emit (self, signals[SIGNAL_DRAG_END], 0, payload, dropped, action);
}
