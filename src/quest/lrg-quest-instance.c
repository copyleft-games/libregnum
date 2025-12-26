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
    GPtrArray     *objective_progress;  /* Copies of objectives with progress */
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

    g_clear_pointer (&self->objective_progress, g_ptr_array_unref);

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

static void
lrg_quest_instance_constructed (GObject *object)
{
    LrgQuestInstance *self = LRG_QUEST_INSTANCE (object);
    GPtrArray        *stages;
    guint             i;

    G_OBJECT_CLASS (lrg_quest_instance_parent_class)->constructed (object);

    /* Copy objectives from quest def for progress tracking */
    if (self->quest_def != NULL)
    {
        stages = lrg_quest_def_get_stages (self->quest_def);
        for (i = 0; i < stages->len; i++)
        {
            LrgQuestObjective *orig = g_ptr_array_index (stages, i);
            LrgQuestObjective *copy = lrg_quest_objective_copy (orig);
            g_ptr_array_add (self->objective_progress, copy);
        }
    }
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
    self->objective_progress = g_ptr_array_new_with_free_func (objective_free_wrapper);
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

    if (self->current_stage >= self->objective_progress->len)
        return NULL;

    return g_ptr_array_index (self->objective_progress, self->current_stage);
}

gboolean
lrg_quest_instance_update_progress (LrgQuestInstance      *self,
                                    LrgQuestObjectiveType  objective_type,
                                    const gchar           *target_id,
                                    guint                  amount)
{
    LrgQuestObjective     *obj;
    LrgQuestObjectiveType  obj_type;
    const gchar           *obj_target;

    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), FALSE);

    if (self->state != LRG_QUEST_STATE_ACTIVE)
        return FALSE;

    obj = lrg_quest_instance_get_current_objective (self);
    if (obj == NULL)
        return FALSE;

    obj_type = lrg_quest_objective_get_objective_type (obj);
    if (obj_type != objective_type)
        return FALSE;

    obj_target = lrg_quest_objective_get_target_id (obj);
    if (target_id != NULL && obj_target != NULL && g_strcmp0 (target_id, obj_target) != 0)
        return FALSE;

    lrg_quest_objective_increment (obj, amount);
    g_signal_emit (self, signals[SIGNAL_OBJECTIVE_UPDATED], 0, obj);

    /* Auto-advance if complete */
    if (lrg_quest_objective_is_complete (obj))
        lrg_quest_instance_advance_stage (self);

    return TRUE;
}

gboolean
lrg_quest_instance_advance_stage (LrgQuestInstance *self)
{
    LrgQuestObjective *obj;

    g_return_val_if_fail (LRG_IS_QUEST_INSTANCE (self), FALSE);

    obj = lrg_quest_instance_get_current_objective (self);
    if (obj != NULL && !lrg_quest_objective_is_complete (obj))
        return FALSE;

    self->current_stage++;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_STAGE]);
    g_signal_emit (self, signals[SIGNAL_STAGE_ADVANCED], 0, self->current_stage);

    /* Check if all stages complete */
    if (self->current_stage >= self->objective_progress->len)
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

    total = self->objective_progress->len;
    if (total == 0)
        return 1.0;

    if (self->current_stage >= total)
        return 1.0;

    /* Calculate progress based on completed stages plus current stage progress */
    stage_progress = 0.0;
    {
        LrgQuestObjective *obj = lrg_quest_instance_get_current_objective (self);
        if (obj != NULL)
            stage_progress = lrg_quest_objective_get_progress (obj);
    }

    return ((gdouble)self->current_stage + stage_progress) / (gdouble)total;
}
