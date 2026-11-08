/* lrg-editor-app.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The libregnum editor application.
 *
 * LrgEditorApp is the full editor built from libregnum itself: an
 * #LrgEditor document runtime presented through the retained UI toolkit
 * (an #LrgCanvas holding an #LrgEditorWindow: menu bar, toolbar, an
 * #LrgDockArea of panels, status bar and toasts) with a 3D/2D viewport,
 * hierarchy, inspector, asset browser, console and the other panels,
 * play-in-editor, autosave and crash recovery, live reload of files
 * edited elsewhere, an IPC server for Emacs and other tools, and
 * $EDITOR / Emacs integration for code.
 *
 * The editor's state that tools and tests care about lives in headless
 * objects: #LrgEditorViewState (cameras, layout, dialog, timeline
 * playhead...), #LrgEditorViewportNav (viewport navigation) and
 * #LrgEditorPlaySession (play-in-editor).
 *
 * Typical use is the `lrgeditor` program (lrg_editor_app_run()). The
 * app can also be driven frame by frame without a window
 * (lrg_editor_app_process_frame() with a synthetic #LrgCanvasInput),
 * which is how it is tested, and run as a window-less IPC server
 * (lrg_editor_app_run_headless()).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../gui/lrg-gui-context.h"
#include "../../ui/lrg-canvas.h"
#include "../../ui/lrg-canvas-input.h"
#include "../../ui/lrg-dock-area.h"
#include "../lrg-editor-view-state.h"
#include "../lrg-editor-viewport-nav.h"
#include "../lrg-editor-play-session.h"
#include "lrg-editor-scene-renderer.h"
#include "../lrg-asset-source.h"
#include "../../gamemodule/lrg-loaded-game.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_APP (lrg_editor_app_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorApp, lrg_editor_app, LRG, EDITOR_APP, GObject)

/* Declared in lrg-editor-window.h, which includes this header. */
typedef struct _LrgEditorWindow LrgEditorWindow;

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
 * The immediate-mode GUI context of the 0.8.0 editor. The editor is drawn
 * with the retained UI toolkit since 0.8.1 and has no such context.
 *
 * Returns: (transfer none) (nullable): always %NULL
 *
 * Deprecated: 0.8.1: Use lrg_editor_app_get_canvas() instead.
 */
LRG_DEPRECATED_IN_0_8_1_FOR (lrg_editor_app_get_canvas)
LrgGuiContext * lrg_editor_app_get_gui (LrgEditorApp *self);

/**
 * lrg_editor_app_get_canvas:
 * @self: an #LrgEditorApp
 *
 * The canvas the editor window lives on. It exists from construction,
 * also without a window, so tests can drive it with
 * lrg_editor_app_process_frame() and inspect its widgets.
 *
 * Returns: (transfer none): the canvas
 */
LRG_AVAILABLE_IN_ALL
LrgCanvas * lrg_editor_app_get_canvas (LrgEditorApp *self);

/**
 * lrg_editor_app_get_window:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the editor window widget (menu bar, toolbar,
 *   dock, status bar)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorWindow * lrg_editor_app_get_window (LrgEditorApp *self);

/**
 * lrg_editor_app_get_dock:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the dock area holding the panels
 */
LRG_AVAILABLE_IN_ALL
LrgDockArea * lrg_editor_app_get_dock (LrgEditorApp *self);

/**
 * lrg_editor_app_get_view_state:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the headless view model (cameras, layout,
 *   render modes, dialog, timeline playhead, tile tool...)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewState * lrg_editor_app_get_view_state (LrgEditorApp *self);

/**
 * lrg_editor_app_get_viewport_nav:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the viewport navigation state machine
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewportNav * lrg_editor_app_get_viewport_nav (LrgEditorApp *self);

/**
 * lrg_editor_app_get_play_session:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the play-in-editor session
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPlaySession * lrg_editor_app_get_play_session (LrgEditorApp *self);

/**
 * lrg_editor_app_get_scene_renderer:
 * @self: an #LrgEditorApp
 *
 * The renderer every viewport draws through (authoring nodes, the play
 * world, baked lighting while authoring, thumbnails).
 *
 * Returns: (transfer none): the scene renderer
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSceneRenderer * lrg_editor_app_get_scene_renderer (LrgEditorApp *self);

/**
 * lrg_editor_app_add_panel:
 * @self: an #LrgEditorApp
 * @panel: (transfer none): a panel with a unique id (lrg_dock_panel_get_id())
 * @where: (nullable): "left", "right", "bottom", "top", "center", or
 *   "tab:<panel-id>" to add it as a tab next to another panel; %NULL is
 *   "center"
 *
 * Docks a custom panel in the editor window and lists it in
 * View > Panels, so it can be closed and reopened like the built-in
 * ones. A saved layout that mentions its id places it on the next
 * lrg_editor_app_load_settings().
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_app_add_panel (LrgEditorApp *self,
                               LrgDockPanel *panel,
                               const gchar  *where);

/* ==========================================================================
 * Game modules and asset sources (since 0.8.3)
 * ========================================================================== */

/**
 * LrgGameModuleEditorInit:
 * @app: the editor application
 * @error: (nullable): return location for an error
 *
 * The type of a game module's optional editor entry point
 * (#LRG_GAME_MODULE_EDITOR_SYMBOL, emitted by
 * LRG_DEFINE_GAME_MODULE_EDITOR()). It runs once, after the application
 * is created and its preferences are loaded, and typically registers
 * asset sources (lrg_editor_app_add_asset_source()), panels
 * (lrg_editor_app_add_panel()) and actions.
 *
 * Returns: %TRUE on success; %FALSE with @error set makes lrgeditor exit
 *
 * Since: 0.8.3
 */
typedef gboolean (*LrgGameModuleEditorInit) (LrgEditorApp  *app,
                                             GError       **error);

/**
 * lrg_editor_app_init_module:
 * @self: an #LrgEditorApp
 * @game: a loaded game module
 * @error: (nullable): return location for an error
 *
 * Runs the module's editor entry point (#LRG_GAME_MODULE_EDITOR_SYMBOL)
 * with @self. A module without one needs nothing from the editor, which
 * is success. `lrgeditor --module SO` calls this right after creating
 * the application and loading its preferences.
 *
 * Returns: %TRUE when the module has no editor hook or it succeeded
 *
 * Since: 0.8.3
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_app_init_module (LrgEditorApp   *self,
                                     LrgLoadedGame  *game,
                                     GError        **error);

/**
 * lrg_editor_app_add_asset_source:
 * @self: an #LrgEditorApp
 * @source: (transfer none): an asset source
 *
 * Registers @source (the app keeps a reference): the Sources panel lists
 * it and the "source.*" IPC methods reach it by its id. A source with
 * the same id replaces the earlier one.
 *
 * Since: 0.8.3
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_app_add_asset_source (LrgEditorApp   *self,
                                      LrgAssetSource *source);

/**
 * lrg_editor_app_remove_asset_source:
 * @self: an #LrgEditorApp
 * @id: a source id
 *
 * Unregisters a source. Imported files stay in the project.
 *
 * Returns: %TRUE if a source with @id was registered
 *
 * Since: 0.8.3
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_app_remove_asset_source (LrgEditorApp *self,
                                             const gchar  *id);

/**
 * lrg_editor_app_get_asset_sources:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer container) (element-type LrgAssetSource): the
 *   registered sources, in registration order
 *
 * Since: 0.8.3
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_app_get_asset_sources (LrgEditorApp *self);

/**
 * lrg_editor_app_lookup_asset_source:
 * @self: an #LrgEditorApp
 * @id: a source id
 *
 * Returns: (transfer none) (nullable): the source registered as @id
 *
 * Since: 0.8.3
 */
LRG_AVAILABLE_IN_ALL
LrgAssetSource * lrg_editor_app_lookup_asset_source (LrgEditorApp *self,
                                                     const gchar  *id);

/**
 * lrg_editor_app_export_source_entry:
 * @self: an #LrgEditorApp
 * @source_id: a registered source id
 * @path: the virtual path of an entry
 * @dest_dir: (nullable): a folder to copy the export into (created if
 *   needed), or %NULL to only export into the cache
 * @error: (nullable): return location for an error
 *
 * Exports an entry through the source's cache
 * (lrg_asset_source_export_cached(): the first call converts, later
 * calls reuse the files). With @dest_dir the exported files (main and
 * side files) are then copied there, overwriting files of the same
 * name. The first export of an entry may take seconds and blocks the
 * caller.
 *
 * Returns: (transfer full) (nullable): the absolute path of the main
 *   file (in the cache, or in @dest_dir), or %NULL on error
 *
 * Since: 0.8.3
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_app_export_source_entry (LrgEditorApp  *self,
                                            const gchar   *source_id,
                                            const gchar   *path,
                                            const gchar   *dest_dir,
                                            GError       **error);

/**
 * lrg_editor_app_import_source_entry:
 * @self: an #LrgEditorApp
 * @source_id: a registered source id
 * @path: the virtual path of an entry
 * @error: (nullable): return location for an error
 *
 * Imports an entry into the open project: exports it (cached) and copies
 * the files to `<project>/assets/<source-id>/<virtual folder>/`, so the
 * asset browser lists them and levels reference them like any project
 * asset. Importing again overwrites the copy.
 *
 * Returns: (transfer full) (nullable): the main file's path relative to
 *   the project, or %NULL on error (%G_IO_ERROR_NOT_INITIALIZED when no
 *   project is open)
 *
 * Since: 0.8.3
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_app_import_source_entry (LrgEditorApp  *self,
                                            const gchar   *source_id,
                                            const gchar   *path,
                                            GError       **error);

/**
 * lrg_editor_app_place_source_entry:
 * @self: an #LrgEditorApp
 * @source_id: a registered source id
 * @path: the virtual path of an entry
 * @error: (nullable): return location for an error
 *
 * Imports the entry (lrg_editor_app_import_source_entry()) and places it
 * in the level the way the Assets panel's "Place in Scene" does: a model
 * becomes a mesh-asset node, a texture a sprite, a prefab a linked
 * instance, at the camera's target, as one undo step, selected.
 *
 * Returns: (transfer none) (nullable): the new node, or %NULL on error
 *
 * Since: 0.8.3
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_app_place_source_entry (LrgEditorApp  *self,
                                             const gchar   *source_id,
                                             const gchar   *path,
                                             GError       **error);

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
 * lrg_editor_app_process_frame:
 * @self: an #LrgEditorApp
 * @input: this frame's input, in canvas coordinates
 * @width: window width in pixels
 * @height: window height in pixels
 *
 * Runs one frame without drawing anything (no window needed): sizes the
 * canvas and window, dispatches @input to the widgets (shortcuts, menus,
 * the viewport), then ticks navigation, play mode, timeline playback,
 * thumbnails, autosave and validation, and advances the canvas clock by
 * @input->delta_time. lrg_editor_app_run() calls this and then renders
 * the canvas.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_app_process_frame (LrgEditorApp         *self,
                                   const LrgCanvasInput *input,
                                   gint                  width,
                                   gint                  height);

/**
 * lrg_editor_app_frame:
 * @self: an #LrgEditorApp
 * @input: this frame's input, in the 0.8.0 immediate-mode format
 * @width: window width in pixels
 * @height: window height in pixels
 *
 * Translates @input into an #LrgCanvasInput (button presses and releases
 * and key releases by comparing with the previous call) and runs
 * lrg_editor_app_process_frame().
 *
 * Deprecated: 0.8.1: Use lrg_editor_app_process_frame() instead.
 */
LRG_DEPRECATED_IN_0_8_1_FOR (lrg_editor_app_process_frame)
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
