/* lrg-editor-server.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The editor's IPC endpoint.
 *
 * LrgEditorServer exposes an #LrgEditor over a Unix domain socket with a
 * line-delimited JSON protocol, so external tools (the first-party Emacs
 * package libregnum.el, scripts, other editors) can inspect and edit the
 * open level, drive play mode and receive live events.
 *
 * Wire format (one JSON object per line, UTF-8):
 *
 *   request   {"id": 7, "method": "node.get", "params": {"node": "House/Door"}}
 *   response  {"id": 7, "result": {...}}
 *             {"id": 7, "error": {"code": "not-found", "message": "..."}}
 *   event     {"event": "selection", "data": {"nodes": ["<guid>"], ...}}
 *
 * A client should start with "hello", optionally declaring capabilities
 * (e.g. "open-file": it can visit files for the user) and the events it
 * wants. Node parameters accept a GUID or a slash-separated name path.
 * See docs/editor/ipc-protocol.org for every method and event.
 *
 * The server runs on the thread-default main context of the thread that
 * calls lrg_editor_server_start(); the editor application iterates that
 * context every frame and the headless server runs a #GMainLoop.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <json-glib/json-glib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_SERVER_PROTOCOL_VERSION:
 *
 * Version of the IPC protocol; bumped on incompatible changes.
 */
#define LRG_EDITOR_SERVER_PROTOCOL_VERSION (1)

/**
 * LRG_EDITOR_SERVER_ERROR:
 *
 * Error domain for IPC requests.
 */
#define LRG_EDITOR_SERVER_ERROR (lrg_editor_server_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_editor_server_error_quark (void);

/**
 * LrgEditorServerError:
 * @LRG_EDITOR_SERVER_ERROR_PARSE: the line is not valid JSON
 * @LRG_EDITOR_SERVER_ERROR_INVALID_REQUEST: not a request object
 * @LRG_EDITOR_SERVER_ERROR_METHOD_NOT_FOUND: unknown method
 * @LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS: missing or mistyped parameters
 * @LRG_EDITOR_SERVER_ERROR_NOT_FOUND: the referenced node/file/action does
 *   not exist
 * @LRG_EDITOR_SERVER_ERROR_FAILED: the operation failed
 *
 * IPC error codes. On the wire they appear as the enum nick
 * ("parse", "invalid-request", ...).
 */
typedef enum
{
	LRG_EDITOR_SERVER_ERROR_PARSE,
	LRG_EDITOR_SERVER_ERROR_INVALID_REQUEST,
	LRG_EDITOR_SERVER_ERROR_METHOD_NOT_FOUND,
	LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
	LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
	LRG_EDITOR_SERVER_ERROR_FAILED
} LrgEditorServerError;

/**
 * LrgEditorServerMethod:
 * @server: the server
 * @params: (nullable): the request's "params" object
 * @user_data: (closure): data given at registration
 * @error: return location for an error
 *
 * Handler for one IPC method.
 *
 * Returns: (transfer full) (nullable): the result; %NULL with @error unset
 *   means a JSON null result
 */
typedef JsonNode * (*LrgEditorServerMethod) (LrgEditorServer  *server,
                                             JsonObject       *params,
                                             gpointer          user_data,
                                             GError          **error);

#define LRG_TYPE_EDITOR_SERVER (lrg_editor_server_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorServer, lrg_editor_server, LRG, EDITOR_SERVER, GObject)

/**
 * lrg_editor_server_new:
 * @editor: (transfer none): the editor to expose
 *
 * Creates a server with the built-in document methods registered. It does
 * not listen until lrg_editor_server_start().
 *
 * Returns: (transfer full): a new #LrgEditorServer
 */
LRG_AVAILABLE_IN_ALL
LrgEditorServer * lrg_editor_server_new (LrgEditor *editor);

/**
 * lrg_editor_server_get_editor:
 * @self: an #LrgEditorServer
 *
 * Returns: (transfer none): the exposed editor
 */
LRG_AVAILABLE_IN_ALL
LrgEditor * lrg_editor_server_get_editor (LrgEditorServer *self);

/**
 * lrg_editor_server_set_external_editor:
 * @self: an #LrgEditorServer
 * @external: (nullable) (transfer none): the launcher whose
 *   #LrgExternalEditor::open-file requests are offered to connected
 *   clients with the "open-file" capability
 *
 * Also used by the "file.open" and "script.open" methods.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_server_set_external_editor (LrgEditorServer   *self,
                                            LrgExternalEditor *external);

/**
 * lrg_editor_server_set_log:
 * @self: an #LrgEditorServer
 * @log: (nullable) (transfer none): a log whose records are streamed as
 *   "log" events and served by "log.tail"
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_server_set_log (LrgEditorServer *self,
                                LrgEditorLog    *log);

/**
 * lrg_editor_server_set_project_dir:
 * @self: an #LrgEditorServer
 * @dir: (nullable): the open project's root, reported by "editor.info"
 *   and used to resolve relative paths
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_server_set_project_dir (LrgEditorServer *self,
                                        const gchar     *dir);

/**
 * lrg_editor_server_get_project_dir:
 * @self: an #LrgEditorServer
 *
 * Returns: (transfer none) (nullable): the project root
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_server_get_project_dir (LrgEditorServer *self);

/**
 * lrg_editor_server_get_default_socket_path:
 *
 * Returns: (transfer full): `$LRG_EDITOR_SOCKET`, or
 *   `$XDG_RUNTIME_DIR/libregnum/editor-PID.sock`
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_server_get_default_socket_path (void);

/**
 * lrg_editor_server_start:
 * @self: an #LrgEditorServer
 * @socket_path: (nullable): where to listen; %NULL for the default path
 * @error: (nullable): return location for an error
 *
 * Starts listening. A stale socket file left by a crashed editor is
 * replaced; a socket another live editor is listening on is not.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_server_start (LrgEditorServer  *self,
                                  const gchar      *socket_path,
                                  GError          **error);

/**
 * lrg_editor_server_stop:
 * @self: an #LrgEditorServer
 *
 * Disconnects every client, stops listening and removes the socket file.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_server_stop (LrgEditorServer *self);

/**
 * lrg_editor_server_get_socket_path:
 * @self: an #LrgEditorServer
 *
 * Returns: (transfer none) (nullable): the listening socket, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_server_get_socket_path (LrgEditorServer *self);

/**
 * lrg_editor_server_get_n_clients:
 * @self: an #LrgEditorServer
 *
 * Returns: the number of connected clients
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_server_get_n_clients (LrgEditorServer *self);

/**
 * lrg_editor_server_has_capability:
 * @self: an #LrgEditorServer
 * @capability: e.g. "open-file"
 *
 * Returns: %TRUE if a connected client declared @capability in "hello"
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_server_has_capability (LrgEditorServer *self,
                                           const gchar     *capability);

/**
 * lrg_editor_server_register_method:
 * @self: an #LrgEditorServer
 * @name: method name, e.g. "play.start"
 * @method: (scope notified) (closure user_data) (destroy destroy): handler
 * @user_data: data for @method
 * @destroy: (nullable): frees @user_data
 *
 * Adds or replaces a method. The editor application registers play,
 * view, action and asset methods this way.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_server_register_method (LrgEditorServer       *self,
                                        const gchar           *name,
                                        LrgEditorServerMethod  method,
                                        gpointer               user_data,
                                        GDestroyNotify         destroy);

/**
 * lrg_editor_server_list_methods:
 * @self: an #LrgEditorServer
 *
 * Returns: (transfer container) (element-type utf8): sorted method names
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_editor_server_list_methods (LrgEditorServer *self);

/**
 * lrg_editor_server_emit_event:
 * @self: an #LrgEditorServer
 * @event: the event name
 * @data: (nullable) (transfer full): the event payload
 *
 * Sends an event to every client subscribed to @event.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_server_emit_event (LrgEditorServer *self,
                                   const gchar     *event,
                                   JsonNode        *data);

/**
 * lrg_editor_server_dispatch:
 * @self: an #LrgEditorServer
 * @line: one request line
 *
 * Handles a request in-process, exactly as if a client had sent it.
 * Used by the editor's own command console and by tests.
 *
 * Returns: (transfer full) (nullable): the response line (without the
 *   trailing newline), or %NULL for a request without an "id"
 *   (a notification)
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_server_dispatch (LrgEditorServer *self,
                                    const gchar     *line);

/**
 * lrg_editor_server_node_to_json:
 * @node: an #LrgNode
 * @recursive: include the children as nested objects (otherwise GUIDs)
 *
 * Serializes a node the way "node.get" reports it.
 *
 * Returns: (transfer full): a JSON object node
 */
LRG_AVAILABLE_IN_ALL
JsonNode * lrg_editor_server_node_to_json (LrgNode  *node,
                                           gboolean  recursive);

G_END_DECLS
