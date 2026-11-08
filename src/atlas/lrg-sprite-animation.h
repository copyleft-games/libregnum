/* lrg-sprite-animation.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Frame animations for sprite sheets and the .rsprite format.
 *
 * - LrgSpriteAnimation: one clip (frames of an LrgSpriteSheet with
 *   durations, fps, loop mode, named events).
 * - LrgSpriteSheet .rsprite support: grid or explicit slicing with pivots,
 *   alpha-island auto slicing, clips; load and save.
 * - LrgSpriteAnimationPlayer: a component that plays clips and drives the
 *   game object's LrgSpriteComponent.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-sprite-sheet.h"
#include "../ecs/lrg-component.h"
#include "../ecs/components/lrg-animator-component.h"


G_BEGIN_DECLS

/* ==========================================================================
 * Errors and enums
 * ========================================================================== */

/**
 * LRG_SPRITE_ANIMATION_ERROR:
 *
 * Error domain of the .rsprite reader and writer.
 */
#define LRG_SPRITE_ANIMATION_ERROR (lrg_sprite_animation_error_quark ())

/**
 * lrg_sprite_animation_error_quark:
 *
 * Returns: the error quark of #LRG_SPRITE_ANIMATION_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_sprite_animation_error_quark (void);

/**
 * LrgSpriteAnimationError:
 * @LRG_SPRITE_ANIMATION_ERROR_IO: the file could not be read or written
 * @LRG_SPRITE_ANIMATION_ERROR_PARSE: the text is not valid YAML
 * @LRG_SPRITE_ANIMATION_ERROR_INVALID: the document breaks the .rsprite
 *   schema; the message names the key path
 *
 * Errors of the .rsprite reader and writer.
 */
typedef enum
{
	LRG_SPRITE_ANIMATION_ERROR_IO,
	LRG_SPRITE_ANIMATION_ERROR_PARSE,
	LRG_SPRITE_ANIMATION_ERROR_INVALID
} LrgSpriteAnimationError;

LRG_AVAILABLE_IN_ALL
GType lrg_sprite_animation_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SPRITE_ANIMATION_ERROR (lrg_sprite_animation_error_get_type ())

/**
 * LrgSpriteLoopMode:
 * @LRG_SPRITE_LOOP_ONCE: play once and hold the last frame
 * @LRG_SPRITE_LOOP_LOOP: start again after the last frame
 * @LRG_SPRITE_LOOP_PING_PONG: play forward, then backward, and repeat (the
 *   end frames are not doubled)
 *
 * How a sprite animation continues after its last frame.
 */
typedef enum
{
	LRG_SPRITE_LOOP_ONCE,
	LRG_SPRITE_LOOP_LOOP,
	LRG_SPRITE_LOOP_PING_PONG
} LrgSpriteLoopMode;

LRG_AVAILABLE_IN_ALL
GType lrg_sprite_loop_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SPRITE_LOOP_MODE (lrg_sprite_loop_mode_get_type ())

/* ==========================================================================
 * LrgSpriteAnimation
 * ========================================================================== */

#define LRG_TYPE_SPRITE_ANIMATION (lrg_sprite_animation_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSpriteAnimation, lrg_sprite_animation, LRG, SPRITE_ANIMATION, GObject)

/**
 * lrg_sprite_animation_new:
 * @name: the clip's name (unique within its sheet)
 *
 * Creates an empty clip at 10 frames per second that loops.
 *
 * Returns: (transfer full): a new #LrgSpriteAnimation
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteAnimation * lrg_sprite_animation_new (const gchar *name);

/**
 * lrg_sprite_animation_get_name:
 * @self: an #LrgSpriteAnimation
 *
 * Returns: (transfer none): the clip's name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sprite_animation_get_name (LrgSpriteAnimation *self);

/**
 * lrg_sprite_animation_set_name:
 * @self: an #LrgSpriteAnimation
 * @name: the new name
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_animation_set_name (LrgSpriteAnimation *self,
                                    const gchar        *name);

/**
 * lrg_sprite_animation_get_fps:
 * @self: an #LrgSpriteAnimation
 *
 * Returns: frames per second for frames without their own duration
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sprite_animation_get_fps (LrgSpriteAnimation *self);

/**
 * lrg_sprite_animation_set_fps:
 * @self: an #LrgSpriteAnimation
 * @fps: frames per second (> 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_animation_set_fps (LrgSpriteAnimation *self,
                                   gfloat              fps);

/**
 * lrg_sprite_animation_get_loop_mode:
 * @self: an #LrgSpriteAnimation
 *
 * Returns: what happens after the last frame
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteLoopMode lrg_sprite_animation_get_loop_mode (LrgSpriteAnimation *self);

/**
 * lrg_sprite_animation_set_loop_mode:
 * @self: an #LrgSpriteAnimation
 * @mode: what happens after the last frame
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_animation_set_loop_mode (LrgSpriteAnimation *self,
                                         LrgSpriteLoopMode   mode);

/**
 * lrg_sprite_animation_add_frame:
 * @self: an #LrgSpriteAnimation
 * @frame: the sheet frame index shown
 * @duration: seconds the frame shows, or 0 for 1 / fps
 *
 * Appends a frame.  A sheet frame may appear several times.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_animation_add_frame (LrgSpriteAnimation *self,
                                     guint               frame,
                                     gfloat              duration);

/**
 * lrg_sprite_animation_insert_frame:
 * @self: an #LrgSpriteAnimation
 * @index: position in the clip (clamped)
 * @frame: the sheet frame index shown
 * @duration: seconds, or 0 for 1 / fps
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_animation_insert_frame (LrgSpriteAnimation *self,
                                        guint               index,
                                        guint               frame,
                                        gfloat              duration);

/**
 * lrg_sprite_animation_remove_frame:
 * @self: an #LrgSpriteAnimation
 * @index: position in the clip
 *
 * Removes a frame; events on later frames move down with them, events on
 * the removed frame are dropped.
 *
 * Returns: %TRUE if a frame was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sprite_animation_remove_frame (LrgSpriteAnimation *self,
                                            guint               index);

/**
 * lrg_sprite_animation_clear_frames:
 * @self: an #LrgSpriteAnimation
 *
 * Removes every frame and event.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_animation_clear_frames (LrgSpriteAnimation *self);

/**
 * lrg_sprite_animation_get_n_frames:
 * @self: an #LrgSpriteAnimation
 *
 * Returns: the number of frames in the clip
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sprite_animation_get_n_frames (LrgSpriteAnimation *self);

/**
 * lrg_sprite_animation_get_frame:
 * @self: an #LrgSpriteAnimation
 * @index: position in the clip
 * @out_duration: (out) (optional): the frame's own duration (0: 1 / fps)
 *
 * Returns: the sheet frame index, or 0 if @index is out of range
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sprite_animation_get_frame (LrgSpriteAnimation *self,
                                      guint               index,
                                      gfloat             *out_duration);

/**
 * lrg_sprite_animation_set_frame:
 * @self: an #LrgSpriteAnimation
 * @index: position in the clip
 * @frame: the sheet frame index
 * @duration: seconds, or 0 for 1 / fps
 *
 * Returns: %TRUE if @index was valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sprite_animation_set_frame (LrgSpriteAnimation *self,
                                         guint               index,
                                         guint               frame,
                                         gfloat              duration);

/**
 * lrg_sprite_animation_get_frame_duration:
 * @self: an #LrgSpriteAnimation
 * @index: position in the clip
 *
 * Returns: how long the frame shows in seconds (its own duration or
 *   1 / fps), 0 if @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sprite_animation_get_frame_duration (LrgSpriteAnimation *self,
                                                guint               index);

/**
 * lrg_sprite_animation_get_duration:
 * @self: an #LrgSpriteAnimation
 *
 * Gets the length of one cycle: every frame once (once, loop), or forward
 * and back without repeating the end frames (ping-pong).
 *
 * Returns: seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sprite_animation_get_duration (LrgSpriteAnimation *self);

/**
 * lrg_sprite_animation_sample:
 * @self: an #LrgSpriteAnimation
 * @time: seconds since the clip started
 * @out_finished: (out) (optional): %TRUE when a once clip has ended
 *
 * Finds which clip frame shows at a time.  Pure and deterministic: the
 * player and editor previews both sample with it.
 *
 * Returns: the position in the clip (not the sheet frame), 0 for an empty
 *   clip
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sprite_animation_sample (LrgSpriteAnimation *self,
                                   gfloat              time,
                                   gboolean           *out_finished);

/**
 * lrg_sprite_animation_add_event:
 * @self: an #LrgSpriteAnimation
 * @frame: the clip position the event fires on (when that frame starts)
 * @name: the event name (footstep, hit, spawn...)
 *
 * Adds a named event, emitted as #LrgSpriteAnimationPlayer::event.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_animation_add_event (LrgSpriteAnimation *self,
                                     guint               frame,
                                     const gchar        *name);

/**
 * lrg_sprite_animation_get_n_events:
 * @self: an #LrgSpriteAnimation
 *
 * Returns: the number of events
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sprite_animation_get_n_events (LrgSpriteAnimation *self);

/**
 * lrg_sprite_animation_get_event:
 * @self: an #LrgSpriteAnimation
 * @index: the event index (events are kept sorted by frame, then by
 *   insertion)
 * @out_frame: (out) (optional): the clip position it fires on
 *
 * Returns: (transfer none) (nullable): the event name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_sprite_animation_get_event (LrgSpriteAnimation *self,
                                              guint               index,
                                              guint              *out_frame);

/**
 * lrg_sprite_animation_remove_event:
 * @self: an #LrgSpriteAnimation
 * @index: the event index
 *
 * Returns: %TRUE if an event was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sprite_animation_remove_event (LrgSpriteAnimation *self,
                                            guint               index);

/* ==========================================================================
 * LrgSpriteSheet: slicing, clips and .rsprite
 * ========================================================================== */

/**
 * lrg_sprite_sheet_slice_grid:
 * @self: an #LrgSpriteSheet
 * @frame_width: frame width in pixels
 * @frame_height: frame height in pixels
 * @columns: frames per row
 * @rows: rows of frames
 * @count: frames to take (0: @columns x @rows)
 * @margin_x: pixels left of the first column
 * @margin_y: pixels above the first row
 * @spacing_x: pixels between columns
 * @spacing_y: pixels between rows
 * @pivot_x: pivot of every frame, pixels from its left edge
 * @pivot_y: pivot, pixels from its top edge
 *
 * Replaces the sheet's frames with a grid, row by row, named
 * `frame_<index>`.  The grid is remembered and saved as the .rsprite
 * `grid:` block; frames added afterwards are saved as explicit frames.
 *
 * Returns: the number of frames created
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sprite_sheet_slice_grid (LrgSpriteSheet *self,
                                   gint            frame_width,
                                   gint            frame_height,
                                   guint           columns,
                                   guint           rows,
                                   guint           count,
                                   gint            margin_x,
                                   gint            margin_y,
                                   gint            spacing_x,
                                   gint            spacing_y,
                                   gfloat          pivot_x,
                                   gfloat          pivot_y);

/**
 * lrg_sprite_sheet_get_grid:
 * @self: an #LrgSpriteSheet
 * @frame_width: (out) (optional): frame width
 * @frame_height: (out) (optional): frame height
 * @columns: (out) (optional): frames per row
 * @count: (out) (optional): grid frames (the first frames of the sheet)
 *
 * Returns: %TRUE if the sheet's first frames come from
 *   lrg_sprite_sheet_slice_grid()
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sprite_sheet_get_grid (LrgSpriteSheet *self,
                                    gint           *frame_width,
                                    gint           *frame_height,
                                    guint          *columns,
                                    guint          *count);

/**
 * lrg_sprite_sheet_slice_alpha:
 * @self: an #LrgSpriteSheet
 * @rgba: (array length=n_bytes): RGBA8 pixels of the sheet image
 * @n_bytes: size of @rgba
 * @width: image width
 * @height: image height
 * @alpha_threshold: pixels with alpha above this are part of a sprite
 * @min_size: islands whose width or height is below this are ignored
 * @pivot_x: pivot as a fraction of each frame's width (0.5: centre)
 * @pivot_y: pivot as a fraction of each frame's height (1: bottom)
 *
 * Automatic slicing: replaces the frames with the bounding boxes of the
 * image's opaque islands (8-connected), ordered in rows top to bottom and
 * left to right.  The sprite slicer's "auto" button.
 *
 * Returns: the number of frames created
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sprite_sheet_slice_alpha (LrgSpriteSheet *self,
                                    const guint8   *rgba,
                                    gsize           n_bytes,
                                    guint           width,
                                    guint           height,
                                    guint8          alpha_threshold,
                                    guint           min_size,
                                    gfloat          pivot_x,
                                    gfloat          pivot_y);

/**
 * lrg_sprite_sheet_add_clip:
 * @self: an #LrgSpriteSheet
 * @clip: (transfer none): the clip
 *
 * Adds a frame animation clip (replacing a clip with the same name).
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_sheet_add_clip (LrgSpriteSheet     *self,
                                LrgSpriteAnimation *clip);

/**
 * lrg_sprite_sheet_remove_clip:
 * @self: an #LrgSpriteSheet
 * @name: the clip's name
 *
 * Returns: %TRUE if a clip was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sprite_sheet_remove_clip (LrgSpriteSheet *self,
                                       const gchar    *name);

/**
 * lrg_sprite_sheet_get_clip:
 * @self: an #LrgSpriteSheet
 * @name: the clip's name
 *
 * Returns: (transfer none) (nullable): the clip
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteAnimation * lrg_sprite_sheet_get_clip (LrgSpriteSheet *self,
                                                const gchar    *name);

/**
 * lrg_sprite_sheet_get_n_clips:
 * @self: an #LrgSpriteSheet
 *
 * Returns: the number of clips
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sprite_sheet_get_n_clips (LrgSpriteSheet *self);

/**
 * lrg_sprite_sheet_get_clip_at:
 * @self: an #LrgSpriteSheet
 * @index: the clip index (clips keep the order they were added in)
 *
 * Returns: (transfer none) (nullable): the clip
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteAnimation * lrg_sprite_sheet_get_clip_at (LrgSpriteSheet *self,
                                                   guint           index);

/**
 * lrg_sprite_sheet_new_from_rsprite_file:
 * @path: path to an .rsprite file
 * @error: return location for an error
 *
 * Loads an .rsprite: the image reference, slicing (grid and/or explicit
 * frames with pivots) and frame animation clips.  No GL context needed;
 * the image path is relative to the file (load the texture yourself, or
 * let #LrgSpriteAnimationPlayer:texture carry it).
 *
 * Returns: (transfer full) (nullable): the sheet, or %NULL with @error
 *   set (%LRG_SPRITE_ANIMATION_ERROR)
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteSheet * lrg_sprite_sheet_new_from_rsprite_file (const gchar  *path,
                                                         GError      **error);

/**
 * lrg_sprite_sheet_new_from_rsprite_data:
 * @data: .rsprite text
 * @length: length of @data, or -1 if NUL-terminated
 * @source: (nullable): name used in error messages
 * @error: return location for an error
 *
 * Parses .rsprite text; see lrg_sprite_sheet_new_from_rsprite_file().
 *
 * Returns: (transfer full) (nullable): the sheet, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteSheet * lrg_sprite_sheet_new_from_rsprite_data (const gchar  *data,
                                                         gssize        length,
                                                         const gchar  *source,
                                                         GError      **error);

/**
 * lrg_sprite_sheet_save_rsprite_to_data:
 * @self: an #LrgSpriteSheet
 * @length: (out) (optional): length of the text
 * @error: return location for an error
 *
 * Writes the sheet as .rsprite text.  The same sheet always gives the
 * same bytes; unknown keys of a loaded file are written back.
 *
 * Returns: (transfer full) (nullable): the text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_sprite_sheet_save_rsprite_to_data (LrgSpriteSheet  *self,
                                               gsize           *length,
                                               GError         **error);

/**
 * lrg_sprite_sheet_save_rsprite_to_file:
 * @self: an #LrgSpriteSheet
 * @path: where to write
 * @error: return location for an error
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sprite_sheet_save_rsprite_to_file (LrgSpriteSheet  *self,
                                                const gchar     *path,
                                                GError         **error);

/**
 * lrg_sprite_sheet_apply_to_animator:
 * @self: an #LrgSpriteSheet with a grid (lrg_sprite_sheet_slice_grid()
 *   without margins or spacing)
 * @animator: the animator component
 * @texture: (nullable): the sheet image, set on the animator with the
 *   grid's frame size
 *
 * Registers the sheet's clips with an #LrgAnimatorComponent, which plays
 * runs of consecutive grid frames at a fixed rate: clips whose frames are
 * consecutive ascending grid frames with one duration and that do not
 * ping-pong.  Other clips need #LrgSpriteAnimationPlayer.
 *
 * Returns: the number of clips registered
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sprite_sheet_apply_to_animator (LrgSpriteSheet       *self,
                                          LrgAnimatorComponent *animator,
                                          GrlTexture           *texture);

/* ==========================================================================
 * LrgSpriteAnimationPlayer
 * ========================================================================== */

#define LRG_TYPE_SPRITE_ANIMATION_PLAYER (lrg_sprite_animation_player_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSpriteAnimationPlayer, lrg_sprite_animation_player,
                      LRG, SPRITE_ANIMATION_PLAYER, LrgComponent)

/**
 * lrg_sprite_animation_player_new:
 * @sheet: (nullable): the sheet whose clips are played
 *
 * Creates a player component.  Attached to an #LrgGameObject it advances
 * every update and writes the current frame (and #LrgSpriteAnimationPlayer:texture
 * when set) into the object's #LrgSpriteComponent; it also works
 * stand-alone with lrg_sprite_animation_player_advance().
 *
 * Returns: (transfer full): a new #LrgSpriteAnimationPlayer
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteAnimationPlayer * lrg_sprite_animation_player_new (LrgSpriteSheet *sheet);

/**
 * lrg_sprite_animation_player_get_sheet:
 * @self: an #LrgSpriteAnimationPlayer
 *
 * Returns: (transfer none) (nullable): the sheet
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteSheet * lrg_sprite_animation_player_get_sheet (LrgSpriteAnimationPlayer *self);

/**
 * lrg_sprite_animation_player_set_sheet:
 * @self: an #LrgSpriteAnimationPlayer
 * @sheet: (nullable): the sheet; stops playback
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_animation_player_set_sheet (LrgSpriteAnimationPlayer *self,
                                            LrgSpriteSheet           *sheet);

/**
 * lrg_sprite_animation_player_get_texture:
 * @self: an #LrgSpriteAnimationPlayer
 *
 * Returns: (transfer none) (nullable): the sheet image
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_sprite_animation_player_get_texture (LrgSpriteAnimationPlayer *self);

/**
 * lrg_sprite_animation_player_set_texture:
 * @self: an #LrgSpriteAnimationPlayer
 * @texture: (nullable): the sheet image, pushed to the sprite component
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_animation_player_set_texture (LrgSpriteAnimationPlayer *self,
                                              GrlTexture               *texture);

/**
 * lrg_sprite_animation_player_play:
 * @self: an #LrgSpriteAnimationPlayer
 * @name: the clip to play
 *
 * Starts a clip from its first frame (restarts it if already playing).
 *
 * Returns: %TRUE if the sheet has the clip
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sprite_animation_player_play (LrgSpriteAnimationPlayer *self,
                                           const gchar              *name);

/**
 * lrg_sprite_animation_player_play_if_different:
 * @self: an #LrgSpriteAnimationPlayer
 * @name: the clip to play
 *
 * Starts a clip unless it is already the current one (state-driven
 * animation: call it every frame with the wanted clip).
 *
 * Returns: %TRUE if the clip is playing afterwards
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sprite_animation_player_play_if_different (LrgSpriteAnimationPlayer *self,
                                                        const gchar              *name);

/**
 * lrg_sprite_animation_player_stop:
 * @self: an #LrgSpriteAnimationPlayer
 *
 * Stops and rewinds.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_animation_player_stop (LrgSpriteAnimationPlayer *self);

/**
 * lrg_sprite_animation_player_set_paused:
 * @self: an #LrgSpriteAnimationPlayer
 * @paused: %TRUE to hold the current frame
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_animation_player_set_paused (LrgSpriteAnimationPlayer *self,
                                             gboolean                  paused);

/**
 * lrg_sprite_animation_player_get_paused:
 * @self: an #LrgSpriteAnimationPlayer
 *
 * Returns: whether playback is paused
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sprite_animation_player_get_paused (LrgSpriteAnimationPlayer *self);

/**
 * lrg_sprite_animation_player_is_playing:
 * @self: an #LrgSpriteAnimationPlayer
 *
 * Returns: %TRUE while a clip runs (not stopped, not finished)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sprite_animation_player_is_playing (LrgSpriteAnimationPlayer *self);

/**
 * lrg_sprite_animation_player_get_current_clip:
 * @self: an #LrgSpriteAnimationPlayer
 *
 * Returns: (transfer none) (nullable): the current clip
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteAnimation * lrg_sprite_animation_player_get_current_clip (LrgSpriteAnimationPlayer *self);

/**
 * lrg_sprite_animation_player_get_time:
 * @self: an #LrgSpriteAnimationPlayer
 *
 * Returns: seconds since the current clip started (speed applied)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sprite_animation_player_get_time (LrgSpriteAnimationPlayer *self);

/**
 * lrg_sprite_animation_player_get_frame:
 * @self: an #LrgSpriteAnimationPlayer
 *
 * Returns: the sheet frame index currently shown (0 without a clip)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sprite_animation_player_get_frame (LrgSpriteAnimationPlayer *self);

/**
 * lrg_sprite_animation_player_get_region:
 * @self: an #LrgSpriteAnimationPlayer
 *
 * Returns: (transfer none) (nullable): the sheet frame currently shown
 *   (rectangle and pivot)
 */
LRG_AVAILABLE_IN_ALL
LrgAtlasRegion * lrg_sprite_animation_player_get_region (LrgSpriteAnimationPlayer *self);

/**
 * lrg_sprite_animation_player_get_speed:
 * @self: an #LrgSpriteAnimationPlayer
 *
 * Returns: the playback speed multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sprite_animation_player_get_speed (LrgSpriteAnimationPlayer *self);

/**
 * lrg_sprite_animation_player_set_speed:
 * @self: an #LrgSpriteAnimationPlayer
 * @speed: playback speed multiplier (>= 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_animation_player_set_speed (LrgSpriteAnimationPlayer *self,
                                            gfloat                    speed);

/**
 * lrg_sprite_animation_player_advance:
 * @self: an #LrgSpriteAnimationPlayer
 * @delta: seconds to advance (before the speed multiplier)
 *
 * Advances playback, emitting #LrgSpriteAnimationPlayer::event for every
 * event frame entered (also across loops), ::frame-changed and, for a
 * once clip, ::finished.  The component's update calls this.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sprite_animation_player_advance (LrgSpriteAnimationPlayer *self,
                                          gfloat                    delta);

G_END_DECLS
