/* lrg-editor-data-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorDataEditor: the base panel of the lrgeditor data editors.
 *
 * One data editor edits one #LrgEditorDocument (an input map, a locale, a
 * sound bank, a theme, a HUD or UI layout, a dialog tree or a procgen
 * dungeon). The base draws everything the eight editors share: a toolbar
 * (title with a dirty dot, Save, Revert, Validate, an issue count chip and
 * room for the subclass's own buttons), an external-change banner (Reload /
 * Keep), the content area of the subclass and a collapsible issues list
 * whose rows reveal the YAML path of an issue in the subclass. Ctrl+Z and
 * Ctrl+Shift+Z inside the panel go to the document's own undo stack, never
 * the level's.
 *
 * Subclasses register per format with lrg_editor_data_editor_register_type()
 * and fill the content area from their constructed() (the document and
 * the app are set by then), then keep it in sync from
 * #LrgEditorDataEditorClass.refresh. lrg_editor_data_editor_open() picks the
 * subclass by lrg_editor_document_detect_format() and docks it as a tab
 * with the id "data:<absolute path>" next to the code panel.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../ui/lrg-container.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../lrg-editor-document.h"
#include "../lrg-editor-app.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_DATA_EDITOR (lrg_editor_data_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgEditorDataEditor, lrg_editor_data_editor, LRG, EDITOR_DATA_EDITOR, LrgContainer)

/**
 * LRG_EDITOR_DATA_EDITOR_PANEL_PREFIX:
 *
 * The prefix of the dock panel id of a data editor; the rest of the id is
 * the absolute path of the document ("data:/home/me/game/data/input.yaml").
 */
#define LRG_EDITOR_DATA_EDITOR_PANEL_PREFIX "data:"

/**
 * LrgEditorDataEditorClass:
 * @parent_class: the parent class
 * @refresh: rebuild or update the content after the document changed.
 *   @path is the YAML path that changed, or "" after a reload, an undo of
 *   a structural edit or anything else that may have touched every path.
 *   Called by the base for every #LrgEditorDocument::changed emission; the
 *   default does nothing.
 * @reveal: the class handler of #LrgEditorDataEditor::reveal: select and
 *   scroll to the widget showing @path (a YAML path, "" for the root) or,
 *   when the subclass shows text, to @line (1-based, 0 when unknown). The
 *   default does nothing.
 * @validate: append subclass-specific #LrgEditorDocumentIssue items to
 *   @issues after the document's own validation ran (for example keys
 *   missing against the reference locale). The default does nothing.
 * @get_state: (nullable): return a short human-readable line describing the
 *   subclass's view state (selected row, active tab) for the IPC method
 *   data.list and for tests; the default returns %NULL.
 * @save: write the document (and any companion documents the subclass
 *   edits, e.g. the other locales of a locale grid); the default saves the
 *   document.
 * @undo: undo the last edit; the default undoes the document. Subclasses
 *   editing several documents undo the one edited last.
 * @redo: redo the last undone edit; the default redoes the document.
 * @get_dirty: whether anything is unsaved (the dirty dot, the close
 *   prompt); the default is #LrgEditorDocument:dirty.
 *
 * The class structure of #LrgEditorDataEditor.
 */
struct _LrgEditorDataEditorClass
{
	LrgContainerClass parent_class;

	/*< public >*/
	void    (*refresh)   (LrgEditorDataEditor *self,
	                      const gchar         *path);
	void    (*reveal)    (LrgEditorDataEditor *self,
	                      const gchar         *path,
	                      guint                line);
	void    (*validate)  (LrgEditorDataEditor *self,
	                      GPtrArray           *issues);
	gchar * (*get_state) (LrgEditorDataEditor *self);
	gboolean (*save)     (LrgEditorDataEditor  *self,
	                      GError              **error);
	gboolean (*undo)     (LrgEditorDataEditor *self);
	gboolean (*redo)     (LrgEditorDataEditor *self);
	gboolean (*get_dirty) (LrgEditorDataEditor *self);

	/*< private >*/
	gpointer _reserved[4];
};

/* ==========================================================================
 * Registry and opening
 * ========================================================================== */

/**
 * lrg_editor_data_editor_register_type:
 * @format: the document format
 * @type: a #GType deriving from #LrgEditorDataEditor
 *
 * Makes @type the editor of @format, replacing an earlier registration.
 * The built-in editors are registered on the first call of any registry or
 * open function; call this afterwards to override one.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_data_editor_register_type (LrgEditorDocumentFormat format,
                                           GType                   type);

/**
 * lrg_editor_data_editor_lookup_type:
 * @format: the document format
 *
 * Returns: the editor type registered for @format, or %G_TYPE_INVALID
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_data_editor_lookup_type (LrgEditorDocumentFormat format);

/**
 * lrg_editor_data_editor_install:
 * @app: the editor application
 *
 * Registers the data.* IPC methods and the data.* actions on @app. Safe to
 * call more than once; lrg_editor_data_editor_open() and the assets panel
 * call it, so embedders rarely need to.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_data_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_data_editor_can_open:
 * @path: a file path
 *
 * Whether @path is a data format with a registered editor: a ".yaml" or
 * ".yml" file whose name and content lrg_editor_document_detect_format()
 * recognises.
 *
 * Returns: %TRUE when lrg_editor_data_editor_open() would pick an editor
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_data_editor_can_open (const gchar *path);

/**
 * lrg_editor_data_editor_open:
 * @app: the editor application
 * @path: the data file, absolute or relative to the project directory
 * @error: return location for a #GError
 *
 * Opens @path in the editor of its format and presents its tab, docked
 * next to the code panel (the center stack when there is none). A file that
 * is already open presents its existing tab.
 *
 * Returns: (transfer none) (nullable): the editor, or %NULL with @error set
 *   (unknown format, unreadable or unparsable file)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDataEditor * lrg_editor_data_editor_open (LrgEditorApp  *app,
                                                   const gchar   *path,
                                                   GError       **error);

/**
 * lrg_editor_data_editor_find:
 * @app: the editor application
 * @path: (nullable): a file path, or %NULL for the focused data editor (the
 *   one that last had keyboard focus or was last opened)
 *
 * Returns: (transfer none) (nullable): the open editor of @path
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDataEditor * lrg_editor_data_editor_find (LrgEditorApp *app,
                                                   const gchar  *path);

/**
 * lrg_editor_data_editor_list:
 * @app: the editor application
 *
 * Returns: (transfer container) (element-type LrgEditorDataEditor): the
 *   open data editors in the order they were opened
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_editor_data_editor_list (LrgEditorApp *app);

/* ==========================================================================
 * The panel
 * ========================================================================== */

/**
 * lrg_editor_data_editor_get_app:
 * @self: a #LrgEditorDataEditor
 *
 * Returns: (transfer none) (nullable): the application (weak)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorApp * lrg_editor_data_editor_get_app (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_get_document:
 * @self: a #LrgEditorDataEditor
 *
 * Returns: (transfer none): the edited document
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDocument * lrg_editor_data_editor_get_document (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_get_path:
 * @self: a #LrgEditorDataEditor
 *
 * Returns: (nullable): the absolute path of the document
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_data_editor_get_path (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_get_panel_id:
 * @self: a #LrgEditorDataEditor
 *
 * Returns: the dock panel id, "data:" followed by the absolute path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_data_editor_get_panel_id (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_get_title:
 * @self: a #LrgEditorDataEditor
 *
 * Returns: the tab title: the file name plus " •" while dirty
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_data_editor_get_title (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_get_dirty:
 * @self: a #LrgEditorDataEditor
 *
 * Returns: whether the editor has unsaved changes (see
 *   #LrgEditorDataEditorClass.get_dirty)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_data_editor_get_dirty (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_sync_title:
 * @self: a #LrgEditorDataEditor
 *
 * Updates the title, the dirty dot and the Save button. The base calls it
 * whenever its document changes; subclasses with companion documents call
 * it when one of those changes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_data_editor_sync_title (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_set_content:
 * @self: a #LrgEditorDataEditor
 * @content: (nullable): the subclass's widget, filling the area between the
 *   toolbar and the issues list
 *
 * Sets the content area. Subclasses call this once from constructed().
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_data_editor_set_content (LrgEditorDataEditor *self,
                                         LrgWidget           *content);

/**
 * lrg_editor_data_editor_get_content:
 * @self: a #LrgEditorDataEditor
 *
 * Returns: (transfer none) (nullable): the content widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_editor_data_editor_get_content (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_add_tool:
 * @self: a #LrgEditorDataEditor
 * @tool: a widget (usually an #LrgButton) for the right end of the toolbar
 *
 * Appends a subclass tool to the toolbar, after Validate.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_data_editor_add_tool (LrgEditorDataEditor *self,
                                      LrgWidget           *tool);

/**
 * lrg_editor_data_editor_new_gesture:
 * @self: a #LrgEditorDataEditor
 *
 * Hands out a fresh non-zero gesture id for the edits of one user gesture
 * (a slider drag, a colour pick) so they merge into one undo step, see
 * lrg_editor_document_set_string().
 *
 * Returns: a gesture id, unique within this process
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_data_editor_new_gesture (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_report_error:
 * @self: a #LrgEditorDataEditor
 * @error: (nullable): the error of a failed edit
 *
 * Shows @error as an error toast of the editor (and logs it). Subclasses
 * call it when an edit through the document fails. Does nothing for %NULL.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_data_editor_report_error (LrgEditorDataEditor *self,
                                          const GError        *error);

/* --- the commands of the toolbar --- */

/**
 * lrg_editor_data_editor_save:
 * @self: a #LrgEditorDataEditor
 * @error: return location for a #GError
 *
 * Saves (#LrgEditorDataEditorClass.save) and validates again.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_data_editor_save (LrgEditorDataEditor  *self,
                                      GError              **error);

/**
 * lrg_editor_data_editor_revert:
 * @self: a #LrgEditorDataEditor
 * @error: return location for a #GError
 *
 * Throws away unsaved changes and reloads the file (no prompt; the Revert
 * button asks first when the document is dirty). Hides the external-change
 * banner.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_data_editor_revert (LrgEditorDataEditor  *self,
                                        GError              **error);

/**
 * lrg_editor_data_editor_validate:
 * @self: a #LrgEditorDataEditor
 *
 * Validates the document (the document's checks, then
 * #LrgEditorDataEditorClass.validate) and refreshes the issues list and
 * the issue chip.
 *
 * Returns: the number of issues with error severity
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_data_editor_validate (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_get_issues:
 * @self: a #LrgEditorDataEditor
 *
 * Returns: (transfer none) (element-type LrgEditorDocumentIssue): the issues
 *   of the last validation; the array stays the same object, refilled by
 *   every validation
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_data_editor_get_issues (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_activate_issue:
 * @self: a #LrgEditorDataEditor
 * @index: an index into lrg_editor_data_editor_get_issues()
 *
 * Does what clicking the issue row does: emits #LrgEditorDataEditor::reveal
 * with the issue's path and line.
 *
 * Returns: %FALSE when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_data_editor_activate_issue (LrgEditorDataEditor *self,
                                                guint                index);

/**
 * lrg_editor_data_editor_reveal:
 * @self: a #LrgEditorDataEditor
 * @path: a YAML path
 * @line: a 1-based line, or 0 to look it up from @path
 *
 * Emits #LrgEditorDataEditor::reveal.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_data_editor_reveal (LrgEditorDataEditor *self,
                                    const gchar         *path,
                                    guint                line);

/**
 * lrg_editor_data_editor_undo:
 * @self: a #LrgEditorDataEditor
 *
 * Undoes the last edit of the document (not of the level), see
 * #LrgEditorDataEditorClass.undo.
 *
 * Returns: %TRUE when something was undone
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_data_editor_undo (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_redo:
 * @self: a #LrgEditorDataEditor
 *
 * Redoes the last undone edit of the document.
 *
 * Returns: %TRUE when something was redone
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_data_editor_redo (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_get_banner_visible:
 * @self: a #LrgEditorDataEditor
 *
 * Returns: whether the "changed on disk" banner is showing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_data_editor_get_banner_visible (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_check_disk:
 * @self: a #LrgEditorDataEditor
 *
 * Compares the file with what was last read or written (what the file
 * monitor does on its own): a clean document reloads silently, a dirty one
 * shows the banner.
 *
 * Returns: %TRUE when the file changed on disk
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_data_editor_check_disk (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_keep_mine:
 * @self: a #LrgEditorDataEditor
 *
 * The banner's Keep button: hides the banner and keeps the edits (the next
 * save overwrites the file).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_data_editor_keep_mine (LrgEditorDataEditor *self);

/**
 * lrg_editor_data_editor_close:
 * @self: a #LrgEditorDataEditor
 * @force: close without asking even when dirty
 *
 * Closes the tab. A dirty document without @force asks Save / Don't Save /
 * Cancel first and returns %FALSE; the tab closes once answered.
 *
 * Returns: %TRUE when the tab was closed right away
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_data_editor_close (LrgEditorDataEditor *self,
                                       gboolean             force);

/**
 * lrg_editor_data_editor_get_state:
 * @self: a #LrgEditorDataEditor
 *
 * Returns: (transfer full) (nullable): the subclass's view state line, see
 *   #LrgEditorDataEditorClass.get_state
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_data_editor_get_state (LrgEditorDataEditor *self);

/* ==========================================================================
 * LrgEditorDataSource: document fields as an LrgPropertySource
 * ========================================================================== */

#define LRG_TYPE_EDITOR_DATA_SOURCE (lrg_editor_data_source_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorDataSource, lrg_editor_data_source, LRG, EDITOR_DATA_SOURCE, GObject)

/**
 * LrgEditorDataSourceWriteFunc:
 * @source: the source
 * @name: the field being written
 * @value: (inout): the value about to be written; may be changed in place
 *   (same type)
 * @user_data: data given to lrg_editor_data_source_set_write_func()
 *
 * Rewrites a value before it reaches the document, e.g. an asset path
 * dropped from the assets panel made relative to a sound bank's folder.
 *
 * Returns: %FALSE to stop the write (refused, or done by the hook
 *   itself through the document)
 */
typedef gboolean (*LrgEditorDataSourceWriteFunc) (LrgEditorDataSource *source,
                                                  const gchar         *name,
                                                  GValue              *value,
                                                  gpointer             user_data);

/**
 * LrgEditorDataSourceReadFunc:
 * @source: the source
 * @name: the field being read
 * @value: an initialised #GValue of the field's type to fill
 * @user_data: data given to lrg_editor_data_source_set_read_func()
 *
 * Supplies a field's value instead of reading the scalar at its path
 * (e.g. a mapping key's name, or a flag that is membership in a YAML
 * sequence).
 *
 * Returns: %TRUE when @value was filled, %FALSE to read the path as usual
 */
typedef gboolean (*LrgEditorDataSourceReadFunc) (LrgEditorDataSource *source,
                                                 const gchar         *name,
                                                 GValue              *value,
                                                 gpointer             user_data);

/**
 * lrg_editor_data_source_new:
 * @editor: the data editor whose document the fields live in
 *
 * Creates an #LrgPropertySource whose properties are YAML paths of the
 * editor's document. Every write is one undoable document command; writes
 * between #LrgPropertyGrid::edit-begin and ::edit-end of a grid bound with
 * lrg_editor_data_source_bind_grid() share a gesture id, so a drag or a
 * typing session is one undo step. A field whose path is missing reads as
 * its default and is reported as %LRG_PROPERTY_STATE_DEFAULT; "Reset"
 * removes the key.
 *
 * Returns: (transfer full): a new #LrgEditorDataSource
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDataSource * lrg_editor_data_source_new (LrgEditorDataEditor *editor);

/**
 * lrg_editor_data_source_add_field:
 * @self: an #LrgEditorDataSource
 * @name: the property name (unique in this source)
 * @path: the YAML path of the value in the document
 * @type: %G_TYPE_STRING, %G_TYPE_BOOLEAN, %G_TYPE_INT64 or %G_TYPE_DOUBLE
 * @label: (nullable): the row label (defaults to @name)
 * @group: (nullable): the section of the grid
 * @hint: (nullable): the editor hint ("color", "asset", "range:0:1:0.05",
 *   "enum-strings:a,b,c", "multiline", ...)
 * @default_text: (nullable): the default value as YAML text, used while
 *   @path is missing
 *
 * Appends a field. @name must be a valid #GParamSpec name (letters,
 * digits, '-' and '_', starting with a letter). Rebuild the grid showing
 * the source afterwards (lrg_property_grid_rebuild()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_data_source_add_field (LrgEditorDataSource *self,
                                       const gchar         *name,
                                       const gchar         *path,
                                       GType                type,
                                       const gchar         *label,
                                       const gchar         *group,
                                       const gchar         *hint,
                                       const gchar         *default_text);

/**
 * lrg_editor_data_source_clear:
 * @self: an #LrgEditorDataSource
 *
 * Removes every field.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_data_source_clear (LrgEditorDataSource *self);

/**
 * lrg_editor_data_source_get_n_fields:
 * @self: an #LrgEditorDataSource
 *
 * Returns: the number of fields
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_data_source_get_n_fields (LrgEditorDataSource *self);

/**
 * lrg_editor_data_source_get_field_path:
 * @self: an #LrgEditorDataSource
 * @name: a field name
 *
 * Returns: (nullable): the YAML path of @name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_data_source_get_field_path (LrgEditorDataSource *self,
                                                     const gchar         *name);

/**
 * lrg_editor_data_source_find_field:
 * @self: an #LrgEditorDataSource
 * @path: a YAML path
 *
 * Returns: (nullable): the name of the field showing @path or its closest
 *   ancestor among the fields
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_data_source_find_field (LrgEditorDataSource *self,
                                                 const gchar         *path);

/**
 * lrg_editor_data_source_document_changed:
 * @self: an #LrgEditorDataSource
 * @path: the path from #LrgEditorDocument::changed ("" for everything)
 *
 * Emits #LrgPropertySource::property-changed for the fields at or below
 * @path (or whose path lies inside it). Call it from
 * #LrgEditorDataEditorClass.refresh.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_data_source_document_changed (LrgEditorDataSource *self,
                                              const gchar         *path);

/**
 * lrg_editor_data_source_set_write_func:
 * @self: an #LrgEditorDataSource
 * @func: (nullable) (scope notified) (closure user_data): the rewrite hook
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data
 *
 * Installs a hook that sees (and may rewrite) every value before it is
 * written.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_data_source_set_write_func (LrgEditorDataSource          *self,
                                            LrgEditorDataSourceWriteFunc  func,
                                            gpointer                      user_data,
                                            GDestroyNotify                destroy);

/**
 * lrg_editor_data_source_set_read_func:
 * @self: an #LrgEditorDataSource
 * @func: (nullable) (scope notified) (closure user_data): the read hook
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data
 *
 * Installs a hook that may supply field values. A field the hook answers
 * is reported as %LRG_PROPERTY_STATE_NONE.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_data_source_set_read_func (LrgEditorDataSource         *self,
                                           LrgEditorDataSourceReadFunc  func,
                                           gpointer                     user_data,
                                           GDestroyNotify               destroy);

/**
 * lrg_editor_data_source_bind_grid:
 * @self: an #LrgEditorDataSource
 * @grid: an #LrgPropertyGrid showing this source
 *
 * Follows @grid's edit gestures so each one becomes a single undo step of
 * the document (a cancelled gesture is undone). Call once per grid, after
 * lrg_property_grid_set_source().
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_data_source_bind_grid (LrgEditorDataSource *self,
                                       LrgPropertyGrid     *grid);

G_END_DECLS
