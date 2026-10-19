/* lrg-photo-mode.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhotoMode - Singleton controller for photo mode.
 *
 * Manages the photo mode state, coordinates free camera control,
 * UI visibility, and screenshot capture.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-screenshot.h"
#include "lrg-photo-camera-controller.h"
#include "lrg-photo-host.h"
#include "lrg-photo-look.h"
#include "lrg-photo-dof.h"
#include "lrg-photo-depth-target.h"
#include "lrg-photo-overlay.h"

G_BEGIN_DECLS

#define LRG_TYPE_PHOTO_MODE (lrg_photo_mode_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPhotoMode, lrg_photo_mode, LRG, PHOTO_MODE, GObject)

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_photo_mode_get_default:
 *
 * Gets the default photo mode instance.
 *
 * Returns: (transfer none): the default #LrgPhotoMode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoMode *
lrg_photo_mode_get_default (void);

/* ==========================================================================
 * State
 * ========================================================================== */

/**
 * lrg_photo_mode_is_active:
 * @self: an #LrgPhotoMode
 *
 * Checks if photo mode is currently active.
 *
 * Returns: %TRUE if photo mode is active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_is_active (LrgPhotoMode *self);

/**
 * lrg_photo_mode_enter:
 * @self: an #LrgPhotoMode
 * @game_camera: (nullable): the game camera to initialize from
 * @error: (nullable): return location for error
 *
 * Enters photo mode, optionally initializing from a game camera.
 * This pauses the game and enables free camera control.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_enter (LrgPhotoMode  *self,
                      LrgCamera3D   *game_camera,
                      GError       **error);

/**
 * lrg_photo_mode_exit:
 * @self: an #LrgPhotoMode
 *
 * Exits photo mode, resuming normal game operation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_exit (LrgPhotoMode *self);

/**
 * lrg_photo_mode_toggle:
 * @self: an #LrgPhotoMode
 * @game_camera: (nullable): the game camera (used when entering)
 * @error: (nullable): return location for error
 *
 * Toggles photo mode on or off.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_toggle (LrgPhotoMode  *self,
                       LrgCamera3D   *game_camera,
                       GError       **error);

/* ==========================================================================
 * Camera
 * ========================================================================== */

/**
 * lrg_photo_mode_get_camera_controller:
 * @self: an #LrgPhotoMode
 *
 * Gets the photo mode camera controller.
 * Only valid while photo mode is active.
 *
 * Returns: (transfer none) (nullable): the #LrgPhotoCameraController
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoCameraController *
lrg_photo_mode_get_camera_controller (LrgPhotoMode *self);

/**
 * lrg_photo_mode_get_camera:
 * @self: an #LrgPhotoMode
 *
 * Gets the photo mode camera for rendering.
 * Only valid while photo mode is active.
 *
 * Returns: (transfer none) (nullable): the #LrgCamera3D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCamera3D *
lrg_photo_mode_get_camera (LrgPhotoMode *self);

/* ==========================================================================
 * UI Visibility
 * ========================================================================== */

/**
 * lrg_photo_mode_get_ui_visible:
 * @self: an #LrgPhotoMode
 *
 * Gets whether UI elements should be visible.
 *
 * Returns: %TRUE if UI should be shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_get_ui_visible (LrgPhotoMode *self);

/**
 * lrg_photo_mode_set_ui_visible:
 * @self: an #LrgPhotoMode
 * @visible: whether UI should be visible
 *
 * Sets whether UI elements should be visible.
 * Games should hide their HUD when this is %FALSE.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_set_ui_visible (LrgPhotoMode *self,
                               gboolean      visible);

/**
 * lrg_photo_mode_toggle_ui:
 * @self: an #LrgPhotoMode
 *
 * Toggles UI visibility.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_toggle_ui (LrgPhotoMode *self);

/* ==========================================================================
 * Screenshot
 * ========================================================================== */

/**
 * lrg_photo_mode_capture:
 * @self: an #LrgPhotoMode
 * @error: (nullable): return location for error
 *
 * Captures a screenshot of the current frame.
 *
 * Returns: (transfer full) (nullable): a new #LrgScreenshot, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScreenshot *
lrg_photo_mode_capture (LrgPhotoMode  *self,
                        GError       **error);

/**
 * lrg_photo_mode_capture_and_save:
 * @self: an #LrgPhotoMode
 * @path: the file path to save to
 * @format: the image format
 * @error: (nullable): return location for error
 *
 * Captures and saves a screenshot in one operation.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_capture_and_save (LrgPhotoMode        *self,
                                 const gchar         *path,
                                 LrgScreenshotFormat  format,
                                 GError             **error);

/**
 * lrg_photo_mode_get_screenshot_directory:
 * @self: an #LrgPhotoMode
 *
 * Gets the directory where screenshots are saved.
 *
 * Returns: (transfer none): the screenshot directory path
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_photo_mode_get_screenshot_directory (LrgPhotoMode *self);

/**
 * lrg_photo_mode_set_screenshot_directory:
 * @self: an #LrgPhotoMode
 * @directory: the screenshot directory path
 *
 * Sets the directory where screenshots are saved.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_set_screenshot_directory (LrgPhotoMode *self,
                                         const gchar  *directory);

/**
 * lrg_photo_mode_get_default_format:
 * @self: an #LrgPhotoMode
 *
 * Gets the default screenshot format.
 *
 * Returns: the #LrgScreenshotFormat
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScreenshotFormat
lrg_photo_mode_get_default_format (LrgPhotoMode *self);

/**
 * lrg_photo_mode_set_default_format:
 * @self: an #LrgPhotoMode
 * @format: the default format
 *
 * Sets the default screenshot format.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_set_default_format (LrgPhotoMode        *self,
                                   LrgScreenshotFormat  format);

/**
 * lrg_photo_mode_generate_filename:
 * @self: an #LrgPhotoMode
 * @format: the screenshot format
 *
 * Generates a unique filename for a screenshot.
 *
 * Returns: (transfer full): a newly allocated filename
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_photo_mode_generate_filename (LrgPhotoMode        *self,
                                  LrgScreenshotFormat  format);

/* ==========================================================================
 * Update
 * ========================================================================== */

/**
 * lrg_photo_mode_update:
 * @self: an #LrgPhotoMode
 * @delta: time elapsed since last update
 *
 * Updates photo mode, including camera controller.
 * Call this each frame while photo mode is active.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_update (LrgPhotoMode *self,
                       gfloat        delta);

/* ==========================================================================
 * Host, look, lens and overlays (M21)
 * ========================================================================== */

/**
 * lrg_photo_mode_get_host:
 * @self: an #LrgPhotoMode
 *
 * Gets the game's host.
 *
 * Returns: (transfer none) (nullable): the host
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoHost *
lrg_photo_mode_get_host (LrgPhotoMode *self);

/**
 * lrg_photo_mode_set_host:
 * @self: an #LrgPhotoMode
 * @host: (nullable): the host
 *
 * Sets the game's #LrgPhotoHost, which freezes the world, hides the
 * HUD and overrides time and weather.  Without a host photo mode only
 * moves the camera and captures.  Not allowed while active.  The mode
 * keeps a weak reference: the host usually owns the mode, and a host
 * that is finalized reads as none.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_set_host (LrgPhotoMode *self,
                         LrgPhotoHost *host);

/**
 * lrg_photo_mode_get_look:
 * @self: an #LrgPhotoMode
 *
 * Gets the colour look; edits apply at once (the host draws through it).
 *
 * Returns: (transfer none): the look
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoLook *
lrg_photo_mode_get_look (LrgPhotoMode *self);

/**
 * lrg_photo_mode_get_dof:
 * @self: an #LrgPhotoMode
 *
 * Gets the depth of field; its field of view follows the camera.
 *
 * Returns: (transfer none): the lens
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoDof *
lrg_photo_mode_get_dof (LrgPhotoMode *self);

/**
 * lrg_photo_mode_get_frame:
 * @self: an #LrgPhotoMode
 *
 * Gets the frame baked into photos.
 *
 * Returns: the frame
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoFrame
lrg_photo_mode_get_frame (LrgPhotoMode *self);

/**
 * lrg_photo_mode_set_frame:
 * @self: an #LrgPhotoMode
 * @frame: the frame
 *
 * Sets the frame baked into photos.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_set_frame (LrgPhotoMode *self,
                          LrgPhotoFrame frame);

/**
 * lrg_photo_mode_get_grid:
 * @self: an #LrgPhotoMode
 *
 * Gets the preview grid.
 *
 * Returns: the grid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoGrid
lrg_photo_mode_get_grid (LrgPhotoMode *self);

/**
 * lrg_photo_mode_set_grid:
 * @self: an #LrgPhotoMode
 * @grid: the grid
 *
 * Sets the preview grid (never baked into a photo).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_set_grid (LrgPhotoMode *self,
                         LrgPhotoGrid  grid);

/**
 * lrg_photo_mode_get_frame_color:
 * @self: an #LrgPhotoMode
 *
 * Gets the frame colour.
 *
 * Returns: 0xRRGGBB
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_photo_mode_get_frame_color (LrgPhotoMode *self);

/**
 * lrg_photo_mode_set_frame_color:
 * @self: an #LrgPhotoMode
 * @rgb: 0xRRGGBB
 *
 * Sets the frame colour.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_set_frame_color (LrgPhotoMode *self,
                                guint         rgb);

/* ==========================================================================
 * The world contract (M21)
 * ========================================================================== */

/**
 * lrg_photo_mode_get_pause_world:
 * @self: an #LrgPhotoMode
 *
 * Whether entering asks the host to freeze the world.
 *
 * Returns: %TRUE when it asks
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_get_pause_world (LrgPhotoMode *self);

/**
 * lrg_photo_mode_set_pause_world:
 * @self: an #LrgPhotoMode
 * @pause: whether to ask
 *
 * Chooses whether lrg_photo_mode_enter() asks the host to freeze
 * the world (default %TRUE).  A game whose world cannot stop (online)
 * returns %FALSE from its freeze method; the camera then simply
 * detaches and the world runs on, see lrg_photo_mode_is_world_frozen().
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_set_pause_world (LrgPhotoMode *self,
                                gboolean      pause);

/**
 * lrg_photo_mode_is_world_frozen:
 * @self: an #LrgPhotoMode
 *
 * Whether the host froze the world on entering.  When %FALSE the world
 * is running behind the camera (not asked, or refused).
 *
 * Returns: %TRUE when the world is frozen
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_is_world_frozen (LrgPhotoMode *self);

/**
 * lrg_photo_mode_get_hud_visible:
 * @self: an #LrgPhotoMode
 *
 * Whether the game's HUD is shown (default %FALSE in photo mode).
 *
 * Returns: %TRUE when shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_get_hud_visible (LrgPhotoMode *self);

/**
 * lrg_photo_mode_set_hud_visible:
 * @self: an #LrgPhotoMode
 * @visible: whether the HUD is drawn
 *
 * Shows or hides the game's HUD through the host while active.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_set_hud_visible (LrgPhotoMode *self,
                                gboolean      visible);

/**
 * lrg_photo_mode_set_time_of_day:
 * @self: an #LrgPhotoMode
 * @hour: the hour
 *
 * Overrides the game's hour of day (wrapped into 0 to 24).  The override
 * is cleared when photo mode exits.
 *
 * Returns: %TRUE when the host has a clock and took it
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_set_time_of_day (LrgPhotoMode *self,
                                gfloat        hour);

/**
 * lrg_photo_mode_get_time_of_day:
 * @self: an #LrgPhotoMode
 * @hour: (out): the hour
 *
 * Reads the game's current hour through the host.
 *
 * Returns: %FALSE when inactive or the game has no clock
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_get_time_of_day (LrgPhotoMode *self,
                                gfloat       *hour);

/**
 * lrg_photo_mode_has_time_override:
 * @self: an #LrgPhotoMode
 *
 * Whether the hour is overridden.
 *
 * Returns: %TRUE when overridden
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_has_time_override (LrgPhotoMode *self);

/**
 * lrg_photo_mode_clear_time_of_day:
 * @self: an #LrgPhotoMode
 *
 * Puts the hour of day back as the game had it.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_clear_time_of_day (LrgPhotoMode *self);

/**
 * lrg_photo_mode_list_weathers:
 * @self: an #LrgPhotoMode
 *
 * Lists the weathers the host lets photo mode choose.
 *
 * Returns: (transfer none) (nullable) (array zero-terminated=1): the ids, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * const *
lrg_photo_mode_list_weathers (LrgPhotoMode *self);

/**
 * lrg_photo_mode_set_weather:
 * @self: an #LrgPhotoMode
 * @weather: a listed weather id
 *
 * Overrides the weather with one of lrg_photo_mode_list_weathers().  The
 * override is cleared when photo mode exits.
 *
 * Returns: %TRUE when the host listed @weather and took it
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_set_weather (LrgPhotoMode *self,
                            const gchar  *weather);

/**
 * lrg_photo_mode_get_weather:
 * @self: an #LrgPhotoMode
 *
 * Gets the game's current weather id through the host.
 *
 * Returns: (transfer none) (nullable): the id
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_photo_mode_get_weather (LrgPhotoMode *self);

/**
 * lrg_photo_mode_get_weather_override:
 * @self: an #LrgPhotoMode
 *
 * Gets the weather photo mode set, if any.
 *
 * Returns: (transfer none) (nullable): the id
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_photo_mode_get_weather_override (LrgPhotoMode *self);

/**
 * lrg_photo_mode_clear_weather:
 * @self: an #LrgPhotoMode
 *
 * Puts the weather back as the game had it.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_clear_weather (LrgPhotoMode *self);

/* ==========================================================================
 * Capture (M21)
 * ========================================================================== */

/**
 * lrg_photo_mode_get_capture_scale:
 * @self: an #LrgPhotoMode
 *
 * Gets the supersampling of a capture.
 *
 * Returns: 1 to 8
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_photo_mode_get_capture_scale (LrgPhotoMode *self);

/**
 * lrg_photo_mode_set_capture_scale:
 * @self: an #LrgPhotoMode
 * @scale: the factor
 *
 * Sets the supersampling: the host renders at @scale times the window's
 * width and height (2 and 4 are the usual choices) and the photo has
 * that many pixels, clamped to 1 .. 8.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_set_capture_scale (LrgPhotoMode *self,
                                  guint         scale);

/**
 * lrg_photo_mode_get_max_capture_side:
 * @self: an #LrgPhotoMode
 *
 * Gets the longest side a capture may have.
 *
 * Returns: pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_photo_mode_get_max_capture_side (LrgPhotoMode *self);

/**
 * lrg_photo_mode_set_max_capture_side:
 * @self: an #LrgPhotoMode
 * @side: pixels, 256 .. 32768
 *
 * Sets the longest side a capture may have (the GPU's texture limit; the
 * default is 8192).  A scale that would exceed it is lowered by
 * lrg_photo_mode_get_capture_size().
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_set_max_capture_side (LrgPhotoMode *self,
                                     guint         side);

/**
 * lrg_photo_mode_get_filename_prefix:
 * @self: an #LrgPhotoMode
 *
 * Gets what photo file names start with.
 *
 * Returns: (transfer none): the prefix
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_photo_mode_get_filename_prefix (LrgPhotoMode *self);

/**
 * lrg_photo_mode_set_filename_prefix:
 * @self: an #LrgPhotoMode
 * @prefix: (nullable): the prefix
 *
 * Sets what file names start with ("screenshot" by default).  A prefix
 * with a slash is refused (the default is used).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_set_filename_prefix (LrgPhotoMode *self,
                                    const gchar  *prefix);

/**
 * lrg_photo_mode_get_capture_size:
 * @self: an #LrgPhotoMode
 * @width: the window's width
 * @height: the window's height
 * @out_width: (out) (optional): the capture's width
 * @out_height: (out) (optional): the capture's height
 *
 * Works out the size of a capture of a @width x @height window: the
 * capture scale, lowered until the longest side fits
 * #LrgPhotoMode:max-capture-side.
 *
 * Returns: the scale actually used
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_photo_mode_get_capture_size (LrgPhotoMode *self,
                                 gint          width,
                                 gint          height,
                                 gint         *out_width,
                                 gint         *out_height);

/**
 * lrg_photo_mode_set_extra_metadata:
 * @self: an #LrgPhotoMode
 * @key: the keyword
 * @value: (nullable): the text
 *
 * Adds a text entry to every photo (game title, version, save name).
 * A key of 1 to 79 characters; %NULL removes it.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_set_extra_metadata (LrgPhotoMode *self,
                                   const gchar  *key,
                                   const gchar  *value);

/**
 * lrg_photo_mode_request_capture:
 * @self: an #LrgPhotoMode
 * @error: (nullable): a location for an error
 *
 * Asks for a photo now: marks a capture pending and emits
 * #LrgPhotoMode::capture-requested, to which the host answers by rendering
 * one frame and calling lrg_photo_mode_finish_capture().
 *
 * Returns: %TRUE when requested; %FALSE with %LRG_PHOTO_MODE_ERROR_NOT_ACTIVE or
  %LRG_PHOTO_MODE_ERROR_BUSY
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_request_capture (LrgPhotoMode *self,
                                GError       **error);

/**
 * lrg_photo_mode_is_capture_pending:
 * @self: an #LrgPhotoMode
 *
 * Whether a requested capture has not been finished.
 *
 * Returns: %TRUE when pending
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_is_capture_pending (LrgPhotoMode *self);

/**
 * lrg_photo_mode_abort_capture:
 * @self: an #LrgPhotoMode
 *
 * Forgets a pending capture (the host could not render it).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_abort_capture (LrgPhotoMode *self);

/**
 * lrg_photo_mode_finish_capture:
 * @self: an #LrgPhotoMode
 * @image: the rendered picture
 * @apply_look: whether to grade it on the CPU
 * @error: (nullable): a location for an error
 *
 * Finishes a requested capture with the rendered @image: the frame is
 * painted on a copy (and, with @apply_look, the look on the CPU, for a host
 * that did not draw through the shaders), the text entries are added (camera,
 * lens, look, time, weather, the host's and the caller's) and the PNG is
 * written under the screenshot directory.  Emits
 * #LrgPhotoMode::screenshot-taken and #LrgPhotoMode::photo-saved.
 *
 * Returns: (transfer full): the path written, or %NULL with @error set
  (the capture is not pending any more)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_photo_mode_finish_capture (LrgPhotoMode *self,
                               GrlImage      *image,
                               gboolean       apply_look,
                               GError       **error);

/**
 * lrg_photo_mode_get_last_path:
 * @self: an #LrgPhotoMode
 *
 * Gets the path of the last photo written.
 *
 * Returns: (transfer none) (nullable): the path
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_photo_mode_get_last_path (LrgPhotoMode *self);

/* ==========================================================================
 * The self timer (M21)
 * ========================================================================== */

/**
 * lrg_photo_mode_get_timer_seconds:
 * @self: an #LrgPhotoMode
 *
 * Gets the self timer's delay.
 *
 * Returns: seconds, 0 for none
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_mode_get_timer_seconds (LrgPhotoMode *self);

/**
 * lrg_photo_mode_set_timer_seconds:
 * @self: an #LrgPhotoMode
 * @seconds: the delay
 *
 * Sets the self timer's delay (0 to 60 seconds).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_set_timer_seconds (LrgPhotoMode *self,
                                  gfloat        seconds);

/**
 * lrg_photo_mode_shoot:
 * @self: an #LrgPhotoMode
 * @error: (nullable): a location for an error
 *
 * The shutter button: with no timer set it requests a capture at once,
 * with one it starts the countdown (lrg_photo_mode_update() counts it
 * down, #LrgPhotoMode::timer-tick reports the seconds) and the capture is
 * requested when it reaches 0.
 *
 * Returns: %TRUE when started; %FALSE with %LRG_PHOTO_MODE_ERROR_NOT_ACTIVE or
  %LRG_PHOTO_MODE_ERROR_BUSY
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_shoot (LrgPhotoMode *self,
                      GError       **error);

/**
 * lrg_photo_mode_is_timer_running:
 * @self: an #LrgPhotoMode
 *
 * Whether a countdown is running.
 *
 * Returns: %TRUE when counting down
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_mode_is_timer_running (LrgPhotoMode *self);

/**
 * lrg_photo_mode_get_timer_remaining:
 * @self: an #LrgPhotoMode
 *
 * Gets the seconds left of a running countdown.
 *
 * Returns: seconds, 0 when not running
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_mode_get_timer_remaining (LrgPhotoMode *self);

/**
 * lrg_photo_mode_cancel_timer:
 * @self: an #LrgPhotoMode
 *
 * Stops a running countdown without taking the photo.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_mode_cancel_timer (LrgPhotoMode *self);

G_END_DECLS
