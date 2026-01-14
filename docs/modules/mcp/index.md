# MCP Module

The MCP (Model Context Protocol) module provides AI-assisted game debugging and control capabilities for libregnum games. It enables tools like Claude Code to interact with running games through a standardized protocol.

## Overview

The module consists of several components:

1. **LrgMcpServer** - Singleton server managing MCP lifecycle and transports
2. **LrgMcpToolProvider** - Interface for tool providers
3. **LrgMcpResourceProvider** - Interface for resource providers
4. **LrgMcpToolGroup** - Abstract base class for tool implementations
5. **LrgMcpResourceGroup** - Abstract base class for resource implementations
6. **Built-in Tool Groups** - Input, Screenshot, Engine, ECS, Save, Debug tools
7. **Built-in Resource Groups** - Engine, ECS, Screenshot resources

## Key Features

- **Loose coupling**: Built only when `MCP=1` is passed to make
- **Multiple transports**: Stdio (Claude Code) and HTTP support
- **Comprehensive tools**: Input injection, screenshots, engine control, ECS manipulation
- **Read-only resources**: Engine state, world/object data, screenshots
- **GObject architecture**: Interfaces and abstract classes for extensibility
- **Type-safe API**: Full GObject introspection annotations

## Quick Start

### Building with MCP Support

```bash
# Build with MCP enabled
make MCP=1

# Debug build with MCP
make MCP=1 DEBUG=1

# Run tests with MCP
make MCP=1 test
```

### Basic Setup

```c
#include <libregnum.h>

#ifdef LRG_ENABLE_MCP

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    LrgMcpServer *mcp;

    /* Initialize engine first */
    LrgEngine *engine = lrg_engine_get_default ();
    if (!lrg_engine_startup (engine, &error))
    {
        g_printerr ("Engine startup failed: %s\n", error->message);
        return 1;
    }

    /* Get MCP server singleton */
    mcp = lrg_mcp_server_get_default ();

    /* Configure server */
    lrg_mcp_server_set_server_name (mcp, "my-game");
    lrg_mcp_server_set_server_version (mcp, "1.0.0");

    /* Register default tool/resource providers */
    lrg_mcp_server_register_default_providers (mcp);

    /* Start the MCP server (stdio transport by default) */
    if (!lrg_mcp_server_start (mcp, &error))
    {
        g_warning ("Failed to start MCP server: %s", error->message);
    }

    /* Run your game loop... */

    /* Cleanup */
    lrg_mcp_server_stop (mcp);
    lrg_engine_shutdown (engine);

    return 0;
}

#endif /* LRG_ENABLE_MCP */
```

### Claude Code Integration

Configure Claude Code to connect to your game:

```json
{
  "mcpServers": {
    "my-game": {
      "command": "/path/to/my-game",
      "args": ["--mcp-server"]
    }
  }
}
```

## Available Tools

### Input Tools

| Tool | Description |
|------|-------------|
| `lrg_input_press_key` | Press a keyboard key |
| `lrg_input_release_key` | Release a keyboard key |
| `lrg_input_tap_key` | Press and release a key |
| `lrg_input_press_mouse_button` | Press a mouse button |
| `lrg_input_release_mouse_button` | Release a mouse button |
| `lrg_input_move_mouse_to` | Move mouse to absolute position |
| `lrg_input_move_mouse_by` | Move mouse by delta |
| `lrg_input_press_gamepad_button` | Press a gamepad button |
| `lrg_input_release_gamepad_button` | Release a gamepad button |
| `lrg_input_set_gamepad_axis` | Set gamepad axis value |
| `lrg_input_clear_all` | Release all held inputs |
| `lrg_input_get_state` | Get current input state |

### Screenshot Tools

| Tool | Description |
|------|-------------|
| `lrg_screenshot_capture` | Capture full screen as base64 PNG |
| `lrg_screenshot_region` | Capture a region as base64 PNG |

### Engine Tools

| Tool | Description |
|------|-------------|
| `lrg_engine_get_info` | Get engine state (FPS, delta, running) |
| `lrg_engine_pause` | Pause the engine |
| `lrg_engine_resume` | Resume the engine |
| `lrg_engine_step_frame` | Advance one frame (when paused) |

### ECS Tools

| Tool | Description |
|------|-------------|
| `lrg_ecs_list_worlds` | List all active worlds |
| `lrg_ecs_list_game_objects` | List objects in a world |
| `lrg_ecs_get_game_object` | Get object details and components |
| `lrg_ecs_get_component` | Get specific component data |
| `lrg_ecs_set_component_property` | Modify a component property |
| `lrg_ecs_get_transform` | Get object transform |
| `lrg_ecs_set_transform` | Set object transform |
| `lrg_ecs_spawn_object` | Spawn a registered object type |
| `lrg_ecs_destroy_object` | Destroy a game object |

### Save Tools

| Tool | Description |
|------|-------------|
| `lrg_save_list_slots` | List save slots |
| `lrg_save_get_info` | Get save slot metadata |
| `lrg_save_create` | Create a new save |
| `lrg_save_load` | Load from a slot |
| `lrg_save_delete` | Delete a save slot |
| `lrg_save_quick_save` | Trigger quick save |
| `lrg_save_quick_load` | Trigger quick load |

### Debug Tools

| Tool | Description |
|------|-------------|
| `lrg_debug_log` | Log a message |
| `lrg_debug_get_fps` | Get detailed FPS statistics |
| `lrg_debug_profiler_start` | Start a profiler section |
| `lrg_debug_profiler_stop` | Stop a profiler section |
| `lrg_debug_profiler_report` | Get profiler report |

## Available Resources

Resources provide read-only access via the `libregnum://` URI scheme.

### Engine Resources

| URI | MIME Type | Description |
|-----|-----------|-------------|
| `libregnum://engine/info` | application/json | Engine state, FPS, delta |
| `libregnum://engine/config` | application/json | Engine configuration |
| `libregnum://engine/registry` | application/json | Registered type names |

### ECS Resources

| URI | MIME Type | Description |
|-----|-----------|-------------|
| `libregnum://ecs/worlds` | application/json | List of active worlds |
| `libregnum://ecs/world/{name}` | application/json | World state and objects |
| `libregnum://ecs/object/{id}` | application/json | GameObject full state |
| `libregnum://ecs/object/{id}/transform` | application/json | Transform data only |

### Screenshot Resources

| URI | MIME Type | Description |
|-----|-----------|-------------|
| `libregnum://screenshot/current` | image/png | Current frame (base64) |
| `libregnum://screenshot/thumbnail` | image/png | Scaled screenshot (256px max) |

## Type Reference

### LrgMcpServer

Singleton MCP server manager.

| Property | Type | Description |
|----------|------|-------------|
| server-name | string | Server name for MCP |
| server-version | string | Server version |
| running | boolean | Whether server is running |

### LrgMcpToolGroup

Abstract base class for tool groups.

| Virtual Method | Description |
|----------------|-------------|
| `get_group_name` | Return the group identifier |
| `register_tools` | Register tools with the group |
| `call_tool` | Handle tool invocation |

### LrgMcpResourceGroup

Abstract base class for resource groups.

| Virtual Method | Description |
|----------------|-------------|
| `get_group_name` | Return the group identifier |
| `register_resources` | Register resources with the group |
| `read_resource` | Handle resource read requests |

## Creating Custom Providers

### Custom Tool Provider

```c
#define MY_TYPE_TOOLS (my_tools_get_type ())
G_DECLARE_FINAL_TYPE (MyTools, my_tools, MY, TOOLS, LrgMcpToolGroup)

struct _MyTools
{
    LrgMcpToolGroup parent_instance;
};

G_DEFINE_TYPE (MyTools, my_tools, LRG_TYPE_MCP_TOOL_GROUP)

static const gchar *
my_tools_get_group_name (LrgMcpToolGroup *group)
{
    return "my-game";
}

static void
my_tools_register_tools (LrgMcpToolGroup *group)
{
    McpTool *tool;

    tool = mcp_tool_new ("my_custom_action", "Do something custom");
    mcp_tool_add_string_parameter (tool, "target", "Target object", TRUE);
    lrg_mcp_tool_group_add_tool (group, tool);
}

static McpToolResult *
my_tools_call_tool (LrgMcpToolGroup  *group,
                    const gchar      *name,
                    JsonObject       *arguments,
                    GError          **error)
{
    if (g_strcmp0 (name, "my_custom_action") == 0)
    {
        const gchar *target = json_object_get_string_member (arguments, "target");
        /* Do something with target */
        return mcp_tool_result_new_text ("Action completed");
    }

    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                 "Unknown tool: %s", name);
    return NULL;
}

static void
my_tools_class_init (MyToolsClass *klass)
{
    LrgMcpToolGroupClass *group_class = LRG_MCP_TOOL_GROUP_CLASS (klass);
    group_class->get_group_name = my_tools_get_group_name;
    group_class->register_tools = my_tools_register_tools;
    group_class->call_tool = my_tools_call_tool;
}

/* Register with server */
LrgMcpServer *mcp = lrg_mcp_server_get_default ();
lrg_mcp_server_add_tool_provider (mcp, LRG_MCP_TOOL_PROVIDER (my_tools_new ()));
```

### Custom Resource Provider

```c
#define MY_TYPE_RESOURCES (my_resources_get_type ())
G_DECLARE_FINAL_TYPE (MyResources, my_resources, MY, RESOURCES, LrgMcpResourceGroup)

static const gchar *
my_resources_get_group_name (LrgMcpResourceGroup *group)
{
    return "my-game";
}

static void
my_resources_register_resources (LrgMcpResourceGroup *group)
{
    McpResource *resource;

    lrg_mcp_resource_group_set_uri_prefix (group, "libregnum://my-game/");

    resource = mcp_resource_new ("libregnum://my-game/stats",
                                 "Game statistics",
                                 "application/json");
    lrg_mcp_resource_group_add_resource (group, resource);
}

static GList *
my_resources_read_resource (LrgMcpResourceGroup  *group,
                            const gchar          *uri,
                            GError              **error)
{
    if (g_strcmp0 (uri, "libregnum://my-game/stats") == 0)
    {
        const gchar *json = "{\"score\": 1000, \"level\": 5}";
        McpResourceContents *contents;
        contents = mcp_resource_contents_new_text (uri, "application/json", json);
        return g_list_append (NULL, contents);
    }

    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                 "Unknown resource: %s", uri);
    return NULL;
}

/* Register with server */
lrg_mcp_server_add_resource_provider (mcp, LRG_MCP_RESOURCE_PROVIDER (my_resources_new ()));
```

## Best Practices

1. **Register default providers** for standard game debugging capabilities
2. **Add custom providers** for game-specific tools and resources
3. **Use meaningful tool names** with clear descriptions
4. **Return structured JSON** from tools for easy parsing
5. **Keep resources read-only** for safety
6. **Handle errors gracefully** - never crash on MCP failures
7. **Stop server on shutdown** - call `lrg_mcp_server_stop()` before exit

## Error Handling

```c
g_autoptr(GError) error = NULL;

if (!lrg_mcp_server_start (mcp, &error))
{
    if (error != NULL)
    {
        g_warning ("MCP server start failed: %s", error->message);
    }
    /* Continue without MCP - it's optional */
}
```

## See Also

- [Transports](transports.md) - Transport modes (stdio, HTTP, both)
- [Architecture](architecture.md) - Type hierarchy and design patterns
- [Tools Reference](tools.md) - Complete tool documentation
- [Resources Reference](resources.md) - Resource URI details
- [Claude Code Integration](claude-code-integration.md) - Setup guide for Claude Code
