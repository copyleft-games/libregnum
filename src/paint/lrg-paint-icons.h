/* lrg-paint-icons.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Built-in SDF icons and SVG icon rasterization.
 *
 * Built-in icons (#LrgIcon) are drawn from paint primitives (lines,
 * triangles, circles, rounded rectangles, curves): no textures, sharp
 * at any size, and they batch with everything else. SVG icons
 * (#LrgSvgIcon) are rasterized on the CPU with graylib's vector
 * renderer and cached as one texture per pixel size.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-paint-list.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Built-in icons
 * ========================================================================== */

/**
 * lrg_paint_list_icon:
 * @self: an #LrgPaintList
 * @icon: the icon
 * @x: left edge of the icon box
 * @y: top edge of the icon box
 * @size: width and height of the icon box
 * @thickness: stroke width, or 0 for the default (9% of @size)
 * @color: icon colour
 *
 * Records a built-in icon as a handful of SDF instances, laid out on
 * a square of @size with a little padding. Outlined icons use
 * @thickness; filled ones (play, star, heart ...) ignore it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_icon (LrgPaintList   *self,
                          LrgIcon         icon,
                          gfloat          x,
                          gfloat          y,
                          gfloat          size,
                          gfloat          thickness,
                          const GrlColor *color);

/**
 * lrg_paint_icon_from_name:
 * @name: an icon name: the #LrgIcon nick, e.g. "check", "chevron-down",
 *   "star-half"
 *
 * Returns: the icon, or %LRG_ICON_NONE for an unknown name
 */
LRG_AVAILABLE_IN_ALL
LrgIcon lrg_paint_icon_from_name (const gchar *name);

/**
 * lrg_paint_icon_get_name:
 * @icon: an icon
 *
 * Returns: (nullable): the icon's name, %NULL for an invalid value
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_paint_icon_get_name (LrgIcon icon);

/* ==========================================================================
 * SVG icons
 * ========================================================================== */

#define LRG_TYPE_SVG_ICON (lrg_svg_icon_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSvgIcon, lrg_svg_icon, LRG, SVG_ICON, GObject)

/**
 * lrg_svg_icon_new_from_file:
 * @path: (type filename): an SVG file
 * @error: return location for a #GError
 *
 * Parses an SVG icon. The drawing is fitted into a square (by the
 * bounds of its paths, centred, aspect kept) when rasterized.
 *
 * Returns: (transfer full) (nullable): the icon, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgSvgIcon * lrg_svg_icon_new_from_file (const gchar  *path,
                                         GError      **error);

/**
 * lrg_svg_icon_new_from_data:
 * @data: (array length=length) (element-type guint8): SVG source
 * @length: length of @data in bytes, or -1 when nul-terminated
 * @error: return location for a #GError
 *
 * Parses an SVG icon from memory. See lrg_svg_icon_new_from_file().
 *
 * Returns: (transfer full) (nullable): the icon, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgSvgIcon * lrg_svg_icon_new_from_data (const gchar  *data,
                                         gssize        length,
                                         GError      **error);

/**
 * lrg_svg_icon_get_n_shapes:
 * @self: an #LrgSvgIcon
 *
 * Returns: number of vector shapes in the icon
 */
LRG_AVAILABLE_IN_ALL
guint lrg_svg_icon_get_n_shapes (LrgSvgIcon *self);

/**
 * lrg_svg_icon_rasterize:
 * @self: an #LrgSvgIcon
 * @pixel_size: width and height of the image, 1..1024
 *
 * Renders the icon on the CPU into a new transparent RGBA image. Works
 * without a window.
 *
 * Returns: (transfer full): the image
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_svg_icon_rasterize (LrgSvgIcon *self,
                                   guint       pixel_size);

/**
 * lrg_svg_icon_get_texture:
 * @self: an #LrgSvgIcon
 * @pixel_size: width and height in pixels, 1..1024
 *
 * The icon rasterized at @pixel_size as a texture, created on first use
 * and cached (one texture per size). Needs an open window.
 *
 * Returns: (transfer none) (nullable): the texture, or %NULL without a
 *   window
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_svg_icon_get_texture (LrgSvgIcon *self,
                                       guint       pixel_size);

/**
 * lrg_svg_icon_clear_cache:
 * @self: an #LrgSvgIcon
 *
 * Drops every cached texture (for example after the UI scale changed
 * and the old sizes will not be drawn again).
 */
LRG_AVAILABLE_IN_ALL
void lrg_svg_icon_clear_cache (LrgSvgIcon *self);

/**
 * lrg_paint_list_svg_icon:
 * @self: an #LrgPaintList
 * @icon: an #LrgSvgIcon
 * @x: left edge
 * @y: top edge
 * @size: width and height in canvas units
 * @tint: (nullable): colour multiplied with the icon, %NULL for white
 *   (the icon's own colours)
 *
 * Records the icon as one image instance, using the cached texture for
 * the pixel size round(@size). Without a window nothing is recorded,
 * since no texture can exist.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_list_svg_icon (LrgPaintList   *self,
                              LrgSvgIcon     *icon,
                              gfloat          x,
                              gfloat          y,
                              gfloat          size,
                              const GrlColor *tint);

G_END_DECLS
