/* lrg-mcp-server.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP server singleton for libregnum.
 */

#include "lrg-mcp-server.h"
#include "../lrg-log.h"
#include "../lrg-version.h"

/* Tool groups */
#include "tools/lrg-mcp-input-tools.h"
#include "tools/lrg-mcp-screenshot-tools.h"
#include "tools/lrg-mcp-engine-tools.h"
#include "tools/lrg-mcp-ecs-tools.h"
#include "tools/lrg-mcp-save-tools.h"
#include "tools/lrg-mcp-debug-tools.h"

/* Resource groups */
#include "resources/lrg-mcp-engine-resources.h"
#include "resources/lrg-mcp-ecs-resources.h"
#include "resources/lrg-mcp-screenshot-resources.h"

#include <mcp.h>
#include <mcp-http-server-transport.h>

/**
 * SECTION:lrg-mcp-server
 * @title: LrgMcpServer
 * @short_description: MCP server singleton for AI-assisted debugging
 *
 * #LrgMcpServer is a singleton that manages the MCP server lifecycle.
 * It aggregates tools and resources from registered providers and
 * handles client connections via stdio transport.
 *
 * ## Quick Start
 *
 * |[<!-- language="C" -->
 * LrgMcpServer *mcp = lrg_mcp_server_get_default ();
 *
 * // Register all built-in tools and resources
 * lrg_mcp_server_register_default_providers (mcp);
 *
 * // Start the server
 * g_autoptr(GError) error = NULL;
 * if (!lrg_mcp_server_start (mcp, &error))
 * {
 *     g_warning ("Failed to start MCP: %s", error->message);
 * }
 *
 * // ... game loop ...
 *
 * // Stop on shutdown
 * lrg_mcp_server_stop (mcp);
 * ]|
 *
 * ## Transports
 *
 * Currently only stdio transport is supported, which is the default
 * for Claude Code integration.
 */

struct _LrgMcpServer
{
	GObject parent_instance;

	/* Configuration */
	gchar *server_name;
	LrgMcpTransportType transport_type;

	/* HTTP configuration */
	guint http_port;
	gchar *http_host;
	gboolean http_require_auth;
	gchar *http_auth_token;

	/* State */
	gboolean running;

	/* Stdio transport (when STDIO or BOTH) */
	McpServer *stdio_server;
	McpStdioTransport *stdio_transport;

	/* HTTP transport (when HTTP or BOTH) */
	McpServer *http_server;
	McpHttpServerTransport *http_transport;

	/* Registered providers */
	GPtrArray *tool_providers;     /* LrgMcpToolProvider* */
	GPtrArray *resource_providers; /* LrgMcpResourceProvider* */
};

G_DEFINE_FINAL_TYPE (LrgMcpServer, lrg_mcp_server, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_RUNNING,
	PROP_SERVER_NAME,
	PROP_TRANSPORT_TYPE,
	PROP_HTTP_PORT,
	PROP_HTTP_HOST,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_CLIENT_CONNECTED,
	SIGNAL_CLIENT_DISCONNECTED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Singleton instance */
static LrgMcpServer *default_server = NULL;

/* ==========================================================================
 * Tool Handler Callback
 *
 * This is a single handler that dispatches to the appropriate provider.
 * ========================================================================== */

static McpToolResult *
tool_handler_callback (McpServer   *server,
                       const gchar *name,
                       JsonObject  *arguments,
                       gpointer     user_data)
{
	LrgMcpServer *self = LRG_MCP_SERVER (user_data);
	McpToolResult *result;
	guint i;

	for (i = 0; i < self->tool_providers->len; i++)
	{
		LrgMcpToolProvider *provider;
		g_autoptr(GError) error = NULL;

		provider = g_ptr_array_index (self->tool_providers, i);

		if (lrg_mcp_tool_provider_has_tool (provider, name))
		{
			result = lrg_mcp_tool_provider_call_tool (provider, name, arguments, &error);

			if (result == NULL && error != NULL)
			{
				/* Return error as tool result */
				result = mcp_tool_result_new (TRUE);
				mcp_tool_result_add_text (result, error->message);
			}

			return result;
		}
	}

	/* Tool not found */
	result = mcp_tool_result_new (TRUE);
	mcp_tool_result_add_text (result, "Unknown tool");
	return result;
}

/* ==========================================================================
 * Resource Handler Callback
 * ========================================================================== */

static GList *
resource_handler_callback (McpServer   *server,
                           const gchar *uri,
                           gpointer     user_data)
{
	LrgMcpServer *self = LRG_MCP_SERVER (user_data);
	guint i;

	for (i = 0; i < self->resource_providers->len; i++)
	{
		LrgMcpResourceProvider *provider;
		g_autoptr(GError) error = NULL;
		GList *contents;

		provider = g_ptr_array_index (self->resource_providers, i);

		if (lrg_mcp_resource_provider_handles_uri (provider, uri))
		{
			contents = lrg_mcp_resource_provider_read_resource (provider, uri, &error);
			return contents;
		}
	}

	/* Resource not found */
	return NULL;
}

/* ==========================================================================
 * Server Initialization Callbacks
 * ========================================================================== */

static void
on_stdio_server_started (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
	LrgMcpServer *self = LRG_MCP_SERVER (user_data);
	g_autoptr(GError) error = NULL;

	if (!mcp_server_start_finish (MCP_SERVER (source), result, &error))
	{
		lrg_warning (LRG_LOG_DOMAIN_MCP, "Stdio MCP server start failed: %s", error->message);
		return;
	}

	lrg_info (LRG_LOG_DOMAIN_MCP, "Stdio MCP server initialization complete");
	g_signal_emit (self, signals[SIGNAL_CLIENT_CONNECTED], 0);
}

static void
on_http_server_started (GObject      *source,
                        GAsyncResult *result,
                        gpointer      user_data)
{
	LrgMcpServer *self = LRG_MCP_SERVER (user_data);
	g_autoptr(GError) error = NULL;
	guint actual_port;

	if (!mcp_server_start_finish (MCP_SERVER (source), result, &error))
	{
		lrg_warning (LRG_LOG_DOMAIN_MCP, "HTTP MCP server start failed: %s", error->message);
		return;
	}

	actual_port = mcp_http_server_transport_get_actual_port (self->http_transport);
	lrg_info (LRG_LOG_DOMAIN_MCP, "HTTP MCP server listening on port %u", actual_port);
}

/* ==========================================================================
 * Register Tools and Resources with McpServer
 * ========================================================================== */

static void
register_tools_with_mcp_server (LrgMcpServer *self,
                                McpServer    *mcp_server)
{
	guint i;

	for (i = 0; i < self->tool_providers->len; i++)
	{
		LrgMcpToolProvider *provider;
		GList *tools;
		GList *l;

		provider = g_ptr_array_index (self->tool_providers, i);
		tools = lrg_mcp_tool_provider_list_tools (provider);

		for (l = tools; l != NULL; l = l->next)
		{
			McpTool *tool = l->data;
			mcp_server_add_tool (mcp_server, tool, tool_handler_callback, self, NULL);
		}

		g_list_free_full (tools, g_object_unref);
	}
}

static void
register_resources_with_mcp_server (LrgMcpServer *self,
                                    McpServer    *mcp_server)
{
	guint i;

	for (i = 0; i < self->resource_providers->len; i++)
	{
		LrgMcpResourceProvider *provider;
		GList *resources;
		GList *l;

		provider = g_ptr_array_index (self->resource_providers, i);
		resources = lrg_mcp_resource_provider_list_resources (provider);

		for (l = resources; l != NULL; l = l->next)
		{
			McpResource *resource = l->data;
			mcp_server_add_resource (mcp_server, resource, resource_handler_callback, self, NULL);
		}

		g_list_free_full (resources, g_object_unref);
	}
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mcp_server_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgMcpServer *self = LRG_MCP_SERVER (object);

	switch (prop_id)
	{
	case PROP_RUNNING:
		g_value_set_boolean (value, self->running);
		break;
	case PROP_SERVER_NAME:
		g_value_set_string (value, self->server_name);
		break;
	case PROP_TRANSPORT_TYPE:
		g_value_set_enum (value, self->transport_type);
		break;
	case PROP_HTTP_PORT:
		g_value_set_uint (value, self->http_port);
		break;
	case PROP_HTTP_HOST:
		g_value_set_string (value, self->http_host);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_mcp_server_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgMcpServer *self = LRG_MCP_SERVER (object);

	switch (prop_id)
	{
	case PROP_SERVER_NAME:
		g_free (self->server_name);
		self->server_name = g_value_dup_string (value);
		break;
	case PROP_TRANSPORT_TYPE:
		self->transport_type = g_value_get_enum (value);
		break;
	case PROP_HTTP_PORT:
		self->http_port = g_value_get_uint (value);
		break;
	case PROP_HTTP_HOST:
		g_free (self->http_host);
		self->http_host = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_mcp_server_finalize (GObject *object)
{
	LrgMcpServer *self = LRG_MCP_SERVER (object);

	lrg_mcp_server_stop (self);

	g_clear_pointer (&self->server_name, g_free);
	g_clear_pointer (&self->http_host, g_free);
	g_clear_pointer (&self->http_auth_token, g_free);
	g_clear_pointer (&self->tool_providers, g_ptr_array_unref);
	g_clear_pointer (&self->resource_providers, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_mcp_server_parent_class)->finalize (object);
}

static void
lrg_mcp_server_class_init (LrgMcpServerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = lrg_mcp_server_get_property;
	object_class->set_property = lrg_mcp_server_set_property;
	object_class->finalize = lrg_mcp_server_finalize;

	/**
	 * LrgMcpServer:running:
	 *
	 * Whether the server is currently running.
	 */
	properties[PROP_RUNNING] =
		g_param_spec_boolean ("running",
		                      "Running",
		                      "Whether the server is running",
		                      FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgMcpServer:server-name:
	 *
	 * The server name reported to clients.
	 */
	properties[PROP_SERVER_NAME] =
		g_param_spec_string ("server-name",
		                     "Server Name",
		                     "The server name reported to clients",
		                     "libregnum",
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgMcpServer:transport-type:
	 *
	 * The transport type to use.
	 */
	properties[PROP_TRANSPORT_TYPE] =
		g_param_spec_enum ("transport-type",
		                   "Transport Type",
		                   "Which transport(s) to use",
		                   LRG_TYPE_MCP_TRANSPORT_TYPE,
		                   LRG_MCP_TRANSPORT_STDIO,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgMcpServer:http-port:
	 *
	 * The HTTP server port (0 = auto-assign).
	 */
	properties[PROP_HTTP_PORT] =
		g_param_spec_uint ("http-port",
		                   "HTTP Port",
		                   "HTTP server port (0 = auto-assign)",
		                   0, G_MAXUINT16, 8080,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgMcpServer:http-host:
	 *
	 * The HTTP host/address to bind to.
	 */
	properties[PROP_HTTP_HOST] =
		g_param_spec_string ("http-host",
		                     "HTTP Host",
		                     "HTTP host/address to bind to (NULL = all interfaces)",
		                     NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgMcpServer::client-connected:
	 * @self: the #LrgMcpServer
	 *
	 * Emitted when a client connects to the server.
	 */
	signals[SIGNAL_CLIENT_CONNECTED] =
		g_signal_new ("client-connected",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	/**
	 * LrgMcpServer::client-disconnected:
	 * @self: the #LrgMcpServer
	 *
	 * Emitted when a client disconnects from the server.
	 */
	signals[SIGNAL_CLIENT_DISCONNECTED] =
		g_signal_new ("client-disconnected",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);
}

static void
lrg_mcp_server_init (LrgMcpServer *self)
{
	self->server_name = g_strdup ("libregnum");
	self->transport_type = LRG_MCP_TRANSPORT_STDIO;
	self->http_port = 8080;
	self->http_host = NULL;
	self->http_require_auth = FALSE;
	self->http_auth_token = NULL;
	self->running = FALSE;

	self->tool_providers = g_ptr_array_new_with_free_func (g_object_unref);
	self->resource_providers = g_ptr_array_new_with_free_func (g_object_unref);
}

/* ==========================================================================
 * Public API - Singleton
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
LrgMcpServer *
lrg_mcp_server_get_default (void)
{
	if (default_server == NULL)
	{
		default_server = g_object_new (LRG_TYPE_MCP_SERVER, NULL);
	}

	return default_server;
}

/* ==========================================================================
 * Public API - Configuration
 * ========================================================================== */

/**
 * lrg_mcp_server_get_server_name:
 * @self: an #LrgMcpServer
 *
 * Gets the server name reported to MCP clients.
 *
 * Returns: (transfer none): the server name
 */
const gchar *
lrg_mcp_server_get_server_name (LrgMcpServer *self)
{
	g_return_val_if_fail (LRG_IS_MCP_SERVER (self), NULL);

	return self->server_name;
}

/**
 * lrg_mcp_server_set_server_name:
 * @self: an #LrgMcpServer
 * @name: the server name
 *
 * Sets the server name. Must be called before starting the server.
 */
void
lrg_mcp_server_set_server_name (LrgMcpServer *self,
                                const gchar  *name)
{
	g_return_if_fail (LRG_IS_MCP_SERVER (self));
	g_return_if_fail (!self->running);

	g_free (self->server_name);
	self->server_name = g_strdup (name);
}

/* ==========================================================================
 * Public API - Transport Configuration
 * ========================================================================== */

/**
 * lrg_mcp_server_get_transport_type:
 * @self: an #LrgMcpServer
 *
 * Gets the transport type configuration.
 *
 * Returns: the configured #LrgMcpTransportType
 */
LrgMcpTransportType
lrg_mcp_server_get_transport_type (LrgMcpServer *self)
{
	g_return_val_if_fail (LRG_IS_MCP_SERVER (self), LRG_MCP_TRANSPORT_STDIO);

	return self->transport_type;
}

/**
 * lrg_mcp_server_set_transport_type:
 * @self: an #LrgMcpServer
 * @type: the transport type to use
 *
 * Sets the transport type. Must be called before starting the server.
 */
void
lrg_mcp_server_set_transport_type (LrgMcpServer        *self,
                                   LrgMcpTransportType  type)
{
	g_return_if_fail (LRG_IS_MCP_SERVER (self));
	g_return_if_fail (!self->running);

	self->transport_type = type;
}

/**
 * lrg_mcp_server_get_http_port:
 * @self: an #LrgMcpServer
 *
 * Gets the configured HTTP port.
 *
 * Returns: the configured port (0 means auto-assign)
 */
guint
lrg_mcp_server_get_http_port (LrgMcpServer *self)
{
	g_return_val_if_fail (LRG_IS_MCP_SERVER (self), 0);

	return self->http_port;
}

/**
 * lrg_mcp_server_set_http_port:
 * @self: an #LrgMcpServer
 * @port: the port to listen on (0 = auto-assign)
 *
 * Sets the HTTP server port. Must be called before starting the server.
 */
void
lrg_mcp_server_set_http_port (LrgMcpServer *self,
                              guint         port)
{
	g_return_if_fail (LRG_IS_MCP_SERVER (self));
	g_return_if_fail (!self->running);

	self->http_port = port;
}

/**
 * lrg_mcp_server_get_http_host:
 * @self: an #LrgMcpServer
 *
 * Gets the HTTP host binding.
 *
 * Returns: (transfer none) (nullable): the host, or %NULL for all interfaces
 */
const gchar *
lrg_mcp_server_get_http_host (LrgMcpServer *self)
{
	g_return_val_if_fail (LRG_IS_MCP_SERVER (self), NULL);

	return self->http_host;
}

/**
 * lrg_mcp_server_set_http_host:
 * @self: an #LrgMcpServer
 * @host: (nullable): the host/address to bind to (%NULL = all interfaces)
 *
 * Sets the HTTP host binding. Must be called before starting the server.
 */
void
lrg_mcp_server_set_http_host (LrgMcpServer *self,
                              const gchar  *host)
{
	g_return_if_fail (LRG_IS_MCP_SERVER (self));
	g_return_if_fail (!self->running);

	g_free (self->http_host);
	self->http_host = g_strdup (host);
}

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
void
lrg_mcp_server_set_http_auth (LrgMcpServer *self,
                              gboolean      require_auth,
                              const gchar  *token)
{
	g_return_if_fail (LRG_IS_MCP_SERVER (self));
	g_return_if_fail (!self->running);

	self->http_require_auth = require_auth;
	g_free (self->http_auth_token);
	self->http_auth_token = g_strdup (token);
}

/**
 * lrg_mcp_server_get_actual_http_port:
 * @self: an #LrgMcpServer
 *
 * Gets the actual HTTP port the server is listening on.
 * This is useful when port 0 was specified for auto-assignment.
 *
 * Returns: the actual port, or 0 if HTTP transport is not running
 */
guint
lrg_mcp_server_get_actual_http_port (LrgMcpServer *self)
{
	g_return_val_if_fail (LRG_IS_MCP_SERVER (self), 0);

	if (self->http_transport == NULL)
	{
		return 0;
	}

	return mcp_http_server_transport_get_actual_port (self->http_transport);
}

/* ==========================================================================
 * Public API - Lifecycle
 * ========================================================================== */

/**
 * lrg_mcp_server_start:
 * @self: an #LrgMcpServer
 * @error: (nullable): return location for a #GError
 *
 * Starts the MCP server.
 *
 * The transport(s) used depend on the transport-type configuration.
 * All registered tool and resource providers will be available to clients.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_mcp_server_start (LrgMcpServer  *self,
                      GError       **error)
{
	const gchar *instructions;

	g_return_val_if_fail (LRG_IS_MCP_SERVER (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (self->running)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
		             "MCP server is already running");
		return FALSE;
	}

	lrg_info (LRG_LOG_DOMAIN_MCP, "Starting MCP server (transport: %s)...",
	          self->transport_type == LRG_MCP_TRANSPORT_STDIO ? "stdio" :
	          self->transport_type == LRG_MCP_TRANSPORT_HTTP ? "http" : "both");

	instructions = "Libregnum Game Engine MCP Server.\n"
		"Provides tools for input injection, screenshots, engine control, "
		"ECS manipulation, save/load, and debugging.\n"
		"Resources provide read-only access to game state.";

	/* Start stdio transport if configured */
	if (self->transport_type == LRG_MCP_TRANSPORT_STDIO ||
	    self->transport_type == LRG_MCP_TRANSPORT_BOTH)
	{
		self->stdio_server = mcp_server_new (self->server_name, LRG_VERSION_STRING);
		mcp_server_set_instructions (self->stdio_server, instructions);

		register_tools_with_mcp_server (self, self->stdio_server);
		register_resources_with_mcp_server (self, self->stdio_server);

		self->stdio_transport = mcp_stdio_transport_new ();
		mcp_server_set_transport (self->stdio_server, MCP_TRANSPORT (self->stdio_transport));

		mcp_server_start_async (self->stdio_server, NULL, on_stdio_server_started, self);
		lrg_info (LRG_LOG_DOMAIN_MCP, "Stdio transport initialized");
	}

	/* Start HTTP transport if configured */
	if (self->transport_type == LRG_MCP_TRANSPORT_HTTP ||
	    self->transport_type == LRG_MCP_TRANSPORT_BOTH)
	{
		self->http_server = mcp_server_new (self->server_name, LRG_VERSION_STRING);
		mcp_server_set_instructions (self->http_server, instructions);

		register_tools_with_mcp_server (self, self->http_server);
		register_resources_with_mcp_server (self, self->http_server);

		self->http_transport = mcp_http_server_transport_new_full (
			self->http_host, self->http_port);

		if (self->http_require_auth && self->http_auth_token != NULL)
		{
			mcp_http_server_transport_set_require_auth (self->http_transport, TRUE);
			mcp_http_server_transport_set_auth_token (self->http_transport,
			                                           self->http_auth_token);
		}

		mcp_server_set_transport (self->http_server, MCP_TRANSPORT (self->http_transport));

		mcp_server_start_async (self->http_server, NULL, on_http_server_started, self);
		lrg_info (LRG_LOG_DOMAIN_MCP, "HTTP transport initialized (port: %u)", self->http_port);
	}

	self->running = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RUNNING]);

	lrg_info (LRG_LOG_DOMAIN_MCP, "MCP server started with %u tool providers and %u resource providers",
	          self->tool_providers->len, self->resource_providers->len);

	return TRUE;
}

/**
 * lrg_mcp_server_stop:
 * @self: an #LrgMcpServer
 *
 * Stops the MCP server.
 *
 * Any connected clients will be disconnected. The server can
 * be restarted by calling lrg_mcp_server_start() again.
 */
void
lrg_mcp_server_stop (LrgMcpServer *self)
{
	g_return_if_fail (LRG_IS_MCP_SERVER (self));

	if (!self->running)
	{
		return;
	}

	lrg_info (LRG_LOG_DOMAIN_MCP, "Stopping MCP server");

	/* Stop stdio server */
	if (self->stdio_server != NULL)
	{
		mcp_server_stop (self->stdio_server);
		g_clear_object (&self->stdio_transport);
		g_clear_object (&self->stdio_server);
	}

	/* Stop HTTP server */
	if (self->http_server != NULL)
	{
		mcp_server_stop (self->http_server);
		g_clear_object (&self->http_transport);
		g_clear_object (&self->http_server);
	}

	self->running = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RUNNING]);

	lrg_info (LRG_LOG_DOMAIN_MCP, "MCP server stopped");
}

/**
 * lrg_mcp_server_is_running:
 * @self: an #LrgMcpServer
 *
 * Checks if the server is currently running.
 *
 * Returns: %TRUE if running
 */
gboolean
lrg_mcp_server_is_running (LrgMcpServer *self)
{
	g_return_val_if_fail (LRG_IS_MCP_SERVER (self), FALSE);

	return self->running;
}

/* ==========================================================================
 * Public API - Provider Management
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
void
lrg_mcp_server_add_tool_provider (LrgMcpServer       *self,
                                  LrgMcpToolProvider *provider)
{
	g_return_if_fail (LRG_IS_MCP_SERVER (self));
	g_return_if_fail (LRG_IS_MCP_TOOL_PROVIDER (provider));

	g_ptr_array_add (self->tool_providers, g_object_ref (provider));

	lrg_debug (LRG_LOG_DOMAIN_MCP, "Added tool provider (%u total)",
	           self->tool_providers->len);
}

/**
 * lrg_mcp_server_remove_tool_provider:
 * @self: an #LrgMcpServer
 * @provider: a #LrgMcpToolProvider
 *
 * Removes a tool provider from the server.
 */
void
lrg_mcp_server_remove_tool_provider (LrgMcpServer       *self,
                                     LrgMcpToolProvider *provider)
{
	g_return_if_fail (LRG_IS_MCP_SERVER (self));
	g_return_if_fail (LRG_IS_MCP_TOOL_PROVIDER (provider));

	g_ptr_array_remove (self->tool_providers, provider);

	lrg_debug (LRG_LOG_DOMAIN_MCP, "Removed tool provider (%u remaining)",
	           self->tool_providers->len);
}

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
void
lrg_mcp_server_add_resource_provider (LrgMcpServer           *self,
                                      LrgMcpResourceProvider *provider)
{
	g_return_if_fail (LRG_IS_MCP_SERVER (self));
	g_return_if_fail (LRG_IS_MCP_RESOURCE_PROVIDER (provider));

	g_ptr_array_add (self->resource_providers, g_object_ref (provider));

	lrg_debug (LRG_LOG_DOMAIN_MCP, "Added resource provider (%u total)",
	           self->resource_providers->len);
}

/**
 * lrg_mcp_server_remove_resource_provider:
 * @self: an #LrgMcpServer
 * @provider: a #LrgMcpResourceProvider
 *
 * Removes a resource provider from the server.
 */
void
lrg_mcp_server_remove_resource_provider (LrgMcpServer           *self,
                                         LrgMcpResourceProvider *provider)
{
	g_return_if_fail (LRG_IS_MCP_SERVER (self));
	g_return_if_fail (LRG_IS_MCP_RESOURCE_PROVIDER (provider));

	g_ptr_array_remove (self->resource_providers, provider);

	lrg_debug (LRG_LOG_DOMAIN_MCP, "Removed resource provider (%u remaining)",
	           self->resource_providers->len);
}

/* ==========================================================================
 * Public API - Default Providers
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
void
lrg_mcp_server_register_default_providers (LrgMcpServer *self)
{
	g_return_if_fail (LRG_IS_MCP_SERVER (self));

	lrg_info (LRG_LOG_DOMAIN_MCP, "Registering default MCP providers");

	/* Register tool providers */
	lrg_mcp_server_add_tool_provider (self, LRG_MCP_TOOL_PROVIDER (lrg_mcp_input_tools_new ()));
	lrg_mcp_server_add_tool_provider (self, LRG_MCP_TOOL_PROVIDER (lrg_mcp_screenshot_tools_new ()));
	lrg_mcp_server_add_tool_provider (self, LRG_MCP_TOOL_PROVIDER (lrg_mcp_engine_tools_new ()));
	lrg_mcp_server_add_tool_provider (self, LRG_MCP_TOOL_PROVIDER (lrg_mcp_ecs_tools_new ()));
	lrg_mcp_server_add_tool_provider (self, LRG_MCP_TOOL_PROVIDER (lrg_mcp_save_tools_new ()));
	lrg_mcp_server_add_tool_provider (self, LRG_MCP_TOOL_PROVIDER (lrg_mcp_debug_tools_new ()));

	/* Register resource providers */
	lrg_mcp_server_add_resource_provider (self, LRG_MCP_RESOURCE_PROVIDER (lrg_mcp_engine_resources_new ()));
	lrg_mcp_server_add_resource_provider (self, LRG_MCP_RESOURCE_PROVIDER (lrg_mcp_ecs_resources_new ()));
	lrg_mcp_server_add_resource_provider (self, LRG_MCP_RESOURCE_PROVIDER (lrg_mcp_screenshot_resources_new ()));

	lrg_info (LRG_LOG_DOMAIN_MCP, "Registered 6 tool providers and 3 resource providers");
}
