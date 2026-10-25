/* lrg-editor.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Host-agnostic editor runtime for an #LrgLevel.
 *
 * LrgEditor owns the level being edited, the current selection, the undo/redo
 * command stacks, and the active manipulation tool / snap settings. All
 * mutating operations go through reversible #LrgEditorCommand objects so they
 * participate in undo/redo. This is the C API a host IDE (e.g. cmacs) and the
 * standalone engine-drawn editor both target.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR (lrg_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditor, lrg_editor, LRG, EDITOR, GObject)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_editor_new:
 *
 * Creates a new editor with a fresh, empty #LrgLevel.
 *
 * Returns: (transfer full): a new #LrgEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditor * lrg_editor_new (void);

/* ==========================================================================
 * Document
 * ========================================================================== */

/**
 * lrg_editor_get_level:
 * @self: an #LrgEditor
 *
 * Returns: (transfer none): the level being edited
 */
LRG_AVAILABLE_IN_ALL
LrgLevel * lrg_editor_get_level (LrgEditor *self);

/**
 * lrg_editor_set_level:
 * @self: an #LrgEditor
 * @level: (transfer none): the level to edit
 *
 * Replaces the edited level, clearing selection and undo/redo history.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_set_level (LrgEditor *self,
                           LrgLevel  *level);

/**
 * lrg_editor_load_level:
 * @self: an #LrgEditor
 * @path: a `.rlevel` file path
 * @error: (nullable): return location for an error
 *
 * Loads a level from disk and makes it the edited document.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_load_level (LrgEditor    *self,
                                const gchar  *path,
                                GError      **error);

/**
 * lrg_editor_save_level:
 * @self: an #LrgEditor
 * @path: destination file path
 * @error: (nullable): return location for an error
 *
 * Saves the edited level to disk.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_save_level (LrgEditor    *self,
                                const gchar  *path,
                                GError      **error);

/**
 * lrg_editor_new_level:
 * @self: an #LrgEditor
 * @name: (nullable): the new level's name
 *
 * Replaces the document with a fresh empty level, clearing the path,
 * selection and history.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_new_level (LrgEditor   *self,
                           const gchar *name);

/**
 * lrg_editor_save:
 * @self: an #LrgEditor
 * @error: (nullable): return location for an error
 *
 * Saves the document to its current #LrgEditor:path. Fails with
 * %G_IO_ERROR_INVALID_FILENAME when no path is set.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_save (LrgEditor  *self,
                          GError    **error);

/**
 * lrg_editor_reload:
 * @self: an #LrgEditor
 * @error: (nullable): return location for an error
 *
 * Re-reads the document from its #LrgEditor:path, discarding unsaved
 * changes (used when the file changed on disk, e.g. edited in Emacs).
 * The selection is restored by GUID where the nodes still exist.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_reload (LrgEditor  *self,
                            GError    **error);

/**
 * lrg_editor_get_path:
 * @self: an #LrgEditor
 *
 * Returns: (transfer none) (nullable): the file the document was loaded
 *   from or last saved to
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_get_path (LrgEditor *self);

/**
 * lrg_editor_set_path:
 * @self: an #LrgEditor
 * @path: (nullable): the document's file path
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_set_path (LrgEditor   *self,
                          const gchar *path);

/**
 * lrg_editor_is_dirty:
 * @self: an #LrgEditor
 *
 * Returns: %TRUE if the document differs from what was last loaded or saved
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_is_dirty (LrgEditor *self);

/**
 * lrg_editor_mark_clean:
 * @self: an #LrgEditor
 *
 * Records the current history position as the saved state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_mark_clean (LrgEditor *self);

/**
 * lrg_editor_mark_modified:
 * @self: an #LrgEditor
 *
 * Marks the document as having unsaved changes even without history
 * (e.g. after restoring an autosave over the file it belongs to).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_mark_modified (LrgEditor *self);

/**
 * lrg_editor_serialize:
 * @self: an #LrgEditor
 *
 * Returns: (transfer full) (nullable): the document as `.rlevel` YAML
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_serialize (LrgEditor *self);

/* ==========================================================================
 * Selection
 * ========================================================================== */

/**
 * lrg_editor_get_selection:
 * @self: an #LrgEditor
 *
 * Returns: (transfer none): the selection model
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSelection * lrg_editor_get_selection (LrgEditor *self);

/**
 * lrg_editor_select:
 * @self: an #LrgEditor
 * @node: (nullable): node to select, or %NULL to clear
 * @additive: if %TRUE, add to the current selection; else replace it
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_select (LrgEditor *self,
                        LrgNode   *node,
                        gboolean   additive);

/**
 * lrg_editor_toggle_selected:
 * @self: an #LrgEditor
 * @node: the node to toggle
 *
 * Adds @node to the selection, or removes it if already selected
 * (Ctrl-click behaviour).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_toggle_selected (LrgEditor *self,
                                 LrgNode   *node);

/**
 * lrg_editor_select_nodes:
 * @self: an #LrgEditor
 * @nodes: (element-type LrgNode) (transfer none): nodes to select
 * @additive: if %TRUE, add to the current selection; else replace it
 *
 * Selects several nodes at once (box select, select range). The last
 * element becomes the primary selection.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_select_nodes (LrgEditor *self,
                              GPtrArray *nodes,
                              gboolean   additive);

/**
 * lrg_editor_select_all:
 * @self: an #LrgEditor
 *
 * Selects every node in the level.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_select_all (LrgEditor *self);

/**
 * lrg_editor_get_selected_roots:
 * @self: an #LrgEditor
 *
 * Returns the selected nodes with any node whose ancestor is also selected
 * removed, in tree (depth-first) order. This is the set structural
 * operations (delete, duplicate, copy) act on.
 *
 * Returns: (transfer container) (element-type LrgNode): the selected roots
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_get_selected_roots (LrgEditor *self);

/**
 * lrg_editor_find_nodes:
 * @self: an #LrgEditor
 * @query: case-insensitive substring of a node name, or a GUID
 *
 * Returns: (transfer container) (element-type LrgNode): matching nodes in
 *   tree order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_find_nodes (LrgEditor   *self,
                                   const gchar *query);

/**
 * lrg_editor_find_node:
 * @self: an #LrgEditor
 * @ref: a node GUID or a slash-separated name path
 *
 * Resolves a node reference as used by the IPC protocol and the console.
 *
 * Returns: (transfer none) (nullable): the node, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_find_node (LrgEditor   *self,
                                const gchar *ref);

/* ==========================================================================
 * Mutating operations (each pushes an undo command)
 * ========================================================================== */

/**
 * lrg_editor_add_node:
 * @self: an #LrgEditor
 * @node: (transfer none): node to add
 * @parent: (nullable): parent node, or %NULL for the root
 *
 * Adds @node under @parent and selects it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_add_node (LrgEditor *self,
                          LrgNode   *node,
                          LrgNode   *parent);

/**
 * lrg_editor_delete_node:
 * @self: an #LrgEditor
 * @node: the node to delete
 *
 * Removes @node from the level (deselecting it first).
 *
 * Returns: %TRUE if the node was deleted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_delete_node (LrgEditor *self,
                                 LrgNode   *node);

/**
 * lrg_editor_reparent_node:
 * @self: an #LrgEditor
 * @node: the node to reparent
 * @new_parent: the destination parent
 *
 * Returns: %TRUE if the node was reparented
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_reparent_node (LrgEditor *self,
                                   LrgNode   *node,
                                   LrgNode   *new_parent);

/**
 * lrg_editor_set_node_transform:
 * @self: an #LrgEditor
 * @node: the node to transform
 * @location: (array fixed-size=3): new local position xyz
 * @rotation: (array fixed-size=3): new local rotation xyz (Euler radians)
 * @scale: (array fixed-size=3): new local scale xyz
 *
 * Sets a node's full local transform as one undoable command.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_set_node_transform (LrgEditor    *self,
                                    LrgNode      *node,
                                    const gfloat *location,
                                    const gfloat *rotation,
                                    const gfloat *scale);

/**
 * lrg_editor_set_node_property:
 * @self: an #LrgEditor
 * @node: the node whose property changes
 * @prop_name: a #GObject property name on the node (e.g. "name", "visible")
 * @value: the new value
 *
 * Sets a node property as one undoable command.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_set_node_property (LrgEditor    *self,
                                   LrgNode      *node,
                                   const gchar  *prop_name,
                                   const GValue *value);

/**
 * lrg_editor_set_visual_param:
 * @self: an #LrgEditor
 * @node: the node whose visual param-bag changes
 * @param: the param-bag key (e.g. "cad:thickness")
 * @value: the new numeric value
 * @merge: %TRUE to coalesce onto the previous command (slider drag)
 *
 * Sets a numeric visual param on @node as an undoable command.  When
 * @merge is %TRUE and the top undo command targets the same node and
 * param, the two collapse into one undo step — so a slider drag is a
 * single undo.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_set_visual_param (LrgEditor   *self,
                                  LrgNode     *node,
                                  const gchar *param,
                                  gdouble      value,
                                  gboolean     merge);

/**
 * lrg_editor_execute:
 * @self: an #LrgEditor
 * @command: (transfer full): a command
 *
 * Applies @command and records it in the history (or in the open group, see
 * lrg_editor_begin_group()). Use this to run custom commands.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_execute (LrgEditor        *self,
                         LrgEditorCommand *command);

/**
 * lrg_editor_insert_node:
 * @self: an #LrgEditor
 * @node: (transfer none): node to add
 * @parent: (nullable): parent node, or %NULL for the root
 * @index: child index, or -1 to append
 *
 * Inserts @node under @parent at @index as one undoable command and selects
 * it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_insert_node (LrgEditor *self,
                             LrgNode   *node,
                             LrgNode   *parent,
                             gint       index);

/**
 * lrg_editor_move_node:
 * @self: an #LrgEditor
 * @node: the node to move
 * @new_parent: (nullable): the destination parent, or %NULL for the root
 * @index: destination index among the new parent's children (after removal
 *   from the old position), or -1 to append
 * @keep_world: if %TRUE, adjust the local transform so the node keeps its
 *   world-space placement under the new parent (Godot/Unity behaviour)
 *
 * Reparents and/or reorders @node as one undoable step.
 *
 * Returns: %TRUE if the node moved
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_move_node (LrgEditor *self,
                               LrgNode   *node,
                               LrgNode   *new_parent,
                               gint       index,
                               gboolean   keep_world);

/**
 * lrg_editor_delete_selection:
 * @self: an #LrgEditor
 *
 * Deletes every selected node (with its subtree) as one undo step.
 *
 * Returns: the number of nodes deleted
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_delete_selection (LrgEditor *self);

/**
 * lrg_editor_duplicate_selection:
 * @self: an #LrgEditor
 *
 * Duplicates the selected subtrees next to their originals with fresh GUIDs
 * and unique names, as one undo step, and selects the copies.
 *
 * Returns: (transfer container) (element-type LrgNode): the new nodes
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_duplicate_selection (LrgEditor *self);

/**
 * lrg_editor_copy_selection:
 * @self: an #LrgEditor
 *
 * Serializes the selected subtrees as `.rlevel` YAML, stores it as the
 * editor clipboard and returns it (hosts may also put it on the system
 * clipboard; pasting it into a `.rlevel` file in a text editor works).
 *
 * Returns: (transfer full) (nullable): the clipboard text, or %NULL if
 *   nothing is selected
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_copy_selection (LrgEditor *self);

/**
 * lrg_editor_cut_selection:
 * @self: an #LrgEditor
 *
 * Copies then deletes the selection.
 *
 * Returns: (transfer full) (nullable): the clipboard text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_cut_selection (LrgEditor *self);

/**
 * lrg_editor_paste:
 * @self: an #LrgEditor
 * @data: (nullable): `.rlevel` YAML to paste, or %NULL for the editor
 *   clipboard
 * @parent: (nullable): parent for the pasted nodes, or %NULL for the root
 * @error: (nullable): return location for an error
 *
 * Pastes nodes with fresh GUIDs (and unique sibling names) as one undo step
 * and selects them.
 *
 * Returns: (transfer container) (element-type LrgNode) (nullable): the
 *   pasted nodes, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_paste (LrgEditor    *self,
                              const gchar  *data,
                              LrgNode      *parent,
                              GError      **error);

/**
 * lrg_editor_get_clipboard:
 * @self: an #LrgEditor
 *
 * Returns: (transfer none) (nullable): the editor clipboard text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_get_clipboard (LrgEditor *self);

/**
 * lrg_editor_set_clipboard:
 * @self: an #LrgEditor
 * @data: (nullable): new clipboard text
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_set_clipboard (LrgEditor   *self,
                               const gchar *data);

/**
 * lrg_editor_rename_node:
 * @self: an #LrgEditor
 * @node: the node to rename
 * @name: the new name
 *
 * Renames @node as one undoable command.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_rename_node (LrgEditor   *self,
                             LrgNode     *node,
                             const gchar *name);

/**
 * lrg_editor_transform_node:
 * @self: an #LrgEditor
 * @node: the node to transform
 * @location: (array fixed-size=3) (nullable): new local position, or %NULL
 *   to keep
 * @rotation: (array fixed-size=3) (nullable): new local Euler rotation
 *   (radians), or %NULL to keep
 * @scale: (array fixed-size=3) (nullable): new local scale, or %NULL to keep
 * @merge: %TRUE to coalesce with the previous transform of the same node
 *   (gizmo and drag-field edits)
 *
 * Sets any of a node's local TRS channels as an undoable command.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_transform_node (LrgEditor    *self,
                                LrgNode      *node,
                                const gfloat *location,
                                const gfloat *rotation,
                                const gfloat *scale,
                                gboolean      merge);

/**
 * lrg_editor_set_object_property:
 * @self: an #LrgEditor
 * @object: any #GObject in the document (node, visual, material, level)
 * @prop_name: the property name
 * @value: the new value (transformed to the property type if needed)
 * @merge: %TRUE to coalesce with a previous edit of the same property
 *
 * Generic undoable property setter used by the reflection inspector.
 *
 * Returns: %TRUE if the property exists and the value was applied
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_set_object_property (LrgEditor    *self,
                                         GObject      *object,
                                         const gchar  *prop_name,
                                         const GValue *value,
                                         gboolean      merge);

/**
 * lrg_editor_snapshot_node:
 * @node: a node
 *
 * Captures the node's own editable state (see lrg_node_copy_state()) in a
 * detached node, for use with lrg_editor_commit_node_state().
 *
 * Returns: (transfer full): the snapshot
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_snapshot_node (LrgNode *node);

/**
 * lrg_editor_commit_node_state:
 * @self: an #LrgEditor
 * @node: a node that has been edited in place
 * @before: the snapshot taken before the edit
 * @label: (nullable): history label, e.g. "Add Component"
 * @merge: %TRUE to coalesce with a previous commit of the same node and label
 *
 * Records an in-place edit of @node as an undoable command. This is the
 * catch-all path for edits without a dedicated command: take a snapshot,
 * mutate the node directly, commit.
 *
 * Returns: %TRUE if the node changed and a command was recorded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_commit_node_state (LrgEditor   *self,
                                       LrgNode     *node,
                                       LrgNode     *before,
                                       const gchar *label,
                                       gboolean     merge);

/**
 * lrg_editor_set_visual:
 * @self: an #LrgEditor
 * @node: a node
 * @visual: (nullable) (transfer none): the new visual payload (copied), or
 *   %NULL to make @node a group node
 *
 * Replaces the node's visual payload as one undoable command.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_set_visual (LrgEditor     *self,
                            LrgNode       *node,
                            LrgNodeVisual *visual);

/**
 * lrg_editor_add_component:
 * @self: an #LrgEditor
 * @node: a node
 * @desc: (transfer none): the component description (copied)
 *
 * Appends a component description as one undoable command.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_add_component (LrgEditor        *self,
                               LrgNode          *node,
                               LrgComponentDesc *desc);

/**
 * lrg_editor_remove_component:
 * @self: an #LrgEditor
 * @node: a node
 * @index: component index
 *
 * Returns: %TRUE if a component was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_remove_component (LrgEditor *self,
                                      LrgNode   *node,
                                      guint      index);

/**
 * lrg_editor_set_component_value:
 * @self: an #LrgEditor
 * @node: a node
 * @index: component index
 * @key: the property key
 * @value: (nullable): new value, or %NULL to remove the key
 * @merge: %TRUE to coalesce consecutive edits of the same key (drags)
 *
 * Returns: %TRUE if the component exists and changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_set_component_value (LrgEditor    *self,
                                         LrgNode      *node,
                                         guint         index,
                                         const gchar  *key,
                                         const GValue *value,
                                         gboolean      merge);

/**
 * lrg_editor_add_script:
 * @self: an #LrgEditor
 * @node: a node
 * @binding: (transfer none): the script binding (copied)
 *
 * Appends a script binding as one undoable command.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_add_script (LrgEditor        *self,
                            LrgNode          *node,
                            LrgScriptBinding *binding);

/**
 * lrg_editor_remove_script:
 * @self: an #LrgEditor
 * @node: a node
 * @index: script index
 *
 * Returns: %TRUE if a script binding was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_remove_script (LrgEditor *self,
                                   LrgNode   *node,
                                   guint      index);

/**
 * lrg_editor_instantiate_prefab:
 * @self: an #LrgEditor
 * @path: a `.rprefab` file
 * @parent: (nullable): parent node, or %NULL for the root
 * @error: (nullable): return location for an error
 *
 * Loads a prefab and inserts a fresh instance (new GUIDs) as one undoable
 * command. The instance root remembers its source in a visual of kind
 * %LRG_NODE_VISUAL_PREFAB_INSTANCE only if it had no visual of its own;
 * otherwise the source path is stored in the `prefab:source` param.
 *
 * Returns: (transfer none) (nullable): the instance root
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_instantiate_prefab (LrgEditor    *self,
                                         const gchar  *path,
                                         LrgNode      *parent,
                                         GError      **error);

/* ==========================================================================
 * Grouping
 * ========================================================================== */

/**
 * lrg_editor_begin_group:
 * @self: an #LrgEditor
 * @label: (nullable): history label for the group
 *
 * Opens a compound undo step. Every command executed until the matching
 * lrg_editor_end_group() is applied immediately and recorded as one undo
 * step. Groups nest; only the outermost one is recorded.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_begin_group (LrgEditor   *self,
                             const gchar *label);

/**
 * lrg_editor_end_group:
 * @self: an #LrgEditor
 *
 * Closes the group opened by lrg_editor_begin_group(). Empty groups are
 * discarded.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_end_group (LrgEditor *self);

/* ==========================================================================
 * Undo / redo
 * ========================================================================== */

/**
 * lrg_editor_can_undo:
 * @self: an #LrgEditor
 *
 * Returns: %TRUE if there is a command to undo
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_can_undo (LrgEditor *self);

/**
 * lrg_editor_can_redo:
 * @self: an #LrgEditor
 *
 * Returns: %TRUE if there is a command to redo
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_can_redo (LrgEditor *self);

/**
 * lrg_editor_undo:
 * @self: an #LrgEditor
 *
 * Undoes the most recent command, if any.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_undo (LrgEditor *self);

/**
 * lrg_editor_redo:
 * @self: an #LrgEditor
 *
 * Redoes the most recently undone command, if any.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_redo (LrgEditor *self);

/**
 * lrg_editor_get_undo_count:
 * @self: an #LrgEditor
 *
 * Returns: the number of undoable steps
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_get_undo_count (LrgEditor *self);

/**
 * lrg_editor_get_redo_count:
 * @self: an #LrgEditor
 *
 * Returns: the number of redoable steps
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_get_redo_count (LrgEditor *self);

/**
 * lrg_editor_get_undo_command:
 * @self: an #LrgEditor
 * @index: 0 is the oldest step
 *
 * Returns: (transfer none) (nullable): the undo-stack command at @index
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_get_undo_command (LrgEditor *self,
                                                guint      index);

/**
 * lrg_editor_get_redo_command:
 * @self: an #LrgEditor
 * @index: 0 is the step that lrg_editor_redo() would apply next
 *
 * Returns: (transfer none) (nullable): the redo-stack command at @index
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_get_redo_command (LrgEditor *self,
                                                guint      index);

/**
 * lrg_editor_jump_to_history:
 * @self: an #LrgEditor
 * @undo_count: the desired number of applied steps (0 = initial document)
 *
 * Undoes or redoes until exactly @undo_count steps are applied (the History
 * panel's click-to-jump).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_jump_to_history (LrgEditor *self,
                                 guint      undo_count);

/**
 * lrg_editor_clear_history:
 * @self: an #LrgEditor
 *
 * Drops all undo and redo steps (the document keeps its dirty state).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_clear_history (LrgEditor *self);

/**
 * lrg_editor_set_history_limit:
 * @self: an #LrgEditor
 * @limit: maximum number of undo steps kept (0 = unlimited)
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_set_history_limit (LrgEditor *self,
                                   guint      limit);

/**
 * lrg_editor_get_history_limit:
 * @self: an #LrgEditor
 *
 * Returns: the maximum number of undo steps kept (0 = unlimited)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_get_history_limit (LrgEditor *self);

/* ==========================================================================
 * Tool / snap state
 * ========================================================================== */

/**
 * lrg_editor_get_tool:
 * @self: an #LrgEditor
 *
 * Returns: the active manipulation tool
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTool lrg_editor_get_tool (LrgEditor *self);

/**
 * lrg_editor_set_tool:
 * @self: an #LrgEditor
 * @tool: the manipulation tool
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_set_tool (LrgEditor     *self,
                          LrgEditorTool  tool);

/**
 * lrg_editor_set_snap:
 * @self: an #LrgEditor
 * @translate: translation snap increment (0 disables)
 * @rotate: rotation snap increment in radians (0 disables)
 * @scale: scale snap increment (0 disables)
 *
 * Sets the gizmo snap increments.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_set_snap (LrgEditor *self,
                          gdouble    translate,
                          gdouble    rotate,
                          gdouble    scale);

/**
 * lrg_editor_get_snap:
 * @self: an #LrgEditor
 * @translate: (out) (optional): translation snap
 * @rotate: (out) (optional): rotation snap
 * @scale: (out) (optional): scale snap
 *
 * Gets the current gizmo snap increments.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_get_snap (LrgEditor *self,
                          gdouble   *translate,
                          gdouble   *rotate,
                          gdouble   *scale);

/**
 * lrg_editor_get_snap_enabled:
 * @self: an #LrgEditor
 *
 * Returns: whether gizmo snapping is active (the increments are kept even
 *   while it is off)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_get_snap_enabled (LrgEditor *self);

/**
 * lrg_editor_set_snap_enabled:
 * @self: an #LrgEditor
 * @enabled: whether gizmo snapping is active
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_set_snap_enabled (LrgEditor *self,
                                  gboolean   enabled);

/**
 * lrg_editor_get_space:
 * @self: an #LrgEditor
 *
 * Returns: the gizmo coordinate space
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSpace lrg_editor_get_space (LrgEditor *self);

/**
 * lrg_editor_set_space:
 * @self: an #LrgEditor
 * @space: the gizmo coordinate space
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_set_space (LrgEditor      *self,
                           LrgEditorSpace  space);

G_END_DECLS
