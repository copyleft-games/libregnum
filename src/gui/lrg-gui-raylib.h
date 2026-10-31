/* lrg-gui-raylib.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * raylib backend for the immediate-mode GUI.
 *
 * LrgGuiRaylib connects an #LrgGuiContext to the engine's window: it
 * loads the UI fonts (a TTF found on the system unless a path is given,
 * falling back to raylib's built-in font), measures text for layout,
 * polls keyboard/mouse/text input into #LrgGuiInput, replays draw lists
 * (with nested scissor clipping) and applies the requested cursor shape.
 * A window (GrlWindow) must exist before fonts are loaded.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-gui-context.h"

G_BEGIN_DECLS

#define LRG_TYPE_GUI_RAYLIB (lrg_gui_raylib_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGuiRaylib, lrg_gui_raylib, LRG, GUI_RAYLIB, GObject)

/**
 * lrg_gui_raylib_new:
 *
 * Returns: (transfer full): a new backend
 */
LRG_AVAILABLE_IN_ALL
LrgGuiRaylib * lrg_gui_raylib_new (void);

/**
 * lrg_gui_raylib_find_system_font:
 * @monospace: look for a monospace face
 *
 * Searches common font locations (DejaVu, Noto, Liberation, Adwaita,
 * Cantarell) for a TrueType file.
 *
 * Returns: (transfer full) (nullable): a font path, or %NULL
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_gui_raylib_find_system_font (gboolean monospace);

/**
 * lrg_gui_raylib_load_fonts:
 * @self: an #LrgGuiRaylib
 * @ui_path: (nullable): proportional UI font, or %NULL to search
 * @mono_path: (nullable): monospace font, or %NULL to search
 * @size: pixel size to rasterize at (use the style's font size)
 *
 * (Re)loads the fonts. Missing files fall back to raylib's default font.
 * Requires an open window.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_raylib_load_fonts (LrgGuiRaylib *self,
                                const gchar  *ui_path,
                                const gchar  *mono_path,
                                gfloat        size);

/**
 * lrg_gui_raylib_get_font_path:
 * @self: an #LrgGuiRaylib
 *
 * Returns: (transfer none) (nullable): the UI font file in use
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_gui_raylib_get_font_path (LrgGuiRaylib *self);

/**
 * lrg_gui_raylib_attach:
 * @self: an #LrgGuiRaylib
 * @ctx: the context to connect
 *
 * Installs this backend's text measurement and the system clipboard into
 * @ctx.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_raylib_attach (LrgGuiRaylib  *self,
                            LrgGuiContext *ctx);

/**
 * lrg_gui_raylib_poll_input:
 * @self: an #LrgGuiRaylib
 * @input: (inout): the snapshot to refresh for a new frame
 *
 * Reads this frame's keyboard, text, mouse and wheel input.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_raylib_poll_input (LrgGuiRaylib *self,
                                LrgGuiInput  *input);

/**
 * lrg_gui_raylib_render:
 * @self: an #LrgGuiRaylib
 * @list: the commands to draw (inside BeginDrawing/EndDrawing)
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_raylib_render (LrgGuiRaylib   *self,
                            LrgGuiDrawList *list);

/**
 * lrg_gui_raylib_apply_cursor:
 * @self: an #LrgGuiRaylib
 * @cursor: the requested shape
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_raylib_apply_cursor (LrgGuiRaylib *self,
                                  LrgGuiCursor  cursor);

G_END_DECLS
