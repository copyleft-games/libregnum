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
    LRG_EDITOR_CMD_NODE_STATE
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
