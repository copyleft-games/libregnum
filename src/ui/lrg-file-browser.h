/* lrg-file-browser.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFileBrowser: browse folders and pick files to open or save.
 * LrgFileFilter: a named set of glob patterns, suffixes and MIME types.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-container.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Enums
 * ========================================================================== */

/**
 * LrgFileBrowserMode:
 * @LRG_FILE_BROWSER_MODE_OPEN: pick one existing file
 * @LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE: pick one or more existing files
 *   (Ctrl toggles, Shift selects a range)
 * @LRG_FILE_BROWSER_MODE_SAVE: choose a folder and type a file name;
 *   the extension of the current filter is added when the name has none
 * @LRG_FILE_BROWSER_MODE_SELECT_FOLDER: pick a folder; files are not
 *   listed
 *
 * What an #LrgFileBrowser is used for.
 */
typedef enum
{
	LRG_FILE_BROWSER_MODE_OPEN,
	LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE,
	LRG_FILE_BROWSER_MODE_SAVE,
	LRG_FILE_BROWSER_MODE_SELECT_FOLDER
} LrgFileBrowserMode;

LRG_AVAILABLE_IN_ALL
GType lrg_file_browser_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FILE_BROWSER_MODE (lrg_file_browser_mode_get_type ())

/**
 * LrgFileBrowserSort:
 * @LRG_FILE_BROWSER_SORT_NAME: by display name, numbers in natural order
 * @LRG_FILE_BROWSER_SORT_SIZE: by size in bytes
 * @LRG_FILE_BROWSER_SORT_MODIFIED: by modification time
 *
 * The column an #LrgFileBrowser sorts its list by. Folders stay on top
 * while #LrgFileBrowser:folders-first is set; ties are broken by name.
 */
typedef enum
{
	LRG_FILE_BROWSER_SORT_NAME,
	LRG_FILE_BROWSER_SORT_SIZE,
	LRG_FILE_BROWSER_SORT_MODIFIED
} LrgFileBrowserSort;

LRG_AVAILABLE_IN_ALL
GType lrg_file_browser_sort_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FILE_BROWSER_SORT (lrg_file_browser_sort_get_type ())

/* ==========================================================================
 * LrgFileFilter
 * ========================================================================== */

#define LRG_TYPE_FILE_FILTER (lrg_file_filter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFileFilter, lrg_file_filter, LRG, FILE_FILTER, GObject)

/**
 * lrg_file_filter_new:
 * @name: (nullable): the name shown in the filter list, e.g. "Images"
 *
 * Creates a filter with no rules. A filter without rules matches every
 * file; add glob patterns, suffixes or MIME types to narrow it.
 *
 * Returns: (transfer full): a new #LrgFileFilter
 */
LRG_AVAILABLE_IN_ALL
LrgFileFilter * lrg_file_filter_new (const gchar *name);

/**
 * lrg_file_filter_get_name:
 * @self: an #LrgFileFilter
 *
 * Returns: (transfer none) (nullable): the filter's name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_file_filter_get_name (LrgFileFilter *self);

/**
 * lrg_file_filter_set_name:
 * @self: an #LrgFileFilter
 * @name: (nullable): the name shown in the filter list
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_filter_set_name (LrgFileFilter *self,
                               const gchar   *name);

/**
 * lrg_file_filter_add_pattern:
 * @self: an #LrgFileFilter
 * @pattern: a glob such as "*.png" or "save-??.dat"
 *
 * Adds a glob pattern ('*' and '?' wildcards) matched against the file
 * name. Matching ignores case, so "*.png" also accepts "SHOT.PNG".
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_filter_add_pattern (LrgFileFilter *self,
                                  const gchar   *pattern);

/**
 * lrg_file_filter_add_suffix:
 * @self: an #LrgFileFilter
 * @suffix: a file extension without the dot, e.g. "png"
 *
 * Adds a suffix rule, the same as the pattern "*.@suffix". The first
 * suffix (or simple "*.ext" pattern) is the extension the browser adds
 * to a name typed in %LRG_FILE_BROWSER_MODE_SAVE.
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_filter_add_suffix (LrgFileFilter *self,
                                 const gchar   *suffix);

/**
 * lrg_file_filter_add_mime_type:
 * @self: an #LrgFileFilter
 * @mime_type: a MIME type such as "image/png", or a whole family such
 *   as "image/\*"
 *
 * Adds a MIME rule, checked against the content type GIO guesses from
 * the file name (subclasses match too: "text/plain" accepts C sources).
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_filter_add_mime_type (LrgFileFilter *self,
                                    const gchar   *mime_type);

/**
 * lrg_file_filter_get_patterns:
 * @self: an #LrgFileFilter
 *
 * Returns: (transfer none) (array zero-terminated=1): the glob patterns
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_file_filter_get_patterns (LrgFileFilter *self);

/**
 * lrg_file_filter_get_suffixes:
 * @self: an #LrgFileFilter
 *
 * Returns: (transfer none) (array zero-terminated=1): the suffixes
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_file_filter_get_suffixes (LrgFileFilter *self);

/**
 * lrg_file_filter_get_mime_types:
 * @self: an #LrgFileFilter
 *
 * Returns: (transfer none) (array zero-terminated=1): the MIME types
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_file_filter_get_mime_types (LrgFileFilter *self);

/**
 * lrg_file_filter_match_name:
 * @self: an #LrgFileFilter
 * @name: a file name (no folder part)
 * @content_type: (nullable): its content type, or %NULL to guess it from
 *   @name when the filter has MIME rules
 *
 * Returns: %TRUE if any rule accepts the file, or the filter has none
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_filter_match_name (LrgFileFilter *self,
                                     const gchar   *name,
                                     const gchar   *content_type);

/**
 * lrg_file_filter_match:
 * @self: an #LrgFileFilter
 * @info: a #GFileInfo with at least "standard::name" (and
 *   "standard::fast-content-type" or "standard::content-type" for MIME
 *   rules)
 *
 * Returns: %TRUE if the filter accepts the file described by @info
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_filter_match (LrgFileFilter *self,
                                GFileInfo     *info);

/**
 * lrg_file_filter_get_default_extension:
 * @self: an #LrgFileFilter
 *
 * The extension a save dialog should add to a bare name: the first
 * suffix, else the first pattern of the form "*.ext".
 *
 * Returns: (transfer full) (nullable): the extension without the dot
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_file_filter_get_default_extension (LrgFileFilter *self);

/* ==========================================================================
 * LrgFileBrowser
 * ========================================================================== */

#define LRG_TYPE_FILE_BROWSER (lrg_file_browser_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFileBrowser, lrg_file_browser, LRG, FILE_BROWSER, LrgContainer)

/**
 * lrg_file_browser_new:
 *
 * Creates a browser in %LRG_FILE_BROWSER_MODE_OPEN showing the current
 * working directory. Folders load asynchronously on the thread-default
 * main context; iterate it for the list to fill.
 *
 * Returns: (transfer full): a new #LrgFileBrowser
 */
LRG_AVAILABLE_IN_ALL
LrgFileBrowser * lrg_file_browser_new (void);

/**
 * lrg_file_browser_get_mode:
 * @self: an #LrgFileBrowser
 *
 * Returns: the browser's mode
 */
LRG_AVAILABLE_IN_ALL
LrgFileBrowserMode lrg_file_browser_get_mode (LrgFileBrowser *self);

/**
 * lrg_file_browser_set_mode:
 * @self: an #LrgFileBrowser
 * @mode: the new mode
 *
 * Switches the mode. %LRG_FILE_BROWSER_MODE_SAVE shows the name field,
 * %LRG_FILE_BROWSER_MODE_SELECT_FOLDER hides files, and leaving
 * %LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE keeps only the cursor selected.
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_set_mode (LrgFileBrowser     *self,
                                LrgFileBrowserMode  mode);

/**
 * lrg_file_browser_get_location:
 * @self: an #LrgFileBrowser
 *
 * Returns: (transfer none): the folder being shown
 */
LRG_AVAILABLE_IN_ALL
GFile * lrg_file_browser_get_location (LrgFileBrowser *self);

/**
 * lrg_file_browser_set_location:
 * @self: an #LrgFileBrowser
 * @location: the folder to show
 *
 * Navigates to @location: the previous folder goes on the back
 * history, any running load is cancelled and a new one starts. Emits
 * #LrgFileBrowser::location-changed. Does nothing when @location is
 * already shown (use lrg_file_browser_reload() to refresh).
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_set_location (LrgFileBrowser *self,
                                    GFile          *location);

/**
 * lrg_file_browser_set_location_path:
 * @self: an #LrgFileBrowser
 * @path: (type filename): a local folder path; "~" expands to the home
 *   folder and relative paths resolve against the current location
 *
 * Convenience wrapper around lrg_file_browser_set_location().
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_set_location_path (LrgFileBrowser *self,
                                         const gchar    *path);

/**
 * lrg_file_browser_go_back:
 * @self: an #LrgFileBrowser
 *
 * Returns to the previous folder in the history (Alt+Left).
 *
 * Returns: %TRUE if there was one
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_go_back (LrgFileBrowser *self);

/**
 * lrg_file_browser_go_forward:
 * @self: an #LrgFileBrowser
 *
 * Undoes lrg_file_browser_go_back() (Alt+Right).
 *
 * Returns: %TRUE if there was a folder to go forward to
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_go_forward (LrgFileBrowser *self);

/**
 * lrg_file_browser_go_up:
 * @self: an #LrgFileBrowser
 *
 * Shows the parent folder with the folder just left selected
 * (Backspace, Alt+Up).
 *
 * Returns: %TRUE if the location has a parent
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_go_up (LrgFileBrowser *self);

/**
 * lrg_file_browser_can_go_back:
 * @self: an #LrgFileBrowser
 *
 * Returns: %TRUE if the back history is not empty
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_can_go_back (LrgFileBrowser *self);

/**
 * lrg_file_browser_can_go_forward:
 * @self: an #LrgFileBrowser
 *
 * Returns: %TRUE if the forward history is not empty
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_can_go_forward (LrgFileBrowser *self);

/**
 * lrg_file_browser_can_go_up:
 * @self: an #LrgFileBrowser
 *
 * Returns: %TRUE if the location has a parent folder
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_can_go_up (LrgFileBrowser *self);

/**
 * lrg_file_browser_reload:
 * @self: an #LrgFileBrowser
 *
 * Lists the current folder again (F5). The old list stays visible until
 * the new one is complete; the selection and cursor are kept by name.
 * The browser also reloads by itself when its folder monitor reports a
 * change.
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_reload (LrgFileBrowser *self);

/**
 * lrg_file_browser_get_loading:
 * @self: an #LrgFileBrowser
 *
 * Returns: %TRUE while the folder is being enumerated
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_get_loading (LrgFileBrowser *self);

/**
 * lrg_file_browser_get_error:
 * @self: an #LrgFileBrowser
 *
 * The error of the last load, shown as a row in the list (for example
 * permission denied or a missing folder).
 *
 * Returns: (transfer none) (nullable): the error, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const GError * lrg_file_browser_get_error (LrgFileBrowser *self);

/**
 * lrg_file_browser_get_show_hidden:
 * @self: an #LrgFileBrowser
 *
 * Returns: %TRUE if hidden and backup files are listed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_get_show_hidden (LrgFileBrowser *self);

/**
 * lrg_file_browser_set_show_hidden:
 * @self: an #LrgFileBrowser
 * @show_hidden: list dot files, backup files and names in ".hidden"
 *   (Ctrl+H toggles)
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_set_show_hidden (LrgFileBrowser *self,
                                       gboolean        show_hidden);

/**
 * lrg_file_browser_get_folders_first:
 * @self: an #LrgFileBrowser
 *
 * Returns: %TRUE if folders are sorted before files
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_get_folders_first (LrgFileBrowser *self);

/**
 * lrg_file_browser_set_folders_first:
 * @self: an #LrgFileBrowser
 * @folders_first: keep folders above files whatever the sort order
 *   (the default)
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_set_folders_first (LrgFileBrowser *self,
                                         gboolean        folders_first);

/**
 * lrg_file_browser_get_sort_column:
 * @self: an #LrgFileBrowser
 *
 * Returns: the column the list is sorted by
 */
LRG_AVAILABLE_IN_ALL
LrgFileBrowserSort lrg_file_browser_get_sort_column (LrgFileBrowser *self);

/**
 * lrg_file_browser_get_sort_ascending:
 * @self: an #LrgFileBrowser
 *
 * Returns: %TRUE for ascending order
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_get_sort_ascending (LrgFileBrowser *self);

/**
 * lrg_file_browser_set_sort:
 * @self: an #LrgFileBrowser
 * @column: the column to sort by
 * @ascending: %TRUE for A to Z, small to large, old to new
 *
 * Sorts the list. Clicking a column header does the same (a second
 * click flips the order).
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_set_sort (LrgFileBrowser     *self,
                                LrgFileBrowserSort  column,
                                gboolean            ascending);

/**
 * lrg_file_browser_add_filter:
 * @self: an #LrgFileBrowser
 * @filter: a filter to offer
 *
 * Adds @filter to the filter list shown under the files. The first
 * filter added becomes the current one.
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_add_filter (LrgFileBrowser *self,
                                  LrgFileFilter  *filter);

/**
 * lrg_file_browser_remove_filter:
 * @self: an #LrgFileBrowser
 * @filter: a filter added earlier
 *
 * Removes @filter; when it was current the first remaining filter (or
 * none) takes over.
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_remove_filter (LrgFileBrowser *self,
                                     LrgFileFilter  *filter);

/**
 * lrg_file_browser_get_filters:
 * @self: an #LrgFileBrowser
 *
 * Returns: (transfer none): a #GListModel of #LrgFileFilter
 */
LRG_AVAILABLE_IN_ALL
GListModel * lrg_file_browser_get_filters (LrgFileBrowser *self);

/**
 * lrg_file_browser_get_filter:
 * @self: an #LrgFileBrowser
 *
 * Returns: (transfer none) (nullable): the filter files are matched
 *   against, or %NULL to show every file
 */
LRG_AVAILABLE_IN_ALL
LrgFileFilter * lrg_file_browser_get_filter (LrgFileBrowser *self);

/**
 * lrg_file_browser_set_filter:
 * @self: an #LrgFileBrowser
 * @filter: (nullable): the filter to apply; it is added to the filter
 *   list when missing; %NULL shows every file
 *
 * Folders are never filtered out. In %LRG_FILE_BROWSER_MODE_SAVE the
 * extension of the typed name follows the new filter's extension.
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_set_filter (LrgFileBrowser *self,
                                  LrgFileFilter  *filter);

/**
 * lrg_file_browser_get_current_name:
 * @self: an #LrgFileBrowser
 *
 * Returns: (transfer none): the text of the name field
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_file_browser_get_current_name (LrgFileBrowser *self);

/**
 * lrg_file_browser_set_current_name:
 * @self: an #LrgFileBrowser
 * @name: (nullable): a suggested file name for
 *   %LRG_FILE_BROWSER_MODE_SAVE
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_set_current_name (LrgFileBrowser *self,
                                        const gchar    *name);

/**
 * lrg_file_browser_get_save_file:
 * @self: an #LrgFileBrowser
 *
 * The file %LRG_FILE_BROWSER_MODE_SAVE would write: the name field
 * resolved against the location ("~" and "/" paths allowed), with the
 * current filter's extension added when the name has none.
 *
 * Returns: (transfer full) (nullable): the file, or %NULL when the
 *   name is empty
 */
LRG_AVAILABLE_IN_ALL
GFile * lrg_file_browser_get_save_file (LrgFileBrowser *self);

/**
 * lrg_file_browser_get_n_entries:
 * @self: an #LrgFileBrowser
 *
 * Returns: the number of rows in the list (after hidden files, the mode
 *   and the filter are applied)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_file_browser_get_n_entries (LrgFileBrowser *self);

/**
 * lrg_file_browser_get_entry_info:
 * @self: an #LrgFileBrowser
 * @index: a row
 *
 * Returns: (transfer none) (nullable): the file info of row @index in
 *   list order, or %NULL when out of range
 */
LRG_AVAILABLE_IN_ALL
GFileInfo * lrg_file_browser_get_entry_info (LrgFileBrowser *self,
                                             guint           index);

/**
 * lrg_file_browser_get_entry_file:
 * @self: an #LrgFileBrowser
 * @index: a row
 *
 * Returns: (transfer full) (nullable): the file of row @index
 */
LRG_AVAILABLE_IN_ALL
GFile * lrg_file_browser_get_entry_file (LrgFileBrowser *self,
                                         guint           index);

/**
 * lrg_file_browser_get_cursor:
 * @self: an #LrgFileBrowser
 *
 * Returns: the keyboard cursor row, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_file_browser_get_cursor (LrgFileBrowser *self);

/**
 * lrg_file_browser_select_file:
 * @self: an #LrgFileBrowser
 * @file: a file inside the current location
 *
 * Selects @file and moves the cursor onto it, scrolling it into view.
 * While the folder is still loading the selection is applied when the
 * file shows up.
 *
 * Returns: %TRUE if @file is listed (or may still be, while loading)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_select_file (LrgFileBrowser *self,
                                       GFile          *file);

/**
 * lrg_file_browser_select_all:
 * @self: an #LrgFileBrowser
 *
 * Selects every row (Ctrl+A); only in
 * %LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE.
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_select_all (LrgFileBrowser *self);

/**
 * lrg_file_browser_unselect_all:
 * @self: an #LrgFileBrowser
 *
 * Clears the selection.
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_unselect_all (LrgFileBrowser *self);

/**
 * lrg_file_browser_get_selected_files:
 * @self: an #LrgFileBrowser
 *
 * Returns: (transfer full): a #GListModel of the selected #GFile items
 *   (folders included) in list order
 */
LRG_AVAILABLE_IN_ALL
GListModel * lrg_file_browser_get_selected_files (LrgFileBrowser *self);

/**
 * lrg_file_browser_get_selected_file:
 * @self: an #LrgFileBrowser
 *
 * Returns: (transfer full) (nullable): the first selected file
 */
LRG_AVAILABLE_IN_ALL
GFile * lrg_file_browser_get_selected_file (LrgFileBrowser *self);

/**
 * lrg_file_browser_add_place:
 * @self: an #LrgFileBrowser
 * @label: the name shown in the sidebar
 * @location: the folder it opens
 * @icon: an icon, e.g. %LRG_ICON_FOLDER or %LRG_ICON_STAR
 *
 * Adds an application place at the top of the sidebar: a game's save
 * folder, a mod folder, a project's assets. Adding a location that is
 * already an application place replaces its label and icon.
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_add_place (LrgFileBrowser *self,
                                 const gchar    *label,
                                 GFile          *location,
                                 LrgIcon         icon);

/**
 * lrg_file_browser_remove_place:
 * @self: an #LrgFileBrowser
 * @location: the folder of an application place
 *
 * Returns: %TRUE if a place was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_remove_place (LrgFileBrowser *self,
                                        GFile          *location);

/**
 * lrg_file_browser_get_n_places:
 * @self: an #LrgFileBrowser
 *
 * Returns: the number of sidebar places (application places, system
 *   places, bookmarks and mounts)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_file_browser_get_n_places (LrgFileBrowser *self);

/**
 * lrg_file_browser_get_place_label:
 * @self: an #LrgFileBrowser
 * @index: a place, in sidebar order
 *
 * Returns: (transfer none) (nullable): the place's label
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_file_browser_get_place_label (LrgFileBrowser *self,
                                                guint           index);

/**
 * lrg_file_browser_get_place_location:
 * @self: an #LrgFileBrowser
 * @index: a place, in sidebar order
 *
 * Returns: (transfer none) (nullable): the folder the place opens
 */
LRG_AVAILABLE_IN_ALL
GFile * lrg_file_browser_get_place_location (LrgFileBrowser *self,
                                             guint           index);

/**
 * lrg_file_browser_get_show_sidebar:
 * @self: an #LrgFileBrowser
 *
 * Returns: %TRUE if the places sidebar is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_get_show_sidebar (LrgFileBrowser *self);

/**
 * lrg_file_browser_set_show_sidebar:
 * @self: an #LrgFileBrowser
 * @show_sidebar: show the places sidebar (the default)
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_set_show_sidebar (LrgFileBrowser *self,
                                        gboolean        show_sidebar);

/**
 * lrg_file_browser_get_show_system_places:
 * @self: an #LrgFileBrowser
 *
 * Returns: %TRUE if home, user folders, the file system root, GTK
 *   bookmarks and mounts are offered as places
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_get_show_system_places (LrgFileBrowser *self);

/**
 * lrg_file_browser_set_show_system_places:
 * @self: an #LrgFileBrowser
 * @show_system_places: %FALSE keeps only the application places, e.g.
 *   for a game's save-slot picker
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_set_show_system_places (LrgFileBrowser *self,
                                              gboolean        show_system_places);

/**
 * lrg_file_browser_get_editing_location:
 * @self: an #LrgFileBrowser
 *
 * Returns: %TRUE while the path field replaces the breadcrumb
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_get_editing_location (LrgFileBrowser *self);

/**
 * lrg_file_browser_set_editing_location:
 * @self: an #LrgFileBrowser
 * @editing: show the path field (Ctrl+L, or typing "/" or "~" in the
 *   list) instead of the breadcrumb
 *
 * Entering edit mode fills the field with the current path and focuses
 * it. Enter navigates (or picks a typed file), Escape goes back to the
 * breadcrumb.
 */
LRG_AVAILABLE_IN_ALL
void lrg_file_browser_set_editing_location (LrgFileBrowser *self,
                                            gboolean        editing);

/**
 * lrg_file_browser_activate_cursor:
 * @self: an #LrgFileBrowser
 *
 * Does what Enter does on the cursor row: enters a folder, or emits
 * #LrgFileBrowser::file-activated for a file.
 *
 * Returns: %TRUE if there was a row to activate
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_file_browser_activate_cursor (LrgFileBrowser *self);

G_END_DECLS
