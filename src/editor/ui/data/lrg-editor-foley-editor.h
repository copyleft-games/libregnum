/* lrg-editor-foley-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorFoleyEditor: the Foley designer, the data editor of .rfoley
 * sound-effect patches (#LrgFoleyPatch).  A layer list, property grids
 * for the patch and the selected layer, a waveform of the render, presets
 * from the built-in kinds, preview on the audio device and export to any
 * audio format.  Every change is one undo step of the document; foley.*
 * IPC methods do the same edits.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../lrg-types.h"
#include "../../../audio/lrg-foley-patch.h"
#include "../../lrg-editor-document.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_FOLEY_EDITOR (lrg_editor_foley_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorFoleyEditor, lrg_editor_foley_editor, LRG, EDITOR_FOLEY_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_foley_editor_new:
 * @app: (nullable): the editor application
 * @document: a .rfoley document
 *
 * Returns: (transfer full): a new Foley designer
 */
LRG_AVAILABLE_IN_ALL
LrgEditorFoleyEditor * lrg_editor_foley_editor_new (LrgEditorApp      *app,
                                                    LrgEditorDocument *document);

/**
 * lrg_editor_foley_editor_install:
 * @app: the editor application
 *
 * Registers the foley.* IPC methods (once).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_foley_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_foley_editor_open:
 * @app: the editor application
 * @path: a .rfoley file, relative to the project or absolute
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer none) (nullable): the open (or newly opened) designer
 */
LRG_AVAILABLE_IN_ALL
LrgEditorFoleyEditor * lrg_editor_foley_editor_open (LrgEditorApp  *app,
                                                     const gchar   *path,
                                                     GError       **error);

/**
 * lrg_editor_foley_editor_create:
 * @app: (nullable): the editor application
 * @path: the new .rfoley file
 * @kind: (nullable): a built-in kind's nick ("footstep-stone", ...) to
 *   start from, or %NULL for a short tone
 * @error: (nullable): return location for an error
 *
 * Writes a new patch file (it must not exist).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_foley_editor_create (LrgEditorApp  *app,
                                         const gchar   *path,
                                         const gchar   *kind,
                                         GError       **error);

/**
 * lrg_editor_foley_editor_get_patch:
 * @self: an #LrgEditorFoleyEditor
 *
 * Returns: (transfer none) (nullable): the patch the document holds
 */
LRG_AVAILABLE_IN_ALL
LrgFoleyPatch * lrg_editor_foley_editor_get_patch (LrgEditorFoleyEditor *self);

/**
 * lrg_editor_foley_editor_get_selected_layer:
 * @self: an #LrgEditorFoleyEditor
 *
 * Returns: the selected layer, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_foley_editor_get_selected_layer (LrgEditorFoleyEditor *self);

/**
 * lrg_editor_foley_editor_select_layer:
 * @self: an #LrgEditorFoleyEditor
 * @index: a layer, or -1 for the patch settings
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_foley_editor_select_layer (LrgEditorFoleyEditor *self,
                                           gint                  index);

/**
 * lrg_editor_foley_editor_set_global:
 * @self: an #LrgEditorFoleyEditor
 * @name: a patch setting (lrg_foley_patch_set_global()) or "name"
 * @value: its number
 * @text: (nullable): the text of "name"
 * @gesture: a gesture id to merge undo steps, or 0
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success (one undo step)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_foley_editor_set_global (LrgEditorFoleyEditor  *self,
                                             const gchar           *name,
                                             gdouble                value,
                                             const gchar           *text,
                                             guint                  gesture,
                                             GError               **error);

/**
 * lrg_editor_foley_editor_add_layer:
 * @self: an #LrgEditorFoleyEditor
 * @type: the layer type
 * @error: (nullable): return location for an error
 *
 * Appends a layer and selects it.
 *
 * Returns: its index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_foley_editor_add_layer (LrgEditorFoleyEditor  *self,
                                        LrgFoleyLayerType      type,
                                        GError               **error);

/**
 * lrg_editor_foley_editor_remove_layer:
 * @self: an #LrgEditorFoleyEditor
 * @index: a layer
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_foley_editor_remove_layer (LrgEditorFoleyEditor  *self,
                                               guint                  index,
                                               GError               **error);

/**
 * lrg_editor_foley_editor_move_layer:
 * @self: an #LrgEditorFoleyEditor
 * @from: a layer
 * @to: its new index
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_foley_editor_move_layer (LrgEditorFoleyEditor  *self,
                                             guint                  from,
                                             guint                  to,
                                             GError               **error);

/**
 * lrg_editor_foley_editor_set_layer_param:
 * @self: an #LrgEditorFoleyEditor
 * @index: a layer
 * @name: a parameter of its type
 * @value: its value (clamped)
 * @gesture: a gesture id to merge undo steps, or 0
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_foley_editor_set_layer_param (LrgEditorFoleyEditor  *self,
                                                  guint                  index,
                                                  const gchar           *name,
                                                  gdouble                value,
                                                  guint                  gesture,
                                                  GError               **error);

/**
 * lrg_editor_foley_editor_load_kind:
 * @self: an #LrgEditorFoleyEditor
 * @kind: a built-in kind
 * @variant: its take
 * @error: (nullable): return location for an error
 *
 * Replaces the whole patch with the kind's recipe (one undo step), at the
 * patch's sample rate.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_foley_editor_load_kind (LrgEditorFoleyEditor  *self,
                                            LrgFoleyKind           kind,
                                            guint                  variant,
                                            GError               **error);

/**
 * lrg_editor_foley_editor_render:
 * @self: an #LrgEditorFoleyEditor
 * @n_frames: (out): the frames rendered
 * @error: (nullable): return location for an error
 *
 * The render the waveform shows (mono float at the patch rate).
 *
 * Returns: (transfer none) (array length=n_frames) (nullable): the
 *   samples, owned by the editor until the patch changes
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_editor_foley_editor_render (LrgEditorFoleyEditor  *self,
                                               gsize                 *n_frames,
                                               GError               **error);

/**
 * lrg_editor_foley_editor_export:
 * @self: an #LrgEditorFoleyEditor
 * @path: the audio file to write (its extension picks the format)
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_foley_editor_export (LrgEditorFoleyEditor  *self,
                                         const gchar           *path,
                                         GError               **error);

/**
 * lrg_editor_foley_editor_play:
 * @self: an #LrgEditorFoleyEditor
 * @error: (nullable): return location for an error
 *
 * Plays the render on the audio device (opened on first use).
 *
 * Returns: %TRUE when it plays
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_foley_editor_play (LrgEditorFoleyEditor  *self,
                                       GError               **error);

G_END_DECLS
