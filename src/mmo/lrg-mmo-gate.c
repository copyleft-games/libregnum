/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-gate.h"
#include "lrg-mmo-service-private.h"
#include <math.h>
typedef struct { gdouble tokens; gint64 last; } Bucket;
struct _LrgMmoGate
{
    GObject parent_instance;
    GHashTable *origins;
    guint capacity;
    guint burst;
    gdouble rate;
    gint64 last_time;
};
G_DEFINE_TYPE (LrgMmoGate, lrg_mmo_gate, G_TYPE_OBJECT)
static void
lrg_mmo_gate_finalize (GObject *object)
{
    g_hash_table_unref (LRG_MMO_GATE (object)->origins);
    G_OBJECT_CLASS (lrg_mmo_gate_parent_class)->finalize (object);
}
static void
lrg_mmo_gate_class_init (LrgMmoGateClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_mmo_gate_finalize;
}
static void
lrg_mmo_gate_init (LrgMmoGate *self)
{
    self->origins = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}
LrgMmoGate *
lrg_mmo_gate_new (guint capacity, guint burst, gdouble rate)
{
    LrgMmoGate *self;
    g_return_val_if_fail (capacity > 0 && burst > 0 && isfinite (rate) && rate > 0 && rate <= 1e6, NULL);
    self = g_object_new (LRG_TYPE_MMO_GATE, NULL);
    self->capacity = capacity;
    self->burst = burst;
    self->rate = rate;
    return self;
}

gboolean
lrg_mmo_gate_admit (LrgMmoGate *self, const gchar *origin, gint64 now_us, GError **error)
{
    Bucket *bucket;
    GHashTableIter iter;
    gpointer value;
    gdouble tokens;
    g_return_val_if_fail (LRG_IS_MMO_GATE (self), FALSE);
    if (origin == NULL || *origin == '\0' || strlen (origin) > 128 || now_us < self->last_time)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid admission origin or clock");
    self->last_time = now_us;
    bucket = g_hash_table_lookup (self->origins, origin);
    if (bucket == NULL)
    {
        if (g_hash_table_size (self->origins) >= self->capacity)
        {
            g_hash_table_iter_init (&iter, self->origins);
            while (g_hash_table_iter_next (&iter, NULL, &value))
            {
                Bucket *candidate = value;
                if (now_us - candidate->last >= 60 * G_TIME_SPAN_SECOND &&
                    candidate->tokens + (now_us - candidate->last) / 1000000.0 * self->rate >= self->burst)
                    g_hash_table_iter_remove (&iter);
            }
        }
        if (g_hash_table_size (self->origins) >= self->capacity)
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Admission origin capacity reached");
        bucket = g_new0 (Bucket, 1);
        bucket->tokens = self->burst;
        bucket->last = now_us;
        g_hash_table_insert (self->origins, g_strdup (origin), bucket);
    }
    tokens = MIN ((gdouble) self->burst,
                  bucket->tokens + (now_us - bucket->last) / 1000000.0 * self->rate);
    bucket->last = now_us;
    bucket->tokens = tokens;
    if (tokens < 1)
        return _lrg_mmo_fail (error, G_IO_ERROR_WOULD_BLOCK, "Admission rate exceeded");
    bucket->tokens--;
    return TRUE;
}
