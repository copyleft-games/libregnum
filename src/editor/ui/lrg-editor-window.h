/* lrg-editor-window.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorWindow: the editor's main window widget.
 *
 * A container that stacks, top to bottom, an #LrgMenuBar built from the
 * editor's action registry, a toolbar (transform tools, snapping with
 * editable increments, local/world space, 2D/3D, play controls and the
 * viewport layout), an #LrgDockArea holding the panels, and a status bar
 * (document and dirty marker, play state, the "changed on disk" prompt,
 * the last status message and the counters). An #LrgToastQueue shows
 * notifications as a canvas overlay.
 *
 * The window inserts the editor's actions as the "editor" action group
 * (menu items refer to "editor.file.save") and registers every action's
 * accelerators as canvas shortcuts.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-menu-bar.h"
#include "../../ui/lrg-dock-area.h"
#include "../../ui/lrg-toast-queue.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_WINDOW (lrg_editor_window_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorWindow, lrg_editor_window, LRG, EDITOR_WINDOW, LrgContainer)

/**
 * lrg_editor_window_new:
 * @app: the application the window presents
 *
 * Creates the window: menus from @app's actions, the toolbar, an empty
 * dock area and the status bar. The application docks the panels.
 *
 * Returns: (transfer full): a new #LrgEditorWindow
 */
LRG_AVAILABLE_IN_ALL
LrgEditorWindow * lrg_editor_window_new (LrgEditorApp *app);

/**
 * lrg_editor_window_get_app:
 * @self: an #LrgEditorWindow
 *
 * Returns: (transfer none): the application
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_window_get_app (LrgEditorWindow *self);

/**
 * lrg_editor_window_get_menu_bar:
 * @self: an #LrgEditorWindow
 *
 * Returns: (transfer none): the menu bar
 */
LRG_AVAILABLE_IN_ALL
LrgMenuBar * lrg_editor_window_get_menu_bar (LrgEditorWindow *self);

/**
 * lrg_editor_window_get_menu_model:
 * @self: an #LrgEditorWindow
 *
 * The menu bar's model: one submenu per top-level menu (File, Edit,
 * Create, Node, View, Play, Tools, Help) whose items name "editor.<id>"
 * actions, plus "win.*" actions for recent files, node creation and
 * panel toggles.
 *
 * Returns: (transfer none): the menu model
 */
LRG_AVAILABLE_IN_ALL
GMenuModel * lrg_editor_window_get_menu_model (LrgEditorWindow *self);

/**
 * lrg_editor_window_get_dock:
 * @self: an #LrgEditorWindow
 *
 * Returns: (transfer none): the dock area
 */
LRG_AVAILABLE_IN_ALL
LrgDockArea * lrg_editor_window_get_dock (LrgEditorWindow *self);

/**
 * lrg_editor_window_get_toasts:
 * @self: an #LrgEditorWindow
 *
 * Returns: (transfer none): the toast queue (a canvas overlay once the
 *   window is on a canvas)
 */
LRG_AVAILABLE_IN_ALL
LrgToastQueue * lrg_editor_window_get_toasts (LrgEditorWindow *self);

/**
 * lrg_editor_window_get_widget:
 * @self: an #LrgEditorWindow
 * @name: a chrome element: "toolbar", "statusbar", "status-reload",
 *   "status-keep", "status-counters", or "tool:<action-id>" for a toolbar
 *   button such as "tool:tool.move" or "tool:play.start"
 *
 * Looks up one of the window's own widgets, for tests and tools that
 * click them by their rectangle.
 *
 * Returns: (transfer none) (nullable): the widget, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_editor_window_get_widget (LrgEditorWindow *self,
                                          const gchar     *name);

G_END_DECLS
