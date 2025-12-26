/* lrg-octree.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Octree spatial data structure for efficient 3D queries.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-bounding-box3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_OCTREE (lrg_octree_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgOctree, lrg_octree, LRG, OCTREE, GObject)

/**
 * lrg_octree_new:
 * @bounds: (transfer none): World bounds for the octree
 *
 * Creates a new octree with default settings.
 *
 * Returns: (transfer full): A new #LrgOctree
 */
LRG_AVAILABLE_IN_ALL
LrgOctree *         lrg_octree_new                  (const LrgBoundingBox3D *bounds);

/**
 * lrg_octree_new_with_depth:
 * @bounds: (transfer none): World bounds for the octree
 * @max_depth: Maximum subdivision depth (default: 8)
 *
 * Creates a new octree with specified maximum depth.
 *
 * Returns: (transfer full): A new #LrgOctree
 */
LRG_AVAILABLE_IN_ALL
LrgOctree *         lrg_octree_new_with_depth       (const LrgBoundingBox3D *bounds,
                                                     guint                   max_depth);

/**
 * lrg_octree_insert:
 * @self: An #LrgOctree
 * @object: Object to insert
 * @bounds: (transfer none): Object's bounding box
 *
 * Inserts an object into the octree.
 *
 * Returns: %TRUE if inserted successfully
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_octree_insert               (LrgOctree              *self,
                                                     gpointer                object,
                                                     const LrgBoundingBox3D *bounds);

/**
 * lrg_octree_remove:
 * @self: An #LrgOctree
 * @object: Object to remove
 *
 * Removes an object from the octree.
 *
 * Returns: %TRUE if the object was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_octree_remove               (LrgOctree              *self,
                                                     gpointer                object);

/**
 * lrg_octree_update:
 * @self: An #LrgOctree
 * @object: Object to update
 * @new_bounds: (transfer none): New bounding box for the object
 *
 * Updates an object's position in the octree.
 * This removes and re-inserts the object.
 *
 * Returns: %TRUE if updated successfully
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_octree_update               (LrgOctree              *self,
                                                     gpointer                object,
                                                     const LrgBoundingBox3D *new_bounds);

/**
 * lrg_octree_clear:
 * @self: An #LrgOctree
 *
 * Removes all objects from the octree.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_octree_clear                (LrgOctree              *self);

/**
 * lrg_octree_query_box:
 * @self: An #LrgOctree
 * @query: (transfer none): Query bounding box
 *
 * Finds all objects that intersect with the query box.
 *
 * Returns: (transfer container) (element-type gpointer): Array of objects
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_octree_query_box            (LrgOctree              *self,
                                                     const LrgBoundingBox3D *query);

/**
 * lrg_octree_query_sphere:
 * @self: An #LrgOctree
 * @center: (transfer none): Sphere center
 * @radius: Sphere radius
 *
 * Finds all objects that intersect with a sphere.
 *
 * Returns: (transfer container) (element-type gpointer): Array of objects
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_octree_query_sphere         (LrgOctree              *self,
                                                     const GrlVector3       *center,
                                                     gfloat                  radius);

/**
 * lrg_octree_query_point:
 * @self: An #LrgOctree
 * @point: (transfer none): Point to query
 *
 * Finds all objects that contain the given point.
 *
 * Returns: (transfer container) (element-type gpointer): Array of objects
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_octree_query_point          (LrgOctree              *self,
                                                     const GrlVector3       *point);

/**
 * lrg_octree_query_nearest:
 * @self: An #LrgOctree
 * @point: (transfer none): Point to search from
 *
 * Finds the nearest object to a point.
 *
 * Returns: (transfer none) (nullable): The nearest object, or %NULL if empty
 */
LRG_AVAILABLE_IN_ALL
gpointer            lrg_octree_query_nearest        (LrgOctree              *self,
                                                     const GrlVector3       *point);

/**
 * lrg_octree_get_bounds:
 * @self: An #LrgOctree
 *
 * Gets the world bounds of the octree.
 *
 * Returns: (transfer full): The bounds
 */
LRG_AVAILABLE_IN_ALL
LrgBoundingBox3D *  lrg_octree_get_bounds           (LrgOctree              *self);

/**
 * lrg_octree_get_object_count:
 * @self: An #LrgOctree
 *
 * Gets the total number of objects in the octree.
 *
 * Returns: Object count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_octree_get_object_count     (LrgOctree              *self);

/**
 * lrg_octree_get_node_count:
 * @self: An #LrgOctree
 *
 * Gets the number of nodes in the octree.
 *
 * Returns: Node count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_octree_get_node_count       (LrgOctree              *self);

/**
 * lrg_octree_get_max_depth:
 * @self: An #LrgOctree
 *
 * Gets the maximum subdivision depth.
 *
 * Returns: Maximum depth
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_octree_get_max_depth        (LrgOctree              *self);

/**
 * lrg_octree_set_max_depth:
 * @self: An #LrgOctree
 * @max_depth: Maximum depth
 *
 * Sets the maximum subdivision depth.
 * Only affects future insertions.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_octree_set_max_depth        (LrgOctree              *self,
                                                     guint                   max_depth);

/**
 * lrg_octree_get_max_objects_per_node:
 * @self: An #LrgOctree
 *
 * Gets the maximum objects per node before subdivision.
 *
 * Returns: Maximum objects per node
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_octree_get_max_objects_per_node (LrgOctree          *self);

/**
 * lrg_octree_set_max_objects_per_node:
 * @self: An #LrgOctree
 * @max_objects: Maximum objects per node
 *
 * Sets the maximum objects per node before subdivision.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_octree_set_max_objects_per_node (LrgOctree          *self,
                                                         guint               max_objects);

/**
 * lrg_octree_rebuild:
 * @self: An #LrgOctree
 *
 * Rebuilds the octree structure.
 * Useful after changing max_depth or max_objects_per_node.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_octree_rebuild              (LrgOctree              *self);

G_END_DECLS
