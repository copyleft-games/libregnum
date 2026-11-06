/* ui-dnd.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Drag and drop with the retained UI toolkit: LrgDragPayload,
 * LrgDragSource and LrgDropTarget on a handful of LrgCustomWidgets.
 *
 *   - Two lists ("Backlog" and "Done"): drag rows to reorder them or to
 *     move them across. The list paints its own insertion line from
 *     LrgDropTarget:current-drop (paint-highlight is off).
 *   - A trash can: drop to delete. Hold Ctrl to drop a COPY (the
 *     original stays); its label shows which action a drop would take.
 *   - A 4 x 3 grid of inventory slots: drag items between slots (an
 *     occupied slot swaps), drop list rows into empty slots, drag items
 *     out of slots back into a list. A slot refuses what it cannot take
 *     (motion returns NONE) and paints red while refusing.
 *
 * Modifiers while dragging: Ctrl copies, Shift moves, Alt (or
 * Ctrl+Shift) links. Escape cancels a drag.
 *
 * Run with --help for options (theme, right-to-left, screenshots and a
 * scripted demo drag for headless captures).
 */

#include <libregnum.h>
#include <graylib.h>
#include <raylib.h>
#include <math.h>
#include <string.h>

#define WINDOW_WIDTH   (1100)
#define WINDOW_HEIGHT  (640)
#define N_SLOTS        (12)
#define SLOT_COLUMNS   (4)
#define SLOT_SIZE      (84.0f)
#define ITEM_TYPE      "application/x-demo-item"

static const gchar *license_text =
	"ui-dnd - libregnum drag and drop example\n"
	"Copyright (C) 2026  Zach Podbielniak\n\n"
	"This program is free software: you can redistribute it and/or modify\n"
	"it under the terms of the GNU Affero General Public License as published by\n"
	"the Free Software Foundation, either version 3 of the License, or\n"
	"(at your option) any later version.\n\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU Affero General Public License for more details.\n\n"
	"You should have received a copy of the GNU Affero General Public License\n"
	"along with this program.  If not, see <https://www.gnu.org/licenses/>.\n";

typedef struct _App App;

/* One list of strings painted by a custom widget */
typedef struct
{
	App             *app;
	const gchar     *title;
	GPtrArray       *items;        /* gchar*, owned */
	LrgCustomWidget *widget;       /* borrowed: the box owns it */
	LrgDropTarget   *target;       /* borrowed: the widget owns it */
	guint            insert_at;    /* insertion index while a drag hovers */
	gint             dragging;     /* row being dragged out, -1 for none */
} ItemList;

/* One inventory slot */
typedef struct
{
	App             *app;
	guint            index;
	gchar           *item;         /* owned, NULL when empty */
	LrgCustomWidget *widget;
	LrgDropTarget   *target;
	gboolean         dragging;     /* its item is being dragged out */
} Slot;

/*
 * What travels in the payload (a pointer payload, freed with it): where
 * the item comes from, so the source can remove it after a MOVE, and
 * whether the target already finished the move itself (a reorder in
 * one list, a swap between slots).
 */
typedef struct
{
	ItemList *from_list;
	Slot     *from_slot;
	guint     index;
	gchar    *text;
	gboolean  handled;
} DragInfo;

struct _App
{
	LrgCanvas  *canvas;
	LrgVBox    *root;
	ItemList    lists[2];
	Slot        slots[N_SLOTS];
	LrgPanel   *trash;
	LrgLabel   *trash_label;
	LrgLabel   *status;
};

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void
drag_info_free (gpointer data)
{
	DragInfo *info = data;

	g_free (info->text);
	g_free (info);
}

/* status: one line at the bottom describing the last thing that happened */
static void
status (App         *app,
        const gchar *format,
        ...) G_GNUC_PRINTF (2, 3);

static void
status (App         *app,
        const gchar *format,
        ...)
{
	g_autofree gchar *text = NULL;
	va_list           args;

	va_start (args, format);
	text = g_strdup_vprintf (format, args);
	va_end (args);
	lrg_label_set_text (app->status, text);
}

/* info_of: the DragInfo of one of our payloads */
static DragInfo *
info_of (LrgDragPayload *payload)
{
	return lrg_drag_payload_get_pointer (payload);
}

/* make_payload: a MOVE/COPY payload carrying @text from a list or slot */
static LrgDragPayload *
make_payload (ItemList    *list,
              Slot        *slot,
              guint        index,
              const gchar *text)
{
	DragInfo       *info = g_new0 (DragInfo, 1);
	LrgDragPayload *payload;

	info->from_list = list;
	info->from_slot = slot;
	info->index = index;
	info->text = g_strdup (text);
	payload = lrg_drag_payload_new_pointer (ITEM_TYPE, info, drag_info_free,
	                                        LRG_DRAG_ACTION_COPY | LRG_DRAG_ACTION_MOVE);
	lrg_drag_payload_set_label (payload, text);
	lrg_drag_payload_set_icon (payload, slot != NULL ? LRG_ICON_STAR : LRG_ICON_FILE);
	return payload;
}

/* paint_label: text at a position in the theme's UI font */
static void
paint_label (LrgPaintList   *list,
             LrgTheme       *theme,
             const gchar    *text,
             gfloat          x,
             gfloat          y,
             gfloat          size,
             const GrlColor *color)
{
	lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), text, x, y, size, 1.0f, color);
}

/* ==========================================================================
 * Lists
 * ========================================================================== */

static gfloat
list_header (LrgTheme *theme)
{
	return lrg_theme_get_metric (theme, LRG_THEME_METRIC_ROW_HEIGHT) + 8.0f;
}

static gfloat
list_row (LrgTheme *theme)
{
	return lrg_theme_get_metric (theme, LRG_THEME_METRIC_ROW_HEIGHT);
}

/* list_row_at: the row under canvas Y, or -1 */
static gint
list_row_at (ItemList *self,
             gfloat    y)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self->widget));
	gfloat    top = lrg_widget_get_world_y (LRG_WIDGET (self->widget)) + list_header (theme);
	gint      row;

	if (y < top)
		return -1;
	row = (gint) ((y - top) / list_row (theme));
	return row < (gint) self->items->len ? row : -1;
}

/* list_insert_at: the gap nearest canvas Y, 0 .. n */
static guint
list_insert_at (ItemList *self,
                gfloat    y)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self->widget));
	gfloat    top = lrg_widget_get_world_y (LRG_WIDGET (self->widget)) + list_header (theme);
	gfloat    gap = floorf ((y - top) / list_row (theme) + 0.5f);

	return (guint) CLAMP (gap, 0.0f, (gfloat) self->items->len);
}

static void
list_measure (LrgCustomWidget     *widget,
              gfloat               avail_width,
              gfloat               avail_height,
              LrgCustomWidgetSize *size,
              gpointer             user_data)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (widget));

	size->width = 220.0f;
	size->height = list_header (theme) + list_row (theme) * 8.0f;
}

/*
 * list_paint: a card with a title, the rows, the row being dragged out
 * dimmed and, while an accepted drag hovers, an accent insertion line
 * at the gap the drop would land in.
 */
static void
list_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	ItemList       *self = user_data;
	LrgTheme       *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat          radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	gfloat          pad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);
	gfloat          font = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat          header = list_header (theme);
	gfloat          row_h = list_row (theme);
	LrgDragPayload *hover = lrg_drop_target_get_current_drop (self->target);
	gboolean        active = hover != NULL &&
	                         lrg_drop_target_get_current_action (self->target) != LRG_DRAG_ACTION_NONE;
	g_autofree gchar *title = g_strdup_printf ("%s (%u)", self->title, self->items->len);
	guint           i;

	/* Card: accent border while it would take the drop */
	lrg_paint_list_rect (list, x, y, width, height, radius,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	lrg_paint_list_border (list, x, y, width, height, radius, active ? 2.0f : 1.0f,
	                       lrg_theme_get_color (theme, active ? LRG_THEME_COLOR_ACCENT
	                                                          : LRG_THEME_COLOR_BORDER));
	paint_label (list, theme, title, x + pad, y + (header - font) * 0.5f, font,
	             lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));

	/* Rows */
	for (i = 0; i < self->items->len; i++)
	{
		gfloat   ry = y + header + row_h * (gfloat) i;
		GrlColor row;

		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL),
		                            (gint) i == self->dragging ? 0.35f : 1.0f, &row);
		lrg_paint_list_rect (list, x + 4.0f, ry + 2.0f, width - 8.0f, row_h - 4.0f, radius * 0.6f, &row);
		paint_label (list, theme, g_ptr_array_index (self->items, i), x + pad,
		             ry + (row_h - font * 0.9f) * 0.5f, font * 0.9f,
		             lrg_theme_get_color (theme, (gint) i == self->dragging ? LRG_THEME_COLOR_TEXT_MUTED
		                                                                    : LRG_THEME_COLOR_TEXT));
	}

	/* Insertion line */
	if (active)
	{
		gfloat ly = y + header + row_h * (gfloat) self->insert_at - 1.5f;

		lrg_paint_list_rect (list, x + 6.0f, ly, width - 12.0f, 3.0f, 1.5f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_paint_list_circle (list, x + 6.0f, ly + 1.5f, 4.0f,
		                       lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
	}
}

/* list_prepare: the row under the press, or no drag over the header */
static LrgDragPayload *
list_prepare (LrgDragSource *source,
              gfloat         x,
              gfloat         y,
              ItemList      *self)
{
	gint row = list_row_at (self, y);

	if (row < 0)
		return NULL;
	return make_payload (self, NULL, (guint) row, g_ptr_array_index (self->items, row));
}

static void
list_drag_begin (LrgDragSource  *source,
                 LrgDragPayload *payload,
                 ItemList       *self)
{
	self->dragging = (gint) info_of (payload)->index;
	status (self->app, "Dragging \"%s\" from %s", info_of (payload)->text, self->title);
}

/* list_drag_end: after a MOVE the row leaves this list, unless the
 * target already moved it (a reorder within this list) */
static void
list_drag_end (LrgDragSource  *source,
               LrgDragPayload *payload,
               gboolean        dropped,
               LrgDragAction   action,
               ItemList       *self)
{
	DragInfo *info = info_of (payload);

	self->dragging = -1;
	if (dropped && action == LRG_DRAG_ACTION_MOVE && !info->handled && info->index < self->items->len)
		g_ptr_array_remove_index (self->items, info->index);
	lrg_widget_queue_layout (LRG_WIDGET (self->widget));
	if (!dropped)
		status (self->app, "Drag of \"%s\" cancelled", info->text);
}

/* list_motion: remember the gap for the insertion line */
static LrgDragAction
list_motion (LrgDropTarget  *target,
             LrgDragPayload *payload,
             gfloat          x,
             gfloat          y,
             ItemList       *self)
{
	self->insert_at = list_insert_at (self, y);
	return lrg_drop_target_get_proposed_action (target);
}

/* list_drop: insert at the gap; a MOVE within the list is a reorder */
static gboolean
list_drop (LrgDropTarget  *target,
           LrgDragPayload *payload,
           gfloat          x,
           gfloat          y,
           ItemList       *self)
{
	DragInfo      *info = info_of (payload);
	LrgDragAction  action = lrg_drop_target_get_current_action (target);
	guint          at = list_insert_at (self, y);

	if (info->from_list == self && action == LRG_DRAG_ACTION_MOVE)
	{
		gchar *item = g_ptr_array_steal_index (self->items, info->index);

		if (at > info->index)
			at--;
		g_ptr_array_insert (self->items, (gint) at, item);
		info->handled = TRUE;
		status (self->app, "Reordered \"%s\" in %s", info->text, self->title);
	}
	else
	{
		g_ptr_array_insert (self->items, (gint) at, g_strdup (info->text));
		status (self->app, "%s \"%s\" into %s", action == LRG_DRAG_ACTION_COPY ? "Copied" : "Moved",
		        info->text, self->title);
	}
	lrg_widget_queue_layout (LRG_WIDGET (self->widget));
	return TRUE;
}

static LrgCustomWidget *
list_new (App         *app,
          ItemList    *self,
          const gchar *title,
          const gchar *const *items)
{
	static const LrgCustomWidgetFuncs funcs = { list_measure, list_paint, NULL, NULL, { NULL } };
	static const gchar *types[] = { ITEM_TYPE, NULL };
	LrgDragSource      *source;
	guint               i;

	self->app = app;
	self->title = title;
	self->dragging = -1;
	self->items = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; items[i] != NULL; i++)
		g_ptr_array_add (self->items, g_strdup (items[i]));

	self->widget = lrg_custom_widget_new ();
	lrg_custom_widget_set_funcs (self->widget, &funcs, self, NULL);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->widget), 1.0f);

	/* Source: the row under the press */
	source = lrg_drag_source_new ();
	g_signal_connect (source, "prepare", G_CALLBACK (list_prepare), self);
	g_signal_connect (source, "drag-begin", G_CALLBACK (list_drag_begin), self);
	g_signal_connect (source, "drag-end", G_CALLBACK (list_drag_end), self);
	lrg_widget_add_drag_source (LRG_WIDGET (self->widget), source);

	/* Target: our items, painting its own feedback */
	self->target = lrg_drop_target_new (types, LRG_DRAG_ACTION_COPY | LRG_DRAG_ACTION_MOVE);
	lrg_drop_target_set_paint_highlight (self->target, FALSE);
	g_signal_connect (self->target, "motion", G_CALLBACK (list_motion), self);
	g_signal_connect (self->target, "drop", G_CALLBACK (list_drop), self);
	lrg_widget_add_drop_target (LRG_WIDGET (self->widget), self->target);
	return self->widget;
}

/* ==========================================================================
 * Trash
 * ========================================================================== */

/* trash_motion: say what a drop would do */
static LrgDragAction
trash_motion (LrgDropTarget  *target,
              LrgDragPayload *payload,
              gfloat          x,
              gfloat          y,
              App            *app)
{
	LrgDragAction action = lrg_drop_target_get_proposed_action (target);

	lrg_label_set_text (app->trash_label, action == LRG_DRAG_ACTION_COPY
	                    ? "Copy: the original stays"
	                    : "Release to delete");
	return action;
}

static void
trash_leave (LrgDropTarget *target,
             App           *app)
{
	lrg_label_set_text (app->trash_label, "Trash");
}

/* trash_drop: nothing to store; the source removes its item on MOVE */
static gboolean
trash_drop (LrgDropTarget  *target,
            LrgDragPayload *payload,
            gfloat          x,
            gfloat          y,
            App            *app)
{
	if (lrg_drop_target_get_current_action (target) == LRG_DRAG_ACTION_MOVE)
		status (app, "Deleted \"%s\"", info_of (payload)->text);
	else
		status (app, "Trashed a copy of \"%s\" (the original stays)", info_of (payload)->text);
	return TRUE;
}

/* ==========================================================================
 * Inventory slots
 * ========================================================================== */

static void
slot_measure (LrgCustomWidget     *widget,
              gfloat               avail_width,
              gfloat               avail_height,
              LrgCustomWidgetSize *size,
              gpointer             user_data)
{
	size->width = SLOT_SIZE;
	size->height = SLOT_SIZE;
}

/*
 * slot_paint: a rounded square with the item's name; while a drag
 * hovers it shows an accent ring when it would take the drop and a
 * danger ring when it refuses (an occupied slot and a list item).
 */
static void
slot_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	Slot           *self = user_data;
	LrgTheme       *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat          radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS) * 1.5f;
	gfloat          font = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	LrgDragPayload *hover = lrg_drop_target_get_current_drop (self->target);
	LrgThemeColor   ring = LRG_THEME_COLOR_BORDER;

	if (hover != NULL)
		ring = lrg_drop_target_get_current_action (self->target) != LRG_DRAG_ACTION_NONE
		       ? LRG_THEME_COLOR_ACCENT : LRG_THEME_COLOR_DANGER;

	lrg_paint_list_rect (list, x, y, width, height, radius,
	                     lrg_theme_get_color (theme, self->item != NULL && !self->dragging
	                                                 ? LRG_THEME_COLOR_CONTROL : LRG_THEME_COLOR_INPUT));
	lrg_paint_list_border (list, x, y, width, height, radius, hover != NULL ? 2.5f : 1.0f,
	                       lrg_theme_get_color (theme, ring));

	if (self->item != NULL)
	{
		gfloat tw = 0.0f;
		gfloat th = 0.0f;
		gfloat icon = width * 0.34f;

		lrg_paint_list_icon (list, LRG_ICON_STAR, x + (width - icon) * 0.5f, y + height * 0.16f, icon, 0.0f,
		                     lrg_theme_get_color (theme, self->dragging ? LRG_THEME_COLOR_TEXT_MUTED
		                                                                : LRG_THEME_COLOR_ACCENT));
		lrg_paint_measure_text (lrg_theme_get_ui_font (theme), self->item, font, 1.0f, &tw, &th);
		paint_label (list, theme, self->item, x + (width - tw) * 0.5f, y + height * 0.62f, font,
		             lrg_theme_get_color (theme, self->dragging ? LRG_THEME_COLOR_TEXT_MUTED
		                                                        : LRG_THEME_COLOR_TEXT));
	}
}

/* slot_prepare: only a filled slot can be dragged */
static LrgDragPayload *
slot_prepare (LrgDragSource *source,
              gfloat         x,
              gfloat         y,
              Slot          *self)
{
	if (self->item == NULL)
		return NULL;
	return make_payload (NULL, self, self->index, self->item);
}

static void
slot_drag_begin (LrgDragSource  *source,
                 LrgDragPayload *payload,
                 Slot           *self)
{
	self->dragging = TRUE;
	status (self->app, "Dragging \"%s\" from slot %u", self->item, self->index + 1);
}

/* slot_drag_end: a MOVE empties the slot unless a swap refilled it */
static void
slot_drag_end (LrgDragSource  *source,
               LrgDragPayload *payload,
               gboolean        dropped,
               LrgDragAction   action,
               Slot           *self)
{
	self->dragging = FALSE;
	if (dropped && action == LRG_DRAG_ACTION_MOVE && !info_of (payload)->handled)
		g_clear_pointer (&self->item, g_free);
	if (!dropped)
		status (self->app, "Drag of \"%s\" cancelled", info_of (payload)->text);
}

/*
 * slot_motion: an empty slot takes anything; an occupied one only swaps
 * with another slot (MOVE). Everything else is refused right here, so
 * the chip and the slot show "no" before the button is released.
 */
static LrgDragAction
slot_motion (LrgDropTarget  *target,
             LrgDragPayload *payload,
             gfloat          x,
             gfloat          y,
             Slot           *self)
{
	DragInfo      *info = info_of (payload);
	LrgDragAction  proposed = lrg_drop_target_get_proposed_action (target);

	if (info->from_slot == self)
		return LRG_DRAG_ACTION_NONE;
	if (self->item == NULL)
		return proposed;
	if (info->from_slot != NULL && proposed == LRG_DRAG_ACTION_MOVE)
		return LRG_DRAG_ACTION_MOVE;
	return LRG_DRAG_ACTION_NONE;
}

static gboolean
slot_drop (LrgDropTarget  *target,
           LrgDragPayload *payload,
           gfloat          x,
           gfloat          y,
           Slot           *self)
{
	DragInfo *info = info_of (payload);

	if (self->item != NULL && info->from_slot != NULL)
	{
		/* Swap: the other slot gets our item, the source must not clear it */
		gchar *mine = g_steal_pointer (&self->item);

		g_free (info->from_slot->item);
		info->from_slot->item = mine;
		info->handled = TRUE;
		self->item = g_strdup (info->text);
		status (self->app, "Swapped slots %u and %u", info->from_slot->index + 1, self->index + 1);
		return TRUE;
	}

	g_free (self->item);
	self->item = g_strdup (info->text);
	status (self->app, "%s \"%s\" into slot %u",
	        lrg_drop_target_get_current_action (target) == LRG_DRAG_ACTION_COPY ? "Copied" : "Moved",
	        info->text, self->index + 1);
	return TRUE;
}

static LrgCustomWidget *
slot_new (App         *app,
          Slot        *self,
          guint        index,
          const gchar *item)
{
	static const LrgCustomWidgetFuncs funcs = { slot_measure, slot_paint, NULL, NULL, { NULL } };
	static const gchar *types[] = { ITEM_TYPE, NULL };
	LrgDragSource      *source;

	self->app = app;
	self->index = index;
	self->item = g_strdup (item);
	self->widget = lrg_custom_widget_new ();
	lrg_custom_widget_set_funcs (self->widget, &funcs, self, NULL);

	source = lrg_drag_source_new ();
	g_signal_connect (source, "prepare", G_CALLBACK (slot_prepare), self);
	g_signal_connect (source, "drag-begin", G_CALLBACK (slot_drag_begin), self);
	g_signal_connect (source, "drag-end", G_CALLBACK (slot_drag_end), self);
	lrg_widget_add_drag_source (LRG_WIDGET (self->widget), source);

	self->target = lrg_drop_target_new (types, LRG_DRAG_ACTION_COPY | LRG_DRAG_ACTION_MOVE);
	lrg_drop_target_set_paint_highlight (self->target, FALSE);
	g_signal_connect (self->target, "motion", G_CALLBACK (slot_motion), self);
	g_signal_connect (self->target, "drop", G_CALLBACK (slot_drop), self);
	lrg_widget_add_drop_target (LRG_WIDGET (self->widget), self->target);
	return self->widget;
}

/* ==========================================================================
 * Building the screen
 * ========================================================================== */

/* titled_column: a heading above @child */
static LrgVBox *
titled_column (const gchar *title,
               LrgWidget   *child)
{
	LrgVBox             *column = lrg_vbox_new ();
	g_autoptr(LrgLabel)  label = lrg_label_new (title);

	lrg_container_set_spacing (LRG_CONTAINER (column), 8.0f);
	lrg_container_add_child (LRG_CONTAINER (column), LRG_WIDGET (label));
	lrg_container_add_child (LRG_CONTAINER (column), child);
	return column;
}

static void
build (App *app)
{
	static const gchar *backlog[] = {
		"Sky shader", "Pathfinding", "Save slots", "Boss intro", "Loot tables", NULL
	};
	static const gchar *done[] = { "Main menu", "Audio mixer", NULL };
	static const gchar *loot[N_SLOTS] = {
		"Sword", "Shield", NULL, "Potion", NULL, "Map", NULL, NULL, "Key", NULL, NULL, "Gem"
	};
	g_autoptr(LrgLabel) heading = lrg_label_new ("Drag and drop");
	g_autoptr(LrgLabel) hint = NULL;
	g_autoptr(LrgHBox)  row = lrg_hbox_new ();
	g_autoptr(LrgGrid)  grid = lrg_grid_new (SLOT_COLUMNS);
	g_autoptr(LrgVBox)  side = lrg_vbox_new ();
	LrgDropTarget      *trash_target;
	static const gchar *trash_types[] = { ITEM_TYPE, NULL };
	guint               i;

	app->canvas = lrg_canvas_new ();
	lrg_canvas_set_paint_background (app->canvas, TRUE);
	app->root = lrg_vbox_new ();
	lrg_container_set_padding (LRG_CONTAINER (app->root), 20.0f);
	lrg_container_set_spacing (LRG_CONTAINER (app->root), 14.0f);
	lrg_container_add_child (LRG_CONTAINER (app->canvas), LRG_WIDGET (app->root));

	lrg_label_set_font_size (heading, 26.0f);
	hint = lrg_label_new ("Drag rows between the lists or into the inventory. Ctrl copies, Shift moves, "
	                      "Alt links, Escape cancels.");
	lrg_container_add_child (LRG_CONTAINER (app->root), LRG_WIDGET (heading));
	lrg_container_add_child (LRG_CONTAINER (app->root), LRG_WIDGET (hint));

	/* Lists, side by side */
	lrg_container_set_spacing (LRG_CONTAINER (row), 18.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (row), 1.0f);
	for (i = 0; i < 2; i++)
	{
		LrgCustomWidget    *w = list_new (app, &app->lists[i], i == 0 ? "Backlog" : "Done",
		                                  i == 0 ? backlog : done);
		g_autoptr(LrgVBox)  col = titled_column (i == 0 ? "List A" : "List B", LRG_WIDGET (w));

		g_object_unref (w);
		lrg_widget_set_fill_weight (LRG_WIDGET (col), 1.0f);
		lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (col));
	}

	/* Inventory grid and the trash under it */
	lrg_grid_set_column_spacing (grid, 8.0f);
	lrg_grid_set_row_spacing (grid, 8.0f);
	for (i = 0; i < N_SLOTS; i++)
	{
		LrgCustomWidget *w = slot_new (app, &app->slots[i], i, loot[i]);

		lrg_container_add_child (LRG_CONTAINER (grid), LRG_WIDGET (w));
		g_object_unref (w);
	}
	lrg_container_set_spacing (LRG_CONTAINER (side), 14.0f);
	{
		g_autoptr(LrgVBox) inv = titled_column ("Inventory", LRG_WIDGET (grid));

		lrg_container_add_child (LRG_CONTAINER (side), LRG_WIDGET (inv));
	}

	app->trash = lrg_panel_new ();
	app->trash_label = lrg_label_new ("Trash");
	lrg_panel_set_fill_children (app->trash, TRUE);
	lrg_container_set_padding (LRG_CONTAINER (app->trash), 18.0f);
	lrg_widget_set_min_size (LRG_WIDGET (app->trash), 0.0f, 70.0f);
	lrg_container_add_child (LRG_CONTAINER (app->trash), LRG_WIDGET (app->trash_label));
	trash_target = lrg_drop_target_new (trash_types, LRG_DRAG_ACTION_COPY | LRG_DRAG_ACTION_MOVE);
	g_signal_connect (trash_target, "motion", G_CALLBACK (trash_motion), app);
	g_signal_connect (trash_target, "leave", G_CALLBACK (trash_leave), app);
	g_signal_connect (trash_target, "drop", G_CALLBACK (trash_drop), app);
	lrg_widget_add_drop_target (LRG_WIDGET (app->trash), trash_target);
	lrg_container_add_child (LRG_CONTAINER (side), LRG_WIDGET (app->trash));

	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (side));
	lrg_container_add_child (LRG_CONTAINER (app->root), LRG_WIDGET (row));

	app->status = lrg_label_new ("Ready.");
	lrg_container_add_child (LRG_CONTAINER (app->root), LRG_WIDGET (app->status));
}

static void
app_clear (App *app)
{
	guint i;

	for (i = 0; i < 2; i++)
		g_clear_pointer (&app->lists[i].items, g_ptr_array_unref);
	for (i = 0; i < N_SLOTS; i++)
		g_clear_pointer (&app->slots[i].item, g_free);
	g_clear_object (&app->trash_label);
	g_clear_object (&app->trash);
	g_clear_object (&app->status);
	g_clear_object (&app->root);
	g_clear_object (&app->canvas);
}

/* ==========================================================================
 * Scripted demo drag (for screenshots without a mouse)
 * ========================================================================== */

/*
 * demo_input: frame @frame of a drag from the second Backlog row to the
 * third inventory slot (empty), button held from frame 10, released at
 * frame 90. With --screenshot and --frames between 40 and 89 the
 * capture shows the chip over the slot.
 */
static void
demo_input (App            *app,
            gint            frame,
            LrgCanvasInput *input)
{
	LrgWidget *from = LRG_WIDGET (app->lists[0].widget);
	LrgWidget *to = LRG_WIDGET (app->slots[2].widget);
	LrgTheme  *theme = lrg_widget_get_theme (from);
	gfloat     x0 = lrg_widget_get_world_x (from) + 60.0f;
	gfloat     y0 = lrg_widget_get_world_y (from) + list_header (theme) + list_row (theme) * 1.5f;
	gfloat     x1 = lrg_widget_get_world_x (to) + lrg_widget_get_width (to) * 0.5f;
	gfloat     y1 = lrg_widget_get_world_y (to) + lrg_widget_get_height (to) * 0.5f;
	gfloat     t = CLAMP ((gfloat) (frame - 12) / 28.0f, 0.0f, 1.0f);
	const guint left = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);

	/* Ease the pointer from the row to the slot */
	t = t * t * (3.0f - 2.0f * t);
	input->pointer_x = x0 + (x1 - x0) * t;
	input->pointer_y = y0 + (y1 - y0) * t;
	input->buttons_pressed = 0;
	input->buttons_released = 0;
	input->buttons_down = (frame >= 10 && frame < 90) ? left : 0;
	if (frame == 10)
		input->buttons_pressed = left;
	if (frame == 90)
		input->buttons_released = left;
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError)         error = NULL;
	g_autoptr(GrlWindow)      window = NULL;
	g_autofree gchar         *opt_theme = NULL;
	g_autofree gchar         *opt_screenshot = NULL;
	gint                      opt_frames = 60;
	gboolean                  opt_license = FALSE;
	gboolean                  opt_rtl = FALSE;
	gboolean                  opt_demo = FALSE;
	App                       app;
	gint                      frame = 0;
	GOptionEntry entries[] = {
		{ "theme", 't', 0, G_OPTION_ARG_STRING, &opt_theme,
		  "Theme preset (see lrg_theme_list_presets(), default midnight)", "NAME" },
		{ "rtl", 0, 0, G_OPTION_ARG_NONE, &opt_rtl, "Right-to-left layout", NULL },
		{ "demo", 'd', 0, G_OPTION_ARG_NONE, &opt_demo,
		  "Play a scripted drag (list row into an inventory slot) instead of reading the mouse", NULL },
		{ "screenshot", 's', 0, G_OPTION_ARG_FILENAME, &opt_screenshot,
		  "Save a PNG after --frames frames and exit", "PATH" },
		{ "frames", 'f', 0, G_OPTION_ARG_INT, &opt_frames, "Frames to run before --screenshot (default 60)", "N" },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Show the license and exit", NULL },
		{ NULL }
	};

	ctx = g_option_context_new ("- drag and drop with the libregnum UI toolkit");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Examples:\n"
		"  ui-dnd\n"
		"  ui-dnd --theme catppuccin-latte\n"
		"  ui-dnd --rtl\n"
		"  ui-dnd --demo                                   (watch a scripted drag)\n"
		"  ui-dnd --demo --screenshot dnd.png --frames 60  (capture mid-drag)\n"
		"\n"
		"While dragging: Ctrl copies, Shift moves, Alt or Ctrl+Shift links, Escape cancels.\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("ui-dnd: %s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("%s", license_text);
		return 0;
	}

	memset (&app, 0, sizeof (app));

	SetConfigFlags (FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
	window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT, "libregnum drag and drop");
	grl_window_set_target_fps (window, 60);

	{
		g_autoptr(LrgTheme) theme = lrg_theme_new_preset (opt_theme != NULL ? opt_theme : "midnight");

		if (theme == NULL)
		{
			g_printerr ("ui-dnd: unknown theme '%s'\n", opt_theme);
			return 2;
		}
		lrg_theme_set_default (theme);
	}

	build (&app);
	if (opt_rtl)
		lrg_canvas_set_default_direction (app.canvas, LRG_WIDGET_DIRECTION_RTL);

	while (!grl_window_should_close (window))
	{
		gfloat w = (gfloat) GetScreenWidth ();
		gfloat h = (gfloat) GetScreenHeight ();

		lrg_widget_set_size (LRG_WIDGET (app.canvas), w, h);
		lrg_widget_set_size (LRG_WIDGET (app.root), w, h);

		if (opt_demo)
		{
			LrgCanvasInput input;

			/* Real keys (Escape) still work during the scripted drag */
			lrg_canvas_input_poll (&input, GetFrameTime ());
			demo_input (&app, frame, &input);
			lrg_canvas_process_input (app.canvas, &input);
		}
		else
		{
			lrg_canvas_handle_input (app.canvas);
		}

		grl_window_begin_drawing (window);
		lrg_canvas_render (app.canvas);
		if (opt_screenshot != NULL && frame >= opt_frames)
		{
			Image shot = LoadImageFromScreen ();

			ExportImage (shot, opt_screenshot);
			UnloadImage (shot);
			grl_window_end_drawing (window);
			break;
		}
		grl_window_end_drawing (window);
		frame++;
	}

	app_clear (&app);
	return 0;
}
