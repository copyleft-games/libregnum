/* lrg-menu.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMenu: a popup menu built from a GMenuModel.
 *
 * The menu is one widget: it flattens its model into a list of rows
 * (items, submenu items, separators, section headers) and paints and
 * hit-tests them itself. That keeps the highlight a single shape that
 * glides between rows on springs instead of a per-row hover fade.
 *
 * Open menus live on the canvas popup layer. A submenu is another
 * LrgMenu shown as a popup stacked above its parent, so the canvas's
 * popup stack does the bookkeeping: dismissing a menu dismisses every
 * submenu above it, a click outside the stack closes it, and focus
 * returns to the anchor (the parent menu, or the owner widget) when a
 * level closes.
 *
 * All timing (submenu hover delay, the safe-triangle grace period,
 * type-ahead reset) is counted in step() from the canvas clock, so
 * tests drive it deterministically with lrg_canvas_update().
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>

#include "lrg-menu.h"
#include "lrg-canvas.h"
#include "lrg-ui-actions.h"
#include "../paint/lrg-paint-icons.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* Inset between the menu surface and its rows */
#define MENU_PAD (4.0f)

/* Narrowest menu, so short menus still read as menus */
#define MENU_MIN_WIDTH (160.0f)

/* Default seconds the pointer rests on a submenu item before it opens */
#define MENU_SUBMENU_DELAY (0.2f)

/* Grace period while the pointer crosses rows inside the safe
 * triangle, as a multiple of the submenu delay */
#define MENU_SAFE_FACTOR (1.5f)

/* Seconds of silence after which type-ahead starts a new prefix */
#define MENU_TYPEAHEAD_RESET (1.0f)

/* ==========================================================================
 * Entry kind enum
 * ========================================================================== */

GType
lrg_menu_entry_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_MENU_ENTRY_ITEM, "LRG_MENU_ENTRY_ITEM", "item" },
			{ LRG_MENU_ENTRY_SUBMENU, "LRG_MENU_ENTRY_SUBMENU", "submenu" },
			{ LRG_MENU_ENTRY_SEPARATOR, "LRG_MENU_ENTRY_SEPARATOR", "separator" },
			{ LRG_MENU_ENTRY_HEADER, "LRG_MENU_ENTRY_HEADER", "header" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgMenuEntryKind"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Types
 * ========================================================================== */

/* One row of the menu, flattened from the model */
typedef struct
{
	LrgMenuEntryKind  kind;
	gchar            *label;        /* display text, mnemonic removed */
	gint              mnemonic;     /* byte offset into label, or -1 */
	gchar            *action;       /* "prefix.name", or NULL */
	GVariant         *target;       /* owned, nullable */
	gchar            *accel_label;  /* "Ctrl+S", or NULL */
	LrgIcon           icon;
	GMenuModel       *submodel;     /* owned, submenu rows only */
	LrgMenu          *submenu;      /* owned, created on first open */
	gfloat            y;            /* row top, relative to the menu */
	gfloat            h;            /* row height */
} MenuEntry;

struct _LrgMenu
{
	LrgWidget   parent_instance;

	GMenuModel *model;
	gulong      model_changed_id;
	GArray     *entries;           /* MenuEntry */

	/* Open state */
	LrgCanvas  *canvas;            /* weak, set while open */
	gulong      dismissed_id;
	LrgWidget  *owner;             /* weak: actions resolve from it */
	LrgMenu    *parent_menu;       /* weak: set for submenus */
	gint        child_index;       /* row whose submenu is open, or -1 */

	/* The gliding highlight: row top, row height and opacity */
	gint        hot;
	LrgSpring   hl_y;
	LrgSpring   hl_h;
	LrgSpring   hl_a;
	gboolean    stepped;           /* FALSE until the first step snaps */

	/* Pointer timers, counted down in step() */
	gint        pending_row;       /* row whose submenu state applies after the delay */
	gfloat      pending_timer;
	gboolean    deferring;         /* a pointer move inside the safe triangle waits */
	gint        deferred_hot;      /* row under the pointer then (may be -1) */
	gfloat      deferred_timer;
	gfloat      last_px;           /* previous pointer position (world) */
	gfloat      last_py;
	gboolean    have_last;
	gint        last_row;          /* row under the pointer at the last move */
	gboolean    leave_pending;     /* pointer left; settle it in step() */
	gboolean    armed;             /* a release may activate (pointer moved or pressed) */

	gfloat      submenu_delay;
	gboolean    keyboard_mode;     /* opened/driven by keys: show mnemonics */

	/* Type-ahead */
	GString    *typeahead;
	gfloat      typeahead_timer;
};

G_DEFINE_FINAL_TYPE (LrgMenu, lrg_menu, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_MODEL,
	PROP_OPEN,
	PROP_HIGHLIGHTED,
	PROP_SUBMENU_DELAY,
	N_PROPS
};

enum
{
	SIGNAL_ACTIVATED,
	SIGNAL_CLOSED,
	SIGNAL_MOVE_OUT,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

static void menu_rebuild (LrgMenu *self);
static void open_submenu (LrgMenu  *self,
                          gint      index,
                          gboolean  keyboard);
static void close_submenu (LrgMenu *self);

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static void
menu_entry_clear (gpointer data)
{
	MenuEntry *e = data;

	g_clear_pointer (&e->label, g_free);
	g_clear_pointer (&e->action, g_free);
	g_clear_pointer (&e->target, g_variant_unref);
	g_clear_pointer (&e->accel_label, g_free);
	g_clear_object (&e->submodel);
	if (e->submenu != NULL)
	{
		lrg_menu_close (e->submenu);
		g_clear_object (&e->submenu);
	}
}

static MenuEntry *
entry_at (LrgMenu *self,
          gint     index)
{
	if (index < 0 || (guint) index >= self->entries->len)
		return NULL;
	return &g_array_index (self->entries, MenuEntry, index);
}

/* root_menu: the top-level menu of a submenu chain */
static LrgMenu *
root_menu (LrgMenu *self)
{
	LrgMenu *m = self;

	while (m->parent_menu != NULL)
		m = m->parent_menu;
	return m;
}

/* action_scope: the widget actions are resolved from */
static LrgWidget *
action_scope (LrgMenu *self)
{
	LrgMenu *root = root_menu (self);

	if (root->owner != NULL)
		return root->owner;
	if (root->canvas != NULL)
		return LRG_WIDGET (root->canvas);
	return LRG_WIDGET (self);
}

/* menu_is_rtl: lay out right to left (follows the owner's direction) */
static gboolean
menu_is_rtl (LrgMenu *self)
{
	return lrg_widget_is_rtl (action_scope (self));
}

/* find_canvas: the canvas @widget lives in, or NULL */
static LrgCanvas *
find_canvas (LrgWidget *widget)
{
	LrgWidget *root = lrg_widget_get_root (widget);

	return LRG_IS_CANVAS (root) ? LRG_CANVAS (root) : NULL;
}

/* entry_action: the GAction behind a row, or NULL */
static GAction *
entry_action (LrgMenu   *self,
              MenuEntry *e)
{
	if (e->action == NULL)
		return NULL;
	return lrg_widget_lookup_action (action_scope (self), e->action);
}

/*
 * entry_accel_label:
 *
 * The shortcut text shown at the row's trailing edge: the model's
 * "accel" attribute when it has one, else the accelerator bound to the
 * row's action and target on the canvas (lrg_canvas_add_shortcut()),
 * so menus follow shortcut changes without editing the model.
 *
 * Returns: (transfer full) (nullable): the label, e.g. "Ctrl+S"
 */
static gchar *
entry_accel_label (LrgMenu   *self,
                   MenuEntry *e)
{
	LrgCanvas        *canvas;
	g_autofree gchar *accel = NULL;

	if (e->accel_label != NULL)
		return g_strdup (e->accel_label);
	if (e->action == NULL || e->kind != LRG_MENU_ENTRY_ITEM)
		return NULL;

	canvas = find_canvas (action_scope (self));
	if (canvas == NULL)
		return NULL;
	accel = lrg_canvas_get_accel_for_action (canvas, e->action, e->target);
	return accel != NULL ? lrg_accel_to_label (accel) : NULL;
}

/*
 * entry_selectable:
 *
 * Rows the highlight may rest on: submenu items always, plain items
 * when their action exists and is enabled (GTK's rule: an item whose
 * action cannot be found is insensitive).
 */
static gboolean
entry_selectable (LrgMenu   *self,
                  MenuEntry *e)
{
	GAction *action;

	if (e == NULL)
		return FALSE;
	if (e->kind == LRG_MENU_ENTRY_SUBMENU)
		return TRUE;
	if (e->kind != LRG_MENU_ENTRY_ITEM)
		return FALSE;
	action = entry_action (self, e);
	return action != NULL && g_action_get_enabled (action);
}

typedef enum
{
	MARK_NONE,
	MARK_CHECK_OFF,
	MARK_CHECK_ON,
	MARK_RADIO_OFF,
	MARK_RADIO_ON
} MarkKind;

/*
 * entry_mark:
 *
 * What to draw in the leading gutter from the action's state: a
 * boolean state without a target is a check box; a string state with a
 * string target is a radio choice, on when they are equal.
 */
static MarkKind
entry_mark (LrgMenu   *self,
            MenuEntry *e)
{
	g_autoptr(GVariant) state = NULL;
	GAction            *action = entry_action (self, e);

	if (action == NULL)
		return MARK_NONE;
	state = g_action_get_state (action);
	if (state == NULL)
		return MARK_NONE;

	if (e->target == NULL && g_variant_is_of_type (state, G_VARIANT_TYPE_BOOLEAN))
		return g_variant_get_boolean (state) ? MARK_CHECK_ON : MARK_CHECK_OFF;

	if (e->target != NULL && g_variant_is_of_type (state, G_VARIANT_TYPE_STRING) &&
	    g_variant_is_of_type (e->target, G_VARIANT_TYPE_STRING))
		return g_variant_equal (state, e->target) ? MARK_RADIO_ON : MARK_RADIO_OFF;

	return MARK_NONE;
}

/* text_size: measure @text with the theme's UI font */
static void
text_size (LrgTheme    *theme,
           const gchar *text,
           gfloat       size,
           gfloat      *w,
           gfloat      *h)
{
	gfloat tw = 0.0f;
	gfloat th = size;

	if (text != NULL && text[0] != '\0')
		lrg_paint_measure_text (lrg_theme_get_ui_font (theme), text, size, 1.0f, &tw, &th);
	if (w != NULL)
		*w = tw;
	if (h != NULL)
		*h = th;
}

/* ==========================================================================
 * Mnemonics
 * ========================================================================== */

gchar *
lrg_menu_split_mnemonic (const gchar *text,
                         gint        *mnemonic)
{
	GString     *out;
	const gchar *p;
	gint         found = -1;

	if (mnemonic != NULL)
		*mnemonic = -1;
	if (text == NULL)
		return NULL;

	/* "__" is a literal underscore; "_x" marks x (the first one wins) */
	out = g_string_sized_new (strlen (text));
	for (p = text; *p != '\0'; p++)
	{
		if (*p == '_')
		{
			if (p[1] == '_')
			{
				g_string_append_c (out, '_');
				p++;
				continue;
			}
			if (p[1] != '\0' && found < 0)
				found = (gint) out->len;
			continue;
		}
		g_string_append_c (out, *p);
	}

	if (mnemonic != NULL)
		*mnemonic = found;
	return g_string_free (out, FALSE);
}

/* entry_key_char: the lower-case character type-ahead matches first */
static gunichar
entry_key_char (MenuEntry *e)
{
	const gchar *s;

	if (e->label == NULL || e->label[0] == '\0')
		return 0;
	s = e->mnemonic >= 0 ? e->label + e->mnemonic : e->label;
	return g_unichar_tolower (g_utf8_get_char (s));
}

/* ==========================================================================
 * Building rows from the model
 * ========================================================================== */

/* last_kind: kind of the last row, or -1 for none */
static gint
last_kind (LrgMenu *self)
{
	if (self->entries->len == 0)
		return -1;
	return (gint) g_array_index (self->entries, MenuEntry, self->entries->len - 1).kind;
}

/* append_separator: one separator, never leading or doubled */
static void
append_separator (LrgMenu *self)
{
	MenuEntry e;
	gint      last = last_kind (self);

	if (last < 0 || last == LRG_MENU_ENTRY_SEPARATOR)
		return;
	memset (&e, 0, sizeof (MenuEntry));
	e.kind = LRG_MENU_ENTRY_SEPARATOR;
	e.mnemonic = -1;
	g_array_append_val (self->entries, e);
}

/*
 * append_model:
 *
 * Flattens @model into rows. Sections are fenced by separators (a
 * labelled section adds a header row); submenu links become submenu
 * rows whose model is kept for when the submenu opens.
 */
static void
append_model (LrgMenu    *self,
              GMenuModel *model)
{
	gint     n = g_menu_model_get_n_items (model);
	gboolean after_section = FALSE;
	gint     i;

	for (i = 0; i < n; i++)
	{
		g_autoptr(GMenuModel) section = g_menu_model_get_item_link (model, i, G_MENU_LINK_SECTION);
		g_autoptr(GMenuModel) submenu = NULL;
		g_autoptr(GVariant)   icon = NULL;
		g_autofree gchar     *label = NULL;
		g_autofree gchar     *accel = NULL;
		MenuEntry             e;

		/* Sections: separator, optional header, the items, separator */
		if (section != NULL)
		{
			append_separator (self);
			if (g_menu_model_get_item_attribute (model, i, G_MENU_ATTRIBUTE_LABEL, "s", &label))
			{
				memset (&e, 0, sizeof (MenuEntry));
				e.kind = LRG_MENU_ENTRY_HEADER;
				e.label = lrg_menu_split_mnemonic (label, NULL);
				e.mnemonic = -1;
				g_array_append_val (self->entries, e);
			}
			append_model (self, section);
			after_section = TRUE;
			continue;
		}

		if (after_section)
		{
			append_separator (self);
			after_section = FALSE;
		}

		memset (&e, 0, sizeof (MenuEntry));
		e.kind = LRG_MENU_ENTRY_ITEM;
		e.mnemonic = -1;

		if (g_menu_model_get_item_attribute (model, i, G_MENU_ATTRIBUTE_LABEL, "s", &label))
			e.label = lrg_menu_split_mnemonic (label, &e.mnemonic);
		g_menu_model_get_item_attribute (model, i, G_MENU_ATTRIBUTE_ACTION, "s", &e.action);
		e.target = g_menu_model_get_item_attribute_value (model, i, G_MENU_ATTRIBUTE_TARGET, NULL);
		if (g_menu_model_get_item_attribute (model, i, "accel", "s", &accel))
			e.accel_label = lrg_accel_to_label (accel);

		/* "icon" holds an LrgIcon nick ("gear", "folder") */
		icon = g_menu_model_get_item_attribute_value (model, i, G_MENU_ATTRIBUTE_ICON,
		                                              G_VARIANT_TYPE_STRING);
		e.icon = icon != NULL ? lrg_paint_icon_from_name (g_variant_get_string (icon, NULL))
		                      : LRG_ICON_NONE;

		submenu = g_menu_model_get_item_link (model, i, G_MENU_LINK_SUBMENU);
		if (submenu != NULL)
		{
			e.kind = LRG_MENU_ENTRY_SUBMENU;
			e.submodel = g_steal_pointer (&submenu);
		}

		g_array_append_val (self->entries, e);
	}
}

/* on_model_changed: the model was edited; rebuild the rows */
static void
on_model_changed (GMenuModel *model,
                  gint        position,
                  gint        removed,
                  gint        added,
                  gpointer    user_data)
{
	menu_rebuild (LRG_MENU (user_data));
}

/*
 * menu_rebuild:
 *
 * Re-flattens the model. Nested section and submenu models are watched
 * through the top-level one only (GMenu emits items-changed on the menu
 * that changed), so a change inside a section reaches us on the next
 * rebuild; edit menus through the top-level model or call
 * lrg_menu_set_model() again.
 */
static void
menu_rebuild (LrgMenu *self)
{
	guint n;

	close_submenu (self);
	self->hot = -1;
	self->pending_row = -1;
	self->deferring = FALSE;
	g_array_set_size (self->entries, 0);

	if (self->model != NULL)
		append_model (self, self->model);

	/* No trailing separator */
	n = self->entries->len;
	if (n > 0 && g_array_index (self->entries, MenuEntry, n - 1).kind == LRG_MENU_ENTRY_SEPARATOR)
		g_array_set_size (self->entries, n - 1);

	lrg_widget_queue_layout (LRG_WIDGET (self));

	/* An open menu takes its new size right away */
	if (self->canvas != NULL)
	{
		gfloat pw;
		gfloat ph;

		lrg_widget_get_preferred_size (LRG_WIDGET (self), &pw, &ph);
		lrg_widget_allocate (LRG_WIDGET (self), lrg_widget_get_x (LRG_WIDGET (self)),
		                     lrg_widget_get_y (LRG_WIDGET (self)), pw, ph);
	}
}

/* ==========================================================================
 * Geometry
 * ========================================================================== */

typedef struct
{
	gfloat size;      /* label font size */
	gfloat small;     /* header font size */
	gfloat pad_x;     /* text inset inside a row */
	gfloat gutter;    /* leading column for marks and icons */
	gfloat arrow;     /* trailing column for the submenu chevron */
	gfloat spacing;
} MenuMetrics;

static void
menu_metrics (LrgMenu     *self,
              MenuMetrics *m)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));

	m->size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	m->small = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	m->pad_x = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);
	m->spacing = lrg_theme_get_metric (theme, LRG_THEME_METRIC_SPACING);
	m->gutter = m->size + m->spacing * 0.5f;
	m->arrow = m->size;
}

/*
 * layout_rows:
 *
 * Stacks the rows top to bottom: items at ROW_HEIGHT, headers a little
 * shorter, separators a thin band. Positions are relative to the menu.
 */
static void
layout_rows (LrgMenu *self)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	gfloat    row_h = lrg_theme_get_metric (theme, LRG_THEME_METRIC_ROW_HEIGHT);
	gfloat    spacing = lrg_theme_get_metric (theme, LRG_THEME_METRIC_SPACING);
	gfloat    y = MENU_PAD;
	guint     i;

	for (i = 0; i < self->entries->len; i++)
	{
		MenuEntry *e = &g_array_index (self->entries, MenuEntry, i);

		switch (e->kind)
		{
		case LRG_MENU_ENTRY_SEPARATOR:
			e->h = MAX (5.0f, spacing + 1.0f);
			break;
		case LRG_MENU_ENTRY_HEADER:
			e->h = roundf (row_h * 0.8f);
			break;
		default:
			e->h = row_h;
			break;
		}
		e->y = y;
		y += e->h;
	}
}

/* row_at: the row under world point (@x, @y), or -1 */
static gint
row_at (LrgMenu *self,
        gfloat   x,
        gfloat   y)
{
	gfloat wx = lrg_widget_get_world_x (LRG_WIDGET (self));
	gfloat wy = lrg_widget_get_world_y (LRG_WIDGET (self));
	gfloat w = lrg_widget_get_width (LRG_WIDGET (self));
	guint  i;

	if (x < wx + MENU_PAD || x >= wx + w - MENU_PAD)
		return -1;

	layout_rows (self);
	for (i = 0; i < self->entries->len; i++)
	{
		MenuEntry *e = &g_array_index (self->entries, MenuEntry, i);

		if (y >= wy + e->y && y < wy + e->y + e->h)
			return (gint) i;
	}
	return -1;
}

/* ==========================================================================
 * Highlight and timers
 * ========================================================================== */

/* chain_has_focus: the canvas focus is somewhere in this menu chain */
static gboolean
chain_has_focus (LrgMenu *self)
{
	LrgCanvas *canvas = self->canvas;
	LrgWidget *focus;
	LrgMenu   *m;

	if (canvas == NULL)
		return FALSE;
	focus = lrg_canvas_get_focused_widget (canvas);
	for (m = root_menu (self); m != NULL;)
	{
		MenuEntry *child;

		if (focus == LRG_WIDGET (m))
			return TRUE;
		child = entry_at (m, m->child_index);
		m = child != NULL ? child->submenu : NULL;
	}
	return FALSE;
}

/*
 * set_hot:
 *
 * Moves the highlight to @index (-1 clears it). Coming from nothing,
 * the highlight appears in place instead of sliding in from the last
 * row it sat on.
 */
static void
set_hot (LrgMenu *self,
         gint     index)
{
	MenuEntry *e = entry_at (self, index);

	if (e == NULL || !entry_selectable (self, e))
		index = -1;
	if (index == self->hot)
		return;

	if (index >= 0)
	{
		layout_rows (self);
		e = entry_at (self, index);
		if (self->hot < 0 || self->hl_a.value < 0.05f)
		{
			lrg_spring_snap (&self->hl_y, e->y);
			lrg_spring_snap (&self->hl_h, e->h);
		}
		lrg_spring_set_target (&self->hl_y, e->y);
		lrg_spring_set_target (&self->hl_h, e->h);
	}

	self->hot = index;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HIGHLIGHTED]);
}

/*
 * apply_pending:
 *
 * The pointer rested on @row for the submenu delay: open its submenu
 * (a submenu row), or close the submenu open from another row. A
 * disabled row also drops the highlight it kept on the submenu item.
 */
static void
apply_pending (LrgMenu *self,
               gint     row)
{
	MenuEntry *e = entry_at (self, row);

	if (e == NULL)
		return;
	if (e->kind == LRG_MENU_ENTRY_SUBMENU && self->hot == row)
	{
		if (self->child_index != row)
			open_submenu (self, row, FALSE);
		return;
	}
	if (row != self->child_index)
		close_submenu (self);
	if (!entry_selectable (self, e))
		set_hot (self, -1);
}

/* schedule_pending: apply @row's submenu state after the delay */
static void
schedule_pending (LrgMenu *self,
                  gint     row)
{
	self->pending_row = row;
	self->pending_timer = self->submenu_delay;
	if (self->submenu_delay <= 0.0f)
	{
		self->pending_row = -1;
		apply_pending (self, row);
	}
}

/*
 * hover_row:
 *
 * The pointer is over @index. Highlight it and schedule the submenu
 * change: open its submenu, or close the one that is open, after the
 * submenu delay (immediately with a zero delay). Over a row that
 * cannot be highlighted, an open submenu's item stays lit until the
 * delay closes it; over the padding nothing changes.
 */
static void
hover_row (LrgMenu *self,
           gint     index)
{
	MenuEntry *e = entry_at (self, index);

	self->deferring = FALSE;

	if (e == NULL)
	{
		set_hot (self, self->child_index >= 0 ? self->child_index : -1);
		self->pending_row = -1;
		return;
	}

	if (!entry_selectable (self, e))
	{
		if (self->child_index >= 0)
		{
			set_hot (self, self->child_index);
			schedule_pending (self, index);
		}
		else
		{
			set_hot (self, -1);
			self->pending_row = -1;
		}
		return;
	}

	set_hot (self, index);
	if (self->child_index == index)
		self->pending_row = -1;
	else if (e->kind == LRG_MENU_ENTRY_SUBMENU || self->child_index >= 0)
		schedule_pending (self, index);
}

/*
 * in_safe_triangle:
 *
 * The "safe triangle": while a submenu is open, the pointer heading
 * from its last position towards the submenu travels through the
 * triangle spanned by that position and the submenu's near edge.
 * Rows crossed on the way must not steal the highlight (and close the
 * submenu), so moves inside it are deferred.
 */
static gboolean
in_safe_triangle (LrgMenu *self,
                  gfloat   px,
                  gfloat   py)
{
	MenuEntry *child = entry_at (self, self->child_index);
	LrgWidget *sub;
	gfloat     sx;
	gfloat     sw;
	gfloat     top;
	gfloat     bottom;
	gfloat     nx;
	gfloat     ax = self->last_px;
	gfloat     ay = self->last_py;
	gfloat     e1;
	gfloat     e2;
	gfloat     e3;
	gboolean   neg;
	gboolean   pos;

	if (child == NULL || child->submenu == NULL || !self->have_last)
		return FALSE;

	sub = LRG_WIDGET (child->submenu);
	sx = lrg_widget_get_world_x (sub);
	sw = lrg_widget_get_width (sub);
	top = lrg_widget_get_world_y (sub);
	bottom = top + lrg_widget_get_height (sub);

	/* Near edge: the submenu side facing this menu */
	nx = sx >= lrg_widget_get_world_x (LRG_WIDGET (self)) ? sx : sx + sw;

	/* Point in triangle (apex a = last pointer position, b = near top,
	 * c = near bottom): the point is inside when the cross products of
	 * the three edges a->b, b->c, c->a with it all have the same sign */
	e1 = (nx - ax) * (py - ay) - (top - ay) * (px - ax);
	e2 = (nx - nx) * (py - top) - (bottom - top) * (px - nx);
	e3 = (ax - nx) * (py - bottom) - (ay - bottom) * (px - nx);

	neg = e1 < 0.0f || e2 < 0.0f || e3 < 0.0f;
	pos = e1 > 0.0f || e2 > 0.0f || e3 > 0.0f;
	return !(neg && pos);
}

/* move_hot: keyboard step through selectable rows, wrapping */
static void
move_hot (LrgMenu *self,
          gint     direction)
{
	gint n = (gint) self->entries->len;
	gint i = self->hot;
	gint k;

	if (n == 0)
		return;
	if (i < 0)
		i = direction > 0 ? -1 : n;

	for (k = 0; k < n; k++)
	{
		i = ((i + direction) % n + n) % n;
		if (entry_selectable (self, entry_at (self, i)))
		{
			if (i != self->hot)
				close_submenu (self);
			set_hot (self, i);
			return;
		}
	}
}

/* first_selectable: index of the first row the highlight can rest on */
static gint
first_selectable (LrgMenu *self)
{
	guint i;

	for (i = 0; i < self->entries->len; i++)
		if (entry_selectable (self, entry_at (self, (gint) i)))
			return (gint) i;
	return -1;
}

/* ==========================================================================
 * Opening and closing
 * ========================================================================== */

/* forget_open: the canvas dismissed this menu; reset its open state */
static void
forget_open (LrgMenu *self)
{
	LrgMenu *parent = self->parent_menu;

	if (self->canvas != NULL && self->dismissed_id != 0)
		g_signal_handler_disconnect (self->canvas, self->dismissed_id);
	self->dismissed_id = 0;
	g_clear_weak_pointer (&self->canvas);
	g_clear_weak_pointer (&self->owner);

	self->child_index = -1;
	self->pending_row = -1;
	self->deferring = FALSE;
	self->armed = FALSE;
	self->have_last = FALSE;
	self->last_row = -2;
	self->leave_pending = FALSE;
	set_hot (self, -1);

	/* Tell the parent its submenu is gone */
	if (parent != NULL)
	{
		MenuEntry *pe = entry_at (parent, parent->child_index);

		if (pe != NULL && pe->submenu == self)
			parent->child_index = -1;
	}

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OPEN]);
	g_signal_emit (self, signals[SIGNAL_CLOSED], 0);
}

static void
on_popup_dismissed (LrgCanvas *canvas,
                    LrgWidget *popup,
                    gpointer   user_data)
{
	LrgMenu *self = LRG_MENU (user_data);

	if (popup == LRG_WIDGET (self))
		forget_open (self);
}

/*
 * place_menu:
 *
 * Positions the open menu next to the anchor rectangle (canvas
 * coordinates) with the canvas placement maths (flip, clamp). Below
 * and above placements align the leading edge: the left edge, or the
 * right edge in right-to-left layouts.
 */
static void
place_menu (LrgMenu           *self,
            gfloat             ax,
            gfloat             ay,
            gfloat             aw,
            gfloat             ah,
            LrgPopupPlacement  placement)
{
	LrgWidget *canvas = LRG_WIDGET (self->canvas);
	gfloat     cw = lrg_widget_get_width (canvas);
	gfloat     ch = lrg_widget_get_height (canvas);
	gfloat     pw;
	gfloat     ph;
	gfloat     x;
	gfloat     y;
	gboolean   rtl = menu_is_rtl (self);

	lrg_widget_queue_layout (LRG_WIDGET (self));
	lrg_widget_get_preferred_size (LRG_WIDGET (self), &pw, &ph);

	if (placement == LRG_POPUP_PLACEMENT_AT_POINTER)
	{
		/* Context menu: open away from the point, flip on each axis */
		x = rtl ? ax - pw : ax;
		if (!rtl && x + pw > cw - 8.0f && ax - pw >= 8.0f)
			x = ax - pw;
		else if (rtl && x < 8.0f && ax + pw <= cw - 8.0f)
			x = ax;
		y = ay;
		if (y + ph > ch - 8.0f && ay - ph >= 8.0f)
			y = ay - ph;
		x = CLAMP (x, 8.0f, MAX (8.0f, cw - pw - 8.0f));
		y = CLAMP (y, 8.0f, MAX (8.0f, ch - ph - 8.0f));
	}
	else
	{
		lrg_canvas_place_popup (cw, ch, ax, ay, aw, ah, pw, ph, placement, &x, &y);
		if (rtl && (placement == LRG_POPUP_PLACEMENT_BELOW || placement == LRG_POPUP_PLACEMENT_ABOVE))
			x = CLAMP (ax + aw - pw, 8.0f, MAX (8.0f, cw - pw - 8.0f));
	}

	lrg_widget_allocate (LRG_WIDGET (self), roundf (x), roundf (y), pw, ph);
}

/*
 * show_menu:
 *
 * Shows @self on @canvas's popup layer (anchored to @anchor for the
 * canvas's OPEN flag, focus return and the close animation), then
 * places it against the anchor rectangle.
 */
static void
show_menu (LrgMenu           *self,
           LrgCanvas         *canvas,
           LrgWidget         *owner,
           LrgWidget         *anchor,
           gfloat             ax,
           gfloat             ay,
           gfloat             aw,
           gfloat             ah,
           LrgPopupPlacement  placement,
           gboolean           keyboard)
{
	g_object_ref (self);

	if (self->canvas != NULL)
		lrg_menu_close (self);

	g_set_weak_pointer (&self->canvas, canvas);
	g_set_weak_pointer (&self->owner, owner);
	self->dismissed_id = g_signal_connect_object (canvas, "popup-dismissed",
	                                              G_CALLBACK (on_popup_dismissed), self, 0);
	self->child_index = -1;
	self->hot = -1;
	self->pending_row = -1;
	self->deferring = FALSE;
	self->armed = FALSE;
	self->have_last = FALSE;
	self->last_row = -2;
	self->leave_pending = FALSE;
	self->keyboard_mode = keyboard;
	self->stepped = FALSE;
	g_string_truncate (self->typeahead, 0);

	if (lrg_widget_get_appear (LRG_WIDGET (self)) == LRG_APPEAR_NONE)
		lrg_widget_set_appear (LRG_WIDGET (self), LRG_APPEAR_POP, 0.0f);

	lrg_widget_set_size (LRG_WIDGET (self), 0.0f, 0.0f);
	lrg_canvas_show_popup (canvas, LRG_WIDGET (self), anchor, LRG_POPUP_PLACEMENT_AT_POINTER, FALSE);
	place_menu (self, ax, ay, aw, ah, placement);

	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (self));
	if (keyboard)
		set_hot (self, first_selectable (self));

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OPEN]);
	g_object_unref (self);
}

/* close_submenu: close the submenu open from this menu, if any */
static void
close_submenu (LrgMenu *self)
{
	MenuEntry *e = entry_at (self, self->child_index);

	self->child_index = -1;
	if (e != NULL && e->submenu != NULL)
		lrg_menu_close (e->submenu);
}

/*
 * open_submenu:
 *
 * Opens row @index's submenu beside it: to the right (left in
 * right-to-left), flipping when it does not fit, with its first row
 * level with the item. From the keyboard it takes focus and
 * highlights its first row.
 */
static void
open_submenu (LrgMenu  *self,
              gint      index,
              gboolean  keyboard)
{
	MenuEntry *e = entry_at (self, index);
	LrgWidget *canvas_w;
	gfloat     cx;
	gfloat     cy;
	gfloat     ix;
	gfloat     iy;
	gfloat     iw;
	gfloat     ih;
	gboolean   rtl;

	if (e == NULL || e->kind != LRG_MENU_ENTRY_SUBMENU || self->canvas == NULL)
		return;

	if (self->child_index == index && e->submenu != NULL && e->submenu->canvas != NULL)
	{
		if (keyboard)
		{
			lrg_canvas_set_focused_widget (self->canvas, LRG_WIDGET (e->submenu));
			e->submenu->keyboard_mode = TRUE;
			set_hot (e->submenu, first_selectable (e->submenu));
		}
		return;
	}
	close_submenu (self);

	/* Submenus are made on first use and keep the parent's classes */
	if (e->submenu == NULL)
	{
		const GQuark *classes;
		guint         n_classes;
		guint         c;

		e->submenu = lrg_menu_new_from_model (e->submodel);
		g_set_weak_pointer (&e->submenu->parent_menu, self);
		classes = lrg_widget_get_classes (LRG_WIDGET (self), &n_classes);
		for (c = 0; c < n_classes; c++)
			lrg_widget_add_class (LRG_WIDGET (e->submenu), g_quark_to_string (classes[c]));
	}
	e->submenu->submenu_delay = self->submenu_delay;

	/* The item's row in canvas coordinates, widened to the surface
	 * edge so the submenu sits flush against this menu */
	canvas_w = LRG_WIDGET (self->canvas);
	cx = lrg_widget_get_world_x (canvas_w);
	cy = lrg_widget_get_world_y (canvas_w);
	lrg_menu_get_entry_rect (self, (guint) index, &ix, &iy, &iw, &ih);
	ix = lrg_widget_get_world_x (LRG_WIDGET (self)) - cx;
	iw = lrg_widget_get_width (LRG_WIDGET (self));
	iy -= cy;
	rtl = menu_is_rtl (self);

	self->child_index = index;
	self->pending_row = -1;
	set_hot (self, index);

	show_menu (e->submenu, self->canvas, NULL, LRG_WIDGET (self),
	           ix, iy - MENU_PAD, iw, ih,
	           rtl ? LRG_POPUP_PLACEMENT_LEFT : LRG_POPUP_PLACEMENT_RIGHT, keyboard);

	/* show_menu() focused the submenu; a pointer-opened one leaves the
	 * keyboard where it was */
	if (!keyboard)
		lrg_canvas_set_focused_widget (self->canvas, LRG_WIDGET (self));
}

/*
 * activate_row:
 *
 * Runs row @index: a submenu row opens its submenu; an item closes the
 * whole chain first (so handlers see a settled UI), then activates its
 * action on the owner and emits ::activated on the top-level menu.
 */
static gboolean
activate_row (LrgMenu  *self,
              gint      index,
              gboolean  keyboard)
{
	MenuEntry          *e = entry_at (self, index);
	LrgMenu            *root;
	g_autoptr(LrgWidget) scope = NULL;
	g_autofree gchar   *action = NULL;
	g_autoptr(GVariant) target = NULL;

	if (e == NULL || !entry_selectable (self, e))
		return FALSE;

	if (e->kind == LRG_MENU_ENTRY_SUBMENU)
	{
		open_submenu (self, index, keyboard);
		return TRUE;
	}

	root = g_object_ref (root_menu (self));
	scope = g_object_ref (action_scope (self));
	action = g_strdup (e->action);
	target = e->target != NULL ? g_variant_ref (e->target) : NULL;

	lrg_menu_close (root);
	lrg_widget_activate_action (scope, action, target);
	g_signal_emit (root, signals[SIGNAL_ACTIVATED], 0, action);

	g_object_unref (root);
	return TRUE;
}

/* ==========================================================================
 * LrgWidget
 * ========================================================================== */

static void
lrg_menu_style_defaults (LrgWidget      *widget,
                         LrgUIPart       part,
                         LrgWidgetState  state,
                         LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_POPUP:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_POPUP_RADIUS));
		lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y));
		break;

	case LRG_UI_PART_ITEM:
		/* The gliding highlight is painted from the hovered look; rows
		 * themselves are flat. Disabled rows grey their text. */
		if (state & LRG_WIDGET_STATE_PRESSED)
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_PRESSED));
		else
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                                                         (state & LRG_WIDGET_STATE_DISABLED)
		                                                         ? LRG_THEME_COLOR_DISABLED
		                                                         : LRG_THEME_COLOR_TEXT));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
		break;

	case LRG_UI_PART_LABEL:
		/* Accelerator text */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                                                         (state & LRG_WIDGET_STATE_DISABLED)
		                                                         ? LRG_THEME_COLOR_DISABLED
		                                                         : LRG_THEME_COLOR_TEXT_MUTED));
		break;

	case LRG_UI_PART_HEADER:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_size (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL));
		break;

	case LRG_UI_PART_MARK:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		break;

	case LRG_UI_PART_ARROW:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		break;

	case LRG_UI_PART_TRACK:
		/* Separator line */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
		break;

	case LRG_UI_PART_FOCUS:
		{
			GrlColor none = *lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS);

			/* The highlight shows keyboard position; no ring */
			none.a = 0;
			lrg_part_look_set_border (look, &none, 0.0f);
		}
		break;

	default:
		LRG_WIDGET_CLASS (lrg_menu_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_menu_measure (LrgWidget *widget,
                  gfloat    *width,
                  gfloat    *height)
{
	LrgMenu     *self = LRG_MENU (widget);
	LrgTheme    *theme = lrg_widget_get_theme (widget);
	MenuMetrics  m;
	gfloat       label_w = 0.0f;
	gfloat       accel_w = 0.0f;
	gfloat       header_w = 0.0f;
	gboolean     has_sub = FALSE;
	gfloat       w;
	guint        i;

	menu_metrics (self, &m);
	layout_rows (self);

	/* Widest label, accelerator and header */
	for (i = 0; i < self->entries->len; i++)
	{
		MenuEntry *e = &g_array_index (self->entries, MenuEntry, i);
		gfloat     tw;

		if (e->kind == LRG_MENU_ENTRY_HEADER)
		{
			text_size (theme, e->label, m.small, &tw, NULL);
			header_w = MAX (header_w, tw);
			continue;
		}
		if (e->kind == LRG_MENU_ENTRY_SEPARATOR)
			continue;

		text_size (theme, e->label, m.size, &tw, NULL);
		label_w = MAX (label_w, tw);
		{
			g_autofree gchar *accel = entry_accel_label (self, e);

			if (accel != NULL)
			{
				text_size (theme, accel, m.size, &tw, NULL);
				accel_w = MAX (accel_w, tw);
			}
		}
		if (e->kind == LRG_MENU_ENTRY_SUBMENU)
			has_sub = TRUE;
	}

	/* pad | pad_x | gutter | label | gap | accel | arrow | pad_x | pad */
	w = label_w;
	if (accel_w > 0.0f)
		w += m.spacing * 4.0f + accel_w;
	if (has_sub)
		w += m.spacing + m.arrow;
	w = MAX (w + m.gutter, header_w);
	w += (MENU_PAD + m.pad_x) * 2.0f;

	*width = ceilf (MAX (w, MENU_MIN_WIDTH));
	if (self->entries->len == 0)
		*height = MENU_PAD * 2.0f;
	else
	{
		MenuEntry *last = &g_array_index (self->entries, MenuEntry, self->entries->len - 1);

		*height = last->y + last->h + MENU_PAD;
	}
}

/*
 * lrg_menu_step:
 *
 * Glides the highlight towards the hot row and runs the pointer
 * timers: the submenu open/close delay, the safe-triangle grace period
 * and the type-ahead reset. The first step after opening snaps.
 *
 * Returns: %TRUE while anything is still moving or counting down
 */
static gboolean
lrg_menu_step (LrgWidget *widget,
               gfloat     delta_time)
{
	LrgMenu   *self = LRG_MENU (widget);
	LrgMotion  motion = lrg_widget_get_motion (widget);
	MenuEntry *e = entry_at (self, self->hot);
	gboolean   busy = FALSE;

	/* The pointer really left (still not hovering a frame later):
	 * keep an open submenu's item lit, clear otherwise */
	if (self->leave_pending)
	{
		self->leave_pending = FALSE;
		if (!(lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_HOVERED))
		{
			self->deferring = FALSE;
			self->last_row = -2;
			if (self->child_index >= 0)
				set_hot (self, self->child_index);
			else if (!self->keyboard_mode)
			{
				set_hot (self, -1);
				self->pending_row = -1;
			}
		}
	}
	e = entry_at (self, self->hot);

	/* Highlight targets */
	lrg_spring_set_motion (&self->hl_y, motion);
	lrg_spring_set_motion (&self->hl_h, motion);
	if (e != NULL)
	{
		layout_rows (self);
		lrg_spring_set_target (&self->hl_y, e->y);
		lrg_spring_set_target (&self->hl_h, e->h);
	}
	lrg_spring_set_target (&self->hl_a, e != NULL ? 1.0f : 0.0f);

	if (!self->stepped || motion == LRG_MOTION_NONE)
	{
		lrg_spring_snap (&self->hl_y, self->hl_y.target);
		lrg_spring_snap (&self->hl_h, self->hl_h.target);
		lrg_spring_snap (&self->hl_a, self->hl_a.target);
		self->stepped = TRUE;
	}
	else
	{
		busy |= lrg_spring_step (&self->hl_y, delta_time);
		busy |= lrg_spring_step (&self->hl_h, delta_time);
		busy |= lrg_spring_step (&self->hl_a, delta_time);
	}

	/* Safe-triangle grace: the pointer stopped on a crossed row */
	if (self->deferring)
	{
		self->deferred_timer -= delta_time;
		busy = TRUE;
		if (self->deferred_timer <= 0.0f)
			hover_row (self, self->deferred_hot);
	}

	/* Submenu hover delay */
	if (self->pending_row >= 0)
	{
		self->pending_timer -= delta_time;
		busy = TRUE;
		if (self->pending_timer <= 0.0f)
		{
			gint row = self->pending_row;

			self->pending_row = -1;
			apply_pending (self, row);
		}
	}

	/* Type-ahead forgets its prefix after a pause */
	if (self->typeahead->len > 0)
	{
		self->typeahead_timer -= delta_time;
		if (self->typeahead_timer <= 0.0f)
			g_string_truncate (self->typeahead, 0);
		else
			busy = TRUE;
	}

	return busy;
}

/* paint_text_v: @text with its left edge at @x, centred on @cy */
static void
paint_text_v (LrgPaintList   *list,
              LrgTheme       *theme,
              const gchar    *text,
              gfloat          x,
              gfloat          cy,
              gfloat          size,
              const GrlColor *color)
{
	gfloat th;

	if (text == NULL || text[0] == '\0')
		return;
	text_size (theme, text, size, NULL, &th);
	lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), text, roundf (x), roundf (cy - th * 0.5f),
	                     size, 1.0f, color);
}

/*
 * paint_mnemonic:
 *
 * Underlines the mnemonic character of @label drawn at (@x, @cy).
 */
static void
paint_mnemonic (LrgPaintList   *list,
                LrgTheme       *theme,
                const gchar    *label,
                gint            mnemonic,
                gfloat          x,
                gfloat          cy,
                gfloat          size,
                const GrlColor *color)
{
	g_autofree gchar *before = NULL;
	g_autofree gchar *ch = NULL;
	gfloat            bw;
	gfloat            cw;
	gfloat            th;
	const gchar      *end;

	if (label == NULL || mnemonic < 0)
		return;
	before = g_strndup (label, (gsize) mnemonic);
	end = g_utf8_next_char (label + mnemonic);
	ch = g_strndup (label + mnemonic, (gsize) (end - (label + mnemonic)));
	text_size (theme, before, size, &bw, NULL);
	text_size (theme, ch, size, &cw, &th);
	lrg_paint_list_rect (list, roundf (x + bw), roundf (cy + th * 0.5f), MAX (cw, 2.0f), 1.0f, 0.0f, color);
}

/*
 * paint_mark:
 *
 * Draws the leading-gutter mark centred on (@cx, @cy): a check stroke
 * for a checked boolean action, a dot for the chosen radio target.
 * Unchecked check items and unchosen radio items leave the gutter
 * empty.
 */
static void
paint_mark (LrgPaintList   *list,
            MarkKind        mark,
            gfloat          cx,
            gfloat          cy,
            gfloat          size,
            const GrlColor *color)
{
	if (mark == MARK_CHECK_ON)
		lrg_paint_list_icon (list, LRG_ICON_CHECK, roundf (cx - size * 0.5f), roundf (cy - size * 0.5f),
		                     size, 0.0f, color);
	else if (mark == MARK_RADIO_ON)
		lrg_paint_list_circle (list, cx, cy, MAX (2.5f, size * 0.2f), color);
}

static void
lrg_menu_paint (LrgWidget    *widget,
                LrgPaintList *list)
{
	LrgMenu     *self = LRG_MENU (widget);
	LrgTheme    *theme = lrg_widget_get_theme (widget);
	LrgPartLook  surface;
	LrgPartLook  item;
	LrgPartLook  item_off;
	LrgPartLook  accel;
	LrgPartLook  accel_off;
	LrgPartLook  mark;
	LrgPartLook  arrow;
	LrgPartLook  sep;
	LrgPartLook  header;
	MenuMetrics  m;
	gfloat       x = lrg_widget_get_world_x (widget);
	gfloat       y = lrg_widget_get_world_y (widget);
	gfloat       w = lrg_widget_get_width (widget);
	gfloat       h = lrg_widget_get_height (widget);
	gfloat       row_x = x + MENU_PAD;
	gfloat       row_w = w - MENU_PAD * 2.0f;
	gboolean     rtl = menu_is_rtl (self);
	guint        i;

	menu_metrics (self, &m);
	layout_rows (self);

	/* Surface: shadow, fill, hairline border */
	lrg_widget_resolve_look (widget, LRG_UI_PART_POPUP, &surface);
	lrg_widget_paint_box (widget, list, &surface, x, y, w, h);

	/* The highlight: one shape gliding between rows */
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, LRG_WIDGET_STATE_HOVERED, &item);
	if (self->hl_a.value > 0.01f)
	{
		LrgPartLook hl = item;
		gfloat      a = CLAMP (self->hl_a.value, 0.0f, 1.0f);

		hl.background.a = (guint8) roundf ((gfloat) hl.background.a * a);
		hl.border.a = (guint8) roundf ((gfloat) hl.border.a * a);
		hl.shadow.a = (guint8) roundf ((gfloat) hl.shadow.a * a);
		lrg_widget_paint_box (widget, list, &hl, row_x, y + self->hl_y.value, row_w,
		                      MAX (self->hl_h.value, 0.0f));
	}

	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, LRG_WIDGET_STATE_DISABLED, &item_off);
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_NONE, &accel);
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_LABEL, LRG_WIDGET_STATE_DISABLED, &accel_off);
	lrg_widget_resolve_look (widget, LRG_UI_PART_MARK, &mark);
	lrg_widget_resolve_look (widget, LRG_UI_PART_ARROW, &arrow);
	lrg_widget_resolve_look (widget, LRG_UI_PART_TRACK, &sep);
	lrg_widget_resolve_look (widget, LRG_UI_PART_HEADER, &header);

	/* Rows. Columns are computed left-to-right and mirrored for RTL. */
	for (i = 0; i < self->entries->len; i++)
	{
		MenuEntry      *e = &g_array_index (self->entries, MenuEntry, i);
		gfloat          ry = y + e->y;
		gfloat          cy = ry + e->h * 0.5f;
		gfloat          lead = row_x + m.pad_x;              /* LTR leading edge */
		gfloat          trail = row_x + row_w - m.pad_x;     /* LTR trailing edge */
		gboolean        enabled;
		const GrlColor *fg;
		gfloat          tw;
		gfloat          gx;
		gfloat          lx;

		switch (e->kind)
		{
		case LRG_MENU_ENTRY_SEPARATOR:
			lrg_paint_list_rect (list, row_x + m.pad_x * 0.5f, roundf (cy - 0.5f),
			                     row_w - m.pad_x, 1.0f, 0.0f, &sep.background);
			continue;

		case LRG_MENU_ENTRY_HEADER:
			{
				gfloat hs = header.size > 0.0f ? header.size : m.small;

				text_size (theme, e->label, hs, &tw, NULL);
				lx = rtl ? trail - tw : lead;
				paint_text_v (list, theme, e->label, lx, cy, hs, &header.foreground);
			}
			continue;

		default:
			break;
		}

		enabled = entry_selectable (self, e);
		fg = enabled ? &item.foreground : &item_off.foreground;

		/* Gutter: check / radio mark, else the icon */
		gx = rtl ? trail - m.gutter * 0.5f - m.spacing * 0.25f : lead + m.gutter * 0.5f - m.spacing * 0.25f;
		{
			MarkKind mk = entry_mark (self, e);

			if (mk != MARK_NONE)
				paint_mark (list, mk, gx, cy, m.size * 0.9f, enabled ? &mark.foreground : fg);
			else if (e->icon != LRG_ICON_NONE)
				lrg_paint_list_icon (list, e->icon, roundf (gx - m.size * 0.45f), roundf (cy - m.size * 0.45f),
				                     m.size * 0.9f, 0.0f, fg);
		}

		/* Label (and its mnemonic underline in keyboard mode) */
		text_size (theme, e->label, m.size, &tw, NULL);
		lx = rtl ? trail - m.gutter - tw : lead + m.gutter;
		paint_text_v (list, theme, e->label, lx, cy, m.size, fg);
		if (self->keyboard_mode)
			paint_mnemonic (list, theme, e->label, e->mnemonic, lx, cy, m.size, fg);

		/* Submenu chevron at the trailing edge */
		if (e->kind == LRG_MENU_ENTRY_SUBMENU)
		{
			gfloat ax = rtl ? lead : trail - m.arrow;

			lrg_paint_list_icon (list, rtl ? LRG_ICON_CHEVRON_LEFT : LRG_ICON_CHEVRON_RIGHT,
			                     roundf (ax), roundf (cy - m.arrow * 0.5f), m.arrow, 0.0f,
			                     (self->child_index == (gint) i) ? fg : &arrow.foreground);
		}

		/* Accelerator, aligned to the trailing edge */
		{
			g_autofree gchar *accel_text = entry_accel_label (self, e);

			if (accel_text != NULL)
			{
				gfloat aw;

				text_size (theme, accel_text, m.size, &aw, NULL);
				paint_text_v (list, theme, accel_text, rtl ? lead : trail - aw, cy, m.size,
				              enabled ? &accel.foreground : &accel_off.foreground);
			}
		}
	}
}

/* take_focus_from_pointer: the pointer moved into this menu; keys follow it */
static void
take_focus_from_pointer (LrgMenu *self)
{
	if (self->canvas == NULL || !chain_has_focus (self))
		return;
	if (lrg_canvas_get_focused_widget (self->canvas) != LRG_WIDGET (self))
		lrg_canvas_set_focused_widget (self->canvas, LRG_WIDGET (self));
}

/* key_is_forward: the key that opens a submenu (Right, Left in RTL) */
static gboolean
key_is_forward (LrgMenu *self,
                GrlKey   key)
{
	return menu_is_rtl (self) ? key == GRL_KEY_LEFT : key == GRL_KEY_RIGHT;
}

/*
 * handle_typeahead:
 *
 * A typed character: with one character, jump to the next row whose
 * mnemonic (or first letter) matches, cycling through repeats; with a
 * longer prefix typed quickly, to the first row whose label starts
 * with it.
 */
static void
handle_typeahead (LrgMenu  *self,
                  gunichar  c)
{
	gint n = (gint) self->entries->len;
	gint start;
	gint k;

	if (n == 0 || !g_unichar_isprint (c))
		return;

	g_string_append_unichar (self->typeahead, g_unichar_tolower (c));
	self->typeahead_timer = MENU_TYPEAHEAD_RESET;
	self->keyboard_mode = TRUE;

	/* A single character starts after the current row so repeats cycle */
	start = g_utf8_strlen (self->typeahead->str, -1) == 1 ? self->hot + 1 : MAX (self->hot, 0);

	for (k = 0; k < n; k++)
	{
		gint       i = ((start + k) % n + n) % n;
		MenuEntry *e = entry_at (self, i);

		if (!entry_selectable (self, e) || e->label == NULL)
			continue;

		if (g_utf8_strlen (self->typeahead->str, -1) == 1)
		{
			if (entry_key_char (e) != g_unichar_tolower (c))
				continue;
		}
		else
		{
			g_autofree gchar *folded = g_utf8_strdown (e->label, -1);

			if (!g_str_has_prefix (folded, self->typeahead->str))
				continue;
		}

		if (i != self->hot)
			close_submenu (self);
		set_hot (self, i);
		return;
	}
}

static gboolean
lrg_menu_handle_event (LrgWidget        *widget,
                       const LrgUIEvent *event)
{
	LrgMenu *self = LRG_MENU (widget);
	gfloat   px = lrg_ui_event_get_x (event);
	gfloat   py = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		{
			gint row;

			if (!lrg_widget_contains_point (widget, px, py))
				return TRUE;

			row = row_at (self, px, py);
			self->armed = TRUE;
			take_focus_from_pointer (self);

			/* Heading into the open submenu: rows crossed on the way
			 * wait before they take the highlight */
			if (self->child_index >= 0 && row != self->child_index && in_safe_triangle (self, px, py))
			{
				if (!self->deferring || self->deferred_hot != row)
				{
					self->deferring = TRUE;
					self->deferred_hot = row;
					self->deferred_timer = self->submenu_delay * MENU_SAFE_FACTOR;
				}
			}
			else if (row != self->last_row || self->deferring ||
			         (row != self->hot && entry_selectable (self, entry_at (self, row))))
				hover_row (self, row);
			self->last_row = row;

			self->last_px = px;
			self->last_py = py;
			self->have_last = TRUE;
			return TRUE;
		}

	case LRG_UI_EVENT_POINTER_LEAVE:
		/* Settled in step(): the canvas also sends a leave when any
		 * popup closes (it re-hovers on the next frame), which must
		 * not drop the highlight under a pointer that did not move */
		self->leave_pending = TRUE;
		return FALSE;

	case LRG_UI_EVENT_POINTER_ENTER:
		self->leave_pending = FALSE;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_widget_contains_point (widget, px, py))
			self->armed = TRUE;
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		{
			gint row;

			/* Activate on release over a row, once the pointer moved
			 * into the menu or pressed in it (so the release of the
			 * click that opened a context menu does nothing) */
			if (!self->armed || !lrg_widget_contains_point (widget, px, py))
				return TRUE;
			row = row_at (self, px, py);
			if (row >= 0)
			{
				self->keyboard_mode = FALSE;
				activate_row (self, row, FALSE);
			}
			return TRUE;
		}

	case LRG_UI_EVENT_SCROLL:
		return TRUE;

	case LRG_UI_EVENT_TEXT_INPUT:
		handle_typeahead (self, lrg_ui_event_get_codepoint (event));
		return TRUE;

	case LRG_UI_EVENT_ACTIVATE:
		if (self->hot >= 0)
			activate_row (self, self->hot, TRUE);
		return TRUE;

	case LRG_UI_EVENT_CANCEL:
		lrg_menu_close (self);
		return TRUE;

	case LRG_UI_EVENT_KEY_DOWN:
		{
			GrlKey key = lrg_ui_event_get_key (event);

			self->keyboard_mode = TRUE;
			self->deferring = FALSE;
			self->pending_row = -1;

			switch (key)
			{
			case GRL_KEY_UP:
				move_hot (self, -1);
				return TRUE;
			case GRL_KEY_DOWN:
				move_hot (self, 1);
				return TRUE;
			case GRL_KEY_HOME:
				self->hot = -1;
				move_hot (self, 1);
				return TRUE;
			case GRL_KEY_END:
				self->hot = -1;
				move_hot (self, -1);
				return TRUE;

			case GRL_KEY_LEFT:
			case GRL_KEY_RIGHT:
				{
					MenuEntry *e = entry_at (self, self->hot);
					gint       visual = key == GRL_KEY_LEFT ? -1 : 1;

					/* Forward on a submenu item opens it */
					if (key_is_forward (self, key) && e != NULL && e->kind == LRG_MENU_ENTRY_SUBMENU)
					{
						open_submenu (self, self->hot, TRUE);
						return TRUE;
					}
					/* Back inside a submenu closes this level */
					if (!key_is_forward (self, key) && self->parent_menu != NULL)
					{
						LrgMenu *parent = self->parent_menu;

						lrg_menu_close (self);
						if (parent->canvas != NULL)
							lrg_canvas_set_focused_widget (parent->canvas, LRG_WIDGET (parent));
						return TRUE;
					}
					/* Otherwise a menu bar may move to its neighbour */
					g_signal_emit (root_menu (self), signals[SIGNAL_MOVE_OUT], 0, visual);
					return TRUE;
				}

			case GRL_KEY_ENTER:
			case GRL_KEY_KP_ENTER:
			case GRL_KEY_SPACE:
				/* A held key must not activate the row (and then
				 * whatever gets focus after the menu closes) */
				if (self->hot >= 0 && !lrg_ui_event_get_repeat (event))
					activate_row (self, self->hot, TRUE);
				return TRUE;

			case GRL_KEY_ESCAPE:
				{
					LrgMenu *parent = self->parent_menu;

					/* One level per press */
					lrg_menu_close (self);
					if (parent != NULL && parent->canvas != NULL)
						lrg_canvas_set_focused_widget (parent->canvas, LRG_WIDGET (parent));
					return TRUE;
				}

			case GRL_KEY_TAB:
				return TRUE;

			default:
				return FALSE;
			}
		}

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_menu_dispose (GObject *object)
{
	LrgMenu *self = LRG_MENU (object);

	lrg_menu_close (self);
	if (self->model != NULL && self->model_changed_id != 0)
		g_signal_handler_disconnect (self->model, self->model_changed_id);
	self->model_changed_id = 0;
	g_clear_object (&self->model);
	if (self->entries != NULL)
		g_array_set_size (self->entries, 0);
	g_clear_weak_pointer (&self->parent_menu);

	G_OBJECT_CLASS (lrg_menu_parent_class)->dispose (object);
}

static void
lrg_menu_finalize (GObject *object)
{
	LrgMenu *self = LRG_MENU (object);

	g_clear_pointer (&self->entries, g_array_unref);
	g_string_free (self->typeahead, TRUE);

	G_OBJECT_CLASS (lrg_menu_parent_class)->finalize (object);
}

static void
lrg_menu_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
	LrgMenu *self = LRG_MENU (object);

	switch (prop_id)
	{
	case PROP_MODEL:
		g_value_set_object (value, self->model);
		break;
	case PROP_OPEN:
		g_value_set_boolean (value, self->canvas != NULL);
		break;
	case PROP_HIGHLIGHTED:
		g_value_set_int (value, self->hot);
		break;
	case PROP_SUBMENU_DELAY:
		g_value_set_float (value, self->submenu_delay);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_menu_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
	LrgMenu *self = LRG_MENU (object);

	switch (prop_id)
	{
	case PROP_MODEL:
		lrg_menu_set_model (self, g_value_get_object (value));
		break;
	case PROP_HIGHLIGHTED:
		lrg_menu_set_highlighted (self, g_value_get_int (value));
		break;
	case PROP_SUBMENU_DELAY:
		lrg_menu_set_submenu_delay (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_menu_class_init (LrgMenuClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_menu_dispose;
	object_class->finalize = lrg_menu_finalize;
	object_class->get_property = lrg_menu_get_property;
	object_class->set_property = lrg_menu_set_property;

	widget_class->style_defaults = lrg_menu_style_defaults;
	widget_class->measure = lrg_menu_measure;
	widget_class->paint = lrg_menu_paint;
	widget_class->step = lrg_menu_step;
	widget_class->handle_event = lrg_menu_handle_event;

	/**
	 * LrgMenu:model:
	 *
	 * The #GMenuModel the rows are built from.
	 */
	properties[PROP_MODEL] =
		g_param_spec_object ("model", NULL, NULL, G_TYPE_MENU_MODEL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgMenu:open:
	 *
	 * Whether the menu is showing on a canvas.
	 */
	properties[PROP_OPEN] =
		g_param_spec_boolean ("open", NULL, NULL, FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgMenu:highlighted:
	 *
	 * The highlighted row, or -1.
	 */
	properties[PROP_HIGHLIGHTED] =
		g_param_spec_int ("highlighted", NULL, NULL, -1, G_MAXINT, -1,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgMenu:submenu-delay:
	 *
	 * Seconds the pointer rests on a submenu item before its submenu
	 * opens (and on another item before an open submenu closes).
	 */
	properties[PROP_SUBMENU_DELAY] =
		g_param_spec_float ("submenu-delay", NULL, NULL, 0.0f, 10.0f, MENU_SUBMENU_DELAY,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgMenu::activated:
	 * @self: the top-level menu
	 * @action: (nullable): the item's action name ("app.quit")
	 *
	 * Emitted on the top-level menu after an item was chosen, the
	 * menu chain closed and the action ran on the owner.
	 */
	signals[SIGNAL_ACTIVATED] =
		g_signal_new ("activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgMenu::closed:
	 * @self: the menu
	 *
	 * Emitted when the menu leaves the screen, whichever way it was
	 * closed (item chosen, Escape, click outside, lrg_menu_close()).
	 */
	signals[SIGNAL_CLOSED] =
		g_signal_new ("closed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgMenu::move-out:
	 * @self: the top-level menu
	 * @direction: -1 for the Left key, 1 for the Right key
	 *
	 * Emitted on the top-level menu when Left or Right is pressed and
	 * there is no submenu to open or close. #LrgMenuBar uses it to move
	 * to the neighbouring menu.
	 */
	signals[SIGNAL_MOVE_OUT] =
		g_signal_new ("move-out", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
}

static void
lrg_menu_init (LrgMenu *self)
{
	self->entries = g_array_new (FALSE, TRUE, sizeof (MenuEntry));
	g_array_set_clear_func (self->entries, menu_entry_clear);
	self->typeahead = g_string_new (NULL);
	self->hot = -1;
	self->child_index = -1;
	self->pending_row = -1;
	self->deferring = FALSE;
	self->last_row = -2;
	self->submenu_delay = MENU_SUBMENU_DELAY;
	lrg_spring_init (&self->hl_y, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->hl_h, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->hl_a, 0.0f, LRG_MOTION_SNAPPY);

	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
	lrg_widget_set_appear (LRG_WIDGET (self), LRG_APPEAR_POP, 0.0f);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgMenu *
lrg_menu_new (void)
{
	return g_object_new (LRG_TYPE_MENU, NULL);
}

LrgMenu *
lrg_menu_new_from_model (GMenuModel *model)
{
	g_return_val_if_fail (model == NULL || G_IS_MENU_MODEL (model), NULL);

	return g_object_new (LRG_TYPE_MENU, "model", model, NULL);
}

GMenuModel *
lrg_menu_get_model (LrgMenu *self)
{
	g_return_val_if_fail (LRG_IS_MENU (self), NULL);

	return self->model;
}

void
lrg_menu_set_model (LrgMenu    *self,
                    GMenuModel *model)
{
	g_return_if_fail (LRG_IS_MENU (self));
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

	menu_rebuild (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
}

void
lrg_menu_popup_at_rect (LrgMenu           *self,
                        LrgWidget         *owner,
                        gfloat             x,
                        gfloat             y,
                        gfloat             width,
                        gfloat             height,
                        LrgPopupPlacement  placement)
{
	LrgCanvas *canvas;

	g_return_if_fail (LRG_IS_MENU (self));
	g_return_if_fail (LRG_IS_WIDGET (owner));

	canvas = find_canvas (owner);
	if (canvas == NULL)
	{
		lrg_debug (LRG_LOG_DOMAIN_UI, "menu owner is not on a canvas; cannot open the menu");
		return;
	}

	/* A menu opened from code is a top-level menu */
	g_clear_weak_pointer (&self->parent_menu);
	show_menu (self, canvas, owner, owner == LRG_WIDGET (canvas) ? NULL : owner,
	           x, y, width, height, placement, FALSE);
}

void
lrg_menu_popup_at_widget (LrgMenu           *self,
                          LrgWidget         *widget,
                          LrgPopupPlacement  placement)
{
	LrgCanvas *canvas;
	gfloat     cx;
	gfloat     cy;

	g_return_if_fail (LRG_IS_MENU (self));
	g_return_if_fail (LRG_IS_WIDGET (widget));

	canvas = find_canvas (widget);
	if (canvas == NULL)
	{
		lrg_debug (LRG_LOG_DOMAIN_UI, "menu anchor is not on a canvas; cannot open the menu");
		return;
	}

	cx = lrg_widget_get_world_x (LRG_WIDGET (canvas));
	cy = lrg_widget_get_world_y (LRG_WIDGET (canvas));
	lrg_menu_popup_at_rect (self, widget,
	                        lrg_widget_get_world_x (widget) - cx, lrg_widget_get_world_y (widget) - cy,
	                        lrg_widget_get_width (widget), lrg_widget_get_height (widget), placement);
}

void
lrg_menu_popup_at_point (LrgMenu   *self,
                         LrgWidget *owner,
                         gfloat     x,
                         gfloat     y)
{
	g_return_if_fail (LRG_IS_MENU (self));
	g_return_if_fail (LRG_IS_WIDGET (owner));

	lrg_menu_popup_at_rect (self, owner, x, y, 0.0f, 0.0f, LRG_POPUP_PLACEMENT_AT_POINTER);
}

void
lrg_menu_close (LrgMenu *self)
{
	g_autoptr(LrgCanvas) canvas = NULL;

	g_return_if_fail (LRG_IS_MENU (self));

	if (self->canvas == NULL)
		return;

	/* The canvas closes this popup and every submenu stacked above it,
	 * emitting popup-dismissed for each (which resets their state) */
	canvas = g_object_ref (self->canvas);
	g_object_ref (self);
	lrg_canvas_dismiss_popup (canvas, LRG_WIDGET (self));
	if (self->canvas != NULL)
		forget_open (self);
	g_object_unref (self);
}

gboolean
lrg_menu_get_open (LrgMenu *self)
{
	g_return_val_if_fail (LRG_IS_MENU (self), FALSE);

	return self->canvas != NULL;
}

LrgWidget *
lrg_menu_get_owner (LrgMenu *self)
{
	g_return_val_if_fail (LRG_IS_MENU (self), NULL);

	return root_menu (self)->owner;
}

LrgMenu *
lrg_menu_get_parent_menu (LrgMenu *self)
{
	g_return_val_if_fail (LRG_IS_MENU (self), NULL);

	return self->parent_menu;
}

LrgMenu *
lrg_menu_get_open_submenu (LrgMenu *self)
{
	MenuEntry *e;

	g_return_val_if_fail (LRG_IS_MENU (self), NULL);

	e = entry_at (self, self->child_index);
	if (e == NULL || e->submenu == NULL || e->submenu->canvas == NULL)
		return NULL;
	return e->submenu;
}

guint
lrg_menu_get_n_entries (LrgMenu *self)
{
	g_return_val_if_fail (LRG_IS_MENU (self), 0);

	return self->entries->len;
}

LrgMenuEntryKind
lrg_menu_get_entry_kind (LrgMenu *self,
                         guint    index)
{
	MenuEntry *e;

	g_return_val_if_fail (LRG_IS_MENU (self), LRG_MENU_ENTRY_SEPARATOR);

	e = entry_at (self, (gint) index);
	return e != NULL ? e->kind : LRG_MENU_ENTRY_SEPARATOR;
}

const gchar *
lrg_menu_get_entry_label (LrgMenu *self,
                          guint    index)
{
	MenuEntry *e;

	g_return_val_if_fail (LRG_IS_MENU (self), NULL);

	e = entry_at (self, (gint) index);
	return e != NULL ? e->label : NULL;
}

const gchar *
lrg_menu_get_entry_action (LrgMenu *self,
                           guint    index)
{
	MenuEntry *e;

	g_return_val_if_fail (LRG_IS_MENU (self), NULL);

	e = entry_at (self, (gint) index);
	return e != NULL ? e->action : NULL;
}

gboolean
lrg_menu_get_entry_enabled (LrgMenu *self,
                            guint    index)
{
	g_return_val_if_fail (LRG_IS_MENU (self), FALSE);

	return entry_selectable (self, entry_at (self, (gint) index));
}

gboolean
lrg_menu_get_entry_rect (LrgMenu *self,
                         guint    index,
                         gfloat  *x,
                         gfloat  *y,
                         gfloat  *width,
                         gfloat  *height)
{
	MenuEntry *e;

	g_return_val_if_fail (LRG_IS_MENU (self), FALSE);

	e = entry_at (self, (gint) index);
	if (e == NULL)
		return FALSE;

	layout_rows (self);
	if (x != NULL)
		*x = lrg_widget_get_world_x (LRG_WIDGET (self)) + MENU_PAD;
	if (y != NULL)
		*y = lrg_widget_get_world_y (LRG_WIDGET (self)) + e->y;
	if (width != NULL)
		*width = MAX (0.0f, lrg_widget_get_width (LRG_WIDGET (self)) - MENU_PAD * 2.0f);
	if (height != NULL)
		*height = e->h;
	return TRUE;
}

gint
lrg_menu_get_highlighted (LrgMenu *self)
{
	g_return_val_if_fail (LRG_IS_MENU (self), -1);

	return self->hot;
}

void
lrg_menu_set_highlighted (LrgMenu *self,
                          gint     index)
{
	g_return_if_fail (LRG_IS_MENU (self));

	if (index != self->hot)
		close_submenu (self);
	set_hot (self, index);
}

gboolean
lrg_menu_activate_entry (LrgMenu *self,
                         guint    index)
{
	g_return_val_if_fail (LRG_IS_MENU (self), FALSE);

	return activate_row (self, (gint) index, FALSE);
}

gfloat
lrg_menu_get_submenu_delay (LrgMenu *self)
{
	g_return_val_if_fail (LRG_IS_MENU (self), MENU_SUBMENU_DELAY);

	return self->submenu_delay;
}

void
lrg_menu_set_submenu_delay (LrgMenu *self,
                            gfloat   delay)
{
	g_return_if_fail (LRG_IS_MENU (self));

	delay = CLAMP (delay, 0.0f, 10.0f);
	if (self->submenu_delay == delay)
		return;
	self->submenu_delay = delay;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBMENU_DELAY]);
}

gboolean
lrg_menu_forward_pointer_event (LrgMenu          *self,
                                const LrgUIEvent *event)
{
	LrgUIEventType  type;
	LrgMenu        *m;
	LrgMenu        *found = NULL;
	gfloat          px;
	gfloat          py;

	g_return_val_if_fail (LRG_IS_MENU (self), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	type = lrg_ui_event_get_event_type (event);
	if (self->canvas == NULL ||
	    (type != LRG_UI_EVENT_MOUSE_MOVE && type != LRG_UI_EVENT_MOUSE_BUTTON_UP))
		return FALSE;

	/* The deepest open menu of the chain under the pointer */
	px = lrg_ui_event_get_x (event);
	py = lrg_ui_event_get_y (event);
	for (m = self; m != NULL && m->canvas != NULL; m = lrg_menu_get_open_submenu (m))
		if (lrg_widget_contains_point (LRG_WIDGET (m), px, py))
			found = m;
	if (found == NULL)
		return FALSE;

	g_object_ref (found);
	lrg_widget_handle_event (LRG_WIDGET (found), event);
	g_object_unref (found);
	return TRUE;
}
