/* lrg-quest-instance.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "quest/lrg-quest-instance.h"

struct _LrgQuestInstance
{
    GObject        parent_instance;

    LrgQuestDef   *quest_def;
    LrgQuestState  state;
    guint          current_stage;
    GPtrArray     *stage_progress;  /* per stage: GPtrArray of LrgQuestObjective copies */
};

G_DEFINE_TYPE (LrgQuestInstance, lrg_quest_instance, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_QUEST_DEF,
    PROP_STATE,
    PROP_CURRENT_STAGE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_STATE_CHANGED,
    SIGNAL_STAGE_ADVANCED,
    SIGNAL_OBJECTIVE_UPDATED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_quest_instance_dispose (GObject *object)
{
    LrgQuestInstance *self = LRG_QUEST_INSTANCE (object);

    g_clear_object (&self->quest_def);

    G_OBJECT_CLASS (lrg_quest_instance_parent_class)->dispose (object);
}

static void
objective_free_wrapper (gpointer data)
{
    lrg_quest_objective_free ((LrgQuestObjective *)data);
}

static void
lrg_quest_instance_finalize (GObject *object)
{
    LrgQuestInstance *self = LRG_QUEST_INSTANCE (object);

    g_clear_pointer (&self->stage_progress, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_quest_instance_parent_class)->finalize (object);
}

static void
lrg_quest_instance_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgQuestInstance *self = LRG_QUEST_INSTANCE (object);

    switch (prop_id)
    {
    case PROP_QUEST_DEF:
        g_value_set_object (value, self->quest_def);
        break;
    case PROP_STATE:
        g_value_set_enum (value, self->state);
        break;
    case PROP_CURRENT_STAGE:
        g_value_set_uint (value, self->current_stage);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_quest_instance_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgQuestInstance *self = LRG_QUEST_INSTANCE (object);

    switch (prop_id)
    {
    case PROP_QUEST_DEF:
        g_set_object (&self->quest_def, g_value_get_object (value));
        break;
    case PROP_STATE:
        lrg_quest_instance_set_state (self, g_value_get_enum (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/*
 * copy_stage_objectives:
 * @self: instance
 * @reset: whether to zero the copied progress
 *
 * Rebuilds the per-stage progress copies from the definition. Every
 * parallel objective of every stage is copied. With @reset the copies
 * start at zero progress and are only complete when their target is 0.
 */
static void
copy_stage_objectives (LrgQuestInstance *self,
                       gboolean          reset)
{
    guint n_stages;
    guint i;
    guint j;

    g_ptr_array_set_size (self->stage_progress, 0);
    if (self->quest_def == NULL)
        return;

    n_stages = lrg_quest_def_get_stage_count (self->quest_def);
    for (i = 0; i < n_stages; i++)
    {
        GPtrArray *orig = lrg_quest_def_get_stage_objectives (self->quest_def, i);
        GPtrArray *copies = g_ptr_array_new_with_free_func (objective_free_wrapper);

        for (j = 0; orig != NULL && j < orig->len; j++)
        {
            LrgQuestObjective *copy = lrg_quest_objective_copy (g_ptr_array_index (orig, j));
            if (reset)
            {
                lrg_quest_objective_set_complete (copy, FALSE);
                lrg_quest_objective_set_current_count (copy, 0);
            }
            g_ptr_array_add (copies, copy);
        }
        g_ptr_array_add (self->stage_progress, copies);
    }
}

/*
 * current_objectives:
 * @self: instance
 *
 * Returns: (nullable): objectives of the current stage, or NULL when done
 */
static GPtrArray *
current_objectives (LrgQuestInstance *self)
{
    if (self->current_stage >= self->stage_progress->len)
        return NULL;
    return g_ptr_array_index (self->stage_progress, self->current_stage);
}

/*
 * current_stage_complete:
 * @self: instance
 *
 * Returns: whether every objective of the current stage is complete
 */
static gboolean
current_stage_complete (LrgQuestInstance *self)
{
    GPtrArray *objectives;
    guint      i;

    objectives = current_objectives (self);
    if (objectives == NULL)
        return TRUE;
    for (i = 0; i < objectives->len; i++)
    {
        if (!lrg_quest_objective_is_complete (g_ptr_array_index (objectives, i)))
            return FALSE;
    }
    return TRUE;
}

static void
lrg_quest_instance_constructed (GObject *object)
{
    LrgQuestInstance *self = LRG_QUEST_INSTANCE (object);

    G_OBJECT_CLASS (lrg_quest_instance_parent_class)->constructed (object);

    /* Copy objectives from quest def for progress tracking, as authored. */
    copy_stage_objectives (self, FALSE);
}

static void
lrg_quest_instance_class_init (LrgQuestInstanceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_quest_instance_dispose;
    object_class->finalize = lrg_quest_instance_finalize;
    object_class->get_property = lrg_quest_instance_get_property;
    object_class->set_property = lrg_quest_instance_set_property;
    object_class->constructed = lrg_quest_instance_constructed;

    properties[PROP_QUEST_DEF] =
        g_param_spec_object ("quest-def", "Quest Definition", "The quest definition",
                             LRG_TYPE_QUEST_DEF,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    properties[PROP_STATE] =
        g_param_spec_enum ("state", "State", "Quest state",
                           LRG_TYPE_QUEST_STATE, LRG_QUEST_STATE_AVAILABLE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CURRENT_STAGE] =
        g_param_spec_uint ("current-stage", "Current Stage", "Current stage index",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    signals[SIGNAL_STATE_CHANGED] =
        g_signal_new ("state-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_QUEST_STATE);

    signals[SIGNAL_STAGE_ADVANCED] =
        g_signal_new ("stage-advanced",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);

    signals[SIGNAL_OBJECTIVE_UPDATED] =
        g_signal_new ("objective-updated",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_QUEST_OBJECTIVE);
}

static void
lrg_quest_instance_init (LrgQuestInstance *self)
{
    self->state = LRG_QUEST_STATE_AVAILABLE;
    self->current_stage = 0;
    self->stage_progress = g_ptr_array_new_with_free_func ((GDestroyNotify)g_ptr_array_unref);
}

LrgQuestInstance *
lrg_quest_instance_new (LrgQuestDef *quest_def)
{
    g_return_val_if_fail (LRG_IS_QUEST_DEF (quest_def), NULL);
    return g_object_new (LRG_TYPE_QUEST_INSTANCE,
                         "quest-def", quest_def,
                         NULL);
}

LrgQuestDef *
lrg_quest_instance_get_quest_def (LrgQuestInstance *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), NULL);
    return self->quest_def;
}

LrgQuestState
lrg_quest_instance_get_state (LrgQuestInstance *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), LRG_QUEST_STATE_AVAILABLE);
    return self->state;
}

void
lrg_quest_instance_set_state (LrgQuestInstance *self,
                              LrgQuestState     state)
{
    g_return_if_fail (LRG_IS_QUEST_INSTANCE (self));

    if (self->state != state)
    {
        self->state = state;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
        g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0, state);
    }
}

guint
lrg_quest_instance_get_current_stage (LrgQuestInstance *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), 0);
    return self->current_stage;
}

LrgQuestObjective *
lrg_quest_instance_get_current_objective (LrgQuestInstance *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), NULL);

    return lrg_quest_instance_get_objective (self, 0);
}

gboolean
lrg_quest_instance_update_progress (LrgQuestInstance      *self,
                                    LrgQuestObjectiveType  objective_type,
                                    const gchar           *target_id,
                                    guint                  amount)
{
    GPtrArray *objectives;
    gboolean   updated;
    guint      i;

    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), FALSE);

    if (self->state != LRG_QUEST_STATE_ACTIVE)
        return FALSE;

    objectives = current_objectives (self);
    if (objectives == NULL)
        return FALSE;

    /* Increment every incomplete objective of the stage that matches. */
    updated = FALSE;
    for (i = 0; i < objectives->len; i++)
    {
        LrgQuestObjective *obj = g_ptr_array_index (objectives, i);
        const gchar       *obj_target;

        if (lrg_quest_objective_is_complete (obj))
            continue;
        if (lrg_quest_objective_get_objective_type (obj) != objective_type)
            continue;

        obj_target = lrg_quest_objective_get_target_id (obj);
        if (target_id != NULL && obj_target != NULL && g_strcmp0 (target_id, obj_target) != 0)
            continue;

        lrg_quest_objective_increment (obj, amount);
        updated = TRUE;
        g_signal_emit (self, signals[SIGNAL_OBJECTIVE_UPDATED], 0, obj);

        /* A handler may have changed the state or stage; stop touching it. */
        if (self->state != LRG_QUEST_STATE_ACTIVE || current_objectives (self) != objectives)
            return TRUE;
    }

    /*
     * Auto-advance once the whole stage is complete. This also advances a
     * stage that was already complete (restored snapshot or objectives
     * with a zero target), so an instance can never get stuck.
     */
    if (current_stage_complete (self))
        lrg_quest_instance_advance_stage (self);

    return updated;
}

gboolean
lrg_quest_instance_advance_stage (LrgQuestInstance *self)
{
    guint n_stages;

    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), FALSE);

    /*
     * Past the last stage there is nothing to advance. A quest without
     * stages may still advance once from stage 0 to complete itself,
     * which keeps the historical zero-stage behaviour.
     */
    n_stages = self->stage_progress->len;
    if (self->current_stage >= MAX (n_stages, 1))
        return FALSE;

    if (!current_stage_complete (self))
        return FALSE;

    self->current_stage++;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_STAGE]);
    g_signal_emit (self, signals[SIGNAL_STAGE_ADVANCED], 0, self->current_stage);

    /* Check if all stages complete */
    if (self->current_stage >= n_stages)
        lrg_quest_instance_complete (self);

    return TRUE;
}

void
lrg_quest_instance_complete (LrgQuestInstance *self)
{
    g_return_if_fail (LRG_IS_QUEST_INSTANCE (self));
    lrg_quest_instance_set_state (self, LRG_QUEST_STATE_COMPLETE);
}

void
lrg_quest_instance_fail (LrgQuestInstance *self)
{
    g_return_if_fail (LRG_IS_QUEST_INSTANCE (self));
    lrg_quest_instance_set_state (self, LRG_QUEST_STATE_FAILED);
}

gboolean
lrg_quest_instance_is_complete (LrgQuestInstance *self)
{
    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), FALSE);
    return self->state == LRG_QUEST_STATE_COMPLETE;
}

gdouble
lrg_quest_instance_get_progress (LrgQuestInstance *self)
{
    guint   total;
    gdouble stage_progress;

    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), 0.0);

    total = self->stage_progress->len;
    if (total == 0)
        return 1.0;

    if (self->current_stage >= total)
        return 1.0;

    /* Calculate progress based on completed stages plus current stage progress */
    /* The current stage contributes the mean of its objectives' progress. */
    stage_progress = 0.0;
    {
        GPtrArray *objectives = current_objectives (self);
        guint      i;

        for (i = 0; objectives != NULL && i < objectives->len; i++)
            stage_progress += MIN (1.0, lrg_quest_objective_get_progress (g_ptr_array_index (objectives, i)));
        if (objectives != NULL && objectives->len > 0)
            stage_progress /= (gdouble)objectives->len;
    }

    return ((gdouble)self->current_stage + stage_progress) / (gdouble)total;
}

guint
lrg_quest_instance_get_objective_count (LrgQuestInstance *self)
{
    GPtrArray *objectives;

    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), 0);
    objectives = current_objectives (self);
    return objectives != NULL ? objectives->len : 0;
}

LrgQuestObjective *
lrg_quest_instance_get_objective (LrgQuestInstance *self,
                                  guint             objective_index)
{
    GPtrArray *objectives;

    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), NULL);
    objectives = current_objectives (self);
    if (objectives == NULL || objective_index >= objectives->len)
        return NULL;
    return g_ptr_array_index (objectives, objective_index);
}

guint
lrg_quest_instance_get_objective_progress (LrgQuestInstance *self,
                                           guint             objective_index)
{
    LrgQuestObjective *obj;

    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), 0);
    obj = lrg_quest_instance_get_objective (self, objective_index);
    return obj != NULL ? lrg_quest_objective_get_current_count (obj) : 0;
}

gboolean
lrg_quest_instance_set_objective_progress (LrgQuestInstance *self,
                                           guint             objective_index,
                                           guint             count)
{
    LrgQuestObjective *obj;
    guint              target;

    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), FALSE);

    obj = lrg_quest_instance_get_objective (self, objective_index);
    if (obj == NULL)
        return FALSE;

    /* Clamp to the target and derive completion from the clamped count. */
    target = lrg_quest_objective_get_target_count (obj);
    count = MIN (count, target);
    lrg_quest_objective_set_complete (obj, FALSE);
    lrg_quest_objective_set_current_count (obj, count);
    return TRUE;
}

gboolean
lrg_quest_instance_set_stage (LrgQuestInstance *self,
                              guint             stage)
{
    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), FALSE);

    if (stage > self->stage_progress->len)
        return FALSE;

    copy_stage_objectives (self, TRUE);
    if (self->current_stage != stage)
    {
        self->current_stage = stage;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_STAGE]);
    }
    return TRUE;
}
