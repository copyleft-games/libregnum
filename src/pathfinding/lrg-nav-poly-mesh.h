/* lrg-nav-poly-mesh.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavPolyMesh - a polygon navigation mesh baked from a triangle soup
 * (the triangles a level's LrgCollisionMesh is built from): the
 * triangles are voxelised into columns of solid spans, the open spans
 * above walkable ones that leave room for the agent become the walkable
 * surface, the surface is eroded by the agent's radius and split into
 * convex (axis-aligned rectangular) polygons joined by portals.  Paths
 * run A* over the polygons and are straightened with the funnel
 * algorithm; off-mesh links (doors, ladders, drops) and tagged areas
 * can be switched on and off without a rebake.
 *
 * Written from the public descriptions of voxel navigation mesh
 * generation (solid heightfield, open spans, walkable filters, distance
 * erosion) and of the "simple stupid funnel" path straightening; no
 * third-party code.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_NAV_POLY_MESH (lrg_nav_poly_mesh_get_type ())

/**
 * LRG_NAV_POLY_MESH_INVALID:
 *
 * Returned for "no polygon" and "no link".
 */
#define LRG_NAV_POLY_MESH_INVALID (G_MAXUINT)

/**
 * LRG_NAV_POLY_MESH_MAX_COLUMNS:
 *
 * Largest number of heightfield columns (width times depth in cells) a
 * bake accepts; larger inputs fail with %G_IO_ERROR_NO_SPACE instead of
 * exhausting memory.  Raise #LrgNavPolyMesh:cell-size for huge levels.
 */
#define LRG_NAV_POLY_MESH_MAX_COLUMNS (64u * 1024u * 1024u)

/**
 * LRG_NAV_POLY_MESH_MAX_BANDS:
 *
 * Most height bands lrg_nav_poly_mesh_find_nearest_banded() takes.
 */
#define LRG_NAV_POLY_MESH_MAX_BANDS (8u)

/**
 * LrgNavPolyFilterFunc:
 * @tag: the nonzero tag of a polygon or off-mesh link a query wants to cross
 * @user_data: data given to the query
 *
 * Decides whether a tagged polygon (an area given to
 * lrg_nav_poly_mesh_add_area_box()) or tagged link may be used now, for
 * example whether a door is open or can be opened by this agent.
 * Untagged polygons and links are always usable.
 *
 * Returns: %TRUE when the polygon or link may be crossed
 */
typedef gboolean (*LrgNavPolyFilterFunc) (guint32  tag,
                                          gpointer user_data);

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNavPolyMesh, lrg_nav_poly_mesh, LRG, NAV_POLY_MESH, GObject)

/**
 * lrg_nav_poly_mesh_new:
 *
 * Creates an empty mesh with the default agent (#LrgNavPolyMesh:cell-size
 * 0.25, cell-height 0.1, agent-radius 0.3, agent-height 1.8, max-climb
 * 0.45, max-slope 50 degrees, no drops, no corner margin).  Set the
 * properties before lrg_nav_poly_mesh_bake().  Every query fails until
 * a bake succeeds.  Single-threaded.
 *
 * Returns: (transfer full): a new #LrgNavPolyMesh
 */
LRG_AVAILABLE_IN_ALL
LrgNavPolyMesh *
lrg_nav_poly_mesh_new (void);

/**
 * lrg_nav_poly_mesh_clear_input:
 * @self: an #LrgNavPolyMesh
 *
 * Drops the staged triangles, area boxes and links.  The baked mesh is
 * kept (links already connected to it stay usable).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_nav_poly_mesh_clear_input (LrgNavPolyMesh *self);

/**
 * lrg_nav_poly_mesh_add_triangles:
 * @self: an #LrgNavPolyMesh
 * @vertices: (array length=n_floats): nine floats per triangle (three
 *   corners, x y z, Y up), the layout of lrg_collision_mesh_set_triangles()
 * @n_floats: number of floats, a multiple of nine
 * @walkable: %TRUE when the triangles may be walked on (those no steeper
 *   than #LrgNavPolyMesh:max-slope are); %FALSE for triangles that only
 *   obstruct (water surfaces, furniture tops the agent must not use)
 *
 * Stages triangles for the next bake (copied).  Winding is ignored.
 * Triangles with a coordinate that is not finite are skipped.
 *
 * Returns: the number of triangles staged
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_poly_mesh_add_triangles (LrgNavPolyMesh *self,
                                 const gfloat   *vertices,
                                 guint           n_floats,
                                 gboolean        walkable);

/**
 * lrg_nav_poly_mesh_add_area_box:
 * @self: an #LrgNavPolyMesh
 * @min: (array fixed-size=3): minimum corner
 * @max: (array fixed-size=3): maximum corner
 * @tag: a nonzero tag (a door's id)
 *
 * Stages a tagged area: the walkable surface whose cell centre lies
 * inside the box (floor height within its Y range) becomes polygons of
 * their own carrying @tag, which queries only cross when their
 * #LrgNavPolyFilterFunc allows it and lrg_nav_poly_mesh_set_tag_enabled()
 * did not switch the tag off (a dynamic obstacle such as a door).  Where
 * boxes overlap the one staged last wins.
 *
 * Returns: %FALSE when @tag is 0 or the box is empty or not finite
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_add_area_box (LrgNavPolyMesh *self,
                                const gfloat   *min,
                                const gfloat   *max,
                                guint32         tag);

/**
 * lrg_nav_poly_mesh_add_link:
 * @self: an #LrgNavPolyMesh
 * @start: (array fixed-size=3): where the link is entered
 * @end: (array fixed-size=3): where it leads
 * @bidirectional: whether it can be used from @end to @start too
 * @cost: extra cost on top of its length (a ladder's climb), at least 0
 * @tag: 0, or a tag queries ask their filter about
 *
 * Adds an off-mesh link: a ladder, a jump, a teleporter, a drop.  Its
 * ends connect to the nearest polygons within the agent's radius plus
 * two cells (and #LrgNavPolyMesh:max-climb vertically, twice for the
 * end); a link added after a bake connects at once, earlier links at the
 * bake.  lrg_nav_poly_mesh_clear_input() keeps links.
 *
 * Returns: the link's id (0, 1, ...), or %LRG_NAV_POLY_MESH_INVALID for
 *   coordinates that are not finite or a negative cost
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_poly_mesh_add_link (LrgNavPolyMesh *self,
                            const gfloat   *start,
                            const gfloat   *end,
                            gboolean        bidirectional,
                            gfloat          cost,
                            guint32         tag);

/**
 * lrg_nav_poly_mesh_bake:
 * @self: an #LrgNavPolyMesh
 * @error: (nullable): return location for a #GError
 *
 * Builds the mesh from the staged input: rasterises the triangles into
 * a heightfield of #LrgNavPolyMesh:cell-size columns and
 * #LrgNavPolyMesh:cell-height layers, keeps the walkable spans with
 * #LrgNavPolyMesh:agent-height of clearance, connects neighbours within
 * #LrgNavPolyMesh:max-climb, erodes by #LrgNavPolyMesh:agent-radius,
 * merges the rest greedily into rectangles of one tag, joins them with
 * portals, adds drop links where #LrgNavPolyMesh:max-drop allows and
 * connects the off-mesh links.  Staged input is kept (a bake can be
 * repeated with other properties).
 *
 * Errors (%G_IO_ERROR): %G_IO_ERROR_INVALID_ARGUMENT without triangles,
 * %G_IO_ERROR_NO_SPACE above %LRG_NAV_POLY_MESH_MAX_COLUMNS,
 * %G_IO_ERROR_NOT_FOUND when nothing is walkable.  On error the
 * previous mesh stays.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_bake (LrgNavPolyMesh  *self,
                        GError         **error);

/**
 * lrg_nav_poly_mesh_get_input_digest:
 * @self: an #LrgNavPolyMesh
 *
 * A digest (SHA-256, lowercase hex) of everything a bake depends on: the
 * bake properties, the staged triangles and their walkable flags, the
 * area boxes and the off-mesh links with their enabled states.  Equal
 * digests bake equal meshes; #LrgNavPolyMeshCache keys bakes by it.
 *
 * Returns: (transfer full): the digest
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_nav_poly_mesh_get_input_digest (LrgNavPolyMesh *self);

/**
 * lrg_nav_poly_mesh_bake_cached:
 * @self: an #LrgNavPolyMesh
 * @cache: (nullable): an #LrgNavPolyMeshCache, or %NULL to bake
 * @error: (nullable): return location for a #GError
 *
 * Like lrg_nav_poly_mesh_bake(), but when @cache holds a bake of the
 * same input (lrg_nav_poly_mesh_get_input_digest()) that bake is loaded
 * (lrg_nav_poly_mesh_load()) instead; otherwise the mesh is baked and a
 * successful bake is stored in @cache.  The result is the same mesh
 * either way (polygons, portals, links and their connections, regions);
 * lrg_nav_poly_mesh_get_bake_seconds() is the time this call took.
 * Failed bakes are not cached.  The errors are lrg_nav_poly_mesh_bake()'s.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_bake_cached (LrgNavPolyMesh       *self,
                               LrgNavPolyMeshCache  *cache,
                               GError              **error);

/**
 * lrg_nav_poly_mesh_get_polygon_count:
 * @self: an #LrgNavPolyMesh
 *
 * Returns: the number of polygons of the last successful bake
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_poly_mesh_get_polygon_count (LrgNavPolyMesh *self);

/**
 * lrg_nav_poly_mesh_get_portal_count:
 * @self: an #LrgNavPolyMesh
 *
 * Returns: the number of portals (shared polygon edges, each counted
 *   once per direction)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_poly_mesh_get_portal_count (LrgNavPolyMesh *self);

/**
 * lrg_nav_poly_mesh_get_span_count:
 * @self: an #LrgNavPolyMesh
 *
 * Returns: the number of walkable cells (heightfield spans inside a
 *   polygon)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_poly_mesh_get_span_count (LrgNavPolyMesh *self);

/**
 * lrg_nav_poly_mesh_get_link_count:
 * @self: an #LrgNavPolyMesh
 *
 * Returns: the number of off-mesh links (added and baked drops)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_poly_mesh_get_link_count (LrgNavPolyMesh *self);

/**
 * lrg_nav_poly_mesh_get_drop_link_count:
 * @self: an #LrgNavPolyMesh
 *
 * Returns: how many of the links the last bake generated as drops
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_poly_mesh_get_drop_link_count (LrgNavPolyMesh *self);

/**
 * lrg_nav_poly_mesh_get_region_count:
 * @self: an #LrgNavPolyMesh
 *
 * Returns: the number of connected regions (polygons joined by portals
 *   or links of either direction, whatever their state)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_poly_mesh_get_region_count (LrgNavPolyMesh *self);

/**
 * lrg_nav_poly_mesh_get_bake_seconds:
 * @self: an #LrgNavPolyMesh
 *
 * Returns: how long the last successful bake took (monotonic clock), 0
 *   for a mesh loaded with lrg_nav_poly_mesh_load()
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_nav_poly_mesh_get_bake_seconds (LrgNavPolyMesh *self);

/**
 * lrg_nav_poly_mesh_get_polygon:
 * @self: an #LrgNavPolyMesh
 * @polygon: a polygon index
 * @min: (out caller-allocates) (array fixed-size=3) (optional): minimum
 *   corner of its bounds
 * @max: (out caller-allocates) (array fixed-size=3) (optional): maximum
 *   corner
 * @tag: (out) (optional): its area tag, 0 for none
 * @region: (out) (optional): its region, 1 or more
 *
 * Returns: %FALSE when @polygon is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_get_polygon (LrgNavPolyMesh *self,
                               guint           polygon,
                               gfloat         *min,
                               gfloat         *max,
                               guint32        *tag,
                               guint          *region);

/**
 * lrg_nav_poly_mesh_find_nearest:
 * @self: an #LrgNavPolyMesh
 * @point: (array fixed-size=3): query point
 * @extents: (array fixed-size=3): half extents of the search box
 * @out: (out caller-allocates) (array fixed-size=3) (optional): the
 *   nearest point on the surface
 * @polygon: (out) (optional): its polygon
 *
 * The nearest point (3D distance) of an enabled polygon whose surface
 * lies inside the box of @extents around @point.  Disabled polygons and
 * polygons whose tag is switched off are skipped; filters are not asked.
 *
 * Returns: %TRUE when one was found
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_find_nearest (LrgNavPolyMesh *self,
                                const gfloat   *point,
                                const gfloat   *extents,
                                gfloat         *out,
                                guint          *polygon);

/**
 * lrg_nav_poly_mesh_find_nearest_banded:
 * @self: an #LrgNavPolyMesh
 * @point: (array fixed-size=3): the query point (its height is not used)
 * @extent: half size of the box in X and Z
 * @band_y: (array length=n_bands): each band's centre height
 * @band_half: (array length=n_bands): each band's half height
 * @n_bands: how many bands (1 .. %LRG_NAV_POLY_MESH_MAX_BANDS)
 * @out: (out caller-allocates) (array fixed-size=3) (optional): the point
 * @polygon: (out) (optional): its polygon
 * @band: (out) (optional): the band it was found in
 *
 * Asks lrg_nav_poly_mesh_find_nearest() for several boxes of the same
 * X/Z extent stacked in height, in order, and answers for the first box
 * that has a floor, as that call would (box i centred at (@point x,
 * @band_y[i], @point z) with extents (@extent, @band_half[i], @extent)).
 * One pass over the columns instead of one per band: a projection that
 * looks for the floor under the feet in bands going down costs one
 * search.
 *
 * Returns: %TRUE when a band has a floor
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_find_nearest_banded (LrgNavPolyMesh *self,
                                       const gfloat   *point,
                                       gfloat          extent,
                                       const gfloat   *band_y,
                                       const gfloat   *band_half,
                                       guint           n_bands,
                                       gfloat         *out,
                                       guint          *polygon,
                                       guint          *band);

/**
 * lrg_nav_poly_mesh_find_nearest_in_region:
 * @self: an #LrgNavPolyMesh
 * @point: (array fixed-size=3): query point
 * @extents: (array fixed-size=3): half extents of the search box
 * @region: the region the point must be in, 0 for any
 * @out: (out caller-allocates) (array fixed-size=3) (optional): the
 *   nearest point on the surface
 * @polygon: (out) (optional): its polygon
 *
 * As lrg_nav_poly_mesh_find_nearest(), restricted to polygons of
 * @region (lrg_nav_poly_mesh_get_polygon_region()): where an agent in
 * that region stands to reach something off the mesh.
 *
 * Returns: %TRUE when one was found
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_find_nearest_in_region (LrgNavPolyMesh *self,
                                          const gfloat   *point,
                                          const gfloat   *extents,
                                          guint           region,
                                          gfloat         *out,
                                          guint          *polygon);

/**
 * lrg_nav_poly_mesh_get_polygon_region:
 * @self: an #LrgNavPolyMesh
 * @polygon: a polygon index
 *
 * Returns: the polygon's region (1 or more), 0 when out of range
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_nav_poly_mesh_get_polygon_region (LrgNavPolyMesh *self,
                                      guint           polygon);

/**
 * lrg_nav_poly_mesh_find_path:
 * @self: an #LrgNavPolyMesh
 * @start: (array fixed-size=3): start position (feet)
 * @goal: (array fixed-size=3): goal position
 * @extents: (array fixed-size=3): half extents for snapping both ends
 *   to the surface (lrg_nav_poly_mesh_find_nearest())
 * @max_nodes: polygons expanded at most, 0 for no limit
 * @filter: (scope call) (closure user_data) (nullable): decides tagged
 *   polygons and links; %NULL allows all of them
 * @user_data: data for @filter
 * @out_expanded: (out) (optional): polygons the search expanded
 * @n_floats: (out): number of floats returned, three per point
 * @error: (nullable): return location for a #GError
 *
 * A* over the enabled polygons (costs between portal midpoints), then
 * straightened with the funnel algorithm through the portals.  The
 * points run from the snapped start to the snapped goal; an off-mesh
 * link on the way contributes its start and end.  %G_IO_ERROR_NOT_FOUND
 * when an end cannot be snapped, the goal is unreachable or the search
 * ran out of @max_nodes.
 *
 * Returns: (transfer full) (array length=n_floats) (nullable): the
 *   points, %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gfloat *
lrg_nav_poly_mesh_find_path (LrgNavPolyMesh        *self,
                             const gfloat          *start,
                             const gfloat          *goal,
                             const gfloat          *extents,
                             guint                  max_nodes,
                             LrgNavPolyFilterFunc   filter,
                             gpointer               user_data,
                             guint                 *out_expanded,
                             guint                 *n_floats,
                             GError               **error);

/**
 * lrg_nav_poly_mesh_raycast:
 * @self: an #LrgNavPolyMesh
 * @start: (array fixed-size=3): start position, snapped to the surface
 * @end: (array fixed-size=3): end position (only X and Z are used)
 * @extents: (array fixed-size=3): half extents for snapping @start
 * @filter: (scope call) (closure user_data) (nullable): decides tagged
 *   polygons, %NULL allows them
 * @user_data: data for @filter
 * @hit_fraction: (out) (optional): where along the segment the walk
 *   stopped, 0 .. 1 (1 when nothing was hit)
 * @hit_point: (out caller-allocates) (array fixed-size=3) (optional):
 *   the point reached, on the surface
 *
 * Walks the straight line over the surface from @start towards @end
 * (in X and Z, following the floor's height) through portals; stops at
 * the first edge without a usable neighbour (a wall, a ledge, a closed
 * door).  Links are not followed.
 *
 * Returns: %TRUE when something blocked the line (or @start could not
 *   be snapped), %FALSE when the whole segment is walkable
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_raycast (LrgNavPolyMesh       *self,
                           const gfloat         *start,
                           const gfloat         *end,
                           const gfloat         *extents,
                           LrgNavPolyFilterFunc  filter,
                           gpointer              user_data,
                           gfloat               *hit_fraction,
                           gfloat               *hit_point);

/**
 * lrg_nav_poly_mesh_random_point:
 * @self: an #LrgNavPolyMesh
 * @from: (array fixed-size=3) (nullable): a position whose region the
 *   point must be in, %NULL for anywhere
 * @extents: (array fixed-size=3) (nullable): half extents for snapping
 *   @from, %NULL for a 1 m box
 * @seed: random seed; the same seed on the same mesh gives the same point
 * @out: (out caller-allocates) (array fixed-size=3): the point
 *
 * A random point on an enabled polygon (uniform over the surface area)
 * reachable from @from (same region) or anywhere.
 *
 * Returns: %FALSE when there is none
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_random_point (LrgNavPolyMesh *self,
                                const gfloat   *from,
                                const gfloat   *extents,
                                guint32         seed,
                                gfloat         *out);

/**
 * lrg_nav_poly_mesh_set_polygon_enabled:
 * @self: an #LrgNavPolyMesh
 * @polygon: a polygon index
 * @enabled: whether queries may use it
 *
 * Returns: %FALSE when @polygon is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_set_polygon_enabled (LrgNavPolyMesh *self,
                                       guint           polygon,
                                       gboolean        enabled);

/**
 * lrg_nav_poly_mesh_set_link_enabled:
 * @self: an #LrgNavPolyMesh
 * @link: a link id from lrg_nav_poly_mesh_add_link()
 * @enabled: whether paths may use it
 *
 * Returns: %FALSE when @link is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_set_link_enabled (LrgNavPolyMesh *self,
                                    guint           link,
                                    gboolean        enabled);

/**
 * lrg_nav_poly_mesh_get_link_connected:
 * @self: an #LrgNavPolyMesh
 * @link: a link id
 *
 * Returns: whether both ends of @link found a polygon
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_get_link_connected (LrgNavPolyMesh *self,
                                      guint           link);

/**
 * lrg_nav_poly_mesh_set_tag_enabled:
 * @self: an #LrgNavPolyMesh
 * @tag: a nonzero area or link tag
 * @enabled: whether polygons and links carrying @tag are usable
 *
 * Switches a dynamic obstacle: a closed and locked door is a tag
 * switched off for every query, whatever the filter says.  Tags start
 * enabled.  Survives rebakes.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_nav_poly_mesh_set_tag_enabled (LrgNavPolyMesh *self,
                                   guint32         tag,
                                   gboolean        enabled);

/**
 * lrg_nav_poly_mesh_get_tag_enabled:
 * @self: an #LrgNavPolyMesh
 * @tag: a tag
 *
 * Returns: whether @tag is enabled (0 always is)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_get_tag_enabled (LrgNavPolyMesh *self,
                                   guint32         tag);

/**
 * lrg_nav_poly_mesh_block_portal:
 * @self: an #LrgNavPolyMesh
 * @from: (array fixed-size=3): a position on the polygon whose portal to block
 * @toward: (array fixed-size=3): the point the blocked way leads to
 *
 * Path repair: disables the portal of the polygon under @from (both
 * directions) whose midpoint lies most nearly towards @toward, for
 * agents whose body could not pass where the mesh offered a way.
 *
 * Returns: %TRUE when a portal was disabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_block_portal (LrgNavPolyMesh *self,
                                const gfloat   *from,
                                const gfloat   *toward);

/**
 * lrg_nav_poly_mesh_to_obj:
 * @self: an #LrgNavPolyMesh
 *
 * The baked polygons as a Wavefront OBJ document (one quad per polygon,
 * corners at the floor heights of its corner cells, one group per
 * region) with the links as line elements, for inspection in a
 * modelling tool.
 *
 * Returns: (transfer full): the OBJ text
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_nav_poly_mesh_to_obj (LrgNavPolyMesh *self);

/**
 * lrg_nav_poly_mesh_save:
 * @self: an #LrgNavPolyMesh
 *
 * The baked mesh (properties, heightfield cells, polygons, portals,
 * links, their enabled states) in a versioned little-endian binary
 * form for caching a bake on disk.  Staged input and tag states are
 * not included.
 *
 * Returns: (transfer full): the bytes
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_nav_poly_mesh_save (LrgNavPolyMesh *self);

/**
 * lrg_nav_poly_mesh_load:
 * @self: an #LrgNavPolyMesh
 * @bytes: data from lrg_nav_poly_mesh_save()
 * @error: (nullable): return location for a #GError
 *
 * Replaces the baked mesh, its links and the bake properties with @bytes.
 * %G_IO_ERROR_INVALID_DATA for anything that is not a complete,
 * consistent mesh of this format version; the previous mesh stays.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_nav_poly_mesh_load (LrgNavPolyMesh  *self,
                        GBytes          *bytes,
                        GError         **error);

G_END_DECLS
