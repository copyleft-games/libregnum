# Animation Module

The Animation module provides a complete skeletal animation system with state machines, blend trees, animation layers, and inverse kinematics.

## Overview

- **LrgBonePose** - Transform data (position, rotation, scale)
- **LrgBone** - Individual bone with bind/local/world poses
- **LrgSkeleton** - Hierarchical bone structure
- **LrgAnimationClip** - Keyframe animation data
- **LrgAnimator** - Simple animation playback
- **LrgAnimationStateMachine** - Parameter-driven state transitions
- **LrgBlendTree** - Multi-animation blending
- **LrgAnimationLayer** - Layered animation with masks
- **LrgIKSolver** - Inverse kinematics solvers

## Quick Start

```c
/* Create skeleton */
g_autoptr(LrgSkeleton) skeleton = lrg_skeleton_new ();

/* Add bones */
LrgBone *root = lrg_bone_new ("root", 0);
LrgBone *spine = lrg_bone_new ("spine", 1);
lrg_bone_set_parent_index (spine, 0);

lrg_skeleton_add_bone (skeleton, root);
lrg_skeleton_add_bone (skeleton, spine);

/* Create animator */
g_autoptr(LrgAnimator) animator = lrg_animator_new (skeleton);

/* Add and play animation */
lrg_animator_add_clip (animator, "idle", idle_clip);
lrg_animator_play (animator, "idle");

/* In game loop */
lrg_animator_update (animator, delta_time);
lrg_skeleton_calculate_world_poses (skeleton);
```

## Architecture

```
LrgAnimationStateMachine
    |
    +-- LrgAnimationState (contains clips or blend trees)
    |       |
    |       +-- LrgAnimationClip
    |       |       |
    |       |       +-- Tracks (per bone)
    |       |               |
    |       |               +-- LrgAnimationKeyframe
    |       |
    |       +-- LrgBlendTree
    |
    +-- LrgAnimationTransition (conditions for state changes)
    |
    +-- LrgAnimationLayer (for layered blending)

LrgSkeleton
    |
    +-- LrgBone (hierarchical)
            |
            +-- LrgBonePose (bind, local, world)

LrgIKChain + LrgIKSolver (post-process)
```

## Files

| File | Description |
|------|-------------|
| [skeleton.md](skeleton.md) | Bones and skeleton hierarchy |
| [clip.md](clip.md) | Animation clips and keyframes |
| [animator.md](animator.md) | Basic animation playback |
| [state-machine.md](state-machine.md) | State machine controller |
| [blend-tree.md](blend-tree.md) | Animation blending |
| [layers.md](layers.md) | Layered animation |
| [ik-solver.md](ik-solver.md) | Inverse kinematics |
