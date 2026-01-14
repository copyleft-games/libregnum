/* lrg-mcp-screenshot-tools.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for screenshot capture.
 *
 * Provides tools for capturing screenshots and returning them
 * as base64-encoded PNG images.
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

#define LRG_TYPE_MCP_SCREENSHOT_TOOLS (lrg_mcp_screenshot_tools_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMcpScreenshotTools, lrg_mcp_screenshot_tools, LRG, MCP_SCREENSHOT_TOOLS, LrgMcpToolGroup)

/**
 * lrg_mcp_screenshot_tools_new:
 *
 * Creates a new screenshot tools provider.
 *
 * ## Available Tools
 *
 * - `lrg_screenshot_capture` - Capture full screen as base64 PNG
 * - `lrg_screenshot_region` - Capture a specific region as base64 PNG
 *
 * Returns: (transfer full): A new #LrgMcpScreenshotTools
 */
LRG_AVAILABLE_IN_ALL
LrgMcpScreenshotTools * lrg_mcp_screenshot_tools_new (void);

G_END_DECLS
