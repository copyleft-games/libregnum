/* Internal server wiring. SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "lrg-mcp-server.h"

/* Shared by the server adapter and its in-memory registration test. */
static inline void
_lrg_mcp_server_register_ecs_templates (McpServer *server,
                                        McpResourceHandler handler,
                                        gpointer user_data)
{
    g_autoptr(McpResourceTemplate) world = NULL;
    g_autoptr(McpResourceTemplate) object = NULL;

    world = mcp_resource_template_new ("libregnum://ecs/world/{name}", "Registered world");
    object = mcp_resource_template_new ("libregnum://ecs/object/{id}", "Game object");
    mcp_resource_template_set_mime_type (world, "application/json");
    mcp_resource_template_set_mime_type (object, "application/json");
    mcp_server_add_resource_template (server, world, handler, user_data, NULL);
    mcp_server_add_resource_template (server, object, handler, user_data, NULL);
}
