/* lrg-mcp-enums.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Enumerations for MCP module.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Transport Type
 * ========================================================================== */

/**
 * LrgMcpTransportType:
 * @LRG_MCP_TRANSPORT_STDIO: Stdio-based transport (for Claude Code)
 * @LRG_MCP_TRANSPORT_HTTP: HTTP-based transport (for network clients)
 * @LRG_MCP_TRANSPORT_BOTH: Enable both stdio and HTTP transports
 *
 * Specifies which transport(s) the MCP server should use.
 */
typedef enum
{
	LRG_MCP_TRANSPORT_STDIO,
	LRG_MCP_TRANSPORT_HTTP,
	LRG_MCP_TRANSPORT_BOTH
} LrgMcpTransportType;

LRG_AVAILABLE_IN_ALL
GType lrg_mcp_transport_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MCP_TRANSPORT_TYPE (lrg_mcp_transport_type_get_type ())

/* ==========================================================================
 * Error Domain
 * ========================================================================== */

/**
 * LRG_MCP_ERROR:
 *
 * Error domain for MCP errors.
 */
#define LRG_MCP_ERROR (lrg_mcp_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_mcp_error_quark (void);

/**
 * LrgMcpError:
 * @LRG_MCP_ERROR_FAILED: Generic failure
 * @LRG_MCP_ERROR_TRANSPORT: Transport error
 * @LRG_MCP_ERROR_TOOL: Tool invocation error
 * @LRG_MCP_ERROR_RESOURCE: Resource access error
 *
 * Error codes for the MCP module.
 */
typedef enum
{
	LRG_MCP_ERROR_FAILED,
	LRG_MCP_ERROR_TRANSPORT,
	LRG_MCP_ERROR_TOOL,
	LRG_MCP_ERROR_RESOURCE
} LrgMcpError;

LRG_AVAILABLE_IN_ALL
GType lrg_mcp_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MCP_ERROR (lrg_mcp_error_get_type ())

G_END_DECLS
