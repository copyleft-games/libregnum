/* lrg-animation-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Animation state transition rules.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_ANIMATION_TRANSITION_ANY_STATE:
 *
 * The source name of an any-state transition: it may fire from whichever
 * state the machine is in (except its own target, unless
 * lrg_animation_transition_set_can_transition_to_self() allows it).
 */
#define LRG_ANIMATION_TRANSITION_ANY_STATE "*"

/**
 * LrgTransitionInterruption:
 * @LRG_TRANSITION_INTERRUPTION_NONE: the transition always runs to the end
 * @LRG_TRANSITION_INTERRUPTION_SOURCE: transitions leaving the source state
 *   may cut in
 * @LRG_TRANSITION_INTERRUPTION_DESTINATION: transitions leaving the
 *   destination state may cut in
 * @LRG_TRANSITION_INTERRUPTION_SOURCE_THEN_DESTINATION: the source's
 *   transitions are checked first, then the destination's
 * @LRG_TRANSITION_INTERRUPTION_DESTINATION_THEN_SOURCE: the destination's
 *   transitions are checked first, then the source's
 *
 * Which transitions may interrupt a transition in progress. An interrupted
 * blend continues from the pose the machine had at that moment.
 */
typedef enum
{
    LRG_TRANSITION_INTERRUPTION_NONE,
    LRG_TRANSITION_INTERRUPTION_SOURCE,
    LRG_TRANSITION_INTERRUPTION_DESTINATION,
    LRG_TRANSITION_INTERRUPTION_SOURCE_THEN_DESTINATION,
    LRG_TRANSITION_INTERRUPTION_DESTINATION_THEN_SOURCE
} LrgTransitionInterruption;

LRG_AVAILABLE_IN_ALL
GType lrg_transition_interruption_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TRANSITION_INTERRUPTION (lrg_transition_interruption_get_type ())

/**
 * LrgTransitionCondition:
 * @parameter: Parameter name to check
 * @comparison: Comparison type
 * @value: Value to compare against
 *
 * A single condition for a transition.
 */
typedef struct _LrgTransitionCondition LrgTransitionCondition;

struct _LrgTransitionCondition
{
    gchar    *parameter;
    gint      comparison;  /* LrgConditionComparison */
    GVariant *value;
};

#define LRG_TYPE_TRANSITION_CONDITION (lrg_transition_condition_get_type ())

LRG_AVAILABLE_IN_ALL
GType                   lrg_transition_condition_get_type   (void) G_GNUC_CONST;

LRG_AVAILABLE_IN_ALL
LrgTransitionCondition *lrg_transition_condition_new        (const gchar    *parameter,
                                                             gint            comparison,
                                                             GVariant       *value);

LRG_AVAILABLE_IN_ALL
LrgTransitionCondition *lrg_transition_condition_copy       (const LrgTransitionCondition *cond);

LRG_AVAILABLE_IN_ALL
void                    lrg_transition_condition_free       (LrgTransitionCondition *cond);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgTransitionCondition, lrg_transition_condition_free)


#define LRG_TYPE_ANIMATION_TRANSITION (lrg_animation_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAnimationTransition, lrg_animation_transition, LRG, ANIMATION_TRANSITION, GObject)

/**
 * lrg_animation_transition_new:
 * @source: Source state name
 * @target: Target state name
 *
 * Creates a new transition between states.
 *
 * Returns: (transfer full): A new #LrgAnimationTransition
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationTransition *lrg_animation_transition_new            (const gchar    *source,
                                                                  const gchar    *target);

/**
 * lrg_animation_transition_get_source:
 * @self: A #LrgAnimationTransition
 *
 * Gets the source state name.
 *
 * Returns: (transfer none): The source state
 */
LRG_AVAILABLE_IN_ALL
const gchar *           lrg_animation_transition_get_source     (LrgAnimationTransition *self);

/**
 * lrg_animation_transition_get_target:
 * @self: A #LrgAnimationTransition
 *
 * Gets the target state name.
 *
 * Returns: (transfer none): The target state
 */
LRG_AVAILABLE_IN_ALL
const gchar *           lrg_animation_transition_get_target     (LrgAnimationTransition *self);

/**
 * lrg_animation_transition_get_duration:
 * @self: A #LrgAnimationTransition
 *
 * Gets the transition blend duration.
 *
 * Returns: Duration in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_animation_transition_get_duration   (LrgAnimationTransition *self);

/**
 * lrg_animation_transition_set_duration:
 * @self: A #LrgAnimationTransition
 * @duration: Duration in seconds
 *
 * Sets the transition blend duration.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_transition_set_duration   (LrgAnimationTransition *self,
                                                                  gfloat                  duration);

/**
 * lrg_animation_transition_get_exit_time:
 * @self: A #LrgAnimationTransition
 *
 * Gets the normalized exit time (0.0-1.0).
 * Transition can only occur after this time in the source animation.
 * -1 means no exit time requirement.
 *
 * Returns: The exit time or -1
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_animation_transition_get_exit_time  (LrgAnimationTransition *self);

/**
 * lrg_animation_transition_set_exit_time:
 * @self: A #LrgAnimationTransition
 * @exit_time: Normalized exit time or -1 for none
 *
 * Sets the exit time requirement.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_transition_set_exit_time  (LrgAnimationTransition *self,
                                                                  gfloat                  exit_time);

/**
 * lrg_animation_transition_get_has_exit_time:
 * @self: A #LrgAnimationTransition
 *
 * Checks if this transition has an exit time requirement.
 *
 * Returns: %TRUE if has exit time
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_animation_transition_get_has_exit_time (LrgAnimationTransition *self);

/**
 * lrg_animation_transition_set_has_exit_time:
 * @self: A #LrgAnimationTransition
 * @has_exit_time: Whether to require exit time
 *
 * Sets whether this transition requires exit time.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_transition_set_has_exit_time (LrgAnimationTransition *self,
                                                                     gboolean                has_exit_time);

/**
 * lrg_animation_transition_add_condition:
 * @self: A #LrgAnimationTransition
 * @parameter: Parameter name
 * @comparison: Comparison operator
 * @value: Value to compare against
 *
 * Adds a condition that must be true for this transition.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_transition_add_condition  (LrgAnimationTransition *self,
                                                                  const gchar            *parameter,
                                                                  gint                    comparison,
                                                                  GVariant               *value);

/**
 * lrg_animation_transition_clear_conditions:
 * @self: A #LrgAnimationTransition
 *
 * Removes all conditions.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_transition_clear_conditions (LrgAnimationTransition *self);

/**
 * lrg_animation_transition_get_conditions:
 * @self: A #LrgAnimationTransition
 *
 * Gets all conditions.
 *
 * Returns: (transfer none) (element-type LrgTransitionCondition): The conditions
 */
LRG_AVAILABLE_IN_ALL
GList *                 lrg_animation_transition_get_conditions (LrgAnimationTransition *self);

/**
 * lrg_animation_transition_get_condition_count:
 * @self: A #LrgAnimationTransition
 *
 * Gets the number of conditions.
 *
 * Returns: Condition count
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_animation_transition_get_condition_count (LrgAnimationTransition *self);

/**
 * lrg_animation_transition_evaluate:
 * @self: A #LrgAnimationTransition
 * @parameters: (element-type utf8 GVariant): Parameter values
 * @source_normalized_time: Current normalized time in source state
 *
 * Evaluates if this transition should fire.
 *
 * Returns: %TRUE if all conditions are met
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_animation_transition_evaluate       (LrgAnimationTransition *self,
                                                                  GHashTable             *parameters,
                                                                  gfloat                  source_normalized_time);

/**
 * lrg_animation_transition_get_priority:
 * @self: A #LrgAnimationTransition
 *
 * Gets the transition priority. Higher priority transitions
 * are evaluated first.
 *
 * Returns: The priority
 */
LRG_AVAILABLE_IN_ALL
gint                    lrg_animation_transition_get_priority   (LrgAnimationTransition *self);

/**
 * lrg_animation_transition_set_priority:
 * @self: A #LrgAnimationTransition
 * @priority: The priority
 *
 * Sets the transition priority.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_transition_set_priority   (LrgAnimationTransition *self,
                                                                  gint                    priority);

/**
 * lrg_animation_transition_set_source:
 * @self: A #LrgAnimationTransition
 * @source: the source state name, or %LRG_ANIMATION_TRANSITION_ANY_STATE
 *
 * Re-points the transition's source (the state machine editor drags
 * transition ends with it).
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_transition_set_source     (LrgAnimationTransition *self,
                                                                  const gchar            *source);

/**
 * lrg_animation_transition_set_target:
 * @self: A #LrgAnimationTransition
 * @target: the target state name
 *
 * Re-points the transition's target.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_transition_set_target     (LrgAnimationTransition *self,
                                                                  const gchar            *target);

/**
 * lrg_animation_transition_is_any_state:
 * @self: A #LrgAnimationTransition
 *
 * Returns: whether the source is %LRG_ANIMATION_TRANSITION_ANY_STATE
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_animation_transition_is_any_state   (LrgAnimationTransition *self);

/**
 * lrg_animation_transition_get_offset:
 * @self: A #LrgAnimationTransition
 *
 * Returns: the normalized time (0..1) the target state starts at
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_animation_transition_get_offset     (LrgAnimationTransition *self);

/**
 * lrg_animation_transition_set_offset:
 * @self: A #LrgAnimationTransition
 * @offset: normalized start time of the target, 0..1
 *
 * Starts the target state part way in (for example a run cycle entered
 * on the opposite foot).
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_transition_set_offset     (LrgAnimationTransition *self,
                                                                  gfloat                  offset);

/**
 * lrg_animation_transition_get_interruption:
 * @self: A #LrgAnimationTransition
 *
 * Returns: which transitions may interrupt this one
 */
LRG_AVAILABLE_IN_ALL
LrgTransitionInterruption lrg_animation_transition_get_interruption (LrgAnimationTransition *self);

/**
 * lrg_animation_transition_set_interruption:
 * @self: A #LrgAnimationTransition
 * @interruption: the interruption source
 *
 * Sets which transitions may cut this one short.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_transition_set_interruption (LrgAnimationTransition    *self,
                                                                    LrgTransitionInterruption  interruption);

/**
 * lrg_animation_transition_get_can_transition_to_self:
 * @self: A #LrgAnimationTransition
 *
 * Returns: whether an any-state transition may fire while its target is
 *   already the current state
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_animation_transition_get_can_transition_to_self (LrgAnimationTransition *self);

/**
 * lrg_animation_transition_set_can_transition_to_self:
 * @self: A #LrgAnimationTransition
 * @allow: whether to restart the target from itself
 *
 * Allows an any-state transition to re-enter its own target (a hit
 * reaction restarting on every hit).
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_transition_set_can_transition_to_self (LrgAnimationTransition *self,
                                                                             gboolean                allow);

/**
 * lrg_animation_transition_evaluate_range:
 * @self: A #LrgAnimationTransition
 * @parameters: (element-type utf8 GVariant): parameter values
 * @previous_normalized_time: the source's normalized time before the
 *   last update (not wrapped: 2.5 is half way through the third loop)
 * @normalized_time: the source's normalized time now
 *
 * Like lrg_animation_transition_evaluate() with crossing semantics for
 * the exit time: an exit time below 1 is reached once per loop, when
 * the fractional part of the normalized time passes it during the last
 * update; an exit time of 1 or more is reached once, when the
 * normalized time passes it. The state machine uses this form.
 *
 * Returns: %TRUE if the transition should fire
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_animation_transition_evaluate_range (LrgAnimationTransition *self,
                                                                  GHashTable             *parameters,
                                                                  gfloat                  previous_normalized_time,
                                                                  gfloat                  normalized_time);

G_END_DECLS
