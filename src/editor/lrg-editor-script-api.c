/* lrg-editor-script-api.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorScriptApi: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-script-api.h"

struct _LrgEditorScriptApi
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorScriptApi, lrg_editor_script_api, G_TYPE_OBJECT)

static void
lrg_editor_script_api_class_init (LrgEditorScriptApiClass *klass)
{
}

static void
lrg_editor_script_api_init (LrgEditorScriptApi *self)
{
}

LrgEditorScriptApi *
lrg_editor_script_api_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_SCRIPT_API, NULL);
}
