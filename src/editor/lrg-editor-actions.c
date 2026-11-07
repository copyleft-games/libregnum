/* lrg-editor-actions.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Named editor commands with shortcuts, exposed as a GActionGroup. See
 * lrg-editor-actions.h.
 *
 * Layout of this file:
 *   - the 0.8.0 shortcut syntax ("Ctrl+Shift+S") parser and formatter,
 *     kept for the deprecated LrgGuiKey API;
 *   - the LrgGuiKey <-> GrlKey bridge;
 *   - conversion of either syntax to accelerators ("<Ctrl><Shift>s");
 *   - fuzzy matching for the command palette;
 *   - the registry itself and its GActionGroup implementation.
 */

#include "lrg-editor-actions.h"
#include "lrg-editor-settings.h"
#include "../ui/lrg-ui-actions.h"
#include <string.h>

/* The modifiers a binding can carry; anything else held is ignored. */
#define ACCEL_MOD_MASK (LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT | \
                        LRG_INPUT_MODIFIER_ALT | LRG_INPUT_MODIFIER_SUPER)

/* ==========================================================================
 * 0.8.0 shortcut text (deprecated LrgGuiKey API)
 * ========================================================================== */

static const struct
{
	const gchar *name;
	LrgGuiKey    key;
} legacy_key_names[] = {
	{ "Tab", LRG_GUI_KEY_TAB }, { "Left", LRG_GUI_KEY_LEFT }, { "Right", LRG_GUI_KEY_RIGHT },
	{ "Up", LRG_GUI_KEY_UP }, { "Down", LRG_GUI_KEY_DOWN }, { "PageUp", LRG_GUI_KEY_PAGE_UP },
	{ "PageDown", LRG_GUI_KEY_PAGE_DOWN }, { "Home", LRG_GUI_KEY_HOME }, { "End", LRG_GUI_KEY_END },
	{ "Insert", LRG_GUI_KEY_INSERT }, { "Delete", LRG_GUI_KEY_DELETE }, { "Del", LRG_GUI_KEY_DELETE },
	{ "Backspace", LRG_GUI_KEY_BACKSPACE }, { "Space", LRG_GUI_KEY_SPACE },
	{ "Enter", LRG_GUI_KEY_ENTER }, { "Return", LRG_GUI_KEY_ENTER },
	{ "Escape", LRG_GUI_KEY_ESCAPE }, { "Esc", LRG_GUI_KEY_ESCAPE },
	{ "-", LRG_GUI_KEY_MINUS }, { "Minus", LRG_GUI_KEY_MINUS },
	{ "=", LRG_GUI_KEY_EQUAL }, { "Equal", LRG_GUI_KEY_EQUAL }, { "Plus", LRG_GUI_KEY_EQUAL },
	{ "[", LRG_GUI_KEY_LEFT_BRACKET }, { "]", LRG_GUI_KEY_RIGHT_BRACKET },
	{ "/", LRG_GUI_KEY_SLASH }, { "\\", LRG_GUI_KEY_BACKSLASH }, { ",", LRG_GUI_KEY_COMMA },
	{ ".", LRG_GUI_KEY_PERIOD }, { ";", LRG_GUI_KEY_SEMICOLON }, { "'", LRG_GUI_KEY_APOSTROPHE },
	{ "`", LRG_GUI_KEY_GRAVE },
};

/*
 * parse_key_name:
 *
 * Resolves one 0.8.0 key name to an #LrgGuiKey (letters, digits, F1-F12,
 * Num0-Num9, then the named table, case-insensitive).
 */
static gboolean
parse_key_name (const gchar *name,
                LrgGuiKey   *key)
{
	gsize i;

	if (name[0] != '\0' && name[1] == '\0')
	{
		gchar c = g_ascii_toupper (name[0]);

		if (c >= 'A' && c <= 'Z')
		{
			*key = LRG_GUI_KEY_A + (c - 'A');
			return TRUE;
		}
		if (c >= '0' && c <= '9')
		{
			*key = LRG_GUI_KEY_0 + (c - '0');
			return TRUE;
		}
	}

	if ((name[0] == 'F' || name[0] == 'f') && g_ascii_isdigit (name[1]))
	{
		gint64 n = g_ascii_strtoll (name + 1, NULL, 10);

		if (n >= 1 && n <= 12)
		{
			*key = LRG_GUI_KEY_F1 + (gint) (n - 1);
			return TRUE;
		}
		return FALSE;
	}

	if (g_ascii_strncasecmp (name, "Num", 3) == 0 && g_ascii_isdigit (name[3]) && name[4] == '\0')
	{
		*key = LRG_GUI_KEY_KP_0 + (name[3] - '0');
		return TRUE;
	}

	for (i = 0; i < G_N_ELEMENTS (legacy_key_names); i++)
		if (g_ascii_strcasecmp (name, legacy_key_names[i].name) == 0)
		{
			*key = legacy_key_names[i].key;
			return TRUE;
		}

	return FALSE;
}

gboolean
lrg_editor_shortcut_parse (const gchar     *text,
                           LrgGuiModifiers *mods,
                           LrgGuiKey       *key)
{
	g_auto(GStrv)   parts = NULL;
	LrgGuiModifiers m = LRG_GUI_MOD_NONE;
	LrgGuiKey       k = LRG_GUI_KEY_NONE;
	guint           i, n;

	if (text == NULL || *text == '\0')
		return FALSE;

	/* "Ctrl++" style: a trailing "+" is the key itself. */
	parts = g_strsplit (text, "+", -1);
	n = g_strv_length (parts);
	if (n >= 2 && parts[n - 1][0] == '\0' && parts[n - 2][0] == '\0')
	{
		g_free (parts[n - 2]);
		parts[n - 2] = g_strdup ("Plus");
		g_free (parts[n - 1]);
		parts[n - 1] = NULL;
		n--;
	}

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *p = g_strstrip (g_strdup (parts[i]));

		if (i + 1 < n)
		{
			if (g_ascii_strcasecmp (p, "Ctrl") == 0 || g_ascii_strcasecmp (p, "Control") == 0)
				m |= LRG_GUI_MOD_CTRL;
			else if (g_ascii_strcasecmp (p, "Shift") == 0)
				m |= LRG_GUI_MOD_SHIFT;
			else if (g_ascii_strcasecmp (p, "Alt") == 0 || g_ascii_strcasecmp (p, "Meta") == 0)
				m |= LRG_GUI_MOD_ALT;
			else if (g_ascii_strcasecmp (p, "Super") == 0 || g_ascii_strcasecmp (p, "Win") == 0 ||
			         g_ascii_strcasecmp (p, "Logo") == 0)
				m |= LRG_GUI_MOD_SUPER;
			else
				return FALSE;
		}
		else if (!parse_key_name (p, &k))
		{
			return FALSE;
		}
	}

	if (k == LRG_GUI_KEY_NONE)
		return FALSE;

	if (mods != NULL)
		*mods = m;
	if (key != NULL)
		*key = k;
	return TRUE;
}

gchar *
lrg_editor_shortcut_format (LrgGuiModifiers mods,
                            LrgGuiKey       key)
{
	GString *out = g_string_new (NULL);
	gsize    i;

	if (mods & LRG_GUI_MOD_CTRL)
		g_string_append (out, "Ctrl+");
	if (mods & LRG_GUI_MOD_SHIFT)
		g_string_append (out, "Shift+");
	if (mods & LRG_GUI_MOD_ALT)
		g_string_append (out, "Alt+");
	if (mods & LRG_GUI_MOD_SUPER)
		g_string_append (out, "Super+");

	if (key >= LRG_GUI_KEY_A && key <= LRG_GUI_KEY_Z)
		g_string_append_c (out, 'A' + (key - LRG_GUI_KEY_A));
	else if (key >= LRG_GUI_KEY_0 && key <= LRG_GUI_KEY_9)
		g_string_append_c (out, '0' + (key - LRG_GUI_KEY_0));
	else if (key >= LRG_GUI_KEY_F1 && key <= LRG_GUI_KEY_F12)
		g_string_append_printf (out, "F%d", key - LRG_GUI_KEY_F1 + 1);
	else if (key >= LRG_GUI_KEY_KP_0 && key <= LRG_GUI_KEY_KP_9)
		g_string_append_printf (out, "Num%d", key - LRG_GUI_KEY_KP_0);
	else
	{
		for (i = 0; i < G_N_ELEMENTS (legacy_key_names); i++)
			if (legacy_key_names[i].key == key)
			{
				g_string_append (out, legacy_key_names[i].name);
				break;
			}
	}

	return g_string_free (out, FALSE);
}

/* ==========================================================================
 * LrgGuiKey <-> GrlKey
 * ========================================================================== */

/* Named keys of both enums; letters, digits, F-keys and the keypad digits
 * are contiguous ranges in both and are handled arithmetically. */
static const struct
{
	LrgGuiKey gui;
	GrlKey    grl;
} gui_grl_keys[] = {
	{ LRG_GUI_KEY_TAB, GRL_KEY_TAB },
	{ LRG_GUI_KEY_LEFT, GRL_KEY_LEFT },
	{ LRG_GUI_KEY_RIGHT, GRL_KEY_RIGHT },
	{ LRG_GUI_KEY_UP, GRL_KEY_UP },
	{ LRG_GUI_KEY_DOWN, GRL_KEY_DOWN },
	{ LRG_GUI_KEY_PAGE_UP, GRL_KEY_PAGE_UP },
	{ LRG_GUI_KEY_PAGE_DOWN, GRL_KEY_PAGE_DOWN },
	{ LRG_GUI_KEY_HOME, GRL_KEY_HOME },
	{ LRG_GUI_KEY_END, GRL_KEY_END },
	{ LRG_GUI_KEY_INSERT, GRL_KEY_INSERT },
	{ LRG_GUI_KEY_DELETE, GRL_KEY_DELETE },
	{ LRG_GUI_KEY_BACKSPACE, GRL_KEY_BACKSPACE },
	{ LRG_GUI_KEY_SPACE, GRL_KEY_SPACE },
	{ LRG_GUI_KEY_ENTER, GRL_KEY_ENTER },
	{ LRG_GUI_KEY_ESCAPE, GRL_KEY_ESCAPE },
	{ LRG_GUI_KEY_MINUS, GRL_KEY_MINUS },
	{ LRG_GUI_KEY_EQUAL, GRL_KEY_EQUAL },
	{ LRG_GUI_KEY_LEFT_BRACKET, GRL_KEY_LEFT_BRACKET },
	{ LRG_GUI_KEY_RIGHT_BRACKET, GRL_KEY_RIGHT_BRACKET },
	{ LRG_GUI_KEY_SLASH, GRL_KEY_SLASH },
	{ LRG_GUI_KEY_BACKSLASH, GRL_KEY_BACKSLASH },
	{ LRG_GUI_KEY_COMMA, GRL_KEY_COMMA },
	{ LRG_GUI_KEY_PERIOD, GRL_KEY_PERIOD },
	{ LRG_GUI_KEY_SEMICOLON, GRL_KEY_SEMICOLON },
	{ LRG_GUI_KEY_APOSTROPHE, GRL_KEY_APOSTROPHE },
	{ LRG_GUI_KEY_GRAVE, GRL_KEY_GRAVE }
};

/*
 * gui_key_to_grl:
 *
 * The raylib key for an immediate-mode GUI key, or GRL_KEY_NULL.
 */
static GrlKey
gui_key_to_grl (LrgGuiKey key)
{
	gsize i;

	if (key >= LRG_GUI_KEY_A && key <= LRG_GUI_KEY_Z)
		return (GrlKey) (GRL_KEY_A + (gint) (key - LRG_GUI_KEY_A));
	if (key >= LRG_GUI_KEY_0 && key <= LRG_GUI_KEY_9)
		return (GrlKey) (GRL_KEY_ZERO + (gint) (key - LRG_GUI_KEY_0));
	if (key >= LRG_GUI_KEY_F1 && key <= LRG_GUI_KEY_F12)
		return (GrlKey) (GRL_KEY_F1 + (gint) (key - LRG_GUI_KEY_F1));
	if (key >= LRG_GUI_KEY_KP_0 && key <= LRG_GUI_KEY_KP_9)
		return (GrlKey) (GRL_KEY_KP_0 + (gint) (key - LRG_GUI_KEY_KP_0));

	for (i = 0; i < G_N_ELEMENTS (gui_grl_keys); i++)
		if (gui_grl_keys[i].gui == key)
			return gui_grl_keys[i].grl;
	return GRL_KEY_NULL;
}

/*
 * grl_key_to_gui:
 *
 * The inverse of gui_key_to_grl(); LRG_GUI_KEY_NONE for keys the
 * immediate-mode GUI has no name for (keypad operators, Pause, ...).
 */
static LrgGuiKey
grl_key_to_gui (GrlKey key)
{
	gsize i;

	if (key >= GRL_KEY_A && key <= GRL_KEY_Z)
		return (LrgGuiKey) (LRG_GUI_KEY_A + (gint) (key - GRL_KEY_A));
	if (key >= GRL_KEY_ZERO && key <= GRL_KEY_NINE)
		return (LrgGuiKey) (LRG_GUI_KEY_0 + (gint) (key - GRL_KEY_ZERO));
	if (key >= GRL_KEY_F1 && key <= GRL_KEY_F12)
		return (LrgGuiKey) (LRG_GUI_KEY_F1 + (gint) (key - GRL_KEY_F1));
	if (key >= GRL_KEY_KP_0 && key <= GRL_KEY_KP_9)
		return (LrgGuiKey) (LRG_GUI_KEY_KP_0 + (gint) (key - GRL_KEY_KP_0));

	for (i = 0; i < G_N_ELEMENTS (gui_grl_keys); i++)
		if (gui_grl_keys[i].grl == key)
			return gui_grl_keys[i].gui;
	return LRG_GUI_KEY_NONE;
}

/* Modifier flag translation in both directions. */
static LrgInputModifiers
gui_mods_to_input (LrgGuiModifiers mods)
{
	LrgInputModifiers m = LRG_INPUT_MODIFIER_NONE;

	if (mods & LRG_GUI_MOD_CTRL)
		m |= LRG_INPUT_MODIFIER_CTRL;
	if (mods & LRG_GUI_MOD_SHIFT)
		m |= LRG_INPUT_MODIFIER_SHIFT;
	if (mods & LRG_GUI_MOD_ALT)
		m |= LRG_INPUT_MODIFIER_ALT;
	if (mods & LRG_GUI_MOD_SUPER)
		m |= LRG_INPUT_MODIFIER_SUPER;
	return m;
}

static LrgGuiModifiers
input_mods_to_gui (LrgInputModifiers mods)
{
	LrgGuiModifiers m = LRG_GUI_MOD_NONE;

	if (mods & LRG_INPUT_MODIFIER_CTRL)
		m |= LRG_GUI_MOD_CTRL;
	if (mods & LRG_INPUT_MODIFIER_SHIFT)
		m |= LRG_GUI_MOD_SHIFT;
	if (mods & LRG_INPUT_MODIFIER_ALT)
		m |= LRG_GUI_MOD_ALT;
	if (mods & LRG_INPUT_MODIFIER_SUPER)
		m |= LRG_GUI_MOD_SUPER;
	return m;
}

/* ==========================================================================
 * Shortcut -> accelerator conversion
 * ========================================================================== */

/* 0.8.0 key names (case-insensitive) and the accelerator key name each
 * becomes. Letters, digits, F-keys and NumN are handled in code. */
static const struct
{
	const gchar *legacy;
	const gchar *accel;
} legacy_to_accel_names[] = {
	{ "Tab", "Tab" }, { "Left", "Left" }, { "Right", "Right" }, { "Up", "Up" },
	{ "Down", "Down" }, { "PageUp", "Page_Up" }, { "PageDown", "Page_Down" },
	{ "Home", "Home" }, { "End", "End" }, { "Insert", "Insert" },
	{ "Delete", "Delete" }, { "Del", "Delete" }, { "Backspace", "BackSpace" },
	{ "Space", "space" }, { "Enter", "Return" }, { "Return", "Return" },
	{ "Escape", "Escape" }, { "Esc", "Escape" },
	{ "-", "minus" }, { "Minus", "minus" }, { "=", "equal" }, { "Equal", "equal" },
	{ "Plus", "plus" }, { "[", "bracketleft" }, { "]", "bracketright" },
	{ "/", "slash" }, { "\\", "backslash" }, { ",", "comma" }, { ".", "period" },
	{ ";", "semicolon" }, { "'", "apostrophe" }, { "`", "grave" }
};

/*
 * legacy_key_to_accel_name:
 *
 * The accelerator key name for one 0.8.0 key token (newly allocated), or
 * NULL when the token is not a 0.8.0 key name. A token that is already an
 * accelerator key name ("comma", "KP_7", "Page_Up") is accepted as is, so
 * mixed spellings such as "Ctrl+comma" still convert.
 */
static gchar *
legacy_key_to_accel_name (const gchar *token)
{
	gsize len = strlen (token);
	gsize i;

	if (len == 0)
		return NULL;

	/* Letters (lower case, the accelerator convention) and digits */
	if (len == 1 && g_ascii_isalpha (token[0]))
		return g_strdup_printf ("%c", g_ascii_tolower (token[0]));
	if (len == 1 && g_ascii_isdigit (token[0]))
		return g_strdup (token);

	/* F1 .. F12 */
	if ((token[0] == 'F' || token[0] == 'f') && g_ascii_isdigit (token[1]))
	{
		gchar  *end = NULL;
		gint64  n = g_ascii_strtoll (token + 1, &end, 10);

		if (end != NULL && *end == '\0' && n >= 1 && n <= 12)
			return g_strdup_printf ("F%d", (gint) n);
		return NULL;
	}

	/* Num0 .. Num9 -> KP_0 .. KP_9 */
	if (len == 4 && g_ascii_strncasecmp (token, "Num", 3) == 0 && g_ascii_isdigit (token[3]))
		return g_strdup_printf ("KP_%c", token[3]);

	for (i = 0; i < G_N_ELEMENTS (legacy_to_accel_names); i++)
		if (g_ascii_strcasecmp (token, legacy_to_accel_names[i].legacy) == 0)
			return g_strdup (legacy_to_accel_names[i].accel);

	/* Already an accelerator key name */
	if (strchr (token, '<') == NULL && lrg_accel_parse (token, NULL, NULL))
		return g_strdup (token);

	return NULL;
}

/*
 * legacy_alt_to_accel:
 *
 * Converts one 0.8.0 alternative ("Ctrl+Shift+S", "Ctrl++", "Num7") to an
 * accelerator, or returns NULL. Modifiers are written Ctrl, Alt, Shift,
 * Super (the lrg_accel_name() order).
 */
static gchar *
legacy_alt_to_accel (const gchar *text)
{
	g_auto(GStrv)      parts = NULL;
	g_autofree gchar  *key_name = NULL;
	LrgInputModifiers  m = LRG_INPUT_MODIFIER_NONE;
	GString           *out;
	guint              i, n;

	/* "Ctrl++": a trailing empty pair of fields is the plus key. A lone
	 * "+" splits to two empty fields as well. */
	parts = g_strsplit (text, "+", -1);
	n = g_strv_length (parts);
	if (n >= 2 && parts[n - 1][0] == '\0' && parts[n - 2][0] == '\0')
	{
		g_free (parts[n - 2]);
		parts[n - 2] = g_strdup ("Plus");
		g_free (parts[n - 1]);
		parts[n - 1] = NULL;
		n--;
	}
	if (n == 0)
		return NULL;

	/* Every field but the last is a modifier */
	for (i = 0; i + 1 < n; i++)
	{
		g_autofree gchar *p = g_strstrip (g_strdup (parts[i]));

		if (g_ascii_strcasecmp (p, "Ctrl") == 0 || g_ascii_strcasecmp (p, "Control") == 0)
			m |= LRG_INPUT_MODIFIER_CTRL;
		else if (g_ascii_strcasecmp (p, "Shift") == 0)
			m |= LRG_INPUT_MODIFIER_SHIFT;
		else if (g_ascii_strcasecmp (p, "Alt") == 0 || g_ascii_strcasecmp (p, "Meta") == 0)
			m |= LRG_INPUT_MODIFIER_ALT;
		else if (g_ascii_strcasecmp (p, "Super") == 0 || g_ascii_strcasecmp (p, "Win") == 0 ||
		         g_ascii_strcasecmp (p, "Logo") == 0)
			m |= LRG_INPUT_MODIFIER_SUPER;
		else
			return NULL;
	}

	/* The last field is the key */
	{
		g_autofree gchar *p = g_strstrip (g_strdup (parts[n - 1]));

		key_name = legacy_key_to_accel_name (p);
		if (key_name == NULL)
			return NULL;
	}

	out = g_string_new (NULL);
	if (m & LRG_INPUT_MODIFIER_CTRL)
		g_string_append (out, "<Ctrl>");
	if (m & LRG_INPUT_MODIFIER_ALT)
		g_string_append (out, "<Alt>");
	if (m & LRG_INPUT_MODIFIER_SHIFT)
		g_string_append (out, "<Shift>");
	if (m & LRG_INPUT_MODIFIER_SUPER)
		g_string_append (out, "<Super>");
	g_string_append (out, key_name);

	/* Final validation against the toolkit parser */
	if (!lrg_accel_parse (out->str, NULL, NULL))
	{
		g_string_free (out, TRUE);
		return NULL;
	}
	return g_string_free (out, FALSE);
}

/*
 * alt_to_accel:
 *
 * One alternative in either syntax to an accelerator (newly allocated),
 * or NULL when invalid. Text with a "<" is accelerator syntax and passes
 * through unchanged (trimmed) once lrg_accel_parse() accepts it; anything
 * else goes through the 0.8.0 converter, which also accepts bare
 * accelerator key names.
 */
static gchar *
alt_to_accel (const gchar *alt)
{
	g_autofree gchar *t = g_strstrip (g_strdup (alt));

	if (*t == '\0')
		return NULL;
	if (strchr (t, '<') != NULL)
		return lrg_accel_parse (t, NULL, NULL) ? g_steal_pointer (&t) : NULL;
	return legacy_alt_to_accel (t);
}

gchar *
lrg_editor_shortcut_to_accel (const gchar *shortcut)
{
	g_auto(GStrv)  alts = NULL;
	GString       *out;
	guint          i;

	if (shortcut == NULL)
		return NULL;

	alts = g_strsplit (shortcut, "|", -1);
	if (alts[0] == NULL)
		return NULL;

	/* Strict: one bad alternative rejects the whole string, so a caller
	 * migrating a file can log and drop it as a unit. */
	out = g_string_new (NULL);
	for (i = 0; alts[i] != NULL; i++)
	{
		g_autofree gchar *accel = alt_to_accel (alts[i]);

		if (accel == NULL)
		{
			g_string_free (out, TRUE);
			return NULL;
		}
		if (i > 0)
			g_string_append_c (out, '|');
		g_string_append (out, accel);
	}
	return g_string_free (out, FALSE);
}

/*
 * shortcut_to_accelv:
 *
 * Lenient variant used for bindings: every valid alternative becomes one
 * element, invalid ones are dropped with a debug message. NULL or "" give
 * an empty vector (unbound).
 */
static GStrv
shortcut_to_accelv (const gchar *shortcut,
                    const gchar *id)
{
	GPtrArray     *arr = g_ptr_array_new ();
	g_auto(GStrv)  alts = NULL;
	guint          i;

	if (shortcut != NULL && *shortcut != '\0')
	{
		alts = g_strsplit (shortcut, "|", -1);
		for (i = 0; alts[i] != NULL; i++)
		{
			gchar *accel = alt_to_accel (alts[i]);

			if (accel != NULL)
				g_ptr_array_add (arr, accel);
			else
				g_debug ("editor actions: ignoring invalid shortcut '%s' for '%s'",
				         alts[i], id != NULL ? id : "?");
		}
	}
	g_ptr_array_add (arr, NULL);
	return (GStrv) g_ptr_array_free (arr, FALSE);
}

gchar *
lrg_editor_accel_to_label (const gchar *accel)
{
	g_auto(GStrv)  accels = NULL;
	GString       *out;
	guint          i;

	if (accel == NULL)
		return NULL;

	accels = shortcut_to_accelv (accel, NULL);
	out = g_string_new (NULL);
	for (i = 0; accels[i] != NULL; i++)
	{
		g_autofree gchar *label = lrg_accel_to_label (accels[i]);

		if (label == NULL)
			continue;
		if (out->len > 0)
			g_string_append (out, ", ");
		g_string_append (out, label);
	}
	if (out->len == 0)
	{
		g_string_free (out, TRUE);
		return NULL;
	}
	return g_string_free (out, FALSE);
}

/*
 * accelv_to_legacy:
 *
 * Writes accelerators back in the 0.8.0 syntax for the deprecated
 * string getters ("<Ctrl><Shift>z" -> "Ctrl+Shift+Z"). Keys the old GUI
 * cannot name use their accelerator label instead. NULL for an empty
 * vector.
 */
static gchar *
accelv_to_legacy (const gchar * const *accels)
{
	GString *out;
	guint    i;

	if (accels == NULL || accels[0] == NULL)
		return NULL;

	out = g_string_new (NULL);
	for (i = 0; accels[i] != NULL; i++)
	{
		GrlKey             key;
		LrgInputModifiers  mods;
		LrgGuiKey          gk;
		g_autofree gchar  *text = NULL;

		if (!lrg_accel_parse (accels[i], &key, &mods))
			continue;
		gk = grl_key_to_gui (key);
		text = (gk != LRG_GUI_KEY_NONE)
		       ? lrg_editor_shortcut_format (input_mods_to_gui (mods), gk)
		       : lrg_accel_to_label (accels[i]);
		if (text == NULL)
			continue;
		if (out->len > 0)
			g_string_append_c (out, '|');
		g_string_append (out, text);
	}
	if (out->len == 0)
	{
		g_string_free (out, TRUE);
		return NULL;
	}
	return g_string_free (out, FALSE);
}

/*
 * is_legacy_text:
 *
 * TRUE when every alternative of @text parses in the 0.8.0 syntax, in
 * which case the deprecated getters return the text untouched.
 */
static gboolean
is_legacy_text (const gchar *text)
{
	g_auto(GStrv) alts = g_strsplit (text, "|", -1);
	guint         i;

	if (alts[0] == NULL)
		return FALSE;
	for (i = 0; alts[i] != NULL; i++)
		if (!lrg_editor_shortcut_parse (alts[i], NULL, NULL))
			return FALSE;
	return TRUE;
}

/* ==========================================================================
 * Fuzzy matching
 * ========================================================================== */

gint
lrg_editor_fuzzy_score (const gchar *pattern,
                        const gchar *text)
{
	g_autofree gchar *p = NULL;
	g_autofree gchar *t = NULL;
	const gchar      *pp, *tp, *prev_match = NULL;
	gint              score = 1;

	if (pattern == NULL || text == NULL)
		return 0;
	if (*pattern == '\0')
		return 1;

	p = g_utf8_casefold (pattern, -1);
	t = g_utf8_casefold (text, -1);

	/* Exact substring beats everything. */
	if (strstr (t, p) != NULL)
		score += 100 + (g_str_has_prefix (t, p) ? 50 : 0);

	for (pp = p, tp = t; *pp != '\0'; pp = g_utf8_next_char (pp))
	{
		gunichar     pc = g_utf8_get_char (pp);
		const gchar *found = NULL;

		if (pc == ' ')
			continue;
		for (; *tp != '\0'; tp = g_utf8_next_char (tp))
		{
			if (g_utf8_get_char (tp) == pc)
			{
				found = tp;
				break;
			}
		}
		if (found == NULL)
			return 0;

		score += 1;
		if (prev_match != NULL && g_utf8_next_char (prev_match) == found)
			score += 5;   /* consecutive */
		if (found == t || strchr (" .:_-/", *(g_utf8_find_prev_char (t, found) != NULL
		                                      ? g_utf8_find_prev_char (t, found) : t)) != NULL)
			score += 8;   /* word start */
		prev_match = found;
		tp = g_utf8_next_char (found);
	}

	/* Shorter candidates win ties. */
	score -= (gint) MIN (20, strlen (t) / 8);
	return MAX (score, 1);
}

/* ==========================================================================
 * Registry
 * ========================================================================== */

/*
 * Action:
 *
 * One registered command. @default_accels is @shortcut converted once at
 * registration; @accels is the effective set last announced through
 * ::accels-changed. @state_known / @last_enabled / @last_checked are what
 * lrg_editor_actions_refresh_state() compares against. The two legacy
 * strings back the transfer-none deprecated getters.
 */
typedef struct
{
	gchar                    *id;
	gchar                    *label;
	gchar                    *category;
	gchar                    *shortcut;
	GStrv                     default_accels;
	GStrv                     accels;
	gchar                    *legacy_default;
	gchar                    *legacy_effective;
	LrgEditorActionFunc       activate;
	LrgEditorActionStateFunc  enabled;
	LrgEditorActionStateFunc  checked;
	gpointer                  user_data;
	gboolean                  state_known;
	gboolean                  last_enabled;
	gboolean                  last_checked;
} Action;

static void
action_free (gpointer data)
{
	Action *a = data;

	g_free (a->id);
	g_free (a->label);
	g_free (a->category);
	g_free (a->shortcut);
	g_strfreev (a->default_accels);
	g_strfreev (a->accels);
	g_free (a->legacy_default);
	g_free (a->legacy_effective);
	g_free (a);
}

struct _LrgEditorActions
{
	GObject            parent_instance;

	LrgEditorSettings *settings;   /* ref, nullable */
	GHashTable        *actions;    /* id -> Action */
	GHashTable        *overrides;  /* id -> accel string; used without settings */
};

static void lrg_editor_actions_action_group_init (GActionGroupInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgEditorActions, lrg_editor_actions, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_ACTION_GROUP,
                                                      lrg_editor_actions_action_group_init))

enum
{
	SIGNAL_ACTIVATED,
	SIGNAL_ACCELS_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static Action *
lookup (LrgEditorActions *self,
        const gchar      *id)
{
	return (id != NULL) ? g_hash_table_lookup (self->actions, id) : NULL;
}

/*
 * effective_text:
 *
 * The binding string in force for @id: a settings override, else a
 * registry override, else @fallback (the default). "" means unbound.
 */
static const gchar *
effective_text (LrgEditorActions *self,
                const gchar      *id,
                const gchar      *fallback)
{
	const gchar *user = NULL;

	if (self->settings != NULL)
		user = lrg_editor_settings_get_keybinding (self->settings, id);
	if (user == NULL)
		user = g_hash_table_lookup (self->overrides, id);
	return (user != NULL) ? user : fallback;
}

/* The effective accelerators of @a, newly allocated (never NULL). */
static GStrv
compute_accels (LrgEditorActions *self,
                Action           *a)
{
	return shortcut_to_accelv (effective_text (self, a->id, a->shortcut), a->id);
}

/*
 * sorted_ids:
 *
 * A snapshot of the registered ids (copies), sorted by strcmp, so loops
 * that emit signals survive handlers that add or replace actions.
 */
static GStrv
sorted_ids (LrgEditorActions *self)
{
	GPtrArray      *arr = g_ptr_array_new ();
	GHashTableIter  iter;
	gpointer        key;

	g_hash_table_iter_init (&iter, self->actions);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_ptr_array_add (arr, g_strdup (key));
	g_ptr_array_sort_values (arr, (GCompareFunc) g_strcmp0);
	g_ptr_array_add (arr, NULL);
	return (GStrv) g_ptr_array_free (arr, FALSE);
}

/*
 * recheck_accels:
 *
 * Recomputes every action's effective accelerators and emits
 * ::accels-changed for those that differ from the last announced set.
 */
static void
recheck_accels (LrgEditorActions *self)
{
	g_auto(GStrv) ids = sorted_ids (self);
	guint         i;

	for (i = 0; ids[i] != NULL; i++)
	{
		Action *a = lookup (self, ids[i]);
		GStrv   now;

		if (a == NULL)
			continue;
		now = compute_accels (self, a);
		if (a->accels != NULL && g_strv_equal ((const gchar * const *) now,
		                                       (const gchar * const *) a->accels))
		{
			g_strfreev (now);
			continue;
		}
		g_strfreev (a->accels);
		a->accels = now;
		g_signal_emit (self, signals[SIGNAL_ACCELS_CHANGED], g_quark_from_string (ids[i]), ids[i]);
	}
}

static void
on_keybindings_changed (LrgEditorActions *self)
{
	recheck_accels (self);
}

/* Live callback values (no caching). */
static gboolean
eval_enabled (Action *a)
{
	return a->enabled == NULL || a->enabled (a->user_data);
}

static gboolean
eval_checked (Action *a)
{
	return a->checked != NULL && a->checked (a->user_data);
}

/* ==========================================================================
 * GActionGroup
 * ========================================================================== */

static gchar **
ag_list_actions (GActionGroup *group)
{
	return sorted_ids (LRG_EDITOR_ACTIONS (group));
}

/*
 * ag_query_action:
 *
 * Every action is parameterless; toggles (a checked callback) carry a
 * boolean state. The first query of an action seeds the refresh cache so
 * a later refresh only reports real changes.
 */
static gboolean
ag_query_action (GActionGroup        *group,
                 const gchar         *action_name,
                 gboolean            *enabled,
                 const GVariantType **parameter_type,
                 const GVariantType **state_type,
                 GVariant           **state_hint,
                 GVariant           **state)
{
	LrgEditorActions *self = LRG_EDITOR_ACTIONS (group);
	Action           *a = lookup (self, action_name);
	gboolean          en, ch;

	if (a == NULL)
		return FALSE;

	en = eval_enabled (a);
	ch = eval_checked (a);
	if (!a->state_known)
	{
		a->state_known = TRUE;
		a->last_enabled = en;
		a->last_checked = ch;
	}

	if (enabled != NULL)
		*enabled = en;
	if (parameter_type != NULL)
		*parameter_type = NULL;
	if (state_type != NULL)
		*state_type = (a->checked != NULL) ? G_VARIANT_TYPE_BOOLEAN : NULL;
	if (state_hint != NULL)
		*state_hint = NULL;
	if (state != NULL)
		*state = (a->checked != NULL) ? g_variant_ref_sink (g_variant_new_boolean (ch)) : NULL;
	return TRUE;
}

/*
 * ag_change_action_state:
 *
 * A toggle is driven to the requested boolean by running its activate
 * callback when the current value differs (the callbacks toggle).
 */
static void
ag_change_action_state (GActionGroup *group,
                        const gchar  *action_name,
                        GVariant     *value)
{
	LrgEditorActions    *self = LRG_EDITOR_ACTIONS (group);
	Action              *a = lookup (self, action_name);
	g_autoptr(GVariant)  owned = NULL;

	/* A floating value is consumed, per the GActionGroup contract. */
	if (value != NULL)
		owned = g_variant_ref_sink (value);

	if (a == NULL || a->checked == NULL || owned == NULL ||
	    !g_variant_is_of_type (owned, G_VARIANT_TYPE_BOOLEAN))
	{
		g_debug ("editor actions: cannot change the state of '%s'", action_name);
		return;
	}

	if (eval_checked (a) != g_variant_get_boolean (owned))
		lrg_editor_actions_activate (self, action_name);
}

/*
 * ag_activate_action:
 *
 * Without a parameter, runs the action. A boolean parameter on a toggle
 * sets its state (like change_action_state); any other parameter is
 * rejected, since no editor action takes one.
 */
static void
ag_activate_action (GActionGroup *group,
                    const gchar  *action_name,
                    GVariant     *parameter)
{
	LrgEditorActions *self = LRG_EDITOR_ACTIONS (group);
	Action           *a = lookup (self, action_name);

	if (a == NULL)
	{
		g_debug ("editor actions: no action '%s'", action_name);
		return;
	}

	if (parameter != NULL)
	{
		g_autoptr(GVariant) owned = g_variant_ref_sink (parameter);

		if (a->checked != NULL && g_variant_is_of_type (owned, G_VARIANT_TYPE_BOOLEAN))
			ag_change_action_state (group, action_name, owned);
		else
			g_debug ("editor actions: '%s' takes no parameter; ignored", action_name);
		return;
	}

	lrg_editor_actions_activate (self, action_name);
}

static void
lrg_editor_actions_action_group_init (GActionGroupInterface *iface)
{
	iface->list_actions = ag_list_actions;
	iface->query_action = ag_query_action;
	iface->activate_action = ag_activate_action;
	iface->change_action_state = ag_change_action_state;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_actions_finalize (GObject *object)
{
	LrgEditorActions *self = LRG_EDITOR_ACTIONS (object);

	g_clear_object (&self->settings);
	g_hash_table_unref (self->actions);
	g_hash_table_unref (self->overrides);

	G_OBJECT_CLASS (lrg_editor_actions_parent_class)->finalize (object);
}

static void
lrg_editor_actions_class_init (LrgEditorActionsClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_actions_finalize;

	/**
	 * LrgEditorActions::activated:
	 * @self: the registry
	 * @id: the action that ran
	 *
	 * Emitted after an action ran (for status messages and IPC events),
	 * whether through lrg_editor_actions_activate() or the #GActionGroup
	 * interface.
	 */
	signals[SIGNAL_ACTIVATED] =
		g_signal_new ("activated",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgEditorActions::accels-changed:
	 * @self: the registry
	 * @id: the action whose effective accelerators changed
	 *
	 * Emitted when an action's effective accelerators change, through
	 * lrg_editor_actions_set_accels(), a settings keybinding change or a
	 * re-registration with a different default. The signal is detailed by
	 * the action id ("accels-changed::file.save"). Use
	 * lrg_editor_actions_get_accels() for the new value.
	 */
	signals[SIGNAL_ACCELS_CHANGED] =
		g_signal_new ("accels-changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_editor_actions_init (LrgEditorActions *self)
{
	self->actions = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, action_free);
	self->overrides = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

LrgEditorActions *
lrg_editor_actions_new (LrgEditorSettings *settings)
{
	LrgEditorActions *self = g_object_new (LRG_TYPE_EDITOR_ACTIONS, NULL);

	if (settings != NULL)
	{
		self->settings = g_object_ref (settings);
		g_signal_connect_object (settings, "keybindings-changed",
		                         G_CALLBACK (on_keybindings_changed), self, G_CONNECT_SWAPPED);
	}
	return self;
}

void
lrg_editor_actions_add (LrgEditorActions         *self,
                        const gchar              *id,
                        const gchar              *label,
                        const gchar              *category,
                        const gchar              *shortcut,
                        LrgEditorActionFunc       activate,
                        LrgEditorActionStateFunc  enabled,
                        LrgEditorActionStateFunc  checked,
                        gpointer                  user_data)
{
	Action        *a;
	Action        *old;
	g_auto(GStrv)  old_accels = NULL;
	gboolean       replaced;

	g_return_if_fail (LRG_IS_EDITOR_ACTIONS (self));
	g_return_if_fail (id != NULL && label != NULL && activate != NULL);
	g_return_if_fail (g_action_name_is_valid (id));

	/* Build the record; both shortcut syntaxes convert here. */
	a = g_new0 (Action, 1);
	a->id = g_strdup (id);
	a->label = g_strdup (label);
	a->category = g_strdup (category != NULL ? category : "");
	a->shortcut = g_strdup (shortcut);
	a->default_accels = shortcut_to_accelv (shortcut, id);
	a->activate = activate;
	a->enabled = enabled;
	a->checked = checked;
	a->user_data = user_data;
	a->accels = compute_accels (self, a);

	/* Replacing: announce the removal first, as GActionGroup expects. */
	old = lookup (self, id);
	replaced = (old != NULL);
	if (replaced)
	{
		old_accels = g_steal_pointer (&old->accels);
		g_hash_table_remove (self->actions, id);
		g_action_group_action_removed (G_ACTION_GROUP (self), id);
	}

	g_hash_table_insert (self->actions, a->id, a);
	g_action_group_action_added (G_ACTION_GROUP (self), id);

	if (replaced && (old_accels == NULL ||
	                 !g_strv_equal ((const gchar * const *) old_accels,
	                                (const gchar * const *) a->accels)))
		g_signal_emit (self, signals[SIGNAL_ACCELS_CHANGED], g_quark_from_string (id), id);
}

gboolean
lrg_editor_actions_has (LrgEditorActions *self,
                        const gchar      *id)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), FALSE);

	return lookup (self, id) != NULL;
}

const gchar *
lrg_editor_actions_get_label (LrgEditorActions *self,
                              const gchar      *id)
{
	Action *a;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), NULL);

	a = lookup (self, id);
	return (a != NULL) ? a->label : NULL;
}

const gchar *
lrg_editor_actions_get_category (LrgEditorActions *self,
                                 const gchar      *id)
{
	Action *a;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), NULL);

	a = lookup (self, id);
	return (a != NULL) ? a->category : NULL;
}

/*
 * legacy_cache:
 *
 * Stores @text in @slot only when it differs, so a pointer handed out by
 * a deprecated getter stays valid while the binding is unchanged; returns
 * the cached string. Takes ownership of @text.
 */
static const gchar *
legacy_cache (gchar **slot,
              gchar  *text)
{
	if (g_strcmp0 (*slot, text) != 0)
	{
		g_free (*slot);
		*slot = text;
	}
	else
	{
		g_free (text);
	}
	return *slot;
}

const gchar *
lrg_editor_actions_get_default_shortcut (LrgEditorActions *self,
                                         const gchar      *id)
{
	Action *a;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), NULL);

	a = lookup (self, id);
	if (a == NULL || a->shortcut == NULL || *a->shortcut == '\0')
		return NULL;

	/* 0.8.0 text is returned as written; accelerators are converted. */
	if (is_legacy_text (a->shortcut))
		return a->shortcut;
	return legacy_cache (&a->legacy_default,
	                     accelv_to_legacy ((const gchar * const *) a->default_accels));
}

const gchar *
lrg_editor_actions_get_shortcut (LrgEditorActions *self,
                                 const gchar      *id)
{
	Action           *a;
	const gchar      *text;
	g_auto(GStrv)     accels = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), NULL);

	a = lookup (self, id);
	if (a == NULL)
		return NULL;

	/* A user override wins; an empty override unbinds. */
	text = effective_text (self, id, a->shortcut);
	if (text == NULL || *text == '\0')
		return NULL;
	if (is_legacy_text (text))
		return text;

	accels = shortcut_to_accelv (text, id);
	return legacy_cache (&a->legacy_effective, accelv_to_legacy ((const gchar * const *) accels));
}

GStrv
lrg_editor_actions_get_accels (LrgEditorActions *self,
                               const gchar      *id)
{
	Action *a;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), NULL);

	a = lookup (self, id);
	return (a != NULL) ? compute_accels (self, a) : NULL;
}

GStrv
lrg_editor_actions_get_default_accels (LrgEditorActions *self,
                                       const gchar      *id)
{
	Action *a;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), NULL);

	a = lookup (self, id);
	return (a != NULL) ? g_strdupv (a->default_accels) : NULL;
}

gboolean
lrg_editor_actions_set_accels (LrgEditorActions    *self,
                               const gchar         *id,
                               const gchar * const *accels)
{
	g_autofree gchar *joined = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	/* Validate and normalise every element before touching anything. */
	if (accels != NULL)
	{
		GString *out = g_string_new (NULL);
		guint    i;

		for (i = 0; accels[i] != NULL; i++)
		{
			g_autofree gchar *conv = lrg_editor_shortcut_to_accel (accels[i]);

			if (conv == NULL)
			{
				g_string_free (out, TRUE);
				return FALSE;
			}
			if (i > 0)
				g_string_append_c (out, '|');
			g_string_append (out, conv);
		}
		joined = g_string_free (out, FALSE);
	}

	/* With settings the override is persisted there; the settings'
	 * keybindings-changed signal brings us back to recheck_accels(). */
	if (self->settings != NULL)
	{
		lrg_editor_settings_set_keybinding (self->settings, id, joined);
		return TRUE;
	}

	if (lookup (self, id) == NULL)
		return FALSE;
	if (joined == NULL)
		g_hash_table_remove (self->overrides, id);
	else
		g_hash_table_insert (self->overrides, g_strdup (id), g_steal_pointer (&joined));
	recheck_accels (self);
	return TRUE;
}

gboolean
lrg_editor_actions_is_enabled (LrgEditorActions *self,
                               const gchar      *id)
{
	Action *a;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), FALSE);

	a = lookup (self, id);
	return a != NULL && eval_enabled (a);
}

gboolean
lrg_editor_actions_is_checked (LrgEditorActions *self,
                               const gchar      *id)
{
	Action *a;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), FALSE);

	a = lookup (self, id);
	return a != NULL && eval_checked (a);
}

gboolean
lrg_editor_actions_activate (LrgEditorActions *self,
                             const gchar      *id)
{
	Action           *a;
	g_autofree gchar *id_copy = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), FALSE);

	a = lookup (self, id);
	if (a == NULL || !eval_enabled (a))
		return FALSE;

	/* The action may re-register actions; keep the id alive. */
	id_copy = g_strdup (a->id);
	a->activate (a->user_data);
	g_signal_emit (self, signals[SIGNAL_ACTIVATED], 0, id_copy);

	/* Running a command usually changes what is enabled or checked
	 * (undo, selection, toggles): report it right away. */
	lrg_editor_actions_refresh_state (self);
	return TRUE;
}

guint
lrg_editor_actions_refresh_state (LrgEditorActions *self)
{
	g_auto(GStrv) ids = NULL;
	guint         i;
	guint         n_emitted = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), 0);

	ids = sorted_ids (self);
	for (i = 0; ids[i] != NULL; i++)
	{
		Action   *a = lookup (self, ids[i]);
		gboolean  en, ch, has_state;

		if (a == NULL)
			continue;

		en = eval_enabled (a);
		ch = eval_checked (a);
		has_state = (a->checked != NULL);

		/* First sight: record silently. */
		if (!a->state_known)
		{
			a->state_known = TRUE;
			a->last_enabled = en;
			a->last_checked = ch;
			continue;
		}

		/* Update the cache before emitting: handlers may query or even
		 * replace the action, so @a is not used after an emission. */
		if (en != a->last_enabled)
		{
			a->last_enabled = en;
			g_action_group_action_enabled_changed (G_ACTION_GROUP (self), ids[i], en);
			n_emitted++;
			a = lookup (self, ids[i]);
			if (a == NULL)
				continue;
		}
		if (has_state && ch != a->last_checked)
		{
			g_autoptr(GVariant) state = g_variant_ref_sink (g_variant_new_boolean (ch));

			a->last_checked = ch;
			g_action_group_action_state_changed (G_ACTION_GROUP (self), ids[i], state);
			n_emitted++;
		}
	}
	return n_emitted;
}

static gint
compare_actions (gconstpointer a,
                 gconstpointer b,
                 gpointer      user_data)
{
	LrgEditorActions *self = user_data;
	Action           *x = lookup (self, a);
	Action           *y = lookup (self, b);
	gint              c;

	if (x == NULL || y == NULL)
		return (x == NULL) - (y == NULL);

	c = g_strcmp0 (x->category, y->category);
	return (c != 0) ? c : g_utf8_collate (x->label, y->label);
}

GList *
lrg_editor_actions_list (LrgEditorActions *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), NULL);

	return g_list_sort_with_data (g_hash_table_get_keys (self->actions), compare_actions, self);
}

typedef struct
{
	const gchar *id;
	gint         score;
} Scored;

static gint
compare_scored (gconstpointer a,
                gconstpointer b)
{
	const Scored *x = a;
	const Scored *y = b;

	if (x->score != y->score)
		return y->score - x->score;
	return g_strcmp0 (x->id, y->id);
}

GList *
lrg_editor_actions_search (LrgEditorActions *self,
                           const gchar      *query)
{
	GArray        *scored;
	GHashTableIter iter;
	gpointer       key, value;
	GList         *out = NULL;
	gint           i;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), NULL);

	if (query == NULL || *query == '\0')
		return lrg_editor_actions_list (self);

	scored = g_array_new (FALSE, FALSE, sizeof (Scored));
	g_hash_table_iter_init (&iter, self->actions);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		Action           *a = value;
		g_autofree gchar *full = g_strdup_printf ("%s: %s", a->category, a->label);
		Scored            s;

		s.id = a->id;
		s.score = MAX (lrg_editor_fuzzy_score (query, a->label) + 10,
		               MAX (lrg_editor_fuzzy_score (query, full), lrg_editor_fuzzy_score (query, a->id)));
		if (lrg_editor_fuzzy_score (query, full) == 0 && lrg_editor_fuzzy_score (query, a->id) == 0 &&
		    lrg_editor_fuzzy_score (query, a->label) == 0)
			continue;
		g_array_append_val (scored, s);
	}

	g_array_sort (scored, compare_scored);
	for (i = (gint) scored->len - 1; i >= 0; i--)
		out = g_list_prepend (out, (gpointer) g_array_index (scored, Scored, i).id);
	g_array_unref (scored);

	return out;
}

const gchar *
lrg_editor_actions_find_by_accel (LrgEditorActions  *self,
                                  GrlKey             key,
                                  LrgInputModifiers  mods)
{
	g_auto(GStrv) ids = NULL;
	guint         i, j;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), NULL);

	if (key == GRL_KEY_NULL)
		return NULL;
	mods &= ACCEL_MOD_MASK;

	/* Sorted ids: deterministic winner when a chord is bound twice. */
	ids = sorted_ids (self);
	for (i = 0; ids[i] != NULL; i++)
	{
		Action        *a = lookup (self, ids[i]);
		g_auto(GStrv)  accels = NULL;

		if (a == NULL)
			continue;
		accels = compute_accels (self, a);
		for (j = 0; accels[j] != NULL; j++)
		{
			GrlKey            k;
			LrgInputModifiers m;

			if (!lrg_accel_parse (accels[j], &k, &m))
				continue;
			if (k == key && m == mods && eval_enabled (a))
				return a->id;
		}
	}
	return NULL;
}

const gchar *
lrg_editor_actions_find_by_shortcut (LrgEditorActions *self,
                                     LrgGuiModifiers   mods,
                                     LrgGuiKey         key)
{
	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), NULL);

	return lrg_editor_actions_find_by_accel (self, gui_key_to_grl (key), gui_mods_to_input (mods));
}

GPtrArray *
lrg_editor_actions_find_conflicts (LrgEditorActions *self)
{
	g_autoptr(GHashTable) by_chord = NULL;
	GPtrArray            *out;
	GList                *ids, *l;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), NULL);

	out = g_ptr_array_new_with_free_func (g_free);
	by_chord = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	/* Key every alternative by its canonical accelerator, so "<Ctrl>plus"
	 * and "<Ctrl>equal" (the same physical key) collide. */
	ids = lrg_editor_actions_list (self);
	for (l = ids; l != NULL; l = l->next)
	{
		Action        *a = lookup (self, l->data);
		g_auto(GStrv)  accels = NULL;
		guint          i;

		if (a == NULL)
			continue;
		accels = compute_accels (self, a);
		for (i = 0; accels[i] != NULL; i++)
		{
			GrlKey             k;
			LrgInputModifiers  m;
			g_autofree gchar  *canon = NULL;
			const gchar       *prev;

			if (!lrg_accel_parse (accels[i], &k, &m))
				continue;
			canon = lrg_accel_name (k, m);
			if (canon == NULL)
				continue;
			prev = g_hash_table_lookup (by_chord, canon);
			if (prev != NULL)
			{
				g_autofree gchar *label = lrg_accel_to_label (canon);

				g_ptr_array_add (out, g_strdup_printf ("%s: %s and %s",
				                                       label != NULL ? label : canon, prev,
				                                       (const gchar *) l->data));
			}
			else
			{
				g_hash_table_insert (by_chord, g_strdup (canon), g_strdup (l->data));
			}
		}
	}
	g_list_free (ids);

	return out;
}
