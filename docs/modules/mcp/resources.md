# MCP Resources Reference

This document describes the MCP resources available in libregnum. Resources provide read-only access to game state via the `libregnum://` URI scheme.

## URI Scheme

All libregnum resources use the `libregnum://` scheme with the following structure:

```
libregnum://<group>/<resource>[/<parameters>]
```

## Engine Resources (`LrgMcpEngineResources`)

Engine resources provide information about the game engine state.

**URI Prefix:** `libregnum://engine/`

### libregnum://engine/info

Current engine state information.

**MIME Type:** `application/json`

**Response:**
```json
{
  "fps": 60.0,
  "delta_time": 0.0166,
  "running": true,
  "paused": false,
  "version": "1.0.0"
}
```

**Fields:**

| Field | Type | Description |
|-------|------|-------------|
| fps | number | Current frames per second |
| delta_time | number | Time since last frame (seconds) |
| running | boolean | Engine is running |
| paused | boolean | Engine is paused |
| version | string | Libregnum version |

### libregnum://engine/config

Engine configuration settings.

**MIME Type:** `application/json`

**Response:**
```json
{
  "target_fps": 60,
  "vsync": true,
  "fixed_timestep": false
}
```

**Fields:**

| Field | Type | Description |
|-------|------|-------------|
| target_fps | integer | Target frame rate |
| vsync | boolean | VSync enabled |
| fixed_timestep | boolean | Using fixed timestep |

### libregnum://engine/registry

List of registered type names.

**MIME Type:** `application/json`

**Response:**
```json
{
  "types": [
    "player",
    "enemy",
    "projectile",
    "pickup"
  ]
}
```

## ECS Resources (`LrgMcpEcsResources`)

ECS resources provide access to worlds, game objects, and components.

**URI Prefix:** `libregnum://ecs/`

### libregnum://ecs/worlds

List of all active game worlds.

**MIME Type:** `application/json`

**Response:**
```json
{
  "worlds": [
    {
      "name": "main",
      "object_count": 150,
      "active": true
    },
    {
      "name": "ui",
      "object_count": 25,
      "active": true
    }
  ]
}
```

**World Fields:**

| Field | Type | Description |
|-------|------|-------------|
| name | string | World identifier |
| object_count | integer | Number of game objects |
| active | boolean | World is active |

### libregnum://ecs/world/{name}

State and objects for a specific world.

**URI Pattern:** `libregnum://ecs/world/<world_name>`

**Example:** `libregnum://ecs/world/main`

**MIME Type:** `application/json`

**Response:**
```json
{
  "name": "main",
  "active": true,
  "objects": [
    {
      "id": "player-1",
      "name": "Player",
      "active": true
    },
    {
      "id": "enemy-1",
      "name": "Goblin",
      "active": true
    }
  ]
}
```

**Object Fields:**

| Field | Type | Description |
|-------|------|-------------|
| id | string | Unique object identifier |
| name | string | Display name |
| active | boolean | Object is active |

### libregnum://ecs/object/{id}

Full state for a specific game object.

**URI Pattern:** `libregnum://ecs/object/<object_id>`

**Example:** `libregnum://ecs/object/player-1`

**MIME Type:** `application/json`

**Response:**
```json
{
  "id": "player-1",
  "name": "Player",
  "active": true,
  "transform": {
    "x": 100.0,
    "y": 200.0,
    "rotation": 0.0,
    "scale_x": 1.0,
    "scale_y": 1.0
  },
  "components": [
    "LrgTransformComponent",
    "LrgSpriteComponent",
    "LrgColliderComponent",
    "LrgAnimatorComponent"
  ]
}
```

**Fields:**

| Field | Type | Description |
|-------|------|-------------|
| id | string | Unique object identifier |
| name | string | Display name |
| active | boolean | Object is active |
| transform | object | Transform data (if available) |
| components | array | List of component type names |

**Transform Fields:**

| Field | Type | Description |
|-------|------|-------------|
| x | number | X position |
| y | number | Y position |
| rotation | number | Rotation in degrees |
| scale_x | number | X scale factor |
| scale_y | number | Y scale factor |

### libregnum://ecs/object/{id}/transform

Just the transform data for an object.

**URI Pattern:** `libregnum://ecs/object/<object_id>/transform`

**Example:** `libregnum://ecs/object/player-1/transform`

**MIME Type:** `application/json`

**Response:**
```json
{
  "x": 100.0,
  "y": 200.0,
  "rotation": 0.0,
  "scale_x": 1.0,
  "scale_y": 1.0
}
```

## Screenshot Resources (`LrgMcpScreenshotResources`)

Screenshot resources capture the current frame as images.

**URI Prefix:** `libregnum://screenshot/`

### libregnum://screenshot/current

Full resolution screenshot of the current frame.

**MIME Type:** `image/png`

**Response:** Base64-encoded PNG image data (blob resource).

**Notes:**
- Captures at native resolution
- May be large depending on window size
- Use thumbnail for smaller file sizes

### libregnum://screenshot/thumbnail

Scaled-down screenshot (maximum 256 pixels on longest dimension).

**MIME Type:** `image/png`

**Response:** Base64-encoded PNG image data (blob resource).

**Notes:**
- Maximum dimension is 256 pixels
- Aspect ratio is preserved
- Suitable for quick previews

## Accessing Resources

### From MCP Client

Resources are accessed via the MCP `resources/read` method:

```json
{
  "jsonrpc": "2.0",
  "method": "resources/read",
  "params": {
    "uri": "libregnum://engine/info"
  },
  "id": 1
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "result": {
    "contents": [
      {
        "uri": "libregnum://engine/info",
        "mimeType": "application/json",
        "text": "{\"fps\": 60.0, ...}"
      }
    ]
  },
  "id": 1
}
```

### From Code

```c
LrgMcpServer *server = lrg_mcp_server_get_default ();
g_autoptr(GError) error = NULL;
GList *contents;

/* Find provider that handles this URI */
LrgMcpResourceProvider *provider = lrg_mcp_server_find_resource_provider (server, "libregnum://engine/info");

if (provider != NULL)
{
    contents = lrg_mcp_resource_provider_read_resource (provider,
                                                         "libregnum://engine/info",
                                                         &error);
    if (contents != NULL)
    {
        /* Process contents */
        g_list_free_full (contents, g_object_unref);
    }
}
```

## Resource Content Types

### Text Resources

Text resources return content as a string:

```json
{
  "uri": "libregnum://engine/info",
  "mimeType": "application/json",
  "text": "{\"fps\": 60.0, \"running\": true}"
}
```

### Blob Resources

Blob resources return binary content as base64:

```json
{
  "uri": "libregnum://screenshot/current",
  "mimeType": "image/png",
  "blob": "iVBORw0KGgoAAAANSUhEUgAA..."
}
```

## Error Handling

If a resource is not found, the server returns an error:

```json
{
  "jsonrpc": "2.0",
  "error": {
    "code": -32002,
    "message": "Resource not found: libregnum://unknown/resource"
  },
  "id": 1
}
```

Common error conditions:
- Resource URI not recognized
- Object/world not found (for dynamic URIs)
- Engine not running (for state resources)
- Screenshot capture failed (for screenshot resources)

## Creating Custom Resources

See the [Architecture](architecture.md) documentation for details on implementing custom resource providers.

Example resource provider:

```c
static void
my_resources_register_resources (LrgMcpResourceGroup *group)
{
    lrg_mcp_resource_group_set_uri_prefix (group, "libregnum://my-game/");

    McpResource *res = mcp_resource_new ("libregnum://my-game/stats",
                                         "Game statistics",
                                         "application/json");
    lrg_mcp_resource_group_add_resource (group, res);
}

static GList *
my_resources_read_resource (LrgMcpResourceGroup  *group,
                            const gchar          *uri,
                            GError              **error)
{
    if (g_strcmp0 (uri, "libregnum://my-game/stats") == 0)
    {
        gchar *json = g_strdup_printf ("{\"score\": %d}", get_score ());
        McpResourceContents *contents;
        contents = mcp_resource_contents_new_text (uri, "application/json", json);
        g_free (json);
        return g_list_append (NULL, contents);
    }

    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                 "Unknown resource: %s", uri);
    return NULL;
}
```
