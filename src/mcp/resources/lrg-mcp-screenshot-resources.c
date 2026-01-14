/* lrg-mcp-screenshot-resources.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP resource group for screenshot access.
 */

#include "lrg-mcp-screenshot-resources.h"
#include "../../lrg-log.h"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>
#include <graylib.h>

/**
 * SECTION:lrg-mcp-screenshot-resources
 * @title: LrgMcpScreenshotResources
 * @short_description: MCP resources for screenshots
 *
 * #LrgMcpScreenshotResources provides MCP resources for read-only
 * access to screenshots as base64-encoded PNG blobs.
 */

#define URI_PREFIX "libregnum://screenshot/"
#define THUMBNAIL_MAX_SIZE 256

struct _LrgMcpScreenshotResources
{
	LrgMcpResourceGroup parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMcpScreenshotResources, lrg_mcp_screenshot_resources, LRG_TYPE_MCP_RESOURCE_GROUP)

/* ==========================================================================
 * Screenshot Capture
 * ========================================================================== */

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

	return base64;
}

static gchar *
capture_thumbnail_base64 (GError **error)
{
	g_autoptr(GrlImage) image = NULL;
	g_autofree guint8 *png_data = NULL;
	gsize png_size = 0;
	gchar *base64;
	gint width, height;
	gfloat scale;
	gint new_width, new_height;

	/* Capture the current screen */
	image = grl_image_new_from_screen ();
	if (image == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Failed to capture screen");
		return NULL;
	}

	/* Calculate scale to fit within THUMBNAIL_MAX_SIZE */
	width = grl_image_get_width (image);
	height = grl_image_get_height (image);

	if (width > height)
	{
		scale = (gfloat)THUMBNAIL_MAX_SIZE / width;
	}
	else
	{
		scale = (gfloat)THUMBNAIL_MAX_SIZE / height;
	}

	/* Only scale down, never up (resize modifies in-place) */
	if (scale < 1.0f)
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

	return base64;
}

/* ==========================================================================
 * Resource Handlers
 * ========================================================================== */

static GList *
read_current_screenshot (LrgMcpScreenshotResources  *self,
                         GError                    **error)
{
	g_autofree gchar *base64 = NULL;
	McpResourceContents *contents;

	base64 = capture_screenshot_base64 (1.0f, error);
	if (base64 == NULL)
	{
		return NULL;
	}

	contents = mcp_resource_contents_new_blob (URI_PREFIX "current", base64, "image/png");
	return g_list_append (NULL, contents);
}

static GList *
read_thumbnail (LrgMcpScreenshotResources  *self,
                GError                    **error)
{
	g_autofree gchar *base64 = NULL;
	McpResourceContents *contents;

	base64 = capture_thumbnail_base64 (error);
	if (base64 == NULL)
	{
		return NULL;
	}

	contents = mcp_resource_contents_new_blob (URI_PREFIX "thumbnail", base64, "image/png");
	return g_list_append (NULL, contents);
}

/* ==========================================================================
 * LrgMcpResourceGroup Virtual Methods
 * ========================================================================== */

static const gchar *
lrg_mcp_screenshot_resources_get_group_name (LrgMcpResourceGroup *group)
{
	return "screenshot";
}

static void
lrg_mcp_screenshot_resources_register_resources (LrgMcpResourceGroup *group)
{
	McpResource *resource;

	lrg_mcp_resource_group_set_uri_prefix (group, URI_PREFIX);

	resource = mcp_resource_new (URI_PREFIX "current", "Current frame screenshot (PNG)");
	mcp_resource_set_mime_type (resource, "image/png");
	lrg_mcp_resource_group_add_resource (group, resource);

	resource = mcp_resource_new (URI_PREFIX "thumbnail", "Scaled-down screenshot (256px max)");
	mcp_resource_set_mime_type (resource, "image/png");
	lrg_mcp_resource_group_add_resource (group, resource);
}

static GList *
lrg_mcp_screenshot_resources_read_resource (LrgMcpResourceGroup  *group,
                                            const gchar          *uri,
                                            GError              **error)
{
	LrgMcpScreenshotResources *self = LRG_MCP_SCREENSHOT_RESOURCES (group);

	if (g_strcmp0 (uri, URI_PREFIX "current") == 0)
		return read_current_screenshot (self, error);
	if (g_strcmp0 (uri, URI_PREFIX "thumbnail") == 0)
		return read_thumbnail (self, error);

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
	             "Unknown resource: %s", uri);
	return NULL;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mcp_screenshot_resources_class_init (LrgMcpScreenshotResourcesClass *klass)
{
	LrgMcpResourceGroupClass *group_class = LRG_MCP_RESOURCE_GROUP_CLASS (klass);

	group_class->get_group_name = lrg_mcp_screenshot_resources_get_group_name;
	group_class->register_resources = lrg_mcp_screenshot_resources_register_resources;
	group_class->read_resource = lrg_mcp_screenshot_resources_read_resource;
}

static void
lrg_mcp_screenshot_resources_init (LrgMcpScreenshotResources *self)
{
	/* Nothing to initialize */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_mcp_screenshot_resources_new:
 *
 * Creates a new screenshot resources provider.
 *
 * Returns: (transfer full): A new #LrgMcpScreenshotResources
 */
LrgMcpScreenshotResources *
lrg_mcp_screenshot_resources_new (void)
{
	return g_object_new (LRG_TYPE_MCP_SCREENSHOT_RESOURCES, NULL);
}
