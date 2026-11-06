/* lrg-menu-bar.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMenuBar: a horizontal bar of menus built from a GMenuModel.
 *
 * The bar paints its titles itself and keeps one LrgMenu per title,
 * created on first use. A pill shape glides between titles on springs
 * (following the pointer, the keyboard cursor or the open menu). The
 * bar is the menus' owner, so their actions resolve from the bar up
 * through its ancestors to the canvas.
 *
 * Interplay with the canvas popup layer: a press outside an open menu
 * dismisses it before the press is delivered, so a press on the title
 * of the menu that was open must not reopen it ("swallow"). While the
 * left button is held after opening a menu, the bar captures the
 * pointer; it forwards moves and the release to the open menus so
 * press-drag-release picks an item in one gesture.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>

#include "lrg-menu-bar.h"
#include "lrg-canvas.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* Inset between the bar's edge and the first title */
#define BAR_PAD (4.0f)

/* Vertical inset of the gliding pill */
#define BAR_PILL_INSET (3.0f)

/* One title of the bar */
typedef struct
{
	gchar      *label;      /* display text, mnemonic removed */
	gint        mnemonic;   /* byte offset, or -1 */
	GMenuModel *submodel;   /* owned; NULL for a plain bar button */
	gchar      *action;     /* plain bar buttons */
	GVariant   *target;
	LrgMenu    *menu;       /* owned, created on first open */
	gulong      handlers[3];
	gfloat      x;          /* relative to the bar */
	gfloat      w;
} BarTitle;

struct _LrgMenuBar
{
	LrgWidget   parent_instance;

	GMenuModel *model;
	gulong      model_changed_id;
	GArray     *titles;          /* BarTitle */

	gint        hot;             /* title under the pointer / keyboard cursor */
	gint        open_index;      /* title whose menu is open */
	gint        swallow;         /* title whose press just closed its menu */
	gboolean    switching;       /* closing one menu to open another */
	gboolean    keyboard_mode;   /* show mnemonics */

	/* The gliding pill */
	LrgSpring   mark_x;
	LrgSpring   mark_w;
	LrgSpring   mark_a;
	gboolean    stepped;
};

G_DEFINE_FINAL_TYPE (LrgMenuBar, lrg_menu_bar, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_MODEL,
	PROP_OPEN_INDEX,
	N_PROPS
};

enum
{
	SIGNAL_ACTIVATED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

static void bar_rebuild (LrgMenuBar *self);

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void
bar_title_clear (gpointer data)
{
	BarTitle *t = data;
	guint     k;

	if (t->menu != NULL)
	{
		for (k = 0; k < G_N_ELEMENTS (t->handlers); k++)
			if (t->handlers[k] != 0)
				g_signal_handler_disconnect (t->menu, t->handlers[k]);
		lrg_menu_close (t->menu);
		g_clear_object (&t->menu);
	}
	g_clear_pointer (&t->label, g_free);
	g_clear_pointer (&t->action, g_free);
	g_clear_pointer (&t->target, g_variant_unref);
	g_clear_object (&t->submodel);
}

static BarTitle *
title_at (LrgMenuBar *self,
          gint        index)
{
	if (index < 0 || (guint) index >= self->titles->len)
		return NULL;
	return &g_array_index (self->titles, BarTitle, index);
}

static LrgCanvas *
find_canvas (LrgWidget *widget)
{
	LrgWidget *root = lrg_widget_get_root (widget);

	return LRG_IS_CANVAS (root) ? LRG_CANVAS (root) : NULL;
}

static gfloat
text_width (LrgTheme    *theme,
            const gchar *text,
            gfloat       size)
{
	gfloat w = 0.0f;

	if (text != NULL && text[0] != '\0')
		lrg_paint_measure_text (lrg_theme_get_ui_font (theme), text, size, 1.0f, &w, NULL);
	return w;
}

/* title_enabled: a menu title always; a plain button when its action is */
static gboolean
title_enabled (LrgMenuBar *self,
               BarTitle   *t)
{
	GAction *action;

	if (t == NULL)
		return FALSE;
	if (t->submodel != NULL)
		return TRUE;
	if (t->action == NULL)
		return FALSE;
	action = lrg_widget_lookup_action (LRG_WIDGET (self), t->action);
	return action != NULL && g_action_get_enabled (action);
}

/*
 * layout_titles:
 *
 * Places the titles side by side from the leading edge: from the left,
 * or from the right in right-to-left layouts.
 */
static void
layout_titles (LrgMenuBar *self)
{
	LrgWidget *widget = LRG_WIDGET (self);
	LrgTheme  *theme = lrg_widget_get_theme (widget);
	gfloat     size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat     pad_x = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);
	gfloat     bar_w = lrg_widget_get_width (widget);
	gboolean   rtl = lrg_widget_is_rtl (widget);
	gfloat     x = BAR_PAD;
	guint      i;

	for (i = 0; i < self->titles->len; i++)
	{
		BarTitle *t = &g_array_index (self->titles, BarTitle, i);

		t->w = ceilf (text_width (theme, t->label, size) + pad_x * 2.0f);
		t->x = rtl ? bar_w - x - t->w : x;
		x += t->w;
	}
}

/* title_index_at: the title under world point (@px, @py), or -1 */
static gint
title_index_at (LrgMenuBar *self,
                gfloat      px,
                gfloat      py)
{
	LrgWidget *widget = LRG_WIDGET (self);
	gfloat     wx = lrg_widget_get_world_x (widget);
	gfloat     wy = lrg_widget_get_world_y (widget);
	guint      i;

	if (py < wy || py >= wy + lrg_widget_get_height (widget))
		return -1;

	layout_titles (self);
	for (i = 0; i < self->titles->len; i++)
	{
		BarTitle *t = &g_array_index (self->titles, BarTitle, i);

		if (px >= wx + t->x && px < wx + t->x + t->w)
			return (gint) i;
	}
	return -1;
}

/* set_hot: move the pill's resting title */
static void
set_hot (LrgMenuBar *self,
         gint        index)
{
	if (title_at (self, index) == NULL)
		index = -1;
	if (index >= 0 && self->hot < 0 && self->mark_a.value < 0.05f)
	{
		/* Appear in place rather than sliding in from the last title */
		BarTitle *t = title_at (self, index);

		layout_titles (self);
		lrg_spring_snap (&self->mark_x, t->x);
		lrg_spring_snap (&self->mark_w, t->w);
	}
	self->hot = index;
}

/* open_menu: the open top-level menu, or NULL */
static LrgMenu *
open_menu (LrgMenuBar *self)
{
	BarTitle *t = title_at (self, self->open_index);

	return t != NULL ? t->menu : NULL;
}

/* ==========================================================================
 * Menu callbacks
 * ========================================================================== */

static void
on_menu_activated (LrgMenu     *menu,
                   const gchar *action,
                   gpointer     user_data)
{
	g_signal_emit (LRG_MENU_BAR (user_data), signals[SIGNAL_ACTIVATED], 0, action);
}

/*
 * on_state_changed:
 *
 * The canvas takes OPEN off the anchor (the bar) when it dismisses a
 * menu, before it clears hover and before the press that caused it is
 * delivered. Losing OPEN while the pointer is on the bar, without us
 * closing the menu ourselves, means a press on the bar closed it:
 * remember the title so that same press does not reopen it.
 */
static void
on_state_changed (LrgWidget      *widget,
                  LrgWidgetState  previous,
                  gpointer        user_data)
{
	LrgMenuBar     *self = LRG_MENU_BAR (widget);
	LrgWidgetState  now = lrg_widget_get_state_flags (widget);

	if ((previous & LRG_WIDGET_STATE_OPEN) && !(now & LRG_WIDGET_STATE_OPEN) &&
	    (now & LRG_WIDGET_STATE_HOVERED) && !self->switching)
		self->swallow = self->open_index;
}

/* on_menu_closed: a title's menu went away, by whatever path */
static void
on_menu_closed (LrgMenu  *menu,
                gpointer  user_data)
{
	LrgMenuBar *self = LRG_MENU_BAR (user_data);
	BarTitle   *t = title_at (self, self->open_index);

	if (t == NULL || t->menu != menu)
		return;

	/* The pill stays on the title for keyboard users; the pointer
	 * puts it back on its next move */
	if (!self->switching && !self->keyboard_mode)
		set_hot (self, -1);

	self->open_index = -1;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OPEN_INDEX]);
}

/* neighbour: the title next to @from in visual direction @visual, wrapping */
static gint
neighbour (LrgMenuBar *self,
           gint        from,
           gint        visual)
{
	gint n = (gint) self->titles->len;
	gint step = lrg_widget_is_rtl (LRG_WIDGET (self)) ? -visual : visual;

	if (n == 0)
		return -1;
	if (from < 0)
		return step > 0 ? 0 : n - 1;
	return ((from + step) % n + n) % n;
}

static void
on_menu_move_out (LrgMenu  *menu,
                  gint      direction,
                  gpointer  user_data)
{
	LrgMenuBar *self = LRG_MENU_BAR (user_data);
	gint        next = neighbour (self, self->open_index, direction);

	if (next >= 0)
		lrg_menu_bar_open_menu (self, (guint) next, TRUE);
}

/* ==========================================================================
 * Model
 * ========================================================================== */

/* append_titles: one title per item; sections are flattened */
static void
append_titles (LrgMenuBar *self,
               GMenuModel *model)
{
	gint n = g_menu_model_get_n_items (model);
	gint i;

	for (i = 0; i < n; i++)
	{
		g_autoptr(GMenuModel) section = g_menu_model_get_item_link (model, i, G_MENU_LINK_SECTION);
		g_autofree gchar     *label = NULL;
		BarTitle              t;

		if (section != NULL)
		{
			append_titles (self, section);
			continue;
		}

		memset (&t, 0, sizeof (BarTitle));
		t.mnemonic = -1;
		if (g_menu_model_get_item_attribute (model, i, G_MENU_ATTRIBUTE_LABEL, "s", &label))
			t.label = lrg_menu_split_mnemonic (label, &t.mnemonic);
		t.submodel = g_menu_model_get_item_link (model, i, G_MENU_LINK_SUBMENU);
		g_menu_model_get_item_attribute (model, i, G_MENU_ATTRIBUTE_ACTION, "s", &t.action);
		t.target = g_menu_model_get_item_attribute_value (model, i, G_MENU_ATTRIBUTE_TARGET, NULL);
		g_array_append_val (self->titles, t);
	}
}

static void
on_model_changed (GMenuModel *model,
                  gint        position,
                  gint        removed,
                  gint        added,
                  gpointer    user_data)
{
	bar_rebuild (LRG_MENU_BAR (user_data));
}

static void
bar_rebuild (LrgMenuBar *self)
{
	self->switching = TRUE;
	g_array_set_size (self->titles, 0);
	self->switching = FALSE;

	if (self->open_index >= 0)
	{
		self->open_index = -1;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OPEN_INDEX]);
	}
	self->hot = -1;
	self->swallow = -1;

	if (self->model != NULL)
		append_titles (self, self->model);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* ==========================================================================
 * LrgWidget
 * ========================================================================== */

static void
lrg_menu_bar_style_defaults (LrgWidget      *widget,
                             LrgUIPart       part,
                             LrgWidgetState  state,
                             LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_BOX:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
		break;

	case LRG_UI_PART_TRACK:
		/* Hairline along the bottom edge */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
		lrg_part_look_set_size (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		break;

	case LRG_UI_PART_ITEM:
		/* The gliding pill: hover colour, deeper while its menu is open */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme,
		                                                         (state & (LRG_WIDGET_STATE_OPEN |
		                                                                   LRG_WIDGET_STATE_PRESSED))
		                                                         ? LRG_THEME_COLOR_CONTROL_PRESSED
		                                                         : LRG_THEME_COLOR_CONTROL_HOVER));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
		break;

	case LRG_UI_PART_LABEL:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                                                         (state & LRG_WIDGET_STATE_DISABLED)
		                                                         ? LRG_THEME_COLOR_DISABLED
		                                                         : LRG_THEME_COLOR_TEXT));
		break;

	case LRG_UI_PART_FOCUS:
		{
			GrlColor none = *lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS);

			/* The pill shows the keyboard cursor; no ring */
			none.a = 0;
			lrg_part_look_set_border (look, &none, 0.0f);
		}
		break;

	default:
		LRG_WIDGET_CLASS (lrg_menu_bar_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_menu_bar_measure (LrgWidget *widget,
                      gfloat    *width,
                      gfloat    *height)
{
	LrgMenuBar *self = LRG_MENU_BAR (widget);
	LrgTheme   *theme = lrg_widget_get_theme (widget);
	gfloat      size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat      pad_x = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);
	gfloat      w = BAR_PAD * 2.0f;
	guint       i;

	for (i = 0; i < self->titles->len; i++)
		w += ceilf (text_width (theme, g_array_index (self->titles, BarTitle, i).label, size) + pad_x * 2.0f);

	*width = w;
	*height = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT);
}

/* pill_index: the title the pill rests on (the open one wins) */
static gint
pill_index (LrgMenuBar *self)
{
	return self->open_index >= 0 ? self->open_index : self->hot;
}

/*
 * lrg_menu_bar_step:
 *
 * Glides the pill to its title and fades it in or out. The first step
 * snaps. The press swallow only lives for the frame it was set in.
 */
static gboolean
lrg_menu_bar_step (LrgWidget *widget,
                   gfloat     delta_time)
{
	LrgMenuBar *self = LRG_MENU_BAR (widget);
	LrgMotion   motion = lrg_widget_get_motion (widget);
	BarTitle   *t = title_at (self, pill_index (self));
	gboolean    busy = FALSE;

	self->swallow = -1;

	lrg_spring_set_motion (&self->mark_x, motion == LRG_MOTION_NONE ? motion : LRG_MOTION_BOUNCY);
	lrg_spring_set_motion (&self->mark_w, motion == LRG_MOTION_NONE ? motion : LRG_MOTION_BOUNCY);
	lrg_spring_set_motion (&self->mark_a, motion);
	if (t != NULL)
	{
		layout_titles (self);
		lrg_spring_set_target (&self->mark_x, t->x);
		lrg_spring_set_target (&self->mark_w, t->w);
	}
	lrg_spring_set_target (&self->mark_a, t != NULL ? 1.0f : 0.0f);

	if (!self->stepped || motion == LRG_MOTION_NONE)
	{
		lrg_spring_snap (&self->mark_x, self->mark_x.target);
		lrg_spring_snap (&self->mark_w, self->mark_w.target);
		lrg_spring_snap (&self->mark_a, self->mark_a.target);
		self->stepped = TRUE;
		return FALSE;
	}

	busy |= lrg_spring_step (&self->mark_x, delta_time);
	busy |= lrg_spring_step (&self->mark_w, delta_time);
	busy |= lrg_spring_step (&self->mark_a, delta_time);
	return busy;
}

static void
lrg_menu_bar_paint (LrgWidget    *widget,
                    LrgPaintList *list)
{
	LrgMenuBar  *self = LRG_MENU_BAR (widget);
	LrgTheme    *theme = lrg_widget_get_theme (widget);
	LrgPartLook  box;
	LrgPartLook  line;
	LrgPartLook  label;
	LrgPartLook  label_off;
	gfloat       x = lrg_widget_get_world_x (widget);
	gfloat       y = lrg_widget_get_world_y (widget);
	gfloat       w = lrg_widget_get_width (widget);
	gfloat       h = lrg_widget_get_height (widget);
	gfloat       size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	GrlFont     *font = lrg_theme_get_ui_font (theme);
	gfloat       cy = y + h * 0.5f;
	guint        i;

	layout_titles (self);

	/* Bar surface and its bottom hairline */
	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, x, y, w, h);
	lrg_widget_resolve_look (widget, LRG_UI_PART_TRACK, &line);
	if (line.size > 0.0f)
		lrg_paint_list_rect (list, x, y + h - line.size, w, line.size, 0.0f, &line.background);

	/* The pill, gliding between titles */
	if (self->mark_a.value > 0.01f)
	{
		LrgPartLook pill;
		gfloat      a = CLAMP (self->mark_a.value, 0.0f, 1.0f);

		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM,
		                                   self->open_index >= 0 ? LRG_WIDGET_STATE_OPEN
		                                                         : LRG_WIDGET_STATE_HOVERED,
		                                   &pill);
		pill.background.a = (guint8) roundf ((gfloat) pill.background.a * a);
		pill.border.a = (guint8) roundf ((gfloat) pill.border.a * a);
		pill.shadow.a = (guint8) roundf ((gfloat) pill.shadow.a * a);
		lrg_widget_paint_box (widget, list, &pill, x + self->mark_x.value, y + BAR_PILL_INSET,
		                      MAX (self->mark_w.value, 0.0f), MAX (h - BAR_PILL_INSET * 2.0f, 0.0f));
	}

	/* Titles, centred in their slots */
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE, &label);
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_DISABLED, &label_off);
	for (i = 0; i < self->titles->len; i++)
	{
		BarTitle       *t = &g_array_index (self->titles, BarTitle, i);
		const GrlColor *fg = title_enabled (self, t) ? &label.foreground : &label_off.foreground;
		gfloat          tw = 0.0f;
		gfloat          th = size;
		gfloat          tx;

		if (t->label == NULL)
			continue;
		lrg_paint_measure_text (font, t->label, size, 1.0f, &tw, &th);
		tx = roundf (x + t->x + (t->w - tw) * 0.5f);
		lrg_paint_list_text (list, font, t->label, tx, roundf (cy - th * 0.5f), size, 1.0f, fg);

		/* Mnemonic underline while driven by the keyboard */
		if (self->keyboard_mode && t->mnemonic >= 0)
		{
			g_autofree gchar *before = g_strndup (t->label, (gsize) t->mnemonic);
			const gchar      *end = g_utf8_next_char (t->label + t->mnemonic);
			g_autofree gchar *ch = g_strndup (t->label + t->mnemonic, (gsize) (end - (t->label + t->mnemonic)));
			gfloat            bw = 0.0f;
			gfloat            cw = 0.0f;

			lrg_paint_measure_text (font, before, size, 1.0f, &bw, NULL);
			lrg_paint_measure_text (font, ch, size, 1.0f, &cw, NULL);
			lrg_paint_list_rect (list, roundf (tx + bw), roundf (cy + th * 0.5f), MAX (cw, 2.0f), 1.0f,
			                     0.0f, fg);
		}
	}
}

/* activate_title: open a menu title, or run a plain button's action */
static void
activate_title (LrgMenuBar *self,
                gint        index,
                gboolean    keyboard)
{
	if (index >= 0)
		lrg_menu_bar_open_menu (self, (guint) index, keyboard);
}

static gboolean
lrg_menu_bar_handle_event (LrgWidget        *widget,
                           const LrgUIEvent *event)
{
	LrgMenuBar *self = LRG_MENU_BAR (widget);
	gfloat      px = lrg_ui_event_get_x (event);
	gfloat      py = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		{
			gint idx;

			/* Captured drag over the open menus: let them track it */
			if (!lrg_widget_contains_point (widget, px, py))
			{
				if (open_menu (self) != NULL)
					lrg_menu_forward_pointer_event (open_menu (self), event);
				return TRUE;
			}

			idx = title_index_at (self, px, py);
			if (idx >= 0)
			{
				BarTitle *t = title_at (self, idx);

				self->keyboard_mode = FALSE;
				set_hot (self, idx);
				/* Hover switches menus while one is open */
				if (self->open_index >= 0 && idx != self->open_index &&
				    t != NULL && t->submodel != NULL)
					lrg_menu_bar_open_menu (self, (guint) idx, FALSE);
			}
			else if (self->open_index < 0 && !self->keyboard_mode)
				set_hot (self, -1);
			return TRUE;
		}

	case LRG_UI_EVENT_POINTER_LEAVE:
		if (self->open_index < 0 && !self->keyboard_mode)
			set_hot (self, -1);
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		{
			gint idx;

			if (lrg_ui_event_get_button (event) != 0 || !lrg_widget_contains_point (widget, px, py))
				return FALSE;
			idx = title_index_at (self, px, py);
			self->keyboard_mode = FALSE;
			if (idx < 0)
				return TRUE;
			set_hot (self, idx);

			/* A press that just closed this title's menu leaves it shut */
			if (self->swallow == idx)
				self->swallow = -1;
			else if (self->open_index == idx)
				lrg_menu_bar_close (self);
			else
				activate_title (self, idx, FALSE);
			return TRUE;
		}

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		/* Release after press-drag over a menu picks the row under it */
		if (!lrg_widget_contains_point (widget, px, py) && open_menu (self) != NULL)
			lrg_menu_forward_pointer_event (open_menu (self), event);
		return lrg_ui_event_get_button (event) == 0;

	case LRG_UI_EVENT_ACTIVATE:
		activate_title (self, self->hot >= 0 ? self->hot : 0, TRUE);
		return TRUE;

	case LRG_UI_EVENT_CANCEL:
		if (self->hot >= 0 || self->keyboard_mode)
		{
			self->keyboard_mode = FALSE;
			set_hot (self, -1);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_FOCUS_OUT:
		if (self->open_index < 0)
		{
			self->keyboard_mode = FALSE;
			if (!(lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_HOVERED))
				set_hot (self, -1);
		}
		return FALSE;

	case LRG_UI_EVENT_TEXT_INPUT:
		{
			gunichar c = g_unichar_tolower (lrg_ui_event_get_codepoint (event));
			guint    i;

			/* Typing a title's mnemonic (or first letter) opens it */
			for (i = 0; i < self->titles->len; i++)
			{
				BarTitle    *t = &g_array_index (self->titles, BarTitle, i);
				const gchar *s;

				if (t->label == NULL || t->label[0] == '\0')
					continue;
				s = t->mnemonic >= 0 ? t->label + t->mnemonic : t->label;
				if (g_unichar_tolower (g_utf8_get_char (s)) == c)
				{
					lrg_menu_bar_open_menu (self, i, TRUE);
					return TRUE;
				}
			}
			return FALSE;
		}

	case LRG_UI_EVENT_KEY_DOWN:
		switch (lrg_ui_event_get_key (event))
		{
		case GRL_KEY_LEFT:
			self->keyboard_mode = TRUE;
			set_hot (self, neighbour (self, self->hot, -1));
			return TRUE;
		case GRL_KEY_RIGHT:
			self->keyboard_mode = TRUE;
			set_hot (self, neighbour (self, self->hot, 1));
			return TRUE;
		case GRL_KEY_DOWN:
		case GRL_KEY_UP:
		case GRL_KEY_ENTER:
		case GRL_KEY_KP_ENTER:
		case GRL_KEY_SPACE:
			activate_title (self, self->hot >= 0 ? self->hot : 0, TRUE);
			return TRUE;
		case GRL_KEY_F10:
			if (self->keyboard_mode)
			{
				self->keyboard_mode = FALSE;
				set_hot (self, -1);
			}
			else
				lrg_menu_bar_focus_titles (self);
			return TRUE;
		case GRL_KEY_ESCAPE:
			if (self->hot >= 0 || self->keyboard_mode)
			{
				self->keyboard_mode = FALSE;
				set_hot (self, -1);
				return TRUE;
			}
			return FALSE;
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
lrg_menu_bar_dispose (GObject *object)
{
	LrgMenuBar *self = LRG_MENU_BAR (object);

	if (self->model != NULL && self->model_changed_id != 0)
		g_signal_handler_disconnect (self->model, self->model_changed_id);
	self->model_changed_id = 0;
	g_clear_object (&self->model);
	if (self->titles != NULL)
	{
		self->switching = TRUE;
		g_array_set_size (self->titles, 0);
	}

	G_OBJECT_CLASS (lrg_menu_bar_parent_class)->dispose (object);
}

static void
lrg_menu_bar_finalize (GObject *object)
{
	LrgMenuBar *self = LRG_MENU_BAR (object);

	g_clear_pointer (&self->titles, g_array_unref);

	G_OBJECT_CLASS (lrg_menu_bar_parent_class)->finalize (object);
}

static void
lrg_menu_bar_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgMenuBar *self = LRG_MENU_BAR (object);

	switch (prop_id)
	{
	case PROP_MODEL:
		g_value_set_object (value, self->model);
		break;
	case PROP_OPEN_INDEX:
		g_value_set_int (value, self->open_index);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_menu_bar_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgMenuBar *self = LRG_MENU_BAR (object);

	switch (prop_id)
	{
	case PROP_MODEL:
		lrg_menu_bar_set_model (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_menu_bar_class_init (LrgMenuBarClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_menu_bar_dispose;
	object_class->finalize = lrg_menu_bar_finalize;
	object_class->get_property = lrg_menu_bar_get_property;
	object_class->set_property = lrg_menu_bar_set_property;

	widget_class->style_defaults = lrg_menu_bar_style_defaults;
	widget_class->measure = lrg_menu_bar_measure;
	widget_class->paint = lrg_menu_bar_paint;
	widget_class->step = lrg_menu_bar_step;
	widget_class->handle_event = lrg_menu_bar_handle_event;

	/**
	 * LrgMenuBar:model:
	 *
	 * The bar description: one item per title, each with a submenu
	 * link, or an action for a plain bar button.
	 */
	properties[PROP_MODEL] =
		g_param_spec_object ("model", NULL, NULL, G_TYPE_MENU_MODEL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgMenuBar:open-index:
	 *
	 * The title whose menu is open, or -1.
	 */
	properties[PROP_OPEN_INDEX] =
		g_param_spec_int ("open-index", NULL, NULL, -1, G_MAXINT, -1,
		                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgMenuBar::activated:
	 * @self: the menu bar
	 * @action: (nullable): the chosen item's action name
	 *
	 * Emitted after an item of one of the bar's menus (or a plain bar
	 * button) was chosen and its action ran.
	 */
	signals[SIGNAL_ACTIVATED] =
		g_signal_new ("activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_menu_bar_init (LrgMenuBar *self)
{
	self->titles = g_array_new (FALSE, TRUE, sizeof (BarTitle));
	g_array_set_clear_func (self->titles, bar_title_clear);
	self->hot = -1;
	self->open_index = -1;
	self->swallow = -1;
	lrg_spring_init (&self->mark_x, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_init (&self->mark_w, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_init (&self->mark_a, 0.0f, LRG_MOTION_SNAPPY);

	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
	g_signal_connect (self, "state-changed", G_CALLBACK (on_state_changed), NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgMenuBar *
lrg_menu_bar_new (void)
{
	return g_object_new (LRG_TYPE_MENU_BAR, NULL);
}

LrgMenuBar *
lrg_menu_bar_new_from_model (GMenuModel *model)
{
	g_return_val_if_fail (model == NULL || G_IS_MENU_MODEL (model), NULL);

	return g_object_new (LRG_TYPE_MENU_BAR, "model", model, NULL);
}

GMenuModel *
lrg_menu_bar_get_model (LrgMenuBar *self)
{
	g_return_val_if_fail (LRG_IS_MENU_BAR (self), NULL);

	return self->model;
}

void
lrg_menu_bar_set_model (LrgMenuBar *self,
                        GMenuModel *model)
{
	g_return_if_fail (LRG_IS_MENU_BAR (self));
	g_return_if_fail (model == NULL || G_IS_MENU_MODEL (model));

	if (self->model == model)
		return;

	if (self->model != NULL && self->model_changed_id != 0)
		g_signal_handler_disconnect (self->model, self->model_changed_id);
	self->model_changed_id = 0;
	g_set_object (&self->model, model);
	if (model != NULL)
		self->model_changed_id = g_signal_connect (model, "items-changed",
		                                           G_CALLBACK (on_model_changed), self);

	bar_rebuild (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
}

guint
lrg_menu_bar_get_n_titles (LrgMenuBar *self)
{
	g_return_val_if_fail (LRG_IS_MENU_BAR (self), 0);

	return self->titles->len;
}

const gchar *
lrg_menu_bar_get_title (LrgMenuBar *self,
                        guint       index)
{
	BarTitle *t;

	g_return_val_if_fail (LRG_IS_MENU_BAR (self), NULL);

	t = title_at (self, (gint) index);
	return t != NULL ? t->label : NULL;
}

gboolean
lrg_menu_bar_get_title_rect (LrgMenuBar *self,
                             guint       index,
                             gfloat     *x,
                             gfloat     *y,
                             gfloat     *width,
                             gfloat     *height)
{
	BarTitle *t;

	g_return_val_if_fail (LRG_IS_MENU_BAR (self), FALSE);

	t = title_at (self, (gint) index);
	if (t == NULL)
		return FALSE;

	layout_titles (self);
	if (x != NULL)
		*x = lrg_widget_get_world_x (LRG_WIDGET (self)) + t->x;
	if (y != NULL)
		*y = lrg_widget_get_world_y (LRG_WIDGET (self));
	if (width != NULL)
		*width = t->w;
	if (height != NULL)
		*height = lrg_widget_get_height (LRG_WIDGET (self));
	return TRUE;
}

LrgMenu *
lrg_menu_bar_get_menu (LrgMenuBar *self,
                       guint       index)
{
	BarTitle *t;

	g_return_val_if_fail (LRG_IS_MENU_BAR (self), NULL);

	t = title_at (self, (gint) index);
	if (t == NULL || t->submodel == NULL)
		return NULL;

	if (t->menu == NULL)
	{
		const GQuark *classes;
		guint         n_classes;
		guint         c;

		t->menu = lrg_menu_new_from_model (t->submodel);
		lrg_widget_set_appear (LRG_WIDGET (t->menu), LRG_APPEAR_FLY_DOWN, 0.0f);
		classes = lrg_widget_get_classes (LRG_WIDGET (self), &n_classes);
		for (c = 0; c < n_classes; c++)
			lrg_widget_add_class (LRG_WIDGET (t->menu), g_quark_to_string (classes[c]));
		t->handlers[0] = g_signal_connect_object (t->menu, "activated", G_CALLBACK (on_menu_activated), self, 0);
		t->handlers[1] = g_signal_connect_object (t->menu, "closed", G_CALLBACK (on_menu_closed), self, 0);
		t->handlers[2] = g_signal_connect_object (t->menu, "move-out", G_CALLBACK (on_menu_move_out), self, 0);
	}
	return t->menu;
}

void
lrg_menu_bar_open_menu (LrgMenuBar *self,
                        guint       index,
                        gboolean    keyboard)
{
	BarTitle  *t;
	LrgMenu   *menu;
	LrgCanvas *canvas;
	gfloat     x;
	gfloat     y;
	gfloat     w;
	gfloat     h;

	g_return_if_fail (LRG_IS_MENU_BAR (self));

	t = title_at (self, (gint) index);
	if (t == NULL)
		return;

	canvas = find_canvas (LRG_WIDGET (self));
	if (canvas == NULL)
	{
		lrg_debug (LRG_LOG_DOMAIN_UI, "menu bar is not on a canvas; cannot open a menu");
		return;
	}

	self->keyboard_mode = keyboard;

	/* Close whatever is open first, without arming the press swallow */
	if (self->open_index >= 0)
	{
		self->switching = TRUE;
		lrg_menu_bar_close (self);
		self->switching = FALSE;
	}

	/* A plain bar button runs its action */
	if (t->submodel == NULL)
	{
		g_autofree gchar *action = g_strdup (t->action);

		if (action == NULL || !title_enabled (self, t))
			return;
		set_hot (self, (gint) index);
		lrg_widget_activate_action (LRG_WIDGET (self), action,
		                            t->target != NULL ? g_variant_ref (t->target) : NULL);
		g_signal_emit (self, signals[SIGNAL_ACTIVATED], 0, action);
		return;
	}

	menu = lrg_menu_bar_get_menu (self, index);
	lrg_menu_bar_get_title_rect (self, index, &x, &y, &w, &h);
	x -= lrg_widget_get_world_x (LRG_WIDGET (canvas));
	y -= lrg_widget_get_world_y (LRG_WIDGET (canvas));

	set_hot (self, (gint) index);
	self->open_index = (gint) index;
	lrg_menu_popup_at_rect (menu, LRG_WIDGET (self), x, y, w, h, LRG_POPUP_PLACEMENT_BELOW);
	if (!lrg_menu_get_open (menu))
	{
		self->open_index = -1;
		return;
	}
	if (keyboard)
	{
		guint i;

		for (i = 0; i < lrg_menu_get_n_entries (menu); i++)
			if (lrg_menu_get_entry_enabled (menu, i))
			{
				lrg_menu_set_highlighted (menu, (gint) i);
				break;
			}
	}
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OPEN_INDEX]);
}

void
lrg_menu_bar_close (LrgMenuBar *self)
{
	LrgMenu *menu;

	g_return_if_fail (LRG_IS_MENU_BAR (self));

	menu = open_menu (self);
	if (menu != NULL)
	{
		gboolean was = self->switching;

		/* Closing from code never arms the press swallow */
		self->switching = TRUE;
		lrg_menu_close (menu);
		self->switching = was;
	}
	if (self->open_index >= 0)
	{
		self->open_index = -1;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OPEN_INDEX]);
	}
}

gint
lrg_menu_bar_get_open_index (LrgMenuBar *self)
{
	g_return_val_if_fail (LRG_IS_MENU_BAR (self), -1);

	return self->open_index;
}

gint
lrg_menu_bar_get_highlighted (LrgMenuBar *self)
{
	g_return_val_if_fail (LRG_IS_MENU_BAR (self), -1);

	return self->hot;
}

void
lrg_menu_bar_focus_titles (LrgMenuBar *self)
{
	LrgCanvas *canvas;

	g_return_if_fail (LRG_IS_MENU_BAR (self));

	lrg_menu_bar_close (self);
	canvas = find_canvas (LRG_WIDGET (self));
	if (canvas != NULL)
		lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (self));
	self->keyboard_mode = TRUE;
	set_hot (self, self->titles->len > 0 ? 0 : -1);
}
