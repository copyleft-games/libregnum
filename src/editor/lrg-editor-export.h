/* lrg-editor-export.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * IPC for packing a project. The editor calls
 * lrg_editor_export_register() once it has a server. Methods:
 *
 *   export.profiles  -> ["rres", "vfs", ...]
 *   export.run       params profile, output, project (project optional
 *                    when a default was given at registration)
 *
 * export.run returns { "ok": true, "log": "..." } or an error.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"
#include "lrg-editor-server.h"

G_BEGIN_DECLS

/**
 * lrg_editor_export_register:
 * @server: the editor IPC server
 * @project_dir: (nullable) (type filename): default project root
 *
 * Registers export.profiles and export.run. @project_dir is copied.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_export_register (LrgEditorServer *server,
                                 const gchar     *project_dir);

G_END_DECLS
