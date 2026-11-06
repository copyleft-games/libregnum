/* lrg-color-picker.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgColorPicker: Saturation/value square, hue and alpha strips, hex entry and swatches.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"
#include "lrg-container.h"

G_BEGIN_DECLS

/**
 * LrgColorPickerArea:
 * @LRG_COLOR_PICKER_AREA_SV: the saturation (x) / value (y) square
 * @LRG_COLOR_PICKER_AREA_HUE: the hue strip
 * @LRG_COLOR_PICKER_AREA_ALPHA: the alpha strip (only with
 *   #LrgColorPicker:show-alpha)
 * @LRG_COLOR_PICKER_AREA_HEX: the hex text field
 * @LRG_COLOR_PICKER_AREA_PREVIEW: the swatch showing the current colour
 * @LRG_COLOR_PICKER_AREA_SWATCHES: the palette and recent colours
 *
 * Regions of a full colour picker, see
 * lrg_color_picker_get_area_bounds().
 */
typedef enum
{
	LRG_COLOR_PICKER_AREA_SV,
	LRG_COLOR_PICKER_AREA_HUE,
	LRG_COLOR_PICKER_AREA_ALPHA,
	LRG_COLOR_PICKER_AREA_HEX,
	LRG_COLOR_PICKER_AREA_PREVIEW,
	LRG_COLOR_PICKER_AREA_SWATCHES
} LrgColorPickerArea;

#define LRG_TYPE_COLOR_PICKER_AREA (lrg_color_picker_area_get_type ())

/**
 * lrg_color_picker_area_get_type:
 *
 * Returns: the #GType of #LrgColorPickerArea
 */
LRG_AVAILABLE_IN_ALL
GType lrg_color_picker_area_get_type (void) G_GNUC_CONST;

/**
 * LRG_COLOR_PICKER_MAX_RECENT:
 *
 * How many recent colours a picker remembers.
 */
#define LRG_COLOR_PICKER_MAX_RECENT (8)

#define LRG_TYPE_COLOR_PICKER (lrg_color_picker_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgColorPicker, lrg_color_picker, LRG, COLOR_PICKER, LrgContainer)

/**
 * lrg_color_picker_new:
 *
 * Creates a full colour picker set to opaque white with alpha editing
 * shown.
 *
 * Returns: (transfer full): a new #LrgColorPicker
 */
LRG_AVAILABLE_IN_ALL
LrgColorPicker * lrg_color_picker_new (void);

/**
 * lrg_color_picker_get_color:
 * @self: an #LrgColorPicker
 * @color: (out caller-allocates): the current colour
 *
 * Reads the current colour.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_picker_get_color (LrgColorPicker *self,
                                 GrlColor       *color);

/**
 * lrg_color_picker_set_color:
 * @self: an #LrgColorPicker
 * @color: the new colour
 *
 * Sets the colour. The hue is kept when @color is grey (it has none),
 * so the square does not jump. Emits #LrgColorPicker::color-changed
 * when the colour changes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_picker_set_color (LrgColorPicker *self,
                                 const GrlColor *color);

/**
 * lrg_color_picker_get_hsv:
 * @self: an #LrgColorPicker
 * @hue: (out) (optional): hue in degrees, 0..360
 * @saturation: (out) (optional): saturation, 0..1
 * @value: (out) (optional): value (brightness), 0..1
 *
 * Reads the colour as HSV, exactly as the square and hue strip show it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_picker_get_hsv (LrgColorPicker *self,
                               gdouble        *hue,
                               gdouble        *saturation,
                               gdouble        *value);

/**
 * lrg_color_picker_set_hsv:
 * @self: an #LrgColorPicker
 * @hue: hue in degrees (wrapped into 0..360)
 * @saturation: saturation, 0..1 (clamped)
 * @value: value, 0..1 (clamped)
 *
 * Sets the colour from HSV, keeping alpha.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_picker_set_hsv (LrgColorPicker *self,
                               gdouble         hue,
                               gdouble         saturation,
                               gdouble         value);

/**
 * lrg_color_picker_get_alpha:
 * @self: an #LrgColorPicker
 *
 * Returns: the opacity, 0..1
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_color_picker_get_alpha (LrgColorPicker *self);

/**
 * lrg_color_picker_set_alpha:
 * @self: an #LrgColorPicker
 * @alpha: opacity, 0..1 (clamped)
 *
 * Sets the opacity, keeping the hue, saturation and value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_picker_set_alpha (LrgColorPicker *self,
                                 gdouble         alpha);

/**
 * lrg_color_picker_get_hex:
 * @self: an #LrgColorPicker
 *
 * Returns: (transfer full): the colour as "#RRGGBB", or "#RRGGBBAA"
 *   while #LrgColorPicker:show-alpha is set
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_color_picker_get_hex (LrgColorPicker *self);

/**
 * lrg_color_picker_set_hex:
 * @self: an #LrgColorPicker
 * @hex: "#RGB", "#RGBA", "#RRGGBB" or "#RRGGBBAA" ("#" optional)
 *
 * Sets the colour from hex text.
 *
 * Returns: %FALSE (and changes nothing) if @hex is not a colour
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_color_picker_set_hex (LrgColorPicker *self,
                                   const gchar    *hex);

/**
 * lrg_color_picker_get_show_alpha:
 * @self: an #LrgColorPicker
 *
 * Returns: whether the alpha strip is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_color_picker_get_show_alpha (LrgColorPicker *self);

/**
 * lrg_color_picker_set_show_alpha:
 * @self: an #LrgColorPicker
 * @show_alpha: whether opacity can be edited
 *
 * Shows or hides the alpha strip. Hiding it does not change the
 * current alpha; the hex field then shows and accepts six digits.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_picker_set_show_alpha (LrgColorPicker *self,
                                      gboolean        show_alpha);

/**
 * lrg_color_picker_get_popup_mode:
 * @self: an #LrgColorPicker
 *
 * Returns: whether the picker is a compact swatch button
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_color_picker_get_popup_mode (LrgColorPicker *self);

/**
 * lrg_color_picker_set_popup_mode:
 * @self: an #LrgColorPicker
 * @popup_mode: %TRUE for a swatch button that opens the full picker in
 *   a popup
 *
 * Switches between the full picker and the compact button.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_picker_set_popup_mode (LrgColorPicker *self,
                                      gboolean        popup_mode);

/**
 * lrg_color_picker_popup:
 * @self: an #LrgColorPicker in popup mode
 *
 * Opens the full picker below the button. The picker must be on an
 * #LrgCanvas.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_picker_popup (LrgColorPicker *self);

/**
 * lrg_color_picker_popdown:
 * @self: an #LrgColorPicker
 *
 * Closes the popup picker if it is open.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_picker_popdown (LrgColorPicker *self);

/**
 * lrg_color_picker_get_popup_open:
 * @self: an #LrgColorPicker
 *
 * Returns: whether the popup picker is showing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_color_picker_get_popup_open (LrgColorPicker *self);

/**
 * lrg_color_picker_get_popup_picker:
 * @self: an #LrgColorPicker in popup mode
 *
 * The full picker shown in the popup, mirrored into @self as it
 * changes.
 *
 * Returns: (transfer none) (nullable): the popup's picker, or %NULL
 *   when closed
 */
LRG_AVAILABLE_IN_ALL
LrgColorPicker * lrg_color_picker_get_popup_picker (LrgColorPicker *self);

/**
 * lrg_color_picker_set_palette:
 * @self: an #LrgColorPicker
 * @colors: (array length=n_colors) (nullable): palette colours
 * @n_colors: number of colours
 *
 * Replaces the swatch palette (12 colours by default). An empty palette
 * hides the palette row.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_picker_set_palette (LrgColorPicker *self,
                                   const GrlColor *colors,
                                   guint           n_colors);

/**
 * lrg_color_picker_get_palette:
 * @self: an #LrgColorPicker
 * @n_colors: (out): number of colours
 *
 * Returns: (array length=n_colors) (transfer none): the palette
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_color_picker_get_palette (LrgColorPicker *self,
                                               guint          *n_colors);

/**
 * lrg_color_picker_add_recent:
 * @self: an #LrgColorPicker
 * @color: a colour
 *
 * Puts @color first in the recent row (moving it if present, dropping
 * the oldest beyond %LRG_COLOR_PICKER_MAX_RECENT). The picker does this
 * itself when a drag ends, a hex value is entered, a palette swatch is
 * clicked or the popup closes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_picker_add_recent (LrgColorPicker *self,
                                  const GrlColor *color);

/**
 * lrg_color_picker_get_recent:
 * @self: an #LrgColorPicker
 * @n_colors: (out): number of colours
 *
 * Returns: (array length=n_colors) (transfer none): recent colours,
 *   newest first
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_color_picker_get_recent (LrgColorPicker *self,
                                              guint          *n_colors);

/**
 * lrg_color_picker_clear_recent:
 * @self: an #LrgColorPicker
 *
 * Forgets the recent colours.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_picker_clear_recent (LrgColorPicker *self);

/**
 * lrg_color_picker_get_area_bounds:
 * @self: an #LrgColorPicker
 * @area: a region
 * @bounds: (out caller-allocates): the region in canvas coordinates
 *
 * Where a region of the full picker is, for hit testing and tests.
 *
 * Returns: %FALSE in popup mode or if @area is hidden
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_color_picker_get_area_bounds (LrgColorPicker     *self,
                                           LrgColorPickerArea  area,
                                           GrlRectangle       *bounds);

/**
 * lrg_color_picker_get_swatch_bounds:
 * @self: an #LrgColorPicker
 * @index: swatch index: palette colours first, then recent colours
 * @bounds: (out caller-allocates): the swatch in canvas coordinates
 *
 * Returns: %FALSE if there is no such swatch
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_color_picker_get_swatch_bounds (LrgColorPicker *self,
                                             guint           index,
                                             GrlRectangle   *bounds);

/**
 * lrg_color_picker_rgb_to_hsv:
 * @color: a colour (alpha ignored)
 * @hue: (out) (optional): hue in degrees, 0..360 (0 for greys)
 * @saturation: (out) (optional): saturation, 0..1
 * @value: (out) (optional): value, 0..1
 *
 * Converts 8-bit RGB to HSV.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_picker_rgb_to_hsv (const GrlColor *color,
                                  gdouble        *hue,
                                  gdouble        *saturation,
                                  gdouble        *value);

/**
 * lrg_color_picker_hsv_to_rgb:
 * @hue: hue in degrees (wrapped)
 * @saturation: saturation, 0..1
 * @value: value, 0..1
 * @alpha: opacity, 0..1
 * @color: (out caller-allocates): the 8-bit colour, rounded
 *
 * Converts HSV (plus alpha) to an 8-bit colour.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_picker_hsv_to_rgb (gdouble   hue,
                                  gdouble   saturation,
                                  gdouble   value,
                                  gdouble   alpha,
                                  GrlColor *color);

G_END_DECLS
