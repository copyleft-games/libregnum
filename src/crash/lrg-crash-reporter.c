/* lrg-crash-reporter.c - Crash capture and reporting
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-crash-reporter.h"
#include "lrg-crash-dialog-terminal.h"

#include <string.h>
#include <time.h>

/* POSIX signal handling (not available on Windows) */
#ifndef LRG_PLATFORM_WINDOWS
#include <signal.h>
#include <unistd.h>
#ifdef __linux__
#include <execinfo.h>
#include <sys/utsname.h>
#endif
#endif /* !LRG_PLATFORM_WINDOWS */

/**
 * SECTION:lrg-crash-reporter
 * @title: LrgCrashReporter
 * @short_description: Crash capture and reporting
 *
 * #LrgCrashReporter provides crash detection and reporting functionality.
 * It installs signal handlers for common crash signals (SIGSEGV, SIGABRT,
 * SIGFPE, SIGBUS, SIGILL) and collects crash information including:
 *
 * - Stack trace (when available)
 * - Signal information
 * - System information (OS, architecture)
 * - Application name and version
 * - Custom metadata
 * - Timestamp
 *
 * When a crash is detected, the reporter formats this information and
 * displays it via a #LrgCrashDialog. By default, a #LrgCrashDialogTerminal
 * is used which outputs to stderr and optionally a log file.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * LrgCrashReporter *reporter = lrg_crash_reporter_get_default ();
 *
 * lrg_crash_reporter_set_app_name (reporter, "My Game");
 * lrg_crash_reporter_set_app_version (reporter, "1.0.0");
 * lrg_crash_reporter_set_log_path (reporter, "crash.log");
 *
 * if (!lrg_crash_reporter_install (reporter, &error))
 *     g_warning ("Failed to install crash handler: %s", error->message);
 * ]|
 */

#define MAX_STACK_FRAMES 64

struct _LrgCrashReporter
{
    GObject          parent_instance;

    LrgCrashDialog  *dialog;
    GHashTable      *metadata;

    gchar           *app_name;
    gchar           *app_version;
    gchar           *log_path;

    gboolean         installed;

#ifndef LRG_PLATFORM_WINDOWS
    /* Previous signal handlers for restoration (POSIX only) */
    struct sigaction old_sigsegv;
    struct sigaction old_sigabrt;
    struct sigaction old_sigfpe;
    struct sigaction old_sigbus;
    struct sigaction old_sigill;
#endif
};

G_DEFINE_TYPE (LrgCrashReporter, lrg_crash_reporter, G_TYPE_OBJECT)

/* Global pointer for signal handler access */
static LrgCrashReporter *g_crash_reporter_instance = NULL;

G_DEFINE_QUARK (lrg-crash-reporter-error-quark, lrg_crash_reporter_error)

enum
{
    PROP_0,
    PROP_APP_NAME,
    PROP_APP_VERSION,
    PROP_LOG_PATH,
    PROP_INSTALLED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

#ifndef LRG_PLATFORM_WINDOWS
/*
 * Get signal name from number (POSIX only)
 */
static const gchar *
get_signal_name (gint sig)
{
    switch (sig)
    {
    case SIGSEGV:
        return "SIGSEGV (Segmentation fault)";
    case SIGABRT:
        return "SIGABRT (Aborted)";
    case SIGFPE:
        return "SIGFPE (Floating point exception)";
    case SIGBUS:
        return "SIGBUS (Bus error)";
    case SIGILL:
        return "SIGILL (Illegal instruction)";
    default:
        return "Unknown signal";
    }
}
#endif /* !LRG_PLATFORM_WINDOWS */

/*
 * Build crash report string
 */
static gchar *
build_crash_report (LrgCrashReporter *self,
                    gint              signal_number)
{
    GString *report;
    time_t now;
    struct tm *tm_info;
    gchar time_buf[64];
#ifdef __linux__
    void *stack_frames[MAX_STACK_FRAMES];
    gint frame_count;
    gchar **symbols;
    struct utsname sys_info;
    gint i;
#endif
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    report = g_string_new (NULL);

    /* Timestamp */
    now = time (NULL);
    tm_info = localtime (&now);
    strftime (time_buf, sizeof (time_buf), "%Y-%m-%d %H:%M:%S %Z", tm_info);
    g_string_append_printf (report, "Timestamp: %s\n\n", time_buf);

    /* Application info */
    if (self->app_name != NULL)
        g_string_append_printf (report, "Application: %s\n", self->app_name);
    if (self->app_version != NULL)
        g_string_append_printf (report, "Version: %s\n", self->app_version);
    g_string_append (report, "\n");

    /* Signal info */
#ifndef LRG_PLATFORM_WINDOWS
    g_string_append_printf (report, "Signal: %d - %s\n\n",
                            signal_number, get_signal_name (signal_number));
#else
    g_string_append_printf (report, "Exception code: %d\n\n", signal_number);
#endif

#ifdef __linux__
    /* System info */
    if (uname (&sys_info) == 0)
    {
        g_string_append (report, "System Information:\n");
        g_string_append_printf (report, "  OS: %s %s\n", sys_info.sysname, sys_info.release);
        g_string_append_printf (report, "  Machine: %s\n", sys_info.machine);
        g_string_append_printf (report, "  Node: %s\n", sys_info.nodename);
        g_string_append (report, "\n");
    }

    /* Stack trace */
    frame_count = backtrace (stack_frames, MAX_STACK_FRAMES);
    if (frame_count > 0)
    {
        symbols = backtrace_symbols (stack_frames, frame_count);
        if (symbols != NULL)
        {
            g_string_append (report, "Stack Trace:\n");
            for (i = 0; i < frame_count; i++)
            {
                g_string_append_printf (report, "  #%d %s\n", i, symbols[i]);
            }
            g_string_append (report, "\n");
            free (symbols);
        }
    }
#else
    g_string_append (report, "Stack trace not available on this platform.\n\n");
#endif

    /* Custom metadata */
    if (g_hash_table_size (self->metadata) > 0)
    {
        g_string_append (report, "Custom Metadata:\n");
        g_hash_table_iter_init (&iter, self->metadata);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            g_string_append_printf (report, "  %s: %s\n",
                                    (const gchar *)key,
                                    (const gchar *)value);
        }
        g_string_append (report, "\n");
    }

    /* Library version */
    g_string_append_printf (report, "Libregnum Version: %d.%d.%d\n",
                            LRG_VERSION_MAJOR,
                            LRG_VERSION_MINOR,
                            LRG_VERSION_MICRO);

    return g_string_free (report, FALSE);
}

#ifndef LRG_PLATFORM_WINDOWS
/*
 * Signal handler function (POSIX only)
 */
static void
crash_signal_handler (int         sig,
                      siginfo_t  *info,
                      void       *context)
{
    if (g_crash_reporter_instance != NULL)
    {
        lrg_crash_reporter_report_crash (g_crash_reporter_instance,
                                         sig, info, context);
    }

    /* Re-raise the signal to get default behavior (core dump, etc.) */
    signal (sig, SIG_DFL);
    raise (sig);
}
#endif /* !LRG_PLATFORM_WINDOWS */

static void
lrg_crash_reporter_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgCrashReporter *self = LRG_CRASH_REPORTER (object);

    switch (prop_id)
    {
    case PROP_APP_NAME:
        lrg_crash_reporter_set_app_name (self, g_value_get_string (value));
        break;
    case PROP_APP_VERSION:
        lrg_crash_reporter_set_app_version (self, g_value_get_string (value));
        break;
    case PROP_LOG_PATH:
        lrg_crash_reporter_set_log_path (self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_crash_reporter_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgCrashReporter *self = LRG_CRASH_REPORTER (object);

    switch (prop_id)
    {
    case PROP_APP_NAME:
        g_value_set_string (value, self->app_name);
        break;
    case PROP_APP_VERSION:
        g_value_set_string (value, self->app_version);
        break;
    case PROP_LOG_PATH:
        g_value_set_string (value, self->log_path);
        break;
    case PROP_INSTALLED:
        g_value_set_boolean (value, self->installed);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_crash_reporter_dispose (GObject *object)
{
    LrgCrashReporter *self = LRG_CRASH_REPORTER (object);

    if (self->installed)
        lrg_crash_reporter_uninstall (self);

    g_clear_object (&self->dialog);

    G_OBJECT_CLASS (lrg_crash_reporter_parent_class)->dispose (object);
}

static void
lrg_crash_reporter_finalize (GObject *object)
{
    LrgCrashReporter *self = LRG_CRASH_REPORTER (object);

    g_clear_pointer (&self->metadata, g_hash_table_unref);
    g_clear_pointer (&self->app_name, g_free);
    g_clear_pointer (&self->app_version, g_free);
    g_clear_pointer (&self->log_path, g_free);

    if (g_crash_reporter_instance == self)
        g_crash_reporter_instance = NULL;

    G_OBJECT_CLASS (lrg_crash_reporter_parent_class)->finalize (object);
}

static void
lrg_crash_reporter_class_init (LrgCrashReporterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = lrg_crash_reporter_set_property;
    object_class->get_property = lrg_crash_reporter_get_property;
    object_class->dispose = lrg_crash_reporter_dispose;
    object_class->finalize = lrg_crash_reporter_finalize;

    properties[PROP_APP_NAME] =
        g_param_spec_string ("app-name",
                             "App Name",
                             "Application name for crash reports",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_APP_VERSION] =
        g_param_spec_string ("app-version",
                             "App Version",
                             "Application version for crash reports",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LOG_PATH] =
        g_param_spec_string ("log-path",
                             "Log Path",
                             "Path for crash log file",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_INSTALLED] =
        g_param_spec_boolean ("installed",
                              "Installed",
                              "Whether crash handlers are installed",
                              FALSE,
                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_crash_reporter_init (LrgCrashReporter *self)
{
    self->metadata = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, g_free);
    self->installed = FALSE;
}

/**
 * lrg_crash_reporter_new:
 *
 * Creates a new #LrgCrashReporter.
 *
 * Returns: (transfer full): A new #LrgCrashReporter
 */
LrgCrashReporter *
lrg_crash_reporter_new (void)
{
    return g_object_new (LRG_TYPE_CRASH_REPORTER, NULL);
}

/**
 * lrg_crash_reporter_get_default:
 *
 * Gets the default crash reporter singleton.
 *
 * Returns: (transfer none): The default #LrgCrashReporter
 */
LrgCrashReporter *
lrg_crash_reporter_get_default (void)
{
    if (g_crash_reporter_instance == NULL)
        g_crash_reporter_instance = lrg_crash_reporter_new ();

    return g_crash_reporter_instance;
}

/**
 * lrg_crash_reporter_install:
 * @self: an #LrgCrashReporter
 * @error: (nullable): return location for error
 *
 * Installs signal handlers for crash detection.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_crash_reporter_install (LrgCrashReporter  *self,
                            GError           **error)
{
    g_return_val_if_fail (LRG_IS_CRASH_REPORTER (self), FALSE);

    if (self->installed)
    {
        g_set_error (error,
                     LRG_CRASH_REPORTER_ERROR,
                     LRG_CRASH_REPORTER_ERROR_ALREADY_INSTALLED,
                     "Crash handlers are already installed");
        return FALSE;
    }

#ifndef LRG_PLATFORM_WINDOWS
    {
        struct sigaction sa;

        /* Set up signal handler */
        memset (&sa, 0, sizeof (sa));
        sa.sa_sigaction = crash_signal_handler;
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
        sigemptyset (&sa.sa_mask);

        /* Install handlers, saving old ones */
        if (sigaction (SIGSEGV, &sa, &self->old_sigsegv) != 0 ||
            sigaction (SIGABRT, &sa, &self->old_sigabrt) != 0 ||
            sigaction (SIGFPE, &sa, &self->old_sigfpe) != 0 ||
            sigaction (SIGBUS, &sa, &self->old_sigbus) != 0 ||
            sigaction (SIGILL, &sa, &self->old_sigill) != 0)
        {
            g_set_error (error,
                         LRG_CRASH_REPORTER_ERROR,
                         LRG_CRASH_REPORTER_ERROR_SIGNAL_FAILED,
                         "Failed to install signal handler");
            return FALSE;
        }
    }
#else
    /* Windows: Signal-based crash handling not implemented yet */
    g_set_error (error,
                 LRG_CRASH_REPORTER_ERROR,
                 LRG_CRASH_REPORTER_ERROR_SIGNAL_FAILED,
                 "Crash handler not available on Windows");
    return FALSE;
#endif

    g_crash_reporter_instance = self;
    self->installed = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INSTALLED]);

    return TRUE;
}

/**
 * lrg_crash_reporter_uninstall:
 * @self: an #LrgCrashReporter
 *
 * Removes the installed signal handlers.
 */
void
lrg_crash_reporter_uninstall (LrgCrashReporter *self)
{
    g_return_if_fail (LRG_IS_CRASH_REPORTER (self));

    if (!self->installed)
        return;

#ifndef LRG_PLATFORM_WINDOWS
    /* Restore old handlers */
    sigaction (SIGSEGV, &self->old_sigsegv, NULL);
    sigaction (SIGABRT, &self->old_sigabrt, NULL);
    sigaction (SIGFPE, &self->old_sigfpe, NULL);
    sigaction (SIGBUS, &self->old_sigbus, NULL);
    sigaction (SIGILL, &self->old_sigill, NULL);
#endif

    if (g_crash_reporter_instance == self)
        g_crash_reporter_instance = NULL;

    self->installed = FALSE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INSTALLED]);
}

/**
 * lrg_crash_reporter_is_installed:
 * @self: an #LrgCrashReporter
 *
 * Checks if crash handlers are installed.
 *
 * Returns: %TRUE if installed
 */
gboolean
lrg_crash_reporter_is_installed (LrgCrashReporter *self)
{
    g_return_val_if_fail (LRG_IS_CRASH_REPORTER (self), FALSE);

    return self->installed;
}

/**
 * lrg_crash_reporter_set_dialog:
 * @self: an #LrgCrashReporter
 * @dialog: (transfer none) (nullable): the #LrgCrashDialog to use
 *
 * Sets the crash dialog.
 */
void
lrg_crash_reporter_set_dialog (LrgCrashReporter *self,
                               LrgCrashDialog   *dialog)
{
    g_return_if_fail (LRG_IS_CRASH_REPORTER (self));
    g_return_if_fail (dialog == NULL || LRG_IS_CRASH_DIALOG (dialog));

    g_set_object (&self->dialog, dialog);
}

/**
 * lrg_crash_reporter_get_dialog:
 * @self: an #LrgCrashReporter
 *
 * Gets the crash dialog.
 *
 * Returns: (transfer none) (nullable): The #LrgCrashDialog
 */
LrgCrashDialog *
lrg_crash_reporter_get_dialog (LrgCrashReporter *self)
{
    g_return_val_if_fail (LRG_IS_CRASH_REPORTER (self), NULL);

    return self->dialog;
}

/**
 * lrg_crash_reporter_set_app_name:
 * @self: an #LrgCrashReporter
 * @app_name: the application name
 *
 * Sets the application name.
 */
void
lrg_crash_reporter_set_app_name (LrgCrashReporter *self,
                                 const gchar      *app_name)
{
    g_return_if_fail (LRG_IS_CRASH_REPORTER (self));

    if (g_strcmp0 (self->app_name, app_name) != 0)
    {
        g_free (self->app_name);
        self->app_name = g_strdup (app_name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_APP_NAME]);
    }
}

/**
 * lrg_crash_reporter_get_app_name:
 * @self: an #LrgCrashReporter
 *
 * Gets the application name.
 *
 * Returns: (transfer none) (nullable): The app name
 */
const gchar *
lrg_crash_reporter_get_app_name (LrgCrashReporter *self)
{
    g_return_val_if_fail (LRG_IS_CRASH_REPORTER (self), NULL);

    return self->app_name;
}

/**
 * lrg_crash_reporter_set_app_version:
 * @self: an #LrgCrashReporter
 * @app_version: the application version
 *
 * Sets the application version.
 */
void
lrg_crash_reporter_set_app_version (LrgCrashReporter *self,
                                    const gchar      *app_version)
{
    g_return_if_fail (LRG_IS_CRASH_REPORTER (self));

    if (g_strcmp0 (self->app_version, app_version) != 0)
    {
        g_free (self->app_version);
        self->app_version = g_strdup (app_version);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_APP_VERSION]);
    }
}

/**
 * lrg_crash_reporter_get_app_version:
 * @self: an #LrgCrashReporter
 *
 * Gets the application version.
 *
 * Returns: (transfer none) (nullable): The app version
 */
const gchar *
lrg_crash_reporter_get_app_version (LrgCrashReporter *self)
{
    g_return_val_if_fail (LRG_IS_CRASH_REPORTER (self), NULL);

    return self->app_version;
}

/**
 * lrg_crash_reporter_set_log_path:
 * @self: an #LrgCrashReporter
 * @log_path: (nullable): path for crash log
 *
 * Sets the crash log path.
 */
void
lrg_crash_reporter_set_log_path (LrgCrashReporter *self,
                                 const gchar      *log_path)
{
    g_return_if_fail (LRG_IS_CRASH_REPORTER (self));

    if (g_strcmp0 (self->log_path, log_path) != 0)
    {
        g_free (self->log_path);
        self->log_path = g_strdup (log_path);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOG_PATH]);
    }
}

/**
 * lrg_crash_reporter_get_log_path:
 * @self: an #LrgCrashReporter
 *
 * Gets the crash log path.
 *
 * Returns: (transfer none) (nullable): The log path
 */
const gchar *
lrg_crash_reporter_get_log_path (LrgCrashReporter *self)
{
    g_return_val_if_fail (LRG_IS_CRASH_REPORTER (self), NULL);

    return self->log_path;
}

/**
 * lrg_crash_reporter_add_metadata:
 * @self: an #LrgCrashReporter
 * @key: metadata key
 * @value: metadata value
 *
 * Adds custom metadata.
 */
void
lrg_crash_reporter_add_metadata (LrgCrashReporter *self,
                                 const gchar      *key,
                                 const gchar      *value)
{
    g_return_if_fail (LRG_IS_CRASH_REPORTER (self));
    g_return_if_fail (key != NULL);
    g_return_if_fail (value != NULL);

    g_hash_table_insert (self->metadata, g_strdup (key), g_strdup (value));
}

/**
 * lrg_crash_reporter_remove_metadata:
 * @self: an #LrgCrashReporter
 * @key: metadata key to remove
 *
 * Removes custom metadata.
 */
void
lrg_crash_reporter_remove_metadata (LrgCrashReporter *self,
                                    const gchar      *key)
{
    g_return_if_fail (LRG_IS_CRASH_REPORTER (self));
    g_return_if_fail (key != NULL);

    g_hash_table_remove (self->metadata, key);
}

/**
 * lrg_crash_reporter_clear_metadata:
 * @self: an #LrgCrashReporter
 *
 * Clears all custom metadata.
 */
void
lrg_crash_reporter_clear_metadata (LrgCrashReporter *self)
{
    g_return_if_fail (LRG_IS_CRASH_REPORTER (self));

    g_hash_table_remove_all (self->metadata);
}

/**
 * lrg_crash_reporter_report_crash:
 * @self: an #LrgCrashReporter
 * @signal_number: the signal that caused the crash
 * @info: (nullable): signal info structure
 * @context: (nullable): signal context
 *
 * Triggers a crash report.
 */
void
lrg_crash_reporter_report_crash (LrgCrashReporter *self,
                                 gint              signal_number,
                                 gpointer          info,
                                 gpointer          context)
{
    g_autofree gchar *report = NULL;
    LrgCrashDialog *dialog;

    g_return_if_fail (LRG_IS_CRASH_REPORTER (self));

    /* Build the crash report */
    report = build_crash_report (self, signal_number);

    /* Get or create dialog */
    dialog = self->dialog;
    if (dialog == NULL)
    {
        /* Use default terminal dialog with log path if set */
        if (self->log_path != NULL)
            dialog = LRG_CRASH_DIALOG (lrg_crash_dialog_terminal_new_with_file (self->log_path));
        else
            dialog = LRG_CRASH_DIALOG (lrg_crash_dialog_terminal_new ());

        /* Show the dialog (it's temporary, so just show) */
        lrg_crash_dialog_show (dialog, report);
        g_object_unref (dialog);
    }
    else
    {
        lrg_crash_dialog_show (dialog, report);
    }
}
