/* lrg-editor-mixer-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorMixerPanel: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_MIXER_PANEL (lrg_editor_mixer_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorMixerPanel, lrg_editor_mixer_panel, LRG, EDITOR_MIXER_PANEL, GObject)

/**
 * lrg_editor_mixer_panel_new:
 *
 * Returns: (transfer full): a new #LrgEditorMixerPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorMixerPanel * lrg_editor_mixer_panel_new (void);

G_END_DECLS
