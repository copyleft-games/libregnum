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

G_END_DECLS
