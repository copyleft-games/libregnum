/* lrg-gltf-animation.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGltfAnimation - imports the animation data of a glTF 2.0 / GLB file
 * into the engine's animation types: skins become #LrgSkeleton objects
 * (hierarchy, bind pose, inverse bind matrices), animations become
 * #LrgAnimationClip objects (translation/rotation/scale channels with
 * step, linear and cubic spline interpolation, plus morph target weight
 * channels) and skinned or morphing meshes become #LrgSkinnedMesh objects
 * (bone weights, inverse binds, #LrgMorphTargets). Parsing goes through
 * the cgltf already compiled into raylib; no graphics context is needed.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "lrg-skeleton.h"
#include "lrg-animation-clip.h"
#include "../graphics/lrg-skinned-mesh.h"

G_BEGIN_DECLS

#define LRG_TYPE_GLTF_ANIMATION (lrg_gltf_animation_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGltfAnimation, lrg_gltf_animation, LRG, GLTF_ANIMATION, GObject)

/**
 * lrg_gltf_animation_new_from_file:
 * @path: (type filename): a .gltf or .glb file
 * @error: return location for a #GError
 *
 * Parses a glTF file and loads its buffers (the GLB binary chunk,
 * "data:" URIs, and external files relative to the file's directory;
 * absolute paths, "://" URLs and ".." components are refused). The file
 * is validated (accessor bounds, references) before anything is built.
 *
 * Errors: %G_FILE_ERROR_NOENT when the file is missing, otherwise
 * %LRG_GLTF_ERROR (%LRG_GLTF_ERROR_INVALID_JSON for a file cgltf cannot
 * parse, %LRG_GLTF_ERROR_INVALID_DATA for failed validation or buffers,
 * %LRG_GLTF_ERROR_UNSUPPORTED for refused URIs).
 *
 * Returns: (transfer full) (nullable): the importer, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgGltfAnimation *
lrg_gltf_animation_new_from_file (const gchar  *path,
                                  GError      **error);

/**
 * lrg_gltf_animation_new_from_bytes:
 * @data: GLB or glTF JSON contents (referenced while the importer lives)
 * @base_dir: (type filename) (nullable): directory external buffer URIs
 *   are relative to; %NULL refuses external buffers with
 *   %LRG_GLTF_ERROR_UNSUPPORTED
 * @error: return location for a #GError
 *
 * Same as lrg_gltf_animation_new_from_file() for data in memory.
 *
 * Returns: (transfer full) (nullable): the importer, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgGltfAnimation *
lrg_gltf_animation_new_from_bytes (GBytes       *data,
                                   const gchar  *base_dir,
                                   GError      **error);

/**
 * lrg_gltf_animation_get_node_count:
 * @self: an #LrgGltfAnimation
 *
 * Returns: the number of nodes in the file
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_animation_get_node_count (LrgGltfAnimation *self);

/**
 * lrg_gltf_animation_get_node_name:
 * @self: an #LrgGltfAnimation
 * @node: node index
 *
 * The name a node gets as a bone, a clip track and a morph target group:
 * its glTF "name", "node_<index>" when it has none, and "<name>_<index>"
 * when an earlier node already uses the name (so every node is unique).
 *
 * Returns: (transfer none) (nullable): the name, %NULL when out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_gltf_animation_get_node_name (LrgGltfAnimation *self,
                                  guint             node);

/**
 * lrg_gltf_animation_get_skin_count:
 * @self: an #LrgGltfAnimation
 *
 * Returns: the number of skins
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_animation_get_skin_count (LrgGltfAnimation *self);

/**
 * lrg_gltf_animation_get_skin_name:
 * @self: an #LrgGltfAnimation
 * @skin: skin index
 *
 * Returns: (transfer none) (nullable): the skin's "name", or
 *   "skin_<index>" when unnamed; %NULL when out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_gltf_animation_get_skin_name (LrgGltfAnimation *self,
                                  guint             skin);

/**
 * lrg_gltf_animation_get_skin_joint_count:
 * @self: an #LrgGltfAnimation
 * @skin: skin index
 *
 * Returns: the skin's joints (the bone count of its skeleton), 0 when out
 *   of range
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_animation_get_skin_joint_count (LrgGltfAnimation *self,
                                         guint             skin);

/**
 * lrg_gltf_animation_create_skeleton:
 * @self: an #LrgGltfAnimation
 * @skin: skin index
 * @error: return location for a #GError
 *
 * Builds the skin's skeleton. Bone i is joint i of the skin (so bone
 * indices match the JOINTS_0 indices of the skinned meshes), named by
 * lrg_gltf_animation_get_node_name(). A bone's parent is its nearest
 * ancestor node that is a joint of the same skin. The bind (and current
 * local) pose is the node's rest transform; for a root joint the world
 * transform of its non-joint ancestors (an "Armature" node with a
 * rotation and scale, typically) is folded in, so world poses are scene
 * space as glTF skinning expects; the same folding is applied to clip
 * channels of those nodes by lrg_gltf_animation_create_clip(). Inverse
 * bind matrices come from the skin (identity when absent). World poses
 * are calculated.
 *
 * Returns: (transfer full) (nullable): the skeleton, or %NULL when @skin
 *   is out of range (%LRG_GLTF_ERROR_INVALID_REFERENCE)
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton *
lrg_gltf_animation_create_skeleton (LrgGltfAnimation  *self,
                                    guint              skin,
                                    GError           **error);

/**
 * lrg_gltf_animation_get_clip_count:
 * @self: an #LrgGltfAnimation
 *
 * Returns: the number of animations
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_animation_get_clip_count (LrgGltfAnimation *self);

/**
 * lrg_gltf_animation_get_clip_name:
 * @self: an #LrgGltfAnimation
 * @clip: animation index
 *
 * Returns: (transfer none) (nullable): the animation's "name", or
 *   "clip_<index>" when unnamed; %NULL when out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_gltf_animation_get_clip_name (LrgGltfAnimation *self,
                                  guint             clip);

/**
 * lrg_gltf_animation_find_clip:
 * @self: an #LrgGltfAnimation
 * @name: a clip name
 *
 * Returns: the index of the animation called @name, or -1
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_animation_find_clip (LrgGltfAnimation *self,
                              const gchar      *name);

/**
 * lrg_gltf_animation_create_clip:
 * @self: an #LrgGltfAnimation
 * @clip: animation index
 * @error: return location for a #GError
 *
 * Converts one animation. Every animated node gets one track named by
 * lrg_gltf_animation_get_node_name() whose translation, rotation and
 * scale channels keep the glTF keys and interpolation exactly
 * (lrg_animation_clip_set_track_channel()); components without a channel
 * come from the node's rest transform (the track's base pose). "weights"
 * channels become morph weight channels whose target is the node's name
 * (lrg_animation_clip_add_weights_channel()). The duration is the last
 * key time; the loop mode is %LRG_ANIMATION_LOOP_REPEAT.
 *
 * Errors: %LRG_GLTF_ERROR_INVALID_REFERENCE for a bad index,
 * %LRG_GLTF_ERROR_INVALID_DATA for a sampler whose key and value counts
 * disagree.
 *
 * Returns: (transfer full) (nullable): the clip, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationClip *
lrg_gltf_animation_create_clip (LrgGltfAnimation  *self,
                                guint              clip,
                                GError           **error);

/**
 * lrg_gltf_animation_create_clips:
 * @self: an #LrgGltfAnimation
 * @error: return location for a #GError
 *
 * Converts every animation (lrg_gltf_animation_create_clip()), in file
 * order.
 *
 * Returns: (transfer full) (element-type LrgAnimationClip) (nullable):
 *   the clips (an empty array for a file without animations), or %NULL
 *   on error
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_gltf_animation_create_clips (LrgGltfAnimation  *self,
                                 GError           **error);

/**
 * lrg_gltf_animation_create_skinned_mesh:
 * @self: an #LrgGltfAnimation
 * @skin: skin index
 * @error: return location for a #GError
 *
 * Merges the triangle primitives of every node that uses @skin into one
 * #LrgSkinnedMesh with one bone per joint: mesh-space POSITION, NORMAL,
 * TEXCOORD_0, COLOR_0, JOINTS_0/WEIGHTS_0 (four influences) and the
 * skin's inverse bind matrices. Morph targets of those meshes become an
 * #LrgMorphTargets on the mesh, one group per node (named like the node,
 * targets named from the mesh's "extras.targetNames" when present),
 * weights starting at the node's or mesh's "weights". The palette starts
 * at the bind pose of lrg_gltf_animation_create_skeleton().
 *
 * Errors: %LRG_GLTF_ERROR_INVALID_REFERENCE for a bad index,
 * %LRG_GLTF_ERROR_INVALID_DATA when no triangle primitive uses the skin,
 * the merged mesh has more than 65535 vertices or the skin more than 255
 * joints.
 *
 * Returns: (transfer full) (nullable): the mesh, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgSkinnedMesh *
lrg_gltf_animation_create_skinned_mesh (LrgGltfAnimation  *self,
                                        guint              skin,
                                        GError           **error);

/**
 * lrg_gltf_animation_get_mesh_count:
 * @self: an #LrgGltfAnimation
 *
 * Returns: the number of glTF meshes
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_animation_get_mesh_count (LrgGltfAnimation *self);

/**
 * lrg_gltf_animation_get_mesh_morph_target_count:
 * @self: an #LrgGltfAnimation
 * @mesh: mesh index
 *
 * Returns: the mesh's morph targets (those of its first primitive), 0
 *   when out of range
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_animation_get_mesh_morph_target_count (LrgGltfAnimation *self,
                                                guint             mesh);

/**
 * lrg_gltf_animation_create_mesh:
 * @self: an #LrgGltfAnimation
 * @mesh: mesh index
 * @error: return location for a #GError
 *
 * One glTF mesh as a rigid, single-bone #LrgSkinnedMesh (identity inverse
 * bind; place it with the draw transform) with its morph targets, for
 * blend-shape meshes without a skin. The morph group is the name of the
 * first node that uses the mesh (the target of that node's weight
 * channels), or the mesh's name when no node does.
 *
 * Returns: (transfer full) (nullable): the mesh, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgSkinnedMesh *
lrg_gltf_animation_create_mesh (LrgGltfAnimation  *self,
                                guint              mesh,
                                GError           **error);

/**
 * lrg_gltf_load_skeleton:
 * @path: (type filename): a .gltf or .glb file
 * @skin: skin index (0 for the usual single-character file)
 * @error: return location for a #GError
 *
 * Convenience: lrg_gltf_animation_new_from_file() then
 * lrg_gltf_animation_create_skeleton().
 *
 * Returns: (transfer full) (nullable): the skeleton, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton *
lrg_gltf_load_skeleton (const gchar  *path,
                        guint         skin,
                        GError      **error);

/**
 * lrg_gltf_load_clips:
 * @path: (type filename): a .gltf or .glb file
 * @error: return location for a #GError
 *
 * Convenience: lrg_gltf_animation_new_from_file() then
 * lrg_gltf_animation_create_clips().
 *
 * Returns: (transfer full) (element-type LrgAnimationClip) (nullable): the
 *   clips, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_gltf_load_clips (const gchar  *path,
                     GError      **error);

G_END_DECLS
