/* lrg-road-network.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <math.h>

#include "lrg-road-network.h"

/* Connection key structure */
typedef struct
{
    gchar *road_id;
    gboolean at_end;
} ConnectionKey;

/* Connection target */
typedef struct
{
    gchar *road_id;
    gboolean at_end;
} ConnectionTarget;

struct _LrgRoadNetwork
{
    GObject parent_instance;

    /* Roads by ID */
    GHashTable *roads;

    /* Connections: key = "road_id:0/1" -> value = GList of ConnectionTarget */
    GHashTable *connections;

    /* Cached road list */
    GList *road_list;
    gboolean list_dirty;
};

G_DEFINE_TYPE (LrgRoadNetwork, lrg_road_network, G_TYPE_OBJECT)

static gchar *
make_connection_key (const gchar *road_id,
                     gboolean     at_end)
{
    return g_strdup_printf ("%s:%d", road_id, at_end ? 1 : 0);
}

static void
connection_target_free (ConnectionTarget *target)
{
    if (target == NULL)
        return;

    g_free (target->road_id);
    g_slice_free (ConnectionTarget, target);
}

static void
connection_list_free (GList *list)
{
    g_list_free_full (list, (GDestroyNotify)connection_target_free);
}

static void
lrg_road_network_finalize (GObject *object)
{
    LrgRoadNetwork *self;

    self = LRG_ROAD_NETWORK (object);

    g_hash_table_unref (self->roads);
    g_hash_table_unref (self->connections);
    g_list_free (self->road_list);

    G_OBJECT_CLASS (lrg_road_network_parent_class)->finalize (object);
}

static void
lrg_road_network_class_init (LrgRoadNetworkClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_road_network_finalize;
}

static void
lrg_road_network_init (LrgRoadNetwork *self)
{
    self->roads = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, (GDestroyNotify)lrg_road_free);
    self->connections = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, (GDestroyNotify)connection_list_free);
    self->road_list = NULL;
    self->list_dirty = TRUE;
}

LrgRoadNetwork *
lrg_road_network_new (void)
{
    return g_object_new (LRG_TYPE_ROAD_NETWORK, NULL);
}

gboolean
lrg_road_network_add_road (LrgRoadNetwork *self,
                           LrgRoad        *road)
{
    const gchar *road_id;

    g_return_val_if_fail (LRG_IS_ROAD_NETWORK (self), FALSE);
    g_return_val_if_fail (road != NULL, FALSE);

    road_id = lrg_road_get_id (road);
    if (road_id == NULL)
        return FALSE;

    /* Check for duplicate */
    if (g_hash_table_contains (self->roads, road_id))
        return FALSE;

    g_hash_table_insert (self->roads, g_strdup (road_id), road);
    self->list_dirty = TRUE;

    return TRUE;
}

LrgRoad *
lrg_road_network_get_road (LrgRoadNetwork *self,
                           const gchar    *road_id)
{
    g_return_val_if_fail (LRG_IS_ROAD_NETWORK (self), NULL);
    g_return_val_if_fail (road_id != NULL, NULL);

    return g_hash_table_lookup (self->roads, road_id);
}

gboolean
lrg_road_network_remove_road (LrgRoadNetwork *self,
                              const gchar    *road_id)
{
    gchar *key_start;
    gchar *key_end;

    g_return_val_if_fail (LRG_IS_ROAD_NETWORK (self), FALSE);
    g_return_val_if_fail (road_id != NULL, FALSE);

    if (!g_hash_table_contains (self->roads, road_id))
        return FALSE;

    /* Remove connections */
    key_start = make_connection_key (road_id, FALSE);
    key_end = make_connection_key (road_id, TRUE);

    g_hash_table_remove (self->connections, key_start);
    g_hash_table_remove (self->connections, key_end);

    g_free (key_start);
    g_free (key_end);

    /* Remove road */
    g_hash_table_remove (self->roads, road_id);
    self->list_dirty = TRUE;

    return TRUE;
}

guint
lrg_road_network_get_road_count (LrgRoadNetwork *self)
{
    g_return_val_if_fail (LRG_IS_ROAD_NETWORK (self), 0);

    return g_hash_table_size (self->roads);
}

GList *
lrg_road_network_get_roads (LrgRoadNetwork *self)
{
    g_return_val_if_fail (LRG_IS_ROAD_NETWORK (self), NULL);

    if (self->list_dirty)
    {
        g_list_free (self->road_list);
        self->road_list = g_hash_table_get_values (self->roads);
        self->list_dirty = FALSE;
    }

    return self->road_list;
}

gboolean
lrg_road_network_connect (LrgRoadNetwork *self,
                          const gchar    *from_road_id,
                          gboolean        from_end,
                          const gchar    *to_road_id,
                          gboolean        to_end)
{
    gchar *key;
    GList *connections;
    GList *l;
    ConnectionTarget *target;

    g_return_val_if_fail (LRG_IS_ROAD_NETWORK (self), FALSE);
    g_return_val_if_fail (from_road_id != NULL, FALSE);
    g_return_val_if_fail (to_road_id != NULL, FALSE);

    /* Verify roads exist */
    if (!g_hash_table_contains (self->roads, from_road_id) ||
        !g_hash_table_contains (self->roads, to_road_id))
        return FALSE;

    key = make_connection_key (from_road_id, from_end);
    connections = g_hash_table_lookup (self->connections, key);

    /* Check for duplicate */
    for (l = connections; l != NULL; l = l->next)
    {
        ConnectionTarget *t = l->data;
        if (g_strcmp0 (t->road_id, to_road_id) == 0 && t->at_end == to_end)
        {
            g_free (key);
            return TRUE; /* Already connected */
        }
    }

    /* Add connection */
    target = g_slice_new (ConnectionTarget);
    target->road_id = g_strdup (to_road_id);
    target->at_end = to_end;

    connections = g_list_append (connections, target);

    /* Update hash table (steal old list if exists) */
    g_hash_table_steal (self->connections, key);
    g_hash_table_insert (self->connections, key, connections);

    return TRUE;
}

gboolean
lrg_road_network_disconnect (LrgRoadNetwork *self,
                             const gchar    *from_road_id,
                             gboolean        from_end,
                             const gchar    *to_road_id,
                             gboolean        to_end)
{
    gchar *key;
    GList *connections;
    GList *l;
    gboolean found;

    g_return_val_if_fail (LRG_IS_ROAD_NETWORK (self), FALSE);
    g_return_val_if_fail (from_road_id != NULL, FALSE);
    g_return_val_if_fail (to_road_id != NULL, FALSE);

    key = make_connection_key (from_road_id, from_end);
    connections = g_hash_table_lookup (self->connections, key);

    found = FALSE;
    for (l = connections; l != NULL; l = l->next)
    {
        ConnectionTarget *t = l->data;
        if (g_strcmp0 (t->road_id, to_road_id) == 0 && t->at_end == to_end)
        {
            /* Remove this connection */
            g_hash_table_steal (self->connections, key);
            connections = g_list_remove_link (connections, l);
            connection_target_free (t);
            g_list_free (l);

            if (connections != NULL)
                g_hash_table_insert (self->connections, key, connections);
            else
                g_free (key);

            found = TRUE;
            break;
        }
    }

    if (!found)
        g_free (key);

    return found;
}

GList *
lrg_road_network_get_connections (LrgRoadNetwork *self,
                                  const gchar    *road_id,
                                  gboolean        from_end)
{
    gchar *key;
    GList *connections;
    GList *result;
    GList *l;

    g_return_val_if_fail (LRG_IS_ROAD_NETWORK (self), NULL);
    g_return_val_if_fail (road_id != NULL, NULL);

    key = make_connection_key (road_id, from_end);
    connections = g_hash_table_lookup (self->connections, key);
    g_free (key);

    /* Return list of road IDs */
    result = NULL;
    for (l = connections; l != NULL; l = l->next)
    {
        ConnectionTarget *t = l->data;
        result = g_list_append (result, t->road_id);
    }

    return result;
}

/*
 * Simple BFS pathfinding
 */
gboolean
lrg_road_network_find_route (LrgRoadNetwork *self,
                             const gchar    *from_road_id,
                             gfloat          from_t,
                             const gchar    *to_road_id,
                             gfloat          to_t,
                             GList         **out_road_sequence)
{
    GQueue *queue;
    GHashTable *visited;
    GHashTable *came_from;
    gboolean found;
    GList *path;
    GList *l;

    g_return_val_if_fail (LRG_IS_ROAD_NETWORK (self), FALSE);
    g_return_val_if_fail (from_road_id != NULL, FALSE);
    g_return_val_if_fail (to_road_id != NULL, FALSE);

    (void)from_t;
    (void)to_t;

    /* Same road? */
    if (g_strcmp0 (from_road_id, to_road_id) == 0)
    {
        if (out_road_sequence != NULL)
            *out_road_sequence = g_list_append (NULL, (gpointer)from_road_id);
        return TRUE;
    }

    /* BFS */
    queue = g_queue_new ();
    visited = g_hash_table_new (g_str_hash, g_str_equal);
    came_from = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);

    g_queue_push_tail (queue, (gpointer)from_road_id);
    g_hash_table_add (visited, (gpointer)from_road_id);

    found = FALSE;

    while (!g_queue_is_empty (queue))
    {
        const gchar *current;
        GList *neighbors_start;
        GList *neighbors_end;

        current = g_queue_pop_head (queue);

        if (g_strcmp0 (current, to_road_id) == 0)
        {
            found = TRUE;
            break;
        }

        /* Get connections from both ends */
        neighbors_start = lrg_road_network_get_connections (self, current, FALSE);
        neighbors_end = lrg_road_network_get_connections (self, current, TRUE);

        for (l = neighbors_start; l != NULL; l = l->next)
        {
            const gchar *neighbor = l->data;
            if (!g_hash_table_contains (visited, neighbor))
            {
                g_hash_table_add (visited, (gpointer)neighbor);
                g_hash_table_insert (came_from, (gpointer)neighbor, (gpointer)current);
                g_queue_push_tail (queue, (gpointer)neighbor);
            }
        }

        for (l = neighbors_end; l != NULL; l = l->next)
        {
            const gchar *neighbor = l->data;
            if (!g_hash_table_contains (visited, neighbor))
            {
                g_hash_table_add (visited, (gpointer)neighbor);
                g_hash_table_insert (came_from, (gpointer)neighbor, (gpointer)current);
                g_queue_push_tail (queue, (gpointer)neighbor);
            }
        }

        g_list_free (neighbors_start);
        g_list_free (neighbors_end);
    }

    /* Reconstruct path */
    path = NULL;
    if (found)
    {
        const gchar *current = to_road_id;

        while (current != NULL)
        {
            path = g_list_prepend (path, (gpointer)current);
            current = g_hash_table_lookup (came_from, current);
        }
    }

    g_queue_free (queue);
    g_hash_table_unref (visited);
    g_hash_table_unref (came_from);

    if (out_road_sequence != NULL)
        *out_road_sequence = path;
    else
        g_list_free (path);

    return found;
}

gfloat
lrg_road_network_get_route_length (LrgRoadNetwork *self,
                                   GList          *road_sequence)
{
    gfloat total_length;
    GList *l;

    g_return_val_if_fail (LRG_IS_ROAD_NETWORK (self), 0.0f);

    total_length = 0.0f;

    for (l = road_sequence; l != NULL; l = l->next)
    {
        const gchar *road_id = l->data;
        LrgRoad *road = lrg_road_network_get_road (self, road_id);

        if (road != NULL)
            total_length += lrg_road_get_length (road);
    }

    return total_length;
}

gboolean
lrg_road_network_get_nearest_road (LrgRoadNetwork *self,
                                   gfloat          x,
                                   gfloat          y,
                                   gfloat          z,
                                   const gchar   **out_road_id,
                                   gfloat         *out_t,
                                   gfloat         *out_distance)
{
    GHashTableIter iter;
    gpointer key, value;
    const gchar *best_road_id;
    gfloat best_t;
    gfloat best_distance;

    g_return_val_if_fail (LRG_IS_ROAD_NETWORK (self), FALSE);

    if (g_hash_table_size (self->roads) == 0)
        return FALSE;

    best_road_id = NULL;
    best_t = 0.0f;
    best_distance = G_MAXFLOAT;

    g_hash_table_iter_init (&iter, self->roads);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        const gchar *road_id = key;
        LrgRoad *road = value;
        gfloat t, distance;

        if (lrg_road_find_nearest_point (road, x, y, z, &t, &distance))
        {
            if (distance < best_distance)
            {
                best_distance = distance;
                best_t = t;
                best_road_id = road_id;
            }
        }
    }

    if (best_road_id == NULL)
        return FALSE;

    if (out_road_id != NULL)
        *out_road_id = best_road_id;
    if (out_t != NULL)
        *out_t = best_t;
    if (out_distance != NULL)
        *out_distance = best_distance;

    return TRUE;
}

gboolean
lrg_road_network_get_random_spawn_point (LrgRoadNetwork *self,
                                         gfloat         *x,
                                         gfloat         *y,
                                         gfloat         *z,
                                         gfloat         *heading,
                                         const gchar   **out_road_id,
                                         gfloat         *out_t)
{
    GList *roads;
    guint count;
    guint index;
    LrgRoad *road;
    const gchar *road_id;
    gfloat t;
    gfloat px, py, pz;
    gfloat dx, dy, dz;

    g_return_val_if_fail (LRG_IS_ROAD_NETWORK (self), FALSE);

    roads = lrg_road_network_get_roads (self);
    count = g_list_length (roads);

    if (count == 0)
        return FALSE;

    /* Pick random road */
    index = g_random_int_range (0, count);
    road = g_list_nth_data (roads, index);
    road_id = lrg_road_get_id (road);

    /* Pick random position on road */
    t = g_random_double_range (0.1, 0.9);

    if (!lrg_road_interpolate (road, t, &px, &py, &pz))
        return FALSE;

    if (!lrg_road_get_direction_at (road, t, &dx, &dy, &dz))
        return FALSE;

    if (x != NULL)
        *x = px;
    if (y != NULL)
        *y = py;
    if (z != NULL)
        *z = pz;
    if (heading != NULL)
        *heading = atan2f (dx, dz);
    if (out_road_id != NULL)
        *out_road_id = road_id;
    if (out_t != NULL)
        *out_t = t;

    return TRUE;
}

void
lrg_road_network_clear (LrgRoadNetwork *self)
{
    g_return_if_fail (LRG_IS_ROAD_NETWORK (self));

    g_hash_table_remove_all (self->roads);
    g_hash_table_remove_all (self->connections);
    g_list_free (self->road_list);
    self->road_list = NULL;
    self->list_dirty = TRUE;
}
