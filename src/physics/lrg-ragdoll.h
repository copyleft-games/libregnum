/* lrg-ragdoll.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhysicsAsset: the `.rphysasset` description of a ragdoll (one rigid
 * part per bone: capsule, box or sphere with an offset and a mass, and a
 * joint with limits to the nearest ancestor part; collision exclusions).
 *
 * LrgRagdoll: an LrgPhysicsAsset instantiated in an LrgDynamicsWorld for
 * one LrgSkeleton: bodies placed from the skeleton's current pose, joints
 * between them, passive or powered toward an animation pose by joint
 * motors, blended with the animation per bone and written back to the
 * skeleton.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../animation/lrg-skeleton.h"
#include "../animation/lrg-bone-pose.h"
#include "lrg-dynamics.h"
#include "lrg-joint.h"

G_BEGIN_DECLS

/**
 * LRG_PHYSICS_ASSET_FORMAT_VERSION:
 *
 * The `.rphysasset` format version this library writes and the newest it
 * reads.
 */
#define LRG_PHYSICS_ASSET_FORMAT_VERSION (1)

/**
 * LRG_RAGDOLL_ERROR:
 *
 * Error domain for #LrgPhysicsAsset and #LrgRagdoll. Errors carry an
 * #LrgRagdollError code.
 */
#define LRG_RAGDOLL_ERROR (lrg_ragdoll_error_quark ())

/**
 * LrgRagdollError:
 * @LRG_RAGDOLL_ERROR_PARSE: the document is not valid YAML or not a mapping
 * @LRG_RAGDOLL_ERROR_FORMAT: a key is unknown, missing, of the wrong type
 *   or out of range (the message names the path, e.g. `/parts/2/mass`)
 * @LRG_RAGDOLL_ERROR_VERSION: the document is newer than
 *   %LRG_PHYSICS_ASSET_FORMAT_VERSION
 * @LRG_RAGDOLL_ERROR_SKELETON: the asset names a bone the skeleton does
 *   not have, or two parts share a bone
 * @LRG_RAGDOLL_ERROR_WORLD: the world refused a body or joint
 * @LRG_RAGDOLL_ERROR_STATE: the ragdoll is (or is not) in a world
 *
 * Error codes for the %LRG_RAGDOLL_ERROR domain.
 */
typedef enum {
	LRG_RAGDOLL_ERROR_PARSE = 1,
	LRG_RAGDOLL_ERROR_FORMAT = 2,
	LRG_RAGDOLL_ERROR_VERSION = 3,
	LRG_RAGDOLL_ERROR_SKELETON = 4,
	LRG_RAGDOLL_ERROR_WORLD = 5,
	LRG_RAGDOLL_ERROR_STATE = 6
} LrgRagdollError;

/**
 * LrgPhysicsAssetShape:
 * @LRG_PHYSICS_ASSET_SHAPE_CAPSULE: a capsule along the part's local Y
 *   axis (radius, half-height of the cylinder)
 * @LRG_PHYSICS_ASSET_SHAPE_BOX: a box (half extents)
 * @LRG_PHYSICS_ASSET_SHAPE_SPHERE: a sphere (radius)
 *
 * The collision shape of one ragdoll part.
 */
typedef enum {
	LRG_PHYSICS_ASSET_SHAPE_CAPSULE = 0,
	LRG_PHYSICS_ASSET_SHAPE_BOX = 1,
	LRG_PHYSICS_ASSET_SHAPE_SPHERE = 2
} LrgPhysicsAssetShape;

/**
 * LrgRagdollMode:
 * @LRG_RAGDOLL_PASSIVE: the joints only limit; the ragdoll falls limp
 * @LRG_RAGDOLL_POWERED: every joint's rotation position motors pull the
 *   parts toward the pose given to lrg_ragdoll_drive_to_pose() (an
 *   active ragdoll that still reacts to hits)
 *
 * How a ragdoll's joints are driven.
 */
typedef enum {
	LRG_RAGDOLL_PASSIVE = 0,
	LRG_RAGDOLL_POWERED = 1
} LrgRagdollMode;

LRG_AVAILABLE_IN_ALL
GQuark lrg_ragdoll_error_quark (void);

LRG_AVAILABLE_IN_ALL
GType lrg_physics_asset_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PHYSICS_ASSET_SHAPE (lrg_physics_asset_shape_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_ragdoll_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_RAGDOLL_MODE (lrg_ragdoll_mode_get_type ())

/* ------------------------------------------------------------------ */
/* LrgPhysicsAssetPart                                                 */
/* ------------------------------------------------------------------ */

typedef struct _LrgPhysicsAssetPart LrgPhysicsAssetPart;

/**
 * LrgPhysicsAssetPart:
 * @bone: name of the skeleton bone the part follows
 * @shape: the collision shape
 * @radius: capsule or sphere radius, metres
 * @half_height: capsule half height (of the cylinder between the caps)
 * @half_extents: box half extents
 * @offset: shape centre in the bone's frame
 * @rotation: shape rotation in the bone's frame, xyzw (a capsule's axis
 *   is its local Y)
 * @mass: mass in kg, positive
 * @joint: joint to the nearest ancestor part: %LRG_JOINT_FIXED,
 *   %LRG_JOINT_POINT, %LRG_JOINT_HINGE, %LRG_JOINT_CONE or
 *   %LRG_JOINT_SWING_TWIST. Ignored on a part without an ancestor part
 *   (the root)
 * @joint_position: joint origin in the bone's frame
 * @joint_rotation: joint frame in the bone's frame, xyzw; its X axis is
 *   the twist (hinge, cone) axis
 * @twist_min: minimum twist (hinge angle), radians
 * @twist_max: maximum twist, radians
 * @swing_y: swing half angle about the joint Y axis (the cone half
 *   angle), radians
 * @swing_z: swing half angle about the joint Z axis, radians
 * @friction: joint friction torque, N m
 * @motor_max_torque: largest torque a powered ragdoll's motors apply to
 *   this joint, N m
 * @motor_frequency: powered motor spring frequency, Hz
 * @motor_damping: powered motor damping ratio
 *
 * One rigid part of a physics asset: a plain struct the editor and code
 * fill in and hand to lrg_physics_asset_add_part().
 */
struct _LrgPhysicsAssetPart
{
	gchar *bone;
	LrgPhysicsAssetShape shape;
	gfloat radius;
	gfloat half_height;
	gfloat half_extents[3];
	gfloat offset[3];
	gfloat rotation[4];
	gfloat mass;
	LrgJointKind joint;
	gfloat joint_position[3];
	gfloat joint_rotation[4];
	gfloat twist_min;
	gfloat twist_max;
	gfloat swing_y;
	gfloat swing_z;
	gfloat friction;
	gfloat motor_max_torque;
	gfloat motor_frequency;
	gfloat motor_damping;
};

LRG_AVAILABLE_IN_ALL
GType lrg_physics_asset_part_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PHYSICS_ASSET_PART (lrg_physics_asset_part_get_type ())

/**
 * lrg_physics_asset_part_new:
 * @bone: the bone name
 *
 * A part with the defaults the format omits: a capsule of radius 0.05 m
 * and half height 0.1 m, no offset, mass 1 kg, a swing-twist joint at
 * the bone origin with twist ±0.25 rad and swing 0.5 rad, no friction,
 * motor torque 300 N m at 10 Hz with damping 8. The motor spring is
 * relative to the part's own inertia while the joint moves the whole
 * chain below it, so a powered ragdoll needs a high damping ratio to be
 * damped in practice.
 *
 * Returns: (transfer full): a new part
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsAssetPart *lrg_physics_asset_part_new (const gchar *bone);

/**
 * lrg_physics_asset_part_copy:
 * @part: a part
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsAssetPart *lrg_physics_asset_part_copy (const LrgPhysicsAssetPart *part);

/**
 * lrg_physics_asset_part_free:
 * @part: (nullable): a part
 *
 * Frees a part and its bone name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_asset_part_free (LrgPhysicsAssetPart *part);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgPhysicsAssetPart, lrg_physics_asset_part_free)

/* ------------------------------------------------------------------ */
/* LrgPhysicsAsset                                                     */
/* ------------------------------------------------------------------ */

#define LRG_TYPE_PHYSICS_ASSET (lrg_physics_asset_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPhysicsAsset, lrg_physics_asset, LRG, PHYSICS_ASSET, GObject)

/**
 * lrg_physics_asset_new:
 *
 * An empty asset: no parts, no exclusions, adjacent parts excluded.
 *
 * Returns: (transfer full): a new asset
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsAsset *lrg_physics_asset_new (void);

/**
 * lrg_physics_asset_new_from_data:
 * @data: (array length=length) (element-type guint8): `.rphysasset` text
 * @length: bytes in @data, or -1 when NUL-terminated
 * @error: (nullable): return location for a #GError
 *
 * Parses a physics asset strictly: unknown keys, keys that do not apply
 * to a part's shape or joint, wrong types and out-of-range values are
 * %LRG_RAGDOLL_ERROR_FORMAT errors naming the path.
 *
 * Returns: (transfer full) (nullable): the asset, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsAsset *lrg_physics_asset_new_from_data (const gchar  *data,
                                                  gssize        length,
                                                  GError      **error);

/**
 * lrg_physics_asset_new_from_file:
 * @path: a `.rphysasset` file
 * @error: (nullable): return location for a #GError
 *
 * Reads and parses a file; messages are prefixed with @path.
 *
 * Returns: (transfer full) (nullable): the asset, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsAsset *lrg_physics_asset_new_from_file (const gchar  *path,
                                                  GError      **error);

/**
 * lrg_physics_asset_new_from_skeleton:
 * @skeleton: a skeleton with bind poses
 * @total_mass: mass shared by the parts in proportion to their volume, kg
 * @radius_ratio: capsule radius as a fraction of the bone length
 *   (0.05 to 1)
 *
 * Generates a starting asset: one capsule per bone that has at least one
 * child, running from the bone to its first child (a leaf bone gets a
 * sphere of the parent's radius), with a swing-twist joint at the bone
 * origin whose twist axis runs along the bone, then evens the masses out
 * with lrg_physics_asset_stabilize(). Tune it afterwards.
 *
 * Returns: (transfer full): a new asset
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsAsset *lrg_physics_asset_new_from_skeleton (LrgSkeleton *skeleton,
                                                      gfloat       total_mass,
                                                      gfloat       radius_ratio);

/**
 * lrg_physics_asset_stabilize:
 * @self: an asset
 * @skeleton: the skeleton that defines which part is whose parent
 *
 * Evens out the masses along each chain of parts so that a child weighs
 * between 0.8 and 1.2 times its parent, keeping the total mass (Jolt's
 * RagdollSettings::Stabilize mass pass). Large mass ratios across a
 * joint are the main cause of limit overshoot and joint stretch in a
 * sequential-impulse solver. lrg_physics_asset_new_from_skeleton() calls
 * it. Parts whose bones the skeleton lacks are left alone.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_asset_stabilize (LrgPhysicsAsset *self,
                                  LrgSkeleton     *skeleton);

/**
 * lrg_physics_asset_save_to_data:
 * @self: an asset
 * @length: (out) (optional): length of the text
 * @error: (nullable): return location for a #GError
 *
 * Writes the asset. Values equal to the defaults are left out, floats are
 * written in their shortest exact form, so loading and saving again is
 * byte stable.
 *
 * Returns: (transfer full) (nullable): the `.rphysasset` text
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_physics_asset_save_to_data (LrgPhysicsAsset  *self,
                                       gsize            *length,
                                       GError          **error);

/**
 * lrg_physics_asset_save_to_file:
 * @self: an asset
 * @path: destination
 * @error: (nullable): return location for a #GError
 *
 * Writes lrg_physics_asset_save_to_data() atomically to @path.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_asset_save_to_file (LrgPhysicsAsset  *self,
                                         const gchar      *path,
                                         GError          **error);

/**
 * lrg_physics_asset_get_name:
 * @self: an asset
 *
 * Returns: (transfer none) (nullable): the asset name
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_physics_asset_get_name (LrgPhysicsAsset *self);

/**
 * lrg_physics_asset_set_name:
 * @self: an asset
 * @name: (nullable): a display name
 *
 * Sets #LrgPhysicsAsset:name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_asset_set_name (LrgPhysicsAsset *self,
                                 const gchar     *name);

/**
 * lrg_physics_asset_get_exclude_adjacent:
 * @self: an asset
 *
 * Returns: whether parts joined by a joint never collide with each other
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_asset_get_exclude_adjacent (LrgPhysicsAsset *self);

/**
 * lrg_physics_asset_set_exclude_adjacent:
 * @self: an asset
 * @exclude: %TRUE (the default) to stop jointed parts colliding
 *
 * Sets #LrgPhysicsAsset:exclude-adjacent.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_asset_set_exclude_adjacent (LrgPhysicsAsset *self,
                                             gboolean         exclude);

/**
 * lrg_physics_asset_get_part_count:
 * @self: an asset
 *
 * Returns: the number of parts
 */
LRG_AVAILABLE_IN_ALL
guint lrg_physics_asset_get_part_count (LrgPhysicsAsset *self);

/**
 * lrg_physics_asset_get_part:
 * @self: an asset
 * @index: a part index
 *
 * Returns: (transfer none) (nullable): the part, or %NULL out of range
 */
LRG_AVAILABLE_IN_ALL
const LrgPhysicsAssetPart *lrg_physics_asset_get_part (LrgPhysicsAsset *self,
                                                       guint            index);

/**
 * lrg_physics_asset_find_part:
 * @self: an asset
 * @bone: a bone name
 *
 * Returns: the index of the part on @bone, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_physics_asset_find_part (LrgPhysicsAsset *self,
                                  const gchar     *bone);

/**
 * lrg_physics_asset_add_part:
 * @self: an asset
 * @part: the part to copy in
 * @error: (nullable): return location for a #GError
 *
 * Appends a copy of @part after validating it (a non-empty unique bone,
 * a positive mass and sizes, a supported joint kind, ordered limits).
 *
 * Returns: the new index, or -1 with @error set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_physics_asset_add_part (LrgPhysicsAsset            *self,
                                 const LrgPhysicsAssetPart  *part,
                                 GError                    **error);

/**
 * lrg_physics_asset_set_part:
 * @self: an asset
 * @index: a part index
 * @part: the new values (copied)
 * @error: (nullable): return location for a #GError
 *
 * Replaces a part after the same validation as lrg_physics_asset_add_part().
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_asset_set_part (LrgPhysicsAsset            *self,
                                     guint                       index,
                                     const LrgPhysicsAssetPart  *part,
                                     GError                    **error);

/**
 * lrg_physics_asset_remove_part:
 * @self: an asset
 * @index: a part index
 *
 * Removes a part; later parts move down one index. Exclusions naming its
 * bone are removed too.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_asset_remove_part (LrgPhysicsAsset *self,
                                    guint            index);

/**
 * lrg_physics_asset_add_exclusion:
 * @self: an asset
 * @bone_a: a bone with a part
 * @bone_b: another bone with a part
 *
 * Stops the two parts colliding with each other (beyond the automatic
 * parent/child exclusion). Adding a pair twice does nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_asset_add_exclusion (LrgPhysicsAsset *self,
                                      const gchar     *bone_a,
                                      const gchar     *bone_b);

/**
 * lrg_physics_asset_get_exclusion_count:
 * @self: an asset
 *
 * Returns: the number of explicit exclusions
 */
LRG_AVAILABLE_IN_ALL
guint lrg_physics_asset_get_exclusion_count (LrgPhysicsAsset *self);

/**
 * lrg_physics_asset_get_exclusion:
 * @self: an asset
 * @index: an exclusion index
 * @bone_a: (out) (transfer none) (optional): first bone
 * @bone_b: (out) (transfer none) (optional): second bone
 *
 * Returns: %FALSE when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_asset_get_exclusion (LrgPhysicsAsset  *self,
                                          guint             index,
                                          const gchar     **bone_a,
                                          const gchar     **bone_b);

/**
 * lrg_physics_asset_remove_exclusion:
 * @self: an asset
 * @index: an exclusion index
 *
 * Removes one explicit exclusion.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_asset_remove_exclusion (LrgPhysicsAsset *self,
                                         guint            index);

/**
 * lrg_physics_asset_validate:
 * @self: an asset
 * @skeleton: the skeleton it will drive
 * @error: (nullable): return location for a #GError
 *
 * Checks that every part and exclusion names a bone of @skeleton.
 *
 * Returns: %TRUE when the asset fits the skeleton
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_asset_validate (LrgPhysicsAsset  *self,
                                     LrgSkeleton      *skeleton,
                                     GError          **error);

/* ------------------------------------------------------------------ */
/* LrgRagdoll                                                          */
/* ------------------------------------------------------------------ */

#define LRG_TYPE_RAGDOLL (lrg_ragdoll_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRagdoll, lrg_ragdoll, LRG, RAGDOLL, GObject)

/**
 * lrg_ragdoll_new:
 * @asset: the physics asset (a reference is kept; do not change it while
 *   the ragdoll is in a world)
 * @skeleton: the skeleton to drive (a reference is kept)
 * @error: (nullable): return location for a #GError
 *
 * Binds an asset to a skeleton: resolves bones and each part's parent
 * part (the nearest ancestor bone with a part) and builds the joint
 * frames from the skeleton's bind pose, so joint limits are measured
 * from the bind pose.
 *
 * Returns: (transfer full) (nullable): a new ragdoll, or %NULL with
 *   @error set
 */
LRG_AVAILABLE_IN_ALL
LrgRagdoll *lrg_ragdoll_new (LrgPhysicsAsset  *asset,
                             LrgSkeleton      *skeleton,
                             GError          **error);

/**
 * lrg_ragdoll_get_asset:
 * @self: a ragdoll
 *
 * Returns: (transfer none): the physics asset
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsAsset *lrg_ragdoll_get_asset (LrgRagdoll *self);

/**
 * lrg_ragdoll_get_skeleton:
 * @self: a ragdoll
 *
 * Returns: (transfer none): the skeleton
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton *lrg_ragdoll_get_skeleton (LrgRagdoll *self);

/**
 * lrg_ragdoll_set_root_transform:
 * @self: a ragdoll
 * @transform: (nullable): skeleton (model) space to world space, or
 *   %NULL for identity. Scale must be 1
 *
 * Where the skeleton stands in the world: used to place the bodies and
 * to convert them back to bone poses.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ragdoll_set_root_transform (LrgRagdoll        *self,
                                     const LrgBonePose *transform);

/**
 * lrg_ragdoll_get_root_transform:
 * @self: a ragdoll
 * @transform: (out caller-allocates): the model-to-world transform
 *
 * Reads the root transform.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ragdoll_get_root_transform (LrgRagdoll  *self,
                                     LrgBonePose *transform);

/**
 * lrg_ragdoll_add_to_world:
 * @self: a ragdoll that is not in a world
 * @world: the world
 * @error: (nullable): return location for a #GError
 *
 * Creates one dynamic body per part, placed from the skeleton's current
 * world poses through the root transform, on %LRG_PHYSICS_LAYER_RAGDOLL;
 * one joint per non-root part; and the collision exclusions. On failure
 * everything created so far is removed again.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ragdoll_add_to_world (LrgRagdoll        *self,
                                   LrgDynamicsWorld  *world,
                                   GError           **error);

/**
 * lrg_ragdoll_remove_from_world:
 * @self: a ragdoll
 *
 * Removes the joints and bodies. Does nothing when not in a world.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ragdoll_remove_from_world (LrgRagdoll *self);

/**
 * lrg_ragdoll_get_world:
 * @self: a ragdoll
 *
 * Returns: (transfer none) (nullable): the world it is in, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgDynamicsWorld *lrg_ragdoll_get_world (LrgRagdoll *self);

/**
 * lrg_ragdoll_get_part_count:
 * @self: a ragdoll
 *
 * Returns: the number of parts (bodies)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_ragdoll_get_part_count (LrgRagdoll *self);

/**
 * lrg_ragdoll_get_part_bone:
 * @self: a ragdoll
 * @part: a part index
 *
 * Returns: the skeleton bone index of the part, or -1 out of range
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ragdoll_get_part_bone (LrgRagdoll *self,
                                guint       part);

/**
 * lrg_ragdoll_get_part_parent:
 * @self: a ragdoll
 * @part: a part index
 *
 * Returns: the parent part index, or -1 for a root part
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ragdoll_get_part_parent (LrgRagdoll *self,
                                  guint       part);

/**
 * lrg_ragdoll_get_body:
 * @self: a ragdoll in a world
 * @part: a part index
 *
 * Returns: the body id of the part, or %G_MAXUINT when not in a world
 */
LRG_AVAILABLE_IN_ALL
guint lrg_ragdoll_get_body (LrgRagdoll *self,
                            guint       part);

/**
 * lrg_ragdoll_get_joint:
 * @self: a ragdoll
 * @part: a part index
 *
 * Returns: (transfer none) (nullable): the joint from the part's parent
 *   to the part, or %NULL for a root part
 */
LRG_AVAILABLE_IN_ALL
LrgJoint *lrg_ragdoll_get_joint (LrgRagdoll *self,
                                 guint       part);

/**
 * lrg_ragdoll_set_mode:
 * @self: a ragdoll
 * @mode: passive or powered
 *
 * Switches the joint motors on (powered) or off (passive).
 */
LRG_AVAILABLE_IN_ALL
void lrg_ragdoll_set_mode (LrgRagdoll     *self,
                           LrgRagdollMode  mode);

/**
 * lrg_ragdoll_get_mode:
 * @self: a ragdoll
 *
 * Returns: the drive mode
 */
LRG_AVAILABLE_IN_ALL
LrgRagdollMode lrg_ragdoll_get_mode (LrgRagdoll *self);

/**
 * lrg_ragdoll_drive_to_pose:
 * @self: a ragdoll
 * @pose: a skeleton with the same bones whose local poses are the target
 *   (typically the animated skeleton before lrg_ragdoll_write_pose())
 *
 * Sets every joint's motor target to the relative rotation the pose asks
 * for. Has an effect in %LRG_RAGDOLL_POWERED mode. Call it every frame
 * with the current animation pose.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ragdoll_drive_to_pose (LrgRagdoll  *self,
                                LrgSkeleton *pose);

/**
 * lrg_ragdoll_set_blend:
 * @self: a ragdoll
 * @weight: 0 (animation only) to 1 (physics only)
 *
 * Sets the physics weight of every bone (see lrg_ragdoll_set_bone_blend()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_ragdoll_set_blend (LrgRagdoll *self,
                            gfloat      weight);

/**
 * lrg_ragdoll_set_bone_blend:
 * @self: a ragdoll
 * @bone: a skeleton bone index
 * @weight: 0 (keep the animation) to 1 (follow the physics body)
 *
 * Per-bone physics/animation blend used by lrg_ragdoll_write_pose(). The
 * default is 1. Bones without a part always keep the animation pose.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ragdoll_set_bone_blend (LrgRagdoll *self,
                                 guint       bone,
                                 gfloat      weight);

/**
 * lrg_ragdoll_get_bone_blend:
 * @self: a ragdoll
 * @bone: a skeleton bone index
 *
 * Returns: the physics weight of the bone
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_ragdoll_get_bone_blend (LrgRagdoll *self,
                                   guint       bone);

/**
 * lrg_ragdoll_write_pose:
 * @self: a ragdoll in a world
 *
 * Writes the bodies back into the skeleton: for each bone with a part,
 * the local pose that puts the bone where its body is, blended with the
 * skeleton's current local pose (the animation) by the bone's weight;
 * then recomputes the skeleton's world poses. Call after each world step
 * (the skinned model reads the skeleton).
 */
LRG_AVAILABLE_IN_ALL
void lrg_ragdoll_write_pose (LrgRagdoll *self);

/**
 * lrg_ragdoll_set_linear_velocity:
 * @self: a ragdoll in a world
 * @velocity: (array fixed-size=3): world velocity for every part
 *
 * Throws the whole ragdoll (for example when it takes over from an
 * animated character that was moving).
 */
LRG_AVAILABLE_IN_ALL
void lrg_ragdoll_set_linear_velocity (LrgRagdoll   *self,
                                      const gfloat *velocity);

G_END_DECLS
