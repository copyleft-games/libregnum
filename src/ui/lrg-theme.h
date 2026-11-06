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
#include "../lrg-enums.h"
#include "lrg-style.h"
#include "../paint/lrg-paint-font.h"

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

/* ==========================================================================
 * Colour roles and metrics
 *
 * The legacy getters above are aliases of these roles:
 * primary = ACCENT, secondary = CONTROL, accent = FOCUS,
 * background = WINDOW, surface = PANEL, text = TEXT,
 * text-secondary = TEXT_MUTED, border = BORDER, error = DANGER,
 * success = SUCCESS; font sizes small/normal/large = FONT_SIZE_SMALL /
 * FONT_SIZE / FONT_SIZE_LARGE; border-width and corner-radius are the
 * metrics of the same name.
 * ========================================================================== */

/**
 * lrg_theme_get_color:
 * @self: an #LrgTheme
 * @role: a colour role
 *
 * Returns: (transfer none): the colour for @role
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_theme_get_color (LrgTheme      *self,
                                      LrgThemeColor  role);

/**
 * lrg_theme_set_color:
 * @self: an #LrgTheme
 * @role: a colour role
 * @color: the new colour
 *
 * Sets one colour role and emits #LrgTheme::changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_color (LrgTheme       *self,
                          LrgThemeColor   role,
                          const GrlColor *color);

/**
 * lrg_theme_get_metric:
 * @self: an #LrgTheme
 * @metric: a metric
 *
 * Returns: the metric's value in canvas units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_theme_get_metric (LrgTheme       *self,
                             LrgThemeMetric  metric);

/**
 * lrg_theme_set_metric:
 * @self: an #LrgTheme
 * @metric: a metric
 * @value: the new value
 *
 * Sets one metric and emits #LrgTheme::changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_metric (LrgTheme       *self,
                           LrgThemeMetric  metric,
                           gfloat          value);

/**
 * lrg_theme_set_accent:
 * @self: an #LrgTheme
 * @accent: the brand colour
 *
 * Sets %LRG_THEME_COLOR_ACCENT and derives the whole accent family
 * from it: hover and pressed shades (lighter/darker depending on
 * #LrgTheme:dark), focus ring, selection, link, and a text-on-accent
 * colour (black or white, whichever contrasts more). One call recolours
 * every widget.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_accent (LrgTheme       *self,
                           const GrlColor *accent);

/**
 * lrg_theme_get_contrast:
 * @self: an #LrgTheme
 * @foreground: a colour role drawn on top
 * @background: a colour role drawn underneath
 *
 * WCAG contrast ratio between two roles. Body text wants 4.5 or more,
 * large text and UI shapes 3 or more.
 *
 * Returns: the contrast ratio (1..21)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_theme_get_contrast (LrgTheme      *self,
                               LrgThemeColor  foreground,
                               LrgThemeColor  background);

/* ==========================================================================
 * Identity and motion
 * ========================================================================== */

/**
 * lrg_theme_get_name:
 * @self: an #LrgTheme
 *
 * Returns: (nullable): the theme's name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_theme_get_name (LrgTheme *self);

/**
 * lrg_theme_set_name:
 * @self: an #LrgTheme
 * @name: (nullable): a name
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_name (LrgTheme    *self,
                         const gchar *name);

/**
 * lrg_theme_get_dark:
 * @self: an #LrgTheme
 *
 * Returns: %TRUE for a dark theme (light text on dark surfaces)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_theme_get_dark (LrgTheme *self);

/**
 * lrg_theme_set_dark:
 * @self: an #LrgTheme
 * @dark: whether the theme is dark
 *
 * Only affects how lrg_theme_set_accent() derives shades.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_dark (LrgTheme *self,
                         gboolean  dark);

/**
 * lrg_theme_get_motion:
 * @self: an #LrgTheme
 *
 * Returns: the spring preset widgets use for state transitions
 */
LRG_AVAILABLE_IN_ALL
LrgMotion lrg_theme_get_motion (LrgTheme *self);

/**
 * lrg_theme_set_motion:
 * @self: an #LrgTheme
 * @motion: the spring preset for state transitions
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_motion (LrgTheme  *self,
                           LrgMotion  motion);

/**
 * lrg_theme_get_generation:
 * @self: an #LrgTheme
 *
 * A counter bumped on every change. Caches keyed on a theme compare
 * generations to know when to rebuild.
 *
 * Returns: the current generation
 */
LRG_AVAILABLE_IN_ALL
guint lrg_theme_get_generation (LrgTheme *self);

/**
 * lrg_theme_copy:
 * @self: an #LrgTheme
 *
 * Deep-copies colours, metrics, motion, name, fonts and style rules.
 * Handy for deriving a variant (e.g. a scoped dark twin).
 *
 * Returns: (transfer full): a new #LrgTheme
 */
LRG_AVAILABLE_IN_ALL
LrgTheme * lrg_theme_copy (LrgTheme *self);

/**
 * lrg_theme_set_default:
 * @theme: (nullable): the theme every widget should use, or %NULL to
 *   go back to a fresh built-in theme
 *
 * Replaces the process-wide default theme. Widgets read the default
 * theme every frame, so this switches the look of a running UI.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_default (LrgTheme *theme);

/* ==========================================================================
 * Presets
 * ========================================================================== */

/**
 * lrg_theme_list_presets:
 *
 * Returns: (transfer none) (array zero-terminated=1): names accepted by
 *   lrg_theme_new_preset(): "midnight", "daylight", "catppuccin-mocha",
 *   "catppuccin-latte", "high-contrast"
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_theme_list_presets (void);

/**
 * lrg_theme_new_preset:
 * @name: a preset name, see lrg_theme_list_presets()
 *
 * Returns: (transfer full) (nullable): a new theme, or %NULL if @name
 *   is unknown
 */
LRG_AVAILABLE_IN_ALL
LrgTheme * lrg_theme_new_preset (const gchar *name);

/**
 * lrg_theme_apply_preset:
 * @self: an #LrgTheme
 * @name: a preset name
 *
 * Overwrites colours, metrics, motion and name with a preset's values.
 * Style rules and fonts are kept.
 *
 * Returns: %TRUE if @name is a known preset
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_theme_apply_preset (LrgTheme    *self,
                                 const gchar *name);

/* ==========================================================================
 * YAML
 * ========================================================================== */

/**
 * lrg_theme_new_from_file:
 * @path: a theme YAML file
 * @error: return location for a #GError
 *
 * Loads a theme file. See docs/guides/theming.org for the format.
 *
 * Returns: (transfer full) (nullable): the theme, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgTheme * lrg_theme_new_from_file (const gchar  *path,
                                    GError      **error);

/**
 * lrg_theme_new_from_data:
 * @data: theme YAML text
 * @length: length of @data, or -1 if nul-terminated
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the theme, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgTheme * lrg_theme_new_from_data (const gchar  *data,
                                    gssize        length,
                                    GError      **error);

/**
 * lrg_theme_load_from_data:
 * @self: an #LrgTheme
 * @data: theme YAML text
 * @length: length of @data, or -1 if nul-terminated
 * @error: return location for a #GError
 *
 * Applies a theme document on top of @self: a `preset:` key is applied
 * first, then `accent:`, then individual `colors:` and `metrics:`,
 * then `styles:` are appended. Nothing changes if the document fails
 * to parse; #LrgTheme::changed is emitted once on success.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_theme_load_from_data (LrgTheme     *self,
                                   const gchar  *data,
                                   gssize        length,
                                   GError      **error);

/**
 * lrg_theme_load_from_file:
 * @self: an #LrgTheme
 * @path: a theme YAML file
 * @error: return location for a #GError
 *
 * Like lrg_theme_load_from_data() for a file. A relative `font:` path
 * in the file is resolved against the file's directory.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_theme_load_from_file (LrgTheme     *self,
                                   const gchar  *path,
                                   GError      **error);

/**
 * lrg_theme_to_data:
 * @self: an #LrgTheme
 * @length: (out) (optional): length of the result
 * @error: return location for a #GError
 *
 * Serialises every colour, metric, the motion and the style rules to
 * YAML that lrg_theme_load_from_data() reads back unchanged.
 *
 * Returns: (transfer full) (nullable): YAML text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_theme_to_data (LrgTheme  *self,
                           gsize     *length,
                           GError   **error);

/**
 * lrg_theme_save_to_file:
 * @self: an #LrgTheme
 * @path: destination file
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_theme_save_to_file (LrgTheme     *self,
                                 const gchar  *path,
                                 GError      **error);

/* ==========================================================================
 * Style rules
 * ========================================================================== */

/**
 * lrg_theme_add_style_rule:
 * @self: an #LrgTheme
 * @rule: a rule; the theme takes a reference
 *
 * Appends a style rule and emits #LrgTheme::changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_add_style_rule (LrgTheme     *self,
                               LrgStyleRule *rule);

/**
 * lrg_theme_clear_style_rules:
 * @self: an #LrgTheme
 *
 * Removes every style rule.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_clear_style_rules (LrgTheme *self);

/**
 * lrg_theme_style_rules_changed:
 * @self: an #LrgTheme
 *
 * Call after editing the look of a rule that is already in the theme
 * (lrg_style_rule_get_look() returns it for in-place edits): drops the
 * cached resolutions and emits #LrgTheme::changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_style_rules_changed (LrgTheme *self);

/**
 * lrg_theme_get_style_rules:
 * @self: an #LrgTheme
 *
 * Returns: (transfer none) (element-type LrgStyleRule): the rules in
 *   the order they were added
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_theme_get_style_rules (LrgTheme *self);

/**
 * lrg_theme_resolve_look:
 * @self: an #LrgTheme
 * @widget_type: the widget's #GType
 * @classes: (array length=n_classes) (nullable): the widget's style classes
 * @n_classes: number of classes
 * @part: the part being drawn
 * @state: the widget's state
 * @look: (inout): the widget's built-in look; matching rules are
 *   merged on top, least specific first
 *
 * Applies the theme's style rules to a look. The merged overlay for a
 * (type, classes, part, state) combination is cached until the theme
 * changes, so this is cheap to call every frame.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_resolve_look (LrgTheme       *self,
                             GType           widget_type,
                             const GQuark   *classes,
                             guint           n_classes,
                             LrgUIPart       part,
                             LrgWidgetState  state,
                             LrgPartLook    *look);

/* ==========================================================================
 * UI font
 * ========================================================================== */

/**
 * lrg_theme_get_ui_font:
 * @self: an #LrgTheme
 *
 * The font paint-based widgets draw with. On first use (with a window
 * open) it loads the first available of Adwaita Sans, Inter, Cantarell,
 * Noto Sans, Open Sans and DejaVu Sans at a large base size with
 * mipmaps, so text stays clean at every size; without any of them it
 * falls back to lrg_theme_get_default_font().
 *
 * Returns: (transfer none) (nullable): the UI font, or %NULL for
 *   raylib's built-in font
 */
LRG_AVAILABLE_IN_ALL
GrlFont * lrg_theme_get_ui_font (LrgTheme *self);

/**
 * lrg_theme_set_ui_font:
 * @self: an #LrgTheme
 * @font: (nullable): the font, or %NULL to pick one automatically again
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_ui_font (LrgTheme *self,
                            GrlFont  *font);

/**
 * lrg_theme_load_ui_font:
 * @self: an #LrgTheme
 * @path: a TTF/OTF file
 * @error: return location for a #GError
 *
 * Loads @path as the UI font (needs a window). Glyphs for Latin,
 * Latin-1, Latin Extended-A and common punctuation and symbols are
 * rasterised at 64 px with mipmaps and trilinear filtering.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_theme_load_ui_font (LrgTheme     *self,
                                 const gchar  *path,
                                 GError      **error);

/**
 * lrg_theme_get_font_path:
 * @self: an #LrgTheme
 *
 * Returns: (nullable): the font file requested by the theme (from YAML
 *   or lrg_theme_load_ui_font()), loaded when the UI font is first used
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_theme_get_font_path (LrgTheme *self);

/* ==========================================================================
 * SDF text
 * ========================================================================== */

/**
 * lrg_theme_get_paint_font:
 * @self: an #LrgTheme
 *
 * The distance-field font the theme's UI text is drawn with. Built on
 * first use (no window needed) from, in order: the `font:` file, the
 * `font-family:`, or the system UI font; then the theme's
 * `font-fallbacks:` and the system fallbacks (symbols, Arabic, Hebrew,
 * monochrome emoji, CJK). Themes with none of these settings share one
 * font of the same face lrg_theme_get_ui_font() picks (Adwaita Sans,
 * Inter, Cantarell, Noto Sans, ...), falling back to
 * lrg_paint_font_get_system_default(). When the UI font is loaded (with
 * a window) this font is bound to it with lrg_paint_font_bind(), so
 * widgets drawing with lrg_theme_get_ui_font() get SDF text.
 *
 * Returns: (transfer none) (nullable): the SDF font, or %NULL when
 *   SDF text is off or no font is installed
 */
LRG_AVAILABLE_IN_ALL
LrgPaintFont * lrg_theme_get_paint_font (LrgTheme *self);

/**
 * lrg_theme_set_paint_font:
 * @self: an #LrgTheme
 * @font: (nullable): the SDF font, or %NULL to build one from the
 *   theme's settings again
 *
 * Replaces the theme's SDF font (and rebinds an already loaded UI font
 * to it).
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_paint_font (LrgTheme     *self,
                               LrgPaintFont *font);

/**
 * lrg_theme_get_font_family:
 * @self: an #LrgTheme
 *
 * Returns: (nullable): the `font-family:` setting
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_theme_get_font_family (LrgTheme *self);

/**
 * lrg_theme_set_font_family:
 * @self: an #LrgTheme
 * @family: (nullable): a family name such as "Noto Sans", found with
 *   lrg_paint_font_find_file(); %NULL for the system UI font
 *
 * Sets the family of the SDF UI font. A `font:` file, when set, wins.
 * The fonts are reloaded on next use.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_font_family (LrgTheme    *self,
                                const gchar *family);

/**
 * lrg_theme_get_font_fallbacks:
 * @self: an #LrgTheme
 *
 * Returns: (nullable) (array zero-terminated=1) (transfer none): the
 *   `font-fallbacks:` setting
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_theme_get_font_fallbacks (LrgTheme *self);

/**
 * lrg_theme_set_font_fallbacks:
 * @self: an #LrgTheme
 * @fallbacks: (nullable) (array zero-terminated=1): family names or
 *   font files (entries containing '/' or ending in .ttf/.otf/.ttc),
 *   tried in order before the system fallbacks
 *
 * Sets the theme's fallback fonts. The fonts are reloaded on next use.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_font_fallbacks (LrgTheme            *self,
                                   const gchar * const *fallbacks);

/**
 * lrg_theme_get_sdf_text:
 * @self: an #LrgTheme
 *
 * Returns: %TRUE (the default) when UI text uses SDF fonts
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_theme_get_sdf_text (LrgTheme *self);

/**
 * lrg_theme_set_sdf_text:
 * @self: an #LrgTheme
 * @enabled: %FALSE to draw UI text with the bitmap #GrlFont only
 *
 * Turns SDF text on or off (YAML `sdf-text:`).
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_set_sdf_text (LrgTheme *self,
                             gboolean  enabled);

/**
 * LRG_THEME_ERROR:
 *
 * Error domain for theme loading.
 */
#define LRG_THEME_ERROR (lrg_theme_error_quark ())

/**
 * LrgThemeError:
 * @LRG_THEME_ERROR_PARSE: the document is not valid YAML or not a mapping
 * @LRG_THEME_ERROR_INVALID: a key holds a value of the wrong kind
 * @LRG_THEME_ERROR_FONT: a font could not be loaded
 *
 * Errors from theme loading.
 */
typedef enum
{
    LRG_THEME_ERROR_PARSE,
    LRG_THEME_ERROR_INVALID,
    LRG_THEME_ERROR_FONT
} LrgThemeError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_theme_error_quark (void);

/* ==========================================================================
 * Palette generation and contrast
 * ========================================================================== */

/**
 * lrg_color_contrast_ratio:
 * @a: a colour
 * @b: another colour
 *
 * WCAG 2.x contrast ratio between two colours, computed from their
 * relative luminance: (L1 + 0.05) / (L2 + 0.05) with L1 the lighter.
 * Alpha is ignored. Black on white gives 21, a colour on itself 1.
 * Body text wants 4.5 or more (AA), 7 or more is AAA; large text and
 * UI shapes want 3 or more.
 *
 * Returns: the contrast ratio, 1..21
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_color_contrast_ratio (const GrlColor *a,
                                 const GrlColor *b);

/**
 * lrg_theme_generate_palette:
 * @self: an #LrgTheme
 * @accent: the brand colour every other colour is derived from
 * @tint: 0..1, how much of the accent's hue bleeds into the neutral
 *   surfaces, borders and text (0 gives pure greys)
 * @depth: 0..1, how far the surfaces sit from the middle and how far
 *   window, panel, surface and popup step apart (0 is a soft, flat
 *   palette; 1 is deep, high-contrast layering)
 * @dark: %TRUE for light text on dark surfaces
 *
 * Replaces every colour role with a palette generated from one accent.
 * The neutrals (window, panel, surface, popup, control states, input,
 * border, track, tooltip, text, muted text, disabled) take the
 * accent's hue at a saturation set by @tint and lightness steps set
 * by @depth; status colours, shadow and veil come from fixed dark or
 * light tables; the accent family (hover, pressed, focus, selection,
 * link, text on accent) is derived exactly as lrg_theme_set_accent()
 * does, except that the link is shaded further if needed to reach 4.5:1
 * on panels and surfaces. Text, muted text and links read at WCAG AA
 * on every surface; text on the hover and pressed accent shades can
 * fall to about 3:1 with mid-tone accents, which
 * lrg_theme_fix_contrast() raises. Sets #LrgTheme:dark to @dark. Metrics, motion, name, fonts and
 * style rules are left alone. The result depends only on the
 * arguments, and #LrgTheme::changed is emitted once.
 */
LRG_AVAILABLE_IN_ALL
void lrg_theme_generate_palette (LrgTheme       *self,
                                 const GrlColor *accent,
                                 gfloat          tint,
                                 gfloat          depth,
                                 gboolean        dark);

/**
 * lrg_theme_get_n_contrast_pairs:
 *
 * The number of foreground/background role pairs that carry text in
 * the built-in widgets, see lrg_theme_get_contrast_pair().
 *
 * Returns: the number of pairs
 */
LRG_AVAILABLE_IN_ALL
guint lrg_theme_get_n_contrast_pairs (void);

/**
 * lrg_theme_get_contrast_pair:
 * @index: a pair index, below lrg_theme_get_n_contrast_pairs()
 * @foreground: (out) (optional): the role drawn on top (text)
 * @background: (out) (optional): the role drawn underneath
 *
 * One of the role pairs lrg_theme_fix_contrast() checks: text on
 * window, panel, surface, control, input and popup; muted text on
 * panel and surface; text on accent over the accent and its hover and
 * pressed shades; link on panel.
 *
 * Returns: %TRUE if @index is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_theme_get_contrast_pair (guint          index,
                                      LrgThemeColor *foreground,
                                      LrgThemeColor *background);

/**
 * lrg_theme_count_contrast_failures:
 * @self: an #LrgTheme
 * @min_ratio: the contrast ratio every pair must reach, e.g. 4.5
 *
 * Returns: how many of the pairs from lrg_theme_get_contrast_pair()
 *   fall below @min_ratio
 */
LRG_AVAILABLE_IN_ALL
guint lrg_theme_count_contrast_failures (LrgTheme *self,
                                         gfloat    min_ratio);

/**
 * lrg_theme_fix_contrast:
 * @self: an #LrgTheme
 * @min_ratio: the contrast ratio to reach, 1..21 (4.5 for WCAG AA
 *   body text, 7 for AAA)
 *
 * Nudges colours until every pair from lrg_theme_get_contrast_pair()
 * reaches @min_ratio, changing as little as possible: each foreground
 * role moves towards white or black (whichever has the better worst
 * case over all the backgrounds it sits on) by the smallest step that
 * passes; only when the foreground is already at that extreme does
 * the background move away from it. Pairs that share a role are
 * rechecked until all pass.
 * #LrgTheme::changed is emitted once if anything changed.
 *
 * Returns: the number of colour roles that were changed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_theme_fix_contrast (LrgTheme *self,
                              gfloat    min_ratio);

G_END_DECLS
