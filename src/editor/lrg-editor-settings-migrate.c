/* lrg-editor-settings-migrate.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Migration of 0.8.0 editor settings to the 0.8.1 format.
 *
 * The migration is a handful of pure conversions (layout string to dock
 * YAML, shortcut syntax to accelerators, legacy theme names) applied to
 * an already loaded #LrgEditorSettings, bracketed by a one-time backup of
 * the file and a save with the new version number. The version number is
 * what makes it idempotent: a migrated file loads as version 2 and
 * lrg_editor_settings_migrate() returns before touching anything.
 */

#include "config.h"

#include "lrg-editor-settings-migrate.h"
#include "lrg-editor-actions.h"
#include <yaml-glib.h>
#include <gio/gio.h>
#include <string.h>
#include <math.h>

/* The 0.8.0 dock refused deeper trees; so do we. */
#define LEGACY_LAYOUT_MAX_DEPTH (64)

/* Ratio limits the 0.8.0 dock clamped every split to. */
#define LEGACY_RATIO_MIN (0.05)
#define LEGACY_RATIO_MAX (0.95)

/* 0.8.0 theme names and their 0.8.1 values. */
static const struct
{
	const gchar *legacy;
	const gchar *current;
} legacy_theme_map[] = {
	{ "mocha", "catppuccin-mocha" },
	{ "latte", "catppuccin-latte" },
	{ "macchiato", "catppuccin-macchiato" },
	{ "frappe", "catppuccin-frappe" }
};

/* ==========================================================================
 * Layout: the 0.8.0 LrgGuiDock grammar
 *
 *   node  := split | tabs
 *   split := "split(" ("v" | "h") "," ratio "," node "," node ")"
 *   tabs  := "tabs(" active ("," id)* ")"
 *
 * Words are [A-Za-z0-9_.-]+; blanks (space, tab, newline) are allowed
 * between tokens. This is a port of parse_node() in src/gui/lrg-gui-dock.c
 * (0.8.0) that emits LrgDockArea YAML through a YamlBuilder instead of
 * building dock nodes.
 * ========================================================================== */

typedef struct
{
	const gchar  *p;
	GError      **error;
} LayoutParser;

static void
layout_skip_ws (LayoutParser *ps)
{
	while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n')
		ps->p++;
}

/*
 * layout_expect:
 *
 * Consumes @tok (after blanks) or sets the parse error.
 */
static gboolean
layout_expect (LayoutParser *ps,
               const gchar  *tok)
{
	layout_skip_ws (ps);
	if (!g_str_has_prefix (ps->p, tok))
	{
		g_set_error (ps->error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		             "legacy layout: expected '%s' at '%.20s'", tok, ps->p);
		return FALSE;
	}
	ps->p += strlen (tok);
	return TRUE;
}

/*
 * layout_word:
 *
 * The next word, or %NULL if there is none at the cursor.
 */
static gchar *
layout_word (LayoutParser *ps)
{
	const gchar *start;

	layout_skip_ws (ps);
	start = ps->p;
	while (g_ascii_isalnum (*ps->p) || *ps->p == '_' || *ps->p == '-' || *ps->p == '.')
		ps->p++;
	return (ps->p > start) ? g_strndup (start, (gsize) (ps->p - start)) : NULL;
}

static gboolean layout_node (LayoutParser *ps,
                             YamlBuilder  *b,
                             guint         depth);

/*
 * layout_split:
 *
 * Parses the body of "split(" (already consumed) and emits
 * { split, ratio, first, second }. The old 'v' (a vertical divider,
 * children side by side) is LrgDockArea's horizontal split; 'h' (children
 * stacked) is its vertical split.
 */
static gboolean
layout_split (LayoutParser *ps,
              YamlBuilder  *b,
              guint         depth)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *ratio_word = NULL;
	gdouble           ratio = 0.5;

	/* Direction */
	dir = layout_word (ps);
	if (dir == NULL || (strcmp (dir, "v") != 0 && strcmp (dir, "h") != 0))
	{
		g_set_error_literal (ps->error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                     "legacy layout: split direction must be v or h");
		return FALSE;
	}
	if (!layout_expect (ps, ","))
		return FALSE;

	/* Ratio: parsed leniently and clamped, like 0.8.0 */
	ratio_word = layout_word (ps);
	if (ratio_word != NULL)
		ratio = g_ascii_strtod (ratio_word, NULL);
	if (!isfinite (ratio))
		ratio = 0.5;
	ratio = CLAMP (ratio, LEGACY_RATIO_MIN, LEGACY_RATIO_MAX);
	if (!layout_expect (ps, ","))
		return FALSE;

	yaml_builder_begin_mapping (b);
	yaml_builder_set_member_name (b, "split");
	yaml_builder_add_string_value (b, dir[0] == 'v' ? "horizontal" : "vertical");
	yaml_builder_set_member_name (b, "ratio");
	/* Four decimals, the precision LrgDockArea saves with */
	yaml_builder_add_double_value (b, round (ratio * 10000.0) / 10000.0);

	/* Children */
	yaml_builder_set_member_name (b, "first");
	if (!layout_node (ps, b, depth + 1) || !layout_expect (ps, ","))
		return FALSE;
	yaml_builder_set_member_name (b, "second");
	if (!layout_node (ps, b, depth + 1) || !layout_expect (ps, ")"))
		return FALSE;
	yaml_builder_end_mapping (b);
	return TRUE;
}

/*
 * layout_tabs:
 *
 * Parses the body of "tabs(" (already consumed) and emits
 * { tabs: [ids], active: id }. The old active index is clamped to the
 * tab count like 0.8.0 did; an empty stack is written as "tabs: []"
 * (LrgDockArea collapses it on load).
 */
static gboolean
layout_tabs (LayoutParser *ps,
             YamlBuilder  *b)
{
	g_autofree gchar    *active_word = NULL;
	g_autoptr(GPtrArray) ids = g_ptr_array_new_with_free_func (g_free);
	gint64               active = 0;
	guint                i;

	/* Active tab index */
	active_word = layout_word (ps);
	if (active_word != NULL)
		active = g_ascii_strtoll (active_word, NULL, 10);
	layout_skip_ws (ps);

	/* Panel ids */
	while (*ps->p == ',')
	{
		gchar *id;

		ps->p++;
		id = layout_word (ps);
		if (id == NULL)
		{
			g_set_error_literal (ps->error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			                     "legacy layout: empty panel id");
			return FALSE;
		}
		g_ptr_array_add (ids, id);
		layout_skip_ws (ps);
	}
	if (!layout_expect (ps, ")"))
		return FALSE;
	active = CLAMP (active, 0, MAX (0, (gint64) ids->len - 1));

	yaml_builder_begin_mapping (b);
	yaml_builder_set_member_name (b, "tabs");
	yaml_builder_begin_sequence (b);
	for (i = 0; i < ids->len; i++)
		yaml_builder_add_string_value (b, g_ptr_array_index (ids, i));
	yaml_builder_end_sequence (b);
	if (ids->len > 0)
	{
		yaml_builder_set_member_name (b, "active");
		yaml_builder_add_string_value (b, g_ptr_array_index (ids, (guint) active));
	}
	yaml_builder_end_mapping (b);
	return TRUE;
}

static gboolean
layout_node (LayoutParser *ps,
             YamlBuilder  *b,
             guint         depth)
{
	layout_skip_ws (ps);
	if (depth > LEGACY_LAYOUT_MAX_DEPTH)
	{
		g_set_error_literal (ps->error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                     "legacy layout nested too deeply");
		return FALSE;
	}

	if (g_str_has_prefix (ps->p, "split("))
	{
		ps->p += strlen ("split(");
		return layout_split (ps, b, depth);
	}
	if (g_str_has_prefix (ps->p, "tabs("))
	{
		ps->p += strlen ("tabs(");
		return layout_tabs (ps, b);
	}

	g_set_error (ps->error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
	             "legacy layout: unexpected '%.20s'", ps->p);
	return FALSE;
}

gchar *
lrg_editor_settings_convert_legacy_layout (const gchar  *layout,
                                           GError      **error)
{
	g_autoptr(YamlBuilder)   builder = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) generator = NULL;
	g_autoptr(GError)        local = NULL;
	LayoutParser             ps;
	gchar                   *out;

	g_return_val_if_fail (layout != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* 1. Header, then the tree under "root" (the same document shape
	 *    lrg_dock_area_save_layout() writes) */
	builder = yaml_builder_new ();
	yaml_builder_begin_mapping (builder);
	yaml_builder_set_member_name (builder, "version");
	yaml_builder_add_int_value (builder, 1);
	yaml_builder_set_member_name (builder, "root");

	ps.p = layout;
	ps.error = &local;
	if (!layout_node (&ps, builder, 0))
	{
		g_propagate_error (error, g_steal_pointer (&local));
		return NULL;
	}
	layout_skip_ws (&ps);
	if (*ps.p != '\0')
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		             "legacy layout: trailing data '%.20s'", ps.p);
		return NULL;
	}

	/* 2. 0.8.0 had no floating panels */
	yaml_builder_set_member_name (builder, "floating");
	yaml_builder_begin_sequence (builder);
	yaml_builder_end_sequence (builder);
	yaml_builder_end_mapping (builder);

	/* 3. Text */
	doc = yaml_builder_get_document (builder);
	generator = yaml_generator_new ();
	yaml_generator_set_document (generator, doc);
	out = yaml_generator_to_data (generator, NULL, &local);
	if (out == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "legacy layout: cannot write YAML: %s",
		             local != NULL ? local->message : "unknown error");
		return NULL;
	}
	return out;
}

/* ==========================================================================
 * Keybindings and themes
 * ========================================================================== */

gchar *
lrg_editor_settings_convert_legacy_shortcut (const gchar *shortcut)
{
	g_auto(GStrv)    parts = NULL;
	g_autoptr(GString) out = NULL;
	gchar           *whole;
	guint            i;

	g_return_val_if_fail (shortcut != NULL, NULL);

	/* An unbinding stays an unbinding */
	if (*shortcut == '\0')
		return g_strdup ("");

	/* The common case: every alternative is valid */
	whole = lrg_editor_shortcut_to_accel (shortcut);
	if (whole != NULL)
		return whole;

	/* Otherwise keep the alternatives that convert */
	parts = g_strsplit (shortcut, "|", -1);
	out = g_string_new (NULL);
	for (i = 0; parts[i] != NULL; i++)
	{
		g_autofree gchar *accel = lrg_editor_shortcut_to_accel (g_strstrip (parts[i]));

		if (accel == NULL)
			continue;
		if (out->len > 0)
			g_string_append_c (out, '|');
		g_string_append (out, accel);
	}
	if (out->len == 0)
		return NULL;
	return g_string_free (g_steal_pointer (&out), FALSE);
}

const gchar *
lrg_editor_settings_convert_legacy_theme (const gchar *theme)
{
	gsize i;

	if (theme == NULL)
		return NULL;
	for (i = 0; i < G_N_ELEMENTS (legacy_theme_map); i++)
		if (g_ascii_strcasecmp (theme, legacy_theme_map[i].legacy) == 0)
			return legacy_theme_map[i].current;
	return theme;
}

/* ==========================================================================
 * Migration
 * ========================================================================== */

gboolean
lrg_editor_settings_needs_migration (LrgEditorSettings *settings)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SETTINGS (settings), FALSE);

	return lrg_editor_settings_get_settings_version (settings) < LRG_EDITOR_SETTINGS_VERSION;
}

/*
 * backup_once:
 *
 * Copies @path to @path.v0.8.0.bak unless the backup already exists. A
 * missing @path (settings that were never on disk) needs no backup.
 */
static gboolean
backup_once (const gchar  *path,
             GError      **error)
{
	g_autofree gchar *bak_path = NULL;
	g_autoptr(GFile)  src = NULL;
	g_autoptr(GFile)  dst = NULL;
	g_autoptr(GError) local = NULL;

	if (!g_file_test (path, G_FILE_TEST_EXISTS))
		return TRUE;

	bak_path = g_strconcat (path, LRG_EDITOR_SETTINGS_BACKUP_SUFFIX, NULL);
	src = g_file_new_for_path (path);
	dst = g_file_new_for_path (bak_path);

	/* Without G_FILE_COPY_OVERWRITE the copy fails with EXISTS rather
	 * than replace an earlier backup: the first 0.8.0 file is the one
	 * worth keeping. */
	if (!g_file_copy (src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, &local))
	{
		if (g_error_matches (local, G_IO_ERROR, G_IO_ERROR_EXISTS))
			return TRUE;
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Cannot back up %s to %s: %s", path, bak_path, local->message);
		return FALSE;
	}
	g_message ("editor settings: backed up %s to %s before migrating", path, bak_path);
	return TRUE;
}

/*
 * migrate_layout:
 *
 * Fills ui-layout from the 0.8.0 layout string (left in place). An
 * existing ui-layout wins; a string that does not parse is logged.
 */
static void
migrate_layout (LrgEditorSettings *settings)
{
	g_autofree gchar  *legacy = NULL;
	g_autofree gchar  *yaml = NULL;
	g_autoptr(GError)  error = NULL;
	const gchar       *current = lrg_editor_settings_get_ui_layout (settings);

	if (current != NULL && *current != '\0')
		return;
	g_object_get (settings, "layout", &legacy, NULL);
	if (legacy == NULL || *legacy == '\0')
		return;

	yaml = lrg_editor_settings_convert_legacy_layout (legacy, &error);
	if (yaml == NULL)
	{
		g_message ("editor settings: cannot migrate the 0.8.0 layout '%s' (%s); "
		           "the default layout will be used",
		           legacy, error != NULL ? error->message : "unknown error");
		return;
	}
	g_object_set (settings, "ui-layout", yaml, NULL);
}

/*
 * migrate_keybindings:
 *
 * Rewrites every override as accelerators, dropping alternatives (and
 * whole bindings) that do not convert.
 */
static void
migrate_keybindings (LrgEditorSettings *settings)
{
	GList *actions = lrg_editor_settings_list_keybindings (settings);
	GList *l;

	for (l = actions; l != NULL; l = l->next)
	{
		const gchar      *action = l->data;
		const gchar      *raw = lrg_editor_settings_get_keybinding (settings, action);
		g_autofree gchar *accel = NULL;
		g_autofree gchar *strict = NULL;

		if (raw == NULL)
			continue;
		accel = lrg_editor_settings_convert_legacy_shortcut (raw);
		if (accel == NULL)
		{
			g_message ("editor settings: dropped the keybinding '%s' of '%s' (not a valid "
			           "shortcut); the action keeps its default", raw, action);
			lrg_editor_settings_set_keybinding (settings, action, NULL);
			continue;
		}
		if (g_strcmp0 (accel, raw) == 0)
			continue;

		/* Some alternatives may have been dropped: say which binding */
		strict = lrg_editor_shortcut_to_accel (raw);
		if (strict == NULL)
			g_message ("editor settings: kept '%s' of the keybinding '%s' of '%s' "
			           "(invalid alternatives dropped)", accel, raw, action);
		lrg_editor_settings_set_keybinding (settings, action, accel);
	}
	g_list_free (actions);
}

/*
 * migrate_theme:
 *
 * Replaces a 0.8.0 theme name with its 0.8.1 value.
 */
static void
migrate_theme (LrgEditorSettings *settings)
{
	g_autofree gchar *theme = NULL;
	const gchar      *current;

	g_object_get (settings, "theme", &theme, NULL);
	current = lrg_editor_settings_convert_legacy_theme (theme);
	if (current != NULL && g_strcmp0 (current, theme) != 0)
		g_object_set (settings, "theme", current, NULL);
}

gboolean
lrg_editor_settings_migrate (LrgEditorSettings  *settings,
                             const gchar        *path,
                             GError            **error)
{
	g_autofree gchar *resolved = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SETTINGS (settings), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* 1. Current already: nothing to do (this is what makes a second
	 *    run a no-op) */
	if (!lrg_editor_settings_needs_migration (settings))
		return TRUE;

	if (path != NULL)
		resolved = g_strdup (path);
	else if (lrg_editor_settings_get_path (settings) != NULL)
		resolved = g_strdup (lrg_editor_settings_get_path (settings));
	else
		resolved = lrg_editor_settings_get_default_path ();

	/* 2. Keep the 0.8.0 file before anything is rewritten */
	if (!backup_once (resolved, error))
		return FALSE;

	/* 3. Convert (ui-scale, recents and unknown keys stay as they are) */
	g_object_freeze_notify (G_OBJECT (settings));
	migrate_layout (settings);
	migrate_keybindings (settings);
	migrate_theme (settings);
	lrg_editor_settings_set_settings_version (settings, LRG_EDITOR_SETTINGS_VERSION);
	g_object_thaw_notify (G_OBJECT (settings));

	/* 4. Write the 0.8.1 file */
	if (!lrg_editor_settings_save (settings, resolved, error))
		return FALSE;
	g_message ("editor settings: migrated %s to settings-version %d",
	           resolved, LRG_EDITOR_SETTINGS_VERSION);
	return TRUE;
}
