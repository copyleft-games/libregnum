/* lrg-editor-bt-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorBtEditor: the `.rbt` data editor (see the header).
 *
 * Layout:
 *
 *   +------------+--------------------------------------+----------------+
 *   | PALETTE    | [Main] [search] [patrol]   live: ... | Node · fight   |
 *   |  sequence  |                                      |  id, name,     |
 *   |  selector  |   LrgNodeGraph, left to right:       |  type, props,  |
 *   |  ...       |   root -> children, one card per     |  params        |
 *   | ACTIONS    |   node, wires parent -> child        |  [+ Param]     |
 *   |  melee_... |                                      | BLACKBOARD     |
 *   | CONDITIONS |                                      |  key type def  |
 *   |  in_range  |                                      | SUBTREES       |
 *   +------------+--------------------------------------+----------------+
 *
 * The document is the truth. Every gesture writes it through the
 * LrgEditorDocument path API; canvas gestures that make several writes
 * share one lazily closed group (closed on idle), API edits one group
 * each. After every change the document text is parsed into an LrgBtAsset
 * (validation, kinds, generated ids) and the node index is rebuilt from
 * the document: id -> YAML path, parent, type. Nodes without `id:` are
 * indexed by the id the parsed asset generated for them (walking the
 * document and the asset in parallel), so they match the ids a running
 * LrgBtRunner reports; the first structural edit writes those ids into the
 * file so they stay stable.
 *
 * The canvas is rebuilt from the index whenever the change did not come
 * from the canvas itself; live tints and tick counts are refreshed every
 * frame from the followed entity's runner, only when its tick changed.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-bt-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../lrg-editor-ai-debug-panel.h"
#include "../../lrg-editor-server.h"
#include "../../../ai/lrg-bt-node.h"
#include "../../../ai/lrg-blackboard.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-button.h"
#include "../../../ui/lrg-combo-box.h"
#include "../../../ui/lrg-text-input.h"
#include "../../../ui/lrg-scroll-box.h"
#include "../../../ui/lrg-wrap-box.h"
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>

#define INSTALLED_KEY  "lrg-editor-bt-installed"
#define COL_DX         (240.0f)
#define ROW_DY         (118.0f)
#define CARD_W         (178.0f)
#define POS_ROOT       "/editor/positions"

typedef struct
{
	gchar *path;      /* YAML path */
	gchar *parent;    /* parent id, NULL for a root */
	gchar *type;
	gint   depth;
	gboolean explicit_id;
} NodeInfo;

static void
node_info_free (gpointer data)
{
	NodeInfo *n = data;

	g_free (n->path);
	g_free (n->parent);
	g_free (n->type);
	g_free (n);
}

struct _LrgEditorBtEditor
{
	LrgEditorDataEditor  parent_instance;

	/* the model */
	LrgBtAsset          *asset;
	gchar               *load_error;
	gchar               *shown;          /* inline subtree shown, NULL: main */
	GPtrArray           *ids;            /* depth-first order */
	GHashTable          *nodes;          /* id -> NodeInfo */
	GHashTable          *auto_pos;       /* id -> gfloat[2] */
	gchar               *sel;

	/* widgets */
	LrgNodeGraph        *canvas;
	LrgWidget           *palette;
	LrgWidget           *tree_bar;
	LrgWidget           *live_label;
	LrgWidget           *inspector_title;
	LrgPropertyGrid     *grid;
	LrgEditorDataSource *source;
	GPtrArray           *param_keys;     /* field "param-N" -> key */
	LrgWidget           *param_row;
	LrgTextInput        *param_input;
	LrgWidget           *bb_rows;
	LrgTextInput        *bb_name;
	LrgComboBox         *bb_type;
	LrgWidget           *sub_rows;
	LrgTextInput        *sub_name;
	LrgTextInput        *sub_file;
	GHashTable          *bodies;         /* id -> card body widget (borrowed) */

	/* bookkeeping */
	gboolean             syncing;
	gboolean             writing;
	gboolean             group_open;
	guint                group_idle;
	gboolean             canvas_stale;
	gboolean             grid_stale;
	gboolean             rows_stale;
	gchar               *grid_sig;
	gchar               *rows_sig;
	gchar               *palette_sig;
	gchar               *live_sig;
	guint                framed;
};

G_DEFINE_FINAL_TYPE (LrgEditorBtEditor, lrg_editor_bt_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void rebuild_canvas (LrgEditorBtEditor *self);
static void rebuild_grid (LrgEditorBtEditor *self);
static void rebuild_rows (LrgEditorBtEditor *self);

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static LrgEditorDocument *
doc_of (LrgEditorBtEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static LrgEditorApp *
app_of (LrgEditorBtEditor *self)
{
	return lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
}

static gchar *
pj (const gchar *parent,
    const gchar *token)
{
	return lrg_editor_document_path_join (parent, token);
}

static gchar *
pji (const gchar *parent,
     guint        index)
{
	g_autofree gchar *tok = g_strdup_printf ("%u", index);

	return lrg_editor_document_path_join (parent, tok);
}

static const gchar *
scalar_at (LrgEditorBtEditor *self,
           const gchar       *parent,
           const gchar       *key)
{
	g_autofree gchar *p = pj (parent, key);

	return lrg_editor_document_get_scalar (doc_of (self), p);
}

static gboolean
has_key (LrgEditorBtEditor *self,
         const gchar       *parent,
         const gchar       *key)
{
	g_autofree gchar *p = pj (parent, key);

	return lrg_editor_document_has_path (doc_of (self), p);
}

static gint
n_items (LrgEditorBtEditor *self,
         const gchar       *path)
{
	if (lrg_editor_document_get_kind (doc_of (self), path) != LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
		return 0;
	return MAX (0, lrg_editor_document_get_length (doc_of (self), path));
}

static gboolean fail_arg (GError **error, const gchar *format, ...) G_GNUC_PRINTF (2, 3);

static gboolean
fail_arg (GError      **error,
          const gchar  *format,
          ...)
{
	va_list args;

	va_start (args, format);
	if (error != NULL)
		*error = g_error_new_valist (G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, format, args);
	va_end (args);
	return FALSE;
}

/* The kind of a type name; unknown types answer from their document node. */
static LrgBtNodeKind
kind_of_type (LrgEditorBtEditor *self,
              const gchar       *type,
              const gchar       *path,
              gboolean          *known)
{
	LrgBtNodeKind kind = LRG_BT_NODE_KIND_LEAF;
	gboolean      k = type != NULL && lrg_bt_lookup_node_type (type, &kind);

	if (known != NULL)
		*known = k;
	if (k)
		return kind;
	if (path != NULL && has_key (self, path, "children"))
		return LRG_BT_NODE_KIND_COMPOSITE;
	if (path != NULL && has_key (self, path, "child"))
		return LRG_BT_NODE_KIND_DECORATOR;
	return LRG_BT_NODE_KIND_LEAF;
}

static const gchar *
kind_name (LrgBtNodeKind kind)
{
	switch (kind)
	{
	case LRG_BT_NODE_KIND_COMPOSITE:
		return "composite";
	case LRG_BT_NODE_KIND_DECORATOR:
		return "decorator";
	case LRG_BT_NODE_KIND_SUBTREE:
		return "subtree";
	case LRG_BT_NODE_KIND_LEAF:
	default:
		return "leaf";
	}
}

static NodeInfo *
info_of (LrgEditorBtEditor *self,
         const gchar       *id)
{
	return id != NULL ? g_hash_table_lookup (self->nodes, id) : NULL;
}

static LrgBtNodeKind
kind_of (LrgEditorBtEditor *self,
         const gchar       *id)
{
	NodeInfo *n = info_of (self, id);

	return n != NULL ? kind_of_type (self, n->type, n->path, NULL) : LRG_BT_NODE_KIND_LEAF;
}

/* The YAML path of the shown tree's root. */
static gchar *
shown_root (LrgEditorBtEditor *self)
{
	if (self->shown != NULL)
	{
		g_autofree gchar *sp = pj ("/subtrees", self->shown);

		return pj (sp, "root");
	}
	return g_strdup ("/root");
}

/* Whether @path lies in the tree rooted at @root (itself included). */
static gboolean
path_under (const gchar *path,
            const gchar *root)
{
	gsize n = strlen (root);

	return strncmp (path, root, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

/* Is @a an ancestor of (or the same node as) @b? */
static gboolean
is_ancestor (LrgEditorBtEditor *self,
             const gchar       *a,
             const gchar       *b)
{
	const gchar *cur = b;

	while (cur != NULL)
	{
		NodeInfo *n;

		if (g_strcmp0 (cur, a) == 0)
			return TRUE;
		n = info_of (self, cur);
		cur = n != NULL ? n->parent : NULL;
	}
	return FALSE;
}

/* --- groups: one lazily closed group per canvas gesture ----------------- */

static gboolean
close_group_idle (gpointer user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);

	self->group_idle = 0;
	if (self->group_open)
	{
		self->group_open = FALSE;
		lrg_editor_document_end_group (doc_of (self));
	}
	return G_SOURCE_REMOVE;
}

static void
flush_group (LrgEditorBtEditor *self)
{
	g_clear_handle_id (&self->group_idle, g_source_remove);
	if (self->group_open)
	{
		self->group_open = FALSE;
		lrg_editor_document_end_group (doc_of (self));
	}
}

static void
canvas_group (LrgEditorBtEditor *self,
              const gchar       *label)
{
	if (!self->group_open)
	{
		lrg_editor_document_begin_group (doc_of (self), label);
		self->group_open = TRUE;
	}
	if (self->group_idle == 0)
		self->group_idle = g_idle_add (close_group_idle, self);
}

/* One undo step of an API / panel edit (nested inside an open canvas
 * group, the group is reused). */
static gboolean
edit_begin (LrgEditorBtEditor *self,
            const gchar       *label)
{
	if (self->group_open)
		return FALSE;
	lrg_editor_document_begin_group (doc_of (self), label);
	return TRUE;
}

static void
edit_end (LrgEditorBtEditor *self,
          gboolean           opened)
{
	if (opened)
		lrg_editor_document_end_group (doc_of (self));
}

/* ==========================================================================
 * Parsing and the node index
 * ========================================================================== */

static void
index_walk (LrgEditorBtEditor *self,
            const gchar       *path,
            LrgBtAssetNode    *anode,
            const gchar       *parent,
            gint               depth)
{
	const gchar      *type = scalar_at (self, path, "type");
	const gchar      *eid = scalar_at (self, path, "id");
	g_autofree gchar *id = NULL;
	g_autofree gchar *children = pj (path, "children");
	g_autofree gchar *child = pj (path, "child");
	NodeInfo         *info;
	gint              n, i;

	if (lrg_editor_document_get_kind (doc_of (self), path) != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
		return;
	if (eid != NULL && *eid != '\0')
		id = g_strdup (eid);
	else if (anode != NULL && lrg_bt_asset_node_get_id (anode) != NULL)
		id = g_strdup (lrg_bt_asset_node_get_id (anode));
	else
		id = g_strconcat ("@", path, NULL);
	/* a duplicate id (an invalid file): keep the first, index the rest by path */
	if (g_hash_table_contains (self->nodes, id))
	{
		g_free (id);
		id = g_strconcat ("@", path, NULL);
	}
	info = g_new0 (NodeInfo, 1);
	info->path = g_strdup (path);
	info->parent = g_strdup (parent);
	info->type = g_strdup (type);
	info->depth = depth;
	info->explicit_id = eid != NULL && *eid != '\0';
	g_hash_table_insert (self->nodes, g_strdup (id), info);
	g_ptr_array_add (self->ids, g_strdup (id));

	n = n_items (self, children);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *cp = pji (children, (guint) i);
		LrgBtAssetNode   *ac = (anode != NULL && (guint) i < lrg_bt_asset_node_get_child_count (anode))
		                       ? lrg_bt_asset_node_get_child (anode, (guint) i) : NULL;

		index_walk (self, cp, ac, id, depth + 1);
	}
	if (lrg_editor_document_get_kind (doc_of (self), child) == LRG_EDITOR_DOCUMENT_NODE_MAPPING)
		index_walk (self, child, (anode != NULL && lrg_bt_asset_node_get_child_count (anode) > 0)
		                         ? lrg_bt_asset_node_get_child (anode, 0) : NULL, id, depth + 1);
}

/* Parses the document and rebuilds the index. */
static void
load_tree (LrgEditorBtEditor *self)
{
	g_autofree gchar *text = lrg_editor_document_to_data (doc_of (self), NULL);
	g_autoptr(GError) error = NULL;
	const gchar      *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	g_auto(GStrv)     subs = NULL;
	guint             i;

	g_clear_object (&self->asset);
	g_clear_pointer (&self->load_error, g_free);
	self->asset = lrg_bt_asset_new_from_data (text, -1, &error);
	if (self->asset == NULL)
		self->load_error = g_strdup (error != NULL ? error->message : "the tree does not load");
	else
	{
		if (path != NULL)
		{
			g_autofree gchar *dir = g_path_get_dirname (path);

			lrg_bt_asset_set_base_dir (self->asset, dir);
		}
		/* generated ids are assigned the first time the asset needs ids */
		lrg_bt_asset_find_node (self->asset, "\x01");
	}

	g_ptr_array_set_size (self->ids, 0);
	g_hash_table_remove_all (self->nodes);
	index_walk (self, "/root", self->asset != NULL ? lrg_bt_asset_get_root (self->asset) : NULL, NULL, 0);
	subs = lrg_editor_document_get_keys (doc_of (self), "/subtrees");
	for (i = 0; subs != NULL && subs[i] != NULL; i++)
	{
		g_autofree gchar *sp = pj ("/subtrees", subs[i]);
		g_autofree gchar *rp = pj (sp, "root");

		index_walk (self, rp, self->asset != NULL ? lrg_bt_asset_get_subtree_root (self->asset, subs[i]) : NULL, NULL,
		            0);
	}
}

/* A free explicit id made from @base ("sequence", "sequence2", ...). */
static gchar *
free_id (LrgEditorBtEditor *self,
         const gchar       *base)
{
	guint n;

	for (n = 1; n < 100000; n++)
	{
		gchar *id = n == 1 ? g_strdup (base) : g_strdup_printf ("%s%u", base, n);

		if (!g_hash_table_contains (self->nodes, id))
			return id;
		g_free (id);
	}
	return g_strdup_printf ("%s_%" G_GINT64_FORMAT, base, g_get_monotonic_time ());
}

static gboolean
valid_id (const gchar *id)
{
	return id != NULL && *id != '\0' && *id != '@' && strchr (id, '/') == NULL;
}

/* Gives every node without `id:` an explicit one (its generated id) so ids
 * survive structural edits. Writes nothing when every id is explicit. */
static void
materialize_ids (LrgEditorBtEditor *self)
{
	g_autoptr(GPtrArray) todo = g_ptr_array_new_with_free_func (g_free);
	guint                i;

	for (i = 0; i < self->ids->len; i++)
	{
		const gchar *id = g_ptr_array_index (self->ids, i);
		NodeInfo    *n = info_of (self, id);

		if (n != NULL && !n->explicit_id && valid_id (id))
			g_ptr_array_add (todo, g_strdup (id));
	}
	for (i = 0; i < todo->len; i++)
	{
		const gchar      *id = g_ptr_array_index (todo, i);
		NodeInfo         *n = info_of (self, id);
		g_autofree gchar *path = n != NULL ? g_strdup (n->path) : NULL;
		g_autofree gchar *y = editor_ai_yaml_str (id);

		if (path != NULL)
			lrg_editor_document_add_key (doc_of (self), path, "id", y, 1, NULL);
	}
}

/* ==========================================================================
 * Positions
 * ========================================================================== */

static gboolean
read_saved_pos (LrgEditorBtEditor *self,
                const gchar       *id,
                gfloat            *x,
                gfloat            *y)
{
	g_autofree gchar *p = pj (POS_ROOT, id);
	g_autofree gchar *p0 = pji (p, 0);
	g_autofree gchar *p1 = pji (p, 1);
	const gchar      *sx = lrg_editor_document_get_scalar (doc_of (self), p0);
	const gchar      *sy = lrg_editor_document_get_scalar (doc_of (self), p1);

	if (sx == NULL || sy == NULL)
		return FALSE;
	*x = (gfloat) g_ascii_strtod (sx, NULL);
	*y = (gfloat) g_ascii_strtod (sy, NULL);
	return TRUE;
}

/* Writes `editor: positions: {id: [x, y]}` (creating the mappings). */
static gboolean
write_pos (LrgEditorBtEditor *self,
           const gchar       *id,
           gfloat             x,
           gfloat             y)
{
	g_autofree gchar *sx = editor_ai_num_str (rint (x), FALSE);
	g_autofree gchar *sy = editor_ai_num_str (rint (y), FALSE);
	g_autofree gchar *text = g_strdup_printf ("[%s, %s]", sx, sy);
	g_autofree gchar *p = pj (POS_ROOT, id);

	if (lrg_editor_document_get_kind (doc_of (self), "/editor") != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
	{
		if (lrg_editor_document_has_path (doc_of (self), "/editor"))
			lrg_editor_document_remove (doc_of (self), "/editor", NULL);
		if (!lrg_editor_document_add_key (doc_of (self), "", "editor", "{positions: {}}", -1, NULL))
			return FALSE;
	}
	if (lrg_editor_document_get_kind (doc_of (self), POS_ROOT) != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
	{
		if (lrg_editor_document_has_path (doc_of (self), POS_ROOT))
			lrg_editor_document_remove (doc_of (self), POS_ROOT, NULL);
		if (!lrg_editor_document_add_key (doc_of (self), "/editor", "positions", "{}", -1, NULL))
			return FALSE;
	}
	if (lrg_editor_document_has_path (doc_of (self), p))
		return lrg_editor_document_replace (doc_of (self), p, text, NULL);
	return lrg_editor_document_add_key (doc_of (self), POS_ROOT, id, text, -1, NULL);
}

static void
remove_pos (LrgEditorBtEditor *self,
            const gchar       *id)
{
	g_autofree gchar *p = pj (POS_ROOT, id);

	if (lrg_editor_document_has_path (doc_of (self), p))
		lrg_editor_document_remove (doc_of (self), p, NULL);
}

/* Tidy tree layout of one tree: leaves in rows, parents centred. */
static gfloat
layout_walk (LrgEditorBtEditor *self,
             const gchar       *id,
             gint               depth,
             gfloat            *row)
{
	NodeInfo *n = info_of (self, id);
	gfloat   *pos;
	gfloat    first = -1.0f, last = -1.0f, y;
	guint     i;

	if (n == NULL)
		return 0.0f;
	for (i = 0; i < self->ids->len; i++)
	{
		const gchar *cid = g_ptr_array_index (self->ids, i);
		NodeInfo    *c = info_of (self, cid);

		if (c != NULL && g_strcmp0 (c->parent, id) == 0)
		{
			gfloat cy = layout_walk (self, cid, depth + 1, row);

			if (first < 0.0f)
				first = cy;
			last = cy;
		}
	}
	if (first < 0.0f)
	{
		y = *row * ROW_DY;
		*row += 1.0f;
	}
	else
		y = (first + last) * 0.5f;
	pos = g_new (gfloat, 2);
	pos[0] = (gfloat) depth * COL_DX;
	pos[1] = y;
	g_hash_table_insert (self->auto_pos, g_strdup (id), pos);
	return y;
}

static void
compute_auto_layout (LrgEditorBtEditor *self)
{
	guint  i;
	gfloat row = 0.0f;

	g_hash_table_remove_all (self->auto_pos);
	for (i = 0; i < self->ids->len; i++)
	{
		const gchar *id = g_ptr_array_index (self->ids, i);
		NodeInfo    *n = info_of (self, id);

		/* every root (main tree, inline subtrees) starts its own block */
		if (n != NULL && n->parent == NULL)
		{
			layout_walk (self, id, 0, &row);
			row += 1.0f;
		}
	}
}

static void
node_pos (LrgEditorBtEditor *self,
          const gchar       *id,
          gfloat            *x,
          gfloat            *y)
{
	gfloat *p;

	if (read_saved_pos (self, id, x, y))
		return;
	p = g_hash_table_lookup (self->auto_pos, id);
	*x = p != NULL ? p[0] : 0.0f;
	*y = p != NULL ? p[1] : 0.0f;
}

/* ==========================================================================
 * The canvas
 * ========================================================================== */

static gchar *
op_symbol (const gchar *op)
{
	static const struct { const gchar *name; const gchar *sym; } ops[] = {
		{ "eq", "==" }, { "ne", "!=" }, { "lt", "<" }, { "le", "<=" }, { "gt", ">" }, { "ge", ">=" },
		{ "set", "is set" }, { "unset", "is unset" }
	};
	guint i;

	for (i = 0; op != NULL && i < G_N_ELEMENTS (ops); i++)
		if (g_strcmp0 (op, ops[i].name) == 0)
			return g_strdup (ops[i].sym);
	return g_strdup (op != NULL ? op : "");
}

/* The card's detail line ("cooldown 1.2 s", "action · melee_attack"). */
static gchar *
node_summary (LrgEditorBtEditor *self,
              const gchar       *id,
              LrgThemeColor     *role)
{
	NodeInfo     *n = info_of (self, id);
	const gchar  *type = n != NULL ? n->type : NULL;
	const gchar  *p = n != NULL ? n->path : "";
	gboolean      known = FALSE;
	LrgBtNodeKind kind = kind_of_type (self, type, p, &known);

	*role = LRG_THEME_COLOR_TEXT_MUTED;
	if (type == NULL)
	{
		*role = LRG_THEME_COLOR_DANGER;
		return g_strdup ("no type");
	}
	if (!known)
	{
		*role = LRG_THEME_COLOR_WARNING;
		return g_strdup_printf ("%s (unknown type)", type);
	}
	if (kind == LRG_BT_NODE_KIND_COMPOSITE)
	{
		g_autofree gchar *ch = pj (p, "children");
		const gchar      *policy = scalar_at (self, p, "policy");

		return g_strdup_printf ("%s \xc2\xb7 %d children%s%s", type, n_items (self, ch), policy != NULL ? " \xc2\xb7 " : "",
		                        policy != NULL ? policy : "");
	}
	if (g_strcmp0 (type, "repeater") == 0)
	{
		const gchar *c = scalar_at (self, p, "count");

		return g_strdup_printf ("repeat \xc3\x97 %s", (c == NULL || g_strcmp0 (c, "0") == 0) && c != NULL ? "\xe2\x88\x9e"
		                                            : c != NULL ? c : "1");
	}
	if (g_strcmp0 (type, "cooldown") == 0 || g_strcmp0 (type, "time_limit") == 0)
	{
		const gchar *s = scalar_at (self, p, "seconds");

		if (s == NULL)
			*role = LRG_THEME_COLOR_DANGER;
		return g_strdup_printf ("%s %s s", g_strcmp0 (type, "cooldown") == 0 ? "cooldown" : "time limit",
		                        s != NULL ? s : "?");
	}
	if (g_strcmp0 (type, "wait") == 0)
	{
		const gchar *s = scalar_at (self, p, "duration");

		if (s == NULL)
			*role = LRG_THEME_COLOR_DANGER;
		return g_strdup_printf ("wait %s s", s != NULL ? s : "?");
	}
	if (g_strcmp0 (type, "guard") == 0 || g_strcmp0 (type, "check") == 0)
	{
		const gchar      *key = scalar_at (self, p, "key");
		const gchar      *op = scalar_at (self, p, "op");
		const gchar      *value = scalar_at (self, p, "value");
		g_autofree gchar *sym = op_symbol (op != NULL ? op : value != NULL ? "eq" : "set");

		*role = LRG_THEME_COLOR_LINK;
		return g_strdup_printf ("%s %s%s%s", key != NULL ? key : "?", sym, value != NULL ? " " : "",
		                        value != NULL ? value : "");
	}
	if (g_strcmp0 (type, "set") == 0)
	{
		const gchar *key = scalar_at (self, p, "key");
		const gchar *value = scalar_at (self, p, "value");

		*role = LRG_THEME_COLOR_LINK;
		return g_strdup_printf ("%s = %s", key != NULL ? key : "?", value != NULL ? value : "?");
	}
	if (g_strcmp0 (type, "action") == 0 || g_strcmp0 (type, "condition") == 0)
	{
		const gchar *bind = scalar_at (self, p, type);
		const gchar *script = scalar_at (self, p, "script");
		gboolean     have;

		if (script != NULL)
		{
			*role = LRG_THEME_COLOR_ACCENT;
			return g_strdup_printf ("script \xc2\xb7 %s", script);
		}
		if (bind == NULL)
		{
			*role = LRG_THEME_COLOR_DANGER;
			return g_strdup_printf ("%s \xc2\xb7 (unbound)", type);
		}
		have = g_strcmp0 (type, "action") == 0 ? lrg_bt_has_action (bind) : lrg_bt_has_condition (bind);
		*role = have ? LRG_THEME_COLOR_TEXT : LRG_THEME_COLOR_WARNING;
		return g_strdup_printf ("%s%s", bind, have ? "" : " (game)");
	}
	if (kind == LRG_BT_NODE_KIND_SUBTREE)
	{
		const gchar      *ref = scalar_at (self, p, "ref");
		g_autofree gchar *sp = ref != NULL ? pj ("/subtrees", ref) : NULL;
		const gchar      *file = sp != NULL ? scalar_at (self, sp, "path") : NULL;

		*role = LRG_THEME_COLOR_ACCENT;
		if (ref == NULL || sp == NULL || !lrg_editor_document_has_path (doc_of (self), sp))
			*role = LRG_THEME_COLOR_DANGER;
		return g_strdup_printf ("\xe2\x86\x92 %s%s%s", ref != NULL ? ref : "?", file != NULL ? " \xc2\xb7 " : " (inline)",
		                        file != NULL ? file : "");
	}
	return g_strdup (type);
}

/* The second line: parameters, or the live status while playing. */
static gchar *
node_params_line (LrgEditorBtEditor *self,
                  const gchar       *path)
{
	g_autofree gchar *pp = pj (path, "params");
	g_auto(GStrv)     keys = lrg_editor_document_get_keys (doc_of (self), pp);
	GString          *s;
	guint             i;

	if (keys == NULL || keys[0] == NULL)
		return g_strdup ("");
	s = g_string_new (NULL);
	for (i = 0; keys[i] != NULL; i++)
	{
		g_autofree gchar *kp = pj (pp, keys[i]);
		const gchar      *v = lrg_editor_document_get_scalar (doc_of (self), kp);

		g_string_append_printf (s, "%s%s=%s", i > 0 ? " " : "", keys[i], v != NULL ? v : "\xe2\x80\xa6");
	}
	return g_string_free (s, FALSE);
}

static const gchar *
icon_for (LrgEditorBtEditor *self,
          const gchar       *id)
{
	NodeInfo *n = info_of (self, id);

	if (n == NULL || n->type == NULL)
		return "warning";
	switch (kind_of (self, id))
	{
	case LRG_BT_NODE_KIND_COMPOSITE:
		return g_strcmp0 (n->type, "selector") == 0 ? "search" : g_strcmp0 (n->type, "parallel") == 0 ? "menu" : "arrow-right";
	case LRG_BT_NODE_KIND_DECORATOR:
		return (g_strcmp0 (n->type, "cooldown") == 0 || g_strcmp0 (n->type, "time_limit") == 0) ? "clock"
		       : g_strcmp0 (n->type, "guard") == 0 ? "lock" : "gear";
	case LRG_BT_NODE_KIND_SUBTREE:
		return "folder";
	case LRG_BT_NODE_KIND_LEAF:
	default:
		if (g_strcmp0 (n->type, "check") == 0 || g_strcmp0 (n->type, "condition") == 0)
			return "check";
		if (g_strcmp0 (n->type, "wait") == 0)
			return "clock";
		if (g_strcmp0 (n->type, "set") == 0)
			return "edit";
		return "play";
	}
}

static void
rebuild_canvas (LrgEditorBtEditor *self)
{
	g_autofree gchar *root = shown_root (self);
	guint             i;

	self->syncing = TRUE;
	self->canvas_stale = FALSE;
	g_clear_pointer (&self->live_sig, g_free);
	g_hash_table_remove_all (self->bodies);
	lrg_node_graph_clear (self->canvas);
	compute_auto_layout (self);

	for (i = 0; i < self->ids->len; i++)
	{
		const gchar             *id = g_ptr_array_index (self->ids, i);
		NodeInfo                *n = info_of (self, id);
		g_autoptr(LrgGraphNode)  card = NULL;
		g_autofree gchar        *summary = NULL;
		g_autofree gchar        *params = NULL;
		g_autofree gchar        *text = NULL;
		const gchar             *name;
		LrgThemeColor            role;
		LrgBtNodeKind            kind;
		LrgWidget               *body;
		LrgGraphPort            *port;
		gfloat                   x, y;

		if (n == NULL || !path_under (n->path, root))
			continue;
		name = scalar_at (self, n->path, "name");
		kind = kind_of (self, id);
		card = lrg_graph_node_new (id, name != NULL ? name : id);
		lrg_graph_node_set_category (card, n->type == NULL ? "invalid" : kind_name (kind));
		lrg_graph_node_set_icon (card, icon_for (self, id));
		lrg_graph_node_set_min_width (card, CARD_W);
		if (n->parent != NULL)
		{
			port = lrg_graph_node_add_port (card, LRG_GRAPH_PORT_INPUT, "in", "bt");
			lrg_graph_port_set_multi_link (port, FALSE);
		}
		if (kind == LRG_BT_NODE_KIND_COMPOSITE || kind == LRG_BT_NODE_KIND_DECORATOR)
		{
			port = lrg_graph_node_add_port (card, LRG_GRAPH_PORT_OUTPUT, "out", "bt");
			lrg_graph_port_set_multi_link (port, kind == LRG_BT_NODE_KIND_COMPOSITE);
		}
		summary = node_summary (self, id, &role);
		params = node_params_line (self, n->path);
		text = *params != '\0' ? g_strdup_printf ("%s\n%s", summary, params) : g_strdup (summary);
		body = editor_ai_card_body_new (text, role, CARD_W - 20.0f, 2);
		g_hash_table_insert (self->bodies, g_strdup (id), body);
		lrg_graph_node_set_body (card, body);
		g_object_unref (body);
		node_pos (self, id, &x, &y);
		lrg_graph_node_set_position (card, x, y);
		lrg_node_graph_append_node (self->canvas, card);
	}
	for (i = 0; i < self->ids->len; i++)
	{
		const gchar *id = g_ptr_array_index (self->ids, i);
		NodeInfo    *n = info_of (self, id);

		if (n != NULL && n->parent != NULL && path_under (n->path, root))
			lrg_node_graph_connect_by_name (self->canvas, n->parent, "out", id, "in", NULL);
	}
	if (self->sel != NULL)
	{
		LrgGraphNode *card = lrg_node_graph_get_node (self->canvas, self->sel);

		if (card != NULL)
			lrg_node_graph_select_node (self->canvas, card, FALSE);
	}
	self->syncing = FALSE;
}

/* --- canvas -> document ------------------------------------------------- */

static gboolean
on_can_connect (LrgNodeGraph *graph,
                LrgGraphPort *output,
                LrgGraphPort *input,
                gpointer      user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	const gchar       *parent = lrg_graph_node_get_id (lrg_graph_port_get_node (output));
	const gchar       *child = lrg_graph_node_get_id (lrg_graph_port_get_node (input));
	NodeInfo          *c = info_of (self, child);

	/* the canvas rebuild wires what the document says */
	if (self->syncing)
		return TRUE;
	if (c == NULL || info_of (self, parent) == NULL || is_ancestor (self, child, parent))
		return FALSE;
	/* a decorator takes one child (its current one may be re-wired) */
	if (kind_of (self, parent) == LRG_BT_NODE_KIND_DECORATOR && g_strcmp0 (c->parent, parent) != 0)
	{
		NodeInfo *p = info_of (self, parent);

		if (has_key (self, p->path, "child"))
			return FALSE;
	}
	return kind_of (self, parent) == LRG_BT_NODE_KIND_COMPOSITE || kind_of (self, parent) == LRG_BT_NODE_KIND_DECORATOR;
}

static void
on_link_added (LrgNodeGraph *graph,
               LrgGraphLink *link,
               gpointer      user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	const gchar       *parent = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_output (link)));
	const gchar       *child = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_input (link)));
	g_autofree gchar  *c = g_strdup (child);
	g_autofree gchar  *p = g_strdup (parent);
	g_autoptr(GError)  error = NULL;
	NodeInfo          *info;

	if (self->syncing)
		return;
	info = info_of (self, c);
	if (info != NULL && g_strcmp0 (info->parent, p) == 0)
		return;
	self->writing = TRUE;
	canvas_group (self, "Move Node");
	if (!lrg_editor_bt_editor_move_node (self, c, p, -1, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	self->writing = FALSE;
	self->canvas_stale = TRUE;
}

static void
on_link_removed (LrgNodeGraph *graph,
                 LrgGraphLink *link,
                 gpointer      user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);

	/* a tree has no detached nodes: an unplugged wire springs back (move
	 * a node by wiring it elsewhere, remove it with Delete) */
	if (!self->syncing)
		self->canvas_stale = TRUE;
}

static void
on_node_moved (LrgNodeGraph *graph,
               LrgGraphNode *card,
               gpointer      user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);

	if (self->syncing || info_of (self, lrg_graph_node_get_id (card)) == NULL)
		return;
	self->writing = TRUE;
	canvas_group (self, "Move Node");
	write_pos (self, lrg_graph_node_get_id (card), lrg_graph_node_get_x (card), lrg_graph_node_get_y (card));
	self->writing = FALSE;
}

static void
on_node_removed (LrgNodeGraph *graph,
                 LrgGraphNode *card,
                 gpointer      user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	g_autofree gchar  *id = g_strdup (lrg_graph_node_get_id (card));
	g_autoptr(GError)  error = NULL;

	if (self->syncing)
		return;
	/* a descendant of a node removed in the same gesture is gone already */
	if (info_of (self, id) != NULL)
	{
		self->writing = TRUE;
		canvas_group (self, "Delete Node");
		if (!lrg_editor_bt_editor_remove_node (self, id, &error))
			lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		self->writing = FALSE;
	}
	self->canvas_stale = TRUE;
	self->grid_stale = TRUE;
}

/* Strips every `id:` of a YAML subtree copy and gives fresh ones. */
static void
fresh_ids (LrgEditorBtEditor *self,
           const gchar       *path)
{
	const gchar      *type = scalar_at (self, path, "type");
	g_autofree gchar *base = g_strdup (type != NULL ? type : "node");
	g_autofree gchar *id = free_id (self, base);
	g_autofree gchar *idp = pj (path, "id");
	g_autofree gchar *children = pj (path, "children");
	g_autofree gchar *child = pj (path, "child");
	gint              n, i;

	if (lrg_editor_document_has_path (doc_of (self), idp))
		lrg_editor_document_set_string (doc_of (self), idp, id, 0, NULL);
	else
	{
		g_autofree gchar *y = editor_ai_yaml_str (id);

		lrg_editor_document_add_key (doc_of (self), path, "id", y, 1, NULL);
	}
	n = n_items (self, children);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *cp = pji (children, (guint) i);

		fresh_ids (self, cp);
	}
	if (lrg_editor_document_has_path (doc_of (self), child))
		fresh_ids (self, child);
}

/* Ctrl+D: a copy of the subtree right after the original (fresh ids). */
static void
on_node_duplicated (LrgNodeGraph *graph,
                    LrgGraphNode *original,
                    LrgGraphNode *copy,
                    gpointer      user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	g_autofree gchar  *id = g_strdup (lrg_graph_node_get_id (original));
	NodeInfo          *n = info_of (self, id);
	NodeInfo          *p;
	g_autofree gchar  *yaml = NULL;
	g_autofree gchar  *list = NULL;
	g_autofree gchar  *base = NULL;
	gint               index;

	self->canvas_stale = TRUE;
	if (self->syncing || n == NULL || n->parent == NULL)
		return;
	p = info_of (self, n->parent);
	if (p == NULL || kind_of (self, n->parent) != LRG_BT_NODE_KIND_COMPOSITE)
		return;
	self->writing = TRUE;
	canvas_group (self, "Duplicate Node");
	materialize_ids (self);
	n = info_of (self, id);
	p = n != NULL ? info_of (self, n->parent) : NULL;
	if (n != NULL && p != NULL)
	{
		yaml = lrg_editor_document_get_yaml (doc_of (self), n->path);
		list = pj (p->path, "children");
		base = g_path_get_basename (n->path);
		index = (gint) g_ascii_strtoll (base, NULL, 10) + 1;
		if (yaml != NULL && lrg_editor_document_insert_item (doc_of (self), list, index, yaml, NULL))
		{
			g_autofree gchar *np = pji (list, (guint) index);

			fresh_ids (self, np);
			{
				const gchar *nid = scalar_at (self, np, "id");

				if (nid != NULL)
				{
					g_autofree gchar *keep = g_strdup (nid);

					write_pos (self, keep, lrg_graph_node_get_x (copy), lrg_graph_node_get_y (copy));
					g_free (self->sel);
					self->sel = g_steal_pointer (&keep);
				}
			}
		}
	}
	self->writing = FALSE;
	self->grid_stale = TRUE;
}

/* A wire dropped on empty canvas: a new action leaf there. */
static void
on_link_dropped (LrgNodeGraph *graph,
                 LrgGraphPort *port,
                 gfloat        x,
                 gfloat        y,
                 gpointer      user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	g_autofree gchar  *from = g_strdup (lrg_graph_node_get_id (lrg_graph_port_get_node (port)));
	g_autofree gchar  *id = NULL;
	g_autoptr(GError)  error = NULL;

	if (self->syncing || lrg_graph_port_get_direction (port) != LRG_GRAPH_PORT_OUTPUT)
		return;
	id = lrg_editor_bt_editor_add_node (self, from, -1, "action", NULL, &error);
	if (id == NULL)
	{
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return;
	}
	lrg_editor_bt_editor_set_node_position (self, id, x, y);
	lrg_editor_bt_editor_select_node (self, id);
}

static void
on_node_activated (LrgNodeGraph *graph,
                   LrgGraphNode *card,
                   gpointer      user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	g_autoptr(GError)  error = NULL;

	if (kind_of (self, lrg_graph_node_get_id (card)) != LRG_BT_NODE_KIND_SUBTREE)
		return;
	if (lrg_editor_bt_editor_open_subtree (self, lrg_graph_node_get_id (card), &error) == NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
}

static void
on_canvas_selection (LrgNodeGraph *graph,
                     gpointer      user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	g_autoptr(GList)   sel = NULL;

	if (self->syncing)
		return;
	sel = lrg_node_graph_get_selected_nodes (graph);
	g_clear_pointer (&self->sel, g_free);
	if (sel != NULL && sel->next == NULL)
		self->sel = g_strdup (lrg_graph_node_get_id (sel->data));
	self->grid_stale = TRUE;
}

/* ==========================================================================
 * Live state (play in editor)
 * ========================================================================== */

/* The runner node id of a card: the card id itself, or (inside an inline
 * subtree, expanded under a subtree node) the first "use/id" match. */
static const gchar *
runner_id (LrgBtRunner *runner,
           const gchar *id)
{
	guint             i, n;
	g_autofree gchar *suffix = NULL;

	if (lrg_bt_runner_get_node (runner, id) != NULL)
		return id;
	suffix = g_strconcat ("/", id, NULL);
	n = lrg_bt_runner_get_node_count (runner);
	for (i = 0; i < n; i++)
	{
		const gchar *rid = lrg_bt_runner_get_node_id (runner, i);

		if (g_str_has_suffix (rid, suffix))
			return rid;
	}
	return NULL;
}

static void
update_live (LrgEditorBtEditor *self)
{
	LrgBtRunner      *runner = lrg_editor_bt_editor_get_live_runner (self);
	LrgTheme         *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	g_autofree gchar *sig = NULL;
	g_auto(GStrv)     path = NULL;
	guint             i, n;

	sig = runner != NULL ? g_strdup_printf ("%p:%" G_GUINT64_FORMAT, (gpointer) runner, lrg_bt_runner_get_tick_count (runner))
	                     : g_strdup ("-");
	if (g_strcmp0 (sig, self->live_sig) == 0)
		return;
	g_free (self->live_sig);
	self->live_sig = g_steal_pointer (&sig);

	path = runner != NULL ? lrg_bt_runner_dup_active_path (runner) : NULL;
	n = lrg_node_graph_get_n_nodes (self->canvas);
	for (i = 0; i < n; i++)
	{
		LrgGraphNode     *card = lrg_node_graph_get_nth_node (self->canvas, i);
		const gchar      *id = lrg_graph_node_get_id (card);
		const gchar      *rid = runner != NULL ? runner_id (runner, id) : NULL;
		LrgWidget        *body = g_hash_table_lookup (self->bodies, id);
		g_autofree gchar *summary = NULL;
		g_autofree gchar *second = NULL;
		g_autofree gchar *text = NULL;
		LrgThemeColor     role;

		if (rid != NULL)
		{
			LrgBTStatus status = lrg_bt_runner_get_node_status (runner, rid);
			gboolean    visited = lrg_bt_runner_get_node_visited (runner, rid);
			LrgBTNode  *node = lrg_bt_runner_get_node (runner, rid);

			if (!visited)
				lrg_graph_node_set_accent (card, NULL);
			else if (status == LRG_BT_STATUS_RUNNING)
				lrg_graph_node_set_accent (card, lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING));
			else if (status == LRG_BT_STATUS_SUCCESS)
				lrg_graph_node_set_accent (card, lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS));
			else if (status == LRG_BT_STATUS_FAILURE)
				lrg_graph_node_set_accent (card, lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER));
			else
				lrg_graph_node_set_accent (card, NULL);
			second = g_strdup_printf ("%s%s \xc2\xb7 %" G_GUINT64_FORMAT " ticks", visited ? "\xe2\x97\x8f " : "",
			                          lrg_bt_status_to_string (status), node != NULL ? lrg_bt_node_get_tick_count (node)
			                                                                         : 0);
		}
		else
		{
			NodeInfo *info = info_of (self, id);

			lrg_graph_node_set_accent (card, NULL);
			second = info != NULL ? node_params_line (self, info->path) : g_strdup ("");
		}
		if (body == NULL)
			continue;
		summary = node_summary (self, id, &role);
		text = *second != '\0' ? g_strdup_printf ("%s\n%s", summary, second) : g_strdup (summary);
		editor_ai_card_body_set (body, text, role);
	}
	/* the running branch: wires between consecutive active nodes */
	{
		g_autoptr(GList) links = lrg_node_graph_get_links (self->canvas);
		GList           *l;

		for (l = links; l != NULL; l = l->next)
		{
			LrgGraphLink *link = l->data;
			const gchar  *a = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_output (link)));
			const gchar  *b = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_input (link)));
			const gchar  *ra = runner != NULL ? runner_id (runner, a) : NULL;
			const gchar  *rb = runner != NULL ? runner_id (runner, b) : NULL;
			gboolean      on = FALSE;

			if (path != NULL && ra != NULL && rb != NULL)
				on = g_strv_contains ((const gchar * const *) path, ra) &&
				     g_strv_contains ((const gchar * const *) path, rb);
			lrg_graph_link_set_color (link, on ? lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING) : NULL);
		}
	}
	/* the toolbar line */
	{
		g_autofree gchar *line = NULL;

		if (runner != NULL)
		{
			LrgAiComponent   *c = lrg_editor_ai_debug_panel_find_for_file (app_of (self),
			                        lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)), FALSE);
			g_autofree gchar *who = c != NULL ? editor_ai_dup_display_name (app_of (self), c) : NULL;

			line = g_strdup_printf ("\xe2\x97\x8f live \xc2\xb7 %s \xc2\xb7 tick %" G_GUINT64_FORMAT " \xc2\xb7 %s",
			                        who != NULL ? who : "?", lrg_bt_runner_get_tick_count (runner),
			                        lrg_bt_status_to_string (lrg_bt_runner_get_status (runner)));
		}
		else
			line = g_strdup (app_of (self) != NULL && lrg_editor_app_is_playing (app_of (self))
			                 ? "playing \xc2\xb7 no entity runs this tree" : "");
		lrg_label_set_text (LRG_LABEL (self->live_label), line);
	}
}

/* ==========================================================================
 * Inspector: property grid over the selected node
 * ========================================================================== */

static const gchar *
sel_path (LrgEditorBtEditor *self)
{
	NodeInfo *n = info_of (self, self->sel);

	return n != NULL ? n->path : NULL;
}

/* The schema's key names, "a,b,c" for an enum hint. */
static gchar *
schema_keys_hint (LrgEditorBtEditor *self)
{
	gint     n = n_items (self, "/blackboard");
	GString *s;
	gint     i;

	if (n == 0)
		return NULL;
	s = g_string_new ("enum-strings:");
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *kp = pji ("/blackboard", (guint) i);
		const gchar      *name = scalar_at (self, kp, "name");

		if (name != NULL)
			g_string_append_printf (s, "%s%s", s->str[s->len - 1] == ':' ? "" : ",", name);
	}
	return g_string_free (s, FALSE);
}

static const gchar *
schema_type (LrgEditorBtEditor *self,
             const gchar       *key)
{
	gint n = n_items (self, "/blackboard");
	gint i;

	for (i = 0; key != NULL && i < n; i++)
	{
		g_autofree gchar *kp = pji ("/blackboard", (guint) i);

		if (g_strcmp0 (scalar_at (self, kp, "name"), key) == 0)
			return scalar_at (self, kp, "type");
	}
	return NULL;
}

static gchar *
types_hint (void)
{
	g_auto(GStrv) names = lrg_bt_dup_node_type_names ();
	g_autofree gchar *joined = g_strjoinv (",", names);

	return g_strconcat ("enum-strings:", joined, NULL);
}

static void
add_field (LrgEditorBtEditor *self,
           const gchar       *name,
           const gchar       *key,
           GType              type,
           const gchar       *label,
           const gchar       *group,
           const gchar       *hint,
           const gchar       *def)
{
	const gchar      *sp = sel_path (self);
	g_autofree gchar *path = key != NULL ? pj (sp, key) : g_strdup (sp);

	lrg_editor_data_source_add_field (self->source, name, path, type, label, group, hint, def);
}

static GType
scalar_gtype (LrgEditorBtEditor *self,
              const gchar       *path)
{
	GValue v = G_VALUE_INIT;
	GType  t = G_TYPE_STRING;

	if (lrg_editor_document_get_value (doc_of (self), path, &v))
	{
		if (G_VALUE_HOLDS_BOOLEAN (&v))
			t = G_TYPE_BOOLEAN;
		else if (G_VALUE_HOLDS_INT64 (&v))
			t = G_TYPE_INT64;
		else if (G_VALUE_HOLDS_DOUBLE (&v))
			t = G_TYPE_DOUBLE;
		g_value_unset (&v);
	}
	return t;
}

static const gchar * const structural_keys[] = { "type", "id", "name", "params", "child", "children", NULL };

static void
build_fields (LrgEditorBtEditor *self)
{
	const gchar      *sp = sel_path (self);
	NodeInfo         *n = info_of (self, self->sel);
	const gchar      *type = n != NULL ? n->type : NULL;
	g_autofree gchar *thint = types_hint ();
	g_autofree gchar *khint = schema_keys_hint (self);
	g_autofree gchar *pp = NULL;
	g_auto(GStrv)     pkeys = NULL;
	gboolean          known = FALSE;
	guint             i;

	add_field (self, "node-id", NULL, G_TYPE_STRING, "Id", "Node", NULL, NULL);
	add_field (self, "node-name", "name", G_TYPE_STRING, "Name", "Node", NULL, "");
	add_field (self, "node-type", "type", G_TYPE_STRING, "Type", "Node", thint, NULL);
	kind_of_type (self, type, sp, &known);

	if (g_strcmp0 (type, "parallel") == 0)
		add_field (self, "prop-policy", "policy", G_TYPE_STRING, "Policy", "Properties", "enum-strings:all,one", "all");
	else if (g_strcmp0 (type, "repeater") == 0)
		add_field (self, "prop-count", "count", G_TYPE_INT64, "Count (0 = forever)", "Properties", "range:0,10000,1",
		           "1");
	else if (g_strcmp0 (type, "cooldown") == 0 || g_strcmp0 (type, "time_limit") == 0)
		add_field (self, "prop-seconds", "seconds", G_TYPE_DOUBLE, "Seconds", "Properties", "range:0,3600,0.05", "1.0");
	else if (g_strcmp0 (type, "wait") == 0)
		add_field (self, "prop-duration", "duration", G_TYPE_DOUBLE, "Duration (s)", "Properties", "range:0,3600,0.05",
		           "1.0");
	else if (g_strcmp0 (type, "guard") == 0 || g_strcmp0 (type, "check") == 0 || g_strcmp0 (type, "set") == 0)
	{
		add_field (self, "prop-key", "key", G_TYPE_STRING, "Key", "Properties", khint, NULL);
		if (g_strcmp0 (type, "set") != 0)
			add_field (self, "prop-op", "op", G_TYPE_STRING, "Operator", "Properties",
			           "enum-strings:eq,ne,lt,le,gt,ge,set,unset", "eq");
		add_field (self, "prop-value", "value", G_TYPE_STRING, "Value", "Properties", NULL, "");
	}
	else if (g_strcmp0 (type, "action") == 0 || g_strcmp0 (type, "condition") == 0)
	{
		g_autofree gchar  *bhint = g_strdup_printf ("enum-strings:%s,script", type);

		add_field (self, "bind-kind", NULL, G_TYPE_STRING, "Binding", "Properties", bhint, type);
		add_field (self, "bind-name", has_key (self, sp, "script") ? "script" : type, G_TYPE_STRING,
		           has_key (self, sp, "script") ? "Script function" : "Name", "Properties", NULL, "");
	}
	else if (g_strcmp0 (type, "subtree") == 0)
	{
		g_auto(GStrv)     subs = lrg_editor_document_get_keys (doc_of (self), "/subtrees");
		g_autofree gchar *joined = subs != NULL ? g_strjoinv (",", subs) : g_strdup ("");
		g_autofree gchar *shint = *joined != '\0' ? g_strconcat ("enum-strings:", joined, NULL) : NULL;

		add_field (self, "prop-ref", "ref", G_TYPE_STRING, "Subtree", "Properties", shint, NULL);
	}
	else if (!known && type != NULL)
	{
		/* a registered or unknown type: every scalar key is a property */
		g_auto(GStrv) keys = lrg_editor_document_get_keys (doc_of (self), sp);

		for (i = 0; keys != NULL && keys[i] != NULL; i++)
		{
			g_autofree gchar *kp = pj (sp, keys[i]);
			g_autofree gchar *fname = g_strdup_printf ("custom-%u", i);

			if (g_strv_contains (structural_keys, keys[i]) ||
			    lrg_editor_document_get_kind (doc_of (self), kp) != LRG_EDITOR_DOCUMENT_NODE_SCALAR)
				continue;
			add_field (self, fname, keys[i], scalar_gtype (self, kp), keys[i], "Properties", NULL, NULL);
		}
	}

	/* binding parameters, typed as written */
	g_ptr_array_set_size (self->param_keys, 0);
	pp = pj (sp, "params");
	pkeys = lrg_editor_document_get_keys (doc_of (self), pp);
	for (i = 0; pkeys != NULL && pkeys[i] != NULL; i++)
	{
		g_autofree gchar *kp = pj (pp, pkeys[i]);
		g_autofree gchar *fname = g_strdup_printf ("param-%u", i);

		g_ptr_array_add (self->param_keys, g_strdup (pkeys[i]));
		lrg_editor_data_source_add_field (self->source, fname, kp, scalar_gtype (self, kp), pkeys[i], "Parameters",
		                                  NULL, NULL);
	}
}

static gboolean
source_read (LrgEditorDataSource *source,
             const gchar         *name,
             GValue              *value,
             gpointer             user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	const gchar       *sp = sel_path (self);

	if (g_strcmp0 (name, "node-id") == 0)
	{
		g_value_set_string (value, self->sel);
		return TRUE;
	}
	if (g_strcmp0 (name, "bind-kind") == 0 && sp != NULL)
	{
		g_value_set_string (value, has_key (self, sp, "script") ? "script" : scalar_at (self, sp, "type"));
		return TRUE;
	}
	return FALSE;
}

static gboolean
source_write (LrgEditorDataSource *source,
              const gchar         *name,
              GValue              *value,
              gpointer             user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	const gchar       *sp = sel_path (self);
	g_autofree gchar  *id = g_strdup (self->sel);
	g_autoptr(GError)  error = NULL;

	if (sp == NULL)
		return FALSE;
	if (g_strcmp0 (name, "node-id") == 0)
	{
		if (!lrg_editor_bt_editor_set_node_id (self, id, g_value_get_string (value), &error))
			lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return FALSE;
	}
	if (g_strcmp0 (name, "node-type") == 0)
	{
		if (!lrg_editor_bt_editor_set_node_type (self, id, g_value_get_string (value), &error))
			lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		self->grid_stale = TRUE;
		return FALSE;
	}
	if (g_strcmp0 (name, "node-name") == 0)
	{
		const gchar *v = g_value_get_string (value);

		/* an empty name removes the key: the id shows */
		if (v == NULL || *v == '\0')
		{
			lrg_editor_bt_editor_set_node_prop (self, id, "name", NULL, NULL);
			return FALSE;
		}
		return TRUE;
	}
	if (g_strcmp0 (name, "bind-kind") == 0)
	{
		const gchar      *kind = g_value_get_string (value);
		const gchar      *type = scalar_at (self, sp, "type");
		const gchar      *cur = has_key (self, sp, "script") ? scalar_at (self, sp, "script") : scalar_at (self, sp, type);
		g_autofree gchar *keep = g_strdup (cur != NULL ? cur : "");
		g_autofree gchar *y = editor_ai_yaml_str (keep);
		gboolean          opened = edit_begin (self, "Change Binding");

		lrg_editor_bt_editor_set_node_prop (self, id, g_strcmp0 (kind, "script") == 0 ? type : "script", NULL, NULL);
		lrg_editor_bt_editor_set_node_prop (self, id, g_strcmp0 (kind, "script") == 0 ? "script" : type, y, NULL);
		edit_end (self, opened);
		self->grid_stale = TRUE;
		return FALSE;
	}
	if (g_strcmp0 (name, "prop-value") == 0)
	{
		/* typed by the key's schema type ("25" for a float key is a number) */
		const gchar      *key = scalar_at (self, sp, "key");
		g_autofree gchar *y = editor_ai_text_to_yaml (g_value_get_string (value), schema_type (self, key));

		if (!lrg_editor_bt_editor_set_node_prop (self, id, "value", y, &error))
			lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return FALSE;
	}
	return TRUE;
}

static gchar *
grid_signature (LrgEditorBtEditor *self)
{
	const gchar      *sp = sel_path (self);
	GString          *s = g_string_new (NULL);
	g_autofree gchar *pp = sp != NULL ? pj (sp, "params") : NULL;
	g_auto(GStrv)     pkeys = pp != NULL ? lrg_editor_document_get_keys (doc_of (self), pp) : NULL;
	g_autofree gchar *pk = pkeys != NULL ? g_strjoinv (",", pkeys) : NULL;
	g_auto(GStrv)     keys = sp != NULL ? lrg_editor_document_get_keys (doc_of (self), sp) : NULL;
	g_autofree gchar *k = keys != NULL ? g_strjoinv (",", keys) : NULL;
	g_auto(GStrv)     subs = lrg_editor_document_get_keys (doc_of (self), "/subtrees");
	g_autofree gchar *sk = subs != NULL ? g_strjoinv (",", subs) : NULL;

	g_string_append_printf (s, "%s|%s|%s|%s|%s|%s|%d", self->sel, sp, sp != NULL ? scalar_at (self, sp, "type") : "",
	                        pk, k, sk, n_items (self, "/blackboard"));
	return g_string_free (s, FALSE);
}

static void
rebuild_grid (LrgEditorBtEditor *self)
{
	g_autofree gchar *title = NULL;

	self->grid_stale = FALSE;
	lrg_editor_data_source_clear (self->source);
	if (sel_path (self) != NULL)
	{
		NodeInfo *n = info_of (self, self->sel);

		build_fields (self);
		title = g_strdup_printf ("%s \xc2\xb7 %s", n->type != NULL ? n->type : "node", self->sel);
	}
	else
	{
		lrg_editor_data_source_add_field (self->source, "tree-name", "/name", G_TYPE_STRING, "Name", "Tree", NULL, "");
		title = g_strdup ("Tree");
	}
	lrg_label_set_text (LRG_LABEL (self->inspector_title), title);
	lrg_widget_set_visible (self->param_row, sel_path (self) != NULL);
	g_free (self->grid_sig);
	self->grid_sig = grid_signature (self);
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_property_grid_rebuild (self->grid);
}

static void
on_param_add (LrgButton *button,
              gpointer   user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	const gchar       *key = lrg_text_input_get_text (self->param_input);
	g_autofree gchar  *k = g_strstrip (g_strdup (key != NULL ? key : ""));
	g_autoptr(GError)  error = NULL;

	if (self->sel == NULL)
		return;
	if (*k == '\0')
	{
		guint n;

		for (n = 1; n < 1000; n++)
		{
			NodeInfo         *info = info_of (self, self->sel);
			g_autofree gchar *pp = info != NULL ? pj (info->path, "params") : NULL;
			g_autofree gchar *cand = g_strdup_printf ("param%u", n);

			if (pp != NULL && !has_key (self, pp, cand))
			{
				g_free (k);
				k = g_steal_pointer (&cand);
				break;
			}
		}
	}
	if (!lrg_editor_bt_editor_set_node_param (self, self->sel, k, "0", &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	lrg_text_input_set_text (self->param_input, "");
	self->grid_stale = TRUE;
}

/* ==========================================================================
 * Rows: palette, blackboard schema, subtrees, tree chips
 * ========================================================================== */

static void
on_palette_chip (LrgWidget *chip,
                 gpointer   user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	const gchar       *type = g_object_get_data (G_OBJECT (chip), "type");
	const gchar       *binding = g_object_get_data (G_OBJECT (chip), "binding");
	g_autofree gchar  *parent = NULL;
	g_autofree gchar  *id = NULL;
	g_autoptr(GError)  error = NULL;
	gint               index = -1;
	gboolean           opened;
	g_autofree gchar  *root = shown_root (self);

	/* where it goes: under the selection, else beside it, else as root */
	if (info_of (self, self->sel) != NULL)
	{
		LrgBtNodeKind k = kind_of (self, self->sel);
		NodeInfo     *n = g_hash_table_lookup (self->nodes, self->sel);

		if (k == LRG_BT_NODE_KIND_COMPOSITE || (k == LRG_BT_NODE_KIND_DECORATOR && !has_key (self, n->path, "child")))
			parent = g_strdup (self->sel);
		else if (n->parent != NULL && kind_of (self, n->parent) == LRG_BT_NODE_KIND_COMPOSITE)
		{
			g_autofree gchar *base = g_path_get_basename (n->path);

			parent = g_strdup (n->parent);
			index = (gint) g_ascii_strtoll (base, NULL, 10) + 1;
		}
	}
	if (parent == NULL && lrg_editor_document_has_path (doc_of (self), root))
	{
		/* the shown root, if it takes children */
		guint i;

		for (i = 0; i < self->ids->len; i++)
		{
			NodeInfo *n = info_of (self, g_ptr_array_index (self->ids, i));

			if (n != NULL && g_strcmp0 (n->path, root) == 0)
			{
				LrgBtNodeKind k = kind_of (self, g_ptr_array_index (self->ids, i));

				if (k == LRG_BT_NODE_KIND_COMPOSITE ||
				    (k == LRG_BT_NODE_KIND_DECORATOR && !has_key (self, n->path, "child")))
					parent = g_strdup (g_ptr_array_index (self->ids, i));
				break;
			}
		}
		if (parent == NULL)
		{
			editor_app_set_status (app_of (self), "Select a composite or decorator to add a node under it");
			return;
		}
	}
	opened = edit_begin (self, "Add Node");
	{
		/* a binding's node is named after it ("melee_attack", "melee_attack2") */
		g_autofree gchar *nid = binding != NULL ? free_id (self, binding) : NULL;

		id = lrg_editor_bt_editor_add_node (self, parent, index, type, nid, &error);
	}
	if (id != NULL && binding != NULL)
	{
		g_autofree gchar *y = editor_ai_yaml_str (binding);

		lrg_editor_bt_editor_set_node_prop (self, id, type, y, NULL);
	}
	edit_end (self, opened);
	if (id == NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	else
		lrg_editor_bt_editor_select_node (self, id);
}

static void
palette_section (LrgEditorBtEditor   *self,
                 const gchar         *heading,
                 const gchar * const *items,
                 const gchar         *type,
                 LrgThemeColor        tint)
{
	LrgWidget *wrap = LRG_WIDGET (lrg_wrap_box_new ());
	guint      i;

	editor_ai_add (self->palette, editor_ai_heading (heading));
	lrg_container_set_spacing (LRG_CONTAINER (wrap), 3.0f);
	for (i = 0; items != NULL && items[i] != NULL; i++)
	{
		g_autofree gchar *cname = g_strdup_printf ("bt-palette-%s%s%s", type != NULL ? type : "", type != NULL ? "-" : "",
		                                           items[i]);
		LrgWidget        *chip = editor_chip_new (cname, LRG_ICON_PLUS, items[i], on_palette_chip, self);

		g_object_set_data_full (G_OBJECT (chip), "type", g_strdup (type != NULL ? type : items[i]), g_free);
		if (type != NULL)
			g_object_set_data_full (G_OBJECT (chip), "binding", g_strdup (items[i]), g_free);
		editor_chip_set_tint (chip, tint);
		editor_ai_add (wrap, chip);
	}
	if (items == NULL || items[0] == NULL)
		editor_ai_add (wrap, editor_ai_heading ("(none registered)"));
	editor_ai_add (self->palette, wrap);
}

/* The palette: built-in types by kind, registered actions/conditions. */
static void
rebuild_palette (LrgEditorBtEditor *self)
{
	g_auto(GStrv)       types = lrg_bt_dup_node_type_names ();
	g_auto(GStrv)       actions = lrg_bt_dup_action_names ();
	g_auto(GStrv)       conds = lrg_bt_dup_condition_names ();
	g_autoptr(GPtrArray) comp = g_ptr_array_new ();
	g_autoptr(GPtrArray) deco = g_ptr_array_new ();
	g_autoptr(GPtrArray) leaf = g_ptr_array_new ();
	g_autofree gchar   *ja = g_strjoinv (",", types);
	g_autofree gchar   *jb = g_strjoinv (",", actions);
	g_autofree gchar   *jc = g_strjoinv (",", conds);
	g_autofree gchar   *sig = g_strdup_printf ("%s|%s|%s", ja, jb, jc);
	guint               i;

	if (g_strcmp0 (sig, self->palette_sig) == 0)
		return;
	g_free (self->palette_sig);
	self->palette_sig = g_steal_pointer (&sig);
	lrg_container_remove_all (LRG_CONTAINER (self->palette));
	for (i = 0; types != NULL && types[i] != NULL; i++)
	{
		LrgBtNodeKind kind = LRG_BT_NODE_KIND_LEAF;

		lrg_bt_lookup_node_type (types[i], &kind);
		g_ptr_array_add (kind == LRG_BT_NODE_KIND_COMPOSITE ? comp : kind == LRG_BT_NODE_KIND_DECORATOR ? deco : leaf,
		                 types[i]);
	}
	g_ptr_array_add (comp, NULL);
	g_ptr_array_add (deco, NULL);
	g_ptr_array_add (leaf, NULL);
	palette_section (self, "COMPOSITES", (const gchar * const *) comp->pdata, NULL, LRG_THEME_COLOR_ACCENT);
	palette_section (self, "DECORATORS", (const gchar * const *) deco->pdata, NULL, LRG_THEME_COLOR_LINK);
	palette_section (self, "LEAVES", (const gchar * const *) leaf->pdata, NULL, LRG_THEME_COLOR_SUCCESS);
	palette_section (self, "ACTIONS (registered)", (const gchar * const *) actions, "action", LRG_THEME_COLOR_WARNING);
	palette_section (self, "CONDITIONS (registered)", (const gchar * const *) conds, "condition",
	                 LRG_THEME_COLOR_WARNING);
	editor_ai_add (self->palette, editor_panel_hint_new ("bt-palette-hint",
	               "Click adds under the selected node. Drag a wire onto a card to re-parent it, "
	               "drop it on empty canvas for a new action."));
}

static void
on_key_default (LrgTextInput *input,
                gpointer      user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	const gchar       *key = g_object_get_data (G_OBJECT (input), "key");
	const gchar       *text = lrg_text_input_get_text (input);
	gint               n = n_items (self, "/blackboard");
	gint               i;

	if (self->syncing)
		return;
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *kp = pji ("/blackboard", (guint) i);

		if (g_strcmp0 (scalar_at (self, kp, "name"), key) == 0)
		{
			g_autofree gchar *dp = pj (kp, "default");

			if (text == NULL || *text == '\0')
			{
				if (lrg_editor_document_has_path (doc_of (self), dp))
					lrg_editor_document_remove (doc_of (self), dp, NULL);
			}
			else
			{
				g_autofree gchar *y = editor_ai_text_to_yaml (text, scalar_at (self, kp, "type"));

				if (lrg_editor_document_has_path (doc_of (self), dp))
					lrg_editor_document_replace (doc_of (self), dp, y, NULL);
				else
					lrg_editor_document_add_key (doc_of (self), kp, "default", y, -1, NULL);
			}
			return;
		}
	}
}

static void
on_key_remove (LrgButton *button,
               gpointer   user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);

	lrg_editor_bt_editor_remove_key (self, g_object_get_data (G_OBJECT (button), "key"));
}

static void
on_key_add (LrgButton *button,
            gpointer   user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	g_autofree gchar  *name = g_strstrip (g_strdup (lrg_text_input_get_text (self->bb_name) != NULL
	                                                ? lrg_text_input_get_text (self->bb_name) : ""));
	const gchar       *type = lrg_combo_box_get_selected_text (self->bb_type);
	g_autoptr(GError)  error = NULL;

	if (*name == '\0')
	{
		editor_app_set_status (app_of (self), "Type a key name first");
		return;
	}
	if (!lrg_editor_bt_editor_add_key (self, name, type != NULL ? type : "float", NULL, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	else
		lrg_text_input_set_text (self->bb_name, "");
}

static void
on_sub_remove (LrgButton *button,
               gpointer   user_data)
{
	lrg_editor_bt_editor_remove_subtree (LRG_EDITOR_BT_EDITOR (user_data), g_object_get_data (G_OBJECT (button), "sub"));
}

static void
on_sub_open (LrgButton *button,
             gpointer   user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	const gchar       *name = g_object_get_data (G_OBJECT (button), "sub");
	g_autofree gchar  *sp = pj ("/subtrees", name);
	const gchar       *file = scalar_at (self, sp, "path");
	g_autoptr(GError)  error = NULL;

	if (file != NULL)
	{
		const gchar      *me = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
		g_autofree gchar *dir = me != NULL ? g_path_get_dirname (me) : g_get_current_dir ();
		g_autofree gchar *abs = g_canonicalize_filename (file, dir);

		if (lrg_editor_bt_editor_open (app_of (self), abs, &error) == NULL)
			lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	}
	else
		lrg_editor_bt_editor_show_subtree (self, name);
}

static void
on_sub_add (LrgButton *button,
            gpointer   user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);
	g_autofree gchar  *name = g_strstrip (g_strdup (lrg_text_input_get_text (self->sub_name) != NULL
	                                                ? lrg_text_input_get_text (self->sub_name) : ""));
	g_autofree gchar  *file = g_strstrip (g_strdup (lrg_text_input_get_text (self->sub_file) != NULL
	                                                ? lrg_text_input_get_text (self->sub_file) : ""));
	g_autoptr(GError)  error = NULL;

	if (*name == '\0')
	{
		editor_app_set_status (app_of (self), "Type a subtree name first");
		return;
	}
	if (!lrg_editor_bt_editor_set_subtree (self, name, *file != '\0' ? file : NULL, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	else
	{
		lrg_text_input_set_text (self->sub_name, "");
		lrg_text_input_set_text (self->sub_file, "");
	}
}

static void
on_tree_chip (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);

	lrg_editor_bt_editor_show_subtree (self, g_object_get_data (G_OBJECT (chip), "sub"));
}

static gchar *
rows_signature (LrgEditorBtEditor *self)
{
	GString       *s = g_string_new (NULL);
	g_auto(GStrv)  subs = lrg_editor_document_get_keys (doc_of (self), "/subtrees");
	gint           n = n_items (self, "/blackboard");
	gint           i;

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *kp = pji ("/blackboard", (guint) i);

		g_string_append_printf (s, "%s:%s=%s;", scalar_at (self, kp, "name"), scalar_at (self, kp, "type"),
		                        scalar_at (self, kp, "default"));
	}
	g_string_append_c (s, '|');
	for (i = 0; subs != NULL && subs[i] != NULL; i++)
	{
		g_autofree gchar *sp = pj ("/subtrees", subs[i]);

		g_string_append_printf (s, "%s=%s;", subs[i], scalar_at (self, sp, "path"));
	}
	g_string_append_printf (s, "|%s", self->shown);
	return g_string_free (s, FALSE);
}

static LrgWidget *
row_new (const gchar *name)
{
	LrgWidget *row = LRG_WIDGET (lrg_hbox_new ());

	lrg_widget_set_name (row, name);
	lrg_container_set_spacing (LRG_CONTAINER (row), 4.0f);
	return row;
}

static void
rebuild_rows (LrgEditorBtEditor *self)
{
	g_auto(GStrv) subs = lrg_editor_document_get_keys (doc_of (self), "/subtrees");
	gint          n = n_items (self, "/blackboard");
	gint          i;

	self->rows_stale = FALSE;
	g_free (self->rows_sig);
	self->rows_sig = rows_signature (self);
	self->syncing = TRUE;

	/* blackboard schema */
	lrg_container_remove_all (LRG_CONTAINER (self->bb_rows));
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *kp = pji ("/blackboard", (guint) i);
		const gchar      *name = scalar_at (self, kp, "name");
		const gchar      *type = scalar_at (self, kp, "type");
		const gchar      *def = scalar_at (self, kp, "default");
		g_autofree gchar *rname = g_strdup_printf ("bt-key-%s", name);
		g_autofree gchar *dname = g_strdup_printf ("bt-key-%s-default", name);
		g_autofree gchar *xname = g_strdup_printf ("bt-key-%s-remove", name);
		LrgWidget        *row = row_new (rname);
		LrgWidget        *label = LRG_WIDGET (lrg_label_new (name != NULL ? name : "?"));
		LrgWidget        *tl = editor_ai_heading (type != NULL ? type : "?");
		LrgWidget        *rm;

		/* name and type in fixed columns, the default takes the rest */
		lrg_widget_set_min_size (label, 96.0f, 0.0f);
		lrg_widget_set_max_size (label, 96.0f, 0.0f);
		lrg_widget_set_valign (label, LRG_ALIGN_CENTER);
		lrg_widget_set_valign (tl, LRG_ALIGN_CENTER);
		lrg_widget_set_min_size (tl, 48.0f, 0.0f);
		lrg_widget_set_max_size (tl, 48.0f, 0.0f);
		editor_ai_add (row, label);
		editor_ai_add (row, tl);
		if (g_strcmp0 (type, "object") != 0)
		{
			LrgTextInput *input = lrg_text_input_new_with_placeholder ("default");

			lrg_widget_set_name (LRG_WIDGET (input), dname);
			lrg_text_input_set_font_size (input, 14.0f);
			lrg_text_input_set_text (input, def != NULL ? def : "");
			lrg_widget_set_min_size (LRG_WIDGET (input), 60.0f, 0.0f);
			lrg_widget_set_max_size (LRG_WIDGET (input), 150.0f, 0.0f);
			lrg_widget_set_fill_weight (LRG_WIDGET (input), 1.0f);
			g_object_set_data_full (G_OBJECT (input), "key", g_strdup (name), g_free);
			g_signal_connect (input, "submitted", G_CALLBACK (on_key_default), self);
			editor_ai_add (row, LRG_WIDGET (input));
		}
		rm = editor_ai_button (xname, "\xc3\x97", G_CALLBACK (on_key_remove), self);
		g_object_set_data_full (G_OBJECT (rm), "key", g_strdup (name), g_free);
		editor_ai_add (row, rm);
		editor_ai_add (self->bb_rows, row);
	}
	if (n == 0)
		editor_ai_add (self->bb_rows, editor_ai_heading ("No keys: check/guard/set accept any key"));

	/* subtrees */
	lrg_container_remove_all (LRG_CONTAINER (self->sub_rows));
	for (i = 0; subs != NULL && subs[i] != NULL; i++)
	{
		g_autofree gchar *sp = pj ("/subtrees", subs[i]);
		const gchar      *file = scalar_at (self, sp, "path");
		g_autofree gchar *rname = g_strdup_printf ("bt-sub-%s", subs[i]);
		g_autofree gchar *oname = g_strdup_printf ("bt-sub-%s-open", subs[i]);
		g_autofree gchar *xname = g_strdup_printf ("bt-sub-%s-remove", subs[i]);
		g_autofree gchar *text = g_strdup_printf ("%s %s %s", subs[i], file != NULL ? "\xe2\x86\x92" : "",
		                                          file != NULL ? file : "(inline)");
		LrgWidget        *row = row_new (rname);
		LrgWidget        *label = LRG_WIDGET (lrg_label_new (text));
		LrgWidget        *b;

		lrg_widget_set_fill_weight (label, 1.0f);
		lrg_widget_set_valign (label, LRG_ALIGN_CENTER);
		editor_ai_add (row, label);
		b = editor_ai_button (oname, file != NULL ? "Open" : "Show", G_CALLBACK (on_sub_open), self);
		g_object_set_data_full (G_OBJECT (b), "sub", g_strdup (subs[i]), g_free);
		editor_ai_add (row, b);
		b = editor_ai_button (xname, "\xc3\x97", G_CALLBACK (on_sub_remove), self);
		g_object_set_data_full (G_OBJECT (b), "sub", g_strdup (subs[i]), g_free);
		editor_ai_add (row, b);
		editor_ai_add (self->sub_rows, row);
	}

	/* tree chips over the canvas: the main tree, then inline subtrees */
	lrg_container_remove_all (LRG_CONTAINER (self->tree_bar));
	{
		LrgWidget *chip = editor_chip_new ("bt-tree-main", LRG_ICON_HOME, "Main tree", on_tree_chip, self);

		editor_chip_set_checked (chip, self->shown == NULL);
		editor_chip_set_tint (chip, LRG_THEME_COLOR_ACCENT);
		editor_ai_add (self->tree_bar, chip);
	}
	for (i = 0; subs != NULL && subs[i] != NULL; i++)
	{
		g_autofree gchar *sp = pj ("/subtrees", subs[i]);
		g_autofree gchar *cname = g_strdup_printf ("bt-tree-%s", subs[i]);
		LrgWidget        *chip;

		if (!has_key (self, sp, "root"))
			continue;
		chip = editor_chip_new (cname, LRG_ICON_FOLDER, subs[i], on_tree_chip, self);
		g_object_set_data_full (G_OBJECT (chip), "sub", g_strdup (subs[i]), g_free);
		editor_chip_set_checked (chip, g_strcmp0 (self->shown, subs[i]) == 0);
		editor_chip_set_tint (chip, LRG_THEME_COLOR_ACCENT);
		editor_ai_add (self->tree_bar, chip);
	}
	self->syncing = FALSE;
}

/* ==========================================================================
 * LrgEditorDataEditor vfuncs
 * ========================================================================== */

static void
bt_refresh (LrgEditorDataEditor *editor,
            const gchar         *path)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (editor);
	g_autofree gchar  *sig = NULL;

	if (self->canvas == NULL)
		return;
	load_tree (self);
	if (self->shown != NULL)
	{
		g_autofree gchar *root = shown_root (self);

		if (!lrg_editor_document_has_path (doc_of (self), root))
			g_clear_pointer (&self->shown, g_free);
	}
	if (self->sel != NULL && info_of (self, self->sel) == NULL)
		g_clear_pointer (&self->sel, g_free);
	if (!self->writing)
		self->canvas_stale = TRUE;
	sig = rows_signature (self);
	if (g_strcmp0 (sig, self->rows_sig) != 0)
		self->rows_stale = TRUE;
	g_free (sig);
	sig = grid_signature (self);
	if (g_strcmp0 (sig, self->grid_sig) != 0)
		self->grid_stale = TRUE;
	else
	{
		lrg_editor_data_source_document_changed (self->source, path);
		lrg_property_grid_refresh (self->grid);
	}
}

/* The id of the node an issue's path points into (deepest match). */
static const gchar *
id_for_path (LrgEditorBtEditor *self,
             const gchar       *path)
{
	const gchar *best = NULL;
	gsize        best_len = 0;
	guint        i;

	for (i = 0; path != NULL && i < self->ids->len; i++)
	{
		const gchar *id = g_ptr_array_index (self->ids, i);
		NodeInfo    *n = info_of (self, id);
		gsize        len = n != NULL ? strlen (n->path) : 0;

		if (n != NULL && path_under (path, n->path) && len > best_len)
		{
			best = id;
			best_len = len;
		}
	}
	return best;
}

static void
add_issue (LrgEditorBtEditor        *self,
           GPtrArray                *issues,
           LrgEditorProblemSeverity  severity,
           const gchar              *message)
{
	g_autofree gchar *path = editor_ai_error_path (message);
	guint             line = 0, col = 0;

	/* "node 'swing' (action): ..." names the node instead of a path */
	if (*path == '\0' && g_str_has_prefix (message, "node '"))
	{
		const gchar      *start = message + 6;
		const gchar      *end = strchr (start, '\'');
		g_autofree gchar *id = end != NULL ? g_strndup (start, end - start) : NULL;
		NodeInfo         *n = info_of (self, id);

		if (n != NULL)
		{
			g_free (path);
			path = g_strdup (n->path);
		}
	}
	while (*path != '\0' && !lrg_editor_document_has_path (doc_of (self), path))
	{
		gchar *slash = strrchr (path, '/');

		if (slash == NULL)
			break;
		*slash = '\0';
	}
	lrg_editor_document_get_line (doc_of (self), path, &line, &col);
	g_ptr_array_add (issues, lrg_editor_document_issue_new (severity, path, line, col, message));
}

/* Gives a document issue without a path (the engine names nodes, "node
 * 'swing' (action): ...", instead of key paths) the node's path and line. */
static void
locate_issue (LrgEditorBtEditor      *self,
              LrgEditorDocumentIssue *issue)
{
	const gchar      *start;
	const gchar      *end;
	g_autofree gchar *id = NULL;
	NodeInfo         *n;
	guint             line = 0, col = 0;

	if ((issue->path != NULL && *issue->path != '\0') || issue->message == NULL ||
	    !g_str_has_prefix (issue->message, "node '"))
		return;
	start = issue->message + 6;
	end = strchr (start, '\'');
	id = end != NULL ? g_strndup (start, end - start) : NULL;
	n = info_of (self, id);
	if (n == NULL)
		return;
	g_free (issue->path);
	issue->path = g_strdup (n->path);
	if (lrg_editor_document_get_line (doc_of (self), n->path, &line, &col))
	{
		issue->line = line;
		issue->column = col;
	}
}

static void
bt_validate (LrgEditorDataEditor *editor,
             GPtrArray           *issues)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (editor);
	g_auto(GStrv)      unbound = NULL;
	guint              i;

	/* the document already ran the engine's loader and validate() */
	for (i = 0; i < issues->len; i++)
		locate_issue (self, g_ptr_array_index (issues, i));
	if (self->asset == NULL)
		return;
	/* bindings the game registers at run time are not errors here */
	unbound = lrg_bt_asset_dup_unbound (self->asset, NULL);
	for (i = 0; unbound != NULL && unbound[i] != NULL; i++)
	{
		g_autofree gchar *msg = NULL;
		const gchar      *colon = strchr (unbound[i], ':');
		const gchar      *what = colon != NULL ? colon + 1 : unbound[i];
		const gchar      *key = g_str_has_prefix (unbound[i], "type:") ? "type"
		                        : g_str_has_prefix (unbound[i], "script:") ? "script"
		                        : g_str_has_prefix (unbound[i], "condition:") ? "condition" : "action";
		const gchar      *where = NULL;
		guint             k;

		for (k = 0; k < self->ids->len && where == NULL; k++)
		{
			NodeInfo *n = info_of (self, g_ptr_array_index (self->ids, k));

			if (n != NULL && g_strcmp0 (scalar_at (self, n->path, key), what) == 0)
				where = g_ptr_array_index (self->ids, k);
		}
		msg = g_strdup_printf ("node '%s' (%s): %s is not registered in the editor (the game must register it)",
		                       where != NULL ? where : "?", key, unbound[i]);
		add_issue (self, issues, g_str_has_prefix (unbound[i], "type:") ? LRG_EDITOR_PROBLEM_WARNING
		                                                               : LRG_EDITOR_PROBLEM_INFO, msg);
	}
}

static void
bt_reveal (LrgEditorDataEditor *editor,
           const gchar         *path,
           guint                line)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (editor);
	const gchar       *id = id_for_path (self, path);

	if (id != NULL)
	{
		g_autofree gchar *keep = g_strdup (id);

		lrg_editor_bt_editor_select_node (self, keep);
		lrg_node_graph_frame_selection (self->canvas, FALSE);
		if (lrg_node_graph_get_zoom (self->canvas) > 1.0f)
			lrg_node_graph_set_zoom (self->canvas, 1.0f);
	}
}

static gchar *
bt_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (editor);

	return g_strdup_printf ("tree=%s nodes=%u selected=%s live=%s", self->shown != NULL ? self->shown : "main",
	                        self->ids->len, self->sel != NULL ? self->sel : "-",
	                        lrg_editor_bt_editor_get_live_runner (self) != NULL ? "yes" : "no");
}

static gboolean
bt_save (LrgEditorDataEditor  *editor,
         GError              **error)
{
	flush_group (LRG_EDITOR_BT_EDITOR (editor));
	return LRG_EDITOR_DATA_EDITOR_CLASS (lrg_editor_bt_editor_parent_class)->save (editor, error);
}

static gboolean
bt_undo (LrgEditorDataEditor *editor)
{
	flush_group (LRG_EDITOR_BT_EDITOR (editor));
	return lrg_editor_document_undo (lrg_editor_data_editor_get_document (editor));
}

static gboolean
bt_redo (LrgEditorDataEditor *editor)
{
	flush_group (LRG_EDITOR_BT_EDITOR (editor));
	return lrg_editor_document_redo (lrg_editor_data_editor_get_document (editor));
}

static gboolean
lrg_editor_bt_editor_step (LrgWidget *widget,
                           gfloat     delta_time)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (widget);
	gboolean           busy = LRG_WIDGET_CLASS (lrg_editor_bt_editor_parent_class)->step (widget, delta_time);
	gboolean           live;

	/* deferred rebuilds: never inside a widget's own signal emission */
	if (self->canvas_stale)
		rebuild_canvas (self);
	if (self->rows_stale)
		rebuild_rows (self);
	if (self->grid_stale)
		rebuild_grid (self);
	rebuild_palette (self);
	update_live (self);
	live = lrg_editor_bt_editor_get_live_runner (self) != NULL;

	if (self->framed < 3 && lrg_widget_get_width (LRG_WIDGET (self->canvas)) > 200.0f)
	{
		self->framed++;
		editor_ai_frame_canvas (self->canvas);
	}
	return busy || live;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

static void
on_layout_chip (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (user_data);

	lrg_editor_bt_editor_auto_layout (self);
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
lrg_editor_bt_editor_constructed (GObject *object)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (object);
	LrgEditorDataEditor *base = LRG_EDITOR_DATA_EDITOR (object);
	LrgEditorApp       *app;
	LrgWidget          *left, *left_scroll, *center, *top, *inspector, *inspector_scroll, *row;
	LrgSplitter        *outer, *inner;
	static const gchar * const key_types[] = { "float", "int", "bool", "string", "object" };
	guint               i;

	G_OBJECT_CLASS (lrg_editor_bt_editor_parent_class)->constructed (object);
	app = app_of (self);
	load_tree (self);

	/* --- left: the palette --- */
	left = vbox_named ("bt-left", 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (left), 8.0f);
	self->palette = vbox_named ("bt-palette", 4.0f);
	editor_ai_add (left, g_object_ref (self->palette));
	g_object_unref (self->palette);
	left_scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (left_scroll, "bt-left-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (left_scroll), left);
	g_object_unref (left);

	/* --- center: tree chips + live line over the canvas --- */
	center = vbox_named ("bt-center", 4.0f);
	top = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (top, "bt-topbar");
	lrg_container_set_spacing (LRG_CONTAINER (top), 8.0f);
	lrg_container_set_padding (LRG_CONTAINER (top), 4.0f);
	self->tree_bar = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (self->tree_bar, "bt-trees");
	lrg_container_set_spacing (LRG_CONTAINER (self->tree_bar), 2.0f);
	editor_ai_add (top, g_object_ref (self->tree_bar));
	g_object_unref (self->tree_bar);
	self->live_label = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_name (self->live_label, "bt-live");
	lrg_widget_set_fill_weight (self->live_label, 1.0f);
	lrg_widget_set_valign (self->live_label, LRG_ALIGN_CENTER);
	editor_ai_add (top, g_object_ref (self->live_label));
	g_object_unref (self->live_label);
	editor_ai_add (center, top);
	self->canvas = lrg_node_graph_new ();
	lrg_widget_set_name (LRG_WIDGET (self->canvas), "bt-canvas");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->canvas), 1.0f);
	lrg_node_graph_set_show_minimap (self->canvas, FALSE);
	lrg_node_graph_set_snap_to_grid (self->canvas, TRUE);
	lrg_node_graph_set_animate_links (self->canvas, TRUE);
	g_signal_connect_object (self->canvas, "can-connect", G_CALLBACK (on_can_connect), self, 0);
	g_signal_connect_object (self->canvas, "link-added", G_CALLBACK (on_link_added), self, 0);
	g_signal_connect_object (self->canvas, "link-removed", G_CALLBACK (on_link_removed), self, 0);
	g_signal_connect_object (self->canvas, "link-dropped", G_CALLBACK (on_link_dropped), self, 0);
	g_signal_connect_object (self->canvas, "node-moved", G_CALLBACK (on_node_moved), self, 0);
	g_signal_connect_object (self->canvas, "node-removed", G_CALLBACK (on_node_removed), self, 0);
	g_signal_connect_object (self->canvas, "node-duplicated", G_CALLBACK (on_node_duplicated), self, 0);
	g_signal_connect_object (self->canvas, "node-activated", G_CALLBACK (on_node_activated), self, 0);
	g_signal_connect_object (self->canvas, "selection-changed", G_CALLBACK (on_canvas_selection), self, 0);
	editor_ai_add (center, g_object_ref (LRG_WIDGET (self->canvas)));
	g_object_unref (self->canvas);

	/* --- right: inspector, blackboard, subtrees --- */
	inspector = vbox_named ("bt-inspector", 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (inspector), 8.0f);
	self->inspector_title = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_name (self->inspector_title, "bt-inspector-title");
	lrg_widget_add_class (self->inspector_title, "heading");
	editor_ai_add (inspector, g_object_ref (self->inspector_title));
	g_object_unref (self->inspector_title);
	self->source = lrg_editor_data_source_new (base);
	lrg_editor_data_source_set_read_func (self->source, source_read, self, NULL);
	lrg_editor_data_source_set_write_func (self->source, source_write, self, NULL);
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "bt-grid");
	lrg_property_grid_set_label_width (self->grid, 112.0f);
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_editor_data_source_bind_grid (self->source, self->grid);
	editor_ai_add (inspector, g_object_ref (LRG_WIDGET (self->grid)));
	g_object_unref (self->grid);
	self->param_row = row_new ("bt-param-row");
	self->param_input = lrg_text_input_new_with_placeholder ("parameter name");
	lrg_widget_set_name (LRG_WIDGET (self->param_input), "bt-param-name");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->param_input), 1.0f);
	lrg_text_input_set_font_size (self->param_input, 14.0f);
	editor_ai_add (self->param_row, g_object_ref (LRG_WIDGET (self->param_input)));
	g_object_unref (self->param_input);
	editor_ai_add (self->param_row, editor_ai_button ("bt-param-add", "+ Param", G_CALLBACK (on_param_add), self));
	editor_ai_add (inspector, g_object_ref (self->param_row));
	g_object_unref (self->param_row);

	editor_ai_add (inspector, editor_ai_heading ("BLACKBOARD"));
	self->bb_rows = vbox_named ("bt-blackboard", 3.0f);
	editor_ai_add (inspector, g_object_ref (self->bb_rows));
	g_object_unref (self->bb_rows);
	row = row_new ("bt-key-add-row");
	self->bb_name = lrg_text_input_new_with_placeholder ("key");
	lrg_widget_set_name (LRG_WIDGET (self->bb_name), "bt-key-name");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->bb_name), 1.0f);
	lrg_text_input_set_font_size (self->bb_name, 14.0f);
	editor_ai_add (row, g_object_ref (LRG_WIDGET (self->bb_name)));
	g_object_unref (self->bb_name);
	self->bb_type = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->bb_type), "bt-key-type");
	for (i = 0; i < G_N_ELEMENTS (key_types); i++)
		lrg_combo_box_append (self->bb_type, key_types[i]);
	lrg_combo_box_set_selected (self->bb_type, 0);
	editor_ai_add (row, g_object_ref (LRG_WIDGET (self->bb_type)));
	g_object_unref (self->bb_type);
	editor_ai_add (row, editor_ai_button ("bt-key-add", "+ Key", G_CALLBACK (on_key_add), self));
	editor_ai_add (inspector, row);

	editor_ai_add (inspector, editor_ai_heading ("SUBTREES"));
	self->sub_rows = vbox_named ("bt-subtrees", 3.0f);
	editor_ai_add (inspector, g_object_ref (self->sub_rows));
	g_object_unref (self->sub_rows);
	row = row_new ("bt-sub-add-row");
	self->sub_name = lrg_text_input_new_with_placeholder ("name");
	lrg_widget_set_name (LRG_WIDGET (self->sub_name), "bt-sub-name");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->sub_name), 1.0f);
	lrg_text_input_set_font_size (self->sub_name, 14.0f);
	editor_ai_add (row, g_object_ref (LRG_WIDGET (self->sub_name)));
	g_object_unref (self->sub_name);
	self->sub_file = lrg_text_input_new_with_placeholder ("file.rbt (empty: inline)");
	lrg_widget_set_name (LRG_WIDGET (self->sub_file), "bt-sub-file");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->sub_file), 1.5f);
	lrg_text_input_set_font_size (self->sub_file, 14.0f);
	editor_ai_add (row, g_object_ref (LRG_WIDGET (self->sub_file)));
	g_object_unref (self->sub_file);
	editor_ai_add (row, editor_ai_button ("bt-sub-add", "+ Subtree", G_CALLBACK (on_sub_add), self));
	editor_ai_add (inspector, row);

	inspector_scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (inspector_scroll, "bt-inspector-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (inspector_scroll), inspector);
	g_object_unref (inspector);

	inner = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (inner), "bt-inner");
	lrg_splitter_set_start_child (inner, center);
	g_object_unref (center);
	lrg_splitter_set_end_child (inner, inspector_scroll);
	g_object_unref (inspector_scroll);
	lrg_splitter_set_default_fraction (inner, 0.68f);
	lrg_splitter_set_fraction (inner, 0.68f, FALSE);
	lrg_splitter_set_min_sizes (inner, 300.0f, 280.0f);

	outer = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (outer), "bt-split");
	lrg_splitter_set_start_child (outer, left_scroll);
	g_object_unref (left_scroll);
	lrg_splitter_set_end_child (outer, LRG_WIDGET (inner));
	g_object_unref (inner);
	lrg_splitter_set_default_fraction (outer, 0.17f);
	lrg_splitter_set_fraction (outer, 0.17f, FALSE);
	lrg_splitter_set_min_sizes (outer, 190.0f, 560.0f);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (outer));
	g_object_unref (outer);

	lrg_editor_data_editor_add_tool (base, editor_chip_new ("bt-layout", LRG_ICON_REFRESH, "Auto Layout",
	                                                        on_layout_chip, self));

	rebuild_palette (self);
	rebuild_canvas (self);
	rebuild_rows (self);
	rebuild_grid (self);
	if (app != NULL)
		lrg_editor_bt_editor_install (app);
}

static void
lrg_editor_bt_editor_dispose (GObject *object)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (object);

	g_clear_handle_id (&self->group_idle, g_source_remove);
	if (self->group_open)
	{
		self->group_open = FALSE;
		lrg_editor_document_end_group (doc_of (self));
	}
	G_OBJECT_CLASS (lrg_editor_bt_editor_parent_class)->dispose (object);
}

static void
lrg_editor_bt_editor_finalize (GObject *object)
{
	LrgEditorBtEditor *self = LRG_EDITOR_BT_EDITOR (object);

	g_clear_object (&self->asset);
	g_clear_object (&self->source);
	g_ptr_array_unref (self->ids);
	g_hash_table_unref (self->nodes);
	g_hash_table_unref (self->auto_pos);
	g_hash_table_unref (self->bodies);
	g_ptr_array_unref (self->param_keys);
	g_free (self->load_error);
	g_free (self->shown);
	g_free (self->sel);
	g_free (self->grid_sig);
	g_free (self->rows_sig);
	g_free (self->palette_sig);
	g_free (self->live_sig);
	G_OBJECT_CLASS (lrg_editor_bt_editor_parent_class)->finalize (object);
}

static void
lrg_editor_bt_editor_class_init (LrgEditorBtEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_bt_editor_constructed;
	object_class->dispose = lrg_editor_bt_editor_dispose;
	object_class->finalize = lrg_editor_bt_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_bt_editor_step;
	data_class->refresh = bt_refresh;
	data_class->validate = bt_validate;
	data_class->reveal = bt_reveal;
	data_class->get_state = bt_get_state;
	data_class->save = bt_save;
	data_class->undo = bt_undo;
	data_class->redo = bt_redo;
}

static void
lrg_editor_bt_editor_init (LrgEditorBtEditor *self)
{
	self->ids = g_ptr_array_new_with_free_func (g_free);
	self->nodes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, node_info_free);
	self->auto_pos = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->bodies = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->param_keys = g_ptr_array_new_with_free_func (g_free);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorBtEditor *
lrg_editor_bt_editor_new (LrgEditorApp      *app,
                          LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_BT_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorBtEditor *
lrg_editor_bt_editor_open (LrgEditorApp  *app,
                           const gchar   *path,
                           GError       **error)
{
	LrgEditorDataEditor *e;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	e = lrg_editor_data_editor_open (app, path, error);
	if (e == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_BT_EDITOR (e))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not a behaviour tree", path);
		return NULL;
	}
	return LRG_EDITOR_BT_EDITOR (e);
}

LrgEditorBtEditor *
lrg_editor_bt_editor_create (LrgEditorApp  *app,
                             const gchar   *path,
                             const gchar   *name,
                             GError       **error)
{
	g_autofree gchar *abs = NULL;
	g_autofree gchar *base = NULL;
	g_autofree gchar *yname = NULL;
	g_autofree gchar *text = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	base = g_path_get_basename (path);
	if (g_str_has_suffix (base, ".rbt"))
		base[strlen (base) - 4] = '\0';
	yname = editor_ai_yaml_str (name != NULL ? name : base);
	text = g_strdup_printf ("version: 1\nname: %s\nblackboard: []\nroot:\n  type: selector\n  id: root\n"
	                        "  children: []\n", yname);
	if (!editor_ai_create_file (app, path, text, &abs, error))
		return NULL;
	return lrg_editor_bt_editor_open (app, abs, error);
}

LrgBtAsset *
lrg_editor_bt_editor_get_asset (LrgEditorBtEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), NULL);
	return self->asset;
}

const gchar *
lrg_editor_bt_editor_get_load_error (LrgEditorBtEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), NULL);
	return self->load_error;
}

LrgNodeGraph *
lrg_editor_bt_editor_get_canvas (LrgEditorBtEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), NULL);
	return self->canvas;
}

const gchar *
lrg_editor_bt_editor_get_shown_subtree (LrgEditorBtEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), NULL);
	return self->shown;
}

gboolean
lrg_editor_bt_editor_show_subtree (LrgEditorBtEditor *self,
                                   const gchar       *name)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), FALSE);

	if (name != NULL)
	{
		g_autofree gchar *sp = pj ("/subtrees", name);

		if (!has_key (self, sp, "root"))
			return FALSE;
	}
	g_free (self->shown);
	self->shown = g_strdup (name);
	if (self->sel != NULL)
	{
		g_autofree gchar *root = shown_root (self);
		NodeInfo         *n = info_of (self, self->sel);

		if (n == NULL || !path_under (n->path, root))
			g_clear_pointer (&self->sel, g_free);
	}
	rebuild_canvas (self);
	self->rows_stale = TRUE;
	self->grid_stale = TRUE;
	self->framed = 0;
	return TRUE;
}

gchar **
lrg_editor_bt_editor_dup_node_ids (LrgEditorBtEditor *self)
{
	GStrvBuilder *b;
	gchar       **out;
	guint         i;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), NULL);

	b = g_strv_builder_new ();
	for (i = 0; i < self->ids->len; i++)
		g_strv_builder_add (b, g_ptr_array_index (self->ids, i));
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);
	return out;
}

const gchar *
lrg_editor_bt_editor_get_node_path (LrgEditorBtEditor *self,
                                    const gchar       *id)
{
	NodeInfo *n;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), NULL);
	n = info_of (self, id);
	return n != NULL ? n->path : NULL;
}

const gchar *
lrg_editor_bt_editor_get_node_parent (LrgEditorBtEditor *self,
                                      const gchar       *id)
{
	NodeInfo *n;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), NULL);
	n = info_of (self, id);
	return n != NULL ? n->parent : NULL;
}

/* Where a new key goes in a node mapping: before params/child/children. */
static gint
prop_position (LrgEditorBtEditor *self,
               const gchar       *path)
{
	g_auto(GStrv) keys = lrg_editor_document_get_keys (doc_of (self), path);
	guint         i;

	for (i = 0; keys != NULL && keys[i] != NULL; i++)
		if (g_strcmp0 (keys[i], "params") == 0 || g_strcmp0 (keys[i], "child") == 0 ||
		    g_strcmp0 (keys[i], "children") == 0)
			return (gint) i;
	return -1;
}

gchar *
lrg_editor_bt_editor_add_node (LrgEditorBtEditor  *self,
                               const gchar        *parent,
                               gint                index,
                               const gchar        *type,
                               const gchar        *id,
                               GError            **error)
{
	g_autofree gchar *pid = g_strdup (parent);
	g_autofree gchar *nid = NULL;
	g_autofree gchar *yt = NULL;
	g_autofree gchar *yi = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *root = NULL;
	LrgBtNodeKind     kind;
	NodeInfo         *p;
	gboolean          known;
	gboolean          ok;
	gboolean          opened;
	gfloat            px = 0.0f, py = 0.0f;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), NULL);
	g_return_val_if_fail (type != NULL && *type != '\0', NULL);

	if (id != NULL && (!valid_id (id) || info_of (self, id) != NULL))
	{
		fail_arg (error, "'%s' is not a free node id", id);
		return NULL;
	}
	if (pid != NULL)
	{
		p = info_of (self, pid);
		if (p == NULL)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no node '%s'", pid);
			return NULL;
		}
		kind = kind_of (self, pid);
		if (kind != LRG_BT_NODE_KIND_COMPOSITE && kind != LRG_BT_NODE_KIND_DECORATOR)
		{
			fail_arg (error, "'%s' is a %s: it takes no children", pid, kind_name (kind));
			return NULL;
		}
		if (kind == LRG_BT_NODE_KIND_DECORATOR && has_key (self, p->path, "child"))
		{
			fail_arg (error, "decorator '%s' already has its child", pid);
			return NULL;
		}
	}
	else
	{
		root = shown_root (self);
		if (lrg_editor_document_has_path (doc_of (self), root))
		{
			fail_arg (error, "the tree has a root: give a parent");
			return NULL;
		}
	}

	nid = id != NULL ? g_strdup (id) : free_id (self, type);
	kind = kind_of_type (self, type, NULL, &known);
	yt = editor_ai_yaml_str (type);
	yi = editor_ai_yaml_str (nid);
	text = kind == LRG_BT_NODE_KIND_COMPOSITE ? g_strdup_printf ("{type: %s, id: %s, children: []}", yt, yi)
	                                          : g_strdup_printf ("{type: %s, id: %s}", yt, yi);
	opened = edit_begin (self, "Add Node");
	materialize_ids (self);
	if (pid != NULL)
	{
		node_pos (self, pid, &px, &py);
		p = info_of (self, pid);
		if (kind_of (self, pid) == LRG_BT_NODE_KIND_DECORATOR)
			ok = lrg_editor_document_add_key (doc_of (self), p->path, "child", text, -1, error);
		else
		{
			g_autofree gchar *list = pj (p->path, "children");
			gint              count;

			if (lrg_editor_document_get_kind (doc_of (self), list) != LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
			{
				if (lrg_editor_document_has_path (doc_of (self), list))
					lrg_editor_document_remove (doc_of (self), list, NULL);
				lrg_editor_document_add_key (doc_of (self), p->path, "children", "[]", -1, NULL);
			}
			count = n_items (self, list);
			ok = lrg_editor_document_insert_item (doc_of (self), list, (index < 0 || index > count) ? -1 : index, text,
			                                      error);
		}
	}
	else if (self->shown != NULL)
	{
		g_autofree gchar *sp = pj ("/subtrees", self->shown);

		ok = lrg_editor_document_add_key (doc_of (self), sp, "root", text, -1, error);
	}
	else
		ok = lrg_editor_document_add_key (doc_of (self), "", "root", text, -1, error);
	if (ok)
	{
		/* next to its parent: one column right, below its last sibling */
		NodeInfo *n = info_of (self, nid);
		gfloat    y = py;
		guint     i;

		for (i = 0; n != NULL && n->parent != NULL && i < self->ids->len; i++)
		{
			const gchar *sid = g_ptr_array_index (self->ids, i);
			NodeInfo    *s = info_of (self, sid);
			gfloat       sx, sy;

			if (s != NULL && g_strcmp0 (s->parent, n->parent) == 0 && g_strcmp0 (sid, nid) != 0)
			{
				node_pos (self, sid, &sx, &sy);
				y = MAX (y, sy + ROW_DY);
			}
		}
		write_pos (self, nid, pid != NULL ? px + COL_DX : 0.0f, y);
	}
	edit_end (self, opened);
	self->canvas_stale = TRUE;
	return ok ? g_steal_pointer (&nid) : NULL;
}

gboolean
lrg_editor_bt_editor_remove_node (LrgEditorBtEditor  *self,
                                  const gchar        *id,
                                  GError            **error)
{
	g_autofree gchar     *nid = g_strdup (id);
	g_autoptr(GPtrArray)  gone = NULL;
	NodeInfo             *n;
	gboolean              opened;
	gboolean              ok;
	guint                 i;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), FALSE);

	n = info_of (self, nid);
	if (n == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no node '%s'", id);
		return FALSE;
	}
	if (n->parent == NULL && g_str_has_prefix (n->path, "/subtrees/"))
		return fail_arg (error, "an inline subtree needs its root: remove the subtree instead");
	opened = edit_begin (self, "Delete Node");
	materialize_ids (self);
	n = info_of (self, nid);
	/* the saved positions of the node and everything under it go too */
	gone = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < self->ids->len; i++)
	{
		NodeInfo *c = info_of (self, g_ptr_array_index (self->ids, i));

		if (c != NULL && path_under (c->path, n->path))
			g_ptr_array_add (gone, g_strdup (g_ptr_array_index (self->ids, i)));
	}
	{
		g_autofree gchar *path = g_strdup (n->path);

		ok = lrg_editor_document_remove (doc_of (self), path, error);
	}
	for (i = 0; ok && i < gone->len; i++)
		remove_pos (self, g_ptr_array_index (gone, i));
	edit_end (self, opened);
	if (g_strcmp0 (self->sel, nid) == 0)
		g_clear_pointer (&self->sel, g_free);
	self->canvas_stale = TRUE;
	return ok;
}

gboolean
lrg_editor_bt_editor_move_node (LrgEditorBtEditor  *self,
                                const gchar        *id,
                                const gchar        *parent,
                                gint                index,
                                GError            **error)
{
	g_autofree gchar *nid = g_strdup (id);
	g_autofree gchar *pid = g_strdup (parent);
	g_autofree gchar *yaml = NULL;
	NodeInfo         *n, *p;
	LrgBtNodeKind     pk;
	gboolean          opened;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), FALSE);

	n = info_of (self, nid);
	p = info_of (self, pid);
	if (n == NULL || p == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no node '%s'", n == NULL ? id : parent);
		return FALSE;
	}
	if (n->parent == NULL)
		return fail_arg (error, "'%s' is a root: it cannot move", id);
	if (is_ancestor (self, nid, pid))
		return fail_arg (error, "'%s' cannot move under itself", id);
	pk = kind_of (self, pid);
	if (pk != LRG_BT_NODE_KIND_COMPOSITE && pk != LRG_BT_NODE_KIND_DECORATOR)
		return fail_arg (error, "'%s' is a %s: it takes no children", parent, kind_name (pk));
	if (pk == LRG_BT_NODE_KIND_DECORATOR && has_key (self, p->path, "child") && g_strcmp0 (n->parent, pid) != 0)
		return fail_arg (error, "decorator '%s' already has its child", parent);

	opened = edit_begin (self, "Move Node");
	materialize_ids (self);
	n = info_of (self, nid);
	p = info_of (self, pid);
	if (n == NULL || p == NULL)
	{
		edit_end (self, opened);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no node '%s'", id);
		return FALSE;
	}
	if (g_strcmp0 (n->parent, pid) == 0 && pk == LRG_BT_NODE_KIND_COMPOSITE)
	{
		/* a reorder among siblings */
		g_autofree gchar *list = pj (p->path, "children");
		g_autofree gchar *base = g_path_get_basename (n->path);
		gint              from = (gint) g_ascii_strtoll (base, NULL, 10);
		gint              count = n_items (self, list);
		gint              to = (index < 0 || index >= count) ? count - 1 : index;

		ok = from == to || lrg_editor_document_move_item (doc_of (self), list, (guint) from, (guint) to, error);
	}
	else if (g_strcmp0 (n->parent, pid) == 0)
		ok = TRUE;
	else
	{
		g_autofree gchar *path = g_strdup (n->path);

		yaml = lrg_editor_document_get_yaml (doc_of (self), path);
		ok = yaml != NULL && lrg_editor_document_remove (doc_of (self), path, error);
		p = info_of (self, pid);
		if (ok && p != NULL)
		{
			if (pk == LRG_BT_NODE_KIND_DECORATOR)
				ok = lrg_editor_document_add_key (doc_of (self), p->path, "child", yaml, -1, error);
			else
			{
				g_autofree gchar *list = pj (p->path, "children");
				gint              count;

				if (lrg_editor_document_get_kind (doc_of (self), list) != LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
					lrg_editor_document_add_key (doc_of (self), p->path, "children", "[]", -1, NULL);
				count = n_items (self, list);
				ok = lrg_editor_document_insert_item (doc_of (self), list, (index < 0 || index > count) ? -1 : index,
				                                      yaml, error);
			}
		}
	}
	edit_end (self, opened);
	self->canvas_stale = TRUE;
	return ok;
}

gboolean
lrg_editor_bt_editor_set_node_id (LrgEditorBtEditor  *self,
                                  const gchar        *id,
                                  const gchar        *new_id,
                                  GError            **error)
{
	g_autofree gchar *nid = g_strdup (id);
	g_autofree gchar *nnew = g_strdup (new_id);
	NodeInfo         *n;
	gboolean          opened;
	gboolean          ok;
	gfloat            x, y;
	gboolean          had_pos;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), FALSE);

	n = info_of (self, nid);
	if (n == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no node '%s'", id);
		return FALSE;
	}
	if (g_strcmp0 (nid, nnew) == 0)
		return TRUE;
	if (!valid_id (nnew) || info_of (self, nnew) != NULL)
		return fail_arg (error, "'%s' is not a free node id (unique, non-empty, no '/')", new_id != NULL ? new_id : "");
	had_pos = read_saved_pos (self, nid, &x, &y);
	opened = edit_begin (self, "Rename Node");
	{
		g_autofree gchar *path = g_strdup (n->path);
		g_autofree gchar *idp = pj (path, "id");

		if (lrg_editor_document_has_path (doc_of (self), idp))
			ok = lrg_editor_document_set_string (doc_of (self), idp, nnew, 0, error);
		else
		{
			g_autofree gchar *y2 = editor_ai_yaml_str (nnew);

			ok = lrg_editor_document_add_key (doc_of (self), path, "id", y2, 1, error);
		}
	}
	if (ok && had_pos)
	{
		remove_pos (self, nid);
		write_pos (self, nnew, x, y);
	}
	edit_end (self, opened);
	if (ok && g_strcmp0 (self->sel, nid) == 0)
	{
		g_free (self->sel);
		self->sel = g_strdup (nnew);
	}
	self->canvas_stale = TRUE;
	self->grid_stale = TRUE;
	return ok;
}

gboolean
lrg_editor_bt_editor_set_node_type (LrgEditorBtEditor  *self,
                                    const gchar        *id,
                                    const gchar        *type,
                                    GError            **error)
{
	g_autofree gchar *nid = g_strdup (id);
	g_autofree gchar *path = NULL;
	g_autofree gchar *children = NULL;
	g_autofree gchar *child = NULL;
	g_autofree gchar *tp = NULL;
	NodeInfo         *n;
	LrgBtNodeKind     to;
	gint              nch;
	gboolean          has_child;
	gboolean          opened;
	gboolean          ok = TRUE;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), FALSE);

	n = info_of (self, nid);
	if (n == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no node '%s'", id);
		return FALSE;
	}
	if (type == NULL || *type == '\0')
		return fail_arg (error, "a node needs a type");
	if (g_strcmp0 (n->type, type) == 0)
		return TRUE;
	path = g_strdup (n->path);
	children = pj (path, "children");
	child = pj (path, "child");
	tp = pj (path, "type");
	to = kind_of_type (self, type, NULL, NULL);
	nch = n_items (self, children);
	has_child = lrg_editor_document_has_path (doc_of (self), child);
	if ((to == LRG_BT_NODE_KIND_LEAF || to == LRG_BT_NODE_KIND_SUBTREE) && (nch > 0 || has_child))
		return fail_arg (error, "'%s' has children: a %s cannot hold them", id, type);
	if (to == LRG_BT_NODE_KIND_DECORATOR && nch > 1)
		return fail_arg (error, "'%s' has %d children: a decorator holds one", id, nch);

	opened = edit_begin (self, "Change Node Type");
	if (to == LRG_BT_NODE_KIND_COMPOSITE)
	{
		g_autofree gchar *yaml = has_child ? lrg_editor_document_get_yaml (doc_of (self), child) : NULL;

		if (has_child)
			lrg_editor_document_remove (doc_of (self), child, NULL);
		if (!lrg_editor_document_has_path (doc_of (self), children))
			lrg_editor_document_add_key (doc_of (self), path, "children", "[]", -1, NULL);
		if (yaml != NULL)
			lrg_editor_document_insert_item (doc_of (self), children, 0, yaml, NULL);
	}
	else if (to == LRG_BT_NODE_KIND_DECORATOR)
	{
		g_autofree gchar *first = pji (children, 0);
		g_autofree gchar *yaml = nch == 1 ? lrg_editor_document_get_yaml (doc_of (self), first) : NULL;

		if (lrg_editor_document_has_path (doc_of (self), children))
			lrg_editor_document_remove (doc_of (self), children, NULL);
		if (yaml != NULL)
			lrg_editor_document_add_key (doc_of (self), path, "child", yaml, -1, NULL);
	}
	else if (lrg_editor_document_has_path (doc_of (self), children))
		lrg_editor_document_remove (doc_of (self), children, NULL);
	ok = lrg_editor_document_set_string (doc_of (self), tp, type, 0, error);
	edit_end (self, opened);
	self->canvas_stale = TRUE;
	self->grid_stale = TRUE;
	return ok;
}

gboolean
lrg_editor_bt_editor_set_node_prop (LrgEditorBtEditor  *self,
                                    const gchar        *id,
                                    const gchar        *key,
                                    const gchar        *yaml,
                                    GError            **error)
{
	NodeInfo         *n;
	g_autofree gchar *path = NULL;
	g_autofree gchar *kp = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	n = info_of (self, id);
	if (n == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no node '%s'", id);
		return FALSE;
	}
	if (g_strcmp0 (key, "type") == 0)
		return fail_arg (error, "use set_node_type to change the type");
	if (g_strcmp0 (key, "id") == 0)
		return fail_arg (error, "use set_node_id to rename a node");
	if (g_strcmp0 (key, "children") == 0 || g_strcmp0 (key, "child") == 0 || g_strcmp0 (key, "params") == 0)
		return fail_arg (error, "'%s' is not a property", key);
	path = g_strdup (n->path);
	kp = pj (path, key);
	self->canvas_stale = TRUE;
	if (yaml == NULL)
		return !lrg_editor_document_has_path (doc_of (self), kp) || lrg_editor_document_remove (doc_of (self), kp, error);
	if (lrg_editor_document_has_path (doc_of (self), kp))
		return lrg_editor_document_replace (doc_of (self), kp, yaml, error);
	return lrg_editor_document_add_key (doc_of (self), path, key, yaml, prop_position (self, path), error);
}

gboolean
lrg_editor_bt_editor_set_node_param (LrgEditorBtEditor  *self,
                                     const gchar        *id,
                                     const gchar        *key,
                                     const gchar        *yaml,
                                     GError            **error)
{
	NodeInfo         *n;
	g_autofree gchar *path = NULL;
	g_autofree gchar *pp = NULL;
	g_autofree gchar *kp = NULL;
	gboolean          opened;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	n = info_of (self, id);
	if (n == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no node '%s'", id);
		return FALSE;
	}
	if (*key == '\0')
		return fail_arg (error, "a parameter needs a name");
	path = g_strdup (n->path);
	pp = pj (path, "params");
	kp = pj (pp, key);
	self->canvas_stale = TRUE;
	self->grid_stale = TRUE;
	if (yaml == NULL)
	{
		if (!lrg_editor_document_has_path (doc_of (self), kp))
			return TRUE;
		opened = edit_begin (self, "Remove Parameter");
		ok = lrg_editor_document_remove (doc_of (self), kp, error);
		if (ok && lrg_editor_document_get_length (doc_of (self), pp) == 0)
			lrg_editor_document_remove (doc_of (self), pp, NULL);
		edit_end (self, opened);
		return ok;
	}
	opened = edit_begin (self, "Set Parameter");
	if (lrg_editor_document_get_kind (doc_of (self), pp) != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
	{
		gint pos;

		if (lrg_editor_document_has_path (doc_of (self), pp))
			lrg_editor_document_remove (doc_of (self), pp, NULL);
		pos = prop_position (self, path);
		lrg_editor_document_add_key (doc_of (self), path, "params", "{}", pos, NULL);
	}
	if (lrg_editor_document_has_path (doc_of (self), kp))
		ok = lrg_editor_document_replace (doc_of (self), kp, yaml, error);
	else
		ok = lrg_editor_document_add_key (doc_of (self), pp, key, yaml, -1, error);
	edit_end (self, opened);
	return ok;
}

gboolean
lrg_editor_bt_editor_set_node_position (LrgEditorBtEditor *self,
                                        const gchar       *id,
                                        gfloat             x,
                                        gfloat             y)
{
	gboolean opened;
	gboolean ok;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), FALSE);

	if (info_of (self, id) == NULL)
		return FALSE;
	opened = edit_begin (self, "Move Node");
	ok = write_pos (self, id, x, y);
	edit_end (self, opened);
	self->canvas_stale = TRUE;
	return ok;
}

gboolean
lrg_editor_bt_editor_get_node_position (LrgEditorBtEditor *self,
                                        const gchar       *id,
                                        gfloat            *x,
                                        gfloat            *y)
{
	gfloat   px = 0.0f, py = 0.0f;
	gboolean saved;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), FALSE);

	saved = read_saved_pos (self, id, &px, &py);
	if (!saved)
	{
		if (g_hash_table_size (self->auto_pos) == 0)
			compute_auto_layout (self);
		node_pos (self, id, &px, &py);
	}
	if (x != NULL)
		*x = px;
	if (y != NULL)
		*y = py;
	return saved;
}

guint
lrg_editor_bt_editor_auto_layout (LrgEditorBtEditor *self)
{
	g_autofree gchar     *root = shown_root (self);
	g_autoptr(GPtrArray)  todo = NULL;
	gboolean              opened;
	guint                 i;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), 0);

	opened = edit_begin (self, "Auto Layout");
	materialize_ids (self);
	/* lay out from scratch: the automatic positions of every node */
	compute_auto_layout (self);
	todo = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < self->ids->len; i++)
	{
		NodeInfo *n = info_of (self, g_ptr_array_index (self->ids, i));

		if (n != NULL && path_under (n->path, root))
			g_ptr_array_add (todo, g_strdup (g_ptr_array_index (self->ids, i)));
	}
	for (i = 0; i < todo->len; i++)
	{
		gfloat *p = g_hash_table_lookup (self->auto_pos, g_ptr_array_index (todo, i));

		if (p != NULL)
		{
			gfloat x = p[0], y = p[1];

			write_pos (self, g_ptr_array_index (todo, i), x, y);
		}
	}
	edit_end (self, opened);
	self->canvas_stale = TRUE;
	self->framed = 0;
	return todo->len;
}

gboolean
lrg_editor_bt_editor_select_node (LrgEditorBtEditor *self,
                                  const gchar       *id)
{
	NodeInfo *n;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), FALSE);

	if (id == NULL)
	{
		g_clear_pointer (&self->sel, g_free);
		self->syncing = TRUE;
		lrg_node_graph_unselect_all (self->canvas);
		self->syncing = FALSE;
		self->grid_stale = TRUE;
		return TRUE;
	}
	n = info_of (self, id);
	if (n == NULL)
		return FALSE;
	/* show the tree that holds it */
	{
		g_autofree gchar *root = shown_root (self);

		if (!path_under (n->path, root))
		{
			g_auto(GStrv) parts = lrg_editor_document_path_split (n->path, NULL);

			g_free (self->shown);
			self->shown = (parts != NULL && g_strcmp0 (parts[0], "subtrees") == 0 && parts[1] != NULL)
			              ? g_strdup (parts[1]) : NULL;
			self->canvas_stale = TRUE;
			self->rows_stale = TRUE;
		}
	}
	g_free (self->sel);
	self->sel = g_strdup (id);
	if (self->canvas_stale)
		rebuild_canvas (self);
	else
	{
		LrgGraphNode *card = lrg_node_graph_get_node (self->canvas, id);

		self->syncing = TRUE;
		if (card != NULL)
			lrg_node_graph_select_node (self->canvas, card, FALSE);
		self->syncing = FALSE;
	}
	self->grid_stale = TRUE;
	return TRUE;
}

const gchar *
lrg_editor_bt_editor_get_selected_node (LrgEditorBtEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), NULL);
	return self->sel;
}

gboolean
lrg_editor_bt_editor_add_key (LrgEditorBtEditor  *self,
                              const gchar        *name,
                              const gchar        *type,
                              const gchar        *default_yaml,
                              GError            **error)
{
	static const gchar * const types[] = { "int", "float", "bool", "string", "object", NULL };
	g_autofree gchar *yn = NULL;
	g_autofree gchar *text = NULL;
	gint              n, i;
	gboolean          opened;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), FALSE);

	if (name == NULL || *name == '\0')
		return fail_arg (error, "a key needs a name");
	if (type == NULL || !g_strv_contains (types, type))
		return fail_arg (error, "'%s' is not a key type (int, float, bool, string, object)", type != NULL ? type : "");
	if (g_strcmp0 (type, "object") == 0 && default_yaml != NULL)
		return fail_arg (error, "object keys have no default");
	n = n_items (self, "/blackboard");
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *kp = pji ("/blackboard", (guint) i);

		if (g_strcmp0 (scalar_at (self, kp, "name"), name) == 0)
			return fail_arg (error, "key '%s' is declared already", name);
	}
	yn = editor_ai_yaml_str (name);
	text = default_yaml != NULL ? g_strdup_printf ("{name: %s, type: %s, default: %s}", yn, type, default_yaml)
	                            : g_strdup_printf ("{name: %s, type: %s}", yn, type);
	opened = edit_begin (self, "Add Blackboard Key");
	if (lrg_editor_document_get_kind (doc_of (self), "/blackboard") != LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
	{
		g_auto(GStrv) keys = lrg_editor_document_get_keys (doc_of (self), "");
		gint          pos = -1;

		if (lrg_editor_document_has_path (doc_of (self), "/blackboard"))
			lrg_editor_document_remove (doc_of (self), "/blackboard", NULL);
		/* after name (and version), before subtrees/root */
		for (i = 0; keys != NULL && keys[i] != NULL; i++)
			if (g_strcmp0 (keys[i], "subtrees") == 0 || g_strcmp0 (keys[i], "root") == 0)
			{
				pos = i;
				break;
			}
		lrg_editor_document_add_key (doc_of (self), "", "blackboard", "[]", pos, NULL);
	}
	ok = lrg_editor_document_insert_item (doc_of (self), "/blackboard", -1, text, error);
	edit_end (self, opened);
	self->rows_stale = TRUE;
	self->grid_stale = TRUE;
	return ok;
}

gboolean
lrg_editor_bt_editor_remove_key (LrgEditorBtEditor *self,
                                 const gchar       *name)
{
	gint n, i;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), FALSE);

	n = n_items (self, "/blackboard");
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *kp = pji ("/blackboard", (guint) i);

		if (g_strcmp0 (scalar_at (self, kp, "name"), name) == 0)
		{
			gboolean ok = lrg_editor_document_remove (doc_of (self), kp, NULL);

			self->rows_stale = TRUE;
			self->grid_stale = TRUE;
			return ok;
		}
	}
	return FALSE;
}

gboolean
lrg_editor_bt_editor_set_subtree (LrgEditorBtEditor  *self,
                                  const gchar        *name,
                                  const gchar        *file,
                                  GError            **error)
{
	g_autofree gchar *sp = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *rid = NULL;
	gboolean          opened;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), FALSE);

	if (name == NULL || *name == '\0' || strchr (name, '/') != NULL)
		return fail_arg (error, "'%s' is not a subtree name", name != NULL ? name : "");
	sp = pj ("/subtrees", name);
	if (file != NULL)
	{
		g_autofree gchar *yf = editor_ai_yaml_str (file);

		text = g_strdup_printf ("{path: %s}", yf);
	}
	else
	{
		g_autofree gchar *base = g_strconcat (name, "_root", NULL);
		g_autofree gchar *yr = NULL;

		rid = free_id (self, base);
		yr = editor_ai_yaml_str (rid);
		text = g_strdup_printf ("{root: {type: selector, id: %s, children: []}}", yr);
	}
	opened = edit_begin (self, "Set Subtree");
	if (lrg_editor_document_get_kind (doc_of (self), "/subtrees") != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
	{
		g_auto(GStrv) keys = lrg_editor_document_get_keys (doc_of (self), "");
		gint          pos = -1, i;

		if (lrg_editor_document_has_path (doc_of (self), "/subtrees"))
			lrg_editor_document_remove (doc_of (self), "/subtrees", NULL);
		for (i = 0; keys != NULL && keys[i] != NULL; i++)
			if (g_strcmp0 (keys[i], "root") == 0)
			{
				pos = i;
				break;
			}
		lrg_editor_document_add_key (doc_of (self), "", "subtrees", "{}", pos, NULL);
	}
	if (lrg_editor_document_has_path (doc_of (self), sp))
		ok = lrg_editor_document_replace (doc_of (self), sp, text, error);
	else
		ok = lrg_editor_document_add_key (doc_of (self), "/subtrees", name, text, -1, error);
	edit_end (self, opened);
	self->rows_stale = TRUE;
	self->canvas_stale = TRUE;
	return ok;
}

gboolean
lrg_editor_bt_editor_remove_subtree (LrgEditorBtEditor *self,
                                     const gchar       *name)
{
	g_autofree gchar *sp = NULL;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), FALSE);

	if (name == NULL)
		return FALSE;
	sp = pj ("/subtrees", name);
	if (!lrg_editor_document_has_path (doc_of (self), sp))
		return FALSE;
	if (g_strcmp0 (self->shown, name) == 0)
		g_clear_pointer (&self->shown, g_free);
	ok = lrg_editor_document_remove (doc_of (self), sp, NULL);
	self->rows_stale = TRUE;
	self->canvas_stale = TRUE;
	return ok;
}

LrgEditorBtEditor *
lrg_editor_bt_editor_open_subtree (LrgEditorBtEditor  *self,
                                   const gchar        *id,
                                   GError            **error)
{
	NodeInfo         *n;
	const gchar      *ref;
	g_autofree gchar *sp = NULL;
	const gchar      *file;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), NULL);

	n = info_of (self, id);
	if (n == NULL || kind_of (self, id) != LRG_BT_NODE_KIND_SUBTREE)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no subtree node '%s'", id != NULL ? id : "");
		return NULL;
	}
	ref = scalar_at (self, n->path, "ref");
	sp = ref != NULL ? pj ("/subtrees", ref) : NULL;
	if (sp == NULL || !lrg_editor_document_has_path (doc_of (self), sp))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "subtree '%s' is not declared", ref != NULL ? ref : "");
		return NULL;
	}
	file = scalar_at (self, sp, "path");
	if (file != NULL)
	{
		const gchar      *me = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
		g_autofree gchar *dir = me != NULL ? g_path_get_dirname (me) : g_get_current_dir ();
		g_autofree gchar *abs = g_canonicalize_filename (file, dir);

		if (app_of (self) == NULL)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "no application to open %s in", abs);
			return NULL;
		}
		return lrg_editor_bt_editor_open (app_of (self), abs, error);
	}
	lrg_editor_bt_editor_show_subtree (self, ref);
	return self;
}

LrgBtRunner *
lrg_editor_bt_editor_get_live_runner (LrgEditorBtEditor *self)
{
	LrgEditorApp   *app;
	const gchar    *path;
	LrgAiComponent *c;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), NULL);

	app = app_of (self);
	path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	if (app == NULL || path == NULL || !lrg_editor_app_is_playing (app))
		return NULL;
	c = lrg_editor_ai_debug_panel_find_for_file (app, path, FALSE);
	return c != NULL ? lrg_ai_component_get_runner (c) : NULL;
}

LrgBTStatus
lrg_editor_bt_editor_get_live_status (LrgEditorBtEditor *self,
                                      const gchar       *id,
                                      gboolean          *visited)
{
	LrgBtRunner *runner;
	const gchar *rid;

	g_return_val_if_fail (LRG_IS_EDITOR_BT_EDITOR (self), LRG_BT_STATUS_INVALID);

	if (visited != NULL)
		*visited = FALSE;
	runner = lrg_editor_bt_editor_get_live_runner (self);
	rid = (runner != NULL && id != NULL) ? runner_id (runner, id) : NULL;
	if (rid == NULL)
		return LRG_BT_STATUS_INVALID;
	if (visited != NULL)
		*visited = lrg_bt_runner_get_node_visited (runner, rid);
	return lrg_bt_runner_get_node_status (runner, rid);
}

/* ==========================================================================
 * IPC: bt.*
 * ========================================================================== */

/* The editor of params.path (opened when needed). */
static LrgEditorBtEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	const gchar       *path = editor_ai_require_string (params, "path", error);
	LrgEditorBtEditor *self;
	g_autoptr(GError)  err = NULL;

	if (path == NULL)
		return NULL;
	self = lrg_editor_bt_editor_open (app, path, &err);
	if (self == NULL)
		editor_ai_fail (error, err);
	return self;
}

/* A node's id from params.id, failing when unknown. */
static const gchar *
ipc_node (LrgEditorBtEditor  *self,
          JsonObject         *params,
          const gchar        *member,
          GError            **error)
{
	const gchar *id = editor_ai_require_string (params, member, error);

	if (id != NULL && info_of (self, id) == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No node '%s'", id);
		return NULL;
	}
	return id;
}

static JsonNode *
str_result (const gchar *member,
            const gchar *value)
{
	JsonObject *o = json_object_new ();

	if (value != NULL)
		json_object_set_string_member (o, member, value);
	else
		json_object_set_null_member (o, member);
	return editor_ai_object_node (o);
}

static JsonNode *
bool_result (const gchar *member,
             gboolean     value)
{
	JsonObject *o = json_object_new ();

	json_object_set_boolean_member (o, member, value);
	return editor_ai_object_node (o);
}

static JsonNode *
m_bt_open (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	return self != NULL ? str_result ("path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self))) : NULL;
}

static JsonNode *
m_bt_create (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	const gchar       *path = editor_ai_require_string (params, "path", error);
	LrgEditorBtEditor *self;
	g_autoptr(GError)  err = NULL;

	if (path == NULL)
		return NULL;
	self = lrg_editor_bt_editor_create (LRG_EDITOR_APP (user_data), path, editor_ai_param_string (params, "name"), &err);
	if (self == NULL)
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return str_result ("path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
}

static JsonObject *
node_json (LrgEditorBtEditor *self,
           const gchar       *id)
{
	NodeInfo         *n = info_of (self, id);
	JsonObject       *o = json_object_new ();
	JsonObject       *props = json_object_new ();
	g_auto(GStrv)     keys = lrg_editor_document_get_keys (doc_of (self), n->path);
	g_autofree gchar *pp = pj (n->path, "params");
	g_autofree gchar *base = g_path_get_basename (n->path);
	const gchar      *name = scalar_at (self, n->path, "name");
	gfloat            x, y;
	guint             i;

	json_object_set_string_member (o, "id", id);
	json_object_set_boolean_member (o, "explicit", n->explicit_id);
	if (n->type != NULL)
		json_object_set_string_member (o, "type", n->type);
	else
		json_object_set_null_member (o, "type");
	json_object_set_string_member (o, "kind", kind_name (kind_of (self, id)));
	if (name != NULL)
		json_object_set_string_member (o, "name", name);
	else
		json_object_set_null_member (o, "name");
	if (n->parent != NULL)
		json_object_set_string_member (o, "parent", n->parent);
	else
		json_object_set_null_member (o, "parent");
	json_object_set_int_member (o, "index", g_ascii_isdigit (*base) ? g_ascii_strtoll (base, NULL, 10) : 0);
	json_object_set_string_member (o, "yaml_path", n->path);
	for (i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		g_autofree gchar *kp = NULL;

		if (g_strv_contains (structural_keys, keys[i]))
			continue;
		kp = pj (n->path, keys[i]);
		json_object_set_member (props, keys[i], editor_ai_doc_to_json (doc_of (self), kp));
	}
	json_object_set_object_member (o, "props", props);
	if (lrg_editor_document_get_kind (doc_of (self), pp) == LRG_EDITOR_DOCUMENT_NODE_MAPPING)
		json_object_set_member (o, "params", editor_ai_doc_to_json (doc_of (self), pp));
	else
		json_object_set_object_member (o, "params", json_object_new ());
	lrg_editor_bt_editor_get_node_position (self, id, &x, &y);
	json_object_set_double_member (o, "x", x);
	json_object_set_double_member (o, "y", y);
	return o;
}

static JsonNode *
m_bt_get (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject        *o;
	JsonArray         *nodes, *subs;
	g_auto(GStrv)      skeys = NULL;
	const gchar       *name;
	const gchar       *root = NULL;
	guint              i;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	nodes = json_array_new ();
	for (i = 0; i < self->ids->len; i++)
	{
		const gchar *id = g_ptr_array_index (self->ids, i);
		NodeInfo    *n = info_of (self, id);

		if (n != NULL && g_strcmp0 (n->path, "/root") == 0)
			root = id;
		if (n != NULL)
			json_array_add_object_element (nodes, node_json (self, id));
	}
	subs = json_array_new ();
	skeys = lrg_editor_document_get_keys (doc_of (self), "/subtrees");
	for (i = 0; skeys != NULL && skeys[i] != NULL; i++)
	{
		g_autofree gchar *sp = pj ("/subtrees", skeys[i]);
		JsonObject       *so = json_object_new ();
		const gchar      *file = scalar_at (self, sp, "path");

		json_object_set_string_member (so, "name", skeys[i]);
		if (file != NULL)
			json_object_set_string_member (so, "file", file);
		else
			json_object_set_null_member (so, "file");
		json_array_add_object_element (subs, so);
	}
	name = scalar_at (self, "", "name");
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	if (name != NULL)
		json_object_set_string_member (o, "name", name);
	else
		json_object_set_null_member (o, "name");
	json_object_set_string_member (o, "shown", self->shown != NULL ? self->shown : "");
	if (root != NULL)
		json_object_set_string_member (o, "root", root);
	else
		json_object_set_null_member (o, "root");
	json_object_set_array_member (o, "nodes", nodes);
	json_object_set_member (o, "blackboard", lrg_editor_document_has_path (doc_of (self), "/blackboard")
	                                         ? editor_ai_doc_to_json (doc_of (self), "/blackboard")
	                                         : json_node_init_array (json_node_alloc (), json_array_new ()));
	json_object_set_array_member (o, "subtrees", subs);
	if (self->sel != NULL)
		json_object_set_string_member (o, "selected", self->sel);
	else
		json_object_set_null_member (o, "selected");
	json_object_set_boolean_member (o, "dirty", lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (self)));
	if (self->load_error != NULL)
		json_object_set_string_member (o, "load_error", self->load_error);
	return editor_ai_object_node (o);
}

static JsonNode *
m_bt_palette (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	g_auto(GStrv) types = lrg_bt_dup_node_type_names ();
	g_auto(GStrv) actions = lrg_bt_dup_action_names ();
	g_auto(GStrv) conds = lrg_bt_dup_condition_names ();
	JsonObject   *o = json_object_new ();
	JsonArray    *ta = json_array_new ();
	JsonArray    *aa = json_array_new ();
	JsonArray    *ca = json_array_new ();
	guint         i;

	for (i = 0; types != NULL && types[i] != NULL; i++)
	{
		JsonObject   *t = json_object_new ();
		LrgBtNodeKind kind = LRG_BT_NODE_KIND_LEAF;

		lrg_bt_lookup_node_type (types[i], &kind);
		json_object_set_string_member (t, "name", types[i]);
		json_object_set_string_member (t, "kind", kind_name (kind));
		json_array_add_object_element (ta, t);
	}
	for (i = 0; actions != NULL && actions[i] != NULL; i++)
		json_array_add_string_element (aa, actions[i]);
	for (i = 0; conds != NULL && conds[i] != NULL; i++)
		json_array_add_string_element (ca, conds[i]);
	json_object_set_array_member (o, "types", ta);
	json_object_set_array_member (o, "actions", aa);
	json_object_set_array_member (o, "conditions", ca);
	return editor_ai_object_node (o);
}

/* Writes params.props / params.params members (null removes). */
static gboolean
apply_members (LrgEditorBtEditor  *self,
               const gchar        *id,
               JsonObject         *members,
               gboolean            as_params,
               GError            **error)
{
	GList   *names = members != NULL ? json_object_get_members (members) : NULL;
	GList   *l;
	gboolean ok = TRUE;

	for (l = names; ok && l != NULL; l = l->next)
	{
		JsonNode         *v = json_object_get_member (members, l->data);
		g_autofree gchar *y = JSON_NODE_HOLDS_NULL (v) ? NULL : editor_ai_json_to_yaml (v);

		ok = as_params ? lrg_editor_bt_editor_set_node_param (self, id, l->data, y, error)
		               : lrg_editor_bt_editor_set_node_prop (self, id, l->data, y, error);
	}
	g_list_free (names);
	return ok;
}

static JsonObject *
object_param (JsonObject  *params,
              const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	return JSON_NODE_HOLDS_OBJECT (n) ? json_node_get_object (n) : NULL;
}

static JsonNode *
m_bt_add_node (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar       *type = self != NULL ? editor_ai_require_string (params, "type", error) : NULL;
	const gchar       *parent;
	const gchar       *name;
	gdouble            index = -1.0, x = 0.0, y = 0.0;
	g_autofree gchar  *id = NULL;
	g_autoptr(GError)  err = NULL;
	gboolean           opened;
	gboolean           ok;

	if (type == NULL)
		return NULL;
	parent = editor_ai_param_string (params, "parent");
	name = editor_ai_param_string (params, "name");
	editor_ai_param_number (params, "index", &index);
	opened = edit_begin (self, "Add Node");
	id = lrg_editor_bt_editor_add_node (self, parent, (gint) index, type, editor_ai_param_string (params, "id"), &err);
	ok = id != NULL;
	if (ok && name != NULL)
	{
		g_autofree gchar *yn = editor_ai_yaml_str (name);

		ok = lrg_editor_bt_editor_set_node_prop (self, id, "name", yn, &err);
	}
	if (ok)
		ok = apply_members (self, id, object_param (params, "props"), FALSE, &err) &&
		     apply_members (self, id, object_param (params, "params"), TRUE, &err);
	if (ok && editor_ai_param_number (params, "x", &x) && editor_ai_param_number (params, "y", &y))
		lrg_editor_bt_editor_set_node_position (self, id, (gfloat) x, (gfloat) y);
	edit_end (self, opened);
	if (!ok)
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return str_result ("id", id);
}

static JsonNode *
m_bt_remove_node (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar       *id = self != NULL ? ipc_node (self, params, "id", error) : NULL;
	g_autoptr(GError)  err = NULL;

	if (id == NULL)
		return NULL;
	if (!lrg_editor_bt_editor_remove_node (self, id, &err))
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return bool_result ("removed", TRUE);
}

static JsonNode *
m_bt_move_node (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar       *id = self != NULL ? ipc_node (self, params, "id", error) : NULL;
	const gchar       *parent = id != NULL ? ipc_node (self, params, "parent", error) : NULL;
	gdouble            index = -1.0;
	g_autofree gchar  *keep = NULL;
	g_autoptr(GError)  err = NULL;

	if (parent == NULL)
		return NULL;
	keep = g_strdup (id);
	editor_ai_param_number (params, "index", &index);
	if (!lrg_editor_bt_editor_move_node (self, id, parent, (gint) index, &err))
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return str_result ("id", keep);
}

static JsonNode *
m_bt_set_node (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar       *id0 = self != NULL ? ipc_node (self, params, "id", error) : NULL;
	g_autofree gchar  *id = g_strdup (id0);
	const gchar       *new_id = editor_ai_param_string (params, "new_id");
	const gchar       *type = editor_ai_param_string (params, "type");
	g_autoptr(GError)  err = NULL;
	gboolean           opened;
	gboolean           ok = TRUE;

	if (id == NULL)
		return NULL;
	opened = edit_begin (self, "Edit Node");
	if (type != NULL)
		ok = lrg_editor_bt_editor_set_node_type (self, id, type, &err);
	if (ok && params != NULL && json_object_has_member (params, "name"))
	{
		const gchar      *name = editor_ai_param_string (params, "name");
		g_autofree gchar *yn = name != NULL && *name != '\0' ? editor_ai_yaml_str (name) : NULL;

		ok = lrg_editor_bt_editor_set_node_prop (self, id, "name", yn, &err);
	}
	if (ok)
		ok = apply_members (self, id, object_param (params, "props"), FALSE, &err) &&
		     apply_members (self, id, object_param (params, "params"), TRUE, &err);
	if (ok && new_id != NULL)
	{
		ok = lrg_editor_bt_editor_set_node_id (self, id, new_id, &err);
		if (ok)
		{
			g_free (id);
			id = g_strdup (new_id);
		}
	}
	edit_end (self, opened);
	if (!ok)
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return str_result ("id", id);
}

static JsonNode *
m_bt_set_position (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar       *id = self != NULL ? ipc_node (self, params, "id", error) : NULL;
	gdouble            x, y;

	if (id == NULL)
		return NULL;
	if (!editor_ai_param_number (params, "x", &x) || !editor_ai_param_number (params, "y", &y))
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Missing number parameters 'x' and 'y'");
		return NULL;
	}
	lrg_editor_bt_editor_set_node_position (self, id, (gfloat) x, (gfloat) y);
	return str_result ("id", id);
}

static JsonNode *
m_bt_layout (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject        *o;

	if (self == NULL)
		return NULL;
	o = json_object_new ();
	json_object_set_int_member (o, "nodes", lrg_editor_bt_editor_auto_layout (self));
	return editor_ai_object_node (o);
}

static JsonNode *
m_bt_select (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar       *id;

	if (self == NULL)
		return NULL;
	id = editor_ai_param_string (params, "id");
	if (id == NULL && !editor_ai_param_is_null (params, "id"))
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Missing parameter 'id' (string or null)");
		return NULL;
	}
	if (!lrg_editor_bt_editor_select_node (self, id))
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such node");
		return NULL;
	}
	return str_result ("selected", self->sel);
}

static JsonNode *
m_bt_show (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	if (self == NULL)
		return NULL;
	if (!lrg_editor_bt_editor_show_subtree (self, editor_ai_param_string (params, "subtree")))
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No inline subtree of that name");
		return NULL;
	}
	return str_result ("shown", self->shown != NULL ? self->shown : "");
}

static JsonNode *
m_bt_add_key (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar       *name = self != NULL ? editor_ai_require_string (params, "name", error) : NULL;
	const gchar       *type = name != NULL ? editor_ai_require_string (params, "type", error) : NULL;
	g_autofree gchar  *def = NULL;
	g_autoptr(GError)  err = NULL;

	if (type == NULL)
		return NULL;
	if (json_object_has_member (params, "default") && !editor_ai_param_is_null (params, "default"))
		def = editor_ai_json_to_yaml (json_object_get_member (params, "default"));
	if (!lrg_editor_bt_editor_add_key (self, name, type, def, &err))
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return str_result ("name", name);
}

static JsonNode *
m_bt_remove_key (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar       *name = self != NULL ? editor_ai_require_string (params, "name", error) : NULL;

	if (name == NULL)
		return NULL;
	return bool_result ("removed", lrg_editor_bt_editor_remove_key (self, name));
}

static JsonNode *
m_bt_set_subtree (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar       *name = self != NULL ? editor_ai_require_string (params, "name", error) : NULL;
	g_autoptr(GError)  err = NULL;

	if (name == NULL)
		return NULL;
	if (!lrg_editor_bt_editor_set_subtree (self, name, editor_ai_param_string (params, "file"), &err))
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return str_result ("name", name);
}

static JsonNode *
m_bt_remove_subtree (LrgEditorServer  *server,
                     JsonObject       *params,
                     gpointer          user_data,
                     GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar       *name = self != NULL ? editor_ai_require_string (params, "name", error) : NULL;

	if (name == NULL)
		return NULL;
	return bool_result ("removed", lrg_editor_bt_editor_remove_subtree (self, name));
}

static JsonNode *
m_bt_open_subtree (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar       *id = self != NULL ? ipc_node (self, params, "id", error) : NULL;
	LrgEditorBtEditor *other;
	g_autoptr(GError)  err = NULL;

	if (id == NULL)
		return NULL;
	other = lrg_editor_bt_editor_open_subtree (self, id, &err);
	if (other == NULL)
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	if (other != self)
		return str_result ("path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (other)));
	return str_result ("shown", self->shown);
}

static JsonNode *
m_bt_validate (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonNode          *r;
	JsonArray         *a;
	guint              i;

	if (self == NULL)
		return NULL;
	r = editor_ai_issues_result (LRG_EDITOR_DATA_EDITOR (self));
	a = json_object_get_array_member (json_node_get_object (r), "issues");
	for (i = 0; i < json_array_get_length (a); i++)
	{
		JsonObject  *io = json_array_get_object_element (a, i);
		const gchar *id = id_for_path (self, json_object_get_string_member (io, "path"));

		if (id != NULL)
			json_object_set_string_member (io, "node", id);
		else
			json_object_set_null_member (io, "node");
	}
	return r;
}

static JsonNode *
m_bt_save (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorBtEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)  err = NULL;

	if (self == NULL)
		return NULL;
	if (!lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (self), &err))
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return str_result ("path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
}

static JsonNode *
m_bt_live (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorApp      *app = LRG_EDITOR_APP (user_data);
	LrgEditorBtEditor *self = ipc_editor (app, params, error);
	LrgBtRunner       *runner;
	JsonObject        *o;
	JsonArray         *nodes, *active;
	guint              i, n;

	if (self == NULL)
		return NULL;
	runner = lrg_editor_bt_editor_get_live_runner (self);
	o = json_object_new ();
	nodes = json_array_new ();
	active = json_array_new ();
	json_object_set_boolean_member (o, "playing", lrg_editor_app_is_playing (app));
	json_object_set_boolean_member (o, "attached", runner != NULL);
	if (runner != NULL)
	{
		LrgAiComponent   *c = lrg_editor_ai_debug_panel_find_for_file (app,
		                        lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)), FALSE);
		g_autofree gchar *who = c != NULL ? lrg_editor_ai_debug_panel_dup_entity_id (app, c) : NULL;
		g_auto(GStrv)     path = lrg_bt_runner_dup_active_path (runner);

		if (who != NULL)
			json_object_set_string_member (o, "entity", who);
		else
			json_object_set_null_member (o, "entity");
		json_object_set_string_member (o, "status", lrg_bt_status_to_string (lrg_bt_runner_get_status (runner)));
		json_object_set_int_member (o, "tick", (gint64) lrg_bt_runner_get_tick_count (runner));
		if (lrg_bt_runner_get_current_node (runner) != NULL)
			json_object_set_string_member (o, "current", lrg_bt_runner_get_current_node (runner));
		else
			json_object_set_null_member (o, "current");
		for (i = 0; path != NULL && path[i] != NULL; i++)
			json_array_add_string_element (active, path[i]);
		n = lrg_bt_runner_get_node_count (runner);
		for (i = 0; i < n; i++)
		{
			const gchar *rid = lrg_bt_runner_get_node_id (runner, i);
			LrgBTNode   *bn = lrg_bt_runner_get_node (runner, rid);
			JsonObject  *no = json_object_new ();

			json_object_set_string_member (no, "id", rid);
			json_object_set_string_member (no, "status", lrg_bt_status_to_string (lrg_bt_runner_get_node_status (runner,
			                                                                                                    rid)));
			json_object_set_boolean_member (no, "visited", lrg_bt_runner_get_node_visited (runner, rid));
			json_object_set_int_member (no, "ticks", bn != NULL ? (gint64) lrg_bt_node_get_tick_count (bn) : 0);
			json_array_add_object_element (nodes, no);
		}
	}
	else
	{
		json_object_set_null_member (o, "entity");
		json_object_set_null_member (o, "status");
		json_object_set_int_member (o, "tick", 0);
		json_object_set_null_member (o, "current");
	}
	json_object_set_array_member (o, "active_path", active);
	json_object_set_array_member (o, "nodes", nodes);
	return editor_ai_object_node (o);
}

void
lrg_editor_bt_editor_install (LrgEditorApp *app)
{
	static const struct { const gchar *name; LrgEditorServerMethod func; } methods[] = {
		{ "bt.open", m_bt_open },
		{ "bt.create", m_bt_create },
		{ "bt.get", m_bt_get },
		{ "bt.palette", m_bt_palette },
		{ "bt.add_node", m_bt_add_node },
		{ "bt.remove_node", m_bt_remove_node },
		{ "bt.move_node", m_bt_move_node },
		{ "bt.set_node", m_bt_set_node },
		{ "bt.set_position", m_bt_set_position },
		{ "bt.layout", m_bt_layout },
		{ "bt.select", m_bt_select },
		{ "bt.show", m_bt_show },
		{ "bt.add_key", m_bt_add_key },
		{ "bt.remove_key", m_bt_remove_key },
		{ "bt.set_subtree", m_bt_set_subtree },
		{ "bt.remove_subtree", m_bt_remove_subtree },
		{ "bt.open_subtree", m_bt_open_subtree },
		{ "bt.validate", m_bt_validate },
		{ "bt.save", m_bt_save },
		{ "bt.live", m_bt_live }
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
	lrg_editor_ai_debug_panel_install (app);
	for (i = 0; i < G_N_ELEMENTS (methods); i++)
		lrg_editor_server_register_method (server, methods[i].name, methods[i].func, app, NULL);
}
