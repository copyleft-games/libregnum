# MCP Architecture

This document describes the architecture and design patterns used in the MCP module.

## Type Hierarchy

```
GObject
├── LrgMcpServer (final, singleton)
│   - Manages McpServer lifecycle
│   - Aggregates tool/resource providers
│   - Handles multiple transports
│
├── LrgMcpToolGroup (derivable, abstract)
│   │   Implements: LrgMcpToolProvider
│   │   vfuncs: get_group_name(), register_tools(), call_tool()
│   │
│   ├── LrgMcpInputTools (final)
│   ├── LrgMcpScreenshotTools (final)
│   ├── LrgMcpEngineTools (final)
│   ├── LrgMcpEcsTools (final)
│   ├── LrgMcpSaveTools (final)
│   └── LrgMcpDebugTools (final)
│
└── LrgMcpResourceGroup (derivable, abstract)
    │   Implements: LrgMcpResourceProvider
    │   vfuncs: get_group_name(), register_resources(), read_resource()
    │
    ├── LrgMcpEngineResources (final)
    ├── LrgMcpEcsResources (final)
    └── LrgMcpScreenshotResources (final)

Interface: LrgMcpToolProvider
    - list_tools() -> GList* of McpTool*
    - call_tool(name, args) -> McpToolResult*

Interface: LrgMcpResourceProvider
    - list_resources() -> GList* of McpResource*
    - read_resource(uri) -> GList* of McpResourceContents*
```

## Design Patterns

### Singleton Pattern (LrgMcpServer)

The MCP server uses the singleton pattern to ensure only one server instance exists:

```c
LrgMcpServer *
lrg_mcp_server_get_default (void)
{
    static LrgMcpServer *default_server = NULL;

    if (g_once_init_enter (&default_server))
    {
        LrgMcpServer *server = g_object_new (LRG_TYPE_MCP_SERVER, NULL);
        g_once_init_leave (&default_server, server);
    }

    return default_server;
}
```

### Interface Pattern (Tool/Resource Providers)

Interfaces define contracts for providers, allowing flexible implementations:

```c
/* Interface definition */
G_DECLARE_INTERFACE (LrgMcpToolProvider, lrg_mcp_tool_provider, LRG, MCP_TOOL_PROVIDER, GObject)

struct _LrgMcpToolProviderInterface
{
    GTypeInterface parent_iface;

    GList *       (*list_tools) (LrgMcpToolProvider *self);
    McpToolResult *(*call_tool) (LrgMcpToolProvider  *self,
                                 const gchar         *name,
                                 JsonObject          *arguments,
                                 GError             **error);
};
```

### Abstract Base Class Pattern (Tool/Resource Groups)

Abstract base classes provide common functionality while requiring subclass implementations:

```c
/* Abstract base class */
G_DECLARE_DERIVABLE_TYPE (LrgMcpToolGroup, lrg_mcp_tool_group, LRG, MCP_TOOL_GROUP, GObject)

struct _LrgMcpToolGroupClass
{
    GObjectClass parent_class;

    /* Pure virtual methods - must be overridden */
    const gchar *   (*get_group_name)  (LrgMcpToolGroup *self);
    void            (*register_tools)  (LrgMcpToolGroup *self);

    /* Virtual method with default implementation */
    McpToolResult * (*call_tool)       (LrgMcpToolGroup  *self,
                                        const gchar      *name,
                                        JsonObject       *arguments,
                                        GError          **error);

    gpointer _reserved[8];
};
```

### Provider Aggregation

The server aggregates multiple providers and dispatches requests:

```c
/* Server maintains lists of providers */
struct _LrgMcpServerPrivate
{
    GPtrArray *tool_providers;     /* LrgMcpToolProvider* */
    GPtrArray *resource_providers; /* LrgMcpResourceProvider* */
};

/* Aggregated tool listing */
GList *
aggregate_all_tools (LrgMcpServer *self)
{
    GList *all_tools = NULL;

    for (guint i = 0; i < self->priv->tool_providers->len; i++)
    {
        LrgMcpToolProvider *provider = g_ptr_array_index (self->priv->tool_providers, i);
        GList *tools = lrg_mcp_tool_provider_list_tools (provider);
        all_tools = g_list_concat (all_tools, tools);
    }

    return all_tools;
}
```

## Module Structure

```
src/mcp/
├── lrg-mcp.h                        # Master include header
├── lrg-mcp-server.h                 # Server singleton header
├── lrg-mcp-server.c                 # Server implementation
├── lrg-mcp-tool-provider.h          # Tool provider interface
├── lrg-mcp-tool-provider.c          # Interface implementation
├── lrg-mcp-resource-provider.h      # Resource provider interface
├── lrg-mcp-resource-provider.c      # Interface implementation
├── lrg-mcp-tool-group.h             # Abstract tool group
├── lrg-mcp-tool-group.c             # Base class implementation
├── lrg-mcp-resource-group.h         # Abstract resource group
├── lrg-mcp-resource-group.c         # Base class implementation
│
├── tools/                           # Tool implementations
│   ├── lrg-mcp-input-tools.h/.c
│   ├── lrg-mcp-screenshot-tools.h/.c
│   ├── lrg-mcp-engine-tools.h/.c
│   ├── lrg-mcp-ecs-tools.h/.c
│   ├── lrg-mcp-save-tools.h/.c
│   └── lrg-mcp-debug-tools.h/.c
│
└── resources/                       # Resource implementations
    ├── lrg-mcp-engine-resources.h/.c
    ├── lrg-mcp-ecs-resources.h/.c
    └── lrg-mcp-screenshot-resources.h/.c
```

## Conditional Compilation

All MCP code is guarded by `LRG_ENABLE_MCP`:

```c
/* In config.mk */
ifeq ($(MCP),1)
    MCP_CFLAGS := -DLRG_ENABLE_MCP=1 -I$(MCP_GLIB_DIR)/src
    MCP_LIBS := -L$(MCP_GLIB_DIR)/build -lmcp-glib
endif

/* In source files */
#ifdef LRG_ENABLE_MCP

/* MCP code here */

#endif /* LRG_ENABLE_MCP */
```

## Tool Registration Flow

```
1. LrgMcpToolGroup created
   └── constructor calls register_tools() virtual method

2. Subclass register_tools() implementation
   ├── Creates McpTool objects
   ├── Adds parameters to tools
   └── Calls lrg_mcp_tool_group_add_tool() for each

3. Tool added to internal hash table
   └── Keyed by tool name for O(1) lookup

4. When server starts, it queries all providers
   └── lrg_mcp_tool_provider_list_tools() returns all registered tools
```

## Resource URI Handling

Resources use a URI scheme with prefix matching:

```c
/* URI prefix is set during registration */
lrg_mcp_resource_group_set_uri_prefix (group, "libregnum://engine/");

/* URI matching in read_resource */
gboolean
lrg_mcp_resource_group_can_handle_uri (LrgMcpResourceGroup *self,
                                        const gchar         *uri)
{
    const gchar *prefix = lrg_mcp_resource_group_get_uri_prefix (self);
    return g_str_has_prefix (uri, prefix);
}

/* Dynamic URIs parsed in read_resource */
if (g_str_has_prefix (uri, URI_PREFIX "object/"))
{
    const gchar *object_id = uri + strlen (URI_PREFIX "object/");
    return read_object_state (self, object_id, error);
}
```

## Thread Safety

The MCP server runs on the GLib main loop. Tool handlers execute on the main thread, making them safe to access game state:

```c
/* Tool handlers are called from main loop context */
static McpToolResult *
handle_engine_pause (LrgMcpEngineTools  *self,
                     JsonObject         *arguments,
                     GError            **error)
{
    LrgEngine *engine = lrg_engine_get_default ();

    /* Safe to call - we're on the main thread */
    lrg_engine_pause (engine);

    return mcp_tool_result_new_text ("Engine paused");
}
```

## Input Injection Architecture

Input tools use `LrgInputSoftware` for injection:

```c
/* Input tools create a software input source */
struct _LrgMcpInputToolsPrivate
{
    LrgInputSoftware *software_input;
};

/* Constructor */
static void
lrg_mcp_input_tools_init (LrgMcpInputTools *self)
{
    self->priv->software_input = lrg_input_software_new ();

    /* Register with input manager at high priority */
    LrgInputManager *manager = lrg_input_manager_get_default ();
    lrg_input_manager_add_input (manager,
                                  LRG_INPUT (self->priv->software_input),
                                  100); /* Priority above hardware */
}

/* Tool handler */
static McpToolResult *
handle_press_key (LrgMcpInputTools *self,
                  JsonObject       *arguments,
                  GError          **error)
{
    const gchar *key_name = json_object_get_string_member (arguments, "key");
    GrlKey key = parse_key_name (key_name);

    lrg_input_software_press_key (self->priv->software_input, key);

    return mcp_tool_result_new_text ("Key pressed");
}
```

## Screenshot Capture Flow

```
1. Tool/Resource handler called
   └── lrg_screenshot_capture or libregnum://screenshot/current

2. Capture screen using graylib
   └── GrlImage *image = grl_image_new_from_screen();

3. Scale if requested
   └── GrlImage *scaled = grl_image_resize(image, width, height);

4. Export to PNG in memory
   └── guint8 *png = grl_image_export_to_memory(image, ".png", &size);

5. Base64 encode
   └── gchar *base64 = g_base64_encode(png, size);

6. Return as tool result or resource contents
   └── mcp_tool_result_add_image(result, base64, "image/png");
   └── mcp_resource_contents_new_blob(uri, "image/png", base64);
```

## Extensibility Points

### Adding New Tools

1. Create new tool group class extending `LrgMcpToolGroup`
2. Implement `get_group_name()` and `register_tools()`
3. Override `call_tool()` or use default dispatcher
4. Register with server: `lrg_mcp_server_add_tool_provider()`

### Adding New Resources

1. Create new resource group class extending `LrgMcpResourceGroup`
2. Implement `get_group_name()` and `register_resources()`
3. Implement `read_resource()` for URI handling
4. Register with server: `lrg_mcp_server_add_resource_provider()`

### Custom Transports

The mcp-glib library supports custom transports. Configure via `McpServer`:

```c
/* The underlying McpServer from mcp-glib */
McpServer *server = mcp_server_new ("game", "1.0.0");

/* Add HTTP transport */
McpHttpTransport *http = mcp_http_transport_new (8080);
mcp_server_add_transport (server, MCP_TRANSPORT (http));

/* Add stdio transport */
McpStdioTransport *stdio = mcp_stdio_transport_new ();
mcp_server_add_transport (server, MCP_TRANSPORT (stdio));
```
