# Tutorial System Module

The Tutorial System provides guided player onboarding with step sequences, UI highlights, input prompts with device-aware glyphs, and progress persistence.

## Overview

The tutorial system guides new players through game mechanics:

- **LrgTutorialStep** - Individual instruction step (boxed)
- **LrgTutorial** - Collection of steps forming a tutorial
- **LrgTutorialManager** - Coordinates tutorials and tracks progress
- **LrgHighlight** - Visual emphasis on UI elements
- **LrgInputPrompt** - Shows control hints with correct glyphs
- **LrgTooltipArrow** - Directional arrow pointing to elements

## Quick Start

```c
/* Get the tutorial manager from engine */
LrgTutorialManager *tutorials = lrg_engine_get_tutorial_manager (engine);

/* Load a tutorial */
LrgTutorial *movement_tutorial = lrg_tutorial_new ("movement");
lrg_tutorial_load (movement_tutorial, "tutorials/movement.yaml", NULL);

/* Register with manager */
lrg_tutorial_manager_register (tutorials, movement_tutorial);

/* Start the tutorial */
lrg_tutorial_manager_start (tutorials, "movement");

/* In game loop */
lrg_tutorial_manager_update (tutorials, delta_time);
```

## Tutorial Steps

### Step Types

| Type | Description |
|------|-------------|
| `TEXT` | Display instructional text |
| `HIGHLIGHT` | Highlight a UI element |
| `INPUT` | Wait for specific input |
| `CONDITION` | Wait for game condition |
| `DELAY` | Wait for time duration |

```c
/* Create steps programmatically */
LrgTutorialStep *step1 = lrg_tutorial_step_new_text (
    "Welcome! Use WASD to move your character."
);

LrgTutorialStep *step2 = lrg_tutorial_step_new_highlight (
    "health-bar",  /* Widget ID to highlight */
    "This is your health. Don't let it reach zero!"
);

LrgTutorialStep *step3 = lrg_tutorial_step_new_input (
    "move",        /* Action name */
    "Try moving now!"
);

LrgTutorialStep *step4 = lrg_tutorial_step_new_condition (
    "player.x > 500",  /* Condition expression */
    "Walk to the right side of the screen."
);
```

## Highlight Effects

```c
LrgHighlight *highlight = lrg_highlight_new ();
lrg_highlight_set_target_widget (highlight, button);
lrg_highlight_set_style (highlight, LRG_HIGHLIGHT_STYLE_GLOW);
lrg_highlight_set_color (highlight, 255, 200, 0, 200);  /* Yellow glow */
lrg_highlight_set_pulse (highlight, TRUE);

/* Add to canvas */
lrg_canvas_add (canvas, LRG_WIDGET (highlight));
```

### Highlight Styles

| Style | Description |
|-------|-------------|
| `OUTLINE` | Animated outline around element |
| `GLOW` | Soft glow effect |
| `DARKEN_OTHERS` | Dim everything except target |
| `SPOTLIGHT` | Circular spotlight on target |

## Input Prompts

The input prompt automatically shows the correct glyph for the current input device:

```c
LrgInputPrompt *prompt = lrg_input_prompt_new ();
lrg_input_prompt_set_action (prompt, "jump");
lrg_input_prompt_set_text (prompt, "Press %s to jump");  /* %s = glyph */

/* Automatically updates when player switches between keyboard and gamepad */
```

### Device Detection

```c
/* Connect to device change signal */
g_signal_connect (input_manager, "device-changed",
                  G_CALLBACK (on_device_changed), prompt);

/* Prompt automatically updates:
 * - Keyboard: "Press SPACE to jump"
 * - Xbox: "Press A to jump" (with A button glyph)
 * - PlayStation: "Press X to jump" (with X button glyph)
 */
```

## Tooltip Arrows

```c
LrgTooltipArrow *arrow = lrg_tooltip_arrow_new ();
lrg_tooltip_arrow_set_target (arrow, target_widget);
lrg_tooltip_arrow_set_direction (arrow, LRG_ARROW_DIRECTION_AUTO);
lrg_tooltip_arrow_set_distance (arrow, 20.0f);
lrg_tooltip_arrow_set_bounce (arrow, TRUE);
```

## Progress Persistence

The tutorial manager implements `LrgSaveable` for automatic progress saving:

```c
/* Check if tutorial completed */
if (lrg_tutorial_manager_is_completed (tutorials, "movement"))
{
    /* Skip tutorial */
}

/* Reset specific tutorial */
lrg_tutorial_manager_reset (tutorials, "movement");

/* Reset all tutorials */
lrg_tutorial_manager_reset_all (tutorials);
```

## Input Blocking

```c
/* Block all input except tutorial-required actions */
lrg_tutorial_step_set_block_input (step, TRUE);
lrg_tutorial_step_set_allowed_actions (step, "move,jump");
```

## YAML Configuration

```yaml
# tutorials/movement.yaml
id: movement
name: "Movement Tutorial"
can-skip: true
steps:
  - type: text
    message: "Welcome to the game!"
    duration: 3.0

  - type: highlight
    target: "wasd-hint"
    style: glow
    message: "Use WASD keys to move."

  - type: input
    action: move
    message: "Try moving around!"
    block-other-input: true

  - type: condition
    expression: "player.position.x > 200"
    message: "Great! Now walk to the door."
    timeout: 30.0
    on-timeout: skip

  - type: text
    message: "Tutorial complete!"
    auto-advance: true
    duration: 2.0
```

## Signals

```c
/* Tutorial signals */
g_signal_connect (tutorial, "step-started", G_CALLBACK (on_step), NULL);
g_signal_connect (tutorial, "step-completed", G_CALLBACK (on_step_done), NULL);
g_signal_connect (tutorial, "completed", G_CALLBACK (on_tutorial_done), NULL);
g_signal_connect (tutorial, "skipped", G_CALLBACK (on_skip), NULL);
```

## Key Classes

| Class | Description |
|-------|-------------|
| `LrgTutorialStep` | Step definition (boxed) |
| `LrgTutorial` | Tutorial sequence |
| `LrgTutorialManager` | Tutorial coordinator |
| `LrgHighlight` | UI highlight widget |
| `LrgInputPrompt` | Input hint display |
| `LrgTooltipArrow` | Pointing arrow widget |

## Files

| File | Description |
|------|-------------|
| `src/tutorial/lrg-tutorial-step.h` | Step boxed type |
| `src/tutorial/lrg-tutorial.h` | Tutorial class |
| `src/tutorial/lrg-tutorial-manager.h` | Manager |
| `src/tutorial/lrg-highlight.h` | Highlight widget |
| `src/tutorial/lrg-input-prompt.h` | Input prompt |
| `src/tutorial/lrg-tooltip-arrow.h` | Arrow widget |
