/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-replicator.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

/*
 * How the replicator keeps its per-viewer work cheap
 * (docs/modules/mmo/index.org, "Cost of a page"):
 *
 * - A viewer's acknowledged baseline is a flat open-addressing table of
 *   (entity ID, revision) pairs, not a GHashTable of boxed integers, so
 *   building a page allocates nothing per visible entity.
 * - A page does not copy the baseline.  It remembers only its own changes
 *   (the updates it carries and the removals it announces); acknowledging
 *   it applies those changes to the baseline, so an acknowledgement costs
 *   the size of the page, not the size of the visible set.
 * - Removals are found by stamping every baseline entry the interest query
 *   meets; the baseline is scanned for unstamped entries only when fewer
 *   entries were met than it holds.
 * - Each entity's wire record (ttddday) is built and serialized once per
 *   revision and shared, by reference, by every page that carries it.
 * - A page carries only what fits its budget, so the changes are not
 *   sorted: they sit in a binary heap ordered by their place in the
 *   round-robin, and only the entities the page takes are popped.
 * - The scratch arrays of the interest query belong to the replicator and
 *   are reused by every build.
 */

/* Initial capacity of a viewer's baseline table (a power of two). */
#define ID_MAP_MIN_CAPACITY (64u)

/* The distance test falls back to hypot() only this close to the radius. */
#define DISTANCE_SLACK (1e-9)

typedef struct
{
    gchar *zone;
    guint zone_hash;
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
    GVariant *encoded;  /* the shared (ttddday) record of this revision, or NULL */
} Entity;

/*
 * IdMap: linear-probing table of entity ID -> revision.  ID 0 marks an
 * empty slot (entity IDs are nonzero); deletion shifts later entries back,
 * so there are no tombstones.  @stamps holds the build stamp that last met
 * each entry and moves with it.
 */
typedef struct
{
    guint64 *ids;
    guint64 *revisions;
    guint64 *stamps;
    guint capacity;
    guint size;
    guint shift;
} IdMap;

/* A changed entity and its place in the round-robin: its ID minus the
 * cursor minus one, wrapping, so IDs after the cursor come first. */
typedef struct
{
    guint64 rank;
    Entity *entity;
} Ranked;

/* One update a pending page carries: the revision it gives the baseline. */
typedef struct
{
    guint64 id;
    guint64 revision;
} Change;

typedef struct
{
    IdMap baseline;
    GArray *pending_updates;    /* Change: what the pending page adds */
    GArray *pending_removals;   /* guint64: what the pending page removes */
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

    /* Grown-only bounds of every cell an entity was indexed in; interest
     * queries never visit cells outside them. */
    gboolean bounded;
    gint min_cell[3];
    gint max_cell[3];

    /* Reused by every build (the replicator is owned by one thread). */
    GPtrArray *scratch_visible;
    GArray *scratch_ranked;
    GArray *scratch_removed;
    guint64 stamp;

    /* lrg_mmo_replicator_get_counters() */
    guint64 pages_built;
    guint64 updates_sent;
    guint64 encodings_built;
};

G_DEFINE_TYPE (LrgMmoReplicator, lrg_mmo_replicator, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------ */
/* IdMap                                                                    */
/* ------------------------------------------------------------------------ */

static guint
id_map_home (const IdMap *map,
             guint64      id)
{
    /* Fibonacci hashing: the top bits of the product spread sequential IDs */
    return (guint) ((id * G_GUINT64_CONSTANT (0x9E3779B97F4A7C15)) >> map->shift);
}

static void
id_map_clear (IdMap *map)
{
    g_clear_pointer (&map->ids, g_free);
    g_clear_pointer (&map->revisions, g_free);
    g_clear_pointer (&map->stamps, g_free);
    map->capacity = 0;
    map->size = 0;
    map->shift = 0;
}

/* The slot holding @id, or G_MAXUINT. */
static guint
id_map_find (const IdMap *map,
             guint64      id)
{
    guint mask;
    guint i;

    if (map->size == 0)
        return G_MAXUINT;
    mask = map->capacity - 1;
    for (i = id_map_home (map, id); map->ids[i] != 0; i = (i + 1) & mask)
        if (map->ids[i] == id)
            return i;
    return G_MAXUINT;
}

static void id_map_set (IdMap *map, guint64 id, guint64 revision);

static void
id_map_grow (IdMap *map)
{
    IdMap bigger = { NULL, NULL, NULL, 0, 0, 0 };
    guint bits = 0;
    guint i;

    bigger.capacity = map->capacity != 0 ? map->capacity * 2 : ID_MAP_MIN_CAPACITY;
    while ((1u << bits) < bigger.capacity)
        bits++;
    bigger.shift = 64 - bits;
    bigger.ids = g_new0 (guint64, bigger.capacity);
    bigger.revisions = g_new (guint64, bigger.capacity);
    bigger.stamps = g_new0 (guint64, bigger.capacity);
    for (i = 0; i < map->capacity; i++)
        if (map->ids[i] != 0)
            id_map_set (&bigger, map->ids[i], map->revisions[i]);
    id_map_clear (map);
    *map = bigger;
}

/* Inserts @id or replaces its revision. */
static void
id_map_set (IdMap   *map,
            guint64  id,
            guint64  revision)
{
    guint mask;
    guint i;

    /* Keep the load at most 5/8 so probe chains stay short */
    if ((map->size + 1) * 8 > map->capacity * 5)
        id_map_grow (map);
    mask = map->capacity - 1;
    for (i = id_map_home (map, id); map->ids[i] != 0; i = (i + 1) & mask)
        if (map->ids[i] == id)
        {
            map->revisions[i] = revision;
            return;
        }
    map->ids[i] = id;
    map->revisions[i] = revision;
    map->stamps[i] = 0;
    map->size++;
}

/* Removes @id if present, shifting its probe chain back. */
static void
id_map_remove (IdMap   *map,
               guint64  id)
{
    guint mask;
    guint i;
    guint j;

    i = id_map_find (map, id);
    if (i == G_MAXUINT)
        return;
    mask = map->capacity - 1;
    j = i;
    for (;;)
    {
        guint home;

        j = (j + 1) & mask;
        if (map->ids[j] == 0)
            break;
        home = id_map_home (map, map->ids[j]);
        /* An entry whose home lies cyclically in (i, j] cannot move to i */
        if (i <= j ? (i < home && home <= j) : (i < home || home <= j))
            continue;
        map->ids[i] = map->ids[j];
        map->revisions[i] = map->revisions[j];
        map->stamps[i] = map->stamps[j];
        i = j;
    }
    map->ids[i] = 0;
    map->size--;
}

/* ------------------------------------------------------------------------ */
/* Cells, entities and viewers                                              */
/* ------------------------------------------------------------------------ */

static guint
cell_hash (gconstpointer data)
{
    const Cell *cell = data;
    return cell->zone_hash ^ ((guint) cell->x * 73856093u) ^
           ((guint) cell->y * 19349663u) ^ ((guint) cell->z * 83492791u);
}

static gboolean
cell_equal (gconstpointer a,
            gconstpointer b)
{
    const Cell *x = a;
    const Cell *y = b;
    return x->x == y->x && x->y == y->y && x->z == y->z && x->zone_hash == y->zone_hash &&
           g_str_equal (x->zone, y->zone);
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
    g_clear_pointer (&entity->encoded, g_variant_unref);
    g_free (entity);
}

static void
viewer_free (gpointer data)
{
    Viewer *viewer = data;
    id_map_clear (&viewer->baseline);
    g_clear_pointer (&viewer->pending_updates, g_array_unref);
    g_clear_pointer (&viewer->pending_removals, g_array_unref);
    g_clear_pointer (&viewer->pending, g_variant_unref);
    g_free (viewer);
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
index_entity (LrgMmoReplicator *self,
              Entity           *entity)
{
    GPtrArray *bucket = g_hash_table_lookup (self->cells, &entity->cell);
    gint coords[3];
    guint axis;

    if (bucket == NULL)
    {
        Cell *cell = g_new (Cell, 1);
        *cell = entity->cell;
        cell->zone = g_strdup (entity->cell.zone);
        bucket = g_ptr_array_new ();
        g_hash_table_insert (self->cells, cell, bucket);
    }
    g_ptr_array_add (bucket, entity);
    /* Grow the bounds the interest queries are clamped to */
    coords[0] = entity->cell.x;
    coords[1] = entity->cell.y;
    coords[2] = entity->cell.z;
    for (axis = 0; axis < 3; axis++)
    {
        if (!self->bounded || coords[axis] < self->min_cell[axis])
            self->min_cell[axis] = coords[axis];
        if (!self->bounded || coords[axis] > self->max_cell[axis])
            self->max_cell[axis] = coords[axis];
    }
    self->bounded = TRUE;
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

/*
 * Whether @entity lies within @radius of (x, y, z): hypot(hypot(dx, dy), dz)
 * <= radius, as before, but decided by squared distances unless the entity
 * sits on the boundary within rounding.
 */
static gboolean
within_radius (const Entity *entity,
               gdouble       x,
               gdouble       y,
               gdouble       z,
               gdouble       radius)
{
    gdouble dx = entity->x - x;
    gdouble dy = entity->y - y;
    gdouble dz = entity->z - z;
    gdouble d2 = dx * dx + dy * dy + dz * dz;
    gdouble r2 = radius * radius;

    if (d2 > r2 * (1 + DISTANCE_SLACK))
        return FALSE;
    if (d2 < r2 * (1 - DISTANCE_SLACK))
        return TRUE;
    return hypot (hypot (dx, dy), dz) <= radius;
}

static gint
compare_ranked (gconstpointer a,
                gconstpointer b)
{
    guint64 x = ((const Ranked *) a)->rank;
    guint64 y = ((const Ranked *) b)->rank;
    return (x > y) - (x < y);
}

/* Restores the min-heap property below @i. */
static void
heap_sift_down (Ranked *heap,
                guint   len,
                guint   i)
{
    Ranked item = heap[i];

    for (;;)
    {
        guint child = 2 * i + 1;
        if (child >= len)
            break;
        if (child + 1 < len && heap[child + 1].rank < heap[child].rank)
            child++;
        if (heap[child].rank >= item.rank)
            break;
        heap[i] = heap[child];
        i = child;
    }
    heap[i] = item;
}

static void
heap_build (Ranked *heap,
            guint   len)
{
    guint i;

    for (i = len / 2; i > 0; i--)
        heap_sift_down (heap, len, i - 1);
}

/* The shared wire record of @entity's current revision. */
static GVariant *
entity_encoding (LrgMmoReplicator *self,
                 Entity           *entity)
{
    if (entity->encoded == NULL)
    {
        entity->encoded = g_variant_ref_sink (
            g_variant_new ("(ttddd@ay)", entity->id, entity->revision,
                           entity->x, entity->y, entity->z,
                           g_variant_new_from_bytes (G_VARIANT_TYPE_BYTESTRING, entity->state, TRUE)));
        /* Serialize now, so every page copies the bytes instead of
         * serializing the record again */
        (void) g_variant_get_data (entity->encoded);
        self->encodings_built++;
    }
    return entity->encoded;
}

static void
lrg_mmo_replicator_finalize (GObject *object)
{
    LrgMmoReplicator *self = LRG_MMO_REPLICATOR (object);
    g_hash_table_unref (self->cells);
    g_hash_table_unref (self->entities);
    g_hash_table_unref (self->viewers);
    g_hash_table_unref (self->focus);
    g_ptr_array_unref (self->scratch_visible);
    g_array_unref (self->scratch_ranked);
    g_array_unref (self->scratch_removed);
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
    self->scratch_visible = g_ptr_array_new ();
    self->scratch_ranked = g_array_new (FALSE, FALSE, sizeof (Ranked));
    self->scratch_removed = g_array_new (FALSE, FALSE, sizeof (guint64));
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
    Cell cell;
    gboolean moved;

    g_return_val_if_fail (LRG_IS_MMO_REPLICATOR (self), FALSE);
    if (entity_id == 0 || !valid_position (self, zone, x, y, z) ||
        state == NULL || g_bytes_get_size (state) > 65536 || self->revision == G_MAXUINT64)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid replicated entity");
        return FALSE;
    }
    entity = g_hash_table_lookup (self->entities, &entity_id);
    if (entity == NULL && g_hash_table_size (self->entities) >= self->max_entities)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Entity capacity reached");
        return FALSE;
    }
    cell.zone = (gchar *) zone;
    cell.zone_hash = g_str_hash (zone);
    cell.x = floor (x / self->cell_size);
    cell.y = floor (y / self->cell_size);
    cell.z = floor (z / self->cell_size);

    /* A known entity is updated in place: its record, and its cell only
     * when it left the one it was in */
    if (entity == NULL)
    {
        entity = g_new0 (Entity, 1);
        entity->id = entity_id;
        g_hash_table_insert (self->entities, &entity->id, entity);
        moved = TRUE;
    }
    else
    {
        moved = !cell_equal (&cell, &entity->cell);
        if (moved)
            unindex_entity (self, entity);
        g_clear_pointer (&entity->encoded, g_variant_unref);
        g_bytes_unref (entity->state);
    }
    entity->revision = ++self->revision;
    entity->x = x;
    entity->y = y;
    entity->z = z;
    entity->state = g_bytes_ref (state);
    if (moved)
    {
        if (entity->cell.zone == NULL || !g_str_equal (entity->cell.zone, zone))
        {
            g_free (entity->cell.zone);
            entity->cell.zone = g_strdup (zone);
        }
        entity->cell.zone_hash = cell.zone_hash;
        entity->cell.x = cell.x;
        entity->cell.y = cell.y;
        entity->cell.z = cell.z;
        index_entity (self, entity);
    }
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
    GPtrArray *visible;
    GArray *ranked;
    GArray *removed;
    Entity *focused = NULL;
    guint64 cursor;
    guint changes;
    GVariantBuilder updates;
    GVariantBuilder removals;
    GVariant *delta;
    Cell cell;
    gint min_x, max_x, min_y, max_y, min_z, max_z;
    guint i;
    gsize budget = 32;
    guint n_updates, n_removals;
    gboolean remaining = FALSE;
    guint64 next_cursor = 0;
    guint64 focus_id;
    guint64 stamp;
    guint met = 0;

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
    visible = self->scratch_visible;
    ranked = self->scratch_ranked;
    removed = self->scratch_removed;
    g_ptr_array_set_size (visible, 0);
    g_array_set_size (ranked, 0);
    g_array_set_size (removed, 0);
    cursor = paginate && viewer != NULL ? viewer->cursor : 0;
    stamp = ++self->stamp;

    /* 1. The interest query: every entity of the zone within the radius,
     * visiting only occupied cell ranges.  Entities the viewer already has
     * at their current revision are only stamped; the others are changes */
    min_x = MAX ((gint) floor (x / self->cell_size - radius / self->cell_size), self->min_cell[0]);
    max_x = MIN ((gint) floor (x / self->cell_size + radius / self->cell_size), self->max_cell[0]);
    min_y = MAX ((gint) floor (y / self->cell_size - radius / self->cell_size), self->min_cell[1]);
    max_y = MIN ((gint) floor (y / self->cell_size + radius / self->cell_size), self->max_cell[1]);
    min_z = MAX ((gint) floor (z / self->cell_size - radius / self->cell_size), self->min_cell[2]);
    max_z = MIN ((gint) floor (z / self->cell_size + radius / self->cell_size), self->max_cell[2]);
    cell.zone = (gchar *) zone;
    cell.zone_hash = g_str_hash (zone);
    if (self->bounded && g_hash_table_size (self->cells) > 0)
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
                        guint slot;
                        if (!within_radius (entity, x, y, z, radius))
                            continue;
                        slot = viewer != NULL ? id_map_find (&viewer->baseline, entity->id) : G_MAXUINT;
                        if (slot != G_MAXUINT)
                        {
                            viewer->baseline.stamps[slot] = stamp;
                            met++;
                            if (viewer->baseline.revisions[slot] == entity->revision)
                                continue;
                        }
                        /* The focus entity leads a page; the others
                         * wait for their turn in the round-robin */
                        if (paginate && entity->id == focus_id)
                            focused = entity;
                        else
                        {
                            Ranked item;
                            item.rank = entity->id - cursor - 1;
                            item.entity = entity;
                            g_array_append_val (ranked, item);
                        }
                        budget = MIN ((gsize) limit + 1, budget + 64 + g_bytes_get_size (entity->state));
                        if (!paginate && budget > limit)
                            goto too_large;
                    }
                }

    /* 2. Removals: baseline entries the query did not meet.  When it met
     * every entry there are none, and the baseline is not scanned */
    if (viewer != NULL && met < viewer->baseline.size)
    {
        IdMap *map = &viewer->baseline;
        for (i = 0; i < map->capacity; i++)
            if (map->ids[i] != 0 && map->stamps[i] != stamp)
            {
                g_array_append_val (removed, map->ids[i]);
                budget = MIN ((gsize) limit + 1, budget + 8);
                if (!paginate && budget > limit)
                    goto too_large;
            }
    }
    g_array_sort (removed, compare_ids);
    changes = ranked->len + (focused != NULL ? 1 : 0);

    /* 3. What goes on the page, in page order.  A whole delta takes every
     * change by ID.  A page takes removals first, then the focus entity
     * (usually the viewer's own avatar) whenever it changed, then the
     * round-robin after the cursor, until one entity does not fit */
    n_removals = removed->len;
    if (!paginate)
    {
        g_array_sort (ranked, compare_ranked);
        for (i = 0; i < ranked->len; i++)
            g_ptr_array_add (visible, g_array_index (ranked, Ranked, i).entity);
    }
    else
    {
        gsize used = 32;
        Ranked *heap = (Ranked *) (gpointer) ranked->data;
        guint heap_len = ranked->len;
        gboolean full = FALSE;

        n_removals = MIN (removed->len, (limit - used) / 8);
        used += n_removals * 8;
        if (focused != NULL)
        {
            gsize cost = 64 + g_bytes_get_size (focused->state);
            if (cost > limit - used)
                full = TRUE;
            else
            {
                used += cost;
                g_ptr_array_add (visible, focused);
            }
        }
        heap_build (heap, heap_len);
        while (!full && heap_len > 0)
        {
            Entity *entity = heap[0].entity;
            gsize cost = 64 + g_bytes_get_size (entity->state);
            if (cost > limit - used)
                break;
            used += cost;
            g_ptr_array_add (visible, entity);
            heap[0] = heap[--heap_len];
            heap_sift_down (heap, heap_len, 0);
        }
        remaining = visible->len < changes || n_removals < removed->len;
        if (remaining && visible->len == 0 && n_removals == 0)
            goto too_large;
    }
    n_updates = visible->len;
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

    /* 5. The page, from the entities' shared records */
    if (viewer == NULL)
    {
        guint64 *id = g_new (guint64, 1);
        *id = viewer_id;
        viewer = g_new0 (Viewer, 1);
        viewer->pending_updates = g_array_new (FALSE, FALSE, sizeof (Change));
        viewer->pending_removals = g_array_new (FALSE, FALSE, sizeof (guint64));
        g_hash_table_insert (self->viewers, id, viewer);
    }
    g_array_set_size (viewer->pending_updates, 0);
    g_array_set_size (viewer->pending_removals, 0);
    g_variant_builder_init (&updates, G_VARIANT_TYPE ("a(ttddday)"));
    g_variant_builder_init (&removals, G_VARIANT_TYPE ("at"));
    for (i = 0; i < n_updates; i++)
    {
        Entity *entity = g_ptr_array_index (visible, i);
        Change change;
        change.id = entity->id;
        change.revision = entity->revision;
        g_array_append_val (viewer->pending_updates, change);
        g_variant_builder_add_value (&updates, entity_encoding (self, entity));
    }
    for (i = 0; i < n_removals; i++)
        g_variant_builder_add (&removals, "t", g_array_index (removed, guint64, i));
    g_array_append_vals (viewer->pending_removals, removed->data, n_removals);
    delta = g_variant_ref_sink (g_variant_new ("(t@a(ttddday)@at)", ++self->sequence,
                                               g_variant_builder_end (&updates),
                                               g_variant_builder_end (&removals)));
    viewer->pending = delta;
    viewer->more = remaining;
    viewer->pending_cursor = next_cursor;
    if (more != NULL)
        *more = remaining;
    viewer->sequence = self->sequence;
    self->pages_built++;
    self->updates_sent += n_updates;
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
    guint i;

    g_return_val_if_fail (LRG_IS_MMO_REPLICATOR (self), FALSE);
    viewer = g_hash_table_lookup (self->viewers, &viewer_id);
    if (viewer == NULL || viewer->pending == NULL || viewer->sequence != sequence)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "No matching pending snapshot");
        return FALSE;
    }
    /* The baseline takes exactly the page's own changes */
    for (i = 0; i < viewer->pending_removals->len; i++)
        id_map_remove (&viewer->baseline, g_array_index (viewer->pending_removals, guint64, i));
    for (i = 0; i < viewer->pending_updates->len; i++)
    {
        const Change *change = &g_array_index (viewer->pending_updates, Change, i);
        id_map_set (&viewer->baseline, change->id, change->revision);
    }
    g_array_set_size (viewer->pending_updates, 0);
    g_array_set_size (viewer->pending_removals, 0);
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
    /* Setting the same focus again (every cycle, usually) costs a lookup */
    value = g_hash_table_lookup (self->focus, &viewer_id);
    if (value != NULL)
    {
        *value = entity_id;
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

void
lrg_mmo_replicator_get_counters (LrgMmoReplicator *self,
                                 guint64          *pages,
                                 guint64          *updates,
                                 guint64          *encodings)
{
    g_return_if_fail (LRG_IS_MMO_REPLICATOR (self));
    if (pages != NULL)
        *pages = self->pages_built;
    if (updates != NULL)
        *updates = self->updates_sent;
    if (encodings != NULL)
        *encodings = self->encodings_built;
}
