/* lrg-editor-bt-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorBtEditor: the data editor of `.rbt` behaviour trees.
 *
 * The canvas (an #LrgNodeGraph) shows the tree left to right: one card per
 * node (composites, decorators, leaves and subtree references, coloured by
 * kind, with a one-line summary of their properties), one wire from every
 * parent's "out" port to each child's "in" port, children ordered top to
 * bottom as in the file. Dragging a wire onto a card re-parents it,
 * dropping a wire on empty canvas adds an action leaf there, Delete removes
 * a node with its subtree, Ctrl+D duplicates one (fresh ids), double click
 * on a subtree card opens the subtree (a file subtree in its own tab, an
 * inline one in this canvas; the chip bar above the canvas switches back).
 *
 * The left column is the palette: the built-in node types grouped by kind,
 * then every action and condition registered with lrg_bt_register_action()
 * / lrg_bt_register_condition() in this process (the binding palette). A
 * click adds the type under the selected node (a composite gets a new last
 * child, an empty decorator its child, anything else a sibling after it).
 * The right column inspects the selection (id, name, type, the type's
 * properties and the binding parameters through a property grid) above the
 * blackboard schema table and the subtree list.
 *
 * The document is the truth: every gesture is one undoable command of the
 * `.rbt` #LrgEditorDocument. Card positions live in the file's top-level
 * `editor: {positions: {ID: [x, y]}}` block, which the engine keeps on
 * save; nodes without a position are placed by the automatic layout. The
 * first structural edit gives every node without an `id:` an explicit one
 * (its generated id), so ids stay stable while the tree changes.
 *
 * After every change the text is parsed into an #LrgBtAsset (validation:
 * structure, properties, subtree references, undeclared blackboard keys;
 * bindings this process does not have are listed as warnings). While the
 * level plays in the editor, the tree of the focused entity's
 * #LrgAiComponent (see lrg_editor_ai_debug_panel_find_for_file()) is shown
 * live: every card is tinted by its node's last status (running, success,
 * failure; not reached this tick: plain), the running branch's wires light
 * up and the cards show their tick counts.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../ai/lrg-bt-asset.h"
#include "../../../ui/lrg-node-graph.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_BT_EDITOR (lrg_editor_bt_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorBtEditor, lrg_editor_bt_editor, LRG, EDITOR_BT_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_bt_editor_new:
 * @app: (nullable): the editor application
 * @document: a behaviour tree document
 *
 * Creates the editor without docking it; lrg_editor_data_editor_open()
 * (or lrg_editor_bt_editor_open()) is what the editor uses.
 *
 * Returns: (transfer full): a new #LrgEditorBtEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorBtEditor * lrg_editor_bt_editor_new (LrgEditorApp      *app,
                                              LrgEditorDocument *document);

/**
 * lrg_editor_bt_editor_install:
 * @app: the editor application
 *
 * Registers the bt.* IPC methods (once). Every editor's construction calls
 * it; the data editor registry installs it at startup.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_bt_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_bt_editor_open:
 * @app: the editor application
 * @path: a `.rbt` file, absolute or relative to the project
 * @error: return location for a #GError
 *
 * Opens (or presents) the tab of @path.
 *
 * Returns: (transfer none) (nullable): the editor, or %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgEditorBtEditor * lrg_editor_bt_editor_open (LrgEditorApp  *app,
                                               const gchar   *path,
                                               GError       **error);

/**
 * lrg_editor_bt_editor_create:
 * @app: the editor application
 * @path: the new `.rbt` file, absolute or relative to the project
 * @name: (nullable): the tree's name (default: the file name)
 * @error: return location for a #GError
 *
 * Writes a starter tree (`root: {type: selector, id: root}` and an empty
 * blackboard) and opens it. Refuses to replace an existing file.
 *
 * Returns: (transfer none) (nullable): the editor of the new file
 */
LRG_AVAILABLE_IN_ALL
LrgEditorBtEditor * lrg_editor_bt_editor_create (LrgEditorApp  *app,
                                                 const gchar   *path,
                                                 const gchar   *name,
                                                 GError       **error);

/**
 * lrg_editor_bt_editor_get_asset:
 * @self: an #LrgEditorBtEditor
 *
 * Returns: (transfer none) (nullable): the tree parsed from the document
 *   after the last change, %NULL while the text does not load
 */
LRG_AVAILABLE_IN_ALL
LrgBtAsset * lrg_editor_bt_editor_get_asset (LrgEditorBtEditor *self);

/**
 * lrg_editor_bt_editor_get_load_error:
 * @self: an #LrgEditorBtEditor
 *
 * Returns: (transfer none) (nullable): why the document does not load
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_bt_editor_get_load_error (LrgEditorBtEditor *self);

/**
 * lrg_editor_bt_editor_get_canvas:
 * @self: an #LrgEditorBtEditor
 *
 * Returns: (transfer none): the node graph widget
 */
LRG_AVAILABLE_IN_ALL
LrgNodeGraph * lrg_editor_bt_editor_get_canvas (LrgEditorBtEditor *self);

/**
 * lrg_editor_bt_editor_get_shown_subtree:
 * @self: an #LrgEditorBtEditor
 *
 * Returns: (transfer none) (nullable): the inline subtree the canvas
 *   shows, %NULL for the main tree
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_bt_editor_get_shown_subtree (LrgEditorBtEditor *self);

/**
 * lrg_editor_bt_editor_show_subtree:
 * @self: an #LrgEditorBtEditor
 * @name: (nullable): an inline subtree's name, %NULL for the main tree
 *
 * Returns: %FALSE when @name is not an inline subtree of the file
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bt_editor_show_subtree (LrgEditorBtEditor *self,
                                            const gchar       *name);

/* --- nodes ---------------------------------------------------------------- */

/**
 * lrg_editor_bt_editor_dup_node_ids:
 * @self: an #LrgEditorBtEditor
 *
 * Every node of the file (the main tree, then the inline subtrees) depth
 * first. Nodes without an `id:` have their generated id.
 *
 * Returns: (transfer full) (array zero-terminated=1): the ids
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_editor_bt_editor_dup_node_ids (LrgEditorBtEditor *self);

/**
 * lrg_editor_bt_editor_get_node_path:
 * @self: an #LrgEditorBtEditor
 * @id: a node id
 *
 * Returns: (transfer none) (nullable): the node's YAML path in the
 *   document ("/root/children/1/child")
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_bt_editor_get_node_path (LrgEditorBtEditor *self,
                                                  const gchar       *id);

/**
 * lrg_editor_bt_editor_get_node_parent:
 * @self: an #LrgEditorBtEditor
 * @id: a node id
 *
 * Returns: (transfer none) (nullable): the parent's id, %NULL for a root
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_bt_editor_get_node_parent (LrgEditorBtEditor *self,
                                                    const gchar       *id);

/**
 * lrg_editor_bt_editor_add_node:
 * @self: an #LrgEditorBtEditor
 * @parent: (nullable): the parent's id; %NULL makes the node the root of
 *   the shown tree, which must be empty
 * @index: the position among the parent's children, -1 to append
 * @type: a node type ("sequence", "action", a registered type)
 * @id: (nullable): the new node's id, %NULL for a free one made from the
 *   type ("sequence2")
 * @error: return location for a #GError
 *
 * Adds a node (one undo step). A decorator parent takes one child; a leaf
 * parent takes none.
 *
 * Returns: (transfer full) (nullable): the new node's id
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_bt_editor_add_node (LrgEditorBtEditor  *self,
                                       const gchar        *parent,
                                       gint                index,
                                       const gchar        *type,
                                       const gchar        *id,
                                       GError            **error);

/**
 * lrg_editor_bt_editor_remove_node:
 * @self: an #LrgEditorBtEditor
 * @id: a node id
 * @error: return location for a #GError
 *
 * Removes a node with everything under it (one undo step).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bt_editor_remove_node (LrgEditorBtEditor  *self,
                                           const gchar        *id,
                                           GError            **error);

/**
 * lrg_editor_bt_editor_move_node:
 * @self: an #LrgEditorBtEditor
 * @id: the node to move
 * @parent: the new parent's id
 * @index: the position among the new parent's children, -1 to append
 * @error: return location for a #GError
 *
 * Re-parents or reorders a node with its subtree (one undo step). A node
 * cannot move under itself or one of its descendants.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bt_editor_move_node (LrgEditorBtEditor  *self,
                                         const gchar        *id,
                                         const gchar        *parent,
                                         gint                index,
                                         GError            **error);

/**
 * lrg_editor_bt_editor_set_node_id:
 * @self: an #LrgEditorBtEditor
 * @id: a node id
 * @new_id: the new id (unique in the file, non-empty, without '/')
 * @error: return location for a #GError
 *
 * Renames a node; its saved position follows.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bt_editor_set_node_id (LrgEditorBtEditor  *self,
                                           const gchar        *id,
                                           const gchar        *new_id,
                                           GError            **error);

/**
 * lrg_editor_bt_editor_set_node_type:
 * @self: an #LrgEditorBtEditor
 * @id: a node id
 * @type: the new type
 * @error: return location for a #GError
 *
 * Changes a node's type, converting `child:` and `children:` between
 * decorators and composites; a node with children cannot become a leaf,
 * nor one with several a decorator.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bt_editor_set_node_type (LrgEditorBtEditor  *self,
                                             const gchar        *id,
                                             const gchar        *type,
                                             GError            **error);

/**
 * lrg_editor_bt_editor_set_node_prop:
 * @self: an #LrgEditorBtEditor
 * @id: a node id
 * @key: a property key (`seconds`, `key`, `action`, ...; also `name`)
 * @yaml: (nullable): the value as YAML text ("1.5", "true", "\"7\""),
 *   %NULL to remove the key
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bt_editor_set_node_prop (LrgEditorBtEditor  *self,
                                             const gchar        *id,
                                             const gchar        *key,
                                             const gchar        *yaml,
                                             GError            **error);

/**
 * lrg_editor_bt_editor_set_node_param:
 * @self: an #LrgEditorBtEditor
 * @id: a node id
 * @key: a binding parameter (`params:` key)
 * @yaml: (nullable): the value as YAML text, %NULL to remove it
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bt_editor_set_node_param (LrgEditorBtEditor  *self,
                                              const gchar        *id,
                                              const gchar        *key,
                                              const gchar        *yaml,
                                              GError            **error);

/**
 * lrg_editor_bt_editor_set_node_position:
 * @self: an #LrgEditorBtEditor
 * @id: a node id
 * @x: left edge in graph units
 * @y: top edge in graph units
 *
 * Saves a card position in the `editor:` block (one undo step).
 *
 * Returns: %FALSE for an unknown id
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bt_editor_set_node_position (LrgEditorBtEditor *self,
                                                 const gchar       *id,
                                                 gfloat             x,
                                                 gfloat             y);

/**
 * lrg_editor_bt_editor_get_node_position:
 * @self: an #LrgEditorBtEditor
 * @id: a node id
 * @x: (out) (optional): left edge
 * @y: (out) (optional): top edge
 *
 * The card position: the saved one, else the automatic layout's.
 *
 * Returns: %TRUE if the position is saved in the file
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bt_editor_get_node_position (LrgEditorBtEditor *self,
                                                 const gchar       *id,
                                                 gfloat            *x,
                                                 gfloat            *y);

/**
 * lrg_editor_bt_editor_auto_layout:
 * @self: an #LrgEditorBtEditor
 *
 * Lays the shown tree out left to right (depth by column, leaves in rows,
 * parents centred on their children) and saves every position (one undo
 * step).
 *
 * Returns: the number of nodes placed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_bt_editor_auto_layout (LrgEditorBtEditor *self);

/**
 * lrg_editor_bt_editor_select_node:
 * @self: an #LrgEditorBtEditor
 * @id: (nullable): a node id, %NULL to clear
 *
 * Selects a card (showing an inline subtree that holds it) and inspects
 * it.
 *
 * Returns: %FALSE for an unknown id
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bt_editor_select_node (LrgEditorBtEditor *self,
                                           const gchar       *id);

/**
 * lrg_editor_bt_editor_get_selected_node:
 * @self: an #LrgEditorBtEditor
 *
 * Returns: (transfer none) (nullable): the selected node's id
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_bt_editor_get_selected_node (LrgEditorBtEditor *self);

/* --- blackboard and subtrees ---------------------------------------------- */

/**
 * lrg_editor_bt_editor_add_key:
 * @self: an #LrgEditorBtEditor
 * @name: the key
 * @type: "int", "float", "bool", "string" or "object"
 * @default_yaml: (nullable): the default as YAML text, %NULL for none
 * @error: return location for a #GError
 *
 * Declares a blackboard key in the `blackboard:` schema.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bt_editor_add_key (LrgEditorBtEditor  *self,
                                       const gchar        *name,
                                       const gchar        *type,
                                       const gchar        *default_yaml,
                                       GError            **error);

/**
 * lrg_editor_bt_editor_remove_key:
 * @self: an #LrgEditorBtEditor
 * @name: a declared key
 *
 * Returns: %TRUE if the key was declared
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bt_editor_remove_key (LrgEditorBtEditor *self,
                                          const gchar       *name);

/**
 * lrg_editor_bt_editor_set_subtree:
 * @self: an #LrgEditorBtEditor
 * @name: the subtree's name
 * @file: (nullable): a `.rbt` path relative to this file for a file
 *   subtree, %NULL for an inline one (a new inline subtree's root is an
 *   empty selector)
 * @error: return location for a #GError
 *
 * Declares (or redeclares) a subtree under `subtrees:`.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bt_editor_set_subtree (LrgEditorBtEditor  *self,
                                           const gchar        *name,
                                           const gchar        *file,
                                           GError            **error);

/**
 * lrg_editor_bt_editor_remove_subtree:
 * @self: an #LrgEditorBtEditor
 * @name: a subtree's name
 *
 * Returns: %TRUE if the subtree was declared
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_bt_editor_remove_subtree (LrgEditorBtEditor *self,
                                              const gchar       *name);

/**
 * lrg_editor_bt_editor_open_subtree:
 * @self: an #LrgEditorBtEditor
 * @id: a `subtree` node's id
 * @error: return location for a #GError
 *
 * What double clicking the card does: opens a file subtree in its own tab
 * or shows an inline one in this canvas.
 *
 * Returns: (transfer none) (nullable): the editor showing the subtree
 *   (@self for an inline one), %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgEditorBtEditor * lrg_editor_bt_editor_open_subtree (LrgEditorBtEditor  *self,
                                                       const gchar        *id,
                                                       GError            **error);

/* --- live state ----------------------------------------------------------- */

/**
 * lrg_editor_bt_editor_get_live_runner:
 * @self: an #LrgEditorBtEditor
 *
 * Returns: (transfer none) (nullable): the runner of the live entity the
 *   canvas follows while the level plays, %NULL when stopped or when no
 *   live entity runs this file
 */
LRG_AVAILABLE_IN_ALL
LrgBtRunner * lrg_editor_bt_editor_get_live_runner (LrgEditorBtEditor *self);

/**
 * lrg_editor_bt_editor_get_live_status:
 * @self: an #LrgEditorBtEditor
 * @id: a node id
 * @visited: (out) (optional): whether the last tick reached the node
 *
 * Returns: the node's status after the last tick that reached it, or
 *   %LRG_BT_STATUS_INVALID when nothing is live
 */
LRG_AVAILABLE_IN_ALL
LrgBTStatus lrg_editor_bt_editor_get_live_status (LrgEditorBtEditor *self,
                                                  const gchar       *id,
                                                  gboolean          *visited);

G_END_DECLS
