/* lrg-editor-app.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Standalone engine-drawn editor application.
 *
 * LrgEditorApp is the entry point for the standalone (non-cmacs) editor: it
 * pairs an #LrgEditor runtime with an engine-drawn #LrgCanvas UI assembled from
 * the engine's own widget toolkit. It mirrors the editor's level tree into an
 * outliner panel and re-syncs on the editor's "changed" signal. A host loop
 * pumps it with lrg_editor_app_handle_input() and lrg_editor_app_render().
 *
 * Only built when libregnum is configured with the editor UI
 * (BUILD_EDITOR_UI=1 / -DLRG_BUILD_EDITOR_UI), which implies the editor module.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_APP (lrg_editor_app_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorApp, lrg_editor_app, LRG, EDITOR_APP, GObject)

/**
 * lrg_editor_app_new:
 * @editor: (nullable) (transfer none): the editor to drive, or %NULL to make one
 *
 * Returns: (transfer full): a new #LrgEditorApp
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_app_new (LrgEditor *editor);

/**
 * lrg_editor_app_get_editor:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the editor runtime
 */
LRG_AVAILABLE_IN_ALL
LrgEditor * lrg_editor_app_get_editor (LrgEditorApp *self);

/**
 * lrg_editor_app_get_canvas:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the root #LrgCanvas
 */
LRG_AVAILABLE_IN_ALL
LrgCanvas * lrg_editor_app_get_canvas (LrgEditorApp *self);

/**
 * lrg_editor_app_refresh:
 * @self: an #LrgEditorApp
 *
 * Rebuilds the outliner panel from the editor's current level tree.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_app_refresh (LrgEditorApp *self);

/**
 * lrg_editor_app_handle_input:
 * @self: an #LrgEditorApp
 *
 * Dispatches pending input to the UI (call once per frame).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_app_handle_input (LrgEditorApp *self);

/**
 * lrg_editor_app_render:
 * @self: an #LrgEditorApp
 *
 * Renders the editor UI (call once per frame, inside the render target).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_app_render (LrgEditorApp *self);

G_END_DECLS
