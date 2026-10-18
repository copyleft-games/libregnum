/* lrg-nav-poly-mesh-cache.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * An LRU of baked navigation meshes: a queue of entries, most recently
 * used at the head, and a table from digest to the entry's queue link.
 */

#include "lrg-nav-poly-mesh-cache.h"

/* one cached bake */
typedef struct
{
    gchar  *digest;
    GBytes *bytes;
} Entry;

struct _LrgNavPolyMeshCache
{
    GObject parent_instance;

    guint       max_entries;
    GQueue      order;      /* Entry *, most recently used first */
    GHashTable *index;      /* digest (borrowed from the entry) -> GList * */
    gsize       total;      /* bytes held */
    guint       hits;
    guint       misses;
};

G_DEFINE_FINAL_TYPE (LrgNavPolyMeshCache, lrg_nav_poly_mesh_cache, G_TYPE_OBJECT)

static void
entry_free (Entry *entry)
{
    g_free (entry->digest);
    g_bytes_unref (entry->bytes);
    g_free (entry);
}

/* removes the entry at @link from the queue and the index */
static void
drop_link (LrgNavPolyMeshCache *self,
           GList               *link)
{
    Entry *entry = link->data;

    g_hash_table_remove (self->index, entry->digest);
    self->total -= g_bytes_get_size (entry->bytes);
    g_queue_delete_link (&self->order, link);
    entry_free (entry);
}

static void
lrg_nav_poly_mesh_cache_finalize (GObject *object)
{
    LrgNavPolyMeshCache *self = LRG_NAV_POLY_MESH_CACHE (object);

    lrg_nav_poly_mesh_cache_clear (self);
    g_hash_table_unref (self->index);

    G_OBJECT_CLASS (lrg_nav_poly_mesh_cache_parent_class)->finalize (object);
}

static void
lrg_nav_poly_mesh_cache_class_init (LrgNavPolyMeshCacheClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_nav_poly_mesh_cache_finalize;
}

static void
lrg_nav_poly_mesh_cache_init (LrgNavPolyMeshCache *self)
{
    self->max_entries = 1;
    g_queue_init (&self->order);
    self->index = g_hash_table_new (g_str_hash, g_str_equal);
}

LrgNavPolyMeshCache *
lrg_nav_poly_mesh_cache_new (guint max_entries)
{
    LrgNavPolyMeshCache *self;

    self = g_object_new (LRG_TYPE_NAV_POLY_MESH_CACHE, NULL);
    self->max_entries = MAX (max_entries, 1u);
    return self;
}

GBytes *
lrg_nav_poly_mesh_cache_lookup (LrgNavPolyMeshCache *self,
                                const gchar         *digest)
{
    GList *link;
    Entry *entry;

    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH_CACHE (self), NULL);
    g_return_val_if_fail (digest != NULL, NULL);

    link = g_hash_table_lookup (self->index, digest);
    if (link == NULL)
    {
        self->misses++;
        return NULL;
    }
    self->hits++;
    /* most recently used: to the head */
    g_queue_unlink (&self->order, link);
    g_queue_push_head_link (&self->order, link);
    entry = link->data;
    return g_bytes_ref (entry->bytes);
}

void
lrg_nav_poly_mesh_cache_insert (LrgNavPolyMeshCache *self,
                                const gchar         *digest,
                                GBytes              *bytes)
{
    GList *link;
    Entry *entry;

    g_return_if_fail (LRG_IS_NAV_POLY_MESH_CACHE (self));
    g_return_if_fail (digest != NULL);
    g_return_if_fail (bytes != NULL);

    link = g_hash_table_lookup (self->index, digest);
    if (link != NULL)
        drop_link (self, link);
    /* room for one more: the least recently used go first */
    while (self->order.length >= self->max_entries)
        drop_link (self, self->order.tail);

    entry = g_new0 (Entry, 1);
    entry->digest = g_strdup (digest);
    entry->bytes = g_bytes_ref (bytes);
    g_queue_push_head (&self->order, entry);
    g_hash_table_insert (self->index, entry->digest, self->order.head);
    self->total += g_bytes_get_size (bytes);
}

void
lrg_nav_poly_mesh_cache_clear (LrgNavPolyMeshCache *self)
{
    g_return_if_fail (LRG_IS_NAV_POLY_MESH_CACHE (self));

    while (self->order.head != NULL)
        drop_link (self, self->order.head);
}

guint
lrg_nav_poly_mesh_cache_get_max_entries (LrgNavPolyMeshCache *self)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH_CACHE (self), 0);
    return self->max_entries;
}

guint
lrg_nav_poly_mesh_cache_get_size (LrgNavPolyMeshCache *self)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH_CACHE (self), 0);
    return self->order.length;
}

gsize
lrg_nav_poly_mesh_cache_get_bytes (LrgNavPolyMeshCache *self)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH_CACHE (self), 0);
    return self->total;
}

guint
lrg_nav_poly_mesh_cache_get_hits (LrgNavPolyMeshCache *self)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH_CACHE (self), 0);
    return self->hits;
}

guint
lrg_nav_poly_mesh_cache_get_misses (LrgNavPolyMeshCache *self)
{
    g_return_val_if_fail (LRG_IS_NAV_POLY_MESH_CACHE (self), 0);
    return self->misses;
}
