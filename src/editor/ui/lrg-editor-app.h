/* lrg-editor-app.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Standalone engine-drawn editor application.
 *
 * LrgEditorApp is the entry point for the standalone (non-cmacs) editor: it
 * pairs an #LrgEditor runtime with an engine-drawn #LrgCanvas UI assembled from
 * the engine's own widget toolkit.  The left panel is a node outliner; the
 * centre area is a live 3D viewport rendered with an orbit #LrgCamera3D; the
 * right panel shows name and transform of the primary selected node.
 *
 * A host loop pumps the app with lrg_editor_app_handle_input() and
 * lrg_editor_app_render().  For the common standalone use-case the convenience
 * function lrg_editor_app_run() opens a #GrlWindow and loops until the user
 * closes it; it checks for a graphical display and exits cleanly when absent.
 *
 * Only built when libregnum is configured with the editor UI
 * (BUILD_EDITOR_UI=1 / -DLRG_BUILD_EDITOR_UI), which implies the editor module.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_APP (lrg_editor_app_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorApp, lrg_editor_app, LRG, EDITOR_APP, GObject)

/**
 * lrg_editor_app_new:
 * @editor: (nullable) (transfer none): the editor to drive, or %NULL to make one
 *
 * Creates a new #LrgEditorApp.  If @editor is %NULL a fresh #LrgEditor is
 * created internally.
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
 * lrg_editor_app_get_canvas:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the root #LrgCanvas
 */
LRG_AVAILABLE_IN_ALL
LrgCanvas * lrg_editor_app_get_canvas (LrgEditorApp *self);

/**
 * lrg_editor_app_refresh:
 * @self: an #LrgEditorApp
 *
 * Rebuilds the outliner panel and properties panel from the editor's current
 * level tree and selection.  Safe to call from headless environments.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_app_refresh (LrgEditorApp *self);

/**
 * lrg_editor_app_handle_input:
 * @self: an #LrgEditorApp
 *
 * Dispatches pending input to the UI and 3D viewport (call once per frame).
 * Handles orbit camera (right-mouse drag), zoom (scroll wheel), node selection
 * (left-click in viewport), and keyboard shortcuts: N (add cube), Delete (delete
 * selected), Ctrl+S (save), Ctrl+Z (undo), Ctrl+Y (redo).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_app_handle_input (LrgEditorApp *self);

/**
 * lrg_editor_app_render:
 * @self: an #LrgEditorApp
 *
 * Renders the 3D viewport and all UI panels for one frame.  Must be called
 * between grl_window_begin_drawing() / grl_window_end_drawing() (or equivalent
 * begin/end frame calls).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_app_render (LrgEditorApp *self);

/**
 * lrg_editor_app_run:
 * @self: an #LrgEditorApp
 *
 * Opens a #GrlWindow, runs the editor loop (handle_input + render + frame swap)
 * until the window is closed, then destroys the window.
 *
 * If no graphical display is available (DISPLAY and WAYLAND_DISPLAY are both
 * unset) this function prints a message to stderr and returns immediately
 * without crashing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_app_run (LrgEditorApp *self);

G_END_DECLS
