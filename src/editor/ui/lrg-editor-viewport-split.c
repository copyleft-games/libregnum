/* lrg-editor-viewport-split.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorViewportSplit: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-viewport-split.h"

struct _LrgEditorViewportSplit
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorViewportSplit, lrg_editor_viewport_split, LRG_TYPE_CONTAINER)

static void
lrg_editor_viewport_split_class_init (LrgEditorViewportSplitClass *klass)
{
}

static void
lrg_editor_viewport_split_init (LrgEditorViewportSplit *self)
{
}

LrgEditorViewportSplit *
lrg_editor_viewport_split_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_VIEWPORT_SPLIT, NULL);
}
