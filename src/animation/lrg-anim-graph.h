/* lrg-anim-graph.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnimGraph: the .ranim animation graph asset (parameters, layers of
 * state machines with blend trees and sub-machines, root motion and an IK
 * post-pass), plus the whole-skeleton pose utilities the animation
 * runtime is built on.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-skeleton.h"
#include "lrg-animation-clip.h"
#include "lrg-animation-event.h"
#include "lrg-animation-state.h"
#include "lrg-animation-state-machine.h"
#include "lrg-animation-layer.h"
#include "lrg-blend-tree.h"
#include "lrg-ik-chain.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Pose utilities
 *
 * A "pose" here is an array of #LrgBonePose local transforms where entry i
 * belongs to the bone whose index (lrg_bone_get_index()) is i. Skeletons
 * built by the glTF importer, lrg_skeleton_copy() and the tests number
 * their bones 0..n-1, so n = lrg_skeleton_get_bone_count().
 * ========================================================================== */

/**
 * lrg_anim_pose_bind:
 * @skeleton: a skeleton
 * @poses: (array length=n_poses) (out caller-allocates): the pose to fill
 * @n_poses: number of entries in @poses
 *
 * Fills @poses with the skeleton's bind (rest) pose. Entries without a
 * bone get the identity.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_pose_bind             (LrgSkeleton       *skeleton,
                                         LrgBonePose       *poses,
                                         guint              n_poses);

/**
 * lrg_anim_pose_sample_clip:
 * @clip: a clip
 * @skeleton: the skeleton the pose is for
 * @time: the time in seconds (the clip's loop mode applies)
 * @poses: (array length=n_poses) (out caller-allocates): the pose to fill
 * @n_poses: number of entries in @poses
 *
 * Samples every track of @clip into @poses; bones the clip does not
 * animate get their bind pose. The bone-to-track table is cached on the
 * clip per skeleton layout (bone names and order), so many characters
 * sharing a clip and copies of one skeleton share one table.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_pose_sample_clip      (LrgAnimationClip  *clip,
                                         LrgSkeleton       *skeleton,
                                         gfloat             time,
                                         LrgBonePose       *poses,
                                         guint              n_poses);

/**
 * lrg_anim_pose_blend:
 * @dest: (array length=n_poses) (inout): the pose blended into
 * @src: (array length=n_poses): the pose blended in
 * @weight: blend weight 0..1 (0 keeps @dest, 1 replaces it)
 * @bone_weights: (array length=n_poses) (nullable): per-bone factors
 *   0..1 multiplied into @weight (a layer mask), or %NULL for all 1
 * @n_poses: number of entries
 *
 * Override blend: lerps translation and scale, slerps rotation (shortest
 * arc) per bone.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_pose_blend            (LrgBonePose       *dest,
                                         const LrgBonePose *src,
                                         gfloat             weight,
                                         const gfloat      *bone_weights,
                                         guint              n_poses);

/**
 * lrg_anim_pose_add:
 * @dest: (array length=n_poses) (inout): the pose added onto
 * @additive: (array length=n_poses): the additive pose
 * @reference: (array length=n_poses): the pose @additive is measured
 *   against (the bind pose for layers)
 * @weight: how much of the difference to add, 0..1
 * @bone_weights: (array length=n_poses) (nullable): per-bone factors
 * @n_poses: number of entries
 *
 * Additive blend: adds (@additive - @reference) * @weight to @dest:
 * translation offsets add, rotations multiply on the right by the
 * weighted difference rotation inverse(reference) * additive, scales
 * multiply by the weighted ratio.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_pose_add              (LrgBonePose       *dest,
                                         const LrgBonePose *additive,
                                         const LrgBonePose *reference,
                                         gfloat             weight,
                                         const gfloat      *bone_weights,
                                         guint              n_poses);

/**
 * lrg_anim_pose_apply:
 * @skeleton: a skeleton
 * @poses: (array length=n_poses): local poses by bone index
 * @n_poses: number of entries
 *
 * Writes @poses into the skeleton's local poses and recomputes the world
 * poses.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_pose_apply            (LrgSkeleton       *skeleton,
                                         const LrgBonePose *poses,
                                         guint              n_poses);

/**
 * lrg_anim_pose_clip_root_motion:
 * @clip: a clip
 * @bone_name: the bone whose motion is measured
 * @t0: start time in seconds (not wrapped)
 * @t1: end time in seconds (not wrapped, normally >= @t0)
 * @out_translation: (out caller-allocates) (array fixed-size=3): the
 *   local translation change
 * @out_yaw: (out) (optional): the rotation change about +Y, radians
 *
 * Measures the bone's motion from @t0 to @t1. A repeating clip counts
 * every loop crossed (each adds the clip's end-minus-start displacement),
 * a clip that does not loop clamps both times to its length. A clip
 * without a track for the bone reports zero.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_pose_clip_root_motion (LrgAnimationClip  *clip,
                                         const gchar       *bone_name,
                                         gfloat             t0,
                                         gfloat             t1,
                                         gfloat            *out_translation,
                                         gfloat            *out_yaw);

/**
 * lrg_anim_pose_clip_events:
 * @clip: a clip
 * @t0: start time in seconds (exclusive, not wrapped)
 * @t1: end time in seconds (inclusive, not wrapped)
 *
 * Lists the clip events whose time falls in (@t0, @t1], once per loop
 * crossed for a repeating clip, in playback order. An interval that
 * starts exactly at 0 includes events at time 0 (a state entered this
 * frame fires its first-frame events).
 *
 * Returns: (transfer container) (element-type LrgAnimationEvent): the
 *   events (owned by the clip)
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_anim_pose_clip_events   (LrgAnimationClip  *clip,
                                         gfloat             t0,
                                         gfloat             t1);

/* ==========================================================================
 * Enumerations
 * ========================================================================== */

/**
 * LrgRootMotionMode:
 * @LRG_ROOT_MOTION_NONE: no root motion; the root bone animates in place
 * @LRG_ROOT_MOTION_XZ: the root bone's horizontal (X/Z) motion is taken
 *   out of the pose and reported (and applied to the entity); vertical
 *   motion stays in the pose (jumps, crouches)
 * @LRG_ROOT_MOTION_XYZ: all of the root bone's translation is taken out
 *
 * How an #LrgAnimGraph extracts root motion. Rotation about +Y is taken
 * out as well when lrg_anim_graph_get_root_motion_rotation() is %TRUE.
 */
typedef enum
{
	LRG_ROOT_MOTION_NONE,
	LRG_ROOT_MOTION_XZ,
	LRG_ROOT_MOTION_XYZ
} LrgRootMotionMode;

LRG_AVAILABLE_IN_ALL
GType lrg_root_motion_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ROOT_MOTION_MODE (lrg_root_motion_mode_get_type ())

/**
 * LRG_ANIM_GRAPH_ERROR:
 *
 * Error domain for #LrgAnimGraph.
 */
#define LRG_ANIM_GRAPH_ERROR (lrg_anim_graph_error_quark ())

/**
 * lrg_anim_graph_error_quark:
 *
 * Gets the error quark for #LrgAnimGraph.
 *
 * Returns: the quark
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_anim_graph_error_quark (void);

/**
 * LrgAnimGraphError:
 * @LRG_ANIM_GRAPH_ERROR_PARSE: the text is not YAML or not a graph
 *   mapping
 * @LRG_ANIM_GRAPH_ERROR_VERSION: the file's version is newer than this
 *   build reads
 * @LRG_ANIM_GRAPH_ERROR_INVALID: a value has the wrong type or range, a
 *   required key is missing, or a name is duplicated
 * @LRG_ANIM_GRAPH_ERROR_REFERENCE: a state, parameter, clip or bone name
 *   refers to something that does not exist
 * @LRG_ANIM_GRAPH_ERROR_CLIP: a clip could not be loaded
 * @LRG_ANIM_GRAPH_ERROR_IO: reading or writing a file failed
 *
 * Error codes in the %LRG_ANIM_GRAPH_ERROR domain. Messages carry the
 * path of the offending key, for example
 * `layers[0].transitions[2].conditions[0]: unknown parameter 'sped'`.
 */
typedef enum
{
	LRG_ANIM_GRAPH_ERROR_PARSE,
	LRG_ANIM_GRAPH_ERROR_VERSION,
	LRG_ANIM_GRAPH_ERROR_INVALID,
	LRG_ANIM_GRAPH_ERROR_REFERENCE,
	LRG_ANIM_GRAPH_ERROR_CLIP,
	LRG_ANIM_GRAPH_ERROR_IO
} LrgAnimGraphError;

LRG_AVAILABLE_IN_ALL
GType lrg_anim_graph_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ANIM_GRAPH_ERROR (lrg_anim_graph_error_get_type ())

/**
 * LRG_ANIM_GRAPH_FORMAT_VERSION:
 *
 * The .ranim format version this build writes (and the newest it reads).
 */
#define LRG_ANIM_GRAPH_FORMAT_VERSION (1)

/* ==========================================================================
 * LrgAnimGraph
 * ========================================================================== */

#define LRG_TYPE_ANIM_GRAPH (lrg_anim_graph_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAnimGraph, lrg_anim_graph, LRG, ANIM_GRAPH, GObject)

/**
 * LrgAnimClipResolveFunc:
 * @graph: the graph resolving its clips
 * @clip_name: the clip key (the name under `clips:`)
 * @source: (nullable): the declared source file, relative to the graph's
 *   base directory (see lrg_anim_graph_get_base_dir())
 * @animation: (nullable): the animation name inside @source
 * @user_data: data given to lrg_anim_graph_set_clip_resolver()
 * @error: return location for a #GError
 *
 * Loads one clip for lrg_anim_graph_resolve_clips().
 *
 * Returns: (transfer full) (nullable): the clip, or %NULL with @error set
 */
typedef LrgAnimationClip * (*LrgAnimClipResolveFunc) (LrgAnimGraph  *graph,
                                                      const gchar   *clip_name,
                                                      const gchar   *source,
                                                      const gchar   *animation,
                                                      gpointer       user_data,
                                                      GError       **error);

/**
 * lrg_anim_graph_new:
 *
 * Creates an empty graph (no parameters, no layers).
 *
 * Returns: (transfer full): a new #LrgAnimGraph
 */
LRG_AVAILABLE_IN_ALL
LrgAnimGraph * lrg_anim_graph_new (void);

/**
 * lrg_anim_graph_new_from_file:
 * @path: a .ranim file
 * @error: return location for a #GError
 *
 * Loads a graph; its base directory becomes the file's directory.
 *
 * Returns: (transfer full) (nullable): the graph, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgAnimGraph * lrg_anim_graph_new_from_file (const gchar  *path,
                                             GError      **error);

/**
 * lrg_anim_graph_new_from_data:
 * @data: .ranim YAML text
 * @length: length of @data, or -1 when NUL-terminated
 * @error: return location for a #GError
 *
 * Parses a graph from text.
 *
 * Returns: (transfer full) (nullable): the graph, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgAnimGraph * lrg_anim_graph_new_from_data (const gchar  *data,
                                             gssize        length,
                                             GError      **error);

/**
 * lrg_anim_graph_load_from_file:
 * @self: a graph
 * @path: a .ranim file
 * @error: return location for a #GError
 *
 * Replaces the graph's contents with the file's. Parsing is strict:
 * unknown enum values, wrong value types, duplicate names and references
 * to missing states or parameters are errors; unknown keys are kept and
 * written back by lrg_anim_graph_to_data(). On error the graph is left
 * unchanged.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_anim_graph_load_from_file (LrgAnimGraph  *self,
                                        const gchar   *path,
                                        GError       **error);

/**
 * lrg_anim_graph_load_from_data:
 * @self: a graph
 * @data: .ranim YAML text
 * @length: length of @data, or -1 when NUL-terminated
 * @error: return location for a #GError
 *
 * Like lrg_anim_graph_load_from_file() from text (the base directory is
 * kept).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_anim_graph_load_from_data (LrgAnimGraph  *self,
                                        const gchar   *data,
                                        gssize         length,
                                        GError       **error);

/**
 * lrg_anim_graph_to_data:
 * @self: a graph
 * @error: return location for a #GError
 *
 * Writes the graph as .ranim text, built from the live runtime objects
 * (the layers' state machines, states, blend trees and transitions), so
 * edits made through their APIs are saved. The output is canonical: a
 * file this function wrote loads and writes back byte for byte.
 *
 * Returns: (transfer full) (nullable): the text, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gchar *  lrg_anim_graph_to_data        (LrgAnimGraph  *self,
                                        GError       **error);

/**
 * lrg_anim_graph_save_to_file:
 * @self: a graph
 * @path: destination path
 * @error: return location for a #GError
 *
 * Writes lrg_anim_graph_to_data() to @path.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_anim_graph_save_to_file   (LrgAnimGraph  *self,
                                        const gchar   *path,
                                        GError       **error);

/**
 * lrg_anim_graph_duplicate:
 * @self: a graph
 * @error: return location for a #GError
 *
 * Makes an independent copy (through the format) with the same base
 * directory, resolver and bound clips (the clip objects are shared, they
 * are immutable at runtime). Use one copy per character: the runtime
 * state (current states, times, parameter values) lives in the graph.
 *
 * Returns: (transfer full) (nullable): the copy, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgAnimGraph * lrg_anim_graph_duplicate (LrgAnimGraph  *self,
                                         GError       **error);

/**
 * lrg_anim_graph_get_name:
 * @self: a graph
 *
 * Returns: (transfer none) (nullable): the graph's display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_anim_graph_get_name (LrgAnimGraph *self);

/**
 * lrg_anim_graph_set_name:
 * @self: a graph
 * @name: (nullable): the display name
 *
 * Sets the display name.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_set_name       (LrgAnimGraph *self,
                                        const gchar  *name);

/**
 * lrg_anim_graph_get_base_dir:
 * @self: a graph
 *
 * Gets the directory clip sources are resolved against (the loaded
 * file's directory).
 *
 * Returns: (transfer none) (nullable): the directory
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_anim_graph_get_base_dir (LrgAnimGraph *self);

/**
 * lrg_anim_graph_set_base_dir:
 * @self: a graph
 * @dir: (nullable): a directory
 *
 * Sets the directory clip sources are resolved against.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_set_base_dir   (LrgAnimGraph *self,
                                        const gchar  *dir);

/* --- parameters --------------------------------------------------------- */

/**
 * lrg_anim_graph_add_parameter:
 * @self: a graph
 * @name: the parameter name
 * @type: its type
 * @default_value: (nullable): the default (a double for float, int32 for
 *   int, boolean for bool and trigger), or %NULL for 0 / %FALSE; floating
 *   references are sunk
 *
 * Declares a parameter (or redeclares one with a new type/default) on
 * every layer's state machine. All layers share one set of parameter
 * values.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_add_parameter  (LrgAnimGraph         *self,
                                        const gchar          *name,
                                        LrgAnimParameterType  type,
                                        GVariant             *default_value);

/**
 * lrg_anim_graph_remove_parameter:
 * @self: a graph
 * @name: the parameter name
 *
 * Removes a parameter declaration (conditions that use it then fail to
 * validate).
 *
 * Returns: %TRUE if it was declared
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_anim_graph_remove_parameter (LrgAnimGraph *self,
                                          const gchar  *name);

/**
 * lrg_anim_graph_get_parameter_count:
 * @self: a graph
 *
 * Returns: the number of declared parameters
 */
LRG_AVAILABLE_IN_ALL
guint    lrg_anim_graph_get_parameter_count (LrgAnimGraph *self);

/**
 * lrg_anim_graph_get_parameter_name:
 * @self: a graph
 * @index: a parameter index, in declaration order
 *
 * Returns: (transfer none) (nullable): the name, %NULL out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_anim_graph_get_parameter_name (LrgAnimGraph *self,
                                                 guint         index);

/**
 * lrg_anim_graph_get_parameter_type:
 * @self: a graph
 * @name: a parameter name
 *
 * Returns: the declared type (%LRG_ANIM_PARAMETER_FLOAT when undeclared)
 */
LRG_AVAILABLE_IN_ALL
LrgAnimParameterType lrg_anim_graph_get_parameter_type (LrgAnimGraph *self,
                                                        const gchar  *name);

/**
 * lrg_anim_graph_get_parameter_default:
 * @self: a graph
 * @name: a parameter name
 *
 * Returns: (transfer none) (nullable): the declared default
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_anim_graph_get_parameter_default (LrgAnimGraph *self,
                                                 const gchar  *name);

/**
 * lrg_anim_graph_set_float:
 * @self: a graph
 * @name: a float parameter
 * @value: the value
 *
 * Sets a float parameter (seen by every layer).
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_set_float      (LrgAnimGraph *self,
                                        const gchar  *name,
                                        gfloat        value);

/**
 * lrg_anim_graph_get_float:
 * @self: a graph
 * @name: a float parameter
 *
 * Returns: the value (0 when unset)
 */
LRG_AVAILABLE_IN_ALL
gfloat   lrg_anim_graph_get_float      (LrgAnimGraph *self,
                                        const gchar  *name);

/**
 * lrg_anim_graph_set_int:
 * @self: a graph
 * @name: an int parameter
 * @value: the value
 *
 * Sets an int parameter.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_set_int        (LrgAnimGraph *self,
                                        const gchar  *name,
                                        gint          value);

/**
 * lrg_anim_graph_get_int:
 * @self: a graph
 * @name: an int parameter
 *
 * Returns: the value (0 when unset)
 */
LRG_AVAILABLE_IN_ALL
gint     lrg_anim_graph_get_int        (LrgAnimGraph *self,
                                        const gchar  *name);

/**
 * lrg_anim_graph_set_bool:
 * @self: a graph
 * @name: a bool parameter
 * @value: the value
 *
 * Sets a bool parameter.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_set_bool       (LrgAnimGraph *self,
                                        const gchar  *name,
                                        gboolean      value);

/**
 * lrg_anim_graph_get_bool:
 * @self: a graph
 * @name: a bool or trigger parameter
 *
 * Returns: the value (%FALSE when unset)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_anim_graph_get_bool       (LrgAnimGraph *self,
                                        const gchar  *name);

/**
 * lrg_anim_graph_set_trigger:
 * @self: a graph
 * @name: a trigger parameter
 *
 * Raises a trigger; the first transition that fires because of it
 * consumes (resets) it.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_set_trigger    (LrgAnimGraph *self,
                                        const gchar  *name);

/**
 * lrg_anim_graph_reset_trigger:
 * @self: a graph
 * @name: a trigger parameter
 *
 * Lowers a trigger that no transition consumed.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_reset_trigger  (LrgAnimGraph *self,
                                        const gchar  *name);

/* --- clips -------------------------------------------------------------- */

/**
 * lrg_anim_graph_declare_clip:
 * @self: a graph
 * @name: the clip key states refer to
 * @source: (nullable): the file the clip comes from (a .gltf/.glb),
 *   relative to the base directory
 * @animation: (nullable): the animation's name in @source (%NULL: the
 *   key itself)
 *
 * Declares (or redeclares) a clip under `clips:`.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_declare_clip   (LrgAnimGraph *self,
                                        const gchar  *name,
                                        const gchar  *source,
                                        const gchar  *animation);

/**
 * lrg_anim_graph_get_clip_count:
 * @self: a graph
 *
 * Returns: the number of declared clips
 */
LRG_AVAILABLE_IN_ALL
guint    lrg_anim_graph_get_clip_count (LrgAnimGraph *self);

/**
 * lrg_anim_graph_get_clip_name:
 * @self: a graph
 * @index: a clip index, in declaration order
 *
 * Returns: (transfer none) (nullable): the clip key, %NULL out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_anim_graph_get_clip_name (LrgAnimGraph *self,
                                            guint         index);

/**
 * lrg_anim_graph_get_clip_source:
 * @self: a graph
 * @name: a clip key
 * @out_animation: (out) (optional) (transfer none) (nullable): the
 *   animation name inside the source
 *
 * Returns: (transfer none) (nullable): the declared source file
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_anim_graph_get_clip_source (LrgAnimGraph  *self,
                                              const gchar   *name,
                                              const gchar  **out_animation);

/**
 * lrg_anim_graph_set_clip:
 * @self: a graph
 * @name: a clip key (declared if it is not)
 * @clip: (nullable): the clip object
 *
 * Binds a clip object to a key and hands it to every state and blend
 * tree child that refers to the key.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_set_clip       (LrgAnimGraph     *self,
                                        const gchar      *name,
                                        LrgAnimationClip *clip);

/**
 * lrg_anim_graph_get_clip:
 * @self: a graph
 * @name: a clip key
 *
 * Returns: (transfer none) (nullable): the bound clip
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationClip * lrg_anim_graph_get_clip (LrgAnimGraph *self,
                                            const gchar  *name);

/**
 * lrg_anim_graph_set_clip_resolver:
 * @self: a graph
 * @func: (nullable) (scope notified) (closure user_data) (destroy destroy): the resolver, or %NULL for the
 *   default (#LrgGltfAnimation on the source file)
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data
 *
 * Sets how lrg_anim_graph_resolve_clips() loads declared clips.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_set_clip_resolver (LrgAnimGraph           *self,
                                           LrgAnimClipResolveFunc  func,
                                           gpointer                user_data,
                                           GDestroyNotify          destroy);

/**
 * lrg_anim_graph_resolve_clips:
 * @self: a graph
 * @error: return location for a #GError
 *
 * Loads every declared clip that has no object yet through the resolver
 * and binds it (lrg_anim_graph_set_clip()). Clips without a source are
 * skipped (bind them yourself). Stops at the first failure.
 *
 * Returns: %TRUE when every clip with a source resolved
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_anim_graph_resolve_clips  (LrgAnimGraph  *self,
                                        GError       **error);

/**
 * lrg_anim_graph_get_missing_clips:
 * @self: a graph
 *
 * Lists the clip keys states or blend tree children refer to that have
 * no clip object bound (the editor's "missing clip" markers).
 *
 * Returns: (transfer full) (array zero-terminated=1): the keys, sorted
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_anim_graph_get_missing_clips (LrgAnimGraph *self);

/* --- layers ------------------------------------------------------------- */

/**
 * lrg_anim_graph_add_layer:
 * @self: a graph
 * @layer: a layer; one without a state machine gets an empty one
 *
 * Appends a layer (layer 0 is the base layer; later layers blend over
 * it in order). The layer's machine shares the graph's parameters.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_add_layer      (LrgAnimGraph      *self,
                                        LrgAnimationLayer *layer);

/**
 * lrg_anim_graph_remove_layer:
 * @self: a graph
 * @name: a layer name
 *
 * Returns: %TRUE if a layer was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_anim_graph_remove_layer   (LrgAnimGraph *self,
                                        const gchar  *name);

/**
 * lrg_anim_graph_get_layer_count:
 * @self: a graph
 *
 * Returns: the number of layers
 */
LRG_AVAILABLE_IN_ALL
guint    lrg_anim_graph_get_layer_count (LrgAnimGraph *self);

/**
 * lrg_anim_graph_get_layer:
 * @self: a graph
 * @index: a layer index
 *
 * Returns: (transfer none) (nullable): the layer, %NULL out of range
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationLayer * lrg_anim_graph_get_layer (LrgAnimGraph *self,
                                              guint         index);

/**
 * lrg_anim_graph_get_layer_by_name:
 * @self: a graph
 * @name: a layer name
 *
 * Returns: (transfer none) (nullable): the layer
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationLayer * lrg_anim_graph_get_layer_by_name (LrgAnimGraph *self,
                                                      const gchar  *name);

/**
 * lrg_anim_graph_get_machine:
 * @self: a graph
 *
 * Shortcut for the base layer's state machine.
 *
 * Returns: (transfer none) (nullable): layer 0's machine
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationStateMachine * lrg_anim_graph_get_machine (LrgAnimGraph *self);

/* --- root motion -------------------------------------------------------- */

/**
 * lrg_anim_graph_get_root_motion_bone:
 * @self: a graph
 *
 * Returns: (transfer none) (nullable): the bone root motion is taken from
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_anim_graph_get_root_motion_bone (LrgAnimGraph *self);

/**
 * lrg_anim_graph_set_root_motion_bone:
 * @self: a graph
 * @bone_name: (nullable): the root motion bone (normally the hips or the
 *   skeleton root)
 *
 * Sets the bone root motion is taken from.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_set_root_motion_bone (LrgAnimGraph *self,
                                              const gchar  *bone_name);

/**
 * lrg_anim_graph_get_root_motion_mode:
 * @self: a graph
 *
 * Returns: how root motion is extracted
 */
LRG_AVAILABLE_IN_ALL
LrgRootMotionMode lrg_anim_graph_get_root_motion_mode (LrgAnimGraph *self);

/**
 * lrg_anim_graph_set_root_motion_mode:
 * @self: a graph
 * @mode: the extraction mode
 *
 * Sets which translation axes are extracted.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_set_root_motion_mode (LrgAnimGraph      *self,
                                              LrgRootMotionMode  mode);

/**
 * lrg_anim_graph_get_root_motion_rotation:
 * @self: a graph
 *
 * Returns: whether rotation about +Y is extracted too
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_anim_graph_get_root_motion_rotation (LrgAnimGraph *self);

/**
 * lrg_anim_graph_set_root_motion_rotation:
 * @self: a graph
 * @rotation: whether to extract yaw
 *
 * Sets whether the root bone's turn about +Y is extracted (turn-in-place
 * clips then rotate the entity).
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_set_root_motion_rotation (LrgAnimGraph *self,
                                                  gboolean      rotation);

/**
 * lrg_anim_graph_get_apply_root_motion:
 * @self: a graph
 *
 * Returns: whether an #LrgAnimatorComponent3D moves its entity by the
 *   extracted motion (the default for the component's
 *   #LrgAnimatorComponent3D:apply-root-motion)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_anim_graph_get_apply_root_motion (LrgAnimGraph *self);

/**
 * lrg_anim_graph_set_apply_root_motion:
 * @self: a graph
 * @apply: whether the entity follows the extracted motion
 *
 * Sets the `apply` flag of the root motion block.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_set_apply_root_motion (LrgAnimGraph *self,
                                               gboolean      apply);

/* --- IK post-pass ------------------------------------------------------- */

/**
 * lrg_anim_graph_add_ik_goal:
 * @self: a graph
 * @goal: an IK goal (its name must be unique)
 *
 * Appends a goal to the IK post-pass (goals run in order after the
 * layers are blended).
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_add_ik_goal    (LrgAnimGraph *self,
                                        LrgIKGoal    *goal);

/**
 * lrg_anim_graph_remove_ik_goal:
 * @self: a graph
 * @name: a goal name
 *
 * Returns: %TRUE if a goal was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_anim_graph_remove_ik_goal (LrgAnimGraph *self,
                                        const gchar  *name);

/**
 * lrg_anim_graph_get_ik_goal_count:
 * @self: a graph
 *
 * Returns: the number of IK goals
 */
LRG_AVAILABLE_IN_ALL
guint    lrg_anim_graph_get_ik_goal_count (LrgAnimGraph *self);

/**
 * lrg_anim_graph_get_ik_goal:
 * @self: a graph
 * @index: a goal index
 *
 * Returns: (transfer none) (nullable): the goal, %NULL out of range
 */
LRG_AVAILABLE_IN_ALL
LrgIKGoal * lrg_anim_graph_get_ik_goal (LrgAnimGraph *self,
                                        guint         index);

/**
 * lrg_anim_graph_get_ik_goal_by_name:
 * @self: a graph
 * @name: a goal name
 *
 * Returns: (transfer none) (nullable): the goal
 */
LRG_AVAILABLE_IN_ALL
LrgIKGoal * lrg_anim_graph_get_ik_goal_by_name (LrgAnimGraph *self,
                                                const gchar  *name);

/* --- runtime ------------------------------------------------------------ */

/**
 * lrg_anim_graph_validate:
 * @self: a graph
 * @error: return location for a #GError
 *
 * Checks references: every layer machine has a default state that
 * exists, transitions name existing states, conditions name declared
 * parameters with a compatible comparison, blend trees name float
 * parameters, layer masks and IK goals name bones of @skeleton (when
 * given). Loading validates everything but bones.
 *
 * Returns: %TRUE when valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_anim_graph_validate       (LrgAnimGraph  *self,
                                        GError       **error);

/**
 * lrg_anim_graph_validate_skeleton:
 * @self: a graph
 * @skeleton: the skeleton the graph will drive
 * @error: return location for a #GError
 *
 * Checks that layer masks, the root motion bone and IK goal bones exist
 * in @skeleton.
 *
 * Returns: %TRUE when every bone name resolves
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_anim_graph_validate_skeleton (LrgAnimGraph  *self,
                                           LrgSkeleton   *skeleton,
                                           GError       **error);

/**
 * lrg_anim_graph_start:
 * @self: a graph
 *
 * Resets the parameters to their defaults and starts every layer's
 * machine in its default state.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_start          (LrgAnimGraph *self);

/**
 * lrg_anim_graph_is_started:
 * @self: a graph
 *
 * Returns: whether lrg_anim_graph_start() ran
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_anim_graph_is_started     (LrgAnimGraph *self);

/**
 * lrg_anim_graph_update:
 * @self: a graph
 * @delta_time: seconds since the last update
 *
 * Advances every layer's machine (times, transitions, events). Starts
 * the graph first if needed.
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_update         (LrgAnimGraph *self,
                                        gfloat        delta_time);

/**
 * lrg_anim_graph_evaluate:
 * @self: a graph
 * @skeleton: the skeleton the pose is for
 * @poses: (array length=n_poses) (out caller-allocates): local poses
 * @n_poses: number of entries (normally the bone count)
 * @out_root_translation: (out caller-allocates) (array fixed-size=3)
 *   (optional): the root motion of the last update, in the root bone's
 *   parent space (model space for a root bone)
 * @out_root_yaw: (out) (optional): the extracted turn, radians
 *
 * Blends the layers into @poses (bind pose, then each enabled layer:
 * override layers lerp toward their pose by weight x mask, additive
 * layers add their difference from the bind pose) and takes the root
 * motion out of the root bone per the root motion settings. Does not
 * touch the skeleton (see lrg_anim_pose_apply()) and does not run IK
 * (see lrg_ik_goal_apply()).
 */
LRG_AVAILABLE_IN_ALL
void     lrg_anim_graph_evaluate       (LrgAnimGraph *self,
                                        LrgSkeleton  *skeleton,
                                        LrgBonePose  *poses,
                                        guint         n_poses,
                                        gfloat       *out_root_translation,
                                        gfloat       *out_root_yaw);

G_END_DECLS
