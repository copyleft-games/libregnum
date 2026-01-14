/* lrg-mcp-engine-resources.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP resource group for engine state.
 *
 * Provides read-only access to engine state, configuration,
 * and type registry via MCP resources.
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

#define LRG_TYPE_MCP_ENGINE_RESOURCES (lrg_mcp_engine_resources_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMcpEngineResources, lrg_mcp_engine_resources, LRG, MCP_ENGINE_RESOURCES, LrgMcpResourceGroup)

/**
 * lrg_mcp_engine_resources_new:
 *
 * Creates a new engine resources provider.
 *
 * ## Available Resources
 *
 * - `libregnum://engine/info` - Engine state (FPS, delta time, running/paused)
 * - `libregnum://engine/config` - Current engine configuration
 * - `libregnum://engine/registry` - Registered type names
 *
 * Returns: (transfer full): A new #LrgMcpEngineResources
 */
LRG_AVAILABLE_IN_ALL
LrgMcpEngineResources * lrg_mcp_engine_resources_new (void);

G_END_DECLS
