/* lrg-editor-viewport-split.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorViewportSplit: the editor's viewport area. Holds one to four
 * #LrgEditorViewportPanel widgets in nested #LrgSplitter panes, following
 * the layout of the app's #LrgEditorViewState (single, split-h, split-v,
 * quad). Panel i always shows camera i, so a layout change keeps every
 * viewport's camera, render mode and overlays; splitter positions are
 * kept too. One viewport can be maximised temporarily (shown alone
 * without changing the layout).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "lrg-editor-app.h"
#include "lrg-editor-viewport-panel.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_VIEWPORT_SPLIT (lrg_editor_viewport_split_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorViewportSplit, lrg_editor_viewport_split, LRG, EDITOR_VIEWPORT_SPLIT, LrgContainer)

/**
 * lrg_editor_viewport_split_new:
 * @app: the application whose view state chooses the layout
 *
 * Creates the four viewport panels and shows the view state's layout.
 * The editor window hosts this widget in its "viewport" dock panel. It
 * also registers the view.layout, view.render_mode and view.overlay IPC
 * methods.
 *
 * Returns: (transfer full): a new #LrgEditorViewportSplit
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewportSplit * lrg_editor_viewport_split_new (LrgEditorApp *app);

/**
 * lrg_editor_viewport_split_get_panel:
 * @self: an #LrgEditorViewportSplit
 * @index: a viewport index, 0 .. %LRG_EDITOR_VIEW_MAX_VIEWPORTS - 1
 *
 * Returns: (transfer none) (nullable): panel @index (shown or not), %NULL
 *   out of range
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewportPanel * lrg_editor_viewport_split_get_panel (LrgEditorViewportSplit *self,
                                                              guint                   index);

/**
 * lrg_editor_viewport_split_get_n_visible:
 * @self: an #LrgEditorViewportSplit
 *
 * Returns: how many panels are on screen (1, 2 or 4; 1 while maximised)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_viewport_split_get_n_visible (LrgEditorViewportSplit *self);

/**
 * lrg_editor_viewport_split_get_active_panel:
 * @self: an #LrgEditorViewportSplit
 *
 * Returns: (transfer none): the panel of the view state's active viewport
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewportPanel * lrg_editor_viewport_split_get_active_panel (LrgEditorViewportSplit *self);

/**
 * lrg_editor_viewport_split_set_maximized:
 * @self: an #LrgEditorViewportSplit
 * @index: the viewport to show alone, or -1 to show the whole layout
 *
 * Temporarily shows one viewport alone (the header's maximise button);
 * the layout itself is not changed and comes back with -1 or a layout
 * change. Maximising also makes @index the active viewport.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_split_set_maximized (LrgEditorViewportSplit *self,
                                              gint                    index);

/**
 * lrg_editor_viewport_split_get_maximized:
 * @self: an #LrgEditorViewportSplit
 *
 * Returns: the maximised viewport, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_viewport_split_get_maximized (LrgEditorViewportSplit *self);

/**
 * lrg_editor_viewport_split_set_gpu_enabled:
 * @self: an #LrgEditorViewportSplit
 * @enabled: %FALSE frees every panel's render textures (before the
 *   window closes); %TRUE renders again
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_split_set_gpu_enabled (LrgEditorViewportSplit *self,
                                                gboolean                enabled);

/**
 * lrg_editor_viewport_split_register_methods:
 * @app: the application
 *
 * Registers the viewport IPC methods on the app's server (idempotent):
 * view.layout {layout?}, view.render_mode {viewport?, mode?} and
 * view.overlay {viewport?, name?, enabled?}. Called by
 * lrg_editor_viewport_split_new() and lrg_editor_viewport_panel_new();
 * see docs/editor/editor-app.org, section Viewport.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_viewport_split_register_methods (LrgEditorApp *app);

G_END_DECLS
