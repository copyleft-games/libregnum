/* lrg-editor-ai-debug-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The lrgeditor AI panel (see the header) and the helpers the behaviour
 * tree and state-machine editors share with it.
 *
 * Layout, top to bottom: a header (title, play status), then in a scroll
 * box the AGENTS rows (one chip per live LrgAiComponent; the checked one
 * is the focus), the selected agent (tree, current node and active path,
 * machine and active states, Open Tree / Open Machine, an event field),
 * its BLACKBOARD (one row per key: name, type, an editable value), and
 * the DECISIONS inspector (per watched brain: totals, one chip per topic,
 * the selected topic's request and answer, the newest log lines).
 *
 * State that must outlive the panel (the focus, the watched brains and
 * their log) lives on the app (AiAppState, object data), so the ai.*
 * methods and the editors' live views work whether or not the panel is
 * docked. The panel polls the play world a few times a second and
 * rebuilds a section only when its signature changed; values are written
 * into existing rows in place so a value being typed is never replaced.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL

#include "lrg-editor-ai-debug-panel.h"
#include "lrg-editor-app-private.h"
#include "lrg-editor-console-panel.h"
#include "data/lrg-editor-bt-editor.h"
#include "data/lrg-editor-fsm-editor.h"
#include "../lrg-editor-server.h"
#include "../lrg-editor-selection.h"
#include "../../ai/lrg-blackboard.h"
#include "../../ai/lrg-bt-asset.h"
#include "../../ai/lrg-logic-machine.h"
#include "../../ai/lrg-decider.h"
#include "../../ai/lrg-decision.h"
#include "../../ecs/lrg-world.h"
#include "../../ecs/lrg-game-object.h"
#include "../../ecs/lrg-component.h"
#include "../../world3d/lrg-world3d.h"
#include "../../world3d/lrg-entity3d.h"
#include "../../level/lrg-node.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-button.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-scroll-box.h"
#include "../../ui/lrg-wrap-box.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../paint/lrg-paint-list.h"
#include <glib/gstdio.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#define STATE_KEY      "lrg-editor-ai-state"
#define PANEL_KEY      "lrg-editor-ai-debug-panel"
#define INSTALLED_KEY  "lrg-editor-ai-installed"
#define LOG_MAX        (200)
#define LOG_SHOWN      (12)
#define POLL_SECONDS   (0.2f)

/* ==========================================================================
 * Shared helpers (declared in the header under LRG_EDITOR_PANEL_INTERNAL)
 * ========================================================================== */

/* A YAML scalar for a string: plain when it is an identifier-like word
 * that YAML reads back as the same string, quoted otherwise. */
gchar *
editor_ai_yaml_str (const gchar *s)
{
	static const gchar * const words[] = { "true", "false", "yes", "no", "on", "off", "null", "y", "n" };
	const gchar               *p;
	gboolean                   plain;
	guint                      i;
	GString                   *out;

	if (s == NULL)
		return g_strdup ("null");
	plain = *s != '\0' && (g_ascii_isalpha (*s) || *s == '_');
	for (p = s; plain && *p != '\0'; p++)
		if (!g_ascii_isalnum (*p) && *p != '_' && *p != '-' && *p != '.' && *p != '/')
			plain = FALSE;
	for (i = 0; plain && i < G_N_ELEMENTS (words); i++)
		if (g_ascii_strcasecmp (s, words[i]) == 0)
			plain = FALSE;
	if (plain)
		return g_strdup (s);
	out = g_string_new ("\"");
	for (p = s; *p != '\0'; p++)
	{
		if (*p == '"' || *p == '\\')
			g_string_append_c (out, '\\');
		if (*p == '\n')
			g_string_append (out, "\\n");
		else
			g_string_append_c (out, *p);
	}
	g_string_append_c (out, '"');
	return g_string_free (out, FALSE);
}

/* A number written the shortest way that reads back as the same number;
 * @keep_point writes integral values as "2.0" so they stay floats. */
gchar *
editor_ai_num_str (gdouble  v,
                   gboolean keep_point)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	if (fabs (v - rint (v)) < 1e-9 && fabs (v) < 1e15)
		return keep_point ? g_strdup_printf ("%.1f", rint (v)) : g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) rint (v));
	g_ascii_formatd (buf, sizeof buf, "%.6g", v);
	return g_strdup (buf);
}

/* "states[1].transitions[0].to: unknown ..." -> "/states/1/transitions/0/to";
 * "" when the message does not start with a key path. Messages may be
 * prefixed with a file name ("a.rbt: root.children[0]: ..."): only the
 * last key path before the text counts. */
gchar *
editor_ai_error_path (const gchar *message)
{
	const gchar *start = message;
	const gchar *colon;
	const gchar *space;
	GString     *s = g_string_new ("");
	const gchar *p;

	if (message == NULL)
		return g_string_free (s, FALSE);
	/* skip leading "dir/file.rbt: " prefixes */
	for (;;)
	{
		colon = strstr (start, ": ");
		if (colon == NULL)
			break;
		if ((colon - start > 4 && strncmp (colon - 4, ".rbt", 4) == 0) ||
		    (colon - start > 5 && strncmp (colon - 5, ".rfsm", 5) == 0))
			start = colon + 2;
		else
			break;
	}
	space = strchr (start, ' ');
	if (colon == NULL || (space != NULL && space < colon) || colon == start)
		return g_string_free (s, FALSE);
	g_string_append_c (s, '/');
	for (p = start; p < colon; p++)
	{
		if (*p == '.' || *p == '[')
			g_string_append_c (s, '/');
		else if (*p != ']')
			g_string_append_c (s, *p);
	}
	return g_string_free (s, FALSE);
}

/* A subtree of @doc as JSON, scalars typed the way YAML reads them. */
JsonNode *
editor_ai_doc_to_json (LrgEditorDocument *doc,
                       const gchar       *path)
{
	LrgEditorDocumentNodeKind kind = lrg_editor_document_get_kind (doc, path);
	JsonNode                 *node;

	if (kind == LRG_EDITOR_DOCUMENT_NODE_MAPPING)
	{
		g_auto(GStrv) keys = lrg_editor_document_get_keys (doc, path);
		JsonObject   *o = json_object_new ();
		guint         i;

		for (i = 0; keys != NULL && keys[i] != NULL; i++)
		{
			g_autofree gchar *child = lrg_editor_document_path_join (path, keys[i]);

			json_object_set_member (o, keys[i], editor_ai_doc_to_json (doc, child));
		}
		node = json_node_new (JSON_NODE_OBJECT);
		json_node_take_object (node, o);
		return node;
	}
	if (kind == LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
	{
		JsonArray *a = json_array_new ();
		gint       n = lrg_editor_document_get_length (doc, path);
		gint       i;

		for (i = 0; i < n; i++)
		{
			g_autofree gchar *idx = g_strdup_printf ("%d", i);
			g_autofree gchar *child = lrg_editor_document_path_join (path, idx);

			json_array_add_element (a, editor_ai_doc_to_json (doc, child));
		}
		node = json_node_new (JSON_NODE_ARRAY);
		json_node_take_array (node, a);
		return node;
	}
	node = json_node_new (JSON_NODE_NULL);
	if (kind == LRG_EDITOR_DOCUMENT_NODE_SCALAR)
	{
		GValue v = G_VALUE_INIT;

		if (lrg_editor_document_get_value (doc, path, &v))
		{
			if (G_VALUE_HOLDS_BOOLEAN (&v))
				json_node_init_boolean (node, g_value_get_boolean (&v));
			else if (G_VALUE_HOLDS_INT64 (&v))
				json_node_init_int (node, g_value_get_int64 (&v));
			else if (G_VALUE_HOLDS_DOUBLE (&v))
				json_node_init_double (node, g_value_get_double (&v));
			else if (G_VALUE_HOLDS_STRING (&v))
				json_node_init_string (node, g_value_get_string (&v));
			g_value_unset (&v);
		}
	}
	return node;
}

/* JSON as one line of flow YAML ("{action: shout, params: {loud: true}}"). */
gchar *
editor_ai_json_to_yaml (JsonNode *node)
{
	if (node == NULL || JSON_NODE_HOLDS_NULL (node))
		return g_strdup ("null");
	if (JSON_NODE_HOLDS_OBJECT (node))
	{
		JsonObject *o = json_node_get_object (node);
		GList      *members = json_object_get_members (o);
		GList      *l;
		GString    *s = g_string_new ("{");

		for (l = members; l != NULL; l = l->next)
		{
			g_autofree gchar *k = editor_ai_yaml_str (l->data);
			g_autofree gchar *v = editor_ai_json_to_yaml (json_object_get_member (o, l->data));

			g_string_append_printf (s, "%s%s: %s", l == members ? "" : ", ", k, v);
		}
		g_list_free (members);
		g_string_append_c (s, '}');
		return g_string_free (s, FALSE);
	}
	if (JSON_NODE_HOLDS_ARRAY (node))
	{
		JsonArray *a = json_node_get_array (node);
		GString   *s = g_string_new ("[");
		guint      i;

		for (i = 0; i < json_array_get_length (a); i++)
		{
			g_autofree gchar *v = editor_ai_json_to_yaml (json_array_get_element (a, i));

			g_string_append_printf (s, "%s%s", i > 0 ? ", " : "", v);
		}
		g_string_append_c (s, ']');
		return g_string_free (s, FALSE);
	}
	switch (json_node_get_value_type (node))
	{
	case G_TYPE_BOOLEAN:
		return g_strdup (json_node_get_boolean (node) ? "true" : "false");
	case G_TYPE_INT64:
		return g_strdup_printf ("%" G_GINT64_FORMAT, json_node_get_int (node));
	case G_TYPE_DOUBLE:
		return editor_ai_num_str (json_node_get_double (node), TRUE);
	default:
		return editor_ai_yaml_str (json_node_get_string (node));
	}
}

gchar *
editor_ai_flow_yaml (LrgEditorDocument *doc,
                     const gchar       *path)
{
	g_autoptr(JsonNode) node = editor_ai_doc_to_json (doc, path);

	return editor_ai_json_to_yaml (node);
}

/* Text typed into a value field as YAML of the given blackboard type
 * ("int", "float", "bool", "string"; NULL: guess from the text). */
gchar *
editor_ai_text_to_yaml (const gchar *text,
                        const gchar *type)
{
	gchar   *end = NULL;
	gdouble  d;

	if (text == NULL)
		return g_strdup ("null");
	if (g_strcmp0 (type, "string") == 0 || g_strcmp0 (type, "object") == 0)
		return editor_ai_yaml_str (text);
	if (g_strcmp0 (type, "bool") == 0)
		return g_strdup ((g_ascii_strcasecmp (text, "true") == 0 || g_strcmp0 (text, "1") == 0 ||
		                  g_ascii_strcasecmp (text, "yes") == 0) ? "true" : "false");
	d = g_ascii_strtod (text, &end);
	if (end != NULL && end != text && *end == '\0')
	{
		if (g_strcmp0 (type, "int") == 0)
			return g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) rint (d));
		if (g_strcmp0 (type, "float") == 0)
			return editor_ai_num_str (d, TRUE);
		return g_strdup (text);
	}
	if (type == NULL && (g_ascii_strcasecmp (text, "true") == 0 || g_ascii_strcasecmp (text, "false") == 0))
		return g_ascii_strdown (text, -1);
	if (g_strcmp0 (type, "int") == 0 || g_strcmp0 (type, "float") == 0)
		return g_strdup ("0");
	return editor_ai_yaml_str (text);
}

/* --- IPC parameter helpers ---------------------------------------------- */

const gchar *
editor_ai_param_string (JsonObject  *params,
                        const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	return (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING) ? json_node_get_string (n)
	                                                                                    : NULL;
}

const gchar *
editor_ai_require_string (JsonObject   *params,
                          const gchar  *name,
                          GError      **error)
{
	const gchar *s = editor_ai_param_string (params, name);

	if (s == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Missing string parameter '%s'", name);
	return s;
}

gboolean
editor_ai_param_number (JsonObject  *params,
                        const gchar *name,
                        gdouble     *out)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) == G_TYPE_STRING ||
	    json_node_get_value_type (n) == G_TYPE_BOOLEAN)
		return FALSE;
	*out = json_node_get_double (n);
	return TRUE;
}

gboolean
editor_ai_param_is_null (JsonObject  *params,
                         const gchar *name)
{
	return params != NULL && json_object_has_member (params, name) &&
	       JSON_NODE_HOLDS_NULL (json_object_get_member (params, name));
}

/* A server error from the error of an edit (not found stays not found). */
void
editor_ai_fail (GError       **error,
                const GError  *cause)
{
	gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

	if (cause != NULL && (g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
	                      g_error_matches (cause, G_FILE_ERROR, G_FILE_ERROR_NOENT)))
		code = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
	else if (cause != NULL && g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
		code = LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS;
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, cause != NULL ? cause->message : "failed");
}

void
editor_ai_fail_msg (GError      **error,
                    gint          code,
                    const gchar  *message)
{
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, message);
}

JsonNode *
editor_ai_object_node (JsonObject *object)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, object);
	return n;
}

/* Validates @editor and returns {ok, issues:[{severity, path, line, message}]}. */
JsonNode *
editor_ai_issues_result (LrgEditorDataEditor *editor)
{
	guint       errors = lrg_editor_data_editor_validate (editor);
	GPtrArray  *issues = lrg_editor_data_editor_get_issues (editor);
	JsonObject *o = json_object_new ();
	JsonArray  *a = json_array_new ();
	guint       i;

	for (i = 0; issues != NULL && i < issues->len; i++)
	{
		LrgEditorDocumentIssue *issue = g_ptr_array_index (issues, i);
		JsonObject             *io = json_object_new ();

		json_object_set_string_member (io, "severity", issue->severity == LRG_EDITOR_PROBLEM_ERROR ? "error"
		                                               : issue->severity == LRG_EDITOR_PROBLEM_WARNING ? "warning"
		                                                                                               : "info");
		json_object_set_string_member (io, "path", issue->path);
		json_object_set_int_member (io, "line", issue->line);
		json_object_set_string_member (io, "message", issue->message);
		json_array_add_object_element (a, io);
	}
	json_object_set_boolean_member (o, "ok", errors == 0);
	json_object_set_array_member (o, "issues", a);
	return editor_ai_object_node (o);
}

/* Writes a new file (project-relative paths resolve against the project);
 * refuses to replace an existing one. */
gboolean
editor_ai_create_file (LrgEditorApp  *app,
                       const gchar   *path,
                       const gchar   *text,
                       gchar        **out_abs,
                       GError       **error)
{
	g_autofree gchar *abs = NULL;
	g_autofree gchar *dir = NULL;
	const gchar      *project = lrg_editor_app_get_project_dir (app);

	if (g_path_is_absolute (path))
		abs = g_canonicalize_filename (path, NULL);
	else
		abs = g_canonicalize_filename (path, project != NULL ? project : g_get_current_dir ());
	if (g_file_test (abs, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s already exists", abs);
		return FALSE;
	}
	dir = g_path_get_dirname (abs);
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "cannot create %s: %s", dir,
		             g_strerror (errno));
		return FALSE;
	}
	if (!g_file_set_contents (abs, text, -1, error))
		return FALSE;
	if (out_abs != NULL)
		*out_abs = g_steal_pointer (&abs);
	return TRUE;
}

/* --- card bodies: one or two lines of detail painted in a role colour --- */

typedef struct
{
	gchar         *text;
	LrgThemeColor  role;
} CardBody;

static void
card_body_free (gpointer data)
{
	CardBody *b = data;

	g_free (b->text);
	g_free (b);
}

static void
card_body_paint (LrgCustomWidget *widget,
                 LrgPaintList    *list,
                 gfloat           x,
                 gfloat           y,
                 gfloat           width,
                 gfloat           height,
                 gpointer         user_data)
{
	CardBody       *b = user_data;
	LrgTheme       *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat          fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor *ink = lrg_theme_get_color (theme, b->role);
	g_auto(GStrv)   lines = g_strsplit (b->text != NULL ? b->text : "", "\n", 3);
	guint           i;

	/* the card lays the body out at its zoomed size: shrink with it */
	fs = MIN (fs, (height - 4.0f) / (1.25f * MAX (1u, g_strv_length (lines))));
	if (fs < 4.0f)
		return;
	lrg_paint_list_push_clip (list, x, y, width, height);
	for (i = 0; lines[i] != NULL; i++)
		lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), lines[i], x + 6.0f, y + 2.0f + (gfloat) i * fs * 1.25f,
		                     fs, 0.0f, i == 0 ? ink : lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
	lrg_paint_list_pop_clip (list);
}

/* @lines: the number of lines the body keeps room for (live text may
 * add lines to the authored ones; the card does not resize while playing) */
LrgWidget *
editor_ai_card_body_new (const gchar   *text,
                         LrgThemeColor  role,
                         gfloat         width,
                         guint          lines)
{
	static const LrgCustomWidgetFuncs funcs = { NULL, card_body_paint, NULL, NULL, { NULL } };
	LrgCustomWidget *w = lrg_custom_widget_new ();
	CardBody        *b = g_new0 (CardBody, 1);

	b->text = g_strdup (text);
	b->role = role;
	lrg_custom_widget_set_funcs (w, &funcs, b, card_body_free);
	g_object_set_data (G_OBJECT (w), "editor-ai-card-body", b);
	lrg_custom_widget_set_preferred_width (w, width);
	lrg_custom_widget_set_preferred_height (w, 8.0f + 16.0f * (gfloat) MAX (2u, lines));
	lrg_widget_set_pointer_transparent (LRG_WIDGET (w), TRUE);
	return LRG_WIDGET (w);
}

/* Changes a body's text in place (live state: no relayout of the card). */
void
editor_ai_card_body_set (LrgWidget     *body,
                         const gchar   *text,
                         LrgThemeColor  role)
{
	/* the funcs own the CardBody; the widget keeps a borrowed pointer */
	CardBody *b = g_object_get_data (G_OBJECT (body), "editor-ai-card-body");

	if (b == NULL)
		return;
	if (g_strcmp0 (b->text, text) != 0)
	{
		g_free (b->text);
		b->text = g_strdup (text);
	}
	b->role = role;
}

LrgWidget *
editor_ai_heading (const gchar *text)
{
	LrgLabel *l = lrg_label_new (text);

	lrg_widget_add_class (LRG_WIDGET (l), "dim-label");
	return LRG_WIDGET (l);
}

LrgWidget *
editor_ai_button (const gchar *name,
                  const gchar *text,
                  GCallback    callback,
                  gpointer     user_data)
{
	LrgButton *b = lrg_button_new (text);

	lrg_widget_set_name (LRG_WIDGET (b), name);
	if (callback != NULL)
		g_signal_connect (b, "clicked", callback, user_data);
	return LRG_WIDGET (b);
}

/* Frames every card at a readable zoom (0.6 .. 1.1); when the graph is
 * wider than that shows, the left edge (where the roots are) stays in
 * view instead of the middle. */
void
editor_ai_frame_canvas (LrgNodeGraph *canvas)
{
	gfloat zoom, cx, cy;
	gfloat minx = G_MAXFLOAT;
	guint  i, n;

	lrg_node_graph_frame_all (canvas, FALSE);
	zoom = lrg_node_graph_get_zoom (canvas);
	if (zoom >= 0.6f && zoom <= 1.1f)
		return;
	n = lrg_node_graph_get_n_nodes (canvas);
	for (i = 0; i < n; i++)
		minx = MIN (minx, lrg_graph_node_get_x (lrg_node_graph_get_nth_node (canvas, i)));
	lrg_node_graph_get_center (canvas, &cx, &cy);
	if (zoom < 0.6f && n > 0)
	{
		zoom = 0.6f;
		cx = minx - 30.0f + lrg_widget_get_width (LRG_WIDGET (canvas)) / zoom * 0.5f;
	}
	else
		zoom = MIN (zoom, 1.1f);
	lrg_node_graph_set_view (canvas, cx, cy, zoom, FALSE);
}

/* Adds @child to @box and drops the caller's reference. */
void
editor_ai_add (LrgWidget *box,
               LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

/* ==========================================================================
 * App state: the focus, the watched brains and their log
 * ========================================================================== */

typedef struct
{
	gchar            *name;
	LrgDecisionBrain *brain;
} BrainWatch;

typedef struct
{
	gdouble  time;      /* seconds since the state was made */
	gchar   *brain;
	gchar   *kind;      /* asked, answered, failed */
	gchar   *topic;
	gchar   *text;
} LogEntry;

typedef struct
{
	LrgEditorApp *app;      /* weak: the state is the app's data */
	gchar        *focus;    /* picked entity id, NULL: follow the selection */
	GPtrArray    *brains;   /* BrainWatch */
	GQueue        log;      /* LogEntry, oldest first */
	gint64        t0;
	guint         serial;   /* bumped by every brain signal */
} AiAppState;

static void
log_entry_free (gpointer data)
{
	LogEntry *e = data;

	g_free (e->brain);
	g_free (e->kind);
	g_free (e->topic);
	g_free (e->text);
	g_free (e);
}

static void brain_watch_free (gpointer data);

static void
ai_state_free (gpointer data)
{
	AiAppState *st = data;

	g_free (st->focus);
	g_ptr_array_unref (st->brains);
	g_queue_clear_full (&st->log, log_entry_free);
	g_free (st);
}

static AiAppState *
ai_state (LrgEditorApp *app)
{
	AiAppState *st = g_object_get_data (G_OBJECT (app), STATE_KEY);

	if (st == NULL)
	{
		st = g_new0 (AiAppState, 1);
		st->app = app;
		st->brains = g_ptr_array_new_with_free_func (brain_watch_free);
		g_queue_init (&st->log);
		st->t0 = g_get_monotonic_time ();
		g_object_set_data_full (G_OBJECT (app), STATE_KEY, st, ai_state_free);
	}
	return st;
}

static void
log_add (AiAppState  *st,
         const gchar *brain,
         const gchar *kind,
         const gchar *topic,
         const gchar *text)
{
	LogEntry *e = g_new0 (LogEntry, 1);

	e->time = (gdouble) (g_get_monotonic_time () - st->t0) / (gdouble) G_USEC_PER_SEC;
	e->brain = g_strdup (brain);
	e->kind = g_strdup (kind);
	e->topic = g_strdup (topic);
	/* one line: requests and answers are JSON objects */
	e->text = g_strdup (text != NULL ? text : "");
	g_strdelimit (e->text, "\n\r\t", ' ');
	g_queue_push_tail (&st->log, e);
	while (g_queue_get_length (&st->log) > LOG_MAX)
		log_entry_free (g_queue_pop_head (&st->log));
	st->serial++;
}

static const gchar *
brain_name_of (AiAppState       *st,
               LrgDecisionBrain *brain)
{
	guint i;

	for (i = 0; i < st->brains->len; i++)
	{
		BrainWatch *w = g_ptr_array_index (st->brains, i);

		if (w->brain == brain)
			return w->name;
	}
	return "?";
}

static void
on_brain_asked (LrgDecisionBrain *brain,
                const gchar      *topic,
                const gchar      *request,
                gpointer          user_data)
{
	AiAppState *st = user_data;

	log_add (st, brain_name_of (st, brain), "asked", topic, request);
}

static void
on_brain_answered (LrgDecisionBrain *brain,
                   const gchar      *topic,
                   LrgJudgments     *judgments,
                   gpointer          user_data)
{
	AiAppState       *st = user_data;
	g_autofree gchar *json = judgments != NULL ? lrg_judgments_dup_json (judgments) : NULL;

	log_add (st, brain_name_of (st, brain), "answered", topic, json);
}

static void
on_brain_failed (LrgDecisionBrain *brain,
                 const gchar      *topic,
                 const gchar      *message,
                 gpointer          user_data)
{
	AiAppState *st = user_data;

	log_add (st, brain_name_of (st, brain), "failed", topic, message);
}

static void
brain_watch_free (gpointer data)
{
	BrainWatch *w = data;

	g_signal_handlers_disconnect_matched (w->brain, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, on_brain_asked, NULL);
	g_signal_handlers_disconnect_matched (w->brain, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, on_brain_answered, NULL);
	g_signal_handlers_disconnect_matched (w->brain, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, on_brain_failed, NULL);
	g_object_unref (w->brain);
	g_free (w->name);
	g_free (w);
}

static BrainWatch *
find_brain (AiAppState  *st,
            const gchar *name)
{
	guint i;

	for (i = 0; i < st->brains->len; i++)
	{
		BrainWatch *w = g_ptr_array_index (st->brains, i);

		if (name == NULL || g_strcmp0 (w->name, name) == 0)
			return w;
	}
	return NULL;
}

void
lrg_editor_ai_debug_panel_watch_brain (LrgEditorApp     *app,
                                       const gchar      *name,
                                       LrgDecisionBrain *brain)
{
	AiAppState *st;
	BrainWatch *w;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));
	g_return_if_fail (name != NULL);
	g_return_if_fail (LRG_IS_DECISION_BRAIN (brain));

	st = ai_state (app);
	lrg_editor_ai_debug_panel_unwatch_brain (app, name);
	w = g_new0 (BrainWatch, 1);
	w->name = g_strdup (name);
	w->brain = g_object_ref (brain);
	g_signal_connect (brain, "asked", G_CALLBACK (on_brain_asked), st);
	g_signal_connect (brain, "answered", G_CALLBACK (on_brain_answered), st);
	g_signal_connect (brain, "failed", G_CALLBACK (on_brain_failed), st);
	g_ptr_array_add (st->brains, w);
	st->serial++;
}

gboolean
lrg_editor_ai_debug_panel_unwatch_brain (LrgEditorApp *app,
                                         const gchar  *name)
{
	AiAppState *st;
	BrainWatch *w;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	st = ai_state (app);
	w = find_brain (st, name);
	if (w == NULL)
		return FALSE;
	g_ptr_array_remove (st->brains, w);
	st->serial++;
	return TRUE;
}

gchar **
lrg_editor_ai_debug_panel_dup_brain_names (LrgEditorApp *app)
{
	AiAppState *st;
	GStrvBuilder *b;
	guint       i;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	st = ai_state (app);
	b = g_strv_builder_new ();
	for (i = 0; i < st->brains->len; i++)
		g_strv_builder_add (b, ((BrainWatch *) g_ptr_array_index (st->brains, i))->name);
	{
		gchar **out = g_strv_builder_end (b);

		g_strv_builder_unref (b);
		return out;
	}
}

LrgDecisionBrain *
lrg_editor_ai_debug_panel_get_brain (LrgEditorApp *app,
                                     const gchar  *name)
{
	BrainWatch *w;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	w = find_brain (ai_state (app), name);
	return w != NULL ? w->brain : NULL;
}

guint
lrg_editor_ai_debug_panel_get_log_length (LrgEditorApp *app)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), 0);
	return g_queue_get_length (&ai_state (app)->log);
}

gchar *
lrg_editor_ai_debug_panel_dup_log_entry (LrgEditorApp *app,
                                         guint         index)
{
	LogEntry *e;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	e = g_queue_peek_nth (&ai_state (app)->log, index);
	if (e == NULL)
		return NULL;
	return g_strdup_printf ("%s %s %s: %s", e->brain, e->kind, e->topic, e->text);
}

/* ==========================================================================
 * Live entities
 * ========================================================================== */

static LrgEditorPlaySession *
play_of (LrgEditorApp *app)
{
	return (app->play != NULL && lrg_editor_play_session_is_playing (app->play)) ? app->play : NULL;
}

GPtrArray *
lrg_editor_ai_debug_panel_list_live (LrgEditorApp *app)
{
	GPtrArray            *out;
	LrgEditorPlaySession *play;
	LrgWorld3D           *w3;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	out = g_ptr_array_new ();
	play = play_of (app);
	if (play == NULL)
		return out;
	w3 = lrg_editor_play_session_get_world3d (play);
	if (w3 != NULL)
	{
		g_autoptr(GPtrArray) comps = lrg_world3d_get_components_of_type (w3, LRG_TYPE_AI_COMPONENT);
		guint                i;

		for (i = 0; comps != NULL && i < comps->len; i++)
			g_ptr_array_add (out, g_ptr_array_index (comps, i));
	}
	if (out->len == 0 && lrg_editor_play_session_get_world (play) != NULL)
	{
		GList *objects = lrg_world_get_objects (lrg_editor_play_session_get_world (play));
		GList *l;

		for (l = objects; l != NULL; l = l->next)
		{
			GList *comps, *c;

			if (!LRG_IS_GAME_OBJECT (l->data))
				continue;
			comps = lrg_game_object_get_components_of_type (LRG_GAME_OBJECT (l->data), LRG_TYPE_AI_COMPONENT);
			for (c = comps; c != NULL; c = c->next)
				g_ptr_array_add (out, c->data);
			g_list_free (comps);
		}
		g_list_free (objects);
	}
	return out;
}

/* The level node a live component's owner was made from, if any. */
static LrgNode *
node_of (LrgEditorApp   *app,
         LrgAiComponent *component)
{
	LrgEditorPlaySession *play = play_of (app);
	GObject              *owner = lrg_component_get_owner_object (LRG_COMPONENT (component));

	if (play == NULL || owner == NULL)
		return NULL;
	return lrg_editor_play_session_get_node_for_live (play, owner);
}

/* A display name: the level node's name, else the entity's, else its type. */
static gchar *
display_name (LrgEditorApp   *app,
              LrgAiComponent *component)
{
	GObject *owner = lrg_component_get_owner_object (LRG_COMPONENT (component));
	LrgNode *node = node_of (app, component);

	if (node != NULL && lrg_node_get_name (node) != NULL)
		return g_strdup (lrg_node_get_name (node));
	if (owner != NULL && LRG_IS_ENTITY3D (owner) && lrg_entity3d_get_name (LRG_ENTITY3D (owner)) != NULL)
		return g_strdup (lrg_entity3d_get_name (LRG_ENTITY3D (owner)));
	return g_strdup (owner != NULL ? G_OBJECT_TYPE_NAME (owner) : "?");
}

gchar *
editor_ai_dup_display_name (LrgEditorApp   *app,
                            LrgAiComponent *component)
{
	return display_name (app, component);
}

gchar *
lrg_editor_ai_debug_panel_dup_entity_id (LrgEditorApp   *app,
                                         LrgAiComponent *component)
{
	LrgEditorPlaySession *play;
	GObject              *owner;
	const gchar          *guid;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_AI_COMPONENT (component), NULL);

	play = play_of (app);
	owner = lrg_component_get_owner_object (LRG_COMPONENT (component));
	if (owner == NULL)
		return NULL;
	guid = play != NULL ? lrg_editor_play_session_get_guid_for_live (play, owner) : NULL;
	if (guid != NULL)
		return g_strdup (guid);
	if (LRG_IS_ENTITY3D (owner) && lrg_entity3d_get_name (LRG_ENTITY3D (owner)) != NULL)
		return g_strdup (lrg_entity3d_get_name (LRG_ENTITY3D (owner)));
	{
		g_autoptr(GPtrArray) all = lrg_editor_ai_debug_panel_list_live (app);
		guint                i;

		for (i = 0; i < all->len; i++)
			if (g_ptr_array_index (all, i) == component)
				return g_strdup_printf ("#%u", i);
	}
	return NULL;
}

/* The AI component on a live object (3D entity or 2D game object). */
static LrgAiComponent *
component_on (GObject *owner)
{
	if (owner == NULL)
		return NULL;
	if (LRG_IS_ENTITY3D (owner))
		return (LrgAiComponent *) lrg_entity3d_get_component (LRG_ENTITY3D (owner), LRG_TYPE_AI_COMPONENT);
	if (LRG_IS_GAME_OBJECT (owner))
		return (LrgAiComponent *) lrg_game_object_get_component (LRG_GAME_OBJECT (owner), LRG_TYPE_AI_COMPONENT);
	return NULL;
}

LrgAiComponent *
lrg_editor_ai_debug_panel_lookup (LrgEditorApp *app,
                                  const gchar  *entity)
{
	g_autoptr(GPtrArray) all = NULL;
	guint                i;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	if (entity == NULL)
		return lrg_editor_ai_debug_panel_get_focus_component (app);
	all = lrg_editor_ai_debug_panel_list_live (app);
	for (i = 0; i < all->len; i++)
	{
		LrgAiComponent   *c = g_ptr_array_index (all, i);
		g_autofree gchar *id = lrg_editor_ai_debug_panel_dup_entity_id (app, c);
		g_autofree gchar *name = display_name (app, c);

		if (g_strcmp0 (id, entity) == 0 || g_strcmp0 (name, entity) == 0)
			return c;
	}
	return NULL;
}

void
lrg_editor_ai_debug_panel_set_focus (LrgEditorApp *app,
                                     const gchar  *entity)
{
	AiAppState *st;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));

	st = ai_state (app);
	if (g_strcmp0 (st->focus, entity) == 0)
		return;
	g_free (st->focus);
	st->focus = g_strdup (entity);
	st->serial++;
}

const gchar *
lrg_editor_ai_debug_panel_get_focus (LrgEditorApp *app)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	return ai_state (app)->focus;
}

LrgAiComponent *
lrg_editor_ai_debug_panel_get_focus_component (LrgEditorApp *app)
{
	AiAppState           *st;
	LrgEditorPlaySession *play;
	LrgNode              *node;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	st = ai_state (app);
	play = play_of (app);
	if (play == NULL)
		return NULL;
	if (st->focus != NULL)
	{
		LrgAiComponent *c = lrg_editor_ai_debug_panel_lookup (app, st->focus);

		if (c != NULL)
			return c;
	}
	node = lrg_editor_selection_get_primary (lrg_editor_get_selection (lrg_editor_app_get_editor (app)));
	if (node == NULL)
		return NULL;
	return component_on (lrg_editor_play_session_get_live_for_node (play, node));
}

/* Does a component's tree-path / machine-path name the file @abs? */
static gboolean
path_matches (LrgEditorApp *app,
              const gchar  *component_path,
              const gchar  *abs)
{
	const gchar      *project = lrg_editor_app_get_project_dir (app);
	g_autofree gchar *resolved = NULL;
	g_autofree gchar *suffix = NULL;

	if (component_path == NULL || *component_path == '\0' || abs == NULL)
		return FALSE;
	if (g_path_is_absolute (component_path))
	{
		resolved = g_canonicalize_filename (component_path, NULL);
		return g_strcmp0 (resolved, abs) == 0;
	}
	if (project != NULL)
	{
		resolved = g_canonicalize_filename (component_path, project);
		if (g_strcmp0 (resolved, abs) == 0)
			return TRUE;
	}
	suffix = g_strconcat (G_DIR_SEPARATOR_S, component_path, NULL);
	return g_str_has_suffix (abs, suffix);
}

static gchar *
abs_path (LrgEditorApp *app,
          const gchar  *path)
{
	const gchar *project = lrg_editor_app_get_project_dir (app);

	if (g_path_is_absolute (path))
		return g_canonicalize_filename (path, NULL);
	return g_canonicalize_filename (path, project != NULL ? project : g_get_current_dir ());
}

LrgAiComponent *
lrg_editor_ai_debug_panel_find_for_file (LrgEditorApp *app,
                                         const gchar  *path,
                                         gboolean      machine)
{
	g_autofree gchar     *abs = NULL;
	g_autoptr(GPtrArray)  all = NULL;
	LrgAiComponent       *focus;
	guint                 i;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	if (play_of (app) == NULL)
		return NULL;
	abs = abs_path (app, path);
	focus = lrg_editor_ai_debug_panel_get_focus_component (app);
	if (focus != NULL && path_matches (app, machine ? lrg_ai_component_get_machine_path (focus)
	                                                : lrg_ai_component_get_tree_path (focus), abs))
		return focus;
	all = lrg_editor_ai_debug_panel_list_live (app);
	for (i = 0; i < all->len; i++)
	{
		LrgAiComponent *c = g_ptr_array_index (all, i);

		if (path_matches (app, machine ? lrg_ai_component_get_machine_path (c) : lrg_ai_component_get_tree_path (c),
		                  abs))
			return c;
	}
	return NULL;
}

/* ==========================================================================
 * Blackboard values
 * ========================================================================== */

static const gchar *
bb_type_name (GType type)
{
	if (type == G_TYPE_INT)
		return "int";
	if (type == G_TYPE_FLOAT)
		return "float";
	if (type == G_TYPE_BOOLEAN)
		return "bool";
	if (type == G_TYPE_STRING)
		return "string";
	if (type == G_TYPE_OBJECT || g_type_is_a (type, G_TYPE_OBJECT))
		return "object";
	if (type == G_TYPE_POINTER)
		return "pointer";
	return "?";
}

/* Writes @text into @key of @bb as @type (NULL: the key's current type,
 * else guessed from the text). */
static gboolean
bb_write (LrgBlackboard  *bb,
          const gchar    *key,
          const gchar    *text,
          const gchar    *type,
          GError        **error)
{
	GType  cur = lrg_blackboard_get_value_type (bb, key);
	gchar *end = NULL;

	if (type == NULL)
		type = cur != G_TYPE_INVALID ? bb_type_name (cur) : NULL;
	if (type == NULL)
	{
		gdouble d = g_ascii_strtod (text, &end);

		if (g_ascii_strcasecmp (text, "true") == 0 || g_ascii_strcasecmp (text, "false") == 0)
			type = "bool";
		else if (end != text && *end == '\0')
			type = (strchr (text, '.') != NULL || strchr (text, 'e') != NULL) ? "float" : "int";
		else
			type = "string";
		(void) d;
	}
	if (g_strcmp0 (type, "int") == 0)
	{
		gint64 v = g_ascii_strtoll (text, &end, 10);

		if (end == text)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "'%s' is not an integer", text);
			return FALSE;
		}
		lrg_blackboard_set_int (bb, key, (gint) v);
	}
	else if (g_strcmp0 (type, "float") == 0)
	{
		gdouble v = g_ascii_strtod (text, &end);

		if (end == text)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "'%s' is not a number", text);
			return FALSE;
		}
		lrg_blackboard_set_float (bb, key, (gfloat) v);
	}
	else if (g_strcmp0 (type, "bool") == 0)
		lrg_blackboard_set_bool (bb, key, g_ascii_strcasecmp (text, "true") == 0 || g_strcmp0 (text, "1") == 0 ||
		                                  g_ascii_strcasecmp (text, "yes") == 0);
	else if (g_strcmp0 (type, "string") == 0)
		lrg_blackboard_set_string (bb, key, text);
	else
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s values cannot be edited", type);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * The panel
 * ========================================================================== */

struct _LrgEditorAiDebugPanel
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;          /* weak */
	LrgEditorPlaySession *play;         /* owned ref */

	LrgWidget            *top;
	LrgWidget            *status;
	LrgWidget            *scroll;
	LrgWidget            *body;

	LrgWidget            *agent_rows;
	LrgWidget            *agent_box;
	LrgWidget            *agent_lines;
	LrgTextInput         *event_input;
	LrgWidget            *bb_rows;
	GHashTable           *bb_inputs;    /* key -> LrgTextInput (borrowed) */
	LrgWidget            *brain_rows;
	LrgWidget            *topic_lines;
	LrgWidget            *log_lines;

	gchar                *agents_sig;
	gchar                *agent_sig;
	gchar                *bb_sig;
	gchar                *brains_sig;
	gchar                *sel_brain;
	gchar                *sel_topic;
	guint                 seen_serial;
	gfloat                poll;
	gboolean              syncing;
};

G_DEFINE_FINAL_TYPE (LrgEditorAiDebugPanel, lrg_editor_ai_debug_panel, LRG_TYPE_CONTAINER)

static void
clear_box (LrgWidget *box)
{
	lrg_container_remove_all (LRG_CONTAINER (box));
}

/* Fills @box with one label per line of @text (at most @max lines). */
static void
fill_lines (LrgWidget   *box,
            const gchar *prefix,
            const gchar *text,
            guint        max,
            gboolean     mono)
{
	g_auto(GStrv) lines = g_strsplit (text != NULL ? text : "", "\n", -1);
	guint         i, n = 0;

	clear_box (box);
	for (i = 0; lines[i] != NULL && n < max; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("%s-%u", prefix, n);
		LrgLabel         *l;

		if (*lines[i] == '\0' && lines[i + 1] == NULL)
			break;
		l = lrg_label_new (lines[i]);
		lrg_widget_set_name (LRG_WIDGET (l), name);
		if (mono)
			lrg_label_set_font (l, editor_panel_mono_font (box));
		editor_ai_add (box, LRG_WIDGET (l));
		n++;
	}
	if (lines[i] != NULL && n >= max)
	{
		g_autofree gchar *more = g_strdup_printf ("\xe2\x80\xa6 %u more lines", g_strv_length (lines) - i);

		editor_ai_add (box, editor_ai_heading (more));
	}
}

/* Pretty-prints JSON text (unchanged when it does not parse). */
static gchar *
pretty_json (const gchar *text)
{
	g_autoptr(JsonParser)    parser = json_parser_new ();
	g_autoptr(JsonGenerator) gen = NULL;

	if (text == NULL || !json_parser_load_from_data (parser, text, -1, NULL))
		return g_strdup (text);
	gen = json_generator_new ();
	json_generator_set_pretty (gen, TRUE);
	json_generator_set_indent (gen, 2);
	json_generator_set_root (gen, json_parser_get_root (parser));
	return json_generator_to_data (gen, NULL);
}

/* --- agents ---------------------------------------------------------- */

static void
on_agent_chip (LrgWidget *chip,
               gpointer   user_data)
{
	LrgEditorAiDebugPanel *self = LRG_EDITOR_AI_DEBUG_PANEL (user_data);
	const gchar           *id = g_object_get_data (G_OBJECT (chip), "entity");

	if (self->app == NULL)
		return;
	/* clicking the picked agent again goes back to following the selection */
	lrg_editor_ai_debug_panel_set_focus (self->app, g_strcmp0 (lrg_editor_ai_debug_panel_get_focus (self->app), id) == 0
	                                                ? NULL : id);
}

static gchar *
agent_line (LrgEditorApp   *app,
            LrgAiComponent *c)
{
	g_autofree gchar *name = display_name (app, c);
	const gchar      *node = lrg_ai_component_get_current_node (c);
	const gchar      *state = lrg_ai_component_get_current_state (c);
	GString          *s = g_string_new (name);

	if (node != NULL)
		g_string_append_printf (s, " \xc2\xb7 %s", node);
	if (state != NULL)
		g_string_append_printf (s, " \xc2\xb7 [%s]", state);
	if (!lrg_ai_component_get_running (c))
		g_string_append (s, " \xc2\xb7 paused");
	return g_string_free (s, FALSE);
}

static void
sync_agents (LrgEditorAiDebugPanel *self)
{
	g_autoptr(GPtrArray) all = lrg_editor_ai_debug_panel_list_live (self->app);
	LrgAiComponent      *focus = lrg_editor_ai_debug_panel_get_focus_component (self->app);
	GString             *sig = g_string_new (NULL);
	guint                i;

	for (i = 0; i < all->len; i++)
	{
		g_autofree gchar *id = lrg_editor_ai_debug_panel_dup_entity_id (self->app, g_ptr_array_index (all, i));
		g_autofree gchar *line = agent_line (self->app, g_ptr_array_index (all, i));

		g_string_append_printf (sig, "%s=%s%s;", id, line, g_ptr_array_index (all, i) == focus ? "*" : "");
	}
	g_string_append_printf (sig, "|%d", play_of (self->app) != NULL);
	if (g_strcmp0 (sig->str, self->agents_sig) == 0)
	{
		g_string_free (sig, TRUE);
		return;
	}
	g_free (self->agents_sig);
	self->agents_sig = g_string_free (sig, FALSE);

	clear_box (self->agent_rows);
	if (play_of (self->app) == NULL)
	{
		editor_ai_add (self->agent_rows, editor_panel_hint_new ("ai-hint",
		               "Press Play to inspect agents: every entity with an AI component shows here."));
		return;
	}
	if (all->len == 0)
	{
		editor_ai_add (self->agent_rows, editor_panel_hint_new ("ai-hint",
		               "No entity of this level has an AI component (add one in the inspector: type 'ai')."));
		return;
	}
	for (i = 0; i < all->len; i++)
	{
		LrgAiComponent   *c = g_ptr_array_index (all, i);
		g_autofree gchar *id = lrg_editor_ai_debug_panel_dup_entity_id (self->app, c);
		g_autofree gchar *line = agent_line (self->app, c);
		g_autofree gchar *cname = g_strdup_printf ("ai-agent-%u", i);
		LrgWidget        *chip = editor_chip_new (cname, LRG_ICON_USER, line, on_agent_chip, self);

		g_object_set_data_full (G_OBJECT (chip), "entity", g_strdup (id), g_free);
		editor_chip_set_checked (chip, c == focus);
		editor_chip_set_tint (chip, c == focus ? LRG_THEME_COLOR_ACCENT : LRG_THEME_COLOR_TEXT_MUTED);
		editor_ai_add (self->agent_rows, chip);
	}
}

/* --- the selected agent ----------------------------------------------- */

static void
on_open_tree (LrgButton *button,
              gpointer   user_data)
{
	LrgEditorAiDebugPanel *self = LRG_EDITOR_AI_DEBUG_PANEL (user_data);
	LrgAiComponent        *c = lrg_editor_ai_debug_panel_get_focus_component (self->app);
	g_autoptr(GError)      error = NULL;

	if (c == NULL || lrg_ai_component_get_tree_path (c) == NULL)
		return;
	if (lrg_editor_bt_editor_open (self->app, lrg_ai_component_get_tree_path (c), &error) == NULL)
		editor_app_notify (self->app, 2, "%s", error->message);
}

static void
on_open_machine (LrgButton *button,
                 gpointer   user_data)
{
	LrgEditorAiDebugPanel *self = LRG_EDITOR_AI_DEBUG_PANEL (user_data);
	LrgAiComponent        *c = lrg_editor_ai_debug_panel_get_focus_component (self->app);
	g_autoptr(GError)      error = NULL;

	if (c == NULL || lrg_ai_component_get_machine_path (c) == NULL)
		return;
	if (lrg_editor_fsm_editor_open (self->app, lrg_ai_component_get_machine_path (c), &error) == NULL)
		editor_app_notify (self->app, 2, "%s", error->message);
}

static void
send_event_now (LrgEditorAiDebugPanel *self)
{
	LrgAiComponent *c = lrg_editor_ai_debug_panel_get_focus_component (self->app);
	const gchar    *ev = lrg_text_input_get_text (self->event_input);

	if (c == NULL || ev == NULL || *ev == '\0')
		return;
	lrg_ai_component_send_event (c, ev);
	editor_app_set_status (self->app, "Sent event '%s'", ev);
}

static void
on_event_submitted (LrgTextInput *input,
                    gpointer      user_data)
{
	send_event_now (LRG_EDITOR_AI_DEBUG_PANEL (user_data));
}

static void
on_event_send (LrgButton *button,
               gpointer   user_data)
{
	send_event_now (LRG_EDITOR_AI_DEBUG_PANEL (user_data));
}

static gchar *
agent_text (LrgEditorApp   *app,
            LrgAiComponent *c)
{
	GString         *s = g_string_new (NULL);
	LrgBtRunner     *runner = lrg_ai_component_get_runner (c);
	LrgLogicMachine *m = lrg_ai_component_get_machine (c);

	{
		const gchar      *tp = lrg_ai_component_get_tree_path (c);
		g_autofree gchar *rel = tp != NULL ? editor_app_relative_path (app, tp) : NULL;

		g_string_append_printf (s, "Tree: %s\n", rel != NULL ? rel : tp != NULL ? tp : "(none)");
	}
	if (runner != NULL)
	{
		g_auto(GStrv) path = lrg_bt_runner_dup_active_path (runner);
		g_autofree gchar *joined = path != NULL ? g_strjoinv (" \xe2\x86\x92 ", path) : g_strdup ("");

		g_string_append_printf (s, "  %s, tick %" G_GUINT64_FORMAT ", node %s\n",
		                        lrg_bt_status_to_string (lrg_bt_runner_get_status (runner)),
		                        lrg_bt_runner_get_tick_count (runner),
		                        lrg_bt_runner_get_current_node (runner) != NULL
		                        ? lrg_bt_runner_get_current_node (runner) : "-");
		if (*joined != '\0')
			g_string_append_printf (s, "  path: %s\n", joined);
	}
	{
		const gchar      *mp = lrg_ai_component_get_machine_path (c);
		g_autofree gchar *rel = mp != NULL ? editor_app_relative_path (app, mp) : NULL;

		g_string_append_printf (s, "Machine: %s\n", rel != NULL ? rel : mp != NULL ? mp : "(none)");
	}
	if (m != NULL && lrg_logic_machine_is_running (m))
	{
		g_auto(GStrv) active = lrg_logic_machine_dup_active_path (m);
		const gchar  *from = NULL, *to = NULL, *ev = NULL;
		guint         i;

		for (i = 0; active != NULL && active[i] != NULL; i++)
			g_string_append_printf (s, "  %s  %.2f s\n", active[i], lrg_logic_machine_get_state_time (m, active[i]));
		if (lrg_logic_machine_get_last_transition (m, &from, &to, &ev) != NULL)
			g_string_append_printf (s, "  last: %s \xe2\x86\x92 %s%s%s\n", *from != '\0' ? from : "(start)", to,
			                        ev != NULL ? " on " : "", ev != NULL ? ev : "");
	}
	return g_string_free (s, FALSE);
}

static void
on_bb_submitted (LrgTextInput *input,
                 gpointer      user_data)
{
	LrgEditorAiDebugPanel *self = LRG_EDITOR_AI_DEBUG_PANEL (user_data);
	LrgAiComponent        *c = lrg_editor_ai_debug_panel_get_focus_component (self->app);
	const gchar           *key = g_object_get_data (G_OBJECT (input), "key");
	g_autoptr(GError)      error = NULL;

	if (c == NULL || key == NULL || self->syncing)
		return;
	if (!bb_write (lrg_ai_component_get_blackboard (c), key, lrg_text_input_get_text (input), NULL, &error))
		editor_app_notify (self->app, 2, "%s", error->message);
	else
		editor_app_set_status (self->app, "%s = %s", key, lrg_text_input_get_text (input));
}

static void
sync_agent (LrgEditorAiDebugPanel *self)
{
	LrgAiComponent   *c = lrg_editor_ai_debug_panel_get_focus_component (self->app);
	LrgBlackboard    *bb;
	g_auto(GStrv)     keys = NULL;
	g_autofree gchar *text = NULL;
	GString          *sig;
	guint             i;

	lrg_widget_set_visible (self->agent_box, c != NULL);
	if (c == NULL)
	{
		g_clear_pointer (&self->bb_sig, g_free);
		g_clear_pointer (&self->agent_sig, g_free);
		return;
	}
	text = agent_text (self->app, c);
	if (g_strcmp0 (text, self->agent_sig) != 0)
	{
		fill_lines (self->agent_lines, "ai-agent-line", text, 16, FALSE);
		g_free (self->agent_sig);
		self->agent_sig = g_steal_pointer (&text);
	}

	/* blackboard rows: rebuilt when the key set changes, values in place */
	bb = lrg_ai_component_get_blackboard (c);
	keys = lrg_blackboard_dup_keys (bb);
	sig = g_string_new (NULL);
	g_string_append_printf (sig, "%p|", (gpointer) c);
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
		g_string_append_printf (sig, "%s:%s;", keys[i], bb_type_name (lrg_blackboard_get_value_type (bb, keys[i])));
	if (g_strcmp0 (sig->str, self->bb_sig) != 0)
	{
		g_free (self->bb_sig);
		self->bb_sig = g_string_free (sig, FALSE);
		clear_box (self->bb_rows);
		g_hash_table_remove_all (self->bb_inputs);
		for (i = 0; keys != NULL && keys[i] != NULL; i++)
		{
			GType             t = lrg_blackboard_get_value_type (bb, keys[i]);
			LrgWidget        *row = LRG_WIDGET (lrg_hbox_new ());
			LrgWidget        *name = LRG_WIDGET (lrg_label_new (keys[i]));
			LrgWidget        *type = editor_ai_heading (bb_type_name (t));
			g_autofree gchar *rname = g_strdup_printf ("ai-bb-%s", keys[i]);
			g_autofree gchar *vname = g_strdup_printf ("ai-bb-%s-value", keys[i]);

			lrg_widget_set_name (row, rname);
			lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
			lrg_widget_set_min_size (name, 110.0f, 0.0f);
			lrg_widget_set_min_size (type, 48.0f, 0.0f);
			lrg_widget_set_valign (name, LRG_ALIGN_CENTER);
			lrg_widget_set_valign (type, LRG_ALIGN_CENTER);
			editor_ai_add (row, name);
			editor_ai_add (row, type);
			if (t == G_TYPE_INT || t == G_TYPE_FLOAT || t == G_TYPE_BOOLEAN || t == G_TYPE_STRING)
			{
				LrgTextInput     *input = lrg_text_input_new ();
				g_autofree gchar *v = lrg_blackboard_dup_value_text (bb, keys[i]);

				lrg_widget_set_name (LRG_WIDGET (input), vname);
				lrg_widget_set_fill_weight (LRG_WIDGET (input), 1.0f);
				lrg_text_input_set_font_size (input, 14.0f);
				lrg_text_input_set_text (input, v != NULL ? v : "");
				g_object_set_data_full (G_OBJECT (input), "key", g_strdup (keys[i]), g_free);
				g_signal_connect (input, "submitted", G_CALLBACK (on_bb_submitted), self);
				g_hash_table_insert (self->bb_inputs, g_strdup (keys[i]), input);
				editor_ai_add (row, LRG_WIDGET (input));
			}
			else
			{
				g_autofree gchar *v = lrg_blackboard_dup_value_text (bb, keys[i]);
				LrgWidget        *val = LRG_WIDGET (lrg_label_new (v != NULL ? v : ""));

				lrg_widget_set_name (val, vname);
				lrg_widget_set_fill_weight (val, 1.0f);
				editor_ai_add (row, val);
			}
			editor_ai_add (self->bb_rows, row);
		}
		if (keys == NULL || keys[0] == NULL)
			editor_ai_add (self->bb_rows, editor_ai_heading ("The blackboard is empty"));
	}
	else
		g_string_free (sig, TRUE);

	/* live values, except in the field being typed into */
	self->syncing = TRUE;
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		LrgTextInput     *input = g_hash_table_lookup (self->bb_inputs, keys[i]);
		g_autofree gchar *v = NULL;

		if (input == NULL || lrg_text_input_get_focused (input))
			continue;
		v = lrg_blackboard_dup_value_text (bb, keys[i]);
		if (g_strcmp0 (v != NULL ? v : "", lrg_text_input_get_text (input)) != 0)
			lrg_text_input_set_text (input, v != NULL ? v : "");
	}
	self->syncing = FALSE;
}

/* --- decisions ------------------------------------------------------- */

static void
on_topic_chip (LrgWidget *chip,
               gpointer   user_data)
{
	LrgEditorAiDebugPanel *self = LRG_EDITOR_AI_DEBUG_PANEL (user_data);

	lrg_editor_ai_debug_panel_select_topic (self, g_object_get_data (G_OBJECT (chip), "brain"),
	                                        g_object_get_data (G_OBJECT (chip), "topic"));
}

static gchar *
brain_totals (const gchar      *name,
              LrgDecisionBrain *brain)
{
	LrgDecider *d = lrg_decision_brain_get_decider (brain);
	guint       asked = 0, answered = 0, cached = 0, failed = 0;
	gint64      tin = 0, tout = 0;
	gdouble     mean = 0.0;

	lrg_decision_brain_get_totals (brain, &asked, &answered, &cached, &failed, &tin, &tout, &mean);
	return g_strdup_printf ("%s \xc2\xb7 %s%s%s \xc2\xb7 asked %u, answered %u, cached %u, failed %u \xc2\xb7 "
	                        "tokens %" G_GINT64_FORMAT "/%" G_GINT64_FORMAT " \xc2\xb7 mean %.2f s",
	                        name, d != NULL ? lrg_decider_get_provider (d) : "?",
	                        (d != NULL && lrg_decider_get_model (d) != NULL) ? "/" : "",
	                        (d != NULL && lrg_decider_get_model (d) != NULL) ? lrg_decider_get_model (d) : "",
	                        asked, answered, cached, failed, tin, tout, mean);
}

static gchar *
topic_label (LrgDecisionBrain *brain,
             const gchar      *topic)
{
	guint   asked = 0, answered = 0, cached = 0, failed = 0;
	gdouble lat = lrg_decision_brain_get_latency (brain, topic);
	gdouble pend = lrg_decision_brain_get_pending_seconds (brain, topic);

	lrg_decision_brain_get_topic_counts (brain, topic, &asked, &answered, &cached, &failed);
	if (pend >= 0.0)
		return g_strdup_printf ("%s \xc2\xb7 pending %.1f s", topic, pend);
	if (lat >= 0.0)
		return g_strdup_printf ("%s \xc2\xb7 %u/%u \xc2\xb7 %.2f s", topic, answered + cached, asked + cached, lat);
	return g_strdup_printf ("%s \xc2\xb7 %u/%u", topic, answered + cached, asked + cached);
}

static void
sync_brains (LrgEditorAiDebugPanel *self)
{
	AiAppState *st = ai_state (self->app);
	GString    *sig = g_string_new (NULL);
	guint       i;

	/* pending timers tick: the signature carries them at 0.1 s */
	for (i = 0; i < st->brains->len; i++)
	{
		BrainWatch   *w = g_ptr_array_index (st->brains, i);
		g_auto(GStrv) topics = lrg_decision_brain_dup_topics (w->brain);
		guint         k;

		g_string_append_printf (sig, "%s:", w->name);
		for (k = 0; topics != NULL && topics[k] != NULL; k++)
			g_string_append_printf (sig, "%s@%.1f,", topics[k], lrg_decision_brain_get_pending_seconds (w->brain,
			                                                                                           topics[k]));
	}
	g_string_append_printf (sig, "|%u|%s|%s", st->serial, self->sel_brain, self->sel_topic);
	if (g_strcmp0 (sig->str, self->brains_sig) == 0)
	{
		g_string_free (sig, TRUE);
		return;
	}
	g_free (self->brains_sig);
	self->brains_sig = g_string_free (sig, FALSE);

	clear_box (self->brain_rows);
	if (st->brains->len == 0)
		editor_ai_add (self->brain_rows, editor_panel_hint_new ("ai-brains-hint",
		               "No decision brains. A game or tool shows one here with "
		               "lrg_editor_ai_debug_panel_watch_brain()."));
	for (i = 0; i < st->brains->len; i++)
	{
		BrainWatch       *w = g_ptr_array_index (st->brains, i);
		g_autofree gchar *totals = brain_totals (w->name, w->brain);
		g_autofree gchar *lname = g_strdup_printf ("ai-brain-%u", i);
		g_auto(GStrv)     topics = lrg_decision_brain_dup_topics (w->brain);
		LrgWidget        *wrap = LRG_WIDGET (lrg_wrap_box_new ());
		LrgLabel         *label = lrg_label_new (totals);
		guint             k;

		lrg_widget_set_name (LRG_WIDGET (label), lname);
		editor_ai_add (self->brain_rows, LRG_WIDGET (label));
		lrg_container_set_spacing (LRG_CONTAINER (wrap), 4.0f);
		for (k = 0; topics != NULL && topics[k] != NULL; k++)
		{
			g_autofree gchar *text = topic_label (w->brain, topics[k]);
			g_autofree gchar *cname = g_strdup_printf ("ai-topic-%s", topics[k]);
			LrgWidget        *chip = editor_chip_new (cname, lrg_decision_brain_is_busy (w->brain, topics[k])
			                                                 ? LRG_ICON_CLOCK
			                                                 : lrg_decision_brain_get_last_error (w->brain, topics[k])
			                                                   != NULL ? LRG_ICON_WARNING : LRG_ICON_CHECK,
			                                          text, on_topic_chip, self);
			gboolean          sel = g_strcmp0 (self->sel_topic, topics[k]) == 0 &&
			                        (self->sel_brain == NULL || g_strcmp0 (self->sel_brain, w->name) == 0);

			g_object_set_data_full (G_OBJECT (chip), "brain", g_strdup (w->name), g_free);
			g_object_set_data_full (G_OBJECT (chip), "topic", g_strdup (topics[k]), g_free);
			editor_chip_set_checked (chip, sel);
			editor_chip_set_tint (chip, lrg_decision_brain_get_last_error (w->brain, topics[k]) != NULL
			                            ? LRG_THEME_COLOR_DANGER
			                            : lrg_decision_brain_is_busy (w->brain, topics[k]) ? LRG_THEME_COLOR_WARNING
			                                                                               : LRG_THEME_COLOR_SUCCESS);
			editor_ai_add (wrap, chip);
		}
		editor_ai_add (self->brain_rows, wrap);
	}

	/* the selected topic */
	{
		BrainWatch       *w = find_brain (st, self->sel_brain);
		g_autofree gchar *state = (w != NULL && self->sel_topic != NULL)
		                          ? lrg_decision_brain_dup_topic_state (w->brain, self->sel_topic) : NULL;
		const gchar      *req = state != NULL ? lrg_decision_brain_get_request (w->brain, self->sel_topic) : NULL;
		LrgJudgments     *latest = state != NULL ? lrg_decision_brain_get_latest (w->brain, self->sel_topic, NULL) : NULL;
		g_autofree gchar *ljson = latest != NULL ? lrg_judgments_dup_json (latest) : NULL;
		g_autofree gchar *pretty = pretty_json (req);
		g_autofree gchar *pretty_answer = pretty_json (ljson);
		g_autofree gchar *text = NULL;

		if (state != NULL)
		{
			/* the YAML repeats both JSON documents escaped: show them pretty */
			g_auto(GStrv) lines = g_strsplit (state, "\n", -1);
			GString      *t = g_string_new (NULL);
			guint         k;

			for (k = 0; lines[k] != NULL; k++)
				if (*lines[k] != '\0' && !g_str_has_prefix (lines[k], "request:") &&
				    !g_str_has_prefix (lines[k], "latest:") && !g_str_has_prefix (lines[k], "  "))
					g_string_append_printf (t, "%s\n", lines[k]);
			g_string_append_printf (t, "request:\n%s\nanswer:\n%s", pretty != NULL ? pretty : "(none)",
			                        pretty_answer != NULL ? pretty_answer : "(none yet)");
			text = g_string_free (t, FALSE);
		}

		lrg_widget_set_visible (self->topic_lines, state != NULL);
		fill_lines (self->topic_lines, "ai-topic-line", text, 40, TRUE);
	}

	/* the log: newest last */
	{
		GString *s = g_string_new (NULL);
		guint    n = g_queue_get_length (&st->log);
		guint    k;

		for (k = n > LOG_SHOWN ? n - LOG_SHOWN : 0; k < n; k++)
		{
			LogEntry *e = g_queue_peek_nth (&st->log, k);
			gchar    *line = g_strdup_printf ("%7.2f  %s %s %s: %.48s%s", e->time, e->brain, e->kind, e->topic, e->text,
			                                  strlen (e->text) > 48 ? "\xe2\x80\xa6" : "");

			g_string_append_printf (s, "%s\n", line);
			g_free (line);
		}
		if (n == 0)
			g_string_append (s, "(no decisions yet)\n");
		fill_lines (self->log_lines, "ai-log-line", s->str, LOG_SHOWN, TRUE);
		g_string_free (s, TRUE);
	}
}

static void
sync_status (LrgEditorAiDebugPanel *self)
{
	g_autoptr(GPtrArray) all = lrg_editor_ai_debug_panel_list_live (self->app);
	const gchar         *focus = lrg_editor_ai_debug_panel_get_focus (self->app);
	g_autofree gchar    *text = NULL;

	if (play_of (self->app) == NULL)
		text = g_strdup ("Stopped");
	else
		text = g_strdup_printf ("Playing \xc2\xb7 %u agent%s \xc2\xb7 %s", all->len, all->len == 1 ? "" : "s",
		                        focus != NULL ? "picked" : "following the selection");
	if (g_strcmp0 (text, lrg_label_get_text (LRG_LABEL (self->status))) != 0)
		lrg_label_set_text (LRG_LABEL (self->status), text);
}

static void
sync_all (LrgEditorAiDebugPanel *self)
{
	if (self->app == NULL)
		return;
	sync_status (self);
	sync_agents (self);
	sync_agent (self);
	sync_brains (self);
}

static gboolean
lrg_editor_ai_debug_panel_step (LrgWidget *widget,
                                gfloat     delta_time)
{
	LrgEditorAiDebugPanel *self = LRG_EDITOR_AI_DEBUG_PANEL (widget);
	LrgWidgetClass        *parent = LRG_WIDGET_CLASS (lrg_editor_ai_debug_panel_parent_class);
	gboolean               busy = parent->step != NULL && parent->step (widget, delta_time);

	/* a panel made before the app's server started registers late */
	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), INSTALLED_KEY) == NULL)
		lrg_editor_ai_debug_panel_install (self->app);

	/* poll the play world a few times a second; brain signals at once */
	self->poll -= delta_time;
	if (self->poll <= 0.0f || (self->app != NULL && ai_state (self->app)->serial != self->seen_serial))
	{
		self->poll = POLL_SECONDS;
		if (self->app != NULL)
			self->seen_serial = ai_state (self->app)->serial;
		sync_all (self);
	}
	return busy || (self->app != NULL && play_of (self->app) != NULL);
}

static void
lrg_editor_ai_debug_panel_layout_children (LrgContainer *container)
{
	LrgEditorAiDebugPanel *self = LRG_EDITOR_AI_DEBUG_PANEL (container);

	editor_panel_place_column (container, self->top, self->scroll, NULL);
}

static void
on_play_changed (LrgEditorAiDebugPanel *self)
{
	g_clear_pointer (&self->agents_sig, g_free);
	g_clear_pointer (&self->bb_sig, g_free);
	sync_all (self);
}

static void
lrg_editor_ai_debug_panel_dispose (GObject *object)
{
	LrgEditorAiDebugPanel *self = LRG_EDITOR_AI_DEBUG_PANEL (object);

	if (self->play != NULL)
		g_signal_handlers_disconnect_by_data (self->play, self);
	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), PANEL_KEY) == self)
		g_object_set_data (G_OBJECT (self->app), PANEL_KEY, NULL);
	g_clear_object (&self->play);
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_ai_debug_panel_parent_class)->dispose (object);
}

static void
lrg_editor_ai_debug_panel_finalize (GObject *object)
{
	LrgEditorAiDebugPanel *self = LRG_EDITOR_AI_DEBUG_PANEL (object);

	g_hash_table_unref (self->bb_inputs);
	g_free (self->agents_sig);
	g_free (self->agent_sig);
	g_free (self->bb_sig);
	g_free (self->brains_sig);
	g_free (self->sel_brain);
	g_free (self->sel_topic);
	G_OBJECT_CLASS (lrg_editor_ai_debug_panel_parent_class)->finalize (object);
}

static void
lrg_editor_ai_debug_panel_class_init (LrgEditorAiDebugPanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_ai_debug_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_ai_debug_panel_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_ai_debug_panel_step;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_ai_debug_panel_layout_children;
}

static LrgWidget *
vbox_named (const gchar *name,
            gfloat       spacing)
{
	LrgWidget *box = LRG_WIDGET (lrg_vbox_new ());

	lrg_widget_set_name (box, name);
	lrg_container_set_spacing (LRG_CONTAINER (box), spacing);
	return box;
}

static void
lrg_editor_ai_debug_panel_init (LrgEditorAiDebugPanel *self)
{
	LrgWidget *title, *row, *send;

	self->bb_inputs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	/* header: title and play status */
	self->top = vbox_named ("ai-top", 2.0f);
	title = LRG_WIDGET (lrg_label_new ("AI agents and decisions"));
	lrg_widget_add_class (title, "heading");
	editor_ai_add (self->top, title);
	self->status = LRG_WIDGET (lrg_label_new ("Stopped"));
	lrg_widget_set_name (self->status, "ai-status");
	lrg_widget_add_class (self->status, "dim-label");
	editor_ai_add (self->top, self->status);
	lrg_container_add_child (LRG_CONTAINER (self), self->top);
	g_object_unref (self->top);

	/* body */
	self->body = vbox_named ("ai-body", 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (self->body), 2.0f);
	editor_ai_add (self->body, editor_ai_heading ("AGENTS"));
	self->agent_rows = vbox_named ("ai-agents", 3.0f);
	editor_ai_add (self->body, g_object_ref (self->agent_rows));
	g_object_unref (self->agent_rows);

	self->agent_box = vbox_named ("ai-agent", 4.0f);
	editor_ai_add (self->agent_box, editor_ai_heading ("SELECTED AGENT"));
	self->agent_lines = vbox_named ("ai-agent-lines", 1.0f);
	editor_ai_add (self->agent_box, g_object_ref (self->agent_lines));
	g_object_unref (self->agent_lines);
	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (row), 4.0f);
	editor_ai_add (row, editor_ai_button ("ai-open-tree", "Open Tree", G_CALLBACK (on_open_tree), self));
	editor_ai_add (row, editor_ai_button ("ai-open-machine", "Open Machine", G_CALLBACK (on_open_machine), self));
	editor_ai_add (self->agent_box, row);
	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (row), 4.0f);
	self->event_input = lrg_text_input_new_with_placeholder ("event name");
	lrg_widget_set_name (LRG_WIDGET (self->event_input), "ai-event");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->event_input), 1.0f);
	lrg_text_input_set_font_size (self->event_input, 14.0f);
	g_signal_connect (self->event_input, "submitted", G_CALLBACK (on_event_submitted), self);
	editor_ai_add (row, g_object_ref (LRG_WIDGET (self->event_input)));
	g_object_unref (self->event_input);
	send = editor_ai_button ("ai-event-send", "Send Event", G_CALLBACK (on_event_send), self);
	editor_ai_add (row, send);
	editor_ai_add (self->agent_box, row);
	editor_ai_add (self->agent_box, editor_ai_heading ("BLACKBOARD (Enter writes a value)"));
	self->bb_rows = vbox_named ("ai-blackboard", 2.0f);
	editor_ai_add (self->agent_box, g_object_ref (self->bb_rows));
	g_object_unref (self->bb_rows);
	lrg_widget_set_visible (self->agent_box, FALSE);
	editor_ai_add (self->body, g_object_ref (self->agent_box));
	g_object_unref (self->agent_box);

	editor_ai_add (self->body, editor_ai_heading ("DECISIONS"));
	self->brain_rows = vbox_named ("ai-brains", 4.0f);
	editor_ai_add (self->body, g_object_ref (self->brain_rows));
	g_object_unref (self->brain_rows);
	self->topic_lines = vbox_named ("ai-topic", 0.0f);
	lrg_widget_set_visible (self->topic_lines, FALSE);
	editor_ai_add (self->body, g_object_ref (self->topic_lines));
	g_object_unref (self->topic_lines);
	editor_ai_add (self->body, editor_ai_heading ("DECISION LOG"));
	self->log_lines = vbox_named ("ai-log", 0.0f);
	editor_ai_add (self->body, g_object_ref (self->log_lines));
	g_object_unref (self->log_lines);

	self->scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (self->scroll, "ai-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (self->scroll), self->body);
	g_object_unref (self->body);
	lrg_container_add_child (LRG_CONTAINER (self), self->scroll);
	g_object_unref (self->scroll);
}

/* ==========================================================================
 * Actions and IPC
 * ========================================================================== */

static void
a_show (gpointer user_data)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);

	editor_app_show_panel (app, LRG_EDITOR_AI_DEBUG_PANEL_ID);
}

static JsonNode *
m_show (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);
	JsonObject   *o = json_object_new ();

	editor_app_show_panel (app, LRG_EDITOR_AI_DEBUG_PANEL_ID);
	json_object_set_boolean_member (o, "shown", editor_app_panel_is_shown (app, LRG_EDITOR_AI_DEBUG_PANEL_ID));
	return editor_ai_object_node (o);
}

static void
set_nullable_string (JsonObject  *o,
                     const gchar *member,
                     const gchar *value)
{
	if (value != NULL)
		json_object_set_string_member (o, member, value);
	else
		json_object_set_null_member (o, member);
}

static JsonNode *
m_entities (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorApp        *app = LRG_EDITOR_APP (user_data);
	g_autoptr(GPtrArray) all = lrg_editor_ai_debug_panel_list_live (app);
	LrgAiComponent      *focus = lrg_editor_ai_debug_panel_get_focus_component (app);
	JsonObject          *o = json_object_new ();
	JsonArray           *a = json_array_new ();
	guint                i;

	for (i = 0; i < all->len; i++)
	{
		LrgAiComponent   *c = g_ptr_array_index (all, i);
		g_autofree gchar *id = lrg_editor_ai_debug_panel_dup_entity_id (app, c);
		g_autofree gchar *name = display_name (app, c);
		JsonObject       *e = json_object_new ();

		set_nullable_string (e, "entity", id);
		json_object_set_string_member (e, "name", name);
		set_nullable_string (e, "tree", lrg_ai_component_get_tree_path (c));
		set_nullable_string (e, "machine", lrg_ai_component_get_machine_path (c));
		set_nullable_string (e, "current_node", lrg_ai_component_get_current_node (c));
		set_nullable_string (e, "current_state", lrg_ai_component_get_current_state (c));
		json_object_set_boolean_member (e, "running", lrg_ai_component_get_running (c));
		json_object_set_int_member (e, "keys", lrg_blackboard_get_size (lrg_ai_component_get_blackboard (c)));
		json_object_set_boolean_member (e, "focused", c == focus);
		json_array_add_object_element (a, e);
	}
	json_object_set_boolean_member (o, "playing", play_of (app) != NULL);
	set_nullable_string (o, "selected", lrg_editor_ai_debug_panel_get_focus (app));
	json_object_set_array_member (o, "entities", a);
	return editor_ai_object_node (o);
}

static JsonNode *
m_select (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);
	const gchar  *entity = editor_ai_param_string (params, "entity");
	JsonObject   *o;

	if (entity == NULL && !editor_ai_param_is_null (params, "entity"))
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Missing parameter 'entity' (string or null)");
		return NULL;
	}
	/* an id is kept as given; a name is resolved to its id while playing */
	if (entity != NULL)
	{
		LrgAiComponent *c = lrg_editor_ai_debug_panel_lookup (app, entity);

		if (c == NULL && play_of (app) != NULL)
		{
			editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No live AI entity with that id or name");
			return NULL;
		}
		if (c != NULL)
		{
			g_autofree gchar *id = lrg_editor_ai_debug_panel_dup_entity_id (app, c);

			lrg_editor_ai_debug_panel_set_focus (app, id);
		}
		else
			lrg_editor_ai_debug_panel_set_focus (app, entity);
	}
	else
		lrg_editor_ai_debug_panel_set_focus (app, NULL);
	o = json_object_new ();
	set_nullable_string (o, "selected", lrg_editor_ai_debug_panel_get_focus (app));
	return editor_ai_object_node (o);
}

static LrgAiComponent *
ipc_component (LrgEditorApp  *app,
               JsonObject    *params,
               GError       **error)
{
	const gchar    *entity = editor_ai_param_string (params, "entity");
	LrgAiComponent *c;

	if (play_of (app) == NULL)
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_FAILED, "The level is not playing");
		return NULL;
	}
	c = lrg_editor_ai_debug_panel_lookup (app, entity);
	if (c == NULL)
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		                    entity != NULL ? "No live AI entity with that id or name"
		                                   : "No AI entity is focused (ai.select one or select its node)");
	return c;
}

static JsonNode *
m_state (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	LrgEditorApp     *app = LRG_EDITOR_APP (user_data);
	LrgAiComponent   *c = ipc_component (app, params, error);
	LrgBlackboard    *bb;
	g_auto(GStrv)     keys = NULL;
	g_autofree gchar *id = NULL;
	g_autofree gchar *debug = NULL;
	JsonObject       *o;
	JsonArray        *a;
	guint             i;

	if (c == NULL)
		return NULL;
	bb = lrg_ai_component_get_blackboard (c);
	keys = lrg_blackboard_dup_keys (bb);
	id = lrg_editor_ai_debug_panel_dup_entity_id (app, c);
	debug = lrg_ai_component_dup_debug_state (c);
	o = json_object_new ();
	a = json_array_new ();
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		JsonObject       *k = json_object_new ();
		g_autofree gchar *v = lrg_blackboard_dup_value_text (bb, keys[i]);

		json_object_set_string_member (k, "key", keys[i]);
		json_object_set_string_member (k, "type", bb_type_name (lrg_blackboard_get_value_type (bb, keys[i])));
		set_nullable_string (k, "value", v);
		json_array_add_object_element (a, k);
	}
	set_nullable_string (o, "entity", id);
	json_object_set_string_member (o, "debug", debug != NULL ? debug : "");
	json_object_set_array_member (o, "blackboard", a);
	return editor_ai_object_node (o);
}

static JsonNode *
m_set_key (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	const gchar       *key = editor_ai_require_string (params, "key", error);
	const gchar       *type = editor_ai_param_string (params, "type");
	LrgAiComponent    *c;
	JsonNode          *vn;
	g_autofree gchar  *text = NULL;
	g_autofree gchar  *now = NULL;
	g_autoptr(GError)  err = NULL;
	JsonObject        *o;

	if (key == NULL)
		return NULL;
	if (params == NULL || !json_object_has_member (params, "value"))
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Missing parameter 'value'");
		return NULL;
	}
	c = ipc_component (app, params, error);
	if (c == NULL)
		return NULL;
	vn = json_object_get_member (params, "value");
	if (!JSON_NODE_HOLDS_VALUE (vn))
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "'value' must be a scalar");
		return NULL;
	}
	switch (json_node_get_value_type (vn))
	{
	case G_TYPE_BOOLEAN:
		text = g_strdup (json_node_get_boolean (vn) ? "true" : "false");
		if (type == NULL && lrg_blackboard_get_value_type (lrg_ai_component_get_blackboard (c), key) == G_TYPE_INVALID)
			type = "bool";
		break;
	case G_TYPE_INT64:
		text = g_strdup_printf ("%" G_GINT64_FORMAT, json_node_get_int (vn));
		break;
	case G_TYPE_DOUBLE:
		text = editor_ai_num_str (json_node_get_double (vn), TRUE);
		if (type == NULL && lrg_blackboard_get_value_type (lrg_ai_component_get_blackboard (c), key) == G_TYPE_INVALID)
			type = "float";
		break;
	default:
		text = g_strdup (json_node_get_string (vn));
		if (type == NULL && lrg_blackboard_get_value_type (lrg_ai_component_get_blackboard (c), key) == G_TYPE_INVALID)
			type = "string";
		break;
	}
	if (!bb_write (lrg_ai_component_get_blackboard (c), key, text, type, &err))
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	now = lrg_blackboard_dup_value_text (lrg_ai_component_get_blackboard (c), key);
	o = json_object_new ();
	json_object_set_string_member (o, "key", key);
	set_nullable_string (o, "value", now);
	return editor_ai_object_node (o);
}

static JsonNode *
m_send_event (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorApp   *app = LRG_EDITOR_APP (user_data);
	const gchar    *ev = editor_ai_require_string (params, "event", error);
	LrgAiComponent *c;
	JsonObject     *o;

	if (ev == NULL)
		return NULL;
	c = ipc_component (app, params, error);
	if (c == NULL)
		return NULL;
	lrg_ai_component_send_event (c, ev);
	o = json_object_new ();
	json_object_set_boolean_member (o, "sent", lrg_ai_component_get_machine (c) != NULL);
	return editor_ai_object_node (o);
}

static JsonNode *
m_brains (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);
	AiAppState   *st = ai_state (app);
	JsonObject   *o = json_object_new ();
	JsonArray    *a = json_array_new ();
	guint         i;

	for (i = 0; i < st->brains->len; i++)
	{
		BrainWatch   *w = g_ptr_array_index (st->brains, i);
		LrgDecider   *d = lrg_decision_brain_get_decider (w->brain);
		JsonObject   *b = json_object_new ();
		JsonArray    *ta = json_array_new ();
		g_auto(GStrv) topics = lrg_decision_brain_dup_topics (w->brain);
		guint         asked = 0, answered = 0, cached = 0, failed = 0, k;
		gint64        tin = 0, tout = 0;
		gdouble       mean = 0.0;

		lrg_decision_brain_get_totals (w->brain, &asked, &answered, &cached, &failed, &tin, &tout, &mean);
		json_object_set_string_member (b, "name", w->name);
		set_nullable_string (b, "provider", d != NULL ? lrg_decider_get_provider (d) : NULL);
		set_nullable_string (b, "model", d != NULL ? lrg_decider_get_model (d) : NULL);
		json_object_set_int_member (b, "asked", asked);
		json_object_set_int_member (b, "answered", answered);
		json_object_set_int_member (b, "cached", cached);
		json_object_set_int_member (b, "failed", failed);
		json_object_set_int_member (b, "input_tokens", tin);
		json_object_set_int_member (b, "output_tokens", tout);
		json_object_set_double_member (b, "mean_seconds", mean);
		for (k = 0; topics != NULL && topics[k] != NULL; k++)
		{
			JsonObject *t = json_object_new ();
			guint       ta_asked = 0, ta_answered = 0, ta_cached = 0, ta_failed = 0;

			lrg_decision_brain_get_topic_counts (w->brain, topics[k], &ta_asked, &ta_answered, &ta_cached, &ta_failed);
			json_object_set_string_member (t, "topic", topics[k]);
			json_object_set_boolean_member (t, "busy", lrg_decision_brain_is_busy (w->brain, topics[k]));
			json_object_set_double_member (t, "pending_seconds",
			                               lrg_decision_brain_get_pending_seconds (w->brain, topics[k]));
			json_object_set_double_member (t, "latency", lrg_decision_brain_get_latency (w->brain, topics[k]));
			json_object_set_int_member (t, "asked", ta_asked);
			json_object_set_int_member (t, "answered", ta_answered);
			json_object_set_int_member (t, "cached", ta_cached);
			json_object_set_int_member (t, "failed", ta_failed);
			set_nullable_string (t, "error", lrg_decision_brain_get_last_error (w->brain, topics[k]));
			json_array_add_object_element (ta, t);
		}
		json_object_set_array_member (b, "topics", ta);
		json_array_add_object_element (a, b);
	}
	json_object_set_array_member (o, "brains", a);
	return editor_ai_object_node (o);
}

static JsonNode *
m_topic (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	LrgEditorApp     *app = LRG_EDITOR_APP (user_data);
	const gchar      *brain = editor_ai_require_string (params, "brain", error);
	const gchar      *topic = brain != NULL ? editor_ai_require_string (params, "topic", error) : NULL;
	BrainWatch       *w;
	g_autofree gchar *state = NULL;
	JsonObject       *o;

	if (topic == NULL)
		return NULL;
	w = find_brain (ai_state (app), brain);
	if (w == NULL)
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No watched brain of that name");
		return NULL;
	}
	state = lrg_decision_brain_dup_topic_state (w->brain, topic);
	if (state == NULL)
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "The brain has no such topic");
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "brain", w->name);
	json_object_set_string_member (o, "topic", topic);
	json_object_set_string_member (o, "state", state);
	set_nullable_string (o, "request", lrg_decision_brain_get_request (w->brain, topic));
	{
		LrgEditorAiDebugPanel *panel = lrg_editor_ai_debug_panel_find (app);

		if (panel != NULL)
			lrg_editor_ai_debug_panel_select_topic (panel, w->name, topic);
	}
	return editor_ai_object_node (o);
}

static JsonNode *
m_log (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);
	AiAppState   *st = ai_state (app);
	const gchar  *brain = editor_ai_param_string (params, "brain");
	gdouble       limit = 50.0;
	JsonObject   *o = json_object_new ();
	JsonArray    *a = json_array_new ();
	GList        *l;
	guint         n = 0, skip = 0, total = 0;

	editor_ai_param_number (params, "limit", &limit);
	for (l = st->log.head; l != NULL; l = l->next)
		if (brain == NULL || g_strcmp0 (((LogEntry *) l->data)->brain, brain) == 0)
			total++;
	if (limit >= 0.0 && (gdouble) total > limit)
		skip = total - (guint) limit;
	for (l = st->log.head; l != NULL; l = l->next)
	{
		LogEntry   *e = l->data;
		JsonObject *eo;

		if (brain != NULL && g_strcmp0 (e->brain, brain) != 0)
			continue;
		if (n++ < skip)
			continue;
		eo = json_object_new ();
		json_object_set_double_member (eo, "time", e->time);
		json_object_set_string_member (eo, "brain", e->brain);
		json_object_set_string_member (eo, "kind", e->kind);
		json_object_set_string_member (eo, "topic", e->topic);
		json_object_set_string_member (eo, "text", e->text);
		json_array_add_object_element (a, eo);
	}
	json_object_set_array_member (o, "entries", a);
	return editor_ai_object_node (o);
}

void
lrg_editor_ai_debug_panel_install (LrgEditorApp *app)
{
	static const struct { const gchar *name; LrgEditorServerMethod func; } methods[] = {
		{ "ai.show", m_show },
		{ "ai.entities", m_entities },
		{ "ai.select", m_select },
		{ "ai.state", m_state },
		{ "ai.set_key", m_set_key },
		{ "ai.send_event", m_send_event },
		{ "ai.brains", m_brains },
		{ "ai.topic", m_topic },
		{ "ai.log", m_log }
	};
	LrgEditorServer *server;
	guint            i;

	g_return_if_fail (LRG_IS_EDITOR_APP (app));

	if (g_object_get_data (G_OBJECT (app), INSTALLED_KEY) != NULL)
		return;
	server = lrg_editor_app_get_server (app);
	if (server == NULL)
		return;
	g_object_set_data (G_OBJECT (app), INSTALLED_KEY, GINT_TO_POINTER (1));
	ai_state (app);
	for (i = 0; i < G_N_ELEMENTS (methods); i++)
		lrg_editor_server_register_method (server, methods[i].name, methods[i].func, app, NULL);
	lrg_editor_actions_add (lrg_editor_app_get_actions (app), "ai.show", "AI Panel", "View", NULL, a_show, NULL, NULL,
	                        app);
}

/* ==========================================================================
 * Public API: the panel
 * ========================================================================== */

LrgEditorAiDebugPanel *
lrg_editor_ai_debug_panel_new (LrgEditorApp *app)
{
	LrgEditorAiDebugPanel *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_AI_DEBUG_PANEL, NULL);
	lrg_widget_set_name (LRG_WIDGET (self), "ai-panel");
	g_set_weak_pointer (&self->app, app);
	self->play = app->play != NULL ? g_object_ref (app->play) : NULL;
	g_object_set_data (G_OBJECT (app), PANEL_KEY, self);
	if (self->play != NULL)
	{
		g_signal_connect_swapped (self->play, "started", G_CALLBACK (on_play_changed), self);
		g_signal_connect_swapped (self->play, "stopped", G_CALLBACK (on_play_changed), self);
	}
	lrg_editor_ai_debug_panel_install (app);
	sync_all (self);
	return self;
}

LrgEditorAiDebugPanel *
lrg_editor_ai_debug_panel_find (LrgEditorApp *app)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	return g_object_get_data (G_OBJECT (app), PANEL_KEY);
}

const gchar *
lrg_editor_ai_debug_panel_get_selected_topic (LrgEditorAiDebugPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_AI_DEBUG_PANEL (self), NULL);
	return self->sel_topic;
}

void
lrg_editor_ai_debug_panel_select_topic (LrgEditorAiDebugPanel *self,
                                        const gchar           *brain,
                                        const gchar           *topic)
{
	g_return_if_fail (LRG_IS_EDITOR_AI_DEBUG_PANEL (self));

	if (brain == NULL && self->app != NULL)
	{
		BrainWatch *w = find_brain (ai_state (self->app), NULL);

		brain = w != NULL ? w->name : NULL;
	}
	g_free (self->sel_brain);
	g_free (self->sel_topic);
	self->sel_brain = g_strdup (brain);
	self->sel_topic = g_strdup (topic);
	g_clear_pointer (&self->brains_sig, g_free);
	sync_all (self);
}

gchar *
lrg_editor_ai_debug_panel_describe (LrgEditorAiDebugPanel *self)
{
	GString             *s;
	g_autoptr(GPtrArray) all = NULL;
	LrgAiComponent      *focus;
	AiAppState          *st;
	guint                i;

	g_return_val_if_fail (LRG_IS_EDITOR_AI_DEBUG_PANEL (self), NULL);

	s = g_string_new (NULL);
	if (self->app == NULL)
		return g_string_free (s, FALSE);
	all = lrg_editor_ai_debug_panel_list_live (self->app);
	focus = lrg_editor_ai_debug_panel_get_focus_component (self->app);
	g_string_append_printf (s, "%s\n", play_of (self->app) != NULL ? "playing" : "stopped");
	for (i = 0; i < all->len; i++)
	{
		g_autofree gchar *line = agent_line (self->app, g_ptr_array_index (all, i));

		g_string_append_printf (s, "%s %s\n", g_ptr_array_index (all, i) == focus ? "*" : "-", line);
	}
	if (focus != NULL)
	{
		LrgBlackboard *bb = lrg_ai_component_get_blackboard (focus);
		g_auto(GStrv)  keys = lrg_blackboard_dup_keys (bb);

		for (i = 0; keys != NULL && keys[i] != NULL; i++)
		{
			g_autofree gchar *v = lrg_blackboard_dup_value_text (bb, keys[i]);

			g_string_append_printf (s, "  %s = %s\n", keys[i], v != NULL ? v : "");
		}
	}
	st = ai_state (self->app);
	for (i = 0; i < st->brains->len; i++)
	{
		BrainWatch       *w = g_ptr_array_index (st->brains, i);
		g_autofree gchar *totals = brain_totals (w->name, w->brain);

		g_string_append_printf (s, "%s\n", totals);
	}
	return g_string_free (s, FALSE);
}
