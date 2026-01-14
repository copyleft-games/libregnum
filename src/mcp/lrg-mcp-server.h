/* lrg-mcp-server.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP server singleton for libregnum.
 *
 * The MCP server enables AI-assisted game debugging by exposing
 * tools for input injection, screenshot capture, engine control,
 * and more via the Model Context Protocol.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-mcp-enums.h"
#include "lrg-mcp-tool-provider.h"
#include "lrg-mcp-resource-provider.h"

G_BEGIN_DECLS

#define LRG_TYPE_MCP_SERVER (lrg_mcp_server_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMcpServer, lrg_mcp_server, LRG, MCP_SERVER, GObject)

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_mcp_server_get_default:
 *
 * Gets the default MCP server instance.
 *
 * The server is created on first call and exists for the lifetime
 * of the application. It is not started automatically; call
 * lrg_mcp_server_start() to begin accepting connections.
 *
 * Returns: (transfer none): The default #LrgMcpServer instance
 */
LRG_AVAILABLE_IN_ALL
LrgMcpServer * lrg_mcp_server_get_default (void);

/* ==========================================================================
 * Configuration
 * ========================================================================== */

/**
 * lrg_mcp_server_get_server_name:
 * @self: an #LrgMcpServer
 *
 * Gets the server name reported to MCP clients.
 *
 * Returns: (transfer none): the server name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_mcp_server_get_server_name (LrgMcpServer *self);

/**
 * lrg_mcp_server_set_server_name:
 * @self: an #LrgMcpServer
 * @name: the server name
 *
 * Sets the server name. Must be called before starting the server.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mcp_server_set_server_name (LrgMcpServer *self,
                                      const gchar  *name);

/* ==========================================================================
 * Transport Configuration
 * ========================================================================== */

/**
 * lrg_mcp_server_get_transport_type:
 * @self: an #LrgMcpServer
 *
 * Gets the transport type configuration.
 *
 * Returns: the configured #LrgMcpTransportType
 */
LRG_AVAILABLE_IN_ALL
LrgMcpTransportType lrg_mcp_server_get_transport_type (LrgMcpServer *self);

/**
 * lrg_mcp_server_set_transport_type:
 * @self: an #LrgMcpServer
 * @type: the transport type to use
 *
 * Sets the transport type. Must be called before starting the server.
 *
 * - %LRG_MCP_TRANSPORT_STDIO: Use stdin/stdout (default, for Claude Code)
 * - %LRG_MCP_TRANSPORT_HTTP: Use HTTP POST + SSE (for network clients)
 * - %LRG_MCP_TRANSPORT_BOTH: Enable both transports simultaneously
 */
LRG_AVAILABLE_IN_ALL
void lrg_mcp_server_set_transport_type (LrgMcpServer        *self,
                                         LrgMcpTransportType  type);

/**
 * lrg_mcp_server_get_http_port:
 * @self: an #LrgMcpServer
 *
 * Gets the configured HTTP port.
 *
 * Returns: the configured port (0 means auto-assign)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_mcp_server_get_http_port (LrgMcpServer *self);

/**
 * lrg_mcp_server_set_http_port:
 * @self: an #LrgMcpServer
 * @port: the port to listen on (0 = auto-assign)
 *
 * Sets the HTTP server port. Must be called before starting the server.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mcp_server_set_http_port (LrgMcpServer *self,
                                    guint         port);

/**
 * lrg_mcp_server_get_http_host:
 * @self: an #LrgMcpServer
 *
 * Gets the HTTP host binding.
 *
 * Returns: (transfer none) (nullable): the host, or %NULL for all interfaces
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_mcp_server_get_http_host (LrgMcpServer *self);

/**
 * lrg_mcp_server_set_http_host:
 * @self: an #LrgMcpServer
 * @host: (nullable): the host/address to bind to (%NULL = all interfaces)
 *
 * Sets the HTTP host binding. Must be called before starting the server.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mcp_server_set_http_host (LrgMcpServer *self,
                                    const gchar  *host);

/**
 * lrg_mcp_server_set_http_auth:
 * @self: an #LrgMcpServer
 * @require_auth: whether to require authentication
 * @token: (nullable): the expected Bearer token
 *
 * Configures HTTP authentication. When enabled, clients must provide
 * the token in the Authorization header as "Bearer <token>".
 *
 * Must be called before starting the server.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mcp_server_set_http_auth (LrgMcpServer *self,
                                    gboolean      require_auth,
                                    const gchar  *token);

/**
 * lrg_mcp_server_get_actual_http_port:
 * @self: an #LrgMcpServer
 *
 * Gets the actual HTTP port the server is listening on.
 * This is useful when port 0 was specified for auto-assignment.
 *
 * Returns: the actual port, or 0 if HTTP transport is not running
 */
LRG_AVAILABLE_IN_ALL
guint lrg_mcp_server_get_actual_http_port (LrgMcpServer *self);

/* ==========================================================================
 * Lifecycle
 * ========================================================================== */

/**
 * lrg_mcp_server_start:
 * @self: an #LrgMcpServer
 * @error: (nullable): return location for a #GError
 *
 * Starts the MCP server.
 *
 * Once started, the server will accept MCP connections via stdio.
 * All registered tool and resource providers will be available to clients.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mcp_server_start (LrgMcpServer  *self,
                                GError       **error);

/**
 * lrg_mcp_server_stop:
 * @self: an #LrgMcpServer
 *
 * Stops the MCP server.
 *
 * Any connected clients will be disconnected. The server can
 * be restarted by calling lrg_mcp_server_start() again.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mcp_server_stop (LrgMcpServer *self);

/**
 * lrg_mcp_server_is_running:
 * @self: an #LrgMcpServer
 *
 * Checks if the server is currently running.
 *
 * Returns: %TRUE if running
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mcp_server_is_running (LrgMcpServer *self);

/* ==========================================================================
 * Provider Management
 * ========================================================================== */

/**
 * lrg_mcp_server_add_tool_provider:
 * @self: an #LrgMcpServer
 * @provider: a #LrgMcpToolProvider
 *
 * Adds a tool provider to the server.
 *
 * The provider's tools will be available to MCP clients.
 * Must be called before starting the server.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mcp_server_add_tool_provider (LrgMcpServer       *self,
                                        LrgMcpToolProvider *provider);

/**
 * lrg_mcp_server_remove_tool_provider:
 * @self: an #LrgMcpServer
 * @provider: a #LrgMcpToolProvider
 *
 * Removes a tool provider from the server.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mcp_server_remove_tool_provider (LrgMcpServer       *self,
                                           LrgMcpToolProvider *provider);

/**
 * lrg_mcp_server_add_resource_provider:
 * @self: an #LrgMcpServer
 * @provider: a #LrgMcpResourceProvider
 *
 * Adds a resource provider to the server.
 *
 * The provider's resources will be available to MCP clients.
 * Must be called before starting the server.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mcp_server_add_resource_provider (LrgMcpServer           *self,
                                            LrgMcpResourceProvider *provider);

/**
 * lrg_mcp_server_remove_resource_provider:
 * @self: an #LrgMcpServer
 * @provider: a #LrgMcpResourceProvider
 *
 * Removes a resource provider from the server.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mcp_server_remove_resource_provider (LrgMcpServer           *self,
                                               LrgMcpResourceProvider *provider);

/* ==========================================================================
 * Convenience: Register Default Providers
 * ========================================================================== */

/**
 * lrg_mcp_server_register_default_providers:
 * @self: an #LrgMcpServer
 *
 * Registers all built-in tool and resource providers:
 * - Input tools (keyboard, mouse, gamepad injection)
 * - Screenshot tools (capture, region)
 * - Engine tools (info, pause, resume)
 * - ECS tools (worlds, objects, transforms)
 * - Save tools (slots, save, load)
 * - Debug tools (log, profiler)
 * - Engine resources
 * - ECS resources
 * - Screenshot resources
 *
 * Call this after creating the server to enable all features.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mcp_server_register_default_providers (LrgMcpServer *self);

G_END_DECLS
