/* lrg-logic-machine.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLogicMachine: the .rfsm generic logic state machine. Hierarchical
 * states (LrgLogicState) with enter/update/exit action lists, transitions
 * (LrgLogicTransition) fired by events, timers and blackboard conditions,
 * and a runtime that keeps the active state chain, per-state timers and the
 * debug state an editor's live view needs. Actions and conditions are
 * registered C callbacks (the lrg_bt_register_action() registry shared
 * with .rbt trees), script functions, blackboard writes or events.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-blackboard.h"
#include "lrg-bt-asset.h"
#include "../scripting/lrg-scripting.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Errors and enumerations
 * ========================================================================== */

/**
 * LRG_LOGIC_MACHINE_ERROR:
 *
 * Error domain for #LrgLogicMachine.
 *
 * Since: 0.9.0
 */
#define LRG_LOGIC_MACHINE_ERROR (lrg_logic_machine_error_quark ())

/**
 * lrg_logic_machine_error_quark:
 *
 * Gets the error quark for #LrgLogicMachine.
 *
 * Returns: the quark
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_logic_machine_error_quark (void);

/**
 * LrgLogicMachineError:
 * @LRG_LOGIC_MACHINE_ERROR_PARSE: the text is not YAML or not a machine
 *   mapping
 * @LRG_LOGIC_MACHINE_ERROR_VERSION: the file's version is newer than this
 *   build reads
 * @LRG_LOGIC_MACHINE_ERROR_INVALID: a value has the wrong type, a required
 *   key is missing, a state name is duplicated, or an action/condition
 *   entry is malformed
 * @LRG_LOGIC_MACHINE_ERROR_REFERENCE: a transition target, an initial
 *   state or a blackboard key does not exist
 * @LRG_LOGIC_MACHINE_ERROR_BINDING: an action, condition or script
 *   function is not registered
 * @LRG_LOGIC_MACHINE_ERROR_IO: reading or writing a file failed
 *
 * Error codes in the %LRG_LOGIC_MACHINE_ERROR domain. Messages start with
 * the key path, for example
 * `states[1].transitions[0].to: unknown state 'attak'`.
 *
 * Since: 0.9.0
 */
typedef enum
{
	LRG_LOGIC_MACHINE_ERROR_PARSE,
	LRG_LOGIC_MACHINE_ERROR_VERSION,
	LRG_LOGIC_MACHINE_ERROR_INVALID,
	LRG_LOGIC_MACHINE_ERROR_REFERENCE,
	LRG_LOGIC_MACHINE_ERROR_BINDING,
	LRG_LOGIC_MACHINE_ERROR_IO
} LrgLogicMachineError;

LRG_AVAILABLE_IN_ALL
GType lrg_logic_machine_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LOGIC_MACHINE_ERROR (lrg_logic_machine_error_get_type ())

/**
 * LrgLogicPhase:
 * @LRG_LOGIC_PHASE_ENTER: run when the state becomes active (`enter:`)
 * @LRG_LOGIC_PHASE_UPDATE: run on every update while active (`update:`)
 * @LRG_LOGIC_PHASE_EXIT: run when the state stops being active (`exit:`)
 *
 * When a state's action list runs.
 *
 * Since: 0.9.0
 */
typedef enum
{
	LRG_LOGIC_PHASE_ENTER,
	LRG_LOGIC_PHASE_UPDATE,
	LRG_LOGIC_PHASE_EXIT
} LrgLogicPhase;

LRG_AVAILABLE_IN_ALL
GType lrg_logic_phase_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LOGIC_PHASE (lrg_logic_phase_get_type ())

/**
 * LrgLogicActionKind:
 * @LRG_LOGIC_ACTION_CALL: call a registered C action (`action: name`)
 * @LRG_LOGIC_ACTION_SCRIPT: call a script function (`script: name`)
 * @LRG_LOGIC_ACTION_SET: write a blackboard key (`set: key`, `value:`)
 * @LRG_LOGIC_ACTION_EVENT: queue an event on the machine itself
 *   (`event: name`), handled on the next update
 *
 * What an action does.
 *
 * Since: 0.9.0
 */
typedef enum
{
	LRG_LOGIC_ACTION_CALL,
	LRG_LOGIC_ACTION_SCRIPT,
	LRG_LOGIC_ACTION_SET,
	LRG_LOGIC_ACTION_EVENT
} LrgLogicActionKind;

LRG_AVAILABLE_IN_ALL
GType lrg_logic_action_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LOGIC_ACTION_KIND (lrg_logic_action_kind_get_type ())

/**
 * LrgLogicConditionKind:
 * @LRG_LOGIC_CONDITION_CHECK: a blackboard comparison (`key:`, `op:`,
 *   `value:`)
 * @LRG_LOGIC_CONDITION_CALL: a registered C condition (`condition: name`)
 * @LRG_LOGIC_CONDITION_SCRIPT: a script function (`script: name`)
 *
 * What a transition condition tests.
 *
 * Since: 0.9.0
 */
typedef enum
{
	LRG_LOGIC_CONDITION_CHECK,
	LRG_LOGIC_CONDITION_CALL,
	LRG_LOGIC_CONDITION_SCRIPT
} LrgLogicConditionKind;

LRG_AVAILABLE_IN_ALL
GType lrg_logic_condition_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LOGIC_CONDITION_KIND (lrg_logic_condition_kind_get_type ())

/**
 * LRG_LOGIC_MACHINE_FORMAT_VERSION:
 *
 * The .rfsm format version this build writes (and the newest it reads).
 *
 * Since: 0.9.0
 */
#define LRG_LOGIC_MACHINE_FORMAT_VERSION (1)

/* ==========================================================================
 * LrgLogicAction (boxed)
 * ========================================================================== */

/**
 * LrgLogicAction:
 *
 * One entry of an `enter:`, `update:`, `exit:` or transition `do:` list.
 * Immutable once made.
 *
 * Since: 0.9.0
 */
typedef struct _LrgLogicAction LrgLogicAction;

LRG_AVAILABLE_IN_ALL
GType lrg_logic_action_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LOGIC_ACTION (lrg_logic_action_get_type ())

/**
 * lrg_logic_action_new_call:
 * @name: a registered action name
 * @params: (nullable): an `a{sv}` dictionary passed to the action (a
 *   floating reference is sunk)
 *
 * Returns: (transfer full): a new action
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicAction *     lrg_logic_action_new_call    (const gchar *name,
                                                   GVariant    *params);

/**
 * lrg_logic_action_new_script:
 * @func: a script function name
 * @params: (nullable): an `a{sv}` dictionary whose values follow the
 *   blackboard and delta time as arguments
 *
 * Returns: (transfer full): a new action
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicAction *     lrg_logic_action_new_script  (const gchar *func,
                                                   GVariant    *params);

/**
 * lrg_logic_action_new_set:
 * @key: a blackboard key
 * @value: the value written (see lrg_blackboard_set_variant())
 *
 * Returns: (transfer full): a new action
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicAction *     lrg_logic_action_new_set     (const gchar *key,
                                                   GVariant    *value);

/**
 * lrg_logic_action_new_event:
 * @event: the event queued on the machine
 *
 * Returns: (transfer full): a new action
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicAction *     lrg_logic_action_new_event   (const gchar *event);

/**
 * lrg_logic_action_copy:
 * @self: an action
 *
 * Returns: (transfer full): a copy
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicAction *     lrg_logic_action_copy        (const LrgLogicAction *self);

/**
 * lrg_logic_action_free:
 * @self: (nullable): an action
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_action_free        (LrgLogicAction *self);

/**
 * lrg_logic_action_get_kind:
 * @self: an action
 *
 * Returns: what the action does
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicActionKind   lrg_logic_action_get_kind    (const LrgLogicAction *self);

/**
 * lrg_logic_action_get_name:
 * @self: an action
 *
 * Returns: (transfer none): the action name, script function, blackboard
 *   key or event, by kind
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *        lrg_logic_action_get_name    (const LrgLogicAction *self);

/**
 * lrg_logic_action_get_params:
 * @self: an action
 *
 * Returns: (transfer none) (nullable): the `a{sv}` parameters of a call
 *   or script action
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GVariant *           lrg_logic_action_get_params  (const LrgLogicAction *self);

/**
 * lrg_logic_action_get_value:
 * @self: an action
 *
 * Returns: (transfer none) (nullable): the value a set action writes
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GVariant *           lrg_logic_action_get_value   (const LrgLogicAction *self);

/* ==========================================================================
 * LrgLogicCondition (boxed)
 * ========================================================================== */

/**
 * LrgLogicCondition:
 *
 * One entry of a transition's `when:` list (all entries must hold).
 * Immutable once made.
 *
 * Since: 0.9.0
 */
typedef struct _LrgLogicCondition LrgLogicCondition;

LRG_AVAILABLE_IN_ALL
GType lrg_logic_condition_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LOGIC_CONDITION (lrg_logic_condition_get_type ())

/**
 * lrg_logic_condition_new_check:
 * @key: a blackboard key
 * @op: the comparison
 * @value: (nullable): the operand (floating is sunk)
 *
 * Returns: (transfer full): a new condition
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicCondition *  lrg_logic_condition_new_check  (const gchar     *key,
                                                     LrgBlackboardOp  op,
                                                     GVariant        *value);

/**
 * lrg_logic_condition_new_call:
 * @name: a registered condition name
 * @params: (nullable): its `a{sv}` parameters
 *
 * Returns: (transfer full): a new condition
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicCondition *  lrg_logic_condition_new_call   (const gchar *name,
                                                     GVariant    *params);

/**
 * lrg_logic_condition_new_script:
 * @func: a script function returning a truth value
 * @params: (nullable): its `a{sv}` parameters
 *
 * Returns: (transfer full): a new condition
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicCondition *  lrg_logic_condition_new_script (const gchar *func,
                                                     GVariant    *params);

/**
 * lrg_logic_condition_copy:
 * @self: a condition
 *
 * Returns: (transfer full): a copy
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicCondition *  lrg_logic_condition_copy       (const LrgLogicCondition *self);

/**
 * lrg_logic_condition_free:
 * @self: (nullable): a condition
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_condition_free       (LrgLogicCondition *self);

/**
 * lrg_logic_condition_get_kind:
 * @self: a condition
 *
 * Returns: what the condition tests
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicConditionKind lrg_logic_condition_get_kind  (const LrgLogicCondition *self);

/**
 * lrg_logic_condition_get_name:
 * @self: a condition
 *
 * Returns: (transfer none): the blackboard key, condition name or script
 *   function, by kind
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *        lrg_logic_condition_get_name   (const LrgLogicCondition *self);

/**
 * lrg_logic_condition_get_op:
 * @self: a condition
 *
 * Returns: the comparison of a check condition
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBlackboardOp      lrg_logic_condition_get_op     (const LrgLogicCondition *self);

/**
 * lrg_logic_condition_get_value:
 * @self: a condition
 *
 * Returns: (transfer none) (nullable): the operand of a check condition
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GVariant *           lrg_logic_condition_get_value  (const LrgLogicCondition *self);

/**
 * lrg_logic_condition_get_params:
 * @self: a condition
 *
 * Returns: (transfer none) (nullable): the `a{sv}` parameters of a call or
 *   script condition
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GVariant *           lrg_logic_condition_get_params (const LrgLogicCondition *self);

/* ==========================================================================
 * LrgLogicTransition
 * ========================================================================== */

#define LRG_TYPE_LOGIC_TRANSITION (lrg_logic_transition_get_type ())

/**
 * LrgLogicTransition:
 *
 * A way out of the state that owns it: to a target state, taken when its
 * event arrives (or, without an event, on any update), the owner has been
 * active for at least @after seconds and every condition holds. Its `do:`
 * actions run between the exits and the entries.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLogicTransition, lrg_logic_transition, LRG, LOGIC_TRANSITION, GObject)

/**
 * lrg_logic_transition_new:
 * @target: the target state: a name resolved from the owner outwards
 *   (siblings, then the parent's siblings, ..., then the owner's own
 *   children), a relative path `a/b`, or an absolute path `/a/b`
 *
 * Returns: (transfer full): a new transition
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicTransition * lrg_logic_transition_new             (const gchar *target);

/**
 * lrg_logic_transition_get_target:
 * @self: a transition
 *
 * Returns: (transfer none): the target as written
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *        lrg_logic_transition_get_target      (LrgLogicTransition *self);

/**
 * lrg_logic_transition_set_target:
 * @self: a transition
 * @target: the target state name or path
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_transition_set_target      (LrgLogicTransition *self,
                                                           const gchar        *target);

/**
 * lrg_logic_transition_get_event:
 * @self: a transition
 *
 * Returns: (transfer none) (nullable): the event that triggers it, %NULL
 *   for a transition checked on every update
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *        lrg_logic_transition_get_event       (LrgLogicTransition *self);

/**
 * lrg_logic_transition_set_event:
 * @self: a transition
 * @event: (nullable): the triggering event
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_transition_set_event       (LrgLogicTransition *self,
                                                           const gchar        *event);

/**
 * lrg_logic_transition_get_after:
 * @self: a transition
 *
 * Returns: the seconds the owner must have been active (0: no timer)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gdouble              lrg_logic_transition_get_after       (LrgLogicTransition *self);

/**
 * lrg_logic_transition_set_after:
 * @self: a transition
 * @seconds: the timer (negative is clamped to 0)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_transition_set_after       (LrgLogicTransition *self,
                                                           gdouble             seconds);

/**
 * lrg_logic_transition_add_condition:
 * @self: a transition
 * @condition: a condition (copied)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_transition_add_condition   (LrgLogicTransition      *self,
                                                           const LrgLogicCondition *condition);

/**
 * lrg_logic_transition_remove_condition:
 * @self: a transition
 * @index: a condition index
 *
 * Returns: %TRUE if @index was in range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean             lrg_logic_transition_remove_condition (LrgLogicTransition *self,
                                                            guint               index);

/**
 * lrg_logic_transition_get_condition_count:
 * @self: a transition
 *
 * Returns: the number of conditions
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint                lrg_logic_transition_get_condition_count (LrgLogicTransition *self);

/**
 * lrg_logic_transition_get_condition:
 * @self: a transition
 * @index: a condition index
 *
 * Returns: (transfer none) (nullable): the condition, %NULL out of range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const LrgLogicCondition * lrg_logic_transition_get_condition (LrgLogicTransition *self,
                                                              guint               index);

/**
 * lrg_logic_transition_add_action:
 * @self: a transition
 * @action: an action (copied) run when the transition is taken
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_transition_add_action      (LrgLogicTransition   *self,
                                                           const LrgLogicAction *action);

/**
 * lrg_logic_transition_remove_action:
 * @self: a transition
 * @index: an action index
 *
 * Returns: %TRUE if @index was in range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean             lrg_logic_transition_remove_action   (LrgLogicTransition *self,
                                                           guint               index);

/**
 * lrg_logic_transition_get_action_count:
 * @self: a transition
 *
 * Returns: the number of `do:` actions
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint                lrg_logic_transition_get_action_count (LrgLogicTransition *self);

/**
 * lrg_logic_transition_get_action:
 * @self: a transition
 * @index: an action index
 *
 * Returns: (transfer none) (nullable): the action, %NULL out of range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const LrgLogicAction * lrg_logic_transition_get_action    (LrgLogicTransition *self,
                                                           guint               index);

/**
 * lrg_logic_transition_get_fire_count:
 * @self: a transition
 *
 * Returns: how many times a running machine took this transition (live
 *   views highlight hot edges with it)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint64              lrg_logic_transition_get_fire_count  (LrgLogicTransition *self);

/* ==========================================================================
 * LrgLogicState
 * ========================================================================== */

#define LRG_TYPE_LOGIC_STATE (lrg_logic_state_get_type ())

/**
 * LrgLogicState:
 *
 * A state: action lists for entering, updating and exiting, outgoing
 * transitions, and optionally child states (a sub-machine with an initial
 * child) that are active while it is.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLogicState, lrg_logic_state, LRG, LOGIC_STATE, GObject)

/**
 * lrg_logic_state_new:
 * @name: the state's name (unique among its siblings, no `/`)
 *
 * Returns: (transfer full): a new state
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicState *      lrg_logic_state_new                (const gchar *name);

/**
 * lrg_logic_state_get_name:
 * @self: a state
 *
 * Returns: (transfer none): the name (empty for a machine's root)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *        lrg_logic_state_get_name           (LrgLogicState *self);

/**
 * lrg_logic_state_set_name:
 * @self: a state
 * @name: the new name
 *
 * Renames the state (transitions naming the old name are not updated).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_state_set_name           (LrgLogicState *self,
                                                         const gchar   *name);

/**
 * lrg_logic_state_dup_path:
 * @self: a state
 *
 * Returns: (transfer full): the path from the machine root, `a/b` (empty
 *   for the root)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar *              lrg_logic_state_dup_path           (LrgLogicState *self);

/**
 * lrg_logic_state_get_parent:
 * @self: a state
 *
 * Returns: (transfer none) (nullable): the parent state
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicState *      lrg_logic_state_get_parent         (LrgLogicState *self);

/**
 * lrg_logic_state_add_state:
 * @self: a state
 * @child: (transfer none): the child state (must not have a parent)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_state_add_state          (LrgLogicState *self,
                                                         LrgLogicState *child);

/**
 * lrg_logic_state_remove_state:
 * @self: a state
 * @child: a child state
 *
 * Returns: %TRUE if @child was a child of @self
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean             lrg_logic_state_remove_state       (LrgLogicState *self,
                                                         LrgLogicState *child);

/**
 * lrg_logic_state_get_state_count:
 * @self: a state
 *
 * Returns: the number of child states
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint                lrg_logic_state_get_state_count    (LrgLogicState *self);

/**
 * lrg_logic_state_get_state:
 * @self: a state
 * @index: a child index
 *
 * Returns: (transfer none) (nullable): the child, %NULL out of range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicState *      lrg_logic_state_get_state          (LrgLogicState *self,
                                                         guint          index);

/**
 * lrg_logic_state_find_state:
 * @self: a state
 * @name: a child's name
 *
 * Returns: (transfer none) (nullable): the direct child of that name
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicState *      lrg_logic_state_find_state         (LrgLogicState *self,
                                                         const gchar   *name);

/**
 * lrg_logic_state_get_initial:
 * @self: a state
 *
 * Returns: (transfer none) (nullable): the child entered first, as
 *   written (%NULL: the first child)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *        lrg_logic_state_get_initial        (LrgLogicState *self);

/**
 * lrg_logic_state_set_initial:
 * @self: a state
 * @name: (nullable): a child's name, or %NULL for the first child
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_state_set_initial        (LrgLogicState *self,
                                                         const gchar   *name);

/**
 * lrg_logic_state_add_action:
 * @self: a state
 * @phase: which list
 * @action: the action (copied), appended
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_state_add_action         (LrgLogicState        *self,
                                                         LrgLogicPhase         phase,
                                                         const LrgLogicAction *action);

/**
 * lrg_logic_state_remove_action:
 * @self: a state
 * @phase: which list
 * @index: an action index
 *
 * Returns: %TRUE if @index was in range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean             lrg_logic_state_remove_action      (LrgLogicState *self,
                                                         LrgLogicPhase  phase,
                                                         guint          index);

/**
 * lrg_logic_state_get_action_count:
 * @self: a state
 * @phase: which list
 *
 * Returns: the number of actions in the list
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint                lrg_logic_state_get_action_count   (LrgLogicState *self,
                                                         LrgLogicPhase  phase);

/**
 * lrg_logic_state_get_action:
 * @self: a state
 * @phase: which list
 * @index: an action index
 *
 * Returns: (transfer none) (nullable): the action, %NULL out of range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const LrgLogicAction * lrg_logic_state_get_action       (LrgLogicState *self,
                                                         LrgLogicPhase  phase,
                                                         guint          index);

/**
 * lrg_logic_state_add_transition:
 * @self: a state
 * @transition: (transfer none): the transition, appended (earlier
 *   transitions win)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_state_add_transition     (LrgLogicState      *self,
                                                         LrgLogicTransition *transition);

/**
 * lrg_logic_state_remove_transition:
 * @self: a state
 * @transition: one of its transitions
 *
 * Returns: %TRUE if @transition belonged to @self
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean             lrg_logic_state_remove_transition  (LrgLogicState      *self,
                                                         LrgLogicTransition *transition);

/**
 * lrg_logic_state_get_transition_count:
 * @self: a state
 *
 * Returns: the number of outgoing transitions
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint                lrg_logic_state_get_transition_count (LrgLogicState *self);

/**
 * lrg_logic_state_get_transition:
 * @self: a state
 * @index: a transition index
 *
 * Returns: (transfer none) (nullable): the transition, %NULL out of range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicTransition * lrg_logic_state_get_transition     (LrgLogicState *self,
                                                         guint          index);

/* ==========================================================================
 * LrgLogicMachine
 * ========================================================================== */

#define LRG_TYPE_LOGIC_MACHINE (lrg_logic_machine_get_type ())

/**
 * LrgLogicMachine:
 *
 * A `.rfsm` state machine: its definition (a root #LrgLogicState whose
 * children are the top-level states and whose transitions apply in any
 * state, plus a blackboard schema) and its runtime (the active chain of
 * states, their timers, the event queue). One machine runs one instance;
 * load the file again (or lrg_logic_machine_duplicate()) for another
 * entity.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLogicMachine, lrg_logic_machine, LRG, LOGIC_MACHINE, GObject)

/**
 * lrg_logic_machine_new:
 *
 * Returns: (transfer full): a new machine with no states
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicMachine *    lrg_logic_machine_new              (void);

/**
 * lrg_logic_machine_new_from_file:
 * @path: a `.rfsm` file
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer full) (nullable): the machine, %NULL on error
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicMachine *    lrg_logic_machine_new_from_file    (const gchar  *path,
                                                         GError      **error);

/**
 * lrg_logic_machine_new_from_data:
 * @data: YAML text
 * @length: its length, or -1 when nul-terminated
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer full) (nullable): the machine, %NULL on error
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicMachine *    lrg_logic_machine_new_from_data    (const gchar  *data,
                                                         gssize        length,
                                                         GError      **error);

/**
 * lrg_logic_machine_load_from_file:
 * @self: a machine (not running)
 * @path: a `.rfsm` file
 * @error: (nullable): return location for an error
 *
 * Replaces the definition (unchanged on error).
 *
 * Returns: %TRUE on success
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean             lrg_logic_machine_load_from_file   (LrgLogicMachine  *self,
                                                         const gchar      *path,
                                                         GError          **error);

/**
 * lrg_logic_machine_load_from_data:
 * @self: a machine (not running)
 * @data: YAML text
 * @length: its length, or -1 when nul-terminated
 * @error: (nullable): return location for an error
 *
 * Replaces the definition (unchanged on error).
 *
 * Returns: %TRUE on success
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean             lrg_logic_machine_load_from_data   (LrgLogicMachine  *self,
                                                         const gchar      *data,
                                                         gssize            length,
                                                         GError          **error);

/**
 * lrg_logic_machine_to_data:
 * @self: a machine
 * @error: (nullable): return location for an error
 *
 * Serialises the definition (not the runtime state) to `.rfsm` YAML.
 *
 * Returns: (transfer full) (nullable): the text
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar *              lrg_logic_machine_to_data          (LrgLogicMachine  *self,
                                                         GError          **error);

/**
 * lrg_logic_machine_save_to_file:
 * @self: a machine
 * @path: the file to write
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean             lrg_logic_machine_save_to_file     (LrgLogicMachine  *self,
                                                         const gchar      *path,
                                                         GError          **error);

/**
 * lrg_logic_machine_duplicate:
 * @self: a machine
 * @error: (nullable): return location for an error
 *
 * Copies the definition into a new, stopped machine (for another entity).
 *
 * Returns: (transfer full) (nullable): the copy
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicMachine *    lrg_logic_machine_duplicate        (LrgLogicMachine  *self,
                                                         GError          **error);

/**
 * lrg_logic_machine_get_name:
 * @self: a machine
 *
 * Returns: (transfer none) (nullable): the machine's name
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *        lrg_logic_machine_get_name         (LrgLogicMachine *self);

/**
 * lrg_logic_machine_set_name:
 * @self: a machine
 * @name: (nullable): the machine's name
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_machine_set_name         (LrgLogicMachine *self,
                                                         const gchar     *name);

/**
 * lrg_logic_machine_get_schema:
 * @self: a machine
 *
 * Returns: (transfer none): the blackboard schema (edit in place)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBlackboardSchema * lrg_logic_machine_get_schema      (LrgLogicMachine *self);

/**
 * lrg_logic_machine_get_root:
 * @self: a machine
 *
 * The root state: its children are the top-level states, its initial is
 * the machine's `initial:`, its transitions are the any-state
 * `transitions:` and its actions the machine-wide `enter:`, `update:` and
 * `exit:` lists. The root is active for as long as the machine runs.
 *
 * Returns: (transfer none): the root state
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicState *      lrg_logic_machine_get_root         (LrgLogicMachine *self);

/**
 * lrg_logic_machine_find_state:
 * @self: a machine
 * @path: a state path (`a/b`, a leading `/` is allowed)
 *
 * Returns: (transfer none) (nullable): the state
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicState *      lrg_logic_machine_find_state       (LrgLogicMachine *self,
                                                         const gchar     *path);

/**
 * lrg_logic_machine_get_allow_unbound:
 * @self: a machine
 *
 * Returns: whether lrg_logic_machine_start() accepts missing bindings
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean             lrg_logic_machine_get_allow_unbound (LrgLogicMachine *self);

/**
 * lrg_logic_machine_set_allow_unbound:
 * @self: a machine
 * @allow: %TRUE to start with missing actions, conditions or script
 *   functions (they are skipped, conditions are false, each logged once)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_machine_set_allow_unbound (LrgLogicMachine *self,
                                                          gboolean         allow);

/**
 * lrg_logic_machine_get_max_transitions:
 * @self: a machine
 *
 * Returns: the cap on event-less transitions taken in one update
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint                lrg_logic_machine_get_max_transitions (LrgLogicMachine *self);

/**
 * lrg_logic_machine_set_max_transitions:
 * @self: a machine
 * @max: how many event-less transitions one update may chain (default
 *   8, at least 1): a guard against transitions that keep firing
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_machine_set_max_transitions (LrgLogicMachine *self,
                                                            guint            max);

/**
 * lrg_logic_machine_validate:
 * @self: a machine
 * @error: (nullable): return location for the first problem
 *
 * Checks the definition: there are states, initial names and transition
 * targets resolve, sibling names are unique, entries are well formed,
 * and `set`/check keys are declared in a non-empty schema. Bindings are
 * not checked (see lrg_logic_machine_dup_unbound()).
 *
 * Returns: %TRUE if the machine can start
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean             lrg_logic_machine_validate         (LrgLogicMachine  *self,
                                                         GError          **error);

/**
 * lrg_logic_machine_dup_unbound:
 * @self: a machine
 * @scripting: (nullable): the context script entries would use
 *
 * Returns: (transfer full) (array zero-terminated=1): `action:NAME`,
 *   `condition:NAME` and `script:NAME` entries for what is missing
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar **             lrg_logic_machine_dup_unbound      (LrgLogicMachine *self,
                                                         LrgScripting    *scripting);

/**
 * lrg_logic_machine_start:
 * @self: a machine
 * @blackboard: (nullable): the blackboard to use (shared), or %NULL for a
 *   new one
 * @scripting: (nullable): the context script entries call into
 * @error: (nullable): return location for an error
 *
 * Validates, checks bindings (unless unbound entries are allowed), writes
 * the schema defaults for missing keys, then enters the root and the
 * initial chain, running their `enter:` actions. A running machine is
 * stopped first.
 *
 * Returns: %TRUE if the machine is running
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean             lrg_logic_machine_start            (LrgLogicMachine  *self,
                                                         LrgBlackboard    *blackboard,
                                                         LrgScripting     *scripting,
                                                         GError          **error);

/**
 * lrg_logic_machine_stop:
 * @self: a machine
 *
 * Exits every active state (innermost first, root last) and stops.
 * Queued events are dropped.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_machine_stop             (LrgLogicMachine *self);

/**
 * lrg_logic_machine_is_running:
 * @self: a machine
 *
 * Returns: %TRUE between start and stop
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean             lrg_logic_machine_is_running       (LrgLogicMachine *self);

/**
 * lrg_logic_machine_update:
 * @self: a machine
 * @delta_time: seconds since the last update
 *
 * One step, deterministic for the same inputs: (1) every active state's
 * timer advances by @delta_time; (2) the events queued before this call
 * are handled in order, each taking at most one transition (the first
 * matching one, outermost state first, in list order); (3) event-less
 * transitions are taken while one matches, up to the max-transitions cap;
 * (4) the active states' `update:` actions run, outermost first. Events
 * queued during the step wait for the next one.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_machine_update           (LrgLogicMachine *self,
                                                         gfloat           delta_time);

/**
 * lrg_logic_machine_send_event:
 * @self: a machine
 * @event: the event name
 *
 * Queues an event for the next update.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                 lrg_logic_machine_send_event       (LrgLogicMachine *self,
                                                         const gchar     *event);

/**
 * lrg_logic_machine_get_blackboard:
 * @self: a machine
 *
 * Returns: (transfer none) (nullable): the blackboard, %NULL before the
 *   first start
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBlackboard *      lrg_logic_machine_get_blackboard   (LrgLogicMachine *self);

/**
 * lrg_logic_machine_get_active_state:
 * @self: a machine
 *
 * Returns: (transfer none) (nullable): the innermost active state's path,
 *   %NULL when stopped
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *        lrg_logic_machine_get_active_state (LrgLogicMachine *self);

/**
 * lrg_logic_machine_dup_active_path:
 * @self: a machine
 *
 * Returns: (transfer full) (array zero-terminated=1): the paths of the
 *   active states, outermost first (the root is not listed)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar **             lrg_logic_machine_dup_active_path  (LrgLogicMachine *self);

/**
 * lrg_logic_machine_is_in_state:
 * @self: a machine
 * @path: a state path
 *
 * Returns: %TRUE if that state is active (itself or as an ancestor of the
 *   innermost one)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean             lrg_logic_machine_is_in_state      (LrgLogicMachine *self,
                                                         const gchar     *path);

/**
 * lrg_logic_machine_get_state_time:
 * @self: a machine
 * @path: (nullable): an active state's path, or %NULL for the innermost
 *
 * Returns: the seconds the state has been active, -1 when it is not
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gdouble              lrg_logic_machine_get_state_time   (LrgLogicMachine *self,
                                                         const gchar     *path);

/**
 * lrg_logic_machine_get_update_count:
 * @self: a machine
 *
 * Returns: the updates since the last start
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint64              lrg_logic_machine_get_update_count (LrgLogicMachine *self);

/**
 * lrg_logic_machine_get_last_transition:
 * @self: a machine
 * @out_from: (out) (optional) (transfer none) (nullable): the innermost
 *   state path before it
 * @out_to: (out) (optional) (transfer none) (nullable): the innermost
 *   state path after it
 * @out_event: (out) (optional) (transfer none) (nullable): the event that
 *   triggered it, %NULL for an event-less one
 *
 * Returns: (transfer none) (nullable): the transition taken last, %NULL
 *   when none was since the start
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicTransition * lrg_logic_machine_get_last_transition (LrgLogicMachine  *self,
                                                            const gchar     **out_from,
                                                            const gchar     **out_to,
                                                            const gchar     **out_event);

/**
 * lrg_logic_machine_dup_debug_state:
 * @self: a machine
 *
 * The runtime as YAML, for IPC and logs:
 *
 * |[
 * running: true
 * updates: 120
 * active:
 * - {state: "combat", time: 2.5}
 * - {state: "combat/approach", time: 0.75}
 * last_transition: {from: "idle", to: "combat/approach", event: "alert"}
 * queued: []
 * ]|
 *
 * Returns: (transfer full): the text
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar *              lrg_logic_machine_dup_debug_state  (LrgLogicMachine *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgLogicAction, lrg_logic_action_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgLogicCondition, lrg_logic_condition_free)

G_END_DECLS
