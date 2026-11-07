/* lrg-editor-hierarchy-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorHierarchyPanel: the node tree of the open level.
 *
 * The panel shows the level's #LrgNode tree in an #LrgTreeView over an
 * #LrgEditorNodeModel. Each row has a kind glyph (light, camera, mesh,
 * sprite, prefab instance...), the name (prefab instances in the accent
 * colour with a link marker, overridden instances with a dot) and
 * visibility / lock toggles. The tree's selection mirrors the editor's
 * #LrgEditorSelection both ways (Ctrl toggles, Shift selects a range,
 * an outside change reveals and scrolls to the primary node). Rows are
 * renamed inline (F2, double click, the node.rename action, the
 * hierarchy.rename IPC method), dragged to reparent or reorder (Alt keeps
 * the world transform), accept assets dropped from the asset browser and
 * have a context menu. A search field (Ctrl+F, the edit.find action)
 * filters by name or GUID. While play-in-editor runs, a "Live" tab lists
 * the game objects of the play world; selecting one emits
 * #LrgEditorHierarchyPanel::live-selected.
 *
 * The panel registers the hierarchy.reveal, hierarchy.rename,
 * hierarchy.filter and hierarchy.state IPC methods and replaces the
 * node.rename and edit.find placeholder actions when it is constructed.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-tree-view.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-menu.h"
#include "../lrg-editor-node-model.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_HIERARCHY_PANEL (lrg_editor_hierarchy_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorHierarchyPanel, lrg_editor_hierarchy_panel, LRG, EDITOR_HIERARCHY_PANEL, LrgContainer)

/**
 * lrg_editor_hierarchy_panel_new:
 * @app: the editor application it presents
 *
 * Creates the panel and registers its actions (node.rename, edit.find)
 * and IPC methods (hierarchy.*) on @app.
 *
 * Returns: (transfer full): a new #LrgEditorHierarchyPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorHierarchyPanel * lrg_editor_hierarchy_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_hierarchy_panel_get_app:
 * @self: an #LrgEditorHierarchyPanel
 *
 * Returns: (transfer none) (nullable): the application, %NULL once it is gone
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_hierarchy_panel_get_app (LrgEditorHierarchyPanel *self);

/**
 * lrg_editor_hierarchy_panel_get_tree_view:
 * @self: an #LrgEditorHierarchyPanel
 *
 * Returns: (transfer none): the tree view of the level's nodes (widget
 *   name "hierarchy-tree")
 */
LRG_AVAILABLE_IN_ALL
LrgTreeView * lrg_editor_hierarchy_panel_get_tree_view (LrgEditorHierarchyPanel *self);

/**
 * lrg_editor_hierarchy_panel_get_live_view:
 * @self: an #LrgEditorHierarchyPanel
 *
 * Returns: (transfer none): the list of live game objects shown on the
 *   Live tab (widget name "hierarchy-live")
 */
LRG_AVAILABLE_IN_ALL
LrgTreeView * lrg_editor_hierarchy_panel_get_live_view (LrgEditorHierarchyPanel *self);

/**
 * lrg_editor_hierarchy_panel_get_node_model:
 * @self: an #LrgEditorHierarchyPanel
 *
 * Returns: (transfer none): the node model behind the tree
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNodeModel * lrg_editor_hierarchy_panel_get_node_model (LrgEditorHierarchyPanel *self);

/**
 * lrg_editor_hierarchy_panel_get_search_input:
 * @self: an #LrgEditorHierarchyPanel
 *
 * Returns: (transfer none): the filter field (widget name
 *   "hierarchy-search")
 */
LRG_AVAILABLE_IN_ALL
LrgTextInput * lrg_editor_hierarchy_panel_get_search_input (LrgEditorHierarchyPanel *self);

/**
 * lrg_editor_hierarchy_panel_set_filter:
 * @self: an #LrgEditorHierarchyPanel
 * @filter: (nullable): name or GUID substring, %NULL or "" to clear
 *
 * Filters the tree (and fills the search field). Matches and their
 * ancestors are shown, expanded.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_hierarchy_panel_set_filter (LrgEditorHierarchyPanel *self,
                                            const gchar             *filter);

/**
 * lrg_editor_hierarchy_panel_focus_search:
 * @self: an #LrgEditorHierarchyPanel
 *
 * Gives the search field the keyboard focus with its text selected, the
 * edit.find action (Ctrl+F). Switches to the Scene tab.
 *
 * Returns: %TRUE if the field is on a canvas and took the focus
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_hierarchy_panel_focus_search (LrgEditorHierarchyPanel *self);

/**
 * lrg_editor_hierarchy_panel_reveal:
 * @self: an #LrgEditorHierarchyPanel
 * @node: a node of the level
 * @select: also make @node the selection
 *
 * Expands the ancestors of @node and scrolls its row into view (the
 * hierarchy.reveal IPC method). A node hidden by the filter clears the
 * filter first.
 *
 * Returns: %TRUE if @node has a row now
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_hierarchy_panel_reveal (LrgEditorHierarchyPanel *self,
                                            LrgNode                 *node,
                                            gboolean                 select);

/**
 * lrg_editor_hierarchy_panel_start_rename:
 * @self: an #LrgEditorHierarchyPanel
 * @node: a node of the level
 *
 * Reveals @node and opens the inline rename field over its row; Enter
 * renames through lrg_editor_rename_node() (undoable), Escape cancels.
 * When the panel is not laid out yet (just shown) the edit starts on the
 * next frame.
 *
 * Returns: %TRUE if editing started or is pending
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_hierarchy_panel_start_rename (LrgEditorHierarchyPanel *self,
                                                  LrgNode                 *node);

/**
 * lrg_editor_hierarchy_panel_get_context_menu:
 * @self: an #LrgEditorHierarchyPanel
 *
 * The context menu widget (open after a right click on a row or on the
 * empty area, or after lrg_editor_hierarchy_panel_popup_context_menu()).
 *
 * Returns: (transfer none) (nullable): the menu, %NULL before the first
 *   popup
 */
LRG_AVAILABLE_IN_ALL
LrgMenu * lrg_editor_hierarchy_panel_get_context_menu (LrgEditorHierarchyPanel *self);

/**
 * lrg_editor_hierarchy_panel_popup_context_menu:
 * @self: an #LrgEditorHierarchyPanel
 * @node: (nullable): the node the menu acts on, %NULL for the empty-area
 *   menu (create, paste, select all)
 * @x: canvas x of the menu's corner
 * @y: canvas y of the menu's corner
 *
 * Opens the context menu as a right click does: a node that is not
 * selected becomes the selection first.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_hierarchy_panel_popup_context_menu (LrgEditorHierarchyPanel *self,
                                                    LrgNode                 *node,
                                                    gfloat                   x,
                                                    gfloat                   y);

/**
 * lrg_editor_hierarchy_panel_get_page:
 * @self: an #LrgEditorHierarchyPanel
 *
 * Returns: (transfer none): "scene" or "live"
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_hierarchy_panel_get_page (LrgEditorHierarchyPanel *self);

/**
 * lrg_editor_hierarchy_panel_set_page:
 * @self: an #LrgEditorHierarchyPanel
 * @page: "scene" or "live" ("live" only while playing)
 *
 * Switches between the level tree and the live game objects.
 *
 * Returns: %TRUE if @page is shown now
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_hierarchy_panel_set_page (LrgEditorHierarchyPanel *self,
                                              const gchar             *page);

/**
 * lrg_editor_hierarchy_panel_get_live_selection:
 * @self: an #LrgEditorHierarchyPanel
 *
 * The game object selected on the Live tab. It is cleared (and
 * #LrgEditorHierarchyPanel::live-selected emitted with %NULL) when play
 * stops.
 *
 * Returns: (transfer none) (nullable): the selected #LrgGameObject
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_editor_hierarchy_panel_get_live_selection (LrgEditorHierarchyPanel *self);

/**
 * lrg_editor_hierarchy_panel_find:
 * @app: an #LrgEditorApp
 *
 * The hierarchy panel of @app, wherever it is docked (or closed).
 *
 * Returns: (transfer none) (nullable): the panel, %NULL if @app has none
 */
LRG_AVAILABLE_IN_ALL
LrgEditorHierarchyPanel * lrg_editor_hierarchy_panel_find (LrgEditorApp *app);

G_END_DECLS
