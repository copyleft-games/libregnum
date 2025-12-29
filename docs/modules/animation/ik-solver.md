# Inverse Kinematics

The IK system provides multiple solver algorithms for positioning bone chains.

## IK Chain

`LrgIKChain` defines a chain of bones to solve:

```c
#define LRG_TYPE_IK_CHAIN (lrg_ik_chain_get_type ())
G_DECLARE_FINAL_TYPE (LrgIKChain, lrg_ik_chain, LRG, IK_CHAIN, GObject)
```

### Chain Functions

```c
LrgIKChain *lrg_ik_chain_new (void);

/* Add bones to chain (root to tip) */
void lrg_ik_chain_add_bone (LrgIKChain *self, LrgBone *bone);
guint lrg_ik_chain_get_bone_count (LrgIKChain *self);
LrgBone *lrg_ik_chain_get_bone (LrgIKChain *self, guint index);

/* Target position */
void lrg_ik_chain_set_target (LrgIKChain *self, gfloat x, gfloat y, gfloat z);
void lrg_ik_chain_get_target (LrgIKChain *self, gfloat *x, gfloat *y, gfloat *z);

/* Pole vector (for knee/elbow direction) */
void lrg_ik_chain_set_pole (LrgIKChain *self, gfloat x, gfloat y, gfloat z);
void lrg_ik_chain_get_pole (LrgIKChain *self, gfloat *x, gfloat *y, gfloat *z);
```

---

## IK Solver Base

```c
#define LRG_TYPE_IK_SOLVER (lrg_ik_solver_get_type ())
G_DECLARE_DERIVABLE_TYPE (LrgIKSolver, lrg_ik_solver, LRG, IK_SOLVER, GObject)
```

### Solver Functions

```c
gboolean lrg_ik_solver_solve (LrgIKSolver *solver, LrgIKChain *chain,
                               guint max_iterations, gfloat tolerance);
gboolean lrg_ik_solver_supports_chain_length (LrgIKSolver *solver, guint bone_count);
```

---

## Solver Types

### FABRIK (Any Length)

Forward And Backward Reaching Inverse Kinematics. Works with any chain length.

```c
#define LRG_TYPE_IK_SOLVER_FABRIK (lrg_ik_solver_fabrik_get_type ())
LrgIKSolverFABRIK *lrg_ik_solver_fabrik_new (void);
```

**Algorithm**: Iteratively adjusts bone positions from tip to root (backward) then root to tip (forward) until convergence.

**Best for**: Tentacles, tails, spines, any multi-bone chain.

### CCD (Any Length)

Cyclic Coordinate Descent. Works with any chain length.

```c
#define LRG_TYPE_IK_SOLVER_CCD (lrg_ik_solver_ccd_get_type ())
LrgIKSolverCCD *lrg_ik_solver_ccd_new (void);
```

**Algorithm**: Rotates each bone from tip to root to minimize distance to target.

**Best for**: Robot arms, mechanical joints.

### Two-Bone (Exactly 2 Bones)

Analytical solution for exactly two bones.

```c
#define LRG_TYPE_IK_SOLVER_TWO_BONE (lrg_ik_solver_two_bone_get_type ())
LrgIKSolverTwoBone *lrg_ik_solver_two_bone_new (void);
```

**Algorithm**: Uses law of cosines for exact, single-frame solution.

**Best for**: Arms (upper arm + forearm), legs (thigh + shin).

### Look-At (Single Bone)

Rotates a single bone to face a target.

```c
#define LRG_TYPE_IK_SOLVER_LOOK_AT (lrg_ik_solver_look_at_get_type ())
LrgIKSolverLookAt *lrg_ik_solver_look_at_new (void);

void lrg_ik_solver_look_at_get_up_vector (LrgIKSolverLookAt *self, gfloat *x, gfloat *y, gfloat *z);
void lrg_ik_solver_look_at_set_up_vector (LrgIKSolverLookAt *self, gfloat x, gfloat y, gfloat z);
```

**Best for**: Head tracking, turrets, eyes.

---

## Solver Comparison

| Solver | Chain Length | Iterations | Use Case |
|--------|-------------|------------|----------|
| FABRIK | Any | 5-20 | Organic movement, spines |
| CCD | Any | 5-20 | Mechanical, robotic |
| TwoBone | Exactly 2 | 1 | Arms, legs |
| LookAt | 1 | 1 | Head, aim direction |

---

## Example: Arm IK

```c
/* Build IK chain for arm */
g_autoptr(LrgIKChain) arm_chain = lrg_ik_chain_new ();
lrg_ik_chain_add_bone (arm_chain, lrg_skeleton_get_bone_by_name (skeleton, "upper_arm"));
lrg_ik_chain_add_bone (arm_chain, lrg_skeleton_get_bone_by_name (skeleton, "forearm"));

/* Create two-bone solver */
g_autoptr(LrgIKSolverTwoBone) solver = lrg_ik_solver_two_bone_new ();

/* In game loop */
void
update_arm_ik (gfloat target_x, gfloat target_y, gfloat target_z)
{
    /* Set target (e.g., weapon aim point) */
    lrg_ik_chain_set_target (arm_chain, target_x, target_y, target_z);

    /* Set pole (elbow direction) */
    lrg_ik_chain_set_pole (arm_chain, 0.0f, -1.0f, 0.0f);  /* Elbow points down */

    /* Solve */
    if (lrg_ik_solver_solve (LRG_IK_SOLVER (solver), arm_chain, 1, 0.001f))
    {
        /* Solution found - bone poses updated */
    }

    /* Recalculate world poses */
    lrg_skeleton_calculate_world_poses (skeleton);
}
```

## Example: Look-At

```c
/* Build chain for head */
g_autoptr(LrgIKChain) head_chain = lrg_ik_chain_new ();
lrg_ik_chain_add_bone (head_chain, lrg_skeleton_get_bone_by_name (skeleton, "head"));

/* Create look-at solver */
g_autoptr(LrgIKSolverLookAt) look_at = lrg_ik_solver_look_at_new ();
lrg_ik_solver_look_at_set_up_vector (look_at, 0.0f, 1.0f, 0.0f);  /* World up */

/* Look at target */
void
look_at_target (gfloat x, gfloat y, gfloat z)
{
    lrg_ik_chain_set_target (head_chain, x, y, z);
    lrg_ik_solver_solve (LRG_IK_SOLVER (look_at), head_chain, 1, 0.0f);
    lrg_skeleton_calculate_world_poses (skeleton);
}
```

## Example: Spine/Tail

```c
/* Build spine chain */
g_autoptr(LrgIKChain) spine_chain = lrg_ik_chain_new ();
lrg_ik_chain_add_bone (spine_chain, lrg_skeleton_get_bone_by_name (skeleton, "spine1"));
lrg_ik_chain_add_bone (spine_chain, lrg_skeleton_get_bone_by_name (skeleton, "spine2"));
lrg_ik_chain_add_bone (spine_chain, lrg_skeleton_get_bone_by_name (skeleton, "spine3"));
lrg_ik_chain_add_bone (spine_chain, lrg_skeleton_get_bone_by_name (skeleton, "chest"));

/* FABRIK works well for organic multi-bone chains */
g_autoptr(LrgIKSolverFABRIK) fabrik = lrg_ik_solver_fabrik_new ();

lrg_ik_chain_set_target (spine_chain, target_x, target_y, target_z);
lrg_ik_solver_solve (LRG_IK_SOLVER (fabrik), spine_chain, 10, 0.01f);
```

## Integration with Animation

Apply IK after animation sampling:

```c
void
update (gfloat delta_time)
{
    /* 1. Update animation */
    lrg_animator_update (animator, delta_time);

    /* 2. Apply IK (modifies local poses) */
    lrg_ik_chain_set_target (arm_chain, aim_target_x, aim_target_y, aim_target_z);
    lrg_ik_solver_solve (LRG_IK_SOLVER (arm_solver), arm_chain, 1, 0.001f);

    /* 3. Calculate final world poses */
    lrg_skeleton_calculate_world_poses (skeleton);
}
```
