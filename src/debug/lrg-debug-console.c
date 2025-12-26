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
};

#pragma GCC visibility push(default)
G_DEFINE_TYPE (LrgDebugConsole, lrg_debug_console, G_TYPE_OBJECT)
#pragma GCC visibility pop

static LrgDebugConsole *default_console = NULL;

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

    g_hash_table_destroy (self->commands);
    g_queue_free_full (self->output, (GDestroyNotify)lrg_console_output_free);
    g_queue_free_full (self->history, g_free);

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

    lrg_debug (LRG_LOG_DOMAIN_DEBUG, "Console history cleared");
}
