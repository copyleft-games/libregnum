/* lrg-mesh-renderer-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMeshRendererComponent: draws a built-in primitive or a mesh asset at
 * its #LrgEntity3D's world transform by submitting #LrgRenderItem records
 * to the world's render list (it never draws directly).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../lrg-enums.h"
#include "../../ecs/lrg-component.h"
#include "../lrg-world3d.h"
#include "../../graphics/lrg-material.h"

G_BEGIN_DECLS

#define LRG_TYPE_MESH_RENDERER_COMPONENT (lrg_mesh_renderer_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMeshRendererComponent, lrg_mesh_renderer_component,
                      LRG, MESH_RENDERER_COMPONENT, LrgComponent)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_mesh_renderer_component_new:
 *
 * Creates a mesh renderer drawing a white unit cube
 * (%LRG_PRIMITIVE_CUBE) that casts and receives shadows.
 *
 * Returns: (transfer full): a new #LrgMeshRendererComponent
 */
LRG_AVAILABLE_IN_ALL
LrgMeshRendererComponent * lrg_mesh_renderer_component_new (void);

/**
 * lrg_mesh_renderer_component_new_primitive:
 * @primitive: the built-in shape to draw
 *
 * Creates a mesh renderer drawing @primitive.
 *
 * Returns: (transfer full): a new #LrgMeshRendererComponent
 */
LRG_AVAILABLE_IN_ALL
LrgMeshRendererComponent * lrg_mesh_renderer_component_new_primitive (LrgPrimitiveType primitive);

/**
 * lrg_mesh_renderer_component_new_mesh:
 * @mesh_asset: a mesh asset path or GUID (glTF, GLB, OBJ, ...)
 *
 * Creates a mesh renderer drawing a mesh asset.  The model is resolved
 * through #LrgMeshRendererComponent:asset-manager when one is set,
 * otherwise by the renderer from lrg_mesh_renderer_component_get_mesh_asset().
 *
 * Returns: (transfer full): a new #LrgMeshRendererComponent
 */
LRG_AVAILABLE_IN_ALL
LrgMeshRendererComponent * lrg_mesh_renderer_component_new_mesh (const gchar *mesh_asset);

/* ==========================================================================
 * Geometry
 * ========================================================================== */

/**
 * lrg_mesh_renderer_component_get_primitive:
 * @self: an #LrgMeshRendererComponent
 *
 * Returns: the built-in shape drawn when no mesh asset is set
 */
LRG_AVAILABLE_IN_ALL
LrgPrimitiveType lrg_mesh_renderer_component_get_primitive (LrgMeshRendererComponent *self);

/**
 * lrg_mesh_renderer_component_set_primitive:
 * @self: an #LrgMeshRendererComponent
 * @primitive: the built-in shape
 *
 * Sets the built-in shape.  A mesh asset, when set, wins over it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_set_primitive (LrgMeshRendererComponent *self,
                                                LrgPrimitiveType          primitive);

/**
 * lrg_mesh_renderer_component_get_mesh_asset:
 * @self: an #LrgMeshRendererComponent
 *
 * Returns: (transfer none) (nullable): the mesh asset path or GUID
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_mesh_renderer_component_get_mesh_asset (LrgMeshRendererComponent *self);

/**
 * lrg_mesh_renderer_component_set_mesh_asset:
 * @self: an #LrgMeshRendererComponent
 * @mesh_asset: (nullable): a mesh asset path or GUID, %NULL to draw the
 *   primitive
 *
 * Sets the mesh asset.  Drops a model resolved for the previous asset.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_set_mesh_asset (LrgMeshRendererComponent *self,
                                                 const gchar              *mesh_asset);

/**
 * lrg_mesh_renderer_component_get_model:
 * @self: an #LrgMeshRendererComponent
 *
 * Gets the model submitted with %LRG_RENDER_ITEM_MODEL items: the one set
 * with lrg_mesh_renderer_component_set_model(), or the one resolved from
 * the mesh asset through the asset manager.
 *
 * Returns: (transfer none) (nullable): the model
 */
LRG_AVAILABLE_IN_ALL
GrlModel * lrg_mesh_renderer_component_get_model (LrgMeshRendererComponent *self);

/**
 * lrg_mesh_renderer_component_set_model:
 * @self: an #LrgMeshRendererComponent
 * @model: (nullable): a loaded model to draw
 *
 * Draws an already loaded model (the component keeps a reference).  This
 * overrides the mesh asset's resolution; %NULL resolves the asset again.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_set_model (LrgMeshRendererComponent *self,
                                            GrlModel                 *model);

/**
 * lrg_mesh_renderer_component_get_asset_manager:
 * @self: an #LrgMeshRendererComponent
 *
 * Returns: (transfer none) (nullable): the asset manager mesh assets load
 *   through
 */
LRG_AVAILABLE_IN_ALL
LrgAssetManager * lrg_mesh_renderer_component_get_asset_manager (LrgMeshRendererComponent *self);

/**
 * lrg_mesh_renderer_component_set_asset_manager:
 * @self: an #LrgMeshRendererComponent
 * @manager: (nullable): an asset manager
 *
 * Sets the asset manager used to resolve the mesh asset into a model on
 * the next collect_render() (needs a graphics context; a failed load is
 * not retried until the asset or the manager changes).
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_set_asset_manager (LrgMeshRendererComponent *self,
                                                    LrgAssetManager          *manager);

/**
 * lrg_mesh_renderer_component_get_size:
 * @self: an #LrgMeshRendererComponent
 * @out: (out caller-allocates) (array fixed-size=3): box size x, y, z
 *
 * Gets the box size (#LrgMeshRendererComponent:size-x .. size-z).
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_get_size (LrgMeshRendererComponent *self,
                                           gfloat                   *out);

/**
 * lrg_mesh_renderer_component_set_size:
 * @self: an #LrgMeshRendererComponent
 * @x: width
 * @y: height
 * @z: depth
 *
 * Sets the size of cube, plane, grid and rectangle primitives.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_set_size (LrgMeshRendererComponent *self,
                                           gfloat                    x,
                                           gfloat                    y,
                                           gfloat                    z);

/**
 * lrg_mesh_renderer_component_get_local_bounds:
 * @self: an #LrgMeshRendererComponent
 * @out: (out caller-allocates): bounds in the entity's local space
 *
 * Computes the local bounds of the primitive or of the resolved model.
 *
 * Returns: %TRUE if the bounds are known (%FALSE for an unresolved mesh
 *   asset)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mesh_renderer_component_get_local_bounds (LrgMeshRendererComponent *self,
                                                       LrgBoundingBox3D         *out);

/* ==========================================================================
 * Material
 * ========================================================================== */

/**
 * lrg_mesh_renderer_component_get_color:
 * @self: an #LrgMeshRendererComponent
 * @out: (out caller-allocates) (array fixed-size=4): RGBA, 0 .. 1
 *
 * Gets the base colour.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_get_color (LrgMeshRendererComponent *self,
                                            gfloat                   *out);

/**
 * lrg_mesh_renderer_component_set_color:
 * @self: an #LrgMeshRendererComponent
 * @r: red, 0 .. 1
 * @g: green, 0 .. 1
 * @b: blue, 0 .. 1
 * @a: alpha, 0 .. 1 (below 1 makes the item transparent)
 *
 * Sets the base colour (#LrgMeshRendererComponent:color-r .. color-a).
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_set_color (LrgMeshRendererComponent *self,
                                            gfloat                    r,
                                            gfloat                    g,
                                            gfloat                    b,
                                            gfloat                    a);

/**
 * lrg_mesh_renderer_component_set_emission:
 * @self: an #LrgMeshRendererComponent
 * @r: red, 0 .. 1
 * @g: green, 0 .. 1
 * @b: blue, 0 .. 1
 * @strength: emission multiplier (0 for none)
 *
 * Sets the emissive colour and strength.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_set_emission (LrgMeshRendererComponent *self,
                                               gfloat                    r,
                                               gfloat                    g,
                                               gfloat                    b,
                                               gfloat                    strength);

/**
 * lrg_mesh_renderer_component_get_material:
 * @self: an #LrgMeshRendererComponent
 *
 * Gets the material slot: an explicit material object (an #LrgMaterial,
 * or a legacy #LrgMaterial3D) that replaces the scalar colour,
 * roughness, metallic and emission properties.
 *
 * Returns: (transfer none) (nullable): the material slot
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_mesh_renderer_component_get_material (LrgMeshRendererComponent *self);

/**
 * lrg_mesh_renderer_component_set_material:
 * @self: an #LrgMeshRendererComponent
 * @material: (nullable): a material object, %NULL to use the scalar
 *   material properties
 *
 * Fills the material slot (the component keeps a reference). With an
 * #LrgMaterial the items carry its base colour as their colour, and its
 * render queue, cull mode and shading model decide the transparent,
 * cutout, double-sided and unlit flags (see
 * lrg_mesh_renderer_component_get_flags()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_set_material (LrgMeshRendererComponent *self,
                                               GObject                  *material);

/**
 * lrg_mesh_renderer_component_get_effective_material:
 * @self: an #LrgMeshRendererComponent
 *
 * Gets the material render items carry: the material slot when filled,
 * otherwise an internal #LrgMaterial3D kept in sync with the colour,
 * roughness, metallic and emission properties.
 *
 * Returns: (transfer none): the material
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_mesh_renderer_component_get_effective_material (LrgMeshRendererComponent *self);

/**
 * lrg_mesh_renderer_component_set_submesh_material:
 * @self: an #LrgMeshRendererComponent
 * @index: the model mesh index (raylib mesh order, see #LrgGltfInfo)
 * @material: (nullable): the material of that mesh, %NULL to fall back to
 *   the material slot
 *
 * Gives one mesh of a mesh asset its own material. When any submesh
 * material is set and the model is resolved, the component submits one
 * item per mesh (mesh_index = i) instead of one for the whole model,
 * each with its submesh material or the effective material. Primitives
 * ignore submesh materials.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_set_submesh_material (LrgMeshRendererComponent *self,
                                                       guint                     index,
                                                       LrgMaterial              *material);

/**
 * lrg_mesh_renderer_component_get_submesh_material:
 * @self: an #LrgMeshRendererComponent
 * @index: the model mesh index
 *
 * Returns: (transfer none) (nullable): the material of mesh @index, or
 *   %NULL when it uses the material slot
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial * lrg_mesh_renderer_component_get_submesh_material (LrgMeshRendererComponent *self,
                                                                guint                     index);

/**
 * lrg_mesh_renderer_component_get_n_submesh_materials:
 * @self: an #LrgMeshRendererComponent
 *
 * Returns: one past the highest mesh index with a submesh material (0
 *   when none is set)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_mesh_renderer_component_get_n_submesh_materials (LrgMeshRendererComponent *self);

/**
 * lrg_mesh_renderer_component_clear_submesh_materials:
 * @self: an #LrgMeshRendererComponent
 *
 * Drops every submesh material (one item for the whole model again).
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_clear_submesh_materials (LrgMeshRendererComponent *self);

/* ==========================================================================
 * Render state
 * ========================================================================== */

/**
 * lrg_mesh_renderer_component_get_cast_shadows:
 * @self: an #LrgMeshRendererComponent
 *
 * Returns: %TRUE if items carry %LRG_RENDER_ITEM_FLAG_CAST_SHADOW
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mesh_renderer_component_get_cast_shadows (LrgMeshRendererComponent *self);

/**
 * lrg_mesh_renderer_component_set_cast_shadows:
 * @self: an #LrgMeshRendererComponent
 * @cast_shadows: whether the mesh casts shadows
 *
 * Sets #LrgMeshRendererComponent:cast-shadows.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_set_cast_shadows (LrgMeshRendererComponent *self,
                                                   gboolean                  cast_shadows);

/**
 * lrg_mesh_renderer_component_get_receive_shadows:
 * @self: an #LrgMeshRendererComponent
 *
 * Returns: %TRUE if items carry %LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mesh_renderer_component_get_receive_shadows (LrgMeshRendererComponent *self);

/**
 * lrg_mesh_renderer_component_set_receive_shadows:
 * @self: an #LrgMeshRendererComponent
 * @receive_shadows: whether the mesh is shadowed
 *
 * Sets #LrgMeshRendererComponent:receive-shadows.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_set_receive_shadows (LrgMeshRendererComponent *self,
                                                      gboolean                  receive_shadows);

/**
 * lrg_mesh_renderer_component_get_layer:
 * @self: an #LrgMeshRendererComponent
 *
 * Returns: the render layer override 0 .. 31, or -1 to use the entity's
 */
LRG_AVAILABLE_IN_ALL
gint lrg_mesh_renderer_component_get_layer (LrgMeshRendererComponent *self);

/**
 * lrg_mesh_renderer_component_set_layer:
 * @self: an #LrgMeshRendererComponent
 * @layer: a render layer 0 .. 31, or -1 to use the entity's layer
 *
 * Overrides the render layer of the items.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_set_layer (LrgMeshRendererComponent *self,
                                            gint                      layer);

/**
 * lrg_mesh_renderer_component_get_flags:
 * @self: an #LrgMeshRendererComponent
 *
 * Computes the #LrgRenderItemFlags the items carry from the shadow,
 * transparency, double-sided and unlit properties (an alpha below 1 adds
 * %LRG_RENDER_ITEM_FLAG_TRANSPARENT). When the material slot holds an
 * #LrgMaterial, its resolved render queue adds
 * %LRG_RENDER_ITEM_FLAG_TRANSPARENT or %LRG_RENDER_ITEM_FLAG_CUTOUT, a
 * %LRG_MATERIAL_CULL_NONE cull mode adds
 * %LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED and %LRG_SHADING_MODEL_UNLIT adds
 * %LRG_RENDER_ITEM_FLAG_UNLIT (the component's colour alpha is then not
 * consulted).
 *
 * Returns: the render flags
 */
LRG_AVAILABLE_IN_ALL
LrgRenderItemFlags lrg_mesh_renderer_component_get_flags (LrgMeshRendererComponent *self);

/**
 * lrg_mesh_renderer_component_get_static_lighting:
 * @self: a #LrgMeshRendererComponent
 *
 * Returns: #LrgMeshRendererComponent:static-lighting
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mesh_renderer_component_get_static_lighting (LrgMeshRendererComponent *self);

/**
 * lrg_mesh_renderer_component_set_static_lighting:
 * @self: a #LrgMeshRendererComponent
 * @static_lighting: whether the mesh takes part in the lighting bake
 *
 * Sets #LrgMeshRendererComponent:static-lighting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_set_static_lighting (LrgMeshRendererComponent *self,
                                                      gboolean                  static_lighting);

/**
 * lrg_mesh_renderer_component_get_lightmap_scale:
 * @self: a #LrgMeshRendererComponent
 *
 * Returns: #LrgMeshRendererComponent:lightmap-scale
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_mesh_renderer_component_get_lightmap_scale (LrgMeshRendererComponent *self);

/**
 * lrg_mesh_renderer_component_set_lightmap_scale:
 * @self: a #LrgMeshRendererComponent
 * @scale: multiplier on the bake's texels per unit (0 .. 16; 0 keeps
 *   the mesh out of the lightmaps)
 *
 * Sets #LrgMeshRendererComponent:lightmap-scale.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mesh_renderer_component_set_lightmap_scale (LrgMeshRendererComponent *self,
                                                     gfloat                    scale);

G_END_DECLS
