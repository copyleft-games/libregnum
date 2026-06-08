/* lrg-mcp-reel-tools.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for reel (video composition) operations.
 */

#include "lrg-mcp-reel-tools.h"
#include "../../lrg-log.h"
#include "../../reel/lrg-reel.h"
#include "../../reel/lrg-reel-loader.h"
#include "../../reel/lrg-reel-renderer.h"
#include "../../reel/lrg-reel-exporter.h"
#include "../../reel/lrg-reel-video-exporter.h"
#include "../../lrg-enums.h"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>

/**
 * SECTION:lrg-mcp-reel-tools
 * @title: LrgMcpReelTools
 * @short_description: MCP tools for reel video-composition operations
 *
 * #LrgMcpReelTools provides MCP tools for loading reel metadata, rendering
 * reels to video files, and capturing still frames from reels.
 *
 * All tools accept a path to a YAML reel description and operate headlessly;
 * no display or GL context is required.
 */

struct _LrgMcpReelTools
{
	LrgMcpToolGroup parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMcpReelTools, lrg_mcp_reel_tools, LRG_TYPE_MCP_TOOL_GROUP)

/* ==========================================================================
 * Internal helpers
 * ========================================================================== */

/*
 * codec_from_string:
 * @str: (nullable): codec name string, e.g. "h264", "vp9", "h265".
 *
 * Maps a codec name to the corresponding #LrgReelVideoCodec.
 * Returns %LRG_REEL_VIDEO_CODEC_H264 for NULL or unrecognised values.
 */
static LrgReelVideoCodec
codec_from_string (const gchar *str)
{
	if (str == NULL || g_strcmp0 (str, "h264") == 0)
		return LRG_REEL_VIDEO_CODEC_H264;
	if (g_strcmp0 (str, "vp9") == 0)
		return LRG_REEL_VIDEO_CODEC_VP9;
	if (g_strcmp0 (str, "h265") == 0)
		return LRG_REEL_VIDEO_CODEC_H265;
	if (g_strcmp0 (str, "prores") == 0)
		return LRG_REEL_VIDEO_CODEC_PRORES;
	if (g_strcmp0 (str, "prores_alpha") == 0)
		return LRG_REEL_VIDEO_CODEC_PRORES_ALPHA;
	if (g_strcmp0 (str, "vp9_alpha") == 0)
		return LRG_REEL_VIDEO_CODEC_VP9_ALPHA;

	lrg_debug (LRG_LOG_DOMAIN_MCP,
	           "Unknown codec '%s', falling back to h264", str);
	return LRG_REEL_VIDEO_CODEC_H264;
}

/*
 * build_json_string:
 * @builder: a #JsonBuilder positioned inside an object or array.
 *
 * Serialises the current builder state to a newly-allocated JSON string.
 * The caller owns the returned string.
 */
static gchar *
build_json_string (JsonBuilder *builder)
{
	g_autoptr(JsonGenerator) generator = NULL;
	g_autoptr(JsonNode) root = NULL;

	root = json_builder_get_root (builder);
	generator = json_generator_new ();
	json_generator_set_root (generator, root);
	json_generator_set_pretty (generator, TRUE);
	return json_generator_to_data (generator, NULL);
}

/* ==========================================================================
 * Tool Handlers
 * ========================================================================== */

static McpToolResult *
handle_get_metadata (LrgMcpReelTools *self,
                     JsonObject      *arguments,
                     GError         **error)
{
	const gchar *path;
	g_autoptr(LrgReel) reel = NULL;
	g_autoptr(GError) load_error = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autofree gchar *json_str = NULL;
	McpToolResult *result;
	gint width;
	gint height;
	gint duration_in_frames;
	gdouble fps;
	const gchar *id;

	path = lrg_mcp_tool_group_get_string_arg (arguments, "path", NULL);
	if (path == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: path");
		return NULL;
	}

	reel = lrg_reel_load_yaml (path, NULL, &load_error);
	if (reel == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Failed to load reel '%s': %s",
		             path, load_error->message);
		return NULL;
	}

	id                = lrg_reel_get_id (reel);
	width             = lrg_reel_get_width (reel);
	height            = lrg_reel_get_height (reel);
	fps               = lrg_reel_get_fps (reel);
	duration_in_frames = lrg_reel_get_duration_in_frames (reel);

	builder = json_builder_new ();
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "id");
	json_builder_add_string_value (builder, id != NULL ? id : "");

	json_builder_set_member_name (builder, "width");
	json_builder_add_int_value (builder, (gint64) width);

	json_builder_set_member_name (builder, "height");
	json_builder_add_int_value (builder, (gint64) height);

	json_builder_set_member_name (builder, "fps");
	json_builder_add_double_value (builder, fps);

	json_builder_set_member_name (builder, "duration_in_frames");
	json_builder_add_int_value (builder, (gint64) duration_in_frames);

	json_builder_end_object (builder);

	json_str = build_json_string (builder);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, json_str);
	return result;
}

static McpToolResult *
handle_render (LrgMcpReelTools *self,
               JsonObject      *arguments,
               GError         **error)
{
	const gchar *path;
	const gchar *output;
	const gchar *codec_str;
	g_autoptr(LrgReel) reel = NULL;
	g_autoptr(GError) op_error = NULL;
	g_autoptr(LrgReelRenderer) renderer = NULL;
	g_autoptr(LrgReelVideoExporter) video_exporter = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autofree gchar *json_str = NULL;
	McpToolResult *result;
	LrgReelVideoCodec codec;
	gint duration_in_frames;

	path = lrg_mcp_tool_group_get_string_arg (arguments, "path", NULL);
	if (path == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: path");
		return NULL;
	}

	output = lrg_mcp_tool_group_get_string_arg (arguments, "output", NULL);
	if (output == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: output");
		return NULL;
	}

	codec_str = lrg_mcp_tool_group_get_string_arg (arguments, "codec", NULL);
	codec     = codec_from_string (codec_str);

	reel = lrg_reel_load_yaml (path, NULL, &op_error);
	if (reel == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Failed to load reel '%s': %s",
		             path, op_error->message);
		return NULL;
	}

	duration_in_frames = lrg_reel_get_duration_in_frames (reel);

	renderer       = lrg_reel_renderer_new (reel);
	video_exporter = lrg_reel_video_exporter_new (output, codec);

	if (!lrg_reel_renderer_render_to_exporter (renderer,
	                                           LRG_REEL_EXPORTER (video_exporter),
	                                           &op_error))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Render failed for '%s': %s",
		             path, op_error->message);
		return NULL;
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "output");
	json_builder_add_string_value (builder, output);

	json_builder_set_member_name (builder, "frames");
	json_builder_add_int_value (builder, (gint64) duration_in_frames);

	json_builder_end_object (builder);

	json_str = build_json_string (builder);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, json_str);
	return result;
}

static McpToolResult *
handle_capture_still (LrgMcpReelTools *self,
                      JsonObject      *arguments,
                      GError         **error)
{
	const gchar *path;
	const gchar *output;
	g_autoptr(LrgReel) reel = NULL;
	g_autoptr(GError) op_error = NULL;
	g_autoptr(LrgReelRenderer) renderer = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autofree gchar *json_str = NULL;
	McpToolResult *result;
	gint64 frame_arg;
	gint frame;
	gint duration_in_frames;

	path = lrg_mcp_tool_group_get_string_arg (arguments, "path", NULL);
	if (path == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: path");
		return NULL;
	}

	output = lrg_mcp_tool_group_get_string_arg (arguments, "output", NULL);
	if (output == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: output");
		return NULL;
	}

	reel = lrg_reel_load_yaml (path, NULL, &op_error);
	if (reel == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Failed to load reel '%s': %s",
		             path, op_error->message);
		return NULL;
	}

	duration_in_frames = lrg_reel_get_duration_in_frames (reel);
	frame_arg          = lrg_mcp_tool_group_get_int_arg (arguments, "frame", 0);

	/* Clamp frame to valid range */
	if (frame_arg < 0)
		frame = 0;
	else if (frame_arg >= (gint64) duration_in_frames)
		frame = duration_in_frames - 1;
	else
		frame = (gint) frame_arg;

	renderer = lrg_reel_renderer_new (reel);

	if (!lrg_reel_renderer_render_still (renderer, frame, output, &op_error))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Still capture failed for '%s' frame %d: %s",
		             path, frame, op_error->message);
		return NULL;
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "output");
	json_builder_add_string_value (builder, output);

	json_builder_set_member_name (builder, "frame");
	json_builder_add_int_value (builder, (gint64) frame);

	json_builder_end_object (builder);

	json_str = build_json_string (builder);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, json_str);
	return result;
}

/* ==========================================================================
 * Schema Builders
 * ========================================================================== */

static JsonNode *
build_schema_metadata (void)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "path");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Path to the YAML reel file");
	json_builder_end_object (builder);

	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "required");
	json_builder_begin_array (builder);
	json_builder_add_string_value (builder, "path");
	json_builder_end_array (builder);

	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

static JsonNode *
build_schema_render (void)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "path");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Path to the YAML reel file");
	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "output");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Output video file path (e.g. /tmp/out.mp4)");
	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "codec");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder,
	    "Video codec: \"h264\" (default), \"vp9\", \"h265\", "
	    "\"prores\", \"prores_alpha\", \"vp9_alpha\"");
	json_builder_end_object (builder);

	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "required");
	json_builder_begin_array (builder);
	json_builder_add_string_value (builder, "path");
	json_builder_add_string_value (builder, "output");
	json_builder_end_array (builder);

	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

static JsonNode *
build_schema_capture_still (void)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "path");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Path to the YAML reel file");
	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "output");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Output image file path (e.g. /tmp/frame.png)");
	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "frame");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "integer");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder,
	    "Frame index to capture (0-based, defaults to 0; "
	    "clamped to valid range)");
	json_builder_end_object (builder);

	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "required");
	json_builder_begin_array (builder);
	json_builder_add_string_value (builder, "path");
	json_builder_add_string_value (builder, "output");
	json_builder_end_array (builder);

	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

/* ==========================================================================
 * LrgMcpToolGroup Virtual Methods
 * ========================================================================== */

static const gchar *
lrg_mcp_reel_tools_get_group_name (LrgMcpToolGroup *group)
{
	return "reel";
}

static void
lrg_mcp_reel_tools_register_tools (LrgMcpToolGroup *group)
{
	McpTool *tool;
	JsonNode *schema;

	tool = mcp_tool_new ("reel_get_metadata",
	                     "Load a YAML reel file and return its metadata "
	                     "(id, width, height, fps, duration_in_frames)");
	schema = build_schema_metadata ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("reel_render",
	                     "Render a YAML reel to a video file via ffmpeg; "
	                     "returns the output path and total frame count");
	schema = build_schema_render ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("reel_capture_still",
	                     "Render a single frame of a YAML reel to an image "
	                     "file (PNG); returns the output path and frame index");
	schema = build_schema_capture_still ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);
}

static McpToolResult *
lrg_mcp_reel_tools_handle_tool (LrgMcpToolGroup  *group,
                                 const gchar      *name,
                                 JsonObject       *arguments,
                                 GError          **error)
{
	LrgMcpReelTools *self = LRG_MCP_REEL_TOOLS (group);

	if (g_strcmp0 (name, "reel_get_metadata") == 0)
		return handle_get_metadata (self, arguments, error);
	if (g_strcmp0 (name, "reel_render") == 0)
		return handle_render (self, arguments, error);
	if (g_strcmp0 (name, "reel_capture_still") == 0)
		return handle_capture_still (self, arguments, error);

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "Unknown tool: %s", name);
	return NULL;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mcp_reel_tools_class_init (LrgMcpReelToolsClass *klass)
{
	LrgMcpToolGroupClass *group_class = LRG_MCP_TOOL_GROUP_CLASS (klass);

	group_class->get_group_name = lrg_mcp_reel_tools_get_group_name;
	group_class->register_tools = lrg_mcp_reel_tools_register_tools;
	group_class->handle_tool    = lrg_mcp_reel_tools_handle_tool;
}

static void
lrg_mcp_reel_tools_init (LrgMcpReelTools *self)
{
	/* Nothing to initialize */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_mcp_reel_tools_new:
 *
 * Creates a new reel tools provider.
 *
 * Returns: (transfer full): A new #LrgMcpReelTools
 */
LrgMcpReelTools *
lrg_mcp_reel_tools_new (void)
{
	return g_object_new (LRG_TYPE_MCP_REEL_TOOLS, NULL);
}
