/* lrg-editor-water-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorWaterEditor: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-water-editor.h"

struct _LrgEditorWaterEditor
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorWaterEditor, lrg_editor_water_editor, G_TYPE_OBJECT)

static void
lrg_editor_water_editor_class_init (LrgEditorWaterEditorClass *klass)
{
}

static void
lrg_editor_water_editor_init (LrgEditorWaterEditor *self)
{
}

LrgEditorWaterEditor *
lrg_editor_water_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_WATER_EDITOR, NULL);
}
