/* lrg-mcp-input-tools.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for input injection.
 */

#include "lrg-mcp-input-tools.h"
#include "../../input/lrg-input-software.h"
#include "../../input/lrg-input-manager.h"
#include "../../lrg-log.h"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>

#include <mcp.h>
#include <graylib.h>
#include <string.h>
#include <ctype.h>

/**
 * SECTION:lrg-mcp-input-tools
 * @title: LrgMcpInputTools
 * @short_description: MCP tools for input injection
 *
 * #LrgMcpInputTools provides MCP tools for injecting keyboard, mouse,
 * and gamepad input into the game via #LrgInputSoftware.
 *
 * ## Key Names
 *
 * Keys are specified by name (case-insensitive):
 * - Letters: "a" through "z"
 * - Numbers: "0" through "9"
 * - Function keys: "f1" through "f12"
 * - Special: "space", "enter", "escape", "tab", "backspace"
 * - Arrows: "up", "down", "left", "right"
 * - Modifiers: "left_shift", "right_shift", "left_control", etc.
 *
 * ## Mouse Buttons
 *
 * - "left" or "1"
 * - "right" or "2"
 * - "middle" or "3"
 *
 * ## Gamepad Buttons
 *
 * Standard Xbox-style names:
 * - Face: "a", "b", "x", "y"
 * - Shoulders: "lb", "rb"
 * - Triggers: "lt", "rt"
 * - Sticks: "ls", "rs" (pressed)
 * - D-pad: "dpad_up", "dpad_down", "dpad_left", "dpad_right"
 * - System: "start", "back", "guide"
 */

struct _LrgMcpInputTools
{
	LrgMcpToolGroup parent_instance;

	LrgInputSoftware *input;
	gboolean          registered_with_manager;
};

G_DEFINE_FINAL_TYPE (LrgMcpInputTools, lrg_mcp_input_tools, LRG_TYPE_MCP_TOOL_GROUP)

/* ==========================================================================
 * String to Enum Conversion
 * ========================================================================== */

/*
 * Parses a key name string to GrlKey enum value.
 * Returns GRL_KEY_NULL if not found.
 */
static GrlKey
parse_key_name (const gchar *name)
{
	GEnumClass *klass;
	GEnumValue *value;
	g_autofree gchar *nick = NULL;
	gsize i;

	if (name == NULL || *name == '\0')
	{
		return GRL_KEY_NULL;
	}

	/*
	 * Convert to lowercase and replace spaces/dashes with underscores
	 * to match GLib nick format
	 */
	nick = g_strdup (name);
	for (i = 0; nick[i] != '\0'; i++)
	{
		nick[i] = g_ascii_tolower (nick[i]);
		if (nick[i] == ' ' || nick[i] == '-')
		{
			nick[i] = '_';
		}
	}

	/* Try direct lookup by nick */
	klass = g_type_class_ref (GRL_TYPE_KEY);
	value = g_enum_get_value_by_nick (klass, nick);

	if (value == NULL)
	{
		/* Try with "key_" prefix for shorthand names */
		g_autofree gchar *prefixed = g_strdup_printf ("key_%s", nick);
		value = g_enum_get_value_by_nick (klass, prefixed);
	}

	g_type_class_unref (klass);

	return value != NULL ? value->value : GRL_KEY_NULL;
}

/*
 * Parses a mouse button name to GrlMouseButton enum.
 * Returns -1 if not found.
 */
static GrlMouseButton
parse_mouse_button_name (const gchar *name)
{
	if (name == NULL || *name == '\0')
	{
		return -1;
	}

	if (g_ascii_strcasecmp (name, "left") == 0 || strcmp (name, "1") == 0)
	{
		return GRL_MOUSE_BUTTON_LEFT;
	}
	if (g_ascii_strcasecmp (name, "right") == 0 || strcmp (name, "2") == 0)
	{
		return GRL_MOUSE_BUTTON_RIGHT;
	}
	if (g_ascii_strcasecmp (name, "middle") == 0 || strcmp (name, "3") == 0)
	{
		return GRL_MOUSE_BUTTON_MIDDLE;
	}

	return -1;
}

/*
 * Parses a gamepad button name to GrlGamepadButton enum.
 * Returns -1 if not found.
 */
static GrlGamepadButton
parse_gamepad_button_name (const gchar *name)
{
	if (name == NULL || *name == '\0')
	{
		return -1;
	}

	/* Xbox-style face buttons */
	if (g_ascii_strcasecmp (name, "a") == 0)
		return GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN;
	if (g_ascii_strcasecmp (name, "b") == 0)
		return GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;
	if (g_ascii_strcasecmp (name, "x") == 0)
		return GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT;
	if (g_ascii_strcasecmp (name, "y") == 0)
		return GRL_GAMEPAD_BUTTON_RIGHT_FACE_UP;

	/* Shoulder buttons */
	if (g_ascii_strcasecmp (name, "lb") == 0 ||
	    g_ascii_strcasecmp (name, "left_shoulder") == 0)
		return GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_1;
	if (g_ascii_strcasecmp (name, "rb") == 0 ||
	    g_ascii_strcasecmp (name, "right_shoulder") == 0)
		return GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_1;

	/* Triggers (as buttons) */
	if (g_ascii_strcasecmp (name, "lt") == 0 ||
	    g_ascii_strcasecmp (name, "left_trigger") == 0)
		return GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_2;
	if (g_ascii_strcasecmp (name, "rt") == 0 ||
	    g_ascii_strcasecmp (name, "right_trigger") == 0)
		return GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_2;

	/* Stick buttons */
	if (g_ascii_strcasecmp (name, "ls") == 0 ||
	    g_ascii_strcasecmp (name, "left_stick") == 0)
		return GRL_GAMEPAD_BUTTON_LEFT_THUMB;
	if (g_ascii_strcasecmp (name, "rs") == 0 ||
	    g_ascii_strcasecmp (name, "right_stick") == 0)
		return GRL_GAMEPAD_BUTTON_RIGHT_THUMB;

	/* D-pad */
	if (g_ascii_strcasecmp (name, "dpad_up") == 0)
		return GRL_GAMEPAD_BUTTON_LEFT_FACE_UP;
	if (g_ascii_strcasecmp (name, "dpad_down") == 0)
		return GRL_GAMEPAD_BUTTON_LEFT_FACE_DOWN;
	if (g_ascii_strcasecmp (name, "dpad_left") == 0)
		return GRL_GAMEPAD_BUTTON_LEFT_FACE_LEFT;
	if (g_ascii_strcasecmp (name, "dpad_right") == 0)
		return GRL_GAMEPAD_BUTTON_LEFT_FACE_RIGHT;

	/* System buttons */
	if (g_ascii_strcasecmp (name, "start") == 0)
		return GRL_GAMEPAD_BUTTON_MIDDLE_RIGHT;
	if (g_ascii_strcasecmp (name, "back") == 0 ||
	    g_ascii_strcasecmp (name, "select") == 0)
		return GRL_GAMEPAD_BUTTON_MIDDLE_LEFT;
	if (g_ascii_strcasecmp (name, "guide") == 0 ||
	    g_ascii_strcasecmp (name, "home") == 0)
		return GRL_GAMEPAD_BUTTON_MIDDLE;

	return -1;
}

/*
 * Parses a gamepad axis name to GrlGamepadAxis enum.
 * Returns -1 if not found.
 */
static GrlGamepadAxis
parse_gamepad_axis_name (const gchar *name)
{
	if (name == NULL || *name == '\0')
	{
		return -1;
	}

	if (g_ascii_strcasecmp (name, "left_x") == 0 ||
	    g_ascii_strcasecmp (name, "lx") == 0)
		return GRL_GAMEPAD_AXIS_LEFT_X;
	if (g_ascii_strcasecmp (name, "left_y") == 0 ||
	    g_ascii_strcasecmp (name, "ly") == 0)
		return GRL_GAMEPAD_AXIS_LEFT_Y;
	if (g_ascii_strcasecmp (name, "right_x") == 0 ||
	    g_ascii_strcasecmp (name, "rx") == 0)
		return GRL_GAMEPAD_AXIS_RIGHT_X;
	if (g_ascii_strcasecmp (name, "right_y") == 0 ||
	    g_ascii_strcasecmp (name, "ry") == 0)
		return GRL_GAMEPAD_AXIS_RIGHT_Y;
	if (g_ascii_strcasecmp (name, "left_trigger") == 0 ||
	    g_ascii_strcasecmp (name, "lt") == 0)
		return GRL_GAMEPAD_AXIS_LEFT_TRIGGER;
	if (g_ascii_strcasecmp (name, "right_trigger") == 0 ||
	    g_ascii_strcasecmp (name, "rt") == 0)
		return GRL_GAMEPAD_AXIS_RIGHT_TRIGGER;

	return -1;
}

/* ==========================================================================
 * Tool Handlers
 * ========================================================================== */

static McpToolResult *
handle_press_key (LrgMcpInputTools *self,
                  JsonObject       *arguments,
                  GError          **error)
{
	const gchar *key_name;
	GrlKey key;
	McpToolResult *result;

	key_name = lrg_mcp_tool_group_get_string_arg (arguments, "key", NULL);
	if (key_name == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: key");
		return NULL;
	}

	key = parse_key_name (key_name);
	if (key == GRL_KEY_NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Unknown key: %s", key_name);
		return NULL;
	}

	lrg_input_software_press_key (self->input, key);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, "Key pressed");
	return result;
}

static McpToolResult *
handle_release_key (LrgMcpInputTools *self,
                    JsonObject       *arguments,
                    GError          **error)
{
	const gchar *key_name;
	GrlKey key;
	McpToolResult *result;

	key_name = lrg_mcp_tool_group_get_string_arg (arguments, "key", NULL);
	if (key_name == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: key");
		return NULL;
	}

	key = parse_key_name (key_name);
	if (key == GRL_KEY_NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Unknown key: %s", key_name);
		return NULL;
	}

	lrg_input_software_release_key (self->input, key);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, "Key released");
	return result;
}

static McpToolResult *
handle_tap_key (LrgMcpInputTools *self,
                JsonObject       *arguments,
                GError          **error)
{
	const gchar *key_name;
	GrlKey key;
	McpToolResult *result;

	key_name = lrg_mcp_tool_group_get_string_arg (arguments, "key", NULL);
	if (key_name == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: key");
		return NULL;
	}

	key = parse_key_name (key_name);
	if (key == GRL_KEY_NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Unknown key: %s", key_name);
		return NULL;
	}

	lrg_input_software_tap_key (self->input, key);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, "Key tapped");
	return result;
}

static McpToolResult *
handle_press_mouse_button (LrgMcpInputTools *self,
                           JsonObject       *arguments,
                           GError          **error)
{
	const gchar *button_name;
	GrlMouseButton button;
	McpToolResult *result;

	button_name = lrg_mcp_tool_group_get_string_arg (arguments, "button", NULL);
	if (button_name == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: button");
		return NULL;
	}

	button = parse_mouse_button_name (button_name);
	if (button < 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Unknown mouse button: %s", button_name);
		return NULL;
	}

	lrg_input_software_press_mouse_button (self->input, button);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, "Mouse button pressed");
	return result;
}

static McpToolResult *
handle_release_mouse_button (LrgMcpInputTools *self,
                             JsonObject       *arguments,
                             GError          **error)
{
	const gchar *button_name;
	GrlMouseButton button;
	McpToolResult *result;

	button_name = lrg_mcp_tool_group_get_string_arg (arguments, "button", NULL);
	if (button_name == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: button");
		return NULL;
	}

	button = parse_mouse_button_name (button_name);
	if (button < 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Unknown mouse button: %s", button_name);
		return NULL;
	}

	lrg_input_software_release_mouse_button (self->input, button);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, "Mouse button released");
	return result;
}

static McpToolResult *
handle_move_mouse_to (LrgMcpInputTools *self,
                      JsonObject       *arguments,
                      GError          **error)
{
	gdouble x, y;
	McpToolResult *result;

	x = lrg_mcp_tool_group_get_double_arg (arguments, "x", 0.0);
	y = lrg_mcp_tool_group_get_double_arg (arguments, "y", 0.0);

	lrg_input_software_move_mouse_to (self->input, (gfloat)x, (gfloat)y);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, "Mouse moved");
	return result;
}

static McpToolResult *
handle_move_mouse_by (LrgMcpInputTools *self,
                      JsonObject       *arguments,
                      GError          **error)
{
	gdouble dx, dy;
	McpToolResult *result;

	dx = lrg_mcp_tool_group_get_double_arg (arguments, "dx", 0.0);
	dy = lrg_mcp_tool_group_get_double_arg (arguments, "dy", 0.0);

	lrg_input_software_move_mouse_by (self->input, (gfloat)dx, (gfloat)dy);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, "Mouse moved");
	return result;
}

static McpToolResult *
handle_press_gamepad_button (LrgMcpInputTools *self,
                             JsonObject       *arguments,
                             GError          **error)
{
	gint gamepad;
	const gchar *button_name;
	GrlGamepadButton button;
	McpToolResult *result;

	gamepad = (gint)lrg_mcp_tool_group_get_int_arg (arguments, "gamepad", 0);
	if (gamepad < 0 || gamepad > 3)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Gamepad index must be 0-3, got %d", gamepad);
		return NULL;
	}

	button_name = lrg_mcp_tool_group_get_string_arg (arguments, "button", NULL);
	if (button_name == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: button");
		return NULL;
	}

	button = parse_gamepad_button_name (button_name);
	if (button < 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Unknown gamepad button: %s", button_name);
		return NULL;
	}

	lrg_input_software_press_gamepad_button (self->input, gamepad, button);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, "Gamepad button pressed");
	return result;
}

static McpToolResult *
handle_release_gamepad_button (LrgMcpInputTools *self,
                               JsonObject       *arguments,
                               GError          **error)
{
	gint gamepad;
	const gchar *button_name;
	GrlGamepadButton button;
	McpToolResult *result;

	gamepad = (gint)lrg_mcp_tool_group_get_int_arg (arguments, "gamepad", 0);
	if (gamepad < 0 || gamepad > 3)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Gamepad index must be 0-3, got %d", gamepad);
		return NULL;
	}

	button_name = lrg_mcp_tool_group_get_string_arg (arguments, "button", NULL);
	if (button_name == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: button");
		return NULL;
	}

	button = parse_gamepad_button_name (button_name);
	if (button < 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Unknown gamepad button: %s", button_name);
		return NULL;
	}

	lrg_input_software_release_gamepad_button (self->input, gamepad, button);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, "Gamepad button released");
	return result;
}

static McpToolResult *
handle_set_gamepad_axis (LrgMcpInputTools *self,
                         JsonObject       *arguments,
                         GError          **error)
{
	gint gamepad;
	const gchar *axis_name;
	GrlGamepadAxis axis;
	gdouble value;
	McpToolResult *result;

	gamepad = (gint)lrg_mcp_tool_group_get_int_arg (arguments, "gamepad", 0);
	if (gamepad < 0 || gamepad > 3)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Gamepad index must be 0-3, got %d", gamepad);
		return NULL;
	}

	axis_name = lrg_mcp_tool_group_get_string_arg (arguments, "axis", NULL);
	if (axis_name == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: axis");
		return NULL;
	}

	axis = parse_gamepad_axis_name (axis_name);
	if (axis < 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Unknown gamepad axis: %s", axis_name);
		return NULL;
	}

	value = lrg_mcp_tool_group_get_double_arg (arguments, "value", 0.0);
	if (value < -1.0 || value > 1.0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Axis value must be -1.0 to 1.0, got %f", value);
		return NULL;
	}

	lrg_input_software_set_gamepad_axis (self->input, gamepad, axis, (gfloat)value);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, "Gamepad axis set");
	return result;
}

static McpToolResult *
handle_clear_all (LrgMcpInputTools *self,
                  JsonObject       *arguments,
                  GError          **error)
{
	McpToolResult *result;

	lrg_input_software_clear_all (self->input);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, "All inputs cleared");
	return result;
}

static McpToolResult *
handle_get_state (LrgMcpInputTools *self,
                  JsonObject       *arguments,
                  GError          **error)
{
	McpToolResult *result;

	/*
	 * TODO: Implement full state reporting
	 * For now, just return a success message
	 */
	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, "Input state: active");
	return result;
}

/* ==========================================================================
 * LrgMcpToolGroup Virtual Methods
 * ========================================================================== */

static const gchar *
lrg_mcp_input_tools_get_group_name (LrgMcpToolGroup *group)
{
	return "input";
}

/*
 * Helper to build a JSON schema for a tool with string parameter
 */
static JsonNode *
build_schema_string_param (const gchar *name,
                           const gchar *description,
                           gboolean     required)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, name);
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, description);
	json_builder_end_object (builder);
	json_builder_end_object (builder);
	if (required)
	{
		json_builder_set_member_name (builder, "required");
		json_builder_begin_array (builder);
		json_builder_add_string_value (builder, name);
		json_builder_end_array (builder);
	}
	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

/*
 * Helper to build a JSON schema for a tool with button parameter
 */
static JsonNode *
build_schema_button_param (const gchar *name,
                           const gchar *description)
{
	return build_schema_string_param (name, description, TRUE);
}

/*
 * Helper to build a JSON schema for mouse move tools (x, y coordinates)
 */
static JsonNode *
build_schema_xy_params (const gchar *x_name,
                        const gchar *y_name)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, x_name);
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "number");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "X coordinate");
	json_builder_end_object (builder);

	json_builder_set_member_name (builder, y_name);
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "number");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Y coordinate");
	json_builder_end_object (builder);

	json_builder_end_object (builder);
	json_builder_set_member_name (builder, "required");
	json_builder_begin_array (builder);
	json_builder_add_string_value (builder, x_name);
	json_builder_add_string_value (builder, y_name);
	json_builder_end_array (builder);
	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

/*
 * Helper to build a JSON schema for gamepad tools (gamepad index + button/axis)
 */
static JsonNode *
build_schema_gamepad_button_params (void)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "gamepad");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "integer");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Gamepad index (0-3)");
	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "button");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Button name (a, b, x, y, lb, rb, etc.)");
	json_builder_end_object (builder);

	json_builder_end_object (builder);
	json_builder_set_member_name (builder, "required");
	json_builder_begin_array (builder);
	json_builder_add_string_value (builder, "button");
	json_builder_end_array (builder);
	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

/*
 * Helper to build a JSON schema for gamepad axis tool
 */
static JsonNode *
build_schema_gamepad_axis_params (void)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "gamepad");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "integer");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Gamepad index (0-3)");
	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "axis");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Axis name (left_x, left_y, right_x, right_y, lt, rt)");
	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "value");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "number");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Axis value (-1.0 to 1.0)");
	json_builder_end_object (builder);

	json_builder_end_object (builder);
	json_builder_set_member_name (builder, "required");
	json_builder_begin_array (builder);
	json_builder_add_string_value (builder, "axis");
	json_builder_add_string_value (builder, "value");
	json_builder_end_array (builder);
	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

static void
lrg_mcp_input_tools_register_tools (LrgMcpToolGroup *group)
{
	McpTool *tool;
	JsonNode *schema;

	/* Keyboard tools */
	tool = mcp_tool_new ("lrg_input_press_key",
	                     "Press a keyboard key (stays down until released)");
	schema = build_schema_string_param ("key", "Key name (e.g., 'space', 'a', 'enter')", TRUE);
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_input_release_key",
	                     "Release a keyboard key");
	schema = build_schema_string_param ("key", "Key name to release", TRUE);
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_input_tap_key",
	                     "Press and release a key in one frame");
	schema = build_schema_string_param ("key", "Key name to tap", TRUE);
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	/* Mouse tools */
	tool = mcp_tool_new ("lrg_input_press_mouse_button",
	                     "Press a mouse button");
	schema = build_schema_button_param ("button", "Button name (left, right, middle)");
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_input_release_mouse_button",
	                     "Release a mouse button");
	schema = build_schema_button_param ("button", "Button name to release");
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_input_move_mouse_to",
	                     "Move mouse to absolute screen position");
	schema = build_schema_xy_params ("x", "y");
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_input_move_mouse_by",
	                     "Move mouse by relative delta");
	schema = build_schema_xy_params ("dx", "dy");
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	/* Gamepad tools */
	tool = mcp_tool_new ("lrg_input_press_gamepad_button",
	                     "Press a gamepad button");
	schema = build_schema_gamepad_button_params ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_input_release_gamepad_button",
	                     "Release a gamepad button");
	schema = build_schema_gamepad_button_params ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_input_set_gamepad_axis",
	                     "Set a gamepad axis value");
	schema = build_schema_gamepad_axis_params ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	/* Utility tools */
	tool = mcp_tool_new ("lrg_input_clear_all",
	                     "Release all held inputs");
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_input_get_state",
	                     "Get current input state summary");
	lrg_mcp_tool_group_add_tool (group, tool);
}

static McpToolResult *
lrg_mcp_input_tools_handle_tool (LrgMcpToolGroup  *group,
                                 const gchar      *name,
                                 JsonObject       *arguments,
                                 GError          **error)
{
	LrgMcpInputTools *self = LRG_MCP_INPUT_TOOLS (group);

	/* Keyboard */
	if (g_strcmp0 (name, "lrg_input_press_key") == 0)
		return handle_press_key (self, arguments, error);
	if (g_strcmp0 (name, "lrg_input_release_key") == 0)
		return handle_release_key (self, arguments, error);
	if (g_strcmp0 (name, "lrg_input_tap_key") == 0)
		return handle_tap_key (self, arguments, error);

	/* Mouse */
	if (g_strcmp0 (name, "lrg_input_press_mouse_button") == 0)
		return handle_press_mouse_button (self, arguments, error);
	if (g_strcmp0 (name, "lrg_input_release_mouse_button") == 0)
		return handle_release_mouse_button (self, arguments, error);
	if (g_strcmp0 (name, "lrg_input_move_mouse_to") == 0)
		return handle_move_mouse_to (self, arguments, error);
	if (g_strcmp0 (name, "lrg_input_move_mouse_by") == 0)
		return handle_move_mouse_by (self, arguments, error);

	/* Gamepad */
	if (g_strcmp0 (name, "lrg_input_press_gamepad_button") == 0)
		return handle_press_gamepad_button (self, arguments, error);
	if (g_strcmp0 (name, "lrg_input_release_gamepad_button") == 0)
		return handle_release_gamepad_button (self, arguments, error);
	if (g_strcmp0 (name, "lrg_input_set_gamepad_axis") == 0)
		return handle_set_gamepad_axis (self, arguments, error);

	/* Utility */
	if (g_strcmp0 (name, "lrg_input_clear_all") == 0)
		return handle_clear_all (self, arguments, error);
	if (g_strcmp0 (name, "lrg_input_get_state") == 0)
		return handle_get_state (self, arguments, error);

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "Unknown tool: %s", name);
	return NULL;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mcp_input_tools_constructed (GObject *object)
{
	LrgMcpInputTools *self = LRG_MCP_INPUT_TOOLS (object);
	LrgInputManager *manager;

	G_OBJECT_CLASS (lrg_mcp_input_tools_parent_class)->constructed (object);

	/* Create and register the software input source */
	self->input = lrg_input_software_new ();

	manager = lrg_input_manager_get_default ();
	if (manager != NULL)
	{
		/* Register with the input manager */
		lrg_input_manager_add_source (manager, LRG_INPUT (self->input));
		self->registered_with_manager = TRUE;

		lrg_info (LRG_LOG_DOMAIN_MCP, "MCP input tools registered with input manager");
	}
	else
	{
		lrg_warning (LRG_LOG_DOMAIN_MCP, "No input manager available for MCP input tools");
	}
}

static void
lrg_mcp_input_tools_finalize (GObject *object)
{
	LrgMcpInputTools *self = LRG_MCP_INPUT_TOOLS (object);

	if (self->registered_with_manager)
	{
		LrgInputManager *manager = lrg_input_manager_get_default ();
		if (manager != NULL)
		{
			lrg_input_manager_remove_source (manager, LRG_INPUT (self->input));
		}
	}

	g_clear_object (&self->input);

	G_OBJECT_CLASS (lrg_mcp_input_tools_parent_class)->finalize (object);
}

static void
lrg_mcp_input_tools_class_init (LrgMcpInputToolsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	LrgMcpToolGroupClass *group_class = LRG_MCP_TOOL_GROUP_CLASS (klass);

	object_class->constructed = lrg_mcp_input_tools_constructed;
	object_class->finalize = lrg_mcp_input_tools_finalize;

	group_class->get_group_name = lrg_mcp_input_tools_get_group_name;
	group_class->register_tools = lrg_mcp_input_tools_register_tools;
	group_class->handle_tool = lrg_mcp_input_tools_handle_tool;
}

static void
lrg_mcp_input_tools_init (LrgMcpInputTools *self)
{
	self->registered_with_manager = FALSE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_mcp_input_tools_new:
 *
 * Creates a new input tools provider.
 *
 * Returns: (transfer full): A new #LrgMcpInputTools
 */
LrgMcpInputTools *
lrg_mcp_input_tools_new (void)
{
	return g_object_new (LRG_TYPE_MCP_INPUT_TOOLS, NULL);
}

/**
 * lrg_mcp_input_tools_get_input_source:
 * @self: an #LrgMcpInputTools
 *
 * Gets the underlying #LrgInputSoftware instance.
 *
 * Returns: (transfer none): The input source
 */
LrgInputSoftware *
lrg_mcp_input_tools_get_input_source (LrgMcpInputTools *self)
{
	g_return_val_if_fail (LRG_IS_MCP_INPUT_TOOLS (self), NULL);

	return self->input;
}
