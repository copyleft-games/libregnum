/* lrg-editor-memory-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorMemoryPanel: GObject counts, asset residency and snapshot diffs.
 * The panel does not dock itself; the shell (or a test) adds it with
 * lrg_editor_app_add_panel() under the id "memory".
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../../debug/lrg-memory-profiler.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

/**
 * LRG_EDITOR_MEMORY_PANEL_ID:
 *
 * Dock id of the memory panel.
 *
 * Since: 0.9.0
 */
#define LRG_EDITOR_MEMORY_PANEL_ID "memory"

#define LRG_TYPE_EDITOR_MEMORY_PANEL (lrg_editor_memory_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorMemoryPanel, lrg_editor_memory_panel, LRG, EDITOR_MEMORY_PANEL, LrgContainer)

/**
 * lrg_editor_memory_panel_new:
 * @app: the editor application
 *
 * Creates the memory panel for @app, or returns another reference to the
 * one already created for it. Registers the memory.* IPC methods and the
 * memory.show-panel / memory.snapshot actions. Does not dock itself.
 *
 * Returns: (transfer full): the panel
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgEditorMemoryPanel * lrg_editor_memory_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_memory_panel_find:
 * @app: an #LrgEditorApp
 *
 * Returns: (transfer none) (nullable): the panel created for @app, or %NULL
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgEditorMemoryPanel * lrg_editor_memory_panel_find (LrgEditorApp *app);

/**
 * lrg_editor_memory_panel_get_profiler:
 * @self: an #LrgEditorMemoryPanel
 *
 * The profiler that owns this panel's snapshots and history. Not the
 * process-wide default.
 *
 * Returns: (transfer none): the panel's #LrgMemoryProfiler
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgMemoryProfiler * lrg_editor_memory_panel_get_profiler (LrgEditorMemoryPanel *self);

G_END_DECLS
