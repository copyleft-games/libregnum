/* lrg-editor-sprite-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSpriteEditor: the `.rsprite` data editor of lrgeditor.
 *
 * One tab edits one sprite sheet (see docs/modules/atlas/
 * sprite-animation.org): the slicer (a grid with frame size, columns,
 * rows, count, margin, spacing and pivot; automatic slicing of the
 * image's opaque islands; or explicit rectangles dragged out on the
 * image), the frames (name, rectangle, pivot dragged on the canvas) and
 * the frame animations (clips with fps and loop mode, a strip of clip
 * positions with per-frame durations, named events on positions) with a
 * playing preview that honours the pivot.
 *
 * The document is the truth: after every change its text is parsed with
 * lrg_sprite_sheet_new_from_rsprite_data(); edits change a fresh parse
 * through the LrgSpriteSheet / LrgSpriteAnimation API and write it back
 * with lrg_sprite_sheet_save_rsprite_to_data() as one undo step of the
 * document (pivot drags merge into one step).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../atlas/lrg-sprite-sheet.h"
#include "../../../atlas/lrg-sprite-animation.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_SPRITE_EDITOR (lrg_editor_sprite_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorSpriteEditor, lrg_editor_sprite_editor, LRG, EDITOR_SPRITE_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_sprite_editor_new:
 * @app: (nullable): the editor application
 * @document: an `.rsprite` document
 *
 * Creates the editor of @document (lrg_editor_data_editor_open() does
 * this for `.rsprite` files and docks it).
 *
 * Returns: (transfer full): a new #LrgEditorSpriteEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSpriteEditor * lrg_editor_sprite_editor_new (LrgEditorApp      *app,
                                                      LrgEditorDocument *document);

/**
 * lrg_editor_sprite_editor_open:
 * @app: the editor application
 * @path: an `.rsprite` file, absolute or relative to the project
 * @error: return location for a #GError
 *
 * Opens (or presents) the tab of @path.
 *
 * Returns: (transfer none) (nullable): the editor, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSpriteEditor * lrg_editor_sprite_editor_open (LrgEditorApp  *app,
                                                       const gchar   *path,
                                                       GError       **error);

/**
 * lrg_editor_sprite_editor_create:
 * @app: the editor application
 * @path: the new `.rsprite` file, absolute or relative to the project
 * @image: (nullable): the sheet image, absolute or relative to the project
 *   (stored relative to the new file; its size is read from the file)
 * @frame_width: grid frame width, or 0 for no grid
 * @frame_height: grid frame height, or 0 for no grid
 * @name: (nullable): the sheet name (default: the file's base name)
 * @error: return location for a #GError
 *
 * Writes a new sprite sheet ("Slice Sprite Sheet" on a .png): with a
 * frame size, the image is sliced into a grid of that size. Refuses to
 * overwrite an existing file.
 *
 * Returns: %TRUE when the file was written
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_create (LrgEditorApp  *app,
                                          const gchar   *path,
                                          const gchar   *image,
                                          gint           frame_width,
                                          gint           frame_height,
                                          const gchar   *name,
                                          GError       **error);

/**
 * lrg_editor_sprite_editor_install:
 * @app: the editor application
 *
 * Registers the sprite.* IPC methods on @app's server (once). The Tiles
 * panel calls it when it is created.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sprite_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_sprite_editor_get_sheet:
 * @self: an #LrgEditorSpriteEditor
 *
 * Returns: (transfer none) (nullable): the sheet parsed from the
 *   document, or %NULL while the text does not load
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteSheet * lrg_editor_sprite_editor_get_sheet (LrgEditorSpriteEditor *self);

/**
 * lrg_editor_sprite_editor_get_parse_error:
 * @self: an #LrgEditorSpriteEditor
 *
 * Returns: (nullable): the loader's message when the document does not
 *   load, else %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_sprite_editor_get_parse_error (LrgEditorSpriteEditor *self);

/**
 * lrg_editor_sprite_editor_get_selected_frame:
 * @self: an #LrgEditorSpriteEditor
 *
 * Returns: the selected sheet frame, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_sprite_editor_get_selected_frame (LrgEditorSpriteEditor *self);

/**
 * lrg_editor_sprite_editor_set_selected_frame:
 * @self: an #LrgEditorSpriteEditor
 * @index: a frame index, or -1
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sprite_editor_set_selected_frame (LrgEditorSpriteEditor *self,
                                                  gint                   index);

/**
 * lrg_editor_sprite_editor_frame_at:
 * @self: an #LrgEditorSpriteEditor
 * @x: canvas x
 * @y: canvas y
 *
 * Returns: the frame under a canvas point of the slicer view (the last
 *   one drawn wins), or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_sprite_editor_frame_at (LrgEditorSpriteEditor *self,
                                        gfloat                 x,
                                        gfloat                 y);

/**
 * lrg_editor_sprite_editor_image_to_canvas:
 * @self: an #LrgEditorSpriteEditor
 * @image_x: x in image pixels
 * @image_y: y in image pixels
 * @canvas_x: (out): canvas x
 * @canvas_y: (out): canvas y
 *
 * Maps an image pixel to the slicer view's canvas position (tests and
 * scripted drags use it).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sprite_editor_image_to_canvas (LrgEditorSpriteEditor *self,
                                               gfloat                 image_x,
                                               gfloat                 image_y,
                                               gfloat                *canvas_x,
                                               gfloat                *canvas_y);

/* --- slicing and frames --- */

/**
 * lrg_editor_sprite_editor_slice_grid:
 * @self: an #LrgEditorSpriteEditor
 * @frame_width: frame width (>= 1)
 * @frame_height: frame height (>= 1)
 * @columns: frames per row (0: as many as fit the image)
 * @rows: rows (0: as many as fit the image)
 * @count: grid frames (0: columns x rows)
 * @margin_x: pixels before the first column
 * @margin_y: pixels before the first row
 * @spacing_x: pixels between columns
 * @spacing_y: pixels between rows
 * @pivot_x: pivot x of every frame
 * @pivot_y: pivot y of every frame
 * @error: return location for a #GError
 *
 * Re-slices the sheet as a grid (lrg_sprite_sheet_slice_grid()) as one
 * undo step. Clips keep their frame indices.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_slice_grid (LrgEditorSpriteEditor  *self,
                                              gint                    frame_width,
                                              gint                    frame_height,
                                              guint                   columns,
                                              guint                   rows,
                                              guint                   count,
                                              gint                    margin_x,
                                              gint                    margin_y,
                                              gint                    spacing_x,
                                              gint                    spacing_y,
                                              gfloat                  pivot_x,
                                              gfloat                  pivot_y,
                                              GError                **error);

/**
 * lrg_editor_sprite_editor_slice_alpha:
 * @self: an #LrgEditorSpriteEditor
 * @threshold: pixels with alpha above this are sprite pixels
 * @min_size: islands narrower or shorter than this are ignored
 * @pivot_x: pivot x as a fraction of each frame's width (0.5: centre)
 * @pivot_y: pivot y as a fraction of each frame's height (1: bottom)
 * @error: return location for a #GError
 *
 * Slices the image's opaque islands into frames
 * (lrg_sprite_sheet_slice_alpha() on the decoded image) as one undo step.
 *
 * Returns: %TRUE on success (an image with no island adds no frame)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_slice_alpha (LrgEditorSpriteEditor  *self,
                                               guint8                  threshold,
                                               guint                   min_size,
                                               gfloat                  pivot_x,
                                               gfloat                  pivot_y,
                                               GError                **error);

/**
 * lrg_editor_sprite_editor_add_frame:
 * @self: an #LrgEditorSpriteEditor
 * @name: (nullable): the frame name (%NULL: frame_<index>)
 * @x: rectangle x
 * @y: rectangle y
 * @width: rectangle width (>= 1)
 * @height: rectangle height (>= 1)
 * @pivot_x: pivot x relative to the rectangle
 * @pivot_y: pivot y relative to the rectangle
 * @error: return location for a #GError
 *
 * Appends an explicit frame and selects it.
 *
 * Returns: the new frame's index, or -1 with @error set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_sprite_editor_add_frame (LrgEditorSpriteEditor  *self,
                                         const gchar            *name,
                                         gint                    x,
                                         gint                    y,
                                         gint                    width,
                                         gint                    height,
                                         gfloat                  pivot_x,
                                         gfloat                  pivot_y,
                                         GError                **error);

/**
 * lrg_editor_sprite_editor_set_frame:
 * @self: an #LrgEditorSpriteEditor
 * @index: a frame index
 * @name: (nullable): a new name, or %NULL to keep it
 * @rect: (array fixed-size=4) (nullable): x, y, width, height for an
 *   explicit frame (grid frames keep the grid's), or %NULL
 * @pivot: (array fixed-size=2) (nullable): the new pivot, or %NULL
 * @gesture: 0, or a gesture id (pivot drags merge into one undo step)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_set_frame (LrgEditorSpriteEditor  *self,
                                             guint                   index,
                                             const gchar            *name,
                                             const gint             *rect,
                                             const gfloat           *pivot,
                                             guint                   gesture,
                                             GError                **error);

/**
 * lrg_editor_sprite_editor_remove_frame:
 * @self: an #LrgEditorSpriteEditor
 * @index: an explicit frame's index (grid frames go with the grid)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_remove_frame (LrgEditorSpriteEditor  *self,
                                                guint                   index,
                                                GError                **error);

/* --- clips --- */

/**
 * lrg_editor_sprite_editor_get_selected_clip:
 * @self: an #LrgEditorSpriteEditor
 *
 * Returns: (nullable): the selected clip's name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_sprite_editor_get_selected_clip (LrgEditorSpriteEditor *self);

/**
 * lrg_editor_sprite_editor_select_clip:
 * @self: an #LrgEditorSpriteEditor
 * @name: (nullable): a clip name, or %NULL
 *
 * Selects a clip (its strip shows and the preview plays it).
 *
 * Returns: %FALSE when there is no such clip
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_select_clip (LrgEditorSpriteEditor *self,
                                               const gchar           *name);

/**
 * lrg_editor_sprite_editor_add_clip:
 * @self: an #LrgEditorSpriteEditor
 * @name: a new, unique clip name
 * @frames: (array length=n_frames) (nullable): sheet frames of the clip
 * @n_frames: number of frames
 * @fps: frames per second (> 0)
 * @loop: the loop mode
 * @error: return location for a #GError
 *
 * Adds a clip and selects it.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_add_clip (LrgEditorSpriteEditor  *self,
                                            const gchar            *name,
                                            const guint            *frames,
                                            guint                   n_frames,
                                            gfloat                  fps,
                                            LrgSpriteLoopMode       loop,
                                            GError                **error);

/**
 * lrg_editor_sprite_editor_remove_clip:
 * @self: an #LrgEditorSpriteEditor
 * @name: a clip name
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_remove_clip (LrgEditorSpriteEditor  *self,
                                               const gchar            *name,
                                               GError                **error);

/**
 * lrg_editor_sprite_editor_set_clip:
 * @self: an #LrgEditorSpriteEditor
 * @name: a clip name
 * @new_name: (nullable): a new name, or %NULL to keep it
 * @fps: the new fps, or a value <= 0 to keep it
 * @loop: the new loop mode, or -1 to keep it
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_set_clip (LrgEditorSpriteEditor  *self,
                                            const gchar            *name,
                                            const gchar            *new_name,
                                            gfloat                  fps,
                                            gint                    loop,
                                            GError                **error);

/**
 * lrg_editor_sprite_editor_insert_clip_frame:
 * @self: an #LrgEditorSpriteEditor
 * @clip: a clip name
 * @position: the clip position to insert at (-1 or past the end appends)
 * @frame: a sheet frame
 * @duration: its own duration in seconds (0: 1 / fps)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_insert_clip_frame (LrgEditorSpriteEditor  *self,
                                                     const gchar            *clip,
                                                     gint                    position,
                                                     guint                   frame,
                                                     gfloat                  duration,
                                                     GError                **error);

/**
 * lrg_editor_sprite_editor_set_clip_frame:
 * @self: an #LrgEditorSpriteEditor
 * @clip: a clip name
 * @position: a clip position
 * @frame: the sheet frame, or -1 to keep it
 * @duration: the duration (0: 1 / fps), or a negative value to keep it
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_set_clip_frame (LrgEditorSpriteEditor  *self,
                                                  const gchar            *clip,
                                                  guint                   position,
                                                  gint                    frame,
                                                  gfloat                  duration,
                                                  GError                **error);

/**
 * lrg_editor_sprite_editor_remove_clip_frame:
 * @self: an #LrgEditorSpriteEditor
 * @clip: a clip name
 * @position: a clip position (its events go with it)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_remove_clip_frame (LrgEditorSpriteEditor  *self,
                                                     const gchar            *clip,
                                                     guint                   position,
                                                     GError                **error);

/**
 * lrg_editor_sprite_editor_move_clip_frame:
 * @self: an #LrgEditorSpriteEditor
 * @clip: a clip name
 * @from: a clip position
 * @to: its new position
 * @error: return location for a #GError
 *
 * Moves a clip position with its duration (drag-reorder in the strip).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_move_clip_frame (LrgEditorSpriteEditor  *self,
                                                   const gchar            *clip,
                                                   guint                   from,
                                                   guint                   to,
                                                   GError                **error);

/**
 * lrg_editor_sprite_editor_add_event:
 * @self: an #LrgEditorSpriteEditor
 * @clip: a clip name
 * @position: a clip position
 * @name: the event name
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_add_event (LrgEditorSpriteEditor  *self,
                                             const gchar            *clip,
                                             guint                   position,
                                             const gchar            *name,
                                             GError                **error);

/**
 * lrg_editor_sprite_editor_remove_event:
 * @self: an #LrgEditorSpriteEditor
 * @clip: a clip name
 * @position: a clip position
 * @name: (nullable): the event name, or %NULL for every event there
 * @error: return location for a #GError
 *
 * Returns: %TRUE when an event was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_remove_event (LrgEditorSpriteEditor  *self,
                                                const gchar            *clip,
                                                guint                   position,
                                                const gchar            *name,
                                                GError                **error);

/* --- the preview --- */

/**
 * lrg_editor_sprite_editor_set_playing:
 * @self: an #LrgEditorSpriteEditor
 * @playing: play the selected clip in the preview
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sprite_editor_set_playing (LrgEditorSpriteEditor *self,
                                           gboolean               playing);

/**
 * lrg_editor_sprite_editor_get_playing:
 * @self: an #LrgEditorSpriteEditor
 *
 * Returns: whether the preview plays
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_sprite_editor_get_playing (LrgEditorSpriteEditor *self);

/**
 * lrg_editor_sprite_editor_seek:
 * @self: an #LrgEditorSpriteEditor
 * @time: the preview time in seconds
 *
 * Scrubs the preview (lrg_sprite_animation_sample() at @time).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_sprite_editor_seek (LrgEditorSpriteEditor *self,
                                    gfloat                 time);

/**
 * lrg_editor_sprite_editor_get_time:
 * @self: an #LrgEditorSpriteEditor
 *
 * Returns: the preview time in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_sprite_editor_get_time (LrgEditorSpriteEditor *self);

/**
 * lrg_editor_sprite_editor_get_preview_frame:
 * @self: an #LrgEditorSpriteEditor
 * @position: (out) (optional): the clip position showing
 *
 * Returns: the sheet frame the preview shows, or -1 without a clip
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_sprite_editor_get_preview_frame (LrgEditorSpriteEditor *self,
                                                 guint                 *position);

G_END_DECLS
