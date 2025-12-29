# Post-Processing Module

The Post-Processing module provides a flexible effect pipeline for screen-space visual effects like bloom, color grading, anti-aliasing, and accessibility filters.

## Overview

Post-processing effects are applied after the scene is rendered, modifying the final image before display:

- **LrgPostEffect** - Base class for all effects (derivable)
- **LrgPostProcessor** - Manages the effect chain and render targets

## Quick Start

```c
/* Create post-processor (window size) */
g_autoptr(LrgPostProcessor) processor = lrg_post_processor_new (800, 600);

/* Add effects */
lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (lrg_bloom_new ()));
lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (lrg_vignette_new ()));
lrg_post_processor_add_effect (processor, LRG_POST_EFFECT (lrg_fxaa_new ()));

/* In game loop */
lrg_post_processor_begin_capture (processor);
/* ... render scene ... */
lrg_post_processor_end_capture (processor);
lrg_post_processor_render (processor, delta_time);
```

## Effect Pipeline

Effects are applied in priority order:

1. **Capture** - Scene is rendered to an offscreen texture
2. **Chain** - Effects are applied sequentially (low priority first)
3. **Output** - Final result is displayed on screen

Each effect reads from one texture and writes to another, ping-ponging through the chain.

## Built-in Effects

| Effect | Description | Use Case |
|--------|-------------|----------|
| LrgBloom | Bright area glow | Fire, magic, lights |
| LrgVignette | Edge darkening | Focus, cinematic look |
| LrgColorGrade | Color adjustments | Mood, style |
| LrgFxaa | Anti-aliasing | Smooth edges |
| LrgFilmGrain | Film texture | Retro, stylized |
| LrgScreenShake | Camera shake | Impacts, explosions |
| LrgColorblindFilter | Accessibility | Color correction |

## Files

| File | Description |
|------|-------------|
| [post-effect.md](post-effect.md) | LrgPostEffect base class |
| [post-processor.md](post-processor.md) | LrgPostProcessor pipeline |
| [effects.md](effects.md) | Built-in effect types |
| [accessibility.md](accessibility.md) | Colorblind filter |
