/* lrg-editor-water-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorWaterEditor: the `.rwater` data editor.
 *
 * Edits an #LrgWaterBody document: the body form (name, shape, level,
 * size, cell size, foam band and depth, wave scale, shallow / deep / foam
 * colours, transparency, reflection, refraction and reflection distance),
 * the Gerstner waves (up to four: direction, amplitude, wavelength, speed,
 * steepness) and the lake / river outline points, with a live, animated
 * 3D preview of the surface over a ground plane (drag to orbit, wheel to
 * zoom). Every change is one undo step of the document (a slider drag
 * merges); the level's water nodes showing the file follow the unsaved
 * document. The editor also registers the water.* IPC methods, which work
 * on a file (through its editor) or on a level `water` node (through the
 * shared store, one level undo step each).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../graphics/lrg-water-body.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-viewport.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_WATER_EDITOR (lrg_editor_water_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorWaterEditor, lrg_editor_water_editor, LRG, EDITOR_WATER_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_water_editor_new:
 * @app: (nullable): the #LrgEditorApp
 * @document: a document of format %LRG_EDITOR_DOCUMENT_FORMAT_WATER
 *
 * Creates the editor (lrg_editor_data_editor_open() does this for `.rwater`
 * files).
 *
 * Returns: (transfer full): a new #LrgEditorWaterEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorWaterEditor * lrg_editor_water_editor_new (LrgEditorApp      *app,
                                                    LrgEditorDocument *document);

/**
 * lrg_editor_water_editor_install:
 * @app: an #LrgEditorApp
 *
 * Registers the water.* IPC methods on @app's server (once per app).
 * The Terrain panel and the first water editor call it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_water_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_water_editor_get_body:
 * @self: an #LrgEditorWaterEditor
 *
 * Returns: (transfer none) (nullable): the body parsed from the document
 *   (unsaved edits included), %NULL while the text does not load
 */
LRG_AVAILABLE_IN_ALL
LrgWaterBody * lrg_editor_water_editor_get_body (LrgEditorWaterEditor *self);

/**
 * lrg_editor_water_editor_get_grid:
 * @self: an #LrgEditorWaterEditor
 *
 * Returns: (transfer none): the body form
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_water_editor_get_grid (LrgEditorWaterEditor *self);

/**
 * lrg_editor_water_editor_get_wave_grid:
 * @self: an #LrgEditorWaterEditor
 *
 * Returns: (transfer none): the selected wave's form
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_water_editor_get_wave_grid (LrgEditorWaterEditor *self);

/**
 * lrg_editor_water_editor_get_point_grid:
 * @self: an #LrgEditorWaterEditor
 *
 * Returns: (transfer none): the selected outline point's form
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyGrid * lrg_editor_water_editor_get_point_grid (LrgEditorWaterEditor *self);

/**
 * lrg_editor_water_editor_get_preview:
 * @self: an #LrgEditorWaterEditor
 *
 * Returns: (transfer none): the live preview viewport
 */
LRG_AVAILABLE_IN_ALL
LrgViewport * lrg_editor_water_editor_get_preview (LrgEditorWaterEditor *self);

/**
 * lrg_editor_water_editor_get_selected_wave:
 * @self: an #LrgEditorWaterEditor
 *
 * Returns: the selected wave, -1 for none
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_water_editor_get_selected_wave (LrgEditorWaterEditor *self);

/**
 * lrg_editor_water_editor_set_selected_wave:
 * @self: an #LrgEditorWaterEditor
 * @index: a wave index, or -1
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_water_editor_set_selected_wave (LrgEditorWaterEditor *self,
                                                gint                  index);

/**
 * lrg_editor_water_editor_get_selected_point:
 * @self: an #LrgEditorWaterEditor
 *
 * Returns: the selected outline point, -1 for none
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_water_editor_get_selected_point (LrgEditorWaterEditor *self);

/**
 * lrg_editor_water_editor_set_selected_point:
 * @self: an #LrgEditorWaterEditor
 * @index: a point index, or -1
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_water_editor_set_selected_point (LrgEditorWaterEditor *self,
                                                 gint                  index);

/**
 * lrg_editor_water_editor_set_field:
 * @self: an #LrgEditorWaterEditor
 * @name: a body field: "name", "shape" (plane / lake / river), "level",
 *   "size" (width, length), "cell-size", "wave-scale", "foam" (band width,
 *   shallow depth), "shallow-color", "deep-color", "foam-color",
 *   "transparency", "reflection", "refraction" or "reflect-distance"
 * @value: the value (numbers, strings, #GrlVector2 pairs, #GrlColor or
 *   "#rrggbb" colours)
 * @gesture: 0 for an undo step of its own, or a gesture id that merges
 *   (lrg_editor_data_editor_new_gesture())
 * @error: return location for a #GError
 *
 * Sets one body field in the document.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_water_editor_set_field (LrgEditorWaterEditor  *self,
                                            const gchar           *name,
                                            const GValue          *value,
                                            guint                  gesture,
                                            GError               **error);

/**
 * lrg_editor_water_editor_set_wave:
 * @self: an #LrgEditorWaterEditor
 * @index: the wave (0 .. 3); waves up to it are added when missing
 * @wave: the wave
 * @gesture: 0, or a gesture id that merges
 * @error: return location for a #GError
 *
 * Sets one Gerstner wave in the document.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_water_editor_set_wave (LrgEditorWaterEditor  *self,
                                           guint                  index,
                                           const LrgWaterWave    *wave,
                                           guint                  gesture,
                                           GError               **error);

/**
 * lrg_editor_water_editor_set_wave_count:
 * @self: an #LrgEditorWaterEditor
 * @count: 0 .. 4 waves (new ones are a gentle swell)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_water_editor_set_wave_count (LrgEditorWaterEditor  *self,
                                                 guint                  count,
                                                 GError               **error);

/**
 * lrg_editor_water_editor_add_point:
 * @self: an #LrgEditorWaterEditor
 * @index: where to insert, -1 to append
 * @point: (array fixed-size=4): x, y, z, width
 * @error: return location for a #GError
 *
 * Inserts an outline point (one undo step) and selects it.
 *
 * Returns: the new point's index, -1 on error
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_water_editor_add_point (LrgEditorWaterEditor  *self,
                                        gint                   index,
                                        const gfloat          *point,
                                        GError               **error);

/**
 * lrg_editor_water_editor_set_point:
 * @self: an #LrgEditorWaterEditor
 * @index: the point
 * @point: (array fixed-size=4): x, y, z, width
 * @gesture: 0, or a gesture id that merges
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_water_editor_set_point (LrgEditorWaterEditor  *self,
                                            guint                  index,
                                            const gfloat          *point,
                                            guint                  gesture,
                                            GError               **error);

/**
 * lrg_editor_water_editor_remove_point:
 * @self: an #LrgEditorWaterEditor
 * @index: the point
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_water_editor_remove_point (LrgEditorWaterEditor  *self,
                                               guint                  index,
                                               GError               **error);

/**
 * lrg_editor_water_editor_sync_from_store:
 * @app: an #LrgEditorApp
 * @path: (type filename): an absolute `.rwater` path
 *
 * Pushes the shared store's body of @path (changed by the viewport's
 * outline handles or a water.* call on a node) into the open editor of
 * @path, as one undo step of its document.  Nothing happens when no
 * editor shows @path.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_water_editor_sync_from_store (LrgEditorApp *app,
                                              const gchar  *path);

/**
 * lrg_editor_water_body_apply_field:
 * @body: an #LrgWaterBody
 * @name: a field name as lrg_editor_water_editor_set_field() takes
 * @value: the value
 *
 * Sets one field of @body from a #GValue (the editor, the IPC methods and
 * scripts share this conversion).
 *
 * Returns: %TRUE when the field exists and the value fits
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_water_body_apply_field (LrgWaterBody *body,
                                            const gchar  *name,
                                            const GValue *value);

G_END_DECLS
