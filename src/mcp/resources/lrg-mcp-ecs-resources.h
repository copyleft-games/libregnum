/* lrg-mcp-ecs-resources.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP resource group for ECS/World state.
 *
 * Provides read-only access to worlds, game objects, and
 * their state via MCP resources.
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

#define LRG_TYPE_MCP_ECS_RESOURCES (lrg_mcp_ecs_resources_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMcpEcsResources, lrg_mcp_ecs_resources, LRG, MCP_ECS_RESOURCES, LrgMcpResourceGroup)

/**
 * lrg_mcp_ecs_resources_new:
 *
 * Creates a new ECS resources provider.
 *
 * ## Available Resources
 *
 * - `libregnum://ecs/worlds` - List of active worlds
 * - `libregnum://ecs/world/{name}` - World state and object list
 * - `libregnum://ecs/object/{id}` - GameObject full state
 * - `libregnum://ecs/object/{id}/transform` - Transform data only
 *
 * Returns: (transfer full): A new #LrgMcpEcsResources
 */
LRG_AVAILABLE_IN_ALL
LrgMcpEcsResources * lrg_mcp_ecs_resources_new (void);

G_END_DECLS
