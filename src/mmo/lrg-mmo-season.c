/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-season.h"
#include "lrg-mmo-service-private.h"

struct _LrgMmoSeason
{
    GObject parent_instance;
    LrgMmoStore *store;
};
G_DEFINE_TYPE (LrgMmoSeason, lrg_mmo_season, G_TYPE_OBJECT)
static void
lrg_mmo_season_dispose (GObject *object)
{
    g_clear_object (&LRG_MMO_SEASON (object)->store);
    G_OBJECT_CLASS (lrg_mmo_season_parent_class)->dispose (object);
}
static void
lrg_mmo_season_class_init (LrgMmoSeasonClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = lrg_mmo_season_dispose;
}
static void
lrg_mmo_season_init (LrgMmoSeason *self)
{
}
LrgMmoSeason *
lrg_mmo_season_new (LrgMmoStore *store)
{
    LrgMmoSeason *self;
    g_return_val_if_fail (LRG_IS_MMO_STORE (store), NULL);
    self = g_object_new (LRG_TYPE_MMO_SEASON, NULL);
    self->store = g_object_ref (store);
    return self;
}

gboolean
lrg_mmo_season_create (LrgMmoSeason *self, const gchar *season, gint64 start,
                       gint64 end, guint capacity, GError **error)
{
    g_autofree gchar *key = NULL;
    g_return_val_if_fail (LRG_IS_MMO_SEASON (self), FALSE);
    if (!_lrg_mmo_id_valid (season) || start < 0 || end <= start || capacity < 2 || capacity > 1000)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid season configuration");
    key = g_strconcat ("season/", season, NULL);
    return _lrg_mmo_put (self->store, key, 0,
                         g_variant_new ("(xxua(suuu))", start, end, capacity, NULL), error);
}

typedef struct
{
    gchar *account;
    guint wins, losses, draws;
} Entry;
static void
entry_free (gpointer data)
{
    Entry *entry = data;
    g_free (entry->account);
    g_free (entry);
}
static gint
entry_compare (gconstpointer a, gconstpointer b)
{
    const Entry *first = *(Entry * const *) a;
    const Entry *second = *(Entry * const *) b;
    guint64 x = (guint64) first->wins * 3 + first->draws;
    guint64 y = (guint64) second->wins * 3 + second->draws;
    if (x != y)
        return x > y ? -1 : 1;
    if (first->wins != second->wins)
        return first->wins > second->wins ? -1 : 1;
    return strcmp (first->account, second->account);
}
static GPtrArray *
entries (GVariant *value)
{
    g_autoptr(GVariant) rows = g_variant_get_child_value (value, 3);
    GPtrArray *array = g_ptr_array_new_with_free_func (entry_free);
    GVariantIter iter;
    const gchar *account;
    guint wins, losses, draws;
    g_variant_iter_init (&iter, rows);
    while (g_variant_iter_next (&iter, "(&suuu)", &account, &wins, &losses, &draws))
    {
        Entry *entry = g_new0 (Entry, 1);
        entry->account = g_strdup (account);
        entry->wins = wins;
        entry->losses = losses;
        entry->draws = draws;
        g_ptr_array_add (array, entry);
    }
    return array;
}
static GVariant *
rows_variant (GPtrArray *array)
{
    GVariantBuilder builder;
    guint i;
    g_ptr_array_sort (array, entry_compare);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(suuu)"));
    for (i = 0; i < array->len; i++)
    {
        Entry *entry = g_ptr_array_index (array, i);
        g_variant_builder_add (&builder, "(suuu)", entry->account, entry->wins, entry->losses, entry->draws);
    }
    return g_variant_builder_end (&builder);
}

gboolean
lrg_mmo_season_record (LrgMmoSeason *self, const gchar *season, const gchar *first,
                       const gchar *second, guint result, const gchar *match,
                       gint64 now, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) intent = NULL;
    g_autoptr(GVariant) batch = NULL;
    g_autoptr(GPtrArray) array = NULL;
    guint64 revision;
    gint64 start, end;
    guint capacity, i, j;
    const gchar *players[2];
    gint prior;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_SEASON (self), FALSE);
    if (!_lrg_mmo_id_valid (season) || !_lrg_mmo_id_valid (first) || !_lrg_mmo_id_valid (second) ||
        g_str_equal (first, second) || result > 2)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid match result");
    intent = g_variant_ref_sink (g_variant_new ("(ssssu)", "season-result", season, first, second, result));
    prior = _lrg_mmo_operation_check (self->store, match, intent, error);
    if (prior != 0)
        return prior == 1;
    key = g_strconcat ("season/", season, NULL);
    value = _lrg_mmo_load (self->store, key, "(xxua(suuu))", &revision, error);
    if (value == NULL)
        return FALSE;
    g_variant_get_child (value, 0, "x", &start);
    g_variant_get_child (value, 1, "x", &end);
    g_variant_get_child (value, 2, "u", &capacity);
    if (now < start || now >= end)
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Season is not active");
    array = entries (value);
    players[0] = first;
    players[1] = second;
    for (i = 0; i < 2; i++)
    {
        Entry *entry = NULL;
        guint *counter;
        for (j = 0; j < array->len; j++)
        {
            Entry *candidate = g_ptr_array_index (array, j);
            if (g_str_equal (candidate->account, players[i]))
                entry = candidate;
        }
        if (entry == NULL)
        {
            if (array->len >= capacity)
                return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Season entrant limit reached");
            entry = g_new0 (Entry, 1);
            entry->account = g_strdup (players[i]);
            g_ptr_array_add (array, entry);
        }
        counter = result == 0 ? &entry->draws : (result == i + 1 ? &entry->wins : &entry->losses);
        if (*counter == G_MAXUINT)
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Season result count exhausted");
        (*counter)++;
    }
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    _lrg_mmo_change (&builder, key, revision,
                     g_variant_new ("(xxu@a(suuu))", start, end, capacity, rows_variant (array)));
    _lrg_mmo_operation_add (&builder, match, intent);
    batch = g_variant_ref_sink (g_variant_builder_end (&builder));
    return lrg_mmo_store_commit (self->store, batch, error);
}

GVariant *
lrg_mmo_season_standings (LrgMmoSeason *self, const gchar *season, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GPtrArray) array = NULL;
    guint64 revision;
    g_return_val_if_fail (LRG_IS_MMO_SEASON (self), NULL);
    if (!_lrg_mmo_id_valid (season))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid season");
        return NULL;
    }
    key = g_strconcat ("season/", season, NULL);
    value = _lrg_mmo_load (self->store, key, "(xxua(suuu))", &revision, error);
    if (value == NULL)
        return NULL;
    array = entries (value);
    return g_variant_ref_sink (rows_variant (array));
}
