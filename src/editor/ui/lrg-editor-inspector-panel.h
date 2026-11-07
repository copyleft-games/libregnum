/* lrg-editor-inspector-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorInspectorPanel: the properties of the selected nodes, one
 * #LrgPropertyGrid per section (header, transform, visual, each
 * component, scripts) over #LrgEditorNodeSource objects, with
 * multi-edit, an Add Component popup, script tools, a prefab bar for
 * linked instances, the level's own properties when nothing is
 * selected, an inspector lock, and live inspection of the play world.
 * It also registers the `inspector.*` IPC methods.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-property-grid.h"
#include "../lrg-editor-node-source.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_INSPECTOR_PANEL (lrg_editor_inspector_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorInspectorPanel, lrg_editor_inspector_panel, LRG, EDITOR_INSPECTOR_PANEL, LrgContainer)

/**
 * lrg_editor_inspector_panel_new:
 * @app: the editor application it presents
 *
 * Creates the inspector. It follows @app's selection, registers the
 * `inspector.*` IPC methods on @app's server and the
 * "inspector.toggle-lock" and "node.add-component" actions.
 *
 * Returns: (transfer full): a new #LrgEditorInspectorPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorInspectorPanel * lrg_editor_inspector_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_inspector_panel_get_app:
 * @self: an #LrgEditorInspectorPanel
 *
 * Returns: (transfer none) (nullable): the application, %NULL once it is gone
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_inspector_panel_get_app (LrgEditorInspectorPanel *self);

/**
 * lrg_editor_inspector_panel_get_nodes:
 * @self: an #LrgEditorInspectorPanel
 *
 * The nodes the inspector shows: the selection, or the locked nodes
 * while the inspector is locked.
 *
 * Returns: (transfer none) (element-type LrgNode): the inspected nodes
 *   (empty when the level section is shown)
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_inspector_panel_get_nodes (LrgEditorInspectorPanel *self);

/**
 * lrg_editor_inspector_panel_get_grid:
 * @self: an #LrgEditorInspectorPanel
 * @section: a node section
 * @component: (nullable): for %LRG_EDITOR_NODE_SECTION_COMPONENT, the
 *   component key ("Health", "Health#2" for the second one)
 *
 * The property grid of one section, for tests and tools that click its
 * rows.
 *
 * Returns: (transfer none) (nullable): the grid, %NULL for a component
 *   the inspected nodes do not all have
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_inspector_panel_get_grid (LrgEditorInspectorPanel *self,
                                                       LrgEditorNodeSection     section,
                                                       const gchar             *component);

/**
 * lrg_editor_inspector_panel_get_level_grid:
 * @self: an #LrgEditorInspectorPanel
 *
 * Returns: (transfer none): the grid of the level's own properties,
 *   shown when nothing is selected
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_inspector_panel_get_level_grid (LrgEditorInspectorPanel *self);

/**
 * lrg_editor_inspector_panel_get_locked:
 * @self: an #LrgEditorInspectorPanel
 *
 * Returns: %TRUE while the inspector keeps showing its locked nodes
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_inspector_panel_get_locked (LrgEditorInspectorPanel *self);

/**
 * lrg_editor_inspector_panel_set_locked:
 * @self: an #LrgEditorInspectorPanel
 * @locked: %TRUE to keep showing the current nodes
 *
 * Locks the inspector to the nodes it shows now (what the lock button
 * does), or unlocks it so it follows the selection again. The first
 * locked node's GUID is stored as the view state's
 * #LrgEditorViewState:inspector-lock; setting that property from
 * elsewhere (IPC) locks the inspector to that node.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_inspector_panel_set_locked (LrgEditorInspectorPanel *self,
                                            gboolean                 locked);

/**
 * lrg_editor_inspector_panel_get_live_object:
 * @self: an #LrgEditorInspectorPanel
 *
 * Returns: (transfer none) (nullable): the play-world object shown live
 *   (an #LrgGameObject), %NULL outside live inspection
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_editor_inspector_panel_get_live_object (LrgEditorInspectorPanel *self);

/**
 * lrg_editor_inspector_panel_set_live_object:
 * @self: an #LrgEditorInspectorPanel
 * @object: (nullable): an #LrgGameObject of the running play session, or
 *   %NULL to follow the selection
 *
 * Shows @object's components live while play mode runs (what picking
 * an object in the hierarchy's Live tab does). Without an explicit
 * object the inspector shows the live object made from the inspected
 * node. The choice is dropped when play stops.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_inspector_panel_set_live_object (LrgEditorInspectorPanel *self,
                                                 GObject                 *object);

/**
 * lrg_editor_inspector_panel_get_live_grid:
 * @self: an #LrgEditorInspectorPanel
 * @component: a component key of the live object ("test-mover",
 *   "test-mover#2" for the second one)
 *
 * Returns: (transfer none) (nullable): the live grid of that component
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_inspector_panel_get_live_grid (LrgEditorInspectorPanel *self,
                                                            const gchar             *component);

/**
 * lrg_editor_inspector_panel_show_add_component:
 * @self: an #LrgEditorInspectorPanel
 *
 * Opens the Add Component popup (a searchable list of the registered
 * #LrgComponent types) below the Add Component button, as the button
 * and the "node.add-component" action do.
 *
 * Returns: %TRUE if the popup opened (nodes are inspected and the panel
 *   is on a canvas)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_inspector_panel_show_add_component (LrgEditorInspectorPanel *self);

/**
 * lrg_editor_inspector_panel_attach_script:
 * @self: an #LrgEditorInspectorPanel
 * @path: a script file, absolute or relative to the project
 *
 * Attaches @path as a new script binding to every inspected node (the
 * language follows the extension), as one undo step. What dropping a
 * script asset on the Scripts section and the Attach popup do.
 *
 * Returns: %TRUE if nodes were inspected
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_inspector_panel_attach_script (LrgEditorInspectorPanel *self,
                                                   const gchar             *path);

G_END_DECLS
