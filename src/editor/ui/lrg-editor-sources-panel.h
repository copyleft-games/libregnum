/* lrg-editor-sources-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSourcesPanel: the browser of the editor's asset sources
 * (#LrgAssetSource, registered with lrg_editor_app_add_asset_source(),
 * typically by a game module's editor hook).
 *
 * The panel shows one source at a time (a chooser appears when several
 * are registered): the current virtual folder with an Up button, a name
 * filter over the folder (Enter, or the Search button, searches the
 * whole source when it implements search), a list of folders and
 * entries with their type and size, and a preview of the selected entry:
 * the entry is exported to the source's cache (once; later selections
 * are instant) and its thumbnail drawn through the editor's thumbnail
 * cache. "Import" copies the export into the project's
 * assets/<source-id>/ folder, "Place" imports and places it in the level
 * like the Assets panel's "Place in Scene", "Export to..." copies it to
 * a folder chosen in a dialog. Without a registered source the panel
 * shows a hint.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"
#include "../lrg-asset-source.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_SOURCES_PANEL (lrg_editor_sources_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorSourcesPanel, lrg_editor_sources_panel, LRG, EDITOR_SOURCES_PANEL, LrgContainer)

/**
 * lrg_editor_sources_panel_new:
 * @app: the editor application it presents
 *
 * Creates the Sources panel of @app. It follows the app's source
 * registry: a source added or removed later shows up on the next frame.
 *
 * Returns: (transfer full): a new #LrgEditorSourcesPanel
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSourcesPanel * lrg_editor_sources_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_sources_panel_get_source:
 * @self: an #LrgEditorSourcesPanel
 *
 * Returns: (transfer none) (nullable): the source shown, %NULL when none
 *   is registered
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
LrgAssetSource * lrg_editor_sources_panel_get_source (LrgEditorSourcesPanel *self);

/**
 * lrg_editor_sources_panel_set_source:
 * @self: an #LrgEditorSourcesPanel
 * @id: a registered source id
 *
 * Shows the source's root folder.
 *
 * Returns: %TRUE if @id is registered
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sources_panel_set_source (LrgEditorSourcesPanel *self,
                                              const gchar           *id);

/**
 * lrg_editor_sources_panel_get_folder:
 * @self: an #LrgEditorSourcesPanel
 *
 * Returns: (transfer none): the virtual folder shown ("" for the root)
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_sources_panel_get_folder (LrgEditorSourcesPanel *self);

/**
 * lrg_editor_sources_panel_set_folder:
 * @self: an #LrgEditorSourcesPanel
 * @folder: a virtual folder of the current source ("" for the root)
 * @error: (nullable): return location for an error
 *
 * Lists @folder (what double-clicking a folder does). Leaves search
 * results; the name filter is cleared.
 *
 * Returns: %TRUE on success; on failure the previous folder stays
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sources_panel_set_folder (LrgEditorSourcesPanel  *self,
                                              const gchar            *folder,
                                              GError                **error);

/**
 * lrg_editor_sources_panel_go_up:
 * @self: an #LrgEditorSourcesPanel
 *
 * Shows the parent folder (from search results: the folder the search
 * started in). Does nothing at the root.
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sources_panel_go_up (LrgEditorSourcesPanel *self);

/**
 * lrg_editor_sources_panel_get_filter:
 * @self: an #LrgEditorSourcesPanel
 *
 * Returns: (transfer none): the name filter ("" for none)
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_sources_panel_get_filter (LrgEditorSourcesPanel *self);

/**
 * lrg_editor_sources_panel_set_filter:
 * @self: an #LrgEditorSourcesPanel
 * @filter: (nullable): a case-insensitive substring of the names to
 *   show, %NULL or "" for all
 *
 * Filters the folder (or the search results) shown.
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sources_panel_set_filter (LrgEditorSourcesPanel *self,
                                          const gchar           *filter);

/**
 * lrg_editor_sources_panel_search:
 * @self: an #LrgEditorSourcesPanel
 * @pattern: the text to search for
 * @error: (nullable): return location for an error
 *
 * Searches the whole current source (lrg_asset_source_search()) and
 * shows the results instead of a folder. Only sources that implement
 * search are searched (%G_IO_ERROR_NOT_SUPPORTED otherwise; the filter
 * still narrows the folder).
 *
 * Returns: %TRUE when results are shown
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sources_panel_search (LrgEditorSourcesPanel  *self,
                                          const gchar            *pattern,
                                          GError                **error);

/**
 * lrg_editor_sources_panel_is_searching:
 * @self: an #LrgEditorSourcesPanel
 *
 * Returns: %TRUE while search results are shown
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sources_panel_is_searching (LrgEditorSourcesPanel *self);

/**
 * lrg_editor_sources_panel_refresh:
 * @self: an #LrgEditorSourcesPanel
 *
 * Lists the folder (or repeats the search) again.
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sources_panel_refresh (LrgEditorSourcesPanel *self);

/**
 * lrg_editor_sources_panel_get_n_entries:
 * @self: an #LrgEditorSourcesPanel
 *
 * Returns: the number of entries shown (after filtering)
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_sources_panel_get_n_entries (LrgEditorSourcesPanel *self);

/**
 * lrg_editor_sources_panel_get_entry:
 * @self: an #LrgEditorSourcesPanel
 * @index: an entry index, in display order (folders first, then by name)
 *
 * Returns: (transfer none) (nullable): the entry, %NULL out of range
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
LrgAssetSourceEntry * lrg_editor_sources_panel_get_entry (LrgEditorSourcesPanel *self,
                                                          guint                  index);

/**
 * lrg_editor_sources_panel_find_entry:
 * @self: an #LrgEditorSourcesPanel
 * @path: a virtual path
 *
 * Returns: the display index of @path, or -1 when it is not shown
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_sources_panel_find_entry (LrgEditorSourcesPanel *self,
                                          const gchar           *path);

/**
 * lrg_editor_sources_panel_select:
 * @self: an #LrgEditorSourcesPanel
 * @path: (nullable): the virtual path of a shown entry, %NULL to clear
 *
 * Selects an entry. Its preview (the export) runs on the panel's next
 * frame, or now with lrg_editor_sources_panel_update_preview().
 *
 * Returns: %TRUE if @path is shown (or @path is %NULL)
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sources_panel_select (LrgEditorSourcesPanel *self,
                                          const gchar           *path);

/**
 * lrg_editor_sources_panel_get_selected:
 * @self: an #LrgEditorSourcesPanel
 *
 * Returns: (transfer none) (nullable): the selected entry
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
LrgAssetSourceEntry * lrg_editor_sources_panel_get_selected (LrgEditorSourcesPanel *self);

/**
 * lrg_editor_sources_panel_activate:
 * @self: an #LrgEditorSourcesPanel
 * @index: an entry index, in display order
 *
 * What double click (or Enter) on a row does: a folder opens; an asset
 * is selected and previewed.
 *
 * Returns: %TRUE if @index is valid
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sources_panel_activate (LrgEditorSourcesPanel *self,
                                            guint                  index);

/**
 * lrg_editor_sources_panel_update_preview:
 * @self: an #LrgEditorSourcesPanel
 *
 * Runs the pending preview of the selection now: exports the entry to
 * the cache (blocking; instant when cached) and requests its thumbnail.
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sources_panel_update_preview (LrgEditorSourcesPanel *self);

/**
 * lrg_editor_sources_panel_get_preview_file:
 * @self: an #LrgEditorSourcesPanel
 *
 * Returns: (transfer none) (nullable): the cached export of the selected
 *   entry once its preview ran, else %NULL
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_sources_panel_get_preview_file (LrgEditorSourcesPanel *self);

/**
 * lrg_editor_sources_panel_import_selected:
 * @self: an #LrgEditorSourcesPanel
 * @error: (nullable): return location for an error
 *
 * The "Import" button: lrg_editor_app_import_source_entry() on the
 * selection, with a status message (or an error toast).
 *
 * Returns: (transfer full) (nullable): the imported file relative to the
 *   project, or %NULL on error
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_sources_panel_import_selected (LrgEditorSourcesPanel  *self,
                                                  GError                **error);

/**
 * lrg_editor_sources_panel_place_selected:
 * @self: an #LrgEditorSourcesPanel
 * @error: (nullable): return location for an error
 *
 * The "Place" button: lrg_editor_app_place_source_entry() on the
 * selection.
 *
 * Returns: (transfer none) (nullable): the new node, or %NULL on error
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_sources_panel_place_selected (LrgEditorSourcesPanel  *self,
                                                   GError                **error);

/**
 * lrg_editor_sources_panel_export_selected:
 * @self: an #LrgEditorSourcesPanel
 * @dest_dir: the folder to copy the export into
 * @error: (nullable): return location for an error
 *
 * What "Export to..." does once a folder is chosen:
 * lrg_editor_app_export_source_entry() into @dest_dir.
 *
 * Returns: (transfer full) (nullable): the main file in @dest_dir
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_sources_panel_export_selected (LrgEditorSourcesPanel  *self,
                                                  const gchar            *dest_dir,
                                                  GError                **error);

G_END_DECLS
