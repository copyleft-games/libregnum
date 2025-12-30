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

G_END_DECLS
