/* lrg-mcp-reel-tools.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for reel (video composition) operations.
 *
 * Provides tools for loading reel metadata, rendering a reel to a video
 * file, and capturing a single still frame from a reel.
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

#define LRG_TYPE_MCP_REEL_TOOLS (lrg_mcp_reel_tools_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMcpReelTools, lrg_mcp_reel_tools, LRG, MCP_REEL_TOOLS, LrgMcpToolGroup)

/**
 * lrg_mcp_reel_tools_new:
 *
 * Creates a new reel tools provider.
 *
 * ## Available Tools
 *
 * - `reel_get_metadata` - Load a YAML reel and return its metadata
 * - `reel_render` - Render a YAML reel to a video file
 * - `reel_capture_still` - Render a single frame of a YAML reel to an image
 *
 * Returns: (transfer full): A new #LrgMcpReelTools
 */
LRG_AVAILABLE_IN_ALL
LrgMcpReelTools * lrg_mcp_reel_tools_new (void);

G_END_DECLS
