/* lrg-editor-code-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorCodePanel: the editor's built-in code editor.
 *
 * A dock panel holding an #LrgTabView of #LrgTextEditor tabs, one per open
 * file, coloured with the #LrgSyntax picked from the file name. A tab's
 * title carries a dot while it has unsaved changes; the File > Save
 * shortcut (Ctrl+S by default) saves the focused tab while focus is in
 * the panel, Ctrl+W closes it (asking first when it is dirty) and Ctrl+G
 * goes to a line. A file changed on disk reloads silently when its tab is
 * clean and asks when it is dirty.
 *
 * The panel also registers, on the application it is built for:
 *
 *  - the "code" action group on the canvas: "code.open" with a string
 *    target "PATH" or "PATH:LINE", "code.save", "code.save-all",
 *    "code.close", "code.reload" and "code.goto-line" (integer target);
 *  - registry actions in the "Code" category (code.save, code.save-all,
 *    code.close-tab, code.reload, code.goto-line) for menus, the command
 *    palette and keybindings;
 *  - the IPC methods "code.open" ({path, line?}), "code.list" and
 *    "code.save" ({path?}), plus "preferences.open" ({page?}) and
 *    "palette.open" ({mode?, query?}) for automation;
 *  - a replacement of "node.open-scripts" that follows the
 *    #LrgEditorSettings:script-editor setting ("internal" opens the
 *    scripts here, "external" and "emacs" keep the external editor flow);
 *  - the command palette (#LrgEditorPalette) and the preferences action
 *    (#LrgEditorPreferences), which have no panel of their own.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-text-editor.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_CODE_PANEL (lrg_editor_code_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorCodePanel, lrg_editor_code_panel, LRG, EDITOR_CODE_PANEL, LrgContainer)

/**
 * lrg_editor_code_panel_new:
 * @app: the editor application it presents
 *
 * Creates the panel and registers its actions and IPC methods on @app
 * (see the section description). The editor window makes one.
 *
 * Returns: (transfer full): a new #LrgEditorCodePanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCodePanel * lrg_editor_code_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_code_panel_get_app:
 * @self: an #LrgEditorCodePanel
 *
 * Returns: (transfer none) (nullable): the application, %NULL once it is gone
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_code_panel_get_app (LrgEditorCodePanel *self);

/**
 * lrg_editor_code_panel_open:
 * @self: an #LrgEditorCodePanel
 * @path: a file, absolute or relative to the project
 * @line: the 1-based line to put the caret on, or 0 to keep the caret
 *   (a file that was not open starts on line 1)
 * @error: (nullable): return location for an error
 *
 * Opens @path in a tab (or switches to its tab when it is open already),
 * moves the caret to @line and makes the tab the active one. The file
 * must be UTF-8 text.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_code_panel_open (LrgEditorCodePanel  *self,
                                     const gchar         *path,
                                     gint                 line,
                                     GError             **error);

/**
 * lrg_editor_code_panel_save:
 * @self: an #LrgEditorCodePanel
 * @path: (nullable): an open file, or %NULL for the active tab
 * @error: (nullable): return location for an error
 *
 * Writes the tab's text to its file and clears its dirty mark.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_code_panel_save (LrgEditorCodePanel  *self,
                                     const gchar         *path,
                                     GError             **error);

/**
 * lrg_editor_code_panel_save_all:
 * @self: an #LrgEditorCodePanel
 * @error: (nullable): return location for the first error
 *
 * Saves every dirty tab.
 *
 * Returns: %TRUE when every dirty tab was saved
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_code_panel_save_all (LrgEditorCodePanel  *self,
                                         GError             **error);

/**
 * lrg_editor_code_panel_close:
 * @self: an #LrgEditorCodePanel
 * @path: (nullable): an open file, or %NULL for the active tab
 * @force: close without asking even when the tab has unsaved changes
 *
 * Closes a tab. A dirty tab is not closed unless @force is set: the
 * "Save changes?" dialog (ui.state dialog "confirm") opens instead and
 * its answer decides.
 *
 * Returns: %TRUE if the tab closed now
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_code_panel_close (LrgEditorCodePanel *self,
                                      const gchar        *path,
                                      gboolean            force);

/**
 * lrg_editor_code_panel_goto_line:
 * @self: an #LrgEditorCodePanel
 * @line: a 1-based line
 *
 * Moves the active tab's caret to the start of @line and scrolls to it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_code_panel_goto_line (LrgEditorCodePanel *self,
                                      gint                line);

/**
 * lrg_editor_code_panel_check_disk:
 * @self: an #LrgEditorCodePanel
 *
 * Compares every open file with its tab: a file changed on disk is
 * reloaded when its tab is clean, and the "changed on disk" dialog
 * (ui.state dialog "code-reload") asks when it is dirty. File monitors
 * call this per file; it is public so the check can also be forced.
 *
 * Returns: the number of tabs that were reloaded
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_code_panel_check_disk (LrgEditorCodePanel *self);

/**
 * lrg_editor_code_panel_get_n_files:
 * @self: an #LrgEditorCodePanel
 *
 * Returns: the number of open tabs
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_code_panel_get_n_files (LrgEditorCodePanel *self);

/**
 * lrg_editor_code_panel_get_path:
 * @self: an #LrgEditorCodePanel
 * @index: a tab index
 *
 * Returns: (transfer none) (nullable): the absolute path of the file in
 *   tab @index, or %NULL when out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_code_panel_get_path (LrgEditorCodePanel *self,
                                              guint               index);

/**
 * lrg_editor_code_panel_get_active_path:
 * @self: an #LrgEditorCodePanel
 *
 * Returns: (transfer none) (nullable): the active tab's file, %NULL when no
 *   file is open
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_code_panel_get_active_path (LrgEditorCodePanel *self);

/**
 * lrg_editor_code_panel_get_editor:
 * @self: an #LrgEditorCodePanel
 * @path: (nullable): an open file, or %NULL for the active tab
 *
 * Returns: (transfer none) (nullable): the text editor of that tab
 */
LRG_AVAILABLE_IN_ALL
LrgTextEditor * lrg_editor_code_panel_get_editor (LrgEditorCodePanel *self,
                                                  const gchar        *path);

/**
 * lrg_editor_code_panel_is_dirty:
 * @self: an #LrgEditorCodePanel
 * @path: (nullable): an open file, or %NULL for "any tab"
 *
 * Returns: %TRUE if that tab (or, for %NULL, any tab) has unsaved changes
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_code_panel_is_dirty (LrgEditorCodePanel *self,
                                         const gchar        *path);

/**
 * lrg_editor_code_panel_open_script:
 * @app: an #LrgEditorApp
 * @path: a script or text file, absolute or relative to the project
 * @line: a 1-based line, or 0
 *
 * Opens @path where #LrgEditorSettings:script-editor says: "internal"
 * opens it in the app's code panel (shown and focused), "external" and
 * "emacs" hand it to the external editor (#LrgExternalEditor, which uses
 * a connected Emacs when Emacs integration is on). Errors are reported
 * as notifications.
 *
 * Returns: %TRUE if it opened in the code panel
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_code_panel_open_script (LrgEditorApp *app,
                                            const gchar  *path,
                                            gint          line);

G_END_DECLS
