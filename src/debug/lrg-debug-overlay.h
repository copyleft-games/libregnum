/* lrg-debug-overlay.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Debug overlay system.
 *
 * The debug overlay provides an on-screen HUD for displaying
 * debug information like FPS, frame time, and profiler data.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_DEBUG_OVERLAY (lrg_debug_overlay_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDebugOverlay, lrg_debug_overlay, LRG, DEBUG_OVERLAY, GObject)

/* ==========================================================================
 * Construction and Singleton
 * ========================================================================== */

/**
 * lrg_debug_overlay_get_default:
 *
 * Gets the default overlay instance.
 *
 * Returns: (transfer none): the default #LrgDebugOverlay
 */
LRG_AVAILABLE_IN_ALL
LrgDebugOverlay * lrg_debug_overlay_get_default  (void);

/**
 * lrg_debug_overlay_new:
 *
 * Creates a new debug overlay.
 *
 * Returns: (transfer full): a new #LrgDebugOverlay
 */
LRG_AVAILABLE_IN_ALL
LrgDebugOverlay * lrg_debug_overlay_new          (void);

/* ==========================================================================
 * Overlay Control
 * ========================================================================== */

/**
 * lrg_debug_overlay_is_visible:
 * @self: a #LrgDebugOverlay
 *
 * Checks if the overlay is visible.
 *
 * Returns: %TRUE if visible
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_debug_overlay_is_visible   (LrgDebugOverlay *self);

/**
 * lrg_debug_overlay_set_visible:
 * @self: a #LrgDebugOverlay
 * @visible: whether to show the overlay
 *
 * Shows or hides the overlay.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_overlay_set_visible  (LrgDebugOverlay *self,
                                                  gboolean         visible);

/**
 * lrg_debug_overlay_toggle:
 * @self: a #LrgDebugOverlay
 *
 * Toggles overlay visibility.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_overlay_toggle       (LrgDebugOverlay *self);

/* ==========================================================================
 * Display Flags
 * ========================================================================== */

/**
 * lrg_debug_overlay_get_flags:
 * @self: a #LrgDebugOverlay
 *
 * Gets the current display flags.
 *
 * Returns: the current #LrgDebugOverlayFlags
 */
LRG_AVAILABLE_IN_ALL
LrgDebugOverlayFlags lrg_debug_overlay_get_flags (LrgDebugOverlay *self);

/**
 * lrg_debug_overlay_set_flags:
 * @self: a #LrgDebugOverlay
 * @flags: flags for what to display
 *
 * Sets the display flags.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_overlay_set_flags    (LrgDebugOverlay      *self,
                                                  LrgDebugOverlayFlags  flags);

/**
 * lrg_debug_overlay_add_flags:
 * @self: a #LrgDebugOverlay
 * @flags: flags to add
 *
 * Adds display flags.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_overlay_add_flags    (LrgDebugOverlay      *self,
                                                  LrgDebugOverlayFlags  flags);

/**
 * lrg_debug_overlay_remove_flags:
 * @self: a #LrgDebugOverlay
 * @flags: flags to remove
 *
 * Removes display flags.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_overlay_remove_flags (LrgDebugOverlay      *self,
                                                  LrgDebugOverlayFlags  flags);

/**
 * lrg_debug_overlay_has_flag:
 * @self: a #LrgDebugOverlay
 * @flag: flag to check
 *
 * Checks if a display flag is set.
 *
 * Returns: %TRUE if the flag is set
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_debug_overlay_has_flag     (LrgDebugOverlay      *self,
                                                  LrgDebugOverlayFlags  flag);

/* ==========================================================================
 * Position and Style
 * ========================================================================== */

/**
 * lrg_debug_overlay_get_position:
 * @self: a #LrgDebugOverlay
 * @x: (out) (nullable): return location for X position
 * @y: (out) (nullable): return location for Y position
 *
 * Gets the overlay position.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_overlay_get_position (LrgDebugOverlay *self,
                                                  gint            *x,
                                                  gint            *y);

/**
 * lrg_debug_overlay_set_position:
 * @self: a #LrgDebugOverlay
 * @x: X position in pixels
 * @y: Y position in pixels
 *
 * Sets the overlay position.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_overlay_set_position (LrgDebugOverlay *self,
                                                  gint             x,
                                                  gint             y);

/**
 * lrg_debug_overlay_get_font_size:
 * @self: a #LrgDebugOverlay
 *
 * Gets the font size.
 *
 * Returns: font size in pixels
 */
LRG_AVAILABLE_IN_ALL
gint              lrg_debug_overlay_get_font_size (LrgDebugOverlay *self);

/**
 * lrg_debug_overlay_set_font_size:
 * @self: a #LrgDebugOverlay
 * @size: font size in pixels
 *
 * Sets the font size.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_overlay_set_font_size (LrgDebugOverlay *self,
                                                   gint             size);

/**
 * lrg_debug_overlay_get_padding:
 * @self: a #LrgDebugOverlay
 *
 * Gets the padding around text.
 *
 * Returns: padding in pixels
 */
LRG_AVAILABLE_IN_ALL
gint              lrg_debug_overlay_get_padding  (LrgDebugOverlay *self);

/**
 * lrg_debug_overlay_set_padding:
 * @self: a #LrgDebugOverlay
 * @padding: padding in pixels
 *
 * Sets the padding around text.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_overlay_set_padding  (LrgDebugOverlay *self,
                                                  gint             padding);

/* ==========================================================================
 * Custom Data Display
 * ========================================================================== */

/**
 * lrg_debug_overlay_set_custom_line:
 * @self: a #LrgDebugOverlay
 * @key: a unique key for this line
 * @format: printf-style format string
 * @...: format arguments
 *
 * Sets a custom display line. Use %NULL format to remove.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_overlay_set_custom_line (LrgDebugOverlay *self,
                                                     const gchar     *key,
                                                     const gchar     *format,
                                                     ...) G_GNUC_PRINTF (3, 4);

/**
 * lrg_debug_overlay_remove_custom_line:
 * @self: a #LrgDebugOverlay
 * @key: the key to remove
 *
 * Removes a custom display line.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_overlay_remove_custom_line (LrgDebugOverlay *self,
                                                        const gchar     *key);

/**
 * lrg_debug_overlay_clear_custom_lines:
 * @self: a #LrgDebugOverlay
 *
 * Removes all custom display lines.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_debug_overlay_clear_custom_lines (LrgDebugOverlay *self);

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/**
 * lrg_debug_overlay_get_text:
 * @self: a #LrgDebugOverlay
 *
 * Gets the current overlay text for rendering.
 *
 * This combines all enabled displays (FPS, frame time, profiler, etc.)
 * into a formatted string suitable for rendering.
 *
 * Returns: (transfer full): the overlay text
 */
LRG_AVAILABLE_IN_ALL
gchar *           lrg_debug_overlay_get_text     (LrgDebugOverlay *self);

/**
 * lrg_debug_overlay_get_line_count:
 * @self: a #LrgDebugOverlay
 *
 * Gets the number of lines in the overlay.
 *
 * Returns: number of display lines
 */
LRG_AVAILABLE_IN_ALL
guint             lrg_debug_overlay_get_line_count (LrgDebugOverlay *self);

G_END_DECLS
