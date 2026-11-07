/* editor-custom-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Extending the libregnum editor from a host program: embed
 * LrgEditorApp, dock a panel of your own and add an action with a
 * shortcut and a menu entry.
 *
 * The "Level Stats" panel (docked as a tab next to the inspector) counts
 * the level's nodes by visual kind (primitive, mesh-asset, light,
 * camera, ...) and refreshes whenever the document changes
 * (LrgEditor::changed) or is replaced (LrgEditor::level-replaced). The
 * "Log Level Stats" action (Ctrl+Alt+L, Tools > Log Level Stats, the
 * command palette and the keybinding editor) writes the same summary to
 * the editor's console and status bar; it is disabled while the level is
 * empty. Everything else is the stock editor: menus, viewport, IPC
 * socket (so lrgeditorctl and Emacs can drive it), preferences.
 *
 * Controls:
 *   Ctrl+Alt+L            log the stats (the custom action)
 *   Ctrl+Shift+P          command palette ("Log Level Stats")
 *   View > Panels         close and reopen "Level Stats"
 *
 * Run with --help for options.
 */

#include <libregnum.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANEL_ID     "level-stats"
#define ACTION_ID    "example.log-stats"
#define WINDOW_WIDTH  (1280)
#define WINDOW_HEIGHT (800)

static const gchar *license_text =
	"editor-custom-panel - extend the libregnum editor with a panel and an action\n"
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

/* ==========================================================================
 * State shared by the panel, the action and the screenshot timer
 * ========================================================================== */

typedef struct
{
	LrgEditorApp *app;          /* not owned */
	LrgEditor    *editor;       /* not owned */
	LrgVBox      *rows;         /* the panel's content, owned by the panel */

	/* --screenshot */
	gchar        *shot_path;
	gint          shot_frames;
	gint          frame;
	gboolean      shot_requested;
} Example;

/* ==========================================================================
 * Counting nodes by kind
 * ========================================================================== */

/*
 * count_nodes:
 * @node: a node whose children are counted (the level root is not)
 * @counts: kind nick (gchar *, not owned) -> count (GUINT_TO_POINTER)
 *
 * Walks the subtree under @node and adds one to the entry of each
 * node's visual kind; a node without a visual counts as "none" (a group).
 * Returns the number of nodes visited.
 */
static guint
count_nodes (LrgNode    *node,
             GHashTable *counts)
{
	GPtrArray *children = lrg_node_get_children (node);
	guint      total = 0;
	guint      i;

	for (i = 0; i < children->len; i++)
	{
		LrgNode       *child = g_ptr_array_index (children, i);
		LrgNodeVisual *visual = lrg_node_get_visual (child);
		LrgNodeVisualKind kind = (visual != NULL) ? lrg_node_visual_get_kind (visual) : LRG_NODE_VISUAL_NONE;
		g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_NODE_VISUAL_KIND);
		GEnumValue   *value = g_enum_get_value (klass, (gint) kind);
		const gchar  *nick = (value != NULL) ? value->value_nick : "unknown";

		g_hash_table_insert (counts, (gpointer) nick,
		                     GUINT_TO_POINTER (GPOINTER_TO_UINT (g_hash_table_lookup (counts, nick)) + 1));
		total += 1 + count_nodes (child, counts);
	}
	return total;
}

/*
 * stats_lines:
 * @editor: the edited document
 * @total: (out): the number of nodes
 *
 * The stats as lines "kind<TAB>count", sorted by kind.
 *
 * Returns: (transfer full): a %NULL-terminated array of lines
 */
static gchar **
stats_lines (LrgEditor *editor,
             guint     *total)
{
	g_autoptr(GHashTable) counts = g_hash_table_new (g_str_hash, g_str_equal);
	g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();
	g_autoptr(GList)       kinds = NULL;
	GList                 *l;

	*total = count_nodes (lrg_level_get_root (lrg_editor_get_level (editor)), counts);
	kinds = g_list_sort (g_hash_table_get_keys (counts), (GCompareFunc) g_strcmp0);
	for (l = kinds; l != NULL; l = l->next)
		g_strv_builder_take (builder, g_strdup_printf ("%s\t%u", (const gchar *) l->data,
		                                               GPOINTER_TO_UINT (g_hash_table_lookup (counts, l->data))));
	return g_strv_builder_end (builder);
}

/* ==========================================================================
 * The "Level Stats" panel
 * ========================================================================== */

/*
 * add_row:
 * @text: a heading, or a "kind<TAB>count" line
 *
 * One line of the panel: a heading label, or an hbox with the kind in a
 * fixed-width column and its count beside it (the UI font is
 * proportional, so padding with spaces would not line up).
 */
static void
add_row (Example     *ex,
         const gchar *text,
         gboolean     heading)
{
	g_auto(GStrv) cols = g_strsplit (text, "\t", 2);
	LrgLabel     *label = lrg_label_new (cols[0]);

	if (heading || cols[1] == NULL)
	{
		if (heading)
			lrg_widget_add_class (LRG_WIDGET (label), "heading");
		lrg_container_add_child (LRG_CONTAINER (ex->rows), LRG_WIDGET (label));
		g_object_unref (label);
		return;
	}
	{
		LrgHBox  *row = lrg_hbox_new ();
		LrgLabel *count = lrg_label_new (cols[1]);

		lrg_widget_set_min_size (LRG_WIDGET (label), 110.0f, 0.0f);
		lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (label));
		lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (count));
		lrg_container_add_child (LRG_CONTAINER (ex->rows), LRG_WIDGET (row));
		g_object_unref (label);
		g_object_unref (count);
		g_object_unref (row);
	}
}

/*
 * refresh_panel:
 *
 * Rebuilds the panel's lines from the level. Connected to
 * LrgEditor::changed (every edit, undo and redo) and
 * LrgEditor::level-replaced (new, open, reload); counting a level is
 * cheap enough to redo on each change.
 */
static void
refresh_panel (Example *ex)
{
	g_auto(GStrv)     lines = NULL;
	g_autofree gchar *total_text = NULL;
	guint             total = 0;
	guint             i;

	lines = stats_lines (ex->editor, &total);
	lrg_container_remove_all (LRG_CONTAINER (ex->rows));
	total_text = g_strdup_printf ("%u node%s", total, total == 1 ? "" : "s");
	add_row (ex, total_text, TRUE);
	for (i = 0; lines[i] != NULL; i++)
		add_row (ex, lines[i], FALSE);
	if (total == 0)
		add_row (ex, "The level is empty", FALSE);
}

static void
on_editor_changed (LrgEditor *editor,
                   gpointer   user_data)
{
	refresh_panel (user_data);
}

static void
on_level_replaced (LrgEditor *editor,
                   LrgLevel  *level,
                   gpointer   user_data)
{
	refresh_panel (user_data);
}

/* make_panel: the dock panel, its content and its signal hookups */
static LrgDockPanel *
make_panel (Example *ex)
{
	LrgDockPanel *panel = lrg_dock_panel_new (PANEL_ID, "Level Stats");

	ex->rows = lrg_vbox_new ();
	lrg_container_set_padding (LRG_CONTAINER (ex->rows), 10.0f);
	lrg_container_set_spacing (LRG_CONTAINER (ex->rows), 4.0f);
	lrg_dock_panel_set_child (panel, LRG_WIDGET (ex->rows));
	g_object_unref (ex->rows);   /* the panel holds it */

	g_signal_connect (ex->editor, "changed", G_CALLBACK (on_editor_changed), ex);
	g_signal_connect (ex->editor, "level-replaced", G_CALLBACK (on_level_replaced), ex);
	refresh_panel (ex);
	return panel;
}

/* ==========================================================================
 * The "Log Level Stats" action
 * ========================================================================== */

/* log_stats: the action's activate callback */
static void
log_stats (gpointer user_data)
{
	Example          *ex = user_data;
	g_auto(GStrv)     lines = NULL;
	g_autofree gchar *joined = NULL;
	guint             total = 0;

	lines = stats_lines (ex->editor, &total);
	joined = g_strjoinv (", ", lines);
	g_strdelimit (joined, "\t", ' ');

	/* g_message() goes to the editor's console (its log handler is
	 * installed by the app); ui.notify also shows it as a toast */
	g_message ("Level stats: %u nodes (%s)", total, joined);
	{
		g_autoptr(JsonBuilder) b = json_builder_new ();
		g_autoptr(JsonGenerator) gen = json_generator_new ();
		g_autoptr(JsonNode)    root = NULL;
		g_autofree gchar      *msg = g_strdup_printf ("%u nodes: %s", total, joined);
		g_autofree gchar      *request = NULL;
		g_autofree gchar      *reply = NULL;

		json_builder_begin_object (b);
		json_builder_set_member_name (b, "id");
		json_builder_add_int_value (b, 1);
		json_builder_set_member_name (b, "method");
		json_builder_add_string_value (b, "ui.notify");
		json_builder_set_member_name (b, "params");
		json_builder_begin_object (b);
		json_builder_set_member_name (b, "message");
		json_builder_add_string_value (b, msg);
		json_builder_set_member_name (b, "level");
		json_builder_add_string_value (b, "success");
		json_builder_end_object (b);
		json_builder_end_object (b);
		root = json_builder_get_root (b);
		json_generator_set_root (gen, root);
		request = json_generator_to_data (gen, NULL);
		reply = lrg_editor_server_dispatch (lrg_editor_app_get_server (ex->app), request);
	}
}

/* has_nodes: the action's enabled callback (polled once per frame) */
static gboolean
has_nodes (gpointer user_data)
{
	Example *ex = user_data;

	return lrg_node_get_n_children (lrg_level_get_root (lrg_editor_get_level (ex->editor))) > 0;
}

/*
 * add_menu_entry:
 *
 * Custom actions are in the palette, the keybinding list and action.run
 * as soon as they are added; a menu entry is up to the host. The menu
 * bar shows lrg_editor_window_get_menu_model(): one submenu per top-level
 * menu, whose items name "editor.<action-id>". Appending a section to
 * the Tools submenu adds a separator and the entry (the menu bar follows
 * the model's items-changed).
 */
static void
add_menu_entry (LrgEditorApp *app,
                const gchar  *label,
                const gchar  *action_id)
{
	GMenuModel *bar = lrg_editor_window_get_menu_model (lrg_editor_app_get_window (app));
	gint        i;

	for (i = 0; i < g_menu_model_get_n_items (bar); i++)
	{
		g_autofree gchar        *title = NULL;
		g_autoptr(GMenuModel)    submenu = NULL;
		g_autoptr(GMenu)         section = NULL;
		g_autofree gchar        *detailed = NULL;

		if (!g_menu_model_get_item_attribute (bar, i, G_MENU_ATTRIBUTE_LABEL, "s", &title) ||
		    g_strcmp0 (title, "_Tools") != 0)
			continue;
		submenu = g_menu_model_get_item_link (bar, i, G_MENU_LINK_SUBMENU);
		if (submenu == NULL || !G_IS_MENU (submenu))
			return;
		section = g_menu_new ();
		detailed = g_strconcat ("editor.", action_id, NULL);
		g_menu_append (section, label, detailed);
		g_menu_append_section (G_MENU (submenu), NULL, G_MENU_MODEL (section));
		return;
	}
}

/* ==========================================================================
 * Sample content
 * ========================================================================== */

/* seed_level: a few nodes of different kinds, so the stats say something */
static void
seed_level (LrgEditor *editor)
{
	static const struct
	{
		const gchar       *name;
		LrgNodeVisualKind  kind;
		gfloat             x;
		gfloat             z;
	} seeds[] = {
		{ "Crate_A", LRG_NODE_VISUAL_PRIMITIVE, 0.0f, 0.0f },
		{ "Crate_B", LRG_NODE_VISUAL_PRIMITIVE, 2.5f, 0.0f },
		{ "Crate_C", LRG_NODE_VISUAL_PRIMITIVE, -2.5f, 0.0f },
		{ "Lamp", LRG_NODE_VISUAL_LIGHT, 0.0f, 3.0f },
		{ "Camera", LRG_NODE_VISUAL_CAMERA, 0.0f, -6.0f },
	};
	g_autoptr(LrgNode) group = lrg_node_new ("Props");
	gsize               i;

	lrg_editor_add_node (editor, group, NULL);
	for (i = 0; i < G_N_ELEMENTS (seeds); i++)
	{
		g_autoptr(LrgNode)       node = lrg_node_new (seeds[i].name);
		g_autoptr(LrgNodeVisual) vis = lrg_node_visual_new (seeds[i].kind);

		if (seeds[i].kind == LRG_NODE_VISUAL_PRIMITIVE)
			lrg_node_visual_set_primitive (vis, LRG_PRIMITIVE_CUBE);
		lrg_node_set_location_xyz (node, seeds[i].x, seeds[i].kind == LRG_NODE_VISUAL_PRIMITIVE ? 0.5f : 2.0f,
		                           seeds[i].z);
		lrg_node_set_visual (node, vis);
		lrg_editor_add_node (editor, node, seeds[i].kind == LRG_NODE_VISUAL_PRIMITIVE ? group : NULL);
	}
}

/* ==========================================================================
 * --screenshot: a PNG of the whole window after N frames
 * ========================================================================== */

/*
 * screenshot_tick:
 *
 * lrg_editor_app_run() drains the GLib main context once per frame, so a
 * 1 ms timeout fires about once per frame (never twice in one drain: it
 * is not due again until the next frame). After --frames frames it asks
 * for a window capture through the editor's own IPC method
 * (view.screenshot window=true, taken after the next frame is drawn),
 * then quits once the file exists (editor.quit force=true: the seeded
 * level is unsaved, and a screenshot run must not stop at a dialog).
 */
static gboolean
screenshot_tick (gpointer user_data)
{
	Example         *ex = user_data;
	LrgEditorServer *server = lrg_editor_app_get_server (ex->app);

	ex->frame++;
	if (!ex->shot_requested && ex->frame >= ex->shot_frames)
	{
		g_autofree gchar *escaped = g_strescape (ex->shot_path, NULL);
		g_autofree gchar *request = g_strdup_printf ("{\"id\":1,\"method\":\"view.screenshot\","
		                                             "\"params\":{\"path\":\"%s\",\"window\":true}}", escaped);
		g_autofree gchar *reply = lrg_editor_server_dispatch (server, request);

		ex->shot_requested = TRUE;
		if (reply != NULL && strstr (reply, "\"error\"") != NULL)
			g_printerr ("editor-custom-panel: %s\n", reply);
		return G_SOURCE_CONTINUE;
	}
	if (ex->shot_requested && (g_file_test (ex->shot_path, G_FILE_TEST_EXISTS) || ex->frame > ex->shot_frames + 30))
	{
		g_autofree gchar *reply = lrg_editor_server_dispatch (server, "{\"id\":2,\"method\":\"editor.quit\","
		                                                              "\"params\":{\"force\":true}}");

		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

/* ==========================================================================
 * main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgEditor)      editor = NULL;
	g_autoptr(LrgEditorApp)   app = NULL;
	g_autoptr(LrgDockPanel)   panel = NULL;
	g_autofree gchar         *opt_project = NULL;
	g_autofree gchar         *opt_screenshot = NULL;
	gint                      opt_frames = 90;
	gboolean                  opt_license = FALSE;
	Example                   ex = { 0 };
	gint                      status;

	GOptionEntry entries[] = {
		{ "project", 'p', 0, G_OPTION_ARG_FILENAME, &opt_project,
		  "Open this project directory (default: a sample level in memory)", "DIR" },
		{ "screenshot", 's', 0, G_OPTION_ARG_FILENAME, &opt_screenshot,
		  "Save a PNG of the whole window after --frames frames and exit", "PATH" },
		{ "frames", 'f', 0, G_OPTION_ARG_INT, &opt_frames, "Frames to run before --screenshot (default 90)", "N" },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Show the license and exit", NULL },
		{ NULL }
	};

	/* 1. Options */
	ctx = g_option_context_new ("- the libregnum editor with a custom panel and action");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Examples:\n"
		"  editor-custom-panel\n"
		"  editor-custom-panel --project ~/games/mygame\n"
		"  editor-custom-panel --screenshot stats.png --frames 120\n"
		"  lrgeditorctl run example.log-stats     # from another terminal\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("editor-custom-panel: %s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("%s", license_text);
		return 0;
	}

	/* 2. The editor needs a display for its window (fonts, GL); exit
	 * cleanly without one, as CI runs every example */
	if ((g_getenv ("DISPLAY") == NULL || *g_getenv ("DISPLAY") == '\0') &&
	    (g_getenv ("WAYLAND_DISPLAY") == NULL || *g_getenv ("WAYLAND_DISPLAY") == '\0'))
	{
		g_printerr ("editor-custom-panel: no graphical display (DISPLAY/WAYLAND_DISPLAY not set). Nothing to do.\n");
		return 0;
	}

	/* 3. The document and the app around it; preferences (theme,
	 * keybindings, saved layout) as the user set them in lrgeditor */
	editor = lrg_editor_new ();
	app = lrg_editor_app_new (editor);
	if (!lrg_editor_app_load_settings (app, NULL, &error))
	{
		g_printerr ("editor-custom-panel: preferences: %s\n", error->message);
		g_clear_error (&error);
	}
	lrg_editor_app_set_window_size (app, WINDOW_WIDTH, WINDOW_HEIGHT);
	if (opt_project != NULL)
	{
		if (!lrg_editor_app_open_project (app, opt_project, &error))
		{
			g_printerr ("editor-custom-panel: %s: %s\n", opt_project, error->message);
			return 1;
		}
	}
	else
		seed_level (editor);

	ex.app = app;
	ex.editor = editor;

	/* 4. The custom panel, as a tab next to the inspector; it is listed
	 * in View > Panels and kept by View > Reset Layout */
	panel = make_panel (&ex);
	lrg_editor_app_add_panel (app, panel, "tab:inspector");

	/* 5. The custom action: palette, keybindings, action.run at once;
	 * the menu entry is added by hand */
	lrg_editor_actions_add (lrg_editor_app_get_actions (app), ACTION_ID, "Log Level Stats", "Example",
	                        "<Ctrl><Alt>l", log_stats, has_nodes, NULL, &ex);
	add_menu_entry (app, "Log Level Stats", ACTION_ID);

	/* 6. The IPC socket, so lrgeditorctl (`lrgeditorctl run
	 * example.log-stats`) and Emacs can drive this editor too */
	if (!lrg_editor_app_start_server (app, NULL, &error))
	{
		g_printerr ("editor-custom-panel: no IPC server: %s\n", error->message);
		g_clear_error (&error);
	}

	/* 7. --screenshot: count frames and capture the window */
	if (opt_screenshot != NULL)
	{
		ex.shot_path = g_canonicalize_filename (opt_screenshot, NULL);
		ex.shot_frames = MAX (opt_frames, 1);
		g_timeout_add (1, screenshot_tick, &ex);
	}

	/* 8. Blocks until the window closes */
	status = lrg_editor_app_run (app);

	g_signal_handlers_disconnect_by_data (editor, &ex);
	g_free (ex.shot_path);
	return status;
}
