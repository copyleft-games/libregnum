/* lrg-property-grid.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPropertyGrid: a reflected property editor over one or more
 * LrgPropertySource objects, with multi-edit, sections, a filter,
 * pluggable editors and undo hooks. LrgPropertyRow is one row.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"
#include "lrg-container.h"
#include "lrg-expander.h"
#include "lrg-property-source.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgPropertyApplyMode
 * ========================================================================== */

/**
 * LrgPropertyApplyMode:
 * @LRG_PROPERTY_APPLY_DIRECT: the grid writes every edit to every
 *   source itself (and still emits the edit signals); Escape during a
 *   drag restores the values it captured at edit-begin
 * @LRG_PROPERTY_APPLY_DELEGATE: the grid only emits the edit signals;
 *   the application applies them (through its undo stack) and the
 *   grid follows #LrgPropertySource::property-changed
 *
 * Who applies edits made in an #LrgPropertyGrid.
 */
typedef enum
{
	LRG_PROPERTY_APPLY_DIRECT,
	LRG_PROPERTY_APPLY_DELEGATE
} LrgPropertyApplyMode;

/**
 * lrg_property_apply_mode_get_type:
 *
 * Returns: the #GType of #LrgPropertyApplyMode
 */
LRG_AVAILABLE_IN_ALL
GType lrg_property_apply_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PROPERTY_APPLY_MODE (lrg_property_apply_mode_get_type ())

/* ==========================================================================
 * Types
 * ========================================================================== */

#define LRG_TYPE_PROPERTY_GRID (lrg_property_grid_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPropertyGrid, lrg_property_grid, LRG, PROPERTY_GRID, LrgContainer)

#define LRG_TYPE_PROPERTY_ROW (lrg_property_row_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPropertyRow, lrg_property_row, LRG, PROPERTY_ROW, LrgContainer)

/**
 * LrgPropertyEditorFactory:
 * @row: the row the editor is for: its info, hint, value and edit API
 * @user_data: the data given at registration
 *
 * Creates the editor widget of a row. The factory reads the value with
 * lrg_property_row_get_value() and connects to #LrgPropertyRow::sync
 * (g_signal_connect_object() with the editor) to reload it whenever
 * the grid refreshes. When the user changes something it calls
 * lrg_property_row_begin_edit(), lrg_property_row_edit_value() (as
 * often as it likes; @interactive %TRUE while dragging) and
 * lrg_property_row_end_edit(), or lrg_property_row_apply_value() for a
 * one-shot change. Calls made while the row is syncing are ignored, so
 * signals the editor emits while being reloaded are harmless.
 *
 * Returns: (transfer full): the editor widget
 */
typedef LrgWidget * (*LrgPropertyEditorFactory) (LrgPropertyRow *row,
                                                 gpointer        user_data);

/**
 * LrgPropertyReferenceFunc:
 * @grid: the grid
 * @name: the property name
 * @hint: the reference hint, e.g. "node-ref" or "asset:texture"
 * @user_data: the data given to lrg_property_grid_set_reference_func()
 *
 * Lists the values a reference property can take (node GUIDs or
 * names, asset paths), for the drop-down of reference editors.
 *
 * Returns: (transfer full) (nullable) (array zero-terminated=1): the
 *   candidates, or %NULL for none
 */
typedef gchar ** (*LrgPropertyReferenceFunc) (LrgPropertyGrid *grid,
                                              const gchar     *name,
                                              const gchar     *hint,
                                              gpointer         user_data);

/* ==========================================================================
 * Editor registry
 * ========================================================================== */

/**
 * lrg_property_grid_register_editor:
 * @hint_or_type_name: a hint ("color", "asset:texture"), a hint kind
 *   ("asset") or a type name ("GrlColor", "gdouble", "GEnum")
 * @factory: (scope notified): creates the editor
 * @user_data: data for @factory
 * @destroy: (nullable): frees @user_data
 *
 * Registers an editor for every grid in the process, replacing an
 * earlier registration of the same key. A row looks for, in order: its
 * full hint, its hint kind, its value type name, then each parent type
 * up to the fundamental type ("GEnum", "gint", "GObject"); at each key
 * a per-grid editor (lrg_property_grid_add_editor()) wins over a
 * global one, which wins over the built-in one.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_register_editor (const gchar              *hint_or_type_name,
                                        LrgPropertyEditorFactory  factory,
                                        gpointer                  user_data,
                                        GDestroyNotify            destroy);

/**
 * lrg_property_grid_unregister_editor:
 * @hint_or_type_name: a key given to lrg_property_grid_register_editor()
 *
 * Removes a global registration (built-in editors stay).
 *
 * Returns: %TRUE if one was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_grid_unregister_editor (const gchar *hint_or_type_name);

/**
 * lrg_property_grid_add_editor:
 * @self: an #LrgPropertyGrid
 * @hint_or_type_name: a hint, hint kind or type name
 * @factory: (scope notified): creates the editor
 * @user_data: data for @factory
 * @destroy: (nullable): frees @user_data
 *
 * Registers an editor for this grid only. Existing rows keep their
 * editors until their schema changes; call lrg_property_grid_rebuild()
 * to apply it at once.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_add_editor (LrgPropertyGrid          *self,
                                   const gchar              *hint_or_type_name,
                                   LrgPropertyEditorFactory  factory,
                                   gpointer                  user_data,
                                   GDestroyNotify            destroy);

/* ==========================================================================
 * LrgPropertyGrid
 * ========================================================================== */

/**
 * lrg_property_grid_new:
 *
 * Creates an empty property grid. Give it sources with
 * lrg_property_grid_set_sources(), lrg_property_grid_set_source() or
 * lrg_property_grid_set_object(). Put it in an #LrgScrollBox when the
 * properties may not fit.
 *
 * Returns: (transfer full): a new #LrgPropertyGrid
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_property_grid_new (void);

/**
 * lrg_property_grid_set_sources:
 * @self: an #LrgPropertyGrid
 * @sources: (nullable) (element-type LrgPropertySource): the sources to
 *   edit together, or %NULL / empty for none
 *
 * Edits several sources at once. Only properties every source has,
 * with the same name and value type, are shown, in the first source's
 * order. Where the sources disagree a row shows "mixed" (an em dash,
 * an indeterminate check box) until an edit writes the new value to
 * all of them. When the new sources have the same schema as the old
 * ones the rows are kept (no rebuild, focus stays) and only refreshed.
 * Ignored while #LrgPropertyGrid:locked is set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_set_sources (LrgPropertyGrid *self,
                                    GPtrArray       *sources);

/**
 * lrg_property_grid_set_source:
 * @self: an #LrgPropertyGrid
 * @source: (nullable): the one source to edit, or %NULL for none
 *
 * lrg_property_grid_set_sources() with one source.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_set_source (LrgPropertyGrid   *self,
                                   LrgPropertySource *source);

/**
 * lrg_property_grid_set_object:
 * @self: an #LrgPropertyGrid
 * @object: (nullable): an object to edit through an
 *   #LrgObjectPropertySource, or %NULL for none
 *
 * Convenience for theme and data editors: wraps @object and edits it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_set_object (LrgPropertyGrid *self,
                                   GObject         *object);

/**
 * lrg_property_grid_get_n_sources:
 * @self: an #LrgPropertyGrid
 *
 * Returns: the number of sources edited
 */
LRG_AVAILABLE_IN_ALL
guint lrg_property_grid_get_n_sources (LrgPropertyGrid *self);

/**
 * lrg_property_grid_get_source:
 * @self: an #LrgPropertyGrid
 * @index: a source index
 *
 * Returns: (transfer none) (nullable): the source at @index
 */
LRG_AVAILABLE_IN_ALL
LrgPropertySource * lrg_property_grid_get_source (LrgPropertyGrid *self,
                                                  guint            index);

/**
 * lrg_property_grid_refresh:
 * @self: an #LrgPropertyGrid
 *
 * Re-reads every value, mixed flag and state from the sources (rows in
 * the middle of an edit keep what they show).
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_refresh (LrgPropertyGrid *self);

/**
 * lrg_property_grid_rebuild:
 * @self: an #LrgPropertyGrid
 *
 * Throws every row away and builds them again (after registering
 * editors, or when a source's schema changed).
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_rebuild (LrgPropertyGrid *self);

/**
 * lrg_property_grid_get_locked:
 * @self: an #LrgPropertyGrid
 *
 * Returns: %TRUE while the grid ignores lrg_property_grid_set_sources()
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_grid_get_locked (LrgPropertyGrid *self);

/**
 * lrg_property_grid_set_locked:
 * @self: an #LrgPropertyGrid
 * @locked: %TRUE to keep the current sources whatever is selected
 *
 * Locks the grid to its sources (an inspector's lock button).
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_set_locked (LrgPropertyGrid *self,
                                   gboolean         locked);

/**
 * lrg_property_grid_get_show_filter:
 * @self: an #LrgPropertyGrid
 *
 * Returns: %TRUE if the filter field is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_grid_get_show_filter (LrgPropertyGrid *self);

/**
 * lrg_property_grid_set_show_filter:
 * @self: an #LrgPropertyGrid
 * @show: %TRUE to show a filter field above the rows
 *
 * Shows or hides the filter field.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_set_show_filter (LrgPropertyGrid *self,
                                        gboolean         show);

/**
 * lrg_property_grid_get_filter_text:
 * @self: an #LrgPropertyGrid
 *
 * Returns: (transfer none): the filter, "" for none
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_property_grid_get_filter_text (LrgPropertyGrid *self);

/**
 * lrg_property_grid_set_filter_text:
 * @self: an #LrgPropertyGrid
 * @text: (nullable): show only rows whose label or name contains this
 *   (case-insensitive); %NULL or "" shows all
 *
 * Filters the rows; sections with no matching row are hidden. The
 * filter field, when shown, edits the same text.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_set_filter_text (LrgPropertyGrid *self,
                                        const gchar     *text);

/**
 * lrg_property_grid_get_label_width:
 * @self: an #LrgPropertyGrid
 *
 * Returns: the width of the label column
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_property_grid_get_label_width (LrgPropertyGrid *self);

/**
 * lrg_property_grid_set_label_width:
 * @self: an #LrgPropertyGrid
 * @width: width of the label column (default 120); at most 60% of a
 *   row is used for it
 *
 * Sets the label column width.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_set_label_width (LrgPropertyGrid *self,
                                        gfloat           width);

/**
 * lrg_property_grid_get_apply_mode:
 * @self: an #LrgPropertyGrid
 *
 * Returns: who applies edits
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyApplyMode lrg_property_grid_get_apply_mode (LrgPropertyGrid *self);

/**
 * lrg_property_grid_set_apply_mode:
 * @self: an #LrgPropertyGrid
 * @mode: %LRG_PROPERTY_APPLY_DIRECT or %LRG_PROPERTY_APPLY_DELEGATE
 *
 * Chooses who applies edits.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_set_apply_mode (LrgPropertyGrid      *self,
                                       LrgPropertyApplyMode  mode);

/**
 * lrg_property_grid_get_row:
 * @self: an #LrgPropertyGrid
 * @name: a property name
 *
 * Returns: (transfer none) (nullable): the row of @name
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyRow * lrg_property_grid_get_row (LrgPropertyGrid *self,
                                            const gchar     *name);

/**
 * lrg_property_grid_get_section:
 * @self: an #LrgPropertyGrid
 * @group: a group name
 *
 * Returns: (transfer none) (nullable): the expander of @group
 */
LRG_AVAILABLE_IN_ALL
LrgExpander * lrg_property_grid_get_section (LrgPropertyGrid *self,
                                             const gchar     *group);

/**
 * lrg_property_grid_get_group_expanded:
 * @self: an #LrgPropertyGrid
 * @group: a group name
 *
 * Returns: whether the section of @group is (or will be) open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_grid_get_group_expanded (LrgPropertyGrid *self,
                                               const gchar     *group);

/**
 * lrg_property_grid_set_group_expanded:
 * @self: an #LrgPropertyGrid
 * @group: a group name
 * @expanded: whether its section is open
 *
 * Opens or closes a section. The state is remembered per group name,
 * across source changes, so a section closed for one node stays closed
 * for the next.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_set_group_expanded (LrgPropertyGrid *self,
                                           const gchar     *group,
                                           gboolean         expanded);

/**
 * lrg_property_grid_set_reference_func:
 * @self: an #LrgPropertyGrid
 * @func: (nullable) (scope notified): lists candidate values of
 *   reference properties
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data
 *
 * Fills the drop-downs of node-ref and asset rows.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_set_reference_func (LrgPropertyGrid          *self,
                                           LrgPropertyReferenceFunc  func,
                                           gpointer                  user_data,
                                           GDestroyNotify            destroy);

/**
 * lrg_property_grid_set_reference_value:
 * @self: an #LrgPropertyGrid
 * @name: a property name
 * @value: the picked value (a string for node-ref and asset hints, an
 *   object for object-ref properties)
 *
 * Applies a value picked outside the grid (after
 * #LrgPropertyGrid::pick-requested, or from a drop) as one edit.
 *
 * Returns: %TRUE if the row exists, is writable and took the value
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_grid_set_reference_value (LrgPropertyGrid *self,
                                                const gchar     *name,
                                                const GValue    *value);

/**
 * lrg_property_grid_drop_reference:
 * @self: an #LrgPropertyGrid
 * @name: a property name
 * @payload_type: the dropped payload's type: "application/x-lrg-node"
 *   or "application/x-lrg-asset"
 * @value: the dropped value (a node GUID / asset path string, or an
 *   object)
 *
 * The drop hook of reference rows: accepts @value when @payload_type
 * matches the row's hint (nodes for node-ref and object-ref, assets
 * for asset) and applies it as one edit. Drop targets in the reference
 * editors call this; applications can call it from their own DnD code.
 *
 * Returns: %TRUE if the drop was accepted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_grid_drop_reference (LrgPropertyGrid *self,
                                           const gchar     *name,
                                           const gchar     *payload_type,
                                           const GValue    *value);

/**
 * lrg_property_grid_reset_property:
 * @self: an #LrgPropertyGrid
 * @name: a property name
 *
 * What the context menu's "Reset to default" does: emits
 * #LrgPropertyGrid::reset-requested and, in DIRECT mode, resets the
 * property on every source with lrg_property_source_reset().
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_grid_reset_property (LrgPropertyGrid *self,
                                       const gchar     *name);

/**
 * lrg_property_grid_copy_value:
 * @self: an #LrgPropertyGrid
 * @name: a property name
 *
 * What "Copy value" does: keeps the row's value for
 * lrg_property_grid_paste_value() and puts its text
 * (lrg_property_value_to_string()) on the system clipboard when a
 * window is open.
 *
 * Returns: %TRUE if a value was copied (not for mixed rows)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_grid_copy_value (LrgPropertyGrid *self,
                                       const gchar     *name);

/**
 * lrg_property_grid_paste_value:
 * @self: an #LrgPropertyGrid
 * @name: a property name
 *
 * What "Paste value" does: applies the copied value as one edit when
 * its type matches, otherwise parses the copied (or clipboard) text
 * with lrg_property_value_from_string().
 *
 * Returns: %TRUE if a value was pasted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_grid_paste_value (LrgPropertyGrid *self,
                                        const gchar     *name);

/**
 * lrg_property_grid_get_value_for_source:
 * @self: an #LrgPropertyGrid
 * @name: a property name
 * @index: a source index
 * @edited: the value of an #LrgPropertyGrid::edit-value emission
 * @out: (out caller-allocates): an uninitialised #GValue
 *
 * What an edit means for one source in multi-edit. For vectors only
 * the components the gesture changed are taken from @edited, the
 * others keep the source's own values (moving X of three nodes keeps
 * their Y and Z); every other type is @edited itself. DIRECT mode
 * applies this; DELEGATE handlers call it per source.
 *
 * Returns: %TRUE if @out was filled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_grid_get_value_for_source (LrgPropertyGrid *self,
                                                 const gchar     *name,
                                                 guint            index,
                                                 const GValue    *edited,
                                                 GValue          *out);

/**
 * lrg_property_grid_get_filter_input:
 * @self: an #LrgPropertyGrid
 *
 * Returns: (transfer none): the filter #LrgTextInput (hidden unless
 *   #LrgPropertyGrid:show-filter)
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_property_grid_get_filter_input (LrgPropertyGrid *self);

/* ==========================================================================
 * LrgPropertyRow
 * ========================================================================== */

/**
 * lrg_property_row_get_grid:
 * @self: an #LrgPropertyRow
 *
 * Returns: (transfer none) (nullable): the grid the row belongs to
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_property_row_get_grid (LrgPropertyRow *self);

/**
 * lrg_property_row_get_info:
 * @self: an #LrgPropertyRow
 *
 * Returns: (transfer none): the property description (the first
 *   source's; for array elements a synthetic one)
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyInfo * lrg_property_row_get_info (LrgPropertyRow *self);

/**
 * lrg_property_row_get_name:
 * @self: an #LrgPropertyRow
 *
 * Returns: (transfer none): the property name ("name[2]" for an array
 *   element)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_property_row_get_name (LrgPropertyRow *self);

/**
 * lrg_property_row_get_hint:
 * @self: an #LrgPropertyRow
 *
 * Returns: (transfer none) (nullable): the editor hint
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_property_row_get_hint (LrgPropertyRow *self);

/**
 * lrg_property_row_get_editor:
 * @self: an #LrgPropertyRow
 *
 * Returns: (transfer none) (nullable): the editor widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_property_row_get_editor (LrgPropertyRow *self);

/**
 * lrg_property_row_get_value:
 * @self: an #LrgPropertyRow
 * @value: (out caller-allocates): an uninitialised #GValue
 *
 * The value shown: the first source's (while editing, the last edited
 * value).
 *
 * Returns: %TRUE if @value was filled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_row_get_value (LrgPropertyRow *self,
                                     GValue         *value);

/**
 * lrg_property_row_get_mixed:
 * @self: an #LrgPropertyRow
 *
 * Returns: %TRUE when the sources hold different values
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_row_get_mixed (LrgPropertyRow *self);

/**
 * lrg_property_row_get_state:
 * @self: an #LrgPropertyRow
 *
 * Returns: the state flags: OVERRIDDEN, ANIMATED and INVALID when any
 *   source reports them, DEFAULT when all do
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyState lrg_property_row_get_state (LrgPropertyRow *self);

/**
 * lrg_property_row_get_read_only:
 * @self: an #LrgPropertyRow
 *
 * Returns: %TRUE when the value cannot be edited
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_row_get_read_only (LrgPropertyRow *self);

/**
 * lrg_property_row_is_syncing:
 * @self: an #LrgPropertyRow
 *
 * Returns: %TRUE while #LrgPropertyRow::sync is being emitted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_row_is_syncing (LrgPropertyRow *self);

/**
 * lrg_property_row_get_editing:
 * @self: an #LrgPropertyRow
 *
 * Returns: %TRUE between begin and end of an edit gesture
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_row_get_editing (LrgPropertyRow *self);

/**
 * lrg_property_row_set_shows_mixed:
 * @self: an #LrgPropertyRow
 * @shows_mixed: %TRUE if the editor displays "mixed" itself
 *
 * Called by editor factories whose editor shows mixed values on its
 * own (an em dash, an indeterminate state). Otherwise the row paints a
 * veil with an em dash over the editor while the value is mixed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_row_set_shows_mixed (LrgPropertyRow *self,
                                       gboolean        shows_mixed);

/**
 * lrg_property_row_begin_edit:
 * @self: an #LrgPropertyRow
 *
 * Starts an edit gesture (a drag, a typing session, a popup): captures
 * the sources' values and emits #LrgPropertyGrid::edit-begin. Calling
 * it again inside a gesture returns the same id.
 *
 * Returns: the gesture id, 0 if the row is read-only or syncing
 */
LRG_AVAILABLE_IN_ALL
guint lrg_property_row_begin_edit (LrgPropertyRow *self);

/**
 * lrg_property_row_edit_value:
 * @self: an #LrgPropertyRow
 * @value: the new value; converted to the property type when it is of
 *   another, transformable type
 * @interactive: %TRUE while the gesture goes on (drag frames), %FALSE
 *   for a final value
 *
 * Applies a value inside the gesture (opening one when none is open):
 * emits #LrgPropertyGrid::edit-value and, in DIRECT mode, writes the
 * value to every source.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_row_edit_value (LrgPropertyRow *self,
                                  const GValue   *value,
                                  gboolean        interactive);

/**
 * lrg_property_row_end_edit:
 * @self: an #LrgPropertyRow
 * @commit: %FALSE to cancel: in DIRECT mode the captured values are
 *   written back
 *
 * Ends the gesture: emits #LrgPropertyGrid::edit-end and reloads the
 * editor from the sources.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_row_end_edit (LrgPropertyRow *self,
                                gboolean        commit);

/**
 * lrg_property_row_apply_value:
 * @self: an #LrgPropertyRow
 * @value: the new value
 *
 * A one-shot edit: begin, one non-interactive value, end.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_row_apply_value (LrgPropertyRow *self,
                                   const GValue   *value);

/**
 * lrg_property_row_get_parent_row:
 * @self: an #LrgPropertyRow
 *
 * Returns: (transfer none) (nullable): for an array element row, the
 *   array's row; otherwise %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyRow * lrg_property_row_get_parent_row (LrgPropertyRow *self);

/**
 * lrg_property_row_get_element_index:
 * @self: an #LrgPropertyRow
 *
 * Returns: the element index of an array element row, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_property_row_get_element_index (LrgPropertyRow *self);

/**
 * lrg_property_row_get_n_elements:
 * @self: an #LrgPropertyRow of an array property
 *
 * Returns: the number of elements shown
 */
LRG_AVAILABLE_IN_ALL
guint lrg_property_row_get_n_elements (LrgPropertyRow *self);

/**
 * lrg_property_row_get_element_row:
 * @self: an #LrgPropertyRow of an array property
 * @index: an element index
 *
 * Returns: (transfer none) (nullable): the row editing element @index
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyRow * lrg_property_row_get_element_row (LrgPropertyRow *self,
                                                   guint           index);

/**
 * lrg_property_row_add_element:
 * @self: an #LrgPropertyRow of an array property
 *
 * Appends a default element (what the "+" button does), as one edit.
 *
 * Returns: %TRUE if an element was added
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_row_add_element (LrgPropertyRow *self);

/**
 * lrg_property_row_remove_element:
 * @self: an #LrgPropertyRow of an array property
 * @index: an element index
 *
 * Removes an element (the element's "x" button), as one edit.
 *
 * Returns: %TRUE if an element was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_row_remove_element (LrgPropertyRow *self,
                                          guint           index);

/**
 * lrg_property_row_move_element:
 * @self: an #LrgPropertyRow of an array property
 * @from: an element index
 * @to: its new index
 *
 * Reorders the elements (the up and down buttons), as one edit.
 *
 * Returns: %TRUE if the element moved
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_row_move_element (LrgPropertyRow *self,
                                        guint           from,
                                        guint           to);

G_END_DECLS
