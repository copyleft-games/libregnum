/* lrg-mcp-screenshot-tools.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for screenshot capture.
 */

#include "lrg-mcp-screenshot-tools.h"
#include "../../lrg-log.h"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>

#include <mcp.h>
#include <graylib.h>

/**
 * SECTION:lrg-mcp-screenshot-tools
 * @title: LrgMcpScreenshotTools
 * @short_description: MCP tools for screenshot capture
 *
 * #LrgMcpScreenshotTools provides MCP tools for capturing screenshots
 * and returning them as base64-encoded PNG data for AI vision analysis.
 *
 * ## Scale Parameter
 *
 * Both tools accept an optional `scale` parameter (0.1 to 1.0) to
 * reduce image size for faster transmission and processing.
 */

struct _LrgMcpScreenshotTools
{
	LrgMcpToolGroup parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMcpScreenshotTools, lrg_mcp_screenshot_tools, LRG_TYPE_MCP_TOOL_GROUP)

/* ==========================================================================
 * Screenshot Capture
 * ========================================================================== */

/*
 * Captures a screenshot and returns it as base64-encoded PNG.
 * Returns NULL on error with error set.
 */
static gchar *
capture_screenshot_base64 (gfloat   scale,
                           GError **error)
{
	g_autoptr(GrlImage) image = NULL;
	g_autofree guint8 *png_data = NULL;
	gsize png_size = 0;
	gchar *base64;
	gint width, height;
	gint new_width, new_height;

	/* Capture the current screen */
	image = grl_image_new_from_screen ();
	if (image == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Failed to capture screen");
		return NULL;
	}

	/* Scale if requested (resize modifies in-place) */
	if (scale < 1.0f && scale > 0.0f)
	{
		width = grl_image_get_width (image);
		height = grl_image_get_height (image);
		new_width = (gint)(width * scale);
		new_height = (gint)(height * scale);

		grl_image_resize (image, new_width, new_height);
	}

	/* Export to PNG in memory */
	png_data = grl_image_export_to_memory (image, ".png", &png_size);
	if (png_data == NULL || png_size == 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Failed to export image to PNG");
		return NULL;
	}

	/* Encode as base64 */
	base64 = g_base64_encode (png_data, png_size);

	lrg_debug (LRG_LOG_DOMAIN_MCP, "Captured screenshot: %zu bytes PNG, %zu bytes base64",
	           png_size, strlen (base64));

	return base64;
}

/*
 * Captures a region of the screen and returns it as base64-encoded PNG.
 */
static gchar *
capture_region_base64 (gint     x,
                       gint     y,
                       gint     width,
                       gint     height,
                       gfloat   scale,
                       GError **error)
{
	g_autoptr(GrlImage) image = NULL;
	g_autoptr(GrlRectangle) rect = NULL;
	g_autofree guint8 *png_data = NULL;
	gsize png_size = 0;
	gchar *base64;
	gint new_width, new_height;

	/* Capture the current screen */
	image = grl_image_new_from_screen ();
	if (image == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Failed to capture screen");
		return NULL;
	}

	/* Crop to the specified region (modifies in-place) */
	rect = grl_rectangle_new (x, y, width, height);
	grl_image_crop (image, rect);

	/* Scale if requested (modifies in-place) */
	if (scale < 1.0f && scale > 0.0f)
	{
		new_width = (gint)(width * scale);
		new_height = (gint)(height * scale);

		grl_image_resize (image, new_width, new_height);
	}

	/* Export to PNG in memory */
	png_data = grl_image_export_to_memory (image, ".png", &png_size);
	if (png_data == NULL || png_size == 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Failed to export image to PNG");
		return NULL;
	}

	/* Encode as base64 */
	base64 = g_base64_encode (png_data, png_size);

	lrg_debug (LRG_LOG_DOMAIN_MCP, "Captured region %dx%d+%d+%d: %zu bytes base64",
	           width, height, x, y, strlen (base64));

	return base64;
}

/* ==========================================================================
 * Tool Handlers
 * ========================================================================== */

static McpToolResult *
handle_capture (LrgMcpScreenshotTools *self,
                JsonObject            *arguments,
                GError               **error)
{
	gdouble scale;
	g_autofree gchar *base64 = NULL;
	McpToolResult *result;

	scale = lrg_mcp_tool_group_get_double_arg (arguments, "scale", 1.0);
	if (scale <= 0.0 || scale > 1.0)
	{
		scale = 1.0;
	}

	base64 = capture_screenshot_base64 ((gfloat)scale, error);
	if (base64 == NULL)
	{
		return NULL;
	}

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_image (result, base64, "image/png");
	return result;
}

static McpToolResult *
handle_region (LrgMcpScreenshotTools *self,
               JsonObject            *arguments,
               GError               **error)
{
	gint64 x, y, width, height;
	gdouble scale;
	g_autofree gchar *base64 = NULL;
	McpToolResult *result;

	x = lrg_mcp_tool_group_get_int_arg (arguments, "x", 0);
	y = lrg_mcp_tool_group_get_int_arg (arguments, "y", 0);
	width = lrg_mcp_tool_group_get_int_arg (arguments, "width", 0);
	height = lrg_mcp_tool_group_get_int_arg (arguments, "height", 0);

	if (width <= 0 || height <= 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Invalid region dimensions: %ldx%ld",
		             (long)width, (long)height);
		return NULL;
	}

	scale = lrg_mcp_tool_group_get_double_arg (arguments, "scale", 1.0);
	if (scale <= 0.0 || scale > 1.0)
	{
		scale = 1.0;
	}

	base64 = capture_region_base64 ((gint)x, (gint)y, (gint)width, (gint)height,
	                                 (gfloat)scale, error);
	if (base64 == NULL)
	{
		return NULL;
	}

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_image (result, base64, "image/png");
	return result;
}

/* ==========================================================================
 * LrgMcpToolGroup Virtual Methods
 * ========================================================================== */

static const gchar *
lrg_mcp_screenshot_tools_get_group_name (LrgMcpToolGroup *group)
{
	return "screenshot";
}

/*
 * Helper to build JSON schema for screenshot capture tool
 */
static JsonNode *
build_schema_capture (void)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "scale");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "number");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Scale factor (0.1 to 1.0, default 1.0)");
	json_builder_end_object (builder);

	json_builder_end_object (builder);
	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

/*
 * Helper to build JSON schema for region capture tool
 */
static JsonNode *
build_schema_region (void)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "x");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "integer");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Region X coordinate");
	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "y");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "integer");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Region Y coordinate");
	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "width");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "integer");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Region width in pixels");
	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "height");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "integer");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Region height in pixels");
	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "scale");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "number");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Scale factor (0.1 to 1.0, default 1.0)");
	json_builder_end_object (builder);

	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "required");
	json_builder_begin_array (builder);
	json_builder_add_string_value (builder, "x");
	json_builder_add_string_value (builder, "y");
	json_builder_add_string_value (builder, "width");
	json_builder_add_string_value (builder, "height");
	json_builder_end_array (builder);

	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

static void
lrg_mcp_screenshot_tools_register_tools (LrgMcpToolGroup *group)
{
	McpTool *tool;
	JsonNode *schema;

	tool = mcp_tool_new ("lrg_screenshot_capture",
	                     "Capture full screen as base64-encoded PNG image");
	schema = build_schema_capture ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_screenshot_region",
	                     "Capture a region of the screen as base64-encoded PNG");
	schema = build_schema_region ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);
}

static McpToolResult *
lrg_mcp_screenshot_tools_handle_tool (LrgMcpToolGroup  *group,
                                      const gchar      *name,
                                      JsonObject       *arguments,
                                      GError          **error)
{
	LrgMcpScreenshotTools *self = LRG_MCP_SCREENSHOT_TOOLS (group);

	if (g_strcmp0 (name, "lrg_screenshot_capture") == 0)
		return handle_capture (self, arguments, error);
	if (g_strcmp0 (name, "lrg_screenshot_region") == 0)
		return handle_region (self, arguments, error);

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "Unknown tool: %s", name);
	return NULL;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mcp_screenshot_tools_class_init (LrgMcpScreenshotToolsClass *klass)
{
	LrgMcpToolGroupClass *group_class = LRG_MCP_TOOL_GROUP_CLASS (klass);

	group_class->get_group_name = lrg_mcp_screenshot_tools_get_group_name;
	group_class->register_tools = lrg_mcp_screenshot_tools_register_tools;
	group_class->handle_tool = lrg_mcp_screenshot_tools_handle_tool;
}

static void
lrg_mcp_screenshot_tools_init (LrgMcpScreenshotTools *self)
{
	/* Nothing to initialize */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_mcp_screenshot_tools_new:
 *
 * Creates a new screenshot tools provider.
 *
 * Returns: (transfer full): A new #LrgMcpScreenshotTools
 */
LrgMcpScreenshotTools *
lrg_mcp_screenshot_tools_new (void)
{
	return g_object_new (LRG_TYPE_MCP_SCREENSHOT_TOOLS, NULL);
}
