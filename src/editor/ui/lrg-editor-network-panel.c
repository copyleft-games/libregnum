/* lrg-editor-network-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorNetworkPanel: (stub, being written)
 */

#include "config.h"

#include "lrg-editor-network-panel.h"

struct _LrgEditorNetworkPanel
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEditorNetworkPanel, lrg_editor_network_panel, G_TYPE_OBJECT)

static void
lrg_editor_network_panel_class_init (LrgEditorNetworkPanelClass *klass)
{
}

static void
lrg_editor_network_panel_init (LrgEditorNetworkPanel *self)
{
}

LrgEditorNetworkPanel *
lrg_editor_network_panel_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_NETWORK_PANEL, NULL);
}
