/* lrg-command-registry.h - The commands a game offers, and its events
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * #LrgCommandRegistry holds a game's #LrgCommand<!-- -->s by name and
 * runs them (lrg_command_registry_invoke() from JSON parameters,
 * lrg_command_registry_invoke_line() from a text line such as
 * "walk-to 12 0 4.5"), and carries the game's event stream: topics
 * ("messages", "combat") with descriptions, and #LrgCommandRegistry::event
 * for every lrg_command_registry_publish().  The control socket, an MCP
 * bridge, scripts and tests all go through it, so a command behaves
 * the same whoever calls it.
 */

#ifndef LRG_COMMAND_REGISTRY_H
#define LRG_COMMAND_REGISTRY_H

#include <glib-object.h>
#include <json-glib/json-glib.h>
#include "../lrg-version.h"
#include "lrg-command.h"

G_BEGIN_DECLS

#define LRG_TYPE_COMMAND_REGISTRY (lrg_command_registry_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCommandRegistry, lrg_command_registry, LRG, COMMAND_REGISTRY, GObject)

/**
 * lrg_command_registry_new:
 *
 * Returns: (transfer full): an empty registry
 */
LRG_AVAILABLE_IN_ALL
LrgCommandRegistry *lrg_command_registry_new (void);

/**
 * lrg_command_registry_register:
 * @self: an #LrgCommandRegistry
 * @command: (transfer none): the command (referenced)
 * @error: return location for a #GError
 *
 * Adds @command and emits #LrgCommandRegistry::command-added.
 *
 * Returns: %FALSE (%LRG_COMMAND_ERROR_INVALID_PARAMS) when the name is
 *   taken or not a valid command name
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_command_registry_register (LrgCommandRegistry  *self,
                                        LrgCommand          *command,
                                        GError             **error);

/**
 * lrg_command_registry_add:
 * @self: an #LrgCommandRegistry
 * @name: the command's name
 * @group: (nullable): its group
 * @summary: one line of help
 * @handler: (scope notified) (closure user_data) (destroy destroy): what runs it
 * @user_data: data for @handler
 * @destroy: (nullable): frees @user_data
 *
 * lrg_command_new() and lrg_command_registry_register() in one; add the
 * parameters to the returned command.  A taken name is a programming
 * error (critical, %NULL).
 *
 * Returns: (transfer none) (nullable): the registered command
 */
LRG_AVAILABLE_IN_ALL
LrgCommand *lrg_command_registry_add (LrgCommandRegistry *self,
                                      const gchar        *name,
                                      const gchar        *group,
                                      const gchar        *summary,
                                      LrgCommandHandler   handler,
                                      gpointer            user_data,
                                      GDestroyNotify      destroy);

/**
 * lrg_command_registry_unregister:
 * @self: an #LrgCommandRegistry
 * @name: a command name
 *
 * Removes the command and emits #LrgCommandRegistry::command-removed.
 *
 * Returns: %TRUE when it was registered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_command_registry_unregister (LrgCommandRegistry *self,
                                          const gchar        *name);

/**
 * lrg_command_registry_lookup:
 * @self: an #LrgCommandRegistry
 * @name: a command name
 *
 * Returns: (transfer none) (nullable): the command
 */
LRG_AVAILABLE_IN_ALL
LrgCommand *lrg_command_registry_lookup (LrgCommandRegistry *self,
                                         const gchar        *name);

/**
 * lrg_command_registry_list:
 * @self: an #LrgCommandRegistry
 * @include_hidden: whether %LRG_COMMAND_FLAG_HIDDEN commands are listed
 *
 * Returns: (transfer container) (element-type LrgCommand): the commands
 *   sorted by group, then name
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_command_registry_list (LrgCommandRegistry *self,
                                      gboolean            include_hidden);

/**
 * lrg_command_registry_get_n_commands:
 * @self: an #LrgCommandRegistry
 *
 * Returns: how many commands are registered
 */
LRG_AVAILABLE_IN_ALL
guint lrg_command_registry_get_n_commands (LrgCommandRegistry *self);

/**
 * lrg_command_registry_describe:
 * @self: an #LrgCommandRegistry
 *
 * Every listed command's lrg_command_describe(), sorted like
 * lrg_command_registry_list().
 *
 * Returns: (transfer full): a JSON array node
 */
LRG_AVAILABLE_IN_ALL
JsonNode *lrg_command_registry_describe (LrgCommandRegistry *self);

/**
 * lrg_command_registry_invoke:
 * @self: an #LrgCommandRegistry
 * @name: the command
 * @params: (nullable): its parameters (not modified)
 * @origin: (nullable): who calls
 *
 * Checks the parameters and runs the command.  Never returns %NULL: an
 * unknown command or bad parameters give a call completed with the
 * error.  A command without %LRG_COMMAND_FLAG_ASYNC whose handler left
 * the call pending is completed with a null result.
 *
 * Returns: (transfer full): the call, completed or pending
 */
LRG_AVAILABLE_IN_ALL
LrgCommandCall *lrg_command_registry_invoke (LrgCommandRegistry *self,
                                             const gchar        *name,
                                             JsonObject         *params,
                                             const gchar        *origin);

/**
 * lrg_command_registry_parse_line:
 * @self: an #LrgCommandRegistry
 * @line: a text command ("walk-to 12 0 4.5", "say 'hello there'")
 * @name: (out) (transfer full): the command's name
 * @params: (out) (transfer full): its parameters (not yet checked)
 * @error: return location for a #GError
 *
 * Splits @line like a shell (quotes, backslashes) and maps the words
 * with lrg_command_parse_arguments().
 *
 * Returns: %FALSE on an empty line, bad quoting, an unknown command or
 *   bad arguments
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_command_registry_parse_line (LrgCommandRegistry  *self,
                                          const gchar         *line,
                                          gchar              **name,
                                          JsonObject         **params,
                                          GError             **error);

/**
 * lrg_command_registry_invoke_line:
 * @self: an #LrgCommandRegistry
 * @line: a text command
 * @origin: (nullable): who calls
 *
 * lrg_command_registry_parse_line() then lrg_command_registry_invoke().
 *
 * Returns: (transfer full): the call, completed or pending
 */
LRG_AVAILABLE_IN_ALL
LrgCommandCall *lrg_command_registry_invoke_line (LrgCommandRegistry *self,
                                                  const gchar        *line,
                                                  const gchar        *origin);

/**
 * lrg_command_registry_call_sync:
 * @self: an #LrgCommandRegistry
 * @name: the command
 * @params: (nullable): its parameters
 * @error: return location for a #GError
 *
 * Runs a command that completes at once.  A call still pending when
 * the handler returns is cancelled and fails with %G_IO_ERROR_PENDING.
 *
 * Returns: (transfer full) (nullable): the result, %NULL on error
 */
LRG_AVAILABLE_IN_ALL
JsonNode *lrg_command_registry_call_sync (LrgCommandRegistry  *self,
                                          const gchar         *name,
                                          JsonObject          *params,
                                          GError             **error);

/**
 * lrg_command_registry_add_topic:
 * @self: an #LrgCommandRegistry
 * @topic: an event topic ("messages", "combat", "quest")
 * @description: (nullable): what its events carry
 *
 * Declares an event topic for listings; publishing to an undeclared
 * topic works too.
 */
LRG_AVAILABLE_IN_ALL
void lrg_command_registry_add_topic (LrgCommandRegistry *self,
                                     const gchar        *topic,
                                     const gchar        *description);

/**
 * lrg_command_registry_list_topics:
 * @self: an #LrgCommandRegistry
 *
 * Returns: (transfer full) (array zero-terminated=1): the declared
 *   topics, sorted
 */
LRG_AVAILABLE_IN_ALL
gchar **lrg_command_registry_list_topics (LrgCommandRegistry *self);

/**
 * lrg_command_registry_get_topic_description:
 * @self: an #LrgCommandRegistry
 * @topic: a topic
 *
 * Returns: (transfer none) (nullable): its description
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_command_registry_get_topic_description (LrgCommandRegistry *self,
                                                         const gchar        *topic);

/**
 * lrg_command_registry_publish:
 * @self: an #LrgCommandRegistry
 * @topic: the event's topic
 * @data: (nullable): its payload (the signal handlers see it; not kept)
 *
 * Emits #LrgCommandRegistry::event with the next sequence number.
 *
 * Returns: the event's sequence number (from 1)
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_command_registry_publish (LrgCommandRegistry *self,
                                      const gchar        *topic,
                                      JsonNode           *data);

/**
 * lrg_command_registry_get_event_seq:
 * @self: an #LrgCommandRegistry
 *
 * Returns: the sequence number of the last event (0 before the first)
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_command_registry_get_event_seq (LrgCommandRegistry *self);

/**
 * lrg_command_topic_matches:
 * @pattern: a topic or a glob ("quest.*", "*")
 * @topic: an event's topic
 *
 * Returns: whether @topic matches @pattern
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_command_topic_matches (const gchar *pattern,
                                    const gchar *topic);

G_END_DECLS

#endif /* LRG_COMMAND_REGISTRY_H */
