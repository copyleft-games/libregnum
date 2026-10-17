/* lrg-feed.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFeed: a GPtrArray of owned rows, oldest first.  Each row has its
 * own time left; merging adds to the count, restarts the time and moves
 * the row to the end so the newest news is always last.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "hud/lrg-feed.h"

#include <math.h>

#define MAX_ROWS_LIMIT (64)

typedef struct
{
    gchar  *key;
    gchar  *label;
    gint64  count;
    gfloat  left;
} Row;

struct _LrgFeed
{
    GObject    parent_instance;

    GPtrArray *rows;      /* Row *, oldest first */
    guint      max_rows;
    gfloat     seconds;
};

G_DEFINE_TYPE (LrgFeed, lrg_feed, G_TYPE_OBJECT)

enum
{
    SIGNAL_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
row_free (gpointer data)
{
    Row *row = data;

    g_free (row->key);
    g_free (row->label);
    g_free (row);
}

static void
lrg_feed_finalize (GObject *object)
{
    LrgFeed *self = LRG_FEED (object);

    g_ptr_array_unref (self->rows);
    G_OBJECT_CLASS (lrg_feed_parent_class)->finalize (object);
}

static void
lrg_feed_class_init (LrgFeedClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_feed_finalize;

    signals[SIGNAL_CHANGED] = g_signal_new ("changed", G_TYPE_FROM_CLASS (klass),
                                            G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                            G_TYPE_NONE, 0);
}

static void
lrg_feed_init (LrgFeed *self)
{
    self->rows = g_ptr_array_new_with_free_func (row_free);
    self->max_rows = 6;
    self->seconds = LRG_FEED_DEFAULT_SECONDS;
}

LrgFeed *
lrg_feed_new (guint  max_rows,
              gfloat seconds)
{
    LrgFeed *self;

    self = g_object_new (LRG_TYPE_FEED, NULL);
    self->max_rows = CLAMP (max_rows, 1u, (guint) MAX_ROWS_LIMIT);
    self->seconds = seconds > 0.0f && isfinite (seconds) ? seconds : LRG_FEED_DEFAULT_SECONDS;
    return self;
}

static Row *
row_at (LrgFeed *self,
        guint    index)
{
    return index < self->rows->len ? g_ptr_array_index (self->rows, index) : NULL;
}

gint
lrg_feed_push (LrgFeed     *self,
               const gchar *key,
               const gchar *label,
               gint64       count)
{
    Row *row;
    guint i;

    g_return_val_if_fail (LRG_IS_FEED (self), -1);

    if (label == NULL || *label == '\0')
        return -1;

    /* --- a shown row of the same key takes the news --- */
    row = NULL;
    for (i = 0; key != NULL && i < self->rows->len; i++)
    {
        Row *candidate = g_ptr_array_index (self->rows, i);

        if (g_strcmp0 (candidate->key, key) == 0)
        {
            row = g_ptr_array_steal_index (self->rows, i);
            break;
        }
    }
    if (row != NULL)
    {
        row->count += count;
        g_free (row->label);
        row->label = g_strdup (label);
    }
    else
    {
        while (self->rows->len >= self->max_rows)
            g_ptr_array_remove_index (self->rows, 0);
        row = g_new0 (Row, 1);
        row->key = g_strdup (key);
        row->label = g_strdup (label);
        row->count = count;
    }
    row->left = self->seconds;
    g_ptr_array_add (self->rows, row);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
    return (gint) self->rows->len - 1;
}

guint
lrg_feed_update (LrgFeed *self,
                 gfloat   delta)
{
    guint dropped;
    guint i;

    g_return_val_if_fail (LRG_IS_FEED (self), 0);

    if (!(delta > 0.0f) || !isfinite (delta))
        return 0;
    dropped = 0;
    i = 0;
    while (i < self->rows->len)
    {
        Row *row = g_ptr_array_index (self->rows, i);

        row->left -= delta;
        if (row->left <= 0.0f)
        {
            g_ptr_array_remove_index (self->rows, i);
            dropped++;
            continue;
        }
        i++;
    }
    if (dropped > 0)
        g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
    return dropped;
}

guint
lrg_feed_get_count (LrgFeed *self)
{
    g_return_val_if_fail (LRG_IS_FEED (self), 0);
    return self->rows->len;
}

const gchar *
lrg_feed_get_label (LrgFeed *self,
                    guint    index)
{
    Row *row;

    g_return_val_if_fail (LRG_IS_FEED (self), NULL);
    row = row_at (self, index);
    return row != NULL ? row->label : NULL;
}

const gchar *
lrg_feed_get_key (LrgFeed *self,
                  guint    index)
{
    Row *row;

    g_return_val_if_fail (LRG_IS_FEED (self), NULL);
    row = row_at (self, index);
    return row != NULL ? row->key : NULL;
}

gint64
lrg_feed_get_row_count (LrgFeed *self,
                        guint    index)
{
    Row *row;

    g_return_val_if_fail (LRG_IS_FEED (self), 0);
    row = row_at (self, index);
    return row != NULL ? row->count : 0;
}

gfloat
lrg_feed_get_alpha (LrgFeed *self,
                    guint    index)
{
    Row *row;

    g_return_val_if_fail (LRG_IS_FEED (self), 0.0f);
    row = row_at (self, index);
    if (row == NULL)
        return 0.0f;
    return CLAMP (row->left / MIN (LRG_FEED_FADE_SECONDS, self->seconds), 0.0f, 1.0f);
}

void
lrg_feed_clear (LrgFeed *self)
{
    g_return_if_fail (LRG_IS_FEED (self));

    if (self->rows->len == 0)
        return;
    g_ptr_array_set_size (self->rows, 0);
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}
