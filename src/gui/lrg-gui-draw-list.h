/* lrg-gui-draw-list.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A recorded list of 2D drawing commands produced by the GUI toolkit.
 *
 * Widgets never draw directly; they append commands here. A backend
 * replays them (lrg_gui_raylib_render()), tests inspect them. Commands
 * are plain structs so replay is a simple switch.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-gui-types.h"

G_BEGIN_DECLS

/**
 * LrgGuiCommandKind:
 * @LRG_GUI_CMD_RECT_FILLED: a filled (optionally rounded) rectangle
 * @LRG_GUI_CMD_RECT_OUTLINE: a rectangle outline
 * @LRG_GUI_CMD_LINE: a line segment
 * @LRG_GUI_CMD_TRIANGLE: a filled triangle (points in @points)
 * @LRG_GUI_CMD_CIRCLE_FILLED: a filled circle (centre in @rect.x/y, radius
 *   in @thickness)
 * @LRG_GUI_CMD_CIRCLE_OUTLINE: a circle outline
 * @LRG_GUI_CMD_TEXT: text at @rect.x/y with @font_size, optionally monospace
 * @LRG_GUI_CMD_CLIP_PUSH: intersect the clip rectangle with @rect
 * @LRG_GUI_CMD_CLIP_POP: restore the previous clip rectangle
 * @LRG_GUI_CMD_IMAGE: draw @texture (a #GrlTexture) stretched into @rect
 * @LRG_GUI_CMD_CALLBACK: call @callback with @rect (custom rendering such
 *   as a 3D viewport, run in draw order)
 *
 * Drawing command kinds.
 */
typedef enum
{
	LRG_GUI_CMD_RECT_FILLED,
	LRG_GUI_CMD_RECT_OUTLINE,
	LRG_GUI_CMD_LINE,
	LRG_GUI_CMD_TRIANGLE,
	LRG_GUI_CMD_CIRCLE_FILLED,
	LRG_GUI_CMD_CIRCLE_OUTLINE,
	LRG_GUI_CMD_TEXT,
	LRG_GUI_CMD_CLIP_PUSH,
	LRG_GUI_CMD_CLIP_POP,
	LRG_GUI_CMD_IMAGE,
	LRG_GUI_CMD_CALLBACK
} LrgGuiCommandKind;

/**
 * LrgGuiDrawCallback:
 * @rect: the area to draw into (screen pixels)
 * @user_data: (closure): data given with the command
 *
 * Custom rendering hook replayed in command order.
 */
typedef void (*LrgGuiDrawCallback) (const LrgGuiRect *rect,
                                    gpointer          user_data);

/**
 * LrgGuiCommand:
 * @kind: the command kind
 * @rect: target rectangle (or position for text/circles)
 * @points: triangle vertices / line endpoints (x0, y0, x1, y1, x2, y2)
 * @color: fill / stroke / text colour
 * @thickness: stroke width, or circle radius
 * @rounding: corner radius for rectangles
 * @font_size: text size in pixels
 * @monospace: use the monospace font for text
 * @text: (nullable): text for %LRG_GUI_CMD_TEXT (owned by the list)
 * @texture: (nullable): a #GrlTexture for %LRG_GUI_CMD_IMAGE (ref held)
 * @callback: (nullable): hook for %LRG_GUI_CMD_CALLBACK
 * @user_data: data for @callback
 *
 * One recorded drawing command.
 */
typedef struct
{
	LrgGuiCommandKind   kind;
	LrgGuiRect          rect;
	gfloat              points[6];
	LrgGuiColor         color;
	gfloat              thickness;
	gfloat              rounding;
	gfloat              font_size;
	gboolean            monospace;
	gchar              *text;
	GObject            *texture;
	LrgGuiDrawCallback  callback;
	gpointer            user_data;
} LrgGuiCommand;

#define LRG_TYPE_GUI_DRAW_LIST (lrg_gui_draw_list_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGuiDrawList, lrg_gui_draw_list, LRG, GUI_DRAW_LIST, GObject)

/**
 * lrg_gui_draw_list_new:
 *
 * Returns: (transfer full): a new empty draw list
 */
LRG_AVAILABLE_IN_ALL
LrgGuiDrawList * lrg_gui_draw_list_new (void);

/**
 * lrg_gui_draw_list_clear:
 * @self: an #LrgGuiDrawList
 *
 * Removes every command (keeping the allocation).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_draw_list_clear (LrgGuiDrawList *self);

/**
 * lrg_gui_draw_list_get_length:
 * @self: an #LrgGuiDrawList
 *
 * Returns: the number of commands
 */
LRG_AVAILABLE_IN_ALL
guint lrg_gui_draw_list_get_length (LrgGuiDrawList *self);

/**
 * lrg_gui_draw_list_get_command:
 * @self: an #LrgGuiDrawList
 * @index: command index
 *
 * Returns: (transfer none) (nullable): the command
 */
LRG_AVAILABLE_IN_ALL
const LrgGuiCommand * lrg_gui_draw_list_get_command (LrgGuiDrawList *self,
                                                     guint           index);

/**
 * lrg_gui_draw_list_rect_filled:
 * @self: an #LrgGuiDrawList
 * @rect: the rectangle
 * @color: fill colour
 * @rounding: corner radius
 *
 * Returns: the command index (usable with lrg_gui_draw_list_set_rect())
 */
LRG_AVAILABLE_IN_ALL
guint lrg_gui_draw_list_rect_filled (LrgGuiDrawList   *self,
                                     const LrgGuiRect *rect,
                                     LrgGuiColor       color,
                                     gfloat            rounding);

/**
 * lrg_gui_draw_list_rect_outline:
 * @self: an #LrgGuiDrawList
 * @rect: the rectangle
 * @color: stroke colour
 * @thickness: stroke width
 * @rounding: corner radius
 *
 * Returns: the command index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_gui_draw_list_rect_outline (LrgGuiDrawList   *self,
                                      const LrgGuiRect *rect,
                                      LrgGuiColor       color,
                                      gfloat            thickness,
                                      gfloat            rounding);

/**
 * lrg_gui_draw_list_line:
 * @self: an #LrgGuiDrawList
 * @x0: start x
 * @y0: start y
 * @x1: end x
 * @y1: end y
 * @color: stroke colour
 * @thickness: stroke width
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_draw_list_line (LrgGuiDrawList *self,
                             gfloat          x0,
                             gfloat          y0,
                             gfloat          x1,
                             gfloat          y1,
                             LrgGuiColor     color,
                             gfloat          thickness);

/**
 * lrg_gui_draw_list_triangle:
 * @self: an #LrgGuiDrawList
 * @x0: first vertex x
 * @y0: first vertex y
 * @x1: second vertex x
 * @y1: second vertex y
 * @x2: third vertex x
 * @y2: third vertex y
 * @color: fill colour
 *
 * Adds a filled triangle (any winding).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_draw_list_triangle (LrgGuiDrawList *self,
                                 gfloat          x0,
                                 gfloat          y0,
                                 gfloat          x1,
                                 gfloat          y1,
                                 gfloat          x2,
                                 gfloat          y2,
                                 LrgGuiColor     color);

/**
 * lrg_gui_draw_list_circle:
 * @self: an #LrgGuiDrawList
 * @cx: centre x
 * @cy: centre y
 * @radius: radius
 * @color: colour
 * @filled: %TRUE to fill, %FALSE for a 1px outline
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_draw_list_circle (LrgGuiDrawList *self,
                               gfloat          cx,
                               gfloat          cy,
                               gfloat          radius,
                               LrgGuiColor     color,
                               gboolean        filled);

/**
 * lrg_gui_draw_list_text:
 * @self: an #LrgGuiDrawList
 * @x: left
 * @y: top
 * @text: UTF-8 text (copied)
 * @length: bytes of @text to use, or -1
 * @font_size: size in pixels
 * @color: colour
 * @monospace: use the monospace font
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_draw_list_text (LrgGuiDrawList *self,
                             gfloat          x,
                             gfloat          y,
                             const gchar    *text,
                             gssize          length,
                             gfloat          font_size,
                             LrgGuiColor     color,
                             gboolean        monospace);

/**
 * lrg_gui_draw_list_push_clip:
 * @self: an #LrgGuiDrawList
 * @rect: the clip rectangle (intersected with the current one at replay)
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_draw_list_push_clip (LrgGuiDrawList   *self,
                                  const LrgGuiRect *rect);

/**
 * lrg_gui_draw_list_pop_clip:
 * @self: an #LrgGuiDrawList
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_draw_list_pop_clip (LrgGuiDrawList *self);

/**
 * lrg_gui_draw_list_image:
 * @self: an #LrgGuiDrawList
 * @rect: destination rectangle
 * @texture: (transfer none): a #GrlTexture
 * @tint: tint colour (white = untinted)
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_draw_list_image (LrgGuiDrawList   *self,
                              const LrgGuiRect *rect,
                              GObject          *texture,
                              LrgGuiColor       tint);

/**
 * lrg_gui_draw_list_callback:
 * @self: an #LrgGuiDrawList
 * @rect: the area handed to @callback
 * @callback: (scope forever) (closure user_data): the hook
 * @user_data: data for @callback (must outlive the replay)
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_draw_list_callback (LrgGuiDrawList     *self,
                                 const LrgGuiRect   *rect,
                                 LrgGuiDrawCallback  callback,
                                 gpointer            user_data);

/**
 * lrg_gui_draw_list_set_rect:
 * @self: an #LrgGuiDrawList
 * @index: a command index returned by a rect function
 * @rect: the new rectangle
 *
 * Patches a recorded rectangle (used to size a background before its
 * content was laid out, e.g. popups).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_draw_list_set_rect (LrgGuiDrawList   *self,
                                 guint             index,
                                 const LrgGuiRect *rect);

/**
 * lrg_gui_draw_list_append:
 * @self: an #LrgGuiDrawList
 * @other: commands to copy onto the end of @self
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_draw_list_append (LrgGuiDrawList *self,
                               LrgGuiDrawList *other);

/**
 * lrg_gui_draw_list_find_text:
 * @self: an #LrgGuiDrawList
 * @text: exact text to look for
 *
 * Test helper: finds the first text command with exactly @text.
 *
 * Returns: (transfer none) (nullable): the command
 */
LRG_AVAILABLE_IN_ALL
const LrgGuiCommand * lrg_gui_draw_list_find_text (LrgGuiDrawList *self,
                                                   const gchar    *text);

G_END_DECLS
