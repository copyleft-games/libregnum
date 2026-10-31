/* lrg-gui-private.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internals shared by the GUI toolkit's translation units
 * (lrg-gui-context.c, lrg-gui-widgets.c, lrg-gui-dock.c).
 */

#pragma once

#include "lrg-gui-context.h"

G_BEGIN_DECLS

/* Draw layers, replayed in this order. */
typedef enum
{
	GUI_LAYER_BASE,
	GUI_LAYER_POPUP,
	GUI_LAYER_OVERLAY,   /* tooltips, drag preview */
	GUI_N_LAYERS
} GuiLayer;

/* Behaviour flags for gui_button_behavior(). */
typedef enum
{
	GUI_BUTTON_ON_RELEASE  = 0,        /* click = release over the item */
	GUI_BUTTON_ON_PRESS    = 1 << 0,   /* click = press (rows, tabs) */
	GUI_BUTTON_RIGHT       = 1 << 1,   /* also accept right button */
	GUI_BUTTON_MIDDLE      = 1 << 2,   /* also accept middle button */
	GUI_BUTTON_NO_ACTIVATE = 1 << 3    /* hover only (menu headers) */
} GuiButtonFlags;

typedef struct
{
	LrgGuiId           id;
	LrgGuiRect         rect;          /* outer rectangle */
	LrgGuiRect         clip;          /* effective clip */
	LrgGuiRegionFlags  flags;
	gint               depth;         /* popup depth: 0 = base */
	GuiLayer           layer;

	gfloat             origin_x;      /* content origin (before scroll) */
	gfloat             origin_y;
	gfloat             cursor_x;      /* next item position */
	gfloat             cursor_y;
	gfloat             line_y;        /* top of the current line */
	gfloat             line_h;        /* tallest item on the current line */
	gfloat             prev_line_y;   /* line of the previous item */
	gfloat             prev_line_h;
	gfloat             prev_item_x2;  /* right edge of the previous item */
	gboolean           same_line;     /* next item continues the line */
	gfloat             indent;
	gfloat             content_w;     /* width available for items */
	gfloat             max_x;         /* extent of laid-out content */
	gfloat             max_y;
	gfloat             scroll_x;
	gfloat             scroll_y;
	gboolean           is_popup;
	gint               popup_index;   /* index in the popup stack, or -1 */
	guint              bg_cmd;        /* deferred background command */
	gboolean           has_bg_cmd;
} GuiRegion;

typedef struct
{
	LrgGuiId   id;
	gfloat     x;
	gfloat     y;
	gfloat     w;             /* measured last frame */
	gfloat     h;
	gfloat     req_w;         /* requested size (0 = fit) */
	gfloat     req_h;
	guint64    open_frame;
	gboolean   modal;
	gboolean   menu;
	gboolean   visited;       /* built this frame */
	LrgGuiRect rect;          /* last drawn rectangle */
	LrgGuiId   source_id;     /* item that opened it */
} GuiPopup;

/* Per-identity persistent state. */
typedef struct
{
	gboolean open;
	gboolean initialized;
	gfloat   f0;      /* scroll x / misc */
	gfloat   f1;      /* scroll y / misc */
	gfloat   f2;      /* content width */
	gfloat   f3;      /* content height */
	LrgGuiId selected;   /* tab bars: selected tab */
	LrgGuiId next_selected;
} GuiState;

typedef struct
{
	LrgGuiId   id;
	LrgGuiRect rect;
	gboolean   hovered;
	gboolean   active;
	gboolean   activated;
	gboolean   deactivated;
	gboolean   clicked[LRG_GUI_MOUSE_BUTTON_COUNT];
	gboolean   double_clicked;
	gboolean   valid;
} GuiItem;

typedef struct
{
	LrgGuiId id;
	gfloat   x;          /* next tab position */
	gfloat   y;
	gfloat   h;
	gfloat   x0;
	LrgGuiId seen_selected;
	gboolean any_seen;
	LrgGuiId first_tab;
} GuiTabBar;

struct _LrgGuiContext
{
	GObject             parent_instance;

	LrgGuiStyle         style;
	LrgGuiMeasureFunc   measure;
	gpointer            measure_data;
	GDestroyNotify      measure_destroy;
	LrgGuiClipboardGetFunc clip_get;
	LrgGuiClipboardSetFunc clip_set;
	gpointer            clip_data;
	GDestroyNotify      clip_destroy;
	gchar              *clipboard;

	/* Frame */
	LrgGuiInput         input;
	gfloat              width;
	gfloat              height;
	guint64             frame;
	gboolean            in_frame;
	gfloat              prev_mouse_x;
	gfloat              prev_mouse_y;
	gboolean            prev_mouse_down[LRG_GUI_MOUSE_BUTTON_COUNT];
	gboolean            mouse_clicked[LRG_GUI_MOUSE_BUTTON_COUNT];
	gboolean            mouse_released[LRG_GUI_MOUSE_BUTTON_COUNT];
	gboolean            mouse_double[LRG_GUI_MOUSE_BUTTON_COUNT];
	gdouble             last_click_time[LRG_GUI_MOUSE_BUTTON_COUNT];
	gfloat              last_click_x[LRG_GUI_MOUSE_BUTTON_COUNT];
	gfloat              last_click_y[LRG_GUI_MOUSE_BUTTON_COUNT];
	gfloat              click_x[LRG_GUI_MOUSE_BUTTON_COUNT];   /* where pressed */
	gfloat              click_y[LRG_GUI_MOUSE_BUTTON_COUNT];
	gfloat              wheel_y;           /* unconsumed wheel */
	gfloat              wheel_x;
	LrgGuiCursor        cursor;

	LrgGuiDrawList     *layers[GUI_N_LAYERS];
	LrgGuiDrawList     *output;

	/* Identity */
	GArray             *id_stack;          /* LrgGuiId */

	/* Layout */
	GArray             *regions;           /* GuiRegion */
	gfloat              next_item_width;
	gboolean            has_next_item_width;
	gint                next_item_open;    /* -1 none, 0 closed, 1 open */
	GArray             *tree_indents;      /* gfloat per open tree node */

	/* Interaction */
	LrgGuiId            hovered_id;        /* hovered this frame */
	LrgGuiId            active_id;
	LrgGuiId            active_id_prev;    /* at frame start */
	gboolean            active_seen;       /* active item submitted */
	gfloat              active_value0;     /* value at drag start */
	gfloat              active_accum;      /* sub-step drag accumulator */
	GuiItem             item;              /* last item */
	gint                mouse_top_depth;
	gint                modal_depth;
	gboolean            any_hovered;

	/* Keyboard focus / text editing */
	LrgGuiId            focus_id;
	gboolean            focus_seen;
	gboolean            focus_request;     /* set_keyboard_focus_here */
	LrgGuiId            focus_request_id;  /* request_focus (specific field) */
	gboolean            focus_next;        /* Tab pressed in a field */
	LrgGuiId            focus_from;
	GString            *edit_buf;
	GString            *edit_initial;
	gint                edit_cursor;       /* byte offset */
	gint                edit_anchor;       /* selection anchor (byte offset) */
	gfloat              edit_scroll;
	gboolean            edit_dirty;        /* modified since focus */
	gdouble             caret_time;
	LrgGuiId            drag_edit_id;      /* drag field in text mode */

	/* Popups */
	GArray             *popups;            /* GuiPopup */
	gint                popup_level;       /* popups being built */
	GHashTable         *popups_at_start;   /* ids open at frame start */
	gfloat              next_popup_w;
	gfloat              next_popup_h;
	gboolean            has_next_popup_size;
	gfloat              next_popup_x;
	gfloat              next_popup_y;
	gboolean            has_next_popup_pos;
	LrgGuiId            menubar_id;        /* bar being built */
	gboolean            in_menubar;

	/* Tabs */
	GArray             *tab_bars;          /* GuiTabBar stack */

	/* Tooltips */
	gchar              *tooltip;
	LrgGuiId            tooltip_item;
	gdouble             hover_start;
	LrgGuiId            hover_item_for_tooltip;

	/* Drag and drop */
	gboolean            dnd_active;
	LrgGuiId            dnd_source;
	gchar              *dnd_type;
	gchar              *dnd_data;
	gchar              *dnd_preview;
	gboolean            dnd_dropped;       /* released this frame */
	gboolean            dnd_accepted;
	gboolean            dnd_source_submitted;

	/* Persistent state */
	GHashTable         *state;             /* LrgGuiId -> GuiState* */

	/* Returned by gui_region() outside any region, so callers never see
	 * NULL (widgets used outside a frame then draw nowhere harmlessly). */
	GuiRegion           fallback_region;
};

/* --- helpers implemented in lrg-gui-context.c --- */

G_GNUC_INTERNAL GuiRegion *   gui_region            (LrgGuiContext *self);
G_GNUC_INTERNAL LrgGuiDrawList * gui_dl             (LrgGuiContext *self);
G_GNUC_INTERNAL LrgGuiId      gui_hash_label        (LrgGuiContext *self, const gchar *label);
G_GNUC_INTERNAL const gchar * gui_label_end         (const gchar *label);
G_GNUC_INTERNAL GuiState *    gui_state             (LrgGuiContext *self, LrgGuiId id);
G_GNUC_INTERNAL gfloat        gui_text_width        (LrgGuiContext *self, const gchar *text, gssize len, gboolean mono);
G_GNUC_INTERNAL gfloat        gui_frame_height      (LrgGuiContext *self);
G_GNUC_INTERNAL gfloat        gui_item_width        (LrgGuiContext *self, gfloat fallback);
G_GNUC_INTERNAL gboolean      gui_item_add          (LrgGuiContext *self, LrgGuiId id, gfloat w, gfloat h, LrgGuiRect *out);
G_GNUC_INTERNAL gboolean      gui_hoverable         (LrgGuiContext *self, const LrgGuiRect *rect);
G_GNUC_INTERNAL gboolean      gui_button_behavior   (LrgGuiContext *self, LrgGuiId id, const LrgGuiRect *rect,
                                     GuiButtonFlags flags, gboolean *out_hovered, gboolean *out_held);
G_GNUC_INTERNAL void          gui_set_active        (LrgGuiContext *self, LrgGuiId id);
G_GNUC_INTERNAL void          gui_clear_active      (LrgGuiContext *self);
G_GNUC_INTERNAL void          gui_finish_item       (LrgGuiContext *self, LrgGuiId id, const LrgGuiRect *rect);
G_GNUC_INTERNAL void          gui_draw_text         (LrgGuiContext *self, gfloat x, gfloat y, const gchar *text,
                                     gssize len, LrgGuiColor color, gboolean mono);
G_GNUC_INTERNAL void          gui_draw_text_clipped (LrgGuiContext *self, const LrgGuiRect *rect, const gchar *text,
                                     gssize len, LrgGuiColor color, gfloat align_x, gboolean mono);
G_GNUC_INTERNAL void          gui_draw_frame        (LrgGuiContext *self, const LrgGuiRect *rect, LrgGuiColor fill,
                                     gboolean border);
G_GNUC_INTERNAL void          gui_draw_arrow        (LrgGuiContext *self, gfloat x, gfloat y, gfloat size,
                                     gboolean down, LrgGuiColor color);
G_GNUC_INTERNAL void          gui_draw_check        (LrgGuiContext *self, const LrgGuiRect *rect, LrgGuiColor color);
G_GNUC_INTERNAL LrgGuiColor   gui_col               (LrgGuiContext *self, LrgGuiStyleColor idx);
G_GNUC_INTERNAL gboolean      gui_rect_contains     (const LrgGuiRect *rect, gfloat x, gfloat y);
G_GNUC_INTERNAL LrgGuiRect    gui_rect_intersect    (const LrgGuiRect *a, const LrgGuiRect *b);
G_GNUC_INTERNAL void          gui_begin_region_full (LrgGuiContext *self, LrgGuiId id, const LrgGuiRect *rect,
                                     LrgGuiRegionFlags flags, gboolean is_popup, gint popup_index,
                                     GuiLayer layer);
G_GNUC_INTERNAL gboolean      gui_begin_popup_ex    (LrgGuiContext *self, LrgGuiId id, gboolean menu);
G_GNUC_INTERNAL void          gui_open_popup_id     (LrgGuiContext *self, LrgGuiId id, gfloat x, gfloat y,
                                     gboolean modal, gboolean menu);
G_GNUC_INTERNAL gboolean      gui_is_popup_open_id  (LrgGuiContext *self, LrgGuiId id);
G_GNUC_INTERNAL gboolean      gui_in_popup          (LrgGuiContext *self);
G_GNUC_INTERNAL void          gui_close_popups_from (LrgGuiContext *self, gint level);
G_GNUC_INTERNAL gboolean      gui_input_text_ex     (LrgGuiContext *self, LrgGuiId id, const LrgGuiRect *rect,
                                     const gchar *hint, GString *buffer,
                                     LrgGuiInputTextFlags flags, gboolean *committed);
G_GNUC_INTERNAL void          gui_focus_text        (LrgGuiContext *self, LrgGuiId id, const gchar *initial,
                                     gboolean select_all);
G_GNUC_INTERNAL void          gui_format_value      (gchar *buf, gsize size, const gchar *format,
                                     gdouble value);
G_GNUC_INTERNAL gboolean      gui_key_pressed       (LrgGuiContext *self, LrgGuiKey key, gboolean repeat,
                                     LrgGuiModifiers *mods);

/* --- implemented in lrg-gui-draw-list.c --- */

G_GNUC_INTERNAL void          lrg_gui_draw_list_steal_append (LrgGuiDrawList *self, LrgGuiDrawList *other);

G_END_DECLS
