/* lrg-editor-assets-panel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorAssetsPanel: the editor's asset browser.
 *
 * A file browser rooted at the open project: the git branch and a
 * breadcrumb of the current folder, a name filter and a type filter, and
 * either a grid of thumbnails (LrgTileView) or a sortable list (LrgTable)
 * with name, type, size, modification time and git status. Every tile
 * and row carries its git badge; folders show the most significant
 * status below them. Double click opens (levels and prefabs in the
 * editor, scripts in the code panel or the external editor, media in the
 * desktop application), tiles drag into the viewport, the inspector and
 * onto folders (which moves them, .meta sidecar included), and a context
 * menu renames, duplicates, trashes, copies paths and GUIDs, creates
 * folders, levels and scripts, and edits an asset's import settings.
 *
 * The panel registers the "asset.reveal", "asset.rename", "asset.move",
 * "asset.duplicate", "asset.trash" and "asset.orphans" IPC methods and
 * gives the "view.asset-grid" and "tools.refresh-assets" actions their
 * callbacks when it is created.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-enums.h"
#include "../../ui/lrg-container.h"
#include "../lrg-editor-vcs.h"
#include "lrg-editor-app.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_ASSETS_PANEL (lrg_editor_assets_panel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorAssetsPanel, lrg_editor_assets_panel, LRG, EDITOR_ASSETS_PANEL, LrgContainer)

/**
 * lrg_editor_assets_panel_new:
 * @app: the editor application it presents
 *
 * Creates the asset browser of @app and registers its actions and IPC
 * methods on the application. It follows the open project: opening
 * another project shows that project's root folder.
 *
 * Returns: (transfer full): a new #LrgEditorAssetsPanel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorAssetsPanel * lrg_editor_assets_panel_new (LrgEditorApp *app);

/**
 * lrg_editor_assets_panel_get_app:
 * @self: an #LrgEditorAssetsPanel
 *
 * Returns: (transfer none) (nullable): the application, %NULL once it is gone
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_assets_panel_get_app (LrgEditorAssetsPanel *self);

/**
 * lrg_editor_assets_panel_get_directory:
 * @self: an #LrgEditorAssetsPanel
 *
 * Returns: (transfer none) (nullable): the absolute path of the folder
 *   shown, or %NULL while no project (or level) gives the panel a root
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_assets_panel_get_directory (LrgEditorAssetsPanel *self);

/**
 * lrg_editor_assets_panel_set_directory:
 * @self: an #LrgEditorAssetsPanel
 * @dir: a folder inside the project (absolute, or relative to the project)
 *
 * Shows @dir. Folders outside the project root are refused, so the
 * breadcrumb always starts at the project.
 *
 * Returns: %TRUE if @dir is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_assets_panel_set_directory (LrgEditorAssetsPanel *self,
                                                const gchar          *dir);

/**
 * lrg_editor_assets_panel_get_grid_view:
 * @self: an #LrgEditorAssetsPanel
 *
 * Returns: %TRUE for the thumbnail grid, %FALSE for the list
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_assets_panel_get_grid_view (LrgEditorAssetsPanel *self);

/**
 * lrg_editor_assets_panel_set_grid_view:
 * @self: an #LrgEditorAssetsPanel
 * @grid: %TRUE for the thumbnail grid, %FALSE for the list
 *
 * Switches between the views; the selection is kept. The
 * "view.asset-grid" action toggles the same state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_assets_panel_set_grid_view (LrgEditorAssetsPanel *self,
                                            gboolean              grid);

/**
 * lrg_editor_assets_panel_get_filter:
 * @self: an #LrgEditorAssetsPanel
 *
 * Returns: (transfer none): the name filter ("" for none)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_assets_panel_get_filter (LrgEditorAssetsPanel *self);

/**
 * lrg_editor_assets_panel_set_filter:
 * @self: an #LrgEditorAssetsPanel
 * @filter: (nullable): a fuzzy name pattern (lrg_editor_fuzzy_score()),
 *   %NULL or "" for none
 *
 * Shows only the entries whose name matches @filter (folders included).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_assets_panel_set_filter (LrgEditorAssetsPanel *self,
                                         const gchar          *filter);

/**
 * lrg_editor_assets_panel_get_type_filter:
 * @self: an #LrgEditorAssetsPanel
 *
 * Returns: the asset type shown, %LRG_ASSET_TYPE_UNKNOWN for all
 */
LRG_AVAILABLE_IN_ALL
LrgAssetType lrg_editor_assets_panel_get_type_filter (LrgEditorAssetsPanel *self);

/**
 * lrg_editor_assets_panel_set_type_filter:
 * @self: an #LrgEditorAssetsPanel
 * @type: the asset type to show, %LRG_ASSET_TYPE_UNKNOWN for all
 *
 * Shows only files of @type. Folders stay visible so the filter works
 * while browsing (they are hidden only when a name filter is set too and
 * they do not match it).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_assets_panel_set_type_filter (LrgEditorAssetsPanel *self,
                                              LrgAssetType          type);

/**
 * lrg_editor_assets_panel_refresh:
 * @self: an #LrgEditorAssetsPanel
 *
 * Lists the folder again now (the panel also refreshes by itself when
 * its directory monitor reports a change, and repaints git badges when
 * #LrgEditorVcs::changed fires).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_assets_panel_refresh (LrgEditorAssetsPanel *self);

/**
 * lrg_editor_assets_panel_get_n_entries:
 * @self: an #LrgEditorAssetsPanel
 *
 * Returns: the number of entries shown (after filtering)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_assets_panel_get_n_entries (LrgEditorAssetsPanel *self);

/**
 * lrg_editor_assets_panel_get_entry_path:
 * @self: an #LrgEditorAssetsPanel
 * @index: an entry index, in display order
 *
 * Returns: (transfer none) (nullable): the absolute path of the entry,
 *   %NULL when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_assets_panel_get_entry_path (LrgEditorAssetsPanel *self,
                                                      guint                 index);

/**
 * lrg_editor_assets_panel_find_entry:
 * @self: an #LrgEditorAssetsPanel
 * @path: an absolute path, or a path relative to the project
 *
 * Returns: the display index of @path, or -1 when it is not shown
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_assets_panel_find_entry (LrgEditorAssetsPanel *self,
                                         const gchar          *path);

/**
 * lrg_editor_assets_panel_get_entry_vcs_status:
 * @self: an #LrgEditorAssetsPanel
 * @index: an entry index, in display order
 *
 * The git status the entry's badge shows: the file's own status, or for
 * a folder the most significant status below it
 * (lrg_editor_vcs_get_directory_status()).
 *
 * Returns: the status, %LRG_EDITOR_VCS_STATUS_UNMODIFIED outside a
 *   repository or for an index out of range
 */
LRG_AVAILABLE_IN_ALL
LrgEditorVcsStatus lrg_editor_assets_panel_get_entry_vcs_status (LrgEditorAssetsPanel *self,
                                                                 guint                 index);

/**
 * lrg_editor_assets_panel_get_selected_paths:
 * @self: an #LrgEditorAssetsPanel
 *
 * Returns: (transfer full) (array zero-terminated=1): the absolute paths
 *   of the selected entries, in display order (empty when nothing is
 *   selected)
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_assets_panel_get_selected_paths (LrgEditorAssetsPanel *self);

/**
 * lrg_editor_assets_panel_reveal:
 * @self: an #LrgEditorAssetsPanel
 * @path: a file or folder inside the project (absolute or relative)
 *
 * Shows the folder containing @path, clears filters that would hide it,
 * selects it and scrolls it into view. The "asset.reveal" IPC method
 * calls this.
 *
 * Returns: %TRUE if @path was found and selected
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_assets_panel_reveal (LrgEditorAssetsPanel *self,
                                         const gchar          *path);

/**
 * lrg_editor_assets_panel_begin_rename:
 * @self: an #LrgEditorAssetsPanel
 * @path: an entry of the current folder (absolute or relative)
 *
 * Starts the inline rename of @path (what F2 does): a text field over
 * the entry's name, with the name selected up to the extension. Enter
 * renames through lrg_asset_database_rename(), so the .meta sidecar
 * and the GUID follow; Escape or a click elsewhere cancels.
 *
 * Returns: %TRUE if the rename field opened
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_assets_panel_begin_rename (LrgEditorAssetsPanel *self,
                                               const gchar          *path);

/**
 * lrg_editor_assets_panel_get_renaming:
 * @self: an #LrgEditorAssetsPanel
 *
 * Returns: (transfer none) (nullable): the absolute path being renamed
 *   inline, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_assets_panel_get_renaming (LrgEditorAssetsPanel *self);

/**
 * lrg_editor_assets_panel_show_import_settings:
 * @self: an #LrgEditorAssetsPanel
 * @path: an asset file (absolute or relative)
 *
 * Opens the Import Settings dialog of @path: an #LrgPropertyGrid over
 * the settings of its .meta sidecar (lrg_asset_meta_list_known_settings()
 * for the asset's importer). Every edit is validated, written to the
 * sidecar right away and regenerates the thumbnail.
 *
 * Returns: %TRUE if the dialog opened (the importer has settings)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_assets_panel_show_import_settings (LrgEditorAssetsPanel *self,
                                                       const gchar          *path);

/**
 * lrg_editor_assets_panel_create_variant:
 * @self: an #LrgEditorAssetsPanel
 * @path: a `.rprefab` or `.rmat` file (absolute or project-relative)
 * @error: return location for a #GError
 *
 * The entry menu's Create Variant: a prefab gets a variant through
 * lrg_editor_create_prefab_variant() (undoable), a material an instance
 * file through lrg_editor_material_editor_create_variant(); both are
 * written next to @path as `<stem>_variant` (numbered when taken).
 *
 * Returns: (transfer full) (nullable): the new file's path, or %NULL
 *   with @error set
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_assets_panel_create_variant (LrgEditorAssetsPanel  *self,
                                                const gchar           *path,
                                                GError               **error);

/**
 * lrg_editor_assets_panel_preview_clips:
 * @self: an #LrgEditorAssetsPanel
 * @path: a glTF model (`.glb` / `.gltf`), absolute or project-relative
 * @clip: (nullable): the animation to play first, %NULL for the first one
 *
 * Opens the Animations dialog of @path (the entry menu's "Preview
 * Animations..."): the model on a turntable playing a clip, a clip list,
 * a scrub bar, a track summary and one slider per morph target.
 *
 * Returns: %TRUE if the dialog opened (the model loaded)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_assets_panel_preview_clips (LrgEditorAssetsPanel *self,
                                                const gchar          *path,
                                                const gchar          *clip);

/**
 * lrg_editor_assets_panel_get_clip_preview_model:
 * @self: an #LrgEditorAssetsPanel
 *
 * Returns: (nullable): the absolute path of the model the open Animations
 *   dialog shows, %NULL when it is closed
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_assets_panel_get_clip_preview_model (LrgEditorAssetsPanel *self);

G_END_DECLS
