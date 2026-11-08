/* lrg-animation-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-animation-transition.h"
#include <math.h>

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

    /* 0.9.0 */
    gfloat                    offset;        /* target start, normalized */
    LrgTransitionInterruption interruption;  /* who may cut in */
    gboolean                  to_self;       /* any-state may re-enter */
};

GType
lrg_transition_interruption_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        static const GEnumValue values[] = {
            { LRG_TRANSITION_INTERRUPTION_NONE, "LRG_TRANSITION_INTERRUPTION_NONE", "none" },
            { LRG_TRANSITION_INTERRUPTION_SOURCE, "LRG_TRANSITION_INTERRUPTION_SOURCE", "source" },
            { LRG_TRANSITION_INTERRUPTION_DESTINATION, "LRG_TRANSITION_INTERRUPTION_DESTINATION", "destination" },
            { LRG_TRANSITION_INTERRUPTION_SOURCE_THEN_DESTINATION,
              "LRG_TRANSITION_INTERRUPTION_SOURCE_THEN_DESTINATION", "source-then-destination" },
            { LRG_TRANSITION_INTERRUPTION_DESTINATION_THEN_SOURCE,
              "LRG_TRANSITION_INTERRUPTION_DESTINATION_THEN_SOURCE", "destination-then-source" },
            { 0, NULL, NULL }
        };
        GType type = g_enum_register_static ("LrgTransitionInterruption", values);

        g_once_init_leave (&type_id, type);
    }
    return (GType)type_id;
}

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
    PROP_OFFSET,
    PROP_INTERRUPTION,
    PROP_CAN_TRANSITION_TO_SELF,
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
    case PROP_OFFSET:
        g_value_set_float (value, self->offset);
        break;
    case PROP_INTERRUPTION:
        g_value_set_enum (value, self->interruption);
        break;
    case PROP_CAN_TRANSITION_TO_SELF:
        g_value_set_boolean (value, self->to_self);
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
        lrg_animation_transition_set_source (self, g_value_get_string (value));
        break;
    case PROP_TARGET:
        lrg_animation_transition_set_target (self, g_value_get_string (value));
        break;
    case PROP_DURATION:
        lrg_animation_transition_set_duration (self, g_value_get_float (value));
        break;
    case PROP_EXIT_TIME:
        lrg_animation_transition_set_exit_time (self, g_value_get_float (value));
        break;
    case PROP_HAS_EXIT_TIME:
        lrg_animation_transition_set_has_exit_time (self, g_value_get_boolean (value));
        break;
    case PROP_PRIORITY:
        lrg_animation_transition_set_priority (self, g_value_get_int (value));
        break;
    case PROP_OFFSET:
        lrg_animation_transition_set_offset (self, g_value_get_float (value));
        break;
    case PROP_INTERRUPTION:
        lrg_animation_transition_set_interruption (self, g_value_get_enum (value));
        break;
    case PROP_CAN_TRANSITION_TO_SELF:
        lrg_animation_transition_set_can_transition_to_self (self, g_value_get_boolean (value));
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

    /**
     * LrgAnimationTransition:source:
     *
     * The source state name, or %LRG_ANIMATION_TRANSITION_ANY_STATE.
     */
    properties[PROP_SOURCE] =
        g_param_spec_string ("source", "Source", "Source state name",
                             NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgAnimationTransition:target:
     *
     * The target state name.
     */
    properties[PROP_TARGET] =
        g_param_spec_string ("target", "Target", "Target state name",
                             NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgAnimationTransition:duration:
     *
     * The cross-fade length in seconds.
     */
    properties[PROP_DURATION] =
        g_param_spec_float ("duration", "Duration", "Blend duration in seconds",
                            0.0f, 10.0f, 0.25f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgAnimationTransition:exit-time:
     *
     * The normalized source time the transition waits for (see
     * lrg_animation_transition_evaluate_range()); values of 1 or more
     * wait for that many loops.
     */
    properties[PROP_EXIT_TIME] =
        g_param_spec_float ("exit-time", "Exit Time", "Normalized exit time requirement",
                            -1.0f, 1000.0f, -1.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgAnimationTransition:has-exit-time:
     *
     * Whether the exit time is required.
     */
    properties[PROP_HAS_EXIT_TIME] =
        g_param_spec_boolean ("has-exit-time", "Has Exit Time", "Whether exit time is required",
                              FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgAnimationTransition:priority:
     *
     * Higher priorities are checked first; equal priorities in the order
     * the transitions were added.
     */
    properties[PROP_PRIORITY] =
        g_param_spec_int ("priority", "Priority", "Transition priority",
                          G_MININT, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgAnimationTransition:offset:
     *
     * Normalized time (0..1) the target state starts at.
     */
    properties[PROP_OFFSET] =
        g_param_spec_float ("offset", "Offset", "Normalized start time of the target",
                            0.0f, 1.0f, 0.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgAnimationTransition:interruption:
     *
     * Which transitions may interrupt this one.
     */
    properties[PROP_INTERRUPTION] =
        g_param_spec_enum ("interruption", "Interruption", "Interruption source",
                           LRG_TYPE_TRANSITION_INTERRUPTION, LRG_TRANSITION_INTERRUPTION_NONE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgAnimationTransition:can-transition-to-self:
     *
     * Whether an any-state transition may re-enter its current target.
     */
    properties[PROP_CAN_TRANSITION_TO_SELF] =
        g_param_spec_boolean ("can-transition-to-self", "Can Transition To Self",
                              "Any-state transition may re-enter its target",
                              FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

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

void
lrg_animation_transition_set_source (LrgAnimationTransition *self,
                                     const gchar            *source)
{
    g_return_if_fail (LRG_IS_ANIMATION_TRANSITION (self));

    if (g_strcmp0 (self->source, source) == 0)
        return;
    g_free (self->source);
    self->source = g_strdup (source);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SOURCE]);
}

void
lrg_animation_transition_set_target (LrgAnimationTransition *self,
                                     const gchar            *target)
{
    g_return_if_fail (LRG_IS_ANIMATION_TRANSITION (self));

    if (g_strcmp0 (self->target, target) == 0)
        return;
    g_free (self->target);
    self->target = g_strdup (target);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET]);
}

gboolean
lrg_animation_transition_is_any_state (LrgAnimationTransition *self)
{
    g_return_val_if_fail (LRG_IS_ANIMATION_TRANSITION (self), FALSE);

    return g_strcmp0 (self->source, LRG_ANIMATION_TRANSITION_ANY_STATE) == 0;
}

gfloat
lrg_animation_transition_get_offset (LrgAnimationTransition *self)
{
    g_return_val_if_fail (LRG_IS_ANIMATION_TRANSITION (self), 0.0f);
    return self->offset;
}

void
lrg_animation_transition_set_offset (LrgAnimationTransition *self,
                                     gfloat                  offset)
{
    g_return_if_fail (LRG_IS_ANIMATION_TRANSITION (self));

    offset = CLAMP (offset, 0.0f, 1.0f);
    if (self->offset == offset)
        return;
    self->offset = offset;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OFFSET]);
}

LrgTransitionInterruption
lrg_animation_transition_get_interruption (LrgAnimationTransition *self)
{
    g_return_val_if_fail (LRG_IS_ANIMATION_TRANSITION (self), LRG_TRANSITION_INTERRUPTION_NONE);
    return self->interruption;
}

void
lrg_animation_transition_set_interruption (LrgAnimationTransition    *self,
                                           LrgTransitionInterruption  interruption)
{
    g_return_if_fail (LRG_IS_ANIMATION_TRANSITION (self));

    if (self->interruption == interruption)
        return;
    self->interruption = interruption;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTERRUPTION]);
}

gboolean
lrg_animation_transition_get_can_transition_to_self (LrgAnimationTransition *self)
{
    g_return_val_if_fail (LRG_IS_ANIMATION_TRANSITION (self), FALSE);
    return self->to_self;
}

void
lrg_animation_transition_set_can_transition_to_self (LrgAnimationTransition *self,
                                                     gboolean                allow)
{
    g_return_if_fail (LRG_IS_ANIMATION_TRANSITION (self));

    allow = !!allow;
    if (self->to_self == allow)
        return;
    self->to_self = allow;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAN_TRANSITION_TO_SELF]);
}

/* exit_time_crossed:
 * Whether the exit time was reached during an update that moved the
 * normalized time from @prev to @now. Below 1 the exit time recurs every
 * loop (k + exit for every whole k); from 1 up it is a single absolute
 * point. A state that did not move (a paused or zero-length state) counts
 * as having reached the exit time when it already sits past it, so a
 * transition out of a finished one-shot clip still fires. */
static gboolean
exit_time_crossed (gfloat exit_time,
                   gfloat prev,
                   gfloat now)
{
    if (exit_time >= 1.0f)
        return (prev < exit_time && now >= exit_time) || (prev == now && now >= exit_time);
    if (now < exit_time)
        return FALSE;
    if (prev == now)
        return TRUE;
    /* a crossing of k + exit for some k in (prev, now] */
    return floorf (now - exit_time) > floorf (prev - exit_time) || prev < exit_time;
}

gboolean
lrg_animation_transition_evaluate_range (LrgAnimationTransition *self,
                                         GHashTable             *parameters,
                                         gfloat                  previous_normalized_time,
                                         gfloat                  normalized_time)
{
    GList *l;

    g_return_val_if_fail (LRG_IS_ANIMATION_TRANSITION (self), FALSE);
    g_return_val_if_fail (parameters != NULL, FALSE);

    if (self->has_exit_time && self->exit_time >= 0.0f &&
        !exit_time_crossed (self->exit_time, previous_normalized_time, normalized_time))
        return FALSE;

    /* every condition must hold (AND) */
    for (l = self->conditions; l != NULL; l = l->next)
        if (!evaluate_condition (l->data, parameters))
            return FALSE;
    return TRUE;
}
