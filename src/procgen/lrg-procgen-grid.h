/* lrg-procgen-grid.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The result of a procedural generator: a grid of cell tags with room,
 * region, start/exit, height and spawn metadata, convertible to a tilemap
 * layer and a navigation grid.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../core/lrg-random-stream.h"
#include "../core/lrg-registry.h"
#include "../ecs/lrg-world.h"
#include "../pathfinding/lrg-nav-grid.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgProcgenRoom (boxed)
 * ========================================================================== */

#define LRG_TYPE_PROCGEN_ROOM (lrg_procgen_room_get_type ())

/**
 * LrgProcgenRoom:
 * @x: left column of the room's floor
 * @y: top row of the room's floor
 * @width: floor width in cells
 * @height: floor height in cells
 *
 * A rectangular room of a generated grid (the floor rectangle, walls
 * excluded).
 */
struct _LrgProcgenRoom
{
    gint x;
    gint y;
    gint width;
    gint height;
};

LRG_AVAILABLE_IN_ALL
GType lrg_procgen_room_get_type (void) G_GNUC_CONST;

/**
 * lrg_procgen_room_new:
 * @x: left column
 * @y: top row
 * @width: width in cells
 * @height: height in cells
 *
 * Creates a room record.
 *
 * Returns: (transfer full): a new #LrgProcgenRoom
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenRoom * lrg_procgen_room_new (gint x,
                                       gint y,
                                       gint width,
                                       gint height);

/**
 * lrg_procgen_room_copy:
 * @self: a room
 *
 * Copies a room record.
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenRoom * lrg_procgen_room_copy (const LrgProcgenRoom *self);

/**
 * lrg_procgen_room_free:
 * @self: (nullable): a room
 *
 * Frees a room record.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_room_free (LrgProcgenRoom *self);

/**
 * lrg_procgen_room_contains:
 * @self: a room
 * @x: column
 * @y: row
 *
 * Returns: whether the cell (@x, @y) lies on the room's floor
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_room_contains (const LrgProcgenRoom *self,
                                    gint                  x,
                                    gint                  y);

/**
 * lrg_procgen_room_get_center:
 * @self: a room
 * @out_x: (out): centre column (rounded down)
 * @out_y: (out): centre row (rounded down)
 *
 * Gets the room's centre cell.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_room_get_center (const LrgProcgenRoom *self,
                                  gint                 *out_x,
                                  gint                 *out_y);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgProcgenRoom, lrg_procgen_room_free)

/* ==========================================================================
 * LrgProcgenSpawn (boxed)
 * ========================================================================== */

#define LRG_TYPE_PROCGEN_SPAWN (lrg_procgen_spawn_get_type ())

/**
 * LrgProcgenSpawn:
 * @kind: what to spawn (an entity kind, a registry name, a loot table id)
 * @x: column of the spawn cell
 * @y: row of the spawn cell
 * @room: index of the room holding the cell, or -1
 *
 * A spawn point placed on a generated grid.
 */
struct _LrgProcgenSpawn
{
    gchar *kind;
    gint   x;
    gint   y;
    gint   room;
};

LRG_AVAILABLE_IN_ALL
GType lrg_procgen_spawn_get_type (void) G_GNUC_CONST;

/**
 * lrg_procgen_spawn_new:
 * @kind: what to spawn
 * @x: column
 * @y: row
 * @room: room index, or -1
 *
 * Creates a spawn point record.
 *
 * Returns: (transfer full): a new #LrgProcgenSpawn
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenSpawn * lrg_procgen_spawn_new (const gchar *kind,
                                         gint         x,
                                         gint         y,
                                         gint         room);

/**
 * lrg_procgen_spawn_copy:
 * @self: a spawn point
 *
 * Copies a spawn point record.
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenSpawn * lrg_procgen_spawn_copy (const LrgProcgenSpawn *self);

/**
 * lrg_procgen_spawn_free:
 * @self: (nullable): a spawn point
 *
 * Frees a spawn point record.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_spawn_free (LrgProcgenSpawn *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgProcgenSpawn, lrg_procgen_spawn_free)

/* ==========================================================================
 * LrgProcgenGrid
 * ========================================================================== */

#define LRG_TYPE_PROCGEN_GRID (lrg_procgen_grid_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProcgenGrid, lrg_procgen_grid, LRG, PROCGEN_GRID, GObject)

/**
 * LrgProcgenSpawnFunc:
 * @grid: the grid being populated
 * @spawn: the spawn point
 * @world_x: world X of the spawn cell's centre
 * @world_y: world Y of the spawn cell's centre
 * @user_data: data passed to lrg_procgen_grid_spawn_entities()
 *
 * Creates the entity for one spawn point.
 *
 * Returns: (transfer full) (nullable): the new game object, or %NULL to
 *   spawn nothing for this point
 */
typedef LrgGameObject * (*LrgProcgenSpawnFunc) (LrgProcgenGrid        *grid,
                                                const LrgProcgenSpawn *spawn,
                                                gfloat                 world_x,
                                                gfloat                 world_y,
                                                gpointer               user_data);

/* --- construction --- */

/**
 * lrg_procgen_grid_new:
 * @width: columns (at least 1)
 * @height: rows (at least 1)
 *
 * Creates a grid filled with %LRG_PROCGEN_CELL_WALL. Floor, corridor and
 * door cells are walkable; the other built-in tags are not.
 *
 * Returns: (transfer full): a new #LrgProcgenGrid
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenGrid * lrg_procgen_grid_new (guint width,
                                       guint height);

/**
 * lrg_procgen_grid_new_from_string:
 * @text: rows of the grid, one per line
 * @error: (nullable): return location for an error
 *
 * Parses a grid from text: `#` wall, `.` floor, `,` corridor, `+` door,
 * `~` water, a space void and `a`..`z` the custom tags
 * %LRG_PROCGEN_CELL_CUSTOM + 0..25. Every line must have the same length;
 * a trailing newline is ignored.
 *
 * Returns: (transfer full) (nullable): the grid, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenGrid * lrg_procgen_grid_new_from_string (const gchar  *text,
                                                   GError      **error);

/**
 * lrg_procgen_grid_copy:
 * @self: a grid
 *
 * Deep-copies the grid with all its metadata.
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgProcgenGrid * lrg_procgen_grid_copy (LrgProcgenGrid *self);

/* --- cells --- */

/**
 * lrg_procgen_grid_get_width:
 * @self: a grid
 *
 * Returns: the number of columns
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_get_width (LrgProcgenGrid *self);

/**
 * lrg_procgen_grid_get_height:
 * @self: a grid
 *
 * Returns: the number of rows
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_get_height (LrgProcgenGrid *self);

/**
 * lrg_procgen_grid_in_bounds:
 * @self: a grid
 * @x: column
 * @y: row
 *
 * Returns: whether (@x, @y) is a cell of the grid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_grid_in_bounds (LrgProcgenGrid *self,
                                     gint            x,
                                     gint            y);

/**
 * lrg_procgen_grid_get_cell:
 * @self: a grid
 * @x: column
 * @y: row
 *
 * Returns: the cell's tag, or %LRG_PROCGEN_CELL_WALL outside the grid
 */
LRG_AVAILABLE_IN_ALL
guint8 lrg_procgen_grid_get_cell (LrgProcgenGrid *self,
                                  gint            x,
                                  gint            y);

/**
 * lrg_procgen_grid_set_cell:
 * @self: a grid
 * @x: column
 * @y: row
 * @tag: the new tag (an #LrgProcgenCell or a custom value)
 *
 * Sets a cell's tag. Cells outside the grid are ignored. Region labels
 * become stale until the next lrg_procgen_grid_label_regions().
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_grid_set_cell (LrgProcgenGrid *self,
                                gint            x,
                                gint            y,
                                guint8          tag);

/**
 * lrg_procgen_grid_fill:
 * @self: a grid
 * @tag: the tag
 *
 * Sets every cell to @tag.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_grid_fill (LrgProcgenGrid *self,
                            guint8          tag);

/**
 * lrg_procgen_grid_fill_rect:
 * @self: a grid
 * @x: left column
 * @y: top row
 * @width: width in cells
 * @height: height in cells
 * @tag: the tag
 *
 * Sets every cell of a rectangle (clipped to the grid) to @tag.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_grid_fill_rect (LrgProcgenGrid *self,
                                 gint            x,
                                 gint            y,
                                 gint            width,
                                 gint            height,
                                 guint8          tag);

/**
 * lrg_procgen_grid_count_cells:
 * @self: a grid
 * @tag: the tag
 *
 * Returns: how many cells hold @tag
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_count_cells (LrgProcgenGrid *self,
                                    guint8          tag);

/**
 * lrg_procgen_grid_get_cells:
 * @self: a grid
 * @out_len: (out): number of cells (width * height)
 *
 * Gets the row-major cell tags.
 *
 * Returns: (array length=out_len) (transfer none): the cells
 */
LRG_AVAILABLE_IN_ALL
const guint8 * lrg_procgen_grid_get_cells (LrgProcgenGrid *self,
                                           gsize          *out_len);

/* --- walkability --- */

/**
 * lrg_procgen_grid_set_tag_walkable:
 * @self: a grid
 * @tag: a tag
 * @walkable: whether cells with @tag can be walked on
 *
 * Changes which tags count as walkable for regions, connectivity,
 * distances, spawn placement and navigation grid conversion.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_grid_set_tag_walkable (LrgProcgenGrid *self,
                                        guint8          tag,
                                        gboolean        walkable);

/**
 * lrg_procgen_grid_is_tag_walkable:
 * @self: a grid
 * @tag: a tag
 *
 * Returns: whether cells with @tag are walkable
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_grid_is_tag_walkable (LrgProcgenGrid *self,
                                           guint8          tag);

/**
 * lrg_procgen_grid_is_walkable:
 * @self: a grid
 * @x: column
 * @y: row
 *
 * Returns: whether the cell is inside the grid and walkable
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_grid_is_walkable (LrgProcgenGrid *self,
                                       gint            x,
                                       gint            y);

/**
 * lrg_procgen_grid_count_walkable:
 * @self: a grid
 *
 * Returns: the number of walkable cells
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_count_walkable (LrgProcgenGrid *self);

/* --- regions and connectivity --- */

/**
 * lrg_procgen_grid_label_regions:
 * @self: a grid
 *
 * Flood-fills the walkable cells into 4-connected regions. Regions are
 * numbered from 0 in the row-major order of their first cell.
 *
 * Returns: the number of regions
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_label_regions (LrgProcgenGrid *self);

/**
 * lrg_procgen_grid_get_region_count:
 * @self: a grid
 *
 * Returns: the number of regions of the last labelling
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_get_region_count (LrgProcgenGrid *self);

/**
 * lrg_procgen_grid_get_region:
 * @self: a grid
 * @x: column
 * @y: row
 *
 * Returns: the region of the cell from the last labelling, or -1 for a
 *   non-walkable or out-of-bounds cell
 */
LRG_AVAILABLE_IN_ALL
gint lrg_procgen_grid_get_region (LrgProcgenGrid *self,
                                  gint            x,
                                  gint            y);

/**
 * lrg_procgen_grid_get_region_size:
 * @self: a grid
 * @region: a region index
 *
 * Returns: the number of cells of @region, or 0 for an unknown index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_get_region_size (LrgProcgenGrid *self,
                                        guint           region);

/**
 * lrg_procgen_grid_is_connected:
 * @self: a grid
 *
 * Returns: whether every walkable cell can reach every other one
 *   (also %TRUE for a grid without walkable cells)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_grid_is_connected (LrgProcgenGrid *self);

/**
 * lrg_procgen_grid_cull_regions:
 * @self: a grid
 * @min_size: regions with fewer cells than this are removed
 * @fill_tag: the tag written over removed regions
 *
 * Removes small regions (and relabels).
 *
 * Returns: the number of regions removed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_cull_regions (LrgProcgenGrid *self,
                                     guint           min_size,
                                     guint8          fill_tag);

/**
 * lrg_procgen_grid_keep_largest_region:
 * @self: a grid
 * @fill_tag: the tag written over the other regions
 *
 * Removes every region but the largest (the first one on a tie).
 *
 * Returns: the number of regions removed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_keep_largest_region (LrgProcgenGrid *self,
                                            guint8          fill_tag);

/**
 * lrg_procgen_grid_connect_regions:
 * @self: a grid
 * @corridor_tag: the tag carved for the connecting corridors
 *
 * Joins disjoint regions until the grid is connected: region 0 grows by a
 * breadth-first search over every cell (walkable or not, border excluded)
 * until it meets another region, and the shortest such path is carved.
 * Only non-walkable cells are overwritten. Deterministic.
 *
 * Returns: the number of corridors carved
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_connect_regions (LrgProcgenGrid *self,
                                        guint8          corridor_tag);

/* --- distances --- */

/**
 * lrg_procgen_grid_distance_map:
 * @self: a grid
 * @from_x: start column
 * @from_y: start row
 * @out_len: (out): number of entries (width * height)
 *
 * Breadth-first walking distances (4-connected) from a cell.
 *
 * Returns: (array length=out_len) (transfer full): row-major steps from
 *   the start cell, -1 where unreachable
 */
LRG_AVAILABLE_IN_ALL
gint * lrg_procgen_grid_distance_map (LrgProcgenGrid *self,
                                      gint            from_x,
                                      gint            from_y,
                                      gsize          *out_len);

/**
 * lrg_procgen_grid_path_distance:
 * @self: a grid
 * @x0: start column
 * @y0: start row
 * @x1: goal column
 * @y1: goal row
 *
 * Returns: walking steps between the cells, or -1 when unreachable
 */
LRG_AVAILABLE_IN_ALL
gint lrg_procgen_grid_path_distance (LrgProcgenGrid *self,
                                     gint            x0,
                                     gint            y0,
                                     gint            x1,
                                     gint            y1);

/* --- rooms --- */

/**
 * lrg_procgen_grid_add_room:
 * @self: a grid
 * @room: the room (copied)
 *
 * Records a room. Does not change cells.
 *
 * Returns: the new room's index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_add_room (LrgProcgenGrid       *self,
                                 const LrgProcgenRoom *room);

/**
 * lrg_procgen_grid_get_room_count:
 * @self: a grid
 *
 * Returns: the number of rooms
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_get_room_count (LrgProcgenGrid *self);

/**
 * lrg_procgen_grid_get_room:
 * @self: a grid
 * @index: room index
 *
 * Returns: (transfer none) (nullable): the room, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const LrgProcgenRoom * lrg_procgen_grid_get_room (LrgProcgenGrid *self,
                                                  guint           index);

/**
 * lrg_procgen_grid_room_at:
 * @self: a grid
 * @x: column
 * @y: row
 *
 * Returns: the index of the first room containing the cell, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_procgen_grid_room_at (LrgProcgenGrid *self,
                               gint            x,
                               gint            y);

/* --- start and exit --- */

/**
 * lrg_procgen_grid_set_start:
 * @self: a grid
 * @x: column
 * @y: row
 *
 * Sets the start cell.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_grid_set_start (LrgProcgenGrid *self,
                                 gint            x,
                                 gint            y);

/**
 * lrg_procgen_grid_get_start:
 * @self: a grid
 * @out_x: (out) (optional): start column
 * @out_y: (out) (optional): start row
 *
 * Returns: whether a start cell is set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_grid_get_start (LrgProcgenGrid *self,
                                     gint           *out_x,
                                     gint           *out_y);

/**
 * lrg_procgen_grid_set_exit:
 * @self: a grid
 * @x: column
 * @y: row
 *
 * Sets the exit cell.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_grid_set_exit (LrgProcgenGrid *self,
                                gint            x,
                                gint            y);

/**
 * lrg_procgen_grid_get_exit:
 * @self: a grid
 * @out_x: (out) (optional): exit column
 * @out_y: (out) (optional): exit row
 *
 * Returns: whether an exit cell is set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_grid_get_exit (LrgProcgenGrid *self,
                                    gint           *out_x,
                                    gint           *out_y);

/**
 * lrg_procgen_grid_place_start_exit:
 * @self: a grid
 *
 * Places the start and exit on a farthest pair of the largest region by a
 * double sweep: the start is the cell farthest from the region's first
 * cell, the exit the cell farthest from the start (exact on trees, a close
 * approximation elsewhere). Deterministic.
 *
 * Returns: %FALSE when the grid has no walkable cell
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_grid_place_start_exit (LrgProcgenGrid *self);

/* --- heights --- */

/**
 * lrg_procgen_grid_has_heights:
 * @self: a grid
 *
 * Returns: whether a heightmap has been set on the grid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_procgen_grid_has_heights (LrgProcgenGrid *self);

/**
 * lrg_procgen_grid_get_height_at:
 * @self: a grid
 * @x: column
 * @y: row
 *
 * Returns: the height of the cell in [0, 1], or 0 without a heightmap
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_procgen_grid_get_height_at (LrgProcgenGrid *self,
                                       gint            x,
                                       gint            y);

/**
 * lrg_procgen_grid_set_height_at:
 * @self: a grid
 * @x: column
 * @y: row
 * @height: the height
 *
 * Sets a cell's height, creating the (zeroed) heightmap on first use.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_grid_set_height_at (LrgProcgenGrid *self,
                                     gint            x,
                                     gint            y,
                                     gfloat          height);

/* --- spawn points --- */

/**
 * lrg_procgen_grid_add_spawn:
 * @self: a grid
 * @spawn: the spawn point (copied)
 *
 * Records a spawn point.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_grid_add_spawn (LrgProcgenGrid        *self,
                                 const LrgProcgenSpawn *spawn);

/**
 * lrg_procgen_grid_get_spawn_count:
 * @self: a grid
 *
 * Returns: the number of spawn points
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_get_spawn_count (LrgProcgenGrid *self);

/**
 * lrg_procgen_grid_get_spawn:
 * @self: a grid
 * @index: spawn index
 *
 * Returns: (transfer none) (nullable): the spawn point, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const LrgProcgenSpawn * lrg_procgen_grid_get_spawn (LrgProcgenGrid *self,
                                                    guint           index);

/**
 * lrg_procgen_grid_clear_spawns:
 * @self: a grid
 *
 * Removes every spawn point.
 */
LRG_AVAILABLE_IN_ALL
void lrg_procgen_grid_clear_spawns (LrgProcgenGrid *self);

/**
 * lrg_procgen_grid_place_spawns:
 * @self: a grid
 * @rng: the random stream to draw from
 * @kind: what to spawn
 * @count: how many points to place
 * @min_distance: minimum walking distance from the start cell (ignored
 *   without a start cell)
 * @rooms_only: only place inside recorded rooms
 *
 * Places up to @count spawn points on distinct free walkable cells (not
 * the start, the exit or an earlier spawn point), chosen uniformly from
 * the candidates with @rng.
 *
 * Returns: the number of points placed (fewer than @count when the
 *   candidates run out)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_place_spawns (LrgProcgenGrid  *self,
                                     LrgRandomStream *rng,
                                     const gchar     *kind,
                                     guint            count,
                                     guint            min_distance,
                                     gboolean         rooms_only);

/**
 * lrg_procgen_grid_spawn_entities:
 * @self: a grid
 * @world: (nullable): the world that receives the entities
 * @registry: (nullable): registry used to create entities by kind
 * @cell_size: world units per cell
 * @func: (nullable) (scope call) (closure user_data): creates the entity of a spawn point
 * @user_data: data for @func
 *
 * Turns every spawn point into an #LrgGameObject at the centre of its cell.
 * With @func, @func makes the entity. Otherwise, when @registry has the
 * spawn's kind registered as an #LrgGameObject type, the registry creates
 * it; otherwise a plain #LrgGameObject is made. An entity without a tag
 * is tagged with the kind. Entities are added to @world (which takes its
 * own reference); without a world they are released after creation.
 *
 * Returns: the number of entities created
 */
LRG_AVAILABLE_IN_ALL
guint lrg_procgen_grid_spawn_entities (LrgProcgenGrid      *self,
                                       LrgWorld            *world,
                                       LrgRegistry         *registry,
                                       gfloat               cell_size,
                                       LrgProcgenSpawnFunc  func,
                                       gpointer             user_data);

/* --- output --- */

/**
 * lrg_procgen_grid_to_string:
 * @self: a grid
 *
 * Renders the cells as text with the legend of
 * lrg_procgen_grid_new_from_string() (tags without a character print as
 * `?`), one line per row, each ending in a newline.
 *
 * Returns: (transfer full): the text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_procgen_grid_to_string (LrgProcgenGrid *self);

/**
 * lrg_procgen_grid_hash:
 * @self: a grid
 *
 * A 64-bit FNV-1a hash of the size and the cells (metadata excluded),
 * identical on every platform; use it for golden determinism tests.
 *
 * Returns: the hash
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_procgen_grid_hash (LrgProcgenGrid *self);

/**
 * lrg_procgen_grid_to_nav_grid:
 * @self: a grid
 *
 * Builds a navigation grid of the same size where non-walkable cells are
 * blocked.
 *
 * Returns: (transfer full): a new #LrgNavGrid
 */
LRG_AVAILABLE_IN_ALL
LrgNavGrid * lrg_procgen_grid_to_nav_grid (LrgProcgenGrid *self);

G_END_DECLS
