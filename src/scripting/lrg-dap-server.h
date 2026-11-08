/* lrg-dap-server.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDapServer: Debug Adapter Protocol over TCP or stdio, driving an
 * #LrgScriptDebugger. One process, Content-Length framing, the same
 * shape as the language server protocol. The editor listens on TCP
 * (stdio is reserved by the in-process MCP server).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-script-debugger.h"

G_BEGIN_DECLS

/**
 * LRG_DAP_SERVER_ERROR:
 *
 * Error domain for #LrgDapServer. Codes are #LrgDapServerError.
 */
#define LRG_DAP_SERVER_ERROR (lrg_dap_server_error_quark ())

/**
 * lrg_dap_server_error_quark:
 *
 * Returns: the #GQuark of %LRG_DAP_SERVER_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_dap_server_error_quark (void);

/**
 * LrgDapServerError:
 * @LRG_DAP_SERVER_ERROR_FAILED: the server could not start or continue
 * @LRG_DAP_SERVER_ERROR_BIND: the TCP port could not be bound
 * @LRG_DAP_SERVER_ERROR_PROTOCOL: a client sent a message the server
 *   cannot parse
 *
 * Error codes in the %LRG_DAP_SERVER_ERROR domain.
 */
typedef enum
{
	LRG_DAP_SERVER_ERROR_FAILED,
	LRG_DAP_SERVER_ERROR_BIND,
	LRG_DAP_SERVER_ERROR_PROTOCOL
} LrgDapServerError;

/**
 * lrg_dap_server_error_get_type:
 *
 * Returns: the #GType of #LrgDapServerError
 */
LRG_AVAILABLE_IN_ALL
GType lrg_dap_server_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DAP_SERVER_ERROR (lrg_dap_server_error_get_type ())

#define LRG_TYPE_DAP_SERVER (lrg_dap_server_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDapServer, lrg_dap_server, LRG, DAP_SERVER, GObject)

/**
 * lrg_dap_server_new:
 *
 * Creates a server bound to lrg_script_debugger_get_default(). Nothing
 * listens until lrg_dap_server_start() or lrg_dap_server_start_stdio().
 *
 * Returns: (transfer full): a new #LrgDapServer
 */
LRG_AVAILABLE_IN_ALL
LrgDapServer * lrg_dap_server_new (void);

/**
 * lrg_dap_server_new_for_debugger:
 * @debugger: (nullable): the debugger requests act on, or %NULL
 *
 * Creates a server for @debugger. The server refs @debugger.
 *
 * Returns: (transfer full): a new #LrgDapServer
 */
LRG_AVAILABLE_IN_ALL
LrgDapServer * lrg_dap_server_new_for_debugger (LrgScriptDebugger *debugger);

/**
 * lrg_dap_server_set_debugger:
 * @self: an #LrgDapServer
 * @debugger: (nullable): the debugger requests act on
 *
 * Replaces the debugger. A listening server keeps its clients.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dap_server_set_debugger (LrgDapServer      *self,
                                  LrgScriptDebugger *debugger);

/**
 * lrg_dap_server_get_debugger:
 * @self: an #LrgDapServer
 *
 * Returns: (transfer none) (nullable): the debugger
 */
LRG_AVAILABLE_IN_ALL
LrgScriptDebugger * lrg_dap_server_get_debugger (LrgDapServer *self);

/**
 * lrg_dap_server_start:
 * @self: an #LrgDapServer
 * @host: (nullable): address to bind, or %NULL for 127.0.0.1
 * @port: TCP port, 0 for a free port
 * @error: (nullable): return location for a #GError
 *
 * Listens for one or more DAP clients. Safe to call again while already
 * listening (the existing port is kept and the call succeeds). Requests
 * are served on the thread-default #GMainContext of the caller, which is
 * also the context a stopped script must iterate so a pause can answer
 * stack and variable requests.
 *
 * Returns: %TRUE if the server is listening
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dap_server_start (LrgDapServer  *self,
                               const gchar   *host,
                               guint16        port,
                               GError       **error);

/**
 * lrg_dap_server_start_stdio:
 * @self: an #LrgDapServer
 * @error: (nullable): return location for a #GError
 *
 * Serves one DAP client on stdin/stdout (Content-Length framing). The
 * editor does not use this: its MCP server owns stdout. Unix only.
 *
 * Returns: %TRUE if the stdio client was attached
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dap_server_start_stdio (LrgDapServer  *self,
                                     GError       **error);

/**
 * lrg_dap_server_stop:
 * @self: an #LrgDapServer
 *
 * Closes every client and the listening socket. The debugger is left
 * attached to whatever it was attached to.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dap_server_stop (LrgDapServer *self);

/**
 * lrg_dap_server_get_listening:
 * @self: an #LrgDapServer
 *
 * Returns: whether a transport is active
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dap_server_get_listening (LrgDapServer *self);

/**
 * lrg_dap_server_get_port:
 * @self: an #LrgDapServer
 *
 * Returns: the bound TCP port, or 0 for stdio and when stopped
 */
LRG_AVAILABLE_IN_ALL
guint16 lrg_dap_server_get_port (LrgDapServer *self);

/**
 * lrg_dap_server_get_n_clients:
 * @self: an #LrgDapServer
 *
 * Returns: connected DAP clients
 */
LRG_AVAILABLE_IN_ALL
guint lrg_dap_server_get_n_clients (LrgDapServer *self);

/**
 * lrg_dap_server_get_host:
 * @self: an #LrgDapServer
 *
 * Returns: (transfer none) (nullable): the address passed to
 *   lrg_dap_server_start(), or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_dap_server_get_host (LrgDapServer *self);

G_END_DECLS
