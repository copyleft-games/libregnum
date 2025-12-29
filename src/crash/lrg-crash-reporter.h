/* lrg-crash-reporter.h - Crash capture and reporting
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_CRASH_REPORTER_H
#define LRG_CRASH_REPORTER_H

#include <glib-object.h>
#include "lrg-crash-dialog.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_CRASH_REPORTER (lrg_crash_reporter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCrashReporter, lrg_crash_reporter, LRG, CRASH_REPORTER, GObject)

/**
 * LrgCrashReporterError:
 * @LRG_CRASH_REPORTER_ERROR_ALREADY_INSTALLED: Handler already installed
 * @LRG_CRASH_REPORTER_ERROR_SIGNAL_FAILED: Failed to install signal handler
 *
 * Error codes for crash reporter operations.
 */
typedef enum
{
    LRG_CRASH_REPORTER_ERROR_ALREADY_INSTALLED,
    LRG_CRASH_REPORTER_ERROR_SIGNAL_FAILED
} LrgCrashReporterError;

#define LRG_CRASH_REPORTER_ERROR (lrg_crash_reporter_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark
lrg_crash_reporter_error_quark (void);

/**
 * lrg_crash_reporter_new:
 *
 * Creates a new #LrgCrashReporter with default settings.
 *
 * Returns: (transfer full): A new #LrgCrashReporter
 */
LRG_AVAILABLE_IN_ALL
LrgCrashReporter *
lrg_crash_reporter_new (void);

/**
 * lrg_crash_reporter_get_default:
 *
 * Gets the default crash reporter singleton.
 * Creates one if it doesn't exist.
 *
 * Returns: (transfer none): The default #LrgCrashReporter
 */
LRG_AVAILABLE_IN_ALL
LrgCrashReporter *
lrg_crash_reporter_get_default (void);

/**
 * lrg_crash_reporter_install:
 * @self: an #LrgCrashReporter
 * @error: (nullable): return location for error
 *
 * Installs signal handlers for crash detection.
 * Handles SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_crash_reporter_install (LrgCrashReporter  *self,
                            GError           **error);

/**
 * lrg_crash_reporter_uninstall:
 * @self: an #LrgCrashReporter
 *
 * Removes the installed signal handlers and restores defaults.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_crash_reporter_uninstall (LrgCrashReporter *self);

/**
 * lrg_crash_reporter_is_installed:
 * @self: an #LrgCrashReporter
 *
 * Checks if crash handlers are currently installed.
 *
 * Returns: %TRUE if handlers are installed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_crash_reporter_is_installed (LrgCrashReporter *self);

/**
 * lrg_crash_reporter_set_dialog:
 * @self: an #LrgCrashReporter
 * @dialog: (transfer none) (nullable): the #LrgCrashDialog to use
 *
 * Sets the dialog to show when a crash is detected.
 * If %NULL, uses the default terminal dialog.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_crash_reporter_set_dialog (LrgCrashReporter *self,
                               LrgCrashDialog   *dialog);

/**
 * lrg_crash_reporter_get_dialog:
 * @self: an #LrgCrashReporter
 *
 * Gets the crash dialog.
 *
 * Returns: (transfer none) (nullable): The #LrgCrashDialog
 */
LRG_AVAILABLE_IN_ALL
LrgCrashDialog *
lrg_crash_reporter_get_dialog (LrgCrashReporter *self);

/**
 * lrg_crash_reporter_set_app_name:
 * @self: an #LrgCrashReporter
 * @app_name: the application name
 *
 * Sets the application name for crash reports.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_crash_reporter_set_app_name (LrgCrashReporter *self,
                                 const gchar      *app_name);

/**
 * lrg_crash_reporter_get_app_name:
 * @self: an #LrgCrashReporter
 *
 * Gets the application name.
 *
 * Returns: (transfer none) (nullable): The app name
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_crash_reporter_get_app_name (LrgCrashReporter *self);

/**
 * lrg_crash_reporter_set_app_version:
 * @self: an #LrgCrashReporter
 * @app_version: the application version string
 *
 * Sets the application version for crash reports.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_crash_reporter_set_app_version (LrgCrashReporter *self,
                                    const gchar      *app_version);

/**
 * lrg_crash_reporter_get_app_version:
 * @self: an #LrgCrashReporter
 *
 * Gets the application version.
 *
 * Returns: (transfer none) (nullable): The app version
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_crash_reporter_get_app_version (LrgCrashReporter *self);

/**
 * lrg_crash_reporter_set_log_path:
 * @self: an #LrgCrashReporter
 * @log_path: (nullable): path for crash log file
 *
 * Sets the path where crash logs are written.
 * If %NULL, no file logging is performed.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_crash_reporter_set_log_path (LrgCrashReporter *self,
                                 const gchar      *log_path);

/**
 * lrg_crash_reporter_get_log_path:
 * @self: an #LrgCrashReporter
 *
 * Gets the crash log path.
 *
 * Returns: (transfer none) (nullable): The log path
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_crash_reporter_get_log_path (LrgCrashReporter *self);

/**
 * lrg_crash_reporter_add_metadata:
 * @self: an #LrgCrashReporter
 * @key: metadata key
 * @value: metadata value
 *
 * Adds custom metadata to include in crash reports.
 * Useful for including game state, player info, etc.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_crash_reporter_add_metadata (LrgCrashReporter *self,
                                 const gchar      *key,
                                 const gchar      *value);

/**
 * lrg_crash_reporter_remove_metadata:
 * @self: an #LrgCrashReporter
 * @key: metadata key to remove
 *
 * Removes custom metadata.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_crash_reporter_remove_metadata (LrgCrashReporter *self,
                                    const gchar      *key);

/**
 * lrg_crash_reporter_clear_metadata:
 * @self: an #LrgCrashReporter
 *
 * Clears all custom metadata.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_crash_reporter_clear_metadata (LrgCrashReporter *self);

/**
 * lrg_crash_reporter_report_crash:
 * @self: an #LrgCrashReporter
 * @signal_number: the signal that caused the crash
 * @info: (nullable): signal info structure
 * @context: (nullable): signal context
 *
 * Manually trigger a crash report. This is called automatically
 * by the signal handler, but can be called manually for testing
 * or for handling other error conditions.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_crash_reporter_report_crash (LrgCrashReporter *self,
                                 gint              signal_number,
                                 gpointer          info,
                                 gpointer          context);

G_END_DECLS

#endif /* LRG_CRASH_REPORTER_H */
