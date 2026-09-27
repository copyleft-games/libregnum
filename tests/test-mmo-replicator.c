/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * test-mmo-replicator.c - page building and acknowledgement of
 * LrgMmoReplicator against a reference model.
 *
 * The reference model below is the replicator as it was before its pages
 * were made cheap (a GHashTable baseline copied into every page, boxed
 * IDs and revisions, a GVariant record built per entity per page).  A
 * seeded fuzz drives both through the same calls and requires every
 * result, error, "more" flag and page to be identical, byte for byte.
 * A benchmark-style test reports how long a crowded town takes a cycle
 * with each (timings are printed, never asserted).
 *
 * Environment:
 *   LRG_REPLICATOR_FUZZ_ROUNDS=N  run N seeds of the fuzz (default 4)
 *   -m perf                       benchmark 512 viewers for more cycles
 */
#include <libregnum.h>
#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Reference model (the replicator before its page-cost rework)             */
/* ------------------------------------------------------------------------ */

typedef struct
{
    gchar *zone;
    gint x;
    gint y;
    gint z;
} RefCell;

typedef struct
{
    guint64 id;
    guint64 revision;
    RefCell cell;
    gdouble x;
    gdouble y;
    gdouble z;
    GBytes *state;
} RefEntity;

typedef struct
{
    GHashTable *baseline;
    GHashTable *pending_baseline;
    GVariant *pending;
    guint64 sequence;
    gboolean more;
    guint64 cursor;
    guint64 pending_cursor;
} RefViewer;

typedef struct
{
    GHashTable *entities;
    GHashTable *cells;
    GHashTable *viewers;
    GHashTable *focus;
    guint max_entities;
    guint max_viewers;
    gdouble cell_size;
    guint64 revision;
    guint64 sequence;
} Ref;

static guint
ref_cell_hash (gconstpointer data)
{
    const RefCell *cell = data;
    return g_str_hash (cell->zone) ^ ((guint) cell->x * 73856093u) ^
           ((guint) cell->y * 19349663u) ^ ((guint) cell->z * 83492791u);
}

static gboolean
ref_cell_equal (gconstpointer a,
                gconstpointer b)
{
    const RefCell *x = a;
    const RefCell *y = b;
    return x->x == y->x && x->y == y->y && x->z == y->z && g_str_equal (x->zone, y->zone);
}

static void
ref_cell_free (gpointer data)
{
    RefCell *cell = data;
    g_free (cell->zone);
    g_free (cell);
}

static void
ref_entity_free (gpointer data)
{
    RefEntity *entity = data;
    g_free (entity->cell.zone);
    g_bytes_unref (entity->state);
    g_free (entity);
}

static void
ref_viewer_free (gpointer data)
{
    RefViewer *viewer = data;
    g_clear_pointer (&viewer->baseline, g_hash_table_unref);
    g_clear_pointer (&viewer->pending_baseline, g_hash_table_unref);
    g_clear_pointer (&viewer->pending, g_variant_unref);
    g_free (viewer);
}

static GHashTable *
ref_new_baseline (void)
{
    return g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, g_free);
}

static Ref *
ref_new (guint   max_entities,
         guint   max_viewers,
         gdouble cell_size)
{
    Ref *self = g_new0 (Ref, 1);
    self->entities = g_hash_table_new_full (g_int64_hash, g_int64_equal, NULL, ref_entity_free);
    self->cells = g_hash_table_new_full (ref_cell_hash, ref_cell_equal, ref_cell_free, (GDestroyNotify) g_ptr_array_unref);
    self->viewers = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, ref_viewer_free);
    self->focus = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, g_free);
    self->max_entities = max_entities;
    self->max_viewers = max_viewers;
    self->cell_size = cell_size;
    return self;
}

static void
ref_free (Ref *self)
{
    g_hash_table_unref (self->cells);
    g_hash_table_unref (self->entities);
    g_hash_table_unref (self->viewers);
    g_hash_table_unref (self->focus);
    g_free (self);
}

static gboolean
ref_valid_position (Ref         *self,
                    const gchar *zone,
                    gdouble      x,
                    gdouble      y,
                    gdouble      z)
{
    return zone != NULL && *zone != '\0' && strlen (zone) <= 128 &&
           g_utf8_validate (zone, -1, NULL) && isfinite (x) && isfinite (y) && isfinite (z) &&
           fabs (x / self->cell_size) <= 1e9 && fabs (y / self->cell_size) <= 1e9 &&
           fabs (z / self->cell_size) <= 1e9;
}

static void
ref_unindex (Ref       *self,
             RefEntity *entity)
{
    GPtrArray *bucket = g_hash_table_lookup (self->cells, &entity->cell);
    g_ptr_array_remove_fast (bucket, entity);
    if (bucket->len == 0)
        g_hash_table_remove (self->cells, &entity->cell);
}

static gint
ref_compare_entities (gconstpointer a,
                      gconstpointer b)
{
    const RefEntity *x = *(RefEntity *const *) a;
    const RefEntity *y = *(RefEntity *const *) b;
    return (x->id > y->id) - (x->id < y->id);
}

static gint
ref_compare_ids (gconstpointer a,
                 gconstpointer b)
{
    guint64 x = *(const guint64 *) a;
    guint64 y = *(const guint64 *) b;
    return (x > y) - (x < y);
}

static gboolean
ref_upsert (Ref          *self,
            guint64       entity_id,
            const gchar  *zone,
            gdouble       x,
            gdouble       y,
            gdouble       z,
            GBytes       *state,
            GError      **error)
{
    RefEntity *entity;
    RefEntity *old;
    GPtrArray *bucket;
    if (entity_id == 0 || !ref_valid_position (self, zone, x, y, z) ||
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
    entity = g_new0 (RefEntity, 1);
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
        ref_unindex (self, old);
    g_hash_table_replace (self->entities, &entity->id, entity);
    bucket = g_hash_table_lookup (self->cells, &entity->cell);
    if (bucket == NULL)
    {
        RefCell *cell = g_new (RefCell, 1);
        *cell = entity->cell;
        cell->zone = g_strdup (entity->cell.zone);
        bucket = g_ptr_array_new ();
        g_hash_table_insert (self->cells, cell, bucket);
    }
    g_ptr_array_add (bucket, entity);
    return TRUE;
}

static void
ref_remove (Ref     *self,
            guint64  entity_id)
{
    RefEntity *entity = g_hash_table_lookup (self->entities, &entity_id);
    if (entity == NULL)
        return;
    ref_unindex (self, entity);
    g_hash_table_remove (self->entities, &entity_id);
}

static GVariant *
ref_build (Ref          *self,
           guint64       viewer_id,
           const gchar  *zone,
           gdouble       x,
           gdouble       y,
           gdouble       z,
           gdouble       radius,
           guint         limit,
           gboolean      paginate,
           gboolean     *more,
           GError      **error)
{
    RefViewer *viewer;
    g_autoptr(GHashTable) baseline = NULL;
    g_autoptr(GPtrArray) visible = NULL;
    g_autoptr(GArray) removed = NULL;
    GHashTableIter iter;
    gpointer key;
    GVariantBuilder updates;
    GVariantBuilder removals;
    GVariant *delta;
    RefCell cell;
    gint min_x, max_x, min_y, max_y, min_z, max_z;
    guint i;
    gsize budget = 32;
    g_autoptr(GHashTable) page_baseline = NULL;
    guint n_updates, n_removals;
    gboolean remaining = FALSE;
    guint64 next_cursor = 0;
    guint64 focus_id;

    {
        guint64 *focused = g_hash_table_lookup (self->focus, &viewer_id);
        focus_id = focused != NULL ? *focused : 0;
    }
    if (more != NULL)
        *more = FALSE;
    if (limit < 128 || limit > 1024 * 1024 || viewer_id == 0 || !ref_valid_position (self, zone, x, y, z) ||
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
    baseline = ref_new_baseline ();
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
                    RefEntity *entity = g_ptr_array_index (bucket, i);
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
    g_ptr_array_sort (visible, ref_compare_entities);
    g_array_sort (removed, ref_compare_ids);
    if (paginate && viewer != NULL && visible->len > 1)
    {
        guint start = 0;
        gpointer *ordered;
        while (start < visible->len && ((RefEntity *) g_ptr_array_index (visible, start))->id <= viewer->cursor)
            start++;
        ordered = g_memdup2 (visible->pdata, visible->len * sizeof (gpointer));
        for (i = 0; i < visible->len; i++)
            visible->pdata[i] = ordered[(start + i) % visible->len];
        g_free (ordered);
    }
    if (paginate && focus_id != 0)
    {
        for (i = 0; i < visible->len; i++)
            if (((RefEntity *) g_ptr_array_index (visible, i))->id == focus_id)
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
        page_baseline = ref_new_baseline ();
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
            RefEntity *entity = g_ptr_array_index (visible, i);
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
    next_cursor = viewer != NULL ? viewer->cursor : 0;
    for (i = n_updates; i > 0; i--)
        if (((RefEntity *) g_ptr_array_index (visible, i - 1))->id != focus_id || focus_id == 0)
        {
            next_cursor = ((RefEntity *) g_ptr_array_index (visible, i - 1))->id;
            break;
        }
    if (n_updates > 1)
        qsort (visible->pdata, n_updates, sizeof (gpointer), ref_compare_entities);
    g_variant_builder_init (&updates, G_VARIANT_TYPE ("a(ttddday)"));
    g_variant_builder_init (&removals, G_VARIANT_TYPE ("at"));
    for (i = 0; i < n_updates; i++)
    {
        RefEntity *entity = g_ptr_array_index (visible, i);
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
        viewer = g_new0 (RefViewer, 1);
        viewer->baseline = ref_new_baseline ();
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

static gboolean
ref_acknowledge (Ref      *self,
                 guint64   viewer_id,
                 guint64   sequence,
                 GError  **error)
{
    RefViewer *viewer = g_hash_table_lookup (self->viewers, &viewer_id);
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

static void
ref_forget (Ref     *self,
            guint64  viewer_id)
{
    g_hash_table_remove (self->viewers, &viewer_id);
    g_hash_table_remove (self->focus, &viewer_id);
}

static void
ref_set_focus (Ref     *self,
               guint64  viewer_id,
               guint64  entity_id)
{
    guint64 *key;
    guint64 *value;
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

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static guint64
page_sequence (GVariant *page)
{
    guint64 sequence;
    g_variant_get_child (page, 0, "t", &sequence);
    return sequence;
}

/* Two results are the same: both NULL with the same error code, or pages
 * equal in value and in serialized bytes. */
static void
assert_same_page (GVariant     *page,
                  const GError *error,
                  GVariant     *expected,
                  const GError *expected_error)
{
    if (expected == NULL)
    {
        g_assert_null (page);
        g_assert_nonnull (error);
        g_assert_nonnull (expected_error);
        g_assert_true (error->domain == expected_error->domain);
        g_assert_cmpint (error->code, ==, expected_error->code);
        return;
    }
    g_assert_nonnull (page);
    g_assert_no_error (error);
    g_assert_true (g_variant_equal (page, expected));
    g_assert_cmpmem (g_variant_get_data (page), g_variant_get_size (page),
                     g_variant_get_data (expected), g_variant_get_size (expected));
}

/* ------------------------------------------------------------------------ */
/* Tests                                                                    */
/* ------------------------------------------------------------------------ */

#define FUZZ_IDS (160)
#define FUZZ_VIEWERS (12)

/* An entity ID of the fuzz: small ones, ones spaced by powers of two
 * (they share hash homes) and ones at the top of the range. */
static guint64
fuzz_id (guint index)
{
    if (index < 96)
        return index + 1;
    if (index < 144)
        return (guint64) (index - 95) << 20;
    return G_MAXUINT64 - (index - 144);
}

static void
fuzz_seed (guint32 seed)
{
    g_autoptr(GRand) rand = g_rand_new_with_seed (seed);
    g_autoptr(LrgMmoReplicator) rep = lrg_mmo_replicator_new (120, 10, 10);
    Ref *ref = ref_new (120, 10, 10);
    static const gchar *zones[] = { "a", "b" };
    guint step;
    guint64 pages;
    guint64 updates;
    guint64 encodings;
    guint built = 0;

    for (step = 0; step < 6000; step++)
    {
        guint op = g_rand_int_range (rand, 0, 100);
        guint64 viewer = (guint64) g_rand_int_range (rand, 1, FUZZ_VIEWERS + 1);

        if (op < 45)
        {
            /* Publish: a random entity somewhere, with random state */
            guint64 id = fuzz_id (g_rand_int_range (rand, 0, FUZZ_IDS));
            const gchar *zone = zones[g_rand_int_range (rand, 0, 5) == 0 ? 1 : 0];
            gdouble x = g_rand_double_range (rand, -60, 60);
            gdouble y = g_rand_int_range (rand, 0, 4) == 0 ? g_rand_double_range (rand, -15, 15) : 0;
            gdouble z = g_rand_double_range (rand, -60, 60);
            guint size = g_rand_int_range (rand, 0, 5) == 0 ? g_rand_int_range (rand, 60, 400) : g_rand_int_range (rand, 0, 40);
            g_autofree guint8 *data = g_malloc (size + 1);
            g_autoptr(GBytes) state = NULL;
            g_autoptr(GError) error = NULL;
            g_autoptr(GError) expected_error = NULL;
            guint i;
            gboolean ok;
            for (i = 0; i < size; i++)
                data[i] = (guint8) g_rand_int (rand);
            state = g_bytes_new (data, size);
            ok = lrg_mmo_replicator_upsert (rep, id, zone, x, y, z, state, &error);
            g_assert_cmpint (ok, ==, ref_upsert (ref, id, zone, x, y, z, state, &expected_error));
            if (!ok)
                g_assert_cmpint (error->code, ==, expected_error->code);
        }
        else if (op < 52)
        {
            guint64 id = fuzz_id (g_rand_int_range (rand, 0, FUZZ_IDS));
            lrg_mmo_replicator_remove (rep, id);
            ref_remove (ref, id);
        }
        else if (op < 82)
        {
            /* A page (or, now and then, a whole delta) for a viewer */
            const gchar *zone = zones[g_rand_int_range (rand, 0, 6) == 0 ? 1 : 0];
            gdouble x = g_rand_double_range (rand, -50, 50);
            gdouble z = g_rand_double_range (rand, -50, 50);
            gdouble radius = g_rand_int_range (rand, 0, 10) == 0 ? 0 : g_rand_double_range (rand, 1, 40);
            guint budget = g_rand_int_range (rand, 0, 4) == 0 ? 128 : g_rand_int_range (rand, 128, 4096);
            gboolean whole = g_rand_int_range (rand, 0, 8) == 0;
            gboolean more = TRUE;
            gboolean expected_more = TRUE;
            g_autoptr(GVariant) page = NULL;
            g_autoptr(GVariant) expected = NULL;
            g_autoptr(GError) error = NULL;
            g_autoptr(GError) expected_error = NULL;
            if (g_rand_int_range (rand, 0, 50) == 0)
                radius = 41; /* invalid: over four cells */
            if (whole)
            {
                page = lrg_mmo_replicator_build (rep, viewer, zone, x, 0, z, radius, &error);
                expected = ref_build (ref, viewer, zone, x, 0, z, radius, 1024 * 1024, FALSE, NULL, &expected_error);
            }
            else
            {
                page = lrg_mmo_replicator_build_page (rep, viewer, zone, x, 0, z, radius, budget, &more, &error);
                expected = ref_build (ref, viewer, zone, x, 0, z, radius, budget, TRUE, &expected_more, &expected_error);
                g_assert_cmpint (more, ==, expected_more);
            }
            assert_same_page (page, error, expected, expected_error);
            if (page != NULL)
                built++;
            /* Usually the client applies and acknowledges it at once */
            if (page != NULL && g_rand_int_range (rand, 0, 10) < 7)
            {
                guint64 sequence = page_sequence (page);
                g_assert_true (lrg_mmo_replicator_acknowledge (rep, viewer, sequence, NULL));
                g_assert_true (ref_acknowledge (ref, viewer, sequence, NULL));
            }
        }
        else if (op < 90)
        {
            /* A late, early or doubled acknowledgement */
            guint64 sequence = (guint64) g_rand_int_range (rand, 0, (gint) MIN (built + 3, 100000));
            g_autoptr(GError) error = NULL;
            gboolean ok = lrg_mmo_replicator_acknowledge (rep, viewer, sequence, &error);
            g_assert_cmpint (ok, ==, ref_acknowledge (ref, viewer, sequence, NULL));
            if (!ok)
                g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
        }
        else if (op < 95)
        {
            guint64 focus = g_rand_int_range (rand, 0, 3) == 0 ? 0 : fuzz_id (g_rand_int_range (rand, 0, FUZZ_IDS));
            lrg_mmo_replicator_set_focus (rep, viewer, focus);
            ref_set_focus (ref, viewer, focus);
        }
        else
        {
            lrg_mmo_replicator_forget (rep, viewer);
            ref_forget (ref, viewer);
        }
    }
    lrg_mmo_replicator_get_counters (rep, &pages, &updates, &encodings);
    g_assert_cmpuint (pages, >, 0);
    g_assert_cmpuint (encodings, <=, updates);
    g_test_message ("seed %u: %" G_GUINT64_FORMAT " pages, %" G_GUINT64_FORMAT " updates, %"
                    G_GUINT64_FORMAT " records encoded", seed, pages, updates, encodings);
    ref_free (ref);
}

/* The replicator and the reference model agree on every call. */
static void
test_replicator_matches_reference (void)
{
    const gchar *env = g_getenv ("LRG_REPLICATOR_FUZZ_ROUNDS");
    guint rounds = env != NULL ? (guint) g_ascii_strtoull (env, NULL, 10) : 4;
    guint i;
    for (i = 0; i < MAX (rounds, 1u); i++)
        fuzz_seed (0x5eed0000u + i);
}

/* Records are shared by the pages of every viewer that sees an entity, and
 * encoded again only after the entity changes. */
static void
test_replicator_shares_records (void)
{
    g_autoptr(LrgMmoReplicator) rep = lrg_mmo_replicator_new (16, 8, 10);
    g_autoptr(GBytes) state = g_bytes_new_static ("abc", 3);
    guint64 pages;
    guint64 updates;
    guint64 encodings;
    guint64 viewer;
    guint64 id;

    for (id = 1; id <= 4; id++)
        g_assert_true (lrg_mmo_replicator_upsert (rep, id, "town", id, 0, 0, state, NULL));
    for (viewer = 1; viewer <= 8; viewer++)
    {
        g_autoptr(GVariant) page = lrg_mmo_replicator_build_page (rep, viewer, "town", 0, 0, 0, 10, 4096, NULL, NULL);
        g_autoptr(GVariant) records = g_variant_get_child_value (page, 1);
        g_assert_cmpuint (g_variant_n_children (records), ==, 4);
        g_assert_true (lrg_mmo_replicator_acknowledge (rep, viewer, page_sequence (page), NULL));
    }
    lrg_mmo_replicator_get_counters (rep, &pages, &updates, &encodings);
    g_assert_cmpuint (pages, ==, 8);
    g_assert_cmpuint (updates, ==, 32);
    g_assert_cmpuint (encodings, ==, 4);

    /* One entity changes: one new record for all eight viewers */
    g_assert_true (lrg_mmo_replicator_upsert (rep, 2, "town", 2, 0, 1, state, NULL));
    for (viewer = 1; viewer <= 8; viewer++)
    {
        g_autoptr(GVariant) page = lrg_mmo_replicator_build_page (rep, viewer, "town", 0, 0, 0, 10, 4096, NULL, NULL);
        g_autoptr(GVariant) records = g_variant_get_child_value (page, 1);
        g_assert_cmpuint (g_variant_n_children (records), ==, 1);
        g_assert_true (lrg_mmo_replicator_acknowledge (rep, viewer, page_sequence (page), NULL));
    }
    lrg_mmo_replicator_get_counters (rep, &pages, &updates, &encodings);
    g_assert_cmpuint (pages, ==, 16);
    g_assert_cmpuint (updates, ==, 40);
    g_assert_cmpuint (encodings, ==, 5);

    /* A pending page returned again is not built again */
    {
        g_autoptr(GVariant) first = NULL;
        g_autoptr(GVariant) again = NULL;
        g_assert_true (lrg_mmo_replicator_upsert (rep, 3, "town", 3, 0, 1, state, NULL));
        first = lrg_mmo_replicator_build_page (rep, 1, "town", 0, 0, 0, 10, 4096, NULL, NULL);
        again = lrg_mmo_replicator_build_page (rep, 1, "town", 0, 0, 0, 10, 4096, NULL, NULL);
        g_assert_true (first == again);
        lrg_mmo_replicator_get_counters (rep, &pages, NULL, NULL);
        g_assert_cmpuint (pages, ==, 17);
    }
}

/* A viewer's baseline survives many entities coming and going: the
 * pages converge on exactly the visible set. */
static void
test_replicator_baseline_churn (void)
{
    g_autoptr(LrgMmoReplicator) rep = lrg_mmo_replicator_new (4096, 1, 32);
    g_autoptr(LrgMmoReplica) replica = lrg_mmo_replica_new (4096);
    g_autoptr(GBytes) state = g_bytes_new_static ("s", 1);
    guint round;
    guint64 id;

    lrg_mmo_replica_reset (replica, "churn");
    for (round = 0; round < 8; round++)
    {
        guint64 first = (guint64) round * 500 + 1;
        gboolean more = TRUE;
        guint guard = 0;
        /* 1000 new entities in view, the previous round's leave */
        for (id = first; id < first + 1000; id++)
            g_assert_true (lrg_mmo_replicator_upsert (rep, id << 7, "town", (gdouble) (id % 100), 0,
                                                      (gdouble) (id % 97), state, NULL));
        if (round > 0)
            for (id = first - 500; id < first; id++)
                lrg_mmo_replicator_remove (rep, id << 7);
        while (more)
        {
            g_autoptr(GVariant) page = NULL;
            g_autoptr(GError) error = NULL;
            page = lrg_mmo_replicator_build_page (rep, 1, "town", 50, 0, 50, 128, 16384, &more, &error);
            g_assert_no_error (error);
            g_assert_true (lrg_mmo_replica_apply (replica, "churn", page, &error));
            g_assert_no_error (error);
            g_assert_true (lrg_mmo_replicator_acknowledge (rep, 1, page_sequence (page), NULL));
            g_assert_cmpuint (++guard, <, 1000);
        }
        /* Exactly this round's entities: the new ones, not the gone ones */
        for (id = first; id < first + 1000; id++)
        {
            g_autoptr(GVariant) found = lrg_mmo_replica_lookup (replica, id << 7);
            g_assert_nonnull (found);
        }
        for (id = first > 500 ? first - 500 : 1; id < first; id++)
        {
            g_autoptr(GVariant) gone = lrg_mmo_replica_lookup (replica, id << 7);
            g_assert_null (gone);
        }
    }
}

/*
 * A crowded town: @viewers heroes walking and 600 townsfolk, 10% of them
 * changing a cycle.  Every cycle publishes, then builds, applies nothing
 * and acknowledges one 8 KiB page per hero (as GNUnited does).
 */
static gdouble
bench_cycles (gboolean reference,
              guint    viewers,
              guint    cycles,
              guint64 *bytes)
{
    LrgMmoReplicator *rep = NULL;
    Ref *ref = NULL;
    g_autoptr(GRand) rand = g_rand_new_with_seed (42);
    g_autoptr(GBytes) state = NULL;
    guint8 data[90];
    gint64 start = 0;
    guint cycle;
    guint i;
    guint64 id;

    memset (data, 7, sizeof data);
    state = g_bytes_new (data, sizeof data);
    if (reference)
        ref = ref_new (4096, viewers, 32);
    else
        rep = lrg_mmo_replicator_new (4096, viewers, 32);
    *bytes = 0;
    /* One warm-up cycle, then the timed ones */
    for (cycle = 0; cycle <= cycles; cycle++)
    {
        if (cycle == 1)
            start = g_get_monotonic_time ();
        for (id = 1; id <= 600; id++)
            if (cycle == 0 || g_rand_int_range (rand, 0, 10) == 0)
            {
                gdouble x = (gdouble) (id % 30) * 6;
                gdouble z = (gdouble) (id / 30) * 9;
                if (reference)
                    g_assert_true (ref_upsert (ref, id, "town", x, 0, z, state, NULL));
                else
                    g_assert_true (lrg_mmo_replicator_upsert (rep, id, "town", x, 0, z, state, NULL));
            }
        for (i = 0; i < viewers; i++)
        {
            gdouble x = fmod ((gdouble) i * 7.3 + cycle * 1.5, 180);
            gdouble z = fmod ((gdouble) i * 3.1 + cycle * 0.7, 180);
            if (reference)
                g_assert_true (ref_upsert (ref, 10000 + i, "town", x, 0, z, state, NULL));
            else
                g_assert_true (lrg_mmo_replicator_upsert (rep, 10000 + i, "town", x, 0, z, state, NULL));
        }
        for (i = 0; i < viewers; i++)
        {
            g_autoptr(GVariant) page = NULL;
            gdouble x = fmod ((gdouble) i * 7.3 + cycle * 1.5, 180);
            gdouble z = fmod ((gdouble) i * 3.1 + cycle * 0.7, 180);
            gboolean more;
            if (reference)
            {
                ref_set_focus (ref, i + 1, 10000 + i);
                page = ref_build (ref, i + 1, "town", x, 0, z, 100, 8192, TRUE, &more, NULL);
            }
            else
            {
                lrg_mmo_replicator_set_focus (rep, i + 1, 10000 + i);
                page = lrg_mmo_replicator_build_page (rep, i + 1, "town", x, 0, z, 100, 8192, &more, NULL);
            }
            g_assert_nonnull (page);
            /* The host serializes the page to send it */
            *bytes += g_variant_get_size (page);
            if (reference)
                g_assert_true (ref_acknowledge (ref, i + 1, page_sequence (page), NULL));
            else
                g_assert_true (lrg_mmo_replicator_acknowledge (rep, i + 1, page_sequence (page), NULL));
        }
    }
    if (reference)
        ref_free (ref);
    else
        g_object_unref (rep);
    return (gdouble) (g_get_monotonic_time () - start) / 1000.0 / cycles;
}

static void
test_replicator_benchmark (void)
{
    guint viewers = g_test_perf () ? 512 : 256;
    guint cycles = g_test_perf () ? 40 : 8;
    guint64 bytes;
    guint64 reference_bytes;
    gdouble ms = bench_cycles (FALSE, viewers, cycles, &bytes);
    gdouble reference_ms = bench_cycles (TRUE, viewers, cycles, &reference_bytes);

    /* The same pages, so the same bytes */
    g_assert_cmpuint (bytes, ==, reference_bytes);
    g_test_message ("benchmark: %u viewers, 600 townsfolk, %u cycles: %.2f ms a cycle "
                    "(reference model %.2f ms, %.1fx), %.1f KiB of pages a cycle",
                    viewers, cycles, ms, reference_ms, reference_ms / MAX (ms, 0.001),
                    (gdouble) bytes / 1024.0 / (cycles + 1));
    if (g_test_verbose () || g_test_perf ())
        g_printerr ("replicator benchmark: %u viewers: %.2f ms a cycle, reference %.2f ms\n",
                    viewers, ms, reference_ms);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/mmo/replicator/matches-reference", test_replicator_matches_reference);
    g_test_add_func ("/mmo/replicator/shares-records", test_replicator_shares_records);
    g_test_add_func ("/mmo/replicator/baseline-churn", test_replicator_baseline_churn);
    g_test_add_func ("/mmo/replicator/benchmark", test_replicator_benchmark);
    return g_test_run ();
}
