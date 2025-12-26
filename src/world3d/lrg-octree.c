/* lrg-octree.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Octree spatial data structure implementation.
 */

#include "config.h"
#include "lrg-octree.h"

#include <math.h>

#define DEFAULT_MAX_DEPTH 8
#define DEFAULT_MAX_OBJECTS 8

/* Object entry stored in the octree */
typedef struct
{
    gpointer        object;
    LrgBoundingBox3D bounds;
} OctreeEntry;

/* Octree node */
typedef struct _OctreeNode OctreeNode;

struct _OctreeNode
{
    LrgBoundingBox3D  bounds;
    GPtrArray        *entries;      /* OctreeEntry* */
    OctreeNode       *children[8];  /* 8 children for octree */
    guint             depth;
    gboolean          is_leaf;
};

struct _LrgOctree
{
    GObject          parent_instance;

    OctreeNode      *root;
    LrgBoundingBox3D bounds;
    guint            max_depth;
    guint            max_objects;
    guint            object_count;
    guint            node_count;

    /* Object lookup for fast removal */
    GHashTable      *object_nodes;  /* gpointer -> OctreeNode* */
};

G_DEFINE_FINAL_TYPE (LrgOctree, lrg_octree, G_TYPE_OBJECT)

/* Forward declarations */
static OctreeNode * octree_node_new             (const LrgBoundingBox3D *bounds,
                                                 guint                   depth);
static void         octree_node_free            (OctreeNode             *node);
static void         octree_node_subdivide       (LrgOctree              *tree,
                                                 OctreeNode             *node);
static gboolean     octree_node_insert          (LrgOctree              *tree,
                                                 OctreeNode             *node,
                                                 OctreeEntry            *entry);
static void         octree_node_query_box       (OctreeNode             *node,
                                                 const LrgBoundingBox3D *query,
                                                 GPtrArray              *results);
static void         octree_node_query_sphere    (OctreeNode             *node,
                                                 const GrlVector3       *center,
                                                 gfloat                  radius,
                                                 GPtrArray              *results);
static guint        octree_node_count_nodes     (OctreeNode             *node);
static gboolean     box_intersects_sphere       (const LrgBoundingBox3D *box,
                                                 const GrlVector3       *center,
                                                 gfloat                  radius);

static OctreeEntry *
octree_entry_new (gpointer                object,
                  const LrgBoundingBox3D *bounds)
{
    OctreeEntry *entry;

    entry = g_new0 (OctreeEntry, 1);
    entry->object = object;
    entry->bounds = *bounds;

    return entry;
}

static void
octree_entry_free (gpointer data)
{
    g_free (data);
}

static OctreeNode *
octree_node_new (const LrgBoundingBox3D *bounds,
                 guint                   depth)
{
    OctreeNode *node;
    gint i;

    node = g_new0 (OctreeNode, 1);
    node->bounds = *bounds;
    node->entries = g_ptr_array_new_with_free_func (octree_entry_free);
    node->depth = depth;
    node->is_leaf = TRUE;

    for (i = 0; i < 8; i++)
        node->children[i] = NULL;

    return node;
}

static void
octree_node_free (OctreeNode *node)
{
    gint i;

    if (node == NULL)
        return;

    g_ptr_array_unref (node->entries);

    for (i = 0; i < 8; i++)
        octree_node_free (node->children[i]);

    g_free (node);
}

static void
octree_node_get_child_bounds (const LrgBoundingBox3D *parent,
                              gint                    index,
                              LrgBoundingBox3D       *child)
{
    gfloat mid_x;
    gfloat mid_y;
    gfloat mid_z;

    mid_x = (parent->min.x + parent->max.x) * 0.5f;
    mid_y = (parent->min.y + parent->max.y) * 0.5f;
    mid_z = (parent->min.z + parent->max.z) * 0.5f;

    /*
     * Child indices:
     * 0: min x, min y, min z
     * 1: max x, min y, min z
     * 2: min x, max y, min z
     * 3: max x, max y, min z
     * 4: min x, min y, max z
     * 5: max x, min y, max z
     * 6: min x, max y, max z
     * 7: max x, max y, max z
     */
    child->min.x = (index & 1) ? mid_x : parent->min.x;
    child->max.x = (index & 1) ? parent->max.x : mid_x;
    child->min.y = (index & 2) ? mid_y : parent->min.y;
    child->max.y = (index & 2) ? parent->max.y : mid_y;
    child->min.z = (index & 4) ? mid_z : parent->min.z;
    child->max.z = (index & 4) ? parent->max.z : mid_z;
}

static void
octree_node_subdivide (LrgOctree  *tree,
                       OctreeNode *node)
{
    gint i;
    guint j;
    LrgBoundingBox3D child_bounds;
    GPtrArray *old_entries;

    if (!node->is_leaf)
        return;

    /* Create children */
    for (i = 0; i < 8; i++)
    {
        octree_node_get_child_bounds (&node->bounds, i, &child_bounds);
        node->children[i] = octree_node_new (&child_bounds, node->depth + 1);
        tree->node_count++;
    }

    node->is_leaf = FALSE;

    /* Re-insert entries into children */
    old_entries = node->entries;
    node->entries = g_ptr_array_new_with_free_func (octree_entry_free);

    for (j = 0; j < old_entries->len; j++)
    {
        OctreeEntry *entry = g_ptr_array_index (old_entries, j);
        OctreeEntry *new_entry = octree_entry_new (entry->object, &entry->bounds);
        octree_node_insert (tree, node, new_entry);
    }

    g_ptr_array_unref (old_entries);
}

static gboolean
octree_node_insert (LrgOctree   *tree,
                    OctreeNode  *node,
                    OctreeEntry *entry)
{
    gint i;
    gint fitting_child;

    /* Check if entry fits in any child */
    if (!node->is_leaf)
    {
        fitting_child = -1;

        for (i = 0; i < 8; i++)
        {
            if (lrg_bounding_box3d_contains (&node->children[i]->bounds, &entry->bounds))
            {
                fitting_child = i;
                break;
            }
        }

        if (fitting_child >= 0)
        {
            return octree_node_insert (tree, node->children[fitting_child], entry);
        }

        /* Entry spans multiple children, store at this level */
        g_ptr_array_add (node->entries, entry);
        g_hash_table_insert (tree->object_nodes, entry->object, node);
        return TRUE;
    }

    /* Leaf node */
    g_ptr_array_add (node->entries, entry);
    g_hash_table_insert (tree->object_nodes, entry->object, node);

    /* Subdivide if needed */
    if (node->entries->len > tree->max_objects && node->depth < tree->max_depth)
    {
        octree_node_subdivide (tree, node);
    }

    return TRUE;
}

static void
octree_node_query_box (OctreeNode             *node,
                       const LrgBoundingBox3D *query,
                       GPtrArray              *results)
{
    guint i;
    gint j;

    /* Check entries at this node */
    for (i = 0; i < node->entries->len; i++)
    {
        OctreeEntry *entry = g_ptr_array_index (node->entries, i);
        if (lrg_bounding_box3d_intersects (&entry->bounds, query))
        {
            g_ptr_array_add (results, entry->object);
        }
    }

    /* Recurse into children */
    if (!node->is_leaf)
    {
        for (j = 0; j < 8; j++)
        {
            if (lrg_bounding_box3d_intersects (&node->children[j]->bounds, query))
            {
                octree_node_query_box (node->children[j], query, results);
            }
        }
    }
}

static gboolean
box_intersects_sphere (const LrgBoundingBox3D *box,
                       const GrlVector3       *center,
                       gfloat                  radius)
{
    gfloat dx;
    gfloat dy;
    gfloat dz;
    gfloat dist_sq;

    /* Find closest point on box to sphere center */
    dx = 0.0f;
    dy = 0.0f;
    dz = 0.0f;

    if (center->x < box->min.x)
        dx = box->min.x - center->x;
    else if (center->x > box->max.x)
        dx = center->x - box->max.x;

    if (center->y < box->min.y)
        dy = box->min.y - center->y;
    else if (center->y > box->max.y)
        dy = center->y - box->max.y;

    if (center->z < box->min.z)
        dz = box->min.z - center->z;
    else if (center->z > box->max.z)
        dz = center->z - box->max.z;

    dist_sq = dx * dx + dy * dy + dz * dz;
    return dist_sq <= radius * radius;
}

static void
octree_node_query_sphere (OctreeNode       *node,
                          const GrlVector3 *center,
                          gfloat            radius,
                          GPtrArray        *results)
{
    guint i;
    gint j;

    /* Check entries at this node */
    for (i = 0; i < node->entries->len; i++)
    {
        OctreeEntry *entry = g_ptr_array_index (node->entries, i);
        if (box_intersects_sphere (&entry->bounds, center, radius))
        {
            g_ptr_array_add (results, entry->object);
        }
    }

    /* Recurse into children */
    if (!node->is_leaf)
    {
        for (j = 0; j < 8; j++)
        {
            if (box_intersects_sphere (&node->children[j]->bounds, center, radius))
            {
                octree_node_query_sphere (node->children[j], center, radius, results);
            }
        }
    }
}

static guint
octree_node_count_nodes (OctreeNode *node)
{
    gint i;
    guint count;

    if (node == NULL)
        return 0;

    count = 1;

    if (!node->is_leaf)
    {
        for (i = 0; i < 8; i++)
            count += octree_node_count_nodes (node->children[i]);
    }

    return count;
}

static void
lrg_octree_finalize (GObject *object)
{
    LrgOctree *self = LRG_OCTREE (object);

    octree_node_free (self->root);
    g_hash_table_unref (self->object_nodes);

    G_OBJECT_CLASS (lrg_octree_parent_class)->finalize (object);
}

static void
lrg_octree_class_init (LrgOctreeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_octree_finalize;
}

static void
lrg_octree_init (LrgOctree *self)
{
    self->root = NULL;
    self->max_depth = DEFAULT_MAX_DEPTH;
    self->max_objects = DEFAULT_MAX_OBJECTS;
    self->object_count = 0;
    self->node_count = 0;
    self->object_nodes = g_hash_table_new (g_direct_hash, g_direct_equal);
}

/**
 * lrg_octree_new:
 * @bounds: (transfer none): World bounds for the octree
 *
 * Creates a new octree with default settings.
 *
 * Returns: (transfer full): A new #LrgOctree
 */
LrgOctree *
lrg_octree_new (const LrgBoundingBox3D *bounds)
{
    return lrg_octree_new_with_depth (bounds, DEFAULT_MAX_DEPTH);
}

/**
 * lrg_octree_new_with_depth:
 * @bounds: (transfer none): World bounds for the octree
 * @max_depth: Maximum subdivision depth (default: 8)
 *
 * Creates a new octree with specified maximum depth.
 *
 * Returns: (transfer full): A new #LrgOctree
 */
LrgOctree *
lrg_octree_new_with_depth (const LrgBoundingBox3D *bounds,
                           guint                   max_depth)
{
    LrgOctree *self;

    g_return_val_if_fail (bounds != NULL, NULL);

    self = g_object_new (LRG_TYPE_OCTREE, NULL);
    self->bounds = *bounds;
    self->max_depth = max_depth;
    self->root = octree_node_new (bounds, 0);
    self->node_count = 1;

    return self;
}

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
gboolean
lrg_octree_insert (LrgOctree              *self,
                   gpointer                object,
                   const LrgBoundingBox3D *bounds)
{
    OctreeEntry *entry;

    g_return_val_if_fail (LRG_IS_OCTREE (self), FALSE);
    g_return_val_if_fail (object != NULL, FALSE);
    g_return_val_if_fail (bounds != NULL, FALSE);

    /* Check if object is already in the tree */
    if (g_hash_table_contains (self->object_nodes, object))
        return FALSE;

    entry = octree_entry_new (object, bounds);

    if (octree_node_insert (self, self->root, entry))
    {
        self->object_count++;
        return TRUE;
    }

    octree_entry_free (entry);
    return FALSE;
}

/**
 * lrg_octree_remove:
 * @self: An #LrgOctree
 * @object: Object to remove
 *
 * Removes an object from the octree.
 *
 * Returns: %TRUE if the object was found and removed
 */
gboolean
lrg_octree_remove (LrgOctree *self,
                   gpointer   object)
{
    OctreeNode *node;
    guint i;

    g_return_val_if_fail (LRG_IS_OCTREE (self), FALSE);
    g_return_val_if_fail (object != NULL, FALSE);

    node = g_hash_table_lookup (self->object_nodes, object);
    if (node == NULL)
        return FALSE;

    /* Find and remove the entry */
    for (i = 0; i < node->entries->len; i++)
    {
        OctreeEntry *entry = g_ptr_array_index (node->entries, i);
        if (entry->object == object)
        {
            g_ptr_array_remove_index (node->entries, i);
            g_hash_table_remove (self->object_nodes, object);
            self->object_count--;
            return TRUE;
        }
    }

    return FALSE;
}

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
gboolean
lrg_octree_update (LrgOctree              *self,
                   gpointer                object,
                   const LrgBoundingBox3D *new_bounds)
{
    g_return_val_if_fail (LRG_IS_OCTREE (self), FALSE);
    g_return_val_if_fail (object != NULL, FALSE);
    g_return_val_if_fail (new_bounds != NULL, FALSE);

    if (!lrg_octree_remove (self, object))
        return FALSE;

    return lrg_octree_insert (self, object, new_bounds);
}

/**
 * lrg_octree_clear:
 * @self: An #LrgOctree
 *
 * Removes all objects from the octree.
 */
void
lrg_octree_clear (LrgOctree *self)
{
    g_return_if_fail (LRG_IS_OCTREE (self));

    octree_node_free (self->root);
    g_hash_table_remove_all (self->object_nodes);

    self->root = octree_node_new (&self->bounds, 0);
    self->node_count = 1;
    self->object_count = 0;
}

/**
 * lrg_octree_query_box:
 * @self: An #LrgOctree
 * @query: (transfer none): Query bounding box
 *
 * Finds all objects that intersect with the query box.
 *
 * Returns: (transfer container) (element-type gpointer): Array of objects
 */
GPtrArray *
lrg_octree_query_box (LrgOctree              *self,
                      const LrgBoundingBox3D *query)
{
    GPtrArray *results;

    g_return_val_if_fail (LRG_IS_OCTREE (self), NULL);
    g_return_val_if_fail (query != NULL, NULL);

    results = g_ptr_array_new ();
    octree_node_query_box (self->root, query, results);

    return results;
}

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
GPtrArray *
lrg_octree_query_sphere (LrgOctree        *self,
                         const GrlVector3 *center,
                         gfloat            radius)
{
    GPtrArray *results;

    g_return_val_if_fail (LRG_IS_OCTREE (self), NULL);
    g_return_val_if_fail (center != NULL, NULL);
    g_return_val_if_fail (radius > 0.0f, NULL);

    results = g_ptr_array_new ();
    octree_node_query_sphere (self->root, center, radius, results);

    return results;
}

/**
 * lrg_octree_query_point:
 * @self: An #LrgOctree
 * @point: (transfer none): Point to query
 *
 * Finds all objects that contain the given point.
 *
 * Returns: (transfer container) (element-type gpointer): Array of objects
 */
GPtrArray *
lrg_octree_query_point (LrgOctree        *self,
                        const GrlVector3 *point)
{
    g_autoptr(LrgBoundingBox3D) tiny_box = NULL;

    g_return_val_if_fail (LRG_IS_OCTREE (self), NULL);
    g_return_val_if_fail (point != NULL, NULL);

    /* Use a tiny box for point query */
    tiny_box = lrg_bounding_box3d_new (point->x, point->y, point->z,
                                       point->x, point->y, point->z);

    return lrg_octree_query_box (self, tiny_box);
}

/**
 * lrg_octree_query_nearest:
 * @self: An #LrgOctree
 * @point: (transfer none): Point to search from
 *
 * Finds the nearest object to a point.
 *
 * Returns: (transfer none) (nullable): The nearest object, or %NULL if empty
 */
gpointer
lrg_octree_query_nearest (LrgOctree        *self,
                          const GrlVector3 *point)
{
    g_autoptr(GPtrArray) candidates = NULL;
    gpointer nearest;
    gfloat nearest_dist_sq;
    guint i;

    g_return_val_if_fail (LRG_IS_OCTREE (self), NULL);
    g_return_val_if_fail (point != NULL, NULL);

    if (self->object_count == 0)
        return NULL;

    /* Start with a reasonable search radius and expand if needed */
    candidates = lrg_octree_query_sphere (self, point, 100.0f);

    if (candidates->len == 0)
    {
        /* Try the whole world */
        g_ptr_array_unref (candidates);
        candidates = lrg_octree_query_box (self, &self->bounds);
    }

    if (candidates->len == 0)
        return NULL;

    nearest = NULL;
    nearest_dist_sq = G_MAXFLOAT;

    for (i = 0; i < candidates->len; i++)
    {
        gpointer obj;
        OctreeNode *node;
        guint j;

        obj = g_ptr_array_index (candidates, i);
        node = g_hash_table_lookup (self->object_nodes, obj);

        if (node == NULL)
            continue;

        /* Find the entry for this object */
        for (j = 0; j < node->entries->len; j++)
        {
            OctreeEntry *entry = g_ptr_array_index (node->entries, j);
            if (entry->object == obj)
            {
                gfloat cx;
                gfloat cy;
                gfloat cz;
                gfloat dx;
                gfloat dy;
                gfloat dz;
                gfloat dist_sq;

                /* Distance from point to center of object bounds */
                cx = (entry->bounds.min.x + entry->bounds.max.x) * 0.5f;
                cy = (entry->bounds.min.y + entry->bounds.max.y) * 0.5f;
                cz = (entry->bounds.min.z + entry->bounds.max.z) * 0.5f;

                dx = point->x - cx;
                dy = point->y - cy;
                dz = point->z - cz;
                dist_sq = dx * dx + dy * dy + dz * dz;

                if (dist_sq < nearest_dist_sq)
                {
                    nearest_dist_sq = dist_sq;
                    nearest = obj;
                }
                break;
            }
        }
    }

    return nearest;
}

/**
 * lrg_octree_get_bounds:
 * @self: An #LrgOctree
 *
 * Gets the world bounds of the octree.
 *
 * Returns: (transfer full): The bounds
 */
LrgBoundingBox3D *
lrg_octree_get_bounds (LrgOctree *self)
{
    g_return_val_if_fail (LRG_IS_OCTREE (self), NULL);

    return lrg_bounding_box3d_copy (&self->bounds);
}

/**
 * lrg_octree_get_object_count:
 * @self: An #LrgOctree
 *
 * Gets the total number of objects in the octree.
 *
 * Returns: Object count
 */
guint
lrg_octree_get_object_count (LrgOctree *self)
{
    g_return_val_if_fail (LRG_IS_OCTREE (self), 0);

    return self->object_count;
}

/**
 * lrg_octree_get_node_count:
 * @self: An #LrgOctree
 *
 * Gets the number of nodes in the octree.
 *
 * Returns: Node count
 */
guint
lrg_octree_get_node_count (LrgOctree *self)
{
    g_return_val_if_fail (LRG_IS_OCTREE (self), 0);

    return octree_node_count_nodes (self->root);
}

/**
 * lrg_octree_get_max_depth:
 * @self: An #LrgOctree
 *
 * Gets the maximum subdivision depth.
 *
 * Returns: Maximum depth
 */
guint
lrg_octree_get_max_depth (LrgOctree *self)
{
    g_return_val_if_fail (LRG_IS_OCTREE (self), 0);

    return self->max_depth;
}

/**
 * lrg_octree_set_max_depth:
 * @self: An #LrgOctree
 * @max_depth: Maximum depth
 *
 * Sets the maximum subdivision depth.
 * Only affects future insertions.
 */
void
lrg_octree_set_max_depth (LrgOctree *self,
                          guint      max_depth)
{
    g_return_if_fail (LRG_IS_OCTREE (self));

    self->max_depth = max_depth;
}

/**
 * lrg_octree_get_max_objects_per_node:
 * @self: An #LrgOctree
 *
 * Gets the maximum objects per node before subdivision.
 *
 * Returns: Maximum objects per node
 */
guint
lrg_octree_get_max_objects_per_node (LrgOctree *self)
{
    g_return_val_if_fail (LRG_IS_OCTREE (self), 0);

    return self->max_objects;
}

/**
 * lrg_octree_set_max_objects_per_node:
 * @self: An #LrgOctree
 * @max_objects: Maximum objects per node
 *
 * Sets the maximum objects per node before subdivision.
 */
void
lrg_octree_set_max_objects_per_node (LrgOctree *self,
                                     guint      max_objects)
{
    g_return_if_fail (LRG_IS_OCTREE (self));
    g_return_if_fail (max_objects > 0);

    self->max_objects = max_objects;
}

/**
 * lrg_octree_rebuild:
 * @self: An #LrgOctree
 *
 * Rebuilds the octree structure.
 * Useful after changing max_depth or max_objects_per_node.
 */
void
lrg_octree_rebuild (LrgOctree *self)
{
    GPtrArray *all_entries;
    GHashTableIter iter;
    gpointer key;
    gpointer value;
    guint i;

    g_return_if_fail (LRG_IS_OCTREE (self));

    /* Collect all entries */
    all_entries = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->object_nodes);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        OctreeNode *node = value;
        guint j;

        for (j = 0; j < node->entries->len; j++)
        {
            OctreeEntry *entry = g_ptr_array_index (node->entries, j);
            if (entry->object == key)
            {
                OctreeEntry *copy = octree_entry_new (entry->object, &entry->bounds);
                g_ptr_array_add (all_entries, copy);
                break;
            }
        }
    }

    /* Clear and rebuild */
    octree_node_free (self->root);
    g_hash_table_remove_all (self->object_nodes);

    self->root = octree_node_new (&self->bounds, 0);
    self->node_count = 1;
    self->object_count = 0;

    /* Re-insert all entries */
    for (i = 0; i < all_entries->len; i++)
    {
        OctreeEntry *entry = g_ptr_array_index (all_entries, i);
        lrg_octree_insert (self, entry->object, &entry->bounds);
    }

    g_ptr_array_free (all_entries, TRUE);
}
