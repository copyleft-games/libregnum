/* lrg-animation-state-machine.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Animation state machine controller.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-skeleton.h"
#include "lrg-animation-state.h"
#include "lrg-animation-transition.h"

G_BEGIN_DECLS

/**
 * LrgAnimParameterType:
 * @LRG_ANIM_PARAMETER_FLOAT: a float (stored as a double #GVariant)
 * @LRG_ANIM_PARAMETER_INT: an integer (int32 #GVariant)
 * @LRG_ANIM_PARAMETER_BOOL: a boolean
 * @LRG_ANIM_PARAMETER_TRIGGER: a boolean that the first transition firing
 *   because of it resets (jump, attack, hit reactions)
 *
 * The declared type of a state machine parameter.
 */
typedef enum
{
    LRG_ANIM_PARAMETER_FLOAT,
    LRG_ANIM_PARAMETER_INT,
    LRG_ANIM_PARAMETER_BOOL,
    LRG_ANIM_PARAMETER_TRIGGER
} LrgAnimParameterType;

LRG_AVAILABLE_IN_ALL
GType lrg_anim_parameter_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ANIM_PARAMETER_TYPE (lrg_anim_parameter_type_get_type ())

#define LRG_TYPE_ANIMATION_STATE_MACHINE (lrg_animation_state_machine_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgAnimationStateMachine, lrg_animation_state_machine,
                          LRG, ANIMATION_STATE_MACHINE, GObject)

/**
 * LrgAnimationStateMachineClass:
 * @parent_class: Parent class
 * @update: Virtual method to update the state machine
 * @state_entered: Signal when entering a state
 * @state_exited: Signal when exiting a state
 *
 * Class structure for #LrgAnimationStateMachine.
 */
struct _LrgAnimationStateMachineClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    void (*update) (LrgAnimationStateMachine *self,
                    gfloat                    delta_time);

    /* Signals */
    void (*state_entered) (LrgAnimationStateMachine *self,
                           const gchar              *state_name);
    void (*state_exited)  (LrgAnimationStateMachine *self,
                           const gchar              *state_name);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_animation_state_machine_new:
 *
 * Creates a new animation state machine.
 *
 * Returns: (transfer full): A new #LrgAnimationStateMachine
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationStateMachine *lrg_animation_state_machine_new (void);

/**
 * lrg_animation_state_machine_get_skeleton:
 * @self: A #LrgAnimationStateMachine
 *
 * Gets the skeleton.
 *
 * Returns: (transfer none) (nullable): The skeleton
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton *   lrg_animation_state_machine_get_skeleton    (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_set_skeleton:
 * @self: A #LrgAnimationStateMachine
 * @skeleton: (nullable): The skeleton
 *
 * Sets the skeleton to animate.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_set_skeleton    (LrgAnimationStateMachine *self,
                                                              LrgSkeleton              *skeleton);

/**
 * lrg_animation_state_machine_add_state:
 * @self: A #LrgAnimationStateMachine
 * @state: The state to add
 *
 * Adds a state to the machine.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_add_state       (LrgAnimationStateMachine *self,
                                                              LrgAnimationState        *state);

/**
 * lrg_animation_state_machine_remove_state:
 * @self: A #LrgAnimationStateMachine
 * @name: State name to remove
 *
 * Removes a state by name.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_remove_state    (LrgAnimationStateMachine *self,
                                                              const gchar              *name);

/**
 * lrg_animation_state_machine_get_state:
 * @self: A #LrgAnimationStateMachine
 * @name: State name
 *
 * Gets a state by name.
 *
 * Returns: (transfer none) (nullable): The state
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationState *lrg_animation_state_machine_get_state    (LrgAnimationStateMachine *self,
                                                              const gchar              *name);

/**
 * lrg_animation_state_machine_get_states:
 * @self: A #LrgAnimationStateMachine
 *
 * Gets all states.
 *
 * Returns: (transfer container) (element-type LrgAnimationState): List of states
 */
LRG_AVAILABLE_IN_ALL
GList *         lrg_animation_state_machine_get_states      (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_add_transition:
 * @self: A #LrgAnimationStateMachine
 * @transition: The transition to add
 *
 * Adds a transition.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_add_transition  (LrgAnimationStateMachine  *self,
                                                              LrgAnimationTransition    *transition);

/**
 * lrg_animation_state_machine_get_transitions:
 * @self: A #LrgAnimationStateMachine
 *
 * Gets all transitions.
 *
 * Returns: (transfer container) (element-type LrgAnimationTransition): Transitions
 */
LRG_AVAILABLE_IN_ALL
GList *         lrg_animation_state_machine_get_transitions (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_set_default_state:
 * @self: A #LrgAnimationStateMachine
 * @name: Default state name
 *
 * Sets the default/entry state.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_set_default_state (LrgAnimationStateMachine *self,
                                                                const gchar              *name);

/**
 * lrg_animation_state_machine_get_default_state:
 * @self: A #LrgAnimationStateMachine
 *
 * Gets the default state name.
 *
 * Returns: (transfer none) (nullable): The default state name
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_animation_state_machine_get_default_state (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_get_current_state:
 * @self: A #LrgAnimationStateMachine
 *
 * Gets the current state.
 *
 * Returns: (transfer none) (nullable): The current state
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationState *lrg_animation_state_machine_get_current_state (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_get_current_state_name:
 * @self: A #LrgAnimationStateMachine
 *
 * Gets the current state name.
 *
 * Returns: (transfer none) (nullable): The current state name
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_animation_state_machine_get_current_state_name (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_set_parameter:
 * @self: A #LrgAnimationStateMachine
 * @name: Parameter name
 * @value: Parameter value
 *
 * Sets a parameter value.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_set_parameter   (LrgAnimationStateMachine *self,
                                                              const gchar              *name,
                                                              GVariant                 *value);

/**
 * lrg_animation_state_machine_get_parameter:
 * @self: A #LrgAnimationStateMachine
 * @name: Parameter name
 *
 * Gets a parameter value.
 *
 * Returns: (transfer none) (nullable): The parameter value
 */
LRG_AVAILABLE_IN_ALL
GVariant *      lrg_animation_state_machine_get_parameter   (LrgAnimationStateMachine *self,
                                                              const gchar              *name);

/**
 * lrg_animation_state_machine_set_float:
 * @self: A #LrgAnimationStateMachine
 * @name: Parameter name
 * @value: Float value
 *
 * Sets a float parameter.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_set_float       (LrgAnimationStateMachine *self,
                                                              const gchar              *name,
                                                              gfloat                    value);

/**
 * lrg_animation_state_machine_get_float:
 * @self: A #LrgAnimationStateMachine
 * @name: Parameter name
 *
 * Gets a float parameter.
 *
 * Returns: The float value or 0.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_animation_state_machine_get_float       (LrgAnimationStateMachine *self,
                                                              const gchar              *name);

/**
 * lrg_animation_state_machine_set_bool:
 * @self: A #LrgAnimationStateMachine
 * @name: Parameter name
 * @value: Boolean value
 *
 * Sets a boolean parameter.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_set_bool        (LrgAnimationStateMachine *self,
                                                              const gchar              *name,
                                                              gboolean                  value);

/**
 * lrg_animation_state_machine_get_bool:
 * @self: A #LrgAnimationStateMachine
 * @name: Parameter name
 *
 * Gets a boolean parameter.
 *
 * Returns: The boolean value
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_animation_state_machine_get_bool        (LrgAnimationStateMachine *self,
                                                              const gchar              *name);

/**
 * lrg_animation_state_machine_set_trigger:
 * @self: A #LrgAnimationStateMachine
 * @name: Trigger name
 *
 * Sets a trigger (auto-resets after transition).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_set_trigger     (LrgAnimationStateMachine *self,
                                                              const gchar              *name);

/**
 * lrg_animation_state_machine_reset_trigger:
 * @self: A #LrgAnimationStateMachine
 * @name: Trigger name
 *
 * Resets a trigger.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_reset_trigger   (LrgAnimationStateMachine *self,
                                                              const gchar              *name);

/**
 * lrg_animation_state_machine_start:
 * @self: A #LrgAnimationStateMachine
 *
 * Starts the state machine from the default state.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_start           (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_stop:
 * @self: A #LrgAnimationStateMachine
 *
 * Stops the state machine.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_stop            (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_update:
 * @self: A #LrgAnimationStateMachine
 * @delta_time: Time since last frame
 *
 * Updates the state machine.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_update          (LrgAnimationStateMachine *self,
                                                              gfloat                    delta_time);

/**
 * lrg_animation_state_machine_force_state:
 * @self: A #LrgAnimationStateMachine
 * @name: State name to force
 *
 * Forces an immediate transition to a state (no blending).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_force_state     (LrgAnimationStateMachine *self,
                                                              const gchar              *name);

/**
 * lrg_animation_state_machine_is_running:
 * @self: A #LrgAnimationStateMachine
 *
 * Checks if the machine is running.
 *
 * Returns: %TRUE if running
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_animation_state_machine_is_running      (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_is_transitioning:
 * @self: A #LrgAnimationStateMachine
 *
 * Checks if currently transitioning between states.
 *
 * Returns: %TRUE if transitioning
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_animation_state_machine_is_transitioning (LrgAnimationStateMachine *self);

/* ==========================================================================
 * 0.9.0: typed parameters, any-state transitions, interruption, whole
 * skeleton evaluation, events and root motion
 * ========================================================================== */

/**
 * lrg_animation_state_machine_add_parameter:
 * @self: A #LrgAnimationStateMachine
 * @name: the parameter name
 * @type: its type
 * @default_value: (nullable): the default (double, int32 or boolean
 *   #GVariant), %NULL for 0 / %FALSE; floating references are sunk
 *
 * Declares a typed parameter and sets it to its default. Undeclared
 * parameters still work (the type is taken from the value), but only
 * declared triggers are consumed by the transitions they fire.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_add_parameter   (LrgAnimationStateMachine *self,
                                                              const gchar              *name,
                                                              LrgAnimParameterType      type,
                                                              GVariant                 *default_value);

/**
 * lrg_animation_state_machine_remove_parameter:
 * @self: A #LrgAnimationStateMachine
 * @name: the parameter name
 *
 * Removes a parameter's declaration and value.
 *
 * Returns: %TRUE if it was declared
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_animation_state_machine_remove_parameter (LrgAnimationStateMachine *self,
                                                               const gchar              *name);

/**
 * lrg_animation_state_machine_has_parameter:
 * @self: A #LrgAnimationStateMachine
 * @name: the parameter name
 *
 * Returns: whether @name is declared
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_animation_state_machine_has_parameter   (LrgAnimationStateMachine *self,
                                                              const gchar              *name);

/**
 * lrg_animation_state_machine_get_parameter_type:
 * @self: A #LrgAnimationStateMachine
 * @name: the parameter name
 *
 * Returns: the declared type (%LRG_ANIM_PARAMETER_FLOAT when undeclared)
 */
LRG_AVAILABLE_IN_ALL
LrgAnimParameterType lrg_animation_state_machine_get_parameter_type (LrgAnimationStateMachine *self,
                                                                     const gchar              *name);

/**
 * lrg_animation_state_machine_get_parameter_names:
 * @self: A #LrgAnimationStateMachine
 *
 * Returns: (transfer full) (array zero-terminated=1): the declared
 *   parameter names in declaration order
 */
LRG_AVAILABLE_IN_ALL
gchar **        lrg_animation_state_machine_get_parameter_names (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_reset_parameters:
 * @self: A #LrgAnimationStateMachine
 *
 * Sets every declared parameter back to its default.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_reset_parameters (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_share_parameters:
 * @self: A #LrgAnimationStateMachine
 * @owner: (nullable): the machine whose parameters @self should use, or
 *   %NULL to give @self its own again
 *
 * Makes @self read and write @owner's parameter table (declarations and
 * values): the layers of an #LrgAnimGraph and nested sub-machines share
 * one set of parameters this way.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_share_parameters (LrgAnimationStateMachine *self,
                                                               LrgAnimationStateMachine *owner);

/**
 * lrg_animation_state_machine_set_int:
 * @self: A #LrgAnimationStateMachine
 * @name: Parameter name
 * @value: Value
 *
 * Sets an integer parameter.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_set_int         (LrgAnimationStateMachine *self,
                                                              const gchar              *name,
                                                              gint                      value);

/**
 * lrg_animation_state_machine_get_int:
 * @self: A #LrgAnimationStateMachine
 * @name: Parameter name
 *
 * Returns: the value (a float parameter is truncated; 0 when unset)
 */
LRG_AVAILABLE_IN_ALL
gint            lrg_animation_state_machine_get_int         (LrgAnimationStateMachine *self,
                                                              const gchar              *name);

/**
 * lrg_animation_state_machine_get_states_ordered:
 * @self: A #LrgAnimationStateMachine
 *
 * Gets the states in the order they were added (the order a .ranim
 * file lists them in).
 *
 * Returns: (transfer container) (element-type LrgAnimationState): the
 *   states
 */
LRG_AVAILABLE_IN_ALL
GList *         lrg_animation_state_machine_get_states_ordered (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_remove_transition:
 * @self: A #LrgAnimationStateMachine
 * @transition: a transition of @self
 *
 * Removes a transition.
 *
 * Returns: %TRUE if it was found
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_animation_state_machine_remove_transition (LrgAnimationStateMachine *self,
                                                                LrgAnimationTransition   *transition);

/**
 * lrg_animation_state_machine_get_next_state:
 * @self: A #LrgAnimationStateMachine
 *
 * Returns: (transfer none) (nullable): the state being blended in during
 *   a transition
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationState *lrg_animation_state_machine_get_next_state (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_get_transition_progress:
 * @self: A #LrgAnimationStateMachine
 *
 * Returns: the blend weight of the next state, 0..1 (0 when not
 *   transitioning)
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_animation_state_machine_get_transition_progress (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_get_active_transition:
 * @self: A #LrgAnimationStateMachine
 *
 * Returns: (transfer none) (nullable): the transition in progress (the
 *   editor highlights it)
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationTransition *lrg_animation_state_machine_get_active_transition (LrgAnimationStateMachine *self);

/**
 * lrg_animation_state_machine_sample_pose:
 * @self: A #LrgAnimationStateMachine
 * @skeleton: the skeleton the pose is for
 * @poses: (array length=n_poses) (out caller-allocates): local poses by
 *   bone index
 * @n_poses: number of entries
 *
 * Samples the current state (blended with the next state during a
 * transition; after an interruption the blend starts from the pose the
 * machine had when it was interrupted) without touching the skeleton.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_sample_pose     (LrgAnimationStateMachine *self,
                                                              LrgSkeleton              *skeleton,
                                                              LrgBonePose              *poses,
                                                              guint                     n_poses);

/**
 * lrg_animation_state_machine_get_root_motion:
 * @self: A #LrgAnimationStateMachine
 * @bone_name: the root motion bone
 * @out_translation: (out caller-allocates) (array fixed-size=3): local
 *   translation change over the last update
 * @out_yaw: (out) (optional): turn about +Y over the last update
 *
 * The root motion of the last update: the current state's (see
 * lrg_animation_state_get_root_motion()), blended with the next state's
 * by the transition weight.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_animation_state_machine_get_root_motion (LrgAnimationStateMachine *self,
                                                              const gchar              *bone_name,
                                                              gfloat                   *out_translation,
                                                              gfloat                   *out_yaw);

G_END_DECLS
