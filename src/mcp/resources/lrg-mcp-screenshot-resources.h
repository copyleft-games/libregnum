/* lrg-mcp-screenshot-resources.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP resource group for screenshot access.
 *
 * Provides read-only access to screenshots via MCP resources.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../lrg-mcp-resource-group.h"

G_BEGIN_DECLS

#define LRG_TYPE_MCP_SCREENSHOT_RESOURCES (lrg_mcp_screenshot_resources_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMcpScreenshotResources, lrg_mcp_screenshot_resources, LRG, MCP_SCREENSHOT_RESOURCES, LrgMcpResourceGroup)

/**
 * lrg_mcp_screenshot_resources_new:
 *
 * Creates a new screenshot resources provider.
 *
 * ## Available Resources
 *
 * - `libregnum://screenshot/current` - Current frame as PNG (base64 blob)
 * - `libregnum://screenshot/thumbnail` - Scaled-down screenshot (256px max)
 *
 * Returns: (transfer full): A new #LrgMcpScreenshotResources
 */
LRG_AVAILABLE_IN_ALL
LrgMcpScreenshotResources * lrg_mcp_screenshot_resources_new (void);

G_END_DECLS
