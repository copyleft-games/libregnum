/* lrg-editor-script-api.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorScriptApi: the `editor` global of editor-time tool scripts.
 *
 * One object per editor session. Lua, Python and Gjs each get a console
 * context that lives as long as this object (the REPL), a fresh context
 * per script file, and one context per run-in-editor binding. Every run
 * opens one undo group; groups a script leaves open are closed when the
 * run returns, and a script's own end_undo cannot close the run's group.
 *
 * Script-facing operations are the GI functions below (Gjs / PyGObject
 * through the typelib) and the LrgScriptable method table of the same
 * names (Lua and the Python bridge). The bridges do not turn a GPtrArray
 * or a GStrv into a table, and they cannot pass a function object in, so
 * list results stay arrays on the C side and add_menu_item takes the
 * function's name. Passing the function itself is what the GI path can
 * do; from Lua or the Python bridge pass the name.
 *
 * Hot reload of `<project>/editor_scripts/` is a directory monitor plus a
 * name/mtime stamp checked from tick(), so it works with no main loop.
 * LrgScriptReloader's watch and process entry points are weak and are
 * called only when this link provides them, so a stub reloader still
 * links. This tree's reloader defines them, so tick uses them.
 */

#include "config.h"

#include "lrg-editor-script-api.h"

#include <string.h>
#include <sys/stat.h>

#include <glib/gstdio.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>

#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "lrg-script-binding.h"
#include "scripting/lrg-script-module.h"
#include "scripting/lrg-script-reloader.h"
#include "scripting/lrg-scriptable.h"
#include "scripting/lrg-scripting-gi.h"
#include "scripting/lrg-scripting-manager.h"
#include "scripting/lrg-scripting.h"

/* Unresolved when the reloader is still the stub. `new` is strong. */
#pragma weak lrg_script_reloader_get_default
#pragma weak lrg_script_reloader_watch_path
#pragma weak lrg_script_reloader_unwatch_path
#pragma weak lrg_script_reloader_process_pending

#define LRG_SCRIPT_DEG2RAD (3.14159265358979323846 / 180.0)

/* --------------------------------------------------------------------------
 * Types
 * -------------------------------------------------------------------------- */

typedef struct
{
	LrgEditorScriptApi *api;
	LrgScriptLanguage   language;
	LrgScripting       *scripting; /* owned */
	gchar              *path;      /* canonical, NULL for a REPL */
	gboolean            tool;      /* loaded from editor_scripts/ */
} HeldContext;

typedef struct
{
	HeldContext *held;
	gint         floor; /* undo_depth on entry; the run's group sits above it */
} RunFrame;

typedef struct
{
	LrgEditorScriptApi *api;
	gchar              *name;
	gchar              *id;       /* script.tool.<name> */
	gchar              *label;    /* as registered, may contain '/' */
	gchar              *function;
	gchar              *shortcut; /* NULL when none */
	HeldContext        *owner;    /* NULL once that context is gone */
	gboolean            active;
} Tool;

typedef struct
{
	LrgEditorScriptApi *api;
	LrgScriptBinding   *binding; /* ref */
	LrgNode            *node;    /* ref */
	HeldContext         held;
	gint64              mtime;
	gchar              *error;   /* NULL when the last start succeeded */
	gboolean            started;
	gboolean            failed;
} EditorRun;

typedef struct
{
	gchar *path;
	gchar *message;
} ScriptFailure;

struct _LrgEditorScriptApi
{
	GObject parent_instance;

	LrgEditor        *editor;  /* ref */
	LrgEditorServer  *server;  /* ref; private until set_server() */
	gboolean          server_private;
	LrgEditorActions *actions; /* ref, nullable */
	LrgEditorLog     *log;     /* ref, nullable */
	gchar            *project_dir;

	GHashTable *repl;  /* language -> HeldContext* */
	GHashTable *files; /* canonical path -> HeldContext* */
	GPtrArray  *tools; /* Tool*, registration order, never compacted */
	GMenu      *menu;
	GPtrArray  *frames; /* RunFrame* */
	gint        undo_depth;

	GHashTable *editor_runs; /* LrgScriptBinding* -> EditorRun* */
	GPtrArray  *failures;    /* ScriptFailure* from the last reload */

	GFileMonitor      *monitor;
	gchar             *watched_dir;
	gboolean           tools_dirty;
	gchar             *tools_stamp;
	LrgScriptReloader *reloader;

	GPtrArray *captures; /* GString*, innermost last */
	gboolean   finalizing;

	guint output_id;
};

enum
{
	PROP_0,
	PROP_EDITOR,
	PROP_PROJECT_DIR,
	N_PROPS
};

enum
{
	SIGNAL_OUTPUT,
	N_SIGNALS
};

static GParamSpec *api_props[N_PROPS];
static guint       api_signals[N_SIGNALS];

static void api_scriptable_init (LrgScriptableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgEditorScriptApi, lrg_editor_script_api, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_SCRIPTABLE, api_scriptable_init))

/* --------------------------------------------------------------------------
 * Small helpers
 * -------------------------------------------------------------------------- */

static void
emit_line (LrgEditorScriptApi *self,
           GLogLevelFlags      level,
           const gchar        *text)
{
	gchar   *line;
	gsize    len;
	GString *cap;

	if (text == NULL)
		text = "";
	line = g_strdup (text);
	len = strlen (line);
	if (len > 0 && line[len - 1] == '\n')
		line[len - 1] = '\0';

	g_signal_emit (self, api_signals[SIGNAL_OUTPUT], 0, line);
	if (self->log != NULL)
		lrg_editor_log_append (self->log, level, "script", line);
	if (self->captures->len > 0)
	{
		cap = (GString *) g_ptr_array_index (self->captures, self->captures->len - 1);
		if (cap->len > 0)
			g_string_append_c (cap, '\n');
		g_string_append (cap, line);
	}
	g_free (line);
}

static void
prefix_location (LrgScripting *scripting,
                 GError      **error)
{
	g_autofree gchar *file = NULL;
	gint              line = 0;
	const gchar      *base;
	g_autofree gchar *prefix = NULL;

	if (error == NULL || *error == NULL || scripting == NULL)
		return;
	if (!lrg_scripting_get_error_location (scripting, &file, &line) || line <= 0)
		return;
	base = (file != NULL) ? file : "(script)";
	prefix = g_strdup_printf ("%s:%d:", base, line);
	if (g_str_has_prefix ((*error)->message, prefix) || strstr ((*error)->message, prefix) != NULL)
		return;
	g_prefix_error (error, "%s:%d: ", base, line);
}

static const gchar *
language_id (LrgScriptLanguage language)
{
	switch (language)
	{
	case LRG_SCRIPT_LANGUAGE_LUA:
		return "lua";
	case LRG_SCRIPT_LANGUAGE_PYTHON:
		return "python";
	case LRG_SCRIPT_LANGUAGE_GJS:
		return "gjs";
	default:
		return "none";
	}
}

static gboolean
name_is_tool (const gchar *name)
{
	const gchar *p;

	if (name == NULL || *name == '\0')
		return FALSE;
	for (p = name; *p != '\0'; p++)
	{
		if (!g_ascii_islower (*p) && !g_ascii_isdigit (*p) && *p != '_' && *p != '-')
			return FALSE;
	}
	return TRUE;
}

static gboolean
path_in_dir (const gchar *path,
             const gchar *dir)
{
	gsize n;

	if (path == NULL || dir == NULL)
		return FALSE;
	n = strlen (dir);
	if (strncmp (path, dir, n) != 0)
		return FALSE;
	return path[n] == '\0' || path[n] == G_DIR_SEPARATOR;
}

static gchar *
resolve_against_project (LrgEditorScriptApi *self,
                         const gchar        *path)
{
	g_autofree gchar *built = NULL;

	if (path == NULL)
		return NULL;
	if (g_path_is_absolute (path) || self->project_dir == NULL)
		return g_canonicalize_filename (path, NULL);
	built = g_build_filename (self->project_dir, path, NULL);
	return g_canonicalize_filename (built, NULL);
}

static gchar *
project_child (LrgEditorScriptApi *self,
               const gchar        *relative,
               GError            **error)
{
	g_autofree gchar *canon = NULL;
	gsize             n;

	if (self->project_dir == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "No project open");
		return NULL;
	}
	if (relative == NULL || *relative == '\0' || g_path_is_absolute (relative))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
		                     "Path must be project-relative");
		return NULL;
	}
	canon = g_canonicalize_filename (relative, self->project_dir);
	n = strlen (self->project_dir);
	if (strncmp (canon, self->project_dir, n) != 0 || canon[n] != G_DIR_SEPARATOR)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
		                     "Path leaves the project");
		return NULL;
	}
	return g_steal_pointer (&canon);
}

/* --------------------------------------------------------------------------
 * Contexts
 * -------------------------------------------------------------------------- */

static gboolean
print_cb (LrgScripting *scripting,
          guint         n_args,
          const GValue *args,
          GValue       *return_value,
          gpointer      user_data,
          GError      **error)
{
	LrgEditorScriptApi *self = (LrgEditorScriptApi *) user_data;
	GString            *line;
	guint               i;

	(void) scripting;
	(void) return_value;
	(void) error;

	line = g_string_new ("");
	for (i = 0; i < n_args; i++)
	{
		g_autofree gchar *text = NULL;

		if (i > 0)
			g_string_append_c (line, '\t');
		if (G_VALUE_HOLDS_STRING (&args[i]) && g_value_get_string (&args[i]) != NULL)
			g_string_append (line, g_value_get_string (&args[i]));
		else
		{
			text = g_strdup_value_contents (&args[i]);
			g_string_append (line, text);
		}
	}
	emit_line (self, G_LOG_LEVEL_MESSAGE, line->str);
	g_string_free (line, TRUE);
	return TRUE;
}

static void
install_print (LrgEditorScriptApi *self,
               LrgScripting       *scripting,
               LrgScriptLanguage   language)
{
	static const gchar *lua =
		"function print(...)\n"
		"  local n = select('#', ...)\n"
		"  local t = {}\n"
		"  for i = 1, n do t[i] = tostring(select(i, ...)) end\n"
		"  __lrg_print(table.concat(t, '\\t'))\n"
		"end\n";
	static const gchar *python =
		"def print(*args, **kwargs):\n"
		"    sep = kwargs.get('sep', ' ')\n"
		"    end = kwargs.get('end', '\\n')\n"
		"    if end is None: end = ''\n"
		"    __lrg_print(sep.join(str(a) for a in args) + end)\n";
	static const gchar *gjs =
		"function print() {\n"
		"  var parts = [];\n"
		"  for (var i = 0; i < arguments.length; i++)\n"
		"    parts.push(String(arguments[i]));\n"
		"  __lrg_print(parts.join('\\t'));\n"
		"}\n";
	const gchar       *src;
	g_autoptr(GError)  ignored = NULL;

	lrg_scripting_register_function (scripting, "__lrg_print", print_cb, self, &ignored);
	g_clear_error (&ignored);
	src = NULL;
	if (language == LRG_SCRIPT_LANGUAGE_LUA)
		src = lua;
	else if (language == LRG_SCRIPT_LANGUAGE_PYTHON)
		src = python;
	else if (language == LRG_SCRIPT_LANGUAGE_GJS)
		src = gjs;
	if (src != NULL && !lrg_scripting_load_string (scripting, "=(print)", src, &ignored))
		g_clear_error (&ignored);
}

static LrgScripting *
context_open (LrgEditorScriptApi *self,
              LrgScriptLanguage   language,
              LrgNode            *node,
              GError            **error)
{
	LrgScriptingManager *manager;
	LrgScripting        *scripting;
	GValue               value = G_VALUE_INIT;
	g_autoptr(GError)    ignored = NULL;

	if (!lrg_editor_script_api_language_is_supported (language))
	{
		g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_UNSUPPORTED,
		             "Language '%s' is not available in this build", language_id (language));
		return NULL;
	}
	manager = lrg_scripting_manager_get_default ();
	scripting = lrg_scripting_manager_create_context (manager, language);
	if (scripting == NULL)
	{
		g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_UNSUPPORTED,
		             "Could not create a %s context", language_id (language));
		return NULL;
	}
	if (self->project_dir != NULL)
		lrg_scripting_add_search_path (scripting, self->project_dir);
	if (self->editor != NULL)
	{
		g_value_init (&value, LRG_TYPE_EDITOR_SCRIPT_API);
		g_value_set_object (&value, self);
		lrg_scripting_set_global (scripting, "editor", &value, &ignored);
		g_value_unset (&value);
		g_clear_error (&ignored);
		if (LRG_IS_SCRIPTING_GI (scripting))
			lrg_scripting_gi_expose_object (LRG_SCRIPTING_GI (scripting), "editor",
			                                G_OBJECT (self), &ignored);
		g_clear_error (&ignored);
	}
	if (node != NULL)
	{
		g_value_init (&value, LRG_TYPE_NODE);
		g_value_set_object (&value, node);
		lrg_scripting_set_global (scripting, "node", &value, &ignored);
		g_value_unset (&value);
		g_clear_error (&ignored);
		if (LRG_IS_SCRIPTING_GI (scripting))
			lrg_scripting_gi_expose_object (LRG_SCRIPTING_GI (scripting), "node",
			                                G_OBJECT (node), &ignored);
		g_clear_error (&ignored);
	}
	install_print (self, scripting, language);
	return scripting;
}

static void
held_free (gpointer data)
{
	HeldContext *held = (HeldContext *) data;

	/* Tools keep their action, disabled, after the context goes away. */
	if (held->api != NULL && !held->api->finalizing)
	{
		guint i;

		for (i = 0; i < held->api->tools->len; i++)
		{
			Tool *tool = (Tool *) g_ptr_array_index (held->api->tools, i);

			if (tool->owner == held)
			{
				tool->owner = NULL;
				tool->active = FALSE;
			}
		}
	}
	g_clear_object (&held->scripting);
	g_free (held->path);
	g_free (held);
}

static HeldContext *
held_new (LrgEditorScriptApi *self,
          LrgScriptLanguage   language,
          LrgNode            *node,
          const gchar        *path,
          gboolean            tool,
          GError            **error)
{
	HeldContext  *held;
	LrgScripting *scripting;

	scripting = context_open (self, language, node, error);
	if (scripting == NULL)
		return NULL;
	held = g_new0 (HeldContext, 1);
	held->api = self;
	held->language = language;
	held->scripting = scripting;
	held->path = g_strdup (path);
	held->tool = tool;
	return held;
}

static HeldContext *
repl_held (LrgEditorScriptApi *self,
           LrgScriptLanguage   language,
           GError            **error)
{
	HeldContext *held;

	held = (HeldContext *) g_hash_table_lookup (self->repl, GINT_TO_POINTER (language));
	if (held != NULL)
		return held;
	held = held_new (self, language, NULL, NULL, FALSE, error);
	if (held == NULL)
		return NULL;
	g_hash_table_insert (self->repl, GINT_TO_POINTER (language), held);
	return held;
}

/* --------------------------------------------------------------------------
 * Undo frames and tools
 * -------------------------------------------------------------------------- */

static RunFrame *
frame_top (LrgEditorScriptApi *self)
{
	if (self->frames->len == 0)
		return NULL;
	return (RunFrame *) g_ptr_array_index (self->frames, self->frames->len - 1);
}

static void
run_enter (LrgEditorScriptApi *self,
           HeldContext        *held,
           const gchar        *label)
{
	RunFrame *frame;

	frame = g_new0 (RunFrame, 1);
	frame->held = held;
	frame->floor = self->undo_depth;
	g_ptr_array_add (self->frames, frame);
	lrg_editor_begin_group (self->editor, label);
	self->undo_depth++;
}

static void
run_leave (LrgEditorScriptApi *self)
{
	RunFrame *frame;

	if (self->frames->len == 0)
		return;
	frame = (RunFrame *) g_ptr_array_index (self->frames, self->frames->len - 1);
	while (self->undo_depth > frame->floor)
	{
		lrg_editor_end_group (self->editor);
		self->undo_depth--;
	}
	g_ptr_array_remove_index (self->frames, self->frames->len - 1);
}

static void
rebuild_menu (LrgEditorScriptApi *self);

/* Activated below, after run_tool exists. Declared here so the add can
 * see the function. */
static void tool_activate (gpointer data);
static gboolean tool_enabled (gpointer data);

static void
tool_sync_action_full (LrgEditorScriptApi *self,
                       Tool               *tool)
{
	const gchar *leaf;
	const gchar *slash;

	if (self->finalizing || self->actions == NULL || !g_action_name_is_valid (tool->id))
		return;
	slash = strrchr (tool->label, '/');
	leaf = (slash != NULL) ? slash + 1 : tool->label;
	lrg_editor_actions_add (self->actions, tool->id, leaf, "Scripts", tool->shortcut,
	                        tool_activate, tool_enabled, NULL, tool);
}

static Tool *
find_tool (LrgEditorScriptApi *self,
           const gchar        *id)
{
	guint        i;
	const gchar *bare;

	if (id == NULL)
		return NULL;
	bare = id;
	if (g_str_has_prefix (id, LRG_EDITOR_SCRIPT_TOOL_PREFIX))
		bare = id + strlen (LRG_EDITOR_SCRIPT_TOOL_PREFIX);
	for (i = 0; i < self->tools->len; i++)
	{
		Tool *tool = (Tool *) g_ptr_array_index (self->tools, i);

		if (g_strcmp0 (tool->id, id) == 0 || g_strcmp0 (tool->name, bare) == 0 ||
		    g_strcmp0 (tool->name, id) == 0)
			return tool;
	}
	return NULL;
}

static GMenu *
ensure_submenu (GMenu       *root,
                GHashTable  *subs,
                const gchar *path)
{
	GMenu            *found;
	const gchar      *slash;
	g_autofree gchar *parent = NULL;
	const gchar      *leaf;
	GMenu            *parent_menu;
	GMenu            *sub;

	if (path == NULL || *path == '\0')
		return root;
	found = (GMenu *) g_hash_table_lookup (subs, path);
	if (found != NULL)
		return found;
	slash = strrchr (path, '/');
	if (slash == NULL)
	{
		parent_menu = root;
		leaf = path;
	}
	else
	{
		parent = g_strndup (path, (gsize) (slash - path));
		parent_menu = ensure_submenu (root, subs, parent);
		leaf = slash + 1;
	}
	sub = g_menu_new ();
	g_menu_append_submenu (parent_menu, leaf, G_MENU_MODEL (sub));
	g_hash_table_insert (subs, g_strdup (path), sub);
	return sub;
}

static void
rebuild_menu (LrgEditorScriptApi *self)
{
	g_autoptr(GHashTable) subs = NULL;
	guint                 i;

	if (self->finalizing)
		return;
	while (g_menu_model_get_n_items (G_MENU_MODEL (self->menu)) > 0)
		g_menu_remove (self->menu, 0);
	subs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	for (i = 0; i < self->tools->len; i++)
	{
		Tool             *tool = (Tool *) g_ptr_array_index (self->tools, i);
		const gchar      *slash;
		g_autofree gchar *parent = NULL;
		const gchar      *leaf;
		GMenu            *dest;
		g_autofree gchar *detailed = NULL;

		if (!tool->active)
			continue;
		slash = strrchr (tool->label, '/');
		if (slash == NULL)
		{
			dest = self->menu;
			leaf = tool->label;
		}
		else
		{
			parent = g_strndup (tool->label, (gsize) (slash - tool->label));
			dest = ensure_submenu (self->menu, subs, parent);
			leaf = slash + 1;
		}
		detailed = g_strconcat ("editor.", tool->id, NULL);
		g_menu_append (dest, leaf, detailed);
	}
}

static void
tool_free (gpointer data)
{
	Tool *tool = (Tool *) data;

	g_free (tool->name);
	g_free (tool->id);
	g_free (tool->label);
	g_free (tool->function);
	g_free (tool->shortcut);
	g_free (tool);
}

/* --------------------------------------------------------------------------
 * Evaluating a snippet
 * -------------------------------------------------------------------------- */

static gboolean
read_result (LrgScripting *scripting,
             gchar       **result)
{
	GValue value = G_VALUE_INIT;

	if (result == NULL)
		return TRUE;
	*result = NULL;
	if (!lrg_scripting_get_global (scripting, "__lrg_result", &value, NULL))
		return TRUE;
	if (G_VALUE_HOLDS_STRING (&value) && g_value_get_string (&value) != NULL)
		*result = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);
	return TRUE;
}

/* Runs @code in @held. @chunk_name is the source name (a file path); NULL
 * is the REPL. Does not open an undo group: the caller does. */
static gboolean
eval_code (HeldContext  *held,
           const gchar  *code,
           const gchar  *chunk_name,
           gchar       **result,
           GError      **error)
{
	static const gchar *lua =
		"local src = __lrg_src\n"
		"local name = __lrg_name or '=(repl)'\n"
		"__lrg_result = nil\n"
		"local function pack(...)\n"
		"  local n = select('#', ...)\n"
		"  if n == 0 then return nil end\n"
		"  local t = {}\n"
		"  for i = 1, n do t[i] = tostring(select(i, ...)) end\n"
		"  return table.concat(t, '\\t')\n"
		"end\n"
		"local fn, err = load('return ' .. src, name)\n"
		"if fn then\n"
		"  local ok, res = pcall(function() return pack(fn()) end)\n"
		"  if not ok then error(res) end\n"
		"  __lrg_result = res\n"
		"else\n"
		"  fn, err = load(src, name)\n"
		"  if not fn then error(err) end\n"
		"  local ok, res = pcall(function() return pack(fn()) end)\n"
		"  if not ok then error(res) end\n"
		"  __lrg_result = res\n"
		"end\n";
	static const gchar *python =
		"import ast\n"
		"_src = __lrg_src\n"
		"_name = __lrg_name or '<repl>'\n"
		"__lrg_result = None\n"
		"try:\n"
		"    _tree = ast.parse(_src, mode='eval')\n"
		"except SyntaxError:\n"
		"    exec(compile(_src, _name, 'exec'))\n"
		"else:\n"
		"    __lrg_result = repr(eval(compile(_tree, _name, 'eval')))\n";
	GValue       src = G_VALUE_INIT;
	GValue       name = G_VALUE_INIT;
	GValue       cleared = G_VALUE_INIT;
	gboolean     ok;
	const gchar *runner;
	const gchar *shown;

	if (result != NULL)
		*result = NULL;
	shown = (chunk_name != NULL) ? chunk_name : "=(repl)";

	g_value_init (&cleared, G_TYPE_POINTER);
	g_value_set_pointer (&cleared, NULL);
	lrg_scripting_set_global (held->scripting, "__lrg_result", &cleared, NULL);
	g_value_unset (&cleared);

	if (held->language == LRG_SCRIPT_LANGUAGE_GJS)
	{
		ok = lrg_scripting_load_string (held->scripting, shown, code, error);
		if (!ok)
			prefix_location (held->scripting, error);
		return ok;
	}

	g_value_init (&src, G_TYPE_STRING);
	g_value_set_string (&src, code);
	ok = lrg_scripting_set_global (held->scripting, "__lrg_src", &src, error);
	g_value_unset (&src);
	if (!ok)
		return FALSE;
	g_value_init (&name, G_TYPE_STRING);
	g_value_set_string (&name, shown);
	ok = lrg_scripting_set_global (held->scripting, "__lrg_name", &name, error);
	g_value_unset (&name);
	if (!ok)
		return FALSE;
	runner = (held->language == LRG_SCRIPT_LANGUAGE_PYTHON) ? python : lua;
	ok = lrg_scripting_load_string (held->scripting, shown, runner, error);
	if (!ok)
		prefix_location (held->scripting, error);
	else
		read_result (held->scripting, result);
	return ok;
}

static gboolean
eval_in (LrgEditorScriptApi *self,
         HeldContext        *held,
         const gchar        *code,
         const gchar        *label,
         gchar             **result,
         GError            **error)
{
	gboolean ok;

	if (result != NULL)
		*result = NULL;
	run_enter (self, held, (label != NULL) ? label : "Script");
	ok = eval_code (held, code, NULL, result, error);
	run_leave (self);
	return ok;
}

/* --------------------------------------------------------------------------
 * Run-in-editor bookkeeping (defined with tick, used by free)
 * -------------------------------------------------------------------------- */

static void editor_run_detach (EditorRun *run);

static void
editor_run_free (gpointer data)
{
	EditorRun *run = (EditorRun *) data;

	editor_run_detach (run);
	if (run->api != NULL && !run->api->finalizing)
	{
		guint i;

		for (i = 0; i < run->api->tools->len; i++)
		{
			Tool *tool = (Tool *) g_ptr_array_index (run->api->tools, i);

			if (tool->owner == &run->held)
			{
				tool->owner = NULL;
				tool->active = FALSE;
			}
		}
		rebuild_menu (run->api);
	}
	g_clear_object (&run->held.scripting);
	g_clear_object (&run->binding);
	g_clear_object (&run->node);
	g_free (run->held.path);
	g_free (run->error);
	g_free (run);
}

static void
failure_free (gpointer data)
{
	ScriptFailure *fail = (ScriptFailure *) data;

	g_free (fail->path);
	g_free (fail->message);
	g_free (fail);
}

static void
remember_failure (LrgEditorScriptApi *self,
                  const gchar        *path,
                  const gchar        *message)
{
	ScriptFailure *fail;

	fail = g_new0 (ScriptFailure, 1);
	fail->path = g_strdup (path);
	fail->message = g_strdup ((message != NULL) ? message : "failed");
	g_ptr_array_add (self->failures, fail);
	emit_line (self, G_LOG_LEVEL_WARNING, fail->message);
}

/* --------------------------------------------------------------------------
 * Public: construction and wiring
 * -------------------------------------------------------------------------- */

LrgEditorScriptApi *
lrg_editor_script_api_new (LrgEditor *editor)
{
	g_return_val_if_fail (LRG_IS_EDITOR (editor), NULL);

	return (LrgEditorScriptApi *) g_object_new (LRG_TYPE_EDITOR_SCRIPT_API,
	                                            "editor", editor,
	                                            NULL);
}

LrgEditor *
lrg_editor_script_api_get_editor (LrgEditorScriptApi *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), NULL);

	return self->editor;
}

void
lrg_editor_script_api_set_server (LrgEditorScriptApi *self,
                                  LrgEditorServer    *server)
{
	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));
	g_return_if_fail (server == NULL || LRG_IS_EDITOR_SERVER (server));

	if (self->server == server)
		return;
	g_clear_object (&self->server);
	self->server_private = FALSE;
	if (server != NULL)
		self->server = (LrgEditorServer *) g_object_ref (server);
}

LrgEditorServer *
lrg_editor_script_api_get_server (LrgEditorScriptApi *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), NULL);

	if (self->server == NULL && self->editor != NULL)
	{
		self->server = lrg_editor_server_new (self->editor);
		self->server_private = TRUE;
	}
	return self->server;
}

void
lrg_editor_script_api_set_actions (LrgEditorScriptApi *self,
                                   LrgEditorActions   *actions)
{
	guint i;

	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));
	g_return_if_fail (actions == NULL || LRG_IS_EDITOR_ACTIONS (actions));

	if (self->actions == actions)
		return;
	g_set_object (&self->actions, actions);
	if (self->actions == NULL)
		return;
	for (i = 0; i < self->tools->len; i++)
		tool_sync_action_full (self, (Tool *) g_ptr_array_index (self->tools, i));
}

LrgEditorActions *
lrg_editor_script_api_get_actions (LrgEditorScriptApi *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), NULL);

	return self->actions;
}

void
lrg_editor_script_api_set_log (LrgEditorScriptApi *self,
                               LrgEditorLog       *log)
{
	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));
	g_return_if_fail (log == NULL || LRG_IS_EDITOR_LOG (log));

	g_set_object (&self->log, log);
}

static void watch_tools_dir (LrgEditorScriptApi *self);
static guint reload_tools_internal (LrgEditorScriptApi *self,
                                    gboolean            announce);

void
lrg_editor_script_api_set_project_dir (LrgEditorScriptApi *self,
                                       const gchar        *dir)
{
	g_autofree gchar *canon = NULL;
	GHashTableIter    iter;
	gpointer          key;
	HeldContext      *held;

	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));

	if (dir != NULL && *dir != '\0')
		canon = g_canonicalize_filename (dir, NULL);
	if (g_strcmp0 (canon, self->project_dir) == 0)
		return;

	g_hash_table_iter_init (&iter, self->files);
	while (g_hash_table_iter_next (&iter, &key, (gpointer *) &held))
	{
		if (held->tool)
			g_hash_table_iter_remove (&iter);
	}
	rebuild_menu (self);

	g_free (self->project_dir);
	self->project_dir = g_steal_pointer (&canon);
	g_free (self->tools_stamp);
	self->tools_stamp = NULL;
	self->tools_dirty = FALSE;
	g_clear_object (&self->monitor);
	g_free (self->watched_dir);
	self->watched_dir = NULL;

	if (self->project_dir != NULL)
	{
		g_hash_table_iter_init (&iter, self->repl);
		while (g_hash_table_iter_next (&iter, &key, (gpointer *) &held))
			lrg_scripting_add_search_path (held->scripting, self->project_dir);
		g_hash_table_iter_init (&iter, self->files);
		while (g_hash_table_iter_next (&iter, &key, (gpointer *) &held))
			lrg_scripting_add_search_path (held->scripting, self->project_dir);
	}

	watch_tools_dir (self);
	reload_tools_internal (self, TRUE);
	g_object_notify_by_pspec (G_OBJECT (self), api_props[PROP_PROJECT_DIR]);
}

const gchar *
lrg_editor_script_api_get_project_dir (LrgEditorScriptApi *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), NULL);

	return self->project_dir;
}

/* --------------------------------------------------------------------------
 * Public: running code
 * -------------------------------------------------------------------------- */

LrgScriptLanguage
lrg_editor_script_api_language_from_name (const gchar *name)
{
	if (name == NULL || *name == '\0')
		return LRG_SCRIPT_LANGUAGE_NONE;
	if (g_ascii_strcasecmp (name, "lua") == 0)
		return LRG_SCRIPT_LANGUAGE_LUA;
	if (g_ascii_strcasecmp (name, "python") == 0 || g_ascii_strcasecmp (name, "py") == 0)
		return LRG_SCRIPT_LANGUAGE_PYTHON;
	if (g_ascii_strcasecmp (name, "gjs") == 0 || g_ascii_strcasecmp (name, "js") == 0 ||
	    g_ascii_strcasecmp (name, "javascript") == 0)
		return LRG_SCRIPT_LANGUAGE_GJS;
	return LRG_SCRIPT_LANGUAGE_NONE;
}

gboolean
lrg_editor_script_api_language_is_supported (LrgScriptLanguage language)
{
	if (language != LRG_SCRIPT_LANGUAGE_LUA && language != LRG_SCRIPT_LANGUAGE_PYTHON &&
	    language != LRG_SCRIPT_LANGUAGE_GJS)
		return FALSE;
	return lrg_scripting_manager_is_available (lrg_scripting_manager_get_default (), language);
}

gboolean
lrg_editor_script_api_run_string (LrgEditorScriptApi *self,
                                  LrgScriptLanguage   language,
                                  const gchar        *code,
                                  const gchar        *label,
                                  gchar             **result,
                                  GError            **error)
{
	HeldContext *held;

	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), FALSE);
	g_return_val_if_fail (code != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (result != NULL)
		*result = NULL;
	held = repl_held (self, language, error);
	if (held == NULL)
		return FALSE;
	return eval_in (self, held, code, label, result, error);
}

gboolean
lrg_editor_script_api_run_file (LrgEditorScriptApi  *self,
                                const gchar         *path,
                                LrgScriptLanguage    language,
                                gchar              **result,
                                GError             **error)
{
	g_autofree gchar *canon = NULL;
	g_autofree gchar *base = NULL;
	g_autofree gchar *label = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *scripts = NULL;
	g_autofree gchar *contents = NULL;
	HeldContext      *held;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (result != NULL)
		*result = NULL;
	canon = resolve_against_project (self, path);
	if (!g_file_test (canon, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No such script '%s'", path);
		return FALSE;
	}
	if (!g_file_get_contents (canon, &contents, NULL, error))
		return FALSE;
	if (language == LRG_SCRIPT_LANGUAGE_NONE)
		language = lrg_scripting_manager_language_for_path (lrg_scripting_manager_get_default (), canon);
	if (self->project_dir != NULL)
		scripts = g_build_filename (self->project_dir, LRG_EDITOR_SCRIPTS_DIR, NULL);

	/* Replacing the file drops the tools its previous context registered. */
	if (g_hash_table_contains (self->files, canon))
	{
		g_hash_table_remove (self->files, canon);
		rebuild_menu (self);
	}

	held = held_new (self, language, NULL, canon, path_in_dir (canon, scripts), error);
	if (held == NULL)
		return FALSE;
	dir = g_path_get_dirname (canon);
	lrg_scripting_add_search_path (held->scripting, dir);
	g_hash_table_insert (self->files, g_strdup (canon), held);

	base = g_path_get_basename (canon);
	label = g_strdup_printf ("Run %s", base);
	run_enter (self, held, label);
	/* Same return capture as the REPL, with the file path as the chunk name. */
	ok = eval_code (held, contents, canon, result, error);
	run_leave (self);
	return ok;
}

/* --------------------------------------------------------------------------
 * Tools
 * -------------------------------------------------------------------------- */

static gboolean
tool_enabled (gpointer data)
{
	Tool *tool = (Tool *) data;

	if (tool == NULL || tool->api == NULL || tool->api->finalizing)
		return FALSE;
	return tool->active && tool->owner != NULL && tool->owner->scripting != NULL;
}

gboolean
lrg_editor_script_api_add_menu_item (LrgEditorScriptApi *self,
                                     const gchar        *name,
                                     const gchar        *label,
                                     const gchar        *function,
                                     const gchar        *shortcut)
{
	RunFrame *frame;
	Tool     *tool;

	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), FALSE);

	frame = frame_top (self);
	if (frame == NULL || frame->held == NULL || !name_is_tool (name) ||
	    function == NULL || *function == '\0')
		return FALSE;
	if (label == NULL || *label == '\0')
		label = name;

	tool = find_tool (self, name);
	if (tool == NULL)
	{
		tool = g_new0 (Tool, 1);
		tool->api = self;
		tool->name = g_strdup (name);
		tool->id = g_strconcat (LRG_EDITOR_SCRIPT_TOOL_PREFIX, name, NULL);
		g_ptr_array_add (self->tools, tool);
	}
	else
	{
		g_free (tool->label);
		g_free (tool->function);
		g_free (tool->shortcut);
	}
	tool->label = g_strdup (label);
	tool->function = g_strdup (function);
	tool->shortcut = (shortcut != NULL && *shortcut != '\0') ? g_strdup (shortcut) : NULL;
	tool->owner = frame->held;
	tool->active = TRUE;
	tool_sync_action_full (self, tool);
	rebuild_menu (self);
	return TRUE;
}

GStrv
lrg_editor_script_api_get_tools (LrgEditorScriptApi *self)
{
	GPtrArray *ids;
	guint      i;

	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), NULL);

	ids = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < self->tools->len; i++)
	{
		Tool *tool = (Tool *) g_ptr_array_index (self->tools, i);

		if (tool->active)
			g_ptr_array_add (ids, g_strdup (tool->id));
	}
	g_ptr_array_add (ids, NULL);
	return (GStrv) g_ptr_array_free (ids, FALSE);
}

const gchar *
lrg_editor_script_api_get_tool_label (LrgEditorScriptApi *self,
                                      const gchar        *id)
{
	Tool *tool;

	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), NULL);

	tool = find_tool (self, id);
	if (tool == NULL)
		return NULL;
	return tool->label;
}

gboolean
lrg_editor_script_api_run_tool (LrgEditorScriptApi *self,
                                const gchar        *id,
                                GError            **error)
{
	Tool     *tool;
	gboolean  ok;

	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	tool = find_tool (self, id);
	if (tool == NULL || !tool->active || tool->owner == NULL || tool->owner->scripting == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "No tool '%s'", id);
		return FALSE;
	}
	if (!lrg_scripting_has_function (tool->owner->scripting, tool->function))
	{
		g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND,
		             "Tool '%s' has no function '%s'", tool->name, tool->function);
		return FALSE;
	}
	run_enter (self, tool->owner, tool->label);
	ok = lrg_scripting_call_function (tool->owner->scripting, tool->function, NULL, 0, NULL, error);
	if (!ok)
		prefix_location (tool->owner->scripting, error);
	run_leave (self);
	return ok;
}

static void
tool_activate (gpointer data)
{
	Tool             *tool = (Tool *) data;
	g_autoptr(GError) error = NULL;

	if (!tool_enabled (tool))
		return;
	if (!lrg_editor_script_api_run_tool (tool->api, tool->id, &error) && error != NULL)
		emit_line (tool->api, G_LOG_LEVEL_WARNING, error->message);
}

GMenuModel *
lrg_editor_script_api_get_menu (LrgEditorScriptApi *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), NULL);

	return G_MENU_MODEL (self->menu);
}

/* --------------------------------------------------------------------------
 * Run-in-editor
 * -------------------------------------------------------------------------- */

static gboolean
call_hook (LrgEditorScriptApi *self,
           EditorRun          *run,
           const gchar        *name,
           guint               n_args,
           const GValue       *args,
           GError            **error)
{
	gboolean ok;

	if (!lrg_scripting_has_function (run->held.scripting, name))
		return TRUE;
	run_enter (self, &run->held, "Run in Editor");
	/* The tick (or stop) already opened the group this run nests under
	 * when called from there. A hook invoked on its own still gets one. */
	ok = lrg_scripting_call_function (run->held.scripting, name, NULL, n_args, args, error);
	if (!ok)
		prefix_location (run->held.scripting, error);
	run_leave (self);
	return ok;
}

static void
editor_run_detach (EditorRun *run)
{
	g_autoptr(GError) error = NULL;

	if (!run->started || run->held.scripting == NULL)
	{
		run->started = FALSE;
		return;
	}
	if (run->api != NULL && run->api->editor != NULL)
		call_hook (run->api, run, LRG_SCRIPT_HOOK_DETACH, 0, NULL, &error);
	run->started = FALSE;
	if (error != NULL && run->api != NULL)
		emit_line (run->api, G_LOG_LEVEL_WARNING, error->message);
}

static gint64
file_mtime (const gchar *path)
{
	GStatBuf st;

	if (path == NULL || g_stat (path, &st) != 0)
		return 0;
	return (gint64) st.st_mtime;
}

static void
editor_run_fail (EditorRun   *run,
                 const gchar *message)
{
	/* A start that already ran gets detach; a failed start does not. */
	if (run->started)
		editor_run_detach (run);
	g_free (run->error);
	run->error = g_strdup ((message != NULL) ? message : "failed");
	run->failed = TRUE;
	run->started = FALSE;
	if (run->api != NULL)
		emit_line (run->api, G_LOG_LEVEL_WARNING, run->error);
}

static void
editor_run_start (LrgEditorScriptApi *self,
                  EditorRun          *run,
                  LrgNode            *node,
                  gfloat              delta)
{
	g_autoptr(GError) error = NULL;
	GValue            arg = G_VALUE_INIT;
	const gchar      *script;
	LrgScriptLanguage language;

	script = lrg_script_binding_get_script (run->binding);
	language = lrg_script_binding_get_language (run->binding);
	if (language == LRG_SCRIPT_LANGUAGE_NONE && script != NULL)
		language = lrg_scripting_manager_language_for_path (lrg_scripting_manager_get_default (), script);

	g_clear_object (&run->held.scripting);
	g_free (run->held.path);
	run->held.path = NULL;
	run->held.language = language;
	run->held.api = self;
	run->held.tool = FALSE;
	g_free (run->error);
	run->error = NULL;
	run->failed = FALSE;
	run->started = FALSE;
	g_set_object (&run->node, node);

	if (script == NULL || *script == '\0')
	{
		editor_run_fail (run, "Script binding has no script");
		return;
	}
	run->held.path = resolve_against_project (self, script);
	run->mtime = file_mtime (run->held.path);
	if (!g_file_test (run->held.path, G_FILE_TEST_IS_REGULAR))
	{
		g_autofree gchar *msg = g_strdup_printf ("No such script '%s'", script);

		editor_run_fail (run, msg);
		return;
	}
	run->held.scripting = context_open (self, language, node, &error);
	if (run->held.scripting == NULL)
	{
		editor_run_fail (run, (error != NULL) ? error->message : "Could not create a context");
		return;
	}
	{
		g_autofree gchar *dir = g_path_get_dirname (run->held.path);

		lrg_scripting_add_search_path (run->held.scripting, dir);
	}
	if (!lrg_scripting_load_file (run->held.scripting, run->held.path, &error))
	{
		prefix_location (run->held.scripting, &error);
		editor_run_fail (run, (error != NULL) ? error->message : "Load failed");
		return;
	}
	if (!call_hook (self, run, LRG_SCRIPT_HOOK_START, 0, NULL, &error))
	{
		editor_run_fail (run, (error != NULL) ? error->message : "lrg_script_start failed");
		return;
	}
	run->started = TRUE;
	g_value_init (&arg, G_TYPE_DOUBLE);
	g_value_set_double (&arg, (gdouble) delta);
	if (!call_hook (self, run, LRG_SCRIPT_HOOK_UPDATE, 1, &arg, &error))
		editor_run_fail (run, (error != NULL) ? error->message : "lrg_script_update failed");
	g_value_unset (&arg);
}

static gboolean
collect_binding (LrgNode  *node,
                 gpointer  user_data)
{
	GHashTable *seen = (GHashTable *) user_data;
	GPtrArray  *scripts;
	guint       i;

	scripts = lrg_node_get_scripts (node);
	if (scripts == NULL)
		return TRUE;
	for (i = 0; i < scripts->len; i++)
	{
		LrgScriptBinding *binding = (LrgScriptBinding *) g_ptr_array_index (scripts, i);

		if (!lrg_script_binding_get_enabled (binding))
			continue;
		if (!lrg_script_binding_get_run_in_editor (binding))
			continue;
		g_hash_table_insert (seen, binding, node);
	}
	return TRUE;
}

static void
editor_run_update (LrgEditorScriptApi *self,
                   EditorRun          *run,
                   LrgNode            *node,
                   gfloat              delta)
{
	const gchar      *script;
	g_autofree gchar *path = NULL;
	gint64            mtime;
	LrgScriptLanguage bound;
	GValue            arg = G_VALUE_INIT;
	g_autoptr(GError) error = NULL;
	gboolean          changed;

	script = lrg_script_binding_get_script (run->binding);
	path = resolve_against_project (self, (script != NULL) ? script : "");
	mtime = file_mtime (path);
	/* NONE is resolved from the path when the context is opened, so
	 * compare the same way or every tick looks like a language change. */
	bound = lrg_script_binding_get_language (run->binding);
	if (bound == LRG_SCRIPT_LANGUAGE_NONE && script != NULL && *script != '\0')
		bound = lrg_scripting_manager_language_for_path (lrg_scripting_manager_get_default (), script);
	changed = g_strcmp0 (path, run->held.path) != 0 || mtime != run->mtime ||
	          bound != run->held.language;
	if (run->failed && !changed)
		return;
	if (!run->started || changed || run->failed)
	{
		editor_run_detach (run);
		g_clear_object (&run->held.scripting);
		editor_run_start (self, run, node, delta);
		return;
	}
	g_set_object (&run->node, node);
	g_value_init (&arg, G_TYPE_DOUBLE);
	g_value_set_double (&arg, (gdouble) delta);
	if (!call_hook (self, run, LRG_SCRIPT_HOOK_UPDATE, 1, &arg, &error))
		editor_run_fail (run, (error != NULL) ? error->message : "lrg_script_update failed");
	g_value_unset (&arg);
}

static gint cmp_str (gconstpointer a, gconstpointer b);

void
lrg_editor_script_api_tick (LrgEditorScriptApi *self,
                            gfloat              delta)
{
	LrgLevel          *level;
	g_autoptr(GHashTable) seen = NULL;
	GHashTableIter     iter;
	gpointer           key;
	gpointer           value;
	gint               floor;

	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));

	if (self->tools_dirty)
		reload_tools_internal (self, TRUE);
	else if (self->project_dir != NULL && self->tools_stamp != NULL)
	{
		/* Compared inside reload; a same-second edit relies on the monitor. */
	}
	if (self->reloader != NULL && lrg_script_reloader_process_pending != NULL)
		lrg_script_reloader_process_pending (self->reloader);

	if (self->editor == NULL)
		return;
	level = lrg_editor_get_level (self->editor);
	if (level == NULL)
		return;

	seen = g_hash_table_new (g_direct_hash, g_direct_equal);
	lrg_node_foreach (lrg_level_get_root (level), collect_binding, seen);

	g_hash_table_iter_init (&iter, self->editor_runs);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		if (!g_hash_table_contains (seen, key))
			g_hash_table_iter_remove (&iter);
	}

	floor = self->undo_depth;
	lrg_editor_begin_group (self->editor, "Run in Editor");
	self->undo_depth++;

	g_hash_table_iter_init (&iter, seen);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		LrgScriptBinding *binding = (LrgScriptBinding *) key;
		LrgNode          *node = (LrgNode *) value;
		EditorRun        *run;

		run = (EditorRun *) g_hash_table_lookup (self->editor_runs, binding);
		if (run == NULL)
		{
			run = g_new0 (EditorRun, 1);
			run->api = self;
			run->binding = (LrgScriptBinding *) g_object_ref (binding);
			run->node = (LrgNode *) g_object_ref (node);
			run->held.api = self;
			g_hash_table_insert (self->editor_runs, binding, run);
			editor_run_start (self, run, node, delta);
		}
		else
			editor_run_update (self, run, node, delta);
	}

	while (self->undo_depth > floor)
	{
		lrg_editor_end_group (self->editor);
		self->undo_depth--;
	}

	/* Stamp polled after the walk so a script that rewrites a tool is
	 * picked up next frame, and so the first tick after set_project_dir
	 * does not reload what reload_tools just ran. */
	if (self->project_dir != NULL)
	{
		g_autofree gchar *stamp = NULL;
		g_autofree gchar *dir = NULL;
		GDir             *folder;
		const gchar      *name;
		GString          *built;

		dir = g_build_filename (self->project_dir, LRG_EDITOR_SCRIPTS_DIR, NULL);
		built = g_string_new ("");
		folder = g_dir_open (dir, 0, NULL);
		if (folder != NULL)
		{
			GPtrArray *names;

			names = g_ptr_array_new_with_free_func (g_free);
			while ((name = g_dir_read_name (folder)) != NULL)
				g_ptr_array_add (names, g_strdup (name));
			g_dir_close (folder);
			g_ptr_array_sort (names, cmp_str);
			{
				guint i;

				for (i = 0; i < names->len; i++)
				{
					g_autofree gchar *path = NULL;
					GStatBuf          st;
					const gchar      *base;

					base = (const gchar *) g_ptr_array_index (names, i);
					path = g_build_filename (dir, base, NULL);
					if (g_stat (path, &st) != 0 || !S_ISREG (st.st_mode))
						continue;
					g_string_append_printf (built, "%s:%ld:%ld;", base,
					                        (long) st.st_mtime, (long) st.st_size);
				}
			}
			g_ptr_array_unref (names);
		}
		stamp = g_string_free (built, FALSE);
		if (self->tools_stamp != NULL && g_strcmp0 (stamp, self->tools_stamp) != 0)
			self->tools_dirty = TRUE;
		if (self->tools_dirty)
			reload_tools_internal (self, TRUE);
	}
}

void
lrg_editor_script_api_stop_editor_scripts (LrgEditorScriptApi *self)
{
	gint floor;

	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));

	if (self->editor_runs == NULL || g_hash_table_size (self->editor_runs) == 0)
		return;
	if (self->editor != NULL)
	{
		floor = self->undo_depth;
		lrg_editor_begin_group (self->editor, "Run in Editor");
		self->undo_depth++;
		g_hash_table_remove_all (self->editor_runs);
		while (self->undo_depth > floor)
		{
			lrg_editor_end_group (self->editor);
			self->undo_depth--;
		}
	}
	else
		g_hash_table_remove_all (self->editor_runs);
}

guint
lrg_editor_script_api_get_n_editor_scripts (LrgEditorScriptApi *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), 0);

	return g_hash_table_size (self->editor_runs);
}

gboolean
lrg_editor_script_api_set_run_in_editor (LrgEditorScriptApi *self,
                                         LrgNode            *node,
                                         guint               index,
                                         gboolean            run_in_editor,
                                         GError            **error)
{
	GPtrArray        *scripts;
	LrgScriptBinding *binding;
	GValue            value = G_VALUE_INIT;
	gboolean          ok;
	gboolean          opened;
	gint              floor;

	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	scripts = lrg_node_get_scripts (node);
	if (scripts == NULL || index >= scripts->len)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "Node has no script binding %u", index);
		return FALSE;
	}
	binding = (LrgScriptBinding *) g_ptr_array_index (scripts, index);
	g_value_init (&value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&value, run_in_editor);
	opened = (self->undo_depth == 0);
	floor = self->undo_depth;
	if (opened)
	{
		lrg_editor_begin_group (self->editor, "Run in Editor");
		self->undo_depth++;
	}
	ok = lrg_editor_set_object_property (self->editor, G_OBJECT (binding), "run-in-editor",
	                                     &value, FALSE);
	g_value_unset (&value);
	if (opened)
	{
		while (self->undo_depth > floor)
		{
			lrg_editor_end_group (self->editor);
			self->undo_depth--;
		}
	}
	if (!ok)
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "Could not set run-in-editor");
	return ok;
}

/* --------------------------------------------------------------------------
 * Script-facing document operations
 * -------------------------------------------------------------------------- */

LrgNode *
lrg_editor_script_api_find_node (LrgEditorScriptApi *self,
                                 const gchar        *ref)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), NULL);

	return lrg_editor_find_node (self->editor, ref);
}

GPtrArray *
lrg_editor_script_api_find_nodes (LrgEditorScriptApi *self,
                                  const gchar        *query)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), NULL);

	/* find_nodes rejects NULL; an empty needle matches every named node. */
	return lrg_editor_find_nodes (self->editor, (query != NULL) ? query : "");
}

static gboolean
primitive_from_name (const gchar      *name,
                     LrgPrimitiveType *out)
{
	GEnumClass       *klass;
	GEnumValue       *value;
	g_autofree gchar *nick = NULL;
	gchar            *p;

	if (name == NULL || *name == '\0')
		return FALSE;
	nick = g_ascii_strdown (name, -1);
	for (p = nick; *p != '\0'; p++)
	{
		if (*p == '_')
			*p = '-';
	}
	if (g_str_has_prefix (nick, "primitive-"))
		memmove (nick, nick + strlen ("primitive-"), strlen (nick + strlen ("primitive-")) + 1);
	klass = (GEnumClass *) g_type_class_ref (LRG_TYPE_PRIMITIVE_TYPE);
	value = g_enum_get_value_by_nick (klass, nick);
	if (value == NULL)
		value = g_enum_get_value_by_name (klass, name);
	if (value != NULL)
		*out = (LrgPrimitiveType) value->value;
	g_type_class_unref (klass);
	return value != NULL;
}

LrgNode *
lrg_editor_script_api_create_node (LrgEditorScriptApi *self,
                                   const gchar        *name,
                                   const gchar        *primitive,
                                   LrgNode            *parent)
{
	LrgNode          *node;
	LrgPrimitiveType  kind = 0;
	LrgNodeVisual    *visual;

	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), NULL);
	g_return_val_if_fail (parent == NULL || LRG_IS_NODE (parent), NULL);

	if (primitive != NULL && *primitive != '\0' && !primitive_from_name (primitive, &kind))
		return NULL;
	node = lrg_node_new ((name != NULL && *name != '\0') ? name : "Node");
	if (primitive != NULL && *primitive != '\0')
	{
		visual = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
		lrg_node_visual_set_primitive (visual, kind);
		lrg_node_set_visual (node, visual);
		g_object_unref (visual);
	}
	/* add_node refs the node and selects it. The visual is already on it,
	 * so the add is the only undo command. */
	lrg_editor_add_node (self->editor, node, parent);
	g_object_unref (node);
	return node;
}

gboolean
lrg_editor_script_api_delete_node (LrgEditorScriptApi *self,
                                   LrgNode            *node)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	return lrg_editor_delete_node (self->editor, node);
}

gboolean
lrg_editor_script_api_reparent_node (LrgEditorScriptApi *self,
                                     LrgNode            *node,
                                     LrgNode            *parent)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (parent == NULL || LRG_IS_NODE (parent), FALSE);

	return lrg_editor_move_node (self->editor, node, parent, -1, FALSE);
}

void
lrg_editor_script_api_rename_node (LrgEditorScriptApi *self,
                                   LrgNode            *node,
                                   const gchar        *name)
{
	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));
	g_return_if_fail (LRG_IS_NODE (node));
	g_return_if_fail (name != NULL);

	lrg_editor_rename_node (self->editor, node, name);
}

gboolean
lrg_editor_script_api_set_value (LrgEditorScriptApi *self,
                                 GObject            *object,
                                 const gchar        *property,
                                 const GValue       *value)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), FALSE);
	g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
	g_return_val_if_fail (property != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	return lrg_editor_set_object_property (self->editor, object, property, value, FALSE);
}

static void
set_trs (LrgEditorScriptApi *self,
         LrgNode            *node,
         const gfloat       *location,
         const gfloat       *rotation,
         const gfloat       *scale)
{
	GrlVector3 *loc;
	GrlVector3 *rot;
	GrlVector3 *scl;
	gfloat      l[3];
	gfloat      r[3];
	gfloat      s[3];

	loc = lrg_node_get_location (node);
	rot = lrg_node_get_rotation (node);
	scl = lrg_node_get_scale (node);
	l[0] = (location != NULL) ? location[0] : loc->x;
	l[1] = (location != NULL) ? location[1] : loc->y;
	l[2] = (location != NULL) ? location[2] : loc->z;
	r[0] = (rotation != NULL) ? rotation[0] : rot->x;
	r[1] = (rotation != NULL) ? rotation[1] : rot->y;
	r[2] = (rotation != NULL) ? rotation[2] : rot->z;
	s[0] = (scale != NULL) ? scale[0] : scl->x;
	s[1] = (scale != NULL) ? scale[1] : scl->y;
	s[2] = (scale != NULL) ? scale[2] : scl->z;
	lrg_editor_transform_node (self->editor, node, l, r, s, FALSE);
}

void
lrg_editor_script_api_set_location (LrgEditorScriptApi *self,
                                    LrgNode            *node,
                                    gdouble             x,
                                    gdouble             y,
                                    gdouble             z)
{
	gfloat loc[3];

	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));
	g_return_if_fail (LRG_IS_NODE (node));

	loc[0] = (gfloat) x;
	loc[1] = (gfloat) y;
	loc[2] = (gfloat) z;
	set_trs (self, node, loc, NULL, NULL);
}

void
lrg_editor_script_api_set_rotation_degrees (LrgEditorScriptApi *self,
                                            LrgNode            *node,
                                            gdouble             x,
                                            gdouble             y,
                                            gdouble             z)
{
	gfloat rot[3];

	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));
	g_return_if_fail (LRG_IS_NODE (node));

	rot[0] = (gfloat) (x * LRG_SCRIPT_DEG2RAD);
	rot[1] = (gfloat) (y * LRG_SCRIPT_DEG2RAD);
	rot[2] = (gfloat) (z * LRG_SCRIPT_DEG2RAD);
	set_trs (self, node, NULL, rot, NULL);
}

void
lrg_editor_script_api_set_scale (LrgEditorScriptApi *self,
                                 LrgNode            *node,
                                 gdouble             x,
                                 gdouble             y,
                                 gdouble             z)
{
	gfloat scl[3];

	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));
	g_return_if_fail (LRG_IS_NODE (node));

	scl[0] = (gfloat) x;
	scl[1] = (gfloat) y;
	scl[2] = (gfloat) z;
	set_trs (self, node, NULL, NULL, scl);
}

GPtrArray *
lrg_editor_script_api_get_selection (LrgEditorScriptApi *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), NULL);

	return lrg_editor_get_selected_roots (self->editor);
}

void
lrg_editor_script_api_select (LrgEditorScriptApi *self,
                              LrgNode            *node,
                              gboolean            add)
{
	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));
	g_return_if_fail (LRG_IS_NODE (node));

	lrg_editor_select (self->editor, node, add);
}

void
lrg_editor_script_api_clear_selection (LrgEditorScriptApi *self)
{
	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));

	lrg_editor_select (self->editor, NULL, FALSE);
}

void
lrg_editor_script_api_begin_undo (LrgEditorScriptApi *self,
                                  const gchar        *label)
{
	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));

	lrg_editor_begin_group (self->editor, (label != NULL) ? label : "Script");
	self->undo_depth++;
}

void
lrg_editor_script_api_end_undo (LrgEditorScriptApi *self)
{
	gint floor;

	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));

	/* Leave the group the current run opened. */
	floor = 0;
	if (self->frames->len > 0)
		floor = frame_top (self)->floor + 1;
	if (self->undo_depth <= floor)
		return;
	lrg_editor_end_group (self->editor);
	self->undo_depth--;
}

void
lrg_editor_script_api_undo (LrgEditorScriptApi *self)
{
	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));

	/* lrg_editor_undo() rejects a call while a group is open. Inside a
	 * run the group stays open and the call is a no-op; the host undoes
	 * the whole run afterwards as one step. */
	if (self->undo_depth > 0)
		return;
	lrg_editor_undo (self->editor);
}

void
lrg_editor_script_api_redo (LrgEditorScriptApi *self)
{
	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));

	if (self->undo_depth > 0)
		return;
	lrg_editor_redo (self->editor);
}

/* --------------------------------------------------------------------------
 * Assets
 * -------------------------------------------------------------------------- */

static void
list_dir (const gchar *root,
          const gchar *dir,
          GPtrArray   *out)
{
	GDir        *folder;
	const gchar *name;

	folder = g_dir_open (dir, 0, NULL);
	if (folder == NULL)
		return;
	while ((name = g_dir_read_name (folder)) != NULL)
	{
		g_autofree gchar *path = NULL;
		GStatBuf          st;
		gsize             n;

		path = g_build_filename (dir, name, NULL);
		if (g_lstat (path, &st) != 0)
			continue;
		if (S_ISLNK (st.st_mode))
			continue;
		if (S_ISDIR (st.st_mode))
		{
			list_dir (root, path, out);
			continue;
		}
		if (!S_ISREG (st.st_mode))
			continue;
		n = strlen (root);
		if (strncmp (path, root, n) == 0 && path[n] == G_DIR_SEPARATOR)
			g_ptr_array_add (out, g_strdup (path + n + 1));
	}
	g_dir_close (folder);
}

static gint
cmp_str (gconstpointer a,
         gconstpointer b)
{
	const gchar *sa = *(const gchar * const *) a;
	const gchar *sb = *(const gchar * const *) b;

	return g_strcmp0 (sa, sb);
}

GStrv
lrg_editor_script_api_list_assets (LrgEditorScriptApi *self,
                                   const gchar        *pattern)
{
	GPtrArray            *paths;
	GPatternSpec         *spec;
	guint                 i;

	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), NULL);

	paths = g_ptr_array_new_with_free_func (g_free);
	if (self->project_dir != NULL)
		list_dir (self->project_dir, self->project_dir, paths);
	if (pattern != NULL && *pattern != '\0')
	{
		spec = g_pattern_spec_new (pattern);
		for (i = paths->len; i > 0; i--)
		{
			const gchar *rel = (const gchar *) g_ptr_array_index (paths, i - 1);

			if (!g_pattern_spec_match_string (spec, rel))
				g_ptr_array_remove_index (paths, i - 1);
		}
		g_pattern_spec_free (spec);
	}
	g_ptr_array_sort (paths, cmp_str);
	g_ptr_array_add (paths, NULL);
	return (GStrv) g_ptr_array_free (paths, FALSE);
}

gchar *
lrg_editor_script_api_import_asset (LrgEditorScriptApi *self,
                                    const gchar        *source,
                                    const gchar        *dest,
                                    GError            **error)
{
	g_autofree gchar *target = NULL;
	g_autofree gchar *rel = NULL;
	g_autofree gchar *base = NULL;
	g_autofree gchar *parent = NULL;
	g_autoptr(GFile)  src = NULL;
	g_autoptr(GFile)  dst = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), NULL);
	g_return_val_if_fail (source != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (dest == NULL || *dest == '\0')
	{
		base = g_path_get_basename (source);
		rel = g_strdup (base);
	}
	else
		rel = g_strdup (dest);
	target = project_child (self, rel, error);
	if (target == NULL)
		return NULL;
	if (g_file_test (target, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "Asset '%s' already exists", rel);
		return NULL;
	}
	parent = g_path_get_dirname (target);
	if (g_mkdir_with_parents (parent, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not create '%s'", parent);
		return NULL;
	}
	src = g_file_new_for_path (source);
	dst = g_file_new_for_path (target);
	if (!g_file_copy (src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, error))
		return NULL;
	return g_strdup (rel);
}

gboolean
lrg_editor_script_api_create_asset (LrgEditorScriptApi *self,
                                    const gchar        *path,
                                    const gchar        *contents,
                                    GError            **error)
{
	g_autofree gchar *target = NULL;
	g_autofree gchar *parent = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (contents != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	target = project_child (self, path, error);
	if (target == NULL)
		return FALSE;
	if (g_file_test (target, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "Asset '%s' already exists", path);
		return FALSE;
	}
	parent = g_path_get_dirname (target);
	if (g_mkdir_with_parents (parent, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not create '%s'", parent);
		return FALSE;
	}
	return g_file_set_contents (target, contents, -1, error);
}

void
lrg_editor_script_api_log (LrgEditorScriptApi *self,
                           const gchar        *message)
{
	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));

	emit_line (self, G_LOG_LEVEL_MESSAGE, (message != NULL) ? message : "");
}

void
lrg_editor_script_api_warn (LrgEditorScriptApi *self,
                            const gchar        *message)
{
	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));

	emit_line (self, G_LOG_LEVEL_WARNING, (message != NULL) ? message : "");
}

static gboolean
server_has_method (LrgEditorServer *server,
                   const gchar     *name)
{
	GList   *methods;
	GList   *l;
	gboolean found;

	methods = lrg_editor_server_list_methods (server);
	found = FALSE;
	for (l = methods; l != NULL; l = l->next)
	{
		if (g_strcmp0 ((const gchar *) l->data, name) == 0)
			found = TRUE;
	}
	g_list_free (methods);
	return found;
}

void
lrg_editor_script_api_toast (LrgEditorScriptApi *self,
                             const gchar        *message,
                             const gchar        *level)
{
	LrgEditorServer  *server;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *params = NULL;
	g_autofree gchar *ignored = NULL;
	const gchar      *use;

	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));

	use = (level != NULL && *level != '\0') ? level : "info";
	server = lrg_editor_script_api_get_server (self);
	if (server != NULL && server_has_method (server, "ui.notify"))
	{
		g_autoptr(JsonObject) obj = json_object_new ();
		g_autoptr(JsonNode)   node = NULL;
		g_autoptr(JsonGenerator) gen = json_generator_new ();

		json_object_set_string_member (obj, "message", (message != NULL) ? message : "");
		json_object_set_string_member (obj, "level", use);
		node = json_node_new (JSON_NODE_OBJECT);
		json_node_set_object (node, obj);
		json_generator_set_root (gen, node);
		params = json_generator_to_data (gen, NULL);
		ignored = lrg_editor_script_api_call (self, "ui.notify", params, &error);
		if (ignored != NULL)
			return;
	}
	emit_line (self, G_LOG_LEVEL_MESSAGE, (message != NULL) ? message : "");
}

gchar *
lrg_editor_script_api_call (LrgEditorScriptApi *self,
                            const gchar        *method,
                            const gchar        *params_json,
                            GError            **error)
{
	LrgEditorServer     *server;
	g_autoptr(JsonObject) req = NULL;
	g_autoptr(JsonGenerator) gen = NULL;
	g_autofree gchar    *line = NULL;
	g_autofree gchar    *response = NULL;
	g_autoptr(JsonParser) parser = NULL;
	JsonNode            *root;
	JsonObject          *obj;
	JsonNode            *result;

	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), NULL);
	g_return_val_if_fail (method != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	server = lrg_editor_script_api_get_server (self);
	if (server == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "No editor server");
		return NULL;
	}
	req = json_object_new ();
	json_object_set_int_member (req, "id", 1);
	json_object_set_string_member (req, "method", method);
	if (params_json != NULL && *params_json != '\0')
	{
		g_autoptr(JsonParser) pp = json_parser_new ();
		JsonNode             *pn;

		if (!json_parser_load_from_data (pp, params_json, -1, error))
			return NULL;
		pn = json_parser_get_root (pp);
		if (pn == NULL || !JSON_NODE_HOLDS_OBJECT (pn))
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR,
			                     LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			                     "params_json must be a JSON object");
			return NULL;
		}
		json_object_set_member (req, "params", json_node_copy (pn));
	}
	else
	{
		JsonObject *empty = json_object_new ();
		JsonNode   *node = json_node_new (JSON_NODE_OBJECT);

		json_node_take_object (node, empty);
		json_object_set_member (req, "params", node);
	}
	gen = json_generator_new ();
	{
		JsonNode *n = json_node_new (JSON_NODE_OBJECT);

		json_node_set_object (n, req);
		json_generator_set_root (gen, n);
		json_node_unref (n);
	}
	line = json_generator_to_data (gen, NULL);
	response = lrg_editor_server_dispatch (server, line);
	if (response == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "IPC dispatch returned nothing");
		return NULL;
	}
	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser, response, -1, error))
		return NULL;
	root = json_parser_get_root (parser);
	if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "IPC response is not an object");
		return NULL;
	}
	obj = json_node_get_object (root);
	if (json_object_has_member (obj, "error"))
	{
		JsonObject  *err = json_object_get_object_member (obj, "error");
		const gchar *msg = "IPC call failed";

		if (err != NULL && json_object_has_member (err, "message"))
			msg = json_object_get_string_member (err, "message");
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, msg);
		return NULL;
	}
	if (!json_object_has_member (obj, "result"))
		return g_strdup ("null");
	result = json_object_get_member (obj, "result");
	{
		g_autoptr(JsonGenerator) rg = json_generator_new ();

		json_generator_set_root (rg, result);
		return json_generator_to_data (rg, NULL);
	}
}

/* --------------------------------------------------------------------------
 * editor_scripts/ reload
 * -------------------------------------------------------------------------- */

static void
on_tools_changed (GFileMonitor       *monitor,
                  GFile              *file,
                  GFile              *other,
                  GFileMonitorEvent   event,
                  gpointer            user_data)
{
	LrgEditorScriptApi *self = (LrgEditorScriptApi *) user_data;

	(void) monitor;
	(void) file;
	(void) other;
	switch (event)
	{
	case G_FILE_MONITOR_EVENT_CHANGED:
	case G_FILE_MONITOR_EVENT_DELETED:
	case G_FILE_MONITOR_EVENT_CREATED:
	case G_FILE_MONITOR_EVENT_MOVED:
	case G_FILE_MONITOR_EVENT_RENAMED:
	case G_FILE_MONITOR_EVENT_MOVED_IN:
	case G_FILE_MONITOR_EVENT_MOVED_OUT:
		self->tools_dirty = TRUE;
		break;
	default:
		break;
	}
}

static void
watch_tools_dir (LrgEditorScriptApi *self)
{
	g_autofree gchar *dir = NULL;
	g_autoptr(GFile)  file = NULL;
	g_autoptr(GError) error = NULL;
	const gchar      *watch;

	if (self->project_dir == NULL)
		return;
	dir = g_build_filename (self->project_dir, LRG_EDITOR_SCRIPTS_DIR, NULL);
	watch = g_file_test (dir, G_FILE_TEST_IS_DIR) ? dir : self->project_dir;
	if (self->reloader != NULL && self->watched_dir != NULL &&
	    lrg_script_reloader_unwatch_path != NULL &&
	    g_strcmp0 (self->watched_dir, watch) != 0)
		lrg_script_reloader_unwatch_path (self->reloader, self->watched_dir);
	if (g_strcmp0 (self->watched_dir, watch) == 0 && self->monitor != NULL)
		return;
	g_clear_object (&self->monitor);
	g_free (self->watched_dir);
	self->watched_dir = g_strdup (watch);
	file = g_file_new_for_path (watch);
	self->monitor = g_file_monitor_directory (file, G_FILE_MONITOR_WATCH_MOVES, NULL, &error);
	if (self->monitor != NULL)
		g_signal_connect (self->monitor, "changed", G_CALLBACK (on_tools_changed), self);
	if (self->reloader != NULL && lrg_script_reloader_watch_path != NULL)
		lrg_script_reloader_watch_path (self->reloader, watch, NULL);
}

static gint
cmp_basename (gconstpointer a,
              gconstpointer b)
{
	const gchar      *sa = *(const gchar * const *) a;
	const gchar      *sb = *(const gchar * const *) b;
	g_autofree gchar *ba = g_path_get_basename (sa);
	g_autofree gchar *bb = g_path_get_basename (sb);
	gint              r;

	r = g_strcmp0 (ba, bb);
	if (r == 0)
		r = g_strcmp0 (sa, sb);
	return r;
}

static gchar *
current_stamp (LrgEditorScriptApi *self)
{
	g_autofree gchar *dir = NULL;
	GDir             *folder;
	const gchar      *name;
	GPtrArray        *names;
	GString          *built;
	guint             i;

	if (self->project_dir == NULL)
		return g_strdup ("");
	dir = g_build_filename (self->project_dir, LRG_EDITOR_SCRIPTS_DIR, NULL);
	names = g_ptr_array_new_with_free_func (g_free);
	folder = g_dir_open (dir, 0, NULL);
	if (folder != NULL)
	{
		while ((name = g_dir_read_name (folder)) != NULL)
			g_ptr_array_add (names, g_strdup (name));
		g_dir_close (folder);
	}
	g_ptr_array_sort (names, cmp_str);
	built = g_string_new ("");
	for (i = 0; i < names->len; i++)
	{
		const gchar      *base = (const gchar *) g_ptr_array_index (names, i);
		g_autofree gchar *path = g_build_filename (dir, base, NULL);
		GStatBuf          st;

		if (g_stat (path, &st) != 0 || !S_ISREG (st.st_mode))
			continue;
		g_string_append_printf (built, "%s:%ld:%ld;", base, (long) st.st_mtime, (long) st.st_size);
	}
	g_ptr_array_unref (names);
	return g_string_free (built, FALSE);
}

static guint
reload_tools_internal (LrgEditorScriptApi *self,
                       gboolean            announce)
{
	g_autofree gchar *dir = NULL;
	GDir             *folder;
	const gchar      *name;
	GPtrArray        *paths;
	GHashTableIter    iter;
	gpointer          key;
	HeldContext      *held;
	guint             i;
	guint             loaded;
	LrgScriptingManager *manager;

	(void) announce;
	g_ptr_array_set_size (self->failures, 0);
	if (self->project_dir == NULL)
	{
		g_free (self->tools_stamp);
		self->tools_stamp = g_strdup ("");
		self->tools_dirty = FALSE;
		return 0;
	}
	dir = g_build_filename (self->project_dir, LRG_EDITOR_SCRIPTS_DIR, NULL);
	paths = g_ptr_array_new_with_free_func (g_free);
	folder = g_dir_open (dir, 0, NULL);
	manager = lrg_scripting_manager_get_default ();
	if (folder != NULL)
	{
		while ((name = g_dir_read_name (folder)) != NULL)
		{
			g_autofree gchar *path = g_build_filename (dir, name, NULL);
			LrgScriptLanguage language;

			if (g_file_test (path, G_FILE_TEST_IS_DIR))
				continue;
			language = lrg_scripting_manager_language_for_path (manager, path);
			if (language != LRG_SCRIPT_LANGUAGE_LUA && language != LRG_SCRIPT_LANGUAGE_PYTHON &&
			    language != LRG_SCRIPT_LANGUAGE_GJS)
				continue;
			g_ptr_array_add (paths, g_steal_pointer (&path));
		}
		g_dir_close (folder);
	}
	g_ptr_array_sort (paths, cmp_basename);

	g_hash_table_iter_init (&iter, self->files);
	while (g_hash_table_iter_next (&iter, &key, (gpointer *) &held))
	{
		if (held->tool || path_in_dir (held->path, dir))
			g_hash_table_iter_remove (&iter);
	}
	rebuild_menu (self);

	loaded = 0;
	for (i = 0; i < paths->len; i++)
	{
		const gchar      *path = (const gchar *) g_ptr_array_index (paths, i);
		g_autoptr(GError) error = NULL;

		if (lrg_editor_script_api_run_file (self, path, LRG_SCRIPT_LANGUAGE_NONE, NULL, &error))
			loaded++;
		else
			remember_failure (self, path, (error != NULL) ? error->message : "failed");
	}
	g_ptr_array_unref (paths);
	g_free (self->tools_stamp);
	self->tools_stamp = current_stamp (self);
	self->tools_dirty = FALSE;
	watch_tools_dir (self);
	for (i = 0; i < self->tools->len; i++)
		tool_sync_action_full (self, (Tool *) g_ptr_array_index (self->tools, i));
	return loaded;
}

guint
lrg_editor_script_api_reload_tools (LrgEditorScriptApi *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCRIPT_API (self), 0);

	return reload_tools_internal (self, TRUE);
}

/* --------------------------------------------------------------------------
 * IPC
 * -------------------------------------------------------------------------- */

static JsonNode *
json_null_node (void)
{
	JsonNode *n = json_node_new (JSON_NODE_NULL);

	return n;
}

static void
json_set_string_or_null (JsonObject  *obj,
                         const gchar *key,
                         const gchar *value)
{
	if (value == NULL)
		json_object_set_member (obj, key, json_null_node ());
	else
		json_object_set_string_member (obj, key, value);
}

static const gchar *
json_member_string (JsonObject  *obj,
                    const gchar *key)
{
	JsonNode *node;

	if (obj == NULL || !json_object_has_member (obj, key))
		return NULL;
	node = json_object_get_member (obj, key);
	if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
		return NULL;
	if (json_node_get_value_type (node) != G_TYPE_STRING)
		return NULL;
	return json_node_get_string (node);
}

static LrgScriptLanguage
ipc_language (const gchar       *name,
              LrgScriptLanguage  fallback,
              gboolean          *ok)
{
	LrgScriptLanguage language;

	*ok = TRUE;
	if (name == NULL || *name == '\0')
		return fallback;
	/* Alias kept for the earlier draft id. Not accepted by language_from_name. */
	if (g_ascii_strcasecmp (name, "pygobject") == 0)
		return LRG_SCRIPT_LANGUAGE_PYTHON;
	language = lrg_editor_script_api_language_from_name (name);
	if (language == LRG_SCRIPT_LANGUAGE_NONE)
		*ok = FALSE;
	return language;
}

static void
capture_push (LrgEditorScriptApi *self)
{
	g_ptr_array_add (self->captures, g_string_new (""));
}

/* Rewrites a non-server error in place. g_set_error refuses a set GError. */
static void
server_error_remap (GError **error,
                    gint     code)
{
	gchar *message;

	if (error == NULL || *error == NULL)
		return;
	if ((*error)->domain == LRG_EDITOR_SERVER_ERROR)
		return;
	message = g_strdup ((*error)->message);
	g_clear_error (error);
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, message);
	g_free (message);
}

static gchar *
capture_pop (LrgEditorScriptApi *self)
{
	GString *s;

	if (self->captures->len == 0)
		return g_strdup ("");
	s = (GString *) g_ptr_array_steal_index (self->captures, self->captures->len - 1);
	return g_string_free (s, FALSE);
}

static JsonNode *
ipc_script_run (LrgEditorServer *server,
                JsonObject      *params,
                gpointer         user_data,
                GError         **error)
{
	LrgEditorScriptApi *self = (LrgEditorScriptApi *) user_data;
	const gchar        *code;
	const gchar        *path;
	const gchar        *label;
	gboolean            lang_ok;
	LrgScriptLanguage   language;
	g_autofree gchar   *result = NULL;
	g_autofree gchar   *output = NULL;
	gboolean            ok;
	JsonObject         *obj;

	(void) server;
	code = json_member_string (params, "code");
	path = json_member_string (params, "path");
	label = json_member_string (params, "label");
	if ((code == NULL) == (path == NULL))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "script.run takes exactly one of code or path");
		return NULL;
	}
	language = ipc_language (json_member_string (params, "language"),
	                         (path != NULL) ? LRG_SCRIPT_LANGUAGE_NONE : LRG_SCRIPT_LANGUAGE_LUA,
	                         &lang_ok);
	if (!lang_ok)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Unknown language");
		return NULL;
	}
	if (language != LRG_SCRIPT_LANGUAGE_NONE &&
	    !lrg_editor_script_api_language_is_supported (language))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Language is not available in this build");
		return NULL;
	}
	capture_push (self);
	if (path != NULL)
		ok = lrg_editor_script_api_run_file (self, path, language, &result, error);
	else
		ok = lrg_editor_script_api_run_string (self, language, code, label, &result, error);
	output = capture_pop (self);
	if (!ok)
	{
		gint code_id = LRG_EDITOR_SERVER_ERROR_FAILED;

		if (error != NULL && *error != NULL &&
		    g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			code_id = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
		else if (error != NULL && *error != NULL &&
		         g_error_matches (*error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_UNSUPPORTED))
			code_id = LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS;
		server_error_remap (error, code_id);
		return NULL;
	}
	if (path != NULL && language == LRG_SCRIPT_LANGUAGE_NONE)
		language = lrg_scripting_manager_language_for_path (lrg_scripting_manager_get_default (), path);
	obj = json_object_new ();
	json_set_string_or_null (obj, "result", result);
	json_object_set_string_member (obj, "output", (output != NULL) ? output : "");
	json_object_set_string_member (obj, "language", language_id (language));
	return json_node_init_object (json_node_new (JSON_NODE_OBJECT), obj);
}

static JsonNode *
ipc_script_languages (LrgEditorServer *server,
                      JsonObject      *params,
                      gpointer         user_data,
                      GError         **error)
{
	static const LrgScriptLanguage langs[] = {
		LRG_SCRIPT_LANGUAGE_LUA, LRG_SCRIPT_LANGUAGE_PYTHON, LRG_SCRIPT_LANGUAGE_GJS
	};
	JsonArray *arr;
	guint      i;

	(void) server;
	(void) params;
	(void) user_data;
	(void) error;

	arr = json_array_new ();
	for (i = 0; i < G_N_ELEMENTS (langs); i++)
	{
		JsonObject *obj = json_object_new ();

		json_object_set_string_member (obj, "id", language_id (langs[i]));
		json_object_set_string_member (obj, "name",
		                               lrg_scripting_manager_get_display_name (
		                                   lrg_scripting_manager_get_default (), langs[i]));
		json_object_set_boolean_member (obj, "available",
		                                lrg_editor_script_api_language_is_supported (langs[i]));
		json_array_add_object_element (arr, obj);
	}
	return json_node_init_array (json_node_new (JSON_NODE_ARRAY), arr);
}

static JsonNode *
ipc_script_tools (LrgEditorServer *server,
                  JsonObject      *params,
                  gpointer         user_data,
                  GError         **error)
{
	LrgEditorScriptApi *self = (LrgEditorScriptApi *) user_data;
	JsonArray          *arr;
	guint               i;

	(void) server;
	(void) params;
	(void) error;

	arr = json_array_new ();
	for (i = 0; i < self->tools->len; i++)
	{
		Tool       *tool = (Tool *) g_ptr_array_index (self->tools, i);
		JsonObject *obj;
		const gchar *slash;
		gchar       *menu;

		if (!tool->active)
			continue;
		obj = json_object_new ();
		json_object_set_string_member (obj, "id", tool->id);
		slash = strrchr (tool->label, '/');
		if (slash != NULL)
		{
			menu = g_strndup (tool->label, (gsize) (slash - tool->label));
			json_object_set_string_member (obj, "label", slash + 1);
			json_object_set_string_member (obj, "menu", menu);
			g_free (menu);
		}
		else
		{
			json_object_set_string_member (obj, "label", tool->label);
			json_object_set_member (obj, "menu", json_null_node ());
		}
		json_set_string_or_null (obj, "script", (tool->owner != NULL) ? tool->owner->path : NULL);
		json_object_set_string_member (obj, "language",
		                               language_id ((tool->owner != NULL) ? tool->owner->language
		                                                                  : LRG_SCRIPT_LANGUAGE_NONE));
		json_object_set_string_member (obj, "function", tool->function);
		json_set_string_or_null (obj, "shortcut", tool->shortcut);
		json_array_add_object_element (arr, obj);
	}
	return json_node_init_array (json_node_new (JSON_NODE_ARRAY), arr);
}

static JsonNode *
ipc_script_run_tool (LrgEditorServer *server,
                     JsonObject      *params,
                     gpointer         user_data,
                     GError         **error)
{
	LrgEditorScriptApi *self = (LrgEditorScriptApi *) user_data;
	const gchar        *id;
	JsonNode           *node;

	(void) server;
	id = json_member_string (params, "id");
	if (id == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "script.run_tool requires id");
		return NULL;
	}
	if (!lrg_editor_script_api_run_tool (self, id, error))
	{
		server_error_remap (error, LRG_EDITOR_SERVER_ERROR_FAILED);
		return NULL;
	}
	node = json_node_new (JSON_NODE_VALUE);
	json_node_set_boolean (node, TRUE);
	return node;
}

static JsonNode *
ipc_script_reload (LrgEditorServer *server,
                   JsonObject      *params,
                   gpointer         user_data,
                   GError         **error)
{
	LrgEditorScriptApi *self = (LrgEditorScriptApi *) user_data;
	JsonObject         *obj;
	JsonArray          *failed;
	guint               loaded;
	guint               i;
	g_autofree gchar   *dir = NULL;

	(void) server;
	(void) params;
	(void) error;

	loaded = lrg_editor_script_api_reload_tools (self);
	obj = json_object_new ();
	if (self->project_dir != NULL)
		dir = g_build_filename (self->project_dir, LRG_EDITOR_SCRIPTS_DIR, NULL);
	json_set_string_or_null (obj, "dir", dir);
	json_object_set_int_member (obj, "loaded", (gint64) loaded);
	failed = json_array_new ();
	for (i = 0; i < self->failures->len; i++)
	{
		ScriptFailure *fail = (ScriptFailure *) g_ptr_array_index (self->failures, i);
		JsonObject    *one = json_object_new ();

		json_object_set_string_member (one, "path", fail->path);
		json_object_set_string_member (one, "message", fail->message);
		json_array_add_object_element (failed, one);
	}
	json_object_set_array_member (obj, "failed", failed);
	return json_node_init_object (json_node_new (JSON_NODE_OBJECT), obj);
}

static JsonNode *
ipc_script_run_in_editor (LrgEditorServer *server,
                          JsonObject      *params,
                          gpointer         user_data,
                          GError         **error)
{
	LrgEditorScriptApi *self = (LrgEditorScriptApi *) user_data;
	const gchar        *ref;
	LrgNode            *node;
	guint               index;
	JsonNode           *enabled_node;
	JsonNode           *out;
	gboolean            enabled;

	(void) server;
	ref = json_member_string (params, "node");
	if (ref == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "script.run_in_editor requires node");
		return NULL;
	}
	if (params == NULL || !json_object_has_member (params, "enabled"))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "script.run_in_editor requires enabled");
		return NULL;
	}
	enabled_node = json_object_get_member (params, "enabled");
	if (enabled_node == NULL || json_node_get_value_type (enabled_node) != G_TYPE_BOOLEAN)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "enabled must be a boolean");
		return NULL;
	}
	enabled = json_node_get_boolean (enabled_node);
	index = 0;
	if (json_object_has_member (params, "index"))
	{
		JsonNode *n = json_object_get_member (params, "index");
		gint64    v;

		if (n == NULL || json_node_get_value_type (n) != G_TYPE_INT64)
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR,
			                     LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			                     "index must be an integer");
			return NULL;
		}
		v = json_node_get_int (n);
		if (v < 0)
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR,
			                     LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			                     "index must be >= 0");
			return NULL;
		}
		index = (guint) v;
	}
	node = lrg_editor_find_node (self->editor, ref);
	if (node == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "No node '%s'", ref);
		return NULL;
	}
	if (!lrg_editor_script_api_set_run_in_editor (self, node, index, enabled, error))
		return NULL;
	out = json_node_new (JSON_NODE_VALUE);
	json_node_set_boolean (out, TRUE);
	return out;
}

static JsonNode *
ipc_script_editor_running (LrgEditorServer *server,
                           JsonObject      *params,
                           gpointer         user_data,
                           GError         **error)
{
	LrgEditorScriptApi *self = (LrgEditorScriptApi *) user_data;
	JsonArray          *arr;
	GHashTableIter      iter;
	gpointer            value;

	(void) server;
	(void) params;
	(void) error;

	arr = json_array_new ();
	g_hash_table_iter_init (&iter, self->editor_runs);
	while (g_hash_table_iter_next (&iter, NULL, &value))
	{
		EditorRun *run = (EditorRun *) value;
		JsonObject *obj = json_object_new ();

		json_object_set_string_member (obj, "node",
		                               (run->node != NULL) ? lrg_node_get_guid (run->node) : "");
		json_set_string_or_null (obj, "script", lrg_script_binding_get_script (run->binding));
		json_object_set_string_member (obj, "language", language_id (run->held.language));
		json_set_string_or_null (obj, "error", run->error);
		json_array_add_object_element (arr, obj);
	}
	return json_node_init_array (json_node_new (JSON_NODE_ARRAY), arr);
}

void
lrg_editor_script_api_register_methods (LrgEditorScriptApi *self,
                                        LrgEditorServer    *server)
{
	g_return_if_fail (LRG_IS_EDITOR_SCRIPT_API (self));
	g_return_if_fail (LRG_IS_EDITOR_SERVER (server));

	lrg_editor_server_register_method (server, "script.run", ipc_script_run, self, NULL);
	lrg_editor_server_register_method (server, "script.languages", ipc_script_languages, self, NULL);
	lrg_editor_server_register_method (server, "script.tools", ipc_script_tools, self, NULL);
	lrg_editor_server_register_method (server, "script.run_tool", ipc_script_run_tool, self, NULL);
	lrg_editor_server_register_method (server, "script.reload", ipc_script_reload, self, NULL);
	lrg_editor_server_register_method (server, "script.run_in_editor", ipc_script_run_in_editor, self, NULL);
	lrg_editor_server_register_method (server, "script.editor_running", ipc_script_editor_running, self, NULL);
}

/* --------------------------------------------------------------------------
 * LrgScriptable
 *
 * The bridges pass arguments already converted. A list-shaped C return
 * goes back as a GStrv (guids or strings). Lua turns that into nil and
 * the Python bridge warns if a test calls it; GI callers use the C
 * functions, which return the real arrays.
 * -------------------------------------------------------------------------- */

static const gchar *
arg_string (const GValue *value)
{
	if (value == NULL || !G_VALUE_HOLDS_STRING (value))
		return NULL;
	return g_value_get_string (value);
}

static GObject *
arg_object (const GValue *value)
{
	if (value == NULL || !G_VALUE_HOLDS_OBJECT (value))
		return NULL;
	return g_value_get_object (value);
}

static gboolean
arg_bool (const GValue *value,
          gboolean      fallback)
{
	if (value == NULL)
		return fallback;
	if (G_VALUE_HOLDS_BOOLEAN (value))
		return g_value_get_boolean (value);
	if (G_VALUE_HOLDS_INT64 (value))
		return g_value_get_int64 (value) != 0;
	if (G_VALUE_HOLDS_INT (value))
		return g_value_get_int (value) != 0;
	if (G_VALUE_HOLDS_LONG (value))
		return g_value_get_long (value) != 0;
	return fallback;
}

static gboolean
arg_double (const GValue *value,
            gdouble      *out)
{
	if (value == NULL)
		return FALSE;
	if (G_VALUE_HOLDS_DOUBLE (value))
	{
		*out = g_value_get_double (value);
		return TRUE;
	}
	if (G_VALUE_HOLDS_FLOAT (value))
	{
		*out = (gdouble) g_value_get_float (value);
		return TRUE;
	}
	if (G_VALUE_HOLDS_INT64 (value))
	{
		*out = (gdouble) g_value_get_int64 (value);
		return TRUE;
	}
	if (G_VALUE_HOLDS_INT (value))
	{
		*out = (gdouble) g_value_get_int (value);
		return TRUE;
	}
	if (G_VALUE_HOLDS_LONG (value))
	{
		*out = (gdouble) g_value_get_long (value);
		return TRUE;
	}
	return FALSE;
}

static gboolean
arg_uint (const GValue *value,
          guint        *out)
{
	if (G_VALUE_HOLDS_INT64 (value))
	{
		gint64 n = g_value_get_int64 (value);

		if (n < 0)
			return FALSE;
		*out = (guint) n;
		return TRUE;
	}
	if (G_VALUE_HOLDS_INT (value))
	{
		gint n = g_value_get_int (value);

		if (n < 0)
			return FALSE;
		*out = (guint) n;
		return TRUE;
	}
	if (G_VALUE_HOLDS_UINT64 (value))
	{
		*out = (guint) g_value_get_uint64 (value);
		return TRUE;
	}
	if (G_VALUE_HOLDS_DOUBLE (value))
	{
		gdouble n = g_value_get_double (value);

		if (n < 0.0)
			return FALSE;
		*out = (guint) n;
		return TRUE;
	}
	return FALSE;
}

static gboolean
ret_bool (GValue   *return_value,
          gboolean  v)
{
	g_value_init (return_value, G_TYPE_BOOLEAN);
	g_value_set_boolean (return_value, v);
	return TRUE;
}

static gboolean
ret_node (GValue  *return_value,
          LrgNode *node)
{
	if (node == NULL)
		return TRUE;
	g_value_init (return_value, LRG_TYPE_NODE);
	g_value_set_object (return_value, (GObject *) node);
	return TRUE;
}

static gboolean
ret_strv (GValue *return_value,
          GStrv   strv)
{
	g_value_init (return_value, G_TYPE_STRV);
	g_value_take_boxed (return_value, strv);
	return TRUE;
}

static gboolean
type_error (GError     **error,
            const gchar *what)
{
	g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPE, "%s", what);
	return FALSE;
}

static LrgNode *
arg_node (const GValue *value,
          gboolean      nullable,
          GError      **error)
{
	GObject *obj;

	obj = arg_object (value);
	if (obj == NULL)
	{
		if (nullable && (value == NULL || G_VALUE_HOLDS_POINTER (value)))
			return NULL;
		g_set_error_literal (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPE,
		                     "Expected a node");
		return NULL;
	}
	if (!LRG_IS_NODE (obj))
	{
		g_set_error_literal (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPE,
		                     "Expected a node");
		return NULL;
	}
	return LRG_NODE (obj);
}

static gboolean
sm_find_node (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	LrgNode *node;

	if (n < 1)
		return type_error (error, "find_node expects a ref");
	node = lrg_editor_script_api_find_node (LRG_EDITOR_SCRIPT_API (obj), arg_string (&args[0]));
	return ret_node (ret, node);
}

static GStrv
nodes_to_guids (GPtrArray *nodes)
{
	GPtrArray *ids;
	guint      i;

	ids = g_ptr_array_new_with_free_func (g_free);
	if (nodes != NULL)
	{
		for (i = 0; i < nodes->len; i++)
		{
			LrgNode *node = (LrgNode *) g_ptr_array_index (nodes, i);

			g_ptr_array_add (ids, g_strdup (lrg_node_get_guid (node)));
		}
		g_ptr_array_unref (nodes);
	}
	g_ptr_array_add (ids, NULL);
	return (GStrv) g_ptr_array_free (ids, FALSE);
}

static gboolean
sm_find_nodes (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	const gchar *query = NULL;

	(void) error;
	if (n > 0)
		query = arg_string (&args[0]);
	return ret_strv (ret, nodes_to_guids (lrg_editor_script_api_find_nodes (LRG_EDITOR_SCRIPT_API (obj), query)));
}

static gboolean
sm_create_node (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	const gchar *name = NULL;
	const gchar *prim = NULL;
	LrgNode     *parent = NULL;
	LrgNode     *node;

	if (n > 0)
		name = arg_string (&args[0]);
	if (n > 1)
		prim = arg_string (&args[1]);
	if (n > 2)
	{
		parent = arg_node (&args[2], TRUE, error);
		if (parent == NULL && error != NULL && *error != NULL)
			return FALSE;
	}
	node = lrg_editor_script_api_create_node (LRG_EDITOR_SCRIPT_API (obj), name, prim, parent);
	return ret_node (ret, node);
}

static gboolean
sm_delete_node (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	LrgNode *node;

	if (n < 1)
		return type_error (error, "delete_node expects a node");
	node = arg_node (&args[0], FALSE, error);
	if (node == NULL)
		return FALSE;
	return ret_bool (ret, lrg_editor_script_api_delete_node (LRG_EDITOR_SCRIPT_API (obj), node));
}

static gboolean
sm_reparent_node (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	LrgNode *node;
	LrgNode *parent = NULL;

	if (n < 1)
		return type_error (error, "reparent_node expects a node");
	node = arg_node (&args[0], FALSE, error);
	if (node == NULL)
		return FALSE;
	if (n > 1)
	{
		parent = arg_node (&args[1], TRUE, error);
		if (parent == NULL && error != NULL && *error != NULL)
			return FALSE;
	}
	return ret_bool (ret, lrg_editor_script_api_reparent_node (LRG_EDITOR_SCRIPT_API (obj), node, parent));
}

static gboolean
sm_rename_node (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	LrgNode     *node;
	const gchar *name;

	(void) ret;
	if (n < 2)
		return type_error (error, "rename_node expects a node and a name");
	node = arg_node (&args[0], FALSE, error);
	name = arg_string (&args[1]);
	if (node == NULL || name == NULL)
		return node != NULL ? type_error (error, "rename_node expects a string name") : FALSE;
	lrg_editor_script_api_rename_node (LRG_EDITOR_SCRIPT_API (obj), node, name);
	return TRUE;
}

static gboolean
sm_set_value (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	GObject     *object;
	const gchar *prop;

	if (n < 3)
		return type_error (error, "set_value expects object, property, value");
	object = arg_object (&args[0]);
	prop = arg_string (&args[1]);
	if (object == NULL || prop == NULL)
		return type_error (error, "set_value expects an object and a property name");
	return ret_bool (ret, lrg_editor_script_api_set_value (LRG_EDITOR_SCRIPT_API (obj), object, prop, &args[2]));
}

static gboolean
sm_set_vec (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error, gint which)
{
	LrgNode *node;
	gdouble  x, y, z;

	(void) ret;
	if (n < 4)
		return type_error (error, "Expected a node and three numbers");
	node = arg_node (&args[0], FALSE, error);
	if (node == NULL)
		return FALSE;
	if (!arg_double (&args[1], &x) || !arg_double (&args[2], &y) || !arg_double (&args[3], &z))
		return type_error (error, "Expected three numbers");
	if (which == 0)
		lrg_editor_script_api_set_location (LRG_EDITOR_SCRIPT_API (obj), node, x, y, z);
	else if (which == 1)
		lrg_editor_script_api_set_rotation_degrees (LRG_EDITOR_SCRIPT_API (obj), node, x, y, z);
	else
		lrg_editor_script_api_set_scale (LRG_EDITOR_SCRIPT_API (obj), node, x, y, z);
	return TRUE;
}

static gboolean
sm_set_location (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	return sm_set_vec (obj, n, args, ret, error, 0);
}

static gboolean
sm_set_rotation (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	return sm_set_vec (obj, n, args, ret, error, 1);
}

static gboolean
sm_set_scale (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	return sm_set_vec (obj, n, args, ret, error, 2);
}

static gboolean
sm_get_selection (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	(void) n;
	(void) args;
	(void) error;
	return ret_strv (ret, nodes_to_guids (lrg_editor_script_api_get_selection (LRG_EDITOR_SCRIPT_API (obj))));
}

static gboolean
sm_select (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	LrgNode *node;

	(void) ret;
	if (n < 1)
		return type_error (error, "select expects a node");
	node = arg_node (&args[0], FALSE, error);
	if (node == NULL)
		return FALSE;
	lrg_editor_script_api_select (LRG_EDITOR_SCRIPT_API (obj), node, (n > 1) ? arg_bool (&args[1], FALSE) : FALSE);
	return TRUE;
}

static gboolean
sm_clear_selection (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	(void) n;
	(void) args;
	(void) ret;
	(void) error;
	lrg_editor_script_api_clear_selection (LRG_EDITOR_SCRIPT_API (obj));
	return TRUE;
}

static gboolean
sm_begin_undo (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	(void) ret;
	(void) error;
	lrg_editor_script_api_begin_undo (LRG_EDITOR_SCRIPT_API (obj), (n > 0) ? arg_string (&args[0]) : NULL);
	return TRUE;
}

static gboolean
sm_end_undo (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	(void) n;
	(void) args;
	(void) ret;
	(void) error;
	lrg_editor_script_api_end_undo (LRG_EDITOR_SCRIPT_API (obj));
	return TRUE;
}

static gboolean
sm_undo (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	(void) n;
	(void) args;
	(void) ret;
	(void) error;
	lrg_editor_script_api_undo (LRG_EDITOR_SCRIPT_API (obj));
	return TRUE;
}

static gboolean
sm_redo (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	(void) n;
	(void) args;
	(void) ret;
	(void) error;
	lrg_editor_script_api_redo (LRG_EDITOR_SCRIPT_API (obj));
	return TRUE;
}

static gboolean
sm_list_assets (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	(void) error;
	return ret_strv (ret, lrg_editor_script_api_list_assets (LRG_EDITOR_SCRIPT_API (obj),
	                                                         (n > 0) ? arg_string (&args[0]) : NULL));
}

static gboolean
sm_import_asset (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	gchar *path;

	if (n < 1 || arg_string (&args[0]) == NULL)
		return type_error (error, "import_asset expects a source path");
	path = lrg_editor_script_api_import_asset (LRG_EDITOR_SCRIPT_API (obj), arg_string (&args[0]),
	                                           (n > 1) ? arg_string (&args[1]) : NULL, error);
	if (path == NULL)
		return FALSE;
	g_value_init (ret, G_TYPE_STRING);
	g_value_take_string (ret, path);
	return TRUE;
}

static gboolean
sm_create_asset (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	if (n < 2 || arg_string (&args[0]) == NULL || arg_string (&args[1]) == NULL)
		return type_error (error, "create_asset expects a path and contents");
	if (!lrg_editor_script_api_create_asset (LRG_EDITOR_SCRIPT_API (obj),
	                                         arg_string (&args[0]),
	                                         arg_string (&args[1]), error))
		return FALSE;
	return ret_bool (ret, TRUE);
}

static gboolean
sm_log (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	(void) ret;
	if (n < 1)
		return type_error (error, "log expects a message");
	lrg_editor_script_api_log (LRG_EDITOR_SCRIPT_API (obj), arg_string (&args[0]));
	return TRUE;
}

static gboolean
sm_warn (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	(void) ret;
	if (n < 1)
		return type_error (error, "warn expects a message");
	lrg_editor_script_api_warn (LRG_EDITOR_SCRIPT_API (obj), arg_string (&args[0]));
	return TRUE;
}

static gboolean
sm_toast (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	(void) ret;
	if (n < 1)
		return type_error (error, "toast expects a message");
	lrg_editor_script_api_toast (LRG_EDITOR_SCRIPT_API (obj), arg_string (&args[0]),
	                             (n > 1) ? arg_string (&args[1]) : NULL);
	return TRUE;
}

static gboolean
sm_call (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	gchar *json;

	if (n < 1 || arg_string (&args[0]) == NULL)
		return type_error (error, "call expects a method name");
	json = lrg_editor_script_api_call (LRG_EDITOR_SCRIPT_API (obj), arg_string (&args[0]),
	                                   (n > 1) ? arg_string (&args[1]) : NULL, error);
	if (json == NULL)
		return FALSE;
	g_value_init (ret, G_TYPE_STRING);
	g_value_take_string (ret, json);
	return TRUE;
}

static gboolean
sm_add_menu_item (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	const gchar *name;
	const gchar *label;
	const gchar *func;

	if (n < 3)
		return type_error (error, "add_menu_item expects name, label, function");
	name = arg_string (&args[0]);
	label = arg_string (&args[1]);
	func = arg_string (&args[2]);
	if (func == NULL)
		return type_error (error, "add_menu_item function must be a name string "
		                          "(the Lua and Python bridges cannot pass a function object)");
	return ret_bool (ret, lrg_editor_script_api_add_menu_item (LRG_EDITOR_SCRIPT_API (obj), name, label, func,
	                                                           (n > 3) ? arg_string (&args[3]) : NULL));
}

static gboolean
sm_get_tools (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	(void) n;
	(void) args;
	(void) error;
	return ret_strv (ret, lrg_editor_script_api_get_tools (LRG_EDITOR_SCRIPT_API (obj)));
}

static gboolean
sm_run_tool (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	if (n < 1 || arg_string (&args[0]) == NULL)
		return type_error (error, "run_tool expects an id");
	if (!lrg_editor_script_api_run_tool (LRG_EDITOR_SCRIPT_API (obj), arg_string (&args[0]), error))
		return FALSE;
	return ret_bool (ret, TRUE);
}

static gboolean
sm_set_run_in_editor (LrgScriptable *obj, guint n, const GValue *args, GValue *ret, GError **error)
{
	LrgNode *node;
	guint    index = 0;

	if (n < 3)
		return type_error (error, "set_run_in_editor expects node, index, enabled");
	node = arg_node (&args[0], FALSE, error);
	if (node == NULL)
		return FALSE;
	if (!arg_uint (&args[1], &index))
		return type_error (error, "set_run_in_editor index must be an integer");
	if (!lrg_editor_script_api_set_run_in_editor (LRG_EDITOR_SCRIPT_API (obj), node, index,
	                                              arg_bool (&args[2], FALSE), error))
		return FALSE;
	return ret_bool (ret, TRUE);
}

static const LrgScriptMethod api_methods[] = {
	LRG_SCRIPT_METHOD ("find_node", sm_find_node, "GUID or name path", 1),
	LRG_SCRIPT_METHOD ("find_nodes", sm_find_nodes, "Name substring", -1),
	LRG_SCRIPT_METHOD ("create_node", sm_create_node, "name, primitive, parent", -1),
	LRG_SCRIPT_METHOD ("delete_node", sm_delete_node, "Delete a node", 1),
	LRG_SCRIPT_METHOD ("reparent_node", sm_reparent_node, "node, parent", -1),
	LRG_SCRIPT_METHOD ("rename_node", sm_rename_node, "node, name", 2),
	LRG_SCRIPT_METHOD ("set_value", sm_set_value, "object, property, value", 3),
	LRG_SCRIPT_METHOD ("set_location", sm_set_location, "node, x, y, z", 4),
	LRG_SCRIPT_METHOD ("set_rotation_degrees", sm_set_rotation, "node, x, y, z degrees", 4),
	LRG_SCRIPT_METHOD ("set_scale", sm_set_scale, "node, x, y, z", 4),
	LRG_SCRIPT_METHOD ("get_selection", sm_get_selection, "Selected root guids", 0),
	LRG_SCRIPT_METHOD ("select", sm_select, "node, add", -1),
	LRG_SCRIPT_METHOD ("clear_selection", sm_clear_selection, "Clear the selection", 0),
	LRG_SCRIPT_METHOD ("begin_undo", sm_begin_undo, "Open an undo group", -1),
	LRG_SCRIPT_METHOD ("end_undo", sm_end_undo, "Close an undo group", 0),
	LRG_SCRIPT_METHOD ("undo", sm_undo, "Undo the previous committed step", 0),
	LRG_SCRIPT_METHOD ("redo", sm_redo, "Redo", 0),
	LRG_SCRIPT_METHOD ("list_assets", sm_list_assets, "Project-relative paths", -1),
	LRG_SCRIPT_METHOD ("import_asset", sm_import_asset, "source, dest", -1),
	LRG_SCRIPT_METHOD ("create_asset", sm_create_asset, "path, contents", 2),
	LRG_SCRIPT_METHOD ("log", sm_log, "Console line", 1),
	LRG_SCRIPT_METHOD ("warn", sm_warn, "Console warning", 1),
	LRG_SCRIPT_METHOD ("toast", sm_toast, "message, level", -1),
	LRG_SCRIPT_METHOD ("call", sm_call, "IPC method, params JSON", -1),
	LRG_SCRIPT_METHOD ("add_menu_item", sm_add_menu_item, "name, label, function, shortcut", -1),
	LRG_SCRIPT_METHOD ("get_tools", sm_get_tools, "Active tool ids", 0),
	LRG_SCRIPT_METHOD ("run_tool", sm_run_tool, "Run a tool by id or name", 1),
	LRG_SCRIPT_METHOD ("set_run_in_editor", sm_set_run_in_editor, "node, index, enabled", 3)
};

static const LrgScriptMethod *
api_get_script_methods (LrgScriptable *obj,
                        guint         *n_methods)
{
	(void) obj;
	if (n_methods != NULL)
		*n_methods = G_N_ELEMENTS (api_methods);
	return api_methods;
}

static void
api_scriptable_init (LrgScriptableInterface *iface)
{
	iface->get_script_methods = api_get_script_methods;
}

/* --------------------------------------------------------------------------
 * GObject
 * -------------------------------------------------------------------------- */

static void
lrg_editor_script_api_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	LrgEditorScriptApi *self = LRG_EDITOR_SCRIPT_API (object);

	switch (prop_id)
	{
	case PROP_EDITOR:
		g_value_set_object (value, self->editor);
		break;
	case PROP_PROJECT_DIR:
		g_value_set_string (value, self->project_dir);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_script_api_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	LrgEditorScriptApi *self = LRG_EDITOR_SCRIPT_API (object);

	switch (prop_id)
	{
	case PROP_EDITOR:
		g_set_object (&self->editor, g_value_get_object (value));
		break;
	case PROP_PROJECT_DIR:
		lrg_editor_script_api_set_project_dir (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_script_api_dispose (GObject *object)
{
	LrgEditorScriptApi *self = LRG_EDITOR_SCRIPT_API (object);

	self->finalizing = TRUE;
	lrg_editor_script_api_stop_editor_scripts (self);
	g_clear_pointer (&self->editor_runs, g_hash_table_unref);
	g_clear_pointer (&self->files, g_hash_table_unref);
	g_clear_pointer (&self->repl, g_hash_table_unref);
	g_clear_object (&self->monitor);
	if (self->reloader != NULL && self->watched_dir != NULL && lrg_script_reloader_unwatch_path != NULL)
		lrg_script_reloader_unwatch_path (self->reloader, self->watched_dir);
	g_clear_object (&self->reloader);
	g_clear_object (&self->server);
	g_clear_object (&self->actions);
	g_clear_object (&self->log);
	g_clear_object (&self->editor);
	g_clear_object (&self->menu);
	G_OBJECT_CLASS (lrg_editor_script_api_parent_class)->dispose (object);
}

static void
lrg_editor_script_api_finalize (GObject *object)
{
	LrgEditorScriptApi *self = LRG_EDITOR_SCRIPT_API (object);
	guint               i;

	g_clear_pointer (&self->tools, g_ptr_array_unref);
	g_clear_pointer (&self->frames, g_ptr_array_unref);
	g_clear_pointer (&self->failures, g_ptr_array_unref);
	for (i = 0; i < self->captures->len; i++)
		g_string_free ((GString *) g_ptr_array_index (self->captures, i), TRUE);
	g_clear_pointer (&self->captures, g_ptr_array_unref);
	g_free (self->project_dir);
	g_free (self->watched_dir);
	g_free (self->tools_stamp);
	G_OBJECT_CLASS (lrg_editor_script_api_parent_class)->finalize (object);
}

static void
lrg_editor_script_api_class_init (LrgEditorScriptApiClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = lrg_editor_script_api_get_property;
	object_class->set_property = lrg_editor_script_api_set_property;
	object_class->dispose = lrg_editor_script_api_dispose;
	object_class->finalize = lrg_editor_script_api_finalize;

	/**
	 * LrgEditorScriptApi:editor:
	 *
	 * The editor whose level scripts edit. Construct-only.
	 */
	api_props[PROP_EDITOR] =
		g_param_spec_object ("editor", "Editor", "The editor scripts edit",
		                     LRG_TYPE_EDITOR,
		                     (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

	/**
	 * LrgEditorScriptApi:project-dir:
	 *
	 * The open project. Changing it reloads `editor_scripts/`. Notifies
	 * only when the canonical path changes.
	 */
	api_props[PROP_PROJECT_DIR] =
		g_param_spec_string ("project-dir", "Project directory", "The open project",
		                     NULL,
		                     (GParamFlags) (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));
	g_object_class_install_properties (object_class, N_PROPS, api_props);

	/**
	 * LrgEditorScriptApi::output:
	 * @self: the API
	 * @line: one line of script output, with a single trailing newline removed
	 *
	 * A `print`, log() or warn() from a script, and a tool-script error.
	 */
	api_signals[SIGNAL_OUTPUT] =
		g_signal_new ("output",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_editor_script_api_init (LrgEditorScriptApi *self)
{
	self->repl = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, held_free);
	self->files = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, held_free);
	self->tools = g_ptr_array_new_with_free_func (tool_free);
	self->menu = g_menu_new ();
	self->frames = g_ptr_array_new_with_free_func (g_free);
	self->editor_runs = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, editor_run_free);
	self->failures = g_ptr_array_new_with_free_func (failure_free);
	self->captures = g_ptr_array_new ();
	if (lrg_script_reloader_get_default != NULL)
		self->reloader = (LrgScriptReloader *) g_object_ref (lrg_script_reloader_get_default ());
	else
		self->reloader = lrg_script_reloader_new ();
}
