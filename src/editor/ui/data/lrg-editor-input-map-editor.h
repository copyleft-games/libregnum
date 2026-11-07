/* lrg-editor-input-map-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorInputMapEditor: the data editor of input maps (LrgInputMap
 * YAML files). A table lists the actions with their bindings as chips; the
 * selected binding is edited in a property grid, and "Capture" records the
 * next key or mouse button pressed.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_INPUT_MAP_EDITOR (lrg_editor_input_map_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorInputMapEditor, lrg_editor_input_map_editor, LRG, EDITOR_INPUT_MAP_EDITOR,
                      LrgEditorDataEditor)

/**
 * lrg_editor_input_map_editor_new:
 * @app: (nullable): the editor application
 * @document: an input map document
 *
 * Creates the editor; lrg_editor_data_editor_open() is the usual way.
 *
 * Returns: (transfer full): a new #LrgEditorInputMapEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorInputMapEditor * lrg_editor_input_map_editor_new (LrgEditorApp      *app,
                                                           LrgEditorDocument *document);

/**
 * lrg_editor_input_map_editor_select:
 * @self: an #LrgEditorInputMapEditor
 * @action: (nullable): an action name, %NULL to clear the selection
 * @slot: a binding index, or -1 for the action itself
 *
 * Selects an action (and one of its bindings) and shows it in the grid.
 *
 * Returns: %FALSE when @action or @slot does not exist
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_input_map_editor_select (LrgEditorInputMapEditor *self,
                                             const gchar             *action,
                                             gint                     slot);

/**
 * lrg_editor_input_map_editor_get_selected_action:
 * @self: an #LrgEditorInputMapEditor
 *
 * Returns: (nullable): the selected action
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_input_map_editor_get_selected_action (LrgEditorInputMapEditor *self);

/**
 * lrg_editor_input_map_editor_get_selected_slot:
 * @self: an #LrgEditorInputMapEditor
 *
 * Returns: the selected binding index, -1 when the action itself is selected
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_input_map_editor_get_selected_slot (LrgEditorInputMapEditor *self);

/**
 * lrg_editor_input_map_editor_add_action:
 * @self: an #LrgEditorInputMapEditor
 * @name: (nullable): the new action's name, %NULL for a free "action_N"
 * @error: return location for a #GError
 *
 * Appends an action with no bindings and selects it.
 *
 * Returns: (transfer none) (nullable): the name used, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_input_map_editor_add_action (LrgEditorInputMapEditor  *self,
                                                      const gchar              *name,
                                                      GError                  **error);

/**
 * lrg_editor_input_map_editor_remove_selected:
 * @self: an #LrgEditorInputMapEditor
 *
 * Removes the selected binding, or the selected action when no binding is
 * selected.
 *
 * Returns: %TRUE when something was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_input_map_editor_remove_selected (LrgEditorInputMapEditor *self);

/**
 * lrg_editor_input_map_editor_start_capture:
 * @self: an #LrgEditorInputMapEditor
 * @action: the action
 * @slot: the binding to replace, or -1 to append a new one
 *
 * Enters "press a key or button" mode: the next key press (with its
 * modifiers) or mouse button press inside the editor becomes the binding;
 * Escape cancels.
 *
 * Returns: %FALSE when @action does not exist
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_input_map_editor_start_capture (LrgEditorInputMapEditor *self,
                                                    const gchar             *action,
                                                    gint                     slot);

/**
 * lrg_editor_input_map_editor_cancel_capture:
 * @self: an #LrgEditorInputMapEditor
 *
 * Leaves capture mode without changing anything.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_input_map_editor_cancel_capture (LrgEditorInputMapEditor *self);

/**
 * lrg_editor_input_map_editor_get_capturing:
 * @self: an #LrgEditorInputMapEditor
 *
 * Returns: whether the editor waits for a key or button
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_input_map_editor_get_capturing (LrgEditorInputMapEditor *self);

/**
 * lrg_editor_input_map_editor_describe_binding:
 * @self: an #LrgEditorInputMapEditor
 * @action: an action name
 * @slot: a binding index
 *
 * Returns: (transfer full) (nullable): the chip text of the binding
 *   ("Ctrl+S", "Mouse Left", "Pad A", "Left Stick X+")
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_input_map_editor_describe_binding (LrgEditorInputMapEditor *self,
                                                      const gchar             *action,
                                                      gint                     slot);

/**
 * lrg_editor_input_map_editor_get_chip_rect:
 * @self: an #LrgEditorInputMapEditor
 * @action: an action name
 * @slot: a binding index, or the number of bindings for the "+" chip
 * @x: (out) (optional): left, canvas coordinates
 * @y: (out) (optional): top
 * @width: (out) (optional): width
 * @height: (out) (optional): height
 *
 * Where a binding chip is drawn (for tests and automation).
 *
 * Returns: %FALSE when the action's row is scrolled out of view
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_input_map_editor_get_chip_rect (LrgEditorInputMapEditor *self,
                                                    const gchar             *action,
                                                    gint                     slot,
                                                    gfloat                  *x,
                                                    gfloat                  *y,
                                                    gfloat                  *width,
                                                    gfloat                  *height);

G_END_DECLS
