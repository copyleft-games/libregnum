/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-tag-board.h"
#include <math.h>
#include <string.h>

#define NAME_LIMIT (128)

typedef struct
{
    gdouble amount;
    gdouble presence;
    guint order;
} Contribution;

typedef struct
{
    GHashTable *by_account;     /* account -> Contribution* */
    gchar *first;
    gdouble total;
} Target;

struct _LrgMmoTagBoard
{
    GObject parent_instance;
    GHashTable *targets;        /* guint64 (boxed) -> Target* */
};

G_DEFINE_FINAL_TYPE (LrgMmoTagBoard, lrg_mmo_tag_board, G_TYPE_OBJECT)

static void
target_free (gpointer data)
{
    Target *target = data;

    g_hash_table_unref (target->by_account);
    g_free (target->first);
    g_free (target);
}

static void
lrg_mmo_tag_board_finalize (GObject *object)
{
    g_clear_pointer (&LRG_MMO_TAG_BOARD (object)->targets, g_hash_table_unref);
    G_OBJECT_CLASS (lrg_mmo_tag_board_parent_class)->finalize (object);
}

static void
lrg_mmo_tag_board_class_init (LrgMmoTagBoardClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_mmo_tag_board_finalize;
}

static void
lrg_mmo_tag_board_init (LrgMmoTagBoard *self)
{
    self->targets = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, target_free);
}

LrgMmoTagBoard *
lrg_mmo_tag_board_new (void)
{
    return g_object_new (LRG_TYPE_MMO_TAG_BOARD, NULL);
}

static gboolean
name_ok (const gchar *name)
{
    gsize len;

    if (name == NULL)
        return FALSE;
    len = strlen (name);
    return len > 0 && len <= NAME_LIMIT && g_utf8_validate (name, -1, NULL);
}

static Target *
target_get (LrgMmoTagBoard *self,
            guint64         id,
            gboolean        create)
{
    Target *target;
    gint64 key = (gint64) id;

    target = g_hash_table_lookup (self->targets, &key);
    if (target == NULL && create)
    {
        gint64 *copy = g_new (gint64, 1);

        *copy = key;
        target = g_new0 (Target, 1);
        target->by_account = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
        g_hash_table_replace (self->targets, copy, target);
    }
    return target;
}

/* the contribution of @account, made on first sight; NULL when full */
static Contribution *
contribution_get (Target      *target,
                  const gchar *account,
                  gboolean    *fresh)
{
    Contribution *c;

    c = g_hash_table_lookup (target->by_account, account);
    *fresh = FALSE;
    if (c == NULL)
    {
        if (g_hash_table_size (target->by_account) >= LRG_MMO_TAG_BOARD_MAX_CONTRIBUTORS)
            return NULL;
        c = g_new0 (Contribution, 1);
        c->order = g_hash_table_size (target->by_account);
        g_hash_table_replace (target->by_account, g_strdup (account), c);
        if (target->first == NULL)
            target->first = g_strdup (account);
        *fresh = TRUE;
    }
    return c;
}

gboolean
lrg_mmo_tag_board_tag (LrgMmoTagBoard *self,
                       guint64         target,
                       const gchar    *account,
                       gdouble         amount)
{
    Target *t;
    Contribution *c;
    gboolean fresh;

    g_return_val_if_fail (LRG_IS_MMO_TAG_BOARD (self), FALSE);
    if (!name_ok (account) || !isfinite (amount) || amount < 0.0)
        return FALSE;
    t = target_get (self, target, TRUE);
    c = contribution_get (t, account, &fresh);
    if (c == NULL)
        return FALSE;
    c->amount += amount;
    t->total += amount;
    return fresh;
}

gboolean
lrg_mmo_tag_board_add_presence (LrgMmoTagBoard *self,
                                guint64         target,
                                const gchar    *account,
                                gdouble         seconds)
{
    Target *t;
    Contribution *c;
    gboolean fresh;

    g_return_val_if_fail (LRG_IS_MMO_TAG_BOARD (self), FALSE);
    if (!name_ok (account) || !isfinite (seconds) || seconds <= 0.0)
        return FALSE;
    t = target_get (self, target, TRUE);
    c = contribution_get (t, account, &fresh);
    if (c == NULL)
        return FALSE;
    c->presence += seconds;
    return fresh;
}

gboolean
lrg_mmo_tag_board_is_tagged (LrgMmoTagBoard *self,
                             guint64         target,
                             const gchar    *account)
{
    Target *t;

    g_return_val_if_fail (LRG_IS_MMO_TAG_BOARD (self), FALSE);
    t = target_get (self, target, FALSE);
    return t != NULL && account != NULL && g_hash_table_contains (t->by_account, account);
}

const gchar *
lrg_mmo_tag_board_get_first (LrgMmoTagBoard *self,
                             guint64         target)
{
    Target *t;

    g_return_val_if_fail (LRG_IS_MMO_TAG_BOARD (self), NULL);
    t = target_get (self, target, FALSE);
    return t != NULL ? t->first : NULL;
}

gdouble
lrg_mmo_tag_board_get_amount (LrgMmoTagBoard *self,
                              guint64         target,
                              const gchar    *account)
{
    Target *t;
    Contribution *c;

    g_return_val_if_fail (LRG_IS_MMO_TAG_BOARD (self), 0.0);
    t = target_get (self, target, FALSE);
    c = t != NULL && account != NULL ? g_hash_table_lookup (t->by_account, account) : NULL;
    return c != NULL ? c->amount : 0.0;
}

gdouble
lrg_mmo_tag_board_get_presence (LrgMmoTagBoard *self,
                                guint64         target,
                                const gchar    *account)
{
    Target *t;
    Contribution *c;

    g_return_val_if_fail (LRG_IS_MMO_TAG_BOARD (self), 0.0);
    t = target_get (self, target, FALSE);
    c = t != NULL && account != NULL ? g_hash_table_lookup (t->by_account, account) : NULL;
    return c != NULL ? c->presence : 0.0;
}

guint
lrg_mmo_tag_board_get_count (LrgMmoTagBoard *self,
                             guint64         target)
{
    Target *t;

    g_return_val_if_fail (LRG_IS_MMO_TAG_BOARD (self), 0);
    t = target_get (self, target, FALSE);
    return t != NULL ? g_hash_table_size (t->by_account) : 0;
}

gdouble
lrg_mmo_tag_board_get_total (LrgMmoTagBoard *self,
                             guint64         target)
{
    Target *t;

    g_return_val_if_fail (LRG_IS_MMO_TAG_BOARD (self), 0.0);
    t = target_get (self, target, FALSE);
    return t != NULL ? t->total : 0.0;
}

typedef struct
{
    const gchar *account;
    Contribution *c;
} Row;

static gint
compare_rows (gconstpointer a,
              gconstpointer b)
{
    const Row *x = a;
    const Row *y = b;

    if (x->c->amount != y->c->amount)
        return x->c->amount > y->c->amount ? -1 : 1;
    return g_strcmp0 (x->account, y->account);
}

static GArray *
sorted_rows (Target *t)
{
    GArray *rows;
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    rows = g_array_new (FALSE, FALSE, sizeof (Row));
    if (t == NULL)
        return rows;
    g_hash_table_iter_init (&iter, t->by_account);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        Row row = { key, value };

        g_array_append_val (rows, row);
    }
    g_array_sort (rows, compare_rows);
    return rows;
}

GVariant *
lrg_mmo_tag_board_list (LrgMmoTagBoard *self,
                        guint64         target)
{
    GVariantBuilder builder;
    g_autoptr(GArray) rows = NULL;
    guint i;

    g_return_val_if_fail (LRG_IS_MMO_TAG_BOARD (self), NULL);
    rows = sorted_rows (target_get (self, target, FALSE));
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sdd)"));
    for (i = 0; i < rows->len; i++)
    {
        Row *row = &g_array_index (rows, Row, i);

        g_variant_builder_add (&builder, "(sdd)", row->account, row->c->amount, row->c->presence);
    }
    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

gchar **
lrg_mmo_tag_board_get_eligible (LrgMmoTagBoard *self,
                                guint64         target,
                                gdouble         min_share,
                                gdouble         min_presence)
{
    g_autoptr(GArray) rows = NULL;
    GPtrArray *out;
    Target *t;
    guint i;

    g_return_val_if_fail (LRG_IS_MMO_TAG_BOARD (self), NULL);
    t = target_get (self, target, FALSE);
    rows = sorted_rows (t);
    out = g_ptr_array_new ();
    for (i = 0; i < rows->len; i++)
    {
        Row *row = &g_array_index (rows, Row, i);
        gboolean by_damage;
        gboolean by_presence;

        by_damage = row->c->amount > 0.0 && t->total > 0.0 &&
                    row->c->amount / t->total >= min_share;
        by_presence = row->c->presence >= min_presence && row->c->presence > 0.0;
        if (by_damage || by_presence)
            g_ptr_array_add (out, g_strdup (row->account));
    }
    g_ptr_array_add (out, NULL);
    return (gchar **) g_ptr_array_free (out, FALSE);
}

gboolean
lrg_mmo_tag_board_clear (LrgMmoTagBoard *self,
                         guint64         target)
{
    gint64 key = (gint64) target;

    g_return_val_if_fail (LRG_IS_MMO_TAG_BOARD (self), FALSE);
    return g_hash_table_remove (self->targets, &key);
}

void
lrg_mmo_tag_board_clear_all (LrgMmoTagBoard *self)
{
    g_return_if_fail (LRG_IS_MMO_TAG_BOARD (self));
    g_hash_table_remove_all (self->targets);
}

guint
lrg_mmo_tag_board_get_target_count (LrgMmoTagBoard *self)
{
    g_return_val_if_fail (LRG_IS_MMO_TAG_BOARD (self), 0);
    return g_hash_table_size (self->targets);
}
