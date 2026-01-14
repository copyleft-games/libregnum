# MCP Transports

Libregnum's MCP server supports multiple transport modes for different use cases. The server can be configured to use stdio, HTTP, or both transports simultaneously.

## Transport Types

| Type | Enum Value | Use Case |
|------|------------|----------|
| Stdio | `LRG_MCP_TRANSPORT_STDIO` | Claude Code integration, local AI tools |
| HTTP | `LRG_MCP_TRANSPORT_HTTP` | Network clients, web dashboards |
| Both | `LRG_MCP_TRANSPORT_BOTH` | Maximum flexibility |

## Architecture

Since mcp-glib's `McpServer` only supports one transport at a time, the `LRG_MCP_TRANSPORT_BOTH` mode creates two internal `McpServer` instances:

```
LrgMcpServer (singleton)
├── McpServer *stdio_server     (when STDIO or BOTH)
│   └── McpStdioTransport
├── McpServer *http_server      (when HTTP or BOTH)
│   └── McpHttpServerTransport
└── Tool/Resource providers registered to BOTH servers
```

This ensures all registered tools and resources are available on both transports.

## Stdio Transport (Default)

Best for Claude Code and local AI tool integration. Uses stdin/stdout for JSON-RPC communication.

```c
LrgMcpServer *mcp = lrg_mcp_server_get_default ();

/* Stdio is the default - no configuration needed */
lrg_mcp_server_register_default_providers (mcp);

g_autoptr(GError) error = NULL;
if (!lrg_mcp_server_start (mcp, &error))
{
    g_warning ("MCP start failed: %s", error->message);
}
```

### Claude Code Configuration

Configure Claude Code to spawn your game with MCP:

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

## HTTP Transport

Enables network access to MCP tools and resources. Uses HTTP POST for requests and Server-Sent Events (SSE) for responses.

```c
LrgMcpServer *mcp = lrg_mcp_server_get_default ();

/* Switch to HTTP transport */
lrg_mcp_server_set_transport_type (mcp, LRG_MCP_TRANSPORT_HTTP);
lrg_mcp_server_set_http_port (mcp, 8080);

lrg_mcp_server_register_default_providers (mcp);

g_autoptr(GError) error = NULL;
if (!lrg_mcp_server_start (mcp, &error))
{
    g_warning ("MCP start failed: %s", error->message);
}

/* Query actual port (useful when port=0 for auto-assign) */
guint port = lrg_mcp_server_get_actual_http_port (mcp);
g_print ("MCP HTTP server listening on port %u\n", port);
```

### HTTP Endpoints

| Method | Path | Description |
|--------|------|-------------|
| POST | `/` | Send JSON-RPC requests |
| GET | `/sse` | Server-Sent Events for responses |

### Host Binding

By default, the HTTP server binds to all interfaces. To restrict to localhost:

```c
lrg_mcp_server_set_http_host (mcp, "127.0.0.1");
```

### Auto-Assigned Port

Set port to 0 for the OS to assign an available port:

```c
lrg_mcp_server_set_http_port (mcp, 0);
lrg_mcp_server_start (mcp, NULL);

/* Get the actual assigned port */
guint port = lrg_mcp_server_get_actual_http_port (mcp);
```

## HTTP Authentication

Optional Bearer token authentication protects the HTTP transport:

```c
lrg_mcp_server_set_http_auth (mcp, TRUE, "your-secret-token");
```

Clients must include the `Authorization` header:

```
Authorization: Bearer your-secret-token
```

Requests without valid authentication will receive a 401 Unauthorized response.

### Security Recommendations

1. **Always use authentication** when exposing HTTP transport on non-localhost
2. **Generate random tokens** for production use
3. **Use HTTPS** when available (configure via libsoup/GTlsCertificate)
4. **Bind to localhost** when only local access is needed

## Both Transports

Run stdio and HTTP simultaneously for maximum flexibility:

```c
LrgMcpServer *mcp = lrg_mcp_server_get_default ();

/* Enable both transports */
lrg_mcp_server_set_transport_type (mcp, LRG_MCP_TRANSPORT_BOTH);

/* Configure HTTP */
lrg_mcp_server_set_http_port (mcp, 8080);
lrg_mcp_server_set_http_auth (mcp, TRUE, "secret-token");

lrg_mcp_server_register_default_providers (mcp);

g_autoptr(GError) error = NULL;
if (!lrg_mcp_server_start (mcp, &error))
{
    g_warning ("MCP start failed: %s", error->message);
}
```

This allows:
- Claude Code to connect via stdio (process spawning)
- Web dashboards/tools to connect via HTTP
- Remote debugging while running locally
- Multiple clients to access the same game instance

## API Reference

### Transport Configuration

| Function | Description |
|----------|-------------|
| `lrg_mcp_server_get_transport_type()` | Get current transport mode |
| `lrg_mcp_server_set_transport_type()` | Set transport mode (before start) |

### HTTP Configuration

| Function | Description |
|----------|-------------|
| `lrg_mcp_server_get_http_port()` | Get configured HTTP port |
| `lrg_mcp_server_set_http_port()` | Set HTTP listen port (before start) |
| `lrg_mcp_server_get_http_host()` | Get HTTP bind address |
| `lrg_mcp_server_set_http_host()` | Set HTTP bind address (before start) |
| `lrg_mcp_server_set_http_auth()` | Configure Bearer token auth (before start) |
| `lrg_mcp_server_get_actual_http_port()` | Get actual port (after start) |

### GObject Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `transport-type` | `LrgMcpTransportType` | `STDIO` | Which transport(s) to use |
| `http-port` | `guint` | `8080` | HTTP server port |
| `http-host` | `gchar*` | `NULL` | HTTP bind address |

## Complete Example

```c
#include <libregnum.h>

#ifdef LRG_ENABLE_MCP

static void
setup_mcp_server (void)
{
    g_autoptr(GError) error = NULL;
    LrgMcpServer *mcp;

    mcp = lrg_mcp_server_get_default ();

    /* Use both transports for maximum flexibility */
    lrg_mcp_server_set_transport_type (mcp, LRG_MCP_TRANSPORT_BOTH);

    /* Configure HTTP transport */
    lrg_mcp_server_set_http_port (mcp, 8080);
    lrg_mcp_server_set_http_host (mcp, "127.0.0.1");  /* localhost only */
    lrg_mcp_server_set_http_auth (mcp, TRUE, g_getenv ("MCP_TOKEN"));

    /* Register all built-in tools and resources */
    lrg_mcp_server_register_default_providers (mcp);

    /* Start the server */
    if (!lrg_mcp_server_start (mcp, &error))
    {
        g_warning ("Failed to start MCP server: %s", error->message);
        return;
    }

    g_print ("MCP stdio transport: ready\n");
    g_print ("MCP HTTP transport: http://127.0.0.1:%u\n",
             lrg_mcp_server_get_actual_http_port (mcp));
}

#endif /* LRG_ENABLE_MCP */
```

## Troubleshooting

### HTTP Transport Not Starting

1. Check if port is already in use
2. Verify host binding is valid
3. Check logs for specific errors

### Authentication Failures

1. Ensure token matches exactly (case-sensitive)
2. Check `Authorization` header format: `Bearer <token>`
3. Verify auth is enabled before start

### Stdio Transport Issues

1. Ensure game is spawned correctly by Claude Code
2. Check stdin/stdout are not redirected elsewhere
3. Verify no other process is reading stdin

## See Also

- [MCP Module Index](index.md) - Module overview
- [Architecture](architecture.md) - Type hierarchy and design
- [Claude Code Integration](claude-code-integration.md) - Setup guide
