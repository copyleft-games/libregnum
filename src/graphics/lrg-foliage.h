/* lrg-foliage.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFoliage: scatter layers of meshes, billboards and grass over a
 * terrain.
 *
 * Every #LrgFoliageLayer says what to place (a mesh such as a model file
 * or a built-in procedural tree, a textured billboard card, or grass
 * blades), how densely (instances per square world unit, scaled per
 * surface layer of the terrain and by a paintable per-cell density map),
 * where (slope and height limits) and how it looks from afar (an LOD
 * mesh, a dithered fade).  Placement is a pure function of the terrain,
 * the density maps and the seeds, done per patch of cells so painting
 * re-scatters only what changed.  Drawing is instanced: one draw per
 * layer, patch run and LOD mesh.  A foliage set is normally owned by an
 * #LrgTerrainAsset (saved inside its `.rterrain`); the
 * #LrgTerrainComponent and #LrgFoliageComponent draw it.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LRG_FOLIAGE_MAX_LAYERS:
 *
 * The most scatter layers one #LrgFoliage holds.
 */
#define LRG_FOLIAGE_MAX_LAYERS (16)

/**
 * LRG_FOLIAGE_MAX_PER_CELL:
 *
 * The most instances one layer places in one terrain cell.
 */
#define LRG_FOLIAGE_MAX_PER_CELL (64)

/**
 * LrgFoliageKind:
 * @LRG_FOLIAGE_KIND_MESH: a mesh per instance: a model file (every mesh
 *   of it, with its own diffuse texture) or a built-in procedural tree
 *   (`builtin:conifer`, `builtin:broadleaf`, `builtin:palm`,
 *   `builtin:dead`, `builtin:shrub`)
 * @LRG_FOLIAGE_KIND_BILLBOARD: two crossed, alpha-tested textured cards
 *   per instance (`width` x `height`)
 * @LRG_FOLIAGE_KIND_GRASS: grass blades drawn by an #LrgGrassField
 *   (colour from the layer's colour, height from `height`, swaying)
 *
 * What a scatter layer places.
 */
typedef enum
{
	LRG_FOLIAGE_KIND_MESH,
	LRG_FOLIAGE_KIND_BILLBOARD,
	LRG_FOLIAGE_KIND_GRASS
} LrgFoliageKind;

LRG_AVAILABLE_IN_ALL
GType lrg_foliage_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FOLIAGE_KIND (lrg_foliage_kind_get_type ())

/**
 * LrgFoliageInstance:
 * @position: the foot of the instance in the terrain's local space
 * @yaw: rotation about the instance's up axis, radians
 * @scale: uniform scale
 * @up: (array fixed-size=3): the instance's up axis (world up blended
 *   towards the ground normal by the layer's `align`)
 *
 * One placed instance.
 */
typedef struct
{
	gfloat position[3];
	gfloat yaw;
	gfloat scale;
	gfloat up[3];
} LrgFoliageInstance;

#define LRG_TYPE_FOLIAGE_INSTANCE (lrg_foliage_instance_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_foliage_instance_get_type (void) G_GNUC_CONST;

/**
 * lrg_foliage_instance_copy:
 * @instance: an #LrgFoliageInstance
 *
 * Returns: (transfer full): a copy
 */
LRG_AVAILABLE_IN_ALL
LrgFoliageInstance * lrg_foliage_instance_copy (const LrgFoliageInstance *instance);

/**
 * lrg_foliage_instance_free:
 * @instance: (nullable): an #LrgFoliageInstance
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_instance_free (LrgFoliageInstance *instance);

/**
 * lrg_foliage_instance_get_matrix:
 * @instance: an #LrgFoliageInstance
 * @out: (out caller-allocates) (array fixed-size=16): the column-major
 *   local transform (scale, yaw about @up, tilt to @up, translation)
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_instance_get_matrix (const LrgFoliageInstance *instance,
                                      gfloat                   *out);

/* ==========================================================================
 * LrgFoliageLayer
 * ========================================================================== */

#define LRG_TYPE_FOLIAGE_LAYER (lrg_foliage_layer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFoliageLayer, lrg_foliage_layer, LRG, FOLIAGE_LAYER, GObject)

/**
 * lrg_foliage_layer_new:
 * @name: (nullable): the layer's name
 * @kind: what the layer places
 *
 * Creates a layer with density 0.05 per square unit, scale 0.8 .. 1.2,
 * any slope up to 35 degrees, any height, align 0, a 1 x 1 card, a fade
 * from 80 to 100 units (25 to 35 for grass), no LOD mesh and seed 0.
 *
 * Returns: (transfer full): a new #LrgFoliageLayer
 */
LRG_AVAILABLE_IN_ALL
LrgFoliageLayer * lrg_foliage_layer_new (const gchar    *name,
                                         LrgFoliageKind  kind);

/**
 * lrg_foliage_layer_get_name:
 * @self: an #LrgFoliageLayer
 *
 * Returns: (nullable): the name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_foliage_layer_get_name (LrgFoliageLayer *self);

/**
 * lrg_foliage_layer_get_kind:
 * @self: an #LrgFoliageLayer
 *
 * Returns: what the layer places
 */
LRG_AVAILABLE_IN_ALL
LrgFoliageKind lrg_foliage_layer_get_kind (LrgFoliageLayer *self);

/**
 * lrg_foliage_layer_get_mesh:
 * @self: an #LrgFoliageLayer
 *
 * Returns: (nullable): the mesh reference (%LRG_FOLIAGE_KIND_MESH)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_foliage_layer_get_mesh (LrgFoliageLayer *self);

/**
 * lrg_foliage_layer_set_mesh:
 * @self: an #LrgFoliageLayer
 * @mesh: (nullable): a model path / `guid:` reference or
 *   `builtin:<tree shape nick>`
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_set_mesh (LrgFoliageLayer *self,
                                 const gchar     *mesh);

/**
 * lrg_foliage_layer_get_lod_mesh:
 * @self: an #LrgFoliageLayer
 *
 * Returns: (nullable): the far mesh, used past the LOD distance
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_foliage_layer_get_lod_mesh (LrgFoliageLayer *self);

/**
 * lrg_foliage_layer_set_lod_mesh:
 * @self: an #LrgFoliageLayer
 * @mesh: (nullable): the far mesh reference, %NULL for none
 * @distance: the distance from the eye to a patch at which it switches
 *   (> 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_set_lod_mesh (LrgFoliageLayer *self,
                                     const gchar     *mesh,
                                     gfloat           distance);

/**
 * lrg_foliage_layer_get_lod_distance:
 * @self: an #LrgFoliageLayer
 *
 * Returns: the LOD switch distance
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_foliage_layer_get_lod_distance (LrgFoliageLayer *self);

/**
 * lrg_foliage_layer_get_texture:
 * @self: an #LrgFoliageLayer
 *
 * Returns: (nullable): the card texture (%LRG_FOLIAGE_KIND_BILLBOARD)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_foliage_layer_get_texture (LrgFoliageLayer *self);

/**
 * lrg_foliage_layer_set_texture:
 * @self: an #LrgFoliageLayer
 * @texture: (nullable): a texture path or `guid:` reference
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_set_texture (LrgFoliageLayer *self,
                                    const gchar     *texture);

/**
 * lrg_foliage_layer_get_color:
 * @self: an #LrgFoliageLayer
 * @out: (out caller-allocates) (array fixed-size=4): RGBA, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_get_color (LrgFoliageLayer *self,
                                  gfloat          *out);

/**
 * lrg_foliage_layer_set_color:
 * @self: an #LrgFoliageLayer
 * @r: red
 * @g: green
 * @b: blue
 * @a: alpha
 *
 * The tint (meshes, cards) or the blade tip colour (grass; the base is a
 * darker shade of it).
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_set_color (LrgFoliageLayer *self,
                                  gfloat           r,
                                  gfloat           g,
                                  gfloat           b,
                                  gfloat           a);

/**
 * lrg_foliage_layer_get_density:
 * @self: an #LrgFoliageLayer
 *
 * Returns: instances per square world unit at full density
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_foliage_layer_get_density (LrgFoliageLayer *self);

/**
 * lrg_foliage_layer_set_density:
 * @self: an #LrgFoliageLayer
 * @density: instances per square unit (>= 0; at most
 *   %LRG_FOLIAGE_MAX_PER_CELL per cell are placed)
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_set_density (LrgFoliageLayer *self,
                                    gfloat           density);

/**
 * lrg_foliage_layer_get_material_density:
 * @self: an #LrgFoliageLayer
 * @terrain_layer: a terrain surface layer index
 *
 * Returns: the density factor on that surface layer (1 by default)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_foliage_layer_get_material_density (LrgFoliageLayer *self,
                                               guint            terrain_layer);

/**
 * lrg_foliage_layer_set_material_density:
 * @self: an #LrgFoliageLayer
 * @terrain_layer: a terrain surface layer index (below
 *   %LRG_TERRAIN_ASSET_MAX_LAYERS)
 * @factor: 0 .. 1; the effective density is the density times the sum
 *   of the surface weights at the spot times their factors (trees on
 *   grass only: factor 1 for grass, 0 for the rest)
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_set_material_density (LrgFoliageLayer *self,
                                             guint            terrain_layer,
                                             gfloat           factor);

/**
 * lrg_foliage_layer_get_scale_range:
 * @self: an #LrgFoliageLayer
 * @min: (out) (optional): smallest scale
 * @max: (out) (optional): largest scale
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_get_scale_range (LrgFoliageLayer *self,
                                        gfloat          *min,
                                        gfloat          *max);

/**
 * lrg_foliage_layer_set_scale_range:
 * @self: an #LrgFoliageLayer
 * @min: smallest scale (> 0)
 * @max: largest scale (>= @min)
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_set_scale_range (LrgFoliageLayer *self,
                                        gfloat           min,
                                        gfloat           max);

/**
 * lrg_foliage_layer_get_slope_range:
 * @self: an #LrgFoliageLayer
 * @min: (out) (optional): lowest ground slope, degrees
 * @max: (out) (optional): steepest ground slope, degrees
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_get_slope_range (LrgFoliageLayer *self,
                                        gfloat          *min,
                                        gfloat          *max);

/**
 * lrg_foliage_layer_set_slope_range:
 * @self: an #LrgFoliageLayer
 * @min: lowest slope in degrees (0 .. 90)
 * @max: steepest slope in degrees (@min .. 90)
 *
 * Instances only go where the ground's slope lies in the range.
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_set_slope_range (LrgFoliageLayer *self,
                                        gfloat           min,
                                        gfloat           max);

/**
 * lrg_foliage_layer_get_height_range:
 * @self: an #LrgFoliageLayer
 * @min: (out) (optional): lowest ground height
 * @max: (out) (optional): highest ground height
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_get_height_range (LrgFoliageLayer *self,
                                         gfloat          *min,
                                         gfloat          *max);

/**
 * lrg_foliage_layer_set_height_range:
 * @self: an #LrgFoliageLayer
 * @min: lowest ground height (local)
 * @max: highest ground height (>= @min)
 *
 * Instances only go where the ground height lies in the range (keep
 * trees out of a lake by starting above its level).
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_set_height_range (LrgFoliageLayer *self,
                                         gfloat           min,
                                         gfloat           max);

/**
 * lrg_foliage_layer_get_align:
 * @self: an #LrgFoliageLayer
 *
 * Returns: 0 (stand upright) .. 1 (follow the ground normal)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_foliage_layer_get_align (LrgFoliageLayer *self);

/**
 * lrg_foliage_layer_set_align:
 * @self: an #LrgFoliageLayer
 * @align: 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_set_align (LrgFoliageLayer *self,
                                  gfloat           align);

/**
 * lrg_foliage_layer_get_size:
 * @self: an #LrgFoliageLayer
 * @width: (out) (optional): card width (billboards)
 * @height: (out) (optional): card or blade height (billboards, grass) or
 *   generated tree height (built-in meshes)
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_get_size (LrgFoliageLayer *self,
                                 gfloat          *width,
                                 gfloat          *height);

/**
 * lrg_foliage_layer_set_size:
 * @self: an #LrgFoliageLayer
 * @width: card width (> 0)
 * @height: card / blade / tree height (> 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_set_size (LrgFoliageLayer *self,
                                 gfloat           width,
                                 gfloat           height);

/**
 * lrg_foliage_layer_get_fade:
 * @self: an #LrgFoliageLayer
 * @start: (out) (optional): distance where instances start to dissolve
 * @end: (out) (optional): distance past which nothing is drawn
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_get_fade (LrgFoliageLayer *self,
                                 gfloat          *start,
                                 gfloat          *end);

/**
 * lrg_foliage_layer_set_fade:
 * @self: an #LrgFoliageLayer
 * @start: dissolve start (>= 0)
 * @end: cull distance (> @start)
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_set_fade (LrgFoliageLayer *self,
                                 gfloat           start,
                                 gfloat           end);

/**
 * lrg_foliage_layer_get_seed:
 * @self: an #LrgFoliageLayer
 *
 * Returns: the layer's own seed (mixed with the foliage seed)
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_foliage_layer_get_seed (LrgFoliageLayer *self);

/**
 * lrg_foliage_layer_set_seed:
 * @self: an #LrgFoliageLayer
 * @seed: the layer seed
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_layer_set_seed (LrgFoliageLayer *self,
                                 guint32          seed);

/* ==========================================================================
 * LrgFoliage
 * ========================================================================== */

#define LRG_TYPE_FOLIAGE (lrg_foliage_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFoliage, lrg_foliage, LRG, FOLIAGE, GObject)

/**
 * lrg_foliage_new:
 *
 * Creates an empty foliage set (seed 0, 16-cell patches, no grid until a
 * terrain asset adopts it or lrg_foliage_set_grid() is called).
 *
 * Returns: (transfer full): a new #LrgFoliage
 */
LRG_AVAILABLE_IN_ALL
LrgFoliage * lrg_foliage_new (void);

/**
 * lrg_foliage_get_seed:
 * @self: an #LrgFoliage
 *
 * Returns: the scatter seed
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_foliage_get_seed (LrgFoliage *self);

/**
 * lrg_foliage_set_seed:
 * @self: an #LrgFoliage
 * @seed: the scatter seed
 *
 * Marks every patch for re-scattering.
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_set_seed (LrgFoliage *self,
                           guint32     seed);

/**
 * lrg_foliage_get_patch_cells:
 * @self: an #LrgFoliage
 *
 * Returns: cells along one side of a patch (the unit of re-scattering,
 *   culling and LOD)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_foliage_get_patch_cells (LrgFoliage *self);

/**
 * lrg_foliage_set_patch_cells:
 * @self: an #LrgFoliage
 * @cells: 1 .. 256
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_set_patch_cells (LrgFoliage *self,
                                  guint       cells);

/**
 * lrg_foliage_set_grid:
 * @self: an #LrgFoliage
 * @cells_x: cells along X
 * @cells_z: cells along Z
 * @cell_size: world size of a cell (> 0)
 *
 * Sizes the density maps (an #LrgTerrainAsset does this for its own
 * foliage); kept cells keep their density, new ones start full.
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_set_grid (LrgFoliage *self,
                           guint       cells_x,
                           guint       cells_z,
                           gfloat      cell_size);

/**
 * lrg_foliage_get_grid:
 * @self: an #LrgFoliage
 * @cells_x: (out) (optional): cells along X
 * @cells_z: (out) (optional): cells along Z
 * @cell_size: (out) (optional): world size of a cell
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_get_grid (LrgFoliage *self,
                           guint      *cells_x,
                           guint      *cells_z,
                           gfloat     *cell_size);

/**
 * lrg_foliage_get_n_layers:
 * @self: an #LrgFoliage
 *
 * Returns: the number of scatter layers
 */
LRG_AVAILABLE_IN_ALL
guint lrg_foliage_get_n_layers (LrgFoliage *self);

/**
 * lrg_foliage_get_layer:
 * @self: an #LrgFoliage
 * @index: the layer index
 *
 * Returns: (transfer none) (nullable): the layer
 */
LRG_AVAILABLE_IN_ALL
LrgFoliageLayer * lrg_foliage_get_layer (LrgFoliage *self,
                                         guint       index);

/**
 * lrg_foliage_add_layer:
 * @self: an #LrgFoliage
 * @layer: the layer to append (the foliage takes a reference)
 *
 * Returns: the new layer's index, or -1 at %LRG_FOLIAGE_MAX_LAYERS
 */
LRG_AVAILABLE_IN_ALL
gint lrg_foliage_add_layer (LrgFoliage      *self,
                            LrgFoliageLayer *layer);

/**
 * lrg_foliage_remove_layer:
 * @self: an #LrgFoliage
 * @index: the layer index
 *
 * Returns: %TRUE when a layer was removed (with its density map)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_foliage_remove_layer (LrgFoliage *self,
                                   guint       index);

/**
 * lrg_foliage_get_density:
 * @self: an #LrgFoliage
 * @layer: the layer
 * @x: cell column
 * @z: cell row
 *
 * Returns: the painted density of the cell, 0 .. 1 (0 off the grid)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_foliage_get_density (LrgFoliage *self,
                                guint       layer,
                                guint       x,
                                guint       z);

/**
 * lrg_foliage_set_density:
 * @self: an #LrgFoliage
 * @layer: the layer
 * @x: cell column
 * @z: cell row
 * @density: 0 .. 1 (stored as a byte)
 *
 * Marks the cell's patch for re-scattering.
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_set_density (LrgFoliage *self,
                              guint       layer,
                              guint       x,
                              guint       z,
                              gfloat      density);

/**
 * lrg_foliage_fill_density:
 * @self: an #LrgFoliage
 * @layer: the layer
 * @density: 0 .. 1 for every cell
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_fill_density (LrgFoliage *self,
                               guint       layer,
                               gfloat      density);

/**
 * lrg_foliage_mark_dirty:
 * @self: an #LrgFoliage
 * @x: first cell column
 * @z: first cell row
 * @width: columns
 * @height: rows
 *
 * Marks the patches overlapping a cell region for re-scattering (the
 * terrain asset does this when heights or weights change).
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_mark_dirty (LrgFoliage *self,
                             guint       x,
                             guint       z,
                             guint       width,
                             guint       height);

/**
 * lrg_foliage_get_dirty_patches:
 * @self: an #LrgFoliage
 *
 * Returns: the number of patches waiting to be re-scattered
 */
LRG_AVAILABLE_IN_ALL
guint lrg_foliage_get_dirty_patches (LrgFoliage *self);

/**
 * lrg_foliage_get_instance_count:
 * @self: an #LrgFoliage
 * @layer: the layer
 *
 * Returns: the instances placed for @layer by the last scatter
 */
LRG_AVAILABLE_IN_ALL
guint lrg_foliage_get_instance_count (LrgFoliage *self,
                                      guint       layer);

/**
 * lrg_foliage_get_instance:
 * @self: an #LrgFoliage
 * @layer: the layer
 * @index: the instance, patch by patch in placement order
 * @out: (out caller-allocates): the instance
 *
 * Returns: %TRUE when @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_foliage_get_instance (LrgFoliage         *self,
                                   guint               layer,
                                   guint               index,
                                   LrgFoliageInstance *out);

/**
 * lrg_foliage_add_instance:
 * @self: an #LrgFoliage
 * @layer: the layer
 * @instance: the instance (local space; it goes into the patch under its
 *   position, clamped to the grid)
 *
 * Places an instance by hand, for foliage without a terrain to scatter
 * on (a scatter replaces the instances of the patches it re-scatters).
 * Needs a grid (lrg_foliage_set_grid()).
 *
 * Returns: %TRUE when the instance was added
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_foliage_add_instance (LrgFoliage               *self,
                                   guint                     layer,
                                   const LrgFoliageInstance *instance);

/**
 * lrg_foliage_clear_instances:
 * @self: an #LrgFoliage
 * @layer: the layer
 *
 * Removes every instance of @layer (without marking patches dirty).
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_clear_instances (LrgFoliage *self,
                                  guint       layer);

/**
 * lrg_foliage_release_gpu:
 * @self: an #LrgFoliage
 *
 * Frees the meshes, instance buffers, shader and grass fields while the
 * GL context lives; the next draw makes them again.
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_release_gpu (LrgFoliage *self);

G_END_DECLS
