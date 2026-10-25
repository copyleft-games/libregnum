/* lrg-editor-app.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The libregnum editor application.
 *
 * LrgEditorApp is the full editor built from libregnum itself: an
 * #LrgEditor document runtime presented through the engine's
 * immediate-mode GUI (#LrgGuiContext) in a dockable layout with a 3D/2D
 * viewport, hierarchy, inspector, asset browser, console, undo history
 * and problems panels, menus, a toolbar, a command palette, quick open,
 * preferences and project settings, play-in-editor, autosave and crash
 * recovery, live reload of files edited elsewhere, an IPC server for
 * Emacs and other tools, and $EDITOR / Emacs integration for code.
 *
 * Typical use is the `lrg-editor` program (lrg_editor_app_run()). The
 * app can also be driven frame by frame without a window
 * (lrg_editor_app_frame()), which is how it is tested, and run as a
 * window-less IPC server (lrg_editor_app_run_headless()).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../gui/lrg-gui-context.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_APP (lrg_editor_app_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorApp, lrg_editor_app, LRG, EDITOR_APP, GObject)

/**
 * lrg_editor_app_new:
 * @editor: (nullable) (transfer none): the editor to drive, or %NULL to make
 *   one
 *
 * Creates the application with default settings (nothing is read from
 * disk until lrg_editor_app_load_settings()).
 *
 * Returns: (transfer full): a new #LrgEditorApp
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_app_new (LrgEditor *editor);

/**
 * lrg_editor_app_get_editor:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the editor runtime
 */
LRG_AVAILABLE_IN_ALL
LrgEditor * lrg_editor_app_get_editor (LrgEditorApp *self);

/**
 * lrg_editor_app_get_settings:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the user preferences
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSettings * lrg_editor_app_get_settings (LrgEditorApp *self);

/**
 * lrg_editor_app_get_actions:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the action registry (menus, shortcuts, palette)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorActions * lrg_editor_app_get_actions (LrgEditorApp *self);

/**
 * lrg_editor_app_get_server:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the IPC server (not listening until
 *   lrg_editor_app_start_server())
 */
LRG_AVAILABLE_IN_ALL
LrgEditorServer * lrg_editor_app_get_server (LrgEditorApp *self);

/**
 * lrg_editor_app_get_external_editor:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the code-editor launcher
 */
LRG_AVAILABLE_IN_ALL
LrgExternalEditor * lrg_editor_app_get_external_editor (LrgEditorApp *self);

/**
 * lrg_editor_app_get_gui:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the GUI context
 */
LRG_AVAILABLE_IN_ALL
LrgGuiContext * lrg_editor_app_get_gui (LrgEditorApp *self);

/**
 * lrg_editor_app_get_project_dir:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none) (nullable): the open project's root
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_app_get_project_dir (LrgEditorApp *self);

/**
 * lrg_editor_app_load_settings:
 * @self: an #LrgEditorApp
 * @path: (nullable): settings file, or %NULL for the default location
 * @error: (nullable): return location for an error
 *
 * Loads preferences and applies them (theme, scale, snapping, history
 * limit, layout).
 *
 * Returns: %TRUE on success (a missing file is not an error)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_app_load_settings (LrgEditorApp  *self,
                                       const gchar   *path,
                                       GError       **error);

/**
 * lrg_editor_app_save_settings:
 * @self: an #LrgEditorApp
 * @error: (nullable): return location for an error
 *
 * Stores the preferences, including the current panel layout.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_app_save_settings (LrgEditorApp  *self,
                                       GError       **error);

/**
 * lrg_editor_app_open_project:
 * @self: an #LrgEditorApp
 * @dir: a project directory (containing project.ryaml), or any directory
 *   to treat as a project root
 * @error: (nullable): return location for an error
 *
 * Opens a project: the asset browser roots there, relative asset and
 * script paths resolve against it, and its default level opens if no
 * level is open yet.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_app_open_project (LrgEditorApp  *self,
                                      const gchar   *dir,
                                      GError       **error);

/**
 * lrg_editor_app_new_project:
 * @self: an #LrgEditorApp
 * @dir: the directory to create the project in (created if needed)
 * @name: the project name
 * @error: (nullable): return location for an error
 *
 * Scaffolds a project (manifest, levels/, scripts/, assets/, prefabs/ and
 * a starter level) and opens it.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_app_new_project (LrgEditorApp  *self,
                                     const gchar   *dir,
                                     const gchar   *name,
                                     GError       **error);

/**
 * lrg_editor_app_open_level:
 * @self: an #LrgEditorApp
 * @path: a `.rlevel` (or `.rprefab`) file
 * @error: (nullable): return location for an error
 *
 * Opens a level, discarding unsaved changes (the UI asks first; this
 * function does not). Offers recovery when a newer autosave exists.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_app_open_level (LrgEditorApp  *self,
                                    const gchar   *path,
                                    GError       **error);

/**
 * lrg_editor_app_start_server:
 * @self: an #LrgEditorApp
 * @socket_path: (nullable): socket path, or %NULL for the settings/default
 * @error: (nullable): return location for an error
 *
 * Starts the IPC server with the application's extra methods (actions,
 * view, play, assets, validation) registered.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_app_start_server (LrgEditorApp  *self,
                                      const gchar   *socket_path,
                                      GError       **error);

/**
 * lrg_editor_app_frame:
 * @self: an #LrgEditorApp
 * @input: this frame's input
 * @width: window width in pixels
 * @height: window height in pixels
 *
 * Builds one frame of UI and processes its input and timers, without
 * drawing anything (no window needed). lrg_editor_app_run() calls this and
 * then renders.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_app_frame (LrgEditorApp      *self,
                           const LrgGuiInput *input,
                           gfloat             width,
                           gfloat             height);

/**
 * lrg_editor_app_activate_action:
 * @self: an #LrgEditorApp
 * @id: an action id (see lrg_editor_actions_list())
 *
 * Returns: %TRUE if the action ran
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_app_activate_action (LrgEditorApp *self,
                                         const gchar  *id);

/**
 * lrg_editor_app_get_status:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none) (nullable): the last status-bar message
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_app_get_status (LrgEditorApp *self);

/**
 * lrg_editor_app_is_playing:
 * @self: an #LrgEditorApp
 *
 * Returns: %TRUE while play-in-editor runs
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_app_is_playing (LrgEditorApp *self);

/**
 * lrg_editor_app_request_quit:
 * @self: an #LrgEditorApp
 *
 * Asks to quit (the UI confirms unsaved changes).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_app_request_quit (LrgEditorApp *self);

/**
 * lrg_editor_app_should_quit:
 * @self: an #LrgEditorApp
 *
 * Returns: %TRUE once quitting was confirmed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_app_should_quit (LrgEditorApp *self);

/**
 * lrg_editor_app_set_window_size:
 * @self: an #LrgEditorApp
 * @width: width in pixels
 * @height: height in pixels
 *
 * Sets the size of the window lrg_editor_app_run() opens (the window can
 * be resized afterwards).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_app_set_window_size (LrgEditorApp *self,
                                     gint          width,
                                     gint          height);

/**
 * lrg_editor_app_run:
 * @self: an #LrgEditorApp
 *
 * Opens the editor window and runs until the user quits. Exits cleanly
 * (returning 1) when no graphical display is available.
 *
 * Returns: a process exit status
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_app_run (LrgEditorApp *self);

/**
 * lrg_editor_app_run_headless:
 * @self: an #LrgEditorApp
 *
 * Runs without a window: only the IPC server, autosave and file monitoring
 * are active, so the editor can be driven entirely from Emacs or scripts.
 * Returns when an IPC client calls "editor.quit" or on SIGINT/SIGTERM.
 *
 * Returns: a process exit status
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_app_run_headless (LrgEditorApp *self);

G_END_DECLS
