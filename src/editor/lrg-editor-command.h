/* lrg-editor-command.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Undoable editor operations.
 *
 * LrgEditorCommand is a reversible mutation of an #LrgLevel pushed onto the
 * #LrgEditor undo stack. It is a discriminated record (see #LrgEditorCommandKind)
 * capturing the before/after state needed to apply and undo the change.
 * Consecutive compatible commands (e.g. successive transforms of the same node
 * during a drag) can be coalesced with lrg_editor_command_merge().
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LrgEditorCommandKind:
 * @LRG_EDITOR_CMD_TRANSFORM: change a node's local TRS transform
 * @LRG_EDITOR_CMD_SET_PROPERTY: set a #GObject property on a node
 * @LRG_EDITOR_CMD_ADD_NODE: add a node to the level
 * @LRG_EDITOR_CMD_DELETE_NODE: remove a node from the level
 * @LRG_EDITOR_CMD_REPARENT: move a node to a new parent
 * @LRG_EDITOR_CMD_SET_VISUAL_PARAM: set a numeric param in a node's
 *   visual param bag (CAD parameters; slider drags coalesce)
 * @LRG_EDITOR_CMD_GROUP: an ordered compound of child commands applied
 *   front-to-back and undone back-to-front as one undo step
 * @LRG_EDITOR_CMD_NODE_STATE: restore a node's own state (name, flags,
 *   transform, visual, components, scripts) from before/after snapshots;
 *   the generic command behind inspector edits
 * @LRG_EDITOR_CMD_TILES: change cells of one layer of a tilemap node's
 *   tile grid (a paint dab, rectangle or flood fill)
 * @LRG_EDITOR_CMD_TIMELINE: add, remove or replace one of the level's
 *   timelines from before/after copies (keyframe edits)
 * @LRG_EDITOR_CMD_PREFAB_OVERRIDE: add or remove one override key on a
 *   linked prefab instance root (since 0.8.1)
 * @LRG_EDITOR_CMD_PREFAB_SYNC: restore a whole prefab instance subtree
 *   (states, links, children) from before/after snapshots; behind sync,
 *   apply, revert, relink and unlink (since 0.8.1)
 * @LRG_EDITOR_CMD_PREFAB_FILE: rewrite a prefab file between two contents
 *   (apply-to-source; since 0.8.1), or create and remove one (Create
 *   Variant; since 0.9.0)
 *
 * The kind of mutation an #LrgEditorCommand represents.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_EDITOR_CMD_TRANSFORM,
    LRG_EDITOR_CMD_SET_PROPERTY,
    LRG_EDITOR_CMD_ADD_NODE,
    LRG_EDITOR_CMD_DELETE_NODE,
    LRG_EDITOR_CMD_REPARENT,
    LRG_EDITOR_CMD_SET_VISUAL_PARAM,
    LRG_EDITOR_CMD_GROUP,
    LRG_EDITOR_CMD_NODE_STATE,
    LRG_EDITOR_CMD_TILES,
    LRG_EDITOR_CMD_TIMELINE,
    LRG_EDITOR_CMD_PREFAB_OVERRIDE,
    LRG_EDITOR_CMD_PREFAB_SYNC,
    LRG_EDITOR_CMD_PREFAB_FILE
} LrgEditorCommandKind;

#define LRG_TYPE_EDITOR_COMMAND (lrg_editor_command_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorCommand, lrg_editor_command, LRG, EDITOR_COMMAND, GObject)

/* ==========================================================================
 * Constructors (typically used by #LrgEditor)
 * ========================================================================== */

/**
 * lrg_editor_command_new_transform:
 * @node: the node being transformed
 * @before: (array fixed-size=9): before TRS (loc xyz, rot xyz, scale xyz)
 * @after: (array fixed-size=9): after TRS (loc xyz, rot xyz, scale xyz)
 *
 * Returns: (transfer full): a new transform command
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_transform (LrgNode      *node,
                                                     const gfloat *before,
                                                     const gfloat *after);

/**
 * lrg_editor_command_new_set_property:
 * @target: the node (as #GObject) whose property changes
 * @prop_name: the property name
 * @before: the previous value
 * @after: the new value
 *
 * Returns: (transfer full): a new set-property command
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_set_property (GObject      *target,
                                                        const gchar  *prop_name,
                                                        const GValue *before,
                                                        const GValue *after);

/**
 * lrg_editor_command_new_add_node:
 * @level: the level
 * @node: the node being added
 * @parent: (nullable): the parent, or %NULL for the root
 *
 * Returns: (transfer full): a new add-node command (appends)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_add_node (LrgLevel *level,
                                                    LrgNode  *node,
                                                    LrgNode  *parent);

/**
 * lrg_editor_command_new_insert_node:
 * @level: the level
 * @node: the node being added
 * @parent: (nullable): the parent, or %NULL for the root
 * @index: child index to insert at, or -1 to append
 *
 * Returns: (transfer full): a new add-node command inserting at @index
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_insert_node (LrgLevel *level,
                                                       LrgNode  *node,
                                                       LrgNode  *parent,
                                                       gint      index);

/**
 * lrg_editor_command_new_delete_node:
 * @level: the level
 * @node: the node being deleted
 * @parent: the node's current parent
 *
 * The node's current child index is recorded so undo restores it in place.
 *
 * Returns: (transfer full): a new delete-node command
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_delete_node (LrgLevel *level,
                                                       LrgNode  *node,
                                                       LrgNode  *parent);

/**
 * lrg_editor_command_new_reparent:
 * @node: the node being reparented
 * @old_parent: the node's current parent
 * @new_parent: the destination parent
 *
 * Returns: (transfer full): a new reparent command
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_reparent (LrgNode *node,
                                                    LrgNode *old_parent,
                                                    LrgNode *new_parent);

/**
 * lrg_editor_command_new_move:
 * @node: the node being moved
 * @new_parent: the destination parent (may equal the current parent to
 *   reorder)
 * @new_index: destination child index, or -1 to append
 *
 * Creates a reparent/reorder command. The node's current parent and index
 * are captured so undo restores the exact position. @new_index is the index
 * in the destination's children *after* the node has been removed from its
 * old position.
 *
 * Returns: (transfer full) (nullable): a new reparent command, or %NULL if
 *   @node has no parent or the move would create a cycle
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_move (LrgNode *node,
                                                LrgNode *new_parent,
                                                gint     new_index);

/**
 * lrg_editor_command_new_set_visual_param:
 * @node: the node whose visual param changes
 * @param: the param-bag key (e.g. "cad:thickness")
 * @before: the previous value
 * @after: the new value
 *
 * Returns: (transfer full): a new set-visual-param command
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_set_visual_param (LrgNode     *node,
                                                            const gchar *param,
                                                            gdouble      before,
                                                            gdouble      after);

/**
 * lrg_editor_command_new_group:
 * @label: (nullable): description shown in the history (e.g. "Duplicate")
 *
 * Creates an empty compound command. Children are added with
 * lrg_editor_command_group_append().
 *
 * Returns: (transfer full): a new group command
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_group (const gchar *label);

/**
 * lrg_editor_command_group_append:
 * @self: a group command
 * @child: (transfer full): the child command
 *
 * Appends @child to the group. The child is not applied; callers that build
 * a group from already-applied children (as #LrgEditor does while a group is
 * open) keep the document consistent themselves.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_command_group_append (LrgEditorCommand *self,
                                      LrgEditorCommand *child);

/**
 * lrg_editor_command_group_get_n_children:
 * @self: a group command
 *
 * Returns: the number of child commands
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_command_group_get_n_children (LrgEditorCommand *self);

/**
 * lrg_editor_command_group_get_child:
 * @self: a group command
 * @index: child index
 *
 * Returns: (transfer none) (nullable): the child command at @index
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_group_get_child (LrgEditorCommand *self,
                                                       guint             index);

/**
 * lrg_editor_command_new_node_state:
 * @node: the live node
 * @before: a snapshot of @node's state before the edit (see
 *   lrg_editor_snapshot_node())
 * @after: a snapshot of @node's state after the edit
 * @label: (nullable): description shown in the history
 *
 * Creates a snapshot-restore command. Applying copies @after's own state
 * into @node with lrg_node_copy_state(); undoing copies @before. The
 * snapshots are copied, so callers keep ownership of theirs.
 *
 * Returns: (transfer full): a new node-state command
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_node_state (LrgNode     *node,
                                                     LrgNode     *before,
                                                     LrgNode     *after,
                                                     const gchar *label);

/**
 * lrg_editor_command_new_tiles:
 * @node: a tilemap node (its visual carries an #LrgNodeTilemap)
 * @layer: the tile layer the changes apply to
 * @label: (nullable): history label, e.g. "Paint Tiles"
 *
 * Creates an empty tile-change command; add cells with
 * lrg_editor_command_tiles_add(). The grid is looked up on the node at
 * apply time, so the command survives the visual being replaced by a
 * node-state undo.
 *
 * Returns: (transfer full): a new command
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_tiles (LrgNode     *node,
                                                 guint        layer,
                                                 const gchar *label);

/**
 * lrg_editor_command_tiles_add:
 * @self: a %LRG_EDITOR_CMD_TILES command
 * @x: cell column
 * @y: cell row
 * @before: the tile id before the change
 * @after: the tile id after the change
 *
 * Records one cell change. Add each cell once per command.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_command_tiles_add (LrgEditorCommand *self,
                                   gint              x,
                                   gint              y,
                                   guint             before,
                                   guint             after);

/**
 * lrg_editor_command_tiles_get_n_changes:
 * @self: a %LRG_EDITOR_CMD_TILES command
 *
 * Returns: the number of recorded cell changes
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_command_tiles_get_n_changes (LrgEditorCommand *self);

/**
 * lrg_editor_command_new_prefab_override:
 * @owner: a linked prefab instance root
 * @key: an override key of @owner's link
 * @add: %TRUE to add @key, %FALSE to remove it
 *
 * Creates a command that adds (or removes) one override key. Undo does the
 * opposite.
 *
 * Returns: (transfer full): a new command
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_prefab_override (LrgNode     *owner,
                                                           const gchar *key,
                                                           gboolean     add);

/**
 * lrg_editor_command_new_prefab_sync:
 * @level: (nullable): the level holding @root, for node-added/-removed
 *   signals on undo and redo
 * @root: the subtree to track (a prefab instance root)
 * @label: (nullable): history label
 *
 * Creates a subtree snapshot command and captures the "before" state of
 * @root now: every node's own state, prefab link and child list (the live
 * node objects are referenced, so nodes removed later come back on undo).
 * Mutate the subtree, then execute the command: applying it the first time
 * captures the "after" state (or call
 * lrg_editor_command_prefab_sync_capture() explicitly).
 *
 * Returns: (transfer full): a new command
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_prefab_sync (LrgLevel    *level,
                                                       LrgNode     *root,
                                                       const gchar *label);

/**
 * lrg_editor_command_prefab_sync_capture:
 * @self: a %LRG_EDITOR_CMD_PREFAB_SYNC command
 *
 * Captures the current state of the tracked subtree as the "after" state.
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_command_prefab_sync_capture (LrgEditorCommand *self);

/**
 * lrg_editor_command_new_prefab_file:
 * @path: the prefab file
 * @before: (nullable): the file contents before, %NULL to leave the file
 *   alone on undo
 * @after: the file contents after
 *
 * Creates a command that writes @after to @path when applied and @before
 * when undone. Write failures are logged with g_warning().
 *
 * Returns: (transfer full): a new command
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_prefab_file (const gchar *path,
                                                       const gchar *before,
                                                       const gchar *after);

/**
 * lrg_editor_command_new_prefab_create:
 * @path: the prefab file the command created
 * @contents: what was written to it
 *
 * Creates a prefab-file command (%LRG_EDITOR_CMD_PREFAB_FILE) for a file
 * that did not exist before, as "Create Variant" makes: applying writes
 * @contents to @path, undoing removes the file again, unless it changed
 * since (then it is left alone with a warning).
 *
 * Returns: (transfer full): a new command
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_prefab_create (const gchar *path,
                                                         const gchar *contents);

/**
 * lrg_editor_command_add_override_mark:
 * @self: an #LrgEditorCommand
 * @owner: a linked prefab instance root
 * @key: an override key of @owner's link
 * @remove: %TRUE when the command removes @key, %FALSE when it adds it
 *
 * Attaches an override-set change to @self: applying the command adds
 * (or removes) @key, undoing it does the opposite. Merging commands keeps
 * the marks of both. #LrgEditor attaches marks automatically (see
 * lrg_editor_command_mark_prefab_overrides()).
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_command_add_override_mark (LrgEditorCommand *self,
                                           LrgNode          *owner,
                                           const gchar      *key,
                                           gboolean          remove);

/**
 * lrg_editor_command_mark_prefab_overrides:
 * @self: a command about to be executed (not yet applied)
 *
 * Derives the prefab override keys a not-yet-applied command will create
 * when it edits a node of a linked prefab instance, and attaches them as
 * marks: transforms, node property sets (`name`, `visible`, `is-2d`,
 * `visual`), visual params, tile edits, snapshot (inspector) edits, and
 * structural edits (`children/+<guid>` when a node is added under an
 * instance node, `children/-<source-guid>` when a mirrored node is
 * removed). Keys already covered are skipped, so marks only record real
 * changes of the override set. Groups recurse into their children.
 * Nodes outside linked instances cost one walk up the parent chain.
 *
 * Returns: the number of marks attached
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_command_mark_prefab_overrides (LrgEditorCommand *self);

/**
 * lrg_editor_command_get_n_override_marks:
 * @self: an #LrgEditorCommand
 *
 * Returns: the number of override marks attached to @self (not counting
 *   group children)
 *
 * Since: 0.8.1
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_command_get_n_override_marks (LrgEditorCommand *self);

/**
 * lrg_editor_command_new_timeline:
 * @level: the level owning the timeline
 * @before: (nullable): the timeline before the change, %NULL when adding
 * @after: (nullable): the timeline after the change, %NULL when removing
 * @label: (nullable): history label
 *
 * Creates a command that swaps one of @level's timelines between two
 * states. Both are copied; the live timeline is found by @before's name
 * (renames work) and replaced in place, keeping its index.
 *
 * Returns: (transfer full): a new command
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_timeline (LrgLevel    *level,
                                                    LrgTimeline *before,
                                                    LrgTimeline *after,
                                                    const gchar *label);

/* ==========================================================================
 * Operations
 * ========================================================================== */

/**
 * lrg_editor_command_get_node:
 * @self: an #LrgEditorCommand
 *
 * Returns: (transfer none) (nullable): the node the command acts on, if any
 *   (for groups, the subject of the first child that has one)
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_command_get_node (LrgEditorCommand *self);

/**
 * lrg_editor_command_get_label:
 * @self: an #LrgEditorCommand
 *
 * Returns: (transfer none) (nullable): the explicit label, if one was set
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_command_get_label (LrgEditorCommand *self);

/**
 * lrg_editor_command_set_label:
 * @self: an #LrgEditorCommand
 * @label: (nullable): a description overriding the generated one
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_command_set_label (LrgEditorCommand *self,
                                   const gchar      *label);

/**
 * lrg_editor_command_get_serial:
 * @self: an #LrgEditorCommand
 *
 * Every command gets a process-unique serial when created and a fresh one
 * whenever another command is merged into it. #LrgEditor compares serials
 * to decide whether the document differs from what was last saved.
 *
 * Returns: the modification serial
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_editor_command_get_serial (LrgEditorCommand *self);

/**
 * lrg_editor_command_get_kind:
 * @self: an #LrgEditorCommand
 *
 * Returns: the command kind
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommandKind lrg_editor_command_get_kind (LrgEditorCommand *self);

/**
 * lrg_editor_command_apply:
 * @self: an #LrgEditorCommand
 *
 * Applies (does/redoes) the command's mutation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_command_apply (LrgEditorCommand *self);

/**
 * lrg_editor_command_undo:
 * @self: an #LrgEditorCommand
 *
 * Reverses the command's mutation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_command_undo (LrgEditorCommand *self);

/**
 * lrg_editor_command_merge:
 * @self: an #LrgEditorCommand
 * @other: a newer command to fold into @self
 *
 * If @other is compatible with @self (same kind and target, for transform,
 * set-property and visual-param commands; same node and label for
 * node-state commands), folds @other's "after" state into @self so the pair
 * collapses into a single undo step. A successful merge refreshes the
 * serial of @self.
 *
 * Returns: %TRUE if @other was merged into @self
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_command_merge (LrgEditorCommand *self,
                                   LrgEditorCommand *other);

/**
 * lrg_editor_command_describe:
 * @self: an #LrgEditorCommand
 *
 * Returns: (transfer full): a short human-readable description
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_command_describe (LrgEditorCommand *self);

G_END_DECLS
