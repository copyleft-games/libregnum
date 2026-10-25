/* lrg-gui-dock.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Dockable panel layout. See lrg-gui-dock.h.
 *
 * The layout is a binary tree: split nodes divide their rectangle by a
 * ratio, leaves are tab stacks of panel ids. Removing the last tab of a
 * leaf collapses it into its sibling, so the tree never holds empty
 * stacks (except an empty root).
 */

#include "lrg-gui-dock.h"
#include "lrg-gui-private.h"
#include <gio/gio.h>
#include <string.h>
#include <math.h>

#define SPLITTER_SIZE   (4.0f)
#define MIN_PANE_SIZE   (48.0f)
#define DOCK_DND_TYPE   "lrg-dock-panel"

typedef struct _DockNode DockNode;

struct _DockNode
{
	gboolean    split;
	gboolean    vertical;   /* split: children side by side */
	gfloat      ratio;      /* split: share of the first child */
	DockNode   *a;
	DockNode   *b;
	DockNode   *parent;
	GPtrArray  *tabs;       /* leaf: gchar* panel ids */
	gint        active;
	LrgGuiRect  rect;       /* computed each frame */
	LrgGuiRect  content;
};

struct _LrgGuiDock
{
	GObject     parent_instance;

	DockNode   *root;
	GHashTable *titles;       /* panel -> title */
	GHashTable *closable;     /* panel -> GINT (0 = not closable) */
	GHashTable *flags;        /* panel -> LrgGuiRegionFlags + 1 */
	gboolean    in_panel;
	guint       split_counter;
};

G_DEFINE_FINAL_TYPE (LrgGuiDock, lrg_gui_dock, G_TYPE_OBJECT)

/* ==========================================================================
 * Tree helpers
 * ========================================================================== */

static DockNode *
leaf_new (void)
{
	DockNode *n = g_new0 (DockNode, 1);

	n->tabs = g_ptr_array_new_with_free_func (g_free);
	return n;
}

static void
node_free (DockNode *n)
{
	if (n == NULL)
		return;
	node_free (n->a);
	node_free (n->b);
	if (n->tabs != NULL)
		g_ptr_array_unref (n->tabs);
	g_free (n);
}

static DockNode *
find_leaf (DockNode    *n,
           const gchar *panel,
           guint       *index)
{
	guint i;

	if (n == NULL)
		return NULL;
	if (n->split)
	{
		DockNode *f = find_leaf (n->a, panel, index);

		return (f != NULL) ? f : find_leaf (n->b, panel, index);
	}
	for (i = 0; i < n->tabs->len; i++)
	{
		if (g_strcmp0 (g_ptr_array_index (n->tabs, i), panel) == 0)
		{
			if (index != NULL)
				*index = i;
			return n;
		}
	}
	return NULL;
}

static DockNode *
first_leaf (DockNode *n)
{
	while (n != NULL && n->split)
		n = n->a;
	return n;
}

static void
largest_leaf (DockNode  *n,
              DockNode **best,
              gfloat    *area)
{
	if (n == NULL)
		return;
	if (n->split)
	{
		largest_leaf (n->a, best, area);
		largest_leaf (n->b, best, area);
		return;
	}
	if (*best == NULL || n->rect.w * n->rect.h > *area)
	{
		*best = n;
		*area = n->rect.w * n->rect.h;
	}
}

/* Replaces @old by @repl in @old's parent (or as root). */
static void
replace_node (LrgGuiDock *self,
              DockNode   *old,
              DockNode   *repl)
{
	DockNode *p = old->parent;

	repl->parent = p;
	if (p == NULL)
		self->root = repl;
	else if (p->a == old)
		p->a = repl;
	else
		p->b = repl;
}

static void
remove_panel (LrgGuiDock  *self,
              const gchar *panel)
{
	DockNode *leaf;
	DockNode *parent;
	DockNode *sibling;
	guint     index;

	leaf = find_leaf (self->root, panel, &index);
	if (leaf == NULL)
		return;

	g_ptr_array_remove_index (leaf->tabs, index);
	if (leaf->active >= (gint) leaf->tabs->len)
		leaf->active = MAX (0, (gint) leaf->tabs->len - 1);
	if (leaf->tabs->len > 0 || leaf->parent == NULL)
		return;

	/* Collapse the empty leaf into its sibling. */
	parent = leaf->parent;
	sibling = (parent->a == leaf) ? parent->b : parent->a;
	parent->a = NULL;
	parent->b = NULL;
	replace_node (self, parent, sibling);
	node_free (leaf);
	node_free (parent);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_gui_dock_finalize (GObject *object)
{
	LrgGuiDock *self = LRG_GUI_DOCK (object);

	node_free (self->root);
	g_hash_table_unref (self->titles);
	g_hash_table_unref (self->closable);
	g_hash_table_unref (self->flags);

	G_OBJECT_CLASS (lrg_gui_dock_parent_class)->finalize (object);
}

static void
lrg_gui_dock_class_init (LrgGuiDockClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_gui_dock_finalize;
}

static void
lrg_gui_dock_init (LrgGuiDock *self)
{
	self->root = leaf_new ();
	self->titles = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->closable = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->flags = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

LrgGuiDock *
lrg_gui_dock_new (void)
{
	return g_object_new (LRG_TYPE_GUI_DOCK, NULL);
}

/* ==========================================================================
 * Serialization
 * ========================================================================== */

static void
serialize (DockNode *n,
           GString  *out)
{
	guint i;

	if (n->split)
	{
		gchar ratio[G_ASCII_DTOSTR_BUF_SIZE];

		g_ascii_formatd (ratio, sizeof ratio, "%.3f", n->ratio);
		g_string_append_printf (out, "split(%c,%s,", n->vertical ? 'v' : 'h', ratio);
		serialize (n->a, out);
		g_string_append_c (out, ',');
		serialize (n->b, out);
		g_string_append_c (out, ')');
		return;
	}

	g_string_append_printf (out, "tabs(%d", n->active);
	for (i = 0; i < n->tabs->len; i++)
		g_string_append_printf (out, ",%s", (const gchar *) g_ptr_array_index (n->tabs, i));
	g_string_append_c (out, ')');
}

gchar *
lrg_gui_dock_to_string (LrgGuiDock *self)
{
	GString *out;

	g_return_val_if_fail (LRG_IS_GUI_DOCK (self), NULL);

	out = g_string_new (NULL);
	serialize (self->root, out);
	return g_string_free (out, FALSE);
}

typedef struct
{
	const gchar *p;
	GError     **error;
} Parser;

static void
skip_ws (Parser *ps)
{
	while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n')
		ps->p++;
}

static gboolean
expect (Parser      *ps,
        const gchar *tok)
{
	skip_ws (ps);
	if (!g_str_has_prefix (ps->p, tok))
	{
		g_set_error (ps->error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		             "Dock layout: expected '%s' at '%.20s'", tok, ps->p);
		return FALSE;
	}
	ps->p += strlen (tok);
	return TRUE;
}

static gchar *
parse_word (Parser *ps)
{
	const gchar *start;

	skip_ws (ps);
	start = ps->p;
	while (g_ascii_isalnum (*ps->p) || *ps->p == '_' || *ps->p == '-' || *ps->p == '.')
		ps->p++;
	return (ps->p > start) ? g_strndup (start, ps->p - start) : NULL;
}

static DockNode *
parse_node (Parser *ps,
            guint   depth)
{
	skip_ws (ps);
	if (depth > 64)
	{
		g_set_error_literal (ps->error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                     "Dock layout nested too deeply");
		return NULL;
	}

	if (g_str_has_prefix (ps->p, "split("))
	{
		DockNode         *n = g_new0 (DockNode, 1);
		g_autofree gchar *dir = NULL;
		g_autofree gchar *ratio = NULL;

		ps->p += 6;
		n->split = TRUE;
		dir = parse_word (ps);
		if (dir == NULL || (strcmp (dir, "v") != 0 && strcmp (dir, "h") != 0) || !expect (ps, ","))
		{
			if (ps->error != NULL && *ps->error == NULL)
				g_set_error_literal (ps->error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
				                     "Dock layout: split direction must be v or h");
			g_free (n);
			return NULL;
		}
		n->vertical = (dir[0] == 'v');
		ratio = parse_word (ps);
		n->ratio = (ratio != NULL) ? (gfloat) g_ascii_strtod (ratio, NULL) : 0.5f;
		n->ratio = CLAMP (n->ratio, 0.05f, 0.95f);
		if (!expect (ps, ","))
		{
			g_free (n);
			return NULL;
		}
		n->a = parse_node (ps, depth + 1);
		if (n->a == NULL || !expect (ps, ","))
		{
			node_free (n);
			return NULL;
		}
		n->b = parse_node (ps, depth + 1);
		if (n->b == NULL || !expect (ps, ")"))
		{
			node_free (n);
			return NULL;
		}
		n->a->parent = n;
		n->b->parent = n;
		return n;
	}

	if (g_str_has_prefix (ps->p, "tabs("))
	{
		DockNode         *n = leaf_new ();
		g_autofree gchar *active = NULL;

		ps->p += 5;
		active = parse_word (ps);
		n->active = (active != NULL) ? (gint) g_ascii_strtoll (active, NULL, 10) : 0;
		skip_ws (ps);
		while (*ps->p == ',')
		{
			gchar *id;

			ps->p++;
			id = parse_word (ps);
			if (id == NULL)
			{
				g_set_error_literal (ps->error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
				                     "Dock layout: empty panel id");
				node_free (n);
				return NULL;
			}
			g_ptr_array_add (n->tabs, id);
			skip_ws (ps);
		}
		if (!expect (ps, ")"))
		{
			node_free (n);
			return NULL;
		}
		n->active = CLAMP (n->active, 0, MAX (0, (gint) n->tabs->len - 1));
		return n;
	}

	g_set_error (ps->error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
	             "Dock layout: unexpected '%.20s'", ps->p);
	return NULL;
}

gboolean
lrg_gui_dock_load_string (LrgGuiDock   *self,
                          const gchar  *layout,
                          GError      **error)
{
	Parser    ps;
	DockNode *root;

	g_return_val_if_fail (LRG_IS_GUI_DOCK (self), FALSE);
	g_return_val_if_fail (layout != NULL, FALSE);

	ps.p = layout;
	ps.error = error;
	root = parse_node (&ps, 0);
	if (root == NULL)
		return FALSE;
	skip_ws (&ps);
	if (*ps.p != '\0')
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		             "Dock layout: trailing data '%.20s'", ps.p);
		node_free (root);
		return FALSE;
	}

	node_free (self->root);
	self->root = root;
	return TRUE;
}

/* ==========================================================================
 * Docking operations
 * ========================================================================== */

void
lrg_gui_dock_dock_panel (LrgGuiDock     *self,
                         const gchar    *panel,
                         const gchar    *target,
                         LrgGuiDockSide  side,
                         gfloat          ratio)
{
	DockNode *target_node;
	DockNode *leaf;
	DockNode *split;

	g_return_if_fail (LRG_IS_GUI_DOCK (self));
	g_return_if_fail (panel != NULL);

	if (g_strcmp0 (panel, target) == 0)
		return;

	remove_panel (self, panel);
	if (self->root == NULL)
		self->root = leaf_new ();

	target_node = (target != NULL) ? find_leaf (self->root, target, NULL) : NULL;
	if (target_node == NULL)
		target_node = (side == LRG_GUI_DOCK_CENTER) ? first_leaf (self->root) : self->root;
	if (target_node == NULL)
		return;

	/* Docking into an empty root just fills it. */
	if (!self->root->split && self->root->tabs->len == 0)
	{
		g_ptr_array_add (self->root->tabs, g_strdup (panel));
		self->root->active = 0;
		return;
	}

	if (side == LRG_GUI_DOCK_CENTER)
	{
		if (target_node->split)
			target_node = first_leaf (target_node);
		if (target_node == NULL)
			return;
		g_ptr_array_add (target_node->tabs, g_strdup (panel));
		target_node->active = (gint) target_node->tabs->len - 1;
		return;
	}

	leaf = leaf_new ();
	g_ptr_array_add (leaf->tabs, g_strdup (panel));

	split = g_new0 (DockNode, 1);
	split->split = TRUE;
	split->vertical = (side == LRG_GUI_DOCK_LEFT || side == LRG_GUI_DOCK_RIGHT);
	ratio = CLAMP (ratio > 0.0f ? ratio : 0.5f, 0.05f, 0.95f);

	replace_node (self, target_node, split);
	if (side == LRG_GUI_DOCK_LEFT || side == LRG_GUI_DOCK_TOP)
	{
		split->a = leaf;
		split->b = target_node;
		split->ratio = ratio;
	}
	else
	{
		split->a = target_node;
		split->b = leaf;
		split->ratio = 1.0f - ratio;
	}
	split->a->parent = split;
	split->b->parent = split;
}

void
lrg_gui_dock_open_panel (LrgGuiDock  *self,
                         const gchar *panel)
{
	DockNode *leaf;
	guint     index;
	gfloat    area = 0.0f;

	g_return_if_fail (LRG_IS_GUI_DOCK (self));
	g_return_if_fail (panel != NULL);

	leaf = find_leaf (self->root, panel, &index);
	if (leaf != NULL)
	{
		leaf->active = (gint) index;
		return;
	}

	leaf = NULL;
	largest_leaf (self->root, &leaf, &area);
	if (leaf == NULL)
		leaf = first_leaf (self->root);
	g_ptr_array_add (leaf->tabs, g_strdup (panel));
	leaf->active = (gint) leaf->tabs->len - 1;
}

void
lrg_gui_dock_close_panel (LrgGuiDock  *self,
                          const gchar *panel)
{
	g_return_if_fail (LRG_IS_GUI_DOCK (self));
	g_return_if_fail (panel != NULL);

	remove_panel (self, panel);
}

gboolean
lrg_gui_dock_has_panel (LrgGuiDock  *self,
                        const gchar *panel)
{
	g_return_val_if_fail (LRG_IS_GUI_DOCK (self), FALSE);
	g_return_val_if_fail (panel != NULL, FALSE);

	return find_leaf (self->root, panel, NULL) != NULL;
}

gboolean
lrg_gui_dock_is_panel_visible (LrgGuiDock  *self,
                               const gchar *panel)
{
	DockNode *leaf;
	guint     index;

	g_return_val_if_fail (LRG_IS_GUI_DOCK (self), FALSE);
	g_return_val_if_fail (panel != NULL, FALSE);

	leaf = find_leaf (self->root, panel, &index);
	return leaf != NULL && leaf->active == (gint) index;
}

gboolean
lrg_gui_dock_get_panel_rect (LrgGuiDock  *self,
                             const gchar *panel,
                             LrgGuiRect  *rect)
{
	DockNode *leaf;
	guint     index;

	g_return_val_if_fail (LRG_IS_GUI_DOCK (self), FALSE);
	g_return_val_if_fail (panel != NULL && rect != NULL, FALSE);

	leaf = find_leaf (self->root, panel, &index);
	if (leaf == NULL || leaf->active != (gint) index)
		return FALSE;

	*rect = leaf->content;
	return TRUE;
}

void
lrg_gui_dock_set_closable (LrgGuiDock  *self,
                           const gchar *panel,
                           gboolean     closable)
{
	g_return_if_fail (LRG_IS_GUI_DOCK (self));
	g_return_if_fail (panel != NULL);

	g_hash_table_replace (self->closable, g_strdup (panel), GINT_TO_POINTER (closable ? 1 : 0));
}

void
lrg_gui_dock_set_panel_flags (LrgGuiDock        *self,
                              const gchar       *panel,
                              LrgGuiRegionFlags  flags)
{
	g_return_if_fail (LRG_IS_GUI_DOCK (self));
	g_return_if_fail (panel != NULL);

	g_hash_table_replace (self->flags, g_strdup (panel), GINT_TO_POINTER ((gint) flags + 1));
}

/* ==========================================================================
 * Layout and drawing
 * ========================================================================== */

static gboolean
panel_closable (LrgGuiDock  *self,
                const gchar *panel)
{
	gpointer v;

	if (!g_hash_table_lookup_extended (self->closable, panel, NULL, &v))
		return TRUE;
	return GPOINTER_TO_INT (v) != 0;
}

static void
layout_node (LrgGuiDock       *self,
             LrgGuiContext    *ctx,
             DockNode         *n,
             const LrgGuiRect *rect,
             guint            *split_index)
{
	n->rect = *rect;

	if (n->split)
	{
		LrgGuiRect ra = *rect, rb = *rect, handle;
		gfloat     total = n->vertical ? rect->w : rect->h;
		gfloat     first, delta = 0.0f;
		gchar      id[32];

		/* The splitter: drag to move the ratio. */
		first = roundf ((total - SPLITTER_SIZE) * n->ratio);
		first = CLAMP (first, MIN (MIN_PANE_SIZE, total * 0.5f),
		               MAX (total - SPLITTER_SIZE - MIN_PANE_SIZE, total * 0.5f));

		if (n->vertical)
		{
			handle.x = rect->x + first;
			handle.y = rect->y;
			handle.w = SPLITTER_SIZE;
			handle.h = rect->h;
		}
		else
		{
			handle.x = rect->x;
			handle.y = rect->y + first;
			handle.w = rect->w;
			handle.h = SPLITTER_SIZE;
		}

		g_snprintf (id, sizeof id, "##dock-split-%u", (*split_index)++);
		if (lrg_gui_splitter (ctx, id, &handle, n->vertical, &delta) && total > SPLITTER_SIZE)
		{
			n->ratio = CLAMP ((first + delta) / (total - SPLITTER_SIZE), 0.05f, 0.95f);
			first = roundf ((total - SPLITTER_SIZE) * n->ratio);
		}

		if (n->vertical)
		{
			ra.w = first;
			rb.x = rect->x + first + SPLITTER_SIZE;
			rb.w = rect->w - first - SPLITTER_SIZE;
		}
		else
		{
			ra.h = first;
			rb.y = rect->y + first + SPLITTER_SIZE;
			rb.h = rect->h - first - SPLITTER_SIZE;
		}

		layout_node (self, ctx, n->a, &ra, split_index);
		layout_node (self, ctx, n->b, &rb, split_index);
		return;
	}

	/* Leaf: tab strip + content. */
	{
		gfloat      strip_h = gui_frame_height (ctx) + 2.0f;
		LrgGuiRect  strip = { rect->x, rect->y, rect->w, strip_h };
		gfloat      x = rect->x + 2.0f;
		guint       i;
		const gchar *close_panel = NULL;

		n->content.x = rect->x;
		n->content.y = rect->y + strip_h;
		n->content.w = rect->w;
		n->content.h = MAX (0.0f, rect->h - strip_h);

		lrg_gui_draw_list_rect_filled (gui_dl (ctx), &strip, gui_col (ctx, LRG_GUI_COL_TAB), 0.0f);

		for (i = 0; i < n->tabs->len; i++)
		{
			const gchar *panel = g_ptr_array_index (n->tabs, i);
			const gchar *title = g_hash_table_lookup (self->titles, panel);
			const gchar *end;
			gboolean     selected = (gint) i == n->active;
			gboolean     closable = panel_closable (self, panel);
			gboolean     hovered, held;
			LrgGuiRect   tab;
			LrgGuiId     tid;
			gchar        idbuf[160];
			gfloat       tw;

			if (title == NULL)
				title = panel;
			end = gui_label_end (title);
			tw = gui_text_width (ctx, title, end - title, FALSE);

			tab.x = x;
			tab.y = rect->y + 2.0f;
			tab.w = tw + ctx->style.frame_padding_x * 2.0f + (closable ? ctx->style.font_size : 0.0f);
			tab.h = strip_h - 2.0f;
			x += tab.w + 2.0f;

			g_snprintf (idbuf, sizeof idbuf, "##dock-tab-%s", panel);
			tid = gui_hash_label (ctx, idbuf);
			if (gui_button_behavior (ctx, tid, &tab, GUI_BUTTON_ON_PRESS | GUI_BUTTON_MIDDLE,
			                         &hovered, &held))
			{
				if (ctx->item.clicked[LRG_GUI_MOUSE_MIDDLE] && closable)
					close_panel = panel;
				else
					n->active = (gint) i;
			}

			/* Dragging a tab carries the panel to a new place. */
			if (lrg_gui_begin_drag_source (ctx))
			{
				lrg_gui_set_drag_payload (ctx, DOCK_DND_TYPE, panel, title);
				lrg_gui_end_drag_source (ctx);
			}

			lrg_gui_draw_list_rect_filled (gui_dl (ctx), &tab,
			                               gui_col (ctx, selected ? LRG_GUI_COL_TAB_ACTIVE
			                                                      : (hovered ? LRG_GUI_COL_TAB_HOVERED
			                                                                 : LRG_GUI_COL_TAB)),
			                               ctx->style.rounding);
			if (selected)
				lrg_gui_draw_list_line (gui_dl (ctx), tab.x + 1.0f, tab.y + 1.0f, tab.x + tab.w - 1.0f,
				                        tab.y + 1.0f, gui_col (ctx, LRG_GUI_COL_ACCENT), 2.0f);
			{
				LrgGuiRect text = { tab.x + ctx->style.frame_padding_x, tab.y, tw, tab.h };

				gui_draw_text_clipped (ctx, &text, title, end - title,
				                       gui_col (ctx, selected ? LRG_GUI_COL_TEXT : LRG_GUI_COL_TEXT_DISABLED),
				                       0.0f, FALSE);
			}

			if (closable)
			{
				LrgGuiRect close = { tab.x + tab.w - ctx->style.font_size - 2.0f, tab.y,
				                     ctx->style.font_size, tab.h };
				gboolean   ch;

				g_snprintf (idbuf, sizeof idbuf, "##dock-close-%s", panel);
				if (gui_button_behavior (ctx, gui_hash_label (ctx, idbuf), &close,
				                         GUI_BUTTON_ON_RELEASE, &ch, NULL))
					close_panel = panel;
				if (ch || hovered || selected)
					gui_draw_text_clipped (ctx, &close, "x", 1,
					                       gui_col (ctx, ch ? LRG_GUI_COL_ERROR : LRG_GUI_COL_TEXT_DISABLED),
					                       0.5f, FALSE);
			}
		}

		if (close_panel != NULL)
		{
			g_autofree gchar *copy = g_strdup (close_panel);

			remove_panel (self, copy);
		}
	}
}

void
lrg_gui_dock_begin (LrgGuiDock       *self,
                    LrgGuiContext    *ctx,
                    const LrgGuiRect *rect)
{
	guint split_index = 0;

	g_return_if_fail (LRG_IS_GUI_DOCK (self));
	g_return_if_fail (LRG_IS_GUI_CONTEXT (ctx));
	g_return_if_fail (rect != NULL);

	lrg_gui_draw_list_rect_filled (gui_dl (ctx), rect, gui_col (ctx, LRG_GUI_COL_WINDOW_BG), 0.0f);
	lrg_gui_push_id (ctx, "##dock");
	layout_node (self, ctx, self->root, rect, &split_index);
	lrg_gui_pop_id (ctx);
}

gboolean
lrg_gui_dock_begin_panel (LrgGuiDock    *self,
                          LrgGuiContext *ctx,
                          const gchar   *panel,
                          const gchar   *title)
{
	DockNode         *leaf;
	guint             index;
	gpointer          flags_v;
	LrgGuiRegionFlags flags = LRG_GUI_REGION_SCROLL_Y | LRG_GUI_REGION_BACKGROUND;
	g_autofree gchar *rid = NULL;

	g_return_val_if_fail (LRG_IS_GUI_DOCK (self), FALSE);
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (ctx), FALSE);
	g_return_val_if_fail (panel != NULL, FALSE);
	g_return_val_if_fail (!self->in_panel, FALSE);

	/* Remember the title for next frame's tab strip. */
	if (title != NULL)
		g_hash_table_replace (self->titles, g_strdup (panel), g_strdup (title));

	leaf = find_leaf (self->root, panel, &index);
	if (leaf == NULL || leaf->active != (gint) index || leaf->content.w < 1.0f ||
	    leaf->content.h < 1.0f)
		return FALSE;

	if (g_hash_table_lookup_extended (self->flags, panel, NULL, &flags_v))
		flags = (LrgGuiRegionFlags) (GPOINTER_TO_INT (flags_v) - 1) | LRG_GUI_REGION_BACKGROUND;

	rid = g_strdup_printf ("##dock-panel-%s", panel);
	lrg_gui_begin_region (ctx, rid, &leaf->content, flags);
	lrg_gui_push_id (ctx, panel);
	self->in_panel = TRUE;
	return TRUE;
}

void
lrg_gui_dock_end_panel (LrgGuiDock    *self,
                        LrgGuiContext *ctx)
{
	g_return_if_fail (LRG_IS_GUI_DOCK (self));
	g_return_if_fail (self->in_panel);

	lrg_gui_pop_id (ctx);
	lrg_gui_end_region (ctx);
	self->in_panel = FALSE;
}

/* Which drop zone of @leaf the pointer is in. */
static LrgGuiDockSide
zone_at (DockNode *leaf,
         gfloat    x,
         gfloat    y,
         LrgGuiRect *zone)
{
	LrgGuiRect r = leaf->rect;
	gfloat     fx = (x - r.x) / MAX (1.0f, r.w);
	gfloat     fy = (y - r.y) / MAX (1.0f, r.h);

	*zone = r;
	if (fx < 0.25f && fx < fy && fx < 1.0f - fy)
	{
		zone->w = r.w * 0.5f;
		return LRG_GUI_DOCK_LEFT;
	}
	if (fx > 0.75f && 1.0f - fx < fy && 1.0f - fx < 1.0f - fy)
	{
		zone->x = r.x + r.w * 0.5f;
		zone->w = r.w * 0.5f;
		return LRG_GUI_DOCK_RIGHT;
	}
	if (fy < 0.25f)
	{
		zone->h = r.h * 0.5f;
		return LRG_GUI_DOCK_TOP;
	}
	if (fy > 0.75f)
	{
		zone->y = r.y + r.h * 0.5f;
		zone->h = r.h * 0.5f;
		return LRG_GUI_DOCK_BOTTOM;
	}
	return LRG_GUI_DOCK_CENTER;
}

static DockNode *
leaf_at (DockNode *n,
         gfloat    x,
         gfloat    y)
{
	if (n == NULL || !gui_rect_contains (&n->rect, x, y))
		return NULL;
	if (!n->split)
		return n;
	{
		DockNode *f = leaf_at (n->a, x, y);

		return (f != NULL) ? f : leaf_at (n->b, x, y);
	}
}

void
lrg_gui_dock_end (LrgGuiDock    *self,
                  LrgGuiContext *ctx)
{
	const gchar *type = NULL;
	const gchar *panel;
	DockNode    *target;
	LrgGuiRect   zone;
	LrgGuiDockSide side;

	g_return_if_fail (LRG_IS_GUI_DOCK (self));
	g_return_if_fail (LRG_IS_GUI_CONTEXT (ctx));

	panel = lrg_gui_get_drag_payload (ctx, &type);
	if (panel == NULL || g_strcmp0 (type, DOCK_DND_TYPE) != 0)
		return;

	target = leaf_at (self->root, ctx->input.mouse_x, ctx->input.mouse_y);
	if (target == NULL)
		return;

	side = zone_at (target, ctx->input.mouse_x, ctx->input.mouse_y, &zone);

	/* Preview where the panel would land. */
	lrg_gui_draw_list_rect_filled (ctx->layers[GUI_LAYER_OVERLAY], &zone,
	                               (gui_col (ctx, LRG_GUI_COL_ACCENT) & 0xffffff00u) | 0x40u, 0.0f);
	lrg_gui_draw_list_rect_outline (ctx->layers[GUI_LAYER_OVERLAY], &zone,
	                                gui_col (ctx, LRG_GUI_COL_ACCENT), 2.0f, 0.0f);

	if (lrg_gui_was_payload_dropped (ctx))
	{
		g_autofree gchar *moving = g_strdup (panel);
		const gchar      *anchor = NULL;

		/* Dropping a panel onto its own lone stack is a no-op. */
		if (target->tabs->len == 1 &&
		    g_strcmp0 (g_ptr_array_index (target->tabs, 0), moving) == 0)
			return;

		/* Dock relative to any other panel of the target stack. */
		{
			guint i;

			for (i = 0; i < target->tabs->len; i++)
				if (g_strcmp0 (g_ptr_array_index (target->tabs, i), moving) != 0)
				{
					anchor = g_ptr_array_index (target->tabs, i);
					break;
				}
		}
		if (anchor == NULL)
			return;

		{
			g_autofree gchar *anchor_copy = g_strdup (anchor);

			lrg_gui_dock_dock_panel (self, moving, anchor_copy, side, 0.5f);
		}
	}
}
