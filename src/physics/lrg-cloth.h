/* lrg-cloth.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCloth: position-based (XPBD) cloth on a triangle mesh or a
 * generated grid.  Distance (stretch and shear), bending (dihedral or
 * distance) and long-range attachment constraints, pins and bone
 * attachments with a maximum distance, collision against analytic
 * sphere / capsule / plane / box colliders, wind and aerodynamic drag,
 * substeps and optional self-collision.  Headless; rendering copies the
 * simulated positions and normals into a #GrlMesh vertex buffer.
 *
 * The solver follows Jolt Physics' soft body solver.
 * Portions derived from Jolt Physics (MIT), Copyright 2021 Jorrit Rouwe.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../debug/lrg-debug-draw.h"
#include "lrg-dynamics.h"

G_BEGIN_DECLS

/**
 * LRG_CLOTH_MAX_SUBSTEPS:
 *
 * The most substeps one lrg_cloth_step() runs (the #LrgCloth:substeps
 * property is clamped to 1 .. this).
 */
#define LRG_CLOTH_MAX_SUBSTEPS (64u)

/**
 * LRG_CLOTH_NO_BONE:
 *
 * Bone index meaning "not attached to a bone" (pins and colliders).
 */
#define LRG_CLOTH_NO_BONE (-1)

/**
 * LRG_CLOTH_ERROR:
 *
 * Error domain of #LrgCloth.
 */
#define LRG_CLOTH_ERROR (lrg_cloth_error_quark ())

/**
 * LrgClothError:
 * @LRG_CLOTH_ERROR_INVALID: an argument or the geometry is invalid
 *   (no triangles, an index out of range, a degenerate edge, too many
 *   vertices, a non-finite value)
 * @LRG_CLOTH_ERROR_NO_GPU: a graphics context is required (mesh creation)
 *
 * Errors of #LrgCloth.
 */
typedef enum
{
	LRG_CLOTH_ERROR_INVALID = 1,
	LRG_CLOTH_ERROR_NO_GPU  = 2
} LrgClothError;

/**
 * LrgClothBendMode:
 * @LRG_CLOTH_BEND_NONE: no bending constraints (cloth folds freely)
 * @LRG_CLOTH_BEND_DISTANCE: a distance constraint between the two
 *   vertices opposite every interior edge (cheap, stiffens in-plane too)
 * @LRG_CLOTH_BEND_DIHEDRAL: a dihedral angle constraint on every pair of
 *   triangles sharing an edge (bends only, keeps the rest angle)
 *
 * How #LrgCloth resists bending.
 */
typedef enum
{
	LRG_CLOTH_BEND_NONE     = 0,
	LRG_CLOTH_BEND_DISTANCE = 1,
	LRG_CLOTH_BEND_DIHEDRAL = 2
} LrgClothBendMode;

/**
 * LrgClothLraMode:
 * @LRG_CLOTH_LRA_NONE: no long-range attachments
 * @LRG_CLOTH_LRA_EUCLIDEAN: each free vertex stays within the straight
 *   rest distance of its closest pinned vertex
 * @LRG_CLOTH_LRA_GEODESIC: each free vertex stays within the rest
 *   distance along the mesh edges of its closest pinned vertex
 *
 * Long-range attachment (LRA) constraints: one per free vertex, tying it
 * to the closest hard-pinned vertex so a hanging cloth cannot stretch
 * past its rest length however few substeps run.
 */
typedef enum
{
	LRG_CLOTH_LRA_NONE      = 0,
	LRG_CLOTH_LRA_EUCLIDEAN = 1,
	LRG_CLOTH_LRA_GEODESIC  = 2
} LrgClothLraMode;

/**
 * LrgClothColliderKind:
 * @LRG_CLOTH_COLLIDER_SPHERE: a sphere (centre, radius)
 * @LRG_CLOTH_COLLIDER_CAPSULE: a capsule (two segment ends, radius)
 * @LRG_CLOTH_COLLIDER_PLANE: an infinite plane (normal, offset); the
 *   solid side is behind the normal
 * @LRG_CLOTH_COLLIDER_BOX: an oriented box (centre, half extents,
 *   rotation)
 *
 * Analytic collider shapes of #LrgCloth.
 */
typedef enum
{
	LRG_CLOTH_COLLIDER_SPHERE  = 0,
	LRG_CLOTH_COLLIDER_CAPSULE = 1,
	LRG_CLOTH_COLLIDER_PLANE   = 2,
	LRG_CLOTH_COLLIDER_BOX     = 3
} LrgClothColliderKind;

/**
 * LrgClothConstraintKind:
 * @LRG_CLOTH_CONSTRAINT_EDGE: distance constraints (stretch, shear and
 *   distance bending)
 * @LRG_CLOTH_CONSTRAINT_BEND: dihedral bend constraints
 * @LRG_CLOTH_CONSTRAINT_LRA: long-range attachments
 * @LRG_CLOTH_CONSTRAINT_ATTACHMENT: pins and bone attachments
 *
 * Constraint families, for lrg_cloth_get_constraint_count().
 */
typedef enum
{
	LRG_CLOTH_CONSTRAINT_EDGE       = 0,
	LRG_CLOTH_CONSTRAINT_BEND       = 1,
	LRG_CLOTH_CONSTRAINT_LRA        = 2,
	LRG_CLOTH_CONSTRAINT_ATTACHMENT = 3
} LrgClothConstraintKind;

LRG_AVAILABLE_IN_ALL
GQuark lrg_cloth_error_quark (void);

LRG_AVAILABLE_IN_ALL
GType lrg_cloth_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CLOTH_ERROR (lrg_cloth_error_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_cloth_bend_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CLOTH_BEND_MODE (lrg_cloth_bend_mode_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_cloth_lra_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CLOTH_LRA_MODE (lrg_cloth_lra_mode_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_cloth_collider_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CLOTH_COLLIDER_KIND (lrg_cloth_collider_kind_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_cloth_constraint_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CLOTH_CONSTRAINT_KIND (lrg_cloth_constraint_kind_get_type ())

#define LRG_TYPE_CLOTH (lrg_cloth_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCloth, lrg_cloth, LRG, CLOTH, GObject)

/* ==========================================================================
 * Construction and geometry
 * ========================================================================== */

/**
 * lrg_cloth_new:
 *
 * Creates an empty cloth.  Give it geometry with lrg_cloth_set_grid() or
 * lrg_cloth_set_mesh() before stepping it.
 *
 * Returns: (transfer full): a new #LrgCloth
 */
LRG_AVAILABLE_IN_ALL
LrgCloth * lrg_cloth_new (void);

/**
 * lrg_cloth_new_grid:
 * @columns: vertices along U (2 .. 1024)
 * @rows: vertices along V (2 .. 1024)
 * @width: size along local +X
 * @height: size along local -Y (the grid hangs down from its top edge)
 *
 * Creates a cloth from a @columns x @rows vertex grid in the local XY
 * plane: vertex (c, r) rests at (c * width / (columns - 1),
 * -r * height / (rows - 1), 0), row 0 is the top edge.  Shorthand for
 * lrg_cloth_new() + lrg_cloth_set_grid() with those axes.
 *
 * Returns: (transfer full) (nullable): a new #LrgCloth, or %NULL when
 *   the arguments are out of range
 */
LRG_AVAILABLE_IN_ALL
LrgCloth * lrg_cloth_new_grid (guint  columns,
                               guint  rows,
                               gfloat width,
                               gfloat height);

/**
 * lrg_cloth_set_grid:
 * @self: an #LrgCloth
 * @columns: vertices along @u_step (2 .. 1024)
 * @rows: vertices along @v_step (2 .. 1024)
 * @origin: (array fixed-size=3): local rest position of vertex (0, 0)
 * @u_step: (array fixed-size=3): offset between neighbouring columns
 * @v_step: (array fixed-size=3): offset between neighbouring rows
 * @error: (nullable): return location for an error
 *
 * Replaces the geometry with a regular grid: vertex index
 * r * @columns + c rests at @origin + c * @u_step + r * @v_step and has
 * texture coordinate (c / (columns - 1), r / (rows - 1)).  Quads are
 * split along alternating diagonals so the cloth has no preferred fold
 * direction.  Pins, attachments and velocities are cleared; colliders
 * and parameters are kept.
 *
 * Returns: %TRUE on success; on failure the cloth is unchanged
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_set_grid (LrgCloth      *self,
                             guint          columns,
                             guint          rows,
                             const gfloat  *origin,
                             const gfloat  *u_step,
                             const gfloat  *v_step,
                             GError       **error);

/**
 * lrg_cloth_set_mesh:
 * @self: an #LrgCloth
 * @n_vertices: number of vertices (3 .. 65535)
 * @positions: (array) (element-type gfloat): 3 floats per vertex, the local
 *   rest positions (the length is 3 * @n_vertices)
 * @texcoords: (array) (element-type gfloat) (nullable): 2 floats per vertex, or %NULL (zero UVs)
 * @n_indices: number of indices (a multiple of 3)
 * @indices: (array length=n_indices): triangle list
 * @error: (nullable): return location for an error
 *
 * Replaces the geometry with an arbitrary triangle mesh.  Vertices must
 * be welded: triangles that should hang together have to share vertex
 * indices (a mesh split at UV seams becomes separate cloth pieces).
 * Rest lengths and angles come from @positions.  Degenerate triangles
 * (repeated indices) are rejected.  Pins, attachments and velocities
 * are cleared.
 *
 * Returns: %TRUE on success; on failure the cloth is unchanged
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_set_mesh (LrgCloth       *self,
                             guint           n_vertices,
                             const gfloat   *positions,
                             const gfloat   *texcoords,
                             guint           n_indices,
                             const guint32  *indices,
                             GError        **error);

/**
 * lrg_cloth_get_vertex_count:
 * @self: an #LrgCloth
 *
 * Returns: the number of simulated vertices (particles)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cloth_get_vertex_count (LrgCloth *self);

/**
 * lrg_cloth_get_triangle_count:
 * @self: an #LrgCloth
 *
 * Returns: the number of triangles
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cloth_get_triangle_count (LrgCloth *self);

/**
 * lrg_cloth_get_grid_index:
 * @self: an #LrgCloth
 * @column: grid column
 * @row: grid row
 *
 * Maps grid coordinates to a vertex index.  Only meaningful for cloth
 * made by lrg_cloth_set_grid() or lrg_cloth_new_grid().
 *
 * Returns: the vertex index, or %G_MAXUINT when the cloth is not a grid
 *   or the coordinates are outside it
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cloth_get_grid_index (LrgCloth *self,
                                guint     column,
                                guint     row);

/**
 * lrg_cloth_get_grid_size:
 * @self: an #LrgCloth
 * @columns: (out) (optional): grid columns (0 for a mesh)
 * @rows: (out) (optional): grid rows (0 for a mesh)
 *
 * Returns: %TRUE when the cloth is a grid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_get_grid_size (LrgCloth *self,
                                  guint    *columns,
                                  guint    *rows);

/* ==========================================================================
 * Placement
 * ========================================================================== */

/**
 * lrg_cloth_set_transform:
 * @self: an #LrgCloth
 * @matrix: (array fixed-size=16) (nullable): column-major local-to-world
 *   matrix, %NULL for identity
 *
 * Places the cloth's local space in the world.  Particles simulate in
 * world space; the transform moves what is expressed locally: pins,
 * bone matrices (lrg_cloth_set_bone_matrices()), bone-attached
 * colliders and lrg_cloth_reset().  Moving the transform between steps
 * moves pinned vertices smoothly across the substeps of the next step
 * and the free ones follow through the constraints (a waving flagpole).
 * Call lrg_cloth_reset() after the first transform to start the cloth
 * at its rest shape there.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_set_transform (LrgCloth     *self,
                              const gfloat *matrix);

/**
 * lrg_cloth_get_transform:
 * @self: an #LrgCloth
 * @out_matrix: (out caller-allocates) (array fixed-size=16): the matrix
 *
 * Reads the local-to-world transform.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_get_transform (LrgCloth *self,
                              gfloat   *out_matrix);

/**
 * lrg_cloth_reset:
 * @self: an #LrgCloth
 *
 * Puts every particle back at its rest position under the current
 * transform (and bone matrices for bone-attached particles), with zero
 * velocity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_reset (LrgCloth *self);

/* ==========================================================================
 * Pins and bone attachments
 * ========================================================================== */

/**
 * lrg_cloth_pin:
 * @self: an #LrgCloth
 * @index: vertex index
 *
 * Pins vertex @index at its local rest position: it no longer moves on
 * its own and follows the transform.  Same as lrg_cloth_pin_at() with
 * the rest position and a max distance of 0.
 *
 * Returns: %TRUE when @index is a vertex
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_pin (LrgCloth *self,
                        guint     index);

/**
 * lrg_cloth_pin_at:
 * @self: an #LrgCloth
 * @index: vertex index
 * @local_position: (array fixed-size=3) (nullable): target in the
 *   cloth's local space, %NULL for the rest position
 * @max_distance: 0 for a hard pin (the vertex becomes kinematic); more
 *   lets the vertex move freely within that distance of the target
 *   (a soft tether, Jolt's skinned constraint "max distance")
 *
 * Attaches vertex @index to a fixed point of the cloth's local space.
 * Replaces any earlier pin or bone attachment of the vertex.
 *
 * Returns: %TRUE when @index is a vertex and @max_distance is finite and
 *   not negative
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_pin_at (LrgCloth     *self,
                           guint         index,
                           const gfloat *local_position,
                           gfloat        max_distance);

/**
 * lrg_cloth_attach_to_bone:
 * @self: an #LrgCloth
 * @index: vertex index
 * @bone: bone index into the matrices of lrg_cloth_set_bone_matrices()
 * @bone_offset: (array fixed-size=3) (nullable): the target in the
 *   bone's space; %NULL computes it from the vertex's rest position and
 *   the current bone matrix (so the vertex keeps its place on the bone)
 * @max_distance: 0 for a hard attachment, more for a soft tether
 *
 * Attaches vertex @index to a bone: each substep the target is the bone
 * matrix (interpolated across the step) times @bone_offset, under the
 * cloth's transform.  Replaces any earlier pin of the vertex.  With
 * %NULL @bone_offset the bone matrix must have been set first.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_attach_to_bone (LrgCloth     *self,
                                   guint         index,
                                   gint          bone,
                                   const gfloat *bone_offset,
                                   gfloat        max_distance);

/**
 * lrg_cloth_unpin:
 * @self: an #LrgCloth
 * @index: vertex index
 *
 * Removes the pin or bone attachment of vertex @index (it becomes a
 * free particle with its area mass).
 *
 * Returns: %TRUE when the vertex had one
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_unpin (LrgCloth *self,
                          guint     index);

/**
 * lrg_cloth_unpin_all:
 * @self: an #LrgCloth
 *
 * Removes every pin and bone attachment.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_unpin_all (LrgCloth *self);

/**
 * lrg_cloth_is_pinned:
 * @self: an #LrgCloth
 * @index: vertex index
 *
 * Returns: %TRUE when vertex @index has a pin or bone attachment of any
 *   max distance
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_is_pinned (LrgCloth *self,
                              guint     index);

/**
 * lrg_cloth_get_attachment_bone:
 * @self: an #LrgCloth
 * @index: vertex index
 *
 * Returns: the bone vertex @index is attached to, or
 *   %LRG_CLOTH_NO_BONE (not attached, or pinned to the local space)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_cloth_get_attachment_bone (LrgCloth *self,
                                    guint     index);

/**
 * lrg_cloth_set_bone_matrices:
 * @self: an #LrgCloth
 * @matrices: (array) (element-type gfloat) (nullable):
 *   16 * @n_bones floats, column-major bone matrices in the cloth's local
 *   space (model space of a skeleton when the transform is the entity's
 *   world matrix)
 * @n_bones: number of bones (0 .. 1024)
 *
 * Sets the current bone matrices.  The previous ones are kept so that
 * the next lrg_cloth_step() moves bone targets smoothly from the old to
 * the new pose across its substeps.  Setting a different bone count
 * drops the history (the next step starts at the new pose).
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_set_bone_matrices (LrgCloth     *self,
                                  const gfloat *matrices,
                                  guint         n_bones);

/**
 * lrg_cloth_set_bone_matrices_from_skeleton:
 * @self: an #LrgCloth
 * @skeleton: an #LrgSkeleton whose world poses are current
 *   (lrg_skeleton_calculate_world_poses())
 *
 * Calls lrg_cloth_set_bone_matrices() with lrg_bone_pose_to_matrix()
 * of every bone's world pose, bone index i to matrix i.
 *
 * Returns: the number of bones set
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cloth_set_bone_matrices_from_skeleton (LrgCloth    *self,
                                                 LrgSkeleton *skeleton);

/**
 * lrg_cloth_get_bone_count:
 * @self: an #LrgCloth
 *
 * Returns: the number of bone matrices set
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cloth_get_bone_count (LrgCloth *self);

/* ==========================================================================
 * Colliders
 * ========================================================================== */

/**
 * lrg_cloth_add_sphere:
 * @self: an #LrgCloth
 * @center: (array fixed-size=3): centre
 * @radius: radius (> 0)
 *
 * Adds a sphere collider in world space (or in its bone's space after
 * lrg_cloth_set_collider_bone()).
 *
 * Returns: the collider id (stable until removed), or %G_MAXUINT on bad
 *   arguments
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cloth_add_sphere (LrgCloth     *self,
                            const gfloat *center,
                            gfloat        radius);

/**
 * lrg_cloth_add_capsule:
 * @self: an #LrgCloth
 * @a: (array fixed-size=3): centre of one end cap
 * @b: (array fixed-size=3): centre of the other end cap
 * @radius: radius (> 0)
 *
 * Adds a capsule collider (the swept sphere of segment @a .. @b).
 *
 * Returns: the collider id, or %G_MAXUINT on bad arguments
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cloth_add_capsule (LrgCloth     *self,
                             const gfloat *a,
                             const gfloat *b,
                             gfloat        radius);

/**
 * lrg_cloth_add_plane:
 * @self: an #LrgCloth
 * @normal: (array fixed-size=3): plane normal (normalised for you)
 * @offset: plane offset: points x with dot (normal, x) = @offset lie on
 *   the plane
 *
 * Adds an infinite plane collider; particles are kept on the side the
 * normal points to (a floor is normal (0, 1, 0), offset 0).
 *
 * Returns: the collider id, or %G_MAXUINT on a zero normal
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cloth_add_plane (LrgCloth     *self,
                           const gfloat *normal,
                           gfloat        offset);

/**
 * lrg_cloth_add_box:
 * @self: an #LrgCloth
 * @center: (array fixed-size=3): centre
 * @half_extents: (array fixed-size=3): half sizes along the box axes
 *   (each > 0)
 * @rotation: (array fixed-size=4) (nullable): orientation quaternion
 *   (x, y, z, w), %NULL for axis aligned
 *
 * Adds an oriented box collider.
 *
 * Returns: the collider id, or %G_MAXUINT on bad arguments
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cloth_add_box (LrgCloth     *self,
                         const gfloat *center,
                         const gfloat *half_extents,
                         const gfloat *rotation);

/**
 * lrg_cloth_set_collider_transform:
 * @self: an #LrgCloth
 * @id: collider id
 * @matrix: (array fixed-size=16) (nullable): column-major matrix applied
 *   to the collider's geometry, %NULL for identity
 *
 * Moves a collider without re-adding it: the geometry given at creation
 * is transformed by @matrix (rigid transforms only; scale is not
 * supported).  Use it to follow a rigid body or a character's limb.
 *
 * Returns: %TRUE when @id is a collider
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_set_collider_transform (LrgCloth     *self,
                                           guint         id,
                                           const gfloat *matrix);

/**
 * lrg_cloth_set_collider_bone:
 * @self: an #LrgCloth
 * @id: collider id
 * @bone: bone index, or %LRG_CLOTH_NO_BONE for world space
 *
 * Makes the collider's geometry (after its collider transform) relative
 * to a bone: world = cloth transform * bone matrix * collider transform
 * * geometry.  A capsule on a thigh bone keeps a skirt off the leg.
 *
 * Returns: %TRUE when @id is a collider
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_set_collider_bone (LrgCloth *self,
                                      guint     id,
                                      gint      bone);

/**
 * lrg_cloth_remove_collider:
 * @self: an #LrgCloth
 * @id: collider id
 *
 * Returns: %TRUE when a collider was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_remove_collider (LrgCloth *self,
                                    guint     id);

/**
 * lrg_cloth_clear_colliders:
 * @self: an #LrgCloth
 *
 * Removes every collider.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_clear_colliders (LrgCloth *self);

/**
 * lrg_cloth_get_collider_count:
 * @self: an #LrgCloth
 *
 * Returns: the number of colliders
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cloth_get_collider_count (LrgCloth *self);

/**
 * lrg_cloth_get_collider_kind:
 * @self: an #LrgCloth
 * @id: collider id
 * @kind: (out): the shape
 *
 * Returns: %TRUE when @id is a collider
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_get_collider_kind (LrgCloth             *self,
                                      guint                 id,
                                      LrgClothColliderKind *kind);

/**
 * lrg_cloth_set_dynamics_world:
 * @self: an #LrgCloth
 * @world: (nullable): an #LrgDynamicsWorld to collide with, %NULL for none
 * @layer_mask: object layers of the bodies the cloth collides with
 *
 * Makes the particles collide with the world's bodies (one way: the
 * bodies do not feel the cloth).  Once per lrg_cloth_step() the bodies
 * whose AABB overlaps the cloth's swept bounds are found with
 * lrg_dynamics_world_overlap_aabb(); every particle inside a body's AABB
 * collides a sphere of #LrgCloth:thickness with the body's shape
 * (lrg_collide_shapes_all(), speculative by the distance the particle
 * can travel this step) and keeps the deepest contact as a plane that
 * the substeps enforce, with friction (Jolt's collision planes).
 * Sensors and infinite plane bodies are skipped: add a plane collider
 * for a floor.  The world is referenced.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_set_dynamics_world (LrgCloth         *self,
                                   LrgDynamicsWorld *world,
                                   guint32           layer_mask);

/**
 * lrg_cloth_get_dynamics_world:
 * @self: an #LrgCloth
 *
 * Returns: (transfer none) (nullable): the world the cloth collides with
 */
LRG_AVAILABLE_IN_ALL
LrgDynamicsWorld * lrg_cloth_get_dynamics_world (LrgCloth *self);

/* ==========================================================================
 * Parameters (also GObject properties)
 * ========================================================================== */

/**
 * lrg_cloth_set_gravity:
 * @self: an #LrgCloth
 * @gravity: (array fixed-size=3): acceleration in m/s² (default
 *   0, -9.81, 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_set_gravity (LrgCloth     *self,
                            const gfloat *gravity);

/**
 * lrg_cloth_get_gravity:
 * @self: an #LrgCloth
 * @out: (out caller-allocates) (array fixed-size=3): the gravity
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_get_gravity (LrgCloth *self,
                            gfloat   *out);

/**
 * lrg_cloth_set_wind:
 * @self: an #LrgCloth
 * @velocity: (array fixed-size=3): wind velocity in m/s (default zero)
 *
 * Sets the air velocity the aerodynamic drag (#LrgCloth:drag) works
 * against: each triangle feels a force along its normal proportional to
 * its area and the square of the normal relative air speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_set_wind (LrgCloth     *self,
                         const gfloat *velocity);

/**
 * lrg_cloth_get_wind:
 * @self: an #LrgCloth
 * @out: (out caller-allocates) (array fixed-size=3): the wind velocity
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_get_wind (LrgCloth *self,
                         gfloat   *out);

/**
 * lrg_cloth_set_substeps:
 * @self: an #LrgCloth
 * @substeps: substeps per lrg_cloth_step(), clamped to
 *   1 .. %LRG_CLOTH_MAX_SUBSTEPS
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_set_substeps (LrgCloth *self,
                             guint     substeps);

/**
 * lrg_cloth_get_substeps:
 * @self: an #LrgCloth
 *
 * Returns: the substeps per step
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cloth_get_substeps (LrgCloth *self);

/**
 * lrg_cloth_set_compliance:
 * @self: an #LrgCloth
 * @stretch: relative compliance of the structural edges
 * @shear: relative compliance of the quad diagonals
 * @bend: relative compliance of the bend constraints
 *
 * Sets the three relative compliances at once (a projection removes 1 / (1 + c) of a violation; see #LrgCloth:stretch-compliance) (each clamped to >= 0; 0 is
 * rigid).  Changing them rebuilds the constraints on the next step.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_set_compliance (LrgCloth *self,
                               gfloat    stretch,
                               gfloat    shear,
                               gfloat    bend);

/**
 * lrg_cloth_get_max_stretch:
 * @self: an #LrgCloth
 *
 * Measures the current strain: the largest ratio of current to rest
 * length over the structural edges (1 is unstretched).
 *
 * Returns: the largest edge stretch ratio, 1 for a cloth without edges
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_cloth_get_max_stretch (LrgCloth *self);

/**
 * lrg_cloth_get_constraint_count:
 * @self: an #LrgCloth
 * @kind: the constraint family
 *
 * Counts constraints (building them first when the topology, pins or
 * modes changed since the last step).
 *
 * Returns: the number of constraints of @kind
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cloth_get_constraint_count (LrgCloth               *self,
                                      LrgClothConstraintKind  kind);

/* ==========================================================================
 * Simulation
 * ========================================================================== */

/**
 * lrg_cloth_step:
 * @self: an #LrgCloth
 * @dt: seconds to advance (> 0, at most 0.1; longer steps are clamped)
 *
 * Advances the cloth by @dt in #LrgCloth:substeps equal XPBD substeps.
 * Each substep: integrate gravity, wind and drag; move kinematic targets
 * (interpolated transform and bones); solve bend, attachment, edge and
 * LRA constraints in a fixed order; optional self-collision; collide
 * with the colliders (push out, friction); derive velocities.  The
 * result depends only on the inputs and the call sequence: two cloths
 * fed the same way stay bit-identical.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_step (LrgCloth *self,
                     gfloat    dt);

/**
 * lrg_cloth_get_positions:
 * @self: an #LrgCloth
 * @n_floats: (out) (optional): length of the array (3 * vertices)
 *
 * Returns: (array length=n_floats) (transfer none) (nullable): world
 *   positions, 3 floats per vertex; valid until the geometry changes
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_cloth_get_positions (LrgCloth *self,
                                        guint    *n_floats);

/**
 * lrg_cloth_get_normals:
 * @self: an #LrgCloth
 * @n_floats: (out) (optional): length of the array (3 * vertices)
 *
 * Area-weighted smooth vertex normals of the current shape (recomputed
 * lazily after a step).
 *
 * Returns: (array length=n_floats) (transfer none) (nullable): unit
 *   normals, 3 floats per vertex
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_cloth_get_normals (LrgCloth *self,
                                      guint    *n_floats);

/**
 * lrg_cloth_get_velocities:
 * @self: an #LrgCloth
 * @n_floats: (out) (optional): length of the array (3 * vertices)
 *
 * Returns: (array length=n_floats) (transfer none) (nullable): particle
 *   velocities in m/s
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_cloth_get_velocities (LrgCloth *self,
                                         guint    *n_floats);

/**
 * lrg_cloth_get_texcoords:
 * @self: an #LrgCloth
 * @n_floats: (out) (optional): length of the array (2 * vertices)
 *
 * Returns: (array length=n_floats) (transfer none) (nullable): texture
 *   coordinates, 2 floats per vertex
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_cloth_get_texcoords (LrgCloth *self,
                                        guint    *n_floats);

/**
 * lrg_cloth_get_indices:
 * @self: an #LrgCloth
 * @n_indices: (out) (optional): number of indices
 *
 * Returns: (array length=n_indices) (transfer none) (nullable): the
 *   triangle list
 */
LRG_AVAILABLE_IN_ALL
const guint32 * lrg_cloth_get_indices (LrgCloth *self,
                                       guint    *n_indices);

/**
 * lrg_cloth_get_rest_positions:
 * @self: an #LrgCloth
 * @n_floats: (out) (optional): length of the array (3 * vertices)
 *
 * Returns: (array length=n_floats) (transfer none) (nullable): local
 *   rest positions
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_cloth_get_rest_positions (LrgCloth *self,
                                             guint    *n_floats);

/**
 * lrg_cloth_get_position:
 * @self: an #LrgCloth
 * @index: vertex index
 * @out: (out caller-allocates) (array fixed-size=3): world position
 *
 * Returns: %TRUE when @index is a vertex
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_get_position (LrgCloth *self,
                                 guint     index,
                                 gfloat   *out);

/**
 * lrg_cloth_set_position:
 * @self: an #LrgCloth
 * @index: vertex index
 * @position: (array fixed-size=3): new world position
 * @keep_velocity: %FALSE zeroes the vertex's velocity
 *
 * Teleports one particle (grabbing cloth with the mouse, scripted
 * placement).  A hard-pinned vertex snaps back to its target next step.
 *
 * Returns: %TRUE when @index is a vertex
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_set_position (LrgCloth     *self,
                                 guint         index,
                                 const gfloat *position,
                                 gboolean      keep_velocity);

/**
 * lrg_cloth_apply_impulse:
 * @self: an #LrgCloth
 * @index: vertex index
 * @impulse: (array fixed-size=3): impulse in N·s (velocity change times
 *   the particle's mass)
 *
 * Kicks one free particle; pinned particles ignore it.
 *
 * Returns: %TRUE when @index is a vertex
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_apply_impulse (LrgCloth     *self,
                                  guint         index,
                                  const gfloat *impulse);

/**
 * lrg_cloth_get_bounds:
 * @self: an #LrgCloth
 * @out_min: (out caller-allocates) (array fixed-size=3): lowest corner
 * @out_max: (out caller-allocates) (array fixed-size=3): highest corner
 *
 * World AABB of the particles.
 *
 * Returns: %FALSE for a cloth without vertices
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_get_bounds (LrgCloth *self,
                               gfloat   *out_min,
                               gfloat   *out_max);

/**
 * lrg_cloth_get_kinetic_energy:
 * @self: an #LrgCloth
 *
 * Returns: the sum of ½ m v² over the free particles, in joules
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_cloth_get_kinetic_energy (LrgCloth *self);

/**
 * lrg_cloth_get_total_mass:
 * @self: an #LrgCloth
 *
 * Returns: the mass of the free particles in kg (pinned ones are
 *   kinematic and do not count)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_cloth_get_total_mass (LrgCloth *self);

/**
 * lrg_cloth_get_contact_count:
 * @self: an #LrgCloth
 *
 * Returns: how many particles touched a collider in the last step
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cloth_get_contact_count (LrgCloth *self);

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/**
 * lrg_cloth_create_mesh:
 * @self: an #LrgCloth
 * @error: (nullable): return location for an error
 *
 * Creates a #GrlMesh with the cloth's vertices, normals, texture
 * coordinates and triangles, uploaded with dynamic vertex buffers so
 * that lrg_cloth_update_mesh() can stream new positions every frame.
 * Needs a graphics context (%LRG_CLOTH_ERROR_NO_GPU without one) and at
 * most 65535 vertices.  Positions are in world space: draw the mesh with
 * an identity model matrix.
 *
 * Returns: (transfer full) (nullable): a new mesh, or %NULL
 */
LRG_AVAILABLE_IN_ALL
GrlMesh * lrg_cloth_create_mesh (LrgCloth  *self,
                                 GError   **error);

/**
 * lrg_cloth_update_mesh:
 * @self: an #LrgCloth
 * @mesh: a mesh from lrg_cloth_create_mesh() (or any mesh with the same
 *   vertex count)
 *
 * Copies the current positions and normals into @mesh's CPU arrays and,
 * when it is uploaded and a graphics context exists, into its GPU vertex
 * buffers (UpdateMeshBuffer()).
 *
 * Returns: %TRUE when the vertex counts match
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cloth_update_mesh (LrgCloth *self,
                                GrlMesh  *mesh);

/**
 * lrg_cloth_draw:
 * @self: an #LrgCloth
 * @color: (nullable): tint (%NULL: white)
 * @texture: (nullable): albedo texture, or %NULL
 *
 * Draws the cloth inside a 3D mode with raylib's default material,
 * double sided.  The cloth keeps its own mesh (created on first draw,
 * updated each draw).  Does nothing without a graphics context.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_draw (LrgCloth       *self,
                     const GrlColor *color,
                     GrlTexture     *texture);

/**
 * lrg_cloth_release_mesh:
 * @self: an #LrgCloth
 *
 * Drops the mesh lrg_cloth_draw() keeps (call before closing the
 * window).
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_release_mesh (LrgCloth *self);

/**
 * lrg_cloth_debug_draw:
 * @self: an #LrgCloth
 * @draw: an #LrgDebugDraw
 * @draw_colliders: also draw the colliders
 *
 * Queues the cloth's edges (green, red when stretched past 10 %), its
 * pinned vertices (yellow points) and, with @draw_colliders, the
 * colliders (cyan) for one frame.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cloth_debug_draw (LrgCloth     *self,
                           LrgDebugDraw *draw,
                           gboolean      draw_colliders);

G_END_DECLS
