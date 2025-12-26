/* lrg-debug-console.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Debug console system.
 *
 * The debug console provides an interactive command interface
 * for debugging and inspecting game state at runtime.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_DEBUG_CONSOLE (lrg_debug_console_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDebugConsole, lrg_debug_console, LRG, DEBUG_CONSOLE, GObject)

/* ==========================================================================
 * Command Callback
 * ========================================================================== */

/**
 * LrgDebugCommandFunc:
 * @console: the #LrgDebugConsole
 * @argc: number of arguments
 * @argv: (array length=argc): argument strings
 * @user_data: (nullable): user data
 *
 * Callback function for console commands.
 *
 * Returns: (transfer full) (nullable): output string, or %NULL
 */
typedef gchar * (*LrgDebugCommandFunc) (LrgDebugConsole  *console,
                                        guint             argc,
                                        const gchar     **argv,
                                        gpointer          user_data);

/* ==========================================================================
 * Console Output Entry (Boxed Type)
 * ========================================================================== */

#define LRG_TYPE_CONSOLE_OUTPUT (lrg_console_output_get_type ())

/**
 * LrgConsoleOutput:
 *
 * A single output entry from the console.
 */
typedef struct _LrgConsoleOutput LrgConsoleOutput;

LRG_AVAILABLE_IN_ALL
GType             lrg_console_output_get_type    (void) G_GNUC_CONST;

LRG_AVAILABLE_IN_ALL
LrgConsoleOutput *lrg_console_output_copy        (const LrgConsoleOutput *self);

LRG_AVAILABLE_IN_ALL
void              lrg_console_output_free        (LrgConsoleOutput *self);

/**
 * lrg_console_output_get_text:
 * @self: a #LrgConsoleOutput
 *
 * Gets the output text.
 *
 * Returns: (transfer none): the output text
 */
LRG_AVAILABLE_IN_ALL
const gchar *     lrg_console_output_get_text    (const LrgConsoleOutput *self);

/**
 * lrg_console_output_is_command:
 * @self: a #LrgConsoleOutput
 *
 * Checks if this output represents an input command.
 *
 * Returns: %TRUE if this is a command input
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_console_output_is_command  (const LrgConsoleOutput *self);

/**
 * lrg_console_output_is_error:
 * @self: a #LrgConsoleOutput
 *
 * Checks if this output represents an error.
 *
 * Returns: %TRUE if this is an error message
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_console_output_is_error    (const LrgConsoleOutput *self);

/**
 * lrg_console_output_get_timestamp:
 * @self: a #LrgConsoleOutput
 *
 * Gets the timestamp when this output was created.
 *
 * Returns: the timestamp in microseconds
 */
LRG_AVAILABLE_IN_ALL
gint64            lrg_console_output_get_timestamp (const LrgConsoleOutput *self);

/* ==========================================================================
 * Construction and Singleton
 * ========================================================================== */

/**
 * lrg_debug_console_get_default:
 *
 * Gets the default console instance.
 *
 * Returns: (transfer none): the default #LrgDebugConsole
 */
LRG_AVAILABLE_IN_ALL
LrgDebugConsole * lrg_debug_console_get_default  (void);

/**
 * lrg_debug_console_new:
 *
 * Creates a new debug console.
 *
 * Returns: (transfer full): a new #LrgDebugConsole
 */
LRG_AVAILABLE_IN_ALL
LrgDebugConsole * lrg_debug_console_new          (void);

/* ==========================================================================
 * Console Control
 * ========================================================================== */

/**
 * lrg_debug_console_is_visible:
 * @self: a #LrgDebugConsole
 *
 * Checks if the console is visible.
 *
 * Returns: %TRUE if visible
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_debug_console_is_visible   (LrgDebugConsole *self);

/**
 * lrg_debug_console_set_visible:
 * @self: a #LrgDebugConsole
 * @visible: whether to show the console
 *
 * Shows or hides the console.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_set_visible  (LrgDebugConsole *self,
                                                  gboolean         visible);

/**
 * lrg_debug_console_toggle:
 * @self: a #LrgDebugConsole
 *
 * Toggles console visibility.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_toggle       (LrgDebugConsole *self);

/**
 * lrg_debug_console_get_max_history:
 * @self: a #LrgDebugConsole
 *
 * Gets the maximum number of history entries.
 *
 * Returns: maximum history size
 */
LRG_AVAILABLE_IN_ALL
guint             lrg_debug_console_get_max_history (LrgDebugConsole *self);

/**
 * lrg_debug_console_set_max_history:
 * @self: a #LrgDebugConsole
 * @max_history: maximum number of history entries
 *
 * Sets the maximum number of command history entries.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_set_max_history (LrgDebugConsole *self,
                                                     guint            max_history);

/* ==========================================================================
 * Command Registration
 * ========================================================================== */

/**
 * lrg_debug_console_register_command:
 * @self: a #LrgDebugConsole
 * @name: command name
 * @description: (nullable): command description
 * @callback: callback function
 * @user_data: (nullable): user data for callback
 * @destroy: (nullable): destroy notify for user_data
 *
 * Registers a new console command.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_register_command (LrgDebugConsole     *self,
                                                      const gchar         *name,
                                                      const gchar         *description,
                                                      LrgDebugCommandFunc  callback,
                                                      gpointer             user_data,
                                                      GDestroyNotify       destroy);

/**
 * lrg_debug_console_unregister_command:
 * @self: a #LrgDebugConsole
 * @name: command name to remove
 *
 * Removes a registered command.
 *
 * Returns: %TRUE if command was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_debug_console_unregister_command (LrgDebugConsole *self,
                                                        const gchar     *name);

/**
 * lrg_debug_console_get_commands:
 * @self: a #LrgDebugConsole
 *
 * Gets all registered command names.
 *
 * Returns: (transfer container) (element-type utf8): list of command names
 */
LRG_AVAILABLE_IN_ALL
GList *           lrg_debug_console_get_commands (LrgDebugConsole *self);

/**
 * lrg_debug_console_get_command_description:
 * @self: a #LrgDebugConsole
 * @name: command name
 *
 * Gets the description for a command.
 *
 * Returns: (transfer none) (nullable): the command description
 */
LRG_AVAILABLE_IN_ALL
const gchar *     lrg_debug_console_get_command_description (LrgDebugConsole *self,
                                                              const gchar     *name);

/* ==========================================================================
 * Command Execution
 * ========================================================================== */

/**
 * lrg_debug_console_execute:
 * @self: a #LrgDebugConsole
 * @command_line: the command line to execute
 * @error: (nullable): return location for error
 *
 * Executes a command line.
 *
 * Returns: (transfer full) (nullable): command output, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gchar *           lrg_debug_console_execute      (LrgDebugConsole  *self,
                                                  const gchar      *command_line,
                                                  GError          **error);

/**
 * lrg_debug_console_print:
 * @self: a #LrgDebugConsole
 * @text: text to print
 *
 * Prints text to the console output.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_print        (LrgDebugConsole *self,
                                                  const gchar     *text);

/**
 * lrg_debug_console_printf:
 * @self: a #LrgDebugConsole
 * @format: printf-style format string
 * @...: format arguments
 *
 * Prints formatted text to the console output.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_printf       (LrgDebugConsole *self,
                                                  const gchar     *format,
                                                  ...) G_GNUC_PRINTF (2, 3);

/**
 * lrg_debug_console_print_error:
 * @self: a #LrgDebugConsole
 * @text: error text to print
 *
 * Prints error text to the console output.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_print_error  (LrgDebugConsole *self,
                                                  const gchar     *text);

/* ==========================================================================
 * History and Output
 * ========================================================================== */

/**
 * lrg_debug_console_get_output:
 * @self: a #LrgDebugConsole
 *
 * Gets all console output entries.
 *
 * Returns: (transfer none) (element-type LrgConsoleOutput): output entries
 */
LRG_AVAILABLE_IN_ALL
GQueue *          lrg_debug_console_get_output   (LrgDebugConsole *self);

/**
 * lrg_debug_console_get_history:
 * @self: a #LrgDebugConsole
 *
 * Gets command history.
 *
 * Returns: (transfer none) (element-type utf8): command history
 */
LRG_AVAILABLE_IN_ALL
GQueue *          lrg_debug_console_get_history  (LrgDebugConsole *self);

/**
 * lrg_debug_console_clear:
 * @self: a #LrgDebugConsole
 *
 * Clears all console output.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_clear        (LrgDebugConsole *self);

/**
 * lrg_debug_console_clear_history:
 * @self: a #LrgDebugConsole
 *
 * Clears command history.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_clear_history (LrgDebugConsole *self);

G_END_DECLS
