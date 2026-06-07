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
    LRG_EDITOR_CMD_REPARENT
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
 * Returns: (transfer full): a new add-node command
 */
LRG_AVAILABLE_IN_ALL
LrgEditorCommand * lrg_editor_command_new_add_node (LrgLevel *level,
                                                    LrgNode  *node,
                                                    LrgNode  *parent);

/**
 * lrg_editor_command_new_delete_node:
 * @level: the level
 * @node: the node being deleted
 * @parent: the node's current parent
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

/* ==========================================================================
 * Operations
 * ========================================================================== */

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
 * If @other is compatible with @self (same kind and target, for transform and
 * set-property commands), folds @other's "after" state into @self so the pair
 * collapses into a single undo step.
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
