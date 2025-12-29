/* lrg-crash-dialog-terminal.h - Terminal/stderr crash dialog
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_CRASH_DIALOG_TERMINAL_H
#define LRG_CRASH_DIALOG_TERMINAL_H

#include "lrg-crash-dialog.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_CRASH_DIALOG_TERMINAL (lrg_crash_dialog_terminal_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCrashDialogTerminal, lrg_crash_dialog_terminal, LRG, CRASH_DIALOG_TERMINAL, LrgCrashDialog)

/**
 * lrg_crash_dialog_terminal_new:
 *
 * Creates a new #LrgCrashDialogTerminal that outputs crash
 * information to stderr.
 *
 * Returns: (transfer full): A new #LrgCrashDialogTerminal
 */
LRG_AVAILABLE_IN_ALL
LrgCrashDialogTerminal *
lrg_crash_dialog_terminal_new (void);

/**
 * lrg_crash_dialog_terminal_new_with_file:
 * @log_path: path to write crash log
 *
 * Creates a new #LrgCrashDialogTerminal that outputs crash
 * information to both stderr and a log file.
 *
 * Returns: (transfer full): A new #LrgCrashDialogTerminal
 */
LRG_AVAILABLE_IN_ALL
LrgCrashDialogTerminal *
lrg_crash_dialog_terminal_new_with_file (const gchar *log_path);

/**
 * lrg_crash_dialog_terminal_get_log_path:
 * @self: an #LrgCrashDialogTerminal
 *
 * Gets the log file path, if set.
 *
 * Returns: (transfer none) (nullable): The log path, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_crash_dialog_terminal_get_log_path (LrgCrashDialogTerminal *self);

/**
 * lrg_crash_dialog_terminal_set_log_path:
 * @self: an #LrgCrashDialogTerminal
 * @log_path: (nullable): path to write crash log, or %NULL to disable
 *
 * Sets the log file path for crash output.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_crash_dialog_terminal_set_log_path (LrgCrashDialogTerminal *self,
                                        const gchar            *log_path);

G_END_DECLS

#endif /* LRG_CRASH_DIALOG_TERMINAL_H */
