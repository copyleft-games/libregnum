/* lrg-control-server.h - A game's control socket (text + JSON-RPC 2.0)
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * #LrgControlServer serves an #LrgCommandRegistry on a Unix stream
 * socket that only the owner can reach (the socket is mode 0600 in a
 * 0700 directory, and peers of another uid are hung up on).  One
 * connection speaks two dialects, line by line:
 *
 * - JSON-RPC 2.0, one JSON value per line: a line starting with `{` is a
 *   request, `[` a batch.  Methods are the command names plus
 *   `rpc.ping`, `rpc.discover`, `rpc.describe`, `auth`,
 *   `events.subscribe` and `events.unsubscribe`.  Events arrive as
 *   notifications `{"jsonrpc":"2.0","method":"event","params":{"event",
 *   "seq","time","data"}}`.
 * - Text: `walk-to 12 0 4.5` answers `OK <json>` or `ERROR <code>
 *   <message>`; `ping`, `help [COMMAND]`, `auth TOKEN`, `subscribe
 *   [TOPIC...]`, `unsubscribe` are built in; events arrive as
 *   `EVENT <topic> <json>`.
 *
 * Responses go out in request order per connection; a command that
 * completes later (walking somewhere) holds the replies after it.  The
 * server never blocks: lrg_control_server_poll() accepts, reads, runs
 * and writes what is ready, so a game calls it once per frame (or
 * lrg_control_server_attach() polls from a #GMainContext).
 */

#ifndef LRG_CONTROL_SERVER_H
#define LRG_CONTROL_SERVER_H

#include <glib-object.h>
#include <json-glib/json-glib.h>
#include "../lrg-version.h"
#include "lrg-command-registry.h"

G_BEGIN_DECLS

/**
 * LRG_CONTROL_PROTOCOL_VERSION:
 *
 * The wire protocol's version, reported by `rpc.discover`.  It changes
 * only when an existing method or event changes meaning.
 */
#define LRG_CONTROL_PROTOCOL_VERSION (1)

/**
 * LRG_CONTROL_MAX_LINE:
 *
 * The longest request line accepted (bytes); longer lines are answered
 * with -32600 and the connection is closed.
 */
#define LRG_CONTROL_MAX_LINE (1024 * 1024)

/**
 * LRG_CONTROL_MAX_PENDING_OUTPUT:
 *
 * Unread output a connection may queue before it is dropped (bytes).
 */
#define LRG_CONTROL_MAX_PENDING_OUTPUT (16 * 1024 * 1024)

#define LRG_TYPE_CONTROL_SERVER (lrg_control_server_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgControlServer, lrg_control_server, LRG, CONTROL_SERVER, GObject)

/**
 * lrg_control_server_new:
 * @registry: the commands served (referenced)
 *
 * Returns: (transfer full): a server, not listening yet
 */
LRG_AVAILABLE_IN_ALL
LrgControlServer *lrg_control_server_new (LrgCommandRegistry *registry);

/**
 * lrg_control_server_get_registry:
 * @self: an #LrgControlServer
 *
 * Returns: (transfer none): the registry
 */
LRG_AVAILABLE_IN_ALL
LrgCommandRegistry *lrg_control_server_get_registry (LrgControlServer *self);

/**
 * lrg_control_server_set_token:
 * @self: an #LrgControlServer
 * @token: (nullable): a secret every connection must give (`auth`)
 *   before anything but `rpc.ping`, `rpc.discover` and `auth`; %NULL or
 *   empty for none
 */
LRG_AVAILABLE_IN_ALL
void lrg_control_server_set_token (LrgControlServer *self,
                                   const gchar      *token);

/**
 * lrg_control_server_set_identity:
 * @self: an #LrgControlServer
 * @server: (nullable): the program and version ("librefall 0.15.0")
 * @instance: (nullable): the instance's name (its socket's basename)
 *
 * What `rpc.discover` reports.
 */
LRG_AVAILABLE_IN_ALL
void lrg_control_server_set_identity (LrgControlServer *self,
                                      const gchar      *server,
                                      const gchar      *instance);

/**
 * lrg_control_server_set_info:
 * @self: an #LrgControlServer
 * @info: (nullable): extra members for `rpc.discover`'s `info` (the
 *   character's name, the pid, the window's title); copied
 */
LRG_AVAILABLE_IN_ALL
void lrg_control_server_set_info (LrgControlServer *self,
                                  JsonObject       *info);

/**
 * lrg_control_server_listen:
 * @self: an #LrgControlServer
 * @path: the socket's path
 * @error: return location for a #GError
 *
 * Creates the socket's directory (mode 0700) when missing, removes a
 * stale socket at @path (one that no process answers on), binds, sets
 * the socket to mode 0600 and listens.  A live socket at @path fails
 * with %G_IO_ERROR_ADDRESS_IN_USE.
 *
 * Returns: %TRUE when listening
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_control_server_listen (LrgControlServer  *self,
                                    const gchar       *path,
                                    GError           **error);

/**
 * lrg_control_server_get_path:
 * @self: an #LrgControlServer
 *
 * Returns: (transfer none) (nullable): the socket's path while listening
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_control_server_get_path (LrgControlServer *self);

/**
 * lrg_control_server_is_listening:
 * @self: an #LrgControlServer
 *
 * Returns: whether the socket is open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_control_server_is_listening (LrgControlServer *self);

/**
 * lrg_control_server_poll:
 * @self: an #LrgControlServer
 *
 * Accepts waiting connections, reads what they sent, runs complete
 * lines, sends finished replies and drops closed connections.  Never
 * blocks.
 *
 * Returns: the requests handled
 */
LRG_AVAILABLE_IN_ALL
guint lrg_control_server_poll (LrgControlServer *self);

/**
 * lrg_control_server_attach:
 * @self: an #LrgControlServer
 * @context: (nullable): the context; %NULL for the thread default
 * @interval_ms: how often to poll
 *
 * Polls from a timeout source on @context (for programs that run a
 * #GMainLoop instead of a frame loop).  Replaces an earlier attach.
 */
LRG_AVAILABLE_IN_ALL
void lrg_control_server_attach (LrgControlServer *self,
                                GMainContext     *context,
                                guint             interval_ms);

/**
 * lrg_control_server_get_n_clients:
 * @self: an #LrgControlServer
 *
 * Returns: the open connections
 */
LRG_AVAILABLE_IN_ALL
guint lrg_control_server_get_n_clients (LrgControlServer *self);

/**
 * lrg_control_server_close:
 * @self: an #LrgControlServer
 *
 * Cancels pending calls, closes every connection and the socket and
 * removes the socket file.  Also done on finalize.
 */
LRG_AVAILABLE_IN_ALL
void lrg_control_server_close (LrgControlServer *self);

/**
 * lrg_control_runtime_dir:
 * @app: the program's name ("librefall")
 *
 * Where a program's control sockets live:
 * `$XDG_RUNTIME_DIR/<app>`, or `<tmp>/<app>-<uid>` without a runtime
 * directory.
 *
 * Returns: (transfer full): the directory
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_control_runtime_dir (const gchar *app);

/**
 * lrg_control_socket_is_live:
 * @path: a socket path
 *
 * Returns: whether a process accepts connections on @path
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_control_socket_is_live (const gchar *path);

/**
 * lrg_control_list_instances:
 * @dir: a runtime directory (lrg_control_runtime_dir())
 * @live_only: leave out sockets nobody answers on
 *
 * The instances with a socket in @dir: each socket's name without
 * `.sock`.
 *
 * Returns: (transfer full) (array zero-terminated=1): the names, sorted
 */
LRG_AVAILABLE_IN_ALL
gchar **lrg_control_list_instances (const gchar *dir,
                                    gboolean     live_only);

G_END_DECLS

#endif /* LRG_CONTROL_SERVER_H */
