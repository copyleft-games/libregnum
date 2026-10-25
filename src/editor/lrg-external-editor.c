/* lrg-external-editor.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Opening files in the user's code editor. See lrg-external-editor.h.
 */

#include "lrg-external-editor.h"
#include "lrg-editor-settings.h"
#include <gio/gio.h>
#include <string.h>

/* ==========================================================================
 * Preset table
 * ========================================================================== */

typedef struct
{
	const gchar *id;
	const gchar *label;
	/* Basenames (or basename prefixes ending in '*') that use this
	 * preset's argument conventions. */
	const gchar *programs[4];
	/* Full template, first token is the program. */
	const gchar *template_str;
	/* Arguments appended to a user-supplied program ($EDITOR wrappers). */
	const gchar *env_args;
	gboolean     terminal;
	gboolean     emacs;
} Preset;

static const Preset presets[] = {
	{ "auto", "Automatic ($VISUAL / $EDITOR)", { NULL }, NULL, NULL, FALSE, FALSE },
	{ "emacsclient", "Emacs (emacsclient)", { "emacsclient*", NULL },
	  "emacsclient -n -r +{line}:{col} {file}", "+{line}:{col} {file}", FALSE, TRUE },
	{ "emacs", "Emacs (new instance)", { "emacs", NULL },
	  "emacs +{line}:{col} {file}", "+{line}:{col} {file}", FALSE, TRUE },
	{ "nvim", "Neovim", { "nvim", NULL },
	  "nvim \"+call cursor({line},{col})\" {file}", "\"+call cursor({line},{col})\" {file}", TRUE, FALSE },
	{ "vim", "Vim", { "vim", "vi", NULL },
	  "vim \"+call cursor({line},{col})\" {file}", "\"+call cursor({line},{col})\" {file}", TRUE, FALSE },
	{ "gvim", "gVim", { "gvim", NULL },
	  "gvim --remote-silent \"+call cursor({line},{col})\" {file}",
	  "\"+call cursor({line},{col})\" {file}", FALSE, FALSE },
	{ "helix", "Helix", { "hx", "helix", NULL },
	  "hx {file}:{line}:{col}", "{file}:{line}:{col}", TRUE, FALSE },
	{ "kakoune", "Kakoune", { "kak", NULL },
	  "kak +{line}:{col} {file}", "+{line}:{col} {file}", TRUE, FALSE },
	{ "micro", "micro", { "micro", NULL },
	  "micro {file}:{line}:{col}", "{file}:{line}:{col}", TRUE, FALSE },
	{ "nano", "GNU nano", { "nano", NULL },
	  "nano +{line},{col} {file}", "+{line},{col} {file}", TRUE, FALSE },
	{ "codium", "VSCodium", { "codium", "vscodium", NULL },
	  "codium {project} --goto {file}:{line}:{col}", "--goto {file}:{line}:{col}", FALSE, FALSE },
	{ "code-oss", "Code - OSS", { "code-oss", NULL },
	  "code-oss {project} --goto {file}:{line}:{col}", "--goto {file}:{line}:{col}", FALSE, FALSE },
	{ "kate", "Kate", { "kate", NULL },
	  "kate {file} --line {line} --column {col}", "{file} --line {line} --column {col}", FALSE, FALSE },
	{ "geany", "Geany", { "geany", NULL },
	  "geany {file} --line {line} --column {col}", "{file} --line {line} --column {col}", FALSE, FALSE },
	{ "gedit", "gedit", { "gedit", NULL },
	  "gedit +{line}:{col} {file}", "+{line}:{col} {file}", FALSE, FALSE },
	{ "gnome-text-editor", "GNOME Text Editor", { "gnome-text-editor", NULL },
	  "gnome-text-editor {file}", "{file}", FALSE, FALSE },
	{ "zed", "Zed", { "zed", "zeditor", NULL },
	  "zed {file}:{line}:{col}", "{file}:{line}:{col}", FALSE, FALSE },
	{ "xdg-open", "Desktop default application", { "xdg-open", NULL },
	  "xdg-open {file}", "{file}", FALSE, FALSE },
	{ "custom", "Custom command", { NULL }, NULL, NULL, FALSE, FALSE },
};

/* Known terminals and the flag that precedes the command to run. */
static const struct
{
	const gchar *program;
	const gchar *flags;   /* space-separated, may be "" */
} terminals[] = {
	{ "gst",            "-e" },
	{ "foot",           "" },
	{ "kitty",          "" },
	{ "alacritty",      "-e" },
	{ "wezterm",        "start --" },
	{ "ghostty",        "-e" },
	{ "gnome-terminal", "--" },
	{ "ptyxis",         "--" },
	{ "konsole",        "-e" },
	{ "xfce4-terminal", "-x" },
	{ "st",             "-e" },
	{ "xterm",          "-e" },
};

static const Preset *
find_preset (const gchar *id)
{
	gsize i;

	if (id == NULL)
		return NULL;

	for (i = 0; i < G_N_ELEMENTS (presets); i++)
		if (g_strcmp0 (presets[i].id, id) == 0)
			return &presets[i];

	return NULL;
}

const gchar * const *
lrg_external_editor_list_presets (void)
{
	static const gchar *ids[G_N_ELEMENTS (presets) + 1];
	static gsize        once = 0;

	if (g_once_init_enter (&once))
	{
		gsize i;

		for (i = 0; i < G_N_ELEMENTS (presets); i++)
			ids[i] = presets[i].id;
		ids[G_N_ELEMENTS (presets)] = NULL;
		g_once_init_leave (&once, 1);
	}

	return ids;
}

const gchar *
lrg_external_editor_preset_get_label (const gchar *preset)
{
	const Preset *p = find_preset (preset);

	return (p != NULL) ? p->label : NULL;
}

const gchar *
lrg_external_editor_preset_get_template (const gchar *preset)
{
	const Preset *p = find_preset (preset);

	return (p != NULL) ? p->template_str : NULL;
}

gboolean
lrg_external_editor_preset_is_terminal (const gchar *preset)
{
	const Preset *p = find_preset (preset);

	return p != NULL && p->terminal;
}

gboolean
lrg_external_editor_preset_is_emacs (const gchar *preset)
{
	const Preset *p = find_preset (preset);

	return p != NULL && p->emacs;
}

const gchar *
lrg_external_editor_preset_for_command (const gchar *command)
{
	g_autofree gchar *base = NULL;
	gsize             i, j;

	if (command == NULL || *command == '\0')
		return NULL;

	base = g_path_get_basename (command);
	for (i = 0; i < G_N_ELEMENTS (presets); i++)
	{
		for (j = 0; presets[i].programs[j] != NULL; j++)
		{
			const gchar *pat = presets[i].programs[j];
			gsize        len = strlen (pat);

			if (len > 0 && pat[len - 1] == '*')
			{
				if (strncmp (base, pat, len - 1) == 0)
					return presets[i].id;
			}
			else if (g_strcmp0 (base, pat) == 0)
			{
				return presets[i].id;
			}
		}
	}

	return NULL;
}

/* ==========================================================================
 * Template expansion
 * ========================================================================== */

/* Replaces every placeholder in @arg. */
static gchar *
substitute (const gchar *arg,
            const gchar *file,
            const gchar *line,
            const gchar *col,
            const gchar *project,
            const gchar *dir)
{
	GString *out = g_string_new (NULL);
	const gchar *p = arg;

	while (*p != '\0')
	{
		if (g_str_has_prefix (p, "{file}"))
		{
			g_string_append (out, file);
			p += 6;
		}
		else if (g_str_has_prefix (p, "{line}"))
		{
			g_string_append (out, line);
			p += 6;
		}
		else if (g_str_has_prefix (p, "{col}"))
		{
			g_string_append (out, col);
			p += 5;
		}
		else if (g_str_has_prefix (p, "{project}"))
		{
			g_string_append (out, project != NULL ? project : "");
			p += 9;
		}
		else if (g_str_has_prefix (p, "{dir}"))
		{
			g_string_append (out, dir);
			p += 5;
		}
		else
		{
			g_string_append_c (out, *p++);
		}
	}

	return g_string_free (out, FALSE);
}

gchar **
lrg_external_editor_expand_template (const gchar  *template_str,
                                     const gchar  *file,
                                     gint          line,
                                     gint          column,
                                     const gchar  *project,
                                     GError      **error)
{
	g_auto(GStrv)     tokens = NULL;
	g_autofree gchar *line_s = NULL;
	g_autofree gchar *col_s = NULL;
	g_autofree gchar *dir = NULL;
	GPtrArray        *argv;
	gboolean          has_file = FALSE;
	gint              i;

	g_return_val_if_fail (template_str != NULL, NULL);
	g_return_val_if_fail (file != NULL, NULL);

	if (!g_shell_parse_argv (template_str, NULL, &tokens, error))
		return NULL;

	line_s = g_strdup_printf ("%d", MAX (line, 1));
	col_s = g_strdup_printf ("%d", MAX (column, 1));
	dir = g_path_get_dirname (file);
	if (project != NULL && *project == '\0')
		project = NULL;

	argv = g_ptr_array_new ();
	for (i = 0; tokens[i] != NULL; i++)
	{
		if (strstr (tokens[i], "{file}") != NULL)
			has_file = TRUE;

		/* A bare {project} argument vanishes when there is no project. */
		if (project == NULL && g_strcmp0 (tokens[i], "{project}") == 0)
			continue;

		g_ptr_array_add (argv, substitute (tokens[i], file, line_s, col_s, project, dir));
	}

	if (!has_file)
		g_ptr_array_add (argv, g_strdup (file));

	g_ptr_array_add (argv, NULL);
	return (gchar **) g_ptr_array_free (argv, FALSE);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

struct _LrgExternalEditor
{
	GObject parent_instance;

	LrgEditorSettings *settings;  /* ref */
	gchar             *project_dir;
};

G_DEFINE_FINAL_TYPE (LrgExternalEditor, lrg_external_editor, G_TYPE_OBJECT)

enum
{
	SIGNAL_OPEN_FILE,
	SIGNAL_LAUNCHED,
	SIGNAL_FAILED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_external_editor_finalize (GObject *object)
{
	LrgExternalEditor *self = LRG_EXTERNAL_EDITOR (object);

	g_clear_object (&self->settings);
	g_clear_pointer (&self->project_dir, g_free);

	G_OBJECT_CLASS (lrg_external_editor_parent_class)->finalize (object);
}

static void
lrg_external_editor_class_init (LrgExternalEditorClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_external_editor_finalize;

	/**
	 * LrgExternalEditor::open-file:
	 * @self: the launcher
	 * @file: absolute file path
	 * @line: 1-based line
	 * @column: 1-based column
	 *
	 * Emitted before launching an Emacs-family editor when Emacs
	 * integration is enabled. Return %TRUE from a handler (e.g. the editor
	 * server forwarding to a connected Emacs) to stop the launch.
	 *
	 * Returns: %TRUE if the request was handled
	 */
	signals[SIGNAL_OPEN_FILE] =
		g_signal_new ("open-file",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0,
		              g_signal_accumulator_true_handled, NULL, NULL,
		              G_TYPE_BOOLEAN, 3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);

	/**
	 * LrgExternalEditor::launched:
	 * @self: the launcher
	 * @command: the command line that was started
	 *
	 * Emitted after a process was spawned.
	 */
	signals[SIGNAL_LAUNCHED] =
		g_signal_new ("launched",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgExternalEditor::failed:
	 * @self: the launcher
	 * @message: what went wrong
	 *
	 * Emitted when a launched editor exits with an error (after any
	 * fallback was tried), so the UI can show it.
	 */
	signals[SIGNAL_FAILED] =
		g_signal_new ("failed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_external_editor_init (LrgExternalEditor *self)
{
}

LrgExternalEditor *
lrg_external_editor_new (LrgEditorSettings *settings)
{
	LrgExternalEditor *self = g_object_new (LRG_TYPE_EXTERNAL_EDITOR, NULL);

	self->settings = (settings != NULL) ? g_object_ref (settings) : lrg_editor_settings_new ();
	return self;
}

LrgEditorSettings *
lrg_external_editor_get_settings (LrgExternalEditor *self)
{
	g_return_val_if_fail (LRG_IS_EXTERNAL_EDITOR (self), NULL);

	return self->settings;
}

void
lrg_external_editor_set_project_dir (LrgExternalEditor *self,
                                     const gchar       *dir)
{
	g_return_if_fail (LRG_IS_EXTERNAL_EDITOR (self));

	g_free (self->project_dir);
	self->project_dir = g_strdup (dir);
}

/* ==========================================================================
 * Resolution
 * ========================================================================== */

/* First non-empty of $VISUAL, $EDITOR; @var receives which one. */
static const gchar *
env_editor (const gchar **var)
{
	const gchar *v = g_getenv ("VISUAL");

	if (v != NULL && *v != '\0')
	{
		*var = "$VISUAL";
		return v;
	}

	v = g_getenv ("EDITOR");
	if (v != NULL && *v != '\0')
	{
		*var = "$EDITOR";
		return v;
	}

	return NULL;
}

gchar *
lrg_external_editor_resolve_preset (LrgExternalEditor  *self,
                                    gchar             **source)
{
	const gchar *choice;
	const gchar *env;
	const gchar *var = NULL;

	g_return_val_if_fail (LRG_IS_EXTERNAL_EDITOR (self), NULL);

	choice = lrg_editor_settings_get_external_editor (self->settings);
	if (g_strcmp0 (choice, "auto") != 0)
	{
		if (source != NULL)
			*source = g_strdup ("settings");
		return g_strdup (find_preset (choice) != NULL ? choice : "custom");
	}

	env = env_editor (&var);
	if (env != NULL)
	{
		g_auto(GStrv) tokens = NULL;
		const gchar  *preset = NULL;

		if (g_shell_parse_argv (env, NULL, &tokens, NULL) && tokens[0] != NULL)
			preset = lrg_external_editor_preset_for_command (tokens[0]);

		if (source != NULL)
			*source = g_strdup (var);
		/* An unknown program is still the user's choice: "env". */
		return g_strdup (preset != NULL ? preset : "env");
	}

	if (source != NULL)
		*source = g_strdup ("desktop default");
	return g_strdup ("xdg-open");
}

static gboolean
want_terminal (LrgExternalEditor *self,
               gboolean           preset_terminal)
{
	g_autofree gchar *mode = NULL;

	g_object_get (self->settings, "editor-in-terminal", &mode, NULL);
	if (g_strcmp0 (mode, "always") == 0)
		return TRUE;
	if (g_strcmp0 (mode, "never") == 0)
		return FALSE;

	return preset_terminal;
}

/* Splits a terminal command and appends its known exec flag if missing. */
static gchar **
terminal_argv_from (const gchar *command)
{
	g_auto(GStrv)     tokens = NULL;
	g_autofree gchar *base = NULL;
	GPtrArray        *argv;
	gsize             i;
	gint              k;

	if (command == NULL || !g_shell_parse_argv (command, NULL, &tokens, NULL) ||
	    tokens[0] == NULL)
		return NULL;

	argv = g_ptr_array_new ();
	for (k = 0; tokens[k] != NULL; k++)
		g_ptr_array_add (argv, g_strdup (tokens[k]));

	/* "gst" alone becomes "gst -e"; "gst -e" is kept as written. */
	if (tokens[1] == NULL)
	{
		base = g_path_get_basename (tokens[0]);
		for (i = 0; i < G_N_ELEMENTS (terminals); i++)
		{
			if (g_strcmp0 (base, terminals[i].program) == 0)
			{
				g_auto(GStrv) flags = g_strsplit (terminals[i].flags, " ", -1);

				for (k = 0; flags[k] != NULL; k++)
					if (*flags[k] != '\0')
						g_ptr_array_add (argv, g_strdup (flags[k]));
				break;
			}
		}
		if (i == G_N_ELEMENTS (terminals))
			g_ptr_array_add (argv, g_strdup ("-e"));
	}

	g_ptr_array_add (argv, NULL);
	return (gchar **) g_ptr_array_free (argv, FALSE);
}

gchar **
lrg_external_editor_find_terminal (LrgExternalEditor *self)
{
	const gchar *configured;
	const gchar *env;
	gsize        i;

	g_return_val_if_fail (LRG_IS_EXTERNAL_EDITOR (self), NULL);

	configured = lrg_editor_settings_get_terminal_command (self->settings);
	if (configured != NULL && *configured != '\0')
		return terminal_argv_from (configured);

	env = g_getenv ("TERMINAL");
	if (env != NULL && *env != '\0')
		return terminal_argv_from (env);

	for (i = 0; i < G_N_ELEMENTS (terminals); i++)
	{
		g_autofree gchar *found = g_find_program_in_path (terminals[i].program);

		if (found != NULL)
			return terminal_argv_from (terminals[i].program);
	}

	return NULL;
}

/* Prepends the terminal wrapper to @argv (consumed). */
static gchar **
wrap_in_terminal (LrgExternalEditor  *self,
                  gchar             **argv,
                  GError            **error)
{
	g_auto(GStrv) term = lrg_external_editor_find_terminal (self);
	GPtrArray    *out;
	gint          i;

	if (term == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                     "This editor runs in a terminal, but no terminal was found; "
		                     "set terminal-command in the editor preferences");
		g_strfreev (argv);
		return NULL;
	}

	out = g_ptr_array_new ();
	for (i = 0; term[i] != NULL; i++)
		g_ptr_array_add (out, g_strdup (term[i]));
	for (i = 0; argv[i] != NULL; i++)
		g_ptr_array_add (out, argv[i]);   /* move the strings */
	g_free (argv);

	g_ptr_array_add (out, NULL);
	return (gchar **) g_ptr_array_free (out, FALSE);
}

/* Concatenates user command tokens with expanded preset arguments. */
static gchar **
join_argv (gchar **head,
           gchar **tail)
{
	GPtrArray *out = g_ptr_array_new ();
	gint       i;

	for (i = 0; head[i] != NULL; i++)
		g_ptr_array_add (out, g_strdup (head[i]));
	for (i = 0; tail[i] != NULL; i++)
		g_ptr_array_add (out, g_strdup (tail[i]));
	g_ptr_array_add (out, NULL);

	return (gchar **) g_ptr_array_free (out, FALSE);
}

/* Escapes a path for an Ex command line (like fnameescape()); "<" is
 * spelled <lt> because --remote-send interprets key notation. */
static gchar *
vim_escape (const gchar *path)
{
	GString     *out = g_string_new (NULL);
	const gchar *p;

	for (p = path; *p != '\0'; p++)
	{
		if (*p == '<')
		{
			g_string_append (out, "<lt>");
			continue;
		}
		if (strchr (" \t\\|%#\"", *p) != NULL)
			g_string_append_c (out, '\\');
		g_string_append_c (out, *p);
	}

	return g_string_free (out, FALSE);
}

/* Inserts "-s NAME" after argv[0] for emacsclient. */
static gchar **
add_emacs_server (gchar       **argv,
                  const gchar  *server)
{
	GPtrArray *out;
	gint       i;

	if (server == NULL || *server == '\0')
		return argv;

	out = g_ptr_array_new ();
	g_ptr_array_add (out, argv[0]);
	g_ptr_array_add (out, g_strdup ("-s"));
	g_ptr_array_add (out, g_strdup (server));
	for (i = 1; argv[i] != NULL; i++)
		g_ptr_array_add (out, argv[i]);
	g_free (argv);

	g_ptr_array_add (out, NULL);
	return (gchar **) g_ptr_array_free (out, FALSE);
}

/*
 * build_argv_full:
 *
 * The heart of the launcher: returns the argv to run and whether it is the
 * emacsclient family (so a failure can fall back to plain emacs).
 */
static gchar **
build_argv_full (LrgExternalEditor  *self,
                 const gchar        *file,
                 gint                line,
                 gint                column,
                 gboolean           *is_emacsclient,
                 GError            **error)
{
	g_autofree gchar *preset_id = NULL;
	g_autofree gchar *source = NULL;
	const Preset     *preset;
	gchar           **argv = NULL;
	gboolean          terminal = FALSE;

	*is_emacsclient = FALSE;
	preset_id = lrg_external_editor_resolve_preset (self, &source);
	preset = find_preset (preset_id);

	/* "custom": the user's template, never wrapped unless asked. */
	if (g_strcmp0 (preset_id, "custom") == 0)
	{
		const gchar *tpl = lrg_editor_settings_get_external_editor_command (self->settings);

		if (tpl == NULL || *tpl == '\0')
		{
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			                     "The custom external editor has no command; "
			                     "set external-editor-command in the editor preferences");
			return NULL;
		}
		argv = lrg_external_editor_expand_template (tpl, file, line, column,
		                                            self->project_dir, error);
		if (argv == NULL)
			return NULL;
		terminal = want_terminal (self, FALSE);
	}
	/* From the environment: keep the user's program (and its own flags),
	 * add the matching preset's file/line arguments. */
	else if (g_strcmp0 (source, "$VISUAL") == 0 || g_strcmp0 (source, "$EDITOR") == 0)
	{
		const gchar     *var = NULL;
		const gchar     *env = env_editor (&var);
		g_auto(GStrv)    head = NULL;
		g_auto(GStrv)    tail = NULL;
		const gchar     *args;

		if (!g_shell_parse_argv (env, NULL, &head, error))
			return NULL;

		args = (preset != NULL) ? preset->env_args : "{file}";
		tail = lrg_external_editor_expand_template (args, file, line, column,
		                                            self->project_dir, error);
		if (tail == NULL)
			return NULL;
		argv = join_argv (head, tail);

		/* Known presets know whether they need a terminal; for unknown
		 * programs, $EDITOR conventionally names a terminal editor and
		 * $VISUAL a graphical one. */
		if (preset != NULL)
			terminal = want_terminal (self, preset->terminal);
		else
			terminal = want_terminal (self, g_strcmp0 (source, "$EDITOR") == 0);

		*is_emacsclient = (g_strcmp0 (preset_id, "emacsclient") == 0);
	}
	else
	{
		g_assert (preset != NULL && preset->template_str != NULL);

		argv = lrg_external_editor_expand_template (preset->template_str, file, line,
		                                            column, self->project_dir, error);
		if (argv == NULL)
			return NULL;
		terminal = want_terminal (self, preset->terminal);
		*is_emacsclient = (g_strcmp0 (preset_id, "emacsclient") == 0);
	}

	if (*is_emacsclient)
	{
		g_autofree gchar *server = NULL;

		g_object_get (self->settings, "emacs-server-name", &server, NULL);
		argv = add_emacs_server (argv, server);
	}

	/* Inside a Neovim terminal, hand the file to that instance. */
	if (g_strcmp0 (preset_id, "nvim") == 0 && g_getenv ("NVIM") != NULL &&
	    *g_getenv ("NVIM") != '\0')
	{
		g_autofree gchar *escaped = vim_escape (file);
		gchar            *keys;

		keys = g_strdup_printf ("<C-\\><C-N>:e %s<CR>:call cursor(%d,%d)<CR>",
		                        escaped, MAX (line, 1), MAX (column, 1));
		g_strfreev (argv);
		argv = g_new0 (gchar *, 6);
		argv[0] = g_strdup ("nvim");
		argv[1] = g_strdup ("--server");
		argv[2] = g_strdup (g_getenv ("NVIM"));
		argv[3] = g_strdup ("--remote-send");
		argv[4] = keys;
		return argv;
	}

	if (terminal)
		return wrap_in_terminal (self, argv, error);

	return argv;
}

gchar **
lrg_external_editor_build_argv (LrgExternalEditor  *self,
                                const gchar        *file,
                                gint                line,
                                gint                column,
                                GError            **error)
{
	gboolean is_emacsclient;

	g_return_val_if_fail (LRG_IS_EXTERNAL_EDITOR (self), NULL);
	g_return_val_if_fail (file != NULL, NULL);

	return build_argv_full (self, file, line, column, &is_emacsclient, error);
}

/* ==========================================================================
 * Launching
 * ========================================================================== */

typedef struct
{
	LrgExternalEditor *self;     /* ref */
	gchar             *file;
	gint               line;
	gint               column;
	gboolean           emacsclient;
	gchar             *command;
} LaunchData;

static void
launch_data_free (LaunchData *data)
{
	g_object_unref (data->self);
	g_free (data->file);
	g_free (data->command);
	g_free (data);
}

static gboolean spawn_argv (LrgExternalEditor *self, gchar **argv, LaunchData *data,
                            GError **error);

static void
on_child_exited (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
	LaunchData       *data = user_data;
	g_autoptr(GError) error = NULL;

	if (!g_subprocess_wait_check_finish (G_SUBPROCESS (source), result, &error))
	{
		/* emacsclient fails when no Emacs server runs: start Emacs. */
		if (data->emacsclient)
		{
			g_autofree gchar *line = g_strdup_printf ("+%d:%d", MAX (data->line, 1),
			                                          MAX (data->column, 1));
			gchar            *argv[] = { (gchar *) "emacs", line, data->file, NULL };
			g_autoptr(GError) spawn_error = NULL;
			LaunchData       *retry;

			g_message ("emacsclient failed (%s); starting a new Emacs", error->message);
			retry = g_new0 (LaunchData, 1);
			retry->self = g_object_ref (data->self);
			retry->file = g_strdup (data->file);
			retry->line = data->line;
			retry->column = data->column;
			if (!spawn_argv (data->self, argv, retry, &spawn_error))
				g_signal_emit (data->self, signals[SIGNAL_FAILED], 0, spawn_error->message);
		}
		else
		{
			g_autofree gchar *msg = g_strdup_printf ("%s: %s", data->command, error->message);

			g_signal_emit (data->self, signals[SIGNAL_FAILED], 0, msg);
		}
	}

	launch_data_free (data);
}

/* Spawns @argv detached; @data (consumed) receives the exit status. */
static gboolean
spawn_argv (LrgExternalEditor  *self,
            gchar             **argv,
            LaunchData         *data,
            GError            **error)
{
	g_autoptr(GSubprocessLauncher) launcher = NULL;
	g_autoptr(GSubprocess)         proc = NULL;

	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
	                                      G_SUBPROCESS_FLAGS_STDERR_SILENCE);
	if (self->project_dir != NULL && g_file_test (self->project_dir, G_FILE_TEST_IS_DIR))
		g_subprocess_launcher_set_cwd (launcher, self->project_dir);

	proc = g_subprocess_launcher_spawnv (launcher, (const gchar * const *) argv, error);
	if (proc == NULL)
	{
		launch_data_free (data);
		return FALSE;
	}

	g_free (data->command);
	data->command = g_strjoinv (" ", argv);
	g_signal_emit (self, signals[SIGNAL_LAUNCHED], 0, data->command);

	/* Reap asynchronously; the process outlives us if the editor quits. */
	g_subprocess_wait_check_async (proc, NULL, on_child_exited, data);
	return TRUE;
}

gboolean
lrg_external_editor_open (LrgExternalEditor  *self,
                          const gchar        *file,
                          gint                line,
                          gint                column,
                          GError            **error)
{
	g_auto(GStrv)     argv = NULL;
	g_autofree gchar *preset = NULL;
	g_autofree gchar *absolute = NULL;
	LaunchData       *data;
	gboolean          is_emacsclient = FALSE;

	g_return_val_if_fail (LRG_IS_EXTERNAL_EDITOR (self), FALSE);
	g_return_val_if_fail (file != NULL, FALSE);

	/* Editors are started in the project directory: always pass an
	 * absolute path so relative asset paths cannot be misread. */
	if (g_path_is_absolute (file))
		absolute = g_strdup (file);
	else if (self->project_dir != NULL)
		absolute = g_build_filename (self->project_dir, file, NULL);
	else
		absolute = g_canonicalize_filename (file, NULL);

	/* Emacs first-party path: ask a connected Emacs before spawning. */
	preset = lrg_external_editor_resolve_preset (self, NULL);
	if (lrg_editor_settings_get_emacs_integration (self->settings) &&
	    lrg_external_editor_preset_is_emacs (preset))
	{
		gboolean handled = FALSE;

		g_signal_emit (self, signals[SIGNAL_OPEN_FILE], 0, absolute,
		               MAX (line, 1), MAX (column, 1), &handled);
		if (handled)
			return TRUE;
	}

	argv = build_argv_full (self, absolute, line, column, &is_emacsclient, error);
	if (argv == NULL)
		return FALSE;

	data = g_new0 (LaunchData, 1);
	data->self = g_object_ref (self);
	data->file = g_strdup (absolute);
	data->line = line;
	data->column = column;
	data->emacsclient = is_emacsclient;

	return spawn_argv (self, argv, data, error);
}
