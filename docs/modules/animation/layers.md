# Animation Layers

`LrgAnimationLayer` enables layered animation blending with bone masks for partial body animation.

## Overview

Layers allow multiple animations to play simultaneously on different parts of the skeleton. For example:
- Base layer: Locomotion (full body)
- Upper body layer: Aiming/shooting (arms only)
- Additive layer: Breathing, swaying

## Type

```c
#define LRG_TYPE_ANIMATION_LAYER (lrg_animation_layer_get_type ())
G_DECLARE_FINAL_TYPE (LrgAnimationLayer, lrg_animation_layer, LRG, ANIMATION_LAYER, GObject)
```

## Creation

```c
LrgAnimationLayer *lrg_animation_layer_new (const gchar *name);
```

## Properties

```c
const gchar *lrg_animation_layer_get_name (LrgAnimationLayer *self);

/* Layer weight (0.0 to 1.0) */
gfloat lrg_animation_layer_get_weight (LrgAnimationLayer *self);
void lrg_animation_layer_set_weight (LrgAnimationLayer *self, gfloat weight);

/* Blend mode */
LrgLayerBlendMode lrg_animation_layer_get_blend_mode (LrgAnimationLayer *self);
void lrg_animation_layer_set_blend_mode (LrgAnimationLayer *self, LrgLayerBlendMode mode);
```

## Blend Modes

```c
typedef enum {
    LRG_LAYER_BLEND_OVERRIDE,   /* Replace lower layers */
    LRG_LAYER_BLEND_ADDITIVE    /* Add to lower layers */
} LrgLayerBlendMode;
```

## Bone Mask

Control which bones are affected by the layer:

```c
/* Add bones to the mask (affected by this layer) */
void lrg_animation_layer_add_bone_mask (LrgAnimationLayer *self, const gchar *bone_name);
void lrg_animation_layer_remove_bone_mask (LrgAnimationLayer *self, const gchar *bone_name);
void lrg_animation_layer_clear_bone_mask (LrgAnimationLayer *self);

/* Check if bone is in mask */
gboolean lrg_animation_layer_has_bone_mask (LrgAnimationLayer *self, const gchar *bone_name);

/* Get all masked bones */
GList *lrg_animation_layer_get_bone_mask (LrgAnimationLayer *self);

/* Include children automatically */
void lrg_animation_layer_set_include_children (LrgAnimationLayer *self, gboolean include);
gboolean lrg_animation_layer_get_include_children (LrgAnimationLayer *self);
```

## Animation Source

```c
/* Set the animation clip or blend tree for this layer */
void lrg_animation_layer_set_clip (LrgAnimationLayer *self, LrgAnimationClip *clip);
LrgAnimationClip *lrg_animation_layer_get_clip (LrgAnimationLayer *self);

void lrg_animation_layer_set_blend_tree (LrgAnimationLayer *self, LrgBlendTree *tree);
LrgBlendTree *lrg_animation_layer_get_blend_tree (LrgAnimationLayer *self);
```

## Layer Management in State Machine

```c
void lrg_animation_state_machine_add_layer (LrgAnimationStateMachine *self, LrgAnimationLayer *layer);
void lrg_animation_state_machine_remove_layer (LrgAnimationStateMachine *self, const gchar *name);
LrgAnimationLayer *lrg_animation_state_machine_get_layer (LrgAnimationStateMachine *self, const gchar *name);
GList *lrg_animation_state_machine_get_layers (LrgAnimationStateMachine *self);
```

---

## Example: Upper Body Override

```c
/* Create upper body layer for aiming */
g_autoptr(LrgAnimationLayer) upper_body = lrg_animation_layer_new ("upper_body");
lrg_animation_layer_set_blend_mode (upper_body, LRG_LAYER_BLEND_OVERRIDE);
lrg_animation_layer_set_weight (upper_body, 0.0f);  /* Start disabled */

/* Mask upper body bones */
lrg_animation_layer_add_bone_mask (upper_body, "spine2");
lrg_animation_layer_add_bone_mask (upper_body, "chest");
lrg_animation_layer_add_bone_mask (upper_body, "neck");
lrg_animation_layer_add_bone_mask (upper_body, "head");
lrg_animation_layer_add_bone_mask (upper_body, "left_shoulder");
lrg_animation_layer_add_bone_mask (upper_body, "right_shoulder");
lrg_animation_layer_set_include_children (upper_body, TRUE);  /* Include arms */

/* Set aiming animation */
lrg_animation_layer_set_clip (upper_body, aim_clip);

/* Add to state machine */
lrg_animation_state_machine_add_layer (fsm, upper_body);

/* Enable when aiming */
void
on_aim_start (void)
{
    LrgAnimationLayer *layer = lrg_animation_state_machine_get_layer (fsm, "upper_body");
    lrg_animation_layer_set_weight (layer, 1.0f);
}

void
on_aim_end (void)
{
    LrgAnimationLayer *layer = lrg_animation_state_machine_get_layer (fsm, "upper_body");
    lrg_animation_layer_set_weight (layer, 0.0f);
}
```

---

## Example: Additive Breathing

```c
/* Create additive layer for subtle breathing */
g_autoptr(LrgAnimationLayer) breathing = lrg_animation_layer_new ("breathing");
lrg_animation_layer_set_blend_mode (breathing, LRG_LAYER_BLEND_ADDITIVE);
lrg_animation_layer_set_weight (breathing, 0.5f);  /* Subtle effect */

/* Affect chest area */
lrg_animation_layer_add_bone_mask (breathing, "spine2");
lrg_animation_layer_add_bone_mask (breathing, "chest");
lrg_animation_layer_set_include_children (breathing, FALSE);

/* Set breathing animation (additive) */
lrg_animation_layer_set_clip (breathing, breathing_clip);

lrg_animation_state_machine_add_layer (fsm, breathing);
```

---

## Example: Smooth Layer Transition

```c
static gfloat layer_weight = 0.0f;
static gfloat target_weight = 0.0f;

void
update_layer_blend (gfloat delta_time)
{
    /* Smooth transition */
    layer_weight = lerp (layer_weight, target_weight, delta_time * 5.0f);

    LrgAnimationLayer *layer = lrg_animation_state_machine_get_layer (fsm, "upper_body");
    lrg_animation_layer_set_weight (layer, layer_weight);
}

void
enable_layer (void)
{
    target_weight = 1.0f;
}

void
disable_layer (void)
{
    target_weight = 0.0f;
}
```

---

## Layer Evaluation Order

Layers are evaluated from bottom to top:

1. **Base layer** (index 0): Full body locomotion
2. **Override layers**: Replace masked bones
3. **Additive layers**: Add offset to current pose

```
Final Pose = Base + (Override - Base) * weight + Additive * weight
```

---

## Common Layer Setups

| Game Type | Layers |
|-----------|--------|
| Shooter | Base (legs), Upper (aim), Additive (recoil) |
| RPG | Base (body), Upper (gesture), Face (expressions) |
| Platformer | Base (movement), Upper (item carry) |
| Fighting | Base (stance), Upper (attack), Additive (hit reaction) |
