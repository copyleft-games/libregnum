/* lrg-editor-palette.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPalette: the command palette and quick open.
 *
 * A popup that springs down below the menu bar with a query field and a
 * ranked list. In %LRG_EDITOR_PALETTE_MODE_COMMANDS (Ctrl+Shift+P or F1,
 * action "tools.palette") it fuzzy-searches every editor action by
 * label, category and id, shows the shortcut and greys out actions that
 * cannot run now; a query starting with "@" lists the level's nodes by
 * name instead and Enter selects and frames one. In
 * %LRG_EDITOR_PALETTE_MODE_FILES (Ctrl+P, action "tools.quick-open") it
 * fuzzy-searches the project's files (up to 20000, scanned on a worker
 * thread each time it opens; hidden entries, build/, node_modules/ and
 * trees/ are skipped) and Enter opens the file: levels and prefabs in the
 * editor, scripts and text in the code panel or the external editor
 * (#LrgEditorSettings:script-editor), media with the desktop's default
 * application.
 *
 * Up / Down / Page Up / Page Down move the highlight, Enter or a click
 * runs it, Escape or a click outside closes the palette. While it shows,
 * #LrgEditorViewState:palette-open is %TRUE (ui.state "palette").
 *
 * The editor makes one palette per application when its code panel is
 * created (lrg_editor_code_panel_new()), which also replaces the
 * tools.palette and tools.quick-open actions with ones that present it.
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

/**
 * LrgEditorPaletteMode:
 * @LRG_EDITOR_PALETTE_MODE_COMMANDS: editor actions ("@" queries list nodes)
 * @LRG_EDITOR_PALETTE_MODE_FILES: project files (quick open)
 *
 * What the palette searches.
 */
typedef enum
{
	LRG_EDITOR_PALETTE_MODE_COMMANDS,
	LRG_EDITOR_PALETTE_MODE_FILES
} LrgEditorPaletteMode;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_palette_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_PALETTE_MODE (lrg_editor_palette_mode_get_type ())

/**
 * LRG_EDITOR_PALETTE_FILE_LIMIT:
 *
 * The most files quick open lists.
 */
#define LRG_EDITOR_PALETTE_FILE_LIMIT (20000)

#define LRG_TYPE_EDITOR_PALETTE (lrg_editor_palette_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorPalette, lrg_editor_palette, LRG, EDITOR_PALETTE, LrgContainer)

/**
 * lrg_editor_palette_new:
 * @app: the editor application it presents
 *
 * Creates a palette for @app and points the "tools.palette" and
 * "tools.quick-open" actions at it.
 *
 * Returns: (transfer full): a new #LrgEditorPalette
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPalette * lrg_editor_palette_new (LrgEditorApp *app);

/**
 * lrg_editor_palette_get_app:
 * @self: an #LrgEditorPalette
 *
 * Returns: (transfer none) (nullable): the application, %NULL once it is gone
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_palette_get_app (LrgEditorPalette *self);

/**
 * lrg_editor_palette_present:
 * @self: an #LrgEditorPalette
 * @mode: what to search
 * @query: (nullable): text to start with, or %NULL for an empty query
 *
 * Shows the palette (or switches the open one to @mode) with the query
 * field focused. Files mode starts a project scan; the list fills in when
 * it finishes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_palette_present (LrgEditorPalette     *self,
                                 LrgEditorPaletteMode  mode,
                                 const gchar          *query);

/**
 * lrg_editor_palette_dismiss:
 * @self: an #LrgEditorPalette
 *
 * Closes the palette without running anything.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_palette_dismiss (LrgEditorPalette *self);

/**
 * lrg_editor_palette_get_open:
 * @self: an #LrgEditorPalette
 *
 * Returns: %TRUE while the palette shows
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_palette_get_open (LrgEditorPalette *self);

/**
 * lrg_editor_palette_get_mode:
 * @self: an #LrgEditorPalette
 *
 * Returns: what the palette searches
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPaletteMode lrg_editor_palette_get_mode (LrgEditorPalette *self);

/**
 * lrg_editor_palette_get_query:
 * @self: an #LrgEditorPalette
 *
 * Returns: (transfer none): the query text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_palette_get_query (LrgEditorPalette *self);

/**
 * lrg_editor_palette_set_query:
 * @self: an #LrgEditorPalette
 * @query: the new query
 *
 * Replaces the query text (as typing would) and re-ranks the results.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_palette_set_query (LrgEditorPalette *self,
                                   const gchar      *query);

/**
 * lrg_editor_palette_get_n_results:
 * @self: an #LrgEditorPalette
 *
 * Returns: the number of results for the current query
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_palette_get_n_results (LrgEditorPalette *self);

/**
 * lrg_editor_palette_get_result:
 * @self: an #LrgEditorPalette
 * @index: a result index (0 is the best match)
 *
 * Returns: (transfer none) (nullable): what result @index stands for: an
 *   action id, a project-relative file path or a node GUID; %NULL when out
 *   of range
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_palette_get_result (LrgEditorPalette *self,
                                             guint             index);

/**
 * lrg_editor_palette_get_selected:
 * @self: an #LrgEditorPalette
 *
 * Returns: the highlighted result, or -1 when there are no results
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_palette_get_selected (LrgEditorPalette *self);

/**
 * lrg_editor_palette_set_selected:
 * @self: an #LrgEditorPalette
 * @index: a result index (clamped)
 *
 * Highlights a result and scrolls it into view.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_palette_set_selected (LrgEditorPalette *self,
                                      gint              index);

/**
 * lrg_editor_palette_activate:
 * @self: an #LrgEditorPalette
 *
 * Runs the highlighted result as Enter does: closes the palette, then
 * runs the action, selects the node or opens the file. A disabled action
 * does not run and the palette stays open.
 *
 * Returns: %TRUE if something ran
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_palette_activate (LrgEditorPalette *self);

/**
 * lrg_editor_palette_get_scanning:
 * @self: an #LrgEditorPalette
 *
 * Returns: %TRUE while the project file scan of quick open runs
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_palette_get_scanning (LrgEditorPalette *self);

/**
 * lrg_editor_palette_get_n_files:
 * @self: an #LrgEditorPalette
 *
 * Returns: the number of project files the last scan found
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_palette_get_n_files (LrgEditorPalette *self);

G_END_DECLS
