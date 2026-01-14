/* lrg-mcp.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP module master include header.
 *
 * Include this header to get all MCP-related types and functions.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>

G_BEGIN_DECLS

/* ==========================================================================
 * MCP Enumerations
 * ========================================================================== */

#include "lrg-mcp-enums.h"

/* ==========================================================================
 * MCP Interfaces
 * ========================================================================== */

#include "lrg-mcp-tool-provider.h"
#include "lrg-mcp-resource-provider.h"

/* ==========================================================================
 * MCP Base Classes
 * ========================================================================== */

#include "lrg-mcp-tool-group.h"
#include "lrg-mcp-resource-group.h"

/* ==========================================================================
 * MCP Server
 * ========================================================================== */

#include "lrg-mcp-server.h"

/* ==========================================================================
 * MCP Tool Groups
 * ========================================================================== */

#include "tools/lrg-mcp-input-tools.h"
#include "tools/lrg-mcp-screenshot-tools.h"
#include "tools/lrg-mcp-engine-tools.h"
#include "tools/lrg-mcp-ecs-tools.h"
#include "tools/lrg-mcp-save-tools.h"
#include "tools/lrg-mcp-debug-tools.h"

/* ==========================================================================
 * MCP Resource Groups
 * ========================================================================== */

#include "resources/lrg-mcp-engine-resources.h"
#include "resources/lrg-mcp-ecs-resources.h"
#include "resources/lrg-mcp-screenshot-resources.h"

G_END_DECLS
