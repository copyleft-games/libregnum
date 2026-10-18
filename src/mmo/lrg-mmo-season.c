/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-season.h"
#include "lrg-mmo-service-private.h"

#include <math.h>
#include <string.h>

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

/* a rated season's ratings: base, K, then account, rating, games */
typedef struct
{
    gchar *account;
    guint rating, games;
} Rating;
static void
rating_free (gpointer data)
{
    Rating *rating = data;
    g_free (rating->account);
    g_free (rating);
}
static gint
rating_compare (gconstpointer a, gconstpointer b)
{
    const Rating *first = *(Rating * const *) a;
    const Rating *second = *(Rating * const *) b;
    if (first->rating != second->rating)
        return first->rating > second->rating ? -1 : 1;
    if (first->games != second->games)
        return first->games > second->games ? -1 : 1;
    return strcmp (first->account, second->account);
}
static GPtrArray *
ratings_of (GVariant *value)
{
    g_autoptr(GVariant) rows = g_variant_get_child_value (value, 2);
    GPtrArray *array = g_ptr_array_new_with_free_func (rating_free);
    GVariantIter iter;
    const gchar *account;
    guint rating, games;
    g_variant_iter_init (&iter, rows);
    while (g_variant_iter_next (&iter, "(&suu)", &account, &rating, &games))
    {
        Rating *entry = g_new0 (Rating, 1);
        entry->account = g_strdup (account);
        entry->rating = rating;
        entry->games = games;
        g_ptr_array_add (array, entry);
    }
    return array;
}
static GVariant *
ratings_variant (GPtrArray *array)
{
    GVariantBuilder builder;
    guint i;
    g_ptr_array_sort (array, rating_compare);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(suu)"));
    for (i = 0; i < array->len; i++)
    {
        Rating *entry = g_ptr_array_index (array, i);
        g_variant_builder_add (&builder, "(suu)", entry->account, entry->rating, entry->games);
    }
    return g_variant_builder_end (&builder);
}
static Rating *
rating_find (GPtrArray *array, const gchar *account, guint base, gboolean add)
{
    Rating *entry;
    guint i;
    for (i = 0; i < array->len; i++)
    {
        entry = g_ptr_array_index (array, i);
        if (g_str_equal (entry->account, account))
            return entry;
    }
    if (!add)
        return NULL;
    entry = g_new0 (Rating, 1);
    entry->account = g_strdup (account);
    entry->rating = base;
    g_ptr_array_add (array, entry);
    return entry;
}
/* the rating record of @season; NULL and no error when it is unrated */
static GVariant *
load_ratings (LrgMmoStore *store, const gchar *season, guint64 *revision, GError **error)
{
    g_autofree gchar *key = g_strconcat ("season-rating/", season, NULL);
    g_autoptr(GError) local = NULL;
    GVariant *value = _lrg_mmo_load (store, key, "(uua(suu))", revision, &local);
    if (value == NULL && !g_error_matches (local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_propagate_error (error, g_steal_pointer (&local));
    return value;
}

gboolean
lrg_mmo_season_create_rated (LrgMmoSeason *self, const gchar *season, gint64 start,
                             gint64 end, guint capacity, guint base_rating, guint k_factor,
                             GError **error)
{
    g_autofree gchar *key = NULL;
    g_autofree gchar *rating_key = NULL;
    g_autoptr(GVariant) batch = NULL;
    GVariantBuilder builder;
    g_return_val_if_fail (LRG_IS_MMO_SEASON (self), FALSE);
    if (!_lrg_mmo_id_valid (season) || start < 0 || end <= start || capacity < 2 || capacity > 1000 ||
        base_rating < 1 || base_rating > 100000 || k_factor < 1 || k_factor > 1000)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid season configuration");
    key = g_strconcat ("season/", season, NULL);
    rating_key = g_strconcat ("season-rating/", season, NULL);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    _lrg_mmo_change (&builder, key, 0, g_variant_new ("(xxua(suuu))", start, end, capacity, NULL));
    _lrg_mmo_change (&builder, rating_key, 0, g_variant_new ("(uua(suu))", base_rating, k_factor, NULL));
    batch = g_variant_ref_sink (g_variant_builder_end (&builder));
    return lrg_mmo_store_commit (self->store, batch, error);
}

/*
 * record_sides:
 * The one way results are applied: @players holds the first side's
 * @n_first accounts, then the second side's @n_second; @intent is the
 * operation's semantic intent (its receipt).
 */
static gboolean
record_sides (LrgMmoSeason *self, const gchar *season, const gchar **players, guint n_first,
              guint n_second, guint result, const gchar *match, gint64 now, GVariant *intent,
              GError **error)
{
    g_autofree gchar *key = NULL;
    g_autofree gchar *rating_key = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) rated = NULL;
    g_autoptr(GVariant) batch = NULL;
    g_autoptr(GPtrArray) array = NULL;
    g_autoptr(GPtrArray) ratings = NULL;
    g_autoptr(GError) local = NULL;
    guint64 revision;
    guint64 rating_revision = 0;
    gint64 start, end;
    guint capacity, i, j;
    gint prior;
    GVariantBuilder builder;
    prior = _lrg_mmo_operation_check (self->store, match, intent, error);
    if (prior != 0)
        return prior == 1;
    key = g_strconcat ("season/", season, NULL);
    value = _lrg_mmo_load (self->store, key, "(xxua(suuu))", &revision, error);
    if (value == NULL)
        return FALSE;
    rated = load_ratings (self->store, season, &rating_revision, &local);
    if (rated == NULL && local != NULL)
    {
        g_propagate_error (error, g_steal_pointer (&local));
        return FALSE;
    }
    g_variant_get_child (value, 0, "x", &start);
    g_variant_get_child (value, 1, "x", &end);
    g_variant_get_child (value, 2, "u", &capacity);
    if (now < start || now >= end)
        return _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Season is not active");
    array = entries (value);
    for (i = 0; i < n_first + n_second; i++)
    {
        Entry *entry = NULL;
        guint *counter;
        guint side = i < n_first ? 1 : 2;
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
        counter = result == 0 ? &entry->draws : (result == side ? &entry->wins : &entry->losses);
        if (*counter == G_MAXUINT)
            return _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Season result count exhausted");
        (*counter)++;
    }
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    _lrg_mmo_change (&builder, key, revision,
                     g_variant_new ("(xxu@a(suuu))", start, end, capacity, rows_variant (array)));
    /* --- a rated season: Elo on the sides' mean ratings --- */
    if (rated != NULL)
    {
        guint base, k;
        gdouble mean[2] = { 0.0, 0.0 };
        gdouble expected, score;
        gint delta;
        g_variant_get_child (rated, 0, "u", &base);
        g_variant_get_child (rated, 1, "u", &k);
        ratings = ratings_of (rated);
        for (i = 0; i < n_first + n_second; i++)
        {
            Rating *entry = rating_find (ratings, players[i], base, TRUE);
            mean[i < n_first ? 0 : 1] += (gdouble) entry->rating;
        }
        mean[0] /= (gdouble) n_first;
        mean[1] /= (gdouble) n_second;
        expected = 1.0 / (1.0 + pow (10.0, (mean[1] - mean[0]) / 400.0));
        score = result == 0 ? 0.5 : (result == 1 ? 1.0 : 0.0);
        delta = (gint) floor ((gdouble) k * (score - expected) + 0.5);
        for (i = 0; i < n_first + n_second; i++)
        {
            /* added above: always found */
            Rating *entry = rating_find (ratings, players[i], base, TRUE);
            gint64 moved = (gint64) entry->rating + (i < n_first ? delta : -delta);
            entry->rating = (guint) CLAMP (moved, (gint64) 0, (gint64) G_MAXUINT32);
            if (entry->games < G_MAXUINT)
                entry->games++;
        }
        rating_key = g_strconcat ("season-rating/", season, NULL);
        _lrg_mmo_change (&builder, rating_key, rating_revision,
                         g_variant_new ("(uu@a(suu))", base, k, ratings_variant (ratings)));
    }
    _lrg_mmo_operation_add (&builder, match, intent);
    batch = g_variant_ref_sink (g_variant_builder_end (&builder));
    return lrg_mmo_store_commit (self->store, batch, error);
}

gboolean
lrg_mmo_season_record (LrgMmoSeason *self, const gchar *season, const gchar *first,
                       const gchar *second, guint result, const gchar *match,
                       gint64 now, GError **error)
{
    g_autoptr(GVariant) intent = NULL;
    const gchar *players[2];
    g_return_val_if_fail (LRG_IS_MMO_SEASON (self), FALSE);
    if (!_lrg_mmo_id_valid (season) || !_lrg_mmo_id_valid (first) || !_lrg_mmo_id_valid (second) ||
        g_str_equal (first, second) || result > 2)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid match result");
    intent = g_variant_ref_sink (g_variant_new ("(ssssu)", "season-result", season, first, second, result));
    players[0] = first;
    players[1] = second;
    return record_sides (self, season, players, 1, 1, result, match, now, intent, error);
}

gboolean
lrg_mmo_season_record_teams (LrgMmoSeason *self, const gchar *season,
                             const gchar * const *first, const gchar * const *second,
                             guint result, const gchar *match, gint64 now, GError **error)
{
    g_autoptr(GVariant) intent = NULL;
    g_autofree const gchar **players = NULL;
    guint n_first, n_second, i, j;
    g_return_val_if_fail (LRG_IS_MMO_SEASON (self), FALSE);
    n_first = first != NULL ? g_strv_length ((gchar **) first) : 0;
    n_second = second != NULL ? g_strv_length ((gchar **) second) : 0;
    if (!_lrg_mmo_id_valid (season) || result > 2 || n_first < 1 || n_second < 1 ||
        n_first > LRG_MMO_SEASON_MAX_TEAM || n_second > LRG_MMO_SEASON_MAX_TEAM)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid team result");
    players = g_new0 (const gchar *, n_first + n_second + 1);
    for (i = 0; i < n_first + n_second; i++)
    {
        players[i] = i < n_first ? first[i] : second[i - n_first];
        if (!_lrg_mmo_id_valid (players[i]))
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid team member");
        for (j = 0; j < i; j++)
            if (g_str_equal (players[i], players[j]))
                return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Account on a team twice");
    }
    intent = g_variant_ref_sink (g_variant_new ("(ss^as^asu)", "season-teams", season, first, second,
                                                result));
    return record_sides (self, season, players, n_first, n_second, result, match, now, intent, error);
}

gboolean
lrg_mmo_season_get_rating (LrgMmoSeason *self, const gchar *season, const gchar *account,
                           guint *out_rating, guint *out_games, GError **error)
{
    g_autoptr(GVariant) rated = NULL;
    g_autoptr(GPtrArray) ratings = NULL;
    g_autoptr(GError) local = NULL;
    guint64 revision;
    guint base;
    Rating *entry;
    g_return_val_if_fail (LRG_IS_MMO_SEASON (self), FALSE);
    g_return_val_if_fail (out_rating != NULL, FALSE);
    if (!_lrg_mmo_id_valid (season) || !_lrg_mmo_id_valid (account))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid season or account");
    rated = load_ratings (self->store, season, &revision, &local);
    if (rated == NULL)
    {
        if (local != NULL)
        {
            g_propagate_error (error, g_steal_pointer (&local));
            return FALSE;
        }
        return _lrg_mmo_fail (error, G_IO_ERROR_NOT_FOUND, "Season is not rated");
    }
    g_variant_get_child (rated, 0, "u", &base);
    ratings = ratings_of (rated);
    entry = rating_find (ratings, account, base, FALSE);
    *out_rating = entry != NULL ? entry->rating : base;
    if (out_games != NULL)
        *out_games = entry != NULL ? entry->games : 0;
    return TRUE;
}

GVariant *
lrg_mmo_season_ratings (LrgMmoSeason *self, const gchar *season, GError **error)
{
    g_autoptr(GVariant) rated = NULL;
    g_autoptr(GPtrArray) ratings = NULL;
    g_autoptr(GError) local = NULL;
    guint64 revision;
    g_return_val_if_fail (LRG_IS_MMO_SEASON (self), NULL);
    if (!_lrg_mmo_id_valid (season))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid season");
        return NULL;
    }
    rated = load_ratings (self->store, season, &revision, &local);
    if (rated == NULL)
    {
        if (local != NULL)
            g_propagate_error (error, g_steal_pointer (&local));
        else
            _lrg_mmo_fail (error, G_IO_ERROR_NOT_FOUND, "Season is not rated");
        return NULL;
    }
    ratings = ratings_of (rated);
    return g_variant_ref_sink (ratings_variant (ratings));
}

gboolean
lrg_mmo_season_get_window (LrgMmoSeason *self, const gchar *season, gint64 *out_start,
                           gint64 *out_end, guint *out_capacity, gboolean *out_rated,
                           GError **error)
{
    g_autofree gchar *key = NULL;
    g_autoptr(GVariant) value = NULL;
    g_autoptr(GVariant) rated = NULL;
    g_autoptr(GError) local = NULL;
    guint64 revision;
    g_return_val_if_fail (LRG_IS_MMO_SEASON (self), FALSE);
    if (!_lrg_mmo_id_valid (season))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid season");
    key = g_strconcat ("season/", season, NULL);
    value = _lrg_mmo_load (self->store, key, "(xxua(suuu))", &revision, error);
    if (value == NULL)
        return FALSE;
    rated = load_ratings (self->store, season, &revision, &local);
    if (rated == NULL && local != NULL)
    {
        g_propagate_error (error, g_steal_pointer (&local));
        return FALSE;
    }
    if (out_start != NULL)
        g_variant_get_child (value, 0, "x", out_start);
    if (out_end != NULL)
        g_variant_get_child (value, 1, "x", out_end);
    if (out_capacity != NULL)
        g_variant_get_child (value, 2, "u", out_capacity);
    if (out_rated != NULL)
        *out_rated = rated != NULL;
    return TRUE;
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
