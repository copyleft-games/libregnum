/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-replicator.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

typedef struct
{
    gchar *zone;
    gint x;
    gint y;
    gint z;
} Cell;

typedef struct
{
    guint64 id;
    guint64 revision;
    Cell cell;
    gdouble x;
    gdouble y;
    gdouble z;
    GBytes *state;
} Entity;

typedef struct
{
    GHashTable *baseline;
    GHashTable *pending_baseline;
    GVariant *pending;
    guint64 sequence;
    gboolean more;
    guint64 cursor;
    guint64 pending_cursor;
} Viewer;

struct _LrgMmoReplicator
{
    GObject parent_instance;
    GHashTable *entities;
    GHashTable *cells;
    GHashTable *viewers;
    GHashTable *focus;          /* viewer ID -> entity ID sent first in pages */
    guint max_entities;
    guint max_viewers;
    gdouble cell_size;
    guint64 revision;
    guint64 sequence;
};

G_DEFINE_TYPE (LrgMmoReplicator, lrg_mmo_replicator, G_TYPE_OBJECT)

static guint
cell_hash (gconstpointer data)
{
    const Cell *cell = data;
    return g_str_hash (cell->zone) ^ ((guint) cell->x * 73856093u) ^
           ((guint) cell->y * 19349663u) ^ ((guint) cell->z * 83492791u);
}

static gboolean
cell_equal (gconstpointer a,
            gconstpointer b)
{
    const Cell *x = a;
    const Cell *y = b;
    return x->x == y->x && x->y == y->y && x->z == y->z && g_str_equal (x->zone, y->zone);
}

static void
cell_free (gpointer data)
{
    Cell *cell = data;
    g_free (cell->zone);
    g_free (cell);
}

static void
entity_free (gpointer data)
{
    Entity *entity = data;
    g_free (entity->cell.zone);
    g_bytes_unref (entity->state);
    g_free (entity);
}

static void
viewer_free (gpointer data)
{
    Viewer *viewer = data;
    g_clear_pointer (&viewer->baseline, g_hash_table_unref);
    g_clear_pointer (&viewer->pending_baseline, g_hash_table_unref);
    g_clear_pointer (&viewer->pending, g_variant_unref);
    g_free (viewer);
}

static GHashTable *
new_baseline (void)
{
    return g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, g_free);
}

static gboolean
valid_position (LrgMmoReplicator *self,
                const gchar      *zone,
                gdouble           x,
                gdouble           y,
                gdouble           z)
{
    return zone != NULL && *zone != '\0' && strlen (zone) <= 128 &&
           g_utf8_validate (zone, -1, NULL) && isfinite (x) && isfinite (y) && isfinite (z) &&
           fabs (x / self->cell_size) <= 1e9 && fabs (y / self->cell_size) <= 1e9 &&
           fabs (z / self->cell_size) <= 1e9;
}

static void
unindex_entity (LrgMmoReplicator *self,
                Entity           *entity)
{
    GPtrArray *bucket = g_hash_table_lookup (self->cells, &entity->cell);
    g_ptr_array_remove_fast (bucket, entity);
    if (bucket->len == 0)
        g_hash_table_remove (self->cells, &entity->cell);
}

static gint
compare_entities (gconstpointer a,
                  gconstpointer b)
{
    const Entity *x = *(Entity *const *) a;
    const Entity *y = *(Entity *const *) b;
    return (x->id > y->id) - (x->id < y->id);
}

static gint
compare_ids (gconstpointer a,
             gconstpointer b)
{
    guint64 x = *(const guint64 *) a;
    guint64 y = *(const guint64 *) b;
    return (x > y) - (x < y);
}

static void
lrg_mmo_replicator_finalize (GObject *object)
{
    LrgMmoReplicator *self = LRG_MMO_REPLICATOR (object);
    g_hash_table_unref (self->cells);
    g_hash_table_unref (self->entities);
    g_hash_table_unref (self->viewers);
    g_hash_table_unref (self->focus);
    G_OBJECT_CLASS (lrg_mmo_replicator_parent_class)->finalize (object);
}

static void
lrg_mmo_replicator_class_init (LrgMmoReplicatorClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_mmo_replicator_finalize;
}

static void
lrg_mmo_replicator_init (LrgMmoReplicator *self)
{
    self->entities = g_hash_table_new_full (g_int64_hash, g_int64_equal, NULL, entity_free);
    self->cells = g_hash_table_new_full (cell_hash, cell_equal, cell_free, (GDestroyNotify) g_ptr_array_unref);
    self->viewers = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, viewer_free);
    self->focus = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, g_free);
}

LrgMmoReplicator *
lrg_mmo_replicator_new (guint   max_entities,
                        guint   max_viewers,
                        gdouble cell_size)
{
    LrgMmoReplicator *self;
    g_return_val_if_fail (max_entities > 0 && max_viewers > 0, NULL);
    g_return_val_if_fail (isfinite (cell_size) && cell_size > 0 && cell_size <= 1e100, NULL);
    self = g_object_new (LRG_TYPE_MMO_REPLICATOR, NULL);
    self->max_entities = max_entities;
    self->max_viewers = max_viewers;
    self->cell_size = cell_size;
    return self;
}

gboolean
lrg_mmo_replicator_upsert (LrgMmoReplicator  *self,
                          guint64            entity_id,
                          const gchar       *zone,
                          gdouble            x,
                          gdouble            y,
                          gdouble            z,
                          GBytes            *state,
                          GError           **error)
{
    Entity *entity;
    Entity *old;
    GPtrArray *bucket;
    g_return_val_if_fail (LRG_IS_MMO_REPLICATOR (self), FALSE);
    if (entity_id == 0 || !valid_position (self, zone, x, y, z) ||
        state == NULL || g_bytes_get_size (state) > 65536 || self->revision == G_MAXUINT64)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid replicated entity");
        return FALSE;
    }
    old = g_hash_table_lookup (self->entities, &entity_id);
    if (old == NULL && g_hash_table_size (self->entities) >= self->max_entities)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Entity capacity reached");
        return FALSE;
    }
    entity = g_new0 (Entity, 1);
    entity->id = entity_id;
    entity->revision = ++self->revision;
    entity->cell.zone = g_strdup (zone);
    entity->cell.x = floor (x / self->cell_size);
    entity->cell.y = floor (y / self->cell_size);
    entity->cell.z = floor (z / self->cell_size);
    entity->x = x;
    entity->y = y;
    entity->z = z;
    entity->state = g_bytes_ref (state);
    if (old != NULL)
        unindex_entity (self, old);
    g_hash_table_replace (self->entities, &entity->id, entity);
    bucket = g_hash_table_lookup (self->cells, &entity->cell);
    if (bucket == NULL)
    {
        Cell *cell = g_new (Cell, 1);
        *cell = entity->cell;
        cell->zone = g_strdup (entity->cell.zone);
        bucket = g_ptr_array_new ();
        g_hash_table_insert (self->cells, cell, bucket);
    }
    g_ptr_array_add (bucket, entity);
    return TRUE;
}

void
lrg_mmo_replicator_remove (LrgMmoReplicator *self,
                          guint64           entity_id)
{
    Entity *entity;
    g_return_if_fail (LRG_IS_MMO_REPLICATOR (self));
    entity = g_hash_table_lookup (self->entities, &entity_id);
    if (entity == NULL)
        return;
    unindex_entity (self, entity);
    g_hash_table_remove (self->entities, &entity_id);
}

static GVariant *
build_internal (LrgMmoReplicator  *self,
                         guint64            viewer_id,
                         const gchar       *zone,
                         gdouble            x,
                         gdouble            y,
                         gdouble            z,
                         gdouble            radius,
                         guint              limit,
                         gboolean           paginate,
                         gboolean          *more,
                         GError           **error)
{
    Viewer *viewer;
    g_autoptr(GHashTable) baseline = NULL;
    g_autoptr(GPtrArray) visible = NULL;
    g_autoptr(GArray) removed = NULL;
    GHashTableIter iter;
    gpointer key;
    GVariantBuilder updates;
    GVariantBuilder removals;
    GVariant *delta;
    Cell cell;
    gint min_x, max_x, min_y, max_y, min_z, max_z;
    guint i;
    gsize budget = 32;
    g_autoptr(GHashTable) page_baseline = NULL;
    guint n_updates, n_removals;
    gboolean remaining = FALSE;
    guint64 next_cursor = 0;
    guint64 focus_id;

    g_return_val_if_fail (LRG_IS_MMO_REPLICATOR (self), NULL);
    {
        guint64 *focused = g_hash_table_lookup (self->focus, &viewer_id);
        focus_id = focused != NULL ? *focused : 0;
    }
    if (more != NULL)
        *more = FALSE;
    if (limit < 128 || limit > 1024 * 1024 || viewer_id == 0 || !valid_position (self, zone, x, y, z) ||
        !isfinite (radius) || radius < 0 || radius / self->cell_size > 4 || self->sequence == G_MAXUINT64)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid viewer position or radius");
        return NULL;
    }
    viewer = g_hash_table_lookup (self->viewers, &viewer_id);
    if (viewer != NULL && viewer->pending != NULL)
    {
        if (more != NULL)
            *more = viewer->more;
        return g_variant_ref (viewer->pending);
    }
    if (viewer == NULL && g_hash_table_size (self->viewers) >= self->max_viewers)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Viewer capacity reached");
        return NULL;
    }
    baseline = new_baseline ();
    visible = g_ptr_array_new ();
    removed = g_array_new (FALSE, FALSE, sizeof (guint64));
    min_x = floor (x / self->cell_size - radius / self->cell_size);
    max_x = floor (x / self->cell_size + radius / self->cell_size);
    min_y = floor (y / self->cell_size - radius / self->cell_size);
    max_y = floor (y / self->cell_size + radius / self->cell_size);
    min_z = floor (z / self->cell_size - radius / self->cell_size);
    max_z = floor (z / self->cell_size + radius / self->cell_size);
    cell.zone = (gchar *) zone;
    for (cell.x = min_x; cell.x <= max_x; cell.x++)
        for (cell.y = min_y; cell.y <= max_y; cell.y++)
            for (cell.z = min_z; cell.z <= max_z; cell.z++)
            {
                GPtrArray *bucket = g_hash_table_lookup (self->cells, &cell);
                if (bucket == NULL)
                    continue;
                for (i = 0; i < bucket->len; i++)
                {
                    Entity *entity = g_ptr_array_index (bucket, i);
                    guint64 *id;
                    guint64 *revision;
                    guint64 *previous;
                    if (hypot (hypot (entity->x - x, entity->y - y), entity->z - z) > radius)
                        continue;
                    id = g_new (guint64, 1);
                    revision = g_new (guint64, 1);
                    *id = entity->id;
                    *revision = entity->revision;
                    g_hash_table_insert (baseline, id, revision);
                    previous = viewer != NULL ? g_hash_table_lookup (viewer->baseline, id) : NULL;
                    if (previous == NULL || *previous != *revision)
                    {
                        g_ptr_array_add (visible, entity);
                        budget = MIN ((gsize) limit + 1, budget + 64 + g_bytes_get_size (entity->state));
                        if (!paginate && budget > limit)
                            goto too_large;
                    }
                }
            }
    if (viewer != NULL)
    {
        g_hash_table_iter_init (&iter, viewer->baseline);
        while (g_hash_table_iter_next (&iter, &key, NULL))
            if (!g_hash_table_contains (baseline, key))
            {
                guint64 id = *(guint64 *) key;
                g_array_append_val (removed, id);
                budget = MIN ((gsize) limit + 1, budget + 8);
                if (!paginate && budget > limit)
                    goto too_large;
            }
    }
    g_ptr_array_sort (visible, compare_entities);
    g_array_sort (removed, compare_ids);
    if (paginate && viewer != NULL && visible->len > 1)
    {
        guint start = 0;
        gpointer *ordered;
        while (start < visible->len && ((Entity *) g_ptr_array_index (visible, start))->id <= viewer->cursor)
            start++;
        ordered = g_memdup2 (visible->pdata, visible->len * sizeof (gpointer));
        for (i = 0; i < visible->len; i++)
            visible->pdata[i] = ordered[(start + i) % visible->len];
        g_free (ordered);
    }
    /* The viewer's focus entity (usually its own avatar) leads every page it
     * changed in, ahead of the round-robin, so it never waits for others */
    if (paginate && focus_id != 0)
    {
        for (i = 0; i < visible->len; i++)
            if (((Entity *) g_ptr_array_index (visible, i))->id == focus_id)
            {
                gpointer focused = g_ptr_array_index (visible, i);
                memmove (visible->pdata + 1, visible->pdata, i * sizeof (gpointer));
                visible->pdata[0] = focused;
                break;
            }
    }
    n_updates = visible->len;
    n_removals = removed->len;
    if (paginate)
    {
        gsize used = 32;
        page_baseline = new_baseline ();
        if (viewer != NULL)
        {
            gpointer value;
            g_hash_table_iter_init (&iter, viewer->baseline);
            while (g_hash_table_iter_next (&iter, &key, &value))
            {
                guint64 *id = g_new (guint64, 1);
                guint64 *revision = g_new (guint64, 1);
                *id = *(guint64 *) key;
                *revision = *(guint64 *) value;
                g_hash_table_insert (page_baseline, id, revision);
            }
        }
        n_removals = MIN (removed->len, (limit - used) / 8);
        used += n_removals * 8;
        for (i = 0; i < n_removals; i++)
            g_hash_table_remove (page_baseline, &g_array_index (removed, guint64, i));
        n_updates = 0;
        for (i = 0; i < visible->len; i++)
        {
            Entity *entity = g_ptr_array_index (visible, i);
            gsize cost = 64 + g_bytes_get_size (entity->state);
            guint64 *id;
            guint64 *revision;
            if (cost > limit - used)
                break;
            used += cost;
            id = g_new (guint64, 1);
            revision = g_new (guint64, 1);
            *id = entity->id;
            *revision = entity->revision;
            g_hash_table_replace (page_baseline, id, revision);
            n_updates++;
        }
        remaining = n_updates < visible->len || n_removals < removed->len;
        if (remaining && n_updates == 0 && n_removals == 0)
            goto too_large;
        g_hash_table_unref (baseline);
        baseline = g_steal_pointer (&page_baseline);
    }
    /* The round-robin resumes after the last entity it reached; the focus
     * entity sits outside it */
    next_cursor = viewer != NULL ? viewer->cursor : 0;
    for (i = n_updates; i > 0; i--)
        if (((Entity *) g_ptr_array_index (visible, i - 1))->id != focus_id || focus_id == 0)
        {
            next_cursor = ((Entity *) g_ptr_array_index (visible, i - 1))->id;
            break;
        }
    if (n_updates > 1)
        qsort (visible->pdata, n_updates, sizeof (gpointer), compare_entities);
    g_variant_builder_init (&updates, G_VARIANT_TYPE ("a(ttddday)"));
    g_variant_builder_init (&removals, G_VARIANT_TYPE ("at"));
    for (i = 0; i < n_updates; i++)
    {
        Entity *entity = g_ptr_array_index (visible, i);
        gsize size;
        gconstpointer data = g_bytes_get_data (entity->state, &size);
        g_variant_builder_add (&updates, "(ttddd@ay)", entity->id, entity->revision,
                               entity->x, entity->y, entity->z,
                               g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, data, size, 1));
    }
    for (i = 0; i < n_removals; i++)
        g_variant_builder_add (&removals, "t", g_array_index (removed, guint64, i));
    delta = g_variant_ref_sink (g_variant_new ("(t@a(ttddday)@at)", ++self->sequence,
                                               g_variant_builder_end (&updates),
                                               g_variant_builder_end (&removals)));
    if (viewer == NULL)
    {
        guint64 *id = g_new (guint64, 1);
        *id = viewer_id;
        viewer = g_new0 (Viewer, 1);
        viewer->baseline = new_baseline ();
        g_hash_table_insert (self->viewers, id, viewer);
    }
    viewer->pending = delta;
    viewer->more = remaining;
    viewer->pending_cursor = next_cursor;
    if (more != NULL)
        *more = remaining;
    viewer->sequence = self->sequence;
    viewer->pending_baseline = g_steal_pointer (&baseline);
    return g_variant_ref (delta);

too_large:
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_MESSAGE_TOO_LARGE,
                         "Interest delta exceeds 1 MiB; reduce radius or state size");
    return NULL;
}

gboolean
lrg_mmo_replicator_acknowledge (LrgMmoReplicator  *self,
                               guint64            viewer_id,
                               guint64            sequence,
                               GError           **error)
{
    Viewer *viewer;
    g_return_val_if_fail (LRG_IS_MMO_REPLICATOR (self), FALSE);
    viewer = g_hash_table_lookup (self->viewers, &viewer_id);
    if (viewer == NULL || viewer->pending == NULL || viewer->sequence != sequence)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "No matching pending snapshot");
        return FALSE;
    }
    g_hash_table_unref (viewer->baseline);
    viewer->baseline = g_steal_pointer (&viewer->pending_baseline);
    viewer->cursor = viewer->pending_cursor;
    g_clear_pointer (&viewer->pending, g_variant_unref);
    return TRUE;
}

void
lrg_mmo_replicator_forget (LrgMmoReplicator *self,
                          guint64           viewer_id)
{
    g_return_if_fail (LRG_IS_MMO_REPLICATOR (self));
    g_hash_table_remove (self->viewers, &viewer_id);
    g_hash_table_remove (self->focus, &viewer_id);
}

void
lrg_mmo_replicator_set_focus (LrgMmoReplicator *self,
                              guint64           viewer_id,
                              guint64           entity_id)
{
    guint64 *key;
    guint64 *value;

    g_return_if_fail (LRG_IS_MMO_REPLICATOR (self));
    g_return_if_fail (viewer_id != 0);
    if (entity_id == 0)
    {
        g_hash_table_remove (self->focus, &viewer_id);
        return;
    }
    key = g_new (guint64, 1);
    value = g_new (guint64, 1);
    *key = viewer_id;
    *value = entity_id;
    g_hash_table_replace (self->focus, key, value);
}

GVariant *
lrg_mmo_replicator_build (LrgMmoReplicator *self, guint64 viewer_id, const gchar *zone,
                          gdouble x, gdouble y, gdouble z, gdouble radius, GError **error)
{
    return build_internal (self, viewer_id, zone, x, y, z, radius, 1024 * 1024, FALSE, NULL, error);
}

GVariant *
lrg_mmo_replicator_build_page (LrgMmoReplicator *self, guint64 viewer_id, const gchar *zone,
                               gdouble x, gdouble y, gdouble z, gdouble radius,
                               guint budget, gboolean *more, GError **error)
{
    return build_internal (self, viewer_id, zone, x, y, z, radius, budget, TRUE, more, error);
}
