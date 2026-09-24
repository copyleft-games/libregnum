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

#include <string.h>

/* Longest accepted quest ID in bytes. */
#define LRG_QUEST_LOG_MAX_ID_LENGTH (128)

/*
 * LrgQuestCompletion:
 *
 * Completion record kept for every quest completed at least once.
 */
typedef struct
{
    guint  count;  /* number of completions, >= 1 */
    gint64 last;   /* Unix seconds of the last completion, 0 if unknown */
} LrgQuestCompletion;

struct _LrgQuestLog
{
    GObject        parent_instance;

    GHashTable    *active_quests;     /* quest_id -> LrgQuestInstance */
    GHashTable    *completed_quests;  /* quest_id -> LrgQuestInstance (latest completed) */
    GHashTable    *records;           /* quest_id -> LrgQuestCompletion */
    GHashTable    *turn_in;           /* set of active quest_ids that wait for turn-in */
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
    SIGNAL_QUEST_READY,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/*
 * quest_id_is_valid:
 * @quest_id: (nullable): candidate ID
 *
 * Returns: whether @quest_id is non-empty UTF-8 of at most 128 bytes
 */
static gboolean
quest_id_is_valid (const gchar *quest_id)
{
    gsize len;

    if (quest_id == NULL || quest_id[0] == '\0')
        return FALSE;
    len = strnlen (quest_id, LRG_QUEST_LOG_MAX_ID_LENGTH + 1);
    if (len > LRG_QUEST_LOG_MAX_ID_LENGTH)
        return FALSE;
    return g_utf8_validate (quest_id, (gssize)len, NULL);
}

/*
 * record_completion:
 * @self: log
 * @quest_id: completed quest
 * @set_last: whether @last is known
 * @last: Unix seconds of this completion
 *
 * Bumps (saturating) or creates the completion record of @quest_id.
 */
static void
record_completion (LrgQuestLog *self,
                   const gchar *quest_id,
                   gboolean     set_last,
                   gint64       last)
{
    LrgQuestCompletion *record;

    record = g_hash_table_lookup (self->records, quest_id);
    if (record == NULL)
    {
        record = g_new0 (LrgQuestCompletion, 1);
        g_hash_table_replace (self->records, g_strdup (quest_id), record);
    }
    if (record->count < G_MAXUINT)
        record->count++;
    if (set_last)
        record->last = last;
}

/*
 * repeat_to_period:
 * @repeat: a repeatable cadence
 *
 * Returns: the reset period that governs @repeat
 */
static LrgResetPeriod
repeat_to_period (LrgQuestRepeat repeat)
{
    if (repeat == LRG_QUEST_REPEAT_WEEKLY)
        return LRG_RESET_PERIOD_WEEKLY;
    return LRG_RESET_PERIOD_DAILY;
}

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

    /* Only the instance currently active under this ID may drive the log. */
    if (g_hash_table_lookup (self->active_quests, quest_id) != instance)
        return;

    if (state == LRG_QUEST_STATE_COMPLETE &&
        g_hash_table_contains (self->turn_in, quest_id))
    {
        /* Checked quests wait in the active set until lrg_quest_log_turn_in(). */
        g_signal_emit (self, signals[SIGNAL_QUEST_READY], 0, instance);
    }
    else if (state == LRG_QUEST_STATE_COMPLETE)
    {
        /* Move from active to completed. Hold a reference across the remove
         * (which frees the active-quests key via g_free and drops the table's
         * reference), then transfer that reference to completed_quests. Using
         * g_hash_table_steal here would orphan the strdup'd key. */
        g_object_ref (instance);
        g_hash_table_remove (self->active_quests, quest_id);
        g_hash_table_replace (self->completed_quests,
                              g_strdup (quest_id),
                              instance);
        /* Legacy completions have no trusted timestamp. */
        record_completion (self, quest_id, FALSE, 0);

        g_signal_emit (self, signals[SIGNAL_QUEST_COMPLETED], 0, instance);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_COUNT]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMPLETED_COUNT]);

        /* Clear tracked if this was it */
        if (self->tracked_quest == instance)
            lrg_quest_log_set_tracked_quest (self, NULL);
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
    g_clear_pointer (&self->records, g_hash_table_unref);
    g_clear_pointer (&self->turn_in, g_hash_table_unref);

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
        g_value_set_uint (value, g_hash_table_size (self->records));
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

    /**
     * LrgQuestLog::quest-ready:
     * @self: the quest log
     * @quest: the quest instance that finished its last stage
     *
     * Emitted when a quest started with lrg_quest_log_start_quest_checked()
     * finishes its last stage and is ready for lrg_quest_log_turn_in().
     */
    signals[SIGNAL_QUEST_READY] =
        g_signal_new ("quest-ready",
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
    self->records = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, g_free);
    self->turn_in = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, NULL);
    self->tracked_quest = NULL;
}

LrgQuestLog *
lrg_quest_log_new (void)
{
    return g_object_new (LRG_TYPE_QUEST_LOG, NULL);
}

/*
 * start_internal:
 * @self: log
 * @quest_def: definition
 * @turn_in: whether completion waits for lrg_quest_log_turn_in()
 * @stage: stage to restore, or G_MAXUINT for a fresh start
 * @progress: (nullable): objective progress of @stage to restore
 * @emit: whether to emit quest-started
 *
 * Creates, registers and connects a new active instance.
 *
 * Returns: (transfer none): the instance
 */
static LrgQuestInstance *
start_internal (LrgQuestLog *self,
                LrgQuestDef *quest_def,
                gboolean     turn_in,
                guint        stage,
                GVariant    *progress,
                gboolean     emit)
{
    LrgQuestInstance *instance;
    const gchar      *quest_id;

    quest_id = lrg_quest_def_get_id (quest_def);

    instance = lrg_quest_instance_new (quest_def);

    /* Restore the stage and its objective progress before going active. */
    if (stage != G_MAXUINT)
    {
        gsize i;

        lrg_quest_instance_set_stage (instance, stage);
        for (i = 0; progress != NULL && i < g_variant_n_children (progress); i++)
        {
            guint32 value;
            g_variant_get_child (progress, i, "u", &value);
            lrg_quest_instance_set_objective_progress (instance, (guint)i, value);
        }
    }

    lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_ACTIVE);
    if (stage != G_MAXUINT && stage >= lrg_quest_def_get_stage_count (quest_def))
        lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_COMPLETE);

    if (turn_in)
        g_hash_table_add (self->turn_in, g_strdup (quest_id));

    /* Connect to instance signals */
    g_signal_connect_object (instance, "state-changed",
                             G_CALLBACK (on_quest_state_changed), self, 0);
    g_signal_connect_object (instance, "objective-updated",
                             G_CALLBACK (on_quest_objective_updated), self, 0);

    g_hash_table_replace (self->active_quests,
                          g_strdup (quest_id),
                          instance);

    if (emit)
    {
        g_signal_emit (self, signals[SIGNAL_QUEST_STARTED], 0, instance);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_COUNT]);
    }

    return instance;
}

LrgQuestInstance *
lrg_quest_log_start_quest (LrgQuestLog *self,
                           LrgQuestDef *quest_def)
{
    const gchar *quest_id;

    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), NULL);
    g_return_val_if_fail (LRG_IS_QUEST_DEF (quest_def), NULL);

    quest_id = lrg_quest_def_get_id (quest_def);
    g_return_val_if_fail (quest_id != NULL, NULL);

    /* Check if already active or completed */
    if (g_hash_table_contains (self->active_quests, quest_id) ||
        g_hash_table_contains (self->records, quest_id))
        return NULL;

    return start_internal (self, quest_def, FALSE, G_MAXUINT, NULL, TRUE);
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
    return g_hash_table_contains (self->records, quest_id);
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
        if (g_hash_table_lookup (self->active_quests, quest_id) != quest)
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

    /* Retained instances must stop updating this log once abandoned. */
    g_signal_handlers_disconnect_by_data (instance, self);

    /* Clear tracking if this was the tracked quest */
    if (self->tracked_quest == instance)
        lrg_quest_log_set_tracked_quest (self, NULL);

    /* Emit before removal so handlers can access the quest */
    g_signal_emit (self, signals[SIGNAL_QUEST_ABANDONED], 0, instance);

    g_hash_table_remove (self->turn_in, quest_id);
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
    return g_hash_table_size (self->records);
}

gboolean
lrg_quest_log_can_start (LrgQuestLog      *self,
                         LrgQuestDef      *quest_def,
                         guint             level,
                         LrgResetSchedule *schedule,
                         gint64            now,
                         GError          **error)
{
    const gchar        *quest_id;
    GPtrArray          *ids;
    LrgQuestCompletion *record;
    LrgQuestRepeat      repeat;
    guint               i;

    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), FALSE);
    g_return_val_if_fail (LRG_IS_QUEST_DEF (quest_def), FALSE);
    g_return_val_if_fail (schedule == NULL || LRG_IS_RESET_SCHEDULE (schedule), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    quest_id = lrg_quest_def_get_id (quest_def);
    if (!quest_id_is_valid (quest_id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Quest definition has an invalid ID");
        return FALSE;
    }

    /* 1. Already active. */
    if (g_hash_table_contains (self->active_quests, quest_id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_DUPLICATE,
                     "Quest '%s' is already active", quest_id);
        return FALSE;
    }

    /* 2. Level requirement. */
    if (level < lrg_quest_def_get_min_level (quest_def))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Quest '%s' requires level %u", quest_id,
                     lrg_quest_def_get_min_level (quest_def));
        return FALSE;
    }

    /* 3. Prerequisites, in authoring order. */
    ids = lrg_quest_def_get_prerequisites (quest_def);
    for (i = 0; i < ids->len; i++)
    {
        const gchar *prereq = g_ptr_array_index (ids, i);
        if (!g_hash_table_contains (self->records, prereq))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                         "Quest '%s' requires %s", quest_id, prereq);
            return FALSE;
        }
    }

    /* 4. Mutually exclusive quests. */
    ids = lrg_quest_def_get_exclusives (quest_def);
    for (i = 0; i < ids->len; i++)
    {
        const gchar *other = g_ptr_array_index (ids, i);
        if (g_hash_table_contains (self->active_quests, other) ||
            g_hash_table_contains (self->records, other))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                         "Quest '%s' is exclusive with %s", quest_id, other);
            return FALSE;
        }
    }

    /* 5 and 6. Completion history and repeat cadence. */
    record = g_hash_table_lookup (self->records, quest_id);
    if (record != NULL)
    {
        LrgResetPeriod period;

        repeat = lrg_quest_def_get_repeat (quest_def);
        if (repeat == LRG_QUEST_REPEAT_NONE)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_DUPLICATE,
                         "Quest '%s' was already completed", quest_id);
            return FALSE;
        }
        if (schedule == NULL)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Repeatable quest '%s' needs a reset schedule", quest_id);
            return FALSE;
        }

        /* Available again only once a later period than the last completion began. */
        period = repeat_to_period (repeat);
        if (lrg_reset_schedule_get_period (schedule, period, record->last) >=
            lrg_reset_schedule_get_period (schedule, period, now))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_READY,
                         "Quest '%s' was already completed this %s", quest_id,
                         period == LRG_RESET_PERIOD_WEEKLY ? "week" : "day");
            return FALSE;
        }
    }

    /* 7. Snapshot capacity, so every reachable log can be persisted. */
    if (g_hash_table_size (self->active_quests) >= LRG_QUEST_LOG_MAX_ACTIVE)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT,
                     "Quest log already holds %u active quests", LRG_QUEST_LOG_MAX_ACTIVE);
        return FALSE;
    }
    if (record == NULL && g_hash_table_size (self->records) >= LRG_QUEST_LOG_MAX_COMPLETED)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT,
                     "Quest log already holds %u completion records",
                     LRG_QUEST_LOG_MAX_COMPLETED);
        return FALSE;
    }

    return TRUE;
}

LrgQuestInstance *
lrg_quest_log_start_quest_checked (LrgQuestLog      *self,
                                   LrgQuestDef      *quest_def,
                                   guint             level,
                                   LrgResetSchedule *schedule,
                                   gint64            now,
                                   GError          **error)
{
    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), NULL);
    g_return_val_if_fail (LRG_IS_QUEST_DEF (quest_def), NULL);
    g_return_val_if_fail (schedule == NULL || LRG_IS_RESET_SCHEDULE (schedule), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!lrg_quest_log_can_start (self, quest_def, level, schedule, now, error))
        return NULL;

    /* The completion record (if any) is kept as history. */
    return start_internal (self, quest_def, TRUE, G_MAXUINT, NULL, TRUE);
}

gboolean
lrg_quest_log_turn_in (LrgQuestLog *self,
                       const gchar *quest_id,
                       gint64       now,
                       GError     **error)
{
    LrgQuestInstance *instance;
    gchar            *key;

    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), FALSE);
    g_return_val_if_fail (quest_id != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    instance = g_hash_table_lookup (self->active_quests, quest_id);
    if (instance == NULL)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND,
                     "Quest '%s' is not active", quest_id);
        return FALSE;
    }
    if (lrg_quest_instance_get_state (instance) != LRG_QUEST_STATE_COMPLETE)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_REQUIREMENT,
                     "Quest '%s' is not complete", quest_id);
        return FALSE;
    }

    /*
     * Copy the ID first: @quest_id may be borrowed from the active table
     * key or the definition, both of which can go away below.
     */
    key = g_strdup (quest_id);

    /* A turned-in instance no longer drives this log. */
    g_signal_handlers_disconnect_by_data (instance, self);

    if (self->tracked_quest == instance)
        lrg_quest_log_set_tracked_quest (self, NULL);

    /* Move the reference from the active table to the completed table. */
    g_object_ref (instance);
    g_hash_table_remove (self->turn_in, key);
    g_hash_table_remove (self->active_quests, key);
    g_hash_table_replace (self->completed_quests, g_strdup (key), instance);
    record_completion (self, key, TRUE, now);

    g_signal_emit (self, signals[SIGNAL_QUEST_COMPLETED], 0, instance);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_COUNT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMPLETED_COUNT]);

    g_free (key);
    return TRUE;
}

guint
lrg_quest_log_get_completion_count (LrgQuestLog *self,
                                    const gchar *quest_id)
{
    LrgQuestCompletion *record;

    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), 0);
    g_return_val_if_fail (quest_id != NULL, 0);

    record = g_hash_table_lookup (self->records, quest_id);
    return record != NULL ? record->count : 0;
}

gint64
lrg_quest_log_get_last_completed (LrgQuestLog *self,
                                  const gchar *quest_id)
{
    LrgQuestCompletion *record;

    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), 0);
    g_return_val_if_fail (quest_id != NULL, 0);

    record = g_hash_table_lookup (self->records, quest_id);
    return record != NULL ? record->last : 0;
}

static gint
compare_str_ptr (gconstpointer a,
                 gconstpointer b)
{
    return g_strcmp0 (*(const gchar * const *)a, *(const gchar * const *)b);
}

/*
 * sorted_keys:
 * @table: string-keyed table
 *
 * Returns: (transfer container): borrowed keys sorted with g_strcmp0()
 */
static GPtrArray *
sorted_keys (GHashTable *table)
{
    GPtrArray      *keys;
    GHashTableIter  iter;
    gpointer        key;

    keys = g_ptr_array_sized_new (g_hash_table_size (table));
    g_hash_table_iter_init (&iter, table);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        g_ptr_array_add (keys, key);
    g_ptr_array_sort (keys, compare_str_ptr);
    return keys;
}

GPtrArray *
lrg_quest_log_get_completed_ids (LrgQuestLog *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), NULL);
    return sorted_keys (self->records);
}

guint
lrg_quest_log_count_completed_in_period (LrgQuestLog      *self,
                                         const gchar      *category,
                                         LrgResetSchedule *schedule,
                                         LrgResetPeriod    period,
                                         gint64            now,
                                         GHashTable       *defs)
{
    GHashTableIter  iter;
    gpointer        key;
    gpointer        value;
    gint64          current;
    guint           count;

    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), 0);
    g_return_val_if_fail (LRG_IS_RESET_SCHEDULE (schedule), 0);
    g_return_val_if_fail (period == LRG_RESET_PERIOD_DAILY ||
                          period == LRG_RESET_PERIOD_WEEKLY, 0);
    g_return_val_if_fail (defs != NULL, 0);

    current = lrg_reset_schedule_get_period (schedule, period, now);
    count = 0;

    g_hash_table_iter_init (&iter, self->records);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        LrgQuestCompletion *record = value;
        LrgQuestDef        *def = g_hash_table_lookup (defs, key);

        /* Only repeatable quests with a known completion time count. */
        if (def == NULL || !LRG_IS_QUEST_DEF (def))
            continue;
        if (lrg_quest_def_get_repeat (def) == LRG_QUEST_REPEAT_NONE)
            continue;
        if (category != NULL && g_strcmp0 (lrg_quest_def_get_category (def), category) != 0)
            continue;
        if (record->last == 0)
            continue;
        if (lrg_reset_schedule_get_period (schedule, period, record->last) == current)
            count++;
    }

    return count;
}

void
lrg_quest_log_mark_completed (LrgQuestLog *self,
                              const gchar *quest_id,
                              guint        count,
                              gint64       last)
{
    LrgQuestCompletion *record;
    gboolean            added;

    g_return_if_fail (LRG_IS_QUEST_LOG (self));
    g_return_if_fail (quest_id_is_valid (quest_id));
    g_return_if_fail (count >= 1);

    record = g_hash_table_lookup (self->records, quest_id);
    added = (record == NULL);
    if (added)
    {
        record = g_new0 (LrgQuestCompletion, 1);
        g_hash_table_replace (self->records, g_strdup (quest_id), record);
    }
    record->count = count;
    record->last = last;

    if (added)
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMPLETED_COUNT]);
}

GVariant *
lrg_quest_log_to_variant (LrgQuestLog *self)
{
    GVariantBuilder  builder;
    GPtrArray       *keys;
    guint            i;

    g_return_val_if_fail (LRG_IS_QUEST_LOG (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE (LRG_QUEST_LOG_VARIANT_TYPE));

    /* Active quests: (id, stage, progress of every objective of the stage). */
    g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(suau)"));
    keys = sorted_keys (self->active_quests);
    for (i = 0; i < keys->len; i++)
    {
        const gchar      *quest_id = g_ptr_array_index (keys, i);
        LrgQuestInstance *instance = g_hash_table_lookup (self->active_quests, quest_id);
        LrgQuestDef      *def = lrg_quest_instance_get_quest_def (instance);
        guint             n_stages = lrg_quest_def_get_stage_count (def);
        guint             stage;
        guint             j;

        /* A complete instance is persisted as "every stage done". */
        stage = lrg_quest_instance_get_current_stage (instance);
        if (lrg_quest_instance_get_state (instance) == LRG_QUEST_STATE_COMPLETE || stage > n_stages)
            stage = n_stages;

        g_variant_builder_open (&builder, G_VARIANT_TYPE ("(suau)"));
        g_variant_builder_add (&builder, "s", quest_id);
        g_variant_builder_add (&builder, "u", (guint32)stage);
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("au"));
        if (stage < n_stages)
        {
            guint n_objectives = MIN (lrg_quest_instance_get_objective_count (instance),
                                      (guint)LRG_QUEST_MAX_STAGE_OBJECTIVES);
            for (j = 0; j < n_objectives; j++)
            {
                LrgQuestObjective *obj = lrg_quest_instance_get_objective (instance, j);
                guint value = MIN (lrg_quest_objective_get_current_count (obj),
                                   lrg_quest_objective_get_target_count (obj));
                g_variant_builder_add (&builder, "u", (guint32)value);
            }
        }
        g_variant_builder_close (&builder);
        g_variant_builder_close (&builder);
    }
    g_ptr_array_unref (keys);
    g_variant_builder_close (&builder);

    /* Completion records: (id, count, last). */
    g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(sux)"));
    keys = sorted_keys (self->records);
    for (i = 0; i < keys->len; i++)
    {
        const gchar        *quest_id = g_ptr_array_index (keys, i);
        LrgQuestCompletion *record = g_hash_table_lookup (self->records, quest_id);

        g_variant_builder_add (&builder, "(sux)", quest_id,
                               (guint32)record->count, (gint64)record->last);
    }
    g_ptr_array_unref (keys);
    g_variant_builder_close (&builder);

    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/*
 * lookup_def:
 * @defs: definitions by ID
 * @quest_id: validated ID
 * @error: error location
 *
 * Returns: (nullable): the definition, or NULL with NOT_FOUND set
 */
static LrgQuestDef *
lookup_def (GHashTable   *defs,
            const gchar  *quest_id,
            GError      **error)
{
    gpointer def;

    def = g_hash_table_lookup (defs, quest_id);
    if (def == NULL || !LRG_IS_QUEST_DEF (def))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND,
                     "Unknown quest '%s' in snapshot", quest_id);
        return NULL;
    }
    return LRG_QUEST_DEF (def);
}

/*
 * validate_active_entry:
 * @entry: one "(suau)" value
 * @defs: definitions
 * @seen: IDs already seen in the active array
 * @error: error location
 *
 * Checks one active entry without creating anything.
 */
static gboolean
validate_active_entry (GVariant    *entry,
                       GHashTable  *defs,
                       GHashTable  *seen,
                       GError     **error)
{
    g_autoptr(GVariant) progress = NULL;
    const gchar        *quest_id;
    guint32             stage;
    LrgQuestDef        *def;
    GPtrArray          *objectives;
    gsize               n_progress;
    gsize               i;

    g_variant_get (entry, "(&su@au)", &quest_id, &stage, &progress);

    if (!quest_id_is_valid (quest_id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Invalid active quest ID in snapshot");
        return FALSE;
    }
    if (!g_hash_table_add (seen, (gpointer)quest_id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Duplicate active quest '%s' in snapshot", quest_id);
        return FALSE;
    }

    def = lookup_def (defs, quest_id, error);
    if (def == NULL)
        return FALSE;

    if (stage > lrg_quest_def_get_stage_count (def))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Quest '%s' stage %u is out of range", quest_id, (guint)stage);
        return FALSE;
    }

    n_progress = g_variant_n_children (progress);
    if (n_progress > LRG_QUEST_MAX_STAGE_OBJECTIVES)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Quest '%s' has too many objective values", quest_id);
        return FALSE;
    }

    /* Every stored value must map onto an objective and not exceed its target. */
    objectives = lrg_quest_def_get_stage_objectives (def, stage);
    if (n_progress > (objectives != NULL ? objectives->len : 0))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Quest '%s' stage %u has more values than objectives",
                     quest_id, (guint)stage);
        return FALSE;
    }
    for (i = 0; i < n_progress; i++)
    {
        guint32 value;
        guint   target;

        g_variant_get_child (progress, i, "u", &value);
        target = lrg_quest_objective_get_target_count (g_ptr_array_index (objectives, i));
        if (value > target)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Quest '%s' objective %u progress %u exceeds target %u",
                         quest_id, (guint)i, (guint)value, target);
            return FALSE;
        }
    }

    return TRUE;
}

/*
 * validate_completed_entry:
 * @entry: one "(sux)" value
 * @defs: definitions
 * @seen: IDs already seen in the completed array
 * @error: error location
 *
 * Checks one completion record without creating anything.
 */
static gboolean
validate_completed_entry (GVariant    *entry,
                          GHashTable  *defs,
                          GHashTable  *seen,
                          GError     **error)
{
    const gchar *quest_id;
    guint32      count;
    gint64       last;

    g_variant_get (entry, "(&sux)", &quest_id, &count, &last);

    if (!quest_id_is_valid (quest_id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Invalid completed quest ID in snapshot");
        return FALSE;
    }
    if (!g_hash_table_add (seen, (gpointer)quest_id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Duplicate completed quest '%s' in snapshot", quest_id);
        return FALSE;
    }
    if (lookup_def (defs, quest_id, error) == NULL)
        return FALSE;
    if (count == 0)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Quest '%s' has a zero completion count", quest_id);
        return FALSE;
    }

    return TRUE;
}

LrgQuestLog *
lrg_quest_log_new_from_variant (GVariant    *variant,
                                GHashTable  *defs,
                                GError     **error)
{
    g_autoptr(GVariant)    active = NULL;
    g_autoptr(GVariant)    completed = NULL;
    g_autoptr(GHashTable)  seen_active = NULL;
    g_autoptr(GHashTable)  seen_completed = NULL;
    LrgQuestLog           *self;
    gsize                  n_active;
    gsize                  n_completed;
    gsize                  i;

    g_return_val_if_fail (variant != NULL, NULL);
    g_return_val_if_fail (defs != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* Shape checks before touching any child. */
    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE (LRG_QUEST_LOG_VARIANT_TYPE)))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Quest log snapshot has type '%s', expected '%s'",
                     g_variant_get_type_string (variant), LRG_QUEST_LOG_VARIANT_TYPE);
        return NULL;
    }
    if (!g_variant_is_normal_form (variant))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Quest log snapshot is not in normal form");
        return NULL;
    }

    active = g_variant_get_child_value (variant, 0);
    completed = g_variant_get_child_value (variant, 1);
    n_active = g_variant_n_children (active);
    n_completed = g_variant_n_children (completed);

    if (n_active > LRG_QUEST_LOG_MAX_ACTIVE)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Quest log snapshot has %" G_GSIZE_FORMAT " active quests (max %u)",
                     n_active, LRG_QUEST_LOG_MAX_ACTIVE);
        return NULL;
    }
    if (n_completed > LRG_QUEST_LOG_MAX_COMPLETED)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Quest log snapshot has %" G_GSIZE_FORMAT " completed quests (max %u)",
                     n_completed, LRG_QUEST_LOG_MAX_COMPLETED);
        return NULL;
    }

    /*
     * Validate every entry first so that nothing is built for a snapshot
     * that will be rejected. The seen sets borrow strings from the
     * variants, which outlive them here.
     */
    seen_active = g_hash_table_new (g_str_hash, g_str_equal);
    for (i = 0; i < n_active; i++)
    {
        g_autoptr(GVariant) entry = g_variant_get_child_value (active, i);
        if (!validate_active_entry (entry, defs, seen_active, error))
            return NULL;
    }
    seen_completed = g_hash_table_new (g_str_hash, g_str_equal);
    for (i = 0; i < n_completed; i++)
    {
        g_autoptr(GVariant) entry = g_variant_get_child_value (completed, i);
        if (!validate_completed_entry (entry, defs, seen_completed, error))
            return NULL;
    }

    /* Build the log. Nothing below can fail. */
    self = lrg_quest_log_new ();

    for (i = 0; i < n_completed; i++)
    {
        const gchar        *quest_id;
        guint32             count;
        gint64              last;
        LrgQuestDef        *def;
        LrgQuestInstance   *instance;
        LrgQuestCompletion *record;

        g_variant_get_child (completed, i, "(&sux)", &quest_id, &count, &last);
        def = LRG_QUEST_DEF (g_hash_table_lookup (defs, quest_id));

        record = g_new0 (LrgQuestCompletion, 1);
        record->count = count;
        record->last = last;
        g_hash_table_replace (self->records, g_strdup (quest_id), record);

        /* Unconnected completed instance, so get_quest() finds it. */
        instance = lrg_quest_instance_new (def);
        lrg_quest_instance_set_stage (instance, lrg_quest_def_get_stage_count (def));
        lrg_quest_instance_set_state (instance, LRG_QUEST_STATE_COMPLETE);
        g_hash_table_replace (self->completed_quests, g_strdup (quest_id), instance);
    }

    for (i = 0; i < n_active; i++)
    {
        g_autoptr(GVariant) progress = NULL;
        const gchar        *quest_id;
        guint32             stage;
        LrgQuestDef        *def;

        g_variant_get_child (active, i, "(&su@au)", &quest_id, &stage, &progress);
        def = LRG_QUEST_DEF (g_hash_table_lookup (defs, quest_id));
        start_internal (self, def, TRUE, (guint)stage, progress, FALSE);
    }

    return self;
}
