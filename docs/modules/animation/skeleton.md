# Skeleton and Bones

This page covers the skeletal hierarchy system: `LrgBonePose`, `LrgBone`, and `LrgSkeleton`.

## LrgBonePose

A GBoxed type representing a bone's transformation (position, rotation as quaternion, scale).

```c
struct _LrgBonePose
{
    gfloat position_x, position_y, position_z;
    gfloat rotation_x, rotation_y, rotation_z, rotation_w;  /* Quaternion */
    gfloat scale_x, scale_y, scale_z;
};
```

### Creation

```c
LrgBonePose *lrg_bone_pose_new (void);  /* Identity transform */
LrgBonePose *lrg_bone_pose_new_with_values (gfloat px, gfloat py, gfloat pz,
                                             gfloat rx, gfloat ry, gfloat rz, gfloat rw,
                                             gfloat sx, gfloat sy, gfloat sz);
LrgBonePose *lrg_bone_pose_copy (const LrgBonePose *self);
void lrg_bone_pose_free (LrgBonePose *self);
```

### Manipulation

```c
void lrg_bone_pose_set_identity (LrgBonePose *self);
void lrg_bone_pose_set_position (LrgBonePose *self, gfloat x, gfloat y, gfloat z);
void lrg_bone_pose_set_rotation (LrgBonePose *self, gfloat x, gfloat y, gfloat z, gfloat w);
void lrg_bone_pose_set_rotation_euler (LrgBonePose *self, gfloat pitch, gfloat yaw, gfloat roll);
void lrg_bone_pose_set_scale (LrgBonePose *self, gfloat x, gfloat y, gfloat z);
void lrg_bone_pose_set_uniform_scale (LrgBonePose *self, gfloat scale);
```

### Interpolation

```c
/* Linear interpolation (slerp for rotation) */
LrgBonePose *lrg_bone_pose_lerp (const LrgBonePose *a, const LrgBonePose *b, gfloat t);
void lrg_bone_pose_lerp_to (const LrgBonePose *a, const LrgBonePose *b, gfloat t, LrgBonePose *result);

/* Additive blending */
LrgBonePose *lrg_bone_pose_blend (const LrgBonePose *a, const LrgBonePose *b, gfloat weight);

/* Hierarchical combination */
LrgBonePose *lrg_bone_pose_multiply (const LrgBonePose *parent, const LrgBonePose *local);
```

---

## LrgBone

A single bone in the skeleton hierarchy.

```c
#define LRG_TYPE_BONE (lrg_bone_get_type ())
G_DECLARE_FINAL_TYPE (LrgBone, lrg_bone, LRG, BONE, GObject)
```

### Creation

```c
LrgBone *lrg_bone_new (const gchar *name, gint index);
```

### Properties

```c
const gchar *lrg_bone_get_name (LrgBone *self);
gint lrg_bone_get_index (LrgBone *self);

/* Parent relationship */
gint lrg_bone_get_parent_index (LrgBone *self);
void lrg_bone_set_parent_index (LrgBone *self, gint parent_index);  /* -1 for root */
gboolean lrg_bone_is_root (LrgBone *self);

/* Bone length */
gfloat lrg_bone_get_length (LrgBone *self);
void lrg_bone_set_length (LrgBone *self, gfloat length);
```

### Poses

Each bone has three poses:

| Pose | Description |
|------|-------------|
| Bind | Rest/reference pose (T-pose) |
| Local | Current animation pose relative to parent |
| World | Accumulated pose with all parent transforms |

```c
/* Bind pose (rest pose) */
const LrgBonePose *lrg_bone_get_bind_pose (LrgBone *self);
void lrg_bone_set_bind_pose (LrgBone *self, const LrgBonePose *pose);

/* Local pose (animation output) */
const LrgBonePose *lrg_bone_get_local_pose (LrgBone *self);
void lrg_bone_set_local_pose (LrgBone *self, const LrgBonePose *pose);

/* World pose (computed from hierarchy) */
const LrgBonePose *lrg_bone_get_world_pose (LrgBone *self);
void lrg_bone_set_world_pose (LrgBone *self, const LrgBonePose *pose);

/* Reset to bind pose */
void lrg_bone_reset_to_bind (LrgBone *self);
```

---

## LrgSkeleton

The complete bone hierarchy.

```c
#define LRG_TYPE_SKELETON (lrg_skeleton_get_type ())
G_DECLARE_DERIVABLE_TYPE (LrgSkeleton, lrg_skeleton, LRG, SKELETON, GObject)
```

### Creation

```c
LrgSkeleton *lrg_skeleton_new (void);
LrgSkeleton *lrg_skeleton_copy (LrgSkeleton *self);
```

### Bone Management

```c
void lrg_skeleton_add_bone (LrgSkeleton *self, LrgBone *bone);
void lrg_skeleton_remove_bone (LrgSkeleton *self, LrgBone *bone);

LrgBone *lrg_skeleton_get_bone (LrgSkeleton *self, gint index);
LrgBone *lrg_skeleton_get_bone_by_name (LrgSkeleton *self, const gchar *name);
guint lrg_skeleton_get_bone_count (LrgSkeleton *self);
GList *lrg_skeleton_get_bones (LrgSkeleton *self);
GList *lrg_skeleton_get_root_bones (LrgSkeleton *self);
GList *lrg_skeleton_get_children (LrgSkeleton *self, LrgBone *bone);
```

### Pose Operations

```c
/* Calculate world poses from local poses */
void lrg_skeleton_calculate_world_poses (LrgSkeleton *self);

/* Reset all bones to bind pose */
void lrg_skeleton_reset_to_bind (LrgSkeleton *self);

/* Set/blend individual bone poses */
void lrg_skeleton_set_pose (LrgSkeleton *self, gint bone_index, const LrgBonePose *pose);
void lrg_skeleton_blend_pose (LrgSkeleton *self, gint bone_index, const LrgBonePose *pose, gfloat weight);
```

### Update

```c
void lrg_skeleton_update (LrgSkeleton *self, gfloat delta_time);
```

## Example: Building a Skeleton

```c
g_autoptr(LrgSkeleton) skeleton = lrg_skeleton_new ();
lrg_skeleton_set_name (skeleton, "humanoid");

/* Root bone */
LrgBone *hips = lrg_bone_new ("hips", 0);
lrg_skeleton_add_bone (skeleton, hips);

/* Spine chain */
LrgBone *spine = lrg_bone_new ("spine", 1);
lrg_bone_set_parent_index (spine, 0);
lrg_skeleton_add_bone (skeleton, spine);

LrgBone *chest = lrg_bone_new ("chest", 2);
lrg_bone_set_parent_index (chest, 1);
lrg_skeleton_add_bone (skeleton, chest);

/* Set bind poses */
g_autoptr(LrgBonePose) hip_pose = lrg_bone_pose_new ();
lrg_bone_pose_set_position (hip_pose, 0.0f, 1.0f, 0.0f);
lrg_bone_set_bind_pose (hips, hip_pose);

/* After animation updates local poses */
lrg_skeleton_calculate_world_poses (skeleton);
```
