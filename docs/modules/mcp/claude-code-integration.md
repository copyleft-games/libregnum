# Claude Code Integration Guide

This guide explains how to set up and use Claude Code with libregnum games for AI-assisted debugging and control.

## Overview

Claude Code is Anthropic's official CLI for Claude. When configured with an MCP server, Claude Code can:

- Take screenshots of your game
- Analyze visual game state
- Inject input (keyboard, mouse, gamepad)
- Query and manipulate game objects
- Control engine state (pause, resume, step)
- Access save/load functionality

## Prerequisites

1. Claude Code installed and authenticated
2. libregnum game built with `MCP=1`
3. Game configured to start MCP server

## Configuration

### Step 1: Build Your Game with MCP

```bash
# Build libregnum with MCP support
cd libregnum
make MCP=1

# Build your game linking against libregnum
make MCP=1
```

### Step 2: Add MCP Server Code

In your game's main file:

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    gboolean mcp_mode = FALSE;

    /* Check for MCP server flag */
    for (int i = 1; i < argc; i++)
    {
        if (g_strcmp0 (argv[i], "--mcp-server") == 0)
        {
            mcp_mode = TRUE;
            break;
        }
    }

    /* Initialize engine */
    LrgEngine *engine = lrg_engine_get_default ();
    if (!lrg_engine_startup (engine, &error))
    {
        g_printerr ("Engine startup failed: %s\n", error->message);
        return 1;
    }

#ifdef LRG_ENABLE_MCP
    if (mcp_mode)
    {
        /* Configure and start MCP server */
        LrgMcpServer *mcp = lrg_mcp_server_get_default ();

        lrg_mcp_server_set_server_name (mcp, "my-game");
        lrg_mcp_server_set_server_version (mcp, "1.0.0");
        lrg_mcp_server_register_default_providers (mcp);

        if (!lrg_mcp_server_start (mcp, &error))
        {
            g_printerr ("MCP server failed: %s\n", error->message);
            return 1;
        }
    }
#endif

    /* Run your game loop */
    while (lrg_engine_is_running (engine))
    {
        lrg_engine_update (engine, lrg_engine_get_delta_time (engine));
        /* ... */
    }

#ifdef LRG_ENABLE_MCP
    if (mcp_mode)
    {
        lrg_mcp_server_stop (lrg_mcp_server_get_default ());
    }
#endif

    lrg_engine_shutdown (engine);
    return 0;
}
```

### Step 3: Configure Claude Code

Add to your Claude Code configuration (`~/.config/claude/claude_code_config.json` or similar):

```json
{
  "mcpServers": {
    "my-game": {
      "command": "/path/to/my-game",
      "args": ["--mcp-server"],
      "env": {
        "LD_LIBRARY_PATH": "/path/to/libregnum/build/lib"
      }
    }
  }
}
```

For development, you might use:

```json
{
  "mcpServers": {
    "my-game-dev": {
      "command": "./build/my-game",
      "args": ["--mcp-server"],
      "cwd": "/home/user/projects/my-game",
      "env": {
        "LD_LIBRARY_PATH": "../libregnum/build/lib:../libregnum/deps/graylib/build/lib"
      }
    }
  }
}
```

## Usage Scenarios

### Scenario 1: Visual Debugging

Ask Claude to help debug visual issues:

```
Claude, take a screenshot of my game and tell me if the player
sprite is positioned correctly relative to the platforms.
```

Claude will:
1. Call `lrg_screenshot_capture` tool
2. Analyze the returned image
3. Provide feedback on positioning

### Scenario 2: Automated Testing

Have Claude test gameplay mechanics:

```
Claude, please test the jump mechanic:
1. Press and hold 'w' to move forward
2. Press 'space' to jump
3. Take a screenshot at the peak of the jump
4. Release all keys
```

Claude will:
1. Call `lrg_input_press_key` with "w"
2. Call `lrg_input_tap_key` with "space"
3. Wait briefly
4. Call `lrg_screenshot_capture`
5. Call `lrg_input_clear_all`
6. Analyze the jump height

### Scenario 3: State Inspection

Query game state programmatically:

```
Claude, list all game objects in the main world and find the
player's current position.
```

Claude will:
1. Read `libregnum://ecs/worlds` resource
2. Read `libregnum://ecs/world/main` resource
3. Find the player object
4. Read `libregnum://ecs/object/<player-id>/transform` resource
5. Report the position

### Scenario 4: Game Object Manipulation

Modify game state for testing:

```
Claude, move the player to position (500, 300) and spawn
an enemy at (600, 300).
```

Claude will:
1. Call `lrg_ecs_set_transform` for the player
2. Call `lrg_ecs_spawn_object` for the enemy
3. Confirm the changes

### Scenario 5: Performance Analysis

Profile game performance:

```
Claude, start profiling the "update" and "render" sections,
run for a few seconds, then show me the profiler report.
```

Claude will:
1. Call `lrg_debug_profiler_start` for "update"
2. Call `lrg_debug_profiler_start` for "render"
3. Wait
4. Call `lrg_debug_profiler_stop` for both
5. Call `lrg_debug_profiler_report`
6. Analyze the timing data

## Tips and Best Practices

### Use Thumbnails for Quick Checks

The thumbnail resource is smaller and faster:

```
Read libregnum://screenshot/thumbnail instead of taking a full screenshot
```

### Pause for Accurate Analysis

Pause the engine when precise analysis is needed:

```
Claude, pause the game, take a screenshot, then resume.
```

### Clear Input State

Always clear inputs after testing:

```
Claude, after any input testing, make sure to call lrg_input_clear_all
```

### Use Step Frame for Animation

Step through animations frame by frame:

```
Claude, pause the game, then step frame by frame and take
a screenshot each time to analyze the animation.
```

### Query Before Modifying

Always query state before modifying:

```
Claude, first list the game objects, then find the one named
"Boss", then modify its health component.
```

## Troubleshooting

### MCP Server Not Starting

Check that:
- Game was built with `MCP=1`
- `--mcp-server` flag is passed
- Library paths are correct in the config

### Screenshots Not Working

Screenshots require a running graphics context:
- Ensure the game window is open
- Make sure graylib is properly initialized
- Check for error messages in stdout

### Input Not Registered

Input injection requires:
- Active window focus (for some systems)
- Input manager properly initialized
- Software input source registered

### Resources Not Found

Check that:
- The engine is running and initialized
- Worlds and objects exist before querying
- URI spelling is correct

## Security Considerations

- MCP server is designed for development/debugging
- Disable MCP in release builds when not needed
- The server has full access to game state
- Input injection can affect gameplay

## Example Session

Here's an example Claude Code session with a libregnum game:

```
User: Take a screenshot of my game and describe what you see.

Claude: I'll capture a screenshot of your game.
[Calls lrg_screenshot_capture tool]

Looking at the screenshot, I can see:
- A 2D platformer level with a blue sky background
- The player character (appears to be a small red square) is near
  the center of the screen
- There are 3 green platforms at different heights
- An enemy (yellow triangle) is on the rightmost platform
- The HUD shows "Score: 150" in the top-left corner

Would you like me to analyze anything specific or help test a
game mechanic?

User: Move the player to the rightmost platform where the enemy is.

Claude: I'll move the player to that position.
[Calls lrg_ecs_list_game_objects to find player ID]
[Calls lrg_ecs_get_game_object to get current position]
[Calls lrg_ecs_set_transform with new coordinates]

Done! I've moved the player from (320, 240) to (600, 150), which
should place them on the rightmost platform near the enemy.

Would you like me to take another screenshot to verify the position?
```

## Reference

- [MCP Module Index](index.md)
- [Tools Reference](tools.md)
- [Resources Reference](resources.md)
- [Architecture](architecture.md)
