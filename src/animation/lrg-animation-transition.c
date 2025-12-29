/* lrg-animation-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-animation-transition.h"

/**
 * SECTION:lrg-animation-transition
 * @Title: LrgAnimationTransition
 * @Short_description: Animation state transition rules
 *
 * #LrgAnimationTransition defines the rules for transitioning
 * between animation states. Each transition has conditions that
 * must be met, an optional exit time requirement, and a blend
 * duration.
 */

/*
 * LrgTransitionCondition boxed type
 */

G_DEFINE_BOXED_TYPE (LrgTransitionCondition, lrg_transition_condition,
                     lrg_transition_condition_copy,
                     lrg_transition_condition_free)

LrgTransitionCondition *
lrg_transition_condition_new (const gchar *parameter,
                               gint         comparison,
                               GVariant    *value)
{
    LrgTransitionCondition *cond;

    cond = g_slice_new0 (LrgTransitionCondition);
    cond->parameter = g_strdup (parameter);
    cond->comparison = comparison;
    cond->value = value ? g_variant_ref_sink (value) : NULL;

    return cond;
}

LrgTransitionCondition *
lrg_transition_condition_copy (const LrgTransitionCondition *cond)
{
    LrgTransitionCondition *copy;

    if (cond == NULL)
        return NULL;

    copy = g_slice_new (LrgTransitionCondition);
    copy->parameter = g_strdup (cond->parameter);
    copy->comparison = cond->comparison;
    copy->value = cond->value ? g_variant_ref (cond->value) : NULL;

    return copy;
}

void
lrg_transition_condition_free (LrgTransitionCondition *cond)
{
    if (cond == NULL)
        return;

    g_free (cond->parameter);
    if (cond->value != NULL)
        g_variant_unref (cond->value);
    g_slice_free (LrgTransitionCondition, cond);
}

/*
 * LrgAnimationTransition
 */

struct _LrgAnimationTransition
{
    GObject parent_instance;

    gchar   *source;
    gchar   *target;
    gfloat   duration;
    gfloat   exit_time;
    gboolean has_exit_time;
    gint     priority;
    GList   *conditions;  /* List of LrgTransitionCondition */
};

G_DEFINE_TYPE (LrgAnimationTransition, lrg_animation_transition, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_SOURCE,
    PROP_TARGET,
    PROP_DURATION,
    PROP_EXIT_TIME,
    PROP_HAS_EXIT_TIME,
    PROP_PRIORITY,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_animation_transition_finalize (GObject *object)
{
    LrgAnimationTransition *self = LRG_ANIMATION_TRANSITION (object);

    g_free (self->source);
    g_free (self->target);
    g_list_free_full (self->conditions, (GDestroyNotify)lrg_transition_condition_free);

    G_OBJECT_CLASS (lrg_animation_transition_parent_class)->finalize (object);
}

static void
lrg_animation_transition_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
    LrgAnimationTransition *self = LRG_ANIMATION_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_SOURCE:
        g_value_set_string (value, self->source);
        break;
    case PROP_TARGET:
        g_value_set_string (value, self->target);
        break;
    case PROP_DURATION:
        g_value_set_float (value, self->duration);
        break;
    case PROP_EXIT_TIME:
        g_value_set_float (value, self->exit_time);
        break;
    case PROP_HAS_EXIT_TIME:
        g_value_set_boolean (value, self->has_exit_time);
        break;
    case PROP_PRIORITY:
        g_value_set_int (value, self->priority);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_animation_transition_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
    LrgAnimationTransition *self = LRG_ANIMATION_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_SOURCE:
        g_free (self->source);
        self->source = g_value_dup_string (value);
        break;
    case PROP_TARGET:
        g_free (self->target);
        self->target = g_value_dup_string (value);
        break;
    case PROP_DURATION:
        self->duration = g_value_get_float (value);
        break;
    case PROP_EXIT_TIME:
        self->exit_time = g_value_get_float (value);
        break;
    case PROP_HAS_EXIT_TIME:
        self->has_exit_time = g_value_get_boolean (value);
        break;
    case PROP_PRIORITY:
        self->priority = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_animation_transition_class_init (LrgAnimationTransitionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_animation_transition_finalize;
    object_class->get_property = lrg_animation_transition_get_property;
    object_class->set_property = lrg_animation_transition_set_property;

    properties[PROP_SOURCE] =
        g_param_spec_string ("source", "Source", "Source state name",
                             NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    properties[PROP_TARGET] =
        g_param_spec_string ("target", "Target", "Target state name",
                             NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    properties[PROP_DURATION] =
        g_param_spec_float ("duration", "Duration", "Blend duration in seconds",
                            0.0f, 10.0f, 0.25f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_EXIT_TIME] =
        g_param_spec_float ("exit-time", "Exit Time", "Normalized exit time requirement",
                            -1.0f, 1.0f, -1.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HAS_EXIT_TIME] =
        g_param_spec_boolean ("has-exit-time", "Has Exit Time", "Whether exit time is required",
                              FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PRIORITY] =
        g_param_spec_int ("priority", "Priority", "Transition priority",
                          G_MININT, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_animation_transition_init (LrgAnimationTransition *self)
{
    self->source = NULL;
    self->target = NULL;
    self->duration = 0.25f;
    self->exit_time = -1.0f;
    self->has_exit_time = FALSE;
    self->priority = 0;
    self->conditions = NULL;
}

/*
 * Public API
 */

LrgAnimationTransition *
lrg_animation_transition_new (const gchar *source,
                               const gchar *target)
{
    return g_object_new (LRG_TYPE_ANIMATION_TRANSITION,
                         "source", source,
                         "target", target,
                         NULL);
}

const gchar *
lrg_animation_transition_get_source (LrgAnimationTransition *self)
{
    g_return_val_if_fail (LRG_IS_ANIMATION_TRANSITION (self), NULL);
    return self->source;
}

const gchar *
lrg_animation_transition_get_target (LrgAnimationTransition *self)
{
    g_return_val_if_fail (LRG_IS_ANIMATION_TRANSITION (self), NULL);
    return self->target;
}

gfloat
lrg_animation_transition_get_duration (LrgAnimationTransition *self)
{
    g_return_val_if_fail (LRG_IS_ANIMATION_TRANSITION (self), 0.0f);
    return self->duration;
}

void
lrg_animation_transition_set_duration (LrgAnimationTransition *self,
                                        gfloat                  duration)
{
    g_return_if_fail (LRG_IS_ANIMATION_TRANSITION (self));

    if (self->duration != duration)
    {
        self->duration = duration;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DURATION]);
    }
}

gfloat
lrg_animation_transition_get_exit_time (LrgAnimationTransition *self)
{
    g_return_val_if_fail (LRG_IS_ANIMATION_TRANSITION (self), -1.0f);
    return self->exit_time;
}

void
lrg_animation_transition_set_exit_time (LrgAnimationTransition *self,
                                         gfloat                  exit_time)
{
    g_return_if_fail (LRG_IS_ANIMATION_TRANSITION (self));

    if (self->exit_time != exit_time)
    {
        self->exit_time = exit_time;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXIT_TIME]);
    }
}

gboolean
lrg_animation_transition_get_has_exit_time (LrgAnimationTransition *self)
{
    g_return_val_if_fail (LRG_IS_ANIMATION_TRANSITION (self), FALSE);
    return self->has_exit_time;
}

void
lrg_animation_transition_set_has_exit_time (LrgAnimationTransition *self,
                                             gboolean                has_exit_time)
{
    g_return_if_fail (LRG_IS_ANIMATION_TRANSITION (self));

    if (self->has_exit_time != has_exit_time)
    {
        self->has_exit_time = has_exit_time;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HAS_EXIT_TIME]);
    }
}

void
lrg_animation_transition_add_condition (LrgAnimationTransition *self,
                                         const gchar            *parameter,
                                         gint                    comparison,
                                         GVariant               *value)
{
    LrgTransitionCondition *cond;

    g_return_if_fail (LRG_IS_ANIMATION_TRANSITION (self));
    g_return_if_fail (parameter != NULL);

    cond = lrg_transition_condition_new (parameter, comparison, value);
    self->conditions = g_list_append (self->conditions, cond);
}

void
lrg_animation_transition_clear_conditions (LrgAnimationTransition *self)
{
    g_return_if_fail (LRG_IS_ANIMATION_TRANSITION (self));

    g_list_free_full (self->conditions, (GDestroyNotify)lrg_transition_condition_free);
    self->conditions = NULL;
}

GList *
lrg_animation_transition_get_conditions (LrgAnimationTransition *self)
{
    g_return_val_if_fail (LRG_IS_ANIMATION_TRANSITION (self), NULL);
    return self->conditions;
}

guint
lrg_animation_transition_get_condition_count (LrgAnimationTransition *self)
{
    g_return_val_if_fail (LRG_IS_ANIMATION_TRANSITION (self), 0);
    return g_list_length (self->conditions);
}

static gboolean
evaluate_condition (LrgTransitionCondition *cond,
                    GHashTable             *parameters)
{
    GVariant *param_value;
    gint comp_result;
    gboolean result;

    if (cond == NULL || cond->parameter == NULL)
        return TRUE;

    param_value = g_hash_table_lookup (parameters, cond->parameter);
    if (param_value == NULL)
        return FALSE;  /* Parameter not found */

    if (cond->value == NULL)
        return TRUE;

    /* Compare based on type */
    if (g_variant_is_of_type (param_value, G_VARIANT_TYPE_DOUBLE) ||
        g_variant_is_of_type (param_value, G_VARIANT_TYPE_INT32))
    {
        gdouble a;
        gdouble b;

        if (g_variant_is_of_type (param_value, G_VARIANT_TYPE_DOUBLE))
            a = g_variant_get_double (param_value);
        else
            a = (gdouble)g_variant_get_int32 (param_value);

        if (g_variant_is_of_type (cond->value, G_VARIANT_TYPE_DOUBLE))
            b = g_variant_get_double (cond->value);
        else if (g_variant_is_of_type (cond->value, G_VARIANT_TYPE_INT32))
            b = (gdouble)g_variant_get_int32 (cond->value);
        else
            return FALSE;

        if (a < b)
            comp_result = -1;
        else if (a > b)
            comp_result = 1;
        else
            comp_result = 0;
    }
    else if (g_variant_is_of_type (param_value, G_VARIANT_TYPE_BOOLEAN))
    {
        gboolean a;
        gboolean b;

        a = g_variant_get_boolean (param_value);
        b = g_variant_get_boolean (cond->value);
        comp_result = (a == b) ? 0 : 1;
    }
    else
    {
        return FALSE;
    }

    switch (cond->comparison)
    {
    case LRG_CONDITION_EQUALS:
        result = (comp_result == 0);
        break;
    case LRG_CONDITION_NOT_EQUALS:
        result = (comp_result != 0);
        break;
    case LRG_CONDITION_GREATER:
        result = (comp_result > 0);
        break;
    case LRG_CONDITION_LESS:
        result = (comp_result < 0);
        break;
    case LRG_CONDITION_GREATER_EQUAL:
        result = (comp_result >= 0);
        break;
    case LRG_CONDITION_LESS_EQUAL:
        result = (comp_result <= 0);
        break;
    default:
        result = FALSE;
    }

    return result;
}

gboolean
lrg_animation_transition_evaluate (LrgAnimationTransition *self,
                                    GHashTable             *parameters,
                                    gfloat                  source_normalized_time)
{
    GList *l;

    g_return_val_if_fail (LRG_IS_ANIMATION_TRANSITION (self), FALSE);

    /* Check exit time first */
    if (self->has_exit_time && self->exit_time >= 0.0f)
    {
        if (source_normalized_time < self->exit_time)
            return FALSE;
    }

    /* Evaluate all conditions (AND) */
    for (l = self->conditions; l != NULL; l = l->next)
    {
        LrgTransitionCondition *cond = l->data;

        if (!evaluate_condition (cond, parameters))
            return FALSE;
    }

    return TRUE;
}

gint
lrg_animation_transition_get_priority (LrgAnimationTransition *self)
{
    g_return_val_if_fail (LRG_IS_ANIMATION_TRANSITION (self), 0);
    return self->priority;
}

void
lrg_animation_transition_set_priority (LrgAnimationTransition *self,
                                        gint                    priority)
{
    g_return_if_fail (LRG_IS_ANIMATION_TRANSITION (self));

    if (self->priority != priority)
    {
        self->priority = priority;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIORITY]);
    }
}
