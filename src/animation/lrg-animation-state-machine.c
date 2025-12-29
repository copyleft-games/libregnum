/* lrg-animation-state-machine.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-animation-state-machine.h"

/**
 * SECTION:lrg-animation-state-machine
 * @Title: LrgAnimationStateMachine
 * @Short_description: Animation state machine controller
 *
 * #LrgAnimationStateMachine manages animation states and transitions
 * between them based on parameters and conditions. It samples animations
 * and applies them to a skeleton.
 */

typedef struct
{
    LrgSkeleton          *skeleton;
    GHashTable           *states;        /* name -> LrgAnimationState */
    GList                *transitions;   /* List of LrgAnimationTransition */
    GHashTable           *parameters;    /* name -> GVariant */

    gchar                *default_state;
    LrgAnimationState    *current_state;
    LrgAnimationState    *next_state;    /* During transition */

    gboolean              running;
    gboolean              transitioning;
    gfloat                transition_progress;
    gfloat                transition_duration;
} LrgAnimationStateMachinePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgAnimationStateMachine, lrg_animation_state_machine, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_SKELETON,
    PROP_RUNNING,
    N_PROPS
};

enum
{
    SIGNAL_STATE_ENTERED,
    SIGNAL_STATE_EXITED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

/*
 * Virtual method implementations
 */

static void
lrg_animation_state_machine_real_update (LrgAnimationStateMachine *self,
                                          gfloat                    delta_time)
{
    LrgAnimationStateMachinePrivate *priv;
    GList *l;

    priv = lrg_animation_state_machine_get_instance_private (self);

    if (!priv->running || priv->current_state == NULL)
        return;

    /* Update current state */
    lrg_animation_state_update (priv->current_state, delta_time);

    /* Update transition if active */
    if (priv->transitioning && priv->next_state != NULL)
    {
        lrg_animation_state_update (priv->next_state, delta_time);

        priv->transition_progress += delta_time / priv->transition_duration;

        if (priv->transition_progress >= 1.0f)
        {
            /* Transition complete */
            const gchar *old_name;
            const gchar *new_name;

            old_name = lrg_animation_state_get_name (priv->current_state);
            new_name = lrg_animation_state_get_name (priv->next_state);

            lrg_animation_state_exit (priv->current_state);
            g_signal_emit (self, signals[SIGNAL_STATE_EXITED], 0, old_name);

            priv->current_state = priv->next_state;
            priv->next_state = NULL;
            priv->transitioning = FALSE;
            priv->transition_progress = 0.0f;

            g_signal_emit (self, signals[SIGNAL_STATE_ENTERED], 0, new_name);
        }
    }
    else
    {
        gfloat norm_time;

        /* Check for transitions */
        norm_time = lrg_animation_state_get_normalized_time (priv->current_state);

        /* Sort transitions by priority and check them */
        for (l = priv->transitions; l != NULL; l = l->next)
        {
            LrgAnimationTransition *trans = l->data;
            const gchar *source_name;
            const gchar *current_name;

            source_name = lrg_animation_transition_get_source (trans);
            current_name = lrg_animation_state_get_name (priv->current_state);

            if (g_strcmp0 (source_name, current_name) != 0)
                continue;

            if (lrg_animation_transition_evaluate (trans, priv->parameters, norm_time))
            {
                const gchar *target_name;
                LrgAnimationState *target_state;

                target_name = lrg_animation_transition_get_target (trans);
                target_state = g_hash_table_lookup (priv->states, target_name);

                if (target_state != NULL)
                {
                    /* Start transition */
                    priv->next_state = target_state;
                    priv->transitioning = TRUE;
                    priv->transition_progress = 0.0f;
                    priv->transition_duration = lrg_animation_transition_get_duration (trans);

                    if (priv->transition_duration <= 0.0f)
                        priv->transition_duration = 0.001f;

                    lrg_animation_state_enter (priv->next_state);
                    break;
                }
            }
        }
    }

    /* Apply animation to skeleton */
    if (priv->skeleton != NULL)
    {
        GList *bones;
        GList *bl;

        bones = lrg_skeleton_get_bones (priv->skeleton);

        for (bl = bones; bl != NULL; bl = bl->next)
        {
            LrgBone *bone = bl->data;
            const gchar *bone_name;
            LrgBonePose pose;
            gint bone_index;

            bone_name = lrg_bone_get_name (bone);
            bone_index = lrg_bone_get_index (bone);

            lrg_animation_state_sample (priv->current_state, &pose, bone_name);

            if (priv->transitioning && priv->next_state != NULL)
            {
                LrgBonePose next_pose;

                lrg_animation_state_sample (priv->next_state, &next_pose, bone_name);
                lrg_bone_pose_lerp_to (&pose, &next_pose, priv->transition_progress, &pose);
            }

            lrg_skeleton_set_pose (priv->skeleton, bone_index, &pose);
        }

        lrg_skeleton_calculate_world_poses (priv->skeleton);
    }
}

/*
 * GObject virtual methods
 */

static void
lrg_animation_state_machine_finalize (GObject *object)
{
    LrgAnimationStateMachine *self = LRG_ANIMATION_STATE_MACHINE (object);
    LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);

    g_clear_object (&priv->skeleton);
    g_clear_pointer (&priv->states, g_hash_table_unref);
    g_list_free_full (priv->transitions, g_object_unref);
    g_clear_pointer (&priv->parameters, g_hash_table_unref);
    g_free (priv->default_state);

    G_OBJECT_CLASS (lrg_animation_state_machine_parent_class)->finalize (object);
}

static void
lrg_animation_state_machine_get_property (GObject    *object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
    LrgAnimationStateMachine *self = LRG_ANIMATION_STATE_MACHINE (object);
    LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_SKELETON:
        g_value_set_object (value, priv->skeleton);
        break;
    case PROP_RUNNING:
        g_value_set_boolean (value, priv->running);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_animation_state_machine_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
    LrgAnimationStateMachine *self = LRG_ANIMATION_STATE_MACHINE (object);
    LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_SKELETON:
        g_set_object (&priv->skeleton, g_value_get_object (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_animation_state_machine_class_init (LrgAnimationStateMachineClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_animation_state_machine_finalize;
    object_class->get_property = lrg_animation_state_machine_get_property;
    object_class->set_property = lrg_animation_state_machine_set_property;

    klass->update = lrg_animation_state_machine_real_update;

    properties[PROP_SKELETON] =
        g_param_spec_object ("skeleton", "Skeleton", "Target skeleton",
                             LRG_TYPE_SKELETON, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_RUNNING] =
        g_param_spec_boolean ("running", "Running", "Whether running",
                              FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgAnimationStateMachine::state-entered:
     * @self: The state machine
     * @state_name: Name of entered state
     *
     * Emitted when entering a state.
     */
    signals[SIGNAL_STATE_ENTERED] =
        g_signal_new ("state-entered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgAnimationStateMachineClass, state_entered),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_STRING);

    /**
     * LrgAnimationStateMachine::state-exited:
     * @self: The state machine
     * @state_name: Name of exited state
     *
     * Emitted when exiting a state.
     */
    signals[SIGNAL_STATE_EXITED] =
        g_signal_new ("state-exited",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgAnimationStateMachineClass, state_exited),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
free_variant (gpointer data)
{
    if (data != NULL)
        g_variant_unref (data);
}

static void
lrg_animation_state_machine_init (LrgAnimationStateMachine *self)
{
    LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);

    priv->skeleton = NULL;
    priv->states = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
    priv->transitions = NULL;
    priv->parameters = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, free_variant);
    priv->default_state = NULL;
    priv->current_state = NULL;
    priv->next_state = NULL;
    priv->running = FALSE;
    priv->transitioning = FALSE;
    priv->transition_progress = 0.0f;
    priv->transition_duration = 0.0f;
}

/*
 * Public API
 */

LrgAnimationStateMachine *
lrg_animation_state_machine_new (void)
{
    return g_object_new (LRG_TYPE_ANIMATION_STATE_MACHINE, NULL);
}

LrgSkeleton *
lrg_animation_state_machine_get_skeleton (LrgAnimationStateMachine *self)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);

    priv = lrg_animation_state_machine_get_instance_private (self);
    return priv->skeleton;
}

void
lrg_animation_state_machine_set_skeleton (LrgAnimationStateMachine *self,
                                           LrgSkeleton              *skeleton)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));

    priv = lrg_animation_state_machine_get_instance_private (self);

    if (g_set_object (&priv->skeleton, skeleton))
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SKELETON]);
}

void
lrg_animation_state_machine_add_state (LrgAnimationStateMachine *self,
                                        LrgAnimationState        *state)
{
    LrgAnimationStateMachinePrivate *priv;
    const gchar *name;

    g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
    g_return_if_fail (LRG_IS_ANIMATION_STATE (state));

    priv = lrg_animation_state_machine_get_instance_private (self);
    name = lrg_animation_state_get_name (state);

    g_hash_table_insert (priv->states, g_strdup (name), g_object_ref (state));
}

void
lrg_animation_state_machine_remove_state (LrgAnimationStateMachine *self,
                                           const gchar              *name)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
    g_return_if_fail (name != NULL);

    priv = lrg_animation_state_machine_get_instance_private (self);
    g_hash_table_remove (priv->states, name);
}

LrgAnimationState *
lrg_animation_state_machine_get_state (LrgAnimationStateMachine *self,
                                        const gchar              *name)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    priv = lrg_animation_state_machine_get_instance_private (self);
    return g_hash_table_lookup (priv->states, name);
}

GList *
lrg_animation_state_machine_get_states (LrgAnimationStateMachine *self)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);

    priv = lrg_animation_state_machine_get_instance_private (self);
    return g_hash_table_get_values (priv->states);
}

void
lrg_animation_state_machine_add_transition (LrgAnimationStateMachine *self,
                                             LrgAnimationTransition   *transition)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
    g_return_if_fail (LRG_IS_ANIMATION_TRANSITION (transition));

    priv = lrg_animation_state_machine_get_instance_private (self);
    priv->transitions = g_list_append (priv->transitions, g_object_ref (transition));
}

GList *
lrg_animation_state_machine_get_transitions (LrgAnimationStateMachine *self)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);

    priv = lrg_animation_state_machine_get_instance_private (self);
    return g_list_copy (priv->transitions);
}

void
lrg_animation_state_machine_set_default_state (LrgAnimationStateMachine *self,
                                                const gchar              *name)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));

    priv = lrg_animation_state_machine_get_instance_private (self);
    g_free (priv->default_state);
    priv->default_state = g_strdup (name);
}

const gchar *
lrg_animation_state_machine_get_default_state (LrgAnimationStateMachine *self)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);

    priv = lrg_animation_state_machine_get_instance_private (self);
    return priv->default_state;
}

LrgAnimationState *
lrg_animation_state_machine_get_current_state (LrgAnimationStateMachine *self)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);

    priv = lrg_animation_state_machine_get_instance_private (self);
    return priv->current_state;
}

const gchar *
lrg_animation_state_machine_get_current_state_name (LrgAnimationStateMachine *self)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);

    priv = lrg_animation_state_machine_get_instance_private (self);

    if (priv->current_state == NULL)
        return NULL;

    return lrg_animation_state_get_name (priv->current_state);
}

void
lrg_animation_state_machine_set_parameter (LrgAnimationStateMachine *self,
                                            const gchar              *name,
                                            GVariant                 *value)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
    g_return_if_fail (name != NULL);

    priv = lrg_animation_state_machine_get_instance_private (self);

    if (value != NULL)
        g_hash_table_insert (priv->parameters, g_strdup (name), g_variant_ref_sink (value));
    else
        g_hash_table_remove (priv->parameters, name);
}

GVariant *
lrg_animation_state_machine_get_parameter (LrgAnimationStateMachine *self,
                                            const gchar              *name)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    priv = lrg_animation_state_machine_get_instance_private (self);
    return g_hash_table_lookup (priv->parameters, name);
}

void
lrg_animation_state_machine_set_float (LrgAnimationStateMachine *self,
                                        const gchar              *name,
                                        gfloat                    value)
{
    lrg_animation_state_machine_set_parameter (self, name,
                                                g_variant_new_double ((gdouble)value));
}

gfloat
lrg_animation_state_machine_get_float (LrgAnimationStateMachine *self,
                                        const gchar              *name)
{
    GVariant *v;

    v = lrg_animation_state_machine_get_parameter (self, name);
    if (v == NULL)
        return 0.0f;

    if (g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE))
        return (gfloat)g_variant_get_double (v);

    return 0.0f;
}

void
lrg_animation_state_machine_set_bool (LrgAnimationStateMachine *self,
                                       const gchar              *name,
                                       gboolean                  value)
{
    lrg_animation_state_machine_set_parameter (self, name,
                                                g_variant_new_boolean (value));
}

gboolean
lrg_animation_state_machine_get_bool (LrgAnimationStateMachine *self,
                                       const gchar              *name)
{
    GVariant *v;

    v = lrg_animation_state_machine_get_parameter (self, name);
    if (v == NULL)
        return FALSE;

    if (g_variant_is_of_type (v, G_VARIANT_TYPE_BOOLEAN))
        return g_variant_get_boolean (v);

    return FALSE;
}

void
lrg_animation_state_machine_set_trigger (LrgAnimationStateMachine *self,
                                          const gchar              *name)
{
    lrg_animation_state_machine_set_bool (self, name, TRUE);
}

void
lrg_animation_state_machine_reset_trigger (LrgAnimationStateMachine *self,
                                            const gchar              *name)
{
    lrg_animation_state_machine_set_bool (self, name, FALSE);
}

void
lrg_animation_state_machine_start (LrgAnimationStateMachine *self)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));

    priv = lrg_animation_state_machine_get_instance_private (self);

    if (priv->default_state != NULL)
    {
        priv->current_state = g_hash_table_lookup (priv->states, priv->default_state);

        if (priv->current_state != NULL)
        {
            lrg_animation_state_enter (priv->current_state);
            g_signal_emit (self, signals[SIGNAL_STATE_ENTERED], 0,
                           lrg_animation_state_get_name (priv->current_state));
        }
    }

    priv->running = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RUNNING]);
}

void
lrg_animation_state_machine_stop (LrgAnimationStateMachine *self)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));

    priv = lrg_animation_state_machine_get_instance_private (self);

    if (priv->current_state != NULL)
    {
        lrg_animation_state_exit (priv->current_state);
        g_signal_emit (self, signals[SIGNAL_STATE_EXITED], 0,
                       lrg_animation_state_get_name (priv->current_state));
    }

    priv->running = FALSE;
    priv->current_state = NULL;
    priv->next_state = NULL;
    priv->transitioning = FALSE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RUNNING]);
}

void
lrg_animation_state_machine_update (LrgAnimationStateMachine *self,
                                     gfloat                    delta_time)
{
    LrgAnimationStateMachineClass *klass;

    g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));

    klass = LRG_ANIMATION_STATE_MACHINE_GET_CLASS (self);
    if (klass->update != NULL)
        klass->update (self, delta_time);
}

void
lrg_animation_state_machine_force_state (LrgAnimationStateMachine *self,
                                          const gchar              *name)
{
    LrgAnimationStateMachinePrivate *priv;
    LrgAnimationState *new_state;

    g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
    g_return_if_fail (name != NULL);

    priv = lrg_animation_state_machine_get_instance_private (self);

    new_state = g_hash_table_lookup (priv->states, name);
    if (new_state == NULL)
        return;

    /* Exit current state */
    if (priv->current_state != NULL)
    {
        lrg_animation_state_exit (priv->current_state);
        g_signal_emit (self, signals[SIGNAL_STATE_EXITED], 0,
                       lrg_animation_state_get_name (priv->current_state));
    }

    /* Cancel any ongoing transition */
    priv->transitioning = FALSE;
    priv->next_state = NULL;
    priv->transition_progress = 0.0f;

    /* Enter new state */
    priv->current_state = new_state;
    lrg_animation_state_enter (priv->current_state);
    g_signal_emit (self, signals[SIGNAL_STATE_ENTERED], 0, name);
}

gboolean
lrg_animation_state_machine_is_running (LrgAnimationStateMachine *self)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), FALSE);

    priv = lrg_animation_state_machine_get_instance_private (self);
    return priv->running;
}

gboolean
lrg_animation_state_machine_is_transitioning (LrgAnimationStateMachine *self)
{
    LrgAnimationStateMachinePrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), FALSE);

    priv = lrg_animation_state_machine_get_instance_private (self);
    return priv->transitioning;
}
