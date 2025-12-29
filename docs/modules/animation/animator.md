# LrgAnimator

`LrgAnimator` provides simple animation playback with crossfading support.

## Type

```c
#define LRG_TYPE_ANIMATOR (lrg_animator_get_type ())
G_DECLARE_DERIVABLE_TYPE (LrgAnimator, lrg_animator, LRG, ANIMATOR, GObject)
```

## Creation

```c
LrgAnimator *lrg_animator_new (LrgSkeleton *skeleton);
```

## Skeleton

```c
LrgSkeleton *lrg_animator_get_skeleton (LrgAnimator *self);
void lrg_animator_set_skeleton (LrgAnimator *self, LrgSkeleton *skeleton);
```

## Clip Management

```c
void lrg_animator_add_clip (LrgAnimator *self, const gchar *name, LrgAnimationClip *clip);
void lrg_animator_remove_clip (LrgAnimator *self, const gchar *name);
LrgAnimationClip *lrg_animator_get_clip (LrgAnimator *self, const gchar *name);
```

## Playback Control

```c
/* Immediate playback */
void lrg_animator_play (LrgAnimator *self, const gchar *name);

/* Smooth transition */
void lrg_animator_crossfade (LrgAnimator *self, const gchar *name, gfloat duration);

/* Control */
void lrg_animator_stop (LrgAnimator *self);
void lrg_animator_pause (LrgAnimator *self);
void lrg_animator_resume (LrgAnimator *self);
```

## Playback State

```c
typedef enum {
    LRG_ANIMATOR_STOPPED,
    LRG_ANIMATOR_PLAYING,
    LRG_ANIMATOR_PAUSED
} LrgAnimatorState;

LrgAnimatorState lrg_animator_get_state (LrgAnimator *self);
const gchar *lrg_animator_get_current_clip (LrgAnimator *self);
```

## Time Control

```c
gfloat lrg_animator_get_time (LrgAnimator *self);
void lrg_animator_set_time (LrgAnimator *self, gfloat time);

gfloat lrg_animator_get_speed (LrgAnimator *self);
void lrg_animator_set_speed (LrgAnimator *self, gfloat speed);
```

## Update

```c
void lrg_animator_update (LrgAnimator *self, gfloat delta_time);
```

This updates playback time, samples the current clip, and applies poses to the skeleton.

## Signals

```c
/* Emitted when animation events fire */
void (*event) (LrgAnimator *self, const LrgAnimationEvent *event);
```

## Example: Basic Usage

```c
g_autoptr(LrgAnimator) animator = lrg_animator_new (skeleton);

/* Add clips */
lrg_animator_add_clip (animator, "idle", idle_clip);
lrg_animator_add_clip (animator, "walk", walk_clip);
lrg_animator_add_clip (animator, "run", run_clip);

/* Play idle */
lrg_animator_play (animator, "idle");

/* In game loop */
lrg_animator_update (animator, delta_time);
lrg_skeleton_calculate_world_poses (skeleton);
```

## Example: Crossfading

```c
/* Player starts moving */
if (is_moving && strcmp (lrg_animator_get_current_clip (animator), "walk") != 0)
{
    lrg_animator_crossfade (animator, "walk", 0.25f);  /* 250ms blend */
}

/* Player stops */
if (!is_moving && strcmp (lrg_animator_get_current_clip (animator), "idle") != 0)
{
    lrg_animator_crossfade (animator, "idle", 0.25f);
}
```

## Example: Event Handling

```c
static void
on_animation_event (LrgAnimator *animator,
                    const LrgAnimationEvent *event,
                    gpointer user_data)
{
    if (g_strcmp0 (event->name, "footstep") == 0)
    {
        play_footstep_sound (event->string_param);
    }
    else if (g_strcmp0 (event->name, "spawn_particle") == 0)
    {
        spawn_particle_at_bone (event->string_param);
    }
}

g_signal_connect (animator, "event", G_CALLBACK (on_animation_event), NULL);
```

## Example: Speed Control

```c
/* Slow motion */
lrg_animator_set_speed (animator, 0.5f);

/* Normal speed */
lrg_animator_set_speed (animator, 1.0f);

/* Double speed */
lrg_animator_set_speed (animator, 2.0f);

/* Reverse playback */
lrg_animator_set_speed (animator, -1.0f);
```

## When to Use LrgAnimator vs LrgAnimationStateMachine

| Use Case | Recommendation |
|----------|----------------|
| Simple playback with few clips | LrgAnimator |
| Parameter-driven transitions | LrgAnimationStateMachine |
| Complex state logic | LrgAnimationStateMachine |
| Blend trees | LrgAnimationStateMachine |
