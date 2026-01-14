/* lrg-mcp-engine-tools.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for engine control.
 *
 * Provides tools for querying engine state and controlling
 * engine execution (pause, resume, step).
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

#define LRG_TYPE_MCP_ENGINE_TOOLS (lrg_mcp_engine_tools_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMcpEngineTools, lrg_mcp_engine_tools, LRG, MCP_ENGINE_TOOLS, LrgMcpToolGroup)

/**
 * lrg_mcp_engine_tools_new:
 *
 * Creates a new engine tools provider.
 *
 * ## Available Tools
 *
 * - `lrg_engine_get_info` - Get engine state (FPS, delta time, etc.)
 * - `lrg_engine_pause` - Pause engine updates
 * - `lrg_engine_resume` - Resume engine updates
 * - `lrg_engine_step_frame` - Advance one frame (when paused)
 *
 * Returns: (transfer full): A new #LrgMcpEngineTools
 */
LRG_AVAILABLE_IN_ALL
LrgMcpEngineTools * lrg_mcp_engine_tools_new (void);

G_END_DECLS
