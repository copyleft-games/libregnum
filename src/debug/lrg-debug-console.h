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
 * Returns: (transfer none): output entries, a #GQueue of #LrgConsoleOutput
 */
LRG_AVAILABLE_IN_ALL
GQueue *          lrg_debug_console_get_output   (LrgDebugConsole *self);

/**
 * lrg_debug_console_get_history:
 * @self: a #LrgDebugConsole
 *
 * Gets command history.
 *
 * Returns: (transfer none): command history, a #GQueue of strings
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

/* ==========================================================================
 * Evaluator, history browsing, completion and the log (a drop-down
 * console's model; a game draws it)
 * ========================================================================== */

/**
 * LrgDebugEvalFunc:
 * @console: the #LrgDebugConsole
 * @line: the line that is no command (code for a scripting runtime)
 * @user_data: (nullable): user data
 * @error: return location for a #GError
 *
 * Runs a line that is not a command (lrg_debug_console_set_evaluator()).
 *
 * Returns: (transfer full) (nullable): output to print, %NULL for none
 *   (with @error set on failure)
 */
typedef gchar * (*LrgDebugEvalFunc) (LrgDebugConsole  *console,
                                     const gchar      *line,
                                     gpointer          user_data,
                                     GError          **error);

/**
 * LrgDebugCompleteFunc:
 * @console: the #LrgDebugConsole
 * @word: the word being completed (may be empty)
 * @user_data: (nullable): user data
 *
 * Offers completions for @word (lrg_debug_console_set_completer()); the
 * console keeps the ones that start with @word.
 *
 * Returns: (transfer full) (array zero-terminated=1) (nullable): candidates
 */
typedef gchar ** (*LrgDebugCompleteFunc) (LrgDebugConsole *console,
                                          const gchar     *word,
                                          gpointer         user_data);

/**
 * lrg_debug_console_set_evaluator:
 * @self: a #LrgDebugConsole
 * @func: (nullable) (scope notified) (closure user_data) (destroy destroy):
 *   the evaluator, %NULL for none
 * @user_data: (nullable): user data for @func
 * @destroy: (nullable): frees @user_data
 *
 * With an evaluator, lrg_debug_console_execute() runs commands only when
 * the line starts with a slash ("/help", "/clear") and hands every other
 * line to @func whole (no shell parsing), printing its output, or its
 * error as an error line.  Without one, every line is a command (the
 * default).
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_set_evaluator (LrgDebugConsole  *self,
                                                   LrgDebugEvalFunc  func,
                                                   gpointer          user_data,
                                                   GDestroyNotify    destroy);

/**
 * lrg_debug_console_has_evaluator:
 * @self: a #LrgDebugConsole
 *
 * Returns: %TRUE when an evaluator is set
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_debug_console_has_evaluator (LrgDebugConsole *self);

/**
 * lrg_debug_console_history_previous:
 * @self: a #LrgDebugConsole
 *
 * Steps the history cursor one entry back (the Up key); the cursor starts
 * after the newest entry and goes back there after every execute and
 * lrg_debug_console_history_reset().
 *
 * Returns: (transfer none) (nullable): the entry, the oldest one again at
 *   the start, %NULL with an empty history
 */
LRG_AVAILABLE_IN_ALL
const gchar *     lrg_debug_console_history_previous (LrgDebugConsole *self);

/**
 * lrg_debug_console_history_next:
 * @self: a #LrgDebugConsole
 *
 * Steps the history cursor one entry forward (the Down key).
 *
 * Returns: (transfer none) (nullable): the entry, %NULL once past the
 *   newest one (an empty input line)
 */
LRG_AVAILABLE_IN_ALL
const gchar *     lrg_debug_console_history_next (LrgDebugConsole *self);

/**
 * lrg_debug_console_history_reset:
 * @self: a #LrgDebugConsole
 *
 * Puts the history cursor after the newest entry.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_history_reset (LrgDebugConsole *self);

/**
 * lrg_debug_console_add_history:
 * @self: a #LrgDebugConsole
 * @entry: a line
 *
 * Appends @entry as execute() does (an empty line or a repeat of the
 * newest entry is skipped; the oldest go beyond the maximum).
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_add_history (LrgDebugConsole *self,
                                                 const gchar     *entry);

/**
 * lrg_debug_console_load_history:
 * @self: a #LrgDebugConsole
 * @path: a history file
 * @error: return location for a #GError
 *
 * Replaces the history by @path's entries: one a line, oldest first,
 * escaped with g_strescape() (so entries of several lines survive).  A
 * missing file is an empty history.
 *
 * Returns: %TRUE on success; %FALSE with a #GFileError
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_debug_console_load_history (LrgDebugConsole  *self,
                                                  const gchar      *path,
                                                  GError          **error);

/**
 * lrg_debug_console_save_history:
 * @self: a #LrgDebugConsole
 * @path: a history file
 * @error: return location for a #GError
 *
 * Writes the history atomically in lrg_debug_console_load_history()'s
 * format, creating the directory.
 *
 * Returns: %TRUE on success; %FALSE with a #GFileError
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_debug_console_save_history (LrgDebugConsole  *self,
                                                  const gchar      *path,
                                                  GError          **error);

/**
 * lrg_debug_console_set_completer:
 * @self: a #LrgDebugConsole
 * @func: (nullable) (scope notified) (closure user_data) (destroy destroy):
 *   the completer, %NULL for none
 * @user_data: (nullable): user data for @func
 * @destroy: (nullable): frees @user_data
 *
 * Adds candidates of the game's own (script function names, ...) to
 * lrg_debug_console_complete().
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_set_completer (LrgDebugConsole      *self,
                                                   LrgDebugCompleteFunc  func,
                                                   gpointer              user_data,
                                                   GDestroyNotify        destroy);

/**
 * lrg_debug_console_get_completion_word:
 * @line: an input line
 *
 * The word lrg_debug_console_complete() completes: the trailing run of
 * letters, digits, `_`, `.`, `:` and `/` of @line.
 *
 * Returns: (transfer full): the word, empty when @line ends otherwise
 */
LRG_AVAILABLE_IN_ALL
gchar *           lrg_debug_console_get_completion_word (const gchar *line);

/**
 * lrg_debug_console_complete:
 * @self: a #LrgDebugConsole
 * @line: the input line
 *
 * The candidates for @line's completion word: the commands (with a
 * leading slash when an evaluator is set) and the completer's, those
 * that start with the word, sorted, without repeats.
 *
 * Returns: (transfer full) (array zero-terminated=1): the candidates
 */
LRG_AVAILABLE_IN_ALL
gchar **          lrg_debug_console_complete (LrgDebugConsole *self,
                                              const gchar     *line);

/**
 * lrg_debug_console_complete_line:
 * @self: a #LrgDebugConsole
 * @line: the input line
 *
 * The Tab key: @line with its completion word extended to the longest
 * prefix every candidate shares (the one candidate whole).
 *
 * Returns: (transfer full): the new line (@line when nothing matches)
 */
LRG_AVAILABLE_IN_ALL
gchar *           lrg_debug_console_complete_line (LrgDebugConsole *self,
                                                   const gchar     *line);

/**
 * lrg_debug_console_set_log_capture:
 * @self: a #LrgDebugConsole
 * @capture: whether @self collects the log
 * @levels: the levels collected (%G_LOG_LEVEL_MASK for all)
 *
 * While @capture is set, every structured or classic g_log() message of
 * @levels (from any thread) is also printed to @self as "[domain] text"
 * (warnings and worse as error lines), then written as usual by
 * g_log_writer_default().  The first capture installs libregnum's log
 * writer with g_log_set_writer_func(), so an application that installs
 * its own writer must not capture.  Messages from other threads are
 * queued and printed by the next call on the console's thread
 * (lrg_debug_console_get_output() or lrg_debug_console_flush_log()).
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_set_log_capture (LrgDebugConsole *self,
                                                     gboolean         capture,
                                                     GLogLevelFlags   levels);

/**
 * lrg_debug_console_get_log_capture:
 * @self: a #LrgDebugConsole
 *
 * Returns: %TRUE while @self collects the log
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_debug_console_get_log_capture (LrgDebugConsole *self);

/**
 * lrg_debug_console_flush_log:
 * @self: a #LrgDebugConsole
 *
 * Prints the log messages queued since the last flush.
 *
 * Returns: the lines printed
 */
LRG_AVAILABLE_IN_ALL
guint             lrg_debug_console_flush_log (LrgDebugConsole *self);

/**
 * lrg_debug_console_get_max_output:
 * @self: a #LrgDebugConsole
 *
 * Returns: the most output lines kept (500 by default)
 */
LRG_AVAILABLE_IN_ALL
guint             lrg_debug_console_get_max_output (LrgDebugConsole *self);

/**
 * lrg_debug_console_set_max_output:
 * @self: a #LrgDebugConsole
 * @max_output: the most output lines kept (at least 1)
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_console_set_max_output (LrgDebugConsole *self,
                                                    guint            max_output);

G_END_DECLS
