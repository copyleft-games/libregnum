/* lrg-mcp-input-tools.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for input injection.
 *
 * Provides tools for injecting keyboard, mouse, and gamepad input
 * via the LrgInputSoftware subsystem for AI-assisted gameplay.
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

#define LRG_TYPE_MCP_INPUT_TOOLS (lrg_mcp_input_tools_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMcpInputTools, lrg_mcp_input_tools, LRG, MCP_INPUT_TOOLS, LrgMcpToolGroup)

/**
 * lrg_mcp_input_tools_new:
 *
 * Creates a new input tools provider.
 *
 * The provider creates and manages an #LrgInputSoftware instance
 * for injecting keyboard, mouse, and gamepad input.
 *
 * ## Available Tools
 *
 * **Keyboard:**
 * - `lrg_input_press_key` - Press a key (stays down until released)
 * - `lrg_input_release_key` - Release a key
 * - `lrg_input_tap_key` - Press and release a key in one frame
 *
 * **Mouse:**
 * - `lrg_input_press_mouse_button` - Press a mouse button
 * - `lrg_input_release_mouse_button` - Release a mouse button
 * - `lrg_input_move_mouse_to` - Move mouse to absolute position
 * - `lrg_input_move_mouse_by` - Move mouse by relative delta
 *
 * **Gamepad:**
 * - `lrg_input_press_gamepad_button` - Press a gamepad button
 * - `lrg_input_release_gamepad_button` - Release a gamepad button
 * - `lrg_input_set_gamepad_axis` - Set gamepad axis value
 *
 * **Utility:**
 * - `lrg_input_clear_all` - Release all held inputs
 * - `lrg_input_get_state` - Get current input state summary
 *
 * Returns: (transfer full): A new #LrgMcpInputTools
 */
LRG_AVAILABLE_IN_ALL
LrgMcpInputTools * lrg_mcp_input_tools_new (void);

/**
 * lrg_mcp_input_tools_get_input_source:
 * @self: an #LrgMcpInputTools
 *
 * Gets the underlying #LrgInputSoftware instance.
 *
 * Returns: (transfer none): The input source
 */
LRG_AVAILABLE_IN_ALL
LrgInputSoftware * lrg_mcp_input_tools_get_input_source (LrgMcpInputTools *self);

G_END_DECLS
