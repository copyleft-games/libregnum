/* lrg-editor-gizmo-paint.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorGizmoPaint: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-gizmo-paint.h"

struct _LrgEditorGizmoPaint
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorGizmoPaint, lrg_editor_gizmo_paint, G_TYPE_OBJECT)

static void
lrg_editor_gizmo_paint_class_init (LrgEditorGizmoPaintClass *klass)
{
}

static void
lrg_editor_gizmo_paint_init (LrgEditorGizmoPaint *self)
{
}

LrgEditorGizmoPaint *
lrg_editor_gizmo_paint_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_GIZMO_PAINT, NULL);
}
