/* lrg-mcp-enums.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * GType implementations for MCP enumerations.
 */

#include "config.h"
#include "lrg-mcp-enums.h"

/* ==========================================================================
 * Transport Type
 * ========================================================================== */

static const GEnumValue lrg_mcp_transport_type_values[] =
{
	{ LRG_MCP_TRANSPORT_STDIO, "LRG_MCP_TRANSPORT_STDIO", "stdio" },
	{ LRG_MCP_TRANSPORT_HTTP, "LRG_MCP_TRANSPORT_HTTP", "http" },
	{ LRG_MCP_TRANSPORT_BOTH, "LRG_MCP_TRANSPORT_BOTH", "both" },
	{ 0, NULL, NULL }
};

GType
lrg_mcp_transport_type_get_type (void)
{
	static volatile gsize g_define_type_id__volatile = 0;

	if (g_once_init_enter (&g_define_type_id__volatile))
	{
		GType g_define_type_id = g_enum_register_static ("LrgMcpTransportType",
		                                                  lrg_mcp_transport_type_values);
		g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
	}

	return g_define_type_id__volatile;
}

/* ==========================================================================
 * Error Domain
 * ========================================================================== */

G_DEFINE_QUARK (lrg-mcp-error-quark, lrg_mcp_error)

static const GEnumValue lrg_mcp_error_values[] =
{
	{ LRG_MCP_ERROR_FAILED, "LRG_MCP_ERROR_FAILED", "failed" },
	{ LRG_MCP_ERROR_TRANSPORT, "LRG_MCP_ERROR_TRANSPORT", "transport" },
	{ LRG_MCP_ERROR_TOOL, "LRG_MCP_ERROR_TOOL", "tool" },
	{ LRG_MCP_ERROR_RESOURCE, "LRG_MCP_ERROR_RESOURCE", "resource" },
	{ 0, NULL, NULL }
};

GType
lrg_mcp_error_get_type (void)
{
	static volatile gsize g_define_type_id__volatile = 0;

	if (g_once_init_enter (&g_define_type_id__volatile))
	{
		GType g_define_type_id = g_enum_register_static ("LrgMcpError",
		                                                  lrg_mcp_error_values);
		g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
	}

	return g_define_type_id__volatile;
}
