/* lrg-screenshot.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScreenshot - Screenshot capture and save.
 *
 * Captures the current frame and saves to PNG or JPG format.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCREENSHOT (lrg_screenshot_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScreenshot, lrg_screenshot, LRG, SCREENSHOT, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_screenshot_new:
 *
 * Creates a new empty screenshot.
 *
 * Returns: (transfer full): a new #LrgScreenshot
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScreenshot *
lrg_screenshot_new (void);

/**
 * lrg_screenshot_new_from_image:
 * @image: a #GrlImage containing the screenshot data
 *
 * Creates a new screenshot from an existing image.
 *
 * Returns: (transfer full): a new #LrgScreenshot
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScreenshot *
lrg_screenshot_new_from_image (GrlImage *image);

/**
 * lrg_screenshot_capture:
 * @error: (nullable): return location for error
 *
 * Captures the current frame from the screen.
 *
 * Returns: (transfer full) (nullable): a new #LrgScreenshot, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScreenshot *
lrg_screenshot_capture (GError **error);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_screenshot_get_width:
 * @self: an #LrgScreenshot
 *
 * Gets the screenshot width in pixels.
 *
 * Returns: the width
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_screenshot_get_width (LrgScreenshot *self);

/**
 * lrg_screenshot_get_height:
 * @self: an #LrgScreenshot
 *
 * Gets the screenshot height in pixels.
 *
 * Returns: the height
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_screenshot_get_height (LrgScreenshot *self);

/**
 * lrg_screenshot_get_image:
 * @self: an #LrgScreenshot
 *
 * Gets the underlying image data.
 *
 * Returns: (transfer none) (nullable): the #GrlImage, or %NULL if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlImage *
lrg_screenshot_get_image (LrgScreenshot *self);

/* ==========================================================================
 * Save Operations
 * ========================================================================== */

/**
 * lrg_screenshot_save:
 * @self: an #LrgScreenshot
 * @path: the file path to save to
 * @format: the image format
 * @error: (nullable): return location for error
 *
 * Saves the screenshot to a file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_screenshot_save (LrgScreenshot       *self,
                     const gchar         *path,
                     LrgScreenshotFormat  format,
                     GError             **error);

/**
 * lrg_screenshot_save_png:
 * @self: an #LrgScreenshot
 * @path: the file path to save to
 * @error: (nullable): return location for error
 *
 * Saves the screenshot as PNG (lossless).
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_screenshot_save_png (LrgScreenshot  *self,
                         const gchar    *path,
                         GError        **error);

/**
 * lrg_screenshot_save_jpg:
 * @self: an #LrgScreenshot
 * @path: the file path to save to
 * @quality: JPG quality (1-100)
 * @error: (nullable): return location for error
 *
 * Saves the screenshot as JPG with specified quality.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_screenshot_save_jpg (LrgScreenshot  *self,
                         const gchar    *path,
                         gint            quality,
                         GError        **error);

/**
 * lrg_screenshot_to_texture:
 * @self: an #LrgScreenshot
 *
 * Converts the screenshot to a texture for display.
 *
 * Returns: (transfer full) (nullable): a new #GrlTexture, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlTexture *
lrg_screenshot_to_texture (LrgScreenshot *self);

/* ==========================================================================
 * Metadata (PNG text chunks)
 * ========================================================================== */

/**
 * lrg_screenshot_new_from_render_texture:
 * @texture: a render texture that has been drawn into
 *
 * Reads @texture back from the GPU as a screenshot, flipped the right
 * way up (render textures are stored bottom up).  Needs a window.
 *
 * Returns: (transfer full) (nullable): a new #LrgScreenshot, or %NULL
 *   when the texture could not be read
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScreenshot *
lrg_screenshot_new_from_render_texture (GrlRenderTexture *texture);

/**
 * lrg_screenshot_set_metadata:
 * @self: an #LrgScreenshot
 * @key: the keyword, 1 to 79 Latin-1 characters (PNG's tEXt keyword)
 * @value: (nullable): the text, or %NULL to remove the key
 *
 * Sets a text entry that lrg_screenshot_save_png() writes as a tEXt
 * chunk (camera position, date, game): the keyword must not be empty or
 * longer than 79 bytes and neither side may hold a NUL.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_screenshot_set_metadata (LrgScreenshot *self,
                             const gchar   *key,
                             const gchar   *value);

/**
 * lrg_screenshot_get_metadata:
 * @self: an #LrgScreenshot
 * @key: the keyword
 *
 * Returns: (transfer none) (nullable): the text, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_screenshot_get_metadata (LrgScreenshot *self,
                             const gchar   *key);

/**
 * lrg_screenshot_get_metadata_keys:
 * @self: an #LrgScreenshot
 *
 * Returns: (transfer full) (array zero-terminated=1): the keywords in
 *   the order they were first set
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar **
lrg_screenshot_get_metadata_keys (LrgScreenshot *self);

/**
 * lrg_screenshot_read_png_metadata:
 * @path: a PNG file
 * @error: (nullable): a location for an error
 *
 * Reads the tEXt chunks of a PNG file (what lrg_screenshot_save_png()
 * wrote, or any other).  Checks the signature and every chunk's CRC.
 *
 * Returns: (transfer full) (element-type utf8 utf8) (nullable): the
 *   keyword to text table, or %NULL when the file is missing or not a
 *   valid PNG
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GHashTable *
lrg_screenshot_read_png_metadata (const gchar  *path,
                                  GError      **error);

G_END_DECLS
