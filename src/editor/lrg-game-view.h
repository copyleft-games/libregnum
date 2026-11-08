/* lrg-game-view.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Resolution presets for the editor game view. The play viewport uses
 * one of these sizes (letterboxed into the panel) so a game is framed
 * the way it will ship.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * lrg_game_view_preset_count:
 *
 * Returns: how many built-in presets there are
 */
LRG_AVAILABLE_IN_ALL
guint lrg_game_view_preset_count (void);

/**
 * lrg_game_view_preset_get:
 * @index: preset index, from 0 to lrg_game_view_preset_count() - 1
 * @name: (out) (transfer none) (optional): preset name
 * @width: (out) (optional): width in pixels
 * @height: (out) (optional): height in pixels
 *
 * Returns: %FALSE when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_game_view_preset_get (guint         index,
                                   const gchar **name,
                                   gint         *width,
                                   gint         *height);

/**
 * lrg_game_view_preset_find:
 * @name: preset name ("1920x1080", "16:9", "steam-deck", ...)
 * @width: (out) (optional): width in pixels
 * @height: (out) (optional): height in pixels
 *
 * Returns: %TRUE when a preset has that name
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_game_view_preset_find (const gchar *name,
                                    gint        *width,
                                    gint        *height);

/**
 * lrg_game_view_letterbox:
 * @preset: a preset name
 * @view_w: the panel width in pixels
 * @view_h: the panel height in pixels
 * @x: (out): left of the fitted rect
 * @y: (out): top of the fitted rect
 * @w: (out): fitted width
 * @h: (out): fitted height
 *
 * Fits the preset inside the view, preserving its aspect, and centres
 * the result. An unknown preset fills the view.
 */
LRG_AVAILABLE_IN_ALL
void lrg_game_view_letterbox (const gchar *preset,
                              gint         view_w,
                              gint         view_h,
                              gint        *x,
                              gint        *y,
                              gint        *w,
                              gint        *h);

G_END_DECLS
