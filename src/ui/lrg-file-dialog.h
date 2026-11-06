/* lrg-file-dialog.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFileDialog: asynchronous open / save / select-folder dialogs, shown
 * in-app on a canvas or through the XDG desktop portal.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "lrg-widget.h"
#include "lrg-file-browser.h"

G_BEGIN_DECLS

/**
 * LRG_FILE_DIALOG_ERROR:
 *
 * Error domain of #LrgFileDialog. Errors in this domain carry a
 * #LrgFileDialogError code.
 */
#define LRG_FILE_DIALOG_ERROR (lrg_file_dialog_error_quark ())

/**
 * lrg_file_dialog_error_quark:
 *
 * Returns: the #LRG_FILE_DIALOG_ERROR quark
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_file_dialog_error_quark (void);

/**
 * LrgFileDialogError:
 * @LRG_FILE_DIALOG_ERROR_FAILED: the dialog could not be shown (no
 *   canvas to show it on, or the portal failed)
 * @LRG_FILE_DIALOG_ERROR_CANCELLED: the operation was cancelled through
 *   its #GCancellable; the dialog was closed by the application
 * @LRG_FILE_DIALOG_ERROR_DISMISSED: the user closed the dialog without
 *   choosing (Cancel, Escape, gamepad B)
 *
 * Error codes of #LRG_FILE_DIALOG_ERROR.
 */
typedef enum
{
	LRG_FILE_DIALOG_ERROR_FAILED,
	LRG_FILE_DIALOG_ERROR_CANCELLED,
	LRG_FILE_DIALOG_ERROR_DISMISSED
} LrgFileDialogError;

LRG_AVAILABLE_IN_ALL
GType lrg_file_dialog_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FILE_DIALOG_ERROR (lrg_file_dialog_error_get_type ())

#define LRG_TYPE_FILE_DIALOG (lrg_file_dialog_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFileDialog, lrg_file_dialog, LRG, FILE_DIALOG, GObject)

/**
 * lrg_file_dialog_new:
 *
 * Creates a file dialog description. Configure it with its properties,
 * then call lrg_file_dialog_open(), lrg_file_dialog_open_multiple(),
 * lrg_file_dialog_save() or lrg_file_dialog_select_folder(). One
 * object can run several operations, one after the other or at once.
 *
 * Returns: (transfer full): a new #LrgFileDialog
 */
LRG_AVAILABLE_IN_ALL
LrgFileDialog * lrg_file_dialog_new (void);

/**
 * lrg_file_dialog_get_title:
 * @self: an #LrgFileDialog
 *
 * Returns: (transfer none) (nullable): the title, or %NULL for the
 *   default of each operation ("Open File", "Save File" ...)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_file_dialog_get_title (LrgFileDialog *self);

/**
 * lrg_file_dialog_set_title:
 * @self: an #LrgFileDialog
 * @title: (nullable): heading of the dialog
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_dialog_set_title (LrgFileDialog *self,
                                const gchar   *title);

/**
 * lrg_file_dialog_get_accept_label:
 * @self: an #LrgFileDialog
 *
 * Returns: (transfer none) (nullable): the accept button text, or
 *   %NULL for the default ("Open", "Save", "Select")
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_file_dialog_get_accept_label (LrgFileDialog *self);

/**
 * lrg_file_dialog_set_accept_label:
 * @self: an #LrgFileDialog
 * @accept_label: (nullable): text of the accept button
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_dialog_set_accept_label (LrgFileDialog *self,
                                       const gchar   *accept_label);

/**
 * lrg_file_dialog_get_initial_folder:
 * @self: an #LrgFileDialog
 *
 * Returns: (transfer none) (nullable): the folder the dialog opens in
 */
LRG_AVAILABLE_IN_ALL
GFile * lrg_file_dialog_get_initial_folder (LrgFileDialog *self);

/**
 * lrg_file_dialog_set_initial_folder:
 * @self: an #LrgFileDialog
 * @folder: (nullable): the folder to start in, %NULL for the current
 *   working directory
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_dialog_set_initial_folder (LrgFileDialog *self,
                                         GFile         *folder);

/**
 * lrg_file_dialog_get_initial_name:
 * @self: an #LrgFileDialog
 *
 * Returns: (transfer none) (nullable): the suggested save name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_file_dialog_get_initial_name (LrgFileDialog *self);

/**
 * lrg_file_dialog_set_initial_name:
 * @self: an #LrgFileDialog
 * @name: (nullable): file name suggested by lrg_file_dialog_save()
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_dialog_set_initial_name (LrgFileDialog *self,
                                       const gchar   *name);

/**
 * lrg_file_dialog_get_filters:
 * @self: an #LrgFileDialog
 *
 * Returns: (transfer none) (nullable): the #GListModel of
 *   #LrgFileFilter offered
 */
LRG_AVAILABLE_IN_ALL
GListModel * lrg_file_dialog_get_filters (LrgFileDialog *self);

/**
 * lrg_file_dialog_set_filters:
 * @self: an #LrgFileDialog
 * @filters: (nullable): a #GListModel of #LrgFileFilter, or %NULL for
 *   no filter list
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_dialog_set_filters (LrgFileDialog *self,
                                  GListModel    *filters);

/**
 * lrg_file_dialog_get_default_filter:
 * @self: an #LrgFileDialog
 *
 * Returns: (transfer none) (nullable): the filter selected at first
 */
LRG_AVAILABLE_IN_ALL
LrgFileFilter * lrg_file_dialog_get_default_filter (LrgFileDialog *self);

/**
 * lrg_file_dialog_set_default_filter:
 * @self: an #LrgFileDialog
 * @filter: (nullable): the filter to start with; %NULL picks the first
 *   of #LrgFileDialog:filters
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_dialog_set_default_filter (LrgFileDialog *self,
                                         LrgFileFilter *filter);

/**
 * lrg_file_dialog_get_modal:
 * @self: an #LrgFileDialog
 *
 * Returns: %TRUE if the dialog blocks the rest of the application
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_dialog_get_modal (LrgFileDialog *self);

/**
 * lrg_file_dialog_set_modal:
 * @self: an #LrgFileDialog
 * @modal: ask the portal for a modal dialog (the default). The in-app
 *   dialog is always modal on its canvas.
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_dialog_set_modal (LrgFileDialog *self,
                                gboolean       modal);

/**
 * lrg_file_dialog_get_prefer_native:
 * @self: an #LrgFileDialog
 *
 * Returns: %TRUE if the desktop's own file chooser is tried first
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_dialog_get_prefer_native (LrgFileDialog *self);

/**
 * lrg_file_dialog_set_prefer_native:
 * @self: an #LrgFileDialog
 * @prefer_native: use the XDG desktop portal
 *   (org.freedesktop.portal.FileChooser on the session bus) when it is
 *   available, falling back to the in-app dialog otherwise
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_dialog_set_prefer_native (LrgFileDialog *self,
                                        gboolean       prefer_native);

/**
 * lrg_file_dialog_get_show_system_places:
 * @self: an #LrgFileDialog
 *
 * Returns: %TRUE if the in-app dialog lists home, user folders,
 *   bookmarks and mounts
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_dialog_get_show_system_places (LrgFileDialog *self);

/**
 * lrg_file_dialog_set_show_system_places:
 * @self: an #LrgFileDialog
 * @show_system_places: %FALSE limits the in-app sidebar to the places
 *   added with lrg_file_dialog_add_place()
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_dialog_set_show_system_places (LrgFileDialog *self,
                                             gboolean       show_system_places);

/**
 * lrg_file_dialog_add_place:
 * @self: an #LrgFileDialog
 * @label: sidebar label
 * @location: the folder
 * @icon: the sidebar icon
 *
 * Adds an application place (a save folder, a mod folder) to the
 * sidebar of the in-app dialog. See lrg_file_browser_add_place().
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_dialog_add_place (LrgFileDialog *self,
                                const gchar   *label,
                                GFile         *location,
                                LrgIcon        icon);

/**
 * lrg_file_dialog_open:
 * @self: an #LrgFileDialog
 * @parent: (nullable): any widget inside the #LrgCanvas the in-app
 *   dialog is shown on (the canvas itself works too); may be %NULL only
 *   when the portal is used
 * @cancellable: (nullable): cancels the operation and closes the dialog
 * @callback: (scope async): called when the user is done
 * @user_data: data for @callback
 *
 * Asks the user for one existing file. Finish with
 * lrg_file_dialog_open_finish().
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_dialog_open (LrgFileDialog       *self,
                           LrgWidget           *parent,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data);

/**
 * lrg_file_dialog_open_finish:
 * @self: an #LrgFileDialog
 * @result: the #GAsyncResult
 * @error: return location for a #GError in #LRG_FILE_DIALOG_ERROR
 *
 * Returns: (transfer full) (nullable): the chosen file, or %NULL with
 *   @error set (%LRG_FILE_DIALOG_ERROR_DISMISSED when the user backed
 *   out)
 */
LRG_AVAILABLE_IN_ALL
GFile * lrg_file_dialog_open_finish (LrgFileDialog  *self,
                                     GAsyncResult   *result,
                                     GError        **error);

/**
 * lrg_file_dialog_open_multiple:
 * @self: an #LrgFileDialog
 * @parent: (nullable): a widget inside the canvas to show on
 * @cancellable: (nullable): a #GCancellable
 * @callback: (scope async): called when the user is done
 * @user_data: data for @callback
 *
 * Asks the user for one or more existing files.
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_dialog_open_multiple (LrgFileDialog       *self,
                                    LrgWidget           *parent,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data);

/**
 * lrg_file_dialog_open_multiple_finish:
 * @self: an #LrgFileDialog
 * @result: the #GAsyncResult
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): a #GListModel of #GFile
 */
LRG_AVAILABLE_IN_ALL
GListModel * lrg_file_dialog_open_multiple_finish (LrgFileDialog  *self,
                                                   GAsyncResult   *result,
                                                   GError        **error);

/**
 * lrg_file_dialog_save:
 * @self: an #LrgFileDialog
 * @parent: (nullable): a widget inside the canvas to show on
 * @cancellable: (nullable): a #GCancellable
 * @callback: (scope async): called when the user is done
 * @user_data: data for @callback
 *
 * Asks the user where to save. The in-app dialog adds the current
 * filter's extension to a bare name and asks before replacing an
 * existing file.
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_dialog_save (LrgFileDialog       *self,
                           LrgWidget           *parent,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data);

/**
 * lrg_file_dialog_save_finish:
 * @self: an #LrgFileDialog
 * @result: the #GAsyncResult
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the file to write
 */
LRG_AVAILABLE_IN_ALL
GFile * lrg_file_dialog_save_finish (LrgFileDialog  *self,
                                     GAsyncResult   *result,
                                     GError        **error);

/**
 * lrg_file_dialog_select_folder:
 * @self: an #LrgFileDialog
 * @parent: (nullable): a widget inside the canvas to show on
 * @cancellable: (nullable): a #GCancellable
 * @callback: (scope async): called when the user is done
 * @user_data: data for @callback
 *
 * Asks the user for a folder. Accepting with no folder selected picks
 * the folder being shown.
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_dialog_select_folder (LrgFileDialog       *self,
                                    LrgWidget           *parent,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data);

/**
 * lrg_file_dialog_select_folder_finish:
 * @self: an #LrgFileDialog
 * @result: the #GAsyncResult
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the chosen folder
 */
LRG_AVAILABLE_IN_ALL
GFile * lrg_file_dialog_select_folder_finish (LrgFileDialog  *self,
                                              GAsyncResult   *result,
                                              GError        **error);

G_END_DECLS
