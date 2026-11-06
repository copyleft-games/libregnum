/* lrg-tile-view.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTileView: A virtualized grid of tiles (icons, thumbnails).
 *
 * Items of a GListModel are laid out as equal-sized cards, as many per
 * row as fit the width, rows scrolling vertically. Only the rows of
 * tiles inside the viewport plus one buffer row above and below exist
 * as widgets; they are recycled like LrgListView rows.
 *
 * Each realized tile carries a lift spring of its own (driven by the
 * pointer position, not by widget hit testing, so the card's padding
 * counts as the tile): a hovered card rises a little and its shadow
 * grows. The card is the ITEM part; the tile widget made by the
 * factory sits inside it.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-tile-view.h"
#include "lrg-label.h"
#include "lrg-scrollbar-private.h"
#include "../lrg-log.h"

/* Rows of tiles kept beyond each end of the visible range */
#define ROW_BUFFER (1)

/* Two presses on one tile within this time (canvas clock) activate it */
#define DOUBLE_CLICK_TIME (0.4)

/* Room around the grid for the hover lift and shadow */
#define LIFT_ROOM (6.0f)

/* How far a hovered card rises, and how much it grows */
#define LIFT_DISTANCE (3.0f)
#define LIFT_SCALE    (0.025f)

/* Space between the card edge and the tile widget */
#define TILE_INSET (6.0f)

/* Preferred number of tile rows */
#define NATURAL_ROWS (3)
#define NATURAL_COLS (4)

typedef struct
{
	LrgWidget *widget;    /* owned */
	guint      position;  /* bound item, or invalid */
	LrgSpring  lift;      /* 0 resting .. 1 hovered */
	gfloat     dy;        /* widget y inside its slot, from the last placement */
	gboolean   placed;
} Tile;

struct _LrgTileView
{
	LrgContainer           parent_instance;

	GListModel            *model;
	gulong                 items_changed_id;

	LrgTileViewCreateFunc  create;
	LrgTileViewBindFunc    bind;
	gpointer               user_data;
	GDestroyNotify         destroy;

	GArray                *tiles;        /* Tile */
	gfloat                 tile_width;
	gfloat                 tile_height;
	gfloat                 spacing;
	guint                  placed_cols;
	gfloat                 placed_x0;

	LrgSelectionMode       mode;
	GHashTable            *selected;     /* set of GUINT_TO_POINTER (pos + 1) */
	guint                  cursor;

	guint                  hover;        /* tile under the pointer, or invalid */
	guint                  pressed;      /* tile being pressed, or invalid */

	LrgScrollAxis          ay;
	LrgScrollFx            fx;

	gdouble                clock;
	gboolean               click_armed;
	gdouble                last_click_time;
	guint                  last_click_pos;
	gboolean               in_layout;
	gboolean               stepped;
};

G_DEFINE_FINAL_TYPE (LrgTileView, lrg_tile_view, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_MODEL,
	PROP_TILE_WIDTH,
	PROP_TILE_HEIGHT,
	PROP_TILE_SPACING,
	PROP_SELECTION_MODE,
	N_PROPS
};

enum
{
	SIGNAL_SELECTION_CHANGED,
	SIGNAL_TILE_ACTIVATED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

#define KEY(pos) (GUINT_TO_POINTER ((pos) + 1))
#define INVALID  LRG_TILE_VIEW_INVALID_POSITION

/* ==========================================================================
 * Geometry
 * ========================================================================== */

static guint
n_items (LrgTileView *self)
{
	return self->model != NULL ? g_list_model_get_n_items (self->model) : 0;
}

static void
viewport (LrgTileView *self,
          gfloat      *pad,
          gfloat      *vw,
          gfloat      *vh)
{
	LrgWidget *w = LRG_WIDGET (self);

	*pad = lrg_container_get_padding (LRG_CONTAINER (self));
	*vw = MAX (0.0f, lrg_widget_get_width (w) - *pad * 2.0f);
	*vh = MAX (0.0f, lrg_widget_get_height (w) - *pad * 2.0f);
}

static gboolean
smooth (LrgTileView *self)
{
	return lrg_widget_get_motion (LRG_WIDGET (self)) != LRG_MOTION_NONE;
}

/* columns: tiles per row for the current width */
static guint
columns (LrgTileView *self)
{
	gfloat pad;
	gfloat vw;
	gfloat vh;
	gfloat room;

	viewport (self, &pad, &vw, &vh);
	room = vw - LIFT_ROOM * 2.0f + self->spacing;
	if (room < self->tile_width + self->spacing)
		return 1;
	return (guint) floorf (room / (self->tile_width + self->spacing));
}

static gfloat
stride_y (LrgTileView *self)
{
	return self->tile_height + self->spacing;
}

static guint
n_tile_rows (LrgTileView *self)
{
	guint cols = columns (self);

	return (n_items (self) + cols - 1) / cols;
}

/* grid_x0: x of the first column, centring the grid (relative) */
static gfloat
grid_x0 (LrgTileView *self)
{
	gfloat pad;
	gfloat vw;
	gfloat vh;
	guint  cols = columns (self);
	gfloat used = (gfloat) cols * self->tile_width + (gfloat) (cols - 1) * self->spacing;

	viewport (self, &pad, &vw, &vh);
	return roundf (pad + MAX (LIFT_ROOM, (vw - used) * 0.5f));
}

/* tile_rect: content-space rectangle of tile @p (relative, unscrolled) */
static void
tile_rect (LrgTileView *self,
           guint        p,
           gfloat      *x,
           gfloat      *y)
{
	guint cols = columns (self);
	gfloat pad = lrg_container_get_padding (LRG_CONTAINER (self));

	*x = grid_x0 (self) + (gfloat) (p % cols) * (self->tile_width + self->spacing);
	*y = pad + LIFT_ROOM + (gfloat) (p / cols) * stride_y (self);
}

static void
update_extent (LrgTileView *self)
{
	gfloat pad;
	gfloat vw;
	gfloat vh;
	guint  rows = n_tile_rows (self);
	gfloat content = rows > 0 ? (gfloat) rows * stride_y (self) - self->spacing + LIFT_ROOM * 2.0f : 0.0f;

	viewport (self, &pad, &vw, &vh);
	lrg_scroll_axis_set_extent (&self->ay, content, vh);
}

static guint
rows_per_page (LrgTileView *self)
{
	gfloat pad;
	gfloat vw;
	gfloat vh;

	viewport (self, &pad, &vw, &vh);
	return MAX (1u, (guint) floorf (vh / stride_y (self)));
}

/* ==========================================================================
 * Default factory
 * ========================================================================== */

static LrgWidget *
default_create (LrgTileView *self,
                gpointer     user_data)
{
	LrgLabel *label = lrg_label_new ("");

	lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_END);
	lrg_widget_set_halign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
	return LRG_WIDGET (label);
}

static void
default_bind (LrgTileView *self,
              LrgWidget   *tile,
              GObject     *item,
              guint        position,
              gpointer     user_data)
{
	static const gchar * const names[] = { "string", "text", "name", "label", "title" };
	g_autofree gchar          *text = NULL;
	GObjectClass              *klass;
	guint                      i;

	if (!LRG_IS_LABEL (tile))
		return;

	klass = G_OBJECT_GET_CLASS (item);
	for (i = 0; i < G_N_ELEMENTS (names) && text == NULL; i++)
	{
		GParamSpec *pspec = g_object_class_find_property (klass, names[i]);

		if (pspec != NULL && pspec->value_type == G_TYPE_STRING && (pspec->flags & G_PARAM_READABLE))
			g_object_get (item, names[i], &text, NULL);
	}
	lrg_label_set_text (LRG_LABEL (tile), text != NULL ? text : G_OBJECT_TYPE_NAME (item));
}

/* ==========================================================================
 * Tile pool
 * ========================================================================== */

static void
grow_pool (LrgTileView *self,
           guint        n)
{
	LrgContainer *c = LRG_CONTAINER (self);
	gboolean      was = self->in_layout;
	guint         i;

	if (n == 0)
		return;

	self->in_layout = TRUE;
	lrg_container_remove_child (c, self->ay.bar);
	for (i = 0; i < n; i++)
	{
		Tile       tile;
		LrgWidget *w = self->create != NULL ? self->create (self, self->user_data) : default_create (self, NULL);

		if (w == NULL)
		{
			g_warning ("LrgTileView: tile factory returned NULL");
			break;
		}
		tile.widget = w;
		tile.position = INVALID;
		tile.placed = FALSE;
		tile.dy = 0.0f;
		lrg_spring_init (&tile.lift, 0.0f, LRG_MOTION_SMOOTH);
		lrg_widget_set_visible (w, FALSE);
		lrg_container_add_child (c, w);
		g_array_append_val (self->tiles, tile);
	}
	lrg_container_add_child (c, self->ay.bar);
	self->in_layout = was;
}

static void
drop_pool (LrgTileView *self)
{
	gboolean was = self->in_layout;
	guint    i;

	self->in_layout = TRUE;
	for (i = 0; i < self->tiles->len; i++)
	{
		Tile *t = &g_array_index (self->tiles, Tile, i);

		if (lrg_widget_get_parent (t->widget) == LRG_CONTAINER (self))
			lrg_container_remove_child (LRG_CONTAINER (self), t->widget);
		g_clear_object (&t->widget);
	}
	g_array_set_size (self->tiles, 0);
	self->in_layout = was;
}

static void
unbind_from (LrgTileView *self,
             guint        position)
{
	guint i;

	for (i = 0; i < self->tiles->len; i++)
	{
		Tile *t = &g_array_index (self->tiles, Tile, i);

		if (t->position != INVALID && t->position >= position)
			t->position = INVALID;
	}
}

static void
bind_tile (LrgTileView *self,
           Tile        *t,
           guint        position)
{
	g_autoptr(GObject) item = g_list_model_get_item (self->model, position);

	t->position = position;
	t->placed = FALSE;
	lrg_spring_snap (&t->lift, position == self->hover ? 1.0f : 0.0f);
	if (item == NULL)
		return;
	if (self->bind != NULL)
		self->bind (self, t->widget, item, position, self->user_data);
	else
		default_bind (self, t->widget, item, position, NULL);
}

/*
 * sync_tiles:
 *
 * Virtualization by rows of tiles: realizes the tiles of the rows in
 * view plus ROW_BUFFER rows each side, rebinding free tiles, and
 * places them (a full placement only after binding or a column change;
 * scrolling just moves them).
 */
static void
sync_tiles (LrgTileView *self)
{
	guint  n = n_items (self);
	guint  cols = columns (self);
	gfloat stride = stride_y (self);
	gfloat pad;
	gfloat vw;
	gfloat vh;
	gfloat off;
	gfloat x0 = grid_x0 (self);
	guint  first_row;
	guint  start;
	guint  end;
	guint  p;
	guint  i;

	viewport (self, &pad, &vw, &vh);
	off = lrg_scroll_axis_get_value (&self->ay);

	/* 1. Range of tiles */
	first_row = off > LIFT_ROOM ? (guint) floorf ((off - LIFT_ROOM) / stride) : 0;
	first_row = first_row > ROW_BUFFER ? first_row - ROW_BUFFER : 0;
	start = MIN (n, first_row * cols);
	end = MIN (n, (first_row + (guint) ceilf (vh / stride) + 1 + ROW_BUFFER * 2) * cols);

	if (self->tiles->len < end - start)
		grow_pool (self, (end - start) - self->tiles->len);

	/* 2. Free tiles outside the range */
	for (i = 0; i < self->tiles->len; i++)
	{
		Tile *t = &g_array_index (self->tiles, Tile, i);

		if (t->position != INVALID && (t->position < start || t->position >= end))
			t->position = INVALID;
	}

	/* 3. Bind free tiles to positions nobody shows */
	for (p = start; p < end; p++)
	{
		Tile    *free_tile = NULL;
		gboolean shown = FALSE;

		for (i = 0; i < self->tiles->len && !shown; i++)
		{
			Tile *t = &g_array_index (self->tiles, Tile, i);

			if (t->position == p)
				shown = TRUE;
			else if (free_tile == NULL && t->position == INVALID)
				free_tile = t;
		}
		if (!shown && free_tile != NULL)
			bind_tile (self, free_tile, p);
	}

	/* 4. Place */
	if (cols != self->placed_cols || x0 != self->placed_x0)
	{
		self->placed_cols = cols;
		self->placed_x0 = x0;
		for (i = 0; i < self->tiles->len; i++)
			g_array_index (self->tiles, Tile, i).placed = FALSE;
	}
	for (i = 0; i < self->tiles->len; i++)
	{
		Tile  *t = &g_array_index (self->tiles, Tile, i);
		gfloat tx;
		gfloat ty;

		if (t->position == INVALID)
		{
			lrg_widget_set_visible (t->widget, FALSE);
			continue;
		}
		lrg_widget_set_visible (t->widget, TRUE);
		tile_rect (self, t->position, &tx, &ty);
		ty -= off;
		if (!t->placed)
		{
			lrg_widget_place_in_slot (t->widget, tx + TILE_INSET, ty + TILE_INSET,
			                          MAX (0.0f, self->tile_width - TILE_INSET * 2.0f),
			                          MAX (0.0f, self->tile_height - TILE_INSET * 2.0f));
			t->dy = lrg_widget_get_y (t->widget) - (ty + TILE_INSET);
			t->placed = TRUE;
		}
		else
			lrg_widget_set_y (t->widget, ty + TILE_INSET + t->dy);

		if (g_hash_table_contains (self->selected, KEY (t->position)))
			lrg_widget_set_state_flags (t->widget, LRG_WIDGET_STATE_SELECTED);
		else
			lrg_widget_unset_state_flags (t->widget, LRG_WIDGET_STATE_SELECTED);
	}
}

/* ==========================================================================
 * Selection
 * ========================================================================== */

static void
selection_changed (LrgTileView *self)
{
	g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
}

static void
select_only (LrgTileView *self,
             guint        position)
{
	if (self->mode == LRG_SELECTION_MODE_NONE)
		return;
	if (g_hash_table_size (self->selected) == 1 && g_hash_table_contains (self->selected, KEY (position)))
		return;
	g_hash_table_remove_all (self->selected);
	g_hash_table_add (self->selected, KEY (position));
	selection_changed (self);
}

static void
move_cursor_to (LrgTileView *self,
                guint        position)
{
	guint n = n_items (self);

	if (n == 0)
		return;
	position = MIN (position, n - 1);
	select_only (self, position);
	self->cursor = position;
	lrg_tile_view_scroll_to (self, position, TRUE);
}

/* ==========================================================================
 * Model
 * ========================================================================== */

static void
on_items_changed (GListModel  *model,
                  guint        position,
                  guint        removed,
                  guint        added,
                  LrgTileView *self)
{
	GHashTable     *moved;
	GHashTableIter  iter;
	gpointer        key;
	gboolean        changed = FALSE;
	guint           n = n_items (self);

	moved = g_hash_table_new (g_direct_hash, g_direct_equal);
	g_hash_table_iter_init (&iter, self->selected);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		guint p = GPOINTER_TO_UINT (key) - 1;

		if (p < position)
			g_hash_table_add (moved, KEY (p));
		else if (p < position + removed)
			changed = TRUE;
		else
		{
			g_hash_table_add (moved, KEY (p - removed + added));
			changed |= removed != added;
		}
	}
	g_hash_table_unref (self->selected);
	self->selected = moved;

	if (self->cursor != INVALID && self->cursor >= position)
	{
		if (self->cursor >= position + removed)
			self->cursor = self->cursor - removed + added;
		else
			self->cursor = n > 0 ? MIN (position, n - 1) : INVALID;
	}
	if (self->cursor != INVALID && self->cursor >= n)
		self->cursor = n > 0 ? n - 1 : INVALID;

	unbind_from (self, position);
	update_extent (self);
	sync_tiles (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));

	if (changed)
		selection_changed (self);
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static void
lrg_tile_view_layout_children (LrgContainer *container)
{
	LrgTileView *self = LRG_TILE_VIEW (container);
	gfloat       pad;
	gfloat       vw;
	gfloat       vh;
	gfloat       zone;

	if (self->in_layout || self->ay.bar == NULL)
		return;
	self->in_layout = TRUE;

	viewport (self, &pad, &vw, &vh);
	update_extent (self);
	zone = lrg_scroll_bar_zone (LRG_WIDGET (self));
	lrg_widget_set_visible (self->ay.bar, lrg_scroll_axis_get_max (&self->ay) > 0.0f);
	lrg_widget_allocate (self->ay.bar, pad + vw - zone, pad, zone, vh);

	sync_tiles (self);
	self->in_layout = FALSE;
}

static void
lrg_tile_view_measure (LrgWidget *widget,
                       gfloat    *preferred_width,
                       gfloat    *preferred_height)
{
	LrgTileView *self = LRG_TILE_VIEW (widget);
	gfloat       pad = lrg_container_get_padding (LRG_CONTAINER (self));
	guint        cols = MAX (1u, MIN (n_items (self), NATURAL_COLS));
	guint        rows = MAX (1u, MIN ((n_items (self) + cols - 1) / cols, NATURAL_ROWS));

	*preferred_width = (gfloat) cols * (self->tile_width + self->spacing) - self->spacing +
	                   LIFT_ROOM * 2.0f + pad * 2.0f;
	*preferred_height = (gfloat) rows * stride_y (self) - self->spacing + LIFT_ROOM * 2.0f + pad * 2.0f;
}

/* ==========================================================================
 * Looks and painting
 * ========================================================================== */

static void
lrg_tile_view_style_defaults (LrgWidget      *widget,
                              LrgUIPart       part,
                              LrgWidgetState  state,
                              LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	gfloat    radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	gfloat    blur = lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR);
	gfloat    bw = lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH);
	GrlColor  c;

	if (lrg_scroll_style_defaults (theme, part, state, look))
		return;

	switch (part)
	{
	case LRG_UI_PART_BOX:
		/* A window-coloured well the cards stand on */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme,
		                          (state & LRG_WIDGET_STATE_FOCUSED) ? LRG_THEME_COLOR_ACCENT
		                                                             : LRG_THEME_COLOR_BORDER), bw);
		lrg_part_look_set_radius (look, radius);
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;

	case LRG_UI_PART_ITEM:
		/* Card: surface with a soft shadow that deepens on hover */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme,
		                              (state & LRG_WIDGET_STATE_PRESSED) ? LRG_THEME_COLOR_CONTROL_PRESSED :
		                              (state & LRG_WIDGET_STATE_HOVERED) ? LRG_THEME_COLOR_CONTROL_HOVER
		                                                                 : LRG_THEME_COLOR_SURFACE));
		if (state & LRG_WIDGET_STATE_SELECTED)
			lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), MAX (2.0f, bw));
		else
			lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), bw);
		lrg_part_look_set_radius (look, radius);
		c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW);
		if (state & LRG_WIDGET_STATE_HOVERED)
			lrg_part_look_set_shadow (look, &c, blur * 0.75f, 5.0f);
		else
			lrg_part_look_set_shadow (look, &c, blur * 0.3f, 1.5f);
		if (state & LRG_WIDGET_STATE_SELECTED)
		{
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.35f, &c);
			lrg_part_look_set_glow (look, &c, 6.0f);
		}
		return;

	case LRG_UI_PART_INDICATOR:
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_FOCUS_RING_WIDTH));
		lrg_part_look_set_radius (look, radius + 3.0f);
		return;

	default:
		break;
	}

	LRG_WIDGET_CLASS (lrg_tile_view_parent_class)->style_defaults (widget, part, state, look);
}

/*
 * paint_tile:
 *
 * Card and tile widget inside one paint group, lifted and scaled by
 * the tile's spring around the card centre. The card look cross-fades
 * resting -> hovered by the same spring, and to pressed while held.
 */
static void
paint_tile (LrgTileView  *self,
            LrgPaintList *list,
            Tile         *t,
            gfloat        x,
            gfloat        y)
{
	LrgWidget     *widget = LRG_WIDGET (self);
	LrgWidgetState base = LRG_WIDGET_STATE_NONE;
	LrgPartLook    look;
	LrgPartLook    variant;
	gfloat         lift = CLAMP (t->lift.value, 0.0f, 1.2f);
	gfloat         w = self->tile_width;
	gfloat         h = self->tile_height;
	guint          mark;

	if (g_hash_table_contains (self->selected, KEY (t->position)))
		base |= LRG_WIDGET_STATE_SELECTED;

	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, base, &look);
	if (lift > 0.001f)
	{
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, base | LRG_WIDGET_STATE_HOVERED, &variant);
		lrg_part_look_lerp (&look, &variant, MIN (lift, 1.0f), &look);
	}
	if (t->position == self->pressed)
	{
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM,
		                                   base | LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED, &look);
		lift *= 0.3f;
	}

	mark = lrg_paint_list_begin_group (list);
	lrg_widget_paint_box (widget, list, &look, x, y, w, h);
	lrg_widget_paint (t->widget, list);
	if (lift > 0.001f)
		lrg_paint_list_transform_group (list, mark, 1.0f + LIFT_SCALE * lift, x + w * 0.5f, y + h * 0.5f,
		                                0.0f, -LIFT_DISTANCE * lift, 1.0f);

	/* Cursor ring after keyboard / gamepad navigation */
	if (t->position == self->cursor && (lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_FOCUS_VISIBLE))
	{
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_INDICATOR, base, &variant);
		lrg_paint_list_rect_ex (list, x - 3.0f, y - 3.0f - LIFT_DISTANCE * lift, w + 6.0f, h + 6.0f,
		                        &variant.radii, NULL, NULL, variant.border_width, &variant.border);
	}
}

/*
 * lrg_tile_view_step:
 *
 * Canvas clock, scroll spring and fade, and every realized tile's lift
 * spring aimed at the hovered tile (snapped on the first frame).
 */
static gboolean
lrg_tile_view_step (LrgWidget *widget,
                    gfloat     delta_time)
{
	LrgTileView *self = LRG_TILE_VIEW (widget);
	LrgMotion    motion = lrg_widget_get_motion (widget);
	gboolean     moving;
	gboolean     keep;
	guint        i;

	if (delta_time > 0.0f)
		self->clock += (gdouble) delta_time;
	if (self->ay.bar == NULL)
		return FALSE;

	moving = lrg_scroll_axis_step_snap (&self->ay, delta_time);
	if (moving)
		lrg_scroll_fx_poke (&self->fx);
	sync_tiles (self);

	for (i = 0; i < self->tiles->len; i++)
	{
		Tile *t = &g_array_index (self->tiles, Tile, i);

		lrg_spring_set_motion (&t->lift, motion);
		lrg_spring_set_target (&t->lift, (t->position != INVALID && t->position == self->hover) ? 1.0f : 0.0f);
		if (!self->stepped || motion == LRG_MOTION_NONE)
			lrg_spring_snap (&t->lift, t->lift.target);
		else
			moving |= lrg_spring_step (&t->lift, delta_time);
	}
	self->stepped = TRUE;

	keep = self->ay.dragging || (lrg_widget_get_state_flags (self->ay.bar) & LRG_WIDGET_STATE_HOVERED);
	return lrg_scroll_fx_update (&self->fx, keep, delta_time) || moving;
}

static void
lrg_tile_view_paint (LrgWidget    *widget,
                     LrgPaintList *list)
{
	LrgTileView *self = LRG_TILE_VIEW (widget);
	LrgPartLook  box;
	gfloat       wx = lrg_widget_get_world_x (widget);
	gfloat       wy = lrg_widget_get_world_y (widget);
	gfloat       pad;
	gfloat       vw;
	gfloat       vh;
	gfloat       off;
	guint        i;
	Tile        *top = NULL;

	sync_tiles (self);

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, wx, wy, lrg_widget_get_width (widget), lrg_widget_get_height (widget));

	viewport (self, &pad, &vw, &vh);
	off = lrg_scroll_axis_get_value (&self->ay);
	lrg_paint_list_push_clip (list, wx + pad, wy + pad, vw, vh);

	/* Resting tiles first, the most lifted one last so its shadow
	 * falls over its neighbours */
	for (i = 0; i < self->tiles->len; i++)
	{
		Tile  *t = &g_array_index (self->tiles, Tile, i);
		gfloat tx;
		gfloat ty;

		if (t->position == INVALID)
			continue;
		if (t->lift.value > 0.001f && (top == NULL || t->lift.value > top->lift.value))
		{
			if (top != NULL)
			{
				tile_rect (self, top->position, &tx, &ty);
				paint_tile (self, list, top, wx + tx, wy + ty - off);
			}
			top = t;
			continue;
		}
		tile_rect (self, t->position, &tx, &ty);
		paint_tile (self, list, t, wx + tx, wy + ty - off);
	}
	if (top != NULL)
	{
		gfloat tx;
		gfloat ty;

		tile_rect (self, top->position, &tx, &ty);
		paint_tile (self, list, top, wx + tx, wy + ty - off);
	}
	lrg_paint_list_pop_clip (list);

	lrg_scroll_bar_paint (widget, list, &self->ay, TRUE, self->fx.fade.value);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/* position_at: the tile under a world point, or invalid (gaps miss) */
static guint
position_at (LrgTileView *self,
             gfloat       x,
             gfloat       y)
{
	gfloat pad;
	gfloat vw;
	gfloat vh;
	gfloat rx;
	gfloat ry;
	gfloat sx = self->tile_width + self->spacing;
	gfloat sy = stride_y (self);
	guint  cols = columns (self);
	guint  col;
	guint  row;
	guint  p;

	viewport (self, &pad, &vw, &vh);
	ry = y - (lrg_widget_get_world_y (LRG_WIDGET (self)) + pad);
	if (ry < 0.0f || ry >= vh)
		return INVALID;
	ry += lrg_scroll_axis_get_value (&self->ay) - LIFT_ROOM;
	rx = x - (lrg_widget_get_world_x (LRG_WIDGET (self)) + grid_x0 (self));
	if (rx < 0.0f || ry < 0.0f)
		return INVALID;

	col = (guint) floorf (rx / sx);
	row = (guint) floorf (ry / sy);
	if (col >= cols || rx - (gfloat) col * sx >= self->tile_width || ry - (gfloat) row * sy >= self->tile_height)
		return INVALID;
	p = row * cols + col;
	return p < n_items (self) ? p : INVALID;
}

static void
click_tile (LrgTileView       *self,
            guint              p,
            LrgInputModifiers  mods)
{
	if (self->mode == LRG_SELECTION_MODE_MULTIPLE && (mods & LRG_INPUT_MODIFIER_CTRL))
	{
		if (!g_hash_table_remove (self->selected, KEY (p)))
			g_hash_table_add (self->selected, KEY (p));
		selection_changed (self);
	}
	else
		select_only (self, p);
	self->cursor = p;

	if (self->click_armed && self->last_click_pos == p && self->clock - self->last_click_time <= DOUBLE_CLICK_TIME)
	{
		self->click_armed = FALSE;
		g_signal_emit (self, signals[SIGNAL_TILE_ACTIVATED], 0, p);
		return;
	}
	self->click_armed = TRUE;
	self->last_click_time = self->clock;
	self->last_click_pos = p;
}

/*
 * handle_key:
 *
 * 2D navigation: Left / Right by one tile, Up / Down by one row (the
 * same column), Page Up / Down by a page of rows, Home / End. Arrows
 * are consumed at the edges so focus stays in the grid.
 */
static gboolean
handle_key (LrgTileView *self,
            GrlKey       key)
{
	guint    n = n_items (self);
	guint    cols = columns (self);
	guint    cur = self->cursor;
	guint    page = rows_per_page (self) * cols;
	gboolean none = cur == INVALID;

	if (n == 0)
		return key == GRL_KEY_UP || key == GRL_KEY_DOWN || key == GRL_KEY_LEFT || key == GRL_KEY_RIGHT;

	switch (key)
	{
	case GRL_KEY_LEFT:
		move_cursor_to (self, none ? 0 : (cur > 0 ? cur - 1 : 0));
		return TRUE;
	case GRL_KEY_RIGHT:
		move_cursor_to (self, none ? 0 : MIN (cur + 1, n - 1));
		return TRUE;
	case GRL_KEY_UP:
		move_cursor_to (self, none ? 0 : (cur >= cols ? cur - cols : cur));
		return TRUE;
	case GRL_KEY_DOWN:
		/* The last row may be short: land on its last tile */
		if (none)
			move_cursor_to (self, 0);
		else if (cur + cols < n)
			move_cursor_to (self, cur + cols);
		else if (cur / cols < (n - 1) / cols)
			move_cursor_to (self, n - 1);
		else
			move_cursor_to (self, cur);
		return TRUE;
	case GRL_KEY_HOME:
		move_cursor_to (self, 0);
		return TRUE;
	case GRL_KEY_END:
		move_cursor_to (self, n - 1);
		return TRUE;
	case GRL_KEY_PAGE_UP:
		move_cursor_to (self, none ? 0 : (cur > page ? cur - page : cur % cols));
		return TRUE;
	case GRL_KEY_PAGE_DOWN:
		move_cursor_to (self, none ? 0 : MIN (cur + page, n - 1));
		return TRUE;
	default:
		return FALSE;
	}
}

static gboolean
lrg_tile_view_handle_event (LrgWidget        *widget,
                            const LrgUIEvent *event)
{
	LrgTileView *self = LRG_TILE_VIEW (widget);
	LrgWidget   *target = lrg_ui_event_get_target (event);
	gfloat       x = lrg_ui_event_get_x (event);
	gfloat       y = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_POINTER_ENTER:
		lrg_scroll_fx_poke (&self->fx);
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		if (target == widget)
			self->hover = INVALID;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_MOVE:
		lrg_scroll_fx_poke (&self->fx);
		if (self->ay.dragging)
		{
			lrg_scroll_bar_drag (&self->ay, TRUE, y);
			return TRUE;
		}
		self->hover = (target == self->ay.bar || !lrg_widget_contains_point (widget, x, y))
		              ? INVALID : position_at (self, x, y);
		if (self->pressed != INVALID && self->pressed != self->hover)
			self->pressed = INVALID;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		{
			guint p;

			if (lrg_ui_event_get_button (event) != 0)
				return FALSE;
			if (target == self->ay.bar)
			{
				lrg_scroll_bar_press (&self->ay, TRUE, y, smooth (self));
				lrg_scroll_fx_poke (&self->fx);
				return TRUE;
			}
			p = position_at (self, x, y);
			self->hover = p;
			if (p == INVALID)
				return FALSE;
			self->pressed = p;
			click_tile (self, p, lrg_ui_event_get_modifiers (event));
			return TRUE;
		}

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		self->pressed = INVALID;
		if (self->ay.dragging)
		{
			self->ay.dragging = FALSE;
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_PAN:
		update_extent (self);
		self->pressed = INVALID;
		if (fabsf (lrg_ui_event_get_delta_y (event)) < fabsf (lrg_ui_event_get_delta_x (event)) * 0.5f)
			return FALSE;
		if (!lrg_scroll_axis_pan (&self->ay, lrg_ui_event_get_delta_y (event)))
			return FALSE;
		lrg_scroll_fx_poke (&self->fx);
		return TRUE;

	case LRG_UI_EVENT_PAN_END:
		lrg_scroll_axis_flick (&self->ay, smooth (self) ? lrg_ui_event_get_velocity_y (event) : 0.0f);
		lrg_scroll_fx_poke (&self->fx);
		return TRUE;

	case LRG_UI_EVENT_SCROLL:
		{
			gboolean handled;

			update_extent (self);
			handled = lrg_scroll_axis_wheel (&self->ay, -lrg_ui_event_get_scroll_y (event) *
			                                 LRG_SCROLL_WHEEL_STEP, smooth (self));
			if (handled)
				lrg_scroll_fx_poke (&self->fx);
			return handled;
		}

	case LRG_UI_EVENT_KEY_DOWN:
		update_extent (self);
		return handle_key (self, lrg_ui_event_get_key (event));

	case LRG_UI_EVENT_ACTIVATE:
		if (self->cursor == INVALID || self->cursor >= n_items (self))
			return FALSE;
		g_signal_emit (self, signals[SIGNAL_TILE_ACTIVATED], 0, self->cursor);
		return TRUE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
clear_factory (LrgTileView *self)
{
	if (self->destroy != NULL && self->user_data != NULL)
		self->destroy (self->user_data);
	self->create = NULL;
	self->bind = NULL;
	self->user_data = NULL;
	self->destroy = NULL;
}

static void
lrg_tile_view_constructed (GObject *object)
{
	LrgTileView *self = LRG_TILE_VIEW (object);

	G_OBJECT_CLASS (lrg_tile_view_parent_class)->constructed (object);

	self->in_layout = TRUE;
	self->ay.bar = lrg_scroll_bar_new ("vscrollbar");
	lrg_container_add_child (LRG_CONTAINER (self), self->ay.bar);
	self->in_layout = FALSE;
}

static void
lrg_tile_view_dispose (GObject *object)
{
	LrgTileView *self = LRG_TILE_VIEW (object);

	if (self->model != NULL)
	{
		g_clear_signal_handler (&self->items_changed_id, self->model);
		g_clear_object (&self->model);
	}
	if (self->tiles != NULL)
		drop_pool (self);
	clear_factory (self);
	g_clear_object (&self->ay.bar);

	G_OBJECT_CLASS (lrg_tile_view_parent_class)->dispose (object);
}

static void
lrg_tile_view_finalize (GObject *object)
{
	LrgTileView *self = LRG_TILE_VIEW (object);

	g_clear_pointer (&self->tiles, g_array_unref);
	g_clear_pointer (&self->selected, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_tile_view_parent_class)->finalize (object);
}

static void
lrg_tile_view_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	LrgTileView *self = LRG_TILE_VIEW (object);

	switch (prop_id)
	{
	case PROP_MODEL:
		g_value_set_object (value, self->model);
		break;
	case PROP_TILE_WIDTH:
		g_value_set_float (value, self->tile_width);
		break;
	case PROP_TILE_HEIGHT:
		g_value_set_float (value, self->tile_height);
		break;
	case PROP_TILE_SPACING:
		g_value_set_float (value, self->spacing);
		break;
	case PROP_SELECTION_MODE:
		g_value_set_enum (value, self->mode);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_tile_view_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	LrgTileView *self = LRG_TILE_VIEW (object);

	switch (prop_id)
	{
	case PROP_MODEL:
		lrg_tile_view_set_model (self, g_value_get_object (value));
		break;
	case PROP_TILE_WIDTH:
		lrg_tile_view_set_tile_size (self, g_value_get_float (value), self->tile_height);
		break;
	case PROP_TILE_HEIGHT:
		lrg_tile_view_set_tile_size (self, self->tile_width, g_value_get_float (value));
		break;
	case PROP_TILE_SPACING:
		lrg_tile_view_set_tile_spacing (self, g_value_get_float (value));
		break;
	case PROP_SELECTION_MODE:
		lrg_tile_view_set_selection_mode (self, (LrgSelectionMode) g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_tile_view_class_init (LrgTileViewClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->constructed = lrg_tile_view_constructed;
	object_class->dispose = lrg_tile_view_dispose;
	object_class->finalize = lrg_tile_view_finalize;
	object_class->get_property = lrg_tile_view_get_property;
	object_class->set_property = lrg_tile_view_set_property;

	widget_class->measure = lrg_tile_view_measure;
	widget_class->paint = lrg_tile_view_paint;
	widget_class->step = lrg_tile_view_step;
	widget_class->style_defaults = lrg_tile_view_style_defaults;
	widget_class->handle_event = lrg_tile_view_handle_event;

	container_class->layout_children = lrg_tile_view_layout_children;

	/**
	 * LrgTileView:model:
	 *
	 * The #GListModel whose items are shown as tiles.
	 */
	properties[PROP_MODEL] =
		g_param_spec_object ("model", "Model", "The items to show",
		                     G_TYPE_LIST_MODEL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTileView:tile-width:
	 *
	 * Width of every tile card.
	 */
	properties[PROP_TILE_WIDTH] =
		g_param_spec_float ("tile-width", "Tile width", "Width of every tile",
		                    8.0f, G_MAXFLOAT, 96.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTileView:tile-height:
	 *
	 * Height of every tile card.
	 */
	properties[PROP_TILE_HEIGHT] =
		g_param_spec_float ("tile-height", "Tile height", "Height of every tile",
		                    8.0f, G_MAXFLOAT, 96.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTileView:tile-spacing:
	 *
	 * Gap between tiles, horizontally and vertically.
	 */
	properties[PROP_TILE_SPACING] =
		g_param_spec_float ("tile-spacing", "Tile spacing", "Gap between tiles",
		                    0.0f, G_MAXFLOAT, 8.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTileView:selection-mode:
	 *
	 * How many tiles can be selected.
	 */
	properties[PROP_SELECTION_MODE] =
		g_param_spec_enum ("selection-mode", "Selection mode", "How many tiles can be selected",
		                   LRG_TYPE_SELECTION_MODE, LRG_SELECTION_MODE_SINGLE,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgTileView::selection-changed:
	 * @self: the tile view
	 *
	 * Emitted when tiles were selected or unselected.
	 */
	signals[SIGNAL_SELECTION_CHANGED] =
		g_signal_new ("selection-changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);

	/**
	 * LrgTileView::tile-activated:
	 * @self: the tile view
	 * @position: the activated tile
	 *
	 * Emitted on a double click, or Enter / Space / gamepad A on the
	 * cursor tile.
	 */
	signals[SIGNAL_TILE_ACTIVATED] =
		g_signal_new ("tile-activated",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
lrg_tile_view_init (LrgTileView *self)
{
	self->tiles = g_array_new (FALSE, TRUE, sizeof (Tile));
	self->selected = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->tile_width = 96.0f;
	self->tile_height = 96.0f;
	self->spacing = 8.0f;
	self->mode = LRG_SELECTION_MODE_SINGLE;
	self->cursor = INVALID;
	self->hover = INVALID;
	self->pressed = INVALID;
	self->last_click_pos = INVALID;
	self->placed_x0 = -1.0f;
	lrg_scroll_axis_init (&self->ay);
	lrg_scroll_fx_init (&self->fx);

	lrg_container_set_padding (LRG_CONTAINER (self), 2.0f);
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgTileView *
lrg_tile_view_new (void)
{
	return g_object_new (LRG_TYPE_TILE_VIEW, NULL);
}

void
lrg_tile_view_set_model (LrgTileView *self,
                         GListModel  *model)
{
	gboolean had_selection;

	g_return_if_fail (LRG_IS_TILE_VIEW (self));
	g_return_if_fail (model == NULL || G_IS_LIST_MODEL (model));

	if (self->model == model)
		return;
	if (self->model != NULL)
	{
		g_clear_signal_handler (&self->items_changed_id, self->model);
		g_clear_object (&self->model);
	}
	if (model != NULL)
	{
		self->model = g_object_ref (model);
		self->items_changed_id = g_signal_connect (model, "items-changed", G_CALLBACK (on_items_changed), self);
	}

	had_selection = g_hash_table_size (self->selected) > 0;
	g_hash_table_remove_all (self->selected);
	self->cursor = INVALID;
	self->hover = INVALID;
	unbind_from (self, 0);
	lrg_scroll_axis_scroll_to (&self->ay, 0.0f, FALSE);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	lrg_container_layout_children (LRG_CONTAINER (self));

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
	if (had_selection)
		selection_changed (self);
}

GListModel *
lrg_tile_view_get_model (LrgTileView *self)
{
	g_return_val_if_fail (LRG_IS_TILE_VIEW (self), NULL);
	return self->model;
}

void
lrg_tile_view_set_factory (LrgTileView           *self,
                           LrgTileViewCreateFunc  create,
                           LrgTileViewBindFunc    bind,
                           gpointer               user_data,
                           GDestroyNotify         destroy)
{
	g_return_if_fail (LRG_IS_TILE_VIEW (self));

	drop_pool (self);
	clear_factory (self);
	self->create = create;
	self->bind = bind;
	self->user_data = user_data;
	self->destroy = destroy;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	lrg_container_layout_children (LRG_CONTAINER (self));
}

void
lrg_tile_view_set_tile_size (LrgTileView *self,
                             gfloat       width,
                             gfloat       height)
{
	gboolean wc;
	gboolean hc;

	g_return_if_fail (LRG_IS_TILE_VIEW (self));

	width = MAX (8.0f, roundf (width));
	height = MAX (8.0f, roundf (height));
	wc = self->tile_width != width;
	hc = self->tile_height != height;
	if (!wc && !hc)
		return;
	self->tile_width = width;
	self->tile_height = height;
	self->placed_cols = 0;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	if (wc)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TILE_WIDTH]);
	if (hc)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TILE_HEIGHT]);
}

void
lrg_tile_view_get_tile_size (LrgTileView *self,
                             gfloat      *width,
                             gfloat      *height)
{
	g_return_if_fail (LRG_IS_TILE_VIEW (self));

	if (width != NULL)
		*width = self->tile_width;
	if (height != NULL)
		*height = self->tile_height;
}

gfloat
lrg_tile_view_get_tile_spacing (LrgTileView *self)
{
	g_return_val_if_fail (LRG_IS_TILE_VIEW (self), 0.0f);
	return self->spacing;
}

void
lrg_tile_view_set_tile_spacing (LrgTileView *self,
                                gfloat       spacing)
{
	g_return_if_fail (LRG_IS_TILE_VIEW (self));

	spacing = MAX (0.0f, roundf (spacing));
	if (self->spacing == spacing)
		return;
	self->spacing = spacing;
	self->placed_cols = 0;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TILE_SPACING]);
}

guint
lrg_tile_view_get_columns (LrgTileView *self)
{
	g_return_val_if_fail (LRG_IS_TILE_VIEW (self), 1);
	return columns (self);
}

LrgSelectionMode
lrg_tile_view_get_selection_mode (LrgTileView *self)
{
	g_return_val_if_fail (LRG_IS_TILE_VIEW (self), LRG_SELECTION_MODE_NONE);
	return self->mode;
}

void
lrg_tile_view_set_selection_mode (LrgTileView      *self,
                                  LrgSelectionMode  mode)
{
	g_return_if_fail (LRG_IS_TILE_VIEW (self));

	if (self->mode == mode)
		return;
	self->mode = mode;
	if (mode == LRG_SELECTION_MODE_NONE && g_hash_table_size (self->selected) > 0)
	{
		g_hash_table_remove_all (self->selected);
		selection_changed (self);
	}
	else if (mode == LRG_SELECTION_MODE_SINGLE && g_hash_table_size (self->selected) > 1)
		select_only (self, lrg_tile_view_get_selected (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECTION_MODE]);
}

void
lrg_tile_view_select (LrgTileView *self,
                      guint        position)
{
	g_return_if_fail (LRG_IS_TILE_VIEW (self));

	if (position >= n_items (self))
		return;
	if (self->mode == LRG_SELECTION_MODE_SINGLE)
		select_only (self, position);
	else if (self->mode == LRG_SELECTION_MODE_MULTIPLE && !g_hash_table_contains (self->selected, KEY (position)))
	{
		g_hash_table_add (self->selected, KEY (position));
		selection_changed (self);
	}
	self->cursor = position;
}

void
lrg_tile_view_unselect_all (LrgTileView *self)
{
	g_return_if_fail (LRG_IS_TILE_VIEW (self));

	if (g_hash_table_size (self->selected) == 0)
		return;
	g_hash_table_remove_all (self->selected);
	selection_changed (self);
}

guint
lrg_tile_view_get_selected (LrgTileView *self)
{
	GHashTableIter iter;
	gpointer       key;
	guint          best = INVALID;

	g_return_val_if_fail (LRG_IS_TILE_VIEW (self), INVALID);

	if (self->cursor != INVALID && g_hash_table_contains (self->selected, KEY (self->cursor)))
		return self->cursor;
	g_hash_table_iter_init (&iter, self->selected);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		best = MIN (best, GPOINTER_TO_UINT (key) - 1);
	return best;
}

gboolean
lrg_tile_view_is_selected (LrgTileView *self,
                           guint        position)
{
	g_return_val_if_fail (LRG_IS_TILE_VIEW (self), FALSE);
	return g_hash_table_contains (self->selected, KEY (position));
}

guint
lrg_tile_view_get_n_selected (LrgTileView *self)
{
	g_return_val_if_fail (LRG_IS_TILE_VIEW (self), 0);
	return g_hash_table_size (self->selected);
}

guint
lrg_tile_view_get_cursor (LrgTileView *self)
{
	g_return_val_if_fail (LRG_IS_TILE_VIEW (self), INVALID);
	return self->cursor;
}

void
lrg_tile_view_scroll_to (LrgTileView *self,
                         guint        position,
                         gboolean     animate)
{
	gfloat pad;
	gfloat vw;
	gfloat vh;
	gfloat tx;
	gfloat top;
	gfloat bottom;
	gfloat target;

	g_return_if_fail (LRG_IS_TILE_VIEW (self));

	if (position >= n_items (self))
		return;

	update_extent (self);
	viewport (self, &pad, &vw, &vh);
	tile_rect (self, position, &tx, &top);
	top -= pad + LIFT_ROOM;
	bottom = top + self->tile_height + LIFT_ROOM * 2.0f;

	target = self->ay.bounce ? lrg_scroll_axis_clamp (&self->ay, self->ay.pos.target) : self->ay.pos.target;
	if (bottom > target + vh)
		target = bottom - vh;
	if (top < target)
		target = top;

	lrg_scroll_axis_scroll_to (&self->ay, target, animate && smooth (self));
	lrg_scroll_fx_poke (&self->fx);
	if (!(animate && smooth (self)))
		sync_tiles (self);
}

gfloat
lrg_tile_view_get_scroll_offset (LrgTileView *self)
{
	g_return_val_if_fail (LRG_IS_TILE_VIEW (self), 0.0f);
	return lrg_scroll_axis_get_value (&self->ay);
}

LrgWidget *
lrg_tile_view_get_tile_widget (LrgTileView *self,
                               guint        position)
{
	guint i;

	g_return_val_if_fail (LRG_IS_TILE_VIEW (self), NULL);

	for (i = 0; i < self->tiles->len; i++)
	{
		Tile *t = &g_array_index (self->tiles, Tile, i);

		if (t->position == position)
			return t->widget;
	}
	return NULL;
}

guint
lrg_tile_view_get_n_tiles (LrgTileView *self)
{
	g_return_val_if_fail (LRG_IS_TILE_VIEW (self), 0);
	return self->tiles->len;
}
