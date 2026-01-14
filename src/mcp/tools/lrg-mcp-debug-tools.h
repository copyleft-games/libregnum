/* lrg-mcp-debug-tools.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for debugging operations.
 *
 * Provides tools for logging, profiling, and debug information.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../lrg-mcp-tool-group.h"

G_BEGIN_DECLS

#define LRG_TYPE_MCP_DEBUG_TOOLS (lrg_mcp_debug_tools_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMcpDebugTools, lrg_mcp_debug_tools, LRG, MCP_DEBUG_TOOLS, LrgMcpToolGroup)

/**
 * lrg_mcp_debug_tools_new:
 *
 * Creates a new debug tools provider.
 *
 * ## Available Tools
 *
 * - `lrg_debug_log` - Log a message to the debug console
 * - `lrg_debug_get_fps` - Get detailed FPS statistics
 * - `lrg_debug_profiler_start` - Start a profiler section
 * - `lrg_debug_profiler_stop` - Stop a profiler section
 * - `lrg_debug_profiler_report` - Get profiler report
 *
 * Returns: (transfer full): A new #LrgMcpDebugTools
 */
LRG_AVAILABLE_IN_ALL
LrgMcpDebugTools * lrg_mcp_debug_tools_new (void);

G_END_DECLS
