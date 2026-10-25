/* lrg-gui-context.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Immediate-mode GUI toolkit.
 *
 * LrgGuiContext is a small immediate-mode UI library in the spirit of Dear
 * ImGui, written for libregnum's own tools (the editor is built from it)
 * and usable for in-game debug and tool UIs. Each frame the program calls
 * widget functions in the order they appear; the functions return what
 * happened ("clicked", "value changed") and append drawing commands:
 *
 *   lrg_gui_context_begin_frame (ctx, &input, width, height);
 *   lrg_gui_begin_region (ctx, "panel", &rect, LRG_GUI_REGION_SCROLL_Y);
 *   if (lrg_gui_button (ctx, "Save"))
 *       save ();
 *   lrg_gui_drag_float3 (ctx, "Position", pos, 0.05f, "%.2f");
 *   lrg_gui_end_region (ctx);
 *   lrg_gui_context_end_frame (ctx);
 *   lrg_gui_raylib_render (lrg_gui_context_get_draw_list (ctx));
 *
 * Identity: widgets are identified by a hash of their label combined with
 * the ID stack (push_id/pop_id). Text after "##" in a label is part of the
 * identity but not displayed ("Delete##node42"); "###" makes only the part
 * after it the identity, so a label can change while the widget keeps its
 * state ("Inspector (3)###inspector").
 *
 * Coordinates are window pixels. All state that must persist between
 * frames (tree open flags, scroll offsets, text being edited, open popups)
 * lives in the context, keyed by identity.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-gui-types.h"
#include "lrg-gui-draw-list.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Input snapshot
 * ========================================================================== */

/**
 * LRG_GUI_MAX_INPUT_EVENTS:
 *
 * Maximum key presses and characters recorded per frame.
 */
#define LRG_GUI_MAX_INPUT_EVENTS (64)

/**
 * LrgGuiKeyEvent:
 * @key: the key
 * @mods: modifiers held at the time
 * @repeat: %TRUE for an auto-repeat
 *
 * One key press.
 */
typedef struct
{
	LrgGuiKey       key;
	LrgGuiModifiers mods;
	gboolean        repeat;
} LrgGuiKeyEvent;

/**
 * LrgGuiInput:
 * @mouse_x: pointer x in window pixels
 * @mouse_y: pointer y in window pixels
 * @mouse_down: (array fixed-size=3): button held state
 * @wheel_x: horizontal wheel delta this frame
 * @wheel_y: vertical wheel delta this frame (positive = away from user)
 * @mods: modifiers currently held
 * @keys: (array length=n_keys): key presses this frame
 * @n_keys: number of entries in @keys
 * @chars: (array length=n_chars): typed characters this frame
 * @n_chars: number of entries in @chars
 * @key_down: (array fixed-size=85): held state per #LrgGuiKey
 * @time: seconds since an arbitrary epoch
 * @delta: seconds since the previous frame
 *
 * Everything the toolkit reads from the platform for one frame.
 * Backends fill it (lrg_gui_raylib_poll_input()); tests construct it.
 */
typedef struct
{
	gfloat          mouse_x;
	gfloat          mouse_y;
	gboolean        mouse_down[LRG_GUI_MOUSE_BUTTON_COUNT];
	gfloat          wheel_x;
	gfloat          wheel_y;
	LrgGuiModifiers mods;
	LrgGuiKeyEvent  keys[LRG_GUI_MAX_INPUT_EVENTS];
	guint           n_keys;
	gunichar        chars[LRG_GUI_MAX_INPUT_EVENTS];
	guint           n_chars;
	gboolean        key_down[LRG_GUI_KEY_COUNT];
	gdouble         time;
	gfloat          delta;
} LrgGuiInput;

/**
 * lrg_gui_input_init:
 * @input: (out caller-allocates): an input snapshot
 *
 * Zeroes @input with the mouse far outside any window.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_input_init (LrgGuiInput *input);

/**
 * lrg_gui_input_new_frame:
 * @input: an input snapshot
 * @delta: seconds since the previous frame
 *
 * Clears the per-frame events (key presses, characters, wheel) and
 * advances the clock, keeping held state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_input_new_frame (LrgGuiInput *input,
                              gfloat       delta);

/**
 * lrg_gui_input_add_key:
 * @input: an input snapshot
 * @key: the key pressed
 * @mods: modifiers
 *
 * Records a key press (also marks it held).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_input_add_key (LrgGuiInput     *input,
                            LrgGuiKey        key,
                            LrgGuiModifiers  mods);

/**
 * lrg_gui_input_add_text:
 * @input: an input snapshot
 * @text: UTF-8 text typed
 *
 * Records typed characters.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_input_add_text (LrgGuiInput *input,
                             const gchar *text);

/* ==========================================================================
 * Style
 * ========================================================================== */

/**
 * LrgGuiStyleColor:
 * @LRG_GUI_COL_TEXT: normal text
 * @LRG_GUI_COL_TEXT_DISABLED: disabled / secondary text
 * @LRG_GUI_COL_WINDOW_BG: application background
 * @LRG_GUI_COL_PANEL_BG: panel background
 * @LRG_GUI_COL_POPUP_BG: popup, menu and tooltip background
 * @LRG_GUI_COL_BORDER: borders
 * @LRG_GUI_COL_FRAME_BG: input field background
 * @LRG_GUI_COL_FRAME_BG_HOVERED: hovered field
 * @LRG_GUI_COL_FRAME_BG_ACTIVE: active field
 * @LRG_GUI_COL_BUTTON: button
 * @LRG_GUI_COL_BUTTON_HOVERED: hovered button
 * @LRG_GUI_COL_BUTTON_ACTIVE: pressed button
 * @LRG_GUI_COL_HEADER: selected row / header
 * @LRG_GUI_COL_HEADER_HOVERED: hovered row
 * @LRG_GUI_COL_HEADER_ACTIVE: pressed row
 * @LRG_GUI_COL_CHECK_MARK: check marks and radio dots
 * @LRG_GUI_COL_SLIDER_GRAB: slider handles
 * @LRG_GUI_COL_SEPARATOR: separators and splitters
 * @LRG_GUI_COL_SCROLLBAR_BG: scrollbar track
 * @LRG_GUI_COL_SCROLLBAR_GRAB: scrollbar thumb
 * @LRG_GUI_COL_TAB: inactive tab
 * @LRG_GUI_COL_TAB_HOVERED: hovered tab
 * @LRG_GUI_COL_TAB_ACTIVE: selected tab
 * @LRG_GUI_COL_MENUBAR_BG: menu bar
 * @LRG_GUI_COL_TEXT_SELECTION: selected text background
 * @LRG_GUI_COL_DROP_TARGET: drag-and-drop target highlight
 * @LRG_GUI_COL_MODAL_DIM: modal backdrop
 * @LRG_GUI_COL_ACCENT: accent (focus rings, active tool)
 * @LRG_GUI_COL_ERROR: error text
 * @LRG_GUI_COL_WARNING: warning text
 * @LRG_GUI_COL_SUCCESS: success text
 * @LRG_GUI_COL_AXIS_X: X axis (red)
 * @LRG_GUI_COL_AXIS_Y: Y axis (green)
 * @LRG_GUI_COL_AXIS_Z: Z axis (blue)
 * @LRG_GUI_COL_COUNT: number of colours
 *
 * Indices into #LrgGuiStyle.colors.
 */
typedef enum
{
	LRG_GUI_COL_TEXT,
	LRG_GUI_COL_TEXT_DISABLED,
	LRG_GUI_COL_WINDOW_BG,
	LRG_GUI_COL_PANEL_BG,
	LRG_GUI_COL_POPUP_BG,
	LRG_GUI_COL_BORDER,
	LRG_GUI_COL_FRAME_BG,
	LRG_GUI_COL_FRAME_BG_HOVERED,
	LRG_GUI_COL_FRAME_BG_ACTIVE,
	LRG_GUI_COL_BUTTON,
	LRG_GUI_COL_BUTTON_HOVERED,
	LRG_GUI_COL_BUTTON_ACTIVE,
	LRG_GUI_COL_HEADER,
	LRG_GUI_COL_HEADER_HOVERED,
	LRG_GUI_COL_HEADER_ACTIVE,
	LRG_GUI_COL_CHECK_MARK,
	LRG_GUI_COL_SLIDER_GRAB,
	LRG_GUI_COL_SEPARATOR,
	LRG_GUI_COL_SCROLLBAR_BG,
	LRG_GUI_COL_SCROLLBAR_GRAB,
	LRG_GUI_COL_TAB,
	LRG_GUI_COL_TAB_HOVERED,
	LRG_GUI_COL_TAB_ACTIVE,
	LRG_GUI_COL_MENUBAR_BG,
	LRG_GUI_COL_TEXT_SELECTION,
	LRG_GUI_COL_DROP_TARGET,
	LRG_GUI_COL_MODAL_DIM,
	LRG_GUI_COL_ACCENT,
	LRG_GUI_COL_ERROR,
	LRG_GUI_COL_WARNING,
	LRG_GUI_COL_SUCCESS,
	LRG_GUI_COL_AXIS_X,
	LRG_GUI_COL_AXIS_Y,
	LRG_GUI_COL_AXIS_Z,
	LRG_GUI_COL_COUNT
} LrgGuiStyleColor;

/**
 * LrgGuiStyle:
 * @colors: (array fixed-size=34): palette, indexed by #LrgGuiStyleColor
 * @font_size: text size in pixels
 * @frame_padding_x: horizontal padding inside buttons and fields
 * @frame_padding_y: vertical padding inside buttons and fields
 * @item_spacing_x: gap between items on one line
 * @item_spacing_y: gap between rows
 * @region_padding: padding inside regions
 * @indent: tree indentation per level
 * @rounding: frame corner radius
 * @scrollbar_size: scrollbar width
 * @label_ratio: share of the width used by property_label() labels
 *
 * Visual parameters. Modify the struct returned by
 * lrg_gui_context_get_style() between frames.
 */
typedef struct
{
	LrgGuiColor colors[LRG_GUI_COL_COUNT];
	gfloat      font_size;
	gfloat      frame_padding_x;
	gfloat      frame_padding_y;
	gfloat      item_spacing_x;
	gfloat      item_spacing_y;
	gfloat      region_padding;
	gfloat      indent;
	gfloat      rounding;
	gfloat      scrollbar_size;
	gfloat      label_ratio;
} LrgGuiStyle;

/**
 * lrg_gui_style_init:
 * @style: (out caller-allocates): a style
 * @theme: (nullable): "mocha" (default), "macchiato", "frappe" or "latte"
 *   (the Catppuccin flavours)
 * @scale: UI scale factor applied to every metric
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_style_init (LrgGuiStyle *style,
                         const gchar *theme,
                         gfloat       scale);

/* ==========================================================================
 * Context
 * ========================================================================== */

/**
 * LrgGuiMeasureFunc:
 * @text: UTF-8 text
 * @length: bytes of @text, or -1
 * @font_size: size in pixels
 * @monospace: whether the monospace font is meant
 * @user_data: (closure): data given to lrg_gui_context_set_measure_func()
 *
 * Measures the advance width of text. Backends supply the real font
 * metrics; the default is a fixed 0.55 em per character, used headless.
 *
 * Returns: width in pixels
 */
typedef gfloat (*LrgGuiMeasureFunc) (const gchar *text,
                                     gssize       length,
                                     gfloat       font_size,
                                     gboolean     monospace,
                                     gpointer     user_data);

/**
 * LrgGuiClipboardGetFunc:
 * @user_data: (closure): data
 *
 * Returns: (transfer full) (nullable): the system clipboard text
 */
typedef gchar * (*LrgGuiClipboardGetFunc) (gpointer user_data);

/**
 * LrgGuiClipboardSetFunc:
 * @text: text to place on the system clipboard
 * @user_data: (closure): data
 */
typedef void (*LrgGuiClipboardSetFunc) (const gchar *text,
                                        gpointer     user_data);

#define LRG_TYPE_GUI_CONTEXT (lrg_gui_context_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGuiContext, lrg_gui_context, LRG, GUI_CONTEXT, GObject)

/**
 * lrg_gui_context_new:
 *
 * Returns: (transfer full): a new context with the default style
 */
LRG_AVAILABLE_IN_ALL
LrgGuiContext * lrg_gui_context_new (void);

/**
 * lrg_gui_context_get_style:
 * @self: an #LrgGuiContext
 *
 * Returns: (transfer none): the live style
 */
LRG_AVAILABLE_IN_ALL
LrgGuiStyle * lrg_gui_context_get_style (LrgGuiContext *self);

/**
 * lrg_gui_context_set_measure_func:
 * @self: an #LrgGuiContext
 * @func: (scope notified) (closure user_data) (destroy destroy) (nullable):
 *   the measure function, or %NULL for the default
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_context_set_measure_func (LrgGuiContext     *self,
                                       LrgGuiMeasureFunc  func,
                                       gpointer           user_data,
                                       GDestroyNotify     destroy);

/**
 * lrg_gui_context_set_clipboard_funcs:
 * @self: an #LrgGuiContext
 * @get_func: (scope notified) (nullable): reads the system clipboard
 * @set_func: (scope notified) (nullable): writes the system clipboard
 * @user_data: (closure): data for both
 * @destroy: (nullable): frees @user_data
 *
 * Without functions, copy/paste uses an internal buffer.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_context_set_clipboard_funcs (LrgGuiContext          *self,
                                          LrgGuiClipboardGetFunc  get_func,
                                          LrgGuiClipboardSetFunc  set_func,
                                          gpointer                user_data,
                                          GDestroyNotify          destroy);

/**
 * lrg_gui_context_set_clipboard:
 * @self: an #LrgGuiContext
 * @text: text to copy
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_context_set_clipboard (LrgGuiContext *self,
                                    const gchar   *text);

/**
 * lrg_gui_context_get_clipboard:
 * @self: an #LrgGuiContext
 *
 * Returns: (transfer full) (nullable): the clipboard text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_gui_context_get_clipboard (LrgGuiContext *self);

/**
 * lrg_gui_context_begin_frame:
 * @self: an #LrgGuiContext
 * @input: this frame's input (copied)
 * @width: window width in pixels
 * @height: window height in pixels
 *
 * Starts a frame: clears the draw lists and processes popup dismissal.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_context_begin_frame (LrgGuiContext     *self,
                                  const LrgGuiInput *input,
                                  gfloat             width,
                                  gfloat             height);

/**
 * lrg_gui_context_end_frame:
 * @self: an #LrgGuiContext
 *
 * Finishes the frame: emits tooltips and the drag preview, merges the
 * layers into the final draw list and releases stale interaction state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_context_end_frame (LrgGuiContext *self);

/**
 * lrg_gui_context_get_draw_list:
 * @self: an #LrgGuiContext
 *
 * Returns: (transfer none): the finished frame's commands (valid until the
 *   next begin_frame)
 */
LRG_AVAILABLE_IN_ALL
LrgGuiDrawList * lrg_gui_context_get_draw_list (LrgGuiContext *self);

/**
 * lrg_gui_context_get_frame:
 * @self: an #LrgGuiContext
 *
 * Returns: the number of frames begun
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_gui_context_get_frame (LrgGuiContext *self);

/**
 * lrg_gui_context_get_cursor:
 * @self: an #LrgGuiContext
 *
 * Returns: the pointer shape requested this frame
 */
LRG_AVAILABLE_IN_ALL
LrgGuiCursor lrg_gui_context_get_cursor (LrgGuiContext *self);

/**
 * lrg_gui_context_wants_mouse:
 * @self: an #LrgGuiContext
 *
 * Returns: %TRUE if the pointer is over UI or a widget is active (the 3D
 *   scene should not react to the mouse)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_context_wants_mouse (LrgGuiContext *self);

/**
 * lrg_gui_context_wants_keyboard:
 * @self: an #LrgGuiContext
 *
 * Returns: %TRUE while a text field has keyboard focus
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_context_wants_keyboard (LrgGuiContext *self);

/**
 * lrg_gui_context_needs_redraw:
 * @self: an #LrgGuiContext
 *
 * Returns: %TRUE if something is animating (caret blink, drags) so an
 *   event-driven host should schedule another frame
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_context_needs_redraw (LrgGuiContext *self);

/* ==========================================================================
 * Identity
 * ========================================================================== */

/**
 * lrg_gui_get_id:
 * @self: an #LrgGuiContext
 * @str: a label or id string
 *
 * Returns: the identity @str would have in the current ID scope
 */
LRG_AVAILABLE_IN_ALL
LrgGuiId lrg_gui_get_id (LrgGuiContext *self,
                         const gchar   *str);

/**
 * lrg_gui_push_id:
 * @self: an #LrgGuiContext
 * @str: a scope name
 *
 * Opens an ID scope (e.g. per inspected object).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_push_id (LrgGuiContext *self,
                      const gchar   *str);

/**
 * lrg_gui_push_id_int:
 * @self: an #LrgGuiContext
 * @value: a scope number (e.g. a list index)
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_push_id_int (LrgGuiContext *self,
                          gint64         value);

/**
 * lrg_gui_push_id_ptr:
 * @self: an #LrgGuiContext
 * @ptr: a scope pointer (e.g. the object being shown)
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_push_id_ptr (LrgGuiContext *self,
                          gconstpointer  ptr);

/**
 * lrg_gui_pop_id:
 * @self: an #LrgGuiContext
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_pop_id (LrgGuiContext *self);

/* ==========================================================================
 * Regions and layout
 * ========================================================================== */

/**
 * LrgGuiRegionFlags:
 * @LRG_GUI_REGION_NONE: plain clipped area
 * @LRG_GUI_REGION_SCROLL_Y: vertical scrolling (wheel + scrollbar)
 * @LRG_GUI_REGION_SCROLL_X: horizontal scrolling (Shift+wheel + scrollbar)
 * @LRG_GUI_REGION_BORDER: draw a border
 * @LRG_GUI_REGION_BACKGROUND: fill with the panel colour
 * @LRG_GUI_REGION_NO_PADDING: content starts at the edge
 * @LRG_GUI_REGION_HORIZONTAL: items flow left to right (toolbars, menu bars)
 *
 * Region behaviour flags.
 */
typedef enum
{
	LRG_GUI_REGION_NONE       = 0,
	LRG_GUI_REGION_SCROLL_Y   = 1 << 0,
	LRG_GUI_REGION_SCROLL_X   = 1 << 1,
	LRG_GUI_REGION_BORDER     = 1 << 2,
	LRG_GUI_REGION_BACKGROUND = 1 << 3,
	LRG_GUI_REGION_NO_PADDING = 1 << 4,
	LRG_GUI_REGION_HORIZONTAL = 1 << 5
} LrgGuiRegionFlags;

/**
 * lrg_gui_begin_region:
 * @self: an #LrgGuiContext
 * @str_id: identity of the region (scroll state is kept per id)
 * @rect: the region's rectangle; %NULL for "the rest of the current
 *   region" laid out as an item (a child region)
 * @flags: #LrgGuiRegionFlags
 *
 * Opens a clipped layout area. Must be paired with lrg_gui_end_region().
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_begin_region (LrgGuiContext     *self,
                           const gchar       *str_id,
                           const LrgGuiRect  *rect,
                           LrgGuiRegionFlags  flags);

/**
 * lrg_gui_begin_child:
 * @self: an #LrgGuiContext
 * @str_id: identity
 * @width: width (<= 0 = available width plus @width)
 * @height: height (<= 0 = available height plus @height)
 * @flags: #LrgGuiRegionFlags
 *
 * Lays out a child region as an item of the current region.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_begin_child (LrgGuiContext     *self,
                          const gchar       *str_id,
                          gfloat             width,
                          gfloat             height,
                          LrgGuiRegionFlags  flags);

/**
 * lrg_gui_end_region:
 * @self: an #LrgGuiContext
 *
 * Closes the current region (also closes a child region).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_end_region (LrgGuiContext *self);

/**
 * lrg_gui_get_region_rect:
 * @self: an #LrgGuiContext
 * @rect: (out caller-allocates): the current region's outer rectangle
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_get_region_rect (LrgGuiContext *self,
                              LrgGuiRect    *rect);

/**
 * lrg_gui_get_content_avail:
 * @self: an #LrgGuiContext
 * @width: (out) (optional): width left on the current line
 * @height: (out) (optional): height left below the cursor
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_get_content_avail (LrgGuiContext *self,
                                gfloat        *width,
                                gfloat        *height);

/**
 * lrg_gui_get_cursor:
 * @self: an #LrgGuiContext
 * @x: (out) (optional): next item x
 * @y: (out) (optional): next item y
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_get_cursor (LrgGuiContext *self,
                         gfloat        *x,
                         gfloat        *y);

/**
 * lrg_gui_set_cursor:
 * @self: an #LrgGuiContext
 * @x: next item x (window pixels)
 * @y: next item y
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_set_cursor (LrgGuiContext *self,
                         gfloat         x,
                         gfloat         y);

/**
 * lrg_gui_same_line:
 * @self: an #LrgGuiContext
 * @spacing: gap in pixels, or < 0 for the style spacing
 *
 * Places the next item to the right of the previous one.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_same_line (LrgGuiContext *self,
                        gfloat         spacing);

/**
 * lrg_gui_spacing:
 * @self: an #LrgGuiContext
 *
 * Adds a small vertical gap.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_spacing (LrgGuiContext *self);

/**
 * lrg_gui_dummy:
 * @self: an #LrgGuiContext
 * @width: width
 * @height: height
 *
 * Reserves empty space as an item.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_dummy (LrgGuiContext *self,
                    gfloat         width,
                    gfloat         height);

/**
 * lrg_gui_separator:
 * @self: an #LrgGuiContext
 *
 * Draws a horizontal rule.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_separator (LrgGuiContext *self);

/**
 * lrg_gui_separator_text:
 * @self: an #LrgGuiContext
 * @label: heading text
 *
 * A rule with a label in it (section headings).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_separator_text (LrgGuiContext *self,
                             const gchar   *label);

/**
 * lrg_gui_indent:
 * @self: an #LrgGuiContext
 * @width: indentation, or <= 0 for the style indent
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_indent (LrgGuiContext *self,
                     gfloat         width);

/**
 * lrg_gui_unindent:
 * @self: an #LrgGuiContext
 * @width: the same value passed to lrg_gui_indent()
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_unindent (LrgGuiContext *self,
                       gfloat         width);

/**
 * lrg_gui_set_next_item_width:
 * @self: an #LrgGuiContext
 * @width: width of the next widget; <= 0 means available width plus @width
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_set_next_item_width (LrgGuiContext *self,
                                  gfloat         width);

/**
 * lrg_gui_property_label:
 * @self: an #LrgGuiContext
 * @label: the property name
 *
 * Draws @label in the left column of a two-column property grid and
 * positions the next widget in the right column with its width set to fill
 * it (inspector rows).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_property_label (LrgGuiContext *self,
                             const gchar   *label);

/**
 * lrg_gui_calc_text_width:
 * @self: an #LrgGuiContext
 * @text: text (the part after "##" is ignored)
 *
 * Returns: the width @text would occupy
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_gui_calc_text_width (LrgGuiContext *self,
                                const gchar   *text);

/**
 * lrg_gui_get_frame_height:
 * @self: an #LrgGuiContext
 *
 * Returns: the height of a button or field
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_gui_get_frame_height (LrgGuiContext *self);

/**
 * lrg_gui_get_draw_list:
 * @self: an #LrgGuiContext
 *
 * Returns: (transfer none): the layer currently being drawn, for custom
 *   widgets (draw only within lrg_gui_get_item_rect() or the region)
 */
LRG_AVAILABLE_IN_ALL
LrgGuiDrawList * lrg_gui_get_draw_list (LrgGuiContext *self);

/**
 * lrg_gui_draw_callback:
 * @self: an #LrgGuiContext
 * @rect: the area
 * @callback: (scope forever) (closure user_data): rendering hook
 * @user_data: data for @callback
 *
 * Records a custom rendering hook in draw order (e.g. the 3D viewport).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_draw_callback (LrgGuiContext      *self,
                            const LrgGuiRect   *rect,
                            LrgGuiDrawCallback  callback,
                            gpointer            user_data);

/* ==========================================================================
 * Text
 * ========================================================================== */

/**
 * lrg_gui_text:
 * @self: an #LrgGuiContext
 * @text: text (not parsed for "##")
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_text (LrgGuiContext *self,
                   const gchar   *text);

/**
 * lrg_gui_textf:
 * @self: an #LrgGuiContext
 * @format: printf format
 * @...: arguments
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_textf (LrgGuiContext *self,
                    const gchar   *format,
                    ...) G_GNUC_PRINTF (2, 3);

/**
 * lrg_gui_text_colored:
 * @self: an #LrgGuiContext
 * @color: text colour
 * @text: text
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_text_colored (LrgGuiContext *self,
                           LrgGuiColor    color,
                           const gchar   *text);

/**
 * lrg_gui_text_disabled:
 * @self: an #LrgGuiContext
 * @text: secondary text
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_text_disabled (LrgGuiContext *self,
                            const gchar   *text);

/**
 * lrg_gui_text_mono:
 * @self: an #LrgGuiContext
 * @color: text colour
 * @text: text in the monospace font (logs, code)
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_text_mono (LrgGuiContext *self,
                        LrgGuiColor    color,
                        const gchar   *text);

/**
 * lrg_gui_text_wrapped:
 * @self: an #LrgGuiContext
 * @text: text wrapped at the region width
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_text_wrapped (LrgGuiContext *self,
                           const gchar   *text);

/* ==========================================================================
 * Buttons and toggles
 * ========================================================================== */

/**
 * lrg_gui_button:
 * @self: an #LrgGuiContext
 * @label: label ("##" rules apply)
 *
 * Returns: %TRUE when clicked
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_button (LrgGuiContext *self,
                         const gchar   *label);

/**
 * lrg_gui_button_sized:
 * @self: an #LrgGuiContext
 * @label: label
 * @width: width (<= 0 = fit text; < 0 with available-width semantics only
 *   via set_next_item_width)
 * @height: height (<= 0 = frame height)
 *
 * Returns: %TRUE when clicked
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_button_sized (LrgGuiContext *self,
                               const gchar   *label,
                               gfloat         width,
                               gfloat         height);

/**
 * lrg_gui_toggle_button:
 * @self: an #LrgGuiContext
 * @label: label
 * @active: (inout): toggled state
 *
 * A button that stays highlighted while active (toolbar tools).
 *
 * Returns: %TRUE when toggled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_toggle_button (LrgGuiContext *self,
                                const gchar   *label,
                                gboolean      *active);

/**
 * lrg_gui_invisible_button:
 * @self: an #LrgGuiContext
 * @str_id: identity
 * @width: width
 * @height: height
 *
 * An interactive area without visuals (custom widgets, the viewport).
 * Item queries (hovered, active, clicked with any button) apply to it.
 *
 * Returns: %TRUE when left-clicked
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_invisible_button (LrgGuiContext *self,
                                   const gchar   *str_id,
                                   gfloat         width,
                                   gfloat         height);

/**
 * lrg_gui_checkbox:
 * @self: an #LrgGuiContext
 * @label: label
 * @value: (inout): the boolean
 *
 * Returns: %TRUE when toggled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_checkbox (LrgGuiContext *self,
                           const gchar   *label,
                           gboolean      *value);

/**
 * lrg_gui_radio_button:
 * @self: an #LrgGuiContext
 * @label: label
 * @active: whether this option is selected
 *
 * Returns: %TRUE when clicked
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_radio_button (LrgGuiContext *self,
                               const gchar   *label,
                               gboolean       active);

/**
 * LrgGuiSelectableFlags:
 * @LRG_GUI_SELECTABLE_NONE: defaults
 * @LRG_GUI_SELECTABLE_DISABLED: greyed out, not clickable
 * @LRG_GUI_SELECTABLE_DONT_CLOSE_POPUP: clicking inside a popup keeps it
 * @LRG_GUI_SELECTABLE_MONOSPACE: monospace label
 *
 * Selectable row flags.
 */
typedef enum
{
	LRG_GUI_SELECTABLE_NONE             = 0,
	LRG_GUI_SELECTABLE_DISABLED         = 1 << 0,
	LRG_GUI_SELECTABLE_DONT_CLOSE_POPUP = 1 << 1,
	LRG_GUI_SELECTABLE_MONOSPACE        = 1 << 2
} LrgGuiSelectableFlags;

/**
 * lrg_gui_selectable:
 * @self: an #LrgGuiContext
 * @label: label
 * @selected: draw as selected
 * @flags: #LrgGuiSelectableFlags
 *
 * A full-width row (lists, combo entries).
 *
 * Returns: %TRUE when clicked
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_selectable (LrgGuiContext         *self,
                             const gchar           *label,
                             gboolean               selected,
                             LrgGuiSelectableFlags  flags);

/* ==========================================================================
 * Trees
 * ========================================================================== */

/**
 * LrgGuiTreeFlags:
 * @LRG_GUI_TREE_NONE: defaults
 * @LRG_GUI_TREE_LEAF: no children, no arrow
 * @LRG_GUI_TREE_SELECTED: highlight as selected
 * @LRG_GUI_TREE_DEFAULT_OPEN: open the first time it appears
 * @LRG_GUI_TREE_OPEN_ON_ARROW: only the arrow toggles; clicking the label
 *   selects (outliner rows)
 * @LRG_GUI_TREE_FRAMED: draw as a header bar (collapsing headers)
 * @LRG_GUI_TREE_DIMMED: draw the label disabled-coloured (hidden nodes)
 *
 * Tree node flags.
 */
typedef enum
{
	LRG_GUI_TREE_NONE          = 0,
	LRG_GUI_TREE_LEAF          = 1 << 0,
	LRG_GUI_TREE_SELECTED      = 1 << 1,
	LRG_GUI_TREE_DEFAULT_OPEN  = 1 << 2,
	LRG_GUI_TREE_OPEN_ON_ARROW = 1 << 3,
	LRG_GUI_TREE_FRAMED        = 1 << 4,
	LRG_GUI_TREE_DIMMED        = 1 << 5
} LrgGuiTreeFlags;

/**
 * lrg_gui_tree_node:
 * @self: an #LrgGuiContext
 * @label: label ("##" rules apply)
 * @flags: #LrgGuiTreeFlags
 *
 * A collapsible row. When it returns %TRUE the children follow and
 * lrg_gui_tree_pop() must be called. Item queries report label clicks.
 *
 * Returns: %TRUE if open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_tree_node (LrgGuiContext   *self,
                            const gchar     *label,
                            LrgGuiTreeFlags  flags);

/**
 * lrg_gui_tree_pop:
 * @self: an #LrgGuiContext
 *
 * Closes an open tree node (undoes its indentation and ID scope).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_tree_pop (LrgGuiContext *self);

/**
 * lrg_gui_set_next_item_open:
 * @self: an #LrgGuiContext
 * @open: open state to force on the next tree node / header
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_set_next_item_open (LrgGuiContext *self,
                                 gboolean       open);

/**
 * lrg_gui_collapsing_header:
 * @self: an #LrgGuiContext
 * @label: label
 * @default_open: initial state
 *
 * A framed section header; no tree_pop() needed.
 *
 * Returns: %TRUE if open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_collapsing_header (LrgGuiContext *self,
                                    const gchar   *label,
                                    gboolean       default_open);

/* ==========================================================================
 * Value editors
 * ========================================================================== */

/**
 * lrg_gui_drag_float:
 * @self: an #LrgGuiContext
 * @label: label (shown inside the field when no property label is used;
 *   "##" hides it)
 * @value: (inout): the value
 * @speed: change per pixel dragged (Shift = x10, Alt = x0.1)
 * @min: lower bound (min >= max disables clamping)
 * @max: upper bound
 * @format: (nullable): printf format, default "%.3f"
 *
 * Drag horizontally to scrub; double-click or Ctrl+click to type.
 *
 * Returns: %TRUE when the value changed this frame
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_drag_float (LrgGuiContext *self,
                             const gchar   *label,
                             gfloat        *value,
                             gfloat         speed,
                             gfloat         min,
                             gfloat         max,
                             const gchar   *format);

/**
 * lrg_gui_drag_float3:
 * @self: an #LrgGuiContext
 * @label: label (identity)
 * @value: (array fixed-size=3) (inout): the vector
 * @speed: change per pixel
 * @format: (nullable): printf format
 *
 * Three drag fields tinted X/Y/Z (transform rows).
 *
 * Returns: %TRUE when any component changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_drag_float3 (LrgGuiContext *self,
                              const gchar   *label,
                              gfloat        *value,
                              gfloat         speed,
                              const gchar   *format);

/**
 * lrg_gui_drag_int:
 * @self: an #LrgGuiContext
 * @label: label
 * @value: (inout): the value
 * @speed: change per pixel
 * @min: lower bound (min >= max disables clamping)
 * @max: upper bound
 *
 * Returns: %TRUE when the value changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_drag_int (LrgGuiContext *self,
                           const gchar   *label,
                           gint          *value,
                           gfloat         speed,
                           gint           min,
                           gint           max);

/**
 * lrg_gui_slider_float:
 * @self: an #LrgGuiContext
 * @label: label
 * @value: (inout): the value
 * @min: lower bound
 * @max: upper bound
 * @format: (nullable): printf format
 *
 * Returns: %TRUE when the value changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_slider_float (LrgGuiContext *self,
                               const gchar   *label,
                               gfloat        *value,
                               gfloat         min,
                               gfloat         max,
                               const gchar   *format);

/**
 * LrgGuiInputTextFlags:
 * @LRG_GUI_INPUT_TEXT_NONE: defaults: report every edit
 * @LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE: return %TRUE only on Enter (or
 *   focus loss after an edit), not on every keystroke
 * @LRG_GUI_INPUT_TEXT_READ_ONLY: selectable and copyable only
 * @LRG_GUI_INPUT_TEXT_PASSWORD: show dots
 * @LRG_GUI_INPUT_TEXT_AUTO_SELECT_ALL: select everything on focus
 * @LRG_GUI_INPUT_TEXT_MONOSPACE: monospace font (console, paths)
 * @LRG_GUI_INPUT_TEXT_KEEP_FOCUS: keep focus after Enter (console input)
 *
 * Text field flags.
 */
typedef enum
{
	LRG_GUI_INPUT_TEXT_NONE               = 0,
	LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE = 1 << 0,
	LRG_GUI_INPUT_TEXT_READ_ONLY          = 1 << 1,
	LRG_GUI_INPUT_TEXT_PASSWORD           = 1 << 2,
	LRG_GUI_INPUT_TEXT_AUTO_SELECT_ALL    = 1 << 3,
	LRG_GUI_INPUT_TEXT_MONOSPACE          = 1 << 4,
	LRG_GUI_INPUT_TEXT_KEEP_FOCUS         = 1 << 5
} LrgGuiInputTextFlags;

/**
 * lrg_gui_input_text:
 * @self: an #LrgGuiContext
 * @label: label ("##" rules apply)
 * @hint: (nullable): placeholder shown while empty and unfocused
 * @buffer: the text, edited in place
 * @flags: #LrgGuiInputTextFlags
 *
 * A single-line text field with selection, word movement (Ctrl+arrows),
 * Ctrl+A/C/X/V, Home/End, Escape to revert and Tab to move to the next
 * field.
 *
 * Returns: %TRUE when the text changed (or was committed, with
 *   %LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_input_text (LrgGuiContext        *self,
                             const gchar          *label,
                             const gchar          *hint,
                             GString              *buffer,
                             LrgGuiInputTextFlags  flags);

/**
 * lrg_gui_set_keyboard_focus_here:
 * @self: an #LrgGuiContext
 *
 * Gives keyboard focus to the next text field (e.g. when a rename or the
 * command palette opens).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_set_keyboard_focus_here (LrgGuiContext *self);

/**
 * lrg_gui_combo:
 * @self: an #LrgGuiContext
 * @label: label
 * @current: (inout): selected index
 * @items: (array length=n_items): item labels
 * @n_items: number of items
 *
 * Returns: %TRUE when the selection changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_combo (LrgGuiContext       *self,
                        const gchar         *label,
                        gint                *current,
                        const gchar * const *items,
                        gint                 n_items);

/**
 * lrg_gui_begin_combo:
 * @self: an #LrgGuiContext
 * @label: label
 * @preview: text shown in the closed combo
 *
 * Custom combo: add lrg_gui_selectable() rows and call lrg_gui_end_combo()
 * when this returns %TRUE.
 *
 * Returns: %TRUE while the list is open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_begin_combo (LrgGuiContext *self,
                              const gchar   *label,
                              const gchar   *preview);

/**
 * lrg_gui_end_combo:
 * @self: an #LrgGuiContext
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_end_combo (LrgGuiContext *self);

/**
 * lrg_gui_color_edit:
 * @self: an #LrgGuiContext
 * @label: label
 * @rgba: (array fixed-size=4) (inout): colour, components 0..1
 * @alpha: edit the alpha component too
 *
 * A swatch plus R/G/B(/A) drag fields; clicking the swatch opens a palette
 * and hex entry popup.
 *
 * Returns: %TRUE when the colour changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_color_edit (LrgGuiContext *self,
                             const gchar   *label,
                             gfloat        *rgba,
                             gboolean       alpha);

/**
 * lrg_gui_progress_bar:
 * @self: an #LrgGuiContext
 * @fraction: 0..1
 * @overlay: (nullable): text drawn on the bar
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_progress_bar (LrgGuiContext *self,
                           gfloat         fraction,
                           const gchar   *overlay);

/**
 * lrg_gui_image:
 * @self: an #LrgGuiContext
 * @texture: (transfer none): a #GrlTexture
 * @width: width
 * @height: height
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_image (LrgGuiContext *self,
                    GObject       *texture,
                    gfloat         width,
                    gfloat         height);

/* ==========================================================================
 * Item queries (about the most recent widget)
 * ========================================================================== */

/**
 * lrg_gui_is_item_hovered:
 * @self: an #LrgGuiContext
 *
 * Returns: %TRUE if the pointer is over the last item (and not blocked by a
 *   popup)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_is_item_hovered (LrgGuiContext *self);

/**
 * lrg_gui_is_item_active:
 * @self: an #LrgGuiContext
 *
 * Returns: %TRUE while the last item is being interacted with (held,
 *   dragged, edited)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_is_item_active (LrgGuiContext *self);

/**
 * lrg_gui_is_item_activated:
 * @self: an #LrgGuiContext
 *
 * Returns: %TRUE on the frame the last item became active (start of a drag:
 *   begin a new undo step there)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_is_item_activated (LrgGuiContext *self);

/**
 * lrg_gui_is_item_deactivated:
 * @self: an #LrgGuiContext
 *
 * Returns: %TRUE on the frame the last item stopped being active
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_is_item_deactivated (LrgGuiContext *self);

/**
 * lrg_gui_is_item_clicked:
 * @self: an #LrgGuiContext
 * @button: the mouse button
 *
 * Returns: %TRUE if @button was pressed on the last item this frame
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_is_item_clicked (LrgGuiContext     *self,
                                  LrgGuiMouseButton  button);

/**
 * lrg_gui_is_item_double_clicked:
 * @self: an #LrgGuiContext
 *
 * Returns: %TRUE on a left double-click on the last item
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_is_item_double_clicked (LrgGuiContext *self);

/**
 * lrg_gui_get_item_rect:
 * @self: an #LrgGuiContext
 * @rect: (out caller-allocates): the last item's rectangle
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_get_item_rect (LrgGuiContext *self,
                            LrgGuiRect    *rect);

/**
 * lrg_gui_get_item_id:
 * @self: an #LrgGuiContext
 *
 * Returns: the last item's identity
 */
LRG_AVAILABLE_IN_ALL
LrgGuiId lrg_gui_get_item_id (LrgGuiContext *self);

/**
 * lrg_gui_set_item_tooltip:
 * @self: an #LrgGuiContext
 * @text: tooltip text
 *
 * Shows @text near the pointer after a short hover on the last item.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_set_item_tooltip (LrgGuiContext *self,
                               const gchar   *text);

/**
 * lrg_gui_set_tooltip:
 * @self: an #LrgGuiContext
 * @text: tooltip text
 *
 * Shows @text near the pointer this frame, unconditionally.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_set_tooltip (LrgGuiContext *self,
                          const gchar   *text);

/* ==========================================================================
 * Menus and popups
 * ========================================================================== */

/**
 * lrg_gui_begin_menu_bar:
 * @self: an #LrgGuiContext
 * @rect: the bar's rectangle
 *
 * Opens a horizontal menu bar region.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_begin_menu_bar (LrgGuiContext    *self,
                             const LrgGuiRect *rect);

/**
 * lrg_gui_end_menu_bar:
 * @self: an #LrgGuiContext
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_end_menu_bar (LrgGuiContext *self);

/**
 * lrg_gui_begin_menu:
 * @self: an #LrgGuiContext
 * @label: menu title
 * @enabled: whether it can open
 *
 * A menu in a menu bar, or a submenu inside a menu. Call
 * lrg_gui_end_menu() when it returns %TRUE.
 *
 * Returns: %TRUE while open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_begin_menu (LrgGuiContext *self,
                             const gchar   *label,
                             gboolean       enabled);

/**
 * lrg_gui_end_menu:
 * @self: an #LrgGuiContext
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_end_menu (LrgGuiContext *self);

/**
 * lrg_gui_menu_item:
 * @self: an #LrgGuiContext
 * @label: item text
 * @shortcut: (nullable): shortcut hint drawn right-aligned
 * @checked: draw a check mark
 * @enabled: whether it can be activated
 *
 * Returns: %TRUE when activated (the menu then closes)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_menu_item (LrgGuiContext *self,
                            const gchar   *label,
                            const gchar   *shortcut,
                            gboolean       checked,
                            gboolean       enabled);

/**
 * lrg_gui_open_popup:
 * @self: an #LrgGuiContext
 * @str_id: popup identity (in the current ID scope)
 *
 * Opens a popup at the pointer; build it with lrg_gui_begin_popup().
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_open_popup (LrgGuiContext *self,
                         const gchar   *str_id);

/**
 * lrg_gui_is_popup_open:
 * @self: an #LrgGuiContext
 * @str_id: popup identity
 *
 * Returns: %TRUE if the popup is open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_is_popup_open (LrgGuiContext *self,
                                const gchar   *str_id);

/**
 * lrg_gui_begin_popup:
 * @self: an #LrgGuiContext
 * @str_id: popup identity
 *
 * Returns: %TRUE if open; then build it and call lrg_gui_end_popup()
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_begin_popup (LrgGuiContext *self,
                              const gchar   *str_id);

/**
 * lrg_gui_begin_popup_context_item:
 * @self: an #LrgGuiContext
 * @str_id: (nullable): popup identity; %NULL derives it from the last item
 *
 * Opens on right-click over the last item.
 *
 * Returns: %TRUE if open (call lrg_gui_end_popup())
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_begin_popup_context_item (LrgGuiContext *self,
                                           const gchar   *str_id);

/**
 * lrg_gui_begin_popup_context_region:
 * @self: an #LrgGuiContext
 * @str_id: popup identity
 *
 * Opens on right-click over empty space of the current region.
 *
 * Returns: %TRUE if open (call lrg_gui_end_popup())
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_begin_popup_context_region (LrgGuiContext *self,
                                             const gchar   *str_id);

/**
 * lrg_gui_begin_popup_modal:
 * @self: an #LrgGuiContext
 * @title: dialog title (identity)
 * @open: (inout) (nullable): set to %FALSE when the user closes it; when
 *   non-%NULL a close button is shown
 *
 * A centred dialog that blocks everything behind it. Open it with
 * lrg_gui_open_popup (title).
 *
 * Returns: %TRUE if open (call lrg_gui_end_popup())
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_begin_popup_modal (LrgGuiContext *self,
                                    const gchar   *title,
                                    gboolean      *open);

/**
 * lrg_gui_end_popup:
 * @self: an #LrgGuiContext
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_end_popup (LrgGuiContext *self);

/**
 * lrg_gui_close_current_popup:
 * @self: an #LrgGuiContext
 *
 * Closes the popup being built (and its children).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_close_current_popup (LrgGuiContext *self);

/**
 * lrg_gui_close_all_popups:
 * @self: an #LrgGuiContext
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_close_all_popups (LrgGuiContext *self);

/**
 * lrg_gui_set_next_popup_size:
 * @self: an #LrgGuiContext
 * @width: width (0 = fit content)
 * @height: height (0 = fit content)
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_set_next_popup_size (LrgGuiContext *self,
                                  gfloat         width,
                                  gfloat         height);

/**
 * lrg_gui_set_next_popup_pos:
 * @self: an #LrgGuiContext
 * @x: left
 * @y: top
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_set_next_popup_pos (LrgGuiContext *self,
                                 gfloat         x,
                                 gfloat         y);

/* ==========================================================================
 * Tabs
 * ========================================================================== */

/**
 * lrg_gui_begin_tab_bar:
 * @self: an #LrgGuiContext
 * @str_id: identity
 *
 * Returns: %TRUE (always; call lrg_gui_end_tab_bar())
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_begin_tab_bar (LrgGuiContext *self,
                                const gchar   *str_id);

/**
 * lrg_gui_tab_item:
 * @self: an #LrgGuiContext
 * @label: tab label
 * @open: (inout) (nullable): shows a close button when non-%NULL
 *
 * Returns: %TRUE if this tab is the selected one (draw its content)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_tab_item (LrgGuiContext *self,
                           const gchar   *label,
                           gboolean      *open);

/**
 * lrg_gui_end_tab_bar:
 * @self: an #LrgGuiContext
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_end_tab_bar (LrgGuiContext *self);

/* ==========================================================================
 * Drag and drop
 * ========================================================================== */

/**
 * lrg_gui_begin_drag_source:
 * @self: an #LrgGuiContext
 *
 * Returns: %TRUE while the last item is being dragged; then call
 *   lrg_gui_set_drag_payload() and lrg_gui_end_drag_source()
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_begin_drag_source (LrgGuiContext *self);

/**
 * lrg_gui_set_drag_payload:
 * @self: an #LrgGuiContext
 * @type: payload type, e.g. "node" or "asset"
 * @data: (nullable): payload string (copied), e.g. a GUID or path
 * @preview: (nullable): text shown next to the pointer
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_set_drag_payload (LrgGuiContext *self,
                               const gchar   *type,
                               const gchar   *data,
                               const gchar   *preview);

/**
 * lrg_gui_end_drag_source:
 * @self: an #LrgGuiContext
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_end_drag_source (LrgGuiContext *self);

/**
 * lrg_gui_accept_drop:
 * @self: an #LrgGuiContext
 * @type: payload type to accept
 *
 * Call right after the target widget. Highlights it while a matching
 * payload hovers.
 *
 * Returns: (transfer none) (nullable): the payload string on the frame it
 *   was dropped on the last item, otherwise %NULL; valid until the next
 *   frame begins
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_gui_accept_drop (LrgGuiContext *self,
                                   const gchar   *type);

/**
 * lrg_gui_get_drag_payload:
 * @self: an #LrgGuiContext
 * @type: (out) (optional) (transfer none): the payload type
 *
 * Returns: (transfer none) (nullable): the payload being dragged, if any
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_gui_get_drag_payload (LrgGuiContext  *self,
                                        const gchar   **type);

/**
 * lrg_gui_was_payload_dropped:
 * @self: an #LrgGuiContext
 *
 * Returns: %TRUE on the frame a payload was released (anywhere)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_was_payload_dropped (LrgGuiContext *self);

/* ==========================================================================
 * Raw input
 * ========================================================================== */

/**
 * lrg_gui_is_key_pressed:
 * @self: an #LrgGuiContext
 * @key: a key
 * @repeat: count auto-repeats
 *
 * Returns: %TRUE if @key was pressed this frame
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_is_key_pressed (LrgGuiContext *self,
                                 LrgGuiKey      key,
                                 gboolean       repeat);

/**
 * lrg_gui_is_key_down:
 * @self: an #LrgGuiContext
 * @key: a key
 *
 * Returns: %TRUE while @key is held
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_is_key_down (LrgGuiContext *self,
                              LrgGuiKey      key);

/**
 * lrg_gui_shortcut:
 * @self: an #LrgGuiContext
 * @mods: exact modifiers required
 * @key: the key
 *
 * Returns: %TRUE if the chord was pressed this frame and no text field has
 *   focus (Ctrl chords except A/C/V/X/Z/Y still fire while typing)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_shortcut (LrgGuiContext   *self,
                           LrgGuiModifiers  mods,
                           LrgGuiKey        key);

/**
 * lrg_gui_get_mods:
 * @self: an #LrgGuiContext
 *
 * Returns: the held modifiers
 */
LRG_AVAILABLE_IN_ALL
LrgGuiModifiers lrg_gui_get_mods (LrgGuiContext *self);

/**
 * lrg_gui_is_mouse_clicked:
 * @self: an #LrgGuiContext
 * @button: the button
 *
 * Returns: %TRUE if @button went down this frame
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_is_mouse_clicked (LrgGuiContext     *self,
                                   LrgGuiMouseButton  button);

/**
 * lrg_gui_is_mouse_released:
 * @self: an #LrgGuiContext
 * @button: the button
 *
 * Returns: %TRUE if @button went up this frame
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_is_mouse_released (LrgGuiContext     *self,
                                    LrgGuiMouseButton  button);

/**
 * lrg_gui_is_mouse_down:
 * @self: an #LrgGuiContext
 * @button: the button
 *
 * Returns: %TRUE while @button is held
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_is_mouse_down (LrgGuiContext     *self,
                                LrgGuiMouseButton  button);

/**
 * lrg_gui_is_mouse_double_clicked:
 * @self: an #LrgGuiContext
 * @button: the button
 *
 * Returns: %TRUE on the second click of a double-click
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_is_mouse_double_clicked (LrgGuiContext     *self,
                                          LrgGuiMouseButton  button);

/**
 * lrg_gui_get_mouse:
 * @self: an #LrgGuiContext
 * @x: (out) (optional): pointer x
 * @y: (out) (optional): pointer y
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_get_mouse (LrgGuiContext *self,
                        gfloat        *x,
                        gfloat        *y);

/**
 * lrg_gui_get_mouse_delta:
 * @self: an #LrgGuiContext
 * @dx: (out) (optional): x motion since the previous frame
 * @dy: (out) (optional): y motion
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_get_mouse_delta (LrgGuiContext *self,
                              gfloat        *dx,
                              gfloat        *dy);

/**
 * lrg_gui_get_mouse_wheel:
 * @self: an #LrgGuiContext
 *
 * Returns: the vertical wheel delta not consumed by a scrolling region
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_gui_get_mouse_wheel (LrgGuiContext *self);

/**
 * lrg_gui_get_delta_time:
 * @self: an #LrgGuiContext
 *
 * Returns: seconds since the previous frame
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_gui_get_delta_time (LrgGuiContext *self);

/**
 * lrg_gui_set_cursor_shape:
 * @self: an #LrgGuiContext
 * @cursor: the pointer shape to request this frame
 */
LRG_AVAILABLE_IN_ALL
void lrg_gui_set_cursor_shape (LrgGuiContext *self,
                               LrgGuiCursor   cursor);

/**
 * lrg_gui_is_rect_hovered:
 * @self: an #LrgGuiContext
 * @rect: a rectangle
 *
 * Returns: %TRUE if the pointer is inside @rect, the current clip and not
 *   blocked by a popup
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_is_rect_hovered (LrgGuiContext    *self,
                                  const LrgGuiRect *rect);

/**
 * lrg_gui_splitter:
 * @self: an #LrgGuiContext
 * @str_id: identity
 * @rect: the splitter handle rectangle
 * @vertical: %TRUE for a vertical bar dragged horizontally
 * @delta: (out): pixels dragged this frame along the drag axis
 *
 * A draggable divider (dock splits).
 *
 * Returns: %TRUE while being dragged
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gui_splitter (LrgGuiContext    *self,
                           const gchar      *str_id,
                           const LrgGuiRect *rect,
                           gboolean          vertical,
                           gfloat           *delta);

G_END_DECLS
