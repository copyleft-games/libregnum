/* lrg-nav-bake.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavBake: the `.rnav` navigation bake settings of a level (the
 * agent the mesh is baked for, the voxel resolution, which geometry of
 * the world counts, area types with traversal costs and off-mesh links)
 * and the bake itself: it gathers the static geometry of an #LrgWorld3D
 * (mesh renderers and static physics colliders, filtered by layers and
 * tags) or explicit triangles and bakes an #LrgNavPolyMesh, through an
 * #LrgNavPolyMeshCache when one is given.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-nav-poly-mesh.h"
#include "lrg-nav-poly-mesh-cache.h"
#include "../world3d/lrg-world3d.h"

G_BEGIN_DECLS

/**
 * LRG_NAV_BAKE_FORMAT_VERSION:
 *
 * The `version:` a `.rnav` file is written with; newer files fail to
 * load with %LRG_NAV_BAKE_ERROR_PARSE.
 *
 * Since: 0.9.0
 */
#define LRG_NAV_BAKE_FORMAT_VERSION (1)

/**
 * LRG_NAV_BAKE_ERROR:
 *
 * Error domain of #LrgNavBake.
 *
 * Since: 0.9.0
 */
#define LRG_NAV_BAKE_ERROR (lrg_nav_bake_error_quark ())

/**
 * LrgNavBakeError:
 * @LRG_NAV_BAKE_ERROR_PARSE: the YAML is malformed or a key holds a
 *   value of the wrong kind (the message names the key path, such as
 *   `areas[1].boxes[0].min`)
 * @LRG_NAV_BAKE_ERROR_IO: the file could not be read or written
 * @LRG_NAV_BAKE_ERROR_INVALID: a value is out of range, or an area name
 *   or tag is used twice, or a link names an unknown area
 * @LRG_NAV_BAKE_ERROR_NO_GEOMETRY: the bake found no triangles
 *
 * Errors of #LrgNavBake.  Bake failures of the mesh itself are passed on
 * with lrg_nav_poly_mesh_bake()'s %G_IO_ERROR codes.
 *
 * Since: 0.9.0
 */
typedef enum
{
	LRG_NAV_BAKE_ERROR_PARSE,
	LRG_NAV_BAKE_ERROR_IO,
	LRG_NAV_BAKE_ERROR_INVALID,
	LRG_NAV_BAKE_ERROR_NO_GEOMETRY
} LrgNavBakeError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_nav_bake_error_quark (void);

LRG_AVAILABLE_IN_ALL
GType lrg_nav_bake_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_NAV_BAKE_ERROR (lrg_nav_bake_error_get_type ())

#define LRG_TYPE_NAV_BAKE (lrg_nav_bake_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNavBake, lrg_nav_bake, LRG, NAV_BAKE, GObject)

/* ==========================================================================
 * Construction and the .rnav format
 * ========================================================================== */

/**
 * lrg_nav_bake_new:
 *
 * Creates bake settings with the defaults of #LrgNavPolyMesh (cell size
 * 0.25, cell height 0.1, agent radius 0.3, height 1.8, max slope 50
 * degrees, max step 0.45, no drops, no corner margin), no region
 * filtering, both geometry sources on, every render layer, no tag
 * filters, no bounds, no areas and no links.
 *
 * Returns: (transfer full): new bake settings
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavBake * lrg_nav_bake_new (void);

/**
 * lrg_nav_bake_new_from_data:
 * @data: `.rnav` YAML text
 * @length: length of @data, or -1 when nul-terminated
 * @error: return location for a #GError
 *
 * Parses `.rnav` text.  Unknown keys are kept (at the top level, in the
 * `agent`, `voxel`, `regions` and `sources` sections and in each area
 * and link) and written back by lrg_nav_bake_to_data().  Errors name the
 * key path.
 *
 * Returns: (transfer full) (nullable): the settings, %NULL on error
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavBake * lrg_nav_bake_new_from_data (const gchar  *data,
                                         gssize        length,
                                         GError      **error);

/**
 * lrg_nav_bake_new_from_file:
 * @path: (type filename): a `.rnav` file
 * @error: return location for a #GError
 *
 * Loads a `.rnav` file (see lrg_nav_bake_new_from_data()); errors are
 * prefixed with @path.
 *
 * Returns: (transfer full) (nullable): the settings, %NULL on error
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavBake * lrg_nav_bake_new_from_file (const gchar  *path,
                                         GError      **error);

/**
 * lrg_nav_bake_to_data:
 * @self: an #LrgNavBake
 *
 * Writes the settings as `.rnav` YAML.  Numbers use the shortest text
 * that reads back to the same float, so a load and save of saved text is
 * byte-identical.
 *
 * Returns: (transfer full): the YAML text
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_nav_bake_to_data (LrgNavBake *self);

/**
 * lrg_nav_bake_save_to_file:
 * @self: an #LrgNavBake
 * @path: (type filename): destination
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_bake_save_to_file (LrgNavBake   *self,
                                    const gchar  *path,
                                    GError      **error);

/**
 * lrg_nav_bake_copy:
 * @self: an #LrgNavBake
 *
 * Returns: (transfer full): a deep copy (areas, links, filters, unknown
 *   keys; staged explicit triangles are not copied)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavBake * lrg_nav_bake_copy (LrgNavBake *self);

/* ==========================================================================
 * Agent and voxel settings (also #GObject properties)
 * ========================================================================== */

/**
 * lrg_nav_bake_get_name:
 * @self: an #LrgNavBake
 *
 * Returns: (transfer none) (nullable): #LrgNavBake:name
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_nav_bake_get_name (LrgNavBake *self);

/**
 * lrg_nav_bake_set_name:
 * @self: an #LrgNavBake
 * @name: (nullable): a display name
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_bake_set_name (LrgNavBake  *self,
                            const gchar *name);

/**
 * lrg_nav_bake_get_agent:
 * @self: an #LrgNavBake
 * @radius: (out) (optional): #LrgNavBake:agent-radius
 * @height: (out) (optional): #LrgNavBake:agent-height
 * @max_slope: (out) (optional): #LrgNavBake:max-slope in degrees
 * @max_step: (out) (optional): #LrgNavBake:max-step
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_bake_get_agent (LrgNavBake *self,
                             gfloat     *radius,
                             gfloat     *height,
                             gfloat     *max_slope,
                             gfloat     *max_step);

/**
 * lrg_nav_bake_set_agent:
 * @self: an #LrgNavBake
 * @radius: the agent's radius (>= 0): the surface is eroded by it
 * @height: the clearance the agent needs (> 0)
 * @max_slope: steepest walkable slope in degrees (0 .. 89)
 * @max_step: highest step the agent climbs (>= 0)
 *
 * Sets the four agent properties at once (invalid values are clamped).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_bake_set_agent (LrgNavBake *self,
                             gfloat      radius,
                             gfloat      height,
                             gfloat      max_slope,
                             gfloat      max_step);

/**
 * lrg_nav_bake_get_cell_size:
 * @self: an #LrgNavBake
 *
 * Returns: #LrgNavBake:cell-size
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_bake_get_cell_size (LrgNavBake *self);

/**
 * lrg_nav_bake_set_cell_size:
 * @self: an #LrgNavBake
 * @cell_size: horizontal voxel size in metres (> 0)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_bake_set_cell_size (LrgNavBake *self,
                                 gfloat      cell_size);

/**
 * lrg_nav_bake_get_cell_height:
 * @self: an #LrgNavBake
 *
 * Returns: #LrgNavBake:cell-height
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_bake_get_cell_height (LrgNavBake *self);

/**
 * lrg_nav_bake_set_cell_height:
 * @self: an #LrgNavBake
 * @cell_height: vertical voxel size in metres (> 0)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_bake_set_cell_height (LrgNavBake *self,
                                   gfloat      cell_height);

/* ==========================================================================
 * Geometry sources
 * ========================================================================== */

/**
 * lrg_nav_bake_set_bounds:
 * @self: an #LrgNavBake
 * @min: (array fixed-size=3) (nullable): lowest corner, %NULL to clear
 * @max: (array fixed-size=3) (nullable): highest corner
 *
 * Restricts the bake to a box: world triangles are clipped to its X/Z
 * extent and dropped outside its Y range.  Without bounds, infinite
 * planes are clipped to the other geometry's extent grown by 1 m (or a
 * 100 m square around the origin when there is nothing else).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_bake_set_bounds (LrgNavBake   *self,
                              const gfloat *min,
                              const gfloat *max);

/**
 * lrg_nav_bake_get_bounds:
 * @self: an #LrgNavBake
 * @min: (out caller-allocates) (array fixed-size=3) (optional): lowest corner
 * @max: (out caller-allocates) (array fixed-size=3) (optional): highest corner
 *
 * Returns: %TRUE when bounds are set
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_bake_get_bounds (LrgNavBake *self,
                                  gfloat     *min,
                                  gfloat     *max);

/**
 * lrg_nav_bake_set_include_tags:
 * @self: an #LrgNavBake
 * @tags: (array zero-terminated=1) (nullable): entity tags; when any are
 *   set only entities with one of them contribute geometry
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_bake_set_include_tags (LrgNavBake         *self,
                                    const gchar *const *tags);

/**
 * lrg_nav_bake_get_include_tags:
 * @self: an #LrgNavBake
 *
 * Returns: (transfer none) (array zero-terminated=1): the include tags
 *   (an empty array when none)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_nav_bake_get_include_tags (LrgNavBake *self);

/**
 * lrg_nav_bake_set_exclude_tags:
 * @self: an #LrgNavBake
 * @tags: (array zero-terminated=1) (nullable): entity tags whose
 *   entities never contribute geometry (tag a moving platform `no-nav`)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_bake_set_exclude_tags (LrgNavBake         *self,
                                    const gchar *const *tags);

/**
 * lrg_nav_bake_get_exclude_tags:
 * @self: an #LrgNavBake
 *
 * Returns: (transfer none) (array zero-terminated=1): the exclude tags
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_nav_bake_get_exclude_tags (LrgNavBake *self);

/**
 * lrg_nav_bake_set_physics_layers:
 * @self: an #LrgNavBake
 * @layers: (array zero-terminated=1) (nullable): physics layer names;
 *   when any are set only colliders on one of them contribute (a
 *   collider without a layer is on `default`)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_bake_set_physics_layers (LrgNavBake         *self,
                                      const gchar *const *layers);

/**
 * lrg_nav_bake_get_physics_layers:
 * @self: an #LrgNavBake
 *
 * Returns: (transfer none) (array zero-terminated=1): the physics layers
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_nav_bake_get_physics_layers (LrgNavBake *self);

/**
 * lrg_nav_bake_add_triangles:
 * @self: an #LrgNavBake
 * @vertices: (array length=n_floats): nine floats per triangle, world space
 * @n_floats: number of floats (a multiple of 9)
 * @walkable: %FALSE for triangles that only obstruct
 *
 * Stages explicit triangles that every later bake adds to the world's
 * geometry (procedural levels, tests).  They are not saved to `.rnav`.
 *
 * Returns: the number of triangles staged
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_nav_bake_add_triangles (LrgNavBake   *self,
                                  const gfloat *vertices,
                                  guint         n_floats,
                                  gboolean      walkable);

/**
 * lrg_nav_bake_clear_triangles:
 * @self: an #LrgNavBake
 *
 * Drops the triangles staged with lrg_nav_bake_add_triangles().
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_bake_clear_triangles (LrgNavBake *self);

/* ==========================================================================
 * Areas
 * ========================================================================== */

/**
 * lrg_nav_bake_add_area:
 * @self: an #LrgNavBake
 * @name: a unique area name (`mud`, `door`)
 * @tag: a unique nonzero tag the polygons of this area carry
 * @cost: the traversal cost multiplier (> 0, 1 is normal)
 *
 * Declares an area type.  Give it boxes with lrg_nav_bake_add_area_box();
 * links can name it to share its tag (a door's link and floor switch
 * together with lrg_nav_poly_mesh_set_tag_enabled()).
 *
 * Returns: the area's index, or -1 when @name or @tag is taken or a
 *   value is invalid
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_nav_bake_add_area (LrgNavBake  *self,
                            const gchar *name,
                            guint32      tag,
                            gfloat       cost);

/**
 * lrg_nav_bake_get_n_areas:
 * @self: an #LrgNavBake
 *
 * Returns: the number of areas
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_nav_bake_get_n_areas (LrgNavBake *self);

/**
 * lrg_nav_bake_get_area:
 * @self: an #LrgNavBake
 * @index: an area index
 * @name: (out) (optional) (transfer none): its name
 * @tag: (out) (optional): its tag
 * @cost: (out) (optional): its cost
 *
 * Returns: %FALSE when @index is out of range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_bake_get_area (LrgNavBake   *self,
                                guint         index,
                                const gchar **name,
                                guint32      *tag,
                                gfloat       *cost);

/**
 * lrg_nav_bake_find_area:
 * @self: an #LrgNavBake
 * @name: an area name
 *
 * Returns: its index, or -1
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_nav_bake_find_area (LrgNavBake  *self,
                             const gchar *name);

/**
 * lrg_nav_bake_set_area_cost:
 * @self: an #LrgNavBake
 * @index: an area index
 * @cost: the new cost (> 0)
 *
 * Returns: %FALSE when @index is out of range or @cost invalid
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_bake_set_area_cost (LrgNavBake *self,
                                     guint       index,
                                     gfloat      cost);

/**
 * lrg_nav_bake_remove_area:
 * @self: an #LrgNavBake
 * @index: an area index
 *
 * Removes the area and its boxes; links that named it lose their area.
 *
 * Returns: %FALSE when @index is out of range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_bake_remove_area (LrgNavBake *self,
                                   guint       index);

/**
 * lrg_nav_bake_add_area_box:
 * @self: an #LrgNavBake
 * @area: an area index
 * @min: (array fixed-size=3): lowest corner
 * @max: (array fixed-size=3): highest corner
 *
 * Marks the walkable surface inside a box as the area (see
 * lrg_nav_poly_mesh_add_area_box(); later boxes win where they overlap,
 * in area order then box order).
 *
 * Returns: the box's index within the area, or -1 for an invalid area or
 *   an empty box
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_nav_bake_add_area_box (LrgNavBake   *self,
                                guint         area,
                                const gfloat *min,
                                const gfloat *max);

/**
 * lrg_nav_bake_get_n_area_boxes:
 * @self: an #LrgNavBake
 * @area: an area index
 *
 * Returns: the number of boxes of @area (0 when out of range)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_nav_bake_get_n_area_boxes (LrgNavBake *self,
                                     guint       area);

/**
 * lrg_nav_bake_get_area_box:
 * @self: an #LrgNavBake
 * @area: an area index
 * @box: a box index
 * @min: (out caller-allocates) (array fixed-size=3) (optional): lowest corner
 * @max: (out caller-allocates) (array fixed-size=3) (optional): highest corner
 *
 * Returns: %FALSE when out of range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_bake_get_area_box (LrgNavBake *self,
                                    guint       area,
                                    guint       box,
                                    gfloat     *min,
                                    gfloat     *max);

/**
 * lrg_nav_bake_remove_area_box:
 * @self: an #LrgNavBake
 * @area: an area index
 * @box: a box index
 *
 * Returns: %FALSE when out of range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_bake_remove_area_box (LrgNavBake *self,
                                       guint       area,
                                       guint       box);

/* ==========================================================================
 * Off-mesh links
 * ========================================================================== */

/**
 * lrg_nav_bake_add_link:
 * @self: an #LrgNavBake
 * @start: (array fixed-size=3): where it is entered
 * @end: (array fixed-size=3): where it leads
 * @bidirectional: whether it works from @end to @start too
 * @cost: extra cost on top of its length (>= 0)
 * @area: (nullable): the name of an area whose tag the link carries
 *
 * Adds an off-mesh link (a ladder, jump, teleporter) that every bake
 * passes to lrg_nav_poly_mesh_add_link().
 *
 * Returns: the link's index (its id in the baked mesh), or -1 for
 *   invalid values or an unknown area
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_nav_bake_add_link (LrgNavBake   *self,
                            const gfloat *start,
                            const gfloat *end,
                            gboolean      bidirectional,
                            gfloat        cost,
                            const gchar  *area);

/**
 * lrg_nav_bake_get_n_links:
 * @self: an #LrgNavBake
 *
 * Returns: the number of links
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_nav_bake_get_n_links (LrgNavBake *self);

/**
 * lrg_nav_bake_get_link:
 * @self: an #LrgNavBake
 * @index: a link index
 * @start: (out caller-allocates) (array fixed-size=3) (optional): its start
 * @end: (out caller-allocates) (array fixed-size=3) (optional): its end
 * @bidirectional: (out) (optional): whether it works both ways
 * @cost: (out) (optional): its extra cost
 * @area: (out) (optional) (transfer none) (nullable): its area name
 *
 * Returns: %FALSE when @index is out of range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_bake_get_link (LrgNavBake   *self,
                                guint         index,
                                gfloat       *start,
                                gfloat       *end,
                                gboolean     *bidirectional,
                                gfloat       *cost,
                                const gchar **area);

/**
 * lrg_nav_bake_remove_link:
 * @self: an #LrgNavBake
 * @index: a link index (later links move down one)
 *
 * Returns: %FALSE when @index is out of range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_bake_remove_link (LrgNavBake *self,
                                   guint       index);

/* ==========================================================================
 * Baking
 * ========================================================================== */

/**
 * lrg_nav_bake_configure:
 * @self: an #LrgNavBake
 * @mesh: the mesh to set up
 *
 * Applies the settings to @mesh without baking: its bake properties,
 * the area boxes (staged after lrg_nav_poly_mesh_clear_input(), which
 * keeps the links the mesh had), the links (added only when the mesh has
 * none, so configuring twice does not double them) and the area costs
 * (lrg_nav_poly_mesh_set_tag_cost()).  Call it before staging triangles.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_bake_configure (LrgNavBake     *self,
                             LrgNavPolyMesh *mesh);

/**
 * lrg_nav_bake_apply_costs:
 * @self: an #LrgNavBake
 * @mesh: a mesh (baked or loaded)
 *
 * Gives @mesh the area costs (lrg_nav_poly_mesh_set_tag_cost()); used
 * after lrg_nav_poly_mesh_load(), which does not store costs.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_bake_apply_costs (LrgNavBake     *self,
                               LrgNavPolyMesh *mesh);

/**
 * lrg_nav_bake_collect_world:
 * @self: an #LrgNavBake
 * @world: the world whose static geometry to gather
 * @mesh: the mesh to stage the triangles on
 *
 * Stages the world's static geometry on @mesh, in world space:
 *
 * - every enabled #LrgMeshRendererComponent of an active entity (its
 *   loaded model, its glTF mesh asset read headless, or its built-in
 *   primitive), unless the entity has a non-trigger collider and
 *   colliders are a source (the collider is the better walkable shape);
 * - every non-trigger #LrgColliderComponent3D whose entity has no
 *   dynamic or kinematic #LrgRigidBodyComponent, triangulated with
 *   lrg_physics_shape_build_debug_mesh() (planes clipped, see
 *   lrg_nav_bake_set_bounds()).
 *
 * Closed convex shapes (box, sphere, capsule, cylinder and convex
 * colliders; cube, sphere, cylinder and cone primitives) are staged with
 * lrg_nav_poly_mesh_add_solid() so their insides are never walkable;
 * models, mesh colliders and open primitives are staged as surfaces.
 *
 * Entities are filtered by #LrgNavBake:static-only, #LrgNavBake:layer-mask,
 * the include / exclude tags and (colliders) the physics layers; the
 * triangles are clipped to the bounds.
 *
 * Returns: the number of triangles staged
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_nav_bake_collect_world (LrgNavBake     *self,
                                  LrgWorld3D     *world,
                                  LrgNavPolyMesh *mesh);

/**
 * lrg_nav_bake_bake_into:
 * @self: an #LrgNavBake
 * @mesh: the mesh to (re)bake
 * @world: (nullable): the world to gather geometry from
 * @cache: (nullable): a cache of earlier bakes
 * @error: return location for a #GError
 *
 * lrg_nav_bake_configure(), lrg_nav_bake_collect_world() (with @world),
 * the explicit triangles, then lrg_nav_poly_mesh_bake_cached(); last the
 * regions smaller than #LrgNavBake:min-region-area are disabled.  The
 * same settings and geometry always give the same mesh.  A positive
 * tile extent (lrg_nav_bake_set_tile_extent()) splits the X/Z bounds
 * into squares and unions those bakes; extent 0 is this one bake.  On
 * error the mesh keeps its previous bake.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_bake_bake_into (LrgNavBake           *self,
                                 LrgNavPolyMesh       *mesh,
                                 LrgWorld3D           *world,
                                 LrgNavPolyMeshCache  *cache,
                                 GError              **error);

/**
 * lrg_nav_bake_bake:
 * @self: an #LrgNavBake
 * @world: (nullable): the world to gather geometry from
 * @cache: (nullable): a cache of earlier bakes
 * @error: return location for a #GError
 *
 * Bakes a new mesh, see lrg_nav_bake_bake_into().
 *
 * Returns: (transfer full) (nullable): the mesh, %NULL on error
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavPolyMesh * lrg_nav_bake_bake (LrgNavBake           *self,
                                    LrgWorld3D           *world,
                                    LrgNavPolyMeshCache  *cache,
                                    GError              **error);

/**
 * lrg_nav_bake_get_last_triangle_count:
 * @self: an #LrgNavBake
 *
 * Returns: how many triangles the last bake staged (world and explicit)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_nav_bake_get_last_triangle_count (LrgNavBake *self);

/**
 * lrg_nav_bake_set_tile_extent:
 * @self: an #LrgNavBake
 * @extent: tile size in metres on X and Z, or 0 for one bake
 *
 * When @extent is greater than zero and bounds are set,
 * lrg_nav_bake_bake_into() splits those bounds into squares of this
 * size (Y stays the full range), bakes each square and unions the
 * walkable polygons. Empty squares are skipped. 0 keeps the single
 * bake. The value is not written to `.rnav`.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_bake_set_tile_extent (LrgNavBake *self,
                                   gfloat      extent);

/**
 * lrg_nav_bake_get_tile_extent:
 * @self: an #LrgNavBake
 *
 * Returns: the tile extent in metres, or 0 for one bake
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_bake_get_tile_extent (LrgNavBake *self);

/**
 * lrg_nav_bake_get_last_tile_count:
 * @self: an #LrgNavBake
 *
 * Returns: how many tiles the last bake covered, including empty ones
 *   (1 when the extent is 0)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_nav_bake_get_last_tile_count (LrgNavBake *self);

G_END_DECLS
