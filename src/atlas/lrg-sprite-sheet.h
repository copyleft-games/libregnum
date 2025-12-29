/* lrg-sprite-sheet.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sprite sheet for animation frames.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-atlas-region.h"

G_BEGIN_DECLS

#define LRG_TYPE_SPRITE_SHEET (lrg_sprite_sheet_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSpriteSheet, lrg_sprite_sheet, LRG, SPRITE_SHEET, GObject)

/**
 * lrg_sprite_sheet_new:
 * @name: Name identifier for the sprite sheet
 *
 * Creates a new empty sprite sheet.
 *
 * Returns: (transfer full): A new #LrgSpriteSheet
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteSheet *    lrg_sprite_sheet_new                    (const gchar *name);

/**
 * lrg_sprite_sheet_new_from_grid:
 * @name: Name identifier
 * @texture_path: Path to the texture file
 * @frame_width: Width of each frame in pixels
 * @frame_height: Height of each frame in pixels
 * @frame_count: Total number of frames (0 = auto-calculate)
 * @columns: Number of columns in the grid (0 = auto-calculate)
 *
 * Creates a sprite sheet from a regular grid layout.
 *
 * Returns: (transfer full): A new #LrgSpriteSheet
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteSheet *    lrg_sprite_sheet_new_from_grid          (const gchar *name,
                                                             const gchar *texture_path,
                                                             gint         frame_width,
                                                             gint         frame_height,
                                                             guint        frame_count,
                                                             guint        columns);

/**
 * lrg_sprite_sheet_new_from_file:
 * @path: Path to the sprite sheet definition file (YAML)
 * @error: (nullable): Return location for error
 *
 * Creates a sprite sheet by loading a definition file.
 *
 * Returns: (transfer full) (nullable): A new #LrgSpriteSheet or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteSheet *    lrg_sprite_sheet_new_from_file          (const gchar  *path,
                                                             GError      **error);

/* Properties */

/**
 * lrg_sprite_sheet_get_name:
 * @self: A #LrgSpriteSheet
 *
 * Gets the name of the sprite sheet.
 *
 * Returns: (transfer none) (nullable): The sprite sheet name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_sprite_sheet_get_name               (LrgSpriteSheet *self);

/**
 * lrg_sprite_sheet_get_texture_path:
 * @self: A #LrgSpriteSheet
 *
 * Gets the path to the texture file.
 *
 * Returns: (transfer none) (nullable): The texture path
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_sprite_sheet_get_texture_path       (LrgSpriteSheet *self);

/**
 * lrg_sprite_sheet_set_texture_path:
 * @self: A #LrgSpriteSheet
 * @path: Path to the texture file
 *
 * Sets the path to the texture file.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sprite_sheet_set_texture_path       (LrgSpriteSheet *self,
                                                             const gchar    *path);

/**
 * lrg_sprite_sheet_get_texture_width:
 * @self: A #LrgSpriteSheet
 *
 * Gets the width of the texture.
 *
 * Returns: Width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_sprite_sheet_get_texture_width      (LrgSpriteSheet *self);

/**
 * lrg_sprite_sheet_set_texture_width:
 * @self: A #LrgSpriteSheet
 * @width: Width in pixels
 *
 * Sets the texture width.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sprite_sheet_set_texture_width      (LrgSpriteSheet *self,
                                                             gint            width);

/**
 * lrg_sprite_sheet_get_texture_height:
 * @self: A #LrgSpriteSheet
 *
 * Gets the height of the texture.
 *
 * Returns: Height in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_sprite_sheet_get_texture_height     (LrgSpriteSheet *self);

/**
 * lrg_sprite_sheet_set_texture_height:
 * @self: A #LrgSpriteSheet
 * @height: Height in pixels
 *
 * Sets the texture height.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sprite_sheet_set_texture_height     (LrgSpriteSheet *self,
                                                             gint            height);

/**
 * lrg_sprite_sheet_set_texture_size:
 * @self: A #LrgSpriteSheet
 * @width: Width in pixels
 * @height: Height in pixels
 *
 * Sets both texture dimensions.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sprite_sheet_set_texture_size       (LrgSpriteSheet *self,
                                                             gint            width,
                                                             gint            height);

/**
 * lrg_sprite_sheet_get_format:
 * @self: A #LrgSpriteSheet
 *
 * Gets the sprite sheet format.
 *
 * Returns: The format type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteSheetFormat lrg_sprite_sheet_get_format            (LrgSpriteSheet *self);

/**
 * lrg_sprite_sheet_set_format:
 * @self: A #LrgSpriteSheet
 * @format: The format type
 *
 * Sets the sprite sheet format.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sprite_sheet_set_format             (LrgSpriteSheet      *self,
                                                             LrgSpriteSheetFormat format);

/* Frame management */

/**
 * lrg_sprite_sheet_get_frame_count:
 * @self: A #LrgSpriteSheet
 *
 * Gets the total number of frames.
 *
 * Returns: The frame count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_sprite_sheet_get_frame_count        (LrgSpriteSheet *self);

/**
 * lrg_sprite_sheet_get_frame:
 * @self: A #LrgSpriteSheet
 * @index: Frame index (0-based)
 *
 * Gets a frame by index.
 *
 * Returns: (transfer none) (nullable): The frame region, or %NULL if out of bounds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAtlasRegion *    lrg_sprite_sheet_get_frame              (LrgSpriteSheet *self,
                                                             guint           index);

/**
 * lrg_sprite_sheet_get_frame_by_name:
 * @self: A #LrgSpriteSheet
 * @name: Frame name
 *
 * Gets a frame by name.
 *
 * Returns: (transfer none) (nullable): The frame region, or %NULL if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAtlasRegion *    lrg_sprite_sheet_get_frame_by_name      (LrgSpriteSheet *self,
                                                             const gchar    *name);

/**
 * lrg_sprite_sheet_add_frame:
 * @self: A #LrgSpriteSheet
 * @frame: The frame region to add
 *
 * Adds a frame to the sprite sheet. Takes ownership of the region.
 *
 * Returns: The index of the added frame
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_sprite_sheet_add_frame              (LrgSpriteSheet *self,
                                                             LrgAtlasRegion *frame);

/**
 * lrg_sprite_sheet_add_frame_rect:
 * @self: A #LrgSpriteSheet
 * @name: (nullable): Frame name (can be NULL for numbered frames)
 * @x: X position in the texture
 * @y: Y position in the texture
 * @width: Width of the frame
 * @height: Height of the frame
 *
 * Convenience function to add a frame by rectangle.
 * UV coordinates are calculated automatically.
 *
 * Returns: The index of the added frame
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_sprite_sheet_add_frame_rect         (LrgSpriteSheet *self,
                                                             const gchar    *name,
                                                             gint            x,
                                                             gint            y,
                                                             gint            width,
                                                             gint            height);

/**
 * lrg_sprite_sheet_remove_frame:
 * @self: A #LrgSpriteSheet
 * @index: Frame index to remove
 *
 * Removes a frame by index.
 *
 * Returns: %TRUE if the frame was removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_sprite_sheet_remove_frame           (LrgSpriteSheet *self,
                                                             guint           index);

/**
 * lrg_sprite_sheet_clear_frames:
 * @self: A #LrgSpriteSheet
 *
 * Removes all frames from the sprite sheet.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sprite_sheet_clear_frames           (LrgSpriteSheet *self);

/* Animation sequences */

/**
 * lrg_sprite_sheet_define_animation:
 * @self: A #LrgSpriteSheet
 * @name: Animation name
 * @start_frame: First frame index
 * @end_frame: Last frame index (inclusive)
 * @frame_duration: Duration per frame in seconds
 * @loop: Whether the animation loops
 *
 * Defines a named animation sequence using consecutive frames.
 *
 * Returns: %TRUE if the animation was defined successfully
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_sprite_sheet_define_animation       (LrgSpriteSheet *self,
                                                             const gchar    *name,
                                                             guint           start_frame,
                                                             guint           end_frame,
                                                             gfloat          frame_duration,
                                                             gboolean        loop);

/**
 * lrg_sprite_sheet_define_animation_frames:
 * @self: A #LrgSpriteSheet
 * @name: Animation name
 * @frames: (array length=n_frames): Array of frame indices
 * @n_frames: Number of frames
 * @frame_duration: Duration per frame in seconds
 * @loop: Whether the animation loops
 *
 * Defines a named animation sequence using arbitrary frames.
 *
 * Returns: %TRUE if the animation was defined successfully
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_sprite_sheet_define_animation_frames (LrgSpriteSheet *self,
                                                              const gchar    *name,
                                                              const guint    *frames,
                                                              guint           n_frames,
                                                              gfloat          frame_duration,
                                                              gboolean        loop);

/**
 * lrg_sprite_sheet_has_animation:
 * @self: A #LrgSpriteSheet
 * @name: Animation name
 *
 * Checks if an animation exists.
 *
 * Returns: %TRUE if the animation exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_sprite_sheet_has_animation          (LrgSpriteSheet *self,
                                                             const gchar    *name);

/**
 * lrg_sprite_sheet_get_animation_frame_count:
 * @self: A #LrgSpriteSheet
 * @name: Animation name
 *
 * Gets the number of frames in an animation.
 *
 * Returns: Frame count, or 0 if animation not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_sprite_sheet_get_animation_frame_count (LrgSpriteSheet *self,
                                                                const gchar    *name);

/**
 * lrg_sprite_sheet_get_animation_duration:
 * @self: A #LrgSpriteSheet
 * @name: Animation name
 *
 * Gets the total duration of an animation.
 *
 * Returns: Duration in seconds, or 0 if animation not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_sprite_sheet_get_animation_duration (LrgSpriteSheet *self,
                                                             const gchar    *name);

/**
 * lrg_sprite_sheet_get_animation_frame:
 * @self: A #LrgSpriteSheet
 * @name: Animation name
 * @time: Time in seconds since animation start
 *
 * Gets the frame region for an animation at a given time.
 *
 * Returns: (transfer none) (nullable): The frame region, or %NULL if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAtlasRegion *    lrg_sprite_sheet_get_animation_frame    (LrgSpriteSheet *self,
                                                             const gchar    *name,
                                                             gfloat          time);

/**
 * lrg_sprite_sheet_get_animation_names:
 * @self: A #LrgSpriteSheet
 *
 * Gets all animation names.
 *
 * Returns: (transfer full) (element-type utf8): Array of animation names
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_sprite_sheet_get_animation_names    (LrgSpriteSheet *self);

/**
 * lrg_sprite_sheet_remove_animation:
 * @self: A #LrgSpriteSheet
 * @name: Animation name
 *
 * Removes an animation definition.
 *
 * Returns: %TRUE if the animation was removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_sprite_sheet_remove_animation       (LrgSpriteSheet *self,
                                                             const gchar    *name);

/* UV calculation */

/**
 * lrg_sprite_sheet_recalculate_uvs:
 * @self: A #LrgSpriteSheet
 *
 * Recalculates UV coordinates for all frames based on their
 * pixel positions and the texture dimensions.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sprite_sheet_recalculate_uvs        (LrgSpriteSheet *self);

/* Grid utilities */

/**
 * lrg_sprite_sheet_generate_grid:
 * @self: A #LrgSpriteSheet
 * @frame_width: Width of each frame
 * @frame_height: Height of each frame
 * @columns: Number of columns (0 = auto from texture width)
 * @rows: Number of rows (0 = auto from texture height)
 * @padding: Padding between frames
 * @offset_x: X offset from texture edge
 * @offset_y: Y offset from texture edge
 *
 * Generates frames from a grid layout. Clears existing frames first.
 *
 * Returns: Number of frames generated
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_sprite_sheet_generate_grid          (LrgSpriteSheet *self,
                                                             gint            frame_width,
                                                             gint            frame_height,
                                                             guint           columns,
                                                             guint           rows,
                                                             gint            padding,
                                                             gint            offset_x,
                                                             gint            offset_y);

/* Serialization */

/**
 * lrg_sprite_sheet_save_to_file:
 * @self: A #LrgSpriteSheet
 * @path: Path to save to
 * @error: (nullable): Return location for error
 *
 * Saves the sprite sheet definition to a YAML file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_sprite_sheet_save_to_file           (LrgSpriteSheet  *self,
                                                             const gchar     *path,
                                                             GError         **error);

G_END_DECLS
