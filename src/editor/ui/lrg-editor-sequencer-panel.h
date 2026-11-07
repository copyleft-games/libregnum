/* lrg-editor-sequencer-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSequencerPanel: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_SEQUENCER_PANEL (lrg_editor_sequencer_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorSequencerPanel, lrg_editor_sequencer_panel, LRG, EDITOR_SEQUENCER_PANEL, GObject)

/**
 * lrg_editor_sequencer_panel_new:
 *
 * Returns: (transfer full): a new #LrgEditorSequencerPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSequencerPanel * lrg_editor_sequencer_panel_new (void);

G_END_DECLS
