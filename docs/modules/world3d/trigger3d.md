# LrgTrigger3D

## Overview

`LrgTrigger3D` defines interactive volumes that fire events when entities enter or interact with them. Triggers are used for quest objectives, area transitions, hazard zones, and other gameplay mechanics.

## Type Information

- **Type Name**: `LrgTrigger3D`
- **Type ID**: `LRG_TYPE_TRIGGER3D`
- **Type Category**: Boxed Type (immutable value type)

## Construction

### lrg_trigger3d_new

```c
LrgTrigger3D *lrg_trigger3d_new(const gchar *id, const LrgBoundingBox3D *bounds,
                                LrgTriggerType trigger_type);
```

Creates a new trigger.

**Parameters:**
- `trigger_type` - Type: VOLUME, INTERACTION, DAMAGE, WATER, etc.

**Example:**
```c
g_autoptr(LrgBoundingBox3D) bounds = lrg_bounding_box3d_new(10, 10, 10, 20, 20, 20);
g_autoptr(LrgTrigger3D) trigger = lrg_trigger3d_new("exit_zone", bounds, LRG_TRIGGER_TYPE_VOLUME);
```

### lrg_trigger3d_new_box

```c
LrgTrigger3D *lrg_trigger3d_new_box(const gchar *id, gfloat min_x, gfloat min_y, gfloat min_z,
                                    gfloat max_x, gfloat max_y, gfloat max_z,
                                    LrgTriggerType trigger_type);
```

Creates a trigger from box coordinates.

### lrg_trigger3d_copy

```c
LrgTrigger3D *lrg_trigger3d_copy(const LrgTrigger3D *self);
```

Creates a copy (nullable safe).

### lrg_trigger3d_free

```c
void lrg_trigger3d_free(LrgTrigger3D *self);
```

Frees a trigger (nullable safe).

**Use with `g_autoptr(LrgTrigger3D)` for automatic cleanup.**

## Properties

### lrg_trigger3d_get_id

```c
const gchar *lrg_trigger3d_get_id(const LrgTrigger3D *self);
```

Gets the trigger identifier.

### lrg_trigger3d_get_bounds

```c
LrgBoundingBox3D *lrg_trigger3d_get_bounds(const LrgTrigger3D *self);
```

Gets the trigger volume bounds.

### lrg_trigger3d_get_trigger_type

```c
LrgTriggerType lrg_trigger3d_get_trigger_type(const LrgTrigger3D *self);
```

Gets the trigger type.

**Possible Values:**
- `LRG_TRIGGER_TYPE_VOLUME` - Fired by entering volume
- `LRG_TRIGGER_TYPE_INTERACTION` - Requires player interaction
- `LRG_TRIGGER_TYPE_DAMAGE` - Damage zone
- `LRG_TRIGGER_TYPE_WATER` - Water/liquid volume
- `LRG_TRIGGER_TYPE_PORTAL` - Level transition
- `LRG_TRIGGER_TYPE_SCRIPT` - Custom script trigger

## Control

### lrg_trigger3d_is_enabled / lrg_trigger3d_set_enabled

```c
gboolean lrg_trigger3d_is_enabled(const LrgTrigger3D *self);
void lrg_trigger3d_set_enabled(LrgTrigger3D *self, gboolean enabled);
```

Gets/sets whether the trigger is active.

**Example:**
```c
g_autoptr(LrgTrigger3D) trap = lrg_trigger3d_new("spike_trap", bounds, LRG_TRIGGER_TYPE_DAMAGE);
lrg_trigger3d_set_enabled(trap, TRUE);

if (lrg_trigger3d_is_enabled(trap))
    g_print("Trap is active\n");
```

### lrg_trigger3d_is_one_shot / lrg_trigger3d_set_one_shot

```c
gboolean lrg_trigger3d_is_one_shot(const LrgTrigger3D *self);
void lrg_trigger3d_set_one_shot(LrgTrigger3D *self, gboolean one_shot);
```

Gets/sets one-shot mode (disables after first trigger).

**Example:**
```c
g_autoptr(LrgTrigger3D) chest = lrg_trigger3d_new("treasure_chest", bounds, LRG_TRIGGER_TYPE_INTERACTION);
lrg_trigger3d_set_one_shot(chest, TRUE);  /* Only open once */
```

## Target and Event

### lrg_trigger3d_get_target_id / lrg_trigger3d_set_target_id

```c
const gchar *lrg_trigger3d_get_target_id(const LrgTrigger3D *self);
void lrg_trigger3d_set_target_id(LrgTrigger3D *self, const gchar *target_id);
```

Gets/sets the target ID (entity or event to activate).

**Example:**
```c
g_autoptr(LrgTrigger3D) trigger = lrg_trigger3d_new("quest_trigger", bounds, LRG_TRIGGER_TYPE_VOLUME);
lrg_trigger3d_set_target_id(trigger, "start_quest_1");

const gchar *target = lrg_trigger3d_get_target_id(trigger);
g_print("Trigger targets: %s\n", target);
```

## Testing

### lrg_trigger3d_test_point

```c
gboolean lrg_trigger3d_test_point(const LrgTrigger3D *self, const GrlVector3 *point);
gboolean lrg_trigger3d_test_point_xyz(const LrgTrigger3D *self, gfloat x, gfloat y, gfloat z);
```

Tests if a point is inside the trigger volume.

Only returns TRUE if trigger is enabled.

**Example:**
```c
g_autoptr(GrlVector3) player_pos = get_player_position();
const LrgTrigger3D *water = lrg_level3d_get_trigger(level, "lake_1");

if (lrg_trigger3d_test_point(water, player_pos)) {
    apply_water_effect(player);
    play_splash_sound();
}
```

## Example Usage

### Quest Trigger

```c
g_autoptr(LrgBoundingBox3D) bounds = lrg_bounding_box3d_new(100, 0, 100, 120, 30, 120);
g_autoptr(LrgTrigger3D) quest_trigger = lrg_trigger3d_new("npc_meetup", bounds, LRG_TRIGGER_TYPE_VOLUME);
lrg_trigger3d_set_target_id(quest_trigger, "meet_npc_quest");
lrg_trigger3d_set_one_shot(quest_trigger, TRUE);  /* Only once */
lrg_level3d_add_trigger(level, quest_trigger);
```

### Damage Zone

```c
g_autoptr(LrgBoundingBox3D) lava_bounds = lrg_bounding_box3d_new(0, 0, 0, 50, 10, 50);
g_autoptr(LrgTrigger3D) lava = lrg_trigger3d_new("lava_pit", lava_bounds, LRG_TRIGGER_TYPE_DAMAGE);
lrg_trigger3d_set_target_id(lava, "10");  /* 10 damage per frame */
lrg_level3d_add_trigger(level, lava);
```

### Game Loop Integration

```c
/* In main game loop */
g_autoptr(GrlVector3) player_pos = get_player_position();
g_autoptr(GPtrArray) active_triggers = lrg_level3d_check_triggers(level, player_pos);

for (guint i = 0; i < active_triggers->len; i++) {
    LrgTrigger3D *trigger = g_ptr_array_index(active_triggers, i);
    const gchar *trigger_id = lrg_trigger3d_get_id(trigger);
    const gchar *target = lrg_trigger3d_get_target_id(trigger);
    LrgTriggerType type = lrg_trigger3d_get_trigger_type(trigger);

    g_print("Activated trigger: %s -> %s\n", trigger_id, target);

    switch (type) {
        case LRG_TRIGGER_TYPE_DAMAGE:
            damage_player(atoi(target));
            break;
        case LRG_TRIGGER_TYPE_PORTAL:
            transition_to_level(target);
            break;
        case LRG_TRIGGER_TYPE_VOLUME:
            activate_event(target);
            break;
        default:
            break;
    }

    /* Disable one-shot triggers after activation */
    if (lrg_trigger3d_is_one_shot(trigger))
        lrg_trigger3d_set_enabled(trigger, FALSE);
}
```

## Common Trigger Types

| Type | Use Case | Target Format |
|------|----------|----------------|
| VOLUME | Area entry | Event ID |
| INTERACTION | Player interaction | Quest/event ID |
| DAMAGE | Hazard zone | Damage amount |
| WATER | Swimming zone | Effect name |
| PORTAL | Level transition | Level ID |
| SCRIPT | Custom logic | Script function |

## Performance Tips

1. Only check triggers for active entities
2. Use spatial queries (`lrg_level3d_check_triggers`) for efficiency
3. Disable triggers after one-shot activation
4. Consider trigger volume size (smaller is faster)

## See Also

- [LrgLevel3D](level3d.md) - Contains triggers
- [LrgBoundingBox3D](bounding-box3d.md) - Trigger bounds
- [LrgSpawnPoint3D](spawn-point3d.md) - May trigger spawning
