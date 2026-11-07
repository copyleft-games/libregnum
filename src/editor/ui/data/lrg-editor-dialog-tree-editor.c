/* lrg-editor-dialog-tree-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The dialog tree editor: node graph | (property grid / play test).
 *
 * Model -> graph. The graph is rebuilt from the document after every
 * change that did not come from the graph's own pending edits (a rebuild
 * is cheap: a dialog has tens of nodes). Card positions come from the
 * node's `editor: {x, y}` key, else from a layered layout: breadth-first
 * depth from the start node gives the column, the order within the
 * column the row; unreachable nodes get a column of their own.
 *
 * Graph -> model. The graph's signals (link-added, link-removed,
 * node-moved, node-removed, node-duplicated) arrive in the middle of the
 * graph's own input handling and, for one gesture, in several pieces (a
 * reconnect is link-removed then link-added). They are queued and written
 * from an idle in one document group, so a gesture is one undo step and
 * the graph is never rebuilt inside its own signal emission. The queue is
 * coalesced: a removal followed by an addition on the same port is just
 * the addition; a link removed because its target card was deleted is
 * kept (the reference turns into a "missing" card instead).
 */

#include "config.h"

#define LRG_EDITOR_DATA_INTERNAL
#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-dialog-tree-editor.h"
#include "lrg-editor-hud-layout-editor.h"
#include "../lrg-editor-console-panel.h"
#include "../../../dialog/lrg-dialog-tree.h"
#include "../../../dialog/lrg-dialog-node.h"
#include "../../../dialog/lrg-dialog-runner.h"
#include "../../../dialog/lrg-dialog-response.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-button.h"
#include "../../../ui/lrg-theme.h"
#include "../../../paint/lrg-paint-list.h"
#include "../../../paint/lrg-paint-icons.h"
#include <math.h>
#include <string.h>

#define COLUMN_W        (300.0f)
#define ROW_H           (170.0f)
#define BODY_W          (220.0f)
#define BODY_H          (24.0f)
#define PORT_TYPE       "dialog"
#define MISSING_PREFIX  "missing:"
#define TRANSCRIPT_MAX  (8)

/* A queued graph edit */
typedef enum
{
	OP_SET_NEXT,
	OP_CLEAR_NEXT,
	OP_MOVE,
	OP_REMOVE,
	OP_DUPLICATE
} OpKind;

typedef struct
{
	OpKind  kind;
	gchar  *node;       /* source / moved / removed / original node id */
	gint    response;   /* -1 for the node's own next */
	gchar  *target;     /* SET/CLEAR: target id; DUPLICATE: the copy's card id */
	gfloat  x;
	gfloat  y;
} Op;

struct _LrgEditorDialogTreeEditor
{
	LrgEditorDataEditor  parent_instance;

	/* Widgets */
	LrgSplitter         *split;          /* graph | side */
	LrgSplitter         *side;           /* grid / play */
	LrgNodeGraph        *graph;
	LrgPropertyGrid     *grid;
	LrgWidget           *play_box;       /* LrgVBox */
	LrgWidget           *bubble;         /* LrgCustomWidget: speaker, text, transcript */
	LrgWidget           *choices;        /* LrgVBox of buttons */

	/* State */
	gchar               *selected;       /* dialog node id in the grid */
	gboolean             syncing;        /* rebuilding the graph */
	GPtrArray           *ops;            /* Op */
	guint                flush_id;
	gint                 shown_index;    /* node index the grid rows were built for */
	gint                 shown_responses;
	gchar               *shown_hint;     /* the ids hint of those rows */
	gboolean             framed;         /* the first frame_all once laid out */

	/* Play test */
	LrgDialogTree       *play_tree;
	LrgDialogRunner     *runner;
	gchar               *play_error;
	gboolean             play_done;      /* End was pressed on the last node */
	GPtrArray           *transcript;     /* gchar*: "speaker: text" / "> choice" */
};

G_DEFINE_FINAL_TYPE (LrgEditorDialogTreeEditor, lrg_editor_dialog_tree_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void rebuild_graph (LrgEditorDialogTreeEditor *self);
static void rebuild_fields (LrgEditorDialogTreeEditor *self);
static void sync_play (LrgEditorDialogTreeEditor *self);

static LrgEditorDocument *
doc_of (LrgEditorDialogTreeEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static void
op_free (gpointer data)
{
	Op *op = data;

	g_free (op->node);
	g_free (op->target);
	g_free (op);
}

/* --------------------------------------------------------------------------
 * Document helpers
 * -------------------------------------------------------------------------- */

static gchar *
node_path (gint         index,
           const gchar *key)
{
	return key != NULL ? g_strdup_printf ("/nodes/%d/%s", index, key) : g_strdup_printf ("/nodes/%d", index);
}

/* Index of the node with @id in `nodes`, or -1. */
static gint
node_index (LrgEditorDialogTreeEditor *self,
            const gchar               *id)
{
	LrgEditorDocument *doc = doc_of (self);
	gint               n = lrg_editor_document_get_length (doc, "/nodes");
	gint               i;

	for (i = 0; id != NULL && i < n; i++)
	{
		g_autofree gchar *path = node_path (i, "id");

		if (g_strcmp0 (lrg_editor_document_get_scalar (doc, path), id) == 0)
			return i;
	}
	return -1;
}

static gint
response_count (LrgEditorDialogTreeEditor *self,
                gint                       index)
{
	g_autofree gchar *path = node_path (index, "responses");

	return lrg_editor_document_get_kind (doc_of (self), path) == LRG_EDITOR_DOCUMENT_NODE_SEQUENCE
	       ? lrg_editor_document_get_length (doc_of (self), path) : 0;
}

/* The path of a `next`: the node's own (@response -1) or a response's. */
static gchar *
next_path (gint index,
           gint response)
{
	return response < 0 ? node_path (index, "next")
	                    : g_strdup_printf ("/nodes/%d/responses/%d/next", index, response);
}

/* A node id not used yet: @base, base-2, base-3, ... */
static gchar *
free_id (LrgEditorDialogTreeEditor *self,
         const gchar               *base)
{
	guint n;

	if (node_index (self, base) < 0)
		return g_strdup (base);
	for (n = 2; ; n++)
	{
		g_autofree gchar *id = g_strdup_printf ("%s-%u", base, n);

		if (node_index (self, id) < 0)
			return g_steal_pointer (&id);
	}
}

/* --------------------------------------------------------------------------
 * Card bodies: a two-line preview of the text
 * -------------------------------------------------------------------------- */

static void
body_paint (LrgCustomWidget *widget,
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
	const gchar       *text = user_data;
	g_auto(GStrv)      words = g_strsplit (text != NULL ? text : "", " ", -1);
	g_autoptr(GString) line = g_string_new (NULL);
	guint              row = 0, i;

	/* greedy wrap into one line (the card gives its body one row), ellipsized */
	lrg_paint_list_push_clip (list, x, y, width, height);
	for (i = 0; words[i] != NULL && row < 1; i++)
	{
		g_autofree gchar *probe = line->len > 0 ? g_strconcat (line->str, " ", words[i], NULL) : g_strdup (words[i]);
		gfloat            w = 0.0f;

		lrg_paint_measure_text (font, probe, fs, 0.0f, &w, NULL);
		if (w > width - 4.0f && line->len > 0)
		{
			if (row == 1)
				g_string_append (line, "…");
			lrg_paint_list_text (list, font, line->str, x + 2.0f, y + (gfloat) row * fs * 1.3f, fs, 0.0f,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
			row++;
			g_string_assign (line, words[i]);
		}
		else
			g_string_assign (line, probe);
	}
	if (row < 1 && line->len > 0)
		lrg_paint_list_text (list, font, line->str, x + 2.0f, y + (gfloat) row * fs * 1.3f, fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
	lrg_paint_list_pop_clip (list);
}

static LrgWidget *
body_new (const gchar *text)
{
	static const LrgCustomWidgetFuncs funcs = { NULL, body_paint, NULL, NULL, { NULL } };
	LrgCustomWidget                  *body = lrg_custom_widget_new ();

	lrg_custom_widget_set_preferred_width (body, BODY_W);
	lrg_custom_widget_set_preferred_height (body, BODY_H);
	lrg_custom_widget_set_funcs (body, &funcs, g_strdup (text), g_free);
	lrg_widget_set_pointer_transparent (LRG_WIDGET (body), TRUE);
	return LRG_WIDGET (body);
}

/* --------------------------------------------------------------------------
 * Graph rebuild
 * -------------------------------------------------------------------------- */

/* Layered auto-layout: column = BFS depth from the start node. */
static GHashTable *
auto_layout (LrgEditorDialogTreeEditor *self,
             GStrv                      ids,
             GPtrArray                 *edges,
             const gchar               *start)
{
	GHashTable *depth = g_hash_table_new (g_str_hash, g_str_equal);
	GHashTable *pos = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
	GQueue      queue = G_QUEUE_INIT;
	guint       rows[64] = { 0 };
	guint       max_depth = 0, i, j;

	if (start != NULL && g_strv_contains ((const gchar * const *) ids, start))
	{
		g_hash_table_insert (depth, (gpointer) start, GUINT_TO_POINTER (1));
		g_queue_push_tail (&queue, (gpointer) start);
	}
	while (!g_queue_is_empty (&queue))
	{
		const gchar *id = g_queue_pop_head (&queue);
		guint        d = GPOINTER_TO_UINT (g_hash_table_lookup (depth, id));

		max_depth = MAX (max_depth, d);
		for (j = 0; j < edges->len; j++)
		{
			LrgEditorDocumentEdge *e = g_ptr_array_index (edges, j);
			gchar                **known;

			if (g_strcmp0 (e->from, id) != 0)
				continue;
			for (known = ids; *known != NULL; known++)
				if (g_strcmp0 (*known, e->to) == 0 && !g_hash_table_contains (depth, *known))
				{
					g_hash_table_insert (depth, *known, GUINT_TO_POINTER (d + 1));
					g_queue_push_tail (&queue, *known);
				}
		}
	}
	for (i = 0; ids[i] != NULL; i++)
	{
		guint   d = GPOINTER_TO_UINT (g_hash_table_lookup (depth, ids[i]));
		gfloat *xy = g_new (gfloat, 2);

		/* unreachable nodes: a column after the last one */
		d = d > 0 ? d - 1 : max_depth;
		d = MIN (d, G_N_ELEMENTS (rows) - 1);
		xy[0] = (gfloat) d * COLUMN_W;
		xy[1] = (gfloat) rows[d]++ * ROW_H;
		g_hash_table_insert (pos, ids[i], xy);
	}
	g_hash_table_unref (depth);
	return pos;
}

static void
rebuild_graph (LrgEditorDialogTreeEditor *self)
{
	LrgEditorDocument   *doc = doc_of (self);
	LrgTheme            *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	g_auto(GStrv)        ids = NULL;
	g_autoptr(GPtrArray) edges = NULL;
	g_autoptr(GHashTable) pos = NULL;
	const gchar         *start;
	gint                 i;

	if (self->graph == NULL)
		return;
	self->syncing = TRUE;
	lrg_node_graph_clear (self->graph);

	ids = lrg_editor_document_dialog_nodes (doc);
	edges = lrg_editor_document_dialog_edges (doc);
	start = lrg_editor_document_get_scalar (doc, "/start");
	if (start == NULL && ids != NULL && ids[0] != NULL)
		start = ids[0];
	pos = auto_layout (self, ids, edges, start);

	/* --- one card per node, in document order --- */
	for (i = 0; ids != NULL && ids[i] != NULL; i++)
	{
		gint              index = node_index (self, ids[i]);
		g_autofree gchar *tpath = node_path (index, "text");
		g_autofree gchar *spath = node_path (index, "speaker");
		g_autofree gchar *xpath = node_path (index, "editor/x");
		g_autofree gchar *ypath = node_path (index, "editor/y");
		g_autoptr(LrgGraphNode) card = lrg_graph_node_new (ids[i], ids[i]);
		gfloat           *xy = g_hash_table_lookup (pos, ids[i]);
		const gchar      *xs = lrg_editor_document_get_scalar (doc, xpath);
		const gchar      *ys = lrg_editor_document_get_scalar (doc, ypath);
		gint              n_resp = response_count (self, index);
		LrgGraphPort     *port;
		LrgWidget        *body;
		gint              r;

		lrg_graph_node_set_category (card, lrg_editor_document_get_scalar (doc, spath));
		if (g_strcmp0 (ids[i], start) == 0)
			lrg_graph_node_set_accent (card, lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS));
		body = body_new (lrg_editor_document_get_scalar (doc, tpath));
		lrg_graph_node_set_body (card, body);
		g_object_unref (body);
		lrg_graph_node_set_min_width (card, BODY_W + 24.0f);

		port = lrg_graph_node_add_port (card, LRG_GRAPH_PORT_INPUT, "in", PORT_TYPE);
		lrg_graph_port_set_multi_link (port, TRUE);
		if (n_resp > 0)
		{
			for (r = 0; r < n_resp; r++)
			{
				g_autofree gchar *rtext = g_strdup_printf ("/nodes/%d/responses/%d/text", index, r);
				g_autofree gchar *short_text = editor_data_ellipsize (lrg_editor_document_get_scalar (doc, rtext), 18);
				g_autofree gchar *name = g_strdup_printf ("%d  %s", r + 1, short_text);

				port = lrg_graph_node_add_port (card, LRG_GRAPH_PORT_OUTPUT, name, PORT_TYPE);
				if (port != NULL)
				{
					lrg_graph_port_set_multi_link (port, FALSE);
					g_object_set_data (G_OBJECT (port), "dialog-response", GINT_TO_POINTER (r + 1));
				}
			}
		}
		else
		{
			port = lrg_graph_node_add_port (card, LRG_GRAPH_PORT_OUTPUT, "next", PORT_TYPE);
			lrg_graph_port_set_multi_link (port, FALSE);
			g_object_set_data (G_OBJECT (port), "dialog-response", GINT_TO_POINTER (0));
		}
		lrg_graph_node_set_position (card, xs != NULL ? (gfloat) g_ascii_strtod (xs, NULL) : xy[0],
		                             ys != NULL ? (gfloat) g_ascii_strtod (ys, NULL) : xy[1]);
		lrg_node_graph_append_node (self->graph, card);
	}

	/* --- links, with "missing" cards for dangling targets --- */
	for (i = 0; i < (gint) edges->len; i++)
	{
		LrgEditorDocumentEdge *e = g_ptr_array_index (edges, i);
		LrgGraphNode          *from = lrg_node_graph_get_node (self->graph, e->from);
		LrgGraphNode          *to = lrg_node_graph_get_node (self->graph, e->to);
		LrgGraphPort          *out;
		LrgGraphLink          *link;

		if (from == NULL || e->to == NULL)
			continue;
		if (to == NULL)
		{
			g_autofree gchar *ghost_id = g_strconcat (MISSING_PREFIX, e->to, NULL);

			to = lrg_node_graph_get_node (self->graph, ghost_id);
			if (to == NULL)
			{
				g_autofree gchar *title = g_strdup_printf ("%s (missing)", e->to);

				to = lrg_node_graph_add_node (self->graph, ghost_id, title, lrg_graph_node_get_x (from) + COLUMN_W,
				                              lrg_graph_node_get_y (from) + 40.0f);
				lrg_graph_node_set_accent (to, lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
				lrg_graph_port_set_multi_link (lrg_graph_node_add_port (to, LRG_GRAPH_PORT_INPUT, "in", PORT_TYPE), TRUE);
			}
		}
		out = lrg_graph_node_get_port (from, LRG_GRAPH_PORT_OUTPUT, e->response >= 0 ? (guint) e->response : 0u);
		if (out == NULL)
			continue;
		link = lrg_node_graph_connect (self->graph, out, lrg_graph_node_find_port (to, LRG_GRAPH_PORT_INPUT, "in"), NULL);
		if (link != NULL && e->path == NULL)
		{
			/* the loader's implicit chain: drawn dimmed */
			GrlColor dim = editor_data_color_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED), 0.6f);

			lrg_graph_link_set_color (link, &dim);
			g_object_set_data (G_OBJECT (link), "dialog-implicit", GINT_TO_POINTER (1));
		}
	}

	/* --- keep the selection --- */
	if (self->selected != NULL)
	{
		LrgGraphNode *card = lrg_node_graph_get_node (self->graph, self->selected);

		if (card != NULL)
			lrg_node_graph_select_node (self->graph, card, FALSE);
		else
		{
			g_clear_pointer (&self->selected, g_free);
			rebuild_fields (self);
		}
	}
	self->syncing = FALSE;
}

/* --------------------------------------------------------------------------
 * Graph -> document (queued)
 * -------------------------------------------------------------------------- */

static gboolean flush_idle (gpointer user_data);

static void
queue_op (LrgEditorDialogTreeEditor *self,
          OpKind                     kind,
          const gchar               *node,
          gint                       response,
          const gchar               *target,
          gfloat                     x,
          gfloat                     y)
{
	Op *op = g_new0 (Op, 1);

	op->kind = kind;
	op->node = g_strdup (node);
	op->response = response;
	op->target = g_strdup (target);
	op->x = x;
	op->y = y;
	g_ptr_array_add (self->ops, op);
	if (self->flush_id == 0)
		self->flush_id = g_idle_add (flush_idle, self);
}

/* The response index an output port stands for (-1: node's next). */
static gint
port_response (LrgGraphPort *port)
{
	gint tag = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (port), "dialog-response"));

	/* a node with responses has tags 1..n; "next" has 0 */
	return tag > 0 ? tag - 1 : -1;
}

static gboolean
is_card (const gchar *id)
{
	return id != NULL && !g_str_has_prefix (id, MISSING_PREFIX);
}

static void
on_link_added (LrgNodeGraph *graph,
               LrgGraphLink *link,
               gpointer      user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);
	LrgGraphPort              *out = lrg_graph_link_get_output (link);
	const gchar               *from = lrg_graph_node_get_id (lrg_graph_port_get_node (out));
	const gchar               *to = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_input (link)));

	if (self->syncing || !is_card (from))
		return;
	if (g_str_has_prefix (to, MISSING_PREFIX))
		to += strlen (MISSING_PREFIX);
	queue_op (self, OP_SET_NEXT, from, port_response (out), to, 0.0f, 0.0f);
}

static void
on_link_removed (LrgNodeGraph *graph,
                 LrgGraphLink *link,
                 gpointer      user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);
	LrgGraphPort              *out = lrg_graph_link_get_output (link);
	const gchar               *from = lrg_graph_node_get_id (lrg_graph_port_get_node (out));
	const gchar               *to = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_input (link)));

	if (self->syncing || !is_card (from) || g_object_get_data (G_OBJECT (link), "dialog-implicit") != NULL)
		return;
	queue_op (self, OP_CLEAR_NEXT, from, port_response (out), to, 0.0f, 0.0f);
}

static void
on_node_moved (LrgNodeGraph *graph,
               LrgGraphNode *card,
               gpointer      user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);

	if (self->syncing || !is_card (lrg_graph_node_get_id (card)))
		return;
	queue_op (self, OP_MOVE, lrg_graph_node_get_id (card), -1, NULL, lrg_graph_node_get_x (card),
	          lrg_graph_node_get_y (card));
}

static void
on_node_removed (LrgNodeGraph *graph,
                 LrgGraphNode *card,
                 gpointer      user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);

	if (self->syncing || !is_card (lrg_graph_node_get_id (card)))
		return;
	queue_op (self, OP_REMOVE, lrg_graph_node_get_id (card), -1, NULL, 0.0f, 0.0f);
}

static void
on_node_duplicated (LrgNodeGraph *graph,
                    LrgGraphNode *original,
                    LrgGraphNode *copy,
                    gpointer      user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);

	if (self->syncing || !is_card (lrg_graph_node_get_id (original)))
		return;
	queue_op (self, OP_DUPLICATE, lrg_graph_node_get_id (original), -1, lrg_graph_node_get_id (copy),
	          lrg_graph_node_get_x (copy), lrg_graph_node_get_y (copy));
}

/* A wire dropped on empty canvas: a new node there, wired to the port. */
static void
on_link_dropped (LrgNodeGraph *graph,
                 LrgGraphPort *port,
                 gfloat        x,
                 gfloat        y,
                 gpointer      user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);
	LrgGraphNode              *card = lrg_graph_port_get_node (port);
	g_autofree gchar          *id = NULL;

	if (self->syncing || lrg_graph_port_get_direction (port) != LRG_GRAPH_PORT_OUTPUT ||
	    !is_card (lrg_graph_node_get_id (card)))
		return;
	id = free_id (self, "node");
	queue_op (self, OP_DUPLICATE, NULL, -1, id, x, y);
	queue_op (self, OP_SET_NEXT, lrg_graph_node_get_id (card), port_response (port), id, 0.0f, 0.0f);
}

/* Whether a later op of the queue overrides op @i (same port). */
static gboolean
superseded (LrgEditorDialogTreeEditor *self,
            guint                      i)
{
	Op   *op = g_ptr_array_index (self->ops, i);
	guint j;

	for (j = i + 1; j < self->ops->len; j++)
	{
		Op *later = g_ptr_array_index (self->ops, j);

		if ((later->kind == OP_SET_NEXT || later->kind == OP_CLEAR_NEXT) && op->kind != OP_MOVE &&
		    g_strcmp0 (later->node, op->node) == 0 && later->response == op->response)
			return TRUE;
		if (op->kind == OP_MOVE && later->kind == OP_MOVE && g_strcmp0 (later->node, op->node) == 0)
			return TRUE;
	}
	return FALSE;
}

static gboolean
removed_in_batch (LrgEditorDialogTreeEditor *self,
                  const gchar               *id)
{
	guint i;

	for (i = 0; id != NULL && i < self->ops->len; i++)
	{
		Op *op = g_ptr_array_index (self->ops, i);

		if (op->kind == OP_REMOVE && g_strcmp0 (op->node, id) == 0)
			return TRUE;
	}
	return FALSE;
}

/* A new node: a copy of @original's YAML (or a fresh one) at (x, y). */
static gboolean
insert_node (LrgEditorDialogTreeEditor  *self,
             const gchar                *original,
             const gchar                *id,
             gfloat                      x,
             gfloat                      y,
             GError                    **error)
{
	LrgEditorDocument *doc = doc_of (self);
	gint               src = node_index (self, original);
	g_autofree gchar  *yaml = NULL;
	g_autofree gchar  *base = NULL;
	gint               n;

	if (src >= 0)
	{
		g_autofree gchar *path = node_path (src, NULL);

		yaml = lrg_editor_document_get_yaml (doc, path);
	}
	else
		yaml = g_strdup ("{ id: x, text: \"...\" }");
	if (lrg_editor_document_get_kind (doc, "/nodes") != LRG_EDITOR_DOCUMENT_NODE_SEQUENCE &&
	    !lrg_editor_document_replace (doc, "/nodes", "[]", error))
		return FALSE;
	if (!lrg_editor_document_insert_item (doc, "/nodes", -1, yaml, error))
		return FALSE;
	n = lrg_editor_document_get_length (doc, "/nodes") - 1;
	base = node_path (n, NULL);
	{
		g_autofree gchar *ipath = node_path (n, "id");
		g_autofree gchar *xpath = node_path (n, "editor/x");
		g_autofree gchar *ypath = node_path (n, "editor/y");
		g_autofree gchar *epath = node_path (n, "editor");

		if (!lrg_editor_document_set_string (doc, ipath, id, 0, error))
			return FALSE;
		if (lrg_editor_document_get_kind (doc, epath) != LRG_EDITOR_DOCUMENT_NODE_MAPPING &&
		    !lrg_editor_document_replace (doc, epath, "{}", error))
			return FALSE;
		return lrg_editor_document_set_int (doc, xpath, (gint64) lroundf (x), 0, error) &&
		       lrg_editor_document_set_int (doc, ypath, (gint64) lroundf (y), 0, error);
	}
}

/* Writes one `next` reference, or removes it (@to NULL). */
static gboolean
write_link (LrgEditorDialogTreeEditor  *self,
            const gchar                *from,
            gint                        response,
            const gchar                *to,
            GError                    **error)
{
	LrgEditorDocument *doc = doc_of (self);
	gint               index = node_index (self, from);
	g_autofree gchar  *path = NULL;

	if (index < 0)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no dialog node '%s'",
		             from);
		return FALSE;
	}
	if (response >= response_count (self, index))
		response = -1;
	path = next_path (index, response);
	if (to != NULL)
		return lrg_editor_document_set_string (doc, path, to, 0, error);
	return !lrg_editor_document_has_path (doc, path) || lrg_editor_document_remove (doc, path, error);
}

/* Writes the queued graph edits as one undo step, then rebuilds once. */
static void
flush_ops (LrgEditorDialogTreeEditor *self)
{
	LrgEditorDocument *doc = doc_of (self);
	g_autoptr(GError)  error = NULL;
	g_autoptr(GArray)  removals = g_array_new (FALSE, FALSE, sizeof (gint));
	guint              i;

	g_clear_handle_id (&self->flush_id, g_source_remove);
	if (self->ops->len == 0)
		return;

	self->syncing = TRUE;   /* no rebuilds while writing */
	lrg_editor_document_begin_group (doc, "Edit dialog graph");
	for (i = 0; i < self->ops->len && error == NULL; i++)
	{
		Op *op = g_ptr_array_index (self->ops, i);

		if (superseded (self, i))
			continue;
		switch (op->kind)
		{
		case OP_SET_NEXT:
			if (!removed_in_batch (self, op->node))
				write_link (self, op->node, op->response, op->target, &error);
			break;
		case OP_CLEAR_NEXT:
			/* a card deleted with its links keeps the references */
			if (!removed_in_batch (self, op->node) && !removed_in_batch (self, op->target))
				write_link (self, op->node, op->response, NULL, &error);
			break;
		case OP_MOVE:
			{
				gint index = node_index (self, op->node);

				if (index >= 0 && !removed_in_batch (self, op->node))
				{
					g_autofree gchar *epath = node_path (index, "editor");
					g_autofree gchar *xpath = node_path (index, "editor/x");
					g_autofree gchar *ypath = node_path (index, "editor/y");

					if (lrg_editor_document_get_kind (doc, epath) != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
						lrg_editor_document_replace (doc, epath, "{}", &error);
					if (error == NULL)
						lrg_editor_document_set_int (doc, xpath, (gint64) lroundf (op->x), 0, &error);
					if (error == NULL)
						lrg_editor_document_set_int (doc, ypath, (gint64) lroundf (op->y), 0, &error);
				}
			}
			break;
		case OP_REMOVE:
			{
				gint index = node_index (self, op->node);

				if (index >= 0)
					g_array_append_val (removals, index);
			}
			break;
		case OP_DUPLICATE:
			{
				g_autofree gchar *id = op->node != NULL ? free_id (self, op->node) : free_id (self, op->target);

				insert_node (self, op->node, id, op->x, op->y, &error);
				/* a wire dropped on the canvas links to this id */
				if (op->node == NULL && g_strcmp0 (id, op->target) != 0)
				{
					guint j;

					for (j = i + 1; j < self->ops->len; j++)
					{
						Op *later = g_ptr_array_index (self->ops, j);

						if (later->kind == OP_SET_NEXT && g_strcmp0 (later->target, op->target) == 0)
						{
							g_free (later->target);
							later->target = g_strdup (id);
						}
					}
				}
			}
			break;
		default:
			break;
		}
	}

	/* removals last, highest index first so the others stay valid */
	if (error == NULL && removals->len > 0)
	{
		gint j, k;

		for (j = 0; j < (gint) removals->len; j++)
			for (k = j + 1; k < (gint) removals->len; k++)
				if (g_array_index (removals, gint, k) > g_array_index (removals, gint, j))
				{
					gint t = g_array_index (removals, gint, j);

					g_array_index (removals, gint, j) = g_array_index (removals, gint, k);
					g_array_index (removals, gint, k) = t;
				}
		for (j = 0; j < (gint) removals->len && error == NULL; j++)
		{
			g_autofree gchar *path = node_path (g_array_index (removals, gint, j), NULL);

			lrg_editor_document_remove (doc, path, &error);
		}
	}
	lrg_editor_document_end_group (doc);
	g_ptr_array_set_size (self->ops, 0);
	self->syncing = FALSE;

	if (error != NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	rebuild_graph (self);
	rebuild_fields (self);
}

static gboolean
flush_idle (gpointer user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);

	self->flush_id = 0;
	flush_ops (self);
	return G_SOURCE_REMOVE;
}

/* --------------------------------------------------------------------------
 * Selection and property grid
 * -------------------------------------------------------------------------- */

static void
on_graph_selection (LrgNodeGraph *graph,
                    gpointer      user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);
	GList                     *sel;
	const gchar               *id = NULL;

	if (self->syncing)
		return;
	sel = lrg_node_graph_get_selected_nodes (graph);
	if (sel != NULL && g_list_length (sel) == 1 && is_card (lrg_graph_node_get_id (sel->data)))
		id = lrg_graph_node_get_id (sel->data);
	if (g_strcmp0 (id, self->selected) != 0)
	{
		g_free (self->selected);
		self->selected = g_strdup (id);
		rebuild_fields (self);
	}
	g_list_free (sel);
}

/* Renames a node and every reference to it, one undo step. */
static gboolean
rename_node (LrgEditorDialogTreeEditor *self,
             const gchar               *old_id,
             const gchar               *new_id,
             guint                      gesture)
{
	LrgEditorDocument   *doc = doc_of (self);
	gint                 index = node_index (self, old_id);
	g_autoptr(GPtrArray) edges = NULL;
	g_autoptr(GError)    error = NULL;
	g_autofree gchar     *old = g_strdup (old_id);
	g_autofree gchar     *ipath = NULL;
	guint                i;

	if (index < 0 || new_id == NULL || *new_id == '\0' || g_strcmp0 (old_id, new_id) == 0)
		return FALSE;
	if (node_index (self, new_id) >= 0)
	{
		g_set_error (&error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_EXISTS,
		             "a node '%s' already exists", new_id);
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return FALSE;
	}
	edges = lrg_editor_document_dialog_edges (doc);
	ipath = node_path (index, "id");
	self->syncing = TRUE;   /* one rebuild after the whole rename */
	lrg_editor_document_begin_group (doc, "Rename dialog node");
	lrg_editor_document_set_string (doc, ipath, new_id, gesture, &error);
	for (i = 0; error == NULL && i < edges->len; i++)
	{
		LrgEditorDocumentEdge *e = g_ptr_array_index (edges, i);

		if (e->path != NULL && g_strcmp0 (e->to, old) == 0)
			lrg_editor_document_set_string (doc, e->path, new_id, 0, &error);
	}
	if (error == NULL && g_strcmp0 (lrg_editor_document_get_scalar (doc, "/start"), old) == 0)
		lrg_editor_document_set_string (doc, "/start", new_id, 0, &error);
	lrg_editor_document_end_group (doc);
	self->syncing = FALSE;
	if (error != NULL)
	{
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return FALSE;
	}
	g_free (self->selected);
	self->selected = g_strdup (new_id);
	return TRUE;
}

static gboolean
dialog_field_get (const gchar *name,
                  GValue      *value,
                  gpointer     user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);

	if (g_strcmp0 (name, "id") == 0)
		g_value_set_string (value, self->selected);
	return TRUE;
}

static gboolean
dialog_field_set (const gchar  *name,
                  const GValue *value,
                  guint         gesture_id,
                  gpointer      user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);

	if (g_strcmp0 (name, "id") == 0 && self->selected != NULL)
	{
		g_autofree gchar *old = g_strdup (self->selected);

		if (!rename_node (self, old, g_value_get_string (value), gesture_id))
			return FALSE;
		rebuild_graph (self);
		return TRUE;
	}
	return FALSE;
}

/* "enum-strings:a,b,c" of the node ids, for `next` and `start` rows. */
static gchar *
ids_hint (LrgEditorDialogTreeEditor *self)
{
	g_auto(GStrv)     ids = lrg_editor_document_dialog_nodes (doc_of (self));
	g_autofree gchar *list = g_strjoinv (",", ids);

	return g_strconcat ("enum-strings:", list, NULL);
}

static void
rebuild_fields (LrgEditorDialogTreeEditor *self)
{
	g_autoptr(EditorDataFields) fields = NULL;
	const GParamFlags           flags = G_PARAM_READWRITE;
	g_autofree gchar           *hint = NULL;
	gint                        index, n, r;

	if (self->grid == NULL)
		return;
	fields = editor_data_fields_new (LRG_EDITOR_DATA_EDITOR (self));
	editor_data_fields_set_virtual (fields, dialog_field_get, dialog_field_set, self);
	hint = ids_hint (self);
	index = node_index (self, self->selected);
	if (index < 0)
	{
		editor_data_fields_add (fields, g_param_spec_string ("tree-id", "Id", NULL, NULL, flags), "/id", "Tree", NULL);
		editor_data_fields_add (fields, g_param_spec_string ("title", "Title", NULL, NULL, flags), "/title", "Tree",
		                        NULL);
		editor_data_fields_add (fields, g_param_spec_string ("start", "Start node", NULL, NULL, flags), "/start",
		                        "Tree", hint);
	}
	else
	{
		g_autofree gchar *spath = node_path (index, "speaker");
		g_autofree gchar *tpath = node_path (index, "text");
		g_autofree gchar *npath = node_path (index, "next");

		editor_data_fields_add (fields, g_param_spec_string ("id", "Id", NULL, NULL, flags), NULL, "Node", NULL);
		editor_data_fields_add (fields, g_param_spec_string ("speaker", "Speaker", NULL, NULL, flags), spath, "Node",
		                        NULL);
		editor_data_fields_add (fields, g_param_spec_string ("text", "Text", NULL, NULL, flags), tpath, "Node",
		                        "multiline");
		n = response_count (self, index);
		if (n == 0)
			editor_data_fields_add (fields, g_param_spec_string ("next", "Next", NULL, NULL, flags), npath, "Node", hint);
		for (r = 0; r < n; r++)
		{
			g_autofree gchar *group = g_strdup_printf ("Response %d", r + 1);
			g_autofree gchar *tname = g_strdup_printf ("response-%d-text", r + 1);
			g_autofree gchar *nname = g_strdup_printf ("response-%d-next", r + 1);
			g_autofree gchar *rtext = g_strdup_printf ("/nodes/%d/responses/%d/text", index, r);
			g_autofree gchar *rnext = g_strdup_printf ("/nodes/%d/responses/%d/next", index, r);

			editor_data_fields_add (fields, g_param_spec_string (tname, "Text", NULL, NULL, flags), rtext, group,
			                        NULL);
			editor_data_fields_add (fields, g_param_spec_string (nname, "Next", NULL, NULL, flags), rnext, group,
			                        hint);
		}
	}
	self->shown_index = index;
	self->shown_responses = index >= 0 ? response_count (self, index) : -1;
	g_free (self->shown_hint);
	self->shown_hint = g_steal_pointer (&hint);
	editor_data_fields_attach (fields, self->grid);
}

/* Whether the grid rows no longer fit the selection (another node index,
 * a response added or removed, a node id added or renamed). */
static gboolean
fields_stale (LrgEditorDialogTreeEditor *self)
{
	gint              index = node_index (self, self->selected);
	g_autofree gchar *hint = ids_hint (self);

	return index != self->shown_index || g_strcmp0 (hint, self->shown_hint) != 0 ||
	       (index >= 0 ? response_count (self, index) : -1) != self->shown_responses;
}

/* --------------------------------------------------------------------------
 * Play test
 * -------------------------------------------------------------------------- */

static void
transcript_add (LrgEditorDialogTreeEditor *self,
                gchar                     *line)
{
	g_ptr_array_add (self->transcript, line);
	while (self->transcript->len > TRANSCRIPT_MAX)
		g_ptr_array_remove_index (self->transcript, 0);
}

static void
on_node_entered (LrgDialogRunner *runner,
                 LrgDialogNode   *node,
                 gpointer         user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);
	LrgGraphNode              *card;
	const gchar               *speaker = lrg_dialog_node_get_speaker (node);

	transcript_add (self, g_strdup_printf ("%s%s%s", speaker != NULL ? speaker : "", speaker != NULL ? ": " : "",
	                                       lrg_dialog_node_get_display_text (node)));
	card = self->graph != NULL ? lrg_node_graph_get_node (self->graph, lrg_dialog_node_get_id (node)) : NULL;
	if (card != NULL)
		lrg_node_graph_select_node (self->graph, card, FALSE);
}

static void
on_choice_clicked (LrgButton *button,
                   gpointer   user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);

	lrg_editor_dialog_tree_editor_play_choose (self,
	                                           GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "dialog-choice")));
}

static void
add_choice (LrgEditorDialogTreeEditor *self,
            const gchar               *label,
            gint                       index,
            gboolean                   enabled)
{
	LrgButton        *button = lrg_button_new (label);
	g_autofree gchar *name = g_strdup_printf ("dialog-choice-%d", index < 0 ? 0 : index + 1);

	lrg_widget_set_name (LRG_WIDGET (button), name);
	g_object_set_data (G_OBJECT (button), "dialog-choice", GINT_TO_POINTER (index));
	lrg_widget_set_enabled (LRG_WIDGET (button), enabled);
	g_signal_connect_object (button, "clicked", G_CALLBACK (on_choice_clicked), self, 0);
	lrg_container_add_child (LRG_CONTAINER (self->choices), LRG_WIDGET (button));
	g_object_unref (button);
}

/* The node the play test shows. The engine's runner turns inactive as
 * soon as it enters a terminal node but keeps it current, so the last
 * line stays on screen until End is pressed. */
static LrgDialogNode *
play_current (LrgEditorDialogTreeEditor *self)
{
	if (self->runner == NULL || self->play_done)
		return NULL;
	return lrg_dialog_runner_get_current_node (self->runner);
}

/* Rebuilds the choice buttons for the runner's current node. */
static void
sync_play (LrgEditorDialogTreeEditor *self)
{
	LrgDialogNode *node;

	if (self->choices == NULL)
		return;
	lrg_container_remove_all (LRG_CONTAINER (self->choices));
	node = play_current (self);
	if (node == NULL)
		return;
	if (!lrg_dialog_runner_is_active (self->runner))
		add_choice (self, "End", -1, TRUE);
	else if (lrg_dialog_node_get_response_count (node) > 0)
	{
		g_autoptr(GPtrArray) available = lrg_dialog_runner_get_available_responses (self->runner);
		guint                i, j;

		for (i = 0; i < lrg_dialog_node_get_response_count (node); i++)
		{
			LrgDialogResponse *r = lrg_dialog_node_get_response (node, i);
			gboolean           ok = FALSE;
			g_autofree gchar  *label = g_strdup_printf ("%u. %s", i + 1, lrg_dialog_response_get_text (r));

			for (j = 0; j < available->len; j++)
				if (g_ptr_array_index (available, j) == (gpointer) r)
					ok = TRUE;
			add_choice (self, label, (gint) i, ok);
		}
	}
	else if (lrg_dialog_node_get_next_node_id (node) != NULL)
		add_choice (self, "Continue →", -1, TRUE);
}

static void
bubble_paint (LrgCustomWidget *widget,
              LrgPaintList    *list,
              gfloat           x,
              gfloat           y,
              gfloat           width,
              gfloat           height,
              gpointer         user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);
	LrgTheme                  *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont                   *font = lrg_theme_get_ui_font (theme);
	gfloat                     fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat                     small = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                     radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	const GrlColor            *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	const GrlColor            *text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	LrgDialogNode             *node = NULL;
	gfloat                     ty = y + 8.0f;
	guint                      i;

	lrg_paint_list_rect (list, x, y, width, height, radius, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
	lrg_paint_list_border (list, x, y, width, height, radius, 1.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
	lrg_paint_list_push_clip (list, x, y, width, height);

	if (self->play_error != NULL)
	{
		lrg_paint_list_icon (list, LRG_ICON_WARNING, x + 10.0f, ty, small + 2.0f, 1.5f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
		lrg_paint_list_text (list, font, self->play_error, x + 30.0f, ty, small, 0.0f, text);
		lrg_paint_list_pop_clip (list);
		return;
	}
	node = play_current (self);

	/* transcript: earlier lines, muted, most recent last (the current line
	 * is the last transcript entry while the dialog runs) */
	for (i = 0; i + (node != NULL ? 1u : 0u) < self->transcript->len; i++)
	{
		g_autofree gchar *line = editor_data_ellipsize (g_ptr_array_index (self->transcript, i), 90);

		lrg_paint_list_text (list, font, line, x + 10.0f, ty, small, 0.0f, muted);
		ty += small * 1.35f;
	}
	if (self->transcript->len > 1)
		ty += 6.0f;

	if (node != NULL)
	{
		const gchar      *speaker = lrg_dialog_node_get_speaker (node);
		g_auto(GStrv)     words = g_strsplit (lrg_dialog_node_get_display_text (node), " ", -1);
		g_autoptr(GString) line = g_string_new (NULL);
		guint             k;

		if (speaker != NULL)
		{
			lrg_paint_list_text (list, font, speaker, x + 10.0f, ty, small, 0.0f,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
			ty += small * 1.4f;
		}
		for (k = 0; words[k] != NULL; k++)
		{
			g_autofree gchar *probe = line->len > 0 ? g_strconcat (line->str, " ", words[k], NULL) : g_strdup (words[k]);
			gfloat            w = 0.0f;

			lrg_paint_measure_text (font, probe, fs, 0.0f, &w, NULL);
			if (w > width - 20.0f && line->len > 0)
			{
				lrg_paint_list_text (list, font, line->str, x + 10.0f, ty, fs, 0.0f, text);
				ty += fs * 1.3f;
				g_string_assign (line, words[k]);
			}
			else
				g_string_assign (line, probe);
		}
		if (line->len > 0)
			lrg_paint_list_text (list, font, line->str, x + 10.0f, ty, fs, 0.0f, text);
	}
	else if (self->runner != NULL && self->transcript->len > 0)
		lrg_paint_list_text (list, font, "— end of dialog —", x + 10.0f, ty, small, 0.0f, muted);
	else
		lrg_paint_list_text (list, font, "Press Play to walk the dialog", x + 10.0f, ty, small, 0.0f, muted);
	lrg_paint_list_pop_clip (list);
}

/* Frames every card once the graph has its size (the first frames). */
static gboolean
bubble_step (LrgCustomWidget *widget,
             gfloat           delta_time,
             gpointer         user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);

	if (!self->framed && self->graph != NULL && lrg_widget_get_width (LRG_WIDGET (self->graph)) > 0.0f)
	{
		self->framed = TRUE;
		lrg_node_graph_frame_all (self->graph, FALSE);
	}
	return FALSE;
}

static void
on_play_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;

	lrg_editor_dialog_tree_editor_play (self, NULL, &error);
}

static void
on_play_here_chip (LrgWidget *chip,
                   gpointer   user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *at = g_strdup (self->selected);

	lrg_editor_dialog_tree_editor_play (self, at, &error);
}

/* --------------------------------------------------------------------------
 * Toolbar
 * -------------------------------------------------------------------------- */

static void
on_add_node (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *id = NULL;
	gfloat                     cx = 0.0f, cy = 0.0f;

	lrg_node_graph_get_center (self->graph, &cx, &cy);
	id = lrg_editor_dialog_tree_editor_add_node (self, NULL, cx - BODY_W * 0.5f, cy - 40.0f, &error);
	if (id == NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_add_response (LrgWidget *chip,
                 gpointer   user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *id = g_strdup (self->selected);

	if (id != NULL && !lrg_editor_dialog_tree_editor_add_response (self, id, "...", NULL, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_delete (LrgWidget *chip,
           gpointer   user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *id = g_strdup (self->selected);

	if (id != NULL && !lrg_editor_dialog_tree_editor_remove_node (self, id, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

/* Auto layout: forget every stored position (one undo step). */
static void
on_layout (LrgWidget *chip,
           gpointer   user_data)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (user_data);
	LrgEditorDocument         *doc = doc_of (self);
	gint                       n = lrg_editor_document_get_length (doc, "/nodes");
	g_autoptr(GError)          error = NULL;
	gint                       i;

	lrg_editor_document_begin_group (doc, "Auto-layout dialog");
	for (i = 0; i < n && error == NULL; i++)
	{
		g_autofree gchar *path = node_path (i, "editor");

		if (lrg_editor_document_has_path (doc, path))
			lrg_editor_document_remove (doc, path, &error);
	}
	lrg_editor_document_end_group (doc);
	if (error != NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	lrg_node_graph_frame_all (self->graph, TRUE);
}

/* --------------------------------------------------------------------------
 * LrgEditorDataEditor
 * -------------------------------------------------------------------------- */

static void
dialog_refresh (LrgEditorDataEditor *editor,
                const gchar         *path)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (editor);
	LrgPropertySource         *source;

	if (self->syncing)
		return;
	rebuild_graph (self);
	source = self->grid != NULL ? lrg_property_grid_get_source (self->grid, 0) : NULL;
	/* the shape of the rows may have changed (responses added, renamed) */
	if (source == NULL || fields_stale (self))
		rebuild_fields (self);
	else
		lrg_property_source_emit_changed (source, NULL);
}

/* "/nodes/<i>/..." selects that node; a tree-level path clears. */
static void
dialog_reveal (LrgEditorDataEditor *editor,
               const gchar         *path,
               guint                line)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (editor);
	g_auto(GStrv)              parts = NULL;
	g_autofree gchar          *at_line = NULL;

	if ((path == NULL || *path == '\0') && line > 0)
		path = at_line = lrg_editor_document_get_path_at_line (doc_of (self), line);
	if (path == NULL)
		return;
	parts = lrg_editor_document_path_split (path, NULL);
	if (parts != NULL && g_strv_length (parts) >= 2 && g_strcmp0 (parts[0], "nodes") == 0)
	{
		g_autofree gchar *ipath = g_strdup_printf ("/nodes/%s/id", parts[1]);
		const gchar      *id = lrg_editor_document_get_scalar (doc_of (self), ipath);
		LrgGraphNode     *card = id != NULL ? lrg_node_graph_get_node (self->graph, id) : NULL;

		lrg_editor_dialog_tree_editor_select (self, id);
		if (card != NULL)
			lrg_node_graph_frame_selection (self->graph, TRUE);
	}
	else
		lrg_editor_dialog_tree_editor_select (self, NULL);
}

static gchar *
dialog_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (editor);
	const gchar               *at = lrg_editor_dialog_tree_editor_get_play_node (self);

	return g_strdup_printf ("cards=%u links=%u selected=%s playing=%s",
	                        lrg_node_graph_get_n_nodes (self->graph), lrg_node_graph_get_n_links (self->graph),
	                        self->selected != NULL ? self->selected : "", at != NULL ? at : "");
}

static LrgWidget *
add_tool (LrgEditorDataEditor *editor,
          const gchar         *name,
          LrgIcon              icon,
          const gchar         *label,
          EditorChipFunc       func)
{
	LrgWidget *chip = editor_chip_new (name, icon, label, func, editor);

	lrg_editor_data_editor_add_tool (editor, chip);
	g_object_unref (chip);
	return chip;
}

static void
lrg_editor_dialog_tree_editor_constructed (GObject *object)
{
	static const LrgCustomWidgetFuncs bubble_funcs = { NULL, bubble_paint, bubble_step, NULL, { NULL } };
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (object);
	LrgEditorDataEditor       *editor = LRG_EDITOR_DATA_EDITOR (object);
	LrgWidget                 *bar, *chip;

	G_OBJECT_CLASS (lrg_editor_dialog_tree_editor_parent_class)->constructed (object);

	/* --- toolbar --- */
	add_tool (editor, "dialog-add-node", LRG_ICON_PLUS, "Node", on_add_node);
	add_tool (editor, "dialog-add-response", LRG_ICON_PLUS, "Response", on_add_response);
	chip = add_tool (editor, "dialog-delete", LRG_ICON_TRASH, NULL, on_delete);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
	add_tool (editor, "dialog-layout", LRG_ICON_REFRESH, "Layout", on_layout);

	/* --- graph --- */
	self->graph = lrg_node_graph_new ();
	lrg_widget_set_name (LRG_WIDGET (self->graph), "dialog-graph");
	lrg_node_graph_set_show_minimap (self->graph, TRUE);
	g_signal_connect_object (self->graph, "link-added", G_CALLBACK (on_link_added), self, 0);
	g_signal_connect_object (self->graph, "link-removed", G_CALLBACK (on_link_removed), self, 0);
	g_signal_connect_object (self->graph, "node-moved", G_CALLBACK (on_node_moved), self, 0);
	g_signal_connect_object (self->graph, "node-removed", G_CALLBACK (on_node_removed), self, 0);
	g_signal_connect_object (self->graph, "node-duplicated", G_CALLBACK (on_node_duplicated), self, 0);
	g_signal_connect_object (self->graph, "link-dropped", G_CALLBACK (on_link_dropped), self, 0);
	g_signal_connect_object (self->graph, "selection-changed", G_CALLBACK (on_graph_selection), self, 0);

	/* --- side: grid over the play test --- */
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "dialog-grid");

	self->play_box = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->play_box, "dialog-play");
	lrg_container_set_spacing (LRG_CONTAINER (self->play_box), 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (self->play_box), 6.0f);
	bar = LRG_WIDGET (lrg_hbox_new ());
	lrg_container_set_spacing (LRG_CONTAINER (bar), 4.0f);
	chip = editor_chip_new ("dialog-play-start", LRG_ICON_PLAY, "Play", on_play_chip, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_SUCCESS);
	lrg_container_add_child (LRG_CONTAINER (bar), chip);
	g_object_unref (chip);
	chip = editor_chip_new ("dialog-play-here", LRG_ICON_ARROW_RIGHT, "From Selected", on_play_here_chip, self);
	lrg_container_add_child (LRG_CONTAINER (bar), chip);
	g_object_unref (chip);
	lrg_container_add_child (LRG_CONTAINER (self->play_box), bar);
	g_object_unref (bar);
	self->bubble = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->bubble, "dialog-play-text");
	lrg_custom_widget_set_preferred_width (LRG_CUSTOM_WIDGET (self->bubble), 240.0f);
	lrg_custom_widget_set_preferred_height (LRG_CUSTOM_WIDGET (self->bubble), 150.0f);
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->bubble), &bubble_funcs, self, NULL);
	lrg_widget_set_fill_weight (self->bubble, 1.0f);
	lrg_container_add_child (LRG_CONTAINER (self->play_box), self->bubble);
	g_object_unref (self->bubble);
	self->choices = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->choices, "dialog-play-choices");
	lrg_container_set_spacing (LRG_CONTAINER (self->choices), 4.0f);
	lrg_container_add_child (LRG_CONTAINER (self->play_box), self->choices);
	g_object_unref (self->choices);

	self->side = lrg_splitter_new (LRG_ORIENTATION_VERTICAL);
	lrg_widget_set_name (LRG_WIDGET (self->side), "dialog-side");
	lrg_splitter_set_start_child (self->side, LRG_WIDGET (self->grid));
	g_object_unref (self->grid);
	lrg_splitter_set_end_child (self->side, self->play_box);
	g_object_unref (self->play_box);
	lrg_splitter_set_fraction (self->side, 0.55f, FALSE);
	lrg_splitter_set_min_sizes (self->side, 120.0f, 140.0f);

	self->split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (self->split), "dialog-split");
	lrg_splitter_set_start_child (self->split, LRG_WIDGET (self->graph));
	g_object_unref (self->graph);
	lrg_splitter_set_end_child (self->split, LRG_WIDGET (self->side));
	g_object_unref (self->side);
	lrg_splitter_set_fraction (self->split, 0.66f, FALSE);
	lrg_splitter_set_min_sizes (self->split, 240.0f, 220.0f);
	lrg_editor_data_editor_set_content (editor, LRG_WIDGET (self->split));
	g_object_unref (self->split);

	rebuild_graph (self);
	rebuild_fields (self);
}

static void
lrg_editor_dialog_tree_editor_dispose (GObject *object)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (object);

	g_clear_handle_id (&self->flush_id, g_source_remove);
	if (self->bubble != NULL)
		lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->bubble), NULL, NULL, NULL);
	if (self->graph != NULL)
		g_signal_handlers_disconnect_by_data (self->graph, self);
	self->graph = NULL;
	self->grid = NULL;
	self->bubble = self->choices = self->play_box = NULL;
	self->split = self->side = NULL;
	if (self->runner != NULL)
		g_signal_handlers_disconnect_by_data (self->runner, self);
	g_clear_object (&self->runner);
	g_clear_object (&self->play_tree);
	G_OBJECT_CLASS (lrg_editor_dialog_tree_editor_parent_class)->dispose (object);
}

static void
lrg_editor_dialog_tree_editor_finalize (GObject *object)
{
	LrgEditorDialogTreeEditor *self = LRG_EDITOR_DIALOG_TREE_EDITOR (object);

	g_ptr_array_unref (self->ops);
	g_ptr_array_unref (self->transcript);
	g_free (self->selected);
	g_free (self->play_error);
	g_free (self->shown_hint);
	G_OBJECT_CLASS (lrg_editor_dialog_tree_editor_parent_class)->finalize (object);
}

static void
lrg_editor_dialog_tree_editor_class_init (LrgEditorDialogTreeEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *editor_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_dialog_tree_editor_constructed;
	object_class->dispose = lrg_editor_dialog_tree_editor_dispose;
	object_class->finalize = lrg_editor_dialog_tree_editor_finalize;
	editor_class->refresh = dialog_refresh;
	editor_class->reveal = dialog_reveal;
	editor_class->get_state = dialog_get_state;
}

static void
lrg_editor_dialog_tree_editor_init (LrgEditorDialogTreeEditor *self)
{
	self->ops = g_ptr_array_new_with_free_func (op_free);
	self->transcript = g_ptr_array_new_with_free_func (g_free);
	self->shown_index = -2;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorDialogTreeEditor *
lrg_editor_dialog_tree_editor_new (LrgEditorApp      *app,
                                   LrgEditorDocument *document)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);

	return g_object_new (LRG_TYPE_EDITOR_DIALOG_TREE_EDITOR, "app", app, "document", document, NULL);
}

LrgNodeGraph *
lrg_editor_dialog_tree_editor_get_graph (LrgEditorDialogTreeEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DIALOG_TREE_EDITOR (self), NULL);
	return self->graph;
}

const gchar *
lrg_editor_dialog_tree_editor_get_selected (LrgEditorDialogTreeEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DIALOG_TREE_EDITOR (self), NULL);
	return self->selected;
}

gboolean
lrg_editor_dialog_tree_editor_select (LrgEditorDialogTreeEditor *self,
                                      const gchar               *node_id)
{
	LrgGraphNode *card;

	g_return_val_if_fail (LRG_IS_EDITOR_DIALOG_TREE_EDITOR (self), FALSE);

	if (node_id != NULL && node_index (self, node_id) < 0)
		return FALSE;
	card = node_id != NULL ? lrg_node_graph_get_node (self->graph, node_id) : NULL;
	self->syncing = TRUE;
	if (card != NULL)
		lrg_node_graph_select_node (self->graph, card, FALSE);
	else
		lrg_node_graph_unselect_all (self->graph);
	self->syncing = FALSE;
	if (g_strcmp0 (node_id, self->selected) != 0)
	{
		g_free (self->selected);
		self->selected = g_strdup (node_id);
		rebuild_fields (self);
	}
	return TRUE;
}

gchar *
lrg_editor_dialog_tree_editor_add_node (LrgEditorDialogTreeEditor  *self,
                                        const gchar                *node_id,
                                        gfloat                      x,
                                        gfloat                      y,
                                        GError                    **error)
{
	LrgEditorDocument *doc;
	g_autofree gchar  *id = NULL;
	gboolean           ok;

	g_return_val_if_fail (LRG_IS_EDITOR_DIALOG_TREE_EDITOR (self), NULL);

	doc = doc_of (self);
	if (node_id != NULL && node_index (self, node_id) >= 0)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_EXISTS,
		             "a node '%s' already exists", node_id);
		return NULL;
	}
	id = node_id != NULL ? g_strdup (node_id) : free_id (self, "node");
	self->syncing = TRUE;
	lrg_editor_document_begin_group (doc, "Add dialog node");
	ok = insert_node (self, NULL, id, x, y, error);
	lrg_editor_document_end_group (doc);
	self->syncing = FALSE;
	rebuild_graph (self);
	if (!ok)
		return NULL;
	lrg_editor_dialog_tree_editor_select (self, id);
	return g_steal_pointer (&id);
}

gboolean
lrg_editor_dialog_tree_editor_add_response (LrgEditorDialogTreeEditor  *self,
                                            const gchar                *node_id,
                                            const gchar                *text,
                                            const gchar                *next,
                                            GError                    **error)
{
	LrgEditorDocument *doc;
	gint               index;
	g_autofree gchar  *rpath = NULL;
	g_autofree gchar  *npath = NULL;
	g_autofree gchar  *target = NULL;
	gint               r;
	gboolean           ok = TRUE;

	g_return_val_if_fail (LRG_IS_EDITOR_DIALOG_TREE_EDITOR (self), FALSE);
	g_return_val_if_fail (text != NULL, FALSE);

	doc = doc_of (self);
	index = node_index (self, node_id);
	if (index < 0)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no dialog node '%s'",
		             node_id);
		return FALSE;
	}
	rpath = node_path (index, "responses");
	npath = node_path (index, "next");
	/* the node's own next moves onto its first response */
	target = g_strdup (next != NULL ? next : lrg_editor_document_get_scalar (doc, npath));

	lrg_editor_document_begin_group (doc, "Add dialog response");
	if (lrg_editor_document_get_kind (doc, rpath) != LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
		ok = lrg_editor_document_replace (doc, rpath, "[]", error);
	if (ok)
		ok = lrg_editor_document_insert_item (doc, rpath, -1, "{}", error);
	r = lrg_editor_document_get_length (doc, rpath) - 1;
	if (ok)
	{
		g_autofree gchar *tpath = g_strdup_printf ("%s/%d/text", rpath, r);

		ok = lrg_editor_document_set_string (doc, tpath, text, 0, error);
	}
	if (ok && target != NULL)
	{
		g_autofree gchar *xpath = g_strdup_printf ("%s/%d/next", rpath, r);

		ok = lrg_editor_document_set_string (doc, xpath, target, 0, error);
	}
	if (ok && lrg_editor_document_has_path (doc, npath))
		ok = lrg_editor_document_remove (doc, npath, error);
	lrg_editor_document_end_group (doc);
	return ok;
}

gboolean
lrg_editor_dialog_tree_editor_link (LrgEditorDialogTreeEditor  *self,
                                    const gchar                *from,
                                    gint                        response,
                                    const gchar                *to,
                                    GError                    **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DIALOG_TREE_EDITOR (self), FALSE);
	g_return_val_if_fail (from != NULL, FALSE);

	return write_link (self, from, response, to, error);
}

gboolean
lrg_editor_dialog_tree_editor_remove_node (LrgEditorDialogTreeEditor  *self,
                                           const gchar                *node_id,
                                           GError                    **error)
{
	gint              index;
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_DIALOG_TREE_EDITOR (self), FALSE);

	index = node_index (self, node_id);
	if (index < 0)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "no dialog node '%s'",
		             node_id);
		return FALSE;
	}
	path = node_path (index, NULL);
	return lrg_editor_document_remove (doc_of (self), path, error);
}

void
lrg_editor_dialog_tree_editor_flush (LrgEditorDialogTreeEditor *self)
{
	g_return_if_fail (LRG_IS_EDITOR_DIALOG_TREE_EDITOR (self));
	flush_ops (self);
}

gboolean
lrg_editor_dialog_tree_editor_play (LrgEditorDialogTreeEditor  *self,
                                    const gchar                *node_id,
                                    GError                    **error)
{
	g_autofree gchar *data = NULL;
	GError           *local = NULL;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_DIALOG_TREE_EDITOR (self), FALSE);

	/* a fresh runner over the document as it is now */
	if (self->runner != NULL)
		g_signal_handlers_disconnect_by_data (self->runner, self);
	g_clear_object (&self->runner);
	g_clear_object (&self->play_tree);
	g_clear_pointer (&self->play_error, g_free);
	g_ptr_array_set_size (self->transcript, 0);
	self->play_done = FALSE;

	data = lrg_editor_document_to_data (doc_of (self), NULL);
	self->play_tree = lrg_dialog_tree_new_from_data (data, -1, &local);
	if (self->play_tree == NULL)
	{
		self->play_error = g_strdup (local->message);
		g_propagate_error (error, local);
		sync_play (self);
		return FALSE;
	}
	self->runner = lrg_dialog_runner_new ();
	lrg_dialog_runner_set_tree (self->runner, self->play_tree);
	g_signal_connect (self->runner, "node-entered", G_CALLBACK (on_node_entered), self);
	ok = node_id != NULL ? lrg_dialog_runner_start_at (self->runner, node_id, &local)
	                     : lrg_dialog_runner_start (self->runner, &local);
	if (!ok)
	{
		self->play_error = g_strdup (local != NULL ? local->message : "the dialog did not start");
		if (local != NULL)
			g_propagate_error (error, local);
	}
	sync_play (self);
	return ok;
}

gboolean
lrg_editor_dialog_tree_editor_play_choose (LrgEditorDialogTreeEditor *self,
                                           gint                       index)
{
	LrgDialogNode *node;
	gboolean       ok;

	g_return_val_if_fail (LRG_IS_EDITOR_DIALOG_TREE_EDITOR (self), FALSE);

	node = play_current (self);
	if (node == NULL)
		return FALSE;
	if (!lrg_dialog_runner_is_active (self->runner))
	{
		/* End on the last line */
		self->play_done = TRUE;
		sync_play (self);
		return TRUE;
	}
	if (index >= 0)
	{
		LrgDialogResponse *r = lrg_dialog_node_get_response (node, (guint) index);

		if (r == NULL)
			return FALSE;
		transcript_add (self, g_strdup_printf ("› %s", lrg_dialog_response_get_text (r)));
		ok = lrg_dialog_runner_select_response (self->runner, (guint) index, NULL);
	}
	else
	{
		ok = lrg_dialog_runner_advance (self->runner, NULL);
	}
	sync_play (self);
	return ok;
}

const gchar *
lrg_editor_dialog_tree_editor_get_play_node (LrgEditorDialogTreeEditor *self)
{
	LrgDialogNode *node;

	g_return_val_if_fail (LRG_IS_EDITOR_DIALOG_TREE_EDITOR (self), NULL);

	node = play_current (self);
	return node != NULL ? lrg_dialog_node_get_id (node) : NULL;
}
