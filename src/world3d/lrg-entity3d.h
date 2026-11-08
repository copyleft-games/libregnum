/* lrg-entity3d.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEntity3D: the runtime 3D scene entity.
 *
 * An #LrgEntity3D is a node of the 3D runtime world: it has a stable GUID,
 * a name, active/visible/static flags, a render layer, a set of string
 * tags, a local transform (position, quaternion rotation, scale, plus the
 * authored Euler rotation kept for editor round trips), an ordered list of
 * children, and an ordered list of #LrgComponent instances that give it
 * behaviour (mesh renderer, light, camera, script, ...).
 *
 * World matrices are cached and recomputed lazily: changing a local
 * transform bumps a version and a global epoch, and the next read of a
 * world matrix (on this entity or any descendant) walks up only as far as
 * the first ancestor that is still valid.
 *
 * Math conventions are those of lrg-math3d.h: column-major 4x4 matrices
 * (m[col * 4 + row]), quaternions (x, y, z, w), Euler XYZ radians with
 * R = Rz * Ry * Rx, Y up.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/* LrgWorld3D is declared in lrg-world3d.h, which includes this header. */
typedef struct _LrgWorld3D LrgWorld3D;

#define LRG_TYPE_ENTITY3D (lrg_entity3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgEntity3D, lrg_entity3d, LRG, ENTITY3D, GObject)

/**
 * LrgEntity3DClass:
 * @parent_class: the parent class
 * @transform_changed: default handler of #LrgEntity3D::transform-changed
 * @child_added: default handler of #LrgEntity3D::child-added
 * @child_removed: default handler of #LrgEntity3D::child-removed
 *
 * The class structure of #LrgEntity3D.  Subclasses may override the
 * signal default handlers; chain up is not required (the base handlers
 * are empty).
 */
struct _LrgEntity3DClass
{
	GObjectClass parent_class;

	void (*transform_changed) (LrgEntity3D *self);
	void (*child_added)       (LrgEntity3D *self,
	                           LrgEntity3D *child);
	void (*child_removed)     (LrgEntity3D *self,
	                           LrgEntity3D *child);

	/*< private >*/
	gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_entity3d_new:
 *
 * Creates an entity with no name, no GUID, the identity transform, active,
 * visible, non-static, on layer %LRG_RENDER_LAYERS_DEFAULT.
 *
 * Returns: (transfer full): a new #LrgEntity3D
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_entity3d_new (void);

/**
 * lrg_entity3d_new_with_name:
 * @name: (nullable): the entity name
 *
 * Creates an entity like lrg_entity3d_new() with its name set.
 *
 * Returns: (transfer full): a new #LrgEntity3D
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_entity3d_new_with_name (const gchar *name);

/* ==========================================================================
 * Identity and flags
 * ========================================================================== */

/**
 * lrg_entity3d_get_guid:
 * @self: an #LrgEntity3D
 *
 * Gets the entity GUID (the #LrgNode GUID when the entity was instantiated
 * from a level).
 *
 * Returns: (transfer none) (nullable): the GUID, or %NULL when unset
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_entity3d_get_guid (LrgEntity3D *self);

/**
 * lrg_entity3d_set_guid:
 * @self: an #LrgEntity3D
 * @guid: a non-empty GUID string
 *
 * Sets the GUID.  A GUID can be set only once: setting it again to a
 * different value fails with a warning and returns %FALSE (setting the
 * same value again is a no-op that returns %TRUE).  When the entity is in
 * an #LrgWorld3D, the world's GUID map is updated.
 *
 * Returns: %TRUE if the entity now has @guid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_set_guid (LrgEntity3D *self,
                                const gchar *guid);

/**
 * lrg_entity3d_ensure_guid:
 * @self: an #LrgEntity3D
 *
 * Gives the entity a random v4 UUID if it has no GUID yet.
 *
 * Returns: (transfer none): the (possibly new) GUID
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_entity3d_ensure_guid (LrgEntity3D *self);

/**
 * lrg_entity3d_get_name:
 * @self: an #LrgEntity3D
 *
 * Returns: (transfer none) (nullable): the entity name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_entity3d_get_name (LrgEntity3D *self);

/**
 * lrg_entity3d_set_name:
 * @self: an #LrgEntity3D
 * @name: (nullable): the new name
 *
 * Sets the entity name.  Names need not be unique; they are used by
 * lrg_entity3d_find_child(), lrg_entity3d_find_by_path() and
 * lrg_world3d_find_by_name().
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_name (LrgEntity3D *self,
                            const gchar *name);

/**
 * lrg_entity3d_get_active:
 * @self: an #LrgEntity3D
 *
 * Gets the entity's own active flag (see
 * lrg_entity3d_is_active_in_hierarchy() for the effective state).
 *
 * Returns: %TRUE if the entity itself is active
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_get_active (LrgEntity3D *self);

/**
 * lrg_entity3d_set_active:
 * @self: an #LrgEntity3D
 * @active: whether the entity is active
 *
 * Inactive entities (and all their descendants) are skipped by every
 * #LrgWorld3D phase: update, fixed update, late update and render
 * collection.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_active (LrgEntity3D *self,
                              gboolean     active);

/**
 * lrg_entity3d_is_active_in_hierarchy:
 * @self: an #LrgEntity3D
 *
 * Returns: %TRUE if this entity and every ancestor are active
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_is_active_in_hierarchy (LrgEntity3D *self);

/**
 * lrg_entity3d_get_visible:
 * @self: an #LrgEntity3D
 *
 * Returns: %TRUE if the entity itself is visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_get_visible (LrgEntity3D *self);

/**
 * lrg_entity3d_set_visible:
 * @self: an #LrgEntity3D
 * @visible: whether the entity is drawn
 *
 * Invisible entities (and their descendants) still update but are skipped
 * by lrg_world3d_collect_render().
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_visible (LrgEntity3D *self,
                               gboolean     visible);

/**
 * lrg_entity3d_is_visible_in_hierarchy:
 * @self: an #LrgEntity3D
 *
 * Returns: %TRUE if this entity and every ancestor are visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_is_visible_in_hierarchy (LrgEntity3D *self);

/**
 * lrg_entity3d_get_static:
 * @self: an #LrgEntity3D
 *
 * Returns: %TRUE if the entity is flagged static
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_get_static (LrgEntity3D *self);

/**
 * lrg_entity3d_set_static:
 * @self: an #LrgEntity3D
 * @is_static: whether the entity never moves at runtime
 *
 * Marks the entity as static.  The flag is a hint for batching, light
 * baking and navigation; the transform setters still work.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_static (LrgEntity3D *self,
                              gboolean     is_static);

/**
 * lrg_entity3d_get_layer:
 * @self: an #LrgEntity3D
 *
 * Returns: the render layer index (0..31)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_entity3d_get_layer (LrgEntity3D *self);

/**
 * lrg_entity3d_set_layer:
 * @self: an #LrgEntity3D
 * @layer: a render layer index (0..31, see %LRG_RENDER_LAYERS_COUNT)
 *
 * Sets the render layer.  An entity is collected for a camera only when
 * LRG_RENDER_LAYERS_BIT(layer) is in the camera's cull mask.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_layer (LrgEntity3D *self,
                             guint        layer);

/**
 * lrg_entity3d_get_layer_mask:
 * @self: an #LrgEntity3D
 *
 * Returns: LRG_RENDER_LAYERS_BIT() of the entity's layer
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_entity3d_get_layer_mask (LrgEntity3D *self);

/* ==========================================================================
 * Tags
 * ========================================================================== */

/**
 * lrg_entity3d_add_tag:
 * @self: an #LrgEntity3D
 * @tag: a tag
 *
 * Adds a tag (tags are a set: adding twice is a no-op).
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_add_tag (LrgEntity3D *self,
                           const gchar *tag);

/**
 * lrg_entity3d_remove_tag:
 * @self: an #LrgEntity3D
 * @tag: a tag
 *
 * Returns: %TRUE if the tag was present
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_remove_tag (LrgEntity3D *self,
                                  const gchar *tag);

/**
 * lrg_entity3d_has_tag:
 * @self: an #LrgEntity3D
 * @tag: a tag
 *
 * Returns: %TRUE if the entity has @tag
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_has_tag (LrgEntity3D *self,
                               const gchar *tag);

/**
 * lrg_entity3d_get_tags:
 * @self: an #LrgEntity3D
 *
 * Returns: (transfer full) (array zero-terminated=1): the tags, sorted
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_entity3d_get_tags (LrgEntity3D *self);

/* ==========================================================================
 * Local transform
 * ========================================================================== */

/**
 * lrg_entity3d_get_position:
 * @self: an #LrgEntity3D
 * @out: (out caller-allocates) (array fixed-size=3): local position
 *
 * Gets the position relative to the parent.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_get_position (LrgEntity3D *self,
                                gfloat      *out);

/**
 * lrg_entity3d_set_position:
 * @self: an #LrgEntity3D
 * @position: (array fixed-size=3): local position
 *
 * Sets the position relative to the parent.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_position (LrgEntity3D  *self,
                                const gfloat *position);

/**
 * lrg_entity3d_set_position_xyz:
 * @self: an #LrgEntity3D
 * @x: local X
 * @y: local Y
 * @z: local Z
 *
 * Sets the position relative to the parent.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_position_xyz (LrgEntity3D *self,
                                    gfloat       x,
                                    gfloat       y,
                                    gfloat       z);

/**
 * lrg_entity3d_get_rotation:
 * @self: an #LrgEntity3D
 * @out: (out caller-allocates) (array fixed-size=4): local rotation
 *   quaternion (x, y, z, w)
 *
 * Gets the rotation relative to the parent.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_get_rotation (LrgEntity3D *self,
                                gfloat      *out);

/**
 * lrg_entity3d_set_rotation:
 * @self: an #LrgEntity3D
 * @rotation: (array fixed-size=4): quaternion (x, y, z, w); normalised on
 *   the way in (a zero quaternion becomes the identity)
 *
 * Sets the rotation relative to the parent and forgets the authored Euler
 * rotation (lrg_entity3d_has_authored_euler() becomes %FALSE).
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_rotation (LrgEntity3D  *self,
                                const gfloat *rotation);

/**
 * lrg_entity3d_get_rotation_euler:
 * @self: an #LrgEntity3D
 * @out: (out caller-allocates) (array fixed-size=3): Euler XYZ radians
 *
 * Gets the local rotation as Euler angles.  When the rotation was last set
 * with lrg_entity3d_set_rotation_euler() the authored angles are returned
 * exactly (so 370 degrees stays 370 degrees for the editor); otherwise the
 * quaternion is decomposed with lrg_math3d_quat_to_euler().
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_get_rotation_euler (LrgEntity3D *self,
                                      gfloat      *out);

/**
 * lrg_entity3d_set_rotation_euler:
 * @self: an #LrgEntity3D
 * @euler: (array fixed-size=3): Euler XYZ radians
 *
 * Sets the local rotation from Euler angles, storing both the quaternion
 * and the authored angles.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_rotation_euler (LrgEntity3D  *self,
                                      const gfloat *euler);

/**
 * lrg_entity3d_has_authored_euler:
 * @self: an #LrgEntity3D
 *
 * Returns: %TRUE while the rotation is the one set by
 *   lrg_entity3d_set_rotation_euler()
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_has_authored_euler (LrgEntity3D *self);

/**
 * lrg_entity3d_get_scale:
 * @self: an #LrgEntity3D
 * @out: (out caller-allocates) (array fixed-size=3): local scale
 *
 * Gets the scale relative to the parent.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_get_scale (LrgEntity3D *self,
                             gfloat      *out);

/**
 * lrg_entity3d_set_scale:
 * @self: an #LrgEntity3D
 * @scale: (array fixed-size=3): local scale
 *
 * Sets the scale relative to the parent.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_scale (LrgEntity3D  *self,
                             const gfloat *scale);

/**
 * lrg_entity3d_set_scale_xyz:
 * @self: an #LrgEntity3D
 * @x: X scale
 * @y: Y scale
 * @z: Z scale
 *
 * Sets the scale relative to the parent.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_scale_xyz (LrgEntity3D *self,
                                 gfloat       x,
                                 gfloat       y,
                                 gfloat       z);

/**
 * lrg_entity3d_set_local_trs:
 * @self: an #LrgEntity3D
 * @position: (array fixed-size=3): local position
 * @rotation: (array fixed-size=4): local rotation quaternion (x, y, z, w)
 * @scale: (array fixed-size=3): local scale
 *
 * Sets all three local components with a single version bump and a
 * single #LrgEntity3D::transform-changed.  Forgets the authored Euler.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_local_trs (LrgEntity3D  *self,
                                 const gfloat *position,
                                 const gfloat *rotation,
                                 const gfloat *scale);

/**
 * lrg_entity3d_get_local_matrix:
 * @self: an #LrgEntity3D
 * @out: (out caller-allocates) (array fixed-size=16): T * R * S
 *
 * Gets the local matrix (cached).
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_get_local_matrix (LrgEntity3D *self,
                                    gfloat      *out);

/**
 * lrg_entity3d_set_local_matrix:
 * @self: an #LrgEntity3D
 * @matrix: (array fixed-size=16): an affine matrix without shear
 *
 * Decomposes @matrix into position, rotation and scale.  Forgets the
 * authored Euler.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_local_matrix (LrgEntity3D  *self,
                                    const gfloat *matrix);

/* ==========================================================================
 * World transform
 * ========================================================================== */

/**
 * lrg_entity3d_get_world_matrix:
 * @self: an #LrgEntity3D
 * @out: (out caller-allocates) (array fixed-size=16): parent world * local
 *
 * Gets the world matrix, recomputing it (and stale ancestors) if needed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_get_world_matrix (LrgEntity3D *self,
                                    gfloat      *out);

/**
 * lrg_entity3d_peek_world_matrix:
 * @self: an #LrgEntity3D
 *
 * Like lrg_entity3d_get_world_matrix() without the copy.  The pointer is
 * valid until the next transform change anywhere in the entity's
 * hierarchy; renderers use it while building a frame.
 *
 * Returns: (transfer none) (array fixed-size=16): the cached world matrix
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_entity3d_peek_world_matrix (LrgEntity3D *self);

/**
 * lrg_entity3d_get_world_version:
 * @self: an #LrgEntity3D
 *
 * Gets a counter that changes whenever this entity's world matrix is
 * recomputed with a different input (own or ancestor change).  Compare it
 * with a stored value to detect movement cheaply.  Calling it validates
 * the world matrix.
 *
 * Returns: the world version
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_entity3d_get_world_version (LrgEntity3D *self);

/**
 * lrg_entity3d_get_world_position:
 * @self: an #LrgEntity3D
 * @out: (out caller-allocates) (array fixed-size=3): world position
 *
 * Gets the world-space position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_get_world_position (LrgEntity3D *self,
                                      gfloat      *out);

/**
 * lrg_entity3d_set_world_position:
 * @self: an #LrgEntity3D
 * @position: (array fixed-size=3): world position
 *
 * Moves the entity so its world position is @position (the local
 * position is solved through the parent's inverse world matrix).
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_world_position (LrgEntity3D  *self,
                                      const gfloat *position);

/**
 * lrg_entity3d_get_world_rotation:
 * @self: an #LrgEntity3D
 * @out: (out caller-allocates) (array fixed-size=4): world rotation
 *   quaternion (x, y, z, w)
 *
 * Gets the world-space rotation (the product of the ancestors'
 * rotations; non-uniform ancestor scale is ignored).
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_get_world_rotation (LrgEntity3D *self,
                                      gfloat      *out);

/**
 * lrg_entity3d_set_world_rotation:
 * @self: an #LrgEntity3D
 * @rotation: (array fixed-size=4): world rotation quaternion (x, y, z, w)
 *
 * Sets the local rotation so that the world rotation is @rotation.
 * Forgets the authored Euler.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_world_rotation (LrgEntity3D  *self,
                                      const gfloat *rotation);

/**
 * lrg_entity3d_get_world_scale:
 * @self: an #LrgEntity3D
 * @out: (out caller-allocates) (array fixed-size=3): world scale
 *
 * Gets the world scale (the basis lengths of the world matrix; lossy under
 * rotated non-uniform ancestor scale).
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_get_world_scale (LrgEntity3D *self,
                                   gfloat      *out);

/**
 * lrg_entity3d_set_world_matrix:
 * @self: an #LrgEntity3D
 * @matrix: (array fixed-size=16): desired world matrix
 *
 * Sets the local transform so that the world matrix is @matrix (as far
 * as a TRS can represent it).  Forgets the authored Euler.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_world_matrix (LrgEntity3D  *self,
                                    const gfloat *matrix);

/**
 * lrg_entity3d_transform_point:
 * @self: an #LrgEntity3D
 * @local_point: (array fixed-size=3): a point in entity space
 * @out: (out caller-allocates) (array fixed-size=3): the point in world
 *   space (may alias @local_point)
 *
 * Transforms an entity-space point to world space.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_transform_point (LrgEntity3D  *self,
                                   const gfloat *local_point,
                                   gfloat       *out);

/**
 * lrg_entity3d_inverse_transform_point:
 * @self: an #LrgEntity3D
 * @world_point: (array fixed-size=3): a point in world space
 * @out: (out caller-allocates) (array fixed-size=3): the point in entity
 *   space (may alias @world_point)
 *
 * Transforms a world-space point to entity space.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_inverse_transform_point (LrgEntity3D  *self,
                                           const gfloat *world_point,
                                           gfloat       *out);

/**
 * lrg_entity3d_get_forward:
 * @self: an #LrgEntity3D
 * @out: (out caller-allocates) (array fixed-size=3): unit world forward
 *
 * Gets the world-space forward direction, the entity's local -Z (the
 * direction a camera or spot/directional light on this entity faces).
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_get_forward (LrgEntity3D *self,
                               gfloat      *out);

/**
 * lrg_entity3d_get_up:
 * @self: an #LrgEntity3D
 * @out: (out caller-allocates) (array fixed-size=3): unit world up (+Y)
 *
 * Gets the world-space up direction (local +Y).
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_get_up (LrgEntity3D *self,
                          gfloat      *out);

/**
 * lrg_entity3d_get_right:
 * @self: an #LrgEntity3D
 * @out: (out caller-allocates) (array fixed-size=3): unit world right (+X)
 *
 * Gets the world-space right direction (local +X).
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_get_right (LrgEntity3D *self,
                             gfloat      *out);

/* ==========================================================================
 * Hierarchy
 * ========================================================================== */

/**
 * lrg_entity3d_get_parent:
 * @self: an #LrgEntity3D
 *
 * Returns: (transfer none) (nullable): the parent entity
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_entity3d_get_parent (LrgEntity3D *self);

/**
 * lrg_entity3d_get_world:
 * @self: an #LrgEntity3D
 *
 * Returns: (transfer none) (nullable): the #LrgWorld3D the entity is in
 */
LRG_AVAILABLE_IN_ALL
LrgWorld3D * lrg_entity3d_get_world (LrgEntity3D *self);

/**
 * lrg_entity3d_get_root:
 * @self: an #LrgEntity3D
 *
 * Returns: (transfer none): the topmost ancestor (or @self)
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_entity3d_get_root (LrgEntity3D *self);

/**
 * lrg_entity3d_get_n_children:
 * @self: an #LrgEntity3D
 *
 * Returns: the number of direct children
 */
LRG_AVAILABLE_IN_ALL
guint lrg_entity3d_get_n_children (LrgEntity3D *self);

/**
 * lrg_entity3d_get_child:
 * @self: an #LrgEntity3D
 * @index: a child index
 *
 * Returns: (transfer none) (nullable): the child at @index, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_entity3d_get_child (LrgEntity3D *self,
                                      guint        index);

/**
 * lrg_entity3d_get_children:
 * @self: an #LrgEntity3D
 *
 * Returns: (transfer container) (element-type LrgEntity3D): the direct
 *   children in order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_entity3d_get_children (LrgEntity3D *self);

/**
 * lrg_entity3d_get_index:
 * @self: an #LrgEntity3D
 *
 * Returns: the index among the parent's children, or -1 without a parent
 */
LRG_AVAILABLE_IN_ALL
gint lrg_entity3d_get_index (LrgEntity3D *self);

/**
 * lrg_entity3d_add_child:
 * @self: an #LrgEntity3D
 * @child: (transfer none): an entity without a parent
 *
 * Appends @child.  The parent takes a reference.  The child's local
 * transform is kept (so its world transform becomes relative to @self).
 * If @self is in a world, @child and its subtree join it.  Emits
 * #LrgEntity3D::child-added.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_add_child (LrgEntity3D *self,
                             LrgEntity3D *child);

/**
 * lrg_entity3d_insert_child:
 * @self: an #LrgEntity3D
 * @child: (transfer none): an entity without a parent
 * @index: position, or -1 (or anything past the end) to append
 *
 * Like lrg_entity3d_add_child() at a given position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_insert_child (LrgEntity3D *self,
                                LrgEntity3D *child,
                                gint         index);

/**
 * lrg_entity3d_remove_child:
 * @self: an #LrgEntity3D
 * @child: a direct child
 *
 * Detaches @child (and its subtree leaves the world).  The parent's
 * reference is dropped, so keep your own reference if you want to reuse
 * the child.  Emits #LrgEntity3D::child-removed.
 *
 * Returns: %TRUE if @child was a child of @self
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_remove_child (LrgEntity3D *self,
                                    LrgEntity3D *child);

/**
 * lrg_entity3d_set_parent:
 * @self: an #LrgEntity3D
 * @parent: (nullable): the new parent, or %NULL to detach
 * @keep_world: %TRUE to keep the world transform (the local transform is
 *   recomputed), %FALSE to keep the local transform
 *
 * Reparents @self, appending it to @parent's children.  The entity stays
 * alive across the move.  Making an entity its own ancestor fails with a
 * warning.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_set_parent (LrgEntity3D *self,
                                  LrgEntity3D *parent,
                                  gboolean     keep_world);

/**
 * lrg_entity3d_set_sibling_index:
 * @self: an #LrgEntity3D with a parent
 * @index: new position among the siblings (clamped)
 *
 * Moves the entity within its parent's child list.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_set_sibling_index (LrgEntity3D *self,
                                     guint        index);

/**
 * lrg_entity3d_is_ancestor_of:
 * @self: an #LrgEntity3D
 * @descendant: another entity
 *
 * Returns: %TRUE if @self is a (strict) ancestor of @descendant
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_is_ancestor_of (LrgEntity3D *self,
                                      LrgEntity3D *descendant);

/**
 * lrg_entity3d_find_child:
 * @self: an #LrgEntity3D
 * @name: a name
 * @recursive: %TRUE to search the whole subtree (depth first, pre-order)
 *
 * Returns: (transfer none) (nullable): the first matching descendant
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_entity3d_find_child (LrgEntity3D *self,
                                       const gchar *name,
                                       gboolean     recursive);

/**
 * lrg_entity3d_find_by_path:
 * @self: an #LrgEntity3D
 * @path: a '/'-separated path of child names relative to @self, e.g.
 *   "body/arm/hand"; "." stays, ".." goes to the parent
 *
 * Returns: (transfer none) (nullable): the entity at @path
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_entity3d_find_by_path (LrgEntity3D *self,
                                         const gchar *path);

/**
 * lrg_entity3d_get_path:
 * @self: an #LrgEntity3D
 *
 * Builds the '/'-separated path of names from the topmost ancestor's
 * children down to @self (the root's own name is not included, so the
 * result resolves with lrg_entity3d_find_by_path() on the root).  Unnamed
 * entities appear as "#<index>".
 *
 * Returns: (transfer full): the path
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_entity3d_get_path (LrgEntity3D *self);

/**
 * LrgEntity3DVisitFunc:
 * @entity: the visited entity
 * @user_data: user data
 *
 * Callback of lrg_entity3d_foreach().
 *
 * Returns: %FALSE to skip @entity's children, %TRUE to descend
 */
typedef gboolean (*LrgEntity3DVisitFunc) (LrgEntity3D *entity,
                                          gpointer     user_data);

/**
 * lrg_entity3d_foreach:
 * @self: an #LrgEntity3D
 * @func: (scope call): visitor
 * @user_data: data for @func
 *
 * Visits @self and its subtree depth first, pre-order.  The hierarchy
 * must not be modified during the walk.
 */
LRG_AVAILABLE_IN_ALL
void lrg_entity3d_foreach (LrgEntity3D          *self,
                           LrgEntity3DVisitFunc  func,
                           gpointer              user_data);

/* ==========================================================================
 * Components
 * ========================================================================== */

/**
 * lrg_entity3d_add_component:
 * @self: an #LrgEntity3D
 * @component: (transfer none): a component without an owner
 *
 * Appends @component (the entity takes a reference), calls its
 * attached_entity() vfunc, then world_entered() if the entity is in a
 * world, and emits #LrgEntity3D::component-added.  A component can belong
 * to one owner (game object or entity) at a time.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_add_component (LrgEntity3D  *self,
                                     LrgComponent *component);

/**
 * lrg_entity3d_remove_component:
 * @self: an #LrgEntity3D
 * @component: a component of @self
 *
 * Calls world_exited() (if in a world) and detached_entity(), removes the
 * component and drops the entity's reference.  Emits
 * #LrgEntity3D::component-removed.
 *
 * Returns: %TRUE if the component was attached to @self
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_remove_component (LrgEntity3D  *self,
                                        LrgComponent *component);

/**
 * lrg_entity3d_get_component:
 * @self: an #LrgEntity3D
 * @component_type: a #GType deriving from #LrgComponent
 *
 * Returns: (transfer none) (nullable): the first component that is a
 *   @component_type (subtypes match)
 */
LRG_AVAILABLE_IN_ALL
LrgComponent * lrg_entity3d_get_component (LrgEntity3D *self,
                                           GType        component_type);

/**
 * lrg_entity3d_get_components_of_type:
 * @self: an #LrgEntity3D
 * @component_type: a #GType deriving from #LrgComponent
 *
 * Returns: (transfer container) (element-type LrgComponent): every
 *   component that is a @component_type, in order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_entity3d_get_components_of_type (LrgEntity3D *self,
                                                 GType        component_type);

/**
 * lrg_entity3d_has_component:
 * @self: an #LrgEntity3D
 * @component_type: a #GType deriving from #LrgComponent
 *
 * Returns: %TRUE if a component of that type is attached
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_has_component (LrgEntity3D *self,
                                     GType        component_type);

/**
 * lrg_entity3d_get_components:
 * @self: an #LrgEntity3D
 *
 * Returns: (transfer container) (element-type LrgComponent): all
 *   components in attach order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_entity3d_get_components (LrgEntity3D *self);

/**
 * lrg_entity3d_get_n_components:
 * @self: an #LrgEntity3D
 *
 * Returns: the number of components
 */
LRG_AVAILABLE_IN_ALL
guint lrg_entity3d_get_n_components (LrgEntity3D *self);

/**
 * lrg_entity3d_get_local_bounds:
 * @self: an #LrgEntity3D
 * @out: (out caller-allocates): the union of the components' local bounds
 *
 * Unions the get_bounds() vfunc results of every enabled component.
 *
 * Returns: %TRUE if at least one component reported bounds
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_get_local_bounds (LrgEntity3D      *self,
                                        LrgBoundingBox3D *out);

/**
 * lrg_entity3d_get_world_bounds:
 * @self: an #LrgEntity3D
 * @out: (out caller-allocates): the world-space AABB of the local bounds
 *
 * Transforms the local bounds' eight corners by the world matrix.  An
 * entity without bounds yields a zero-size box at its world position.
 *
 * Returns: %TRUE if the entity has real bounds
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_entity3d_get_world_bounds (LrgEntity3D      *self,
                                        LrgBoundingBox3D *out);

G_END_DECLS
