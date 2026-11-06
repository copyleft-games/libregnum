/* lrg-ui-actions.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Accelerator parsing and labels, and the built-in "app" and "ui"
 * action groups every LrgCanvas installs.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <stdlib.h>
#include <string.h>

#include "lrg-ui-actions.h"
#include "lrg-widget.h"
#include "lrg-canvas.h"
#include "lrg-canvas-private.h"
#include "lrg-tab-view.h"
#include "lrg-toast.h"
#include "lrg-toast-queue.h"
#include "lrg-theme.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Key names
 * ========================================================================== */

/*
 * KeyName:
 *
 * One accepted key name. The first entry for a key is its canonical
 * name (what lrg_accel_name() writes); later entries are aliases.
 * @label is what lrg_accel_to_label() shows.
 */
typedef struct
{
	const gchar *name;
	GrlKey       key;
	const gchar *label;
} KeyName;

static const KeyName key_names[] = {
	{ "Escape", GRL_KEY_ESCAPE, "Esc" },
	{ "Esc", GRL_KEY_ESCAPE, "Esc" },
	{ "Return", GRL_KEY_ENTER, "Enter" },
	{ "Enter", GRL_KEY_ENTER, "Enter" },
	{ "Tab", GRL_KEY_TAB, "Tab" },
	{ "BackSpace", GRL_KEY_BACKSPACE, "Backspace" },
	{ "Delete", GRL_KEY_DELETE, "Del" },
	{ "Del", GRL_KEY_DELETE, "Del" },
	{ "Insert", GRL_KEY_INSERT, "Ins" },
	{ "Home", GRL_KEY_HOME, "Home" },
	{ "End", GRL_KEY_END, "End" },
	{ "Page_Up", GRL_KEY_PAGE_UP, "PgUp" },
	{ "PageUp", GRL_KEY_PAGE_UP, "PgUp" },
	{ "Page_Down", GRL_KEY_PAGE_DOWN, "PgDn" },
	{ "PageDown", GRL_KEY_PAGE_DOWN, "PgDn" },
	{ "Up", GRL_KEY_UP, "Up" },
	{ "Down", GRL_KEY_DOWN, "Down" },
	{ "Left", GRL_KEY_LEFT, "Left" },
	{ "Right", GRL_KEY_RIGHT, "Right" },
	{ "space", GRL_KEY_SPACE, "Space" },
	{ "plus", GRL_KEY_EQUAL, "+" },
	{ "minus", GRL_KEY_MINUS, "-" },
	{ "equal", GRL_KEY_EQUAL, "=" },
	{ "comma", GRL_KEY_COMMA, "," },
	{ "period", GRL_KEY_PERIOD, "." },
	{ "slash", GRL_KEY_SLASH, "/" },
	{ "backslash", GRL_KEY_BACKSLASH, "\\" },
	{ "semicolon", GRL_KEY_SEMICOLON, ";" },
	{ "apostrophe", GRL_KEY_APOSTROPHE, "'" },
	{ "grave", GRL_KEY_GRAVE, "`" },
	{ "bracketleft", GRL_KEY_LEFT_BRACKET, "[" },
	{ "bracketright", GRL_KEY_RIGHT_BRACKET, "]" },
	{ "KP_Add", GRL_KEY_KP_ADD, "Num +" },
	{ "KP_Subtract", GRL_KEY_KP_SUBTRACT, "Num -" },
	{ "KP_Multiply", GRL_KEY_KP_MULTIPLY, "Num *" },
	{ "KP_Divide", GRL_KEY_KP_DIVIDE, "Num /" },
	{ "KP_Decimal", GRL_KEY_KP_DECIMAL, "Num ." },
	{ "KP_Enter", GRL_KEY_KP_ENTER, "Num Enter" },
	{ "KP_Equal", GRL_KEY_KP_EQUAL, "Num =" },
	{ "Pause", GRL_KEY_PAUSE, "Pause" },
	{ "Print", GRL_KEY_PRINT_SCREEN, "PrtSc" },
	{ "Menu", GRL_KEY_KB_MENU, "Menu" }
};

/* Single punctuation characters accepted as key names */
static const struct
{
	gchar  c;
	GrlKey key;
} punct_keys[] = {
	{ '+', GRL_KEY_EQUAL },
	{ '=', GRL_KEY_EQUAL },
	{ '-', GRL_KEY_MINUS },
	{ ',', GRL_KEY_COMMA },
	{ '.', GRL_KEY_PERIOD },
	{ '/', GRL_KEY_SLASH },
	{ '\\', GRL_KEY_BACKSLASH },
	{ ';', GRL_KEY_SEMICOLON },
	{ '\'', GRL_KEY_APOSTROPHE },
	{ '`', GRL_KEY_GRAVE },
	{ '[', GRL_KEY_LEFT_BRACKET },
	{ ']', GRL_KEY_RIGHT_BRACKET }
};

/*
 * key_from_name:
 *
 * Resolves a key name: single letters / digits / punctuation, F1-F12,
 * KP_0-KP_9, then the named table (case-insensitive).
 */
static gboolean
key_from_name (const gchar *name,
               GrlKey      *key)
{
	gsize len = strlen (name);
	guint i;

	if (len == 0)
		return FALSE;

	/* One character: a letter, digit or punctuation key */
	if (len == 1)
	{
		gchar c = name[0];

		if (g_ascii_isalpha (c))
		{
			*key = (GrlKey) (GRL_KEY_A + (g_ascii_toupper (c) - 'A'));
			return TRUE;
		}
		if (g_ascii_isdigit (c))
		{
			*key = (GrlKey) (GRL_KEY_ZERO + (c - '0'));
			return TRUE;
		}
		for (i = 0; i < G_N_ELEMENTS (punct_keys); i++)
			if (punct_keys[i].c == c)
			{
				*key = punct_keys[i].key;
				return TRUE;
			}
		return FALSE;
	}

	/* F1 .. F12 */
	if ((name[0] == 'F' || name[0] == 'f') && len <= 3 && g_ascii_isdigit (name[1]) &&
	    (len == 2 || g_ascii_isdigit (name[2])))
	{
		gint n = atoi (name + 1);

		if (n >= 1 && n <= 12)
		{
			*key = (GrlKey) (GRL_KEY_F1 + (n - 1));
			return TRUE;
		}
		return FALSE;
	}

	/* KP_0 .. KP_9 */
	if (len == 4 && g_ascii_strncasecmp (name, "KP_", 3) == 0 && g_ascii_isdigit (name[3]))
	{
		*key = (GrlKey) (GRL_KEY_KP_0 + (name[3] - '0'));
		return TRUE;
	}

	for (i = 0; i < G_N_ELEMENTS (key_names); i++)
		if (g_ascii_strcasecmp (key_names[i].name, name) == 0)
		{
			*key = key_names[i].key;
			return TRUE;
		}
	return FALSE;
}

/*
 * key_to_name:
 *
 * Canonical name (@label FALSE) or display label (@label TRUE) of a
 * key, newly allocated, or NULL for an unnamed key.
 */
static gchar *
key_to_name (GrlKey   key,
             gboolean label)
{
	guint i;

	if (key >= GRL_KEY_A && key <= GRL_KEY_Z)
	{
		gchar c = (gchar) ('a' + (key - GRL_KEY_A));

		return g_strdup_printf ("%c", label ? g_ascii_toupper (c) : c);
	}
	if (key >= GRL_KEY_ZERO && key <= GRL_KEY_NINE)
		return g_strdup_printf ("%c", (gchar) ('0' + (key - GRL_KEY_ZERO)));
	if (key >= GRL_KEY_F1 && key <= GRL_KEY_F12)
		return g_strdup_printf ("F%d", (gint) (key - GRL_KEY_F1) + 1);
	if (key >= GRL_KEY_KP_0 && key <= GRL_KEY_KP_9)
		return g_strdup_printf (label ? "Num %d" : "KP_%d", (gint) (key - GRL_KEY_KP_0));

	/* The first table entry for a key is canonical */
	for (i = 0; i < G_N_ELEMENTS (key_names); i++)
		if (key_names[i].key == key)
			return g_strdup (label ? key_names[i].label : key_names[i].name);
	return NULL;
}

/*
 * modifier_from_name:
 *
 * The flag for a bracketed modifier token (without the brackets).
 */
static gboolean
modifier_from_name (const gchar       *name,
                    gsize              len,
                    LrgInputModifiers *mod)
{
	static const struct
	{
		const gchar       *name;
		LrgInputModifiers  mod;
	} mods[] = {
		{ "ctrl", LRG_INPUT_MODIFIER_CTRL },
		{ "control", LRG_INPUT_MODIFIER_CTRL },
		{ "primary", LRG_INPUT_MODIFIER_CTRL },
		{ "shift", LRG_INPUT_MODIFIER_SHIFT },
		{ "alt", LRG_INPUT_MODIFIER_ALT },
		{ "super", LRG_INPUT_MODIFIER_SUPER },
		{ "meta", LRG_INPUT_MODIFIER_SUPER }
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (mods); i++)
		if (strlen (mods[i].name) == len && g_ascii_strncasecmp (mods[i].name, name, len) == 0)
		{
			*mod = mods[i].mod;
			return TRUE;
		}
	return FALSE;
}

/* ==========================================================================
 * Accelerators
 * ========================================================================== */

gboolean
lrg_accel_parse (const gchar       *accel,
                 GrlKey            *key,
                 LrgInputModifiers *mods)
{
	LrgInputModifiers  m = LRG_INPUT_MODIFIER_NONE;
	GrlKey             k = GRL_KEY_NULL;
	const gchar       *p;

	g_return_val_if_fail (accel != NULL, FALSE);

	/* Leading <Modifier> tokens, each at most once */
	p = accel;
	while (*p == ' ')
		p++;
	while (*p == '<')
	{
		const gchar       *close = strchr (p, '>');
		LrgInputModifiers  mod;

		if (close == NULL || !modifier_from_name (p + 1, (gsize) (close - p - 1), &mod))
			return FALSE;
		if (m & mod)
			return FALSE;
		m |= mod;
		p = close + 1;
	}

	/* The rest is exactly one key name (trailing blanks allowed) */
	{
		g_autofree gchar *name = g_strstrip (g_strdup (p));

		if (!key_from_name (name, &k))
			return FALSE;
	}

	if (key != NULL)
		*key = k;
	if (mods != NULL)
		*mods = m;
	return TRUE;
}

gchar *
lrg_accel_name (GrlKey            key,
                LrgInputModifiers mods)
{
	g_autofree gchar *name = key_to_name (key, FALSE);
	GString          *out;

	if (name == NULL)
		return NULL;

	out = g_string_new (NULL);
	if (mods & LRG_INPUT_MODIFIER_CTRL)
		g_string_append (out, "<Ctrl>");
	if (mods & LRG_INPUT_MODIFIER_ALT)
		g_string_append (out, "<Alt>");
	if (mods & LRG_INPUT_MODIFIER_SHIFT)
		g_string_append (out, "<Shift>");
	if (mods & LRG_INPUT_MODIFIER_SUPER)
		g_string_append (out, "<Super>");
	g_string_append (out, name);
	return g_string_free (out, FALSE);
}

gchar *
lrg_accel_to_label (const gchar *accel)
{
	GrlKey             key;
	LrgInputModifiers  mods;
	g_autofree gchar  *name = NULL;
	GString           *out;

	g_return_val_if_fail (accel != NULL, NULL);

	if (!lrg_accel_parse (accel, &key, &mods))
		return NULL;
	name = key_to_name (key, TRUE);
	if (name == NULL)
		return NULL;

	out = g_string_new (NULL);
	if (mods & LRG_INPUT_MODIFIER_CTRL)
		g_string_append (out, "Ctrl+");
	if (mods & LRG_INPUT_MODIFIER_ALT)
		g_string_append (out, "Alt+");
	if (mods & LRG_INPUT_MODIFIER_SHIFT)
		g_string_append (out, "Shift+");
	if (mods & LRG_INPUT_MODIFIER_SUPER)
		g_string_append (out, "Super+");
	g_string_append (out, name);
	return g_string_free (out, FALSE);
}

/* ==========================================================================
 * Built-in action groups
 * ========================================================================== */

/*
 * The groups are owned by the canvas (inserted on it), so they only
 * keep a weak reference back: a strong one would be a cycle.
 */
static GQuark
canvas_ref_quark (void)
{
	return g_quark_from_static_string ("lrg-ui-actions-canvas");
}

static void
weak_ref_free (gpointer data)
{
	GWeakRef *ref = data;

	g_weak_ref_clear (ref);
	g_free (ref);
}

/* group_canvas: the canvas a built-in group belongs to (new ref) or NULL */
static LrgCanvas *
group_canvas (gpointer group)
{
	GWeakRef *ref = g_object_get_qdata (G_OBJECT (group), canvas_ref_quark ());

	return ref != NULL ? g_weak_ref_get (ref) : NULL;
}

static GSimpleActionGroup *
new_group (LrgCanvas *canvas)
{
	GSimpleActionGroup *group = g_simple_action_group_new ();
	GWeakRef           *ref = g_new0 (GWeakRef, 1);

	g_weak_ref_init (ref, canvas);
	g_object_set_qdata_full (G_OBJECT (group), canvas_ref_quark (), ref, weak_ref_free);
	return group;
}

/* app.quit: the application decides what quitting means */
static void
app_quit (GSimpleAction *action,
          GVariant      *parameter,
          gpointer       user_data)
{
	g_autoptr(LrgCanvas) canvas = group_canvas (user_data);

	if (canvas != NULL)
		_lrg_canvas_emit_quit_requested (canvas);
}

GActionGroup *
_lrg_ui_actions_new_app_group (LrgCanvas *canvas)
{
	GSimpleActionGroup *group = new_group (canvas);
	const GActionEntry  entries[] = {
		{ "quit", app_quit, NULL, NULL, NULL, { 0 } }
	};

	g_action_map_add_action_entries (G_ACTION_MAP (group), entries, G_N_ELEMENTS (entries), group);
	return G_ACTION_GROUP (group);
}

/*
 * find_target:
 *
 * The widget a "ui" action addresses by name: the canvas tree, open
 * popups, then overlays.
 */
static LrgWidget *
find_target (LrgCanvas   *canvas,
             GVariant    *parameter)
{
	const gchar *name = g_variant_get_string (parameter, NULL);
	LrgWidget   *w;

	w = _lrg_canvas_find_widget (canvas, name);
	if (w == NULL)
		lrg_debug (LRG_LOG_DOMAIN, "ui action: no widget named \"%s\"", name);
	return w;
}

static void
ui_show (GSimpleAction *action,
         GVariant      *parameter,
         gpointer       user_data)
{
	g_autoptr(LrgCanvas) canvas = group_canvas (user_data);
	LrgWidget           *w = canvas != NULL ? find_target (canvas, parameter) : NULL;

	if (w != NULL)
		lrg_widget_set_visible (w, TRUE);
}

static void
ui_hide (GSimpleAction *action,
         GVariant      *parameter,
         gpointer       user_data)
{
	g_autoptr(LrgCanvas) canvas = group_canvas (user_data);
	LrgWidget           *w = canvas != NULL ? find_target (canvas, parameter) : NULL;

	if (w != NULL)
		lrg_widget_set_visible (w, FALSE);
}

static void
ui_toggle (GSimpleAction *action,
           GVariant      *parameter,
           gpointer       user_data)
{
	g_autoptr(LrgCanvas) canvas = group_canvas (user_data);
	LrgWidget           *w = canvas != NULL ? find_target (canvas, parameter) : NULL;

	if (w != NULL)
		lrg_widget_set_visible (w, !lrg_widget_get_visible (w));
}

/*
 * select_page:
 *
 * Switches @w to the page called @page. Generic: a string property
 * "visible-child-name" (stacks, switchers) is set; an #LrgTabView
 * selects the tab whose label is @page, or whose index @page spells.
 */
static gboolean
select_page (LrgWidget   *w,
             const gchar *page)
{
	GParamSpec *pspec;

	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (w), "visible-child-name");
	if (pspec != NULL && pspec->value_type == G_TYPE_STRING && (pspec->flags & G_PARAM_WRITABLE))
	{
		g_object_set (w, "visible-child-name", page, NULL);
		return TRUE;
	}

	if (LRG_IS_TAB_VIEW (w))
	{
		LrgTabView *tabs = LRG_TAB_VIEW (w);
		guint       n = lrg_tab_view_get_tab_count (tabs);
		guint       i;
		guint64     index;

		for (i = 0; i < n; i++)
			if (g_strcmp0 (lrg_tab_view_get_tab_label (tabs, i), page) == 0)
			{
				lrg_tab_view_set_active_tab (tabs, i);
				return TRUE;
			}
		if (g_ascii_string_to_unsigned (page, 10, 0, n > 0 ? n - 1 : 0, &index, NULL) && n > 0)
		{
			lrg_tab_view_set_active_tab (tabs, (guint) index);
			return TRUE;
		}
	}
	return FALSE;
}

/* ui.page("switcher:page") */
static void
ui_page (GSimpleAction *action,
         GVariant      *parameter,
         gpointer       user_data)
{
	g_autoptr(LrgCanvas)  canvas = group_canvas (user_data);
	const gchar          *spec = g_variant_get_string (parameter, NULL);
	const gchar          *colon = strchr (spec, ':');
	g_autofree gchar     *name = NULL;
	LrgWidget            *w;

	if (canvas == NULL)
		return;
	if (colon == NULL || colon == spec)
	{
		lrg_debug (LRG_LOG_DOMAIN, "ui.page: \"%s\" is not \"widget:page\"", spec);
		return;
	}

	name = g_strndup (spec, (gsize) (colon - spec));
	w = _lrg_canvas_find_widget (canvas, name);
	if (w == NULL || !select_page (w, colon + 1))
		lrg_debug (LRG_LOG_DOMAIN, "ui.page: cannot show page \"%s\" of \"%s\"", colon + 1, name);
}

/* ui.theme(preset): a fresh preset theme on the canvas */
static void
ui_theme (GSimpleAction *action,
          GVariant      *parameter,
          gpointer      user_data)
{
	g_autoptr(LrgCanvas) canvas = group_canvas (user_data);
	g_autoptr(LrgTheme)  theme = NULL;

	if (canvas == NULL)
		return;
	theme = lrg_theme_new_preset (g_variant_get_string (parameter, NULL));
	if (theme == NULL)
	{
		lrg_debug (LRG_LOG_DOMAIN, "ui.theme: unknown preset \"%s\"", g_variant_get_string (parameter, NULL));
		return;
	}
	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
}

/*
 * ui.motion(nick): a copy of the canvas's current theme with another
 * spring preset (the shared default theme is never modified).
 */
static void
ui_motion (GSimpleAction *action,
           GVariant      *parameter,
           gpointer       user_data)
{
	g_autoptr(LrgCanvas)  canvas = group_canvas (user_data);
	g_autoptr(GEnumClass) klass = NULL;
	g_autoptr(LrgTheme)   theme = NULL;
	GEnumValue           *value;

	if (canvas == NULL)
		return;
	klass = g_type_class_ref (LRG_TYPE_MOTION);
	value = g_enum_get_value_by_nick (klass, g_variant_get_string (parameter, NULL));
	if (value == NULL)
	{
		lrg_debug (LRG_LOG_DOMAIN, "ui.motion: unknown motion \"%s\"", g_variant_get_string (parameter, NULL));
		return;
	}
	theme = lrg_theme_copy (lrg_widget_get_theme (LRG_WIDGET (canvas)));
	lrg_theme_set_motion (theme, (LrgMotion) value->value);
	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
}

/* ui.direction("ltr" | "rtl") */
static void
ui_direction (GSimpleAction *action,
              GVariant      *parameter,
              gpointer       user_data)
{
	g_autoptr(LrgCanvas) canvas = group_canvas (user_data);
	const gchar         *nick = g_variant_get_string (parameter, NULL);

	if (canvas == NULL)
		return;
	if (g_ascii_strcasecmp (nick, "rtl") == 0)
		lrg_canvas_set_default_direction (canvas, LRG_WIDGET_DIRECTION_RTL);
	else if (g_ascii_strcasecmp (nick, "ltr") == 0)
		lrg_canvas_set_default_direction (canvas, LRG_WIDGET_DIRECTION_LTR);
	else
		lrg_debug (LRG_LOG_DOMAIN, "ui.direction: \"%s\" is neither ltr nor rtl", nick);
}

/* ui.focus(name): keyboard-style focus, ring visible */
static void
ui_focus (GSimpleAction *action,
          GVariant      *parameter,
          gpointer       user_data)
{
	g_autoptr(LrgCanvas) canvas = group_canvas (user_data);
	LrgWidget           *w = canvas != NULL ? find_target (canvas, parameter) : NULL;

	if (w != NULL && lrg_widget_get_focusable (w) && lrg_widget_get_enabled (w))
		_lrg_canvas_focus_widget (canvas, w);
}

/* ui.popup-close: the newest popup */
static void
ui_popup_close (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
	g_autoptr(LrgCanvas) canvas = group_canvas (user_data);
	LrgWidget           *top;

	if (canvas == NULL)
		return;
	top = lrg_canvas_get_top_popup (canvas);
	if (top != NULL)
		lrg_canvas_dismiss_popup (canvas, top);
}

/* find_toast_queue: first LrgToastQueue in the overlays or the tree */
static LrgToastQueue *
find_toast_queue (LrgWidget *w)
{
	GList *l;

	if (LRG_IS_TOAST_QUEUE (w))
		return LRG_TOAST_QUEUE (w);
	if (!LRG_IS_CONTAINER (w))
		return NULL;
	for (l = lrg_container_get_children (LRG_CONTAINER (w)); l != NULL; l = l->next)
	{
		LrgToastQueue *q = find_toast_queue (LRG_WIDGET (l->data));

		if (q != NULL)
			return q;
	}
	return NULL;
}

/* ui.toast(message): shown on the canvas's toast queue, if it has one */
static void
ui_toast (GSimpleAction *action,
          GVariant      *parameter,
          gpointer       user_data)
{
	g_autoptr(LrgCanvas)  canvas = group_canvas (user_data);
	g_autoptr(GPtrArray)  overlays = NULL;
	g_autoptr(LrgToast)   toast = NULL;
	LrgToastQueue        *queue = NULL;
	guint                 i;

	if (canvas == NULL)
		return;

	overlays = g_ptr_array_new ();
	_lrg_canvas_get_layers (canvas, NULL, overlays);
	for (i = 0; i < overlays->len && queue == NULL; i++)
		queue = find_toast_queue (g_ptr_array_index (overlays, i));
	if (queue == NULL)
		queue = find_toast_queue (LRG_WIDGET (canvas));
	if (queue == NULL)
	{
		lrg_debug (LRG_LOG_DOMAIN, "ui.toast: the canvas has no LrgToastQueue");
		return;
	}

	toast = lrg_toast_new (NULL, g_variant_get_string (parameter, NULL));
	lrg_toast_queue_show (queue, toast);
}

GActionGroup *
_lrg_ui_actions_new_ui_group (LrgCanvas *canvas)
{
	GSimpleActionGroup *group = new_group (canvas);
	const GActionEntry  entries[] = {
		{ "show", ui_show, "s", NULL, NULL, { 0 } },
		{ "hide", ui_hide, "s", NULL, NULL, { 0 } },
		{ "toggle", ui_toggle, "s", NULL, NULL, { 0 } },
		{ "page", ui_page, "s", NULL, NULL, { 0 } },
		{ "theme", ui_theme, "s", NULL, NULL, { 0 } },
		{ "motion", ui_motion, "s", NULL, NULL, { 0 } },
		{ "direction", ui_direction, "s", NULL, NULL, { 0 } },
		{ "focus", ui_focus, "s", NULL, NULL, { 0 } },
		{ "popup-close", ui_popup_close, NULL, NULL, NULL, { 0 } },
		{ "toast", ui_toast, "s", NULL, NULL, { 0 } }
	};

	g_action_map_add_action_entries (G_ACTION_MAP (group), entries, G_N_ELEMENTS (entries), group);
	return G_ACTION_GROUP (group);
}
