/* lrg-editor-hud-layout-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorHudLayoutEditor: the data editor of #LrgHudLayout files.
 *
 * The content is a stage and a property grid side by side. The stage
 * draws a virtual game screen (1280 x 720 by default, cycled from the
 * toolbar) split into the thirds that name the nine #LrgHudAnchor points,
 * and every piece of the file's `pieces:` mapping as a rectangle placed
 * exactly the way the game places it (lrg_hud_layout_resolve()). Pieces
 * are dragged to move them: on release the anchor becomes the third the
 * piece's centre lies in and the offset follows, the rule of
 * lrg_hud_layout_place_rect(), so the piece keeps its place from that
 * edge when the game's screen changes size. The corner handles resize a
 * piece: the format stores no size, so the size is the editor's preview
 * size, and the offset is rewritten so the piece's anchor point stays put
 * (the anchor is respected). Locked pieces do not move, hidden pieces are
 * drawn faded. The grid edits the selected piece's anchor (the piece stays
 * where it is: the offset is converted), offset, lock and hidden flags and
 * its preview size; without a selection it edits the file's version.
 *
 * Every change is an edit of the #LrgEditorDocument (one undo step per
 * drag), so Ctrl+Z, the IPC data.* methods and the YAML stay in step.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_HUD_LAYOUT_EDITOR (lrg_editor_hud_layout_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorHudLayoutEditor, lrg_editor_hud_layout_editor, LRG, EDITOR_HUD_LAYOUT_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_hud_layout_editor_new:
 * @app: (nullable): the editor application
 * @document: a HUD layout document
 *
 * Creates the editor of @document. lrg_editor_data_editor_open() is the
 * usual way to get one; this is for embedding and tests.
 *
 * Returns: (transfer full): a new #LrgEditorHudLayoutEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorHudLayoutEditor * lrg_editor_hud_layout_editor_new (LrgEditorApp      *app,
                                                             LrgEditorDocument *document);

/**
 * lrg_editor_hud_layout_editor_get_selected:
 * @self: an #LrgEditorHudLayoutEditor
 *
 * Returns: (nullable): the id of the selected piece, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_hud_layout_editor_get_selected (LrgEditorHudLayoutEditor *self);

/**
 * lrg_editor_hud_layout_editor_select:
 * @self: an #LrgEditorHudLayoutEditor
 * @piece_id: (nullable): a piece of the document, or %NULL to clear
 *
 * Selects a piece and shows it in the property grid.
 *
 * Returns: %TRUE when @piece_id is a piece (or %NULL)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_hud_layout_editor_select (LrgEditorHudLayoutEditor *self,
                                              const gchar              *piece_id);

/**
 * lrg_editor_hud_layout_editor_get_piece_rect:
 * @self: an #LrgEditorHudLayoutEditor
 * @piece_id: a piece
 * @x: (out) (optional): left edge in canvas coordinates
 * @y: (out) (optional): top edge in canvas coordinates
 * @width: (out) (optional): width in canvas coordinates
 * @height: (out) (optional): height in canvas coordinates
 *
 * Where the stage draws a piece (after the last layout), so a test or a
 * tool can click and drag it.
 *
 * Returns: %TRUE for a known piece on a laid-out stage
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_hud_layout_editor_get_piece_rect (LrgEditorHudLayoutEditor *self,
                                                      const gchar              *piece_id,
                                                      gfloat                   *x,
                                                      gfloat                   *y,
                                                      gfloat                   *width,
                                                      gfloat                   *height);

/**
 * lrg_editor_hud_layout_editor_get_piece_size:
 * @self: an #LrgEditorHudLayoutEditor
 * @piece_id: a piece
 * @width: (out) (optional): preview width in screen units
 * @height: (out) (optional): preview height in screen units
 *
 * The preview size of a piece (not stored in the file).
 *
 * Returns: %TRUE for a known piece
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_hud_layout_editor_get_piece_size (LrgEditorHudLayoutEditor *self,
                                                      const gchar              *piece_id,
                                                      gfloat                   *width,
                                                      gfloat                   *height);

/**
 * lrg_editor_hud_layout_editor_set_piece_size:
 * @self: an #LrgEditorHudLayoutEditor
 * @piece_id: a piece
 * @width: preview width in screen units (at least 8)
 * @height: preview height in screen units (at least 8)
 *
 * Sets the preview size of a piece without moving its anchor point (the
 * offset is rewritten through the document when the anchor is not the
 * top-left corner).
 *
 * Returns: %TRUE for a known piece
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_hud_layout_editor_set_piece_size (LrgEditorHudLayoutEditor *self,
                                                      const gchar              *piece_id,
                                                      gfloat                    width,
                                                      gfloat                    height);

/**
 * lrg_editor_hud_layout_editor_get_screen_size:
 * @self: an #LrgEditorHudLayoutEditor
 * @width: (out) (optional): virtual screen width
 * @height: (out) (optional): virtual screen height
 *
 * The size of the game screen the stage simulates.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_hud_layout_editor_get_screen_size (LrgEditorHudLayoutEditor *self,
                                                   gfloat                   *width,
                                                   gfloat                   *height);

/**
 * lrg_editor_hud_layout_editor_set_screen_size:
 * @self: an #LrgEditorHudLayoutEditor
 * @width: virtual screen width (at least 64)
 * @height: virtual screen height (at least 64)
 *
 * Changes the simulated screen; anchored pieces follow their anchors.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_hud_layout_editor_set_screen_size (LrgEditorHudLayoutEditor *self,
                                                   gfloat                    width,
                                                   gfloat                    height);

/**
 * lrg_editor_hud_layout_editor_add_piece:
 * @self: an #LrgEditorHudLayoutEditor
 * @piece_id: (nullable): the new piece's id, %NULL for "piece-N"
 * @error: return location for a #GError
 *
 * Adds a piece placed at the top-left corner and selects it (one undo
 * step).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_hud_layout_editor_add_piece (LrgEditorHudLayoutEditor  *self,
                                                 const gchar               *piece_id,
                                                 GError                   **error);

G_END_DECLS

/*
 * Helpers shared by the HUD, UI, dialog tree and dungeon editors; not
 * part of the public API.
 *
 * EditorDataFields is an #LrgPropertySource whose rows are values of the
 * document at YAML paths (or virtual rows served by callbacks), so an
 * #LrgPropertyGrid edits the document directly: every set is an
 * lrg_editor_document_set_value() carrying the gesture id of the grid's
 * current edit gesture, so a drag on a spin box is one undo step.
 */
#ifdef LRG_EDITOR_DATA_INTERNAL
#include "../../../ui/lrg-property-source.h"
#include "../../../ui/lrg-property-grid.h"

#define EDITOR_TYPE_DATA_FIELDS (editor_data_fields_get_type ())
G_DECLARE_FINAL_TYPE (EditorDataFields, editor_data_fields, EDITOR, DATA_FIELDS, GObject)

/* Virtual row callbacks: @value is initialised to the row's type for get */
typedef gboolean (*EditorDataFieldGet) (const gchar  *name,
                                        GValue       *value,
                                        gpointer      user_data);
typedef gboolean (*EditorDataFieldSet) (const gchar  *name,
                                        const GValue *value,
                                        guint         gesture_id,
                                        gpointer      user_data);

G_GNUC_INTERNAL EditorDataFields * editor_data_fields_new        (LrgEditorDataEditor *editor);
G_GNUC_INTERNAL void               editor_data_fields_add        (EditorDataFields *self, GParamSpec *pspec,
                                                                  const gchar *path, const gchar *group,
                                                                  const gchar *hint);
G_GNUC_INTERNAL void               editor_data_fields_set_virtual (EditorDataFields *self, EditorDataFieldGet get,
                                                                   EditorDataFieldSet set, gpointer user_data);
G_GNUC_INTERNAL const gchar *      editor_data_fields_get_path   (EditorDataFields *self, const gchar *name);
G_GNUC_INTERNAL guint              editor_data_fields_get_count  (EditorDataFields *self);
G_GNUC_INTERNAL void               editor_data_fields_attach     (EditorDataFields *self, LrgPropertyGrid *grid);
G_GNUC_INTERNAL void               editor_data_fields_set_value  (EditorDataFields *self, const gchar *name,
                                                                  const gchar *text);
G_GNUC_INTERNAL GrlColor           editor_data_color_alpha       (const GrlColor *color, gfloat alpha);
G_GNUC_INTERNAL gchar *            editor_data_ellipsize         (const gchar *text, guint max_chars);
#endif
