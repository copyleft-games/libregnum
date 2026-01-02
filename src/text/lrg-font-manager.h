/* lrg-font-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Font loading and caching manager.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_FONT_MANAGER (lrg_font_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFontManager, lrg_font_manager, LRG, FONT_MANAGER, GObject)

/**
 * lrg_font_manager_get_default:
 *
 * Gets the default font manager instance.
 *
 * Returns: (transfer none): The default #LrgFontManager
 */
LRG_AVAILABLE_IN_ALL
LrgFontManager *    lrg_font_manager_get_default        (void);

/**
 * lrg_font_manager_initialize:
 * @self: A #LrgFontManager
 * @error: (nullable): Return location for error
 *
 * Initializes the font manager by searching for system fonts.
 * This is called automatically during engine startup if a window exists.
 *
 * On Linux, searches for Liberation Sans, Noto Sans, or DejaVu Sans.
 * On Windows, searches for Segoe UI, Arial, or Verdana.
 *
 * Loads the first available font at multiple sizes (12, 16, 24) for
 * ui-small, ui-normal, and ui-large presets.
 *
 * Returns: %TRUE if at least one font was loaded, %FALSE otherwise
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_font_manager_initialize         (LrgFontManager *self,
                                                         GError        **error);

/**
 * lrg_font_manager_get_font:
 * @self: A #LrgFontManager
 * @name: (nullable): Font name, or %NULL for default
 *
 * Gets a loaded font by name.
 *
 * Returns: (transfer none) (nullable): The #GrlFont, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
GrlFont *           lrg_font_manager_get_font           (LrgFontManager *self,
                                                         const gchar    *name);

/**
 * lrg_font_manager_get_default_font:
 * @self: A #LrgFontManager
 *
 * Gets the default font object.
 *
 * Returns: (transfer none) (nullable): The default #GrlFont
 */
LRG_AVAILABLE_IN_ALL
GrlFont *           lrg_font_manager_get_default_font   (LrgFontManager *self);

/**
 * lrg_font_manager_load_font:
 * @self: A #LrgFontManager
 * @name: A unique name for the font
 * @path: Path to the font file
 * @size: Base font size in pixels
 * @error: (nullable): Return location for error
 *
 * Loads a font from a file and caches it.
 *
 * Returns: %TRUE if the font was loaded successfully
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_font_manager_load_font          (LrgFontManager *self,
                                                         const gchar    *name,
                                                         const gchar    *path,
                                                         gint            size,
                                                         GError        **error);

/**
 * lrg_font_manager_has_font:
 * @self: A #LrgFontManager
 * @name: Font name
 *
 * Checks if a font is loaded.
 *
 * Returns: %TRUE if the font is loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_font_manager_has_font           (LrgFontManager *self,
                                                         const gchar    *name);

/**
 * lrg_font_manager_unload_font:
 * @self: A #LrgFontManager
 * @name: Font name to unload
 *
 * Unloads a font and frees its resources.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_font_manager_unload_font        (LrgFontManager *self,
                                                         const gchar    *name);

/**
 * lrg_font_manager_unload_all:
 * @self: A #LrgFontManager
 *
 * Unloads all fonts.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_font_manager_unload_all         (LrgFontManager *self);

/**
 * lrg_font_manager_get_default_font_name:
 * @self: A #LrgFontManager
 *
 * Gets the default font name.
 *
 * Returns: (transfer none) (nullable): The default font name
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_font_manager_get_default_font_name (LrgFontManager *self);

/**
 * lrg_font_manager_set_default_font_name:
 * @self: A #LrgFontManager
 * @name: The default font name
 *
 * Sets which loaded font to use as the default.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_font_manager_set_default_font_name (LrgFontManager *self,
                                                            const gchar    *name);

/**
 * lrg_font_manager_get_font_names:
 * @self: A #LrgFontManager
 *
 * Gets a list of all loaded font names.
 *
 * Returns: (transfer container) (element-type utf8): Array of font names
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_font_manager_get_font_names     (LrgFontManager *self);

/**
 * lrg_font_manager_measure_text:
 * @self: A #LrgFontManager
 * @font_name: (nullable): Font to use, or %NULL for default
 * @text: Text to measure
 * @font_size: Font size in pixels
 * @width: (out): Resulting width
 * @height: (out): Resulting height
 *
 * Measures the dimensions of rendered text.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_font_manager_measure_text       (LrgFontManager *self,
                                                         const gchar    *font_name,
                                                         const gchar    *text,
                                                         gfloat          font_size,
                                                         gfloat         *width,
                                                         gfloat         *height);

/**
 * lrg_font_manager_draw_text:
 * @self: A #LrgFontManager
 * @font_name: (nullable): Font to use, or %NULL for default
 * @text: Text to draw
 * @x: X position
 * @y: Y position
 * @font_size: Font size in pixels
 * @r: Red component (0-255)
 * @g: Green component (0-255)
 * @b: Blue component (0-255)
 * @a: Alpha component (0-255)
 *
 * Draws text at the specified position.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_font_manager_draw_text          (LrgFontManager *self,
                                                         const gchar    *font_name,
                                                         const gchar    *text,
                                                         gfloat          x,
                                                         gfloat          y,
                                                         gfloat          font_size,
                                                         guint8          r,
                                                         guint8          g,
                                                         guint8          b,
                                                         guint8          a);

G_END_DECLS
