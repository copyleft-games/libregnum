/* lrg-editor-sequencer-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSequencerPanel: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-sequencer-panel.h"

struct _LrgEditorSequencerPanel
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorSequencerPanel, lrg_editor_sequencer_panel, G_TYPE_OBJECT)

static void
lrg_editor_sequencer_panel_class_init (LrgEditorSequencerPanelClass *klass)
{
}

static void
lrg_editor_sequencer_panel_init (LrgEditorSequencerPanel *self)
{
}

LrgEditorSequencerPanel *
lrg_editor_sequencer_panel_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_SEQUENCER_PANEL, NULL);
}
