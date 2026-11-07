/* lrg-editor-mixer-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorMixerPanel: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-mixer-panel.h"

struct _LrgEditorMixerPanel
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorMixerPanel, lrg_editor_mixer_panel, G_TYPE_OBJECT)

static void
lrg_editor_mixer_panel_class_init (LrgEditorMixerPanelClass *klass)
{
}

static void
lrg_editor_mixer_panel_init (LrgEditorMixerPanel *self)
{
}

LrgEditorMixerPanel *
lrg_editor_mixer_panel_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_MIXER_PANEL, NULL);
}
