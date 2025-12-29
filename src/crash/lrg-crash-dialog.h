/* lrg-crash-dialog.h - Abstract base class for crash dialogs
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_CRASH_DIALOG_H
#define LRG_CRASH_DIALOG_H

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_CRASH_DIALOG (lrg_crash_dialog_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCrashDialog, lrg_crash_dialog, LRG, CRASH_DIALOG, GObject)

/**
 * LrgCrashDialogClass:
 * @parent_class: the parent class
 * @show: Show the crash dialog with crash information
 * @hide: Hide the crash dialog
 *
 * The virtual function table for #LrgCrashDialog.
 *
 * Subclasses must implement show() to display crash information
 * to the user. The hide() method has a default empty implementation.
 */
struct _LrgCrashDialogClass
{
    GObjectClass parent_class;

    /*< public >*/

    /**
     * LrgCrashDialogClass::show:
     * @self: an #LrgCrashDialog
     * @crash_info: crash information string to display
     *
     * Shows the crash dialog with the provided crash information.
     * Subclasses must implement this method.
     */
    void (*show) (LrgCrashDialog *self,
                  const gchar    *crash_info);

    /**
     * LrgCrashDialogClass::hide:
     * @self: an #LrgCrashDialog
     *
     * Hides the crash dialog.
     * Default implementation does nothing.
     */
    void (*hide) (LrgCrashDialog *self);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_crash_dialog_show:
 * @self: an #LrgCrashDialog
 * @crash_info: crash information string
 *
 * Shows the crash dialog with the provided crash information.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_crash_dialog_show (LrgCrashDialog *self,
                       const gchar    *crash_info);

/**
 * lrg_crash_dialog_hide:
 * @self: an #LrgCrashDialog
 *
 * Hides the crash dialog.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_crash_dialog_hide (LrgCrashDialog *self);

G_END_DECLS

#endif /* LRG_CRASH_DIALOG_H */
