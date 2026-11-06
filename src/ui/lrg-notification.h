/* lrg-notification.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNotification: desktop notifications over org.freedesktop.Notifications,
 * with an in-app toast fallback.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "lrg-widget.h"
#include "lrg-toast-queue.h"

G_BEGIN_DECLS

/**
 * LrgNotificationUrgency:
 * @LRG_NOTIFICATION_URGENCY_LOW: background information
 * @LRG_NOTIFICATION_URGENCY_NORMAL: the default
 * @LRG_NOTIFICATION_URGENCY_CRITICAL: needs attention now; servers
 *   usually keep it up until it is dismissed
 *
 * How important a notification is; sent as the "urgency" hint (0, 1,
 * 2) and mapped to the toast kind in the in-app fallback.
 */
typedef enum
{
	LRG_NOTIFICATION_URGENCY_LOW,
	LRG_NOTIFICATION_URGENCY_NORMAL,
	LRG_NOTIFICATION_URGENCY_CRITICAL
} LrgNotificationUrgency;

LRG_AVAILABLE_IN_ALL
GType lrg_notification_urgency_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_NOTIFICATION_URGENCY (lrg_notification_urgency_get_type ())

/**
 * LrgNotificationCloseReason:
 * @LRG_NOTIFICATION_CLOSE_REASON_EXPIRED: its timeout ran out
 * @LRG_NOTIFICATION_CLOSE_REASON_DISMISSED: the user dismissed it
 * @LRG_NOTIFICATION_CLOSE_REASON_CLOSED: the application withdrew it
 *   (lrg_notification_withdraw())
 * @LRG_NOTIFICATION_CLOSE_REASON_UNDEFINED: the server did not say
 *
 * Why a notification went away; the values match the reasons of the
 * NotificationClosed D-Bus signal.
 */
typedef enum
{
	LRG_NOTIFICATION_CLOSE_REASON_EXPIRED = 1,
	LRG_NOTIFICATION_CLOSE_REASON_DISMISSED = 2,
	LRG_NOTIFICATION_CLOSE_REASON_CLOSED = 3,
	LRG_NOTIFICATION_CLOSE_REASON_UNDEFINED = 4
} LrgNotificationCloseReason;

LRG_AVAILABLE_IN_ALL
GType lrg_notification_close_reason_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_NOTIFICATION_CLOSE_REASON (lrg_notification_close_reason_get_type ())

/**
 * LrgNotificationBackend:
 * @LRG_NOTIFICATION_BACKEND_NONE: not shown
 * @LRG_NOTIFICATION_BACKEND_DBUS: shown by the desktop's notification
 *   server
 * @LRG_NOTIFICATION_BACKEND_TOAST: shown in-app as an #LrgToast
 *
 * Where a notification is currently shown.
 */
typedef enum
{
	LRG_NOTIFICATION_BACKEND_NONE,
	LRG_NOTIFICATION_BACKEND_DBUS,
	LRG_NOTIFICATION_BACKEND_TOAST
} LrgNotificationBackend;

LRG_AVAILABLE_IN_ALL
GType lrg_notification_backend_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_NOTIFICATION_BACKEND (lrg_notification_backend_get_type ())

/**
 * LRG_NOTIFICATION_ERROR:
 *
 * Error domain for #LrgNotification. Errors in this domain are from
 * the #LrgNotificationError enumeration.
 */
#define LRG_NOTIFICATION_ERROR (lrg_notification_error_quark ())

/**
 * LrgNotificationError:
 * @LRG_NOTIFICATION_ERROR_NO_BACKEND: no notification server answered
 *   and no toast queue was available for the in-app fallback
 * @LRG_NOTIFICATION_ERROR_FAILED: the server rejected the notification
 *
 * Error codes for #LRG_NOTIFICATION_ERROR.
 */
typedef enum
{
	LRG_NOTIFICATION_ERROR_NO_BACKEND,
	LRG_NOTIFICATION_ERROR_FAILED
} LrgNotificationError;

/**
 * lrg_notification_error_quark:
 *
 * Returns: the error quark for #LRG_NOTIFICATION_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_notification_error_quark (void);

#define LRG_TYPE_NOTIFICATION (lrg_notification_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNotification, lrg_notification, LRG, NOTIFICATION, GObject)

/**
 * lrg_notification_new:
 * @title: the summary line
 *
 * Returns: (transfer full): a new #LrgNotification
 */
LRG_AVAILABLE_IN_ALL
LrgNotification * lrg_notification_new (const gchar *title);

/**
 * lrg_notification_get_title:
 * @self: an #LrgNotification
 *
 * Returns: (nullable): the title (summary)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_notification_get_title (LrgNotification *self);

/**
 * lrg_notification_set_title:
 * @self: an #LrgNotification
 * @title: (nullable): the summary line
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_set_title (LrgNotification *self,
                                 const gchar     *title);

/**
 * lrg_notification_get_body:
 * @self: an #LrgNotification
 *
 * Returns: (nullable): the body text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_notification_get_body (LrgNotification *self);

/**
 * lrg_notification_set_body:
 * @self: an #LrgNotification
 * @body: (nullable): detail text under the title
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_set_body (LrgNotification *self,
                                const gchar     *body);

/**
 * lrg_notification_get_icon_name:
 * @self: an #LrgNotification
 *
 * Returns: (nullable): the icon name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_notification_get_icon_name (LrgNotification *self);

/**
 * lrg_notification_set_icon_name:
 * @self: an #LrgNotification
 * @icon_name: (nullable): a freedesktop icon name or a file:// URI,
 *   sent as the app_icon argument
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_set_icon_name (LrgNotification *self,
                                     const gchar     *icon_name);

/**
 * lrg_notification_get_urgency:
 * @self: an #LrgNotification
 *
 * Returns: the urgency
 */
LRG_AVAILABLE_IN_ALL
LrgNotificationUrgency lrg_notification_get_urgency (LrgNotification *self);

/**
 * lrg_notification_set_urgency:
 * @self: an #LrgNotification
 * @urgency: how important it is (default normal)
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_set_urgency (LrgNotification        *self,
                                   LrgNotificationUrgency  urgency);

/**
 * lrg_notification_get_timeout:
 * @self: an #LrgNotification
 *
 * Returns: the timeout in milliseconds, -1 for the server default, 0
 *   for never
 */
LRG_AVAILABLE_IN_ALL
gint lrg_notification_get_timeout (LrgNotification *self);

/**
 * lrg_notification_set_timeout:
 * @self: an #LrgNotification
 * @timeout_ms: milliseconds before it expires; -1 (default) lets the
 *   server decide, 0 keeps it until it is dismissed
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_set_timeout (LrgNotification *self,
                                   gint             timeout_ms);

/**
 * lrg_notification_get_default_action:
 * @self: an #LrgNotification
 *
 * Returns: (nullable): the detailed action activated by a click on the
 *   notification itself
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_notification_get_default_action (LrgNotification *self);

/**
 * lrg_notification_set_default_action:
 * @self: an #LrgNotification
 * @detailed_action: (nullable): a detailed action name such as
 *   "app.show-inbox" or "win.open::42" (see g_action_parse_detailed_name()),
 *   activated on the anchor widget when the notification is clicked
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_set_default_action (LrgNotification *self,
                                          const gchar     *detailed_action);

/**
 * lrg_notification_add_button:
 * @self: an #LrgNotification
 * @label: the button label
 * @detailed_action: a detailed action name activated on the anchor
 *   widget when the button is pressed
 *
 * Adds a button. Notification servers show a few (usually up to three).
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_add_button (LrgNotification *self,
                                  const gchar     *label,
                                  const gchar     *detailed_action);

/**
 * lrg_notification_get_n_buttons:
 * @self: an #LrgNotification
 *
 * Returns: the number of buttons
 */
LRG_AVAILABLE_IN_ALL
guint lrg_notification_get_n_buttons (LrgNotification *self);

/**
 * lrg_notification_get_button:
 * @self: an #LrgNotification
 * @index: a button index
 * @label: (out) (optional) (transfer none): the label
 * @detailed_action: (out) (optional) (transfer none): the action
 *
 * Returns: %TRUE if @index is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_notification_get_button (LrgNotification  *self,
                                      guint             index,
                                      const gchar     **label,
                                      const gchar     **detailed_action);

/**
 * lrg_notification_clear_buttons:
 * @self: an #LrgNotification
 *
 * Removes every button.
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_clear_buttons (LrgNotification *self);

/**
 * lrg_notification_get_app_name:
 * @self: an #LrgNotification
 *
 * Returns: (nullable): the application name sent to the server
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_notification_get_app_name (LrgNotification *self);

/**
 * lrg_notification_set_app_name:
 * @self: an #LrgNotification
 * @app_name: (nullable): the application name; %NULL uses
 *   g_get_application_name()
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_set_app_name (LrgNotification *self,
                                    const gchar     *app_name);

/**
 * lrg_notification_get_desktop_entry:
 * @self: an #LrgNotification
 *
 * Returns: (nullable): the desktop entry hint
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_notification_get_desktop_entry (LrgNotification *self);

/**
 * lrg_notification_set_desktop_entry:
 * @self: an #LrgNotification
 * @desktop_entry: (nullable): the application's desktop file name
 *   without ".desktop" (e.g. "org.example.Game"), sent as the
 *   "desktop-entry" hint so the server can group and theme it; %NULL
 *   uses g_get_prgname()
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_set_desktop_entry (LrgNotification *self,
                                         const gchar     *desktop_entry);

/**
 * lrg_notification_get_in_app:
 * @self: an #LrgNotification
 *
 * Returns: %TRUE if this notification always shows as a toast
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_notification_get_in_app (LrgNotification *self);

/**
 * lrg_notification_set_in_app:
 * @self: an #LrgNotification
 * @in_app: show as an in-app toast even when a notification server
 *   exists (see also lrg_notification_set_force_in_app())
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_set_in_app (LrgNotification *self,
                                  gboolean         in_app);

/**
 * lrg_notification_get_toast_queue:
 * @self: an #LrgNotification
 *
 * Returns: (transfer none) (nullable): the toast queue set on this
 *   notification
 */
LRG_AVAILABLE_IN_ALL
LrgToastQueue * lrg_notification_get_toast_queue (LrgNotification *self);

/**
 * lrg_notification_set_toast_queue:
 * @self: an #LrgNotification
 * @queue: (nullable): the queue for the in-app fallback; %NULL uses
 *   the default queue (lrg_notification_set_default_toast_queue())
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_set_toast_queue (LrgNotification *self,
                                       LrgToastQueue   *queue);

/**
 * lrg_notification_get_id:
 * @self: an #LrgNotification
 *
 * Returns: the server's id for the shown notification, 0 when not
 *   shown by a server
 */
LRG_AVAILABLE_IN_ALL
guint lrg_notification_get_id (LrgNotification *self);

/**
 * lrg_notification_get_backend:
 * @self: an #LrgNotification
 *
 * Returns: where the notification is shown right now
 */
LRG_AVAILABLE_IN_ALL
LrgNotificationBackend lrg_notification_get_backend (LrgNotification *self);

/**
 * lrg_notification_get_toast:
 * @self: an #LrgNotification
 *
 * Returns: (transfer none) (nullable): the toast of the in-app
 *   fallback while it is up
 */
LRG_AVAILABLE_IN_ALL
LrgToast * lrg_notification_get_toast (LrgNotification *self);

/**
 * lrg_notification_show_async:
 * @self: an #LrgNotification
 * @anchor: (nullable): the widget actions are activated on (its
 *   action groups and the canvas's "app" / "ui" groups); %NULL uses
 *   the toast queue
 * @cancellable: (nullable): a #GCancellable
 * @callback: (scope async): called when the notification is up
 * @user_data: (closure): data for @callback
 *
 * Shows the notification, or updates it in place when it is already
 * shown (the server replaces it; a toast changes its text). Unless
 * in-app display is forced, it is sent to org.freedesktop.Notifications
 * on the session bus; when there is no bus or no server it falls back
 * to an #LrgToast in the toast queue. While shown, the notification
 * keeps itself alive until it is closed or withdrawn.
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_show_async (LrgNotification     *self,
                                  LrgWidget           *anchor,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data);

/**
 * lrg_notification_show_finish:
 * @self: an #LrgNotification
 * @result: the #GAsyncResult
 * @error: return location for a #GError
 *
 * Returns: %TRUE if the notification is shown (see
 *   lrg_notification_get_backend() for where)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_notification_show_finish (LrgNotification  *self,
                                       GAsyncResult     *result,
                                       GError          **error);

/**
 * lrg_notification_show:
 * @self: an #LrgNotification
 * @anchor: (nullable): the widget actions are activated on, or %NULL
 * @error: return location for a #GError
 *
 * Fire-and-forget lrg_notification_show_async(): it never blocks. The
 * D-Bus call completes in the main loop (iterate the default main
 * context, which a running app does); failures there fall back to the
 * toast queue, or are logged. Fails immediately only when in-app
 * display is forced and no toast queue is available.
 *
 * Returns: %TRUE if the notification is on its way
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_notification_show (LrgNotification  *self,
                                LrgWidget        *anchor,
                                GError          **error);

/**
 * lrg_notification_withdraw:
 * @self: an #LrgNotification
 *
 * Takes the notification down (CloseNotification, or dismisses the
 * toast) and emits #LrgNotification::closed with
 * %LRG_NOTIFICATION_CLOSE_REASON_CLOSED. Does nothing when it is not
 * shown.
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_withdraw (LrgNotification *self);

/**
 * lrg_notification_set_default_toast_queue:
 * @queue: (nullable): the toast queue in-app notifications go to when
 *   they have none of their own (held weakly)
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_set_default_toast_queue (LrgToastQueue *queue);

/**
 * lrg_notification_get_default_toast_queue:
 *
 * Returns: (transfer none) (nullable): the default toast queue
 */
LRG_AVAILABLE_IN_ALL
LrgToastQueue * lrg_notification_get_default_toast_queue (void);

/**
 * lrg_notification_set_force_in_app:
 * @force: show every notification as an in-app toast (a game in
 *   fullscreen, a kiosk, a setting in the options menu)
 */
LRG_AVAILABLE_IN_ALL
void lrg_notification_set_force_in_app (gboolean force);

/**
 * lrg_notification_get_force_in_app:
 *
 * Returns: %TRUE if every notification is shown in-app
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_notification_get_force_in_app (void);

G_END_DECLS
