/* lrgeditorctl-client.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Client side of the editor's JSON-lines IPC socket, shared by
 * lrgeditorctl and the MCP bridge: finding running editor sessions,
 * calling methods and streaming events. See docs/editor/ipc-protocol.org.
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#define LRG_CTL_ERROR (lrg_ctl_error_quark ())

/**
 * LrgCtlError:
 * @LRG_CTL_ERROR_NO_SESSION: no running editor could be found
 * @LRG_CTL_ERROR_REMOTE: the editor answered with an error
 * @LRG_CTL_ERROR_PROTOCOL: the editor sent something unexpected
 * @LRG_CTL_ERROR_USAGE: the command line was wrong
 *
 * Errors of lrgeditorctl.
 */
typedef enum
{
	LRG_CTL_ERROR_NO_SESSION,
	LRG_CTL_ERROR_REMOTE,
	LRG_CTL_ERROR_PROTOCOL,
	LRG_CTL_ERROR_USAGE
} LrgCtlError;

GQuark lrg_ctl_error_quark (void);

/**
 * LrgCtlSession:
 * @socket: absolute socket path
 * @pid: the editor's process id (0 when unknown)
 * @project: (nullable): open project directory
 * @level: (nullable): open level file
 * @mtime: socket modification time (newest wins)
 *
 * A running editor found by lrg_ctl_list_sessions().
 */
typedef struct
{
	gchar  *socket;
	gint    pid;
	gchar  *project;
	gchar  *level;
	gint64  mtime;
} LrgCtlSession;

void lrg_ctl_session_free (LrgCtlSession *session);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgCtlSession, lrg_ctl_session_free)

/**
 * LrgCtlCallFunc:
 * @method: IPC method name
 * @params: (nullable): parameters object
 * @user_data: closure
 * @error: return location for an error
 *
 * Calls one editor method. lrgeditorctl and `lrgeditorctl mcp` call over
 * the socket; `lrgeditor --mcp` dispatches in-process.
 *
 * Returns: (transfer full) (nullable): the "result" member
 */
typedef JsonNode * (*LrgCtlCallFunc) (const gchar  *method,
                                      JsonObject   *params,
                                      gpointer      user_data,
                                      GError      **error);

gchar *       lrg_ctl_socket_dir       (void);
GPtrArray *   lrg_ctl_list_sessions    (void);
gchar *       lrg_ctl_resolve_session  (const gchar  *selector,
                                        GError      **error);
JsonNode *    lrg_ctl_call             (const gchar  *socket_path,
                                        const gchar  *method,
                                        JsonObject   *params,
                                        guint         timeout_ms,
                                        GError      **error);
JsonNode *    lrg_ctl_call_selected    (const gchar  *method,
                                        JsonObject   *params,
                                        gpointer      selector,
                                        GError      **error);

typedef gboolean (*LrgCtlEventFunc) (const gchar *event,
                                     JsonNode    *data,
                                     gpointer     user_data);

gboolean      lrg_ctl_follow_events    (const gchar          *socket_path,
                                        const gchar * const  *events,
                                        LrgCtlEventFunc       func,
                                        gpointer              user_data,
                                        GError              **error);

G_END_DECLS
