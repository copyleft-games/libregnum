/* lrg-quest-log.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "quest/lrg-quest-log.h"

struct _LrgQuestLog
{
    GObject        parent_instance;

    GHashTable    *active_quests;     /* quest_id -> LrgQuestInstance */
    GHashTable    *completed_quests;  /* quest_id -> LrgQuestInstance */
    LrgQuestInstance *tracked_quest;
};

G_DEFINE_TYPE (LrgQuestLog, lrg_quest_log, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_TRACKED_QUEST,
    PROP_ACTIVE_COUNT,
    PROP_COMPLETED_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_QUEST_STARTED,
    SIGNAL_QUEST_COMPLETED,
    SIGNAL_QUEST_FAILED,
    SIGNAL_QUEST_ABANDONED,
    SIGNAL_OBJECTIVE_UPDATED,
    SIGNAL_TRACKED_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
on_quest_state_changed (LrgQuestInstance *instance,
                        LrgQuestState     state,
                        gpointer          user_data)
{
    LrgQuestLog *self = LRG_QUEST_LOG (user_data);
    LrgQuestDef *def;
    const gchar *quest_id;

    def = lrg_quest_instance_get_quest_def (instance);
    quest_id = lrg_quest_def_get_id (def);

    if (state == LRG_QUEST_STATE_COMPLETE)
    {
        /* Move from active to completed */
        g_hash_table_steal (self->active_quests, quest_id);
        g_hash_table_replace (self->completed_quests,
                              g_strdup (quest_id),
                              g_object_ref (instance));

        g_signal_emit (self, signals[SIGNAL_QUEST_COMPLETED], 0, instance);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_COUNT]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMPLETED_COUNT]);

        /* Clear tracked if this was it */
        if (self->tracked_quest == instance)
            lrg_quest_log_set_tracked_quest (self, NULL);

        g_object_unref (instance);
    }
    else if (state == LRG_QUEST_STATE_FAILED)
    {
        g_signal_emit (self, signals[SIGNAL_QUEST_FAILED], 0, instance);
    }
}

static void
on_quest_objective_updated (LrgQuestInstance  *instance,
                            LrgQuestObjective *objective,
                            gpointer           user_data)
{
    LrgQuestLog *self = LRG_QUEST_LOG (user_data);
    g_signal_emit (self, signals[SIGNAL_OBJECTIVE_UPDATED], 0, instance, objective);
}

static void
lrg_quest_log_dispose (GObject *object)
{
    LrgQuestLog *self = LRG_QUEST_LOG (object);

    g_clear_object (&self->tracked_quest);

    G_OBJECT_CLASS (lrg_quest_log_parent_class)->dispose (object);
}

static void
lrg_quest_log_finalize (GObject *object)
{
    LrgQuestLog *self = LRG_QUEST_LOG (object);

    g_clear_pointer (&self->active_quests, g_hash_table_unref);
    g_clear_pointer (&self->completed_quests, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_quest_log_parent_class)->finalize (object);
}

static void
lrg_quest_log_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgQuestLog *self = LRG_QUEST_LOG (object);

    switch (prop_id)
    {
    case PROP_TRACKED_QUEST:
        g_value_set_object (value, self->tracked_quest);
        break;
    case PROP_ACTIVE_COUNT:
        g_value_set_uint (value, g_hash_table_size (self->active_quests));
        break;
    case PROP_COMPLETED_COUNT:
        g_value_set_uint (value, g_hash_table_size (self->completed_quests));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_quest_log_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgQuestLog *self = LRG_QUEST_LOG (object);

    switch (prop_id)
    {
    case PROP_TRACKED_QUEST:
        lrg_quest_log_set_tracked_quest (self, g_value_get_object (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_quest_log_class_init (LrgQuestLogClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_quest_log_dispose;
    object_class->finalize = lrg_quest_log_finalize;
    object_class->get_property = lrg_quest_log_get_property;
    object_class->set_property = lrg_quest_log_set_property;

    properties[PROP_TRACKED_QUEST] =
        g_param_spec_object ("tracked-quest", "Tracked Quest", "Currently tracked quest",
                             LRG_TYPE_QUEST_INSTANCE,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_ACTIVE_COUNT] =
        g_param_spec_uint ("active-count", "Active Count", "Number of active quests",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_COMPLETED_COUNT] =
        g_param_spec_uint ("completed-count", "Completed Count", "Number of completed quests",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgQuestLog::quest-started:
     * @self: the quest log
     * @quest: the started quest instance
     *
     * Emitted when a new quest is started.
     */
    signals[SIGNAL_QUEST_STARTED] =
        g_signal_new ("quest-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_QUEST_INSTANCE);

    /**
     * LrgQuestLog::quest-completed:
     * @self: the quest log
     * @quest: the completed quest instance
     *
     * Emitted when a quest is completed.
     */
    signals[SIGNAL_QUEST_COMPLETED] =
        g_signal_new ("quest-completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_QUEST_INSTANCE);

    /**
     * LrgQuestLog::quest-failed:
     * @self: the quest log
     * @quest: the failed quest instance
     *
     * Emitted when a quest is failed.
     */
    signals[SIGNAL_QUEST_FAILED] =
        g_signal_new ("quest-failed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_QUEST_INSTANCE);

    /**
     * LrgQuestLog::quest-abandoned:
     * @self: the quest log
     * @quest: the abandoned quest instance
     *
     * Emitted when a quest is abandoned.
     */
    signals[SIGNAL_QUEST_ABANDONED] =
        g_signal_new ("quest-abandoned",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_QUEST_INSTANCE);

    /**
     * LrgQuestLog::objective-updated:
     * @self: the quest log
     * @quest: the quest instance
     * @objective: the updated objective
     *
     * Emitted when an objective is updated.
     */
    signals[SIGNAL_OBJECTIVE_UPDATED] =
        g_signal_new ("objective-updated",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, LRG_TYPE_QUEST_INSTANCE, LRG_TYPE_QUEST_OBJECTIVE);

    /**
     * LrgQuestLog::tracked-changed:
     * @self: the quest log
     * @quest: (nullable): the newly tracked quest, or %NULL
     *
     * Emitted when the tracked quest changes.
     */
    signals[SIGNAL_TRACKED_CHANGED] =
        g_signal_new ("tracked-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_QUEST_INSTANCE);
}

static void
lrg_quest_log_init (LrgQuestLog *self)
{
    self->active_quests = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, g_object_unref);
    self->completed_quests = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                     g_free, g_object_unref);
    self->tracked_quest = NULL;
}

LrgQuestLog *
lrg_quest_log_new (void)
{
    return g_object_new (LRG_TYPE_QUEST_LOG, NULL);
}

LrgQuestInstance *
lrg_quest_log_start_quest (LrgQuestLog *self,
                           LrgQuestDef *quest_def)
{
    LrgQuestInstance *instance;
    const gchar      *quest_id;

    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), NULL);
    g_return_val_if_fail (LRG_IS_QUEST_DEF (quest_def), NULL);

    quest_id = lrg_quest_def_get_id (quest_def);

    /* Check if already active or completed */
    if (g_hash_table_contains (self->active_quests, quest_id) ||
        g_hash_table_contains (self->completed_quests, quest_id))
        return NULL;

    instance = lrg_quest_instance_new (quest_def);
    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_ACTIVE);

    /* Connect to instance signals */
    g_signal_connect (instance, "state-changed",
                      G_CALLBACK (on_quest_state_changed), self);
    g_signal_connect (instance, "objective-updated",
                      G_CALLBACK (on_quest_objective_updated), self);

    g_hash_table_replace (self->active_quests,
                          g_strdup (quest_id),
                          instance);

    g_signal_emit (self, signals[SIGNAL_QUEST_STARTED], 0, instance);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_COUNT]);

    return instance;
}

LrgQuestInstance *
lrg_quest_log_get_quest (LrgQuestLog *self,
                         const gchar *quest_id)
{
    LrgQuestInstance *instance;

    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), NULL);
    g_return_val_if_fail (quest_id != NULL, NULL);

    instance = g_hash_table_lookup (self->active_quests, quest_id);
    if (instance != NULL)
        return instance;

    return g_hash_table_lookup (self->completed_quests, quest_id);
}

GList *
lrg_quest_log_get_active_quests (LrgQuestLog *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), NULL);
    return g_hash_table_get_values (self->active_quests);
}

GList *
lrg_quest_log_get_completed_quests (LrgQuestLog *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), NULL);
    return g_hash_table_get_values (self->completed_quests);
}

gboolean
lrg_quest_log_is_quest_completed (LrgQuestLog *self,
                                  const gchar *quest_id)
{
    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), FALSE);
    g_return_val_if_fail (quest_id != NULL, FALSE);
    return g_hash_table_contains (self->completed_quests, quest_id);
}

gboolean
lrg_quest_log_is_quest_active (LrgQuestLog *self,
                               const gchar *quest_id)
{
    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), FALSE);
    g_return_val_if_fail (quest_id != NULL, FALSE);
    return g_hash_table_contains (self->active_quests, quest_id);
}

LrgQuestInstance *
lrg_quest_log_get_tracked_quest (LrgQuestLog *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), NULL);
    return self->tracked_quest;
}

void
lrg_quest_log_set_tracked_quest (LrgQuestLog      *self,
                                 LrgQuestInstance *quest)
{
    g_return_if_fail (LRG_IS_QUEST_LOG (self));
    g_return_if_fail (quest == NULL || LRG_IS_QUEST_INSTANCE (quest));

    if (self->tracked_quest == quest)
        return;

    /* Only allow tracking active quests */
    if (quest != NULL)
    {
        LrgQuestDef *def = lrg_quest_instance_get_quest_def (quest);
        const gchar *quest_id = lrg_quest_def_get_id (def);
        if (!g_hash_table_contains (self->active_quests, quest_id))
            return;
    }

    g_set_object (&self->tracked_quest, quest);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRACKED_QUEST]);
    g_signal_emit (self, signals[SIGNAL_TRACKED_CHANGED], 0, quest);
}

gboolean
lrg_quest_log_track_quest (LrgQuestLog *self,
                           const gchar *quest_id)
{
    LrgQuestInstance *instance;

    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), FALSE);
    g_return_val_if_fail (quest_id != NULL, FALSE);

    instance = g_hash_table_lookup (self->active_quests, quest_id);
    if (instance == NULL)
        return FALSE;

    lrg_quest_log_set_tracked_quest (self, instance);
    return TRUE;
}

gboolean
lrg_quest_log_abandon_quest (LrgQuestLog *self,
                             const gchar *quest_id)
{
    LrgQuestInstance *instance;

    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), FALSE);
    g_return_val_if_fail (quest_id != NULL, FALSE);

    instance = g_hash_table_lookup (self->active_quests, quest_id);
    if (instance == NULL)
        return FALSE;

    /* Clear tracking if this was the tracked quest */
    if (self->tracked_quest == instance)
        lrg_quest_log_set_tracked_quest (self, NULL);

    /* Emit before removal so handlers can access the quest */
    g_signal_emit (self, signals[SIGNAL_QUEST_ABANDONED], 0, instance);

    g_hash_table_remove (self->active_quests, quest_id);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_COUNT]);

    return TRUE;
}

guint
lrg_quest_log_get_active_count (LrgQuestLog *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), 0);
    return g_hash_table_size (self->active_quests);
}

guint
lrg_quest_log_get_completed_count (LrgQuestLog *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), 0);
    return g_hash_table_size (self->completed_quests);
}
