/* lrg-mcp-ecs-tools.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for ECS/World manipulation.
 *
 * Provides tools for querying and manipulating the Entity-Component-System,
 * including worlds, game objects, components, and transforms.
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

#define LRG_TYPE_MCP_ECS_TOOLS (lrg_mcp_ecs_tools_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMcpEcsTools, lrg_mcp_ecs_tools, LRG, MCP_ECS_TOOLS, LrgMcpToolGroup)

/**
 * lrg_mcp_ecs_tools_new:
 *
 * Creates a new ECS tools provider.
 *
 * ## Available Tools
 *
 * **World Tools:**
 * - `lrg_ecs_list_worlds` - List all active worlds
 *
 * **GameObject Tools:**
 * - `lrg_ecs_list_game_objects` - List GameObjects in a world
 * - `lrg_ecs_get_game_object` - Get GameObject details
 * - `lrg_ecs_spawn_object` - Spawn a registered object type
 * - `lrg_ecs_destroy_object` - Destroy a GameObject
 *
 * **Component Tools:**
 * - `lrg_ecs_get_component` - Get component data
 * - `lrg_ecs_set_component_property` - Set a component property
 *
 * **Transform Tools:**
 * - `lrg_ecs_get_transform` - Get transform (position, rotation, scale)
 * - `lrg_ecs_set_transform` - Set transform values
 *
 * Returns: (transfer full): A new #LrgMcpEcsTools
 */
LRG_AVAILABLE_IN_ALL
LrgMcpEcsTools * lrg_mcp_ecs_tools_new (void);

G_END_DECLS
