/* lrg-editor-actions.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Named editor commands with shortcuts. See lrg-editor-actions.h.
 */

#include "lrg-editor-actions.h"
#include "lrg-editor-settings.h"
#include <string.h>

/* ==========================================================================
 * Shortcut text
 * ========================================================================== */

static const struct
{
	const gchar *name;
	LrgGuiKey    key;
} key_names[] = {
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

	for (i = 0; i < G_N_ELEMENTS (key_names); i++)
		if (g_ascii_strcasecmp (name, key_names[i].name) == 0)
		{
			*key = key_names[i].key;
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
		for (i = 0; i < G_N_ELEMENTS (key_names); i++)
			if (key_names[i].key == key)
			{
				g_string_append (out, key_names[i].name);
				break;
			}
	}

	return g_string_free (out, FALSE);
}

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

typedef struct
{
	gchar                    *id;
	gchar                    *label;
	gchar                    *category;
	gchar                    *shortcut;
	LrgEditorActionFunc       activate;
	LrgEditorActionStateFunc  enabled;
	LrgEditorActionStateFunc  checked;
	gpointer                  user_data;
} Action;

static void
action_free (gpointer data)
{
	Action *a = data;

	g_free (a->id);
	g_free (a->label);
	g_free (a->category);
	g_free (a->shortcut);
	g_free (a);
}

struct _LrgEditorActions
{
	GObject            parent_instance;

	LrgEditorSettings *settings;   /* ref, nullable */
	GHashTable        *actions;    /* id -> Action */
};

G_DEFINE_FINAL_TYPE (LrgEditorActions, lrg_editor_actions, G_TYPE_OBJECT)

enum
{
	SIGNAL_ACTIVATED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_editor_actions_finalize (GObject *object)
{
	LrgEditorActions *self = LRG_EDITOR_ACTIONS (object);

	g_clear_object (&self->settings);
	g_hash_table_unref (self->actions);

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
	 * Emitted after an action ran (for status messages and IPC events).
	 */
	signals[SIGNAL_ACTIVATED] =
		g_signal_new ("activated",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_editor_actions_init (LrgEditorActions *self)
{
	self->actions = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, action_free);
}

LrgEditorActions *
lrg_editor_actions_new (LrgEditorSettings *settings)
{
	LrgEditorActions *self = g_object_new (LRG_TYPE_EDITOR_ACTIONS, NULL);

	if (settings != NULL)
		self->settings = g_object_ref (settings);
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
	Action *a;

	g_return_if_fail (LRG_IS_EDITOR_ACTIONS (self));
	g_return_if_fail (id != NULL && label != NULL && activate != NULL);

	a = g_new0 (Action, 1);
	a->id = g_strdup (id);
	a->label = g_strdup (label);
	a->category = g_strdup (category != NULL ? category : "");
	a->shortcut = g_strdup (shortcut);
	a->activate = activate;
	a->enabled = enabled;
	a->checked = checked;
	a->user_data = user_data;
	g_hash_table_replace (self->actions, a->id, a);
}

static Action *
lookup (LrgEditorActions *self,
        const gchar      *id)
{
	return (id != NULL) ? g_hash_table_lookup (self->actions, id) : NULL;
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

const gchar *
lrg_editor_actions_get_default_shortcut (LrgEditorActions *self,
                                         const gchar      *id)
{
	Action *a;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), NULL);

	a = lookup (self, id);
	return (a != NULL) ? a->shortcut : NULL;
}

const gchar *
lrg_editor_actions_get_shortcut (LrgEditorActions *self,
                                 const gchar      *id)
{
	Action      *a;
	const gchar *user;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), NULL);

	a = lookup (self, id);
	if (a == NULL)
		return NULL;

	/* A user override wins; an empty override unbinds. */
	user = (self->settings != NULL) ? lrg_editor_settings_get_keybinding (self->settings, id) : NULL;
	if (user != NULL)
		return (*user != '\0') ? user : NULL;

	return a->shortcut;
}

gboolean
lrg_editor_actions_is_enabled (LrgEditorActions *self,
                               const gchar      *id)
{
	Action *a;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), FALSE);

	a = lookup (self, id);
	return a != NULL && (a->enabled == NULL || a->enabled (a->user_data));
}

gboolean
lrg_editor_actions_is_checked (LrgEditorActions *self,
                               const gchar      *id)
{
	Action *a;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), FALSE);

	a = lookup (self, id);
	return a != NULL && a->checked != NULL && a->checked (a->user_data);
}

gboolean
lrg_editor_actions_activate (LrgEditorActions *self,
                             const gchar      *id)
{
	Action           *a;
	g_autofree gchar *id_copy = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), FALSE);

	a = lookup (self, id);
	if (a == NULL || (a->enabled != NULL && !a->enabled (a->user_data)))
		return FALSE;

	/* The action may re-register actions; keep the id alive. */
	id_copy = g_strdup (a->id);
	a->activate (a->user_data);
	g_signal_emit (self, signals[SIGNAL_ACTIVATED], 0, id_copy);
	return TRUE;
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
lrg_editor_actions_find_by_shortcut (LrgEditorActions *self,
                                     LrgGuiModifiers   mods,
                                     LrgGuiKey         key)
{
	GHashTableIter iter;
	gpointer       k, v;

	g_return_val_if_fail (LRG_IS_EDITOR_ACTIONS (self), NULL);

	g_hash_table_iter_init (&iter, self->actions);
	while (g_hash_table_iter_next (&iter, &k, &v))
	{
		Action         *a = v;
		const gchar    *text = lrg_editor_actions_get_shortcut (self, a->id);
		g_auto(GStrv)   alts = NULL;
		guint           i;

		if (text == NULL)
			continue;

		/* "Ctrl+Shift+Z|Ctrl+Y": any alternative triggers the action. */
		alts = g_strsplit (text, "|", -1);
		for (i = 0; alts[i] != NULL; i++)
		{
			LrgGuiModifiers m;
			LrgGuiKey       sk;

			if (!lrg_editor_shortcut_parse (alts[i], &m, &sk))
				continue;
			if (m == mods && sk == key && (a->enabled == NULL || a->enabled (a->user_data)))
				return a->id;
		}
	}

	return NULL;
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

	ids = lrg_editor_actions_list (self);
	for (l = ids; l != NULL; l = l->next)
	{
		const gchar   *text = lrg_editor_actions_get_shortcut (self, l->data);
		g_auto(GStrv)  alts = NULL;
		guint          i;

		if (text == NULL)
			continue;

		alts = g_strsplit (text, "|", -1);
		for (i = 0; alts[i] != NULL; i++)
		{
			LrgGuiModifiers   m;
			LrgGuiKey         k;
			g_autofree gchar *canon = NULL;
			const gchar      *prev;

			if (!lrg_editor_shortcut_parse (alts[i], &m, &k))
				continue;
			canon = lrg_editor_shortcut_format (m, k);
			prev = g_hash_table_lookup (by_chord, canon);
			if (prev != NULL)
				g_ptr_array_add (out, g_strdup_printf ("%s: %s and %s", canon, prev,
				                                       (const gchar *) l->data));
			else
				g_hash_table_insert (by_chord, g_strdup (canon), g_strdup (l->data));
		}
	}
	g_list_free (ids);

	return out;
}
