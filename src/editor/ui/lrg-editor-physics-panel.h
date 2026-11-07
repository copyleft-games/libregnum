/* lrg-editor-physics-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPhysicsPanel: the lrgeditor "Physics" panel.
 *
 * Edits the `physics:` block (#LrgNodePhysics) of the selected node: the
 * body (kind, mass, collision layer, friction, restitution, damping,
 * gravity factor, CCD), its ordered list of collision shapes (add, remove,
 * reorder; kind, dimensions, offset, rotation, trigger, per-shape layer
 * and material overrides, "Fit to mesh") and its joints (kind, target
 * node, anchor, axis, limits, spring, motor, break force). While the game
 * runs in the editor it shows the live body: velocity, angular velocity,
 * sleeping and contacts.
 *
 * Every edit is one level command (a node-state snapshot): Ctrl+Z undoes
 * it, a slider drag is one step, and the level file stores the block. The
 * panel registers the physics.* IPC methods and the "physics.*" actions
 * itself.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-property-grid.h"
#include "../../level/lrg-node-physics.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_PHYSICS_PANEL_ID:
 *
 * The dock panel id of the physics panel ("physics").
 */
#define LRG_EDITOR_PHYSICS_PANEL_ID "physics"

#define LRG_TYPE_EDITOR_PHYSICS_PANEL (lrg_editor_physics_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorPhysicsPanel, lrg_editor_physics_panel, LRG, EDITOR_PHYSICS_PANEL, LrgContainer)

/**
 * lrg_editor_physics_panel_new:
 * @app: the editor application
 *
 * Creates the panel. It follows the primary selection, registers the
 * physics.* IPC methods on the app's server and the actions
 * "physics.show", "physics.add-body", "physics.remove-body",
 * "physics.add-shape" and "physics.fit". The window docks it with the id
 * %LRG_EDITOR_PHYSICS_PANEL_ID; embedders may dock it themselves with
 * lrg_editor_app_add_panel().
 *
 * Returns: (transfer full): a new #LrgEditorPhysicsPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPhysicsPanel * lrg_editor_physics_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_physics_panel_find:
 * @app: the editor application
 *
 * The panel created for @app (one per app).
 *
 * Returns: (transfer none) (nullable): the panel, or %NULL when none was
 *   created
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPhysicsPanel * lrg_editor_physics_panel_find (LrgEditorApp *app);

/**
 * lrg_editor_physics_panel_get_node:
 * @self: an #LrgEditorPhysicsPanel
 *
 * Returns: (transfer none) (nullable): the node the panel shows (the
 *   primary selection), %NULL without one
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_physics_panel_get_node (LrgEditorPhysicsPanel *self);

/**
 * lrg_editor_physics_panel_get_body_grid:
 * @self: an #LrgEditorPhysicsPanel
 *
 * The property grid of the body. Its rows are the #LrgNodePhysics
 * property names ("body-kind", "mass", "layer", "friction",
 * "restitution", "linear-damping", "angular-damping", "gravity-factor",
 * "ccd").
 *
 * Returns: (transfer none): the grid
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_physics_panel_get_body_grid (LrgEditorPhysicsPanel *self);

/**
 * lrg_editor_physics_panel_get_shape_grid:
 * @self: an #LrgEditorPhysicsPanel
 *
 * The property grid of the selected shape: "kind", then the dimensions
 * the kind uses ("half-extents" as a vector, "radius", "half-height",
 * "mesh"), "offset", "rotation" (degrees), "trigger", "layer",
 * "friction" and "restitution" (-1: the body's).
 *
 * Returns: (transfer none): the grid
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_physics_panel_get_shape_grid (LrgEditorPhysicsPanel *self);

/**
 * lrg_editor_physics_panel_get_joint_grid:
 * @self: an #LrgEditorPhysicsPanel
 *
 * The property grid of the selected joint: "kind", "target" (a node
 * path, picked from the level's nodes; empty for the world), "anchor",
 * "axis", "limits-enabled", "limit-min", "limit-max",
 * "spring-frequency", "spring-damping", "motor-velocity",
 * "motor-max-force" and "break-force".
 *
 * Returns: (transfer none): the grid
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_physics_panel_get_joint_grid (LrgEditorPhysicsPanel *self);

/**
 * lrg_editor_physics_panel_get_selected_shape:
 * @self: an #LrgEditorPhysicsPanel
 *
 * Returns: the index of the shape the shape grid edits (and the viewport
 *   highlights), -1 for none
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_physics_panel_get_selected_shape (LrgEditorPhysicsPanel *self);

/**
 * lrg_editor_physics_panel_set_selected_shape:
 * @self: an #LrgEditorPhysicsPanel
 * @index: a shape index of the shown node, or -1
 *
 * Selects the shape the shape grid edits. Out of range values select
 * nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_physics_panel_set_selected_shape (LrgEditorPhysicsPanel *self,
                                                  gint                   index);

/**
 * lrg_editor_physics_panel_get_selected_joint:
 * @self: an #LrgEditorPhysicsPanel
 *
 * Returns: the index of the joint the joint grid edits, -1 for none
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_physics_panel_get_selected_joint (LrgEditorPhysicsPanel *self);

/**
 * lrg_editor_physics_panel_set_selected_joint:
 * @self: an #LrgEditorPhysicsPanel
 * @index: a joint index of the shown node, or -1
 *
 * Selects the joint the joint grid edits.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_physics_panel_set_selected_joint (LrgEditorPhysicsPanel *self,
                                                  gint                   index);

/**
 * lrg_editor_physics_panel_add_body:
 * @self: an #LrgEditorPhysicsPanel
 * @node: (nullable): the node, %NULL for the shown one
 * @kind: the body kind
 * @fit: also add one shape fitted to the node's geometry (a sphere for a
 *   sphere primitive, a cylinder for cylinders and cones, a convex hull
 *   (dynamic) or triangle mesh (static, kinematic) for a mesh asset, a box
 *   otherwise)
 *
 * Gives @node a `physics:` block as one undoable command.
 *
 * Returns: %TRUE when a body was added (%FALSE when @node already has one)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_panel_add_body (LrgEditorPhysicsPanel *self,
                                            LrgNode               *node,
                                            LrgDynamicsBodyKind    kind,
                                            gboolean               fit);

/**
 * lrg_editor_physics_panel_remove_body:
 * @self: an #LrgEditorPhysicsPanel
 * @node: (nullable): the node, %NULL for the shown one
 *
 * Removes @node's `physics:` block (shapes and joints included) as one
 * undoable command.
 *
 * Returns: %TRUE when a body was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_panel_remove_body (LrgEditorPhysicsPanel *self,
                                               LrgNode               *node);

/**
 * lrg_editor_physics_panel_set_body_field:
 * @self: an #LrgEditorPhysicsPanel
 * @node: (nullable): the node, %NULL for the shown one
 * @name: an #LrgNodePhysics property name ('_' works for '-')
 * @value: the value, of the property's type or transformable to it
 *   (enums also as their nick strings)
 *
 * Sets one body field as an undoable command; a node without a body gets
 * a dynamic one in the same step.
 *
 * Returns: %TRUE when the value was applied
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_panel_set_body_field (LrgEditorPhysicsPanel *self,
                                                  LrgNode               *node,
                                                  const gchar           *name,
                                                  const GValue          *value);

/**
 * lrg_editor_physics_panel_add_shape:
 * @self: an #LrgEditorPhysicsPanel
 * @node: (nullable): the node, %NULL for the shown one
 * @kind: the shape kind
 * @fit: fit the new shape to the node's geometry
 *
 * Appends a shape (the component defaults, or fitted) to @node's body as
 * one undoable command, creating a dynamic body first when @node has
 * none, and selects it.
 *
 * Returns: the new shape's index, -1 on failure
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_physics_panel_add_shape (LrgEditorPhysicsPanel *self,
                                         LrgNode               *node,
                                         LrgCollider3DKind      kind,
                                         gboolean               fit);

/**
 * lrg_editor_physics_panel_remove_shape:
 * @self: an #LrgEditorPhysicsPanel
 * @node: (nullable): the node, %NULL for the shown one
 * @index: a shape index
 *
 * Removes one shape (undoable).
 *
 * Returns: %TRUE when the shape existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_panel_remove_shape (LrgEditorPhysicsPanel *self,
                                                LrgNode               *node,
                                                guint                  index);

/**
 * lrg_editor_physics_panel_move_shape:
 * @self: an #LrgEditorPhysicsPanel
 * @node: (nullable): the node, %NULL for the shown one
 * @index: a shape index
 * @to: its new index (clamped to the list)
 *
 * Reorders the shapes (undoable). The order is the order the runtime
 * builds the compound in.
 *
 * Returns: %TRUE when the order changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_panel_move_shape (LrgEditorPhysicsPanel *self,
                                              LrgNode               *node,
                                              guint                  index,
                                              guint                  to);

/**
 * lrg_editor_physics_panel_set_shape_field:
 * @self: an #LrgEditorPhysicsPanel
 * @node: (nullable): the node, %NULL for the shown one
 * @index: a shape index
 * @name: an #LrgNodePhysicsShape property name, or one of the vector
 *   fields "half-extents", "offset", "rotation" (a #GrlVector3, or a
 *   string "x,y,z")
 * @value: the value
 *
 * Sets one shape field as an undoable command.
 *
 * Returns: %TRUE when the value was applied
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_panel_set_shape_field (LrgEditorPhysicsPanel *self,
                                                   LrgNode               *node,
                                                   guint                  index,
                                                   const gchar           *name,
                                                   const GValue          *value);

/**
 * lrg_editor_physics_panel_fit_shape:
 * @self: an #LrgEditorPhysicsPanel
 * @node: (nullable): the node, %NULL for the shown one
 * @index: a shape index
 * @kind: the kind to fit as (box, sphere, capsule, cylinder, convex or
 *   mesh)
 *
 * Sets shape @index to @kind sized to the node's geometry: the model's
 * bounds for a mesh asset (read from the file when a window exists), the
 * primitive's extents otherwise (lrg_editor_node_get_local_bounds()).
 * Convex and mesh shapes take the node's own model and a zero offset.
 * One undoable command.
 *
 * Returns: %TRUE when the shape was fitted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_panel_fit_shape (LrgEditorPhysicsPanel *self,
                                             LrgNode               *node,
                                             guint                  index,
                                             LrgCollider3DKind      kind);

/**
 * lrg_editor_physics_panel_add_joint:
 * @self: an #LrgEditorPhysicsPanel
 * @node: (nullable): the node, %NULL for the shown one
 * @kind: the joint kind
 * @target: (nullable): the target node (a GUID or name path), %NULL for
 *   the world
 *
 * Appends a joint to @node's body as one undoable command (creating a
 * dynamic body first when needed) and selects it.
 *
 * Returns: the new joint's index, -1 on failure (an unknown target)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_physics_panel_add_joint (LrgEditorPhysicsPanel *self,
                                         LrgNode               *node,
                                         LrgJointKind           kind,
                                         const gchar           *target);

/**
 * lrg_editor_physics_panel_remove_joint:
 * @self: an #LrgEditorPhysicsPanel
 * @node: (nullable): the node, %NULL for the shown one
 * @index: a joint index
 *
 * Removes one joint (undoable).
 *
 * Returns: %TRUE when the joint existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_panel_remove_joint (LrgEditorPhysicsPanel *self,
                                                LrgNode               *node,
                                                guint                  index);

/**
 * lrg_editor_physics_panel_set_joint_field:
 * @self: an #LrgEditorPhysicsPanel
 * @node: (nullable): the node, %NULL for the shown one
 * @index: a joint index
 * @name: an #LrgNodePhysicsJoint property name, or "anchor" / "axis"
 *   (vectors); "target" takes a GUID or a name path ("" for the world)
 * @value: the value
 *
 * Sets one joint field as an undoable command.
 *
 * Returns: %TRUE when the value was applied
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_panel_set_joint_field (LrgEditorPhysicsPanel *self,
                                                   LrgNode               *node,
                                                   guint                  index,
                                                   const gchar           *name,
                                                   const GValue          *value);

/**
 * lrg_editor_physics_panel_describe_live:
 * @self: an #LrgEditorPhysicsPanel
 *
 * The live body state the panel shows while playing: velocity, angular
 * velocity, sleeping, contacts.
 *
 * Returns: (transfer full) (nullable): the text, %NULL when not playing
 *   or the shown node has no live body
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_physics_panel_describe_live (LrgEditorPhysicsPanel *self);

/**
 * lrg_editor_physics_fit_shape_to_bounds:
 * @shape: a shape description
 * @kind: the kind to fit as
 * @min: (array fixed-size=3): the local bounds' minimum corner
 * @max: (array fixed-size=3): the local bounds' maximum corner
 *
 * Sizes @shape to the box (@min, @max) in node units: a box of its half
 * extents, a sphere of its largest half extent, a capsule or cylinder
 * along Y (radius the larger of the X and Z half extents; a capsule's
 * half height leaves room for the caps); the offset is the box's centre.
 * Convex and mesh shapes only take @kind, a zero offset and the node's
 * own model. The rotation is reset.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_physics_fit_shape_to_bounds (LrgNodePhysicsShape *shape,
                                             LrgCollider3DKind    kind,
                                             const gfloat        *min,
                                             const gfloat        *max);

G_END_DECLS
