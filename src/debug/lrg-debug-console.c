/* lrg-debug-console.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Debug console implementation.
 */

#include "config.h"
#include "lrg-debug-console.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DEBUG
#include "../lrg-log.h"

#include <errno.h>
#include <string.h>

/* ==========================================================================
 * Console Output Entry
 * ========================================================================== */

struct _LrgConsoleOutput
{
    gchar    *text;
    gboolean  is_command;
    gboolean  is_error;
    gint64    timestamp;
};

static LrgConsoleOutput *
lrg_console_output_new (const gchar *text,
                        gboolean     is_command,
                        gboolean     is_error)
{
    LrgConsoleOutput *output;

    output = g_slice_new0 (LrgConsoleOutput);
    output->text = g_strdup (text);
    output->is_command = is_command;
    output->is_error = is_error;
    output->timestamp = g_get_monotonic_time ();

    return output;
}

LrgConsoleOutput *
lrg_console_output_copy (const LrgConsoleOutput *self)
{
    LrgConsoleOutput *copy;

    if (self == NULL)
        return NULL;

    copy = g_slice_new0 (LrgConsoleOutput);
    copy->text = g_strdup (self->text);
    copy->is_command = self->is_command;
    copy->is_error = self->is_error;
    copy->timestamp = self->timestamp;

    return copy;
}

void
lrg_console_output_free (LrgConsoleOutput *self)
{
    if (self == NULL)
        return;

    g_free (self->text);
    g_slice_free (LrgConsoleOutput, self);
}

const gchar *
lrg_console_output_get_text (const LrgConsoleOutput *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->text;
}

gboolean
lrg_console_output_is_command (const LrgConsoleOutput *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->is_command;
}

gboolean
lrg_console_output_is_error (const LrgConsoleOutput *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->is_error;
}

gint64
lrg_console_output_get_timestamp (const LrgConsoleOutput *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->timestamp;
}

G_DEFINE_BOXED_TYPE (LrgConsoleOutput, lrg_console_output,
                     lrg_console_output_copy,
                     lrg_console_output_free)

/* ==========================================================================
 * Command Registration Data
 * ========================================================================== */

typedef struct
{
    gchar               *name;
    gchar               *description;
    LrgDebugCommandFunc  callback;
    gpointer             user_data;
    GDestroyNotify       destroy;
} CommandData;

static CommandData *
command_data_new (const gchar         *name,
                  const gchar         *description,
                  LrgDebugCommandFunc  callback,
                  gpointer             user_data,
                  GDestroyNotify       destroy)
{
    CommandData *data;

    data = g_slice_new0 (CommandData);
    data->name = g_strdup (name);
    data->description = g_strdup (description);
    data->callback = callback;
    data->user_data = user_data;
    data->destroy = destroy;

    return data;
}

static void
command_data_free (CommandData *data)
{
    if (data == NULL)
        return;

    g_free (data->name);
    g_free (data->description);

    if (data->destroy && data->user_data)
        data->destroy (data->user_data);

    g_slice_free (CommandData, data);
}

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgDebugConsole
{
    GObject      parent_instance;

    gboolean     visible;
    guint        max_history;
    guint        max_output;

    GHashTable  *commands;     /* name -> CommandData */
    GQueue      *output;       /* GQueue of LrgConsoleOutput */
    GQueue      *history;      /* GQueue of gchar* */

    /* the history cursor: an index into history, or its length (after
     * the newest entry) */
    guint        history_cursor;

    /* lines that are no command (set_evaluator) */
    LrgDebugEvalFunc      eval_func;
    gpointer              eval_data;
    GDestroyNotify        eval_destroy;

    /* the game's completions (set_completer) */
    LrgDebugCompleteFunc  complete_func;
    gpointer              complete_data;
    GDestroyNotify        complete_destroy;

    /* the log (set_log_capture); pending is guarded by log_lock */
    gboolean              log_capture;
    GLogLevelFlags        log_levels;
    GMutex                log_lock;
    GPtrArray            *log_pending;  /* LrgConsoleOutput */
};

#pragma GCC visibility push(default)
G_DEFINE_TYPE (LrgDebugConsole, lrg_debug_console, G_TYPE_OBJECT)
#pragma GCC visibility pop

static LrgDebugConsole *default_console = NULL;

/* the consoles collecting the log, guarded by capture_lock: the writer
 * runs on any thread */
static GMutex  capture_lock;
static GSList *capturing = NULL;

/* ==========================================================================
 * Built-in Commands
 * ========================================================================== */

static gchar *
cmd_help (LrgDebugConsole  *console,
          guint             argc,
          const gchar     **argv,
          gpointer          user_data)
{
    GString *result;
    GList *commands;
    GList *l;

    result = g_string_new ("Available commands:\n");
    commands = lrg_debug_console_get_commands (console);

    for (l = commands; l != NULL; l = l->next)
    {
        const gchar *name = l->data;
        const gchar *desc = lrg_debug_console_get_command_description (console, name);

        if (desc != NULL)
            g_string_append_printf (result, "  %-16s - %s\n", name, desc);
        else
            g_string_append_printf (result, "  %s\n", name);
    }

    g_list_free (commands);

    return g_string_free (result, FALSE);
}

static gchar *
cmd_clear (LrgDebugConsole  *console,
           guint             argc,
           const gchar     **argv,
           gpointer          user_data)
{
    lrg_debug_console_clear (console);
    return NULL;
}

static gchar *
cmd_echo (LrgDebugConsole  *console,
          guint             argc,
          const gchar     **argv,
          gpointer          user_data)
{
    GString *result;
    guint i;

    if (argc < 2)
        return g_strdup ("");

    result = g_string_new (NULL);

    for (i = 1; i < argc; i++)
    {
        if (i > 1)
            g_string_append_c (result, ' ');
        g_string_append (result, argv[i]);
    }

    return g_string_free (result, FALSE);
}

static gchar *
cmd_history (LrgDebugConsole  *console,
             guint             argc,
             const gchar     **argv,
             gpointer          user_data)
{
    GString *result;
    GList *l;
    guint index;

    result = g_string_new ("Command history:\n");

    index = 0;
    for (l = console->history->head; l != NULL; l = l->next)
    {
        g_string_append_printf (result, "  %3u: %s\n", index++, (const gchar *)l->data);
    }

    if (index == 0)
        g_string_append (result, "  (empty)\n");

    return g_string_free (result, FALSE);
}

static void
register_builtin_commands (LrgDebugConsole *self)
{
    lrg_debug_console_register_command (self, "help",
                                        "Show available commands",
                                        cmd_help, NULL, NULL);

    lrg_debug_console_register_command (self, "clear",
                                        "Clear console output",
                                        cmd_clear, NULL, NULL);

    lrg_debug_console_register_command (self, "echo",
                                        "Echo text back",
                                        cmd_echo, NULL, NULL);

    lrg_debug_console_register_command (self, "history",
                                        "Show command history",
                                        cmd_history, NULL, NULL);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_debug_console_finalize (GObject *object)
{
    LrgDebugConsole *self = LRG_DEBUG_CONSOLE (object);

    /* no log line may reach a console being finalized */
    g_mutex_lock (&capture_lock);
    capturing = g_slist_remove (capturing, self);
    g_mutex_unlock (&capture_lock);

    g_hash_table_destroy (self->commands);
    g_queue_free_full (self->output, (GDestroyNotify)lrg_console_output_free);
    g_queue_free_full (self->history, g_free);
    if (self->eval_destroy != NULL && self->eval_data != NULL)
        self->eval_destroy (self->eval_data);
    if (self->complete_destroy != NULL && self->complete_data != NULL)
        self->complete_destroy (self->complete_data);
    g_ptr_array_unref (self->log_pending);
    g_mutex_clear (&self->log_lock);

    if (default_console == self)
        default_console = NULL;

    G_OBJECT_CLASS (lrg_debug_console_parent_class)->finalize (object);
}

static void
lrg_debug_console_class_init (LrgDebugConsoleClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_debug_console_finalize;
}

static void
lrg_debug_console_init (LrgDebugConsole *self)
{
    self->visible = FALSE;
    self->max_history = 100;
    self->max_output = 500;

    self->commands = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            NULL, (GDestroyNotify)command_data_free);
    self->output = g_queue_new ();
    self->history = g_queue_new ();
    self->history_cursor = 0;
    self->eval_func = NULL;
    self->eval_data = NULL;
    self->eval_destroy = NULL;
    self->complete_func = NULL;
    self->complete_data = NULL;
    self->complete_destroy = NULL;
    self->log_capture = FALSE;
    self->log_levels = 0;
    g_mutex_init (&self->log_lock);
    self->log_pending = g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_console_output_free);

    register_builtin_commands (self);

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Created debug console");
}

/* ==========================================================================
 * Construction and Singleton
 * ========================================================================== */

LrgDebugConsole *
lrg_debug_console_get_default (void)
{
    if (default_console == NULL)
        default_console = lrg_debug_console_new ();

    return default_console;
}

LrgDebugConsole *
lrg_debug_console_new (void)
{
    return g_object_new (LRG_TYPE_DEBUG_CONSOLE, NULL);
}

/* ==========================================================================
 * Console Control
 * ========================================================================== */

gboolean
lrg_debug_console_is_visible (LrgDebugConsole *self)
{
    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), FALSE);
    return self->visible;
}

void
lrg_debug_console_set_visible (LrgDebugConsole *self,
                               gboolean         visible)
{
    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));
    self->visible = visible;

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Console %s",
               visible ? "shown" : "hidden");
}

void
lrg_debug_console_toggle (LrgDebugConsole *self)
{
    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));
    lrg_debug_console_set_visible (self, !self->visible);
}

guint
lrg_debug_console_get_max_history (LrgDebugConsole *self)
{
    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), 0);
    return self->max_history;
}

void
lrg_debug_console_set_max_history (LrgDebugConsole *self,
                                   guint            max_history)
{
    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));
    self->max_history = MAX (1, max_history);

    /* Trim history if needed */
    while (g_queue_get_length (self->history) > self->max_history)
    {
        gchar *old = g_queue_pop_head (self->history);
        g_free (old);
    }
}

/* ==========================================================================
 * Command Registration
 * ========================================================================== */

void
lrg_debug_console_register_command (LrgDebugConsole     *self,
                                    const gchar         *name,
                                    const gchar         *description,
                                    LrgDebugCommandFunc  callback,
                                    gpointer             user_data,
                                    GDestroyNotify       destroy)
{
    CommandData *data;

    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));
    g_return_if_fail (name != NULL);
    g_return_if_fail (callback != NULL);

    /* Remove existing if present */
    lrg_debug_console_unregister_command (self, name);

    data = command_data_new (name, description, callback, user_data, destroy);
    g_hash_table_insert (self->commands, data->name, data);

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Registered command: %s", name);
}

gboolean
lrg_debug_console_unregister_command (LrgDebugConsole *self,
                                      const gchar     *name)
{
    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    return g_hash_table_remove (self->commands, name);
}

GList *
lrg_debug_console_get_commands (LrgDebugConsole *self)
{
    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), NULL);
    return g_hash_table_get_keys (self->commands);
}

const gchar *
lrg_debug_console_get_command_description (LrgDebugConsole *self,
                                           const gchar     *name)
{
    CommandData *data;

    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    data = g_hash_table_lookup (self->commands, name);
    if (data == NULL)
        return NULL;

    return data->description;
}

/* ==========================================================================
 * Command Execution
 * ========================================================================== */

static void
add_output (LrgDebugConsole *self,
            const gchar     *text,
            gboolean         is_command,
            gboolean         is_error)
{
    LrgConsoleOutput *output;

    output = lrg_console_output_new (text, is_command, is_error);
    g_queue_push_tail (self->output, output);

    /* Trim old output */
    while (g_queue_get_length (self->output) > self->max_output)
    {
        LrgConsoleOutput *old = g_queue_pop_head (self->output);
        lrg_console_output_free (old);
    }
}

static void
add_to_history (LrgDebugConsole *self,
                const gchar     *command_line)
{
    /* Don't add duplicates of the last command */
    if (g_queue_get_length (self->history) > 0)
    {
        const gchar *last = g_queue_peek_tail (self->history);
        if (g_strcmp0 (last, command_line) == 0)
            return;
    }

    g_queue_push_tail (self->history, g_strdup (command_line));

    /* Trim old history */
    while (g_queue_get_length (self->history) > self->max_history)
    {
        gchar *old = g_queue_pop_head (self->history);
        g_free (old);
    }
    self->history_cursor = g_queue_get_length (self->history);
}

/*
 * run_evaluator:
 * A line that is no command goes to the evaluator whole; its output is
 * printed, its error printed as an error line and returned.
 */
static gchar *
run_evaluator (LrgDebugConsole  *self,
               const gchar      *line,
               GError          **error)
{
    g_autoptr(GError) local = NULL;
    gchar *result;

    result = self->eval_func (self, line, self->eval_data, &local);
    if (local != NULL)
    {
        add_output (self, local->message, FALSE, TRUE);
        g_propagate_error (error, g_steal_pointer (&local));
        g_free (result);
        return NULL;
    }
    if (result != NULL && result[0] != '\0')
        add_output (self, result, FALSE, FALSE);
    return result;
}

gchar *
lrg_debug_console_execute (LrgDebugConsole  *self,
                           const gchar      *command_line,
                           GError          **error)
{
    gchar **argv;
    gint argc;
    CommandData *cmd;
    gchar *result;
    g_autoptr(GError) parse_error = NULL;

    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), NULL);
    g_return_val_if_fail (command_line != NULL, NULL);

    /* Skip empty commands */
    if (command_line[0] == '\0' || g_str_has_prefix (command_line, " "))
    {
        return NULL;
    }

    /* Add to output and history */
    add_output (self, command_line, TRUE, FALSE);
    add_to_history (self, command_line);
    self->history_cursor = g_queue_get_length (self->history);

    /* With an evaluator, only "/command" lines are commands */
    if (self->eval_func != NULL)
    {
        if (command_line[0] != '/')
            return run_evaluator (self, command_line, error);
        command_line++;
    }

    /* Parse command line */
    if (!g_shell_parse_argv (command_line, &argc, &argv, &parse_error))
    {
        g_autofree gchar *err_msg = g_strdup_printf ("Parse error: %s",
                                                      parse_error->message);
        add_output (self, err_msg, FALSE, TRUE);

        g_set_error (error, LRG_DEBUG_ERROR, LRG_DEBUG_ERROR_INVALID_ARGS,
                     "Failed to parse command: %s", parse_error->message);
        return NULL;
    }

    if (argc == 0)
    {
        g_strfreev (argv);
        return NULL;
    }

    /* Find command */
    cmd = g_hash_table_lookup (self->commands, argv[0]);
    if (cmd == NULL)
    {
        g_autofree gchar *err_msg = g_strdup_printf ("Unknown command: %s", argv[0]);
        add_output (self, err_msg, FALSE, TRUE);

        g_set_error (error, LRG_DEBUG_ERROR, LRG_DEBUG_ERROR_COMMAND_NOT_FOUND,
                     "Unknown command: %s", argv[0]);
        g_strfreev (argv);
        return NULL;
    }

    /* Execute command */
    result = cmd->callback (self, (guint)argc, (const gchar **)argv, cmd->user_data);
    g_strfreev (argv);

    /* Add result to output */
    if (result != NULL && result[0] != '\0')
    {
        add_output (self, result, FALSE, FALSE);
    }

    return result;
}

void
lrg_debug_console_print (LrgDebugConsole *self,
                         const gchar     *text)
{
    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));
    g_return_if_fail (text != NULL);

    add_output (self, text, FALSE, FALSE);
}

void
lrg_debug_console_printf (LrgDebugConsole *self,
                          const gchar     *format,
                          ...)
{
    va_list args;
    gchar *text;

    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));
    g_return_if_fail (format != NULL);

    va_start (args, format);
    text = g_strdup_vprintf (format, args);
    va_end (args);

    add_output (self, text, FALSE, FALSE);
    g_free (text);
}

void
lrg_debug_console_print_error (LrgDebugConsole *self,
                               const gchar     *text)
{
    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));
    g_return_if_fail (text != NULL);

    add_output (self, text, FALSE, TRUE);
}

/* ==========================================================================
 * History and Output
 * ========================================================================== */

GQueue *
lrg_debug_console_get_output (LrgDebugConsole *self)
{
    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), NULL);

    /* the log lines of other threads come in first */
    lrg_debug_console_flush_log (self);
    return self->output;
}

GQueue *
lrg_debug_console_get_history (LrgDebugConsole *self)
{
    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), NULL);
    return self->history;
}

void
lrg_debug_console_clear (LrgDebugConsole *self)
{
    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));

    g_queue_free_full (self->output, (GDestroyNotify)lrg_console_output_free);
    self->output = g_queue_new ();

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Console output cleared");
}

void
lrg_debug_console_clear_history (LrgDebugConsole *self)
{
    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));

    g_queue_free_full (self->history, g_free);
    self->history = g_queue_new ();
    self->history_cursor = 0;

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Console history cleared");
}

/* ==========================================================================
 * Evaluator
 * ========================================================================== */

void
lrg_debug_console_set_evaluator (LrgDebugConsole  *self,
                                 LrgDebugEvalFunc  func,
                                 gpointer          user_data,
                                 GDestroyNotify    destroy)
{
    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));

    if (self->eval_destroy != NULL && self->eval_data != NULL)
        self->eval_destroy (self->eval_data);
    self->eval_func = func;
    self->eval_data = func != NULL ? user_data : NULL;
    self->eval_destroy = func != NULL ? destroy : NULL;
}

gboolean
lrg_debug_console_has_evaluator (LrgDebugConsole *self)
{
    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), FALSE);
    return self->eval_func != NULL;
}

/* ==========================================================================
 * History browsing and persistence
 * ========================================================================== */

const gchar *
lrg_debug_console_history_previous (LrgDebugConsole *self)
{
    guint length;

    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), NULL);

    length = g_queue_get_length (self->history);
    if (length == 0)
        return NULL;
    /* the oldest entry stays when the cursor is at the start */
    if (self->history_cursor > length)
        self->history_cursor = length;
    if (self->history_cursor > 0)
        self->history_cursor--;
    return (const gchar *)g_queue_peek_nth (self->history, self->history_cursor);
}

const gchar *
lrg_debug_console_history_next (LrgDebugConsole *self)
{
    guint length;

    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), NULL);

    length = g_queue_get_length (self->history);
    if (self->history_cursor >= length)
    {
        self->history_cursor = length;
        return NULL;
    }
    self->history_cursor++;
    if (self->history_cursor >= length)
        return NULL;
    return (const gchar *)g_queue_peek_nth (self->history, self->history_cursor);
}

void
lrg_debug_console_history_reset (LrgDebugConsole *self)
{
    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));
    self->history_cursor = g_queue_get_length (self->history);
}

void
lrg_debug_console_add_history (LrgDebugConsole *self,
                               const gchar     *entry)
{
    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));
    g_return_if_fail (entry != NULL);

    if (entry[0] == '\0')
        return;
    add_to_history (self, entry);
}

gboolean
lrg_debug_console_load_history (LrgDebugConsole  *self,
                                const gchar      *path,
                                GError          **error)
{
    g_autofree gchar *contents = NULL;
    g_auto(GStrv) lines = NULL;
    g_autoptr(GError) local = NULL;
    guint i;

    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (!g_file_get_contents (path, &contents, NULL, &local))
    {
        /* no file yet: an empty history */
        if (g_error_matches (local, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
            lrg_debug_console_clear_history (self);
            self->history_cursor = 0;
            return TRUE;
        }
        g_propagate_error (error, g_steal_pointer (&local));
        return FALSE;
    }

    /* the file is read whole first: a bad file leaves the history alone */
    lines = g_strsplit (contents, "\n", -1);
    lrg_debug_console_clear_history (self);
    for (i = 0; lines[i] != NULL; i++)
    {
        g_autofree gchar *entry = NULL;

        if (lines[i][0] == '\0')
            continue;
        entry = g_strcompress (lines[i]);
        add_to_history (self, entry);
    }
    self->history_cursor = g_queue_get_length (self->history);
    return TRUE;
}

gboolean
lrg_debug_console_save_history (LrgDebugConsole  *self,
                                const gchar      *path,
                                GError          **error)
{
    g_autofree gchar *dir = NULL;
    GString *text;
    GList *l;
    gboolean ok;

    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    dir = g_path_get_dirname (path);
    if (g_mkdir_with_parents (dir, 0755) != 0)
    {
        gint saved = errno;

        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved),
                     "cannot create %s: %s", dir, g_strerror (saved));
        return FALSE;
    }

    /* one escaped entry a line, oldest first */
    text = g_string_new (NULL);
    for (l = self->history->head; l != NULL; l = l->next)
    {
        g_autofree gchar *escaped = g_strescape ((const gchar *)l->data, NULL);

        g_string_append (text, escaped);
        g_string_append_c (text, '\n');
    }
    ok = g_file_set_contents (path, text->str, (gssize)text->len, error);
    g_string_free (text, TRUE);
    return ok;
}

/* ==========================================================================
 * Completion
 * ========================================================================== */

void
lrg_debug_console_set_completer (LrgDebugConsole      *self,
                                 LrgDebugCompleteFunc  func,
                                 gpointer              user_data,
                                 GDestroyNotify        destroy)
{
    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));

    if (self->complete_destroy != NULL && self->complete_data != NULL)
        self->complete_destroy (self->complete_data);
    self->complete_func = func;
    self->complete_data = func != NULL ? user_data : NULL;
    self->complete_destroy = func != NULL ? destroy : NULL;
}

static gboolean
is_word_char (gchar c)
{
    return g_ascii_isalnum (c) || c == '_' || c == '.' || c == ':' || c == '/';
}

gchar *
lrg_debug_console_get_completion_word (const gchar *line)
{
    const gchar *start;
    const gchar *end;

    g_return_val_if_fail (line != NULL, NULL);

    end = line + strlen (line);
    start = end;
    while (start > line && is_word_char (start[-1]))
        start--;
    return g_strndup (start, (gsize)(end - start));
}

/* adds @candidate when it extends @word and is new */
static void
offer (GPtrArray   *found,
       GHashTable  *seen,
       const gchar *word,
       const gchar *candidate)
{
    if (candidate == NULL || !g_str_has_prefix (candidate, word))
        return;
    if (g_hash_table_contains (seen, candidate))
        return;
    g_hash_table_add (seen, g_strdup (candidate));
    g_ptr_array_add (found, g_strdup (candidate));
}

static gint
compare_strings (gconstpointer a,
                 gconstpointer b)
{
    return g_strcmp0 (*(const gchar * const *)a, *(const gchar * const *)b);
}

gchar **
lrg_debug_console_complete (LrgDebugConsole *self,
                            const gchar     *line)
{
    g_autofree gchar *word = NULL;
    g_autoptr(GHashTable) seen = NULL;
    GPtrArray *found;
    GList *commands;
    GList *l;

    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), NULL);
    g_return_val_if_fail (line != NULL, NULL);

    word = lrg_debug_console_get_completion_word (line);
    seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    found = g_ptr_array_new ();

    /* the commands: "/name" with an evaluator, where the line starts */
    commands = lrg_debug_console_get_commands (self);
    for (l = commands; l != NULL; l = l->next)
    {
        if (self->eval_func != NULL)
        {
            g_autofree gchar *slashed = g_strconcat ("/", (const gchar *)l->data, NULL);

            if (strlen (word) == strlen (line))
                offer (found, seen, word, slashed);
        }
        else
        {
            offer (found, seen, word, (const gchar *)l->data);
        }
    }
    g_list_free (commands);

    /* the game's */
    if (self->complete_func != NULL)
    {
        g_auto(GStrv) extra = NULL;
        guint i;

        extra = self->complete_func (self, word, self->complete_data);
        for (i = 0; extra != NULL && extra[i] != NULL; i++)
            offer (found, seen, word, extra[i]);
    }

    g_ptr_array_sort (found, compare_strings);
    g_ptr_array_add (found, NULL);
    return (gchar **)g_ptr_array_free (found, FALSE);
}

gchar *
lrg_debug_console_complete_line (LrgDebugConsole *self,
                                 const gchar     *line)
{
    g_auto(GStrv) candidates = NULL;
    g_autofree gchar *word = NULL;
    gsize common;
    guint i;

    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), NULL);
    g_return_val_if_fail (line != NULL, NULL);

    candidates = lrg_debug_console_complete (self, line);
    if (candidates[0] == NULL)
        return g_strdup (line);

    /* the longest prefix every candidate shares */
    common = strlen (candidates[0]);
    for (i = 1; candidates[i] != NULL; i++)
    {
        gsize n;

        n = 0;
        while (n < common && candidates[i][n] != '\0' && candidates[i][n] == candidates[0][n])
            n++;
        common = n;
    }
    word = lrg_debug_console_get_completion_word (line);
    return g_strdup_printf ("%.*s%.*s", (gint)(strlen (line) - strlen (word)), line,
                            (gint)common, candidates[0]);
}

/* ==========================================================================
 * The log
 * ========================================================================== */

/* the default handler libregnum's chains to (classic g_log() calls) */
static GLogFunc previous_handler = NULL;
static gpointer previous_handler_data = NULL;

/*
 * capture_message:
 * A copy of one message for every capturing console whose levels match,
 * queued under the console's lock (any thread).  It never logs itself.
 */
static void
capture_message (const gchar    *domain,
                 GLogLevelFlags  log_level,
                 const gchar    *message)
{
    GSList *l;

    if (message == NULL)
        return;
    g_mutex_lock (&capture_lock);
    for (l = capturing; l != NULL; l = l->next)
    {
        LrgDebugConsole *console = (LrgDebugConsole *)l->data;
        g_autofree gchar *text = NULL;
        gboolean is_error;

        if ((console->log_levels & (log_level & G_LOG_LEVEL_MASK)) == 0)
            continue;
        is_error = (log_level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL |
                                 G_LOG_LEVEL_WARNING)) != 0;
        text = g_strdup_printf ("[%s] %s", domain != NULL ? domain : "log", message);
        g_mutex_lock (&console->log_lock);
        g_ptr_array_add (console->log_pending, lrg_console_output_new (text, FALSE, is_error));
        g_mutex_unlock (&console->log_lock);
    }
    g_mutex_unlock (&capture_lock);
}

/*
 * capture_handler:
 * Classic g_log() calls reach the default handler (GLib's own, or a test
 * harness's): copied, then handed on.
 */
static void
capture_handler (const gchar    *domain,
                 GLogLevelFlags  log_level,
                 const gchar    *message,
                 gpointer        user_data)
{
    (void)user_data;
    capture_message (domain, log_level, message);
    if (previous_handler != NULL)
        previous_handler (domain, log_level, message, previous_handler_data);
    else
        g_log_default_handler (domain, log_level, message, NULL);
}

/*
 * capture_writer:
 * Structured messages (g_log_structured()): copied, then written by the
 * default writer.  Messages that came through the classic API carry
 * GLIB_OLD_LOG_API and were copied by capture_handler() already.
 */
static GLogWriterOutput
capture_writer (GLogLevelFlags   log_level,
                const GLogField *fields,
                gsize            n_fields,
                gpointer         user_data)
{
    const gchar *domain;
    const gchar *message;
    gboolean classic;
    gsize i;

    domain = NULL;
    message = NULL;
    classic = FALSE;
    for (i = 0; i < n_fields; i++)
    {
        if (g_strcmp0 (fields[i].key, "MESSAGE") == 0 && fields[i].length < 0)
            message = (const gchar *)fields[i].value;
        else if (g_strcmp0 (fields[i].key, "GLIB_DOMAIN") == 0 && fields[i].length < 0)
            domain = (const gchar *)fields[i].value;
        else if (g_strcmp0 (fields[i].key, "GLIB_OLD_LOG_API") == 0)
            classic = TRUE;
    }
    if (!classic)
        capture_message (domain, log_level, message);
    return g_log_writer_default (log_level, fields, n_fields, user_data);
}

void
lrg_debug_console_set_log_capture (LrgDebugConsole *self,
                                   gboolean         capture,
                                   GLogLevelFlags   levels)
{
    static gsize installed = 0;

    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));

    /* the writer can be set once per process */
    if (capture && g_once_init_enter (&installed))
    {
        g_log_set_writer_func (capture_writer, NULL, NULL);
        previous_handler = g_log_set_default_handler (capture_handler, NULL);
        /* GLib hands back its own handler; its data is unused */
        previous_handler_data = NULL;
        g_once_init_leave (&installed, 1);
    }

    g_mutex_lock (&capture_lock);
    capturing = g_slist_remove (capturing, self);
    if (capture)
        capturing = g_slist_prepend (capturing, self);
    self->log_capture = capture;
    self->log_levels = levels;
    g_mutex_unlock (&capture_lock);
}

gboolean
lrg_debug_console_get_log_capture (LrgDebugConsole *self)
{
    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), FALSE);
    return self->log_capture;
}

guint
lrg_debug_console_flush_log (LrgDebugConsole *self)
{
    g_autoptr(GPtrArray) pending = NULL;
    guint i;

    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), 0);

    g_mutex_lock (&self->log_lock);
    pending = g_steal_pointer (&self->log_pending);
    self->log_pending = g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_console_output_free);
    g_mutex_unlock (&self->log_lock);

    for (i = 0; i < pending->len; i++)
    {
        LrgConsoleOutput *output = (LrgConsoleOutput *)g_ptr_array_index (pending, i);

        add_output (self, output->text, FALSE, output->is_error);
    }
    return pending->len;
}

guint
lrg_debug_console_get_max_output (LrgDebugConsole *self)
{
    g_return_val_if_fail (LRG_IS_DEBUG_CONSOLE (self), 0);
    return self->max_output;
}

void
lrg_debug_console_set_max_output (LrgDebugConsole *self,
                                  guint            max_output)
{
    g_return_if_fail (LRG_IS_DEBUG_CONSOLE (self));

    self->max_output = MAX (1, max_output);
    while (g_queue_get_length (self->output) > self->max_output)
        lrg_console_output_free ((LrgConsoleOutput *)g_queue_pop_head (self->output));
}
