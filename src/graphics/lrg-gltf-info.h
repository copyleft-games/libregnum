/* lrg-gltf-info.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGltfInfo - headless inspection of glTF 2.0 / GLB files: the meshes
 * raylib will create (in raylib's exact order, with the node and glTF mesh
 * each came from), skins and skin joints, node transforms, animation
 * names, Draco usage and vertex counts. Needs no graphics context.
 * Inspection never touches raylib; lrg_gltf_info_fix_animation_roots()
 * patches raylib animation data in place.
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

#define LRG_TYPE_GLTF_INFO (lrg_gltf_info_get_type ())

/**
 * LRG_GLTF_RAYLIB_NAME_MAX:
 *
 * raylib stores animation names in a 32-byte buffer, keeping at most this
 * many bytes. See lrg_gltf_info_get_animation_raylib_name().
 */
#define LRG_GLTF_RAYLIB_NAME_MAX (31)

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGltfInfo, lrg_gltf_info, LRG, GLTF_INFO, GObject)

/**
 * lrg_gltf_info_new_from_file:
 * @path: (type filename): a .glb or .gltf file
 * @error: (nullable): return location for a #GError
 *
 * Reads and inspects a glTF file. Binary GLB (magic "glTF", version 2,
 * first chunk JSON) and plain JSON glTF are accepted. External buffers are
 * never opened; only the JSON is inspected.
 *
 * Files that cgltf (and therefore raylib) refuses because of broken
 * structure are rejected with %LRG_GLTF_ERROR_INVALID_REFERENCE: indices
 * out of range among meshes, accessors, buffer views, skins, nodes and
 * scenes, a node with two parents, or a scene root that has a parent.
 * Material, texture and extension references are not checked.
 *
 * Errors: %G_FILE_ERROR when the file cannot be read, otherwise
 * %LRG_GLTF_ERROR.
 *
 * Returns: (transfer full) (nullable): the info, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgGltfInfo *
lrg_gltf_info_new_from_file (const gchar  *path,
                             GError      **error);

/**
 * lrg_gltf_info_new_from_bytes:
 * @data: GLB or glTF JSON file contents
 * @error: (nullable): return location for a #GError
 *
 * Same as lrg_gltf_info_new_from_file() for in-memory data.
 *
 * Returns: (transfer full) (nullable): the info, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgGltfInfo *
lrg_gltf_info_new_from_bytes (GBytes  *data,
                              GError **error);

/**
 * lrg_gltf_read_mesh_names:
 * @path: (type filename): a .glb or .gltf file
 * @error: (nullable): return location for a #GError
 *
 * Convenience wrapper returning the display names of the meshes raylib
 * creates, in raylib mesh index order (see lrg_gltf_info_get_mesh_name()).
 *
 * Returns: (transfer full) (array zero-terminated=1) (nullable): the
 *   names, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gchar **
lrg_gltf_read_mesh_names (const gchar  *path,
                          GError      **error);

/**
 * lrg_gltf_info_get_mesh_count:
 * @self: an #LrgGltfInfo
 *
 * Number of meshes raylib 6.0 LoadGLTF() creates: one per triangle
 * primitive (mode absent or 4) of every node that references a mesh,
 * visiting nodes in array order and ignoring scenes. A glTF mesh used by
 * two nodes therefore appears twice.
 *
 * Returns: the raylib mesh count
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_info_get_mesh_count (LrgGltfInfo *self);

/**
 * lrg_gltf_info_get_mesh_name:
 * @self: an #LrgGltfInfo
 * @index: raylib mesh index
 *
 * Display name of a raylib mesh: the glTF mesh name (or "mesh<N>" with the
 * glTF mesh index when unnamed); when that glTF mesh has more than one
 * primitive, "#<k>" is appended with the primitive's index k inside the
 * glTF mesh, e.g. "Horse#3".
 *
 * Returns: (transfer none) (nullable): the name, or %NULL when @index is
 *   out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_gltf_info_get_mesh_name (LrgGltfInfo *self,
                             guint        index);

/**
 * lrg_gltf_info_get_mesh_source_name:
 * @self: an #LrgGltfInfo
 * @index: raylib mesh index
 *
 * Returns: (transfer none) (nullable): the glTF mesh's own "name", or
 *   %NULL when unnamed or @index is out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_gltf_info_get_mesh_source_name (LrgGltfInfo *self,
                                    guint        index);

/**
 * lrg_gltf_info_get_mesh_node_name:
 * @self: an #LrgGltfInfo
 * @index: raylib mesh index
 *
 * Name of the node that references the mesh. Exporters usually give the
 * node the meaningful name ("Knight_Helmet") while the mesh keeps an
 * editor name ("Cube.124"). All primitives of one node share its name.
 *
 * Returns: (transfer none) (nullable): the node name, or %NULL when the
 *   node is unnamed or @index is out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_gltf_info_get_mesh_node_name (LrgGltfInfo *self,
                                  guint        index);

/**
 * lrg_gltf_info_get_mesh_node_index:
 * @self: an #LrgGltfInfo
 * @index: raylib mesh index
 *
 * Returns: the glTF node index, or -1 when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_info_get_mesh_node_index (LrgGltfInfo *self,
                                   guint        index);

/**
 * lrg_gltf_info_get_mesh_source_index:
 * @self: an #LrgGltfInfo
 * @index: raylib mesh index
 *
 * Returns: the glTF mesh index, or -1 when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_info_get_mesh_source_index (LrgGltfInfo *self,
                                     guint        index);

/**
 * lrg_gltf_info_get_mesh_primitive:
 * @self: an #LrgGltfInfo
 * @index: raylib mesh index
 *
 * Returns: the primitive index inside the glTF mesh, or -1 when @index is
 *   out of range
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_info_get_mesh_primitive (LrgGltfInfo *self,
                                  guint        index);

/**
 * lrg_gltf_info_get_mesh_vertex_count:
 * @self: an #LrgGltfInfo
 * @index: raylib mesh index
 *
 * Returns: the POSITION accessor's count (0 without POSITION or when
 *   @index is out of range)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_info_get_mesh_vertex_count (LrgGltfInfo *self,
                                     guint        index);

/**
 * lrg_gltf_info_get_mesh_material:
 * @self: an #LrgGltfInfo
 * @index: raylib mesh index
 *
 * glTF material index of the primitive. raylib's
 * Model.meshMaterial[@index] is this value + 1 (raylib material 0 is its
 * default material), or 0 when the primitive has no material.
 *
 * Returns: the glTF material index, or -1 for none / out of range
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_info_get_mesh_material (LrgGltfInfo *self,
                                 guint        index);

/**
 * lrg_gltf_info_find_mesh_by_node:
 * @self: an #LrgGltfInfo
 * @node_name: node name to look for
 *
 * Returns: the first raylib mesh index whose node is named @node_name, or
 *   -1
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_info_find_mesh_by_node (LrgGltfInfo *self,
                                 const gchar *node_name);

/**
 * lrg_gltf_info_build_node_mask:
 * @self: an #LrgGltfInfo
 * @hidden_nodes: (array zero-terminated=1) (nullable): node names to hide
 *
 * Builds a per-raylib-mesh visibility mask: one byte per mesh, 1 visible,
 * 0 when the mesh's node name is listed in @hidden_nodes. Names that match
 * no node are ignored. Pass the result to lrg_model_animator_draw_masked().
 *
 * Returns: (transfer full): the mask, lrg_gltf_info_get_mesh_count() bytes
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_gltf_info_build_node_mask (LrgGltfInfo        *self,
                               const gchar *const *hidden_nodes);

/**
 * lrg_gltf_info_get_node_count:
 * @self: an #LrgGltfInfo
 *
 * Returns: the number of glTF nodes
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_info_get_node_count (LrgGltfInfo *self);

/**
 * lrg_gltf_info_get_skin_count:
 * @self: an #LrgGltfInfo
 *
 * raylib loads only the first skin.
 *
 * Returns: the number of skins
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_info_get_skin_count (LrgGltfInfo *self);

/**
 * lrg_gltf_info_get_animation_count:
 * @self: an #LrgGltfInfo
 *
 * Returns: the number of animations (raylib loads them only when the file
 *   has a skin)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_info_get_animation_count (LrgGltfInfo *self);

/**
 * lrg_gltf_info_get_animation_name:
 * @self: an #LrgGltfInfo
 * @index: animation index
 *
 * Returns: (transfer none) (nullable): the full animation name ("" when
 *   unnamed), or %NULL when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_gltf_info_get_animation_name (LrgGltfInfo *self,
                                  guint        index);

/**
 * lrg_gltf_info_get_animation_raylib_name:
 * @self: an #LrgGltfInfo
 * @index: animation index
 *
 * The name exactly as raylib reports it: the first
 * %LRG_GLTF_RAYLIB_NAME_MAX bytes (which may split a UTF-8 sequence).
 *
 * Returns: (transfer full) (nullable): the truncated name, or %NULL when
 *   @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_gltf_info_get_animation_raylib_name (LrgGltfInfo *self,
                                         guint        index);

/**
 * lrg_gltf_info_get_uses_draco:
 * @self: an #LrgGltfInfo
 *
 * Whether KHR_draco_mesh_compression is listed in extensionsUsed /
 * extensionsRequired or on any primitive. raylib 6.0 refuses to load
 * Draco-compressed meshes (the model ends up with no meshes).
 *
 * Returns: %TRUE if Draco compression is used
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gltf_info_get_uses_draco (LrgGltfInfo *self);

/**
 * lrg_gltf_info_get_is_binary:
 * @self: an #LrgGltfInfo
 *
 * Returns: %TRUE for a GLB container, %FALSE for plain JSON glTF
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gltf_info_get_is_binary (LrgGltfInfo *self);

/**
 * lrg_gltf_info_get_joint_count:
 * @self: an #LrgGltfInfo
 *
 * Number of joints of the first skin, the only skin raylib loads. raylib
 * bone k is joint k, so this equals raylib's bone count for the file's
 * animations.
 *
 * Returns: the joint count, 0 when the file has no skin
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_info_get_joint_count (LrgGltfInfo *self);

/**
 * lrg_gltf_info_get_joint_node_index:
 * @self: an #LrgGltfInfo
 * @joint: joint (raylib bone) index
 *
 * Returns: the glTF node index of @joint, or -1 when out of range
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_info_get_joint_node_index (LrgGltfInfo *self,
                                    guint        joint);

/**
 * lrg_gltf_info_get_joint_name:
 * @self: an #LrgGltfInfo
 * @joint: joint (raylib bone) index
 *
 * The full node name of @joint (raylib keeps only 31 bytes of it).
 *
 * Returns: (transfer none) (nullable): the name, or %NULL when unnamed or
 *   out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_gltf_info_get_joint_name (LrgGltfInfo *self,
                              guint        joint);

/**
 * lrg_gltf_info_get_joint_parent:
 * @self: an #LrgGltfInfo
 * @joint: joint (raylib bone) index
 *
 * The raylib parent bone of @joint: the first joint whose node is the
 * parent node of @joint's node. A joint whose parent node is not a joint
 * (or that has no parent node) is a *root* joint.
 *
 * Returns: the parent joint index, or -1 for a root joint or when out of
 *   range
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_gltf_info_get_joint_parent (LrgGltfInfo *self,
                                guint        joint);

/**
 * lrg_gltf_info_get_joint_root_transform:
 * @self: an #LrgGltfInfo
 * @joint: joint (raylib bone) index
 * @out_translation: (out caller-allocates) (array fixed-size=3) (optional):
 *   translation x, y, z
 * @out_rotation: (out caller-allocates) (array fixed-size=4) (optional):
 *   rotation quaternion x, y, z, w
 * @out_scale: (out caller-allocates) (array fixed-size=3) (optional):
 *   scale x, y, z
 *
 * The world transform of @joint's *parent node* (the glTF parent chain
 * composed like cgltf_node_transform_world(), then decomposed like
 * raymath's MatrixDecompose()). For a root joint under an armature node
 * this is the armature transform, e.g. rotation -90 degrees about X and
 * scale 100 for a Blender export. A joint without a parent node gets the
 * identity.
 *
 * Returns: %TRUE on success, %FALSE when @joint is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gltf_info_get_joint_root_transform (LrgGltfInfo *self,
                                        guint        joint,
                                        gfloat      *out_translation,
                                        gfloat      *out_rotation,
                                        gfloat      *out_scale);

/**
 * lrg_gltf_info_fix_animation_roots:
 * @self: an #LrgGltfInfo for the file the clips were loaded from
 * @clips: (element-type GrlModelAnimation): clips loaded by raylib from
 *   the same file, e.g. with grl_model_animation_load()
 *
 * Corrects raylib 6.0's glTF animation loader for skeletons with more than
 * one root joint. raylib applies the world transform of the parent node of
 * joint 0 only to bone 0, so every subtree under another root joint (IK
 * leg targets in Quaternius animals, a separate Head root, ...) is posed
 * without its armature transform and the mesh stretches or twists.
 *
 * For every bone whose model-space pose raylib rooted at a root joint r
 * other than 0, this left-composes W, the world transform of r's parent
 * node (see lrg_gltf_info_get_joint_root_transform()), onto every keyframe
 * pose: rotation = W.rotation * rotation, translation = W.rotation *
 * (W.scale * translation) + W.translation, scale = W.scale * scale. With a
 * uniform W scale (the common case) this equals giving each root its own
 * parent-node transform inside raylib. Bones raylib left unposed because
 * the joints are not topologically sorted are not touched.
 *
 * Each clip is marked once processed and skipped by later calls, so the
 * function is idempotent per clip object. Clips whose bone count differs
 * from lrg_gltf_info_get_joint_count() are skipped and not marked.
 * lrg_asset_manager_load_model_animations() already calls this for .glb and
 * .gltf files.
 *
 * Returns: the number of clips whose poses changed
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gltf_info_fix_animation_roots (LrgGltfInfo *self,
                                   GPtrArray   *clips);

G_END_DECLS
