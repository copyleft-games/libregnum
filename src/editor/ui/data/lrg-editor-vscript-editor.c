/* lrg-editor-vscript-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorVscriptEditor: the data editor of a `.rvscript` program.
 *
 * The document is the truth. refresh() parses it and rebuilds either the
 * node-graph canvas or the block workspace, whichever the program's
 * `view:` key names. IPC methods edit a duplicate and commit it as one
 * undo step (lrg_editor_document_commit_object(), gesture 0). Run,
 * validate, get, live, select, gi_search and node_types do not commit.
 */

#include "config.h"

#define LRG_VSCRIPT_PRIVATE 1   /* lrg_vscript_priv_types_compatible () */
#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-vscript-editor.h"
#include "../lrg-editor-app-private.h"
#include "../../lrg-editor-server.h"
#include "../../../scripting/lrg-scripting-graph.h"

#include "../../../paint/lrg-paint-list.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-menu.h"
#include "../lrg-editor-console-panel.h"
#include <errno.h>
#include <math.h>
#include <string.h>
#include <json-glib/json-glib.h>

struct _LrgEditorVscriptEditor
{
	LrgEditorDataEditor  parent_instance;
	LrgNodeGraph        *canvas;
	LrgBlockEditor      *blocks;
	LrgVscriptProgram   *program;
	GPtrArray           *selected;
	gchar               *shown;
	gboolean             kinds_ready;
	gboolean             rebuilding;
	gboolean             framed;        /* the shown graph was fitted once */
	gchar               *framed_graph;  /* which graph that was */

	/* canvas -> program: mouse edits are collected and committed from an
	 * idle, one undo step per gesture, never inside a canvas signal */
	guint                sync_idle;
	GHashTable          *dupes;         /* canvas copy id -> original node id */

	/* the Add Node picker */
	LrgMenu             *palette;
	LrgWidget           *add_chip;      /* the toolbar's Add Node chip (the toolbar owns it) */
	gfloat               add_x;         /* where a picked node lands (graph units) */
	gfloat               add_y;
	gchar               *drop_node;     /* a wire dropped on empty canvas came from */
	gchar               *drop_pin;      /*   this pin, which the new node is wired to */
	gboolean             drop_output;
};

G_DEFINE_FINAL_TYPE (LrgEditorVscriptEditor, lrg_editor_vscript_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static const gchar *INSTALLED_KEY = "lrg-editor-vscript-installed";

#define CARD_BODY_W (168.0f)   /* the value line under a card's pins */
#define CARD_BODY_H (20.0f)

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static const gchar *
view_name (LrgVscriptView view)
{
	return view == LRG_VSCRIPT_VIEW_BLOCKS ? "blocks" : "graph";
}

static gboolean
view_from_name (const gchar     *name,
                LrgVscriptView  *out,
                GError         **error)
{
	if (name == NULL || g_strcmp0 (name, "graph") == 0)
	{
		*out = LRG_VSCRIPT_VIEW_GRAPH;
		return TRUE;
	}
	if (g_strcmp0 (name, "blocks") == 0)
	{
		*out = LRG_VSCRIPT_VIEW_BLOCKS;
		return TRUE;
	}
	g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
	             "Unknown view '%s' (graph or blocks)", name);
	return FALSE;
}

static gchar *
abs_path (LrgEditorApp *app,
          const gchar  *path)
{
	if (path == NULL)
		return NULL;
	if (g_path_is_absolute (path) || app == NULL)
		return g_strdup (path);
	return editor_app_project_path (app, path);
}

static const gchar *
param_string (JsonObject  *params,
              const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_STRING)
		return NULL;
	return json_node_get_string (n);
}

static const gchar *
require_string (JsonObject   *params,
                const gchar  *name,
                GError      **error)
{
	const gchar *s = param_string (params, name);

	if (s == NULL || s[0] == '\0')
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Missing string parameter '%s'", name);
	return s;
}

static gboolean
param_number (JsonObject  *params,
              const gchar *name,
              gdouble     *out)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	if (json_node_get_value_type (n) == G_TYPE_STRING || json_node_get_value_type (n) == G_TYPE_BOOLEAN)
		return FALSE;
	*out = json_node_get_double (n);
	return TRUE;
}

static guint
param_uint (JsonObject  *params,
            const gchar *name,
            guint        fallback)
{
	gdouble d;

	if (!param_number (params, name, &d) || d < 0.0)
		return fallback;
	if (d > 1000000.0)
		return 1000000;
	return (guint) d;
}

static gboolean
param_bool (JsonObject  *params,
            const gchar *name,
            gboolean    *out)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_BOOLEAN)
		return FALSE;
	*out = json_node_get_boolean (n);
	return TRUE;
}

static JsonObject *
param_object (JsonObject  *params,
              const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_OBJECT (n))
		return NULL;
	return json_node_get_object (n);
}

static void
fail (GError       **error,
      const GError  *cause)
{
	gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

	if (cause != NULL && (g_error_matches (cause, LRG_VSCRIPT_ERROR, LRG_VSCRIPT_ERROR_NOT_FOUND) ||
	                      g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
	                      g_error_matches (cause, G_FILE_ERROR, G_FILE_ERROR_NOENT) ||
	                      g_error_matches (cause, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND)))
		code = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
	else if (cause != NULL && (g_error_matches (cause, LRG_VSCRIPT_ERROR, LRG_VSCRIPT_ERROR_INVALID) ||
	                           g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT) ||
	                           g_error_matches (cause, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS)))
		code = LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS;
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, cause != NULL ? cause->message : "failed");
}

static JsonNode *
object_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

/* A JSON scalar becomes a floating variant. Null becomes NULL (remove). */
static gboolean
json_to_variant (JsonNode   *node,
                 GVariant  **out,
                 GError    **error)
{
	GType type;

	*out = NULL;
	if (node == NULL || JSON_NODE_HOLDS_NULL (node))
		return TRUE;
	if (!JSON_NODE_HOLDS_VALUE (node))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "A value must be a boolean, number, string or null");
		return FALSE;
	}
	type = json_node_get_value_type (node);
	if (type == G_TYPE_BOOLEAN)
		*out = g_variant_new_boolean (json_node_get_boolean (node));
	else if (type == G_TYPE_STRING)
		*out = g_variant_new_string (json_node_get_string (node));
	else
	{
		gdouble d = json_node_get_double (node);

		if (d >= (gdouble) G_MININT64 && d <= (gdouble) G_MAXINT64)
		{
			gint64 i = (gint64) d;

			if ((gdouble) i == d)
				*out = g_variant_new_int64 (i);
			else
				*out = g_variant_new_double (d);
		}
		else
			*out = g_variant_new_double (d);
	}
	return TRUE;
}

static void
json_set_variant (JsonObject *obj,
                  const gchar *key,
                  GVariant    *value)
{
	if (value == NULL)
	{
		json_object_set_null_member (obj, key);
		return;
	}
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
		json_object_set_boolean_member (obj, key, g_variant_get_boolean (value));
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT32))
		json_object_set_int_member (obj, key, g_variant_get_int32 (value));
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT64))
		json_object_set_int_member (obj, key, g_variant_get_int64 (value));
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_DOUBLE))
		json_object_set_double_member (obj, key, g_variant_get_double (value));
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
		json_object_set_string_member (obj, key, g_variant_get_string (value, NULL));
	else
	{
		gchar *text = g_variant_print (value, FALSE);

		json_object_set_string_member (obj, key, text);
		g_free (text);
	}
}

static gboolean
split_pin (const gchar  *spec,
           gchar       **node,
           gchar       **pin,
           GError      **error)
{
	const gchar *dot;

	*node = NULL;
	*pin = NULL;
	if (spec == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing pin (\"node.pin\")");
		return FALSE;
	}
	dot = strchr (spec, '.');
	if (dot == NULL || dot == spec || dot[1] == '\0')
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Bad pin '%s' (expected \"node.pin\")", spec);
		return FALSE;
	}
	*node = g_strndup (spec, (gsize) (dot - spec));
	*pin = g_strdup (dot + 1);
	return TRUE;
}

static LrgVscriptProgram *
edit_begin (LrgEditorVscriptEditor  *self,
            GError                 **error)
{
	GObject *obj;

	obj = lrg_editor_document_dup_object (lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self)), error);
	if (obj == NULL)
		return NULL;
	if (!LRG_IS_VSCRIPT_PROGRAM (obj))
	{
		g_object_unref (obj);
		g_set_error_literal (error, LRG_VSCRIPT_ERROR, LRG_VSCRIPT_ERROR_INVALID, "Not a visual script");
		return NULL;
	}
	return LRG_VSCRIPT_PROGRAM (obj);
}

static LrgVscriptGraph *
graph_named (LrgVscriptProgram  *program,
             const gchar        *name,
             GError            **error)
{
	LrgVscriptGraph *graph = lrg_vscript_program_get_graph (program, name);

	if (graph == NULL)
		g_set_error (error, LRG_VSCRIPT_ERROR, LRG_VSCRIPT_ERROR_NOT_FOUND, "No subgraph '%s'", name);
	return graph;
}

static void
remember_graph (LrgEditorVscriptEditor *self,
                const gchar            *name)
{
	g_free (self->shown);
	self->shown = (name != NULL && name[0] != '\0') ? g_strdup (name) : NULL;
}

/* ==========================================================================
 * JSON of a program
 * ========================================================================== */

static const gchar *
severity_name (LrgVscriptSeverity severity)
{
	switch (severity)
	{
	case LRG_VSCRIPT_SEVERITY_WARNING: return "warning";
	case LRG_VSCRIPT_SEVERITY_ERROR:   return "error";
	default:                           return "info";
	}
}

static JsonObject *
pin_json (const LrgVscriptPin *pin)
{
	JsonObject *o = json_object_new ();

	json_object_set_string_member (o, "name", pin->name != NULL ? pin->name : "");
	json_object_set_string_member (o, "direction", pin->direction == LRG_VSCRIPT_PIN_OUTPUT ? "output" : "input");
	json_object_set_boolean_member (o, "exec", pin->exec);
	json_object_set_string_member (o, "type", pin->type != NULL ? pin->type : "any");
	return o;
}

static JsonArray *
pins_json (LrgVscriptProgram *program,
           LrgVscriptGraph   *graph,
           LrgVscriptNode    *node)
{
	g_autoptr(GPtrArray) pins = lrg_vscript_program_dup_node_pins (program, graph, node);
	JsonArray           *a = json_array_new ();
	guint                i;

	for (i = 0; pins != NULL && i < pins->len; i++)
		json_array_add_object_element (a, pin_json ((LrgVscriptPin *) g_ptr_array_index (pins, i)));
	return a;
}

static JsonObject *
variant_map_json (gchar   **keys,
                  GVariant *(*get) (gpointer, const gchar *),
                  gpointer   obj)
{
	JsonObject *o = json_object_new ();
	guint       i;

	for (i = 0; keys != NULL && keys[i] != NULL; i++)
		json_set_variant (o, keys[i], get (obj, keys[i]));
	return o;
}

static GVariant *
node_param (gpointer     node,
            const gchar *key)
{
	return lrg_vscript_node_get_param ((LrgVscriptNode *) node, key);
}

static GVariant *
node_input (gpointer     node,
            const gchar *key)
{
	return lrg_vscript_node_get_input ((LrgVscriptNode *) node, key);
}

static JsonArray *
issues_json (GPtrArray *issues)
{
	JsonArray *a = json_array_new ();
	guint      i;

	for (i = 0; issues != NULL && i < issues->len; i++)
	{
		LrgVscriptIssue *issue = (LrgVscriptIssue *) g_ptr_array_index (issues, i);
		JsonObject      *o = json_object_new ();

		json_object_set_string_member (o, "severity", severity_name (issue->severity));
		json_object_set_string_member (o, "graph", issue->graph != NULL ? issue->graph : "");
		if (issue->node != NULL)
			json_object_set_string_member (o, "node", issue->node);
		else
			json_object_set_null_member (o, "node");
		json_object_set_string_member (o, "path", issue->path != NULL ? issue->path : "");
		json_object_set_string_member (o, "message", issue->message != NULL ? issue->message : "");
		json_array_add_object_element (a, o);
	}
	return a;
}

static JsonNode *
program_json (LrgEditorVscriptEditor *self,
              const gchar            *graph_name)
{
	LrgVscriptProgram *program = self->program;
	LrgVscriptGraph   *graph;
	JsonObject        *o = json_object_new ();
	JsonObject        *iface = json_object_new ();
	JsonArray         *nodes = json_array_new ();
	JsonArray         *links = json_array_new ();
	JsonArray         *comments = json_array_new ();
	JsonArray         *selected = json_array_new ();
	JsonArray         *variables = json_array_new ();
	JsonArray         *subgraphs = json_array_new ();
	JsonArray         *in_pins = json_array_new ();
	JsonArray         *out_pins = json_array_new ();
	g_auto(GStrv)      var_names = NULL;
	g_auto(GStrv)      sub_names = NULL;
	g_autoptr(GPtrArray) issues = NULL;
	GPtrArray         *iface_pins;
	const gchar       *first_error = NULL;
	guint              i;
	gboolean           ok;

	graph = lrg_vscript_program_get_graph (program, graph_name);
	if (graph == NULL)
		graph = lrg_vscript_program_get_main (program);

	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_boolean_member (o, "dirty", lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "view", view_name (lrg_vscript_program_get_view (program)));
	json_object_set_string_member (o, "name", lrg_vscript_program_get_name (program));
	json_object_set_string_member (o, "description", lrg_vscript_program_get_description (program));
	json_object_set_int_member (o, "seed", (gint64) lrg_vscript_program_get_seed (program));
	json_object_set_int_member (o, "step_budget", (gint64) lrg_vscript_program_get_step_budget (program));
	json_object_set_double_member (o, "fixed_step", lrg_vscript_program_get_fixed_step (program));
	json_object_set_string_member (o, "graph", graph_name != NULL ? graph_name : "");

	var_names = lrg_vscript_program_dup_variable_names (program);
	for (i = 0; var_names != NULL && var_names[i] != NULL; i++)
	{
		JsonObject *v = json_object_new ();

		json_object_set_string_member (v, "name", var_names[i]);
		json_object_set_string_member (v, "type", lrg_vscript_program_get_variable_type (program, var_names[i]));
		json_set_variant (v, "default", lrg_vscript_program_get_variable_default (program, var_names[i]));
		json_array_add_object_element (variables, v);
	}
	json_object_set_array_member (o, "variables", variables);

	sub_names = lrg_vscript_program_dup_subgraph_names (program);
	for (i = 0; sub_names != NULL && sub_names[i] != NULL; i++)
		json_array_add_string_element (subgraphs, sub_names[i]);
	json_object_set_array_member (o, "subgraphs", subgraphs);

	iface_pins = lrg_vscript_graph_get_interface (graph, LRG_VSCRIPT_PIN_INPUT);
	for (i = 0; iface_pins != NULL && i < iface_pins->len; i++)
		json_array_add_object_element (in_pins, pin_json ((LrgVscriptPin *) g_ptr_array_index (iface_pins, i)));
	iface_pins = lrg_vscript_graph_get_interface (graph, LRG_VSCRIPT_PIN_OUTPUT);
	for (i = 0; iface_pins != NULL && i < iface_pins->len; i++)
		json_array_add_object_element (out_pins, pin_json ((LrgVscriptPin *) g_ptr_array_index (iface_pins, i)));
	json_object_set_array_member (iface, "inputs", in_pins);
	json_object_set_array_member (iface, "outputs", out_pins);
	json_object_set_object_member (o, "interface", iface);

	for (i = 0; i < lrg_vscript_graph_get_n_nodes (graph); i++)
	{
		LrgVscriptNode *node = lrg_vscript_graph_get_nth_node (graph, i);
		const gchar    *kind = lrg_vscript_node_get_kind (node);
		const gchar    *title = lrg_vscript_node_type_get_title (kind);
		g_auto(GStrv)   params = lrg_vscript_node_dup_param_keys (node);
		g_auto(GStrv)   inputs = lrg_vscript_node_dup_input_keys (node);
		JsonObject     *n = json_object_new ();
		gdouble         x = 0.0;
		gdouble         y = 0.0;

		lrg_vscript_node_get_position (node, &x, &y);
		json_object_set_string_member (n, "id", lrg_vscript_node_get_id (node));
		json_object_set_string_member (n, "type", kind);
		json_object_set_string_member (n, "title", title != NULL ? title : kind);
		json_object_set_double_member (n, "x", x);
		json_object_set_double_member (n, "y", y);
		json_object_set_object_member (n, "params", variant_map_json (params, node_param, node));
		json_object_set_object_member (n, "inputs", variant_map_json (inputs, node_input, node));
		json_object_set_array_member (n, "pins", pins_json (program, graph, node));
		json_array_add_object_element (nodes, n);
	}
	json_object_set_array_member (o, "nodes", nodes);

	{
		GPtrArray *wire = lrg_vscript_graph_get_links (graph);

		for (i = 0; wire != NULL && i < wire->len; i++)
		{
			LrgVscriptLink *link = (LrgVscriptLink *) g_ptr_array_index (wire, i);
			JsonObject     *l = json_object_new ();
			gchar          *from = g_strdup_printf ("%s.%s", link->from_node, link->from_pin);
			gchar          *to = g_strdup_printf ("%s.%s", link->to_node, link->to_pin);

			json_object_set_string_member (l, "from", from);
			json_object_set_string_member (l, "to", to);
			g_free (from);
			g_free (to);
			json_array_add_object_element (links, l);
		}
	}
	json_object_set_array_member (o, "links", links);

	{
		GPtrArray *frames = lrg_vscript_graph_get_comments (graph);

		for (i = 0; frames != NULL && i < frames->len; i++)
		{
			LrgVscriptComment *cmt = (LrgVscriptComment *) g_ptr_array_index (frames, i);
			JsonObject        *c = json_object_new ();

			json_object_set_string_member (c, "id", cmt->id != NULL ? cmt->id : "");
			json_object_set_string_member (c, "text", cmt->text != NULL ? cmt->text : "");
			json_object_set_double_member (c, "x", cmt->x);
			json_object_set_double_member (c, "y", cmt->y);
			json_object_set_double_member (c, "width", cmt->width);
			json_object_set_double_member (c, "height", cmt->height);
			if (cmt->color != NULL)
				json_object_set_string_member (c, "color", cmt->color);
			else
				json_object_set_null_member (c, "color");
			json_array_add_object_element (comments, c);
		}
	}
	json_object_set_array_member (o, "comments", comments);

	for (i = 0; i < self->selected->len; i++)
		json_array_add_string_element (selected, (const gchar *) g_ptr_array_index (self->selected, i));
	json_object_set_array_member (o, "selected", selected);

	ok = lrg_vscript_program_validate (program, &issues);
	json_object_set_array_member (o, "issues", issues_json (issues));
	for (i = 0; !ok && issues != NULL && i < issues->len; i++)
	{
		LrgVscriptIssue *issue = (LrgVscriptIssue *) g_ptr_array_index (issues, i);

		if (issue->severity == LRG_VSCRIPT_SEVERITY_ERROR)
		{
			first_error = issue->message;
			break;
		}
	}
	if (first_error != NULL)
		json_object_set_string_member (o, "error", first_error);
	return object_node (o);
}

/* ==========================================================================
 * The two views
 * ========================================================================== */

static LrgBlockShape
block_shape (const gchar *kind)
{
	if (lrg_vscript_node_type_is_event (kind))
		return LRG_BLOCK_SHAPE_HAT;
	if (lrg_vscript_node_type_is_pure (kind))
	{
		if (g_str_has_prefix (kind, "logic.") || g_str_has_prefix (kind, "compare."))
			return LRG_BLOCK_SHAPE_BOOLEAN;
		return LRG_BLOCK_SHAPE_REPORTER;
	}
	return LRG_BLOCK_SHAPE_STATEMENT;
}

static gchar *
plain_label (const gchar *text)
{
	gchar *s = g_strdup (text != NULL ? text : "node");
	gchar *p;

	for (p = s; *p != '\0'; p++)
	{
		if (*p == '{' || *p == '}' || *p == '|')
			*p = ' ';
	}
	return s;
}

static void
define_kind (LrgEditorVscriptEditor *self,
             const gchar            *kind)
{
	const gchar *title = lrg_vscript_node_type_get_title (kind);
	const gchar *category = lrg_vscript_node_type_get_category (kind);
	gchar       *label;

	if (kind == NULL || lrg_block_editor_get_kind (self->blocks, kind) != NULL)
		return;
	label = plain_label (title != NULL ? title : kind);
	lrg_block_editor_define_kind (self->blocks, kind, block_shape (kind),
	                              category != NULL ? category : "Script", label, NULL);
	g_free (label);
}

static void
ensure_kinds (LrgEditorVscriptEditor *self)
{
	g_auto(GStrv) types = NULL;
	guint         i;

	if (self->kinds_ready)
		return;
	self->kinds_ready = TRUE;
	types = lrg_vscript_node_types_list ();
	for (i = 0; types != NULL && types[i] != NULL; i++)
		define_kind (self, types[i]);
}

/*
 * on_can_connect:
 *
 * The canvas asks before it draws a link (and before a drag may land).
 * Its default rule only knows equal types or an untyped port; a visual
 * script also widens int to float and lets "any" and "object" meet
 * anything, so the program's own rule answers. Exec pins only meet exec
 * pins.
 */
static gboolean
on_can_connect (LrgNodeGraph *graph,
                LrgGraphPort *output,
                LrgGraphPort *input,
                gpointer      user_data)
{
	(void) graph;
	(void) user_data;
	return lrg_vscript_priv_types_compatible (lrg_graph_port_get_type_name (output),
	                                          lrg_graph_port_get_type_name (input));
}

/* value_text: a literal as a card shows it (strings quoted) */
static gchar *
value_text (GVariant *value)
{
	if (value == NULL)
		return g_strdup ("null");
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
		return g_strdup_printf ("\xe2\x80\x9c%s\xe2\x80\x9d", g_variant_get_string (value, NULL));
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_DOUBLE))
		return g_strdup_printf ("%g", g_variant_get_double (value));
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
		return g_strdup (g_variant_get_boolean (value) ? "true" : "false");
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_VARIANT))
	{
		g_autoptr(GVariant) inner = g_variant_get_variant (value);

		return value_text (inner);
	}
	return g_variant_print (value, FALSE);
}

/* input_linked: whether a link feeds this input (its literal is unused) */
static gboolean
input_linked (LrgVscriptGraph *graph,
              const gchar     *node_id,
              const gchar     *pin)
{
	GPtrArray *wire = lrg_vscript_graph_get_links (graph);
	guint      i;

	for (i = 0; wire != NULL && i < wire->len; i++)
	{
		LrgVscriptLink *link = (LrgVscriptLink *) g_ptr_array_index (wire, i);

		if (g_strcmp0 (link->to_node, node_id) == 0 && g_strcmp0 (link->to_pin, pin) == 0)
			return TRUE;
	}
	return FALSE;
}

/*
 * node_summary:
 *
 * The one line a card shows under its pins: its parameters, then the
 * literal of every input no link feeds. A "name" or "value" parameter
 * is shown bare (Get Variable reads "health", a Float is "5"); other
 * entries are "key: value". Returns %NULL when there is nothing to show.
 */
static gchar *
node_summary (LrgVscriptGraph *graph,
              LrgVscriptNode  *node)
{
	g_auto(GStrv)      params = lrg_vscript_node_dup_param_keys (node);
	g_auto(GStrv)      inputs = lrg_vscript_node_dup_input_keys (node);
	g_autoptr(GString) line = g_string_new (NULL);
	const gchar       *id = lrg_vscript_node_get_id (node);
	guint              i;

	/* --- parameters --- */
	for (i = 0; params != NULL && params[i] != NULL; i++)
	{
		g_autofree gchar *text = value_text (lrg_vscript_node_get_param (node, params[i]));

		if (line->len > 0)
			g_string_append (line, "  \xc2\xb7  ");
		if (g_strcmp0 (params[i], "name") == 0 || g_strcmp0 (params[i], "value") == 0)
			g_string_append (line, text);
		else
			g_string_append_printf (line, "%s: %s", params[i], text);
	}

	/* --- literals of unlinked inputs --- */
	for (i = 0; inputs != NULL && inputs[i] != NULL; i++)
	{
		g_autofree gchar *text = NULL;

		if (input_linked (graph, id, inputs[i]))
			continue;
		text = value_text (lrg_vscript_node_get_input (node, inputs[i]));
		if (line->len > 0)
			g_string_append (line, "  \xc2\xb7  ");
		g_string_append_printf (line, "%s = %s", inputs[i], text);
	}
	return line->len > 0 ? g_string_free (g_steal_pointer (&line), FALSE) : NULL;
}

/* summary_paint: one muted line, ellipsized to the body width */
static void
summary_paint (LrgCustomWidget *widget,
               LrgPaintList    *list,
               gfloat           x,
               gfloat           y,
               gfloat           width,
               gfloat           height,
               gpointer         user_data)
{
	LrgTheme          *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont           *font = lrg_theme_get_ui_font (theme);
	gfloat             fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const gchar       *text = (const gchar *) user_data;
	g_autoptr(GString) shown = g_string_new (text != NULL ? text : "");
	gfloat             w = 0.0f;

	/* --- drop characters from the end until the line plus an ellipsis
	 *     fits (the whole line when it fits as it is) --- */
	lrg_paint_measure_text (font, shown->str, fs, 0.0f, &w, NULL);
	if (w > width - 4.0f)
	{
		do
		{
			const gchar *last = g_utf8_find_prev_char (shown->str, shown->str + shown->len);

			g_string_truncate (shown, last != NULL ? (gsize) (last - shown->str) : 0);
			while (shown->len > 0 && shown->str[shown->len - 1] == ' ')
				g_string_truncate (shown, shown->len - 1);
			g_string_append (shown, "\xe2\x80\xa6");
			lrg_paint_measure_text (font, shown->str, fs, 0.0f, &w, NULL);
			if (w <= width - 4.0f || shown->len <= strlen ("\xe2\x80\xa6"))
				break;
			g_string_truncate (shown, shown->len - strlen ("\xe2\x80\xa6"));
		}
		while (shown->len > 0);
	}
	lrg_paint_list_push_clip (list, x, y, width, height);
	lrg_paint_list_text (list, font, shown->str, x + 2.0f, y + (height - fs) * 0.5f, fs, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
	lrg_paint_list_pop_clip (list);
}

/* summary_new: the body widget carrying @text (copied) */
static LrgWidget *
summary_new (const gchar *text)
{
	static const LrgCustomWidgetFuncs funcs = { NULL, summary_paint, NULL, NULL, { NULL } };
	LrgCustomWidget                  *body = lrg_custom_widget_new ();

	lrg_custom_widget_set_preferred_width (body, CARD_BODY_W);
	lrg_custom_widget_set_preferred_height (body, CARD_BODY_H);
	lrg_custom_widget_set_funcs (body, &funcs, g_strdup (text), g_free);
	lrg_widget_set_pointer_transparent (LRG_WIDGET (body), TRUE);
	return LRG_WIDGET (body);
}

static void
rebuild_canvas (LrgEditorVscriptEditor *self,
                LrgVscriptGraph        *graph)
{
	GPtrArray *frames;
	guint      i;

	lrg_node_graph_clear (self->canvas);

	/* --- comment frames first: the canvas draws them under the cards --- */
	frames = lrg_vscript_graph_get_comments (graph);
	for (i = 0; frames != NULL && i < frames->len; i++)
	{
		LrgVscriptComment *c = (LrgVscriptComment *) g_ptr_array_index (frames, i);

		lrg_node_graph_add_note (self->canvas, c->id, c->text, (gfloat) c->x, (gfloat) c->y,
		                         (gfloat) c->width, (gfloat) c->height, c->color);
	}

	for (i = 0; i < lrg_vscript_graph_get_n_nodes (graph); i++)
	{
		LrgVscriptNode       *node = lrg_vscript_graph_get_nth_node (graph, i);
		const gchar          *kind = lrg_vscript_node_get_kind (node);
		const gchar          *title = lrg_vscript_node_type_get_title (kind);
		g_autoptr(GPtrArray)  pins = NULL;
		LrgGraphNode         *card;
		gdouble               x = 0.0;
		gdouble               y = 0.0;
		guint                 p;

		lrg_vscript_node_get_position (node, &x, &y);
		card = lrg_node_graph_add_node (self->canvas, lrg_vscript_node_get_id (node),
		                                title != NULL ? title : kind, (gfloat) x, (gfloat) y);
		if (card == NULL)
			continue;

		/* --- the node's parameters and literals, under its pins --- */
		{
			g_autofree gchar *summary = node_summary (graph, node);

			if (summary != NULL)
			{
				LrgWidget *body = summary_new (summary);

				lrg_graph_node_set_body (card, body);
				g_object_unref (body);
				lrg_graph_node_set_min_width (card, CARD_BODY_W + 24.0f);
			}
		}
		pins = lrg_vscript_program_dup_node_pins (self->program, graph, node);
		for (p = 0; pins != NULL && p < pins->len; p++)
		{
			LrgVscriptPin *pin = (LrgVscriptPin *) g_ptr_array_index (pins, p);

			LrgGraphPort  *port;
			gboolean       out = pin->direction == LRG_VSCRIPT_PIN_OUTPUT;

			port = lrg_graph_node_add_port (card, out ? LRG_GRAPH_PORT_OUTPUT : LRG_GRAPH_PORT_INPUT,
			                                pin->name, pin->type);

			/* the program's link rule: a data input and an exec output
			 * hold one link (a new wire replaces the old one); a data
			 * output and an exec input take any number */
			lrg_graph_port_set_multi_link (port, pin->exec ? !out : out);
		}
	}
	{
		GPtrArray *wire = lrg_vscript_graph_get_links (graph);

		for (i = 0; wire != NULL && i < wire->len; i++)
		{
			LrgVscriptLink    *link = (LrgVscriptLink *) g_ptr_array_index (wire, i);
			g_autoptr(GError)  err = NULL;

			/* the program already accepted this link: a refusal here is
			 * a canvas/program rule mismatch and must not hide silently */
			if (lrg_node_graph_connect_by_name (self->canvas, link->from_node, link->from_pin,
			                                    link->to_node, link->to_pin, &err) == NULL)
				g_warning ("vscript canvas: link %s.%s -> %s.%s not drawn: %s", link->from_node,
				           link->from_pin, link->to_node, link->to_pin, err->message);
		}
	}
	lrg_node_graph_unselect_all (self->canvas);
	for (i = 0; i < self->selected->len; i++)
	{
		LrgGraphNode *card = lrg_node_graph_get_node (self->canvas,
		                                              (const gchar *) g_ptr_array_index (self->selected, i));

		if (card != NULL)
			lrg_node_graph_select_node (self->canvas, card, TRUE);
	}
}

static void
rebuild_blocks (LrgEditorVscriptEditor *self,
                LrgVscriptGraph        *graph)
{
	guint i;

	ensure_kinds (self);
	lrg_block_editor_clear_program (self->blocks);
	for (i = 0; i < lrg_vscript_graph_get_n_nodes (graph); i++)
	{
		LrgVscriptNode *node = lrg_vscript_graph_get_nth_node (graph, i);
		gdouble         x = 0.0;
		gdouble         y = 0.0;

		define_kind (self, lrg_vscript_node_get_kind (node));
		lrg_vscript_node_get_position (node, &x, &y);
		lrg_block_editor_create_block (self->blocks, lrg_vscript_node_get_kind (node), (gfloat) x, (gfloat) y);
	}
}

static void
vscript_refresh (LrgEditorDataEditor *editor,
                 const gchar         *path)
{
	LrgEditorVscriptEditor *self = LRG_EDITOR_VSCRIPT_EDITOR (editor);
	g_autoptr(GError)       error = NULL;
	g_autoptr(GObject)      obj = NULL;
	LrgVscriptGraph        *graph;
	LrgWidget              *content;
	guint                   i;

	(void) path;
	if (self->rebuilding || self->canvas == NULL)
		return;
	self->rebuilding = TRUE;
	obj = lrg_editor_document_dup_object (lrg_editor_data_editor_get_document (editor), &error);
	if (obj == NULL || !LRG_IS_VSCRIPT_PROGRAM (obj))
	{
		self->rebuilding = FALSE;
		return;
	}
	g_set_object (&self->program, LRG_VSCRIPT_PROGRAM (obj));
	graph = lrg_vscript_program_get_graph (self->program, self->shown);
	if (graph == NULL)
		graph = lrg_vscript_program_get_main (self->program);
	for (i = 0; i < self->selected->len;)
	{
		if (lrg_vscript_graph_get_node (graph, (const gchar *) g_ptr_array_index (self->selected, i)) == NULL)
			g_ptr_array_remove_index (self->selected, i);
		else
			i++;
	}
	if (lrg_vscript_program_get_view (self->program) == LRG_VSCRIPT_VIEW_BLOCKS)
	{
		rebuild_blocks (self, graph);
		content = LRG_WIDGET (self->blocks);
	}
	else
	{
		const gchar *name = self->shown != NULL ? self->shown : "";

		rebuild_canvas (self, graph);
		content = LRG_WIDGET (self->canvas);

		/* --- fit the graph the first time it is shown (open, or a
		 *     switch to another subgraph); later edits keep the view --- */
		if (!self->framed || g_strcmp0 (self->framed_graph, name) != 0)
		{
			self->framed = TRUE;
			g_free (self->framed_graph);
			self->framed_graph = g_strdup (name);
			lrg_node_graph_frame_all (self->canvas, FALSE);
		}
	}
	lrg_editor_data_editor_set_content (editor, content);
	self->rebuilding = FALSE;
}

/* ==========================================================================
 * Canvas -> program
 *
 * The canvas changes itself as the user drags (a moved card, a new or
 * deleted wire, deleted or duplicated cards). Each change only marks the
 * canvas as ahead of the program; an idle then compares the two and
 * commits every difference as one undo step. Diffing instead of replaying
 * each signal keeps one gesture (Delete on a selection fires a removal per
 * card and per wire) one step, does not depend on signal order, and never
 * rebuilds the canvas from inside one of its own signals.
 * ========================================================================== */

static gboolean commit_edit (LrgEditorVscriptEditor *self,
                             LrgVscriptProgram      *program,
                             const gchar            *label,
                             const gchar            *graph,
                             GError                **error);

/* link_key: "from.pin>to.pin", the identity of a wire on either side */
static gchar *
link_key (const gchar *from_node,
          const gchar *from_pin,
          const gchar *to_node,
          const gchar *to_pin)
{
	return g_strdup_printf ("%s.%s>%s.%s", from_node, from_pin, to_node, to_pin);
}

/* copy_node_values: the duplicate gets the original's parameters and literals */
static void
copy_node_values (LrgVscriptNode *from,
                  LrgVscriptNode *to)
{
	g_auto(GStrv) params = lrg_vscript_node_dup_param_keys (from);
	g_auto(GStrv) inputs = lrg_vscript_node_dup_input_keys (from);
	guint         i;

	for (i = 0; params != NULL && params[i] != NULL; i++)
		lrg_vscript_node_set_param (to, params[i], lrg_vscript_node_get_param (from, params[i]));
	for (i = 0; inputs != NULL && inputs[i] != NULL; i++)
		lrg_vscript_node_set_input (to, inputs[i], lrg_vscript_node_get_input (from, inputs[i]));
}

/* connect_key: connects one "from.pin>to.pin" wire through the program's checks */
static gboolean
connect_key (LrgVscriptProgram  *program,
             LrgVscriptGraph    *graph,
             const gchar        *key,
             GError            **error)
{
	g_auto(GStrv)  ends = g_strsplit (key, ">", 2);
	gchar         *from_dot;
	gchar         *to_dot;

	if (ends[0] == NULL || ends[1] == NULL)
		return FALSE;
	from_dot = strchr (ends[0], '.');
	to_dot = strchr (ends[1], '.');
	if (from_dot == NULL || to_dot == NULL)
		return FALSE;
	*from_dot = '\0';
	*to_dot = '\0';
	return lrg_vscript_program_connect (program, graph, ends[0], from_dot + 1, ends[1], to_dot + 1, error);
}

/*
 * sync_from_canvas:
 *
 * Applies the canvas to a copy of the program, in four passes: cards
 * that are gone remove their nodes; moved cards move their nodes; cards
 * the canvas duplicated become new nodes (kind, parameters and literals
 * of the original, a free id); then the wires: program links with no
 * wire are removed and wires with no link are connected through the
 * program's checks (a refused wire is reported and disappears on the
 * rebuild). Commits when anything changed; otherwise the canvas is
 * rebuilt from the program so a refused edit does not linger.
 */
static void
sync_from_canvas (LrgEditorVscriptEditor *self)
{
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(GHashTable)        ids = NULL;    /* canvas card id -> program node id */
	g_autoptr(GHashTable)        wires = NULL;  /* canvas wire keys, program ids */
	g_autoptr(GError)            error = NULL;
	LrgVscriptGraph             *graph;
	GPtrArray                   *links;
	GHashTableIter               iter;
	gpointer                     key;
	const gchar                 *label = NULL;
	gboolean                     changed = FALSE;
	guint                        n;
	gint                         i;

	program = edit_begin (self, &error);
	if (program == NULL)
	{
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return;
	}
	graph = lrg_vscript_program_get_graph (program, self->shown);
	if (graph == NULL)
		graph = lrg_vscript_program_get_main (program);
	ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	/* --- 1, 2: removed and moved cards --- */
	for (i = (gint) lrg_vscript_graph_get_n_nodes (graph) - 1; i >= 0; i--)
	{
		LrgVscriptNode   *node = lrg_vscript_graph_get_nth_node (graph, (guint) i);
		g_autofree gchar *id = g_strdup (lrg_vscript_node_get_id (node));
		LrgGraphNode     *card = lrg_node_graph_get_node (self->canvas, id);
		gdouble           x = 0.0;
		gdouble           y = 0.0;

		if (card == NULL)
		{
			lrg_vscript_graph_remove_node (graph, id);
			label = "Delete Nodes";
			changed = TRUE;
			continue;
		}
		g_hash_table_insert (ids, g_strdup (id), g_strdup (id));
		lrg_vscript_node_get_position (node, &x, &y);
		if (fabs (x - lrg_graph_node_get_x (card)) > 0.25 || fabs (y - lrg_graph_node_get_y (card)) > 0.25)
		{
			lrg_vscript_node_set_position (node, lrg_graph_node_get_x (card), lrg_graph_node_get_y (card));
			if (label == NULL)
				label = "Move Nodes";
			changed = TRUE;
		}
	}

	/* --- 3: duplicated cards --- */
	n = lrg_node_graph_get_n_nodes (self->canvas);
	for (i = 0; i < (gint) n; i++)
	{
		LrgGraphNode     *card = lrg_node_graph_get_nth_node (self->canvas, (guint) i);
		const gchar      *cid = lrg_graph_node_get_id (card);
		const gchar      *orig_id;
		LrgVscriptNode   *orig;
		LrgVscriptNode   *copy;
		g_autofree gchar *nid = NULL;

		if (g_hash_table_contains (ids, cid))
			continue;
		orig_id = g_hash_table_lookup (self->dupes, cid);
		orig = orig_id != NULL ? lrg_vscript_graph_get_node (graph, orig_id) : NULL;
		if (orig == NULL)
			continue;
		nid = lrg_vscript_graph_dup_free_id (graph, orig_id);
		copy = lrg_vscript_graph_create_node (graph, lrg_vscript_node_get_kind (orig), nid,
		                                      lrg_graph_node_get_x (card), lrg_graph_node_get_y (card), NULL);
		if (copy == NULL)
			continue;
		copy_node_values (orig, copy);
		g_hash_table_insert (ids, g_strdup (cid), g_strdup (nid));
		label = "Duplicate Nodes";
		changed = TRUE;
	}
	g_hash_table_remove_all (self->dupes);

	/* --- 4: wires: the canvas's, in program ids --- */
	wires = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	n = lrg_node_graph_get_n_links (self->canvas);
	for (i = 0; i < (gint) n; i++)
	{
		LrgGraphLink *wire = lrg_node_graph_get_nth_link (self->canvas, (guint) i);
		LrgGraphPort *out = lrg_graph_link_get_output (wire);
		LrgGraphPort *in = lrg_graph_link_get_input (wire);
		const gchar  *a = g_hash_table_lookup (ids, lrg_graph_node_get_id (lrg_graph_port_get_node (out)));
		const gchar  *b = g_hash_table_lookup (ids, lrg_graph_node_get_id (lrg_graph_port_get_node (in)));

		if (a != NULL && b != NULL)
			g_hash_table_add (wires, link_key (a, lrg_graph_port_get_name (out), b, lrg_graph_port_get_name (in)));
	}

	/* program links without a wire go; links on both sides stay */
	links = lrg_vscript_graph_get_links (graph);
	for (i = links != NULL ? (gint) links->len - 1 : -1; i >= 0; i--)
	{
		LrgVscriptLink   *link = (LrgVscriptLink *) g_ptr_array_index (links, (guint) i);
		g_autofree gchar *lk = link_key (link->from_node, link->from_pin, link->to_node, link->to_pin);
		g_autofree gchar *fn = NULL;
		g_autofree gchar *fp = NULL;
		g_autofree gchar *tn = NULL;
		g_autofree gchar *tp = NULL;

		if (g_hash_table_remove (wires, lk))
			continue;
		fn = g_strdup (link->from_node);
		fp = g_strdup (link->from_pin);
		tn = g_strdup (link->to_node);
		tp = g_strdup (link->to_pin);
		lrg_vscript_graph_remove_link (graph, fn, fp, tn, tp);
		if (label == NULL)
			label = "Disconnect";
		changed = TRUE;
	}

	/* what is left exists only on the canvas: connect it */
	g_hash_table_iter_init (&iter, wires);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		g_autoptr(GError) err = NULL;

		if (connect_key (program, graph, (const gchar *) key, &err))
		{
			if (label == NULL || g_strcmp0 (label, "Disconnect") == 0)
				label = "Connect";
			changed = TRUE;
		}
		else
			lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), err);
	}

	/* --- commit (its refresh rebuilds the canvas), or put the canvas back --- */
	if (changed)
	{
		g_clear_error (&error);
		if (!commit_edit (self, program, label, self->shown, &error))
			lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	}
	else
		vscript_refresh (LRG_EDITOR_DATA_EDITOR (self), NULL);
}

static gboolean
sync_idle_cb (gpointer user_data)
{
	LrgEditorVscriptEditor *self = LRG_EDITOR_VSCRIPT_EDITOR (user_data);

	self->sync_idle = 0;
	sync_from_canvas (self);
	return G_SOURCE_REMOVE;
}

/* queue_sync: the canvas moved ahead of the program; catch up when idle */
static void
queue_sync (LrgEditorVscriptEditor *self)
{
	if (self->rebuilding || self->sync_idle != 0)
		return;
	self->sync_idle = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, sync_idle_cb, g_object_ref (self),
	                                   g_object_unref);
}

/*
 * lrg_editor_vscript_editor_flush: (documented in the header)
 * @self: a #LrgEditorVscriptEditor
 *
 * Commits pending canvas edits now instead of from the idle.
 */
void
lrg_editor_vscript_editor_flush (LrgEditorVscriptEditor *self)
{
	g_return_if_fail (LRG_IS_EDITOR_VSCRIPT_EDITOR (self));

	if (self->sync_idle == 0)
		return;
	g_clear_handle_id (&self->sync_idle, g_source_remove);
	sync_from_canvas (self);
}

static void
on_canvas_node (LrgNodeGraph *graph,
                LrgGraphNode *card,
                gpointer      user_data)
{
	(void) graph;
	(void) card;
	queue_sync (LRG_EDITOR_VSCRIPT_EDITOR (user_data));
}

static void
on_canvas_link (LrgNodeGraph *graph,
                LrgGraphLink *link,
                gpointer      user_data)
{
	(void) graph;
	(void) link;
	queue_sync (LRG_EDITOR_VSCRIPT_EDITOR (user_data));
}

/* a duplicate is remembered so the sync knows what it copies */
static void
on_canvas_duplicated (LrgNodeGraph *graph,
                      LrgGraphNode *original,
                      LrgGraphNode *copy,
                      gpointer      user_data)
{
	LrgEditorVscriptEditor *self = LRG_EDITOR_VSCRIPT_EDITOR (user_data);

	(void) graph;
	if (self->rebuilding)
		return;
	g_hash_table_insert (self->dupes, g_strdup (lrg_graph_node_get_id (copy)),
	                     g_strdup (lrg_graph_node_get_id (original)));
	queue_sync (self);
}

/* the canvas selection is the editor's (not an undo step) */
static void
on_canvas_selection (LrgNodeGraph *graph,
                     gpointer      user_data)
{
	LrgEditorVscriptEditor *self = LRG_EDITOR_VSCRIPT_EDITOR (user_data);
	GList                  *sel;
	GList                  *l;

	if (self->rebuilding)
		return;
	sel = lrg_node_graph_get_selected_nodes (graph);
	g_ptr_array_set_size (self->selected, 0);
	for (l = sel; l != NULL; l = l->next)
		g_ptr_array_add (self->selected, g_strdup (lrg_graph_node_get_id (LRG_GRAPH_NODE (l->data))));
	g_list_free (sel);
}

/* ==========================================================================
 * The Add Node picker
 *
 * One menu, a submenu per catalogue category, an entry per kind; each
 * entry activates vscript.add-node with the kind. The toolbar's Add Node
 * opens it and the node lands at the centre of the view; a wire dropped
 * on empty canvas opens it at the drop point, and the new node is wired
 * to the pin the wire came from (the first compatible pin, exec first).
 * ========================================================================== */

static GMenuModel *
palette_model (void)
{
	g_auto(GStrv)          types = lrg_vscript_node_types_list ();
	g_autoptr(GHashTable)  submenus = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);
	g_autoptr(GPtrArray)   order = g_ptr_array_new ();
	GMenu                 *menu = g_menu_new ();
	guint                  i;

	/* --- one submenu per category, in catalogue order --- */
	for (i = 0; types != NULL && types[i] != NULL; i++)
	{
		const gchar      *category = lrg_vscript_node_type_get_category (types[i]);
		const gchar      *title = lrg_vscript_node_type_get_title (types[i]);
		GMenu            *sub;
		g_autofree gchar *detailed = NULL;

		if (category == NULL)
			category = "Other";
		sub = g_hash_table_lookup (submenus, category);
		if (sub == NULL)
		{
			sub = g_menu_new ();
			g_hash_table_insert (submenus, (gpointer) category, sub);
			g_ptr_array_add (order, (gpointer) category);
		}
		detailed = g_action_print_detailed_name ("vscript.add-node", g_variant_new_string (types[i]));
		g_menu_append (sub, title != NULL ? title : types[i], detailed);
	}
	for (i = 0; i < order->len; i++)
	{
		const gchar *category = g_ptr_array_index (order, i);

		g_menu_append_submenu (menu, category, G_MENU_MODEL (g_hash_table_lookup (submenus, category)));
	}
	return G_MENU_MODEL (menu);
}

static void
clear_drop (LrgEditorVscriptEditor *self)
{
	g_clear_pointer (&self->drop_node, g_free);
	g_clear_pointer (&self->drop_pin, g_free);
	self->drop_output = FALSE;
}

/*
 * wire_new_node:
 *
 * Connects the dropped-from pin to the first compatible pin of @node on
 * the other side, preferring exec to exec (first pass) over anything the
 * program accepts (second pass). Quietly does nothing when there is none.
 */
static void
wire_new_node (LrgEditorVscriptEditor *self,
               LrgVscriptProgram      *program,
               LrgVscriptGraph        *graph,
               LrgVscriptNode         *node)
{
	g_autoptr(GPtrArray) theirs = NULL;
	g_autoptr(GPtrArray) mine = NULL;
	LrgVscriptNode      *from;
	LrgVscriptPin       *src = NULL;
	guint                pass;
	guint                i;

	from = lrg_vscript_graph_get_node (graph, self->drop_node);
	if (from == NULL)
		return;

	/* --- the pin the wire came from --- */
	theirs = lrg_vscript_program_dup_node_pins (program, graph, from);
	for (i = 0; theirs != NULL && i < theirs->len; i++)
	{
		LrgVscriptPin *pin = (LrgVscriptPin *) g_ptr_array_index (theirs, i);

		if (g_strcmp0 (pin->name, self->drop_pin) == 0 &&
		    (pin->direction == LRG_VSCRIPT_PIN_OUTPUT) == self->drop_output)
			src = pin;
	}
	if (src == NULL)
		return;

	/* --- the first pin of the new node that takes it --- */
	mine = lrg_vscript_program_dup_node_pins (program, graph, node);
	for (pass = 0; pass < 2; pass++)
	{
		for (i = 0; mine != NULL && i < mine->len; i++)
		{
			LrgVscriptPin *pin = (LrgVscriptPin *) g_ptr_array_index (mine, i);
			gboolean       ok;

			if ((pin->direction == LRG_VSCRIPT_PIN_OUTPUT) == self->drop_output)
				continue;
			if (pass == 0 && pin->exec != src->exec)
				continue;
			if (self->drop_output)
				ok = lrg_vscript_program_connect (program, graph, self->drop_node, src->name,
				                                  lrg_vscript_node_get_id (node), pin->name, NULL);
			else
				ok = lrg_vscript_program_connect (program, graph, lrg_vscript_node_get_id (node), pin->name,
				                                  self->drop_node, src->name, NULL);
			if (ok)
				return;
		}
	}
}

/*
 * lrg_editor_vscript_editor_add_node_at: (documented in the header)
 * @self: a #LrgEditorVscriptEditor
 * @kind: a catalogued node kind
 * @x: left edge in graph units
 * @y: top edge in graph units
 * @error: return location for a #GError
 *
 * Adds a node of @kind to the shown graph as one undo step, wired to a
 * pending dropped wire when there is one, and selects it. This is what
 * the Add Node picker does.
 *
 * Returns: (transfer full) (nullable): the new node's id
 */
gchar *
lrg_editor_vscript_editor_add_node_at (LrgEditorVscriptEditor  *self,
                                       const gchar             *kind,
                                       gdouble                  x,
                                       gdouble                  y,
                                       GError                 **error)
{
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autofree gchar            *stem = NULL;
	g_autofree gchar            *id = NULL;
	LrgVscriptGraph             *graph;
	LrgVscriptNode              *node;
	const gchar                 *dot;

	g_return_val_if_fail (LRG_IS_EDITOR_VSCRIPT_EDITOR (self), NULL);
	g_return_val_if_fail (kind != NULL, NULL);

	lrg_editor_vscript_editor_flush (self);
	program = edit_begin (self, error);
	if (program == NULL)
		return NULL;
	graph = lrg_vscript_program_get_graph (program, self->shown);
	if (graph == NULL)
		graph = lrg_vscript_program_get_main (program);

	/* --- an id from the kind's last part: "flow.branch" -> "branch" --- */
	dot = strrchr (kind, '.');
	stem = g_strdup (dot != NULL ? dot + 1 : kind);
	id = lrg_vscript_graph_dup_free_id (graph, stem);
	node = lrg_vscript_graph_create_node (graph, kind, id, x, y, error);
	if (node == NULL)
		return NULL;

	/* --- a dropped wire leads into (or out of) the new node --- */
	if (self->drop_node != NULL)
		wire_new_node (self, program, graph, node);
	clear_drop (self);
	if (!commit_edit (self, program, "Add Node", self->shown, error))
		return NULL;
	g_ptr_array_set_size (self->selected, 0);
	g_ptr_array_add (self->selected, g_strdup (id));
	vscript_refresh (LRG_EDITOR_DATA_EDITOR (self), NULL);
	return g_steal_pointer (&id);
}

/*
 * lrg_editor_vscript_editor_set_drop: (documented in the header)
 * @self: a #LrgEditorVscriptEditor
 * @node: (nullable): the node a wire was dragged from, or %NULL to clear
 * @pin: (nullable): its pin
 * @output: whether @pin is an output
 *
 * Records the pin a wire was dropped from, as dropping it on empty canvas
 * does, so the next lrg_editor_vscript_editor_add_node_at() wires the new
 * node to it.
 */
void
lrg_editor_vscript_editor_set_drop (LrgEditorVscriptEditor *self,
                                    const gchar            *node,
                                    const gchar            *pin,
                                    gboolean                output)
{
	g_return_if_fail (LRG_IS_EDITOR_VSCRIPT_EDITOR (self));

	clear_drop (self);
	if (node == NULL || pin == NULL)
		return;
	self->drop_node = g_strdup (node);
	self->drop_pin = g_strdup (pin);
	self->drop_output = output;
}

static void
on_add_node_action (GSimpleAction *action,
                    GVariant      *parameter,
                    gpointer       user_data)
{
	LrgEditorVscriptEditor *self = LRG_EDITOR_VSCRIPT_EDITOR (user_data);
	g_autofree gchar       *id = NULL;
	g_autoptr(GError)       error = NULL;

	(void) action;
	id = lrg_editor_vscript_editor_add_node_at (self, g_variant_get_string (parameter, NULL), self->add_x,
	                                            self->add_y, &error);
	if (id == NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* the toolbar's Add Node: the picker under the chip, nodes at the view centre */
static void
on_add_chip (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorVscriptEditor *self = LRG_EDITOR_VSCRIPT_EDITOR (user_data);

	clear_drop (self);
	lrg_node_graph_get_center (self->canvas, &self->add_x, &self->add_y);
	lrg_menu_popup_at_widget (self->palette, chip, LRG_POPUP_PLACEMENT_BELOW);
}

/* a wire dropped on empty canvas: the picker at the drop point */
static void
on_link_dropped (LrgNodeGraph *graph,
                 LrgGraphPort *port,
                 gfloat        x,
                 gfloat        y,
                 gpointer      user_data)
{
	LrgEditorVscriptEditor *self = LRG_EDITOR_VSCRIPT_EDITOR (user_data);
	gfloat                  sx = 0.0f;
	gfloat                  sy = 0.0f;

	if (self->rebuilding)
		return;
	lrg_editor_vscript_editor_set_drop (self, lrg_graph_node_get_id (lrg_graph_port_get_node (port)),
	                                    lrg_graph_port_get_name (port),
	                                    lrg_graph_port_get_direction (port) == LRG_GRAPH_PORT_OUTPUT);
	self->add_x = x;
	self->add_y = y;
	lrg_node_graph_graph_to_screen (graph, x, y, &sx, &sy);
	lrg_menu_popup_at_point (self->palette, LRG_WIDGET (graph), sx, sy);
}

static gchar *
vscript_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorVscriptEditor *self = LRG_EDITOR_VSCRIPT_EDITOR (editor);
	guint                   n = 0;

	if (self->program != NULL)
		n = lrg_vscript_graph_get_n_nodes (lrg_vscript_program_get_main (self->program));
	return g_strdup_printf ("%u nodes", n);
}

static void
vscript_reveal (LrgEditorDataEditor *editor,
                const gchar         *path,
                guint                line)
{
	LrgEditorVscriptEditor *self = LRG_EDITOR_VSCRIPT_EDITOR (editor);
	LrgVscriptGraph        *graph;
	const gchar            *id = path;

	(void) line;
	if (self->program == NULL || path == NULL)
		return;
	graph = lrg_vscript_program_get_main (self->program);
	if (lrg_vscript_graph_get_node (graph, id) == NULL && strrchr (path, '.') != NULL)
		id = strrchr (path, '.') + 1;
	if (lrg_vscript_graph_get_node (graph, id) == NULL)
		return;
	g_ptr_array_set_size (self->selected, 0);
	g_ptr_array_add (self->selected, g_strdup (id));
	vscript_refresh (editor, NULL);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_vscript_editor_constructed (GObject *object)
{
	LrgEditorVscriptEditor *self = LRG_EDITOR_VSCRIPT_EDITOR (object);

	G_OBJECT_CLASS (lrg_editor_vscript_editor_parent_class)->constructed (object);
	self->canvas = lrg_node_graph_new ();
	self->blocks = lrg_block_editor_new ();
	lrg_widget_set_name (LRG_WIDGET (self->canvas), "vscript-canvas");
	lrg_widget_set_name (LRG_WIDGET (self->blocks), "vscript-blocks");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->canvas), 1.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->blocks), 1.0f);
	lrg_node_graph_set_snap_to_grid (self->canvas, TRUE);
	g_signal_connect_object (self->canvas, "can-connect", G_CALLBACK (on_can_connect), self, 0);
	g_signal_connect_object (self->canvas, "node-moved", G_CALLBACK (on_canvas_node), self, 0);
	g_signal_connect_object (self->canvas, "node-removed", G_CALLBACK (on_canvas_node), self, 0);
	g_signal_connect_object (self->canvas, "link-added", G_CALLBACK (on_canvas_link), self, 0);
	g_signal_connect_object (self->canvas, "link-removed", G_CALLBACK (on_canvas_link), self, 0);
	g_signal_connect_object (self->canvas, "node-duplicated", G_CALLBACK (on_canvas_duplicated), self, 0);
	g_signal_connect_object (self->canvas, "selection-changed", G_CALLBACK (on_canvas_selection), self, 0);
	g_signal_connect_object (self->canvas, "link-dropped", G_CALLBACK (on_link_dropped), self, 0);

	/* --- the Add Node picker and its toolbar chip --- */
	{
		static const GActionEntry entries[] = {
			{ "add-node", on_add_node_action, "s", NULL, NULL, { 0 } }
		};
		g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
		g_autoptr(GMenuModel)         model = palette_model ();
		LrgWidget                    *chip;

		g_action_map_add_action_entries (G_ACTION_MAP (group), entries, G_N_ELEMENTS (entries), self);
		lrg_widget_insert_action_group (LRG_WIDGET (self), "vscript", G_ACTION_GROUP (group));
		self->palette = lrg_menu_new_from_model (model);
		chip = editor_chip_new ("vscript-add-node", LRG_ICON_PLUS, "Add Node", on_add_chip, self);
		lrg_editor_data_editor_add_tool (LRG_EDITOR_DATA_EDITOR (self), chip);
		self->add_chip = chip;
		g_object_unref (chip);
	}
	ensure_kinds (self);
	vscript_refresh (LRG_EDITOR_DATA_EDITOR (self), NULL);
	if (lrg_editor_data_editor_get_content (LRG_EDITOR_DATA_EDITOR (self)) == NULL)
		lrg_editor_data_editor_set_content (LRG_EDITOR_DATA_EDITOR (self), LRG_WIDGET (self->canvas));
}

static void
lrg_editor_vscript_editor_dispose (GObject *object)
{
	LrgEditorVscriptEditor *self = LRG_EDITOR_VSCRIPT_EDITOR (object);

	g_clear_handle_id (&self->sync_idle, g_source_remove);
	if (self->palette != NULL)
		lrg_menu_close (self->palette);
	g_clear_object (&self->palette);
	lrg_editor_data_editor_set_content (LRG_EDITOR_DATA_EDITOR (self), NULL);
	g_clear_object (&self->canvas);
	g_clear_object (&self->blocks);
	g_clear_object (&self->program);
	G_OBJECT_CLASS (lrg_editor_vscript_editor_parent_class)->dispose (object);
}

static void
lrg_editor_vscript_editor_finalize (GObject *object)
{
	LrgEditorVscriptEditor *self = LRG_EDITOR_VSCRIPT_EDITOR (object);

	g_clear_pointer (&self->selected, g_ptr_array_unref);
	g_free (self->shown);
	g_free (self->framed_graph);
	g_clear_pointer (&self->dupes, g_hash_table_unref);
	g_free (self->drop_node);
	g_free (self->drop_pin);
	G_OBJECT_CLASS (lrg_editor_vscript_editor_parent_class)->finalize (object);
}

static void
lrg_editor_vscript_editor_class_init (LrgEditorVscriptEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_vscript_editor_constructed;
	object_class->dispose = lrg_editor_vscript_editor_dispose;
	object_class->finalize = lrg_editor_vscript_editor_finalize;
	data_class->refresh = vscript_refresh;
	data_class->reveal = vscript_reveal;
	data_class->get_state = vscript_get_state;
}

static void
lrg_editor_vscript_editor_init (LrgEditorVscriptEditor *self)
{
	self->selected = g_ptr_array_new_with_free_func (g_free);
	self->dupes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorVscriptEditor *
lrg_editor_vscript_editor_new (LrgEditorApp      *app,
                               LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_VSCRIPT_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorVscriptEditor *
lrg_editor_vscript_editor_open (LrgEditorApp  *app,
                                const gchar   *path,
                                GError       **error)
{
	LrgEditorDataEditor *editor;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	editor = lrg_editor_data_editor_open (app, path, error);
	if (editor == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_VSCRIPT_EDITOR (editor))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not a visual script", path);
		return NULL;
	}
	return LRG_EDITOR_VSCRIPT_EDITOR (editor);
}

gboolean
lrg_editor_vscript_editor_create (LrgEditorApp  *app,
                                  const gchar   *path,
                                  const gchar   *view,
                                  GError       **error)
{
	g_autoptr(LrgEditorDocument) doc = NULL;
	g_autofree gchar            *abs = NULL;
	g_autofree gchar            *dir = NULL;
	LrgVscriptView               which = LRG_VSCRIPT_VIEW_GRAPH;

	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	if (!view_from_name (view, &which, error))
		return FALSE;
	abs = abs_path (app, path);
	if (g_file_test (abs, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s already exists", abs);
		return FALSE;
	}
	dir = g_path_get_dirname (abs);
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Could not create %s", dir);
		return FALSE;
	}
	doc = lrg_editor_document_new (LRG_EDITOR_DOCUMENT_FORMAT_VSCRIPT);
	if (which == LRG_VSCRIPT_VIEW_BLOCKS)
	{
		g_autoptr(LrgVscriptProgram) program = NULL;

		program = LRG_VSCRIPT_PROGRAM (lrg_editor_document_dup_object (doc, error));
		if (program == NULL)
			return FALSE;
		lrg_vscript_program_set_view (program, which);
		if (!lrg_editor_document_commit_object (doc, G_OBJECT (program), "View", 0, error))
			return FALSE;
	}
	return lrg_editor_document_save_as (doc, abs, error);
}

LrgVscriptProgram *
lrg_editor_vscript_editor_get_program (LrgEditorVscriptEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VSCRIPT_EDITOR (self), NULL);
	return self->program;
}

LrgNodeGraph *
lrg_editor_vscript_editor_get_canvas (LrgEditorVscriptEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VSCRIPT_EDITOR (self), NULL);
	return self->canvas;
}

LrgBlockEditor *
lrg_editor_vscript_editor_get_blocks (LrgEditorVscriptEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VSCRIPT_EDITOR (self), NULL);
	return self->blocks;
}

/* ==========================================================================
 * IPC
 * ========================================================================== */

static LrgEditorVscriptEditor *
need_editor (LrgEditorApp  *app,
             JsonObject    *params,
             GError       **error)
{
	const gchar         *path = param_string (params, "path");
	LrgEditorDataEditor *editor;
	g_autoptr(GError)    err = NULL;

	if (path == NULL)
	{
		editor = lrg_editor_data_editor_find (app, NULL);
		if (!LRG_IS_EDITOR_VSCRIPT_EDITOR (editor))
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			                     "No visual script is open");
			return NULL;
		}
		return LRG_EDITOR_VSCRIPT_EDITOR (editor);
	}
	editor = LRG_EDITOR_DATA_EDITOR (lrg_editor_vscript_editor_open (app, path, &err));
	if (editor == NULL)
		fail (error, err);
	return LRG_EDITOR_VSCRIPT_EDITOR (editor);
}

static gboolean
apply_map (LrgVscriptNode *node,
           JsonObject     *obj,
           gboolean        input,
           GError        **error)
{
	GList *keys;
	GList *l;

	if (obj == NULL)
		return TRUE;
	keys = json_object_get_members (obj);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar *key = (const gchar *) l->data;
		GVariant    *value = NULL;

		if (!json_to_variant (json_object_get_member (obj, key), &value, error))
		{
			g_list_free (keys);
			return FALSE;
		}
		if (input)
			lrg_vscript_node_set_input (node, key, value);
		else
			lrg_vscript_node_set_param (node, key, value);
	}
	g_list_free (keys);
	return TRUE;
}

static gboolean
apply_interface (LrgVscriptGraph        *graph,
                 JsonObject             *params,
                 const gchar            *key,
                 LrgVscriptPinDirection  direction,
                 GError                **error)
{
	JsonNode  *node;
	JsonArray *array;
	guint      i;
	guint      n;

	if (params == NULL || !json_object_has_member (params, key))
		return TRUE;
	node = json_object_get_member (params, key);
	if (!JSON_NODE_HOLDS_ARRAY (node))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "'%s' must be an array of {name, type}", key);
		return FALSE;
	}
	array = json_node_get_array (node);
	n = json_array_get_length (array);
	for (i = 0; i < n; i++)
	{
		JsonObject  *item;
		const gchar *name;
		const gchar *type;

		if (!JSON_NODE_HOLDS_OBJECT (json_array_get_element (array, i)))
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			                     "An interface pin must be an object");
			return FALSE;
		}
		item = json_array_get_object_element (array, i);
		name = param_string (item, "name");
		type = param_string (item, "type");
		if (name == NULL)
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			                     "An interface pin needs a name");
			return FALSE;
		}
		lrg_vscript_graph_set_interface_pin (graph, direction, name, type != NULL ? type : "any");
	}
	return TRUE;
}

static gboolean
commit_edit (LrgEditorVscriptEditor *self,
             LrgVscriptProgram      *program,
             const gchar            *label,
             const gchar            *graph,
             GError                **error)
{
	g_autoptr(GError) err = NULL;

	remember_graph (self, graph);
	if (!lrg_editor_document_commit_object (lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self)),
	                                        G_OBJECT (program), label, 0, &err))
	{
		fail (error, err);
		return FALSE;
	}
	return TRUE;
}

static JsonNode *
m_create (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorApp             *app = LRG_EDITOR_APP (user_data);
	const gchar              *path = require_string (params, "path", error);
	const gchar              *name = param_string (params, "name");
	const gchar              *view = param_string (params, "view");
	g_autofree gchar         *abs = NULL;
	g_autoptr(GError)         err = NULL;
	g_autoptr(LrgEditorDocument) doc = NULL;
	JsonObject               *o;

	(void) server;
	if (path == NULL)
		return NULL;
	if (!lrg_editor_vscript_editor_create (app, path, view, &err))
	{
		fail (error, err);
		return NULL;
	}
	abs = abs_path (app, path);
	if (name != NULL)
	{
		g_autoptr(LrgVscriptProgram) program = NULL;

		doc = lrg_editor_document_new_from_file (abs, &err);
		if (doc == NULL)
		{
			fail (error, err);
			return NULL;
		}
		program = LRG_VSCRIPT_PROGRAM (lrg_editor_document_dup_object (doc, &err));
		if (program == NULL)
		{
			fail (error, err);
			return NULL;
		}
		lrg_vscript_program_set_name (program, name);
		if (!lrg_editor_document_commit_object (doc, G_OBJECT (program), "Name", 0, &err) ||
		    !lrg_editor_document_save (doc, &err))
		{
			fail (error, err);
			return NULL;
		}
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", abs);
	json_object_set_string_member (o, "view", view != NULL ? view : "graph");
	return object_node (o);
}

static JsonNode *
m_open (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorVscriptEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar            *view = param_string (params, "view");
	JsonObject             *o;

	(void) server;
	if (self == NULL)
		return NULL;
	if (view != NULL)
	{
		LrgVscriptView               which;
		g_autoptr(LrgVscriptProgram) program = NULL;
		g_autoptr(GError)            err = NULL;

		if (!view_from_name (view, &which, error))
			return NULL;
		program = edit_begin (self, &err);
		if (program == NULL)
		{
			fail (error, err);
			return NULL;
		}
		lrg_vscript_program_set_view (program, which);
		if (!commit_edit (self, program, "View", NULL, error))
			return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "panel", lrg_editor_data_editor_get_panel_id (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "view",
	                               self->program != NULL ? view_name (lrg_vscript_program_get_view (self->program))
	                                                     : "graph");
	return object_node (o);
}

static JsonNode *
m_get (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorVscriptEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);

	(void) server;
	if (self == NULL || self->program == NULL)
	{
		if (self != NULL)
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
			                     "The visual script could not be read");
		return NULL;
	}
	return program_json (self, param_string (params, "graph"));
}

static JsonNode *
m_node_types (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	const gchar   *query = param_string (params, "query");
	const gchar   *category = param_string (params, "category");
	g_auto(GStrv)  types = lrg_vscript_node_types_list ();
	JsonArray     *a = json_array_new ();
	JsonObject    *o = json_object_new ();
	guint          i;

	(void) server;
	(void) user_data;
	(void) error;
	for (i = 0; types != NULL && types[i] != NULL; i++)
	{
		const gchar *title = lrg_vscript_node_type_get_title (types[i]);
		const gchar *cat = lrg_vscript_node_type_get_category (types[i]);
		const gchar *desc = lrg_vscript_node_type_get_description (types[i]);
		JsonObject  *t;

		if (category != NULL && g_strcmp0 (cat, category) != 0)
			continue;
		if (query != NULL && query[0] != '\0' &&
		    (types[i] == NULL || g_strstr_len (types[i], -1, query) == NULL) &&
		    (title == NULL || g_strstr_len (title, -1, query) == NULL) &&
		    (desc == NULL || g_strstr_len (desc, -1, query) == NULL))
			continue;
		t = json_object_new ();
		json_object_set_string_member (t, "type", types[i]);
		json_object_set_string_member (t, "title", title != NULL ? title : types[i]);
		json_object_set_string_member (t, "category", cat != NULL ? cat : "");
		json_object_set_string_member (t, "description", desc != NULL ? desc : "");
		json_object_set_boolean_member (t, "event", lrg_vscript_node_type_is_event (types[i]));
		json_object_set_boolean_member (t, "pure", lrg_vscript_node_type_is_pure (types[i]));
		json_array_add_object_element (a, t);
	}
	json_object_set_array_member (o, "types", a);
	return object_node (o);
}

static JsonNode *
m_gi_search (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	const gchar   *query = require_string (params, "query", error);
	g_auto(GStrv)  found = NULL;
	JsonArray     *a = json_array_new ();
	JsonObject    *o = json_object_new ();
	guint          i;

	(void) server;
	(void) user_data;
	if (query == NULL)
		return NULL;
	found = lrg_vscript_gi_search (query, param_uint (params, "limit", 50));
	for (i = 0; found != NULL && found[i] != NULL; i++)
		json_array_add_string_element (a, found[i]);
	json_object_set_boolean_member (o, "available", lrg_vscript_gi_available ());
	json_object_set_array_member (o, "results", a);
	return object_node (o);
}

static JsonNode *
m_add_node (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorVscriptEditor      *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                 *type;
	const gchar                 *graph_name;
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(GError)            err = NULL;
	LrgVscriptGraph             *graph;
	LrgVscriptNode              *node;
	gdouble                      x = 0.0;
	gdouble                      y = 0.0;
	JsonObject                  *o;

	(void) server;
	if (self == NULL || (type = require_string (params, "type", error)) == NULL)
		return NULL;
	program = edit_begin (self, &err);
	if (program == NULL)
	{
		fail (error, err);
		return NULL;
	}
	graph_name = param_string (params, "graph");
	graph = graph_named (program, graph_name, &err);
	if (graph == NULL)
	{
		fail (error, err);
		return NULL;
	}
	param_number (params, "x", &x);
	param_number (params, "y", &y);
	node = lrg_vscript_graph_create_node (graph, type, param_string (params, "id"), x, y, &err);
	if (node == NULL || !apply_map (node, param_object (params, "params"), FALSE, error) ||
	    !apply_map (node, param_object (params, "inputs"), TRUE, error))
	{
		if (node == NULL)
			fail (error, err);
		return NULL;
	}
	if (!commit_edit (self, program, "Add node", graph_name, error))
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "id", lrg_vscript_node_get_id (node));
	json_object_set_array_member (o, "pins", pins_json (program, graph, node));
	return object_node (o);
}

static JsonNode *
m_remove_node (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorVscriptEditor      *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                 *id;
	const gchar                 *graph_name;
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(GError)            err = NULL;
	LrgVscriptGraph             *graph;
	JsonObject                  *o;

	(void) server;
	if (self == NULL || (id = require_string (params, "id", error)) == NULL)
		return NULL;
	program = edit_begin (self, &err);
	if (program == NULL)
	{
		fail (error, err);
		return NULL;
	}
	graph_name = param_string (params, "graph");
	graph = graph_named (program, graph_name, &err);
	if (graph == NULL || !lrg_vscript_graph_remove_node (graph, id))
	{
		if (graph != NULL)
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No node '%s'", id);
		else
			fail (error, err);
		return NULL;
	}
	if (!commit_edit (self, program, "Remove node", graph_name, error))
		return NULL;
	o = json_object_new ();
	json_object_set_boolean_member (o, "removed", TRUE);
	return object_node (o);
}

static JsonNode *
m_set_node (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorVscriptEditor      *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                 *id;
	const gchar                 *graph_name;
	const gchar                 *rename;
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(GError)            err = NULL;
	LrgVscriptGraph             *graph;
	LrgVscriptNode              *node;
	gdouble                      x;
	gdouble                      y;
	JsonObject                  *o;

	(void) server;
	if (self == NULL || (id = require_string (params, "id", error)) == NULL)
		return NULL;
	program = edit_begin (self, &err);
	if (program == NULL)
	{
		fail (error, err);
		return NULL;
	}
	graph_name = param_string (params, "graph");
	graph = graph_named (program, graph_name, &err);
	node = graph != NULL ? lrg_vscript_graph_get_node (graph, id) : NULL;
	if (node == NULL)
	{
		if (graph == NULL)
			fail (error, err);
		else
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No node '%s'", id);
		return NULL;
	}
	lrg_vscript_node_get_position (node, &x, &y);
	param_number (params, "x", &x);
	param_number (params, "y", &y);
	lrg_vscript_node_set_position (node, x, y);
	rename = param_string (params, "rename");
	if (rename != NULL && !lrg_vscript_graph_rename_node (graph, id, rename, &err))
	{
		fail (error, err);
		return NULL;
	}
	if (!apply_map (node, param_object (params, "params"), FALSE, error) ||
	    !apply_map (node, param_object (params, "inputs"), TRUE, error))
		return NULL;
	if (!commit_edit (self, program, "Edit node", graph_name, error))
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "id", lrg_vscript_node_get_id (node));
	json_object_set_array_member (o, "pins", pins_json (program, graph, node));
	return object_node (o);
}

static JsonNode *
m_connect (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorVscriptEditor      *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                 *from_spec;
	const gchar                 *to_spec;
	const gchar                 *graph_name;
	g_autofree gchar            *from_node = NULL;
	g_autofree gchar            *from_pin = NULL;
	g_autofree gchar            *to_node = NULL;
	g_autofree gchar            *to_pin = NULL;
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(GError)            err = NULL;
	LrgVscriptGraph             *graph;
	JsonObject                  *o;

	(void) server;
	if (self == NULL || (from_spec = require_string (params, "from", error)) == NULL ||
	    (to_spec = require_string (params, "to", error)) == NULL)
		return NULL;
	if (!split_pin (from_spec, &from_node, &from_pin, error) || !split_pin (to_spec, &to_node, &to_pin, error))
		return NULL;
	program = edit_begin (self, &err);
	if (program == NULL)
	{
		fail (error, err);
		return NULL;
	}
	graph_name = param_string (params, "graph");
	graph = graph_named (program, graph_name, &err);
	if (graph == NULL || !lrg_vscript_program_connect (program, graph, from_node, from_pin, to_node, to_pin, &err))
	{
		fail (error, err);
		return NULL;
	}
	if (!commit_edit (self, program, "Connect", graph_name, error))
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "from", from_spec);
	json_object_set_string_member (o, "to", to_spec);
	return object_node (o);
}

static JsonNode *
m_disconnect (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorVscriptEditor      *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                 *from_spec;
	const gchar                 *to_spec;
	const gchar                 *graph_name;
	g_autofree gchar            *from_node = NULL;
	g_autofree gchar            *from_pin = NULL;
	g_autofree gchar            *to_node = NULL;
	g_autofree gchar            *to_pin = NULL;
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(GError)            err = NULL;
	LrgVscriptGraph             *graph;
	gboolean                     removed;
	JsonObject                  *o;

	(void) server;
	if (self == NULL || (from_spec = require_string (params, "from", error)) == NULL ||
	    (to_spec = require_string (params, "to", error)) == NULL)
		return NULL;
	if (!split_pin (from_spec, &from_node, &from_pin, error) || !split_pin (to_spec, &to_node, &to_pin, error))
		return NULL;
	program = edit_begin (self, &err);
	if (program == NULL)
	{
		fail (error, err);
		return NULL;
	}
	graph_name = param_string (params, "graph");
	graph = graph_named (program, graph_name, &err);
	if (graph == NULL)
	{
		fail (error, err);
		return NULL;
	}
	removed = lrg_vscript_graph_remove_link (graph, from_node, from_pin, to_node, to_pin);
	if (removed && !commit_edit (self, program, "Disconnect", graph_name, error))
		return NULL;
	o = json_object_new ();
	json_object_set_boolean_member (o, "removed", removed);
	return object_node (o);
}

static JsonNode *
m_set_variable (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorVscriptEditor      *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                 *name;
	gboolean                     remove = FALSE;
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(GError)            err = NULL;
	JsonObject                  *o;

	(void) server;
	if (self == NULL || (name = require_string (params, "name", error)) == NULL)
		return NULL;
	param_bool (params, "remove", &remove);
	program = edit_begin (self, &err);
	if (program == NULL)
	{
		fail (error, err);
		return NULL;
	}
	if (remove)
	{
		if (!lrg_vscript_program_remove_variable (program, name))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			             "No variable '%s'", name);
			return NULL;
		}
	}
	else
	{
		const gchar *type = param_string (params, "type");
		GVariant    *value = NULL;

		if (params != NULL && json_object_has_member (params, "default") &&
		    !json_to_variant (json_object_get_member (params, "default"), &value, error))
			return NULL;
		lrg_vscript_program_set_variable (program, name, type != NULL ? type : "any", value);
	}
	if (!commit_edit (self, program, remove ? "Remove variable" : "Set variable", NULL, error))
		return NULL;
	o = json_object_new ();
	if (remove)
		json_object_set_boolean_member (o, "removed", TRUE);
	else
	{
		json_object_set_string_member (o, "name", name);
		json_object_set_string_member (o, "type", lrg_vscript_program_get_variable_type (program, name));
		json_set_variant (o, "default", lrg_vscript_program_get_variable_default (program, name));
	}
	return object_node (o);
}

static JsonNode *
m_add_subgraph (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorVscriptEditor      *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                 *name;
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(GError)            err = NULL;
	LrgVscriptGraph             *graph;
	JsonObject                  *o;

	(void) server;
	if (self == NULL || (name = require_string (params, "name", error)) == NULL)
		return NULL;
	program = edit_begin (self, &err);
	if (program == NULL)
	{
		fail (error, err);
		return NULL;
	}
	graph = lrg_vscript_program_add_subgraph (program, name, &err);
	if (graph == NULL || !apply_interface (graph, params, "inputs", LRG_VSCRIPT_PIN_INPUT, error) ||
	    !apply_interface (graph, params, "outputs", LRG_VSCRIPT_PIN_OUTPUT, error))
	{
		if (graph == NULL)
			fail (error, err);
		return NULL;
	}
	if (!commit_edit (self, program, "Add subgraph", name, error))
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "name", name);
	return object_node (o);
}

static JsonNode *
m_remove_subgraph (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorVscriptEditor      *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                 *name;
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(GError)            err = NULL;
	JsonObject                  *o;

	(void) server;
	if (self == NULL || (name = require_string (params, "name", error)) == NULL)
		return NULL;
	program = edit_begin (self, &err);
	if (program == NULL)
	{
		fail (error, err);
		return NULL;
	}
	if (!lrg_vscript_program_remove_subgraph (program, name))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "No subgraph '%s'", name);
		return NULL;
	}
	if (!commit_edit (self, program, "Remove subgraph", NULL, error))
		return NULL;
	o = json_object_new ();
	json_object_set_boolean_member (o, "removed", TRUE);
	return object_node (o);
}

static JsonNode *
m_add_comment (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorVscriptEditor      *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                 *text;
	const gchar                 *graph_name;
	const gchar                 *color;
	const gchar                 *id;
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(LrgVscriptComment) comment = NULL;
	g_autoptr(GError)            err = NULL;
	LrgVscriptGraph             *graph;
	gdouble                      x = 0.0;
	gdouble                      y = 0.0;
	gdouble                      width = 240.0;
	gdouble                      height = 120.0;
	JsonObject                  *o;

	(void) server;
	if (self == NULL || (text = require_string (params, "text", error)) == NULL)
		return NULL;
	if (!param_number (params, "x", &x) || !param_number (params, "y", &y))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "A comment needs x and y");
		return NULL;
	}
	param_number (params, "width", &width);
	param_number (params, "height", &height);
	program = edit_begin (self, &err);
	if (program == NULL)
	{
		fail (error, err);
		return NULL;
	}
	graph_name = param_string (params, "graph");
	graph = graph_named (program, graph_name, &err);
	if (graph == NULL)
	{
		fail (error, err);
		return NULL;
	}
	comment = lrg_vscript_comment_new (NULL, text, x, y, width, height);
	color = param_string (params, "color");
	if (color != NULL)
	{
		g_free (comment->color);
		comment->color = g_strdup (color);
	}
	id = lrg_vscript_graph_add_comment (graph, comment);
	if (!commit_edit (self, program, "Add comment", graph_name, error))
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "id", id);
	return object_node (o);
}

static JsonNode *
m_remove_comment (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorVscriptEditor      *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                 *id;
	const gchar                 *graph_name;
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(GError)            err = NULL;
	LrgVscriptGraph             *graph;
	JsonObject                  *o;

	(void) server;
	if (self == NULL || (id = require_string (params, "id", error)) == NULL)
		return NULL;
	program = edit_begin (self, &err);
	if (program == NULL)
	{
		fail (error, err);
		return NULL;
	}
	graph_name = param_string (params, "graph");
	graph = graph_named (program, graph_name, &err);
	if (graph == NULL || !lrg_vscript_graph_remove_comment (graph, id))
	{
		if (graph != NULL)
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			             "No comment '%s'", id);
		else
			fail (error, err);
		return NULL;
	}
	if (!commit_edit (self, program, "Remove comment", graph_name, error))
		return NULL;
	o = json_object_new ();
	json_object_set_boolean_member (o, "removed", TRUE);
	return object_node (o);
}

static JsonNode *
m_set_program (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorVscriptEditor      *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                 *name;
	const gchar                 *description;
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(GError)            err = NULL;
	gdouble                      number;

	(void) server;
	if (self == NULL)
		return NULL;
	program = edit_begin (self, &err);
	if (program == NULL)
	{
		fail (error, err);
		return NULL;
	}
	name = param_string (params, "name");
	description = param_string (params, "description");
	if (name != NULL)
		lrg_vscript_program_set_name (program, name);
	if (description != NULL)
		lrg_vscript_program_set_description (program, description);
	if (param_number (params, "seed", &number))
		lrg_vscript_program_set_seed (program, (guint32) number);
	if (param_number (params, "step_budget", &number) && number >= 0.0)
		lrg_vscript_program_set_step_budget (program, (guint) number);
	if (param_number (params, "fixed_step", &number))
		lrg_vscript_program_set_fixed_step (program, number);
	if (!commit_edit (self, program, "Program", NULL, error))
		return NULL;
	return program_json (self, param_string (params, "graph"));
}

static JsonNode *
m_select (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorVscriptEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonNode               *node;
	JsonArray              *array;
	LrgVscriptGraph        *graph;
	JsonArray              *selected;
	JsonObject             *o;
	guint                   i;
	guint                   n;

	(void) server;
	if (self == NULL || self->program == NULL)
		return NULL;
	if (params == NULL || !json_object_has_member (params, "nodes") ||
	    !JSON_NODE_HOLDS_ARRAY (json_object_get_member (params, "nodes")))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing array parameter 'nodes'");
		return NULL;
	}
	graph = lrg_vscript_program_get_graph (self->program, self->shown);
	if (graph == NULL)
		graph = lrg_vscript_program_get_main (self->program);
	array = json_object_get_array_member (params, "nodes");
	n = json_array_get_length (array);
	g_ptr_array_set_size (self->selected, 0);
	for (i = 0; i < n; i++)
	{
		node = json_array_get_element (array, i);
		if (!JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_STRING)
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			                     "nodes must be node ids");
			return NULL;
		}
		if (lrg_vscript_graph_get_node (graph, json_node_get_string (node)) == NULL)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			             "No node '%s'", json_node_get_string (node));
			return NULL;
		}
		g_ptr_array_add (self->selected, g_strdup (json_node_get_string (node)));
	}
	vscript_refresh (LRG_EDITOR_DATA_EDITOR (self), NULL);
	selected = json_array_new ();
	for (i = 0; i < self->selected->len; i++)
		json_array_add_string_element (selected, (const gchar *) g_ptr_array_index (self->selected, i));
	o = json_object_new ();
	json_object_set_array_member (o, "selected", selected);
	return object_node (o);
}

/*
 * m_frame:
 *
 * vscript.frame: fits the shown graph in the canvas, as Home does, or
 * the selected nodes, as F does, when selection is true. animate glides
 * there (default false). Returns {zoom, x, y}: the target view.
 */
static JsonNode *
m_frame (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	LrgEditorVscriptEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	gboolean                animate = FALSE;
	gboolean                selection = FALSE;
	JsonObject             *o;
	gfloat                  cx = 0.0f;
	gfloat                  cy = 0.0f;

	(void) server;
	if (self == NULL || self->program == NULL)
		return NULL;
	(void) param_bool (params, "animate", &animate);
	(void) param_bool (params, "selection", &selection);
	if (selection)
		lrg_node_graph_frame_selection (self->canvas, animate);
	else
		lrg_node_graph_frame_all (self->canvas, animate);
	lrg_node_graph_get_center (self->canvas, &cx, &cy);
	o = json_object_new ();
	json_object_set_double_member (o, "zoom", lrg_node_graph_get_zoom (self->canvas));
	json_object_set_double_member (o, "x", cx);
	json_object_set_double_member (o, "y", cy);
	return object_node (o);
}

/*
 * m_palette:
 *
 * vscript.palette: opens the Add Node picker under the toolbar chip, as
 * clicking Add Node does (a picked node lands at the view centre), or
 * closes it when open is false. category names a top-level row (such as
 * "Math") whose submenu is opened as well. Returns {open, entries}: the
 * picker state and the labels of its top-level rows. Not an undo step.
 */
static JsonNode *
m_palette (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorVscriptEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	gboolean                open = TRUE;
	const gchar            *category = NULL;
	JsonArray              *entries;
	JsonObject             *o;
	guint                   i;
	guint                   n;

	(void) server;
	if (self == NULL || self->program == NULL)
		return NULL;
	(void) param_bool (params, "open", &open);
	if (params != NULL && json_object_has_member (params, "category"))
		category = json_object_get_string_member (params, "category");

	/* --- open (as the chip click) or close --- */
	if (!open)
		lrg_menu_close (self->palette);
	else if (!lrg_menu_get_open (self->palette))
		on_add_chip (self->add_chip, self);

	/* --- the top-level rows; open the named category's submenu --- */
	entries = json_array_new ();
	n = lrg_menu_get_n_entries (self->palette);
	for (i = 0; i < n; i++)
	{
		const gchar *label = lrg_menu_get_entry_label (self->palette, i);

		if (lrg_menu_get_entry_kind (self->palette, i) != LRG_MENU_ENTRY_ITEM &&
		    lrg_menu_get_entry_kind (self->palette, i) != LRG_MENU_ENTRY_SUBMENU)
			continue;
		json_array_add_string_element (entries, label);
		if (open && category != NULL && g_strcmp0 (label, category) == 0 &&
		    lrg_menu_get_entry_kind (self->palette, i) == LRG_MENU_ENTRY_SUBMENU)
		{
			lrg_menu_set_highlighted (self->palette, (gint) i);
			lrg_menu_activate_entry (self->palette, i);
			category = NULL;
		}
	}
	if (category != NULL)
	{
		json_array_unref (entries);
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "No node category '%s'", category);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_boolean_member (o, "open", lrg_menu_get_open (self->palette));
	json_object_set_array_member (o, "entries", entries);
	return object_node (o);
}

static JsonNode *
m_validate (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorVscriptEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GPtrArray)    issues = NULL;
	JsonObject             *o;
	gboolean                ok;

	(void) server;
	if (self == NULL || self->program == NULL)
		return NULL;
	ok = lrg_vscript_program_validate (self->program, &issues);
	o = json_object_new ();
	json_object_set_boolean_member (o, "ok", ok);
	json_object_set_array_member (o, "issues", issues_json (issues));
	return object_node (o);
}

static void
variant_to_gvalue (GVariant *value,
                   GValue   *out)
{
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
	{
		g_value_init (out, G_TYPE_BOOLEAN);
		g_value_set_boolean (out, g_variant_get_boolean (value));
	}
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT64))
	{
		g_value_init (out, G_TYPE_INT64);
		g_value_set_int64 (out, g_variant_get_int64 (value));
	}
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_DOUBLE))
	{
		g_value_init (out, G_TYPE_DOUBLE);
		g_value_set_double (out, g_variant_get_double (value));
	}
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
	{
		g_value_init (out, G_TYPE_STRING);
		g_value_set_string (out, g_variant_get_string (value, NULL));
	}
	else
	{
		g_value_init (out, G_TYPE_STRING);
		g_value_take_string (out, g_variant_print (value, FALSE));
	}
}

static JsonNode *
m_run (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgEditorVscriptEditor       *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(LrgVscriptProgram)  program = NULL;
	g_autoptr(LrgScriptingGraph)  graph = NULL;
	g_autoptr(GError)             err = NULL;
	g_autofree gchar             *text = NULL;
	g_auto(GStrv)                 log = NULL;
	g_autoptr(GVariant)           vars = NULL;
	JsonObject                   *overlay;
	JsonObject                   *o;
	JsonArray                    *log_json;
	JsonObject                   *var_json;
	const gchar                  *event;
	const gchar                  *message = NULL;
	GVariantIter                  iter;
	GValue                        dt_value = G_VALUE_INIT;
	GValue                        args[16];
	guint                         n_args = 0;
	guint                         frames;
	guint                         done = 0;
	guint                         steps = 0;
	guint                         i;
	gdouble                       dt = 1.0 / 60.0;
	gboolean                      ok = TRUE;

	(void) server;
	if (self == NULL)
		return NULL;
	program = edit_begin (self, &err);
	if (program == NULL)
	{
		fail (error, err);
		return NULL;
	}
	overlay = param_object (params, "variables");
	if (overlay != NULL)
	{
		GList *keys = json_object_get_members (overlay);
		GList *l;

		for (l = keys; l != NULL; l = l->next)
		{
			GVariant *value = NULL;

			if (!json_to_variant (json_object_get_member (overlay, (const gchar *) l->data), &value, error))
			{
				g_list_free (keys);
				return NULL;
			}
			lrg_vscript_program_set_variable (program, (const gchar *) l->data, "any", value);
		}
		g_list_free (keys);
	}
	text = lrg_vscript_program_to_data (program, &err);
	if (text == NULL)
	{
		fail (error, err);
		return NULL;
	}
	frames = param_uint (params, "frames", 1);
	if (param_number (params, "dt", &dt) && dt < 0.0)
		dt = 0.0;
	graph = lrg_scripting_graph_new ();
	if (!lrg_scripting_load_string (LRG_SCRIPTING (graph), "vscript", text, &err))
	{
		ok = FALSE;
		message = err->message;
	}
	else
		steps += lrg_scripting_graph_get_steps (graph);
	g_value_init (&dt_value, G_TYPE_DOUBLE);
	g_value_set_double (&dt_value, dt);
	for (i = 0; ok && i < frames; i++)
	{
		g_clear_error (&err);
		if (!lrg_scripting_call_function (LRG_SCRIPTING (graph), "update", NULL, 1, &dt_value, &err))
		{
			ok = FALSE;
			message = err != NULL ? err->message : "update failed";
			break;
		}
		steps += lrg_scripting_graph_get_steps (graph);
		done++;
	}
	event = param_string (params, "event");
	if (ok && event != NULL)
	{
		JsonNode *args_node = params != NULL ? json_object_get_member (params, "args") : NULL;

		if (args_node != NULL && JSON_NODE_HOLDS_ARRAY (args_node))
		{
			JsonArray *arr = json_node_get_array (args_node);

			n_args = MIN (json_array_get_length (arr), 16);
			for (i = 0; i < n_args; i++)
			{
				GVariant *value = NULL;

				if (!json_to_variant (json_array_get_element (arr, i), &value, error))
				{
					g_value_unset (&dt_value);
					return NULL;
				}
				if (value == NULL)
				{
					g_value_init (&args[i], G_TYPE_POINTER);
					g_value_set_pointer (&args[i], NULL);
				}
				else
				{
					variant_to_gvalue (value, &args[i]);
					g_variant_unref (g_variant_ref_sink (value));
				}
			}
		}
		g_clear_error (&err);
		if (!lrg_scripting_call_function (LRG_SCRIPTING (graph), event, NULL, n_args, n_args > 0 ? args : NULL, &err))
		{
			ok = FALSE;
			message = err != NULL ? err->message : "event failed";
		}
		else
			steps += lrg_scripting_graph_get_steps (graph);
	}
	for (i = 0; i < n_args; i++)
		g_value_unset (&args[i]);
	g_value_unset (&dt_value);

	log = lrg_scripting_graph_dup_log (graph);
	vars = lrg_scripting_graph_dup_variables (graph);
	log_json = json_array_new ();
	for (i = 0; log != NULL && log[i] != NULL; i++)
		json_array_add_string_element (log_json, log[i]);
	var_json = json_object_new ();
	if (vars != NULL)
	{
		gchar    *key = NULL;
		GVariant *value = NULL;

		g_variant_iter_init (&iter, vars);
		while (g_variant_iter_next (&iter, "{sv}", &key, &value))
		{
			json_set_variant (var_json, key, value);
			g_free (key);
			g_variant_unref (value);
		}
	}
	o = json_object_new ();
	json_object_set_boolean_member (o, "ok", ok);
	json_object_set_int_member (o, "steps", (gint64) steps);
	json_object_set_double_member (o, "time", dt * (gdouble) done);
	json_object_set_array_member (o, "log", log_json);
	json_object_set_object_member (o, "variables", var_json);
	if (message != NULL)
		json_object_set_string_member (o, "error", message);
	return object_node (o);
}

static gboolean
same_script (LrgScriptingGraph *graph,
             const gchar       *path)
{
	const gchar *got = lrg_scripting_graph_get_path (graph);

	return path != NULL && got != NULL && g_strcmp0 (got, path) == 0;
}

static JsonNode *
m_live (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorVscriptEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GPtrArray)    instances = NULL;
	JsonArray              *active = json_array_new ();
	JsonObject             *hits = json_object_new ();
	JsonObject             *o;
	const gchar            *path;
	const gchar            *paused = NULL;
	guint                   n = 0;
	guint                   i;

	(void) server;
	if (self == NULL)
		return NULL;
	path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	instances = lrg_scripting_graph_dup_instances ();
	for (i = 0; instances != NULL && i < instances->len; i++)
	{
		LrgScriptingGraph *graph = (LrgScriptingGraph *) g_ptr_array_index (instances, i);
		g_auto(GStrv)      ids = NULL;
		guint              a;

		if (!same_script (graph, path))
			continue;
		n++;
		ids = lrg_scripting_graph_dup_active (graph);
		for (a = 0; ids != NULL && ids[a] != NULL; a++)
			json_array_add_string_element (active, ids[a]);
		if (lrg_scripting_graph_is_paused (graph) && paused == NULL)
			paused = lrg_scripting_graph_get_paused_node (graph);
		if (self->program != NULL)
		{
			LrgVscriptGraph *main = lrg_vscript_program_get_main (self->program);
			guint            k;

			for (k = 0; k < lrg_vscript_graph_get_n_nodes (main); k++)
			{
				const gchar *id = lrg_vscript_node_get_id (lrg_vscript_graph_get_nth_node (main, k));
				guint        count = lrg_scripting_graph_get_hit (graph, id);

				if (count > 0)
					json_object_set_int_member (hits, id, (gint64) count);
			}
		}
	}
	o = json_object_new ();
	json_object_set_boolean_member (o, "playing", n > 0);
	json_object_set_int_member (o, "instances", (gint64) n);
	json_object_set_array_member (o, "active", active);
	json_object_set_object_member (o, "hits", hits);
	if (paused != NULL)
		json_object_set_string_member (o, "paused_at", paused);
	return object_node (o);
}

static JsonNode *
m_breakpoint (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorVscriptEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar            *node;
	const gchar            *path;
	gboolean                enabled = FALSE;
	g_autoptr(GPtrArray)    instances = NULL;
	JsonObject             *o;
	guint                   i;

	(void) server;
	if (self == NULL || (node = require_string (params, "node", error)) == NULL)
		return NULL;
	if (!param_bool (params, "enabled", &enabled))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing boolean parameter 'enabled'");
		return NULL;
	}
	path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	lrg_scripting_graph_set_breakpoint (NULL, path, node, enabled);
	instances = lrg_scripting_graph_dup_instances ();
	for (i = 0; instances != NULL && i < instances->len; i++)
	{
		LrgScriptingGraph *graph = (LrgScriptingGraph *) g_ptr_array_index (instances, i);

		if (same_script (graph, path))
			lrg_scripting_graph_set_breakpoint (graph, path, node, enabled);
	}
	o = json_object_new ();
	json_object_set_string_member (o, "node", node);
	json_object_set_boolean_member (o, "enabled", enabled);
	return object_node (o);
}

static JsonNode *
m_continue (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorVscriptEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GPtrArray)    instances = NULL;
	const gchar            *path;
	JsonObject             *o;
	guint                   resumed = 0;
	guint                   i;

	(void) server;
	if (self == NULL)
		return NULL;
	path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	instances = lrg_scripting_graph_dup_instances ();
	for (i = 0; instances != NULL && i < instances->len; i++)
	{
		LrgScriptingGraph *graph = (LrgScriptingGraph *) g_ptr_array_index (instances, i);
		g_autoptr(GError)  err = NULL;

		if (!same_script (graph, path) || !lrg_scripting_graph_is_paused (graph))
			continue;
		if (lrg_scripting_graph_continue (graph, &err))
			resumed++;
	}
	o = json_object_new ();
	json_object_set_int_member (o, "resumed", (gint64) resumed);
	return object_node (o);
}

static JsonNode *
m_save (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorVscriptEditor *self = need_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)       err = NULL;
	JsonObject             *o;

	(void) server;
	if (self == NULL)
		return NULL;
	if (!lrg_editor_document_save (lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self)), &err))
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	return object_node (o);
}

void
lrg_editor_vscript_editor_install (LrgEditorApp *app)
{
	static const struct
	{
		const gchar           *name;
		LrgEditorServerMethod  func;
	} methods[] = {
		{ "vscript.create", m_create },
		{ "vscript.open", m_open },
		{ "vscript.get", m_get },
		{ "vscript.node_types", m_node_types },
		{ "vscript.gi_search", m_gi_search },
		{ "vscript.add_node", m_add_node },
		{ "vscript.remove_node", m_remove_node },
		{ "vscript.set_node", m_set_node },
		{ "vscript.connect", m_connect },
		{ "vscript.disconnect", m_disconnect },
		{ "vscript.set_variable", m_set_variable },
		{ "vscript.add_subgraph", m_add_subgraph },
		{ "vscript.remove_subgraph", m_remove_subgraph },
		{ "vscript.add_comment", m_add_comment },
		{ "vscript.remove_comment", m_remove_comment },
		{ "vscript.set_program", m_set_program },
		{ "vscript.select", m_select },
		{ "vscript.frame", m_frame },
		{ "vscript.palette", m_palette },
		{ "vscript.validate", m_validate },
		{ "vscript.run", m_run },
		{ "vscript.live", m_live },
		{ "vscript.breakpoint", m_breakpoint },
		{ "vscript.continue", m_continue },
		{ "vscript.save", m_save }
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
	lrg_editor_data_editor_install (app);
	for (i = 0; i < G_N_ELEMENTS (methods); i++)
		lrg_editor_server_register_method (server, methods[i].name, methods[i].func, app, NULL);
}
