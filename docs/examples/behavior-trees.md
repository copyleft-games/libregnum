# Behavior Tree Examples

Complete working examples using the AI module's behavior tree system.

## Example 1: Simple Enemy AI

A basic enemy that patrols or attacks based on player visibility.

```c
#include <glib.h>
#include <libregnum.h>

/* Conditions */
static gboolean
is_player_visible(LrgBlackboard *bb, gpointer ud)
{
    (void)ud;
    return lrg_blackboard_get_bool(bb, "player_visible", FALSE);
}

/* Actions */
static LrgBTStatus
patrol_action(LrgBlackboard *bb, gfloat dt, gpointer ud)
{
    (void)dt;(void)ud;

    gint pos = lrg_blackboard_get_int(bb, "patrol_pos", 0);
    pos = (pos + 1) % 100;
    lrg_blackboard_set_int(bb, "patrol_pos", pos);

    g_print("Patrolling at position %d\n", pos);
    return LRG_BT_STATUS_SUCCESS;
}

static LrgBTStatus
attack_action(LrgBlackboard *bb, gfloat dt, gpointer ud)
{
    (void)dt;(void)ud;

    gint attacks = lrg_blackboard_get_int(bb, "attack_count", 0);
    attacks++;
    lrg_blackboard_set_int(bb, "attack_count", attacks);

    g_print("Attacking! Total attacks: %d\n", attacks);
    return LRG_BT_STATUS_SUCCESS;
}

int main(void) {
    /* Create tree */
    g_autoptr(LrgBehaviorTree) tree = lrg_behavior_tree_new();
    LrgBlackboard *bb = lrg_behavior_tree_get_blackboard(tree);

    /* Initialize state */
    lrg_blackboard_set_int(bb, "patrol_pos", 0);
    lrg_blackboard_set_int(bb, "attack_count", 0);

    /* Build tree:
     * Selector (if/else)
     *   ├── Sequence (if player visible, attack)
     *   │     ├── Condition: is_player_visible
     *   │     └── Action: attack
     *   └── Action: patrol
     */
    g_autoptr(LrgBTSelector) root = lrg_bt_selector_new();
    g_autoptr(LrgBTSequence) attack_seq = lrg_bt_sequence_new();
    g_autoptr(LrgBTCondition) see_player = lrg_bt_condition_new_simple(is_player_visible);
    g_autoptr(LrgBTAction) attack = lrg_bt_action_new_simple(attack_action);
    g_autoptr(LrgBTAction) patrol = lrg_bt_action_new_simple(patrol_action);

    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(attack_seq), LRG_BT_NODE(see_player));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(attack_seq), LRG_BT_NODE(attack));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(attack_seq));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(patrol));

    lrg_behavior_tree_set_root(tree, LRG_BT_NODE(root));

    /* Simulate game loop */
    g_print("=== Phase 1: Patrol (no player) ===\n");
    for (gint i = 0; i < 3; i++) {
        lrg_behavior_tree_tick(tree, 0.016f);
        lrg_behavior_tree_reset(tree);
    }

    g_print("\n=== Phase 2: Player appears ===\n");
    lrg_blackboard_set_bool(bb, "player_visible", TRUE);
    for (gint i = 0; i < 3; i++) {
        lrg_behavior_tree_tick(tree, 0.016f);
        lrg_behavior_tree_reset(tree);
    }

    return 0;
}
```

## Example 2: State Machine with Behavior Trees

Multiple behavior trees for different states.

```c
#include <glib.h>
#include <libregnum.h>

typedef enum {
    STATE_IDLE,
    STATE_ALERT,
    STATE_COMBAT
} AIState;

typedef struct {
    AIState current_state;
    AIState next_state;
    LrgBehaviorTree *tree;
} AIEntity;

/* Conditions */
static gboolean
threat_detected(LrgBlackboard *bb, gpointer ud)
{
    (void)ud;
    return lrg_blackboard_get_bool(bb, "threat_detected", FALSE);
}

static gboolean
threat_lost(LrgBlackboard *bb, gpointer ud)
{
    (void)ud;
    return !lrg_blackboard_get_bool(bb, "threat_detected", FALSE);
}

/* Actions */
static LrgBTStatus
idle_action(LrgBlackboard *bb, gfloat dt, gpointer ud)
{
    (void)dt;(void)ud;
    g_print("  [Idle] Doing nothing\n");
    return LRG_BT_STATUS_SUCCESS;
}

static LrgBTStatus
alert_action(LrgBlackboard *bb, gfloat dt, gpointer ud)
{
    (void)dt;(void)ud;
    g_print("  [Alert] Searching for threat\n");
    return LRG_BT_STATUS_RUNNING;
}

static LrgBTStatus
combat_action(LrgBlackboard *bb, gfloat dt, gpointer ud)
{
    (void)dt;(void)ud;
    g_print("  [Combat] Engaging enemy\n");
    return LRG_BT_STATUS_RUNNING;
}

int main(void) {
    g_autoptr(LrgBlackboard) bb = lrg_blackboard_new();
    AIEntity entity = { .current_state = STATE_IDLE };

    /* Create idle tree */
    LrgBehaviorTree *idle_tree = lrg_behavior_tree_new();
    LrgBTAction *idle = lrg_bt_action_new_simple(idle_action);
    lrg_behavior_tree_set_root(idle_tree, LRG_BT_NODE(idle));

    /* Create alert tree */
    LrgBehaviorTree *alert_tree = lrg_behavior_tree_new();
    LrgBTAction *alert = lrg_bt_action_new_simple(alert_action);
    lrg_behavior_tree_set_root(alert_tree, LRG_BT_NODE(alert));

    /* Create combat tree */
    LrgBehaviorTree *combat_tree = lrg_behavior_tree_new();
    LrgBTAction *combat = lrg_bt_action_new_simple(combat_action);
    lrg_behavior_tree_set_root(combat_tree, LRG_BT_NODE(combat));

    entity.tree = idle_tree;

    /* Simulate transitions */
    g_print("=== Frame 1-2: Idle ===\n");
    lrg_behavior_tree_tick(entity.tree, 0.016f);
    lrg_behavior_tree_tick(entity.tree, 0.016f);

    g_print("\n=== Frame 3: Threat detected ===\n");
    lrg_blackboard_set_bool(bb, "threat_detected", TRUE);
    entity.tree = alert_tree;
    lrg_behavior_tree_tick(entity.tree, 0.016f);

    g_print("\n=== Frame 4: Entering combat ===\n");
    entity.tree = combat_tree;
    lrg_behavior_tree_tick(entity.tree, 0.016f);

    g_print("\n=== Frame 5: Threat lost ===\n");
    lrg_blackboard_set_bool(bb, "threat_detected", FALSE);
    entity.tree = idle_tree;
    lrg_behavior_tree_reset(entity.tree);
    lrg_behavior_tree_tick(entity.tree, 0.016f);

    g_object_unref(idle);
    g_object_unref(alert);
    g_object_unref(combat);
    g_object_unref(idle_tree);
    g_object_unref(alert_tree);
    g_object_unref(combat_tree);

    return 0;
}
```

## Example 3: Using Decorators

Practical decorator usage patterns.

```c
#include <glib.h>
#include <libregnum.h>

static LrgBTStatus
healing_action(LrgBlackboard *bb, gfloat dt, gpointer ud)
{
    (void)dt;(void)ud;

    gint health = lrg_blackboard_get_int(bb, "health", 50);
    health = (health + 20 > 100) ? 100 : health + 20;
    lrg_blackboard_set_int(bb, "health", health);

    g_print("Healing... health now: %d\n", health);

    return (health >= 100) ? LRG_BT_STATUS_SUCCESS : LRG_BT_STATUS_RUNNING;
}

static gboolean
is_healthy(LrgBlackboard *bb, gpointer ud)
{
    (void)ud;
    gint health = lrg_blackboard_get_int(bb, "health", 100);
    return health > 75;
}

int main(void) {
    g_autoptr(LrgBlackboard) bb = lrg_blackboard_new();

    lrg_blackboard_set_int(bb, "health", 30);

    /* Build tree:
     * Sequence
     *   ├── Inverter(is_healthy) - succeeds if NOT healthy
     *   ├── Repeater(heal, 3) - heal up to 3 times
     *   └── Succeeder(some_action) - ignore failure
     */
    g_autoptr(LrgBTSequence) root = lrg_bt_sequence_new();

    g_autoptr(LrgBTCondition) healthy = lrg_bt_condition_new_simple(is_healthy);
    g_autoptr(LrgBTInverter) not_healthy = lrg_bt_inverter_new(LRG_BT_NODE(healthy));

    g_autoptr(LrgBTAction) heal = lrg_bt_action_new_simple(healing_action);
    g_autoptr(LrgBTRepeater) heal_3x = lrg_bt_repeater_new(LRG_BT_NODE(heal), 3);

    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(not_healthy));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(heal_3x));

    /* Execute */
    g_print("Initial health: %d\n\n", lrg_blackboard_get_int(bb, "health", 0));

    for (gint i = 0; i < 4; i++) {
        g_print("=== Tick %d ===\n", i + 1);
        LrgBTStatus status = lrg_behavior_tree_tick(
            lrg_behavior_tree_new_with_root(LRG_BT_NODE(root)), 0.016f);
        g_print("Status: %d\n", status);
        lrg_bt_node_reset(LRG_BT_NODE(root));
    }

    return 0;
}
```

## Example 4: Complex Behavior Tree

A more complex tree with multiple strategies.

```c
#include <glib.h>
#include <libregnum.h>

/* Helper to show tree execution */
static void log_action(const gchar *name) {
    g_print("  > %s\n", name);
}

/* Conditions */
static gboolean
is_low_health(LrgBlackboard *bb, gpointer ud)
{
    (void)ud;
    gint health = lrg_blackboard_get_int(bb, "health", 100);
    return health < 30;
}

static gboolean
is_enemy_near(LrgBlackboard *bb, gpointer ud)
{
    (void)ud;
    gint distance = lrg_blackboard_get_int(bb, "enemy_distance", 1000);
    return distance < 50;
}

static gboolean
is_armed(LrgBlackboard *bb, gpointer ud)
{
    (void)ud;
    return lrg_blackboard_get_bool(bb, "has_weapon", FALSE);
}

/* Actions */
static LrgBTStatus flee_action(LrgBlackboard *bb, gfloat dt, gpointer ud) {
    (void)bb;(void)dt;(void)ud;
    log_action("FLEE from enemy");
    return LRG_BT_STATUS_SUCCESS;
}

static LrgBTStatus attack_action(LrgBlackboard *bb, gfloat dt, gpointer ud) {
    (void)bb;(void)dt;(void)ud;
    log_action("ATTACK enemy");
    return LRG_BT_STATUS_SUCCESS;
}

static LrgBTStatus heal_action(LrgBlackboard *bb, gfloat dt, gpointer ud) {
    (void)dt;(void)ud;
    gint health = lrg_blackboard_get_int(bb, "health", 100);
    health += 10;
    lrg_blackboard_set_int(bb, "health", health);
    log_action("HEAL (now at 50% health)");
    return LRG_BT_STATUS_SUCCESS;
}

static LrgBTStatus pickup_action(LrgBlackboard *bb, gfloat dt, gpointer ud) {
    (void)dt;(void)ud;
    lrg_blackboard_set_bool(bb, "has_weapon", TRUE);
    log_action("PICK UP weapon");
    return LRG_BT_STATUS_SUCCESS;
}

static LrgBTStatus idle_action(LrgBlackboard *bb, gfloat dt, gpointer ud) {
    (void)bb;(void)dt;(void)ud;
    log_action("IDLE (nothing to do)");
    return LRG_BT_STATUS_SUCCESS;
}

int main(void) {
    g_autoptr(LrgBlackboard) bb = lrg_blackboard_new();

    /* Build complex tree:
     * Selector (priority-based fallback)
     *   ├── Sequence: "Survive" (if low health, heal)
     *   │     ├── Condition: is_low_health
     *   │     └── Action: heal
     *   ├── Sequence: "Fight armed" (if enemy near and armed, attack)
     *   │     ├── Condition: is_enemy_near
     *   │     ├── Condition: is_armed
     *   │     └── Action: attack
     *   ├── Sequence: "Arm up" (if enemy near and not armed, pick up weapon)
     *   │     ├── Condition: is_enemy_near
     *   │     ├── Inverter(is_armed)
     *   │     └── Action: pickup
     *   ├── Sequence: "Flee" (if low health and enemy near, flee)
     *   │     ├── Condition: is_low_health
     *   │     ├── Condition: is_enemy_near
     *   │     └── Action: flee
     *   └── Action: idle
     */
    g_autoptr(LrgBTSelector) root = lrg_bt_selector_new();

    /* Survive: heal if low health */
    g_autoptr(LrgBTSequence) survive = lrg_bt_sequence_new();
    g_autoptr(LrgBTCondition) low_health = lrg_bt_condition_new_simple(is_low_health);
    g_autoptr(LrgBTAction) heal = lrg_bt_action_new_simple(heal_action);
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(survive), LRG_BT_NODE(low_health));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(survive), LRG_BT_NODE(heal));

    /* Fight: attack if enemy near and armed */
    g_autoptr(LrgBTSequence) fight = lrg_bt_sequence_new();
    g_autoptr(LrgBTCondition) enemy_near = lrg_bt_condition_new_simple(is_enemy_near);
    g_autoptr(LrgBTCondition) armed = lrg_bt_condition_new_simple(is_armed);
    g_autoptr(LrgBTAction) attack = lrg_bt_action_new_simple(attack_action);
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(fight), LRG_BT_NODE(enemy_near));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(fight), LRG_BT_NODE(armed));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(fight), LRG_BT_NODE(attack));

    /* Arm: pick up if enemy near and not armed */
    g_autoptr(LrgBTSequence) arm = lrg_bt_sequence_new();
    g_autoptr(LrgBTInverter) not_armed = lrg_bt_inverter_new(LRG_BT_NODE(
        lrg_bt_condition_new_simple(is_armed)));
    g_autoptr(LrgBTAction) pickup = lrg_bt_action_new_simple(pickup_action);
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(arm), LRG_BT_NODE(
        lrg_bt_condition_new_simple(is_enemy_near)));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(arm), LRG_BT_NODE(not_armed));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(arm), LRG_BT_NODE(pickup));

    /* Flee: if low health and enemy near */
    g_autoptr(LrgBTSequence) run = lrg_bt_sequence_new();
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(run), LRG_BT_NODE(
        lrg_bt_condition_new_simple(is_low_health)));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(run), LRG_BT_NODE(
        lrg_bt_condition_new_simple(is_enemy_near)));
    g_autoptr(LrgBTAction) flee = lrg_bt_action_new_simple(flee_action);
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(run), LRG_BT_NODE(flee));

    /* Assemble root */
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(survive));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(fight));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(arm));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(run));
    g_autoptr(LrgBTAction) idle = lrg_bt_action_new_simple(idle_action);
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(root), LRG_BT_NODE(idle));

    /* Create tree and test scenarios */
    g_autoptr(LrgBehaviorTree) tree = lrg_behavior_tree_new_with_root(
        LRG_BT_NODE(root));

    g_print("=== Scenario 1: Idle (all clear) ===\n");
    lrg_blackboard_set_int(bb, "health", 100);
    lrg_blackboard_set_bool(bb, "has_weapon", FALSE);
    lrg_blackboard_set_int(bb, "enemy_distance", 1000);
    lrg_behavior_tree_tick(tree, 0.016f);

    g_print("\n=== Scenario 2: Low health, need healing ===\n");
    lrg_blackboard_set_int(bb, "health", 20);
    lrg_behavior_tree_reset(tree);
    lrg_behavior_tree_tick(tree, 0.016f);

    g_print("\n=== Scenario 3: Enemy near, pick up weapon ===\n");
    lrg_blackboard_set_int(bb, "health", 100);
    lrg_blackboard_set_int(bb, "enemy_distance", 30);
    lrg_behavior_tree_reset(tree);
    lrg_behavior_tree_tick(tree, 0.016f);

    g_print("\n=== Scenario 4: Enemy near, armed, attack ===\n");
    lrg_blackboard_set_bool(bb, "has_weapon", TRUE);
    lrg_behavior_tree_reset(tree);
    lrg_behavior_tree_tick(tree, 0.016f);

    g_print("\n=== Scenario 5: Low health with enemy, flee! ===\n");
    lrg_blackboard_set_int(bb, "health", 20);
    lrg_behavior_tree_reset(tree);
    lrg_behavior_tree_tick(tree, 0.016f);

    return 0;
}
```

## Example 5: Parallel Execution

Running multiple actions simultaneously.

```c
#include <glib.h>
#include <libregnum.h>

static LrgBTStatus
move_action(LrgBlackboard *bb, gfloat dt, gpointer ud)
{
    (void)dt;(void)ud;
    gint pos = lrg_blackboard_get_int(bb, "position", 0);
    pos += 5;
    lrg_blackboard_set_int(bb, "position", pos);
    g_print("  Moving... (pos=%d)\n", pos);
    return (pos >= 50) ? LRG_BT_STATUS_SUCCESS : LRG_BT_STATUS_RUNNING;
}

static LrgBTStatus
animate_action(LrgBlackboard *bb, gfloat dt, gpointer ud)
{
    (void)bb;(void)dt;(void)ud;
    static gint frame = 0;
    g_print("  Animating... (frame=%d)\n", frame++);
    return (frame >= 5) ? LRG_BT_STATUS_SUCCESS : LRG_BT_STATUS_RUNNING;
}

int main(void) {
    g_autoptr(LrgBlackboard) bb = lrg_blackboard_new();

    /* Parallel node that runs move and animate simultaneously */
    g_autoptr(LrgBTParallel) parallel =
        lrg_bt_parallel_new(LRG_BT_PARALLEL_REQUIRE_ALL);
    g_autoptr(LrgBTAction) move = lrg_bt_action_new_simple(move_action);
    g_autoptr(LrgBTAction) animate = lrg_bt_action_new_simple(animate_action);

    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(parallel), LRG_BT_NODE(move));
    lrg_bt_composite_add_child(LRG_BT_COMPOSITE(parallel), LRG_BT_NODE(animate));

    g_print("Running parallel move and animate:\n");
    for (gint i = 0; i < 6; i++) {
        g_print("Tick %d:\n", i + 1);
        LrgBTStatus status = lrg_bt_node_tick(
            LRG_BT_NODE(parallel), bb, 0.016f);
        g_print("  Status: %d\n", status);
        if (status != LRG_BT_STATUS_RUNNING) {
            break;
        }
        lrg_bt_node_reset(LRG_BT_NODE(parallel));
    }

    return 0;
}
```

## Compilation

Compile with:

```bash
gcc -Wall -o behavior_tree_example example.c `pkg-config --cflags --libs libregnum glib-2.0`
```

Or using your project's Makefile system.
