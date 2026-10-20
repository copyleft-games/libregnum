/* lrg-control-client.h - A blocking client for a game's control socket
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * #LrgControlClient speaks the #LrgControlServer protocol from a tool
 * (a `*ctl` command, a test, a bridge): JSON-RPC calls that wait for
 * their own reply, raw text lines, and an event stream after
 * lrg_control_client_subscribe().  Events that arrive while a call
 * waits are kept for lrg_control_client_read_event().
 */

#ifndef LRG_CONTROL_CLIENT_H
#define LRG_CONTROL_CLIENT_H

#include <glib-object.h>
#include <json-glib/json-glib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_CONTROL_CLIENT (lrg_control_client_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgControlClient, lrg_control_client, LRG, CONTROL_CLIENT, GObject)

/**
 * lrg_control_client_new:
 *
 * Returns: (transfer full): an unconnected client
 */
LRG_AVAILABLE_IN_ALL
LrgControlClient *lrg_control_client_new (void);

/**
 * lrg_control_client_connect:
 * @self: an #LrgControlClient
 * @path: the server's socket
 * @error: return location for a #GError
 *
 * Returns: %TRUE when connected
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_control_client_connect (LrgControlClient  *self,
                                     const gchar       *path,
                                     GError           **error);

/**
 * lrg_control_client_set_timeout:
 * @self: an #LrgControlClient
 * @timeout_ms: how long a call waits for its reply; 0 waits forever
 *   (the default is 30 000)
 */
LRG_AVAILABLE_IN_ALL
void lrg_control_client_set_timeout (LrgControlClient *self,
                                     guint             timeout_ms);

/**
 * lrg_control_client_call:
 * @self: a connected #LrgControlClient
 * @method: a command or built-in method
 * @params: (nullable): its parameters
 * @error: return location for a #GError
 *
 * Sends a JSON-RPC request and waits for its response.  A JSON-RPC
 * error becomes an %LRG_COMMAND_ERROR error
 * (lrg_command_error_from_jsonrpc_code()); a timeout
 * %G_IO_ERROR_TIMED_OUT; a closed connection %G_IO_ERROR_CLOSED.
 *
 * Returns: (transfer full) (nullable): the result, %NULL on error
 */
LRG_AVAILABLE_IN_ALL
JsonNode *lrg_control_client_call (LrgControlClient  *self,
                                   const gchar       *method,
                                   JsonObject        *params,
                                   GError           **error);

/**
 * lrg_control_client_call_line:
 * @self: a connected #LrgControlClient
 * @line: a text command
 * @error: return location for a #GError
 *
 * Sends a text-dialect line and waits for its `OK`/`ERROR` reply line.
 *
 * Returns: (transfer full) (nullable): the reply line without its
 *   newline, %NULL on a connection error
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_control_client_call_line (LrgControlClient  *self,
                                     const gchar       *line,
                                     GError           **error);

/**
 * lrg_control_client_subscribe:
 * @self: a connected #LrgControlClient
 * @topics: (nullable) (array zero-terminated=1): topics or globs;
 *   %NULL for every event
 * @error: return location for a #GError
 *
 * Returns: %TRUE when subscribed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_control_client_subscribe (LrgControlClient    *self,
                                       const gchar *const  *topics,
                                       GError             **error);

/**
 * lrg_control_client_read_event:
 * @self: a subscribed #LrgControlClient
 * @timeout_ms: how long to wait; 0 waits forever
 * @error: return location for a #GError
 *
 * The next event: the notification's `params` object (`event`, `seq`,
 * `time`, `data`).  %NULL without an error on a timeout.
 *
 * Returns: (transfer full) (nullable): the event
 */
LRG_AVAILABLE_IN_ALL
JsonNode *lrg_control_client_read_event (LrgControlClient  *self,
                                         guint              timeout_ms,
                                         GError           **error);

/**
 * lrg_control_client_close:
 * @self: an #LrgControlClient
 *
 * Closes the connection (also done on finalize).
 */
LRG_AVAILABLE_IN_ALL
void lrg_control_client_close (LrgControlClient *self);

G_END_DECLS

#endif /* LRG_CONTROL_CLIENT_H */
