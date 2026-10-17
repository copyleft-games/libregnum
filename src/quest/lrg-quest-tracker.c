/* lrg-quest-tracker.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgQuestTracker: a GPtrArray of owned ids in tracking order (short:
 * linear lookups are cheaper than a second index at these sizes).
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "quest/lrg-quest-tracker.h"

#include <string.h>

struct _LrgQuestTracker
{
    GObject    parent_instance;

    GPtrArray *ids;          /* gchar *, tracking order */
    guint      max_tracked;
};

G_DEFINE_TYPE (LrgQuestTracker, lrg_quest_tracker, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_MAX_TRACKED,
    PROP_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static guint
clamp_limit (guint max_tracked)
{
    if (max_tracked == 0)
        return LRG_QUEST_TRACKER_DEFAULT_MAX;
    return MIN (max_tracked, (guint) LRG_QUEST_TRACKER_MAX_LIMIT);
}

/* announces a change: the signal, then the count's notify */
static void
changed (LrgQuestTracker *self,
         const gchar     *quest_id,
         gboolean         tracked)
{
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0, quest_id, tracked);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);
}

static void
lrg_quest_tracker_finalize (GObject *object)
{
    LrgQuestTracker *self = LRG_QUEST_TRACKER (object);

    g_ptr_array_unref (self->ids);
    G_OBJECT_CLASS (lrg_quest_tracker_parent_class)->finalize (object);
}

static void
lrg_quest_tracker_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgQuestTracker *self = LRG_QUEST_TRACKER (object);

    switch (prop_id)
    {
    case PROP_MAX_TRACKED:
        g_value_set_uint (value, self->max_tracked);
        break;
    case PROP_COUNT:
        g_value_set_uint (value, self->ids->len);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_quest_tracker_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgQuestTracker *self = LRG_QUEST_TRACKER (object);

    switch (prop_id)
    {
    case PROP_MAX_TRACKED:
        lrg_quest_tracker_set_max_tracked (self, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_quest_tracker_class_init (LrgQuestTrackerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_quest_tracker_finalize;
    object_class->get_property = lrg_quest_tracker_get_property;
    object_class->set_property = lrg_quest_tracker_set_property;

    /**
     * LrgQuestTracker:max-tracked:
     *
     * How many quests may be tracked at once.
     */
    properties[PROP_MAX_TRACKED] =
        g_param_spec_uint ("max-tracked", "Max tracked", "How many quests may be tracked",
                           1, LRG_QUEST_TRACKER_MAX_LIMIT, LRG_QUEST_TRACKER_DEFAULT_MAX,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgQuestTracker:count:
     *
     * How many quests are tracked.
     */
    properties[PROP_COUNT] =
        g_param_spec_uint ("count", "Count", "How many quests are tracked",
                           0, LRG_QUEST_TRACKER_MAX_LIMIT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_properties (object_class, N_PROPS, properties);

    signals[SIGNAL_CHANGED] =
        g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
                      NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_BOOLEAN);
}

static void
lrg_quest_tracker_init (LrgQuestTracker *self)
{
    self->ids = g_ptr_array_new_with_free_func (g_free);
    self->max_tracked = LRG_QUEST_TRACKER_DEFAULT_MAX;
}

LrgQuestTracker *
lrg_quest_tracker_new (guint max_tracked)
{
    return (LrgQuestTracker *) g_object_new (LRG_TYPE_QUEST_TRACKER,
                                             "max-tracked", clamp_limit (max_tracked),
                                             NULL);
}

guint
lrg_quest_tracker_get_max_tracked (LrgQuestTracker *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_TRACKER (self), 0);

    return self->max_tracked;
}

void
lrg_quest_tracker_set_max_tracked (LrgQuestTracker *self,
                                   guint            max_tracked)
{
    gboolean dropped;

    g_return_if_fail (LRG_IS_QUEST_TRACKER (self));

    max_tracked = MAX (1u, MIN (max_tracked, (guint) LRG_QUEST_TRACKER_MAX_LIMIT));
    if (max_tracked == self->max_tracked)
        return;
    self->max_tracked = max_tracked;
    /* the latest tracked go first */
    dropped = self->ids->len > max_tracked;
    if (dropped)
        g_ptr_array_set_size (self->ids, max_tracked);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_TRACKED]);
    if (dropped)
        changed (self, NULL, FALSE);
}

gint
lrg_quest_tracker_get_index (LrgQuestTracker *self,
                             const gchar     *quest_id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_QUEST_TRACKER (self), -1);

    if (quest_id == NULL)
        return -1;
    for (i = 0; i < self->ids->len; i++)
    {
        if (strcmp ((const gchar *) g_ptr_array_index (self->ids, i), quest_id) == 0)
            return (gint) i;
    }
    return -1;
}

gboolean
lrg_quest_tracker_is_tracked (LrgQuestTracker *self,
                              const gchar     *quest_id)
{
    g_return_val_if_fail (LRG_IS_QUEST_TRACKER (self), FALSE);

    return lrg_quest_tracker_get_index (self, quest_id) >= 0;
}

gboolean
lrg_quest_tracker_track (LrgQuestTracker *self,
                         const gchar     *quest_id)
{
    g_return_val_if_fail (LRG_IS_QUEST_TRACKER (self), FALSE);

    if (quest_id == NULL || quest_id[0] == '\0' ||
        lrg_quest_tracker_get_index (self, quest_id) >= 0 ||
        self->ids->len >= self->max_tracked)
        return FALSE;
    g_ptr_array_add (self->ids, g_strdup (quest_id));
    changed (self, quest_id, TRUE);
    return TRUE;
}

gboolean
lrg_quest_tracker_untrack (LrgQuestTracker *self,
                           const gchar     *quest_id)
{
    g_autofree gchar *id = NULL;
    gint index;

    g_return_val_if_fail (LRG_IS_QUEST_TRACKER (self), FALSE);

    index = lrg_quest_tracker_get_index (self, quest_id);
    if (index < 0)
        return FALSE;
    /* the caller's string may be the stored one: keep a copy for the signal */
    id = g_strdup (quest_id);
    g_ptr_array_remove_index (self->ids, (guint) index);
    changed (self, id, FALSE);
    return TRUE;
}

gboolean
lrg_quest_tracker_set_tracked (LrgQuestTracker *self,
                               const gchar     *quest_id,
                               gboolean         tracked)
{
    g_return_val_if_fail (LRG_IS_QUEST_TRACKER (self), FALSE);

    return tracked ? lrg_quest_tracker_track (self, quest_id)
                   : lrg_quest_tracker_untrack (self, quest_id);
}

gboolean
lrg_quest_tracker_move (LrgQuestTracker *self,
                        const gchar     *quest_id,
                        guint            index)
{
    gpointer id;
    gint from;

    g_return_val_if_fail (LRG_IS_QUEST_TRACKER (self), FALSE);

    from = lrg_quest_tracker_get_index (self, quest_id);
    if (from < 0)
        return FALSE;
    index = MIN (index, self->ids->len - 1);
    if ((guint) from == index)
        return TRUE;
    id = g_ptr_array_steal_index (self->ids, (guint) from);
    g_ptr_array_insert (self->ids, (gint) index, id);
    changed (self, NULL, FALSE);
    return TRUE;
}

guint
lrg_quest_tracker_get_count (LrgQuestTracker *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_TRACKER (self), 0);

    return self->ids->len;
}

gchar **
lrg_quest_tracker_get_ids (LrgQuestTracker *self)
{
    gchar **ids;
    guint i;

    g_return_val_if_fail (LRG_IS_QUEST_TRACKER (self), NULL);

    ids = g_new0 (gchar *, self->ids->len + 1);
    for (i = 0; i < self->ids->len; i++)
        ids[i] = g_strdup ((const gchar *) g_ptr_array_index (self->ids, i));
    return ids;
}

void
lrg_quest_tracker_clear (LrgQuestTracker *self)
{
    g_return_if_fail (LRG_IS_QUEST_TRACKER (self));

    if (self->ids->len == 0)
        return;
    g_ptr_array_set_size (self->ids, 0);
    changed (self, NULL, FALSE);
}

GVariant *
lrg_quest_tracker_to_variant (LrgQuestTracker *self)
{
    GVariantBuilder builder;
    guint i;

    g_return_val_if_fail (LRG_IS_QUEST_TRACKER (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE_STRING_ARRAY);
    for (i = 0; i < self->ids->len; i++)
        g_variant_builder_add (&builder, "s", (const gchar *) g_ptr_array_index (self->ids, i));
    return g_variant_builder_end (&builder);
}

gboolean
lrg_quest_tracker_from_variant (LrgQuestTracker  *self,
                                GVariant         *variant,
                                GError          **error)
{
    g_autoptr(GPtrArray) ids = NULL;
    GVariantIter iter;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_QUEST_TRACKER (self), FALSE);
    g_return_val_if_fail (variant != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE_STRING_ARRAY))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "quest tracker: expected \"as\", got \"%s\"",
                     g_variant_get_type_string (variant));
        return FALSE;
    }

    /* --- staged: empty and repeated ids skipped, the limit kept --- */
    ids = g_ptr_array_new_with_free_func (g_free);
    g_variant_iter_init (&iter, variant);
    while (g_variant_iter_next (&iter, "&s", &id))
    {
        gboolean seen;
        guint i;

        if (id[0] == '\0' || ids->len >= self->max_tracked)
            continue;
        seen = FALSE;
        for (i = 0; i < ids->len && !seen; i++)
            seen = strcmp ((const gchar *) g_ptr_array_index (ids, i), id) == 0;
        if (!seen)
            g_ptr_array_add (ids, g_strdup (id));
    }

    g_ptr_array_unref (self->ids);
    self->ids = g_steal_pointer (&ids);
    changed (self, NULL, FALSE);
    return TRUE;
}
