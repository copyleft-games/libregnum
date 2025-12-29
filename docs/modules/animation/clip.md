# Animation Clips

`LrgAnimationClip` contains keyframe animation data organized into tracks per bone.

## Type

```c
#define LRG_TYPE_ANIMATION_CLIP (lrg_animation_clip_get_type ())
G_DECLARE_DERIVABLE_TYPE (LrgAnimationClip, lrg_animation_clip, LRG, ANIMATION_CLIP, GObject)
```

## Creation

```c
LrgAnimationClip *lrg_animation_clip_new (const gchar *name);
```

## Properties

```c
const gchar *lrg_animation_clip_get_name (LrgAnimationClip *self);

gfloat lrg_animation_clip_get_duration (LrgAnimationClip *self);
void lrg_animation_clip_set_duration (LrgAnimationClip *self, gfloat duration);

LrgAnimationLoopMode lrg_animation_clip_get_loop_mode (LrgAnimationClip *self);
void lrg_animation_clip_set_loop_mode (LrgAnimationClip *self, LrgAnimationLoopMode mode);
```

## Loop Modes

```c
typedef enum {
    LRG_ANIMATION_LOOP_NONE,      /* Play once and stop */
    LRG_ANIMATION_LOOP_REPEAT,    /* Loop from start */
    LRG_ANIMATION_LOOP_PING_PONG  /* Reverse at end */
} LrgAnimationLoopMode;
```

## Tracks

Each track animates one bone:

```c
/* Add a track for a bone */
guint lrg_animation_clip_add_track (LrgAnimationClip *self, const gchar *bone_name);

guint lrg_animation_clip_get_track_count (LrgAnimationClip *self);
const gchar *lrg_animation_clip_get_track_bone_name (LrgAnimationClip *self, guint track_index);
```

## Keyframes

```c
void lrg_animation_clip_add_keyframe (LrgAnimationClip *self, guint track_index,
                                       const LrgAnimationKeyframe *keyframe);
guint lrg_animation_clip_get_keyframe_count (LrgAnimationClip *self, guint track_index);
const LrgAnimationKeyframe *lrg_animation_clip_get_keyframe (LrgAnimationClip *self,
                                                              guint track_index, guint keyframe_index);

/* Auto-calculate smooth tangents */
void lrg_animation_clip_calculate_smooth_tangents (LrgAnimationClip *self);
```

## Sampling

```c
/* Sample all tracks at a time */
void lrg_animation_clip_sample (LrgAnimationClip *self, gfloat time, GPtrArray *out_poses);

/* Sample a single track */
void lrg_animation_clip_sample_track (LrgAnimationClip *self, guint track_index,
                                       gfloat time, LrgBonePose *out_pose);
```

## Animation Events

Events trigger callbacks at specific times:

```c
void lrg_animation_clip_add_event (LrgAnimationClip *self, const LrgAnimationEvent *event);
guint lrg_animation_clip_get_event_count (LrgAnimationClip *self);
const LrgAnimationEvent *lrg_animation_clip_get_event (LrgAnimationClip *self, guint index);

/* Get events in a time range (for playback) */
GList *lrg_animation_clip_get_events_in_range (LrgAnimationClip *self,
                                                 gfloat start_time, gfloat end_time);
```

## Example: Building a Clip

```c
/* Create a 2-second walk animation */
g_autoptr(LrgAnimationClip) walk = lrg_animation_clip_new ("walk");
lrg_animation_clip_set_duration (walk, 2.0f);
lrg_animation_clip_set_loop_mode (walk, LRG_ANIMATION_LOOP_REPEAT);

/* Add tracks */
guint hip_track = lrg_animation_clip_add_track (walk, "hips");
guint leg_track = lrg_animation_clip_add_track (walk, "left_leg");

/* Add keyframes */
LrgAnimationKeyframe kf = { 0 };
kf.time = 0.0f;
lrg_bone_pose_set_position (&kf.pose, 0.0f, 0.0f, 0.0f);
lrg_animation_clip_add_keyframe (walk, hip_track, &kf);

kf.time = 1.0f;
lrg_bone_pose_set_position (&kf.pose, 0.0f, 0.1f, 0.0f);
lrg_animation_clip_add_keyframe (walk, hip_track, &kf);

kf.time = 2.0f;
lrg_bone_pose_set_position (&kf.pose, 0.0f, 0.0f, 0.0f);
lrg_animation_clip_add_keyframe (walk, hip_track, &kf);

/* Auto-smooth the tangents */
lrg_animation_clip_calculate_smooth_tangents (walk);

/* Add footstep event */
LrgAnimationEvent evt = { 0 };
evt.time = 0.5f;
evt.name = "footstep";
evt.string_param = "left";
lrg_animation_clip_add_event (walk, &evt);
```

## Example: Sampling

```c
/* Sample at 0.75 seconds */
g_autoptr(GPtrArray) poses = g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_bone_pose_free);
g_ptr_array_set_size (poses, lrg_animation_clip_get_track_count (clip));

for (guint i = 0; i < poses->len; i++)
    poses->pdata[i] = lrg_bone_pose_new ();

lrg_animation_clip_sample (clip, 0.75f, poses);

/* Apply to skeleton */
for (guint i = 0; i < poses->len; i++)
{
    const gchar *bone_name = lrg_animation_clip_get_track_bone_name (clip, i);
    LrgBone *bone = lrg_skeleton_get_bone_by_name (skeleton, bone_name);
    if (bone)
        lrg_bone_set_local_pose (bone, poses->pdata[i]);
}
```
