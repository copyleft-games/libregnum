/* lrg-editor-nav-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorNavPanel: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-nav-panel.h"

struct _LrgEditorNavPanel
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorNavPanel, lrg_editor_nav_panel, G_TYPE_OBJECT)

static void
lrg_editor_nav_panel_class_init (LrgEditorNavPanelClass *klass)
{
}

static void
lrg_editor_nav_panel_init (LrgEditorNavPanel *self)
{
}

LrgEditorNavPanel *
lrg_editor_nav_panel_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_NAV_PANEL, NULL);
}
