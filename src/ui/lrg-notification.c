/* lrg-notification.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNotification: desktop notifications over org.freedesktop.Notifications,
 * with an in-app toast fallback.
 *
 * libregnum apps are not GApplications, so GNotification is not an
 * option; this talks the freedesktop protocol directly over GDBus:
 *
 *   Notify (app_name, replaces_id, app_icon, summary, body, actions,
 *           hints, expire_timeout) -> id
 *   CloseNotification (id)
 *   signal ActionInvoked (id, action_key)
 *   signal NotificationClosed (id, reason)
 *
 * Everything is asynchronous so showing a notification never stalls a
 * frame. The flow of lrg_notification_show_async():
 *
 *   in-app forced? ---- yes ---> toast
 *        | no
 *   g_bus_get (session) -- error --> toast (or NO_BACKEND error)
 *        |
 *   Notify (replaces_id = current id) -- error --> toast (or error)
 *        |
 *   remember id, subscribe to ActionInvoked / NotificationClosed
 *
 * Only one Notify is in flight per notification: later show requests
 * wait in a queue and are sent once the id of the first is known, so an
 * update always replaces rather than duplicates.
 *
 * While shown, the notification holds a reference on itself (a
 * fire-and-forget notification must still route its button clicks);
 * it is dropped when the notification closes or is withdrawn.
 *
 * Actions: the default action and each button carry a detailed action
 * name ("app.show-inbox", "win.open::42"). When invoked, the action is
 * activated with lrg_widget_activate_action() on the anchor widget
 * passed to show (or the toast queue), so groups on that widget, its
 * ancestors and the canvas's "app" / "ui" groups all resolve.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <string.h>

#include "lrg-notification.h"
#include "../lrg-log.h"

#define NOTIFY_BUS_NAME  "org.freedesktop.Notifications"
#define NOTIFY_PATH      "/org/freedesktop/Notifications"
#define NOTIFY_INTERFACE "org.freedesktop.Notifications"

/* Action key of the default action (the freedesktop convention) */
#define NOTIFY_DEFAULT_KEY "default"

/* Prefix of button action keys: "lrg-button-<index>" */
#define NOTIFY_BUTTON_PREFIX "lrg-button-"

typedef struct
{
	gchar *label;
	gchar *action;
} Button;

struct _LrgNotification
{
	GObject                 parent_instance;

	/* Content */
	gchar                  *title;
	gchar                  *body;
	gchar                  *icon_name;
	LrgNotificationUrgency  urgency;
	gint                    timeout;
	gchar                  *default_action;
	GArray                 *buttons;          /* Button */
	gchar                  *app_name;
	gchar                  *desktop_entry;
	gboolean                in_app;
	LrgToastQueue          *toast_queue;

	/* Live state */
	LrgNotificationBackend  backend;
	GWeakRef                anchor;
	gboolean                alive;            /* holds a self reference */

	/* D-Bus */
	GDBusConnection        *conn;
	guint                   id;
	guint                   sub_action;
	guint                   sub_closed;
	gboolean                in_flight;        /* a Notify call is pending */
	GQueue                  waiting;          /* GTask, show requests behind it */
	gboolean                withdraw_pending; /* withdrawn while in flight */

	/* Toast fallback */
	LrgToast               *toast;
	gulong                  toast_handler;
	gboolean                withdrawing;
};

G_DEFINE_FINAL_TYPE (LrgNotification, lrg_notification, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-notification-error-quark, lrg_notification_error)

enum
{
	PROP_0,
	PROP_TITLE,
	PROP_BODY,
	PROP_ICON_NAME,
	PROP_URGENCY,
	PROP_TIMEOUT,
	PROP_DEFAULT_ACTION,
	PROP_APP_NAME,
	PROP_DESKTOP_ENTRY,
	PROP_IN_APP,
	PROP_TOAST_QUEUE,
	PROP_ID,
	PROP_BACKEND,
	N_PROPS
};

enum
{
	SIGNAL_ACTIVATED,
	SIGNAL_ACTION_INVOKED,
	SIGNAL_CLOSED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* Process-wide settings */
static GWeakRef default_queue;
static gboolean force_in_app = FALSE;

/* ==========================================================================
 * Enum types
 * ========================================================================== */

GType
lrg_notification_urgency_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_NOTIFICATION_URGENCY_LOW, "LRG_NOTIFICATION_URGENCY_LOW", "low" },
			{ LRG_NOTIFICATION_URGENCY_NORMAL, "LRG_NOTIFICATION_URGENCY_NORMAL", "normal" },
			{ LRG_NOTIFICATION_URGENCY_CRITICAL, "LRG_NOTIFICATION_URGENCY_CRITICAL", "critical" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgNotificationUrgency"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

GType
lrg_notification_close_reason_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_NOTIFICATION_CLOSE_REASON_EXPIRED, "LRG_NOTIFICATION_CLOSE_REASON_EXPIRED", "expired" },
			{ LRG_NOTIFICATION_CLOSE_REASON_DISMISSED, "LRG_NOTIFICATION_CLOSE_REASON_DISMISSED", "dismissed" },
			{ LRG_NOTIFICATION_CLOSE_REASON_CLOSED, "LRG_NOTIFICATION_CLOSE_REASON_CLOSED", "closed" },
			{ LRG_NOTIFICATION_CLOSE_REASON_UNDEFINED, "LRG_NOTIFICATION_CLOSE_REASON_UNDEFINED", "undefined" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgNotificationCloseReason"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

GType
lrg_notification_backend_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_NOTIFICATION_BACKEND_NONE, "LRG_NOTIFICATION_BACKEND_NONE", "none" },
			{ LRG_NOTIFICATION_BACKEND_DBUS, "LRG_NOTIFICATION_BACKEND_DBUS", "dbus" },
			{ LRG_NOTIFICATION_BACKEND_TOAST, "LRG_NOTIFICATION_BACKEND_TOAST", "toast" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgNotificationBackend"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void
button_clear (gpointer data)
{
	Button *b = data;

	g_clear_pointer (&b->label, g_free);
	g_clear_pointer (&b->action, g_free);
}

/* resolve_queue: the toast queue for the in-app fallback, or NULL */
static LrgToastQueue *
resolve_queue (LrgNotification *self)
{
	LrgToastQueue *queue;

	if (self->toast_queue != NULL)
		return self->toast_queue;

	/* The default queue is held weakly; the returned pointer stays
	 * valid while the canvas owns it (the caller uses it right away) */
	queue = g_weak_ref_get (&default_queue);
	if (queue != NULL)
		g_object_unref (queue);
	return queue;
}

/* resolve_anchor: where actions are activated (new ref), or NULL */
static LrgWidget *
resolve_anchor (LrgNotification *self)
{
	LrgWidget     *anchor = g_weak_ref_get (&self->anchor);
	LrgToastQueue *queue;

	if (anchor != NULL)
		return anchor;
	queue = resolve_queue (self);
	return queue != NULL ? g_object_ref (LRG_WIDGET (queue)) : NULL;
}

/* use_in_app: whether this notification skips the notification server */
static gboolean
use_in_app (LrgNotification *self)
{
	return self->in_app || force_in_app;
}

/* set_backend: switch backend, taking / dropping the keep-alive ref */
static void
set_backend (LrgNotification        *self,
             LrgNotificationBackend  backend)
{
	if (self->backend == backend)
		return;
	self->backend = backend;
	if (backend != LRG_NOTIFICATION_BACKEND_NONE && !self->alive)
	{
		self->alive = TRUE;
		g_object_ref (self);
	}
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKEND]);
}

/* release_alive: drop the keep-alive ref (may finalize @self: call last) */
static void
release_alive (LrgNotification *self)
{
	if (self->alive && self->backend == LRG_NOTIFICATION_BACKEND_NONE)
	{
		self->alive = FALSE;
		g_object_unref (self);
	}
}

/* unsubscribe: stop listening to the server's signals */
static void
unsubscribe (LrgNotification *self)
{
	if (self->conn != NULL)
	{
		if (self->sub_action != 0)
			g_dbus_connection_signal_unsubscribe (self->conn, self->sub_action);
		if (self->sub_closed != 0)
			g_dbus_connection_signal_unsubscribe (self->conn, self->sub_closed);
	}
	self->sub_action = 0;
	self->sub_closed = 0;
}

/* drop_toast: forget the fallback toast */
static void
drop_toast (LrgNotification *self)
{
	if (self->toast == NULL)
		return;
	g_clear_signal_handler (&self->toast_handler, self->toast);
	g_clear_object (&self->toast);
}

/*
 * finish_closed:
 *
 * The notification went away: reset the live state, emit ::closed and
 * finally drop the keep-alive reference.
 */
static void
finish_closed (LrgNotification            *self,
               LrgNotificationCloseReason  reason)
{
	gboolean had_id = self->id != 0;

	g_object_ref (self);

	unsubscribe (self);
	self->id = 0;
	drop_toast (self);
	if (had_id)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ID]);
	set_backend (self, LRG_NOTIFICATION_BACKEND_NONE);

	g_signal_emit (self, signals[SIGNAL_CLOSED], 0, reason);

	release_alive (self);
	g_object_unref (self);
}

/*
 * invoke:
 *
 * The user picked an action: activate @detailed_action on the anchor
 * (when there is one), emit ::action-invoked with its name and, for
 * the default action, ::activated.
 */
static void
invoke (LrgNotification *self,
        const gchar     *detailed_action,
        gboolean         is_default)
{
	g_object_ref (self);

	if (detailed_action != NULL)
	{
		g_autofree gchar     *name = NULL;
		g_autoptr(GVariant)   target = NULL;
		g_autoptr(GError)     error = NULL;
		g_autoptr(LrgWidget)  anchor = resolve_anchor (self);

		if (!g_action_parse_detailed_name (detailed_action, &name, &target, &error))
			lrg_warning (LRG_LOG_DOMAIN, "notification: bad action \"%s\": %s",
			             detailed_action, error->message);
		else if (anchor == NULL)
			lrg_debug (LRG_LOG_DOMAIN, "notification: no anchor to activate \"%s\" on", detailed_action);
		else if (!lrg_widget_activate_action (anchor, name, target))
			lrg_debug (LRG_LOG_DOMAIN, "notification: action \"%s\" not found or disabled", name);

		g_signal_emit (self, signals[SIGNAL_ACTION_INVOKED], 0, detailed_action);
	}

	if (is_default)
		g_signal_emit (self, signals[SIGNAL_ACTIVATED], 0);

	g_object_unref (self);
}

/* ==========================================================================
 * Toast fallback
 * ========================================================================== */

static LrgToastKind
toast_kind (LrgNotification *self)
{
	return self->urgency == LRG_NOTIFICATION_URGENCY_CRITICAL ? LRG_TOAST_KIND_DANGER : LRG_TOAST_KIND_INFO;
}

/* toast_duration: seconds; critical notifications without a timeout stay */
static gdouble
toast_duration (LrgNotification *self)
{
	if (self->timeout == 0)
		return 0.0;
	if (self->timeout < 0)
		return self->urgency == LRG_NOTIFICATION_URGENCY_CRITICAL ? 0.0 : LRG_TOAST_DEFAULT_DURATION;
	return (gdouble) self->timeout / 1000.0;
}

/*
 * on_toast_dismissed:
 *
 * Works out why the toast went: a click (the toast is still PRESSED
 * while it handles the release) invokes the default action and counts
 * as a dismissal; a finished timer is an expiry; anything else (the
 * queue was cleared) is undefined. Withdrawals are reported by
 * lrg_notification_withdraw() itself.
 */
static void
on_toast_dismissed (LrgToast *toast,
                    gpointer  user_data)
{
	LrgNotification            *self = LRG_NOTIFICATION (user_data);
	LrgNotificationCloseReason  reason;
	gdouble                     duration = lrg_toast_get_duration (toast);

	if (self->withdrawing || toast != self->toast)
		return;

	g_object_ref (self);
	if (lrg_widget_get_state_flags (LRG_WIDGET (toast)) & LRG_WIDGET_STATE_PRESSED)
	{
		invoke (self, self->default_action, TRUE);
		reason = LRG_NOTIFICATION_CLOSE_REASON_DISMISSED;
	}
	else if (duration > 0.0 && lrg_toast_get_elapsed (toast) >= duration)
		reason = LRG_NOTIFICATION_CLOSE_REASON_EXPIRED;
	else
		reason = LRG_NOTIFICATION_CLOSE_REASON_UNDEFINED;

	/* Invoking may have withdrawn or re-shown us */
	if (self->toast == toast)
		finish_closed (self, reason);
	g_object_unref (self);
}

/*
 * show_toast:
 *
 * Shows (or updates) the in-app toast. Returns FALSE when there is no
 * toast queue to show it in.
 */
static gboolean
show_toast (LrgNotification *self)
{
	LrgToastQueue *queue;
	LrgToast      *toast;

	/* Already up: update in place */
	if (self->toast != NULL && !lrg_toast_get_dismissed (self->toast))
	{
		lrg_toast_set_title (self->toast, self->title);
		lrg_toast_set_message (self->toast, self->body);
		lrg_toast_set_kind (self->toast, toast_kind (self));
		lrg_toast_set_duration (self->toast, toast_duration (self));
		return TRUE;
	}

	queue = resolve_queue (self);
	if (queue == NULL)
		return FALSE;

	drop_toast (self);
	toast = lrg_toast_new (self->title, self->body);
	lrg_toast_set_kind (toast, toast_kind (self));
	lrg_toast_set_duration (toast, toast_duration (self));
	self->toast = toast;
	self->toast_handler = g_signal_connect (toast, "dismissed", G_CALLBACK (on_toast_dismissed), self);
	lrg_toast_queue_show (queue, toast);

	set_backend (self, LRG_NOTIFICATION_BACKEND_TOAST);
	return TRUE;
}

/*
 * fall_back:
 *
 * The notification server is out of reach (@error says why): show the
 * toast instead, or fail @task with NO_BACKEND. Cancellation is never
 * turned into a toast.
 */
static void
fall_back (LrgNotification *self,
           GTask           *task,
           const GError    *error)
{
	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
	{
		g_task_return_error (task, g_error_copy (error));
		return;
	}

	lrg_debug (LRG_LOG_DOMAIN, "notification: no notification server (%s), trying in-app",
	           error != NULL ? error->message : "unknown");
	if (show_toast (self))
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_new_error (task, LRG_NOTIFICATION_ERROR, LRG_NOTIFICATION_ERROR_NO_BACKEND,
		                         "No notification server (%s) and no toast queue",
		                         error != NULL ? error->message : "unknown");
}

/* ==========================================================================
 * D-Bus backend
 * ========================================================================== */

/* on_action_invoked: ActionInvoked (u id, s action_key) */
static void
on_action_invoked (GDBusConnection *conn,
                   const gchar     *sender,
                   const gchar     *path,
                   const gchar     *iface,
                   const gchar     *signal,
                   GVariant        *params,
                   gpointer         user_data)
{
	LrgNotification *self = LRG_NOTIFICATION (user_data);
	guint32          id = 0;
	const gchar     *key = NULL;

	if (!g_variant_is_of_type (params, G_VARIANT_TYPE ("(us)")))
		return;
	g_variant_get (params, "(u&s)", &id, &key);
	if (id == 0 || id != self->id)
		return;

	if (g_strcmp0 (key, NOTIFY_DEFAULT_KEY) == 0)
		invoke (self, self->default_action, TRUE);
	else if (g_str_has_prefix (key, NOTIFY_BUTTON_PREFIX))
	{
		guint64 index = 0;

		if (g_ascii_string_to_unsigned (key + strlen (NOTIFY_BUTTON_PREFIX), 10, 0, G_MAXUINT,
		                                &index, NULL) &&
		    index < self->buttons->len)
		{
			/* Copy: the handler may clear the buttons */
			g_autofree gchar *action = g_strdup (g_array_index (self->buttons, Button, index).action);

			invoke (self, action, FALSE);
		}
	}
}

/* on_notification_closed: NotificationClosed (u id, u reason) */
static void
on_notification_closed (GDBusConnection *conn,
                        const gchar     *sender,
                        const gchar     *path,
                        const gchar     *iface,
                        const gchar     *signal,
                        GVariant        *params,
                        gpointer         user_data)
{
	LrgNotification *self = LRG_NOTIFICATION (user_data);
	guint32          id = 0;
	guint32          reason = 0;

	if (!g_variant_is_of_type (params, G_VARIANT_TYPE ("(uu)")))
		return;
	g_variant_get (params, "(uu)", &id, &reason);
	if (id == 0 || id != self->id)
		return;

	if (reason < LRG_NOTIFICATION_CLOSE_REASON_EXPIRED || reason > LRG_NOTIFICATION_CLOSE_REASON_UNDEFINED)
		reason = LRG_NOTIFICATION_CLOSE_REASON_UNDEFINED;
	finish_closed (self, (LrgNotificationCloseReason) reason);
}

/* subscribe: listen to the server's signals on @conn */
static void
subscribe (LrgNotification *self)
{
	if (self->sub_action != 0)
		return;
	self->sub_action =
		g_dbus_connection_signal_subscribe (self->conn, NOTIFY_BUS_NAME, NOTIFY_INTERFACE, "ActionInvoked",
		                                    NOTIFY_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		                                    on_action_invoked, self, NULL);
	self->sub_closed =
		g_dbus_connection_signal_subscribe (self->conn, NOTIFY_BUS_NAME, NOTIFY_INTERFACE, "NotificationClosed",
		                                    NOTIFY_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		                                    on_notification_closed, self, NULL);
}

/* notify_params: the (susssasa{sv}i) arguments of Notify */
static GVariant *
notify_params (LrgNotification *self)
{
	GVariantBuilder actions;
	GVariantBuilder hints;
	const gchar    *app_name;
	const gchar    *desktop_entry;
	guint           i;

	/* Actions: key / label pairs; "default" is the click on the body */
	g_variant_builder_init (&actions, G_VARIANT_TYPE ("as"));
	g_variant_builder_add (&actions, "s", NOTIFY_DEFAULT_KEY);
	g_variant_builder_add (&actions, "s", "");
	for (i = 0; i < self->buttons->len; i++)
	{
		g_autofree gchar *key = g_strdup_printf (NOTIFY_BUTTON_PREFIX "%u", i);

		g_variant_builder_add (&actions, "s", key);
		g_variant_builder_add (&actions, "s", g_array_index (self->buttons, Button, i).label);
	}

	/* Hints: urgency always, desktop-entry when known */
	g_variant_builder_init (&hints, G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_add (&hints, "{sv}", "urgency", g_variant_new_byte ((guchar) self->urgency));
	desktop_entry = self->desktop_entry != NULL ? self->desktop_entry : g_get_prgname ();
	if (desktop_entry != NULL)
		g_variant_builder_add (&hints, "{sv}", "desktop-entry", g_variant_new_string (desktop_entry));

	app_name = self->app_name != NULL ? self->app_name : g_get_application_name ();

	return g_variant_new ("(susss@as@a{sv}i)",
	                      app_name != NULL ? app_name : "",
	                      (guint32) self->id,
	                      self->icon_name != NULL ? self->icon_name : "",
	                      self->title != NULL ? self->title : "",
	                      self->body != NULL ? self->body : "",
	                      g_variant_builder_end (&actions),
	                      g_variant_builder_end (&hints),
	                      (gint32) self->timeout);
}

static void send_notify (LrgNotification *self,
                         GTask           *task);

/* close_on_server: CloseNotification (id), fire and forget */
static void
close_on_server (GDBusConnection *conn,
                 guint            id)
{
	g_dbus_connection_call (conn, NOTIFY_BUS_NAME, NOTIFY_PATH, NOTIFY_INTERFACE, "CloseNotification",
	                        g_variant_new ("(u)", (guint32) id), NULL, G_DBUS_CALL_FLAGS_NONE, -1,
	                        NULL, NULL, NULL);
}

/* next_waiting: send the next queued show request, if any */
static void
next_waiting (LrgNotification *self)
{
	GTask *task = g_queue_pop_head (&self->waiting);

	if (task != NULL)
		send_notify (self, task);
}

/*
 * on_notify_reply:
 *
 * Notify returned the id (or failed). Store the id, subscribe, and
 * mark the notification shown; a withdraw that came in meanwhile
 * closes it straight away.
 */
static void
on_notify_reply (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
	g_autoptr(GTask)    task = user_data;
	LrgNotification    *self = g_task_get_source_object (task);
	g_autoptr(GVariant) reply = NULL;
	g_autoptr(GError)   error = NULL;
	guint32             id = 0;

	self->in_flight = FALSE;
	reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
	if (reply == NULL)
	{
		if (self->backend != LRG_NOTIFICATION_BACKEND_DBUS)
			unsubscribe (self);
		fall_back (self, task, error);
		next_waiting (self);
		return;
	}

	g_variant_get (reply, "(u)", &id);

	if (self->withdraw_pending)
	{
		/* Withdrawn before the server answered: take it down again */
		self->withdraw_pending = FALSE;
		if (self->backend != LRG_NOTIFICATION_BACKEND_DBUS)
			unsubscribe (self);
		close_on_server (G_DBUS_CONNECTION (source), id);
		g_task_return_boolean (task, TRUE);
		next_waiting (self);
		return;
	}

	/* A toast shown earlier (e.g. before the server started) gives way */
	if (self->toast != NULL)
	{
		self->withdrawing = TRUE;
		lrg_toast_dismiss (self->toast);
		self->withdrawing = FALSE;
		drop_toast (self);
	}

	if (self->id != id)
	{
		self->id = id;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ID]);
	}
	set_backend (self, LRG_NOTIFICATION_BACKEND_DBUS);
	g_task_return_boolean (task, TRUE);
	next_waiting (self);
}

/* send_notify: Notify on the connection we have (takes @task) */
static void
send_notify (LrgNotification *self,
             GTask           *task)
{
	/* One call at a time so an update knows the id it replaces */
	if (self->in_flight)
	{
		g_queue_push_tail (&self->waiting, task);
		return;
	}
	self->in_flight = TRUE;

	/*
	 * Subscribe before the call: GDBus resolves the owner of the
	 * well-known sender name with a call of its own, which is answered
	 * before Notify can be, so no signal for this id can be missed.
	 */
	subscribe (self);
	g_dbus_connection_call (self->conn, NOTIFY_BUS_NAME, NOTIFY_PATH, NOTIFY_INTERFACE, "Notify",
	                        notify_params (self), G_VARIANT_TYPE ("(u)"), G_DBUS_CALL_FLAGS_NONE, -1,
	                        g_task_get_cancellable (task), on_notify_reply, task);
}

/* on_bus: the session bus connection is ready (or not) */
static void
on_bus (GObject      *source,
        GAsyncResult *result,
        gpointer      user_data)
{
	GTask                     *task = user_data;
	LrgNotification           *self = g_task_get_source_object (task);
	g_autoptr(GError)          error = NULL;
	g_autoptr(GDBusConnection) conn = g_bus_get_finish (result, &error);

	if (conn == NULL)
	{
		fall_back (self, task, error);
		g_object_unref (task);
		return;
	}

	/* Keep the connection while we may need it for updates / signals */
	if (self->conn != conn)
	{
		unsubscribe (self);
		g_set_object (&self->conn, conn);
	}
	send_notify (self, task);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_notification_dispose (GObject *object)
{
	LrgNotification *self = LRG_NOTIFICATION (object);

	unsubscribe (self);
	drop_toast (self);
	g_clear_object (&self->conn);
	g_clear_object (&self->toast_queue);

	G_OBJECT_CLASS (lrg_notification_parent_class)->dispose (object);
}

static void
lrg_notification_finalize (GObject *object)
{
	LrgNotification *self = LRG_NOTIFICATION (object);

	g_free (self->title);
	g_free (self->body);
	g_free (self->icon_name);
	g_free (self->default_action);
	g_free (self->app_name);
	g_free (self->desktop_entry);
	g_array_unref (self->buttons);
	g_weak_ref_clear (&self->anchor);

	G_OBJECT_CLASS (lrg_notification_parent_class)->finalize (object);
}

static void
lrg_notification_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	LrgNotification *self = LRG_NOTIFICATION (object);

	switch (prop_id)
	{
	case PROP_TITLE:
		g_value_set_string (value, self->title);
		break;
	case PROP_BODY:
		g_value_set_string (value, self->body);
		break;
	case PROP_ICON_NAME:
		g_value_set_string (value, self->icon_name);
		break;
	case PROP_URGENCY:
		g_value_set_enum (value, self->urgency);
		break;
	case PROP_TIMEOUT:
		g_value_set_int (value, self->timeout);
		break;
	case PROP_DEFAULT_ACTION:
		g_value_set_string (value, self->default_action);
		break;
	case PROP_APP_NAME:
		g_value_set_string (value, self->app_name);
		break;
	case PROP_DESKTOP_ENTRY:
		g_value_set_string (value, self->desktop_entry);
		break;
	case PROP_IN_APP:
		g_value_set_boolean (value, self->in_app);
		break;
	case PROP_TOAST_QUEUE:
		g_value_set_object (value, self->toast_queue);
		break;
	case PROP_ID:
		g_value_set_uint (value, self->id);
		break;
	case PROP_BACKEND:
		g_value_set_enum (value, self->backend);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_notification_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	LrgNotification *self = LRG_NOTIFICATION (object);

	switch (prop_id)
	{
	case PROP_TITLE:
		lrg_notification_set_title (self, g_value_get_string (value));
		break;
	case PROP_BODY:
		lrg_notification_set_body (self, g_value_get_string (value));
		break;
	case PROP_ICON_NAME:
		lrg_notification_set_icon_name (self, g_value_get_string (value));
		break;
	case PROP_URGENCY:
		lrg_notification_set_urgency (self, (LrgNotificationUrgency) g_value_get_enum (value));
		break;
	case PROP_TIMEOUT:
		lrg_notification_set_timeout (self, g_value_get_int (value));
		break;
	case PROP_DEFAULT_ACTION:
		lrg_notification_set_default_action (self, g_value_get_string (value));
		break;
	case PROP_APP_NAME:
		lrg_notification_set_app_name (self, g_value_get_string (value));
		break;
	case PROP_DESKTOP_ENTRY:
		lrg_notification_set_desktop_entry (self, g_value_get_string (value));
		break;
	case PROP_IN_APP:
		lrg_notification_set_in_app (self, g_value_get_boolean (value));
		break;
	case PROP_TOAST_QUEUE:
		lrg_notification_set_toast_queue (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_notification_class_init (LrgNotificationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_notification_dispose;
	object_class->finalize = lrg_notification_finalize;
	object_class->get_property = lrg_notification_get_property;
	object_class->set_property = lrg_notification_set_property;

	/**
	 * LrgNotification:title:
	 *
	 * The summary line.
	 */
	properties[PROP_TITLE] =
		g_param_spec_string ("title", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNotification:body:
	 *
	 * Detail text under the title.
	 */
	properties[PROP_BODY] =
		g_param_spec_string ("body", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNotification:icon-name:
	 *
	 * A freedesktop icon name or file:// URI.
	 */
	properties[PROP_ICON_NAME] =
		g_param_spec_string ("icon-name", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNotification:urgency:
	 *
	 * How important the notification is.
	 */
	properties[PROP_URGENCY] =
		g_param_spec_enum ("urgency", NULL, NULL, LRG_TYPE_NOTIFICATION_URGENCY,
		                   LRG_NOTIFICATION_URGENCY_NORMAL,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNotification:timeout:
	 *
	 * Milliseconds before it expires: -1 for the server default, 0 for never.
	 */
	properties[PROP_TIMEOUT] =
		g_param_spec_int ("timeout", NULL, NULL, -1, G_MAXINT, -1,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNotification:default-action:
	 *
	 * Detailed action activated on the anchor when the notification is clicked.
	 */
	properties[PROP_DEFAULT_ACTION] =
		g_param_spec_string ("default-action", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNotification:app-name:
	 *
	 * Application name sent to the server (%NULL: g_get_application_name()).
	 */
	properties[PROP_APP_NAME] =
		g_param_spec_string ("app-name", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNotification:desktop-entry:
	 *
	 * Desktop file name for the "desktop-entry" hint (%NULL: g_get_prgname()).
	 */
	properties[PROP_DESKTOP_ENTRY] =
		g_param_spec_string ("desktop-entry", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNotification:in-app:
	 *
	 * Always show as an in-app toast.
	 */
	properties[PROP_IN_APP] =
		g_param_spec_boolean ("in-app", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNotification:toast-queue:
	 *
	 * The toast queue of the in-app fallback (%NULL: the default queue).
	 */
	properties[PROP_TOAST_QUEUE] =
		g_param_spec_object ("toast-queue", NULL, NULL, LRG_TYPE_TOAST_QUEUE,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNotification:id:
	 *
	 * The server's id while shown by a notification server, else 0.
	 */
	properties[PROP_ID] =
		g_param_spec_uint ("id", NULL, NULL, 0, G_MAXUINT32, 0,
		                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNotification:backend:
	 *
	 * Where the notification is shown right now.
	 */
	properties[PROP_BACKEND] =
		g_param_spec_enum ("backend", NULL, NULL, LRG_TYPE_NOTIFICATION_BACKEND,
		                   LRG_NOTIFICATION_BACKEND_NONE,
		                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgNotification::activated:
	 * @self: the notification
	 *
	 * Emitted when the notification itself is clicked (the default
	 * action), after #LrgNotification:default-action was activated.
	 */
	signals[SIGNAL_ACTIVATED] =
		g_signal_new ("activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgNotification::action-invoked:
	 * @self: the notification
	 * @action: the detailed action name of the default action or button
	 *
	 * Emitted after an action was activated on the anchor (also when no
	 * anchor or action group could take it).
	 */
	signals[SIGNAL_ACTION_INVOKED] =
		g_signal_new ("action-invoked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgNotification::closed:
	 * @self: the notification
	 * @reason: why it went away
	 *
	 * Emitted once when a shown notification goes away.
	 */
	signals[SIGNAL_CLOSED] =
		g_signal_new ("closed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_NOTIFICATION_CLOSE_REASON);
}

static void
lrg_notification_init (LrgNotification *self)
{
	self->urgency = LRG_NOTIFICATION_URGENCY_NORMAL;
	self->timeout = -1;
	self->buttons = g_array_new (FALSE, TRUE, sizeof (Button));
	g_array_set_clear_func (self->buttons, button_clear);
	g_weak_ref_init (&self->anchor, NULL);
	g_queue_init (&self->waiting);
}

/* ==========================================================================
 * Public API: content
 * ========================================================================== */

LrgNotification *
lrg_notification_new (const gchar *title)
{
	return g_object_new (LRG_TYPE_NOTIFICATION, "title", title, NULL);
}

/* set_string: replace a string field and notify when it changed */
static void
set_string (LrgNotification  *self,
            gchar           **field,
            const gchar      *value,
            guint             prop)
{
	if (g_strcmp0 (*field, value) == 0)
		return;
	g_free (*field);
	*field = g_strdup (value);
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

const gchar *
lrg_notification_get_title (LrgNotification *self)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), NULL);
	return self->title;
}

void
lrg_notification_set_title (LrgNotification *self,
                            const gchar     *title)
{
	g_return_if_fail (LRG_IS_NOTIFICATION (self));
	set_string (self, &self->title, title, PROP_TITLE);
}

const gchar *
lrg_notification_get_body (LrgNotification *self)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), NULL);
	return self->body;
}

void
lrg_notification_set_body (LrgNotification *self,
                           const gchar     *body)
{
	g_return_if_fail (LRG_IS_NOTIFICATION (self));
	set_string (self, &self->body, body, PROP_BODY);
}

const gchar *
lrg_notification_get_icon_name (LrgNotification *self)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), NULL);
	return self->icon_name;
}

void
lrg_notification_set_icon_name (LrgNotification *self,
                                const gchar     *icon_name)
{
	g_return_if_fail (LRG_IS_NOTIFICATION (self));
	set_string (self, &self->icon_name, icon_name, PROP_ICON_NAME);
}

LrgNotificationUrgency
lrg_notification_get_urgency (LrgNotification *self)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), LRG_NOTIFICATION_URGENCY_NORMAL);
	return self->urgency;
}

void
lrg_notification_set_urgency (LrgNotification        *self,
                              LrgNotificationUrgency  urgency)
{
	g_return_if_fail (LRG_IS_NOTIFICATION (self));
	g_return_if_fail (urgency <= LRG_NOTIFICATION_URGENCY_CRITICAL);

	if (self->urgency == urgency)
		return;
	self->urgency = urgency;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_URGENCY]);
}

gint
lrg_notification_get_timeout (LrgNotification *self)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), -1);
	return self->timeout;
}

void
lrg_notification_set_timeout (LrgNotification *self,
                              gint             timeout_ms)
{
	g_return_if_fail (LRG_IS_NOTIFICATION (self));

	timeout_ms = MAX (timeout_ms, -1);
	if (self->timeout == timeout_ms)
		return;
	self->timeout = timeout_ms;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIMEOUT]);
}

const gchar *
lrg_notification_get_default_action (LrgNotification *self)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), NULL);
	return self->default_action;
}

void
lrg_notification_set_default_action (LrgNotification *self,
                                     const gchar     *detailed_action)
{
	g_return_if_fail (LRG_IS_NOTIFICATION (self));
	set_string (self, &self->default_action, detailed_action, PROP_DEFAULT_ACTION);
}

void
lrg_notification_add_button (LrgNotification *self,
                             const gchar     *label,
                             const gchar     *detailed_action)
{
	Button b;

	g_return_if_fail (LRG_IS_NOTIFICATION (self));
	g_return_if_fail (label != NULL);
	g_return_if_fail (detailed_action != NULL);

	b.label = g_strdup (label);
	b.action = g_strdup (detailed_action);
	g_array_append_val (self->buttons, b);
}

guint
lrg_notification_get_n_buttons (LrgNotification *self)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), 0);
	return self->buttons->len;
}

gboolean
lrg_notification_get_button (LrgNotification  *self,
                             guint             index,
                             const gchar     **label,
                             const gchar     **detailed_action)
{
	const Button *b;

	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), FALSE);

	if (index >= self->buttons->len)
		return FALSE;
	b = &g_array_index (self->buttons, Button, index);
	if (label != NULL)
		*label = b->label;
	if (detailed_action != NULL)
		*detailed_action = b->action;
	return TRUE;
}

void
lrg_notification_clear_buttons (LrgNotification *self)
{
	g_return_if_fail (LRG_IS_NOTIFICATION (self));
	g_array_set_size (self->buttons, 0);
}

const gchar *
lrg_notification_get_app_name (LrgNotification *self)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), NULL);
	return self->app_name;
}

void
lrg_notification_set_app_name (LrgNotification *self,
                               const gchar     *app_name)
{
	g_return_if_fail (LRG_IS_NOTIFICATION (self));
	set_string (self, &self->app_name, app_name, PROP_APP_NAME);
}

const gchar *
lrg_notification_get_desktop_entry (LrgNotification *self)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), NULL);
	return self->desktop_entry;
}

void
lrg_notification_set_desktop_entry (LrgNotification *self,
                                    const gchar     *desktop_entry)
{
	g_return_if_fail (LRG_IS_NOTIFICATION (self));
	set_string (self, &self->desktop_entry, desktop_entry, PROP_DESKTOP_ENTRY);
}

gboolean
lrg_notification_get_in_app (LrgNotification *self)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), FALSE);
	return self->in_app;
}

void
lrg_notification_set_in_app (LrgNotification *self,
                             gboolean         in_app)
{
	g_return_if_fail (LRG_IS_NOTIFICATION (self));

	in_app = !!in_app;
	if (self->in_app == in_app)
		return;
	self->in_app = in_app;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IN_APP]);
}

LrgToastQueue *
lrg_notification_get_toast_queue (LrgNotification *self)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), NULL);
	return self->toast_queue;
}

void
lrg_notification_set_toast_queue (LrgNotification *self,
                                  LrgToastQueue   *queue)
{
	g_return_if_fail (LRG_IS_NOTIFICATION (self));
	g_return_if_fail (queue == NULL || LRG_IS_TOAST_QUEUE (queue));

	if (g_set_object (&self->toast_queue, queue))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TOAST_QUEUE]);
}

guint
lrg_notification_get_id (LrgNotification *self)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), 0);
	return self->id;
}

LrgNotificationBackend
lrg_notification_get_backend (LrgNotification *self)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), LRG_NOTIFICATION_BACKEND_NONE);
	return self->backend;
}

LrgToast *
lrg_notification_get_toast (LrgNotification *self)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), NULL);
	return self->toast;
}

/* ==========================================================================
 * Public API: showing
 * ========================================================================== */

void
lrg_notification_show_async (LrgNotification     *self,
                             LrgWidget           *anchor,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
	GTask *task;

	g_return_if_fail (LRG_IS_NOTIFICATION (self));
	g_return_if_fail (anchor == NULL || LRG_IS_WIDGET (anchor));

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, lrg_notification_show_async);
	g_weak_ref_set (&self->anchor, anchor);
	self->withdraw_pending = FALSE;

	/*
	 * In-app (forced, or already showing as a toast because there was
	 * no server): the toast is the whole story.
	 */
	if (use_in_app (self) || self->backend == LRG_NOTIFICATION_BACKEND_TOAST)
	{
		if (show_toast (self))
			g_task_return_boolean (task, TRUE);
		else
			g_task_return_new_error (task, LRG_NOTIFICATION_ERROR, LRG_NOTIFICATION_ERROR_NO_BACKEND,
			                         "In-app notification without a toast queue");
		g_object_unref (task);
		return;
	}

	/* Shown before on this connection: send the update right away */
	if (self->conn != NULL && !g_dbus_connection_is_closed (self->conn))
	{
		send_notify (self, task);
		return;
	}

	g_bus_get (G_BUS_TYPE_SESSION, cancellable, on_bus, task);
}

gboolean
lrg_notification_show_finish (LrgNotification  *self,
                              GAsyncResult     *result,
                              GError          **error)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, self), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/* on_shown: completion of the fire-and-forget show */
static void
on_shown (GObject      *source,
          GAsyncResult *result,
          gpointer      user_data)
{
	g_autoptr(GError) error = NULL;

	if (!lrg_notification_show_finish (LRG_NOTIFICATION (source), result, &error))
		lrg_warning (LRG_LOG_DOMAIN, "notification \"%s\" not shown: %s",
		             LRG_NOTIFICATION (source)->title != NULL ? LRG_NOTIFICATION (source)->title : "",
		             error->message);
}

gboolean
lrg_notification_show (LrgNotification  *self,
                       LrgWidget        *anchor,
                       GError          **error)
{
	g_return_val_if_fail (LRG_IS_NOTIFICATION (self), FALSE);
	g_return_val_if_fail (anchor == NULL || LRG_IS_WIDGET (anchor), FALSE);

	/* The one failure we can see without waiting */
	if (use_in_app (self) && resolve_queue (self) == NULL &&
	    (self->toast == NULL || lrg_toast_get_dismissed (self->toast)))
	{
		g_set_error (error, LRG_NOTIFICATION_ERROR, LRG_NOTIFICATION_ERROR_NO_BACKEND,
		             "In-app notification without a toast queue");
		return FALSE;
	}

	lrg_notification_show_async (self, anchor, NULL, on_shown, NULL);
	return TRUE;
}

void
lrg_notification_withdraw (LrgNotification *self)
{
	g_return_if_fail (LRG_IS_NOTIFICATION (self));

	switch (self->backend)
	{
	case LRG_NOTIFICATION_BACKEND_DBUS:
		if (self->conn != NULL && self->id != 0)
			close_on_server (self->conn, self->id);
		/* An update still in flight must not bring it back */
		if (self->in_flight)
			self->withdraw_pending = TRUE;
		finish_closed (self, LRG_NOTIFICATION_CLOSE_REASON_CLOSED);
		break;

	case LRG_NOTIFICATION_BACKEND_TOAST:
		self->withdrawing = TRUE;
		if (self->toast != NULL)
			lrg_toast_dismiss (self->toast);
		self->withdrawing = FALSE;
		finish_closed (self, LRG_NOTIFICATION_CLOSE_REASON_CLOSED);
		break;

	case LRG_NOTIFICATION_BACKEND_NONE:
	default:
		/* Not up yet: close it as soon as the server answers */
		if (self->in_flight)
			self->withdraw_pending = TRUE;
		break;
	}
}

/* ==========================================================================
 * Public API: process-wide settings
 * ========================================================================== */

void
lrg_notification_set_default_toast_queue (LrgToastQueue *queue)
{
	g_return_if_fail (queue == NULL || LRG_IS_TOAST_QUEUE (queue));

	g_weak_ref_set (&default_queue, queue);
}

LrgToastQueue *
lrg_notification_get_default_toast_queue (void)
{
	LrgToastQueue *queue = g_weak_ref_get (&default_queue);

	/* transfer none: the canvas tree owns the queue */
	if (queue != NULL)
		g_object_unref (queue);
	return queue;
}

void
lrg_notification_set_force_in_app (gboolean force)
{
	force_in_app = !!force;
}

gboolean
lrg_notification_get_force_in_app (void)
{
	return force_in_app;
}
