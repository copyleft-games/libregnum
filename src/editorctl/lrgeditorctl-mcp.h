/* lrgeditorctl-mcp.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP (Model Context Protocol) tools for the libregnum editor, built on
 * mcp-glib. The same tool set is served two ways:
 *
 *   lrgeditorctl mcp     bridges to a running editor over its socket
 *                        (any session; reconnects if the editor restarts)
 *   lrgeditor --mcp      serves from inside the editor process
 *
 * Tools forward to the editor's IPC methods through an #LrgCtlCallFunc,
 * so both transports behave identically.
 */

#pragma once

#include <glib.h>
#include <mcp.h>
#include "lrgeditorctl-client.h"

G_BEGIN_DECLS

#define LRG_CTL_MCP_SERVER_NAME "libregnum-editor"

McpServer *   lrg_ctl_mcp_server_new        (LrgCtlCallFunc  call,
                                             gpointer        call_data);
guint         lrg_ctl_mcp_n_tools           (void);
const gchar * lrg_ctl_mcp_tool_name         (guint           index);
const gchar * lrg_ctl_mcp_tool_description  (guint           index);

G_END_DECLS
