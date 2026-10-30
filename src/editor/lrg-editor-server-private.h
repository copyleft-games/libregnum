/* lrg-editor-server-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal hooks between the IPC server and the files that add built-in
 * method groups to it.
 */

#pragma once

#include "lrg-editor-server.h"

G_BEGIN_DECLS

/* lrg-editor-server-tools.c: tilemap.* and timeline.* methods. */
G_GNUC_INTERNAL void editor_server_register_tools (LrgEditorServer *self);

G_END_DECLS
