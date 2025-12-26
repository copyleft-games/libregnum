/* lrg-theme.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Theme singleton for consistent UI styling.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_THEME (lrg_theme_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTheme, lrg_theme, LRG, THEME, GObject)

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_theme_get_default:
 *
 * Gets the default theme singleton.
 *
 * Returns: (transfer none): The default #LrgTheme instance
 */
LRG_AVAILABLE_IN_ALL
LrgTheme * lrg_theme_get_default (void);

/**
 * lrg_theme_new:
 *
 * Creates a new custom theme.
 *
 * Returns: (transfer full): A new #LrgTheme
 */
LRG_AVAILABLE_IN_ALL
LrgTheme * lrg_theme_new (void);

/* ==========================================================================
 * Colors
 * ========================================================================== */

/**
 * lrg_theme_get_primary_color:
 * @self: an #LrgTheme
 *
 * Gets the primary color.
 *
 * Returns: (transfer none): The primary color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_theme_get_primary_color (LrgTheme *self);

/**
 * lrg_theme_set_primary_color:
 * @self: an #LrgTheme
 * @color: the primary color
 *
 * Sets the primary color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_primary_color (LrgTheme       *self,
                                  const GrlColor *color);

/**
 * lrg_theme_get_secondary_color:
 * @self: an #LrgTheme
 *
 * Gets the secondary color.
 *
 * Returns: (transfer none): The secondary color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_theme_get_secondary_color (LrgTheme *self);

/**
 * lrg_theme_set_secondary_color:
 * @self: an #LrgTheme
 * @color: the secondary color
 *
 * Sets the secondary color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_secondary_color (LrgTheme       *self,
                                    const GrlColor *color);

/**
 * lrg_theme_get_accent_color:
 * @self: an #LrgTheme
 *
 * Gets the accent color.
 *
 * Returns: (transfer none): The accent color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_theme_get_accent_color (LrgTheme *self);

/**
 * lrg_theme_set_accent_color:
 * @self: an #LrgTheme
 * @color: the accent color
 *
 * Sets the accent color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_accent_color (LrgTheme       *self,
                                 const GrlColor *color);

/**
 * lrg_theme_get_background_color:
 * @self: an #LrgTheme
 *
 * Gets the background color.
 *
 * Returns: (transfer none): The background color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_theme_get_background_color (LrgTheme *self);

/**
 * lrg_theme_set_background_color:
 * @self: an #LrgTheme
 * @color: the background color
 *
 * Sets the background color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_background_color (LrgTheme       *self,
                                     const GrlColor *color);

/**
 * lrg_theme_get_surface_color:
 * @self: an #LrgTheme
 *
 * Gets the surface color (panels, cards, etc.).
 *
 * Returns: (transfer none): The surface color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_theme_get_surface_color (LrgTheme *self);

/**
 * lrg_theme_set_surface_color:
 * @self: an #LrgTheme
 * @color: the surface color
 *
 * Sets the surface color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_surface_color (LrgTheme       *self,
                                  const GrlColor *color);

/**
 * lrg_theme_get_text_color:
 * @self: an #LrgTheme
 *
 * Gets the primary text color.
 *
 * Returns: (transfer none): The text color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_theme_get_text_color (LrgTheme *self);

/**
 * lrg_theme_set_text_color:
 * @self: an #LrgTheme
 * @color: the text color
 *
 * Sets the primary text color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_text_color (LrgTheme       *self,
                               const GrlColor *color);

/**
 * lrg_theme_get_text_secondary_color:
 * @self: an #LrgTheme
 *
 * Gets the secondary text color.
 *
 * Returns: (transfer none): The secondary text color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_theme_get_text_secondary_color (LrgTheme *self);

/**
 * lrg_theme_set_text_secondary_color:
 * @self: an #LrgTheme
 * @color: the secondary text color
 *
 * Sets the secondary text color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_text_secondary_color (LrgTheme       *self,
                                         const GrlColor *color);

/**
 * lrg_theme_get_border_color:
 * @self: an #LrgTheme
 *
 * Gets the border color.
 *
 * Returns: (transfer none): The border color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_theme_get_border_color (LrgTheme *self);

/**
 * lrg_theme_set_border_color:
 * @self: an #LrgTheme
 * @color: the border color
 *
 * Sets the border color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_border_color (LrgTheme       *self,
                                 const GrlColor *color);

/**
 * lrg_theme_get_error_color:
 * @self: an #LrgTheme
 *
 * Gets the error color.
 *
 * Returns: (transfer none): The error color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_theme_get_error_color (LrgTheme *self);

/**
 * lrg_theme_set_error_color:
 * @self: an #LrgTheme
 * @color: the error color
 *
 * Sets the error color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_error_color (LrgTheme       *self,
                                const GrlColor *color);

/**
 * lrg_theme_get_success_color:
 * @self: an #LrgTheme
 *
 * Gets the success color.
 *
 * Returns: (transfer none): The success color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_theme_get_success_color (LrgTheme *self);

/**
 * lrg_theme_set_success_color:
 * @self: an #LrgTheme
 * @color: the success color
 *
 * Sets the success color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_success_color (LrgTheme       *self,
                                  const GrlColor *color);

/* ==========================================================================
 * Typography
 * ========================================================================== */

/**
 * lrg_theme_get_default_font:
 * @self: an #LrgTheme
 *
 * Gets the default font.
 *
 * Returns: (transfer none) (nullable): The default font
 */
LRG_AVAILABLE_IN_ALL
GrlFont * lrg_theme_get_default_font (LrgTheme *self);

/**
 * lrg_theme_set_default_font:
 * @self: an #LrgTheme
 * @font: (nullable): the default font
 *
 * Sets the default font.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_default_font (LrgTheme *self,
                                 GrlFont  *font);

/**
 * lrg_theme_get_font_size_small:
 * @self: an #LrgTheme
 *
 * Gets the small font size.
 *
 * Returns: The small font size
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_theme_get_font_size_small (LrgTheme *self);

/**
 * lrg_theme_set_font_size_small:
 * @self: an #LrgTheme
 * @size: the small font size
 *
 * Sets the small font size.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_font_size_small (LrgTheme *self,
                                    gfloat    size);

/**
 * lrg_theme_get_font_size_normal:
 * @self: an #LrgTheme
 *
 * Gets the normal font size.
 *
 * Returns: The normal font size
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_theme_get_font_size_normal (LrgTheme *self);

/**
 * lrg_theme_set_font_size_normal:
 * @self: an #LrgTheme
 * @size: the normal font size
 *
 * Sets the normal font size.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_font_size_normal (LrgTheme *self,
                                     gfloat    size);

/**
 * lrg_theme_get_font_size_large:
 * @self: an #LrgTheme
 *
 * Gets the large font size.
 *
 * Returns: The large font size
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_theme_get_font_size_large (LrgTheme *self);

/**
 * lrg_theme_set_font_size_large:
 * @self: an #LrgTheme
 * @size: the large font size
 *
 * Sets the large font size.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_font_size_large (LrgTheme *self,
                                    gfloat    size);

/* ==========================================================================
 * Spacing
 * ========================================================================== */

/**
 * lrg_theme_get_padding_small:
 * @self: an #LrgTheme
 *
 * Gets the small padding.
 *
 * Returns: The small padding
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_theme_get_padding_small (LrgTheme *self);

/**
 * lrg_theme_set_padding_small:
 * @self: an #LrgTheme
 * @padding: the small padding
 *
 * Sets the small padding.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_padding_small (LrgTheme *self,
                                  gfloat    padding);

/**
 * lrg_theme_get_padding_normal:
 * @self: an #LrgTheme
 *
 * Gets the normal padding.
 *
 * Returns: The normal padding
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_theme_get_padding_normal (LrgTheme *self);

/**
 * lrg_theme_set_padding_normal:
 * @self: an #LrgTheme
 * @padding: the normal padding
 *
 * Sets the normal padding.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_padding_normal (LrgTheme *self,
                                   gfloat    padding);

/**
 * lrg_theme_get_padding_large:
 * @self: an #LrgTheme
 *
 * Gets the large padding.
 *
 * Returns: The large padding
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_theme_get_padding_large (LrgTheme *self);

/**
 * lrg_theme_set_padding_large:
 * @self: an #LrgTheme
 * @padding: the large padding
 *
 * Sets the large padding.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_padding_large (LrgTheme *self,
                                  gfloat    padding);

/**
 * lrg_theme_get_border_width:
 * @self: an #LrgTheme
 *
 * Gets the default border width.
 *
 * Returns: The border width
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_theme_get_border_width (LrgTheme *self);

/**
 * lrg_theme_set_border_width:
 * @self: an #LrgTheme
 * @width: the border width
 *
 * Sets the default border width.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_border_width (LrgTheme *self,
                                 gfloat    width);

/**
 * lrg_theme_get_corner_radius:
 * @self: an #LrgTheme
 *
 * Gets the default corner radius.
 *
 * Returns: The corner radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_theme_get_corner_radius (LrgTheme *self);

/**
 * lrg_theme_set_corner_radius:
 * @self: an #LrgTheme
 * @radius: the corner radius
 *
 * Sets the default corner radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_corner_radius (LrgTheme *self,
                                  gfloat    radius);

G_END_DECLS
