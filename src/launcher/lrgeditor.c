/* lrg-editor.c - the libregnum editor program
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * `lrgeditor` opens the editor application (#LrgEditorApp) on a project
 * folder or a level file:
 *
 *     lrgeditor [OPTIONS] [PROJECT-DIR | LEVEL-FILE]
 *
 * It can also run without a window as an IPC server for Emacs and other
 * tools (--headless), validate a level and exit (--validate, for CI), or
 * create a new project (--new-project).
 */

#include <libregnum.h>
#include <glib.h>
#include <stdio.h>
#include <unistd.h>
#ifdef LRG_EDITOR_HAVE_MCP
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include "lrgeditorctl-mcp.h"
#endif

static gchar    *opt_project     = NULL;
static gchar    *opt_level       = NULL;
static gchar    *opt_new_project = NULL;
static gchar    *opt_name        = NULL;
static gchar    *opt_settings    = NULL;
static gchar    *opt_socket      = NULL;
static gchar    *opt_editor      = NULL;
static gchar    *opt_module      = NULL;
static gboolean  opt_headless    = FALSE;
static gboolean  opt_no_server   = FALSE;
static gboolean  opt_mcp         = FALSE;
static gboolean  opt_validate    = FALSE;
static gboolean  opt_license     = FALSE;
static gboolean  opt_version     = FALSE;
static gint      opt_width       = 1600;
static gint      opt_height      = 960;
static gchar   **opt_rest        = NULL;

static const GOptionEntry entries[] = {
	{ "project", 'p', 0, G_OPTION_ARG_FILENAME, &opt_project,
	  "Open the project in DIR (a folder with project.ryaml, or any folder)", "DIR" },
	{ "level", 'l', 0, G_OPTION_ARG_FILENAME, &opt_level,
	  "Open the level FILE (relative paths resolve against the project)", "FILE" },
	{ "new-project", 'n', 0, G_OPTION_ARG_FILENAME, &opt_new_project,
	  "Create a new project in DIR and open it", "DIR" },
	{ "name", 0, 0, G_OPTION_ARG_STRING, &opt_name,
	  "Name for --new-project", "NAME" },
	{ "settings", 's', 0, G_OPTION_ARG_FILENAME, &opt_settings,
	  "Use this preferences file instead of $XDG_CONFIG_HOME/libregnum/editor.yaml", "FILE" },
	{ "editor", 'e', 0, G_OPTION_ARG_STRING, &opt_editor,
	  "External code editor for this session: auto, emacsclient, nvim, codium... or a command "
	  "template with {file} {line} {col}", "EDITOR" },
	{ "module", 'm', 0, G_OPTION_ARG_FILENAME, &opt_module,
	  "Load a game module (.so) so its component types can be edited and played", "SO" },
	{ "socket", 0, 0, G_OPTION_ARG_FILENAME, &opt_socket,
	  "IPC socket path (default $XDG_RUNTIME_DIR/libregnum/editor-PID.sock)", "PATH" },
	{ "no-server", 0, 0, G_OPTION_ARG_NONE, &opt_no_server,
	  "Do not start the IPC server", NULL },
	{ "mcp", 0, 0, G_OPTION_ARG_NONE, &opt_mcp,
	  "Serve MCP (Model Context Protocol) on stdin/stdout so an AI agent can drive this editor; "
	  "headless when there is no display", NULL },
	{ "headless", 0, 0, G_OPTION_ARG_NONE, &opt_headless,
	  "Run without a window, serving IPC only (for Emacs and scripts)", NULL },
	{ "validate", 0, 0, G_OPTION_ARG_NONE, &opt_validate,
	  "Validate the level, print problems and exit (status 1 on errors)", NULL },
	{ "width", 'W', 0, G_OPTION_ARG_INT, &opt_width,
	  "Initial window width", "N" },
	{ "height", 'H', 0, G_OPTION_ARG_INT, &opt_height,
	  "Initial window height", "N" },
	{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license,
	  "Print the license and exit", NULL },
	{ "version", 'v', 0, G_OPTION_ARG_NONE, &opt_version,
	  "Print the version and exit", NULL },
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &opt_rest,
	  NULL, "[PROJECT-DIR | LEVEL-FILE]" },
	G_OPTION_ENTRY_NULL
};

static const gchar *examples =
	"Examples:\n"
	"  lrgeditor ~/games/mygame                 open a project folder\n"
	"  lrgeditor levels/arena.rlevel            open a level (its folder is the root)\n"
	"  lrgeditor -p ~/games/mygame -l levels/arena.rlevel\n"
	"  lrgeditor --new-project ~/games/newgame --name \"New Game\"\n"
	"  lrgeditor --editor emacsclient ~/games/mygame\n"
	"  lrgeditor --editor 'codium --goto {file}:{line}:{col}' .\n"
	"  lrgeditor --headless --socket /tmp/lrg.sock ~/games/mygame\n"
	"  lrgeditor --validate levels/arena.rlevel  (CI: exit status 1 on errors)\n"
	"  lrgeditor --mcp ~/games/mygame            AI agent drives it (claude mcp add ...)\n"
	"\n"
	"Code opens in $VISUAL/$EDITOR unless preferences or --editor choose\n"
	"another; with Emacs, load editors/emacs/libregnum.el and run\n"
	"M-x libregnum-connect for the first-party integration.\n";

static const gchar *license_text =
	"lrgeditor - the libregnum level, scene and world editor\n"
	"Copyright (C) 2025  Zach Podbielniak\n"
	"\n"
	"This program is free software: you can redistribute it and/or modify\n"
	"it under the terms of the GNU Affero General Public License as published by\n"
	"the Free Software Foundation, either version 3 of the License, or\n"
	"(at your option) any later version.\n"
	"\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU Affero General Public License for more details.\n"
	"\n"
	"You should have received a copy of the GNU Affero General Public License\n"
	"along with this program.  If not, see <https://www.gnu.org/licenses/>.\n";

/* --validate: print problems like a compiler, one per line. */
static gint
run_validate (LrgEditorApp *app)
{
	LrgEditor           *editor = lrg_editor_app_get_editor (app);
	LrgEngine           *engine = lrg_engine_get_default ();
	g_autoptr(GPtrArray) problems = NULL;
	const gchar         *path = lrg_editor_get_path (editor);
	const gchar         *base = lrg_editor_app_get_project_dir (app);
	g_autofree gchar    *dir = NULL;
	guint                errors = 0, warnings = 0, i;

	if (path == NULL)
	{
		g_printerr ("lrgeditor: --validate needs a level (--level or a level file argument)\n");
		return 2;
	}
	if (base == NULL)
		base = dir = g_path_get_dirname (path);

	problems = lrg_editor_validate_level (lrg_editor_get_level (editor), base,
	                                      engine != NULL ? lrg_engine_get_registry (engine) : NULL);
	for (i = 0; i < problems->len; i++)
	{
		LrgEditorProblem *p = g_ptr_array_index (problems, i);

		g_print ("%s: %s: %s%s%s [%s]\n", path, lrg_editor_problem_severity_to_string (p->severity),
		         p->node_path != NULL ? p->node_path : "", p->node_path != NULL ? ": " : "",
		         p->message, p->code != NULL ? p->code : "");
		if (p->severity == LRG_EDITOR_PROBLEM_ERROR)
			errors++;
		else if (p->severity == LRG_EDITOR_PROBLEM_WARNING)
			warnings++;
	}
	g_print ("%s: %u error(s), %u warning(s)\n", path, errors, warnings);
	return errors > 0 ? 1 : 0;
}

/* Restores the most recent project (and level) when nothing was named. */
static void
restore_session (LrgEditorApp *app)
{
	LrgEditorSettings   *settings = lrg_editor_app_get_settings (app);
	const gchar * const *projects = lrg_editor_settings_get_recent_projects (settings);
	const gchar * const *levels = lrg_editor_settings_get_recent_levels (settings);
	gboolean             restore = FALSE;
	g_autoptr(GError)    error = NULL;

	g_object_get (settings, "restore-last-session", &restore, NULL);
	if (!restore)
		return;

	if (projects != NULL && projects[0] != NULL && g_file_test (projects[0], G_FILE_TEST_IS_DIR))
	{
		if (!lrg_editor_app_open_project (app, projects[0], &error))
			g_printerr ("lrgeditor: could not reopen %s: %s\n", projects[0], error->message);
		return;
	}
	if (levels != NULL && levels[0] != NULL && g_file_test (levels[0], G_FILE_TEST_IS_REGULAR) &&
	    !lrg_editor_app_open_level (app, levels[0], &error))
		g_printerr ("lrgeditor: could not reopen %s: %s\n", levels[0], error->message);
}

#ifdef LRG_EDITOR_HAVE_MCP
/* MCP tool calls dispatch in-process, exactly like a socket client. */
static JsonNode *
in_process_call (const gchar  *method,
                 JsonObject   *params,
                 gpointer      user_data,
                 GError      **error)
{
	LrgEditorServer          *server = lrg_editor_app_get_server (LRG_EDITOR_APP (user_data));
	g_autoptr(JsonObject)     req = json_object_new ();
	g_autoptr(JsonNode)       req_node = json_node_new (JSON_NODE_OBJECT);
	g_autoptr(JsonGenerator)  gen = json_generator_new ();
	g_autoptr(JsonParser)     parser = json_parser_new ();
	g_autofree gchar         *line = NULL;
	g_autofree gchar         *reply = NULL;
	JsonObject               *obj;

	json_object_set_int_member (req, "id", 1);
	json_object_set_string_member (req, "method", method);
	if (params != NULL)
		json_object_set_object_member (req, "params", json_object_ref (params));
	json_node_set_object (req_node, req);
	json_generator_set_root (gen, req_node);
	line = json_generator_to_data (gen, NULL);

	reply = lrg_editor_server_dispatch (server, line);
	if (reply == NULL || !json_parser_load_from_data (parser, reply, -1, error) ||
	    !JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser)))
	{
		if (error != NULL && *error == NULL)
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s: no reply", method);
		return NULL;
	}
	obj = json_node_get_object (json_parser_get_root (parser));
	if (json_object_has_member (obj, "error"))
	{
		JsonObject *e = json_object_get_object_member (obj, "error");

		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s: %s (%s)", method,
		             json_object_get_string_member_with_default (e, "message", "failed"),
		             json_object_get_string_member_with_default (e, "code", "failed"));
		return NULL;
	}
	return json_object_has_member (obj, "result") ? json_node_copy (json_object_get_member (obj, "result"))
	                                              : json_node_new (JSON_NODE_NULL);
}

/* The agent went away: a headless editor exits unless that would lose
 * unsaved work (then it stays up for `lrgeditorctl save` / `quit`). */
static void
on_mcp_disconnected (McpServer *server,
                     gpointer   user_data)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);

	if (!opt_headless)
		return;
	if (lrg_editor_is_dirty (lrg_editor_app_get_editor (app)))
	{
		g_printerr ("lrgeditor: the MCP client disconnected; the level has unsaved changes, "
		            "so the editor keeps running (lrgeditorctl save / lrgeditorctl quit --force)\n");
		return;
	}
	lrg_editor_app_request_quit (app);
}
#endif

int
main (int    argc,
      char **argv)
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgEditor)      editor = NULL;
	g_autoptr(LrgEditorApp)   app = NULL;
	g_autoptr(LrgLoadedGame)  game = NULL;
	gint                      status;
#ifdef LRG_EDITOR_HAVE_MCP
	McpServer                *mcp = NULL;
	McpStdioTransport        *mcp_transport = NULL;
	gint                      mcp_out_fd = -1;
#endif

	ctx = g_option_context_new ("[PROJECT-DIR | LEVEL-FILE] - edit libregnum levels, scenes and worlds");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx, examples);
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("lrgeditor: %s\n", error->message);
		return 2;
	}

	/* MCP owns stdout: keep a private copy for the protocol and point fd 1
	 * at stderr, so nothing else (logs, raylib, the socket banner) can
	 * corrupt the stream. Without a display it runs headless. */
	if (opt_mcp)
	{
#ifdef LRG_EDITOR_HAVE_MCP
		const gchar *x = g_getenv ("DISPLAY");
		const gchar *w = g_getenv ("WAYLAND_DISPLAY");

		mcp_out_fd = dup (STDOUT_FILENO);
		if (mcp_out_fd < 0 || dup2 (STDERR_FILENO, STDOUT_FILENO) < 0)
		{
			g_printerr ("lrgeditor: cannot set up stdout for MCP\n");
			return 1;
		}
		if ((x == NULL || *x == '\0') && (w == NULL || *w == '\0'))
			opt_headless = TRUE;
#else
		g_printerr ("lrgeditor: built without MCP support (needs mcp-glib, libsoup-3.0, libdex-1)\n");
		return 2;
#endif
	}

	if (opt_license)
	{
		fputs (license_text, stdout);
		return 0;
	}
	if (opt_version)
	{
		g_print ("lrgeditor %d.%d.%d\n", LRG_VERSION_MAJOR, LRG_VERSION_MINOR, LRG_VERSION_MICRO);
		return 0;
	}

	/* A bare argument is a project folder or a level file. */
	if (opt_rest != NULL && opt_rest[0] != NULL)
	{
		if (opt_rest[1] != NULL)
		{
			g_printerr ("lrgeditor: expected at most one PROJECT-DIR or LEVEL-FILE\n");
			return 2;
		}
		if (g_file_test (opt_rest[0], G_FILE_TEST_IS_DIR) && opt_project == NULL)
			opt_project = g_strdup (opt_rest[0]);
		else if (opt_level == NULL)
			opt_level = g_strdup (opt_rest[0]);
	}

	/* Game types first, so levels referencing them load and validate. */
	if (opt_module != NULL)
	{
		game = lrg_loaded_game_load (opt_module, &error);
		if (game == NULL)
		{
			g_printerr ("lrgeditor: cannot load module %s: %s\n", opt_module, error->message);
			return 1;
		}
	}

	editor = lrg_editor_new ();
	app = lrg_editor_app_new (editor);

	if (!lrg_editor_app_load_settings (app, opt_settings, &error))
	{
		/* A broken preferences file must not lock anyone out. */
		g_printerr ("lrgeditor: preferences: %s (using defaults)\n", error->message);
		g_clear_error (&error);
	}
	if (opt_editor != NULL)
	{
		LrgEditorSettings *settings = lrg_editor_app_get_settings (app);
		const gchar       *preset = lrg_external_editor_preset_get_label (opt_editor) != NULL ? opt_editor : NULL;

		/* A preset id, or a command template used as "custom". */
		if (preset != NULL)
			g_object_set (settings, "external-editor", preset, NULL);
		else
			g_object_set (settings, "external-editor", "custom", "external-editor-command", opt_editor, NULL);
	}

	if (opt_new_project != NULL)
	{
		if (!lrg_editor_app_new_project (app, opt_new_project, opt_name, &error))
		{
			g_printerr ("lrgeditor: cannot create %s: %s\n", opt_new_project, error->message);
			return 1;
		}
	}
	else if (opt_project != NULL && !lrg_editor_app_open_project (app, opt_project, &error))
	{
		g_printerr ("lrgeditor: cannot open project %s: %s\n", opt_project, error->message);
		return 1;
	}

	if (opt_level != NULL && !lrg_editor_app_open_level (app, opt_level, &error))
	{
		g_printerr ("lrgeditor: cannot open %s: %s\n", opt_level, error->message);
		return 1;
	}

	if (opt_validate)
		return run_validate (app);

	if (opt_project == NULL && opt_level == NULL && opt_new_project == NULL)
		restore_session (app);

	if (!opt_no_server)
	{
		gboolean enabled = TRUE;

		g_object_get (lrg_editor_app_get_settings (app), "server-enabled", &enabled, NULL);
		if ((enabled || opt_headless || opt_socket != NULL) &&
		    !lrg_editor_app_start_server (app, opt_socket, &error))
		{
			g_printerr ("lrgeditor: IPC server: %s\n", error->message);
			if (opt_headless)
				return 1;
			g_clear_error (&error);
		}
	}

#ifdef LRG_EDITOR_HAVE_MCP
	if (opt_mcp)
	{
		g_autoptr(GInputStream)  in = g_unix_input_stream_new (STDIN_FILENO, FALSE);
		g_autoptr(GOutputStream) out = g_unix_output_stream_new (mcp_out_fd, TRUE);

		mcp = lrg_ctl_mcp_server_new (in_process_call, app);
		mcp_transport = mcp_stdio_transport_new_with_streams (in, out);
		mcp_server_set_transport (mcp, MCP_TRANSPORT (mcp_transport));
		g_signal_connect (mcp, "client-disconnected", G_CALLBACK (on_mcp_disconnected), app);
		mcp_server_start_async (mcp, NULL, NULL, NULL);
	}
#endif

	if (opt_headless)
	{
		LrgEditorServer *server = lrg_editor_app_get_server (app);

		g_print ("%s\n", lrg_editor_server_get_socket_path (server));
		fflush (stdout);
		status = lrg_editor_app_run_headless (app);
	}
	else
	{
		lrg_editor_app_set_window_size (app, opt_width, opt_height);
		status = lrg_editor_app_run (app);
	}

	{
		g_autoptr(GError) serr = NULL;

		if (opt_headless && !lrg_editor_app_save_settings (app, &serr))
			g_printerr ("lrgeditor: could not save preferences: %s\n", serr->message);
	}

#ifdef LRG_EDITOR_HAVE_MCP
	if (mcp != NULL)
		mcp_server_stop (mcp);
	g_clear_object (&mcp);
	g_clear_object (&mcp_transport);
#endif

	g_free (opt_project);
	g_free (opt_level);
	g_free (opt_new_project);
	g_free (opt_name);
	g_free (opt_settings);
	g_free (opt_socket);
	g_free (opt_editor);
	g_free (opt_module);
	g_strfreev (opt_rest);
	return status;
}
