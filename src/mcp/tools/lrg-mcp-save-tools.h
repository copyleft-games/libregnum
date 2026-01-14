/* lrg-mcp-save-tools.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for save/load operations.
 *
 * Provides tools for listing save slots, saving, loading,
 * and managing game saves.
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

#define LRG_TYPE_MCP_SAVE_TOOLS (lrg_mcp_save_tools_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMcpSaveTools, lrg_mcp_save_tools, LRG, MCP_SAVE_TOOLS, LrgMcpToolGroup)

/**
 * lrg_mcp_save_tools_new:
 *
 * Creates a new save tools provider.
 *
 * ## Available Tools
 *
 * - `lrg_save_list_slots` - List available save slots
 * - `lrg_save_get_info` - Get save slot metadata
 * - `lrg_save_create` - Create a save in a slot
 * - `lrg_save_load` - Load from a save slot
 * - `lrg_save_delete` - Delete a save slot
 * - `lrg_save_quick_save` - Trigger quick save
 * - `lrg_save_quick_load` - Trigger quick load
 *
 * Returns: (transfer full): A new #LrgMcpSaveTools
 */
LRG_AVAILABLE_IN_ALL
LrgMcpSaveTools * lrg_mcp_save_tools_new (void);

G_END_DECLS
