# Tween Module

The Tween module provides property animation with easing functions, sequences, and parallel execution for smooth UI animations, camera movements, and game object transitions.

## Overview

The tweening system follows a hierarchical pattern for flexible animation composition:

- **LrgEasing** - Library of 30+ easing functions (linear, quad, cubic, elastic, bounce, etc.)
- **LrgTweenBase** - Abstract base class for all tween types
- **LrgTween** - Single property animation
- **LrgTweenGroup** - Base class for compound tweens
- **LrgTweenSequence** - Run tweens one after another
- **LrgTweenParallel** - Run tweens simultaneously
- **LrgTweenManager** - Coordinates all active tweens

## Quick Start

```c
/* Create a tween manager */
LrgTweenManager *manager = lrg_tween_manager_new ();

/* Create a simple opacity tween (target, property, duration) */
LrgTween *fade = lrg_tween_new (G_OBJECT (sprite), "opacity", 0.5f);
lrg_tween_set_from_float (fade, 0.0f);
lrg_tween_set_to_float (fade, 1.0f);
lrg_tween_base_set_easing (LRG_TWEEN_BASE (fade), LRG_EASING_EASE_OUT_CUBIC);

/* Add to manager and start */
lrg_tween_manager_add (manager, LRG_TWEEN_BASE (fade));
lrg_tween_base_start (LRG_TWEEN_BASE (fade));

/* In game loop */
lrg_tween_manager_update (manager, delta_time);
```

## Easing Functions

| Category | Functions |
|----------|-----------|
| Linear | `LINEAR` |
| Quadratic | `EASE_IN_QUAD`, `EASE_OUT_QUAD`, `EASE_IN_OUT_QUAD` |
| Cubic | `EASE_IN_CUBIC`, `EASE_OUT_CUBIC`, `EASE_IN_OUT_CUBIC` |
| Quartic | `EASE_IN_QUART`, `EASE_OUT_QUART`, `EASE_IN_OUT_QUART` |
| Quintic | `EASE_IN_QUINT`, `EASE_OUT_QUINT`, `EASE_IN_OUT_QUINT` |
| Sine | `EASE_IN_SINE`, `EASE_OUT_SINE`, `EASE_IN_OUT_SINE` |
| Exponential | `EASE_IN_EXPO`, `EASE_OUT_EXPO`, `EASE_IN_OUT_EXPO` |
| Circular | `EASE_IN_CIRC`, `EASE_OUT_CIRC`, `EASE_IN_OUT_CIRC` |
| Elastic | `EASE_IN_ELASTIC`, `EASE_OUT_ELASTIC`, `EASE_IN_OUT_ELASTIC` |
| Back | `EASE_IN_BACK`, `EASE_OUT_BACK`, `EASE_IN_OUT_BACK` |
| Bounce | `EASE_IN_BOUNCE`, `EASE_OUT_BOUNCE`, `EASE_IN_OUT_BOUNCE` |

## Sequences and Parallel

```c
/* Create a sequence: fade in, wait, fade out */
LrgTweenSequence *seq = lrg_tween_sequence_new ();

LrgTween *fade_in = lrg_tween_new (G_OBJECT (label), "opacity", 0.3f);
lrg_tween_set_to_float (fade_in, 1.0f);

LrgTween *fade_out = lrg_tween_new (G_OBJECT (label), "opacity", 0.3f);
lrg_tween_set_to_float (fade_out, 0.0f);

lrg_tween_sequence_append (seq, LRG_TWEEN_BASE (fade_in));
lrg_tween_sequence_append_interval (seq, 1.0f);  /* 1 second delay */
lrg_tween_sequence_append (seq, LRG_TWEEN_BASE (fade_out));

/* Run multiple tweens at once */
LrgTweenParallel *parallel = lrg_tween_parallel_new ();
lrg_tween_parallel_add (parallel, LRG_TWEEN_BASE (move_tween));
lrg_tween_parallel_add (parallel, LRG_TWEEN_BASE (scale_tween));
lrg_tween_parallel_add (parallel, LRG_TWEEN_BASE (rotate_tween));
```

## Looping

```c
/* Infinite ping-pong loop */
lrg_tween_base_set_loop_count (LRG_TWEEN_BASE (tween), -1);  /* -1 = infinite */
lrg_tween_base_set_loop_mode (LRG_TWEEN_BASE (tween), LRG_TWEEN_LOOP_PING_PONG);

/* Fixed number of restarts */
lrg_tween_base_set_loop_count (LRG_TWEEN_BASE (tween), 3);
lrg_tween_base_set_loop_mode (LRG_TWEEN_BASE (tween), LRG_TWEEN_LOOP_RESTART);
```

## Callbacks

```c
/* Connect to signals */
g_signal_connect (tween, "started", G_CALLBACK (on_tween_started), NULL);
g_signal_connect (tween, "updated", G_CALLBACK (on_tween_updated), NULL);
g_signal_connect (tween, "completed", G_CALLBACK (on_tween_completed), NULL);
g_signal_connect (tween, "loop-completed", G_CALLBACK (on_loop), NULL);
```

## YAML Configuration

```yaml
type: Tween
target: "@player.sprite"
property: opacity
from: 0.0
to: 1.0
duration: 0.5
easing: ease-out-cubic
loop-count: 1
loop-mode: restart
```

## Key Classes

| Class | Description |
|-------|-------------|
| `LrgEasing` | Static easing function library |
| `LrgTweenBase` | Abstract base (derivable) |
| `LrgTween` | Single property tween |
| `LrgTweenGroup` | Compound tween base (derivable) |
| `LrgTweenSequence` | Sequential execution |
| `LrgTweenParallel` | Parallel execution |
| `LrgTweenManager` | Active tween coordinator |

## Files

| File | Description |
|------|-------------|
| `src/tween/lrg-easing.h` | Easing function library |
| `src/tween/lrg-tween-base.h` | Abstract base class |
| `src/tween/lrg-tween.h` | Single property tween |
| `src/tween/lrg-tween-group.h` | Group base class |
| `src/tween/lrg-tween-sequence.h` | Sequential tweens |
| `src/tween/lrg-tween-parallel.h` | Parallel tweens |
| `src/tween/lrg-tween-manager.h` | Tween manager |
