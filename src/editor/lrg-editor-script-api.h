/* lrg-editor-script-api.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorScriptApi: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_SCRIPT_API (lrg_editor_script_api_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorScriptApi, lrg_editor_script_api, LRG, EDITOR_SCRIPT_API, GObject)

/**
 * lrg_editor_script_api_new:
 *
 * Returns: (transfer full): a new #LrgEditorScriptApi
 */
LRG_AVAILABLE_IN_ALL
LrgEditorScriptApi * lrg_editor_script_api_new (void);

G_END_DECLS
