/* lrg-pagination.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPagination: a page picker, "< 1 ... 4 5 6 ... 20 >".
 *
 * The row is a list of "slots" rebuilt from the properties whenever it
 * is needed: an optional previous arrow, the items from
 * lrg_pagination_compute_items() (pages and gaps) and an optional next
 * arrow. Every slot has the same width (wide enough for the largest
 * page number), so the row never jitters as the current page moves.
 * Slots are laid out left to right in local coordinates and mirrored
 * with lrg_widget_mirror_x() when painting and hit-testing in
 * right-to-left.
 *
 * Motion: each slot owns a hover and a press spring (positional: they
 * belong to the slot index, not the page it shows), and an accent pill
 * glides on two springs (x and width) to the current page's slot. The
 * page label under the pill cross-fades to the on-accent colour as the
 * pill arrives.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-pagination.h"
#include "../paint/lrg-paint-icons.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* How far a pressed button shrinks on each side, in units */
#define PAGINATION_SQUASH (1.5f)

/* Opacity of a disabled arrow (first / last page) */
#define PAGINATION_DISABLED_OPACITY (0.35f)

/* One button of the row, in local left-to-right coordinates */
typedef struct
{
	LrgPaginationItem kind;
	gint              page;     /* page shown / jumped to; -1 if inert */
	gfloat            x;
	gfloat            width;
} Slot;

struct _LrgPagination
{
	LrgWidget  parent_instance;

	guint      n_pages;
	guint      page;
	guint      siblings;
	guint      boundary;
	gboolean   show_prev_next;
	gboolean   jump_on_gap;

	/* Slot cache, rebuilt by build_slots() */
	GArray    *slots;           /* Slot */

	/* Pointer state: slot index under the pointer / pressed, -1 for none */
	gint       hover_slot;
	gint       press_slot;

	/* Motion: one hover and one press spring per slot index; the pill */
	GArray    *hover;           /* LrgSpring */
	GArray    *press;           /* LrgSpring */
	LrgSpring  pill_x;          /* local x of the pill */
	LrgSpring  pill_w;
	gboolean   stepped;
};

G_DEFINE_FINAL_TYPE (LrgPagination, lrg_pagination, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_N_PAGES,
	PROP_PAGE,
	PROP_SIBLINGS,
	PROP_BOUNDARY,
	PROP_SHOW_PREV_NEXT,
	PROP_JUMP_ON_GAP,
	N_PROPS
};

enum
{
	SIGNAL_PAGE_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

GType
lrg_pagination_item_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_PAGINATION_ITEM_NONE, "LRG_PAGINATION_ITEM_NONE", "none" },
			{ LRG_PAGINATION_ITEM_PAGE, "LRG_PAGINATION_ITEM_PAGE", "page" },
			{ LRG_PAGINATION_ITEM_GAP, "LRG_PAGINATION_ITEM_GAP", "gap" },
			{ LRG_PAGINATION_ITEM_PREVIOUS, "LRG_PAGINATION_ITEM_PREVIOUS", "previous" },
			{ LRG_PAGINATION_ITEM_NEXT, "LRG_PAGINATION_ITEM_NEXT", "next" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgPaginationItem"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

/* ==========================================================================
 * The pure item computation
 * ========================================================================== */

/* push_item: append @value if there is room; always count it */
static void
push_item (gint  *items,
           guint  max_items,
           guint *count,
           gint   value)
{
	if (items != NULL && *count < max_items)
		items[*count] = value;
	(*count)++;
}

guint
lrg_pagination_compute_items (guint  n_pages,
                              guint  page,
                              guint  siblings,
                              guint  boundary,
                              gint  *items,
                              guint  max_items)
{
	gint  n = (gint) n_pages;
	gint  p;
	gint  s = (gint) siblings;
	gint  b = (gint) boundary;
	gint  total;
	gint  ws;
	gint  we;
	gint  i;
	guint count = 0;

	if (n_pages == 0)
		return 0;
	p = (gint) MIN (page, n_pages - 1);

	/*
	 * Everything fits: boundary pages at each end, the window of
	 * 2 * siblings + 1 around the current page, and one slot on each
	 * side of the window for either a page or a gap.
	 */
	total = 2 * b + 2 * s + 3;
	if (n <= total)
	{
		for (i = 0; i < n; i++)
			push_item (items, max_items, &count, i);
		return count;
	}

	/*
	 * The window [ws, we] around the current page is clamped so that
	 * there is always exactly one slot between it and each boundary
	 * run. When that slot covers a single page it shows the page; when
	 * it covers two or more it is a gap. That keeps the item count
	 * constant (total) and never hides a lone page behind "...".
	 */
	ws = CLAMP (p - s, b + 1, n - b - 2 * s - 2);
	we = ws + 2 * s;

	for (i = 0; i < b; i++)
		push_item (items, max_items, &count, i);

	push_item (items, max_items, &count, ws == b + 1 ? b : LRG_PAGINATION_GAP);

	for (i = ws; i <= we; i++)
		push_item (items, max_items, &count, i);

	push_item (items, max_items, &count, we == n - b - 2 ? n - b - 1 : LRG_PAGINATION_GAP);

	for (i = n - b; i < n; i++)
		push_item (items, max_items, &count, i);

	return count;
}

/* ==========================================================================
 * Geometry
 * ========================================================================== */

static gfloat
metric (LrgPagination  *self,
        LrgThemeMetric  m)
{
	return lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)), m);
}

/* font_size: page numbers use the regular UI size */
static gfloat
font_size (LrgPagination *self)
{
	return metric (self, LRG_THEME_METRIC_FONT_SIZE);
}

/* button_height: one control height */
static gfloat
button_height (LrgPagination *self)
{
	return roundf (metric (self, LRG_THEME_METRIC_CONTROL_HEIGHT));
}

/* button_gap: space between neighbouring buttons */
static gfloat
button_gap (LrgPagination *self)
{
	return roundf (MAX (2.0f, metric (self, LRG_THEME_METRIC_SPACING) * 0.5f));
}

/*
 * button_width:
 *
 * Square buttons, widened for the widest page label (the label of the
 * last page) so all slots share one width and the row is stable.
 */
static gfloat
button_width (LrgPagination *self)
{
	GrlFont          *font = lrg_theme_get_ui_font (lrg_widget_get_theme (LRG_WIDGET (self)));
	g_autofree gchar *label = g_strdup_printf ("%u", MAX (self->n_pages, 1u));
	gfloat            tw = 0.0f;
	gfloat            h = button_height (self);

	lrg_paint_measure_text (font, label, font_size (self), 1.0f, &tw, NULL);
	return roundf (MAX (h, tw + metric (self, LRG_THEME_METRIC_PADDING_X)));
}

/* jump: pages a gap click or Page Up / Down moves */
static gint
jump (LrgPagination *self)
{
	return (gint) self->siblings * 2 + 1;
}

/* push_slot: append a slot of @kind showing / leading to @page */
static void
push_slot (GArray            *slots,
           LrgPaginationItem  kind,
           gint               page,
           gfloat            *x,
           gfloat             width,
           gfloat             gap)
{
	Slot slot;

	slot.kind = kind;
	slot.page = page;
	slot.x = *x;
	slot.width = width;
	g_array_append_val (slots, slot);
	*x += width + gap;
}

/* row_width: width of @n_slots buttons and the gaps between them */
static gfloat
row_width (LrgPagination *self,
           guint          n_slots)
{
	if (n_slots == 0)
		return 0.0f;
	return (gfloat) n_slots * button_width (self) + (gfloat) (n_slots - 1) * button_gap (self);
}

/*
 * build_slots:
 *
 * Rebuilds the slot cache from the properties and the current width:
 * arrows, then the computed items. The row is centred in the
 * allocation when the widget is wider than it needs to be.
 */
static void
build_slots (LrgPagination *self)
{
	guint            n_items;
	g_autofree gint *items = NULL;
	gfloat           bw = button_width (self);
	gfloat           gap = button_gap (self);
	gfloat           x;
	gint             p = (gint) self->page;
	gint             last = (gint) self->n_pages - 1;
	guint            n_slots;
	guint            i;

	n_items = lrg_pagination_compute_items (self->n_pages, self->page, self->siblings,
	                                        self->boundary, NULL, 0);
	items = g_new0 (gint, MAX (n_items, 1u));
	lrg_pagination_compute_items (self->n_pages, self->page, self->siblings, self->boundary,
	                              items, n_items);

	n_slots = n_items + (self->show_prev_next ? 2 : 0);
	x = MAX (0.0f, roundf ((lrg_widget_get_width (LRG_WIDGET (self)) - row_width (self, n_slots)) * 0.5f));

	g_array_set_size (self->slots, 0);

	if (self->show_prev_next)
		push_slot (self->slots, LRG_PAGINATION_ITEM_PREVIOUS, p > 0 ? p - 1 : -1, &x, bw, gap);

	for (i = 0; i < n_items; i++)
	{
		if (items[i] == LRG_PAGINATION_GAP)
		{
			/* A gap before the current page jumps back, after it forward */
			gboolean before = i > 0 && items[i - 1] < p;
			gint     target = before ? MAX (p - jump (self), 0) : MIN (p + jump (self), last);

			push_slot (self->slots, LRG_PAGINATION_ITEM_GAP, self->jump_on_gap ? target : -1,
			           &x, bw, gap);
		}
		else
			push_slot (self->slots, LRG_PAGINATION_ITEM_PAGE, items[i], &x, bw, gap);
	}

	if (self->show_prev_next)
		push_slot (self->slots, LRG_PAGINATION_ITEM_NEXT, p < last ? p + 1 : -1, &x, bw, gap);
}

/*
 * slot_world_rect:
 *
 * World rectangle of slot @i: local x + world origin, mirrored in
 * right-to-left; vertically centred in the widget.
 */
static void
slot_world_rect (LrgPagination *self,
                 guint          i,
                 gfloat        *x,
                 gfloat        *y,
                 gfloat        *w,
                 gfloat        *h)
{
	LrgWidget  *widget = LRG_WIDGET (self);
	const Slot *slot = &g_array_index (self->slots, Slot, i);
	gfloat      bh = button_height (self);

	*w = slot->width;
	*h = bh;
	*x = lrg_widget_mirror_x (widget, lrg_widget_get_world_x (widget) + slot->x, slot->width);
	*y = roundf (lrg_widget_get_world_y (widget) + (lrg_widget_get_height (widget) - bh) * 0.5f);
}

/* current_slot: index of the current page's slot, or -1 */
static gint
current_slot (LrgPagination *self)
{
	guint i;

	for (i = 0; i < self->slots->len; i++)
	{
		const Slot *slot = &g_array_index (self->slots, Slot, i);

		if (slot->kind == LRG_PAGINATION_ITEM_PAGE && slot->page == (gint) self->page)
			return (gint) i;
	}
	return -1;
}

/* slot_at: index of the slot under the world point, or -1 */
static gint
slot_at (LrgPagination *self,
         gfloat         px,
         gfloat         py)
{
	guint i;

	build_slots (self);
	for (i = 0; i < self->slots->len; i++)
	{
		gfloat x, y, w, h;

		slot_world_rect (self, i, &x, &y, &w, &h);
		if (px >= x && px < x + w && py >= y && py < y + h)
			return (gint) i;
	}
	return -1;
}

/* slot_active: whether a slot reacts to the pointer */
static gboolean
slot_active (const Slot *slot)
{
	return slot->page >= 0;
}

/* ensure_springs: one hover and one press spring per slot */
static void
ensure_springs (LrgPagination *self)
{
	guint old = self->hover->len;
	guint i;

	if (old >= self->slots->len)
		return;
	g_array_set_size (self->hover, self->slots->len);
	g_array_set_size (self->press, self->slots->len);
	for (i = old; i < self->slots->len; i++)
	{
		lrg_spring_init (&g_array_index (self->hover, LrgSpring, i), 0.0f, LRG_MOTION_SMOOTH);
		lrg_spring_init (&g_array_index (self->press, LrgSpring, i), 0.0f, LRG_MOTION_SNAPPY);
	}
}

/* ==========================================================================
 * Page changes
 * ========================================================================== */

/* change_page: set the page and tell everyone (no-op when unchanged) */
static gboolean
change_page (LrgPagination *self,
             gint           page)
{
	guint target;

	if (self->n_pages == 0)
		return FALSE;
	target = (guint) CLAMP (page, 0, (gint) self->n_pages - 1);
	if (target == self->page)
		return FALSE;

	self->page = target;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAGE]);
	g_signal_emit (self, signals[SIGNAL_PAGE_CHANGED], 0, self->page);
	return TRUE;
}

/* ==========================================================================
 * LrgWidget vfuncs
 * ========================================================================== */

static void
lrg_pagination_style_defaults (LrgWidget      *widget,
                               LrgUIPart       part,
                               LrgWidgetState  state,
                               LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	gfloat    radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);

	switch (part)
	{
	case LRG_UI_PART_ITEM:
		{
			/* Page buttons: transparent at rest, control shades when
			 * hovered / pressed. The resting colour is the hover colour
			 * at zero alpha so the cross-fade does not pass through grey. */
			GrlColor rest;

			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER),
			                            0.0f, &rest);
			if (state & LRG_WIDGET_STATE_PRESSED)
				lrg_part_look_set_background (look, lrg_theme_get_color (theme,
				                                                         LRG_THEME_COLOR_CONTROL_PRESSED));
			else if (state & LRG_WIDGET_STATE_HOVERED)
				lrg_part_look_set_background (look, lrg_theme_get_color (theme,
				                                                         LRG_THEME_COLOR_CONTROL_HOVER));
			else
				lrg_part_look_set_background (look, &rest);
			lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
			lrg_part_look_set_radius (look, radius);
			if (state & LRG_WIDGET_STATE_DISABLED)
				lrg_part_look_set_opacity (look, PAGINATION_DISABLED_OPACITY);
		}
		break;

	case LRG_UI_PART_INDICATOR:
		{
			/* The pill under the current page: accent with a soft glow
			 * of shadow beneath */
			GrlColor shadow;

			lrg_part_look_set_background (look, lrg_theme_get_color (theme,
			                                                         (state & LRG_WIDGET_STATE_HOVERED)
			                                                         ? LRG_THEME_COLOR_ACCENT_HOVER
			                                                         : LRG_THEME_COLOR_ACCENT));
			lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_ON_ACCENT));
			lrg_part_look_set_radius (look, radius);
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.35f, &shadow);
			lrg_part_look_set_shadow (look, &shadow,
			                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR) * 0.6f,
			                          MAX (1.0f, lrg_theme_get_metric (theme,
			                                                           LRG_THEME_METRIC_SHADOW_OFFSET_Y) * 0.5f));
			if (state & LRG_WIDGET_STATE_DISABLED)
				lrg_part_look_set_opacity (look, 0.45f);
		}
		break;

	case LRG_UI_PART_ARROW:
		/* Prev / next chevrons and the gap dots */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                                                         (state & LRG_WIDGET_STATE_HOVERED)
		                                                         ? LRG_THEME_COLOR_TEXT
		                                                         : LRG_THEME_COLOR_TEXT_MUTED));
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, PAGINATION_DISABLED_OPACITY);
		break;

	default:
		LRG_WIDGET_CLASS (lrg_pagination_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_pagination_measure (LrgWidget *widget,
                        gfloat    *width,
                        gfloat    *height)
{
	LrgPagination *self = LRG_PAGINATION (widget);
	guint          n_items;

	/* The item count only depends on the page near the ends while all
	 * pages fit, so the middle page gives the widest row */
	n_items = lrg_pagination_compute_items (self->n_pages, self->n_pages / 2, self->siblings,
	                                        self->boundary, NULL, 0);
	*width = row_width (self, n_items + (self->show_prev_next ? 2 : 0));
	*height = button_height (self);
}

/*
 * lrg_pagination_step:
 *
 * Moves each slot's hover / press spring towards the pointer state and
 * the pill towards the current page's slot. The first step snaps so a
 * new screen does not animate in.
 */
static gboolean
lrg_pagination_step (LrgWidget *widget,
                     gfloat     delta_time)
{
	LrgPagination *self = LRG_PAGINATION (widget);
	LrgMotion      motion = lrg_widget_get_motion (widget);
	gboolean       snap = !self->stepped || motion == LRG_MOTION_NONE;
	gboolean       busy = FALSE;
	gint           cur;
	guint          i;

	build_slots (self);
	ensure_springs (self);

	/* Pill target: the current page's slot (local coordinates) */
	cur = current_slot (self);
	if (cur >= 0)
	{
		const Slot *slot = &g_array_index (self->slots, Slot, cur);

		lrg_spring_set_motion (&self->pill_x, motion);
		lrg_spring_set_motion (&self->pill_w, motion);
		lrg_spring_set_target (&self->pill_x, slot->x);
		lrg_spring_set_target (&self->pill_w, slot->width);
	}

	/* Per-slot pointer springs */
	for (i = 0; i < self->hover->len; i++)
	{
		LrgSpring *h = &g_array_index (self->hover, LrgSpring, i);
		LrgSpring *p = &g_array_index (self->press, LrgSpring, i);
		gboolean   live = i < self->slots->len &&
		                  slot_active (&g_array_index (self->slots, Slot, i)) &&
		                  lrg_widget_get_enabled (widget);

		lrg_spring_set_motion (h, motion);
		lrg_spring_set_motion (p, motion == LRG_MOTION_NONE ? LRG_MOTION_NONE : LRG_MOTION_SNAPPY);
		lrg_spring_set_target (h, live && (gint) i == self->hover_slot ? 1.0f : 0.0f);
		lrg_spring_set_target (p, live && (gint) i == self->press_slot && (gint) i == self->hover_slot
		                          ? 1.0f : 0.0f);
		if (snap)
		{
			lrg_spring_snap (h, h->target);
			lrg_spring_snap (p, p->target);
		}
		else
		{
			busy |= lrg_spring_step (h, delta_time);
			busy |= lrg_spring_step (p, delta_time);
		}
	}

	if (snap)
	{
		lrg_spring_snap (&self->pill_x, self->pill_x.target);
		lrg_spring_snap (&self->pill_w, self->pill_w.target);
		self->stepped = TRUE;
		return FALSE;
	}

	busy |= lrg_spring_step (&self->pill_x, delta_time);
	busy |= lrg_spring_step (&self->pill_w, delta_time);
	return busy;
}

/* spring_value: a slot's spring, or the instant value before stepping */
static gfloat
spring_value (LrgPagination *self,
              GArray        *springs,
              guint          i,
              gboolean       instant)
{
	if (!self->stepped || i >= springs->len)
		return instant ? 1.0f : 0.0f;
	return CLAMP (g_array_index (springs, LrgSpring, i).value, 0.0f, 1.0f);
}

/* paint_text_centered: @text centred on (@cx, @cy) */
static void
paint_text_centered (LrgPagination  *self,
                     LrgPaintList   *list,
                     const gchar    *text,
                     gfloat          cx,
                     gfloat          cy,
                     const GrlColor *color)
{
	GrlFont *font = lrg_theme_get_ui_font (lrg_widget_get_theme (LRG_WIDGET (self)));
	gfloat   size = font_size (self);
	gfloat   tw = 0.0f;
	gfloat   th = 0.0f;

	lrg_paint_measure_text (font, text, size, 1.0f, &tw, &th);
	lrg_paint_list_text (list, font, text, roundf (cx - tw * 0.5f), roundf (cy - th * 0.5f),
	                     size, 1.0f, color);
}

/* pill_rect: the pill's world rectangle right now */
static void
pill_rect (LrgPagination *self,
           gfloat        *x,
           gfloat        *y,
           gfloat        *w,
           gfloat        *h)
{
	LrgWidget *widget = LRG_WIDGET (self);
	gfloat     bh = button_height (self);
	gfloat     lx;
	gfloat     lw;
	gint       cur = current_slot (self);

	if (self->stepped)
	{
		lx = self->pill_x.value;
		lw = self->pill_w.value;
	}
	else if (cur >= 0)
	{
		lx = g_array_index (self->slots, Slot, cur).x;
		lw = g_array_index (self->slots, Slot, cur).width;
	}
	else
	{
		lx = 0.0f;
		lw = 0.0f;
	}

	*w = lw;
	*h = bh;
	*x = lrg_widget_mirror_x (widget, lrg_widget_get_world_x (widget) + lx, lw);
	*y = roundf (lrg_widget_get_world_y (widget) + (lrg_widget_get_height (widget) - bh) * 0.5f);
}

/* slot_state: the base state a slot is resolved in */
static LrgWidgetState
slot_state (LrgPagination *self,
            const Slot    *slot)
{
	LrgWidgetState state = lrg_widget_get_state_flags (LRG_WIDGET (self)) & LRG_WIDGET_STATE_DISABLED;

	if (!slot_active (slot) && slot->kind != LRG_PAGINATION_ITEM_GAP)
		state |= LRG_WIDGET_STATE_DISABLED;
	return state;
}

/*
 * paint_slot_content:
 *
 * The page number, chevron or gap dots of slot @i. A page label fades
 * from the item colour to the on-accent colour as the pill covers it.
 */
static void
paint_slot_content (LrgPagination *self,
                    LrgPaintList  *list,
                    guint          i,
                    gfloat         pill_x,
                    gfloat         pill_w)
{
	LrgWidget      *widget = LRG_WIDGET (self);
	const Slot     *slot = &g_array_index (self->slots, Slot, i);
	LrgWidgetState  base = slot_state (self, slot);
	gfloat          hov = spring_value (self, self->hover, i, (gint) i == self->hover_slot);
	gfloat          x, y, w, h;
	gfloat          cx, cy;

	slot_world_rect (self, i, &x, &y, &w, &h);
	cx = x + w * 0.5f;
	cy = y + h * 0.5f;

	switch (slot->kind)
	{
	case LRG_PAGINATION_ITEM_PAGE:
		{
			g_autofree gchar *label = g_strdup_printf ("%d", slot->page + 1);
			LrgPartLook       item;
			LrgPartLook       pill;
			GrlColor          color;
			gfloat            overlap;

			/* How much of this button the pill covers right now */
			overlap = MIN (x + w, pill_x + pill_w) - MAX (x, pill_x);
			overlap = w > 0.0f ? CLAMP (overlap / w, 0.0f, 1.0f) : 0.0f;

			lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, base, &item);
			lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_INDICATOR, base, &pill);
			lrg_paint_color_mix (&item.foreground, &pill.foreground, overlap, &color);
			paint_text_centered (self, list, label, cx, cy, &color);
		}
		break;

	case LRG_PAGINATION_ITEM_GAP:
		{
			LrgPartLook rest;
			LrgPartLook lit;
			LrgPartLook look;
			gfloat      r = MAX (1.5f, roundf (font_size (self) * 0.09f));
			gint        k;

			/* Three dots; gaps that jump brighten under the pointer */
			lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ARROW, base, &rest);
			lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ARROW, base | LRG_WIDGET_STATE_HOVERED, &lit);
			lrg_part_look_lerp (&rest, &lit, slot_active (slot) ? hov : 0.0f, &look);
			for (k = -1; k <= 1; k++)
				lrg_paint_list_circle (list, cx + (gfloat) k * r * 3.0f, cy + r * 0.5f, r, &look.foreground);
		}
		break;

	case LRG_PAGINATION_ITEM_PREVIOUS:
	case LRG_PAGINATION_ITEM_NEXT:
		{
			LrgPartLook rest;
			LrgPartLook lit;
			LrgPartLook look;
			gfloat      size = roundf (h * 0.55f);
			gboolean    left = (slot->kind == LRG_PAGINATION_ITEM_PREVIOUS) != lrg_widget_is_rtl (widget);

			lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ARROW, base, &rest);
			lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ARROW, base | LRG_WIDGET_STATE_HOVERED, &lit);
			lrg_part_look_lerp (&rest, &lit, slot_active (slot) ? hov : 0.0f, &look);
			if (look.opacity < 1.0f)
				lrg_paint_list_push_opacity (list, look.opacity);
			lrg_paint_list_icon (list, left ? LRG_ICON_CHEVRON_LEFT : LRG_ICON_CHEVRON_RIGHT,
			                     roundf (cx - size * 0.5f), roundf (cy - size * 0.5f), size, 0.0f,
			                     &look.foreground);
			if (look.opacity < 1.0f)
				lrg_paint_list_pop_opacity (list);
		}
		break;

	case LRG_PAGINATION_ITEM_NONE:
	default:
		break;
	}
}

/*
 * lrg_pagination_paint:
 *
 * Three passes so layering is right: button backgrounds (hover /
 * press blended by the slot springs, squashed while pressed), the
 * accent pill on top of them, then labels and icons above the pill.
 */
static void
lrg_pagination_paint (LrgWidget    *widget,
                      LrgPaintList *list)
{
	LrgPagination *self = LRG_PAGINATION (widget);
	LrgPartLook    pill;
	gfloat         px, py, pw, ph;
	guint          i;

	build_slots (self);

	/* Pass 1: button backgrounds */
	for (i = 0; i < self->slots->len; i++)
	{
		const Slot     *slot = &g_array_index (self->slots, Slot, i);
		LrgWidgetState  base = slot_state (self, slot);
		LrgPartLook     rest;
		LrgPartLook     lit;
		LrgPartLook     down;
		LrgPartLook     look;
		gfloat          hov;
		gfloat          prs;
		gfloat          inset;
		gfloat          x, y, w, h;

		if (!slot_active (slot))
			continue;

		hov = spring_value (self, self->hover, i, (gint) i == self->hover_slot);
		prs = spring_value (self, self->press, i, (gint) i == self->press_slot && (gint) i == self->hover_slot);

		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, base, &rest);
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, base | LRG_WIDGET_STATE_HOVERED, &lit);
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM,
		                                   base | LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED, &down);
		lrg_part_look_lerp (&rest, &lit, hov, &look);
		lrg_part_look_lerp (&look, &down, prs, &look);

		slot_world_rect (self, i, &x, &y, &w, &h);
		inset = PAGINATION_SQUASH * prs;
		lrg_widget_paint_box (widget, list, &look, x + inset, y + inset, w - inset * 2.0f, h - inset * 2.0f);
	}

	/* Pass 2: the pill */
	pill_rect (self, &px, &py, &pw, &ph);
	if (pw > 0.5f && self->n_pages > 0)
	{
		gint  cur = current_slot (self);
		gfloat inset = 0.0f;

		/* The pill squashes with its button too */
		if (cur >= 0 && cur == self->press_slot)
			inset = PAGINATION_SQUASH * spring_value (self, self->press, (guint) cur, cur == self->hover_slot);
		lrg_widget_resolve_look (widget, LRG_UI_PART_INDICATOR, &pill);
		lrg_widget_paint_box (widget, list, &pill, px + inset, py + inset, pw - inset * 2.0f, ph - inset * 2.0f);
	}

	/* Pass 3: labels, chevrons and gap dots */
	for (i = 0; i < self->slots->len; i++)
		paint_slot_content (self, list, i, px, pw);
}

/* activate_slot: what a click on slot @i does */
static void
activate_slot (LrgPagination *self,
               gint           i)
{
	const Slot *slot;

	if (i < 0 || (guint) i >= self->slots->len)
		return;
	slot = &g_array_index (self->slots, Slot, i);
	if (slot_active (slot))
		change_page (self, slot->page);
}

static gboolean
lrg_pagination_handle_event (LrgWidget        *widget,
                             const LrgUIEvent *event)
{
	LrgPagination *self = LRG_PAGINATION (widget);
	gfloat         ex = lrg_ui_event_get_x (event);
	gfloat         ey = lrg_ui_event_get_y (event);
	gboolean       rtl = lrg_widget_is_rtl (widget);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		self->hover_slot = lrg_widget_contains_point (widget, ex, ey) ? slot_at (self, ex, ey) : -1;
		return self->hover_slot >= 0;

	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hover_slot = -1;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		{
			gint i;

			if (lrg_ui_event_get_button (event) != 0)
				return FALSE;
			i = slot_at (self, ex, ey);
			self->hover_slot = i;
			if (i < 0 || !slot_active (&g_array_index (self->slots, Slot, i)))
				return FALSE;
			self->press_slot = i;
			return TRUE;
		}

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		{
			gint i;
			gint pressed = self->press_slot;

			if (lrg_ui_event_get_button (event) != 0 || pressed < 0)
				return FALSE;
			self->press_slot = -1;

			/* A click lands when the release is on the pressed button */
			i = slot_at (self, ex, ey);
			if (i == pressed)
				activate_slot (self, i);
			self->hover_slot = lrg_widget_contains_point (widget, ex, ey) ? slot_at (self, ex, ey) : -1;
			return TRUE;
		}

	case LRG_UI_EVENT_KEY_DOWN:
		switch (lrg_ui_event_get_key (event))
		{
		case GRL_KEY_LEFT:
			/* Arrows are always consumed so focus does not wander off */
			change_page (self, (gint) self->page + (rtl ? 1 : -1));
			return TRUE;
		case GRL_KEY_RIGHT:
			change_page (self, (gint) self->page + (rtl ? -1 : 1));
			return TRUE;
		case GRL_KEY_HOME:
			change_page (self, 0);
			return TRUE;
		case GRL_KEY_END:
			change_page (self, (gint) self->n_pages - 1);
			return TRUE;
		case GRL_KEY_PAGE_UP:
			change_page (self, (gint) self->page - jump (self));
			return TRUE;
		case GRL_KEY_PAGE_DOWN:
			change_page (self, (gint) self->page + jump (self));
			return TRUE;
		default:
			return FALSE;
		}

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_pagination_finalize (GObject *object)
{
	LrgPagination *self = LRG_PAGINATION (object);

	g_clear_pointer (&self->slots, g_array_unref);
	g_clear_pointer (&self->hover, g_array_unref);
	g_clear_pointer (&self->press, g_array_unref);

	G_OBJECT_CLASS (lrg_pagination_parent_class)->finalize (object);
}

static void
lrg_pagination_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgPagination *self = LRG_PAGINATION (object);

	switch (prop_id)
	{
	case PROP_N_PAGES:
		g_value_set_uint (value, self->n_pages);
		break;
	case PROP_PAGE:
		g_value_set_uint (value, self->page);
		break;
	case PROP_SIBLINGS:
		g_value_set_uint (value, self->siblings);
		break;
	case PROP_BOUNDARY:
		g_value_set_uint (value, self->boundary);
		break;
	case PROP_SHOW_PREV_NEXT:
		g_value_set_boolean (value, self->show_prev_next);
		break;
	case PROP_JUMP_ON_GAP:
		g_value_set_boolean (value, self->jump_on_gap);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_pagination_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgPagination *self = LRG_PAGINATION (object);

	switch (prop_id)
	{
	case PROP_N_PAGES:
		lrg_pagination_set_n_pages (self, g_value_get_uint (value));
		break;
	case PROP_PAGE:
		lrg_pagination_set_page (self, g_value_get_uint (value));
		break;
	case PROP_SIBLINGS:
		lrg_pagination_set_siblings (self, g_value_get_uint (value));
		break;
	case PROP_BOUNDARY:
		lrg_pagination_set_boundary (self, g_value_get_uint (value));
		break;
	case PROP_SHOW_PREV_NEXT:
		lrg_pagination_set_show_prev_next (self, g_value_get_boolean (value));
		break;
	case PROP_JUMP_ON_GAP:
		lrg_pagination_set_jump_on_gap (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_pagination_class_init (LrgPaginationClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->finalize = lrg_pagination_finalize;
	object_class->get_property = lrg_pagination_get_property;
	object_class->set_property = lrg_pagination_set_property;

	widget_class->paint = lrg_pagination_paint;
	widget_class->style_defaults = lrg_pagination_style_defaults;
	widget_class->measure = lrg_pagination_measure;
	widget_class->handle_event = lrg_pagination_handle_event;
	widget_class->step = lrg_pagination_step;

	/**
	 * LrgPagination:n-pages:
	 *
	 * Number of pages. 0 shows only the (disabled) arrows.
	 */
	properties[PROP_N_PAGES] =
		g_param_spec_uint ("n-pages", NULL, NULL, 0, G_MAXINT, 1,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPagination:page:
	 *
	 * The current page, 0-based; displayed 1-based.
	 */
	properties[PROP_PAGE] =
		g_param_spec_uint ("page", NULL, NULL, 0, G_MAXINT, 0,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPagination:siblings:
	 *
	 * Pages shown on each side of the current page.
	 */
	properties[PROP_SIBLINGS] =
		g_param_spec_uint ("siblings", NULL, NULL, 0, 64, 1,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPagination:boundary:
	 *
	 * Pages always shown at the start and at the end.
	 */
	properties[PROP_BOUNDARY] =
		g_param_spec_uint ("boundary", NULL, NULL, 0, 64, 1,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPagination:show-prev-next:
	 *
	 * Whether the previous / next arrow buttons are shown.
	 */
	properties[PROP_SHOW_PREV_NEXT] =
		g_param_spec_boolean ("show-prev-next", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPagination:jump-on-gap:
	 *
	 * Whether clicking a "..." gap jumps siblings * 2 + 1 pages towards it.
	 */
	properties[PROP_JUMP_ON_GAP] =
		g_param_spec_boolean ("jump-on-gap", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgPagination::page-changed:
	 * @self: the pagination
	 * @page: the new page, 0-based
	 *
	 * Emitted when the current page changes, by the user or through
	 * lrg_pagination_set_page().
	 */
	signals[SIGNAL_PAGE_CHANGED] =
		g_signal_new ("page-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
lrg_pagination_init (LrgPagination *self)
{
	self->n_pages = 1;
	self->siblings = 1;
	self->boundary = 1;
	self->show_prev_next = TRUE;
	self->jump_on_gap = TRUE;
	self->hover_slot = -1;
	self->press_slot = -1;
	self->slots = g_array_new (FALSE, TRUE, sizeof (Slot));
	self->hover = g_array_new (FALSE, TRUE, sizeof (LrgSpring));
	self->press = g_array_new (FALSE, TRUE, sizeof (LrgSpring));
	lrg_spring_init (&self->pill_x, 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&self->pill_w, 0.0f, LRG_MOTION_SMOOTH);

	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgPagination *
lrg_pagination_new (guint n_pages)
{
	return g_object_new (LRG_TYPE_PAGINATION, "n-pages", n_pages, NULL);
}

guint
lrg_pagination_get_n_pages (LrgPagination *self)
{
	g_return_val_if_fail (LRG_IS_PAGINATION (self), 0);

	return self->n_pages;
}

void
lrg_pagination_set_n_pages (LrgPagination *self,
                            guint          n_pages)
{
	g_return_if_fail (LRG_IS_PAGINATION (self));

	if (self->n_pages == n_pages)
		return;
	self->n_pages = n_pages;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_PAGES]);

	/* Keep the page in range */
	if (n_pages > 0 && self->page >= n_pages)
		change_page (self, (gint) n_pages - 1);
	else if (n_pages == 0 && self->page != 0)
	{
		self->page = 0;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAGE]);
		g_signal_emit (self, signals[SIGNAL_PAGE_CHANGED], 0, self->page);
	}
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

guint
lrg_pagination_get_page (LrgPagination *self)
{
	g_return_val_if_fail (LRG_IS_PAGINATION (self), 0);

	return self->page;
}

void
lrg_pagination_set_page (LrgPagination *self,
                         guint          page)
{
	g_return_if_fail (LRG_IS_PAGINATION (self));

	change_page (self, (gint) MIN (page, (guint) G_MAXINT));
}

guint
lrg_pagination_get_siblings (LrgPagination *self)
{
	g_return_val_if_fail (LRG_IS_PAGINATION (self), 0);

	return self->siblings;
}

void
lrg_pagination_set_siblings (LrgPagination *self,
                             guint          siblings)
{
	g_return_if_fail (LRG_IS_PAGINATION (self));

	if (self->siblings == siblings)
		return;
	self->siblings = siblings;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SIBLINGS]);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

guint
lrg_pagination_get_boundary (LrgPagination *self)
{
	g_return_val_if_fail (LRG_IS_PAGINATION (self), 0);

	return self->boundary;
}

void
lrg_pagination_set_boundary (LrgPagination *self,
                             guint          boundary)
{
	g_return_if_fail (LRG_IS_PAGINATION (self));

	if (self->boundary == boundary)
		return;
	self->boundary = boundary;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOUNDARY]);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

gboolean
lrg_pagination_get_show_prev_next (LrgPagination *self)
{
	g_return_val_if_fail (LRG_IS_PAGINATION (self), FALSE);

	return self->show_prev_next;
}

void
lrg_pagination_set_show_prev_next (LrgPagination *self,
                                   gboolean       show)
{
	g_return_if_fail (LRG_IS_PAGINATION (self));

	show = !!show;
	if (self->show_prev_next == show)
		return;
	self->show_prev_next = show;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_PREV_NEXT]);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

gboolean
lrg_pagination_get_jump_on_gap (LrgPagination *self)
{
	g_return_val_if_fail (LRG_IS_PAGINATION (self), FALSE);

	return self->jump_on_gap;
}

void
lrg_pagination_set_jump_on_gap (LrgPagination *self,
                                gboolean       jump)
{
	g_return_if_fail (LRG_IS_PAGINATION (self));

	jump = !!jump;
	if (self->jump_on_gap == jump)
		return;
	self->jump_on_gap = jump;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_JUMP_ON_GAP]);
}

gboolean
lrg_pagination_next (LrgPagination *self)
{
	g_return_val_if_fail (LRG_IS_PAGINATION (self), FALSE);

	return change_page (self, (gint) self->page + 1);
}

gboolean
lrg_pagination_previous (LrgPagination *self)
{
	g_return_val_if_fail (LRG_IS_PAGINATION (self), FALSE);

	return change_page (self, (gint) self->page - 1);
}

LrgPaginationItem
lrg_pagination_get_item_at (LrgPagination *self,
                            gfloat         x,
                            gfloat         y,
                            gint          *page)
{
	gint i;

	g_return_val_if_fail (LRG_IS_PAGINATION (self), LRG_PAGINATION_ITEM_NONE);

	i = slot_at (self, x, y);
	if (i < 0)
	{
		if (page != NULL)
			*page = -1;
		return LRG_PAGINATION_ITEM_NONE;
	}
	if (page != NULL)
		*page = g_array_index (self->slots, Slot, i).page;
	return g_array_index (self->slots, Slot, i).kind;
}

gboolean
lrg_pagination_get_page_rect (LrgPagination *self,
                              guint          page,
                              gfloat        *x,
                              gfloat        *y,
                              gfloat        *width,
                              gfloat        *height)
{
	guint i;

	g_return_val_if_fail (LRG_IS_PAGINATION (self), FALSE);

	build_slots (self);
	for (i = 0; i < self->slots->len; i++)
	{
		const Slot *slot = &g_array_index (self->slots, Slot, i);
		gfloat      sx, sy, sw, sh;

		if (slot->kind != LRG_PAGINATION_ITEM_PAGE || slot->page != (gint) page)
			continue;
		slot_world_rect (self, i, &sx, &sy, &sw, &sh);
		if (x != NULL)
			*x = sx;
		if (y != NULL)
			*y = sy;
		if (width != NULL)
			*width = sw;
		if (height != NULL)
			*height = sh;
		return TRUE;
	}
	return FALSE;
}

gfloat
lrg_pagination_get_indicator_x (LrgPagination *self)
{
	gfloat x, y, w, h;

	g_return_val_if_fail (LRG_IS_PAGINATION (self), 0.0f);

	build_slots (self);
	pill_rect (self, &x, &y, &w, &h);
	return x;
}
