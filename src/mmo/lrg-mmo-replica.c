/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-replica.h"
#include "lrg-mmo-service-private.h"
#include <math.h>

typedef struct
{
    guint64 id;
    GVariant *current;
    gdouble previous[3];
} ReplicaEntity;
struct _LrgMmoReplica
{
    GObject parent_instance;
    guint capacity;
    gchar *stream;
    guint64 sequence;
    GVariant *last_delta;
    GHashTable *entities;
};
G_DEFINE_TYPE (LrgMmoReplica, lrg_mmo_replica, G_TYPE_OBJECT)
static void
entity_free (gpointer data)
{
    ReplicaEntity *entity = data;
    g_variant_unref (entity->current);
    g_free (entity);
}
static void
lrg_mmo_replica_finalize (GObject *object)
{
    LrgMmoReplica *self = LRG_MMO_REPLICA (object);
    g_free (self->stream);
    g_clear_pointer (&self->last_delta, g_variant_unref);
    g_hash_table_unref (self->entities);
    G_OBJECT_CLASS (lrg_mmo_replica_parent_class)->finalize (object);
}
static void
lrg_mmo_replica_class_init (LrgMmoReplicaClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_mmo_replica_finalize;
}
static void
lrg_mmo_replica_init (LrgMmoReplica *self)
{
    self->entities = g_hash_table_new_full (g_int64_hash, g_int64_equal, NULL, entity_free);
}
LrgMmoReplica *
lrg_mmo_replica_new (guint capacity)
{
    LrgMmoReplica *self;
    g_return_val_if_fail (capacity > 0, NULL);
    self = g_object_new (LRG_TYPE_MMO_REPLICA, NULL);
    self->capacity = capacity;
    return self;
}
void
lrg_mmo_replica_reset (LrgMmoReplica *self, const gchar *stream)
{
    gchar *copy;
    g_return_if_fail (LRG_IS_MMO_REPLICA (self));
    g_return_if_fail (stream != NULL && *stream != '\0' && strlen (stream) <= 128);
    copy = g_strdup (stream);
    g_free (self->stream);
    self->stream = copy;
    self->sequence = 0;
    g_clear_pointer (&self->last_delta, g_variant_unref);
    g_hash_table_remove_all (self->entities);
}

gboolean
lrg_mmo_replica_apply (LrgMmoReplica *self, const gchar *stream, GVariant *delta, GError **error)
{
    g_autoptr(GVariant) updates = NULL;
    g_autoptr(GVariant) removals = NULL;
    g_autoptr(GHashTable) seen = NULL;
    g_autoptr(GPtrArray) staged = NULL;
    GVariantIter iter;
    GVariant *bytes;
    guint64 sequence, id, revision;
    gdouble x, y, z;
    guint count, i;
    g_return_val_if_fail (LRG_IS_MMO_REPLICA (self), FALSE);
    if (self->stream == NULL || stream == NULL || !g_str_equal (self->stream, stream) ||
        delta == NULL || !g_variant_is_of_type (delta, G_VARIANT_TYPE ("(ta(ttddday)at)")) ||
        g_variant_get_size (delta) > 1024 * 1024 || !g_variant_is_normal_form (delta))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Invalid delta or server stream");
    g_variant_get (delta, "(t@a(ttddday)@at)", &sequence, &updates, &removals);
    if (sequence == self->sequence && self->last_delta != NULL && g_variant_equal (delta, self->last_delta))
        return TRUE;
    if (sequence == 0 || sequence <= self->sequence)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Stale or altered snapshot sequence");
    seen = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
    staged = g_ptr_array_new_with_free_func (entity_free);
    count = g_hash_table_size (self->entities);
    g_variant_iter_init (&iter, removals);
    while (g_variant_iter_next (&iter, "t", &id))
    {
        guint64 *key;
        if (id == 0 || g_hash_table_contains (seen, &id))
            goto invalid;
        key = g_new (guint64, 1);
        *key = id;
        g_hash_table_add (seen, key);
        if (g_hash_table_contains (self->entities, &id))
            count--;
    }
    g_variant_iter_init (&iter, updates);
    while (g_variant_iter_next (&iter, "(ttddd@ay)", &id, &revision, &x, &y, &z, &bytes))
    {
        ReplicaEntity *entity;
        ReplicaEntity *old = g_hash_table_lookup (self->entities, &id);
        guint64 *key;
        guint64 old_revision = 0;
        if (old != NULL)
            g_variant_get_child (old->current, 0, "t", &old_revision);
        if (id == 0 || revision == 0 || revision <= old_revision ||
            !isfinite (x) || !isfinite (y) || !isfinite (z) ||
            g_variant_get_size (bytes) > 65536 || g_hash_table_contains (seen, &id))
        {
            g_variant_unref (bytes);
            goto invalid;
        }
        if (old == NULL && count++ >= self->capacity)
        {
            g_variant_unref (bytes);
            goto invalid;
        }
        entity = g_new0 (ReplicaEntity, 1);
        entity->id = id;
        entity->current = g_variant_ref_sink (g_variant_new ("(tddd@ay)", revision, x, y, z, bytes));
        g_variant_unref (bytes);
        if (old != NULL)
        {
            g_variant_get_child (old->current, 1, "d", &entity->previous[0]);
            g_variant_get_child (old->current, 2, "d", &entity->previous[1]);
            g_variant_get_child (old->current, 3, "d", &entity->previous[2]);
        }
        else
        {
            entity->previous[0] = x;
            entity->previous[1] = y;
            entity->previous[2] = z;
        }
        g_ptr_array_add (staged, entity);
        key = g_new (guint64, 1);
        *key = id;
        g_hash_table_add (seen, key);
    }
    /* No observable mutation happens until all validation has succeeded. */
    g_variant_iter_init (&iter, removals);
    while (g_variant_iter_next (&iter, "t", &id))
        g_hash_table_remove (self->entities, &id);
    for (i = 0; i < staged->len; i++)
    {
        ReplicaEntity *entity = g_ptr_array_index (staged, i);
        g_hash_table_replace (self->entities, &entity->id, entity);
    }
    g_ptr_array_set_free_func (staged, NULL);
    g_clear_pointer (&self->last_delta, g_variant_unref);
    self->last_delta = g_variant_ref (delta);
    self->sequence = sequence;
    return TRUE;
invalid:
    return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Invalid entity, duplicate ID or replica capacity exceeded");
}

GVariant *
lrg_mmo_replica_lookup (LrgMmoReplica *self, guint64 entity_id)
{
    ReplicaEntity *entity;
    g_return_val_if_fail (LRG_IS_MMO_REPLICA (self), NULL);
    entity = g_hash_table_lookup (self->entities, &entity_id);
    return entity != NULL ? g_variant_ref (entity->current) : NULL;
}

GVariant *
lrg_mmo_replica_interpolate (LrgMmoReplica *self, guint64 entity_id, gdouble alpha)
{
    ReplicaEntity *entity;
    gdouble coordinates[3];
    guint i;
    g_return_val_if_fail (LRG_IS_MMO_REPLICA (self), NULL);
    if (!isfinite (alpha) || alpha < 0 || alpha > 1)
        return NULL;
    entity = g_hash_table_lookup (self->entities, &entity_id);
    if (entity == NULL)
        return NULL;
    for (i = 0; i < 3; i++)
    {
        gdouble current;
        g_variant_get_child (entity->current, i + 1, "d", &current);
        coordinates[i] = entity->previous[i] * (1 - alpha) + current * alpha;
    }
    return g_variant_ref_sink (g_variant_new ("(ddd)", coordinates[0], coordinates[1], coordinates[2]));
}

guint64
lrg_mmo_replica_get_sequence (LrgMmoReplica *self)
{
    g_return_val_if_fail (LRG_IS_MMO_REPLICA (self), 0);
    return self->sequence;
}
