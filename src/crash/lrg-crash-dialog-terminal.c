/* lrg-crash-dialog-terminal.c - Terminal/stderr crash dialog
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-crash-dialog-terminal.h"
#include <stdio.h>

/**
 * SECTION:lrg-crash-dialog-terminal
 * @title: LrgCrashDialogTerminal
 * @short_description: Terminal/stderr crash dialog
 *
 * #LrgCrashDialogTerminal is a concrete implementation of
 * #LrgCrashDialog that outputs crash information to stderr
 * and optionally to a log file.
 *
 * This is the default crash dialog used when no GUI is available
 * or when running in a terminal environment.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * LrgCrashDialogTerminal *dialog;
 *
 * // Output to stderr only
 * dialog = lrg_crash_dialog_terminal_new ();
 *
 * // Or output to stderr and a file
 * dialog = lrg_crash_dialog_terminal_new_with_file ("/tmp/crash.log");
 * ]|
 */

struct _LrgCrashDialogTerminal
{
    LrgCrashDialog  parent_instance;

    gchar          *log_path;
};

G_DEFINE_TYPE (LrgCrashDialogTerminal, lrg_crash_dialog_terminal, LRG_TYPE_CRASH_DIALOG)

enum
{
    PROP_0,
    PROP_LOG_PATH,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_crash_dialog_terminal_show (LrgCrashDialog *dialog,
                                const gchar    *crash_info)
{
    LrgCrashDialogTerminal *self = LRG_CRASH_DIALOG_TERMINAL (dialog);
    FILE *log_file;

    /* Output crash banner to stderr */
    fprintf (stderr, "\n");
    fprintf (stderr, "================================================================================\n");
    fprintf (stderr, "                            CRASH REPORT\n");
    fprintf (stderr, "================================================================================\n");
    fprintf (stderr, "%s\n", crash_info);
    fprintf (stderr, "================================================================================\n");
    fprintf (stderr, "\n");
    fflush (stderr);

    /* Optionally write to log file */
    if (self->log_path != NULL)
    {
        log_file = fopen (self->log_path, "a");
        if (log_file != NULL)
        {
            fprintf (log_file, "================================================================================\n");
            fprintf (log_file, "                            CRASH REPORT\n");
            fprintf (log_file, "================================================================================\n");
            fprintf (log_file, "%s\n", crash_info);
            fprintf (log_file, "================================================================================\n\n");
            fclose (log_file);

            fprintf (stderr, "Crash log written to: %s\n", self->log_path);
        }
        else
        {
            fprintf (stderr, "Warning: Could not write crash log to: %s\n", self->log_path);
        }
    }
}

static void
lrg_crash_dialog_terminal_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
    LrgCrashDialogTerminal *self = LRG_CRASH_DIALOG_TERMINAL (object);

    switch (prop_id)
    {
    case PROP_LOG_PATH:
        lrg_crash_dialog_terminal_set_log_path (self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_crash_dialog_terminal_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
    LrgCrashDialogTerminal *self = LRG_CRASH_DIALOG_TERMINAL (object);

    switch (prop_id)
    {
    case PROP_LOG_PATH:
        g_value_set_string (value, self->log_path);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_crash_dialog_terminal_finalize (GObject *object)
{
    LrgCrashDialogTerminal *self = LRG_CRASH_DIALOG_TERMINAL (object);

    g_clear_pointer (&self->log_path, g_free);

    G_OBJECT_CLASS (lrg_crash_dialog_terminal_parent_class)->finalize (object);
}

static void
lrg_crash_dialog_terminal_class_init (LrgCrashDialogTerminalClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgCrashDialogClass *dialog_class = LRG_CRASH_DIALOG_CLASS (klass);

    object_class->set_property = lrg_crash_dialog_terminal_set_property;
    object_class->get_property = lrg_crash_dialog_terminal_get_property;
    object_class->finalize = lrg_crash_dialog_terminal_finalize;

    dialog_class->show = lrg_crash_dialog_terminal_show;

    /**
     * LrgCrashDialogTerminal:log-path:
     *
     * Path to write crash log file. If %NULL, only stderr is used.
     */
    properties[PROP_LOG_PATH] =
        g_param_spec_string ("log-path",
                             "Log Path",
                             "Path to write crash log file",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_crash_dialog_terminal_init (LrgCrashDialogTerminal *self)
{
    self->log_path = NULL;
}

/**
 * lrg_crash_dialog_terminal_new:
 *
 * Creates a new #LrgCrashDialogTerminal that outputs to stderr.
 *
 * Returns: (transfer full): A new #LrgCrashDialogTerminal
 */
LrgCrashDialogTerminal *
lrg_crash_dialog_terminal_new (void)
{
    return g_object_new (LRG_TYPE_CRASH_DIALOG_TERMINAL, NULL);
}

/**
 * lrg_crash_dialog_terminal_new_with_file:
 * @log_path: path to write crash log
 *
 * Creates a new #LrgCrashDialogTerminal that outputs to
 * both stderr and a log file.
 *
 * Returns: (transfer full): A new #LrgCrashDialogTerminal
 */
LrgCrashDialogTerminal *
lrg_crash_dialog_terminal_new_with_file (const gchar *log_path)
{
    return g_object_new (LRG_TYPE_CRASH_DIALOG_TERMINAL,
                         "log-path", log_path,
                         NULL);
}

/**
 * lrg_crash_dialog_terminal_get_log_path:
 * @self: an #LrgCrashDialogTerminal
 *
 * Gets the log file path.
 *
 * Returns: (transfer none) (nullable): The log path
 */
const gchar *
lrg_crash_dialog_terminal_get_log_path (LrgCrashDialogTerminal *self)
{
    g_return_val_if_fail (LRG_IS_CRASH_DIALOG_TERMINAL (self), NULL);

    return self->log_path;
}

/**
 * lrg_crash_dialog_terminal_set_log_path:
 * @self: an #LrgCrashDialogTerminal
 * @log_path: (nullable): path to write crash log
 *
 * Sets the log file path.
 */
void
lrg_crash_dialog_terminal_set_log_path (LrgCrashDialogTerminal *self,
                                        const gchar            *log_path)
{
    g_return_if_fail (LRG_IS_CRASH_DIALOG_TERMINAL (self));

    if (g_strcmp0 (self->log_path, log_path) != 0)
    {
        g_free (self->log_path);
        self->log_path = g_strdup (log_path);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOG_PATH]);
    }
}
