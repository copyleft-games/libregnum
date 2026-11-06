/* lrg-editor-timeline-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorTimelinePanel: the level's animation timelines (Wave 4: tracks, keys, easing).
 *
 * Since 0.8.1 (Wave 3) this is a placeholder: a muted "Timeline (Wave 4)"
 * label. The constructor signature is final.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_TIMELINE_PANEL (lrg_editor_timeline_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorTimelinePanel, lrg_editor_timeline_panel, LRG, EDITOR_TIMELINE_PANEL, LrgContainer)

/**
 * lrg_editor_timeline_panel_new:
 * @app: the editor application it presents
 *
 * Returns: (transfer full): a new #LrgEditorTimelinePanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTimelinePanel * lrg_editor_timeline_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_timeline_panel_get_app:
 * @self: an #LrgEditorTimelinePanel
 *
 * Returns: (transfer none) (nullable): the application, %NULL once it is gone
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_timeline_panel_get_app (LrgEditorTimelinePanel *self);

G_END_DECLS
