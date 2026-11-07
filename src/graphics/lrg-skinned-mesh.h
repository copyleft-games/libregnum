/* lrg-skinned-mesh.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkinnedMesh - a skinned (bone-weighted) triangle mesh drawn with a
 * bone matrix palette. Each vertex carries up to four bone indices and
 * weights; a pose is a palette of one matrix per bone, world(bone) *
 * inverse_bind(bone). The palette math and a CPU reference skinning run
 * headlessly; upload() and draw() need a GL context and skin on the GPU
 * (a GLSL 3.30 vertex shader with a uniform matrix array), falling back
 * to CPU skinning into a dynamic vertex buffer when the mesh has more
 * bones than the GPU palette holds.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-material.h"

G_BEGIN_DECLS

/**
 * LRG_SKINNED_MESH_MAX_BONES:
 *
 * Size of the GPU bone palette (the shader's uniform matrix array).
 * Meshes with more bones are skinned on the CPU when drawn.
 */
#define LRG_SKINNED_MESH_MAX_BONES (64)

/**
 * LRG_SKINNED_MESH_INFLUENCES:
 *
 * Bone influences per vertex.
 */
#define LRG_SKINNED_MESH_INFLUENCES (4)

#define LRG_TYPE_MORPH_TARGETS (lrg_morph_targets_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMorphTargets, lrg_morph_targets, LRG, MORPH_TARGETS, GObject)

/**
 * lrg_morph_targets_new:
 * @n_vertices: vertices of the mesh the targets deform (at least 1)
 *
 * Creates an empty set of morph targets (blend shapes). Each target is a
 * per-vertex position delta (and optionally a normal delta); a morphed
 * vertex is base + sum(weight[i] * delta[i]). Targets belong to a group
 * (the glTF importer uses the node name, which is also the target name
 * of the clip's weight channels for that node, see
 * lrg_animation_clip_add_weights_channel()) and have an index inside it.
 *
 * Returns: (transfer full) (nullable): a new #LrgMorphTargets, %NULL
 *   when @n_vertices is 0
 */
LRG_AVAILABLE_IN_ALL
LrgMorphTargets *
lrg_morph_targets_new (guint n_vertices);

/**
 * lrg_morph_targets_add_target:
 * @self: an #LrgMorphTargets
 * @group: (nullable): the group the target belongs to (%NULL: "")
 * @name: (nullable): the target's name (%NULL: "<group>.<index>")
 * @position_deltas: (array) (element-type gfloat): 3 floats per vertex
 * @normal_deltas: (array) (element-type gfloat) (nullable): 3 floats per
 *   vertex
 * @default_weight: the weight before any is set
 *
 * Appends a target; its index inside @group is the number of targets the
 * group had before.
 *
 * Returns: the target's index in the set
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_morph_targets_add_target (LrgMorphTargets *self,
                              const gchar     *group,
                              const gchar     *name,
                              const gfloat    *position_deltas,
                              const gfloat    *normal_deltas,
                              gfloat           default_weight);

/**
 * lrg_morph_targets_get_vertex_count:
 * @self: an #LrgMorphTargets
 *
 * Returns: the vertices every target covers
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_morph_targets_get_vertex_count (LrgMorphTargets *self);

/**
 * lrg_morph_targets_get_target_count:
 * @self: an #LrgMorphTargets
 *
 * Returns: the number of targets
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_morph_targets_get_target_count (LrgMorphTargets *self);

/**
 * lrg_morph_targets_get_target_name:
 * @self: an #LrgMorphTargets
 * @target: target index
 *
 * Returns: (transfer none) (nullable): the target's name, %NULL when out
 *   of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_morph_targets_get_target_name (LrgMorphTargets *self,
                                   guint            target);

/**
 * lrg_morph_targets_get_target_group:
 * @self: an #LrgMorphTargets
 * @target: target index
 *
 * Returns: (transfer none) (nullable): the target's group, %NULL when out
 *   of range
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_morph_targets_get_target_group (LrgMorphTargets *self,
                                    guint            target);

/**
 * lrg_morph_targets_find_target:
 * @self: an #LrgMorphTargets
 * @name: a target name
 *
 * Returns: the first target called @name, or -1
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_morph_targets_find_target (LrgMorphTargets *self,
                               const gchar     *name);

/**
 * lrg_morph_targets_find_group:
 * @self: an #LrgMorphTargets
 * @group: a group name
 * @out_count: (out) (optional): the number of targets in the group
 *
 * Targets of one group are stored next to each other.
 *
 * Returns: the index of the group's first target, or -1
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_morph_targets_find_group (LrgMorphTargets *self,
                              const gchar     *group,
                              guint           *out_count);

/**
 * lrg_morph_targets_get_default_weight:
 * @self: an #LrgMorphTargets
 * @target: target index
 *
 * Returns: the target's default weight (0 when out of range)
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_morph_targets_get_default_weight (LrgMorphTargets *self,
                                      guint            target);

/**
 * lrg_morph_targets_get_position_deltas:
 * @self: an #LrgMorphTargets
 * @target: target index
 *
 * Returns: (transfer none) (nullable) (array) (element-type gfloat): 3
 *   floats per vertex, %NULL when out of range
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_morph_targets_get_position_deltas (LrgMorphTargets *self,
                                       guint            target);

/**
 * lrg_morph_targets_get_normal_deltas:
 * @self: an #LrgMorphTargets
 * @target: target index
 *
 * Returns: (transfer none) (nullable) (array) (element-type gfloat): 3
 *   floats per vertex, %NULL when the target has none or is out of range
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_morph_targets_get_normal_deltas (LrgMorphTargets *self,
                                     guint            target);

/**
 * lrg_morph_targets_apply:
 * @self: an #LrgMorphTargets
 * @weights: (array length=n_weights): one weight per target (missing
 *   ones count as 0)
 * @n_weights: number of weights
 * @base_positions: (array) (element-type gfloat): 3 floats per vertex
 * @base_normals: (array) (element-type gfloat) (nullable): 3 floats per
 *   vertex
 * @out_positions: (array) (element-type gfloat) (out caller-allocates):
 *   3 floats per vertex
 * @out_normals: (array) (element-type gfloat) (out caller-allocates)
 *   (nullable): 3 floats per vertex (renormalised; only written with
 *   @base_normals)
 *
 * The CPU blend: base + sum of weight times delta. Targets with a zero
 * weight cost nothing.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_morph_targets_apply (LrgMorphTargets *self,
                         const gfloat    *weights,
                         guint            n_weights,
                         const gfloat    *base_positions,
                         const gfloat    *base_normals,
                         gfloat          *out_positions,
                         gfloat          *out_normals);

#define LRG_TYPE_SKINNED_MESH (lrg_skinned_mesh_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSkinnedMesh, lrg_skinned_mesh, LRG, SKINNED_MESH, GObject)

/**
 * lrg_skinned_mesh_new:
 * @n_bones: number of bones the vertices may name (at least 1, at most 255)
 *
 * Creates an empty skinned mesh. Every bone starts with an identity
 * inverse bind matrix and the palette starts as identities (the bind
 * pose).
 *
 * Returns: (transfer full) (nullable): a new #LrgSkinnedMesh, or %NULL
 *   when @n_bones is 0 or above 255
 */
LRG_AVAILABLE_IN_ALL
LrgSkinnedMesh *
lrg_skinned_mesh_new (guint n_bones);

/**
 * lrg_skinned_mesh_set_geometry:
 * @self: an #LrgSkinnedMesh
 * @n_vertices: number of vertices (at most 65535)
 * @positions: (array) (element-type gfloat): 3 floats per vertex, bind space
 * @normals: (array) (element-type gfloat) (nullable): 3 floats per vertex
 * @texcoords: (array) (element-type gfloat) (nullable): 2 floats per vertex
 * @colors: (array) (element-type guint8) (nullable): RGBA8 per vertex
 * @bone_indices: (array) (element-type guint8): %LRG_SKINNED_MESH_INFLUENCES
 *   bone indices per vertex
 * @bone_weights: (array) (element-type gfloat): %LRG_SKINNED_MESH_INFLUENCES
 *   weights per vertex (renormalised; a vertex whose weights sum to 0
 *   follows bone 0)
 * @n_indices: number of triangle indices (a multiple of 3)
 * @indices: (array length=n_indices) (element-type guint32): triangle list
 * @error: return location for a #GError
 *
 * Replaces the geometry (copied). Fails with %G_IO_ERROR_INVALID_DATA
 * when a bone index is not below the bone count, an index names a
 * missing vertex, a weight is negative or not finite, or the counts are
 * out of range; the mesh is then unchanged. An uploaded mesh is
 * re-uploaded on the next draw.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_skinned_mesh_set_geometry (LrgSkinnedMesh  *self,
                               guint            n_vertices,
                               const gfloat    *positions,
                               const gfloat    *normals,
                               const gfloat    *texcoords,
                               const guint8    *colors,
                               const guint8    *bone_indices,
                               const gfloat    *bone_weights,
                               guint            n_indices,
                               const guint32   *indices,
                               GError         **error);

/**
 * lrg_skinned_mesh_get_bone_count:
 * @self: an #LrgSkinnedMesh
 *
 * Returns: the number of bones
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_skinned_mesh_get_bone_count (LrgSkinnedMesh *self);

/**
 * lrg_skinned_mesh_get_vertex_count:
 * @self: an #LrgSkinnedMesh
 *
 * Returns: the number of vertices
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_skinned_mesh_get_vertex_count (LrgSkinnedMesh *self);

/**
 * lrg_skinned_mesh_get_triangle_count:
 * @self: an #LrgSkinnedMesh
 *
 * Returns: the number of triangles
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_skinned_mesh_get_triangle_count (LrgSkinnedMesh *self);

/**
 * lrg_skinned_mesh_get_bind_positions:
 * @self: an #LrgSkinnedMesh
 *
 * Returns: (transfer none) (nullable): the bind-space positions (3
 *   floats per vertex), or %NULL without geometry
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_skinned_mesh_get_bind_positions (LrgSkinnedMesh *self);

/**
 * lrg_skinned_mesh_get_weights:
 * @self: an #LrgSkinnedMesh
 * @out_bone_indices: (out) (optional) (transfer none): the bone indices
 *
 * Returns: (transfer none) (nullable): the renormalised weights (4 per
 *   vertex), or %NULL without geometry
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_skinned_mesh_get_weights (LrgSkinnedMesh  *self,
                              const guint8   **out_bone_indices);

/**
 * lrg_skinned_mesh_set_inverse_bind:
 * @self: an #LrgSkinnedMesh
 * @bone: the bone
 * @matrix: (array fixed-size=16): column-major matrix taking a
 *   bind-space position into the bone's space
 *
 * Returns: %FALSE when @bone is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_skinned_mesh_set_inverse_bind (LrgSkinnedMesh *self,
                                   guint           bone,
                                   const gfloat   *matrix);

/**
 * lrg_skinned_mesh_get_inverse_bind:
 * @self: an #LrgSkinnedMesh
 * @bone: the bone
 *
 * Returns: (transfer none) (nullable) (array fixed-size=16): the bone's
 *   inverse bind matrix, or %NULL when @bone is out of range
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_skinned_mesh_get_inverse_bind (LrgSkinnedMesh *self,
                                   guint           bone);

/**
 * lrg_skinned_mesh_compute_palette:
 * @self: an #LrgSkinnedMesh
 * @bone_world: (array) (element-type gfloat): 16 floats per bone, each
 *   bone's posed world (model-space) matrix, column-major
 * @out_palette: (array) (element-type gfloat) (out caller-allocates):
 *   16 floats per bone
 *
 * Computes the skinning palette world(bone) * inverse_bind(bone). It
 * does not change the mesh's current palette.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_skinned_mesh_compute_palette (LrgSkinnedMesh *self,
                                  const gfloat   *bone_world,
                                  gfloat         *out_palette);

/**
 * lrg_skinned_mesh_compute_palette_from_skeleton:
 * @self: an #LrgSkinnedMesh
 * @skeleton: an #LrgSkeleton whose world poses are current
 *   (lrg_skeleton_calculate_world_poses())
 * @out_palette: (array) (element-type gfloat) (out caller-allocates):
 *   16 floats per bone
 *
 * The palette of the skeleton's pose: mesh bone i follows skeleton bone
 * i (lrg_bone_pose_to_matrix() of its world pose).
 *
 * Returns: %FALSE when the skeleton has fewer bones than the mesh
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_skinned_mesh_compute_palette_from_skeleton (LrgSkinnedMesh *self,
                                                LrgSkeleton    *skeleton,
                                                gfloat         *out_palette);

/**
 * lrg_skinned_mesh_set_palette:
 * @self: an #LrgSkinnedMesh
 * @palette: (array) (element-type gfloat): 16 floats per bone
 *
 * Sets the pose drawn and skinned next.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_skinned_mesh_set_palette (LrgSkinnedMesh *self,
                              const gfloat   *palette);

/**
 * lrg_skinned_mesh_get_palette:
 * @self: an #LrgSkinnedMesh
 *
 * Returns: (transfer none) (array) (element-type gfloat): the current
 *   palette, 16 floats per bone
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_skinned_mesh_get_palette (LrgSkinnedMesh *self);

/**
 * lrg_skinned_mesh_skin:
 * @self: an #LrgSkinnedMesh
 * @out_positions: (array) (element-type gfloat) (out caller-allocates):
 *   3 floats per vertex
 * @out_normals: (array) (element-type gfloat) (out caller-allocates) (nullable):
 *   3 floats per vertex (unit length; zero without normals)
 *
 * The CPU reference skinning of the current palette: each vertex is
 * the weighted sum of palette(bone) * position; normals use the
 * palette's upper 3x3 and are renormalised. The GPU shader computes the
 * same sum.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_skinned_mesh_skin (LrgSkinnedMesh *self,
                       gfloat         *out_positions,
                       gfloat         *out_normals);

/**
 * lrg_skinned_mesh_get_skinned_bounds:
 * @self: an #LrgSkinnedMesh
 * @out_min: (array fixed-size=3) (out caller-allocates): minimum corner
 * @out_max: (array fixed-size=3) (out caller-allocates): maximum corner
 *
 * The bounds of the current pose (CPU skinning).
 *
 * Returns: %FALSE without geometry
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_skinned_mesh_get_skinned_bounds (LrgSkinnedMesh *self,
                                     gfloat         *out_min,
                                     gfloat         *out_max);

/**
 * lrg_skinned_mesh_uses_gpu:
 * @self: an #LrgSkinnedMesh
 *
 * Returns: %TRUE when the bones fit the GPU palette
 *   (%LRG_SKINNED_MESH_MAX_BONES); otherwise draw() skins on the CPU
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_skinned_mesh_uses_gpu (LrgSkinnedMesh *self);

/**
 * lrg_skinned_mesh_set_texture:
 * @self: an #LrgSkinnedMesh
 * @texture: (nullable): the albedo texture (referenced), or %NULL for white
 */
LRG_AVAILABLE_IN_ALL
void
lrg_skinned_mesh_set_texture (LrgSkinnedMesh *self,
                              GrlTexture     *texture);

/**
 * lrg_skinned_mesh_set_tint:
 * @self: an #LrgSkinnedMesh
 * @r: red
 * @g: green
 * @b: blue
 * @a: alpha
 *
 * Multiplies the texture (default opaque white).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_skinned_mesh_set_tint (LrgSkinnedMesh *self,
                           guint8          r,
                           guint8          g,
                           guint8          b,
                           guint8          a);

/**
 * lrg_skinned_mesh_set_alpha_cutoff:
 * @self: an #LrgSkinnedMesh
 * @cutoff: fragments with a lower alpha are discarded (0 disables)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_skinned_mesh_set_alpha_cutoff (LrgSkinnedMesh *self,
                                   gfloat          cutoff);

/**
 * lrg_skinned_mesh_set_material:
 * @self: an #LrgSkinnedMesh
 * @material: (nullable): a runtime material (referenced), or %NULL to draw
 *   with the texture, tint and alpha cutoff setters again
 *
 * Draws the mesh with @material: lrg_material_apply() on the mesh's
 * shader before the draw (base colour times the tint, texture slots,
 * PBR parameters, emission, alpha cutoff), the same maps given to
 * DrawMesh(), and lrg_material_apply_state() /
 * lrg_material_restore_state() around it. The material's albedo map
 * replaces lrg_skinned_mesh_set_texture() when it has one.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_skinned_mesh_set_material (LrgSkinnedMesh *self,
                               LrgMaterial    *material);

/**
 * lrg_skinned_mesh_get_material:
 * @self: an #LrgSkinnedMesh
 *
 * Returns: (transfer none) (nullable): the material set with
 *   lrg_skinned_mesh_set_material()
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial *
lrg_skinned_mesh_get_material (LrgSkinnedMesh *self);

/**
 * lrg_skinned_mesh_set_shader:
 * @self: an #LrgSkinnedMesh
 * @shader: (nullable): a shader with the "vertexBoneIndices" and
 *   "vertexBoneWeights" attributes and a "boneMatrices" uniform array
 *   (lrg_skinned_mesh_get_vertex_shader_source() is one), or %NULL for
 *   the built-in one
 */
LRG_AVAILABLE_IN_ALL
void
lrg_skinned_mesh_set_shader (LrgSkinnedMesh *self,
                             GrlShader      *shader);

/**
 * lrg_skinned_mesh_get_vertex_shader_source:
 *
 * Returns: (transfer none): the built-in GLSL 3.30 skinning vertex
 *   shader, to start custom shaders from
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_skinned_mesh_get_vertex_shader_source (void);

/**
 * lrg_skinned_mesh_upload:
 * @self: an #LrgSkinnedMesh
 * @error: return location for a #GError
 *
 * Uploads the geometry and bone attributes. Fails with
 * %G_IO_ERROR_NOT_INITIALIZED without a graphics context and
 * %G_IO_ERROR_INVALID_DATA without geometry.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_skinned_mesh_upload (LrgSkinnedMesh  *self,
                         GError         **error);

/**
 * lrg_skinned_mesh_is_uploaded:
 * @self: an #LrgSkinnedMesh
 *
 * Returns: %TRUE when the current geometry is on the GPU
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_skinned_mesh_is_uploaded (LrgSkinnedMesh *self);

/**
 * lrg_skinned_mesh_unload:
 * @self: an #LrgSkinnedMesh
 *
 * Releases the GPU buffers (the CPU geometry stays).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_skinned_mesh_unload (LrgSkinnedMesh *self);

/**
 * lrg_skinned_mesh_draw:
 * @self: an #LrgSkinnedMesh
 * @transform: (nullable): the model matrix, or %NULL for identity
 *
 * Draws the current pose inside a 3D mode, uploading first when needed
 * (an upload failure draws nothing).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_skinned_mesh_draw (LrgSkinnedMesh  *self,
                       const GrlMatrix *transform);

/**
 * lrg_skinned_mesh_set_inverse_binds_from_skeleton:
 * @self: an #LrgSkinnedMesh
 * @skeleton: an #LrgSkeleton
 *
 * Copies the skeleton's inverse bind matrices
 * (lrg_skeleton_get_inverse_bind_matrix()) into the mesh: mesh bone i
 * takes skeleton bone i's. Bones without one keep theirs.
 *
 * Returns: the number of matrices copied
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_skinned_mesh_set_inverse_binds_from_skeleton (LrgSkinnedMesh *self,
                                                  LrgSkeleton    *skeleton);

/**
 * lrg_skinned_mesh_apply_skeleton:
 * @self: an #LrgSkinnedMesh
 * @skeleton: an #LrgSkeleton whose world poses are current
 *
 * Poses the mesh: lrg_skinned_mesh_compute_palette_from_skeleton() into
 * the mesh's palette (lrg_skinned_mesh_set_palette()). This is the one
 * call an animator makes per frame after posing the skeleton.
 *
 * Returns: %FALSE (palette unchanged) when the skeleton has fewer bones
 *   than the mesh
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_skinned_mesh_apply_skeleton (LrgSkinnedMesh *self,
                                 LrgSkeleton    *skeleton);

/**
 * lrg_skinned_mesh_set_morph_targets:
 * @self: an #LrgSkinnedMesh
 * @targets: (nullable): morph targets with the mesh's vertex count
 *   (referenced), or %NULL to remove them
 *
 * Gives the mesh blend shapes. The weights start at the targets'
 * defaults. Blending runs on the CPU before skinning (the GPU then
 * skins the blended positions, which are re-uploaded when the weights
 * change). Set the geometry first.
 *
 * Returns: %FALSE when the vertex counts differ
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_skinned_mesh_set_morph_targets (LrgSkinnedMesh  *self,
                                    LrgMorphTargets *targets);

/**
 * lrg_skinned_mesh_get_morph_targets:
 * @self: an #LrgSkinnedMesh
 *
 * Returns: (transfer none) (nullable): the morph targets
 */
LRG_AVAILABLE_IN_ALL
LrgMorphTargets *
lrg_skinned_mesh_get_morph_targets (LrgSkinnedMesh *self);

/**
 * lrg_skinned_mesh_set_morph_weights:
 * @self: an #LrgSkinnedMesh
 * @weights: (array length=n_weights): weights from target 0 on
 * @n_weights: number of weights (extra ones are ignored; targets past
 *   them keep their weight)
 *
 * Sets morph weights by target index.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_skinned_mesh_set_morph_weights (LrgSkinnedMesh *self,
                                    const gfloat   *weights,
                                    guint           n_weights);

/**
 * lrg_skinned_mesh_set_morph_weight:
 * @self: an #LrgSkinnedMesh
 * @target: target index
 * @weight: the weight
 *
 * Sets one morph weight.
 *
 * Returns: %FALSE when @target is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_skinned_mesh_set_morph_weight (LrgSkinnedMesh *self,
                                   guint           target,
                                   gfloat          weight);

/**
 * lrg_skinned_mesh_set_morph_group_weights:
 * @self: an #LrgSkinnedMesh
 * @group: a morph target group (a clip weight channel's target, see
 *   lrg_animation_clip_get_weights_channel_target())
 * @weights: (array length=n_weights): weights from the group's first
 *   target on
 * @n_weights: number of weights (clamped to the group's size)
 *
 * Sets the weights of one group, as sampled by
 * lrg_animation_clip_sample_weights().
 *
 * Returns: %FALSE when the mesh has no such group
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_skinned_mesh_set_morph_group_weights (LrgSkinnedMesh *self,
                                          const gchar    *group,
                                          const gfloat   *weights,
                                          guint           n_weights);

/**
 * lrg_skinned_mesh_get_morph_weights:
 * @self: an #LrgSkinnedMesh
 * @n_weights: (out) (optional): number of weights (the target count)
 *
 * Returns: (transfer none) (nullable) (array length=n_weights): the
 *   current weights, %NULL without morph targets
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_skinned_mesh_get_morph_weights (LrgSkinnedMesh *self,
                                    guint          *n_weights);

/**
 * lrg_skinned_mesh_get_morphed_positions:
 * @self: an #LrgSkinnedMesh
 *
 * The bind-space positions with the current morph weights applied
 * (lrg_skinned_mesh_get_bind_positions() without morph targets): what
 * lrg_skinned_mesh_skin() skins.
 *
 * Returns: (transfer none) (nullable): 3 floats per vertex, %NULL
 *   without geometry
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_skinned_mesh_get_morphed_positions (LrgSkinnedMesh *self);

G_END_DECLS
