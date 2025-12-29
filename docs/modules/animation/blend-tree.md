# Blend Trees

`LrgBlendTree` provides parametric blending between multiple animations.

## Overview

Blend trees allow smooth transitions between animations based on continuous parameters (like speed or direction) rather than discrete state changes.

## Type

```c
#define LRG_TYPE_BLEND_TREE (lrg_blend_tree_get_type ())
G_DECLARE_FINAL_TYPE (LrgBlendTree, lrg_blend_tree, LRG, BLEND_TREE, GObject)
```

## Creation

```c
LrgBlendTree *lrg_blend_tree_new (const gchar *name);
```

## Blend Types

```c
typedef enum {
    LRG_BLEND_TYPE_1D,        /* Single parameter (e.g., speed) */
    LRG_BLEND_TYPE_2D_SIMPLE, /* Two parameters, simple blend */
    LRG_BLEND_TYPE_2D_FREEFORM /* Two parameters, freeform positions */
} LrgBlendType;

LrgBlendType lrg_blend_tree_get_blend_type (LrgBlendTree *self);
void lrg_blend_tree_set_blend_type (LrgBlendTree *self, LrgBlendType type);
```

## Parameters

```c
/* 1D blending uses X parameter */
void lrg_blend_tree_set_parameter_x (LrgBlendTree *self, const gchar *name);
const gchar *lrg_blend_tree_get_parameter_x (LrgBlendTree *self);

/* 2D blending uses both X and Y */
void lrg_blend_tree_set_parameter_y (LrgBlendTree *self, const gchar *name);
const gchar *lrg_blend_tree_get_parameter_y (LrgBlendTree *self);
```

## Motion Management

Add clips with their blend positions:

```c
/* Add a motion (clip) at a position */
void lrg_blend_tree_add_motion (LrgBlendTree *self, LrgAnimationClip *clip,
                                 gfloat position_x, gfloat position_y);

guint lrg_blend_tree_get_motion_count (LrgBlendTree *self);
LrgAnimationClip *lrg_blend_tree_get_motion_clip (LrgBlendTree *self, guint index);
void lrg_blend_tree_get_motion_position (LrgBlendTree *self, guint index,
                                          gfloat *x, gfloat *y);
```

## Sampling

```c
/* Sample the blended result */
void lrg_blend_tree_sample (LrgBlendTree *self, gfloat param_x, gfloat param_y,
                             GPtrArray *out_poses);
```

---

## Example: 1D Speed Blend

Blend between idle, walk, and run based on speed:

```c
g_autoptr(LrgBlendTree) locomotion = lrg_blend_tree_new ("locomotion");
lrg_blend_tree_set_blend_type (locomotion, LRG_BLEND_TYPE_1D);
lrg_blend_tree_set_parameter_x (locomotion, "speed");

/* Add motions at speed thresholds */
lrg_blend_tree_add_motion (locomotion, idle_clip, 0.0f, 0.0f);   /* speed = 0 */
lrg_blend_tree_add_motion (locomotion, walk_clip, 0.5f, 0.0f);   /* speed = 0.5 */
lrg_blend_tree_add_motion (locomotion, run_clip, 1.0f, 0.0f);    /* speed = 1.0 */

/* At speed 0.25: blend 50% idle + 50% walk */
/* At speed 0.75: blend 50% walk + 50% run */
```

---

## Example: 2D Directional Blend

Blend based on both speed and direction:

```c
g_autoptr(LrgBlendTree) movement = lrg_blend_tree_new ("movement");
lrg_blend_tree_set_blend_type (movement, LRG_BLEND_TYPE_2D_FREEFORM);
lrg_blend_tree_set_parameter_x (movement, "velocity_x");
lrg_blend_tree_set_parameter_y (movement, "velocity_y");

/* Center: idle */
lrg_blend_tree_add_motion (movement, idle_clip, 0.0f, 0.0f);

/* Cardinal directions */
lrg_blend_tree_add_motion (movement, walk_forward_clip, 0.0f, 1.0f);
lrg_blend_tree_add_motion (movement, walk_back_clip, 0.0f, -1.0f);
lrg_blend_tree_add_motion (movement, walk_left_clip, -1.0f, 0.0f);
lrg_blend_tree_add_motion (movement, walk_right_clip, 1.0f, 0.0f);

/* Diagonals */
lrg_blend_tree_add_motion (movement, walk_forward_left_clip, -0.7f, 0.7f);
lrg_blend_tree_add_motion (movement, walk_forward_right_clip, 0.7f, 0.7f);
```

---

## Integration with State Machine

Use blend trees inside states:

```c
/* Create a locomotion state that uses a blend tree */
LrgAnimationState *locomotion_state = lrg_animation_state_new ("locomotion");
lrg_animation_state_set_blend_tree (locomotion_state, locomotion_blend_tree);

lrg_animation_state_machine_add_state (fsm, locomotion_state);

/* In game loop: update the parameter */
lrg_animation_state_machine_set_float (fsm, "speed", current_speed);
lrg_animation_state_machine_update (fsm, delta_time);
```

---

## Blend Algorithm

### 1D Blending

Finds the two motions nearest to the parameter value and interpolates:

```
param = 0.7, motions at [0.0, 0.5, 1.0]
→ Blend between 0.5 (60%) and 1.0 (40%)
```

### 2D Freeform Blending

Uses triangulation or gradient-based weighting to blend between nearby motions in 2D space.

---

## Performance Tips

1. **Limit motion count** - More motions = more sampling overhead
2. **Match clip durations** - Prevents foot sliding during blend
3. **Sync animation phases** - Use normalized time for looping clips
4. **Cache blend weights** - Recalculate only when parameters change significantly
