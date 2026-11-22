/* lrg-sprite-sheet-builder.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSpriteSheetBuilder: makes an LrgSpriteSheet and its image from a
 * list of frames (image files or pixel buffers) with named animations,
 * optionally removing the background by a key colour or an alpha
 * threshold and reducing the frames to a shared palette of N colours.
 * Everything runs on the CPU, without a window.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-sprite-sheet.h"

G_BEGIN_DECLS

/**
 * LRG_SPRITE_SHEET_BUILDER_ERROR:
 *
 * Error domain of #LrgSpriteSheetBuilder.
 *
 * Since: 0.11.0
 */
#define LRG_SPRITE_SHEET_BUILDER_ERROR (lrg_sprite_sheet_builder_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_sprite_sheet_builder_error_quark (void);

/**
 * LrgSpriteSheetBuilderError:
 * @LRG_SPRITE_SHEET_BUILDER_ERROR_IO: a file could not be read or written
 * @LRG_SPRITE_SHEET_BUILDER_ERROR_EMPTY: there are no frames to build from
 *
 * Errors of #LrgSpriteSheetBuilder.
 *
 * Since: 0.11.0
 */
typedef enum
{
	LRG_SPRITE_SHEET_BUILDER_ERROR_IO,
	LRG_SPRITE_SHEET_BUILDER_ERROR_EMPTY
} LrgSpriteSheetBuilderError;

LRG_AVAILABLE_IN_ALL
GType lrg_sprite_sheet_builder_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SPRITE_SHEET_BUILDER_ERROR (lrg_sprite_sheet_builder_error_get_type ())

#define LRG_TYPE_SPRITE_SHEET_BUILDER (lrg_sprite_sheet_builder_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSpriteSheetBuilder, lrg_sprite_sheet_builder, LRG, SPRITE_SHEET_BUILDER, GObject)

/**
 * lrg_sprite_sheet_builder_new:
 *
 * Creates an empty builder: no background removal, no palette, square-ish
 * grid, 1 pixel of padding.
 *
 * Returns: (transfer full): a new #LrgSpriteSheetBuilder
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteSheetBuilder * lrg_sprite_sheet_builder_new (void);

/**
 * lrg_sprite_sheet_builder_add_frame_file:
 * @self: an #LrgSpriteSheetBuilder
 * @path: an image file (PNG, BMP, TGA, JPG, ... as raylib reads them)
 * @error: (nullable): return location for an error
 *
 * Adds a frame from a file.
 *
 * Returns: the frame's index, or -1 when the file could not be read
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_sprite_sheet_builder_add_frame_file (LrgSpriteSheetBuilder  *self,
                                              const gchar            *path,
                                              GError                **error);

/**
 * lrg_sprite_sheet_builder_add_frame_image:
 * @self: an #LrgSpriteSheetBuilder
 * @image: an image (copied; converted to RGBA8)
 *
 * Adds a frame from an image.
 *
 * Returns: the frame's index
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_sprite_sheet_builder_add_frame_image (LrgSpriteSheetBuilder *self,
                                               GrlImage              *image);

/**
 * lrg_sprite_sheet_builder_add_frame_pixels:
 * @self: an #LrgSpriteSheetBuilder
 * @rgba: (array) (element-type guint8): top-down RGBA8 rows (copied)
 * @width: width in pixels (> 0)
 * @height: height in pixels (> 0)
 * @stride: bytes from one row to the next (>= @width x 4)
 *
 * Adds a frame from a pixel buffer (a video frame, a render).
 *
 * Returns: the frame's index
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_sprite_sheet_builder_add_frame_pixels (LrgSpriteSheetBuilder *self,
                                                const guint8          *rgba,
                                                gint                   width,
                                                gint                   height,
                                                gint                   stride);

/**
 * lrg_sprite_sheet_builder_get_n_frames:
 * @self: an #LrgSpriteSheetBuilder
 *
 * Returns: the number of frames added
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sprite_sheet_builder_get_n_frames (LrgSpriteSheetBuilder *self);

/**
 * lrg_sprite_sheet_builder_add_animation:
 * @self: an #LrgSpriteSheetBuilder
 * @name: the animation's name ("walk-down", "idle")
 * @frames: (array length=n_frames): frame indices in playing order
 *   (repeats allowed)
 * @n_frames: number of frames (> 0)
 * @frame_duration: seconds per frame (> 0)
 * @loop: whether it loops
 *
 * Names an animation over the added frames.
 *
 * Returns: %FALSE when a frame index is out of range or the name is
 *   already taken
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sprite_sheet_builder_add_animation (LrgSpriteSheetBuilder *self,
                                                 const gchar           *name,
                                                 const guint           *frames,
                                                 guint                  n_frames,
                                                 gfloat                 frame_duration,
                                                 gboolean               loop);

/**
 * lrg_sprite_sheet_builder_add_animation_range:
 * @self: an #LrgSpriteSheetBuilder
 * @name: the animation's name
 * @first: the first frame index
 * @count: how many consecutive frames (> 0)
 * @frame_duration: seconds per frame (> 0)
 * @loop: whether it loops
 *
 * Names an animation over consecutive frames.
 *
 * Returns: as lrg_sprite_sheet_builder_add_animation()
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sprite_sheet_builder_add_animation_range (LrgSpriteSheetBuilder *self,
                                                       const gchar           *name,
                                                       guint                  first,
                                                       guint                  count,
                                                       gfloat                 frame_duration,
                                                       gboolean               loop);

/**
 * lrg_sprite_sheet_builder_set_key_color:
 * @self: an #LrgSpriteSheetBuilder
 * @key: (nullable): the background colour to remove; %NULL turns it off
 * @tolerance: how far (largest channel difference, 0 to 255) a pixel may
 *   be from @key and still count as background
 *
 * Makes pixels of a key colour (a green screen, a magenta matte)
 * transparent.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_sheet_builder_set_key_color (LrgSpriteSheetBuilder *self,
                                             const GrlColor        *key,
                                             guint                  tolerance);

/**
 * lrg_sprite_sheet_builder_set_alpha_threshold:
 * @self: an #LrgSpriteSheetBuilder
 * @threshold: pixels with alpha below this become fully transparent;
 *   with @binary, the others become fully opaque (0 turns it off)
 * @binary: make the alpha either 0 or 255 (crisp pixel art)
 *
 * Cleans up soft or noisy alpha (a render's anti-aliased edge, a
 * matte's fringe).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_sheet_builder_set_alpha_threshold (LrgSpriteSheetBuilder *self,
                                                   guint8                 threshold,
                                                   gboolean               binary);

/**
 * lrg_sprite_sheet_builder_set_palette_size:
 * @self: an #LrgSpriteSheetBuilder
 * @n_colors: colours in the shared palette (2 to 256), 0 for none
 *
 * Reduces every frame to one palette of at most @n_colors colours,
 * chosen by median cut over the visible pixels of all frames together,
 * so an animation does not flicker between palettes.  Deterministic.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_sheet_builder_set_palette_size (LrgSpriteSheetBuilder *self,
                                                guint                  n_colors);

/**
 * lrg_sprite_sheet_builder_set_layout:
 * @self: an #LrgSpriteSheetBuilder
 * @columns: frames per row; 0 picks the smallest square-ish grid
 * @padding: transparent pixels around and between cells
 *
 * Sets how frames are laid out.  Every cell is as large as the largest
 * frame; smaller frames sit centred horizontally on the cell's bottom
 * edge (where a character's feet are).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_sheet_builder_set_layout (LrgSpriteSheetBuilder *self,
                                          guint                  columns,
                                          guint                  padding);

/**
 * lrg_sprite_sheet_builder_build:
 * @self: an #LrgSpriteSheetBuilder
 * @name: the sprite sheet's name
 * @error: (nullable): return location for an error
 *
 * Processes the frames (background removal, then the palette), lays
 * them out into one image and describes it as a sprite sheet: one frame
 * per cell, named "frame-N", and the animations.  The image is kept
 * (lrg_sprite_sheet_builder_get_image()); the sheet's texture path is
 * set by lrg_sprite_sheet_builder_save().
 *
 * Returns: (transfer full) (nullable): the sprite sheet, %NULL with no
 *   frames
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteSheet * lrg_sprite_sheet_builder_build (LrgSpriteSheetBuilder  *self,
                                                 const gchar            *name,
                                                 GError                **error);

/**
 * lrg_sprite_sheet_builder_get_image:
 * @self: an #LrgSpriteSheetBuilder
 *
 * Returns: (transfer none) (nullable): the image of the last build
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_sprite_sheet_builder_get_image (LrgSpriteSheetBuilder *self);

/**
 * lrg_sprite_sheet_builder_get_cell_size:
 * @self: an #LrgSpriteSheetBuilder
 * @width: (out) (optional): cell width of the last build
 * @height: (out) (optional): cell height of the last build
 *
 * Gets the size of one cell.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_sheet_builder_get_cell_size (LrgSpriteSheetBuilder *self,
                                             gint                  *width,
                                             gint                  *height);

/**
 * lrg_sprite_sheet_builder_save:
 * @self: an #LrgSpriteSheetBuilder
 * @name: the sprite sheet's name
 * @png_path: where the image goes
 * @yaml_path: where the sprite sheet definition goes
 * @error: (nullable): return location for an error
 *
 * Builds and writes both files.  The definition's texture_path is the
 * PNG's file name when both are in one directory, its full path
 * otherwise.
 *
 * Returns: %TRUE when both files were written
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sprite_sheet_builder_save (LrgSpriteSheetBuilder  *self,
                                        const gchar            *name,
                                        const gchar            *png_path,
                                        const gchar            *yaml_path,
                                        GError                **error);

G_END_DECLS
