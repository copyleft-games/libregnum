/* lrg-editor-environment-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorEnvironmentPanel: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-environment-panel.h"

struct _LrgEditorEnvironmentPanel
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorEnvironmentPanel, lrg_editor_environment_panel, G_TYPE_OBJECT)

static void
lrg_editor_environment_panel_class_init (LrgEditorEnvironmentPanelClass *klass)
{
}

static void
lrg_editor_environment_panel_init (LrgEditorEnvironmentPanel *self)
{
}

LrgEditorEnvironmentPanel *
lrg_editor_environment_panel_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_ENVIRONMENT_PANEL, NULL);
}
