/* lrg-editor-shader-graph-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorShaderGraphEditor: the `.rshadergraph` editor (see the header).
 *
 * Layout:
 *
 *   +---------+------------------------------+-----------------+
 *   | palette |                              |  preview sphere |
 *   | [find ] |   LrgNodeGraph canvas        +-----------------+
 *   | INPUTS  |   (cards, typed ports,       | [Node] [GLSL]   |
 *   |  UV     |    red cards = problems)     |  property grid  |
 *   |  ...    |                              |  or the code    |
 *   +---------+------------------------------+-----------------+
 *
 * Three representations stay in step:
 *
 *  - the LrgEditorDocument (the truth, undo, save, external changes),
 *  - an LrgShaderGraph model parsed from it (validation, generation),
 *  - the LrgNodeGraph widget (what the user drags).
 *
 * Canvas gestures (wire, move, delete, duplicate) edit the model right
 * away and queue a commit; the commit (on idle, so a delete with its
 * five link removals is one step) serializes the model and replaces the
 * document root inside a labelled group. While committing, refresh()
 * keeps the widget as it is (it already shows the change). Any other
 * document change (undo, redo, reload, the YAML on disk) re-parses the
 * model and rebuilds the widget. API and IPC edits commit at once.
 *
 * After every change the graph is validated (problem nodes get a red
 * accent and a message body), the fragment stage is generated for the
 * GLSL tab and a fresh LrgShaderAsset is generated for the preview,
 * which compiles it lazily on its next frame.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-shader-graph-editor.h"
#include "data/lrg-editor-material-editor.h"
#include "lrg-editor-app-private.h"
#include "lrg-editor-console-panel.h"
#include "../../ui/lrg-splitter.h"
#include "../../ui/lrg-property-grid.h"
#include "../../ui/lrg-property-source.h"
#include "../../ui/lrg-list-view.h"
#include "../../ui/lrg-tab-view.h"
#include "../../ui/lrg-text-editor.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-syntax.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../paint/lrg-paint-list.h"
#include "../../paint/lrg-paint-types.h"
#include <json-glib/json-glib.h>
#include <errno.h>
#include <string.h>
#include <raylib.h>

#define INSTALLED_KEY  "lrg-editor-shadergraph-installed"
#define BODY_W         (190.0f)

/* ==========================================================================
 * Palette rows
 * ========================================================================== */

#define SG_TYPE_PALETTE_ROW (sg_palette_row_get_type ())
G_DECLARE_FINAL_TYPE (SgPaletteRow, sg_palette_row, SG, PALETTE_ROW, GObject)

struct _SgPaletteRow
{
	GObject  parent_instance;
	gchar   *type;      /* NULL for a category header */
	gchar   *label;
	gchar   *category;
};

G_DEFINE_FINAL_TYPE (SgPaletteRow, sg_palette_row, G_TYPE_OBJECT)

static void
sg_palette_row_finalize (GObject *object)
{
	SgPaletteRow *self = SG_PALETTE_ROW (object);

	g_free (self->type);
	g_free (self->label);
	g_free (self->category);
	G_OBJECT_CLASS (sg_palette_row_parent_class)->finalize (object);
}

static void
sg_palette_row_class_init (SgPaletteRowClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = sg_palette_row_finalize;
}

static void
sg_palette_row_init (SgPaletteRow *self)
{
}

/* The theme role of a node category (palette dots, card headers). */
static LrgThemeColor
category_role (const gchar *category)
{
	if (g_strcmp0 (category, "input") == 0)
		return LRG_THEME_COLOR_LINK;
	if (g_strcmp0 (category, "math") == 0)
		return LRG_THEME_COLOR_ACCENT;
	if (g_strcmp0 (category, "texture") == 0)
		return LRG_THEME_COLOR_SUCCESS;
	if (g_strcmp0 (category, "procedural") == 0)
		return LRG_THEME_COLOR_WARNING;
	if (g_strcmp0 (category, "output") == 0)
		return LRG_THEME_COLOR_DANGER;
	return LRG_THEME_COLOR_TEXT_MUTED;
}

static const gchar *
category_title (const gchar *category)
{
	if (g_strcmp0 (category, "input") == 0)
		return "INPUTS";
	if (g_strcmp0 (category, "math") == 0)
		return "MATHS";
	if (g_strcmp0 (category, "texture") == 0)
		return "TEXTURE";
	if (g_strcmp0 (category, "procedural") == 0)
		return "PROCEDURAL";
	if (g_strcmp0 (category, "output") == 0)
		return "OUTPUT";
	return "OTHER";
}

/* ==========================================================================
 * Instance
 * ========================================================================== */

#define SG_TYPE_SOURCE (sg_source_get_type ())
G_DECLARE_FINAL_TYPE (SgSource, sg_source, SG, SOURCE, GObject)

struct _LrgEditorShaderGraphEditor
{
	LrgEditorDataEditor  parent_instance;

	LrgShaderGraph      *model;
	gchar               *parse_error;

	/* widgets */
	LrgNodeGraph        *canvas;
	LrgTextInput        *filter;
	LrgListView         *palette;
	GListStore          *palette_rows;
	LrgWidget           *preview;
	LrgTabView          *tabs;
	LrgPropertyGrid     *grid;
	SgSource            *source;
	LrgTextEditor       *code;

	/* state */
	gchar               *selected;
	gchar               *glsl;
	LrgShaderAsset      *asset;
	gboolean             syncing;        /* widget being rebuilt: ignore its signals */
	gboolean             committing;     /* our own document write */
	gboolean             palette_syncing;
	gchar               *pending_label;  /* queued commit */
	guint                commit_id;
	gboolean             rebuild_widget; /* after the queued commit */
	gboolean             grid_stale;
	guint                framed;         /* frames the view was fitted on */
	guint                commits;        /* commit_now() calls, for gestures */
};

G_DEFINE_FINAL_TYPE (LrgEditorShaderGraphEditor, lrg_editor_shader_graph_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void rebuild_canvas (LrgEditorShaderGraphEditor *self);
static void after_change (LrgEditorShaderGraphEditor *self);
static void rebuild_grid (LrgEditorShaderGraphEditor *self);
static void flush_commit (LrgEditorShaderGraphEditor *self);

static LrgEditorDocument *
doc_of (LrgEditorShaderGraphEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static LrgEditorApp *
app_of (LrgEditorShaderGraphEditor *self)
{
	return lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
}

/* ==========================================================================
 * Model <-> document
 * ========================================================================== */

/* Parses the document into the model (keeps the old one on failure). */
static void
load_model (LrgEditorShaderGraphEditor *self)
{
	g_autofree gchar         *text = lrg_editor_document_to_data (doc_of (self), NULL);
	g_autoptr(GError)         error = NULL;
	LrgShaderGraph           *graph = lrg_shader_graph_new_from_data (text, -1, &error);

	g_clear_pointer (&self->parse_error, g_free);
	if (graph == NULL)
	{
		self->parse_error = g_strdup (error->message);
		if (self->model == NULL)
			self->model = lrg_shader_graph_new ();
		return;
	}
	g_clear_object (&self->model);
	self->model = graph;
	if (self->selected != NULL && !lrg_shader_graph_has_node (self->model, self->selected))
		g_clear_pointer (&self->selected, g_free);
}

/* Writes the model into the document as one undo step. */
static void
commit_now (LrgEditorShaderGraphEditor *self,
            const gchar                *label)
{
	LrgEditorDocument *doc = doc_of (self);
	g_autofree gchar  *text = NULL;
	g_autoptr(GError)  error = NULL;

	g_clear_handle_id (&self->commit_id, g_source_remove);
	g_clear_pointer (&self->pending_label, g_free);
	text = lrg_shader_graph_save_to_data (self->model, NULL, &error);
	if (text == NULL)
	{
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return;
	}
	self->commits++;
	self->committing = TRUE;
	lrg_editor_document_begin_group (doc, label != NULL ? label : "Edit Graph");
	if (!lrg_editor_document_replace (doc, "", text, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	lrg_editor_document_end_group (doc);
	self->committing = FALSE;
	if (self->rebuild_widget)
	{
		self->rebuild_widget = FALSE;
		rebuild_canvas (self);
	}
	after_change (self);
}

static gboolean
commit_idle (gpointer user_data)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (user_data);
	g_autofree gchar           *label = g_steal_pointer (&self->pending_label);

	self->commit_id = 0;
	commit_now (self, label);
	return G_SOURCE_REMOVE;
}

/* Coalesces the canvas edits of one gesture into one commit. */
static void
queue_commit (LrgEditorShaderGraphEditor *self,
              const gchar                *label)
{
	if (self->pending_label == NULL)
		self->pending_label = g_strdup (label);
	if (self->commit_id == 0)
		self->commit_id = g_idle_add (commit_idle, self);
}

static void
flush_commit (LrgEditorShaderGraphEditor *self)
{
	if (self->commit_id != 0)
	{
		g_autofree gchar *label = g_steal_pointer (&self->pending_label);

		commit_now (self, label);
	}
}

/* ==========================================================================
 * The canvas
 * ========================================================================== */

static const gchar *
port_type_name (LrgShaderPortType type)
{
	switch (type)
	{
	case LRG_SHADER_PORT_FLOAT:
		return "float";
	case LRG_SHADER_PORT_VEC2:
		return "vec2";
	case LRG_SHADER_PORT_VEC3:
		return "vec3";
	case LRG_SHADER_PORT_VEC4:
		return "vec4";
	case LRG_SHADER_PORT_SAMPLER2D:
		return "sampler2D";
	default:
		return NULL;
	}
}

/* The card title: the library label, plus the uniform name of a
 * parameter ("Parameter · tint"). */
static gchar *
card_title (LrgShaderGraph *graph,
            const gchar    *id)
{
	const gchar *type = lrg_shader_graph_get_node_type (graph, id);
	const gchar *label = lrg_shader_node_type_get_label (type);
	const gchar *name = g_strcmp0 (type, "parameter") == 0 ? lrg_shader_graph_get_node_param (graph, id, "name")
	                                                       : NULL;

	if (name != NULL)
		return g_strdup_printf ("%s \xc2\xb7 %s", label, name);
	return g_strdup (label != NULL ? label : type);
}

/* Body of a card with problems: the messages in the danger colour. */
typedef struct
{
	gchar *text;
	guint  lines;
} ProblemBody;

static void
problem_body_free (gpointer data)
{
	ProblemBody *b = data;

	g_free (b->text);
	g_free (b);
}

static void
problem_body_paint (LrgCustomWidget *widget,
                    LrgPaintList    *list,
                    gfloat           x,
                    gfloat           y,
                    gfloat           width,
                    gfloat           height,
                    gpointer         user_data)
{
	ProblemBody    *b = user_data;
	LrgTheme       *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat          fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor *danger = lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);
	GrlColor        wash;

	lrg_paint_color_with_alpha (danger, 0.14f, &wash);
	lrg_paint_list_rect (list, x, y, width, height, 4.0f, &wash);
	lrg_paint_list_rect (list, x, y, 3.0f, height, 1.5f, danger);
	/* the card lays the body out at its zoomed size: shrink the text to
	 * fit (it is laid out at full size when zoomed in) */
	fs = MIN (fs, (height - 6.0f) / (gfloat) MAX (b->lines, 1u) / 1.15f);
	if (fs < 4.0f)
		return;
	lrg_paint_list_push_clip (list, x, y, width, height);
	lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), b->text, x + 8.0f, y + 3.0f, fs, 0.0f, danger);
	lrg_paint_list_pop_clip (list);
}

static LrgWidget *
problem_body_new (const gchar *text)
{
	static const LrgCustomWidgetFuncs funcs = { NULL, problem_body_paint, NULL, NULL, { NULL } };
	LrgCustomWidget *w = lrg_custom_widget_new ();
	ProblemBody     *b = g_new0 (ProblemBody, 1);
	guint            lines = 1;
	const gchar     *p;

	/* wrap at about 30 characters (the card is narrow) */
	{
		GString *out = g_string_new (NULL);
		guint    col = 0;

		for (p = text; *p != '\0'; p++)
		{
			if (*p == '\n')
				col = 0;
			else if (*p == ' ' && col >= 28)
			{
				g_string_append_c (out, '\n');
				col = 0;
				continue;
			}
			else
				col++;
			g_string_append_c (out, *p);
		}
		b->text = g_string_free (out, FALSE);
	}
	for (p = b->text; *p != '\0'; p++)
		if (*p == '\n')
			lines++;
	b->lines = lines;
	lrg_custom_widget_set_funcs (w, &funcs, b, problem_body_free);
	lrg_custom_widget_set_preferred_width (w, BODY_W);
	lrg_custom_widget_set_preferred_height (w, 8.0f + (gfloat) lines * 15.0f);
	lrg_widget_set_pointer_transparent (LRG_WIDGET (w), TRUE);
	return LRG_WIDGET (w);
}

/* Problem accents and bodies after a validation. */
static void
decorate_cards (LrgEditorShaderGraphEditor *self)
{
	GPtrArray           *problems = lrg_shader_graph_get_problems (self->model);
	g_autoptr(GHashTable) text = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
	g_auto(GStrv)         ids = lrg_shader_graph_get_node_ids (self->model);
	LrgTheme             *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	g_autoptr(GPtrArray)  owned = g_ptr_array_new_with_free_func (g_free);
	guint                 i;

	for (i = 0; problems != NULL && i < problems->len; i++)
	{
		LrgShaderGraphProblem *p = g_ptr_array_index (problems, i);
		const gchar           *id = lrg_shader_graph_problem_get_node_id (p);
		const gchar           *old;
		gchar                 *joined;

		if (id == NULL)
			continue;
		old = g_hash_table_lookup (text, id);
		joined = old != NULL ? g_strconcat (old, "\n", lrg_shader_graph_problem_get_message (p), NULL)
		                     : g_strdup (lrg_shader_graph_problem_get_message (p));
		g_ptr_array_add (owned, joined);
		g_hash_table_insert (text, (gpointer) id, joined);
	}

	self->syncing = TRUE;
	for (i = 0; ids != NULL && ids[i] != NULL; i++)
	{
		LrgGraphNode *card = lrg_node_graph_get_node (self->canvas, ids[i]);
		const gchar  *msg = g_hash_table_lookup (text, ids[i]);

		if (card == NULL)
			continue;
		if (msg != NULL)
		{
			g_autoptr(LrgWidget) body = problem_body_new (msg);

			lrg_graph_node_set_accent (card, lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
			lrg_graph_node_set_body (card, body);
		}
		else
		{
			lrg_graph_node_set_accent (card, NULL);
			if (lrg_graph_node_get_body (card) != NULL)
				lrg_graph_node_set_body (card, NULL);
		}
		{
			g_autofree gchar *title = card_title (self->model, ids[i]);

			lrg_graph_node_set_title (card, title);
		}
	}
	self->syncing = FALSE;
}

/* Rebuilds every card and wire from the model. */
static void
rebuild_canvas (LrgEditorShaderGraphEditor *self)
{
	g_auto(GStrv) ids = lrg_shader_graph_get_node_ids (self->model);
	guint         i, n;

	self->syncing = TRUE;
	lrg_node_graph_clear (self->canvas);
	for (i = 0; ids != NULL && ids[i] != NULL; i++)
	{
		const gchar          *type = lrg_shader_graph_get_node_type (self->model, ids[i]);
		g_autofree gchar     *title = card_title (self->model, ids[i]);
		g_autoptr(LrgGraphNode) card = lrg_graph_node_new (ids[i], title);
		g_auto(GStrv)         ins = lrg_shader_node_type_get_inputs (type);
		g_auto(GStrv)         outs = lrg_shader_node_type_get_outputs (type);
		gfloat                x = 0.0f, y = 0.0f;
		guint                 k;

		lrg_graph_node_set_category (card, lrg_shader_node_type_get_category (type));
		lrg_graph_node_set_min_width (card, 150.0f);
		for (k = 0; ins != NULL && ins[k] != NULL; k++)
		{
			LrgShaderPortType pt = lrg_shader_node_type_get_port_type (type, ins[k], FALSE);

			lrg_graph_node_add_port (card, LRG_GRAPH_PORT_INPUT, ins[k], port_type_name (pt));
		}
		for (k = 0; outs != NULL && outs[k] != NULL; k++)
		{
			LrgShaderPortType pt = lrg_shader_node_type_get_port_type (type, outs[k], TRUE);

			/* a parameter's value is typed by its "type" param */
			if (g_strcmp0 (type, "parameter") == 0)
			{
				const gchar *ptype = lrg_shader_graph_get_node_param (self->model, ids[i], "type");

				lrg_graph_node_add_port (card, LRG_GRAPH_PORT_OUTPUT, outs[k],
				                         g_strcmp0 (ptype, "color") == 0 ? "vec4" : ptype);
			}
			else
				lrg_graph_node_add_port (card, LRG_GRAPH_PORT_OUTPUT, outs[k], port_type_name (pt));
		}
		lrg_shader_graph_get_node_position (self->model, ids[i], &x, &y);
		lrg_graph_node_set_position (card, x, y);
		lrg_node_graph_append_node (self->canvas, card);
	}
	n = lrg_shader_graph_get_n_links (self->model);
	for (i = 0; i < n; i++)
	{
		const gchar *fn, *fp, *tn, *tp;

		if (lrg_shader_graph_get_link (self->model, i, &fn, &fp, &tn, &tp))
			lrg_node_graph_connect_by_name (self->canvas, fn, fp, tn, tp, NULL);
	}
	if (self->selected != NULL)
	{
		LrgGraphNode *card = lrg_node_graph_get_node (self->canvas, self->selected);

		if (card != NULL)
			lrg_node_graph_select_node (self->canvas, card, FALSE);
	}
	self->syncing = FALSE;
}

/* --- canvas -> model --------------------------------------------------------- */

static gboolean
on_can_connect (LrgNodeGraph *graph,
                LrgGraphPort *output,
                LrgGraphPort *input,
                gpointer      user_data)
{
	gboolean a = g_strcmp0 (lrg_graph_port_get_type_name (output), "sampler2D") == 0;
	gboolean b = g_strcmp0 (lrg_graph_port_get_type_name (input), "sampler2D") == 0;

	/* everything numeric may connect (validation reports widening); a
	 * sampler only feeds a sampler */
	return a == b;
}

static void
on_link_added (LrgNodeGraph *graph,
               LrgGraphLink *link,
               gpointer      user_data)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (user_data);
	LrgGraphPort               *out = lrg_graph_link_get_output (link);
	LrgGraphPort               *in = lrg_graph_link_get_input (link);

	if (self->syncing)
		return;
	if (lrg_shader_graph_connect (self->model, lrg_graph_node_get_id (lrg_graph_port_get_node (out)),
	                              lrg_graph_port_get_name (out), lrg_graph_node_get_id (lrg_graph_port_get_node (in)),
	                              lrg_graph_port_get_name (in), NULL))
		queue_commit (self, "Connect");
}

static void
on_link_removed (LrgNodeGraph *graph,
                 LrgGraphLink *link,
                 gpointer      user_data)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (user_data);
	LrgGraphPort               *out = lrg_graph_link_get_output (link);
	LrgGraphPort               *in = lrg_graph_link_get_input (link);
	const gchar                *to = lrg_graph_node_get_id (lrg_graph_port_get_node (in));
	const gchar                *from = lrg_graph_node_get_id (lrg_graph_port_get_node (out));
	guint                       i, n;

	if (self->syncing)
		return;
	/* only the link the canvas removed: a replacing link may already be
	 * in the model */
	n = lrg_shader_graph_get_n_links (self->model);
	for (i = 0; i < n; i++)
	{
		const gchar *fn, *fp, *tn, *tp;

		lrg_shader_graph_get_link (self->model, i, &fn, &fp, &tn, &tp);
		if (g_strcmp0 (tn, to) == 0 && g_strcmp0 (tp, lrg_graph_port_get_name (in)) == 0 &&
		    g_strcmp0 (fn, from) == 0 && g_strcmp0 (fp, lrg_graph_port_get_name (out)) == 0)
		{
			lrg_shader_graph_disconnect (self->model, to, lrg_graph_port_get_name (in));
			queue_commit (self, "Disconnect");
			return;
		}
	}
}

static void
on_node_moved (LrgNodeGraph *graph,
               LrgGraphNode *card,
               gpointer      user_data)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (user_data);

	if (self->syncing)
		return;
	if (lrg_shader_graph_set_node_position (self->model, lrg_graph_node_get_id (card),
	                                        (gfloat) (gint) lrg_graph_node_get_x (card),
	                                        (gfloat) (gint) lrg_graph_node_get_y (card)))
		queue_commit (self, "Move Node");
}

static void
on_node_removed (LrgNodeGraph *graph,
                 LrgGraphNode *card,
                 gpointer      user_data)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (user_data);

	if (self->syncing)
		return;
	if (lrg_shader_graph_remove_node (self->model, lrg_graph_node_get_id (card)))
	{
		if (g_strcmp0 (self->selected, lrg_graph_node_get_id (card)) == 0)
			g_clear_pointer (&self->selected, g_free);
		self->grid_stale = TRUE;
		queue_commit (self, "Delete Node");
	}
}

/* Copies a node's parameters and input values onto another one. */
static void
copy_node_data (LrgShaderGraph *graph,
                const gchar    *from,
                const gchar    *to)
{
	static const gchar * const strs[] = { "name", "type", "hint", "texture", "mask", "blend", "cull" };
	static const gchar * const nums[] = { "default", "range", "value" };
	g_auto(GStrv) ins = lrg_shader_node_type_get_inputs (lrg_shader_graph_get_node_type (graph, from));
	gfloat        v[4];
	guint         i, n;

	for (i = 0; i < G_N_ELEMENTS (strs); i++)
		if (lrg_shader_graph_get_node_param (graph, from, strs[i]) != NULL)
			lrg_shader_graph_set_node_param (graph, to, strs[i], lrg_shader_graph_get_node_param (graph, from, strs[i]));
	for (i = 0; i < G_N_ELEMENTS (nums); i++)
		if ((n = lrg_shader_graph_get_node_values (graph, from, nums[i], v)) > 0)
			lrg_shader_graph_set_node_values (graph, to, nums[i], v, n);
	for (i = 0; ins != NULL && ins[i] != NULL; i++)
		if ((n = lrg_shader_graph_get_input_value (graph, from, ins[i], v)) > 0)
			lrg_shader_graph_set_input_value (graph, to, ins[i], v, n);
}

/* A free uniform name "<base>", "<base>2", ... among parameter nodes. */
static gchar *
free_uniform_name (LrgShaderGraph *graph,
                   const gchar    *base)
{
	g_auto(GStrv) ids = lrg_shader_graph_get_node_ids (graph);
	guint         n;

	for (n = 1; ; n++)
	{
		g_autofree gchar *name = n == 1 ? g_strdup (base) : g_strdup_printf ("%s%u", base, n);
		gboolean          taken = FALSE;
		guint             i;

		for (i = 0; ids != NULL && ids[i] != NULL; i++)
			if (g_strcmp0 (lrg_shader_graph_get_node_param (graph, ids[i], "name"), name) == 0)
				taken = TRUE;
		if (!taken || n > 9999)
			return g_steal_pointer (&name);
	}
}

/* Ctrl+D: the canvas made a card with its own id; the model gets a node
 * with a GLSL-safe id and the canvas is rebuilt after the commit. */
static void
on_node_duplicated (LrgNodeGraph *graph,
                    LrgGraphNode *original,
                    LrgGraphNode *copy,
                    gpointer      user_data)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (user_data);
	const gchar                *src = lrg_graph_node_get_id (original);
	const gchar                *type;
	const gchar                *id;

	if (self->syncing || !lrg_shader_graph_has_node (self->model, src))
		return;
	type = lrg_shader_graph_get_node_type (self->model, src);
	id = lrg_shader_graph_add_node (self->model, type, NULL, NULL);
	if (id == NULL)
		return;
	copy_node_data (self->model, src, id);
	if (g_strcmp0 (type, "parameter") == 0)
	{
		g_autofree gchar *name = free_uniform_name (self->model,
		                                            lrg_shader_graph_get_node_param (self->model, src, "name"));

		lrg_shader_graph_set_node_param (self->model, id, "name", name);
	}
	lrg_shader_graph_set_node_position (self->model, id, (gfloat) (gint) lrg_graph_node_get_x (copy),
	                                    (gfloat) (gint) lrg_graph_node_get_y (copy));
	self->rebuild_widget = TRUE;
	queue_commit (self, "Duplicate Node");
}

static void
on_canvas_selection (LrgNodeGraph *graph,
                     gpointer      user_data)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (user_data);
	g_autoptr(GList)            sel = NULL;
	const gchar                *id = NULL;

	if (self->syncing)
		return;
	sel = lrg_node_graph_get_selected_nodes (graph);
	if (sel != NULL && sel->next == NULL)
		id = lrg_graph_node_get_id (sel->data);
	if (g_strcmp0 (id, self->selected) == 0)
		return;
	g_free (self->selected);
	self->selected = g_strdup (id);
	self->grid_stale = TRUE;
}

/* ==========================================================================
 * After a change: validation, code, preview asset
 * ========================================================================== */

static void
after_change (LrgEditorShaderGraphEditor *self)
{
	g_autoptr(GError) error = NULL;

	lrg_shader_graph_validate (self->model, NULL);
	decorate_cards (self);

	g_clear_pointer (&self->glsl, g_free);
	self->glsl = lrg_shader_graph_generate_fragment (self->model, NULL);
	if (self->code != NULL)
	{
		lrg_text_editor_set_read_only (self->code, FALSE);
		lrg_text_editor_set_text (self->code, self->glsl != NULL ? self->glsl
		                          : "/* The graph has problems: fix the red nodes to see the code. */\n");
		lrg_text_editor_clear_history (self->code);
		lrg_text_editor_set_read_only (self->code, TRUE);
	}

	/* keep-last-good: an invalid graph leaves the preview on the last
	 * valid shader; the previous program dies with its asset (needs the
	 * context) */
	{
		LrgShaderAsset *fresh = lrg_shader_graph_generate (self->model, &error);

		if (fresh != NULL)
		{
			if (self->asset != NULL && IsWindowReady ())
				lrg_shader_asset_release (self->asset);
			g_clear_object (&self->asset);
			self->asset = fresh;
		}
	}
	self->grid_stale = TRUE;
}

/* ==========================================================================
 * Palette
 * ========================================================================== */

static void
fill_palette (LrgEditorShaderGraphEditor *self)
{
	g_auto(GStrv)        types = lrg_shader_graph_list_node_types ();
	g_autoptr(GPtrArray) rows = g_ptr_array_new_with_free_func (g_object_unref);
	const gchar         *needle = self->filter != NULL ? lrg_text_input_get_text (self->filter) : NULL;
	g_autofree gchar    *lower = needle != NULL ? g_utf8_casefold (needle, -1) : NULL;
	const gchar         *last = NULL;
	guint                i;

	for (i = 0; types != NULL && types[i] != NULL; i++)
	{
		const gchar      *category = lrg_shader_node_type_get_category (types[i]);
		const gchar      *label = lrg_shader_node_type_get_label (types[i]);
		g_autofree gchar *hay = g_utf8_casefold (label != NULL ? label : types[i], -1);
		SgPaletteRow     *row;

		if (lower != NULL && *lower != '\0' && strstr (hay, lower) == NULL && strstr (types[i], lower) == NULL)
			continue;
		if (g_strcmp0 (category, last) != 0)
		{
			row = g_object_new (SG_TYPE_PALETTE_ROW, NULL);
			row->category = g_strdup (category);
			row->label = g_strdup (category_title (category));
			g_ptr_array_add (rows, row);
			last = category;
		}
		row = g_object_new (SG_TYPE_PALETTE_ROW, NULL);
		row->type = g_strdup (types[i]);
		row->label = g_strdup (label != NULL ? label : types[i]);
		row->category = g_strdup (category);
		g_ptr_array_add (rows, row);
	}
	self->palette_syncing = TRUE;
	g_list_store_splice (self->palette_rows, 0, g_list_model_get_n_items (G_LIST_MODEL (self->palette_rows)),
	                     rows->pdata, rows->len);
	lrg_list_view_unselect_all (self->palette);
	self->palette_syncing = FALSE;
}

typedef struct
{
	SgPaletteRow *row;   /* borrowed while bound */
} PaletteCell;

static void
palette_cell_paint (LrgCustomWidget *widget,
                    LrgPaintList    *list,
                    gfloat           x,
                    gfloat           y,
                    gfloat           width,
                    gfloat           height,
                    gpointer         user_data)
{
	PaletteCell    *c = user_data;
	LrgTheme       *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont        *font = lrg_theme_get_ui_font (theme);
	gfloat          fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor *role;

	if (c->row == NULL)
		return;
	role = lrg_theme_get_color (theme, category_role (c->row->category));
	if (c->row->type == NULL)
	{
		/* a header: tracked caps in the category colour, a hairline */
		GrlColor line;

		lrg_paint_list_text (list, font, c->row->label, x + 8.0f, y + height - fs - 5.0f, fs * 0.9f, 1.2f, role);
		lrg_paint_color_with_alpha (role, 0.35f, &line);
		lrg_paint_list_rect (list, x + 8.0f, y + height - 2.0f, width - 16.0f, 1.0f, 0.0f, &line);
		return;
	}
	lrg_paint_list_circle (list, x + 16.0f, y + height * 0.5f, 4.0f, role);
	lrg_paint_list_text (list, font, c->row->label, x + 28.0f, y + (height - fs) * 0.5f, fs, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
}

static LrgWidget *
palette_create (LrgListView *view,
                gpointer     user_data)
{
	static const LrgCustomWidgetFuncs funcs = { NULL, palette_cell_paint, NULL, NULL, { NULL } };
	LrgCustomWidget *w = lrg_custom_widget_new ();
	PaletteCell     *c = g_new0 (PaletteCell, 1);

	lrg_custom_widget_set_funcs (w, &funcs, c, g_free);
	g_object_set_data (G_OBJECT (w), "sg-cell", c);
	return LRG_WIDGET (w);
}

static void
palette_bind (LrgListView *view,
              LrgWidget   *row,
              GObject     *item,
              guint        position,
              gpointer     user_data)
{
	PaletteCell *c = g_object_get_data (G_OBJECT (row), "sg-cell");

	if (c != NULL)
		c->row = SG_PALETTE_ROW (item);
}

/* A click on a node type adds it in the middle of the view. */
static void
on_palette_selection (LrgListView *view,
                      gpointer     user_data)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (user_data);
	guint                       pos = lrg_list_view_get_selected (view);
	g_autoptr(SgPaletteRow)     row = NULL;
	g_autoptr(GError)           error = NULL;
	gfloat                      cx = 0.0f, cy = 0.0f;

	if (self->palette_syncing || pos >= g_list_model_get_n_items (G_LIST_MODEL (self->palette_rows)))
		return;
	row = g_list_model_get_item (G_LIST_MODEL (self->palette_rows), pos);
	self->palette_syncing = TRUE;
	lrg_list_view_unselect_all (view);
	self->palette_syncing = FALSE;
	if (row->type == NULL)
		return;
	lrg_node_graph_get_center (self->canvas, &cx, &cy);
	if (lrg_editor_shader_graph_editor_add_node (self, row->type, NULL, cx - 75.0f, cy - 40.0f, &error) == NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_filter_changed (LrgTextInput *input,
                   gpointer      user_data)
{
	fill_palette (LRG_EDITOR_SHADER_GRAPH_EDITOR (user_data));
}

/* ==========================================================================
 * SgSource: the selected node in the property grid
 * ========================================================================== */

typedef enum
{
	SG_STRING,
	SG_ENUM,
	SG_REF,
	SG_NUMBER,
	SG_VECTOR,
	SG_COLOR
} SgKind;

typedef enum
{
	SG_TARGET_PARAM,     /* lrg_shader_graph_set_node_param */
	SG_TARGET_VALUES,    /* lrg_shader_graph_set_node_values */
	SG_TARGET_INPUT      /* lrg_shader_graph_set_input_value */
} SgTarget;

typedef struct
{
	gchar    *name;
	gchar    *label;
	gchar    *group;
	gchar    *hint;
	gchar    *key;       /* param key or port */
	SgKind    kind;
	SgTarget  target;
	guint     width;     /* numbers: 1 .. 4 */
} SgField;

static void
sg_field_free (gpointer data)
{
	SgField *f = data;

	g_free (f->name);
	g_free (f->label);
	g_free (f->group);
	g_free (f->hint);
	g_free (f->key);
	g_free (f);
}

struct _SgSource
{
	GObject                     parent_instance;
	LrgEditorShaderGraphEditor *editor;     /* weak */
	GPtrArray                  *fields;
	gboolean                    in_gesture;
	guint                       grid_gesture;
	guint                       commits_at_begin;
};

static void sg_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (SgSource, sg_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE, sg_source_iface_init))

static SgField *
sg_field_add (GPtrArray   *fields,
              const gchar *name,
              const gchar *label,
              const gchar *group,
              const gchar *key,
              SgKind       kind,
              SgTarget     target,
              guint        width,
              const gchar *hint)
{
	SgField *f = g_new0 (SgField, 1);

	f->name = g_strdup (name);
	f->label = g_strdup (label);
	f->group = g_strdup (group);
	f->key = g_strdup (key);
	f->kind = kind;
	f->target = target;
	f->width = width;
	f->hint = g_strdup (hint);
	g_ptr_array_add (fields, f);
	return f;
}

static SgField *
sg_source_field (SgSource    *self,
                 const gchar *name)
{
	guint i;

	for (i = 0; i < self->fields->len; i++)
		if (g_strcmp0 (((SgField *) g_ptr_array_index (self->fields, i))->name, name) == 0)
			return g_ptr_array_index (self->fields, i);
	return NULL;
}

static guint
width_of (LrgShaderPortType type)
{
	switch (type)
	{
	case LRG_SHADER_PORT_VEC2:
		return 2;
	case LRG_SHADER_PORT_VEC3:
		return 3;
	case LRG_SHADER_PORT_VEC4:
		return 4;
	default:
		return 1;
	}
}

/* The rows of the selected node. */
static void
sg_source_build (SgSource *self)
{
	LrgEditorShaderGraphEditor *ed = self->editor;
	LrgShaderGraph             *g;
	const gchar                *id;
	const gchar                *type;
	g_auto(GStrv)               ins = NULL;
	guint                       i;

	g_ptr_array_set_size (self->fields, 0);
	if (ed == NULL || ed->selected == NULL || !lrg_shader_graph_has_node (ed->model, ed->selected))
		return;
	g = ed->model;
	id = ed->selected;
	type = lrg_shader_graph_get_node_type (g, id);

	if (g_strcmp0 (type, "parameter") == 0)
	{
		const gchar *ptype = lrg_shader_graph_get_node_param (g, id, "type");
		guint        w = g_strcmp0 (ptype, "vec2") == 0 ? 2 : g_strcmp0 (ptype, "vec3") == 0 ? 3
		                 : (g_strcmp0 (ptype, "vec4") == 0 || g_strcmp0 (ptype, "color") == 0) ? 4 : 1;

		sg_field_add (self->fields, "p-name", "Uniform", "Parameter", "name", SG_STRING, SG_TARGET_PARAM, 0, NULL);
		sg_field_add (self->fields, "p-type", "Type", "Parameter", "type", SG_ENUM, SG_TARGET_PARAM, 0,
		              "enum-strings:float,vec2,vec3,vec4,color,sampler2D");
		if (g_strcmp0 (ptype, "sampler2D") == 0)
			sg_field_add (self->fields, "p-texture", "Texture", "Parameter", "texture", SG_REF, SG_TARGET_PARAM, 0,
			              "asset:texture");
		else
		{
			sg_field_add (self->fields, "p-default", "Default", "Parameter", "default",
			              g_strcmp0 (ptype, "color") == 0 ? SG_COLOR : w == 1 ? SG_NUMBER : SG_VECTOR,
			              SG_TARGET_VALUES, w, NULL);
			sg_field_add (self->fields, "p-range", "Range", "Parameter", "range", SG_VECTOR, SG_TARGET_VALUES, 2,
			              NULL);
		}
		sg_field_add (self->fields, "p-hint", "Hint", "Parameter", "hint", SG_STRING, SG_TARGET_PARAM, 0, NULL);
	}
	else if (g_strcmp0 (type, "constant") == 0)
	{
		gfloat v[4];
		guint  n = MAX (lrg_shader_graph_get_node_values (g, id, "value", v), 1u);

		sg_field_add (self->fields, "c-value", "Value", "Constant", "value", n == 1 ? SG_NUMBER : SG_VECTOR,
		              SG_TARGET_VALUES, n, NULL);
	}
	else if (g_strcmp0 (type, "swizzle") == 0)
		sg_field_add (self->fields, "s-mask", "Mask", "Swizzle", "mask", SG_STRING, SG_TARGET_PARAM, 0, NULL);
	else if (g_str_has_suffix (type, "_output"))
	{
		sg_field_add (self->fields, "o-blend", "Blend", "Output", "blend", SG_ENUM, SG_TARGET_PARAM, 0,
		              "enum-strings:,opaque,cutout,alpha,additive,multiply");
		sg_field_add (self->fields, "o-cull", "Cull", "Output", "cull", SG_ENUM, SG_TARGET_PARAM, 0,
		              "enum-strings:,back,front,none");
	}

	/* the values of unconnected inputs */
	ins = lrg_shader_node_type_get_inputs (type);
	for (i = 0; ins != NULL && ins[i] != NULL; i++)
	{
		LrgShaderPortType pt = lrg_shader_graph_get_resolved_port_type (g, id, ins[i], FALSE);
		g_autofree gchar *pname = NULL;
		guint             w;
		guint             k, n = lrg_shader_graph_get_n_links (g);
		gboolean          linked = FALSE;
		gchar            *p;

		if (pt == LRG_SHADER_PORT_SAMPLER2D || lrg_shader_node_type_get_port_type (type, ins[i], FALSE) ==
		                                       LRG_SHADER_PORT_SAMPLER2D)
			continue;
		for (k = 0; k < n; k++)
		{
			const gchar *tn, *tp;

			lrg_shader_graph_get_link (g, k, NULL, NULL, &tn, &tp);
			if (g_strcmp0 (tn, id) == 0 && g_strcmp0 (tp, ins[i]) == 0)
				linked = TRUE;
		}
		if (linked)
			continue;
		if (pt == LRG_SHADER_PORT_DYNAMIC)
			pt = lrg_shader_node_type_get_port_type (type, ins[i], FALSE);
		w = width_of (pt);
		pname = g_strconcat ("in-", ins[i], NULL);
		for (p = pname; *p != '\0'; p++)
			if (!g_ascii_isalnum (*p) && *p != '-')
				*p = '-';
		sg_field_add (self->fields, pname, ins[i], "Inputs", ins[i], w == 1 ? SG_NUMBER : SG_VECTOR,
		              SG_TARGET_INPUT, w, NULL);
	}
}

static GType
sg_field_type (const SgField *f)
{
	switch (f->kind)
	{
	case SG_NUMBER:
		return G_TYPE_DOUBLE;
	case SG_COLOR:
		return GRL_TYPE_COLOR;
	case SG_VECTOR:
		return f->width == 2 ? GRL_TYPE_VECTOR2 : f->width == 3 ? GRL_TYPE_VECTOR3 : GRL_TYPE_VECTOR4;
	default:
		return G_TYPE_STRING;
	}
}

static GPtrArray *
sg_source_list (LrgPropertySource *source)
{
	SgSource  *self = SG_SOURCE (source);
	GPtrArray *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	guint      i;

	for (i = 0; i < self->fields->len; i++)
	{
		SgField         *f = g_ptr_array_index (self->fields, i);
		GParamSpec      *pspec;
		LrgPropertyInfo *info;

		if (f->kind == SG_NUMBER)
			pspec = g_param_spec_double (f->name, f->label, NULL, -1.0e6, 1.0e6, 0.0, G_PARAM_READWRITE);
		else if (f->kind == SG_COLOR || f->kind == SG_VECTOR)
			pspec = g_param_spec_boxed (f->name, f->label, NULL, sg_field_type (f), G_PARAM_READWRITE);
		else
			pspec = g_param_spec_string (f->name, f->label, NULL, NULL, G_PARAM_READWRITE);
		g_param_spec_ref_sink (pspec);
		info = lrg_property_info_new (pspec);
		g_param_spec_unref (pspec);
		lrg_property_info_set_label (info, f->label);
		lrg_property_info_set_group (info, f->group);
		if (f->hint != NULL)
			lrg_property_info_set_hint (info, f->hint);
		else if (f->kind == SG_COLOR)
			lrg_property_info_set_hint (info, "color");
		else if (f->kind == SG_VECTOR)
			lrg_property_info_set_hint (info, f->width == 2 ? "vector2" : f->width == 3 ? "vector3" : "vector4");
		g_ptr_array_add (out, info);
	}
	return out;
}

/* Reads up to 4 numbers of a field. */
static guint
sg_read_numbers (LrgEditorShaderGraphEditor *ed,
                 const SgField              *f,
                 gfloat                     *v)
{
	memset (v, 0, sizeof (gfloat) * 4);
	if (f->target == SG_TARGET_INPUT)
		return lrg_shader_graph_get_input_value (ed->model, ed->selected, f->key, v);
	return lrg_shader_graph_get_node_values (ed->model, ed->selected, f->key, v);
}

static gboolean
sg_source_get (LrgPropertySource *source,
               const gchar       *name,
               GValue            *value)
{
	SgSource                   *self = SG_SOURCE (source);
	SgField                    *f = sg_source_field (self, name);
	LrgEditorShaderGraphEditor *ed = self->editor;
	gfloat                      v[4];
	guint                       n;

	if (f == NULL || ed == NULL || ed->selected == NULL)
		return FALSE;
	g_value_init (value, sg_field_type (f));
	switch (f->kind)
	{
	case SG_NUMBER:
		sg_read_numbers (ed, f, v);
		g_value_set_double (value, v[0]);
		break;
	case SG_COLOR:
		{
			GrlColor c;

			n = sg_read_numbers (ed, f, v);
			if (n < 4)
				v[3] = 1.0f;
			c.r = (guint8) CLAMP ((gint) (v[0] * 255.0f + 0.5f), 0, 255);
			c.g = (guint8) CLAMP ((gint) (v[1] * 255.0f + 0.5f), 0, 255);
			c.b = (guint8) CLAMP ((gint) (v[2] * 255.0f + 0.5f), 0, 255);
			c.a = (guint8) CLAMP ((gint) (v[3] * 255.0f + 0.5f), 0, 255);
			g_value_set_boxed (value, &c);
		}
		break;
	case SG_VECTOR:
		n = sg_read_numbers (ed, f, v);
		/* a single stored number splats */
		if (n == 1)
			v[1] = v[2] = v[3] = v[0];
		if (f->width == 2)
		{
			GrlVector2 x = { v[0], v[1] };

			g_value_set_boxed (value, &x);
		}
		else if (f->width == 3)
		{
			GrlVector3 x = { v[0], v[1], v[2] };

			g_value_set_boxed (value, &x);
		}
		else
		{
			GrlVector4 x;

			x.x = v[0];
			x.y = v[1];
			x.z = v[2];
			x.w = v[3];
			g_value_set_boxed (value, &x);
		}
		break;
	default:
		{
			const gchar *s = lrg_shader_graph_get_node_param (ed->model, ed->selected, f->key);

			g_value_set_string (value, s != NULL ? s : "");
		}
		break;
	}
	return TRUE;
}

static gboolean
sg_source_set (LrgPropertySource *source,
               const gchar       *name,
               const GValue      *value)
{
	SgSource                   *self = SG_SOURCE (source);
	SgField                    *f = sg_source_field (self, name);
	LrgEditorShaderGraphEditor *ed = self->editor;
	g_auto(GValue)              v = G_VALUE_INIT;
	gfloat                      n[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	g_autofree gchar           *label = NULL;
	gboolean                    ok = FALSE;

	if (f == NULL || ed == NULL || ed->selected == NULL)
		return FALSE;
	g_value_init (&v, sg_field_type (f));
	if (!g_value_transform (value, &v))
		return FALSE;
	label = g_strdup_printf ("Edit %s", f->label);

	switch (f->kind)
	{
	case SG_NUMBER:
		n[0] = (gfloat) g_value_get_double (&v);
		break;
	case SG_COLOR:
		{
			const GrlColor *c = g_value_get_boxed (&v);

			if (c == NULL)
				return FALSE;
			n[0] = (gfloat) c->r / 255.0f;
			n[1] = (gfloat) c->g / 255.0f;
			n[2] = (gfloat) c->b / 255.0f;
			n[3] = (gfloat) c->a / 255.0f;
		}
		break;
	case SG_VECTOR:
		{
			gconstpointer p = g_value_get_boxed (&v);

			if (p == NULL)
				return FALSE;
			memcpy (n, p, sizeof (gfloat) * f->width);
		}
		break;
	default:
		{
			const gchar *s = g_value_get_string (&v);

			if (s != NULL && *s == '\0')
				s = NULL;
			if (f->kind == SG_REF && s != NULL && !g_str_has_prefix (s, "guid:") && ed != NULL &&
			    lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (ed)) != NULL)
			{
				/* stored relative to the graph's folder, like the material editor */
				LrgEditorApp     *app = app_of (ed);
				g_autofree gchar *dir = g_path_get_dirname (lrg_editor_data_editor_get_path (
				                                            LRG_EDITOR_DATA_EDITOR (ed)));
				g_autofree gchar *abs = app != NULL ? editor_app_project_path (app, s) : g_strdup (s);
				g_autofree gchar *rel = NULL;

				if (g_file_test (abs, G_FILE_TEST_EXISTS))
					rel = editor_mat_relative_to (dir, abs);
				return lrg_editor_shader_graph_editor_set_param (ed, ed->selected, f->key, rel != NULL ? rel : s);
			}
			ok = lrg_editor_shader_graph_editor_set_param (ed, ed->selected, f->key, s);
			/* the type decides the default's width: new rows */
			if (ok && g_strcmp0 (f->key, "type") == 0)
				ed->grid_stale = TRUE;
			return ok;
		}
	}

	if (f->target == SG_TARGET_INPUT)
		ok = lrg_editor_shader_graph_editor_set_input (ed, ed->selected, f->key, n, f->width);
	else
		ok = lrg_editor_shader_graph_editor_set_values (ed, ed->selected, f->key, n, f->width);
	return ok;
}

static LrgPropertyState
sg_source_state (LrgPropertySource *source,
                 const gchar       *name)
{
	SgSource                   *self = SG_SOURCE (source);
	SgField                    *f = sg_source_field (self, name);
	LrgEditorShaderGraphEditor *ed = self->editor;
	gfloat                      v[4];

	if (f == NULL || ed == NULL || ed->selected == NULL)
		return LRG_PROPERTY_STATE_NONE;
	if (f->target == SG_TARGET_PARAM)
		return lrg_shader_graph_get_node_param (ed->model, ed->selected, f->key) != NULL
		       ? LRG_PROPERTY_STATE_NONE : LRG_PROPERTY_STATE_DEFAULT;
	return sg_read_numbers (ed, f, v) > 0 ? LRG_PROPERTY_STATE_NONE : LRG_PROPERTY_STATE_DEFAULT;
}

/* Reset clears the value (inputs fall back to the library default). */
static gboolean
sg_source_reset (LrgPropertySource *source,
                 const gchar       *name)
{
	SgSource                   *self = SG_SOURCE (source);
	SgField                    *f = sg_source_field (self, name);
	LrgEditorShaderGraphEditor *ed = self->editor;

	if (f == NULL || ed == NULL || ed->selected == NULL)
		return FALSE;
	if (f->target == SG_TARGET_PARAM)
		return lrg_editor_shader_graph_editor_set_param (ed, ed->selected, f->key, NULL);
	if (f->target == SG_TARGET_INPUT)
		return lrg_editor_shader_graph_editor_set_input (ed, ed->selected, f->key, NULL, 0);
	return lrg_editor_shader_graph_editor_set_values (ed, ed->selected, f->key, NULL, 0);
}

static void
sg_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = sg_source_list;
	iface->get_value = sg_source_get;
	iface->set_value = sg_source_set;
	iface->get_state = sg_source_state;
	iface->reset = sg_source_reset;
}

static void
sg_source_finalize (GObject *object)
{
	SgSource *self = SG_SOURCE (object);

	g_ptr_array_unref (self->fields);
	g_clear_weak_pointer (&self->editor);
	G_OBJECT_CLASS (sg_source_parent_class)->finalize (object);
}

static void
sg_source_class_init (SgSourceClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = sg_source_finalize;
}

static void
sg_source_init (SgSource *self)
{
	self->fields = g_ptr_array_new_with_free_func (sg_field_free);
}

/* A grid gesture is one document group: one undo step. */
static void
on_grid_edit_begin (LrgPropertyGrid *grid,
                    const gchar     *name,
                    guint            gesture_id,
                    gpointer         user_data)
{
	SgSource *self = SG_SOURCE (user_data);

	if (self->editor == NULL || self->in_gesture)
		return;
	flush_commit (self->editor);
	lrg_editor_document_begin_group (doc_of (self->editor), "Edit Node");
	self->commits_at_begin = self->editor->commits;
	self->in_gesture = TRUE;
	self->grid_gesture = gesture_id;
}

static void
on_grid_edit_end (LrgPropertyGrid *grid,
                  const gchar     *name,
                  guint            gesture_id,
                  gboolean         commit,
                  gpointer         user_data)
{
	SgSource *self = SG_SOURCE (user_data);

	if (self->editor == NULL || !self->in_gesture || gesture_id != self->grid_gesture)
		return;
	lrg_editor_document_end_group (doc_of (self->editor));
	self->in_gesture = FALSE;
	if (!commit && self->editor->commits != self->commits_at_begin)
		lrg_editor_document_undo (doc_of (self->editor));
}

static void
rebuild_grid (LrgEditorShaderGraphEditor *self)
{
	self->grid_stale = FALSE;
	sg_source_build (self->source);
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_property_grid_rebuild (self->grid);
}

static gchar **
reference_items (LrgPropertyGrid *grid,
                 const gchar     *name,
                 const gchar     *hint,
                 gpointer         user_data)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (user_data);
	LrgEditorApp               *app = app_of (self);
	const gchar                *root = app != NULL ? lrg_editor_app_get_project_dir (app) : NULL;
	GStrvBuilder               *b = g_strv_builder_new ();
	g_autoptr(GDir)             d = NULL;
	gchar                     **out;

	/* textures of the project's top two levels, project-relative */
	if (root != NULL && (d = g_dir_open (root, 0, NULL)) != NULL)
	{
		const gchar *n;

		while ((n = g_dir_read_name (d)) != NULL)
		{
			g_autofree gchar *child = g_build_filename (root, n, NULL);
			g_autofree gchar *lower = g_ascii_strdown (n, -1);

			if (g_file_test (child, G_FILE_TEST_IS_DIR) && n[0] != '.')
			{
				g_autoptr(GDir) sub = g_dir_open (child, 0, NULL);
				const gchar    *m;

				while (sub != NULL && (m = g_dir_read_name (sub)) != NULL)
				{
					g_autofree gchar *ml = g_ascii_strdown (m, -1);

					if (g_str_has_suffix (ml, ".png") || g_str_has_suffix (ml, ".jpg"))
						g_strv_builder_take (b, g_build_filename (n, m, NULL));
				}
			}
			else if (g_str_has_suffix (lower, ".png") || g_str_has_suffix (lower, ".jpg"))
				g_strv_builder_add (b, n);
		}
	}
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);
	return out;
}

/* ==========================================================================
 * Preview
 * ========================================================================== */

static void
preview_source (gpointer         user_data,
                LrgMaterial    **material,
                LrgShaderAsset **shader)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (user_data);

	*material = NULL;
	*shader = self->asset;
}

/* ==========================================================================
 * LrgEditorDataEditor
 * ========================================================================== */

static void
graph_refresh (LrgEditorDataEditor *editor,
               const gchar         *path)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (editor);

	/* our own commit: the canvas already shows it */
	if (self->committing)
		return;
	g_clear_handle_id (&self->commit_id, g_source_remove);
	g_clear_pointer (&self->pending_label, g_free);
	load_model (self);
	rebuild_canvas (self);
	after_change (self);
}

/* The index of node @id in the file's node list. */
static gint
node_index (LrgShaderGraph *graph,
            const gchar    *id)
{
	g_auto(GStrv) ids = lrg_shader_graph_get_node_ids (graph);
	gint          i;

	for (i = 0; ids != NULL && ids[i] != NULL; i++)
		if (g_strcmp0 (ids[i], id) == 0)
			return i;
	return -1;
}

static void
graph_validate (LrgEditorDataEditor *editor,
                GPtrArray           *issues)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (editor);
	GPtrArray                  *problems;
	guint                       i;

	if (self->parse_error != NULL)
	{
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, "", 1, 1,
		                                                        self->parse_error));
		return;
	}
	lrg_shader_graph_validate (self->model, NULL);
	problems = lrg_shader_graph_get_problems (self->model);
	for (i = 0; problems != NULL && i < problems->len; i++)
	{
		LrgShaderGraphProblem *p = g_ptr_array_index (problems, i);
		const gchar           *id = lrg_shader_graph_problem_get_node_id (p);
		gint                   idx = id != NULL ? node_index (self->model, id) : -1;
		g_autofree gchar      *path = idx >= 0 ? g_strdup_printf ("/nodes/%d", idx) : g_strdup ("");
		g_autofree gchar      *msg = id != NULL ? g_strdup_printf ("%s: %s", id, lrg_shader_graph_problem_get_message (p))
		                                       : g_strdup (lrg_shader_graph_problem_get_message (p));
		guint                  line = 0, col = 0;

		lrg_editor_document_get_line (doc_of (self), path, &line, &col);
		g_ptr_array_add (issues, lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, path, line, col, msg));
	}
}

static void
graph_reveal (LrgEditorDataEditor *editor,
              const gchar         *path,
              guint                line)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (editor);
	g_auto(GStrv)               parts = lrg_editor_document_path_split (path, NULL);
	g_auto(GStrv)               ids = lrg_shader_graph_get_node_ids (self->model);
	guint                       idx;

	if (parts == NULL || g_strcmp0 (parts[0], "nodes") != 0 || parts[1] == NULL)
		return;
	idx = (guint) g_ascii_strtoull (parts[1], NULL, 10);
	if (ids != NULL && idx < g_strv_length (ids))
	{
		lrg_editor_shader_graph_editor_select_node (self, ids[idx]);
		lrg_node_graph_frame_selection (self->canvas, TRUE);
	}
}

static gchar *
graph_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (editor);
	GPtrArray                  *problems = lrg_shader_graph_get_problems (self->model);

	return g_strdup_printf ("nodes=%u links=%u problems=%u selected=%s", lrg_shader_graph_get_n_nodes (self->model),
	                        lrg_shader_graph_get_n_links (self->model), problems != NULL ? problems->len : 0u,
	                        self->selected != NULL ? self->selected : "-");
}

/* Save writes the graph, then the generated shader when it is valid. */
static gboolean
graph_save (LrgEditorDataEditor  *editor,
            GError              **error)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (editor);
	LrgEditorDataEditorClass   *parent = LRG_EDITOR_DATA_EDITOR_CLASS (lrg_editor_shader_graph_editor_parent_class);
	g_autoptr(GError)           err = NULL;

	flush_commit (self);
	if (!parent->save (editor, error))
		return FALSE;
	if (lrg_shader_graph_validate (self->model, NULL) && lrg_editor_data_editor_get_path (editor) != NULL &&
	    !lrg_editor_shader_graph_editor_export (self, NULL, &err))
		lrg_editor_data_editor_report_error (editor, err);
	return TRUE;
}

static gboolean
graph_undo (LrgEditorDataEditor *editor)
{
	flush_commit (LRG_EDITOR_SHADER_GRAPH_EDITOR (editor));
	return lrg_editor_document_undo (lrg_editor_data_editor_get_document (editor));
}

static gboolean
lrg_editor_shader_graph_editor_step (LrgWidget *widget,
                                     gfloat     delta_time)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (widget);
	gboolean                    busy = LRG_WIDGET_CLASS (lrg_editor_shader_graph_editor_parent_class)->step (
	                                       widget, delta_time);

	if (self->grid_stale)
		rebuild_grid (self);
	/* frame the graph while the dock settles on its size (the first
	 * frames with a real width) */
	if (self->framed < 3 && lrg_widget_get_width (LRG_WIDGET (self->canvas)) > 200.0f)
	{
		self->framed++;
		lrg_node_graph_frame_all (self->canvas, FALSE);
	}
	return busy;
}

static void
lrg_editor_shader_graph_editor_constructed (GObject *object)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (object);
	LrgEditorDataEditor        *base = LRG_EDITOR_DATA_EDITOR (object);
	LrgEditorApp               *app;
	LrgSplitter                *outer, *inner, *side;
	LrgVBox                    *left;
	LrgSyntax                  *glsl;

	G_OBJECT_CLASS (lrg_editor_shader_graph_editor_parent_class)->constructed (object);
	app = app_of (self);
	load_model (self);

	/* --- palette --- */
	left = lrg_vbox_new ();
	lrg_widget_set_name (LRG_WIDGET (left), "shadergraph-palette-box");
	lrg_container_set_spacing (LRG_CONTAINER (left), 4.0f);
	lrg_container_set_padding (LRG_CONTAINER (left), 6.0f);
	self->filter = lrg_text_input_new_with_placeholder ("Find a node...");
	lrg_widget_set_name (LRG_WIDGET (self->filter), "shadergraph-filter");
	g_signal_connect_object (self->filter, "text-changed", G_CALLBACK (on_filter_changed), self, 0);
	lrg_container_add_child (LRG_CONTAINER (left), LRG_WIDGET (self->filter));
	g_object_unref (self->filter);
	self->palette_rows = g_list_store_new (SG_TYPE_PALETTE_ROW);
	self->palette = lrg_list_view_new ();
	lrg_widget_set_name (LRG_WIDGET (self->palette), "shadergraph-palette");
	lrg_list_view_set_factory (self->palette, palette_create, palette_bind, self, NULL);
	lrg_list_view_set_model (self->palette, G_LIST_MODEL (self->palette_rows));
	lrg_list_view_set_row_height (self->palette, 26.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->palette), 1.0f);
	g_signal_connect_object (self->palette, "selection-changed", G_CALLBACK (on_palette_selection), self, 0);
	lrg_container_add_child (LRG_CONTAINER (left), LRG_WIDGET (self->palette));
	g_object_unref (self->palette);
	fill_palette (self);

	/* --- canvas --- */
	self->canvas = lrg_node_graph_new ();
	lrg_widget_set_name (LRG_WIDGET (self->canvas), "shadergraph-canvas");
	/* the canvas shares the tab with the palette and the side: no minimap */
	lrg_node_graph_set_show_minimap (self->canvas, FALSE);
	lrg_node_graph_set_snap_to_grid (self->canvas, TRUE);
	lrg_node_graph_set_selection_flow (self->canvas, TRUE);
	g_signal_connect_object (self->canvas, "can-connect", G_CALLBACK (on_can_connect), self, 0);
	g_signal_connect_object (self->canvas, "link-added", G_CALLBACK (on_link_added), self, 0);
	g_signal_connect_object (self->canvas, "link-removed", G_CALLBACK (on_link_removed), self, 0);
	g_signal_connect_object (self->canvas, "node-moved", G_CALLBACK (on_node_moved), self, 0);
	g_signal_connect_object (self->canvas, "node-removed", G_CALLBACK (on_node_removed), self, 0);
	g_signal_connect_object (self->canvas, "node-duplicated", G_CALLBACK (on_node_duplicated), self, 0);
	g_signal_connect_object (self->canvas, "selection-changed", G_CALLBACK (on_canvas_selection), self, 0);

	/* --- side: preview over node properties / code --- */
	self->preview = editor_mat_preview_view_new (app, "shadergraph-preview", preview_source, self);
	self->source = g_object_new (SG_TYPE_SOURCE, NULL);
	g_set_weak_pointer (&self->source->editor, self);
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "shadergraph-grid");
	lrg_property_grid_set_label_width (self->grid, 96.0f);
	lrg_property_grid_set_reference_func (self->grid, reference_items, self, NULL);
	g_signal_connect_object (self->grid, "edit-begin", G_CALLBACK (on_grid_edit_begin), self->source, 0);
	g_signal_connect_object (self->grid, "edit-end", G_CALLBACK (on_grid_edit_end), self->source, 0);
	self->code = lrg_text_editor_new ();
	lrg_widget_set_name (LRG_WIDGET (self->code), "shadergraph-glsl");
	lrg_text_editor_set_show_line_numbers (self->code, TRUE);
	glsl = lrg_syntax_get_for_name ("glsl");
	if (glsl != NULL)
		lrg_text_editor_set_syntax (self->code, glsl);
	lrg_text_editor_set_read_only (self->code, TRUE);
	self->tabs = lrg_tab_view_new ();
	lrg_widget_set_name (LRG_WIDGET (self->tabs), "shadergraph-tabs");
	lrg_tab_view_add_tab (self->tabs, "Node", LRG_WIDGET (self->grid));
	g_object_unref (self->grid);
	lrg_tab_view_add_tab (self->tabs, "GLSL", LRG_WIDGET (self->code));
	g_object_unref (self->code);

	side = lrg_splitter_new (LRG_ORIENTATION_VERTICAL);
	lrg_widget_set_name (LRG_WIDGET (side), "shadergraph-side");
	lrg_splitter_set_start_child (side, self->preview);
	g_object_unref (self->preview);
	lrg_splitter_set_end_child (side, LRG_WIDGET (self->tabs));
	g_object_unref (self->tabs);
	lrg_splitter_set_default_fraction (side, 0.42f);
	lrg_splitter_set_fraction (side, 0.42f, FALSE);
	lrg_splitter_set_min_sizes (side, 160.0f, 160.0f);

	inner = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (inner), "shadergraph-inner");
	lrg_splitter_set_start_child (inner, LRG_WIDGET (self->canvas));
	g_object_unref (self->canvas);
	lrg_splitter_set_end_child (inner, LRG_WIDGET (side));
	g_object_unref (side);
	lrg_splitter_set_default_fraction (inner, 0.70f);
	lrg_splitter_set_fraction (inner, 0.70f, FALSE);
	lrg_splitter_set_min_sizes (inner, 260.0f, 220.0f);

	outer = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (outer), "shadergraph-split");
	lrg_splitter_set_start_child (outer, LRG_WIDGET (left));
	g_object_unref (left);
	lrg_splitter_set_end_child (outer, LRG_WIDGET (inner));
	g_object_unref (inner);
	lrg_splitter_set_default_fraction (outer, 0.16f);
	lrg_splitter_set_fraction (outer, 0.16f, FALSE);
	lrg_splitter_set_min_sizes (outer, 150.0f, 400.0f);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (outer));
	g_object_unref (outer);

	/* start with the output node in the grid */
	{
		g_auto(GStrv) ids = lrg_shader_graph_get_node_ids (self->model);
		guint         i;

		for (i = 0; ids != NULL && ids[i] != NULL && self->selected == NULL; i++)
			if (g_str_has_suffix (lrg_shader_graph_get_node_type (self->model, ids[i]), "_output"))
				self->selected = g_strdup (ids[i]);
	}
	rebuild_canvas (self);
	after_change (self);
	rebuild_grid (self);
}

static void
lrg_editor_shader_graph_editor_dispose (GObject *object)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (object);

	g_clear_handle_id (&self->commit_id, g_source_remove);
	if (self->source != NULL)
		g_clear_weak_pointer (&self->source->editor);
	if (self->asset != NULL && IsWindowReady ())
		lrg_shader_asset_release (self->asset);
	g_clear_object (&self->asset);
	G_OBJECT_CLASS (lrg_editor_shader_graph_editor_parent_class)->dispose (object);
}

static void
lrg_editor_shader_graph_editor_finalize (GObject *object)
{
	LrgEditorShaderGraphEditor *self = LRG_EDITOR_SHADER_GRAPH_EDITOR (object);

	g_clear_object (&self->model);
	g_clear_object (&self->source);
	g_clear_object (&self->palette_rows);
	g_free (self->parse_error);
	g_free (self->selected);
	g_free (self->glsl);
	g_free (self->pending_label);
	G_OBJECT_CLASS (lrg_editor_shader_graph_editor_parent_class)->finalize (object);
}

static void
lrg_editor_shader_graph_editor_class_init (LrgEditorShaderGraphEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_shader_graph_editor_constructed;
	object_class->dispose = lrg_editor_shader_graph_editor_dispose;
	object_class->finalize = lrg_editor_shader_graph_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_shader_graph_editor_step;
	data_class->refresh = graph_refresh;
	data_class->validate = graph_validate;
	data_class->reveal = graph_reveal;
	data_class->get_state = graph_get_state;
	data_class->save = graph_save;
	data_class->undo = graph_undo;
}

static void
lrg_editor_shader_graph_editor_init (LrgEditorShaderGraphEditor *self)
{
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorShaderGraphEditor *
lrg_editor_shader_graph_editor_new (LrgEditorApp      *app,
                                    LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_SHADER_GRAPH_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorShaderGraphEditor *
lrg_editor_shader_graph_editor_open (LrgEditorApp  *app,
                                     const gchar   *path,
                                     GError       **error)
{
	LrgEditorDataEditor *e;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	e = lrg_editor_data_editor_open (app, path, error);
	if (e == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (e))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not a shader graph", path);
		return NULL;
	}
	return LRG_EDITOR_SHADER_GRAPH_EDITOR (e);
}

gboolean
lrg_editor_shader_graph_editor_create (LrgEditorApp  *app,
                                       const gchar   *path,
                                       const gchar   *master,
                                       GError       **error)
{
	g_autoptr(LrgShaderGraph) graph = NULL;
	g_autofree gchar         *abs = NULL;
	g_autofree gchar         *dir = NULL;
	g_autofree gchar         *base = NULL;
	g_autofree gchar         *stem = NULL;
	const gfloat              tint[4] = { 0.85f, 0.45f, 0.25f, 1.0f };
	const gfloat              rim[4] = { 0.35f, 0.65f, 1.0f, 1.0f };
	const gfloat              power = 3.0f;
	gboolean                  pbr;
	const gchar              *dot;

	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	pbr = master == NULL || g_strcmp0 (master, "pbr") == 0;
	if (!pbr && g_strcmp0 (master, "unlit") != 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unknown master '%s' (pbr or unlit)", master);
		return FALSE;
	}
	abs = (app != NULL && !g_path_is_absolute (path)) ? editor_app_project_path (app, path) : g_strdup (path);
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
	base = g_path_get_basename (abs);
	dot = strrchr (base, '.');
	stem = dot != NULL && dot != base ? g_strndup (base, (gsize) (dot - base)) : g_strdup (base);

	/* tint -> colour; pbr adds a fresnel rim on the emission */
	graph = lrg_shader_graph_new ();
	lrg_shader_graph_set_name (graph, stem);
	lrg_shader_graph_add_node (graph, "parameter", "tint", NULL);
	lrg_shader_graph_set_node_param (graph, "tint", "name", "tint");
	lrg_shader_graph_set_node_param (graph, "tint", "type", "color");
	lrg_shader_graph_set_node_param (graph, "tint", "hint", "color");
	lrg_shader_graph_set_node_values (graph, "tint", "default", tint, 4);
	lrg_shader_graph_set_node_position (graph, "tint", -320.0f, -100.0f);
	lrg_shader_graph_add_node (graph, pbr ? "pbr_output" : "unlit_output", "out", NULL);
	lrg_shader_graph_set_node_position (graph, "out", 110.0f, -40.0f);
	lrg_shader_graph_connect (graph, "tint", "value", "out", pbr ? "albedo" : "color", NULL);
	if (pbr)
	{
		lrg_shader_graph_add_node (graph, "fresnel", "rim", NULL);
		lrg_shader_graph_set_input_value (graph, "rim", "power", &power, 1);
		lrg_shader_graph_set_node_position (graph, "rim", -320.0f, 40.0f);
		lrg_shader_graph_add_node (graph, "constant", "rim_color", NULL);
		lrg_shader_graph_set_node_values (graph, "rim_color", "value", rim, 3);
		lrg_shader_graph_set_node_position (graph, "rim_color", -320.0f, 170.0f);
		lrg_shader_graph_add_node (graph, "mul", "glow", NULL);
		lrg_shader_graph_set_node_position (graph, "glow", -110.0f, 100.0f);
		lrg_shader_graph_connect (graph, "rim", "out", "glow", "a", NULL);
		lrg_shader_graph_connect (graph, "rim_color", "value", "glow", "b", NULL);
		lrg_shader_graph_connect (graph, "glow", "out", "out", "emission", NULL);
	}
	return lrg_shader_graph_save_to_file (graph, abs, error);
}

LrgShaderGraph *
lrg_editor_shader_graph_editor_get_graph (LrgEditorShaderGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), NULL);
	return self->model;
}

LrgNodeGraph *
lrg_editor_shader_graph_editor_get_canvas (LrgEditorShaderGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), NULL);
	return self->canvas;
}

const gchar *
lrg_editor_shader_graph_editor_add_node (LrgEditorShaderGraphEditor  *self,
                                         const gchar                 *type,
                                         const gchar                 *id,
                                         gfloat                       x,
                                         gfloat                       y,
                                         GError                     **error)
{
	const gchar      *nid;
	g_autofree gchar *label = NULL;
	g_autofree gchar *keep = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), NULL);
	g_return_val_if_fail (type != NULL, NULL);

	flush_commit (self);
	nid = lrg_shader_graph_add_node (self->model, type, id, error);
	if (nid == NULL)
		return NULL;
	keep = g_strdup (nid);
	lrg_shader_graph_set_node_position (self->model, keep, (gfloat) (gint) x, (gfloat) (gint) y);
	if (g_strcmp0 (type, "parameter") == 0)
	{
		g_autofree gchar *name = free_uniform_name (self->model, "param");

		lrg_shader_graph_set_node_param (self->model, keep, "name", name);
		lrg_shader_graph_set_node_param (self->model, keep, "type", "float");
	}
	else if (g_strcmp0 (type, "constant") == 0)
	{
		const gfloat one = 1.0f;

		lrg_shader_graph_set_node_values (self->model, keep, "value", &one, 1);
	}
	else if (g_strcmp0 (type, "swizzle") == 0)
		lrg_shader_graph_set_node_param (self->model, keep, "mask", "xyz");
	g_free (self->selected);
	self->selected = g_strdup (keep);
	label = g_strdup_printf ("Add %s", lrg_shader_node_type_get_label (type));
	self->rebuild_widget = TRUE;
	commit_now (self, label);
	/* the model may have been replaced: hand out its copy of the id */
	return lrg_shader_graph_has_node (self->model, keep) ? self->selected : NULL;
}

gboolean
lrg_editor_shader_graph_editor_remove_node (LrgEditorShaderGraphEditor *self,
                                            const gchar                *id)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	flush_commit (self);
	if (!lrg_shader_graph_remove_node (self->model, id))
		return FALSE;
	if (g_strcmp0 (self->selected, id) == 0)
		g_clear_pointer (&self->selected, g_free);
	self->rebuild_widget = TRUE;
	commit_now (self, "Delete Node");
	return TRUE;
}

gboolean
lrg_editor_shader_graph_editor_connect (LrgEditorShaderGraphEditor  *self,
                                        const gchar                 *from_node,
                                        const gchar                 *from_port,
                                        const gchar                 *to_node,
                                        const gchar                 *to_port,
                                        GError                     **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), FALSE);

	flush_commit (self);
	if (!lrg_shader_graph_connect (self->model, from_node, from_port, to_node, to_port, error))
		return FALSE;
	self->rebuild_widget = TRUE;
	commit_now (self, "Connect");
	return TRUE;
}

gboolean
lrg_editor_shader_graph_editor_disconnect (LrgEditorShaderGraphEditor *self,
                                           const gchar                *to_node,
                                           const gchar                *to_port)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), FALSE);

	flush_commit (self);
	if (!lrg_shader_graph_disconnect (self->model, to_node, to_port))
		return FALSE;
	self->rebuild_widget = TRUE;
	commit_now (self, "Disconnect");
	return TRUE;
}

gboolean
lrg_editor_shader_graph_editor_set_param (LrgEditorShaderGraphEditor *self,
                                          const gchar                *id,
                                          const gchar                *key,
                                          const gchar                *value)
{
	g_autofree gchar *label = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), FALSE);
	g_return_val_if_fail (id != NULL && key != NULL, FALSE);

	flush_commit (self);
	if (!lrg_shader_graph_set_node_param (self->model, id, key, value))
		return FALSE;
	/* a parameter's type changes its output port */
	if (g_strcmp0 (key, "type") == 0)
		self->rebuild_widget = TRUE;
	label = g_strdup_printf ("Set %s", key);
	commit_now (self, label);
	return TRUE;
}

gboolean
lrg_editor_shader_graph_editor_set_values (LrgEditorShaderGraphEditor *self,
                                           const gchar                *id,
                                           const gchar                *key,
                                           const gfloat               *values,
                                           guint                       n_values)
{
	g_autofree gchar *label = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), FALSE);
	g_return_val_if_fail (id != NULL && key != NULL, FALSE);

	flush_commit (self);
	if (!lrg_shader_graph_set_node_values (self->model, id, key, values, n_values))
		return FALSE;
	label = g_strdup_printf ("Set %s", key);
	commit_now (self, label);
	return TRUE;
}

gboolean
lrg_editor_shader_graph_editor_set_input (LrgEditorShaderGraphEditor *self,
                                          const gchar                *id,
                                          const gchar                *port,
                                          const gfloat               *values,
                                          guint                       n_values)
{
	g_autofree gchar *label = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), FALSE);
	g_return_val_if_fail (id != NULL && port != NULL, FALSE);

	flush_commit (self);
	if (!lrg_shader_graph_set_input_value (self->model, id, port, values, n_values))
		return FALSE;
	label = g_strdup_printf ("Set %s", port);
	commit_now (self, label);
	return TRUE;
}

gboolean
lrg_editor_shader_graph_editor_select_node (LrgEditorShaderGraphEditor *self,
                                            const gchar                *id)
{
	LrgGraphNode *card;

	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), FALSE);

	if (id != NULL && !lrg_shader_graph_has_node (self->model, id))
		return FALSE;
	g_free (self->selected);
	self->selected = g_strdup (id);
	self->syncing = TRUE;
	card = id != NULL ? lrg_node_graph_get_node (self->canvas, id) : NULL;
	if (card != NULL)
		lrg_node_graph_select_node (self->canvas, card, FALSE);
	else
		lrg_node_graph_unselect_all (self->canvas);
	self->syncing = FALSE;
	rebuild_grid (self);
	return TRUE;
}

const gchar *
lrg_editor_shader_graph_editor_get_selected (LrgEditorShaderGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), NULL);
	return self->selected;
}

GPtrArray *
lrg_editor_shader_graph_editor_get_problems (LrgEditorShaderGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), NULL);
	return lrg_shader_graph_get_problems (self->model);
}

const gchar *
lrg_editor_shader_graph_editor_get_glsl (LrgEditorShaderGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), NULL);
	return self->glsl;
}

LrgShaderAsset *
lrg_editor_shader_graph_editor_get_asset (LrgEditorShaderGraphEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), NULL);
	return self->asset;
}

gchar *
lrg_editor_shader_graph_editor_get_shader_path (LrgEditorShaderGraphEditor *self)
{
	const gchar *path;
	const gchar *dot;

	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), NULL);

	path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	if (path == NULL)
		return NULL;
	dot = strrchr (path, '.');
	if (dot == NULL || strchr (dot, G_DIR_SEPARATOR) != NULL)
		return g_strconcat (path, ".rshader", NULL);
	{
		g_autofree gchar *stem = g_strndup (path, (gsize) (dot - path));

		return g_strconcat (stem, ".rshader", NULL);
	}
}

gboolean
lrg_editor_shader_graph_editor_export (LrgEditorShaderGraphEditor  *self,
                                       const gchar                 *path,
                                       GError                     **error)
{
	g_autoptr(LrgShaderAsset) asset = NULL;
	g_autofree gchar         *target = NULL;
	LrgEditorApp             *app;

	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), FALSE);

	flush_commit (self);
	target = path != NULL ? g_strdup (path) : lrg_editor_shader_graph_editor_get_shader_path (self);
	if (target == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                     "An untitled graph needs a path to export to");
		return FALSE;
	}
	asset = lrg_shader_graph_generate (self->model, error);
	if (asset == NULL)
		return FALSE;
	if (!lrg_shader_asset_save_to_file (asset, target, error))
		return FALSE;
	app = app_of (self);
	if (app != NULL)
	{
		/* materials using it pick the new code up */
		editor_mat_preview_file_changed (app, target);
		editor_app_set_status (app, "Wrote %s", target);
	}
	return TRUE;
}

GrlImage *
lrg_editor_shader_graph_editor_render_preview (LrgEditorShaderGraphEditor *self,
                                               guint                       size)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SHADER_GRAPH_EDITOR (self), NULL);
	return editor_mat_preview_view_render (self->preview, size);
}

/* ==========================================================================
 * IPC: shadergraph.*
 * ========================================================================== */

static const gchar *
param_string (JsonObject  *params,
              const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	return (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING) ? json_node_get_string (n)
	                                                                                    : NULL;
}

static const gchar *
require_string (JsonObject   *params,
                const gchar  *name,
                GError      **error)
{
	const gchar *s = param_string (params, name);

	if (s == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Missing string parameter '%s'", name);
	return s;
}

static void
fail (GError       **error,
      const GError  *cause)
{
	gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

	if (cause != NULL && (g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
	                      g_error_matches (cause, G_FILE_ERROR, G_FILE_ERROR_NOENT) ||
	                      g_error_matches (cause, LRG_SHADER_GRAPH_ERROR, LRG_SHADER_GRAPH_ERROR_UNKNOWN_NODE)))
		code = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
	g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, code, cause != NULL ? cause->message : "failed");
}

static JsonNode *
object_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static JsonNode *
ok_node (gboolean     ok,
         const gchar *member)
{
	JsonObject *o = json_object_new ();

	json_object_set_boolean_member (o, member, ok);
	return object_node (o);
}

static LrgEditorShaderGraphEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	const gchar                *path = require_string (params, "path", error);
	LrgEditorShaderGraphEditor *self;
	g_autoptr(GError)           err = NULL;

	if (path == NULL)
		return NULL;
	self = lrg_editor_shader_graph_editor_open (app, path, &err);
	if (self == NULL)
		fail (error, err);
	return self;
}

/* Up to 4 numbers from a JSON number or array; -1 when neither. */
static gint
json_numbers (JsonNode *node,
              gfloat   *out)
{
	if (node == NULL || JSON_NODE_HOLDS_NULL (node))
		return 0;
	if (JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) != G_TYPE_STRING &&
	    json_node_get_value_type (node) != G_TYPE_BOOLEAN)
	{
		out[0] = (gfloat) json_node_get_double (node);
		return 1;
	}
	if (JSON_NODE_HOLDS_ARRAY (node))
	{
		JsonArray *a = json_node_get_array (node);
		guint      i, n = MIN (json_array_get_length (a), 4u);

		for (i = 0; i < n; i++)
			out[i] = (gfloat) json_array_get_double_element (a, i);
		return (gint) n;
	}
	return -1;
}

static JsonNode *
problems_json (GPtrArray *problems)
{
	JsonArray *a = json_array_new ();
	JsonNode  *n = json_node_new (JSON_NODE_ARRAY);
	GEnumClass *klass = g_type_class_ref (LRG_TYPE_SHADER_GRAPH_ERROR);
	guint      i;

	for (i = 0; problems != NULL && i < problems->len; i++)
	{
		LrgShaderGraphProblem *p = g_ptr_array_index (problems, i);
		GEnumValue            *ev = g_enum_get_value (klass, (gint) lrg_shader_graph_problem_get_code (p));
		JsonObject            *o = json_object_new ();

		json_object_set_string_member (o, "code", ev != NULL ? ev->value_nick : "invalid");
		json_object_set_string_member (o, "node", lrg_shader_graph_problem_get_node_id (p));
		json_object_set_string_member (o, "port", lrg_shader_graph_problem_get_port (p));
		json_object_set_string_member (o, "message", lrg_shader_graph_problem_get_message (p));
		json_array_add_object_element (a, o);
	}
	g_type_class_unref (klass);
	json_node_take_array (n, a);
	return n;
}

static JsonNode *
m_sg_open (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorShaderGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject                 *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "panel", lrg_editor_data_editor_get_panel_id (LRG_EDITOR_DATA_EDITOR (self)));
	return object_node (o);
}

static JsonNode *
m_sg_create (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	const gchar       *path = require_string (params, "path", error);
	g_autoptr(GError)  err = NULL;
	JsonObject        *o;

	if (path == NULL)
		return NULL;
	if (!lrg_editor_shader_graph_editor_create (app, path, param_string (params, "master"), &err))
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	{
		g_autofree gchar *abs = g_path_is_absolute (path) ? g_strdup (path) : editor_app_project_path (app, path);

		json_object_set_string_member (o, "path", abs);
	}
	return object_node (o);
}

static JsonNode *
m_sg_node_types (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	g_auto(GStrv) types = lrg_shader_graph_list_node_types ();
	JsonArray    *a = json_array_new ();
	JsonObject   *o = json_object_new ();
	guint         i, k;

	for (i = 0; types != NULL && types[i] != NULL; i++)
	{
		JsonObject   *t = json_object_new ();
		JsonArray    *ins = json_array_new ();
		JsonArray    *outs = json_array_new ();
		g_auto(GStrv) in = lrg_shader_node_type_get_inputs (types[i]);
		g_auto(GStrv) out = lrg_shader_node_type_get_outputs (types[i]);

		for (k = 0; in != NULL && in[k] != NULL; k++)
			json_array_add_string_element (ins, in[k]);
		for (k = 0; out != NULL && out[k] != NULL; k++)
			json_array_add_string_element (outs, out[k]);
		json_object_set_string_member (t, "type", types[i]);
		json_object_set_string_member (t, "label", lrg_shader_node_type_get_label (types[i]));
		json_object_set_string_member (t, "category", lrg_shader_node_type_get_category (types[i]));
		json_object_set_array_member (t, "inputs", ins);
		json_object_set_array_member (t, "outputs", outs);
		json_array_add_object_element (a, t);
	}
	json_object_set_array_member (o, "types", a);
	return object_node (o);
}

static JsonNode *
m_sg_get (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorShaderGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	static const gchar * const  strs[] = { "name", "type", "hint", "texture", "mask", "blend", "cull" };
	static const gchar * const  nums[] = { "default", "range", "value" };
	g_auto(GStrv)               ids = NULL;
	JsonObject                 *o;
	JsonArray                  *nodes, *links;
	guint                       i, k, n;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	nodes = json_array_new ();
	links = json_array_new ();
	ids = lrg_shader_graph_get_node_ids (self->model);
	for (i = 0; ids != NULL && ids[i] != NULL; i++)
	{
		JsonObject *nd = json_object_new ();
		JsonObject *pr = json_object_new ();
		gfloat      x = 0.0f, y = 0.0f, v[4];

		lrg_shader_graph_get_node_position (self->model, ids[i], &x, &y);
		json_object_set_string_member (nd, "id", ids[i]);
		json_object_set_string_member (nd, "type", lrg_shader_graph_get_node_type (self->model, ids[i]));
		json_object_set_double_member (nd, "x", x);
		json_object_set_double_member (nd, "y", y);
		for (k = 0; k < G_N_ELEMENTS (strs); k++)
			if (lrg_shader_graph_get_node_param (self->model, ids[i], strs[k]) != NULL)
				json_object_set_string_member (pr, strs[k], lrg_shader_graph_get_node_param (self->model, ids[i], strs[k]));
		for (k = 0; k < G_N_ELEMENTS (nums); k++)
			if ((n = lrg_shader_graph_get_node_values (self->model, ids[i], nums[k], v)) > 0)
			{
				JsonArray *a = json_array_new ();
				guint      j;

				for (j = 0; j < n; j++)
					json_array_add_double_element (a, v[j]);
				json_object_set_array_member (pr, nums[k], a);
			}
		json_object_set_object_member (nd, "params", pr);
		json_array_add_object_element (nodes, nd);
	}
	n = lrg_shader_graph_get_n_links (self->model);
	for (i = 0; i < n; i++)
	{
		const gchar *fn, *fp, *tn, *tp;
		JsonObject  *l = json_object_new ();

		lrg_shader_graph_get_link (self->model, i, &fn, &fp, &tn, &tp);
		json_object_set_string_member (l, "from", fn);
		json_object_set_string_member (l, "from_port", fp);
		json_object_set_string_member (l, "to", tn);
		json_object_set_string_member (l, "to_port", tp);
		json_array_add_object_element (links, l);
	}
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	json_object_set_string_member (o, "name", lrg_shader_graph_get_name (self->model));
	json_object_set_array_member (o, "nodes", nodes);
	json_object_set_array_member (o, "links", links);
	return object_node (o);
}

static JsonNode *
m_sg_add_node (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorShaderGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                *type;
	const gchar                *id;
	gfloat                      x = 0.0f, y = 0.0f;
	g_autoptr(GError)           err = NULL;
	JsonObject                 *o;

	if (self == NULL)
		return NULL;
	type = require_string (params, "type", error);
	if (type == NULL)
		return NULL;
	if (json_object_has_member (params, "x"))
		x = (gfloat) json_object_get_double_member (params, "x");
	if (json_object_has_member (params, "y"))
		y = (gfloat) json_object_get_double_member (params, "y");
	id = lrg_editor_shader_graph_editor_add_node (self, type, param_string (params, "id"), x, y, &err);
	if (id == NULL)
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "id", id);
	return object_node (o);
}

static JsonNode *
m_sg_remove_node (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorShaderGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                *id;

	if (self == NULL || (id = require_string (params, "id", error)) == NULL)
		return NULL;
	return ok_node (lrg_editor_shader_graph_editor_remove_node (self, id), "removed");
}

static JsonNode *
m_sg_connect (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorShaderGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                *from, *from_port, *to, *to_port;
	g_autoptr(GError)           err = NULL;

	if (self == NULL || (from = require_string (params, "from", error)) == NULL ||
	    (from_port = require_string (params, "from_port", error)) == NULL ||
	    (to = require_string (params, "to", error)) == NULL ||
	    (to_port = require_string (params, "to_port", error)) == NULL)
		return NULL;
	if (!lrg_editor_shader_graph_editor_connect (self, from, from_port, to, to_port, &err))
	{
		fail (error, err);
		return NULL;
	}
	return ok_node (TRUE, "ok");
}

static JsonNode *
m_sg_disconnect (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorShaderGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                *to, *to_port;

	if (self == NULL || (to = require_string (params, "to", error)) == NULL ||
	    (to_port = require_string (params, "to_port", error)) == NULL)
		return NULL;
	return ok_node (lrg_editor_shader_graph_editor_disconnect (self, to, to_port), "removed");
}

static JsonNode *
m_sg_set_param (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorShaderGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                *id, *key;
	JsonNode                   *value;
	gfloat                      v[4];
	gint                        n;
	gboolean                    ok;

	if (self == NULL || (id = require_string (params, "id", error)) == NULL ||
	    (key = require_string (params, "key", error)) == NULL)
		return NULL;
	value = json_object_has_member (params, "value") ? json_object_get_member (params, "value") : NULL;
	n = json_numbers (value, v);
	if (value != NULL && JSON_NODE_HOLDS_VALUE (value) && json_node_get_value_type (value) == G_TYPE_STRING)
		ok = lrg_editor_shader_graph_editor_set_param (self, id, key, json_node_get_string (value));
	else if (n > 0 || (value != NULL && JSON_NODE_HOLDS_ARRAY (value)))
		ok = lrg_editor_shader_graph_editor_set_values (self, id, key, v, (guint) MAX (n, 0));
	else if (n == 0)
		ok = lrg_editor_shader_graph_editor_set_param (self, id, key, NULL) ||
		     lrg_editor_shader_graph_editor_set_values (self, id, key, NULL, 0);
	else
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "'value' must be a string, a number, an array of numbers or null");
		return NULL;
	}
	if (!ok)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Node '%s' does not take '%s' with that value", id, key);
		return NULL;
	}
	return ok_node (TRUE, "ok");
}

static JsonNode *
m_sg_set_input (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorShaderGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar                *id, *port;
	gfloat                      v[4];
	gint                        n;

	if (self == NULL || (id = require_string (params, "id", error)) == NULL ||
	    (port = require_string (params, "port", error)) == NULL)
		return NULL;
	n = json_numbers (json_object_has_member (params, "value") ? json_object_get_member (params, "value") : NULL, v);
	if (n < 0 || !lrg_editor_shader_graph_editor_set_input (self, id, port, v, (guint) n))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Input '%s.%s' does not take that value", id, port);
		return NULL;
	}
	return ok_node (TRUE, "ok");
}

static JsonNode *
m_sg_validate (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorShaderGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject                 *o;
	gboolean                    ok;

	if (self == NULL)
		return NULL;
	ok = lrg_shader_graph_validate (self->model, NULL);
	o = json_object_new ();
	json_object_set_boolean_member (o, "ok", ok);
	json_object_set_member (o, "problems", problems_json (lrg_shader_graph_get_problems (self->model)));
	return object_node (o);
}

static JsonNode *
m_sg_glsl (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorShaderGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)           err = NULL;
	g_autofree gchar           *code = NULL;
	JsonObject                 *o;

	if (self == NULL)
		return NULL;
	code = lrg_shader_graph_generate_fragment (self->model, &err);
	if (code == NULL)
	{
		fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "fragment", code);
	return object_node (o);
}

static JsonNode *
m_sg_save (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorShaderGraphEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)           err = NULL;
	g_autofree gchar           *shader = NULL;
	JsonObject                 *o;

	if (self == NULL)
		return NULL;
	if (!lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (self), &err))
	{
		fail (error, err);
		return NULL;
	}
	shader = lrg_editor_shader_graph_editor_get_shader_path (self);
	o = json_object_new ();
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	if (shader != NULL && g_file_test (shader, G_FILE_TEST_IS_REGULAR) && lrg_shader_graph_validate (self->model, NULL))
		json_object_set_string_member (o, "shader", shader);
	else
		json_object_set_null_member (o, "shader");
	return object_node (o);
}

void
lrg_editor_shader_graph_editor_install (LrgEditorApp *app)
{
	static const struct { const gchar *name; LrgEditorServerMethod func; } methods[] = {
		{ "shadergraph.open", m_sg_open },
		{ "shadergraph.create", m_sg_create },
		{ "shadergraph.node_types", m_sg_node_types },
		{ "shadergraph.get", m_sg_get },
		{ "shadergraph.add_node", m_sg_add_node },
		{ "shadergraph.remove_node", m_sg_remove_node },
		{ "shadergraph.connect", m_sg_connect },
		{ "shadergraph.disconnect", m_sg_disconnect },
		{ "shadergraph.set_param", m_sg_set_param },
		{ "shadergraph.set_input", m_sg_set_input },
		{ "shadergraph.validate", m_sg_validate },
		{ "shadergraph.glsl", m_sg_glsl },
		{ "shadergraph.save", m_sg_save }
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
