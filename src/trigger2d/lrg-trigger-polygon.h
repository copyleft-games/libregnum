/* lrg-trigger-polygon.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Polygon trigger zone.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include "lrg-trigger2d.h"

G_BEGIN_DECLS

#define LRG_TYPE_TRIGGER_POLYGON (lrg_trigger_polygon_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTriggerPolygon, lrg_trigger_polygon, LRG, TRIGGER_POLYGON, LrgTrigger2D)

/**
 * lrg_trigger_polygon_new:
 *
 * Creates a new empty polygon trigger zone.
 * Use lrg_trigger_polygon_add_vertex() to add vertices.
 *
 * Returns: (transfer full): A new #LrgTriggerPolygon
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTriggerPolygon * lrg_trigger_polygon_new             (void);

/**
 * lrg_trigger_polygon_new_with_id:
 * @id: Unique identifier for the trigger
 *
 * Creates a new empty polygon trigger zone with an ID.
 *
 * Returns: (transfer full): A new #LrgTriggerPolygon
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTriggerPolygon * lrg_trigger_polygon_new_with_id     (const gchar *id);

/**
 * lrg_trigger_polygon_new_from_points:
 * @points: (array length=n_points) (element-type gfloat): Array of x,y pairs
 * @n_points: Number of points (array length is n_points * 2)
 *
 * Creates a new polygon trigger from an array of points.
 * The points array should contain x,y pairs: [x1,y1,x2,y2,...].
 *
 * Returns: (transfer full): A new #LrgTriggerPolygon
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTriggerPolygon * lrg_trigger_polygon_new_from_points (const gfloat *points,
                                                         gsize         n_points);

/* Vertex management */

/**
 * lrg_trigger_polygon_add_vertex:
 * @self: A #LrgTriggerPolygon
 * @x: X coordinate of the vertex
 * @y: Y coordinate of the vertex
 *
 * Adds a vertex to the polygon.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_polygon_add_vertex      (LrgTriggerPolygon *self,
                                                         gfloat             x,
                                                         gfloat             y);

/**
 * lrg_trigger_polygon_insert_vertex:
 * @self: A #LrgTriggerPolygon
 * @index: Position to insert at
 * @x: X coordinate of the vertex
 * @y: Y coordinate of the vertex
 *
 * Inserts a vertex at the specified position.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_polygon_insert_vertex   (LrgTriggerPolygon *self,
                                                         guint              index,
                                                         gfloat             x,
                                                         gfloat             y);

/**
 * lrg_trigger_polygon_remove_vertex:
 * @self: A #LrgTriggerPolygon
 * @index: Index of vertex to remove
 *
 * Removes a vertex at the specified index.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_polygon_remove_vertex   (LrgTriggerPolygon *self,
                                                         guint              index);

/**
 * lrg_trigger_polygon_set_vertex:
 * @self: A #LrgTriggerPolygon
 * @index: Index of vertex to modify
 * @x: New X coordinate
 * @y: New Y coordinate
 *
 * Sets the position of a vertex.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_polygon_set_vertex      (LrgTriggerPolygon *self,
                                                         guint              index,
                                                         gfloat             x,
                                                         gfloat             y);

/**
 * lrg_trigger_polygon_get_vertex:
 * @self: A #LrgTriggerPolygon
 * @index: Index of vertex to get
 * @out_x: (out) (nullable): Return location for X
 * @out_y: (out) (nullable): Return location for Y
 *
 * Gets the position of a vertex.
 *
 * Returns: %TRUE if the index was valid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger_polygon_get_vertex      (LrgTriggerPolygon *self,
                                                         guint              index,
                                                         gfloat            *out_x,
                                                         gfloat            *out_y);

/**
 * lrg_trigger_polygon_get_vertex_count:
 * @self: A #LrgTriggerPolygon
 *
 * Gets the number of vertices in the polygon.
 *
 * Returns: The vertex count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_trigger_polygon_get_vertex_count (LrgTriggerPolygon *self);

/**
 * lrg_trigger_polygon_clear_vertices:
 * @self: A #LrgTriggerPolygon
 *
 * Removes all vertices from the polygon.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_polygon_clear_vertices  (LrgTriggerPolygon *self);

/* Transform */

/**
 * lrg_trigger_polygon_translate:
 * @self: A #LrgTriggerPolygon
 * @dx: X offset
 * @dy: Y offset
 *
 * Moves all vertices by the specified offset.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_polygon_translate       (LrgTriggerPolygon *self,
                                                         gfloat             dx,
                                                         gfloat             dy);

/**
 * lrg_trigger_polygon_scale:
 * @self: A #LrgTriggerPolygon
 * @sx: X scale factor
 * @sy: Y scale factor
 *
 * Scales all vertices around the centroid.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_polygon_scale           (LrgTriggerPolygon *self,
                                                         gfloat             sx,
                                                         gfloat             sy);

/**
 * lrg_trigger_polygon_rotate:
 * @self: A #LrgTriggerPolygon
 * @angle: Rotation angle in radians
 *
 * Rotates all vertices around the centroid.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_polygon_rotate          (LrgTriggerPolygon *self,
                                                         gfloat             angle);

/* Properties */

/**
 * lrg_trigger_polygon_get_centroid:
 * @self: A #LrgTriggerPolygon
 * @out_x: (out) (nullable): Return location for centroid X
 * @out_y: (out) (nullable): Return location for centroid Y
 *
 * Gets the centroid (center of mass) of the polygon.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_polygon_get_centroid    (LrgTriggerPolygon *self,
                                                         gfloat            *out_x,
                                                         gfloat            *out_y);

/**
 * lrg_trigger_polygon_get_area:
 * @self: A #LrgTriggerPolygon
 *
 * Gets the area of the polygon using the shoelace formula.
 *
 * Returns: The area (always positive)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_trigger_polygon_get_area        (LrgTriggerPolygon *self);

/**
 * lrg_trigger_polygon_is_convex:
 * @self: A #LrgTriggerPolygon
 *
 * Checks if the polygon is convex.
 *
 * Returns: %TRUE if the polygon is convex
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger_polygon_is_convex       (LrgTriggerPolygon *self);

/**
 * lrg_trigger_polygon_is_valid:
 * @self: A #LrgTriggerPolygon
 *
 * Checks if the polygon is valid (has at least 3 vertices and no
 * self-intersecting edges).
 *
 * Returns: %TRUE if the polygon is valid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger_polygon_is_valid        (LrgTriggerPolygon *self);

G_END_DECLS
