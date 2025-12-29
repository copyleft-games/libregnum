# Animation State Machine

`LrgAnimationStateMachine` provides a parameter-driven animation controller with states, transitions, and condition-based logic.

## Type

```c
#define LRG_TYPE_ANIMATION_STATE_MACHINE (lrg_animation_state_machine_get_type ())
G_DECLARE_DERIVABLE_TYPE (LrgAnimationStateMachine, lrg_animation_state_machine,
                          LRG, ANIMATION_STATE_MACHINE, GObject)
```

## Creation

```c
LrgAnimationStateMachine *lrg_animation_state_machine_new (void);
```

## Skeleton

```c
LrgSkeleton *lrg_animation_state_machine_get_skeleton (LrgAnimationStateMachine *self);
void lrg_animation_state_machine_set_skeleton (LrgAnimationStateMachine *self, LrgSkeleton *skeleton);
```

## State Management

```c
void lrg_animation_state_machine_add_state (LrgAnimationStateMachine *self, LrgAnimationState *state);
void lrg_animation_state_machine_remove_state (LrgAnimationStateMachine *self, const gchar *name);
LrgAnimationState *lrg_animation_state_machine_get_state (LrgAnimationStateMachine *self, const gchar *name);
GList *lrg_animation_state_machine_get_states (LrgAnimationStateMachine *self);
```

## Transitions

```c
void lrg_animation_state_machine_add_transition (LrgAnimationStateMachine *self, LrgAnimationTransition *transition);
GList *lrg_animation_state_machine_get_transitions (LrgAnimationStateMachine *self);
```

## Default State

```c
void lrg_animation_state_machine_set_default_state (LrgAnimationStateMachine *self, const gchar *name);
const gchar *lrg_animation_state_machine_get_default_state (LrgAnimationStateMachine *self);
```

## Current State

```c
LrgAnimationState *lrg_animation_state_machine_get_current_state (LrgAnimationStateMachine *self);
const gchar *lrg_animation_state_machine_get_current_state_name (LrgAnimationStateMachine *self);
```

## Parameters

Parameters drive transition conditions:

```c
/* Generic (GVariant) */
void lrg_animation_state_machine_set_parameter (LrgAnimationStateMachine *self, const gchar *name, GVariant *value);
GVariant *lrg_animation_state_machine_get_parameter (LrgAnimationStateMachine *self, const gchar *name);

/* Typed helpers */
void lrg_animation_state_machine_set_float (LrgAnimationStateMachine *self, const gchar *name, gfloat value);
gfloat lrg_animation_state_machine_get_float (LrgAnimationStateMachine *self, const gchar *name);

void lrg_animation_state_machine_set_bool (LrgAnimationStateMachine *self, const gchar *name, gboolean value);
gboolean lrg_animation_state_machine_get_bool (LrgAnimationStateMachine *self, const gchar *name);

/* Triggers (auto-reset after transition) */
void lrg_animation_state_machine_set_trigger (LrgAnimationStateMachine *self, const gchar *name);
void lrg_animation_state_machine_reset_trigger (LrgAnimationStateMachine *self, const gchar *name);
```

## Control

```c
void lrg_animation_state_machine_start (LrgAnimationStateMachine *self);
void lrg_animation_state_machine_stop (LrgAnimationStateMachine *self);
void lrg_animation_state_machine_update (LrgAnimationStateMachine *self, gfloat delta_time);

void lrg_animation_state_machine_force_state (LrgAnimationStateMachine *self, const gchar *name);

gboolean lrg_animation_state_machine_is_running (LrgAnimationStateMachine *self);
gboolean lrg_animation_state_machine_is_transitioning (LrgAnimationStateMachine *self);
```

## Signals

```c
void (*state_entered) (LrgAnimationStateMachine *self, const gchar *state_name);
void (*state_exited) (LrgAnimationStateMachine *self, const gchar *state_name);
```

## Example: Character Controller

```c
g_autoptr(LrgAnimationStateMachine) fsm = lrg_animation_state_machine_new ();
lrg_animation_state_machine_set_skeleton (fsm, skeleton);

/* Create states */
LrgAnimationState *idle = lrg_animation_state_new ("idle");
lrg_animation_state_set_clip (idle, idle_clip);

LrgAnimationState *walk = lrg_animation_state_new ("walk");
lrg_animation_state_set_clip (walk, walk_clip);

LrgAnimationState *run = lrg_animation_state_new ("run");
lrg_animation_state_set_clip (run, run_clip);

LrgAnimationState *jump = lrg_animation_state_new ("jump");
lrg_animation_state_set_clip (jump, jump_clip);
lrg_animation_state_set_loop (jump, FALSE);

/* Add states */
lrg_animation_state_machine_add_state (fsm, idle);
lrg_animation_state_machine_add_state (fsm, walk);
lrg_animation_state_machine_add_state (fsm, run);
lrg_animation_state_machine_add_state (fsm, jump);

/* Create transitions */

/* idle -> walk when speed > 0.1 */
LrgAnimationTransition *t1 = lrg_animation_transition_new ("idle", "walk");
lrg_animation_transition_add_condition (t1, "speed", LRG_CONDITION_GREATER, 0.1f);
lrg_animation_transition_set_duration (t1, 0.25f);

/* walk -> idle when speed < 0.1 */
LrgAnimationTransition *t2 = lrg_animation_transition_new ("walk", "idle");
lrg_animation_transition_add_condition (t2, "speed", LRG_CONDITION_LESS, 0.1f);
lrg_animation_transition_set_duration (t2, 0.25f);

/* walk -> run when speed > 0.6 */
LrgAnimationTransition *t3 = lrg_animation_transition_new ("walk", "run");
lrg_animation_transition_add_condition (t3, "speed", LRG_CONDITION_GREATER, 0.6f);
lrg_animation_transition_set_duration (t3, 0.2f);

/* any -> jump on trigger */
LrgAnimationTransition *t4 = lrg_animation_transition_new_any ("jump");
lrg_animation_transition_add_trigger (t4, "jump");
lrg_animation_transition_set_duration (t4, 0.1f);

/* jump -> idle at exit */
LrgAnimationTransition *t5 = lrg_animation_transition_new ("jump", "idle");
lrg_animation_transition_set_has_exit_time (t5, TRUE);
lrg_animation_transition_set_duration (t5, 0.2f);

/* Add transitions */
lrg_animation_state_machine_add_transition (fsm, t1);
lrg_animation_state_machine_add_transition (fsm, t2);
lrg_animation_state_machine_add_transition (fsm, t3);
lrg_animation_state_machine_add_transition (fsm, t4);
lrg_animation_state_machine_add_transition (fsm, t5);

/* Set default and start */
lrg_animation_state_machine_set_default_state (fsm, "idle");
lrg_animation_state_machine_start (fsm);
```

## Example: Game Loop

```c
void
update_character (gfloat delta_time)
{
    /* Update parameters from game state */
    lrg_animation_state_machine_set_float (fsm, "speed", character_speed);
    lrg_animation_state_machine_set_bool (fsm, "grounded", is_grounded);

    /* Check for jump input */
    if (jump_pressed && is_grounded)
    {
        lrg_animation_state_machine_set_trigger (fsm, "jump");
    }

    /* Update state machine */
    lrg_animation_state_machine_update (fsm, delta_time);

    /* Apply to skeleton */
    lrg_skeleton_calculate_world_poses (skeleton);
}
```

## Transition Conditions

| Condition | Description |
|-----------|-------------|
| `LRG_CONDITION_GREATER` | param > value |
| `LRG_CONDITION_LESS` | param < value |
| `LRG_CONDITION_EQUAL` | param == value |
| `LRG_CONDITION_NOT_EQUAL` | param != value |
| `LRG_CONDITION_TRIGGER` | trigger is set |
