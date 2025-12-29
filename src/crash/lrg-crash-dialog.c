/* lrg-crash-dialog.c - Abstract base class for crash dialogs
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-crash-dialog.h"

/**
 * SECTION:lrg-crash-dialog
 * @title: LrgCrashDialog
 * @short_description: Abstract base class for crash dialogs
 *
 * #LrgCrashDialog is an abstract base class for displaying crash
 * information to users. Subclasses can implement various UIs:
 *
 * - #LrgCrashDialogTerminal: Outputs to stderr/file (default)
 * - Custom GTK dialog
 * - Custom raylib/graylib overlay
 *
 * The crash dialog is called by #LrgCrashReporter when a crash
 * is detected.
 */

G_DEFINE_ABSTRACT_TYPE (LrgCrashDialog, lrg_crash_dialog, G_TYPE_OBJECT)

/*
 * Default implementation of hide() - does nothing
 */
static void
lrg_crash_dialog_real_hide (LrgCrashDialog *self)
{
    /* Default implementation does nothing */
}

static void
lrg_crash_dialog_class_init (LrgCrashDialogClass *klass)
{
    /* Set default implementation for hide */
    klass->hide = lrg_crash_dialog_real_hide;
}

static void
lrg_crash_dialog_init (LrgCrashDialog *self)
{
}

/**
 * lrg_crash_dialog_show:
 * @self: an #LrgCrashDialog
 * @crash_info: crash information string
 *
 * Shows the crash dialog with the provided crash information.
 * Subclasses must implement this method.
 */
void
lrg_crash_dialog_show (LrgCrashDialog *self,
                       const gchar    *crash_info)
{
    LrgCrashDialogClass *klass;

    g_return_if_fail (LRG_IS_CRASH_DIALOG (self));
    g_return_if_fail (crash_info != NULL);

    klass = LRG_CRASH_DIALOG_GET_CLASS (self);
    g_return_if_fail (klass->show != NULL);

    klass->show (self, crash_info);
}

/**
 * lrg_crash_dialog_hide:
 * @self: an #LrgCrashDialog
 *
 * Hides the crash dialog.
 * Default implementation does nothing.
 */
void
lrg_crash_dialog_hide (LrgCrashDialog *self)
{
    LrgCrashDialogClass *klass;

    g_return_if_fail (LRG_IS_CRASH_DIALOG (self));

    klass = LRG_CRASH_DIALOG_GET_CLASS (self);
    if (klass->hide != NULL)
        klass->hide (self);
}
