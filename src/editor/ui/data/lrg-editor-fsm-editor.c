/* lrg-editor-fsm-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorFsmEditor: the `.rfsm` data editor (see the header).
 *
 * Layout:
 *
 *   +----------------------------------------------+------------------+
 *   | sentry > combat                  live: ...   | State · attack   |
 *   |                                              |  name, initial   |
 *   |  [Any State] --> (patrol) --> (combat)       |  ENTER actions   |
 *   |                        \--> (dead)           |  UPDATE actions  |
 *   |   LrgNodeGraph: one level of the hierarchy   |  EXIT actions    |
 *   |                                              |  TRANSITIONS     |
 *   |                                              | BLACKBOARD       |
 *   +----------------------------------------------+------------------+
 *
 * Addressing. States are named by path ("combat/attack", "" the root);
 * the YAML path of a state is found by walking `states:` lists by name on
 * every access, so indexes never go stale. A transition is (owner path,
 * index in the owner's `transitions:`). Targets (`to:`) are resolved with
 * the engine's rule (siblings of the owner, then of each ancestor, then
 * the owner's own children; "/a/b" absolute) to decide which card a wire
 * ends on.
 *
 * The document is the truth; canvas gestures share one lazily closed
 * group, API edits one group each. After every change the text is parsed
 * into an LrgLogicMachine for validation; the canvas is rebuilt unless the
 * change came from the canvas. Live tints come from the focused entity's
 * running machine every frame, when its update count changed.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-fsm-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../lrg-editor-ai-debug-panel.h"
#include "../../lrg-editor-server.h"
#include "../../../ai/lrg-blackboard.h"
#include "../../../ai/lrg-bt-asset.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-button.h"
#include "../../../ui/lrg-combo-box.h"
#include "../../../ui/lrg-text-input.h"
#include "../../../ui/lrg-scroll-box.h"
#include "../../../ui/lrg-breadcrumb.h"
#include "../../../ui/lrg-wrap-box.h"
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>

#define INSTALLED_KEY  "lrg-editor-fsm-installed"
#define OWNER          LRG_EDITOR_FSM_EDITOR_OWNER_CARD
#define EXT_PREFIX     "@to:"
#define GRID_COLS      (3)
#define GRID_DX        (280.0f)
#define GRID_DY        (190.0f)
#define CARD_W         (190.0f)
#define POS_ROOT       "/editor/positions"

/* One wire of the canvas and the transition it stands for. */
typedef struct
{
	gchar *from_card;
	gchar *to_card;
	gchar *owner;      /* state path */
	guint  index;
} Wire;

static void
wire_free (gpointer data)
{
	Wire *w = data;

	g_free (w->from_card);
	g_free (w->to_card);
	g_free (w->owner);
	g_free (w);
}

static const gchar * const phase_keys[] = { "enter", "update", "exit" };

struct _LrgEditorFsmEditor
{
	LrgEditorDataEditor  parent_instance;

	LrgLogicMachine     *machine;        /* parsed preview, never started */
	gchar               *load_error;
	gchar               *shown;          /* state path whose children show */
	GPtrArray           *wires;          /* Wire */
	GHashTable          *bodies;         /* card id -> body widget (borrowed) */

	gchar               *sel_state;
	gboolean             sel_tr;
	gchar               *sel_from;
	guint                sel_index;

	LrgNodeGraph        *canvas;
	LrgBreadcrumb       *crumbs;
	LrgWidget           *live_label;
	LrgWidget           *inspector_title;
	LrgPropertyGrid     *grid;
	LrgEditorDataSource *source;
	LrgWidget           *lists;          /* action / condition lists */
	LrgWidget           *tr_rows;
	LrgWidget           *bb_rows;
	LrgTextInput        *bb_name;
	LrgComboBox         *bb_type;
	LrgTextInput        *event_input;

	gboolean             syncing;
	gboolean             writing;
	gboolean             group_open;
	guint                group_idle;
	gboolean             canvas_stale;
	gboolean             grid_stale;
	gboolean             rows_stale;
	gchar               *grid_sig;
	gchar               *rows_sig;
	gchar               *live_sig;
	guint                framed;
};

G_DEFINE_FINAL_TYPE (LrgEditorFsmEditor, lrg_editor_fsm_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void rebuild_canvas (LrgEditorFsmEditor *self);
static void rebuild_grid (LrgEditorFsmEditor *self);
static void rebuild_rows (LrgEditorFsmEditor *self);

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgEditorDocument *
doc_of (LrgEditorFsmEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

static LrgEditorApp *
app_of (LrgEditorFsmEditor *self)
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
scalar_at (LrgEditorFsmEditor *self,
           const gchar        *parent,
           const gchar        *key)
{
	g_autofree gchar *p = pj (parent, key);

	return lrg_editor_document_get_scalar (doc_of (self), p);
}

static gint
n_items (LrgEditorFsmEditor *self,
         const gchar        *path)
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

static gboolean
not_found (GError      **error,
           const gchar  *what,
           const gchar  *name)
{
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no %s '%s'", what, name != NULL ? name : "");
	return FALSE;
}

/* "a/b" + "c" -> "a/b/c"; "" + "c" -> "c". */
static gchar *
state_join (const gchar *parent,
            const gchar *name)
{
	if (parent == NULL || *parent == '\0')
		return g_strdup (name);
	return g_strconcat (parent, "/", name, NULL);
}

/* The parent's path ("a/b" -> "a", "a" -> ""). */
static gchar *
state_parent (const gchar *path)
{
	const gchar *slash = strrchr (path, '/');

	return slash != NULL ? g_strndup (path, slash - path) : g_strdup ("");
}

static const gchar *
state_name (const gchar *path)
{
	const gchar *slash = strrchr (path, '/');

	return slash != NULL ? slash + 1 : path;
}

/* The YAML path of a state, NULL when it does not exist. */
static gchar *
state_yaml (LrgEditorFsmEditor *self,
            const gchar        *path)
{
	g_auto(GStrv)     parts = NULL;
	g_autofree gchar *cur = g_strdup ("");
	guint             i;

	if (path == NULL)
		return NULL;
	if (*path == '\0')
		return g_strdup ("");
	parts = g_strsplit (path, "/", -1);
	for (i = 0; parts[i] != NULL; i++)
	{
		g_autofree gchar *list = pj (cur, "states");
		gint              n = n_items (self, list);
		gint              k;
		gboolean          found = FALSE;

		for (k = 0; k < n; k++)
		{
			g_autofree gchar *sp = pji (list, (guint) k);

			if (g_strcmp0 (scalar_at (self, sp, "name"), parts[i]) == 0)
			{
				g_free (cur);
				cur = g_steal_pointer (&sp);
				found = TRUE;
				break;
			}
		}
		if (!found)
			return NULL;
	}
	return g_steal_pointer (&cur);
}

static gboolean
state_exists (LrgEditorFsmEditor *self,
              const gchar        *path)
{
	g_autofree gchar *yp = state_yaml (self, path);

	return yp != NULL;
}

/* The child names of a state, in file order. */
static GPtrArray *
child_names (LrgEditorFsmEditor *self,
             const gchar        *path)
{
	g_autofree gchar *yp = state_yaml (self, path);
	g_autofree gchar *list = yp != NULL ? pj (yp, "states") : NULL;
	GPtrArray        *out = g_ptr_array_new_with_free_func (g_free);
	gint              n = list != NULL ? n_items (self, list) : 0;
	gint              i;

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *sp = pji (list, (guint) i);
		const gchar      *name = scalar_at (self, sp, "name");

		if (name != NULL)
			g_ptr_array_add (out, g_strdup (name));
	}
	return out;
}

/* The engine's target rule: "/a/b" absolute; else the first segment among
 * the owner's siblings, its parent's siblings ... the top level, then the
 * owner's own children; the rest descends. */
static gchar *
resolve_target (LrgEditorFsmEditor *self,
                const gchar        *owner,
                const gchar        *to)
{
	g_auto(GStrv)     segs = NULL;
	g_autofree gchar *base = NULL;
	g_autofree gchar *cur = NULL;
	guint             i;

	if (to == NULL || *to == '\0')
		return NULL;
	if (*to == '/')
		return state_exists (self, to + 1) ? g_strdup (to + 1) : NULL;
	segs = g_strsplit (to, "/", -1);
	cur = g_strdup (owner);
	while (*cur != '\0')
	{
		g_autofree gchar *par = state_parent (cur);
		g_autofree gchar *cand = state_join (par, segs[0]);

		if (state_exists (self, cand))
		{
			base = g_steal_pointer (&cand);
			break;
		}
		g_free (cur);
		cur = g_steal_pointer (&par);
	}
	if (base == NULL)
	{
		g_autofree gchar *cand = state_join (owner, segs[0]);

		if (!state_exists (self, cand))
			return NULL;
		base = g_steal_pointer (&cand);
	}
	for (i = 1; segs[i] != NULL; i++)
	{
		gchar *next = state_join (base, segs[i]);

		g_free (base);
		base = next;
		if (!state_exists (self, base))
			return NULL;
	}
	return g_steal_pointer (&base);
}

/* How a transition of @owner should name @target: the bare name when the
 * engine's rule finds it, else the absolute path. */
static gchar *
target_text (LrgEditorFsmEditor *self,
             const gchar        *owner,
             const gchar        *target)
{
	g_autofree gchar *r = resolve_target (self, owner, state_name (target));

	if (g_strcmp0 (r, target) == 0)
		return g_strdup (state_name (target));
	return g_strconcat ("/", target, NULL);
}

/* "a/b" is inside (or equal to) "a". */
static gboolean
state_under (const gchar *path,
             const gchar *ancestor)
{
	gsize n = strlen (ancestor);

	if (n == 0)
		return TRUE;
	return strncmp (path, ancestor, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

/* The shown-level card a state lies under, NULL when outside the level. */
static gchar *
card_of_state (LrgEditorFsmEditor *self,
               const gchar        *path)
{
	gsize  n = strlen (self->shown);
	const gchar *rest;
	const gchar *slash;

	if (path == NULL || !state_under (path, self->shown) || g_strcmp0 (path, self->shown) == 0)
		return NULL;
	rest = n == 0 ? path : path + n + 1;
	slash = strchr (rest, '/');
	return slash != NULL ? g_strndup (rest, slash - rest) : g_strdup (rest);
}

/* --- groups ----------------------------------------------------------- */

static gboolean
close_group_idle (gpointer user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);

	self->group_idle = 0;
	if (self->group_open)
	{
		self->group_open = FALSE;
		lrg_editor_document_end_group (doc_of (self));
	}
	return G_SOURCE_REMOVE;
}

static void
flush_group (LrgEditorFsmEditor *self)
{
	g_clear_handle_id (&self->group_idle, g_source_remove);
	if (self->group_open)
	{
		self->group_open = FALSE;
		lrg_editor_document_end_group (doc_of (self));
	}
}

static void
canvas_group (LrgEditorFsmEditor *self,
              const gchar        *label)
{
	if (!self->group_open)
	{
		lrg_editor_document_begin_group (doc_of (self), label);
		self->group_open = TRUE;
	}
	if (self->group_idle == 0)
		self->group_idle = g_idle_add (close_group_idle, self);
}

static gboolean
edit_begin (LrgEditorFsmEditor *self,
            const gchar        *label)
{
	if (self->group_open)
		return FALSE;
	lrg_editor_document_begin_group (doc_of (self), label);
	return TRUE;
}

static void
edit_end (LrgEditorFsmEditor *self,
          gboolean            opened)
{
	if (opened)
		lrg_editor_document_end_group (doc_of (self));
}

/* ==========================================================================
 * Parsing and positions
 * ========================================================================== */

static void
load_machine (LrgEditorFsmEditor *self)
{
	g_autofree gchar *text = lrg_editor_document_to_data (doc_of (self), NULL);
	g_autoptr(GError) error = NULL;

	g_clear_object (&self->machine);
	g_clear_pointer (&self->load_error, g_free);
	self->machine = lrg_logic_machine_new_from_data (text, -1, &error);
	if (self->machine == NULL)
		self->load_error = g_strdup (error != NULL ? error->message : "the machine does not load");
}

static gboolean
read_pos (LrgEditorFsmEditor *self,
          const gchar        *key,
          gfloat             *x,
          gfloat             *y)
{
	g_autofree gchar *p = pj (POS_ROOT, key);
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

static gboolean
write_pos (LrgEditorFsmEditor *self,
           const gchar        *key,
           gfloat              x,
           gfloat              y)
{
	g_autofree gchar *sx = editor_ai_num_str (rint (x), FALSE);
	g_autofree gchar *sy = editor_ai_num_str (rint (y), FALSE);
	g_autofree gchar *text = g_strdup_printf ("[%s, %s]", sx, sy);
	g_autofree gchar *p = pj (POS_ROOT, key);

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
	return lrg_editor_document_add_key (doc_of (self), POS_ROOT, key, text, -1, NULL);
}

/* Moves (prefix-renames) or drops the saved positions of a state and its
 * descendants; @to NULL drops. */
static void
rekey_positions (LrgEditorFsmEditor *self,
                 const gchar        *from,
                 const gchar        *to)
{
	g_auto(GStrv) keys = lrg_editor_document_get_keys (doc_of (self), POS_ROOT);
	guint         i;

	for (i = 0; keys != NULL && keys[i] != NULL; i++)
	{
		g_autofree gchar *p = NULL;
		gfloat            x, y;

		if (!state_under (keys[i], from))
			continue;
		p = pj (POS_ROOT, keys[i]);
		if (to != NULL && read_pos (self, keys[i], &x, &y))
		{
			g_autofree gchar *nk = g_strconcat (to, keys[i] + strlen (from), NULL);

			lrg_editor_document_remove (doc_of (self), p, NULL);
			write_pos (self, nk, x, y);
		}
		else
			lrg_editor_document_remove (doc_of (self), p, NULL);
	}
}

static void
card_pos (LrgEditorFsmEditor *self,
          const gchar        *state,
          guint               index,
          gfloat             *x,
          gfloat             *y)
{
	if (read_pos (self, state, x, y))
		return;
	*x = (gfloat) (index % GRID_COLS) * GRID_DX;
	*y = (gfloat) (index / GRID_COLS) * GRID_DY;
}

/* ==========================================================================
 * The canvas
 * ========================================================================== */

/* "{action: shout}" -> "shout", "{set: mode, value: calm}" -> "mode = calm". */
static gchar *
action_brief (LrgEditorFsmEditor *self,
              const gchar        *path)
{
	const gchar *v;

	if ((v = scalar_at (self, path, "action")) != NULL)
		return g_strdup (v);
	if ((v = scalar_at (self, path, "script")) != NULL)
		return g_strdup_printf ("%s()", v);
	if ((v = scalar_at (self, path, "set")) != NULL)
	{
		const gchar *val = scalar_at (self, path, "value");

		return g_strdup_printf ("%s=%s", v, val != NULL ? val : "?");
	}
	if ((v = scalar_at (self, path, "event")) != NULL)
		return g_strdup_printf ("!%s", v);
	return g_strdup ("?");
}

static gchar *
phase_line (LrgEditorFsmEditor *self,
            const gchar        *yp,
            const gchar        *phase)
{
	g_autofree gchar *list = pj (yp, phase);
	gint              n = n_items (self, list);
	GString          *s;
	gint              i;

	if (n == 0)
		return NULL;
	s = g_string_new (phase);
	g_string_append (s, ":");
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *ap = pji (list, (guint) i);
		g_autofree gchar *b = action_brief (self, ap);

		g_string_append_printf (s, " %s", b);
	}
	return g_string_free (s, FALSE);
}

/* A state card's body: children, then the actions. */
static gchar *
state_detail (LrgEditorFsmEditor *self,
              const gchar        *path)
{
	g_autofree gchar     *yp = state_yaml (self, path);
	g_autoptr(GPtrArray)  kids = child_names (self, path);
	GString              *s = g_string_new (NULL);
	guint                 i;

	if (kids->len > 0)
		g_string_append_printf (s, "\xe2\x96\xb8 %u sub-states", kids->len);
	for (i = 0; yp != NULL && i < G_N_ELEMENTS (phase_keys); i++)
	{
		g_autofree gchar *line = phase_line (self, yp, phase_keys[i]);

		if (line != NULL)
			g_string_append_printf (s, "%s%s", s->len > 0 ? "\n" : "", line);
	}
	if (s->len == 0)
		g_string_append (s, "(no actions)");
	return g_string_free (s, FALSE);
}

/* A wire's short label for owner cards ("hurt \xe2\x86\x92 alert"). */
static gchar *
transition_brief (LrgEditorFsmEditor *self,
                  const gchar        *tp)
{
	const gchar      *to = scalar_at (self, tp, "to");
	const gchar      *ev = scalar_at (self, tp, "event");
	const gchar      *after = scalar_at (self, tp, "after");
	g_autofree gchar *wp = pj (tp, "when");
	gint              nw = n_items (self, wp);
	GString          *s = g_string_new (NULL);

	if (ev != NULL)
		g_string_append_printf (s, "on %s ", ev);
	if (after != NULL)
		g_string_append_printf (s, "after %ss ", after);
	if (nw > 0)
		g_string_append_printf (s, "when %d condition%s ", nw, nw == 1 ? "" : "s");
	g_string_append_printf (s, "\xe2\x86\x92 %s", to != NULL ? to : "?");
	return g_string_free (s, FALSE);
}

static void
add_wire (LrgEditorFsmEditor *self,
          const gchar        *from_card,
          const gchar        *to_card,
          const gchar        *owner,
          guint               index)
{
	Wire *w = g_new0 (Wire, 1);

	w->from_card = g_strdup (from_card);
	w->to_card = g_strdup (to_card);
	w->owner = g_strdup (owner);
	w->index = index;
	g_ptr_array_add (self->wires, w);
}

static LrgGraphNode *
make_card (LrgEditorFsmEditor *self,
           const gchar        *id,
           const gchar        *title,
           const gchar        *category,
           const gchar        *icon,
           const gchar        *body_text,
           LrgThemeColor       role,
           gboolean            input,
           gboolean            output,
           guint               lines)
{
	LrgGraphNode *card = lrg_graph_node_new (id, title);
	LrgGraphPort *port;
	LrgWidget    *body;

	lrg_graph_node_set_category (card, category);
	lrg_graph_node_set_icon (card, icon);
	lrg_graph_node_set_min_width (card, CARD_W);
	if (input)
	{
		port = lrg_graph_node_add_port (card, LRG_GRAPH_PORT_INPUT, "in", "state");
		lrg_graph_port_set_multi_link (port, TRUE);
	}
	if (output)
	{
		port = lrg_graph_node_add_port (card, LRG_GRAPH_PORT_OUTPUT, "out", "state");
		lrg_graph_port_set_multi_link (port, TRUE);
	}
	body = editor_ai_card_body_new (body_text, role, CARD_W - 20.0f, lines);
	g_hash_table_insert (self->bodies, g_strdup (id), body);
	lrg_graph_node_set_body (card, body);
	g_object_unref (body);
	return card;
}

static void
rebuild_canvas (LrgEditorFsmEditor *self)
{
	g_autofree gchar     *yp = state_yaml (self, self->shown);
	g_autoptr(GPtrArray)  kids = NULL;
	g_autofree gchar     *initial = NULL;
	g_autoptr(GHashTable) ext = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	gfloat                max_x = 0.0f, ext_y = 0.0f;
	gfloat                ox = -GRID_DX, oy = 0.0f;
	guint                 i;

	self->syncing = TRUE;
	self->canvas_stale = FALSE;
	g_clear_pointer (&self->live_sig, g_free);
	g_hash_table_remove_all (self->bodies);
	g_ptr_array_set_size (self->wires, 0);
	lrg_node_graph_clear (self->canvas);
	if (yp == NULL)
	{
		self->syncing = FALSE;
		return;
	}
	kids = child_names (self, self->shown);
	initial = g_strdup (scalar_at (self, yp, "initial"));
	if (initial == NULL && kids->len > 0)
		initial = g_strdup (g_ptr_array_index (kids, 0));

	/* the child states */
	for (i = 0; i < kids->len; i++)
	{
		const gchar             *name = g_ptr_array_index (kids, i);
		g_autofree gchar        *path = state_join (self->shown, name);
		g_autofree gchar        *detail = state_detail (self, path);
		g_autoptr(GPtrArray)     sub = child_names (self, path);
		g_autoptr(LrgGraphNode)  card = NULL;
		gfloat                   x, y;

		card = make_card (self, name, name, sub->len > 0 ? "machine" : "state",
		                  g_strcmp0 (name, initial) == 0 ? "star" : sub->len > 0 ? "menu" : "play", detail,
		                  LRG_THEME_COLOR_TEXT_MUTED, TRUE, TRUE, 5);
		card_pos (self, path, i, &x, &y);
		max_x = MAX (max_x, x);
		lrg_graph_node_set_position (card, x, y);
		lrg_node_graph_append_node (self->canvas, card);
	}

	/* the owner card: the shown state's own transitions */
	{
		g_autofree gchar        *title = *self->shown == '\0' ? g_strdup ("Any State")
		                                 : g_strdup_printf ("While in %s", state_name (self->shown));
		g_autofree gchar        *key = state_join (self->shown, OWNER);
		g_autoptr(LrgGraphNode)  card = make_card (self, OWNER, title, "any", "refresh",
		                                           *self->shown == '\0' ? "machine-wide transitions"
		                                                                : "outranks the children",
		                                           LRG_THEME_COLOR_TEXT_MUTED, FALSE, TRUE, 4);

		read_pos (self, key, &ox, &oy);
		lrg_graph_node_set_position (card, ox, oy);
		lrg_node_graph_append_node (self->canvas, card);
	}

	/* wires: every child's transitions, then the owner's */
	for (i = 0; i <= kids->len; i++)
	{
		gboolean          owner = i == kids->len;
		g_autofree gchar *path = owner ? g_strdup (self->shown) : state_join (self->shown, g_ptr_array_index (kids, i));
		g_autofree gchar *syp = state_yaml (self, path);
		g_autofree gchar *list = syp != NULL ? pj (syp, "transitions") : NULL;
		gint              n = list != NULL ? n_items (self, list) : 0;
		gint              k;

		for (k = 0; k < n; k++)
		{
			g_autofree gchar *tp = pji (list, (guint) k);
			const gchar      *to = scalar_at (self, tp, "to");
			g_autofree gchar *target = resolve_target (self, path, to);
			g_autofree gchar *to_card = target != NULL ? card_of_state (self, target) : NULL;
			const gchar      *from_card = owner ? OWNER : (const gchar *) g_ptr_array_index (kids, i);

			/* a target outside the level (or unknown): an exit stub */
			if (to_card == NULL || g_strcmp0 (target, self->shown) == 0)
			{
				g_free (to_card);
				to_card = g_strconcat (EXT_PREFIX, to != NULL ? to : "?", NULL);
				if (!g_hash_table_contains (ext, to_card))
				{
					g_autofree gchar        *title = g_strdup_printf ("\xe2\x86\x92 %s", to != NULL ? to : "?");
					g_autoptr(LrgGraphNode)  card = make_card (self, to_card, title, "exit", "arrow-right",
					                                           target != NULL ? "outside this level" : "unknown state",
					                                           target != NULL ? LRG_THEME_COLOR_TEXT_MUTED
					                                                          : LRG_THEME_COLOR_DANGER,
					                                           TRUE, FALSE, 1);

					lrg_graph_node_set_position (card, max_x + GRID_DX * 1.1f, ext_y);
					ext_y += GRID_DY * 0.7f;
					lrg_node_graph_append_node (self->canvas, card);
					g_hash_table_add (ext, g_strdup (to_card));
				}
			}
			add_wire (self, from_card, to_card, path, (guint) k);
			if (g_strcmp0 (from_card, to_card) != 0)
				lrg_node_graph_connect_by_name (self->canvas, from_card, "out", to_card, "in", NULL);
		}
	}

	/* the selection survives */
	if (self->sel_state != NULL)
	{
		g_autofree gchar *card_id = card_of_state (self, self->sel_state);
		LrgGraphNode     *card = card_id != NULL ? lrg_node_graph_get_node (self->canvas, card_id) : NULL;

		if (card != NULL)
			lrg_node_graph_select_node (self->canvas, card, FALSE);
	}
	else if (self->sel_tr)
	{
		guint w;

		for (w = 0; w < self->wires->len; w++)
		{
			Wire *wi = g_ptr_array_index (self->wires, w);

			if (g_strcmp0 (wi->owner, self->sel_from) == 0 && wi->index == self->sel_index)
			{
				LrgGraphNode *a = lrg_node_graph_get_node (self->canvas, wi->from_card);
				LrgGraphNode *b = lrg_node_graph_get_node (self->canvas, wi->to_card);
				LrgGraphLink *link = (a != NULL && b != NULL)
				                     ? lrg_node_graph_find_link (self->canvas,
				                                                 lrg_graph_node_find_port (a, LRG_GRAPH_PORT_OUTPUT, "out"),
				                                                 lrg_graph_node_find_port (b, LRG_GRAPH_PORT_INPUT, "in"))
				                     : NULL;

				if (link != NULL)
					lrg_node_graph_select_link (self->canvas, link);
				break;
			}
		}
	}
	self->syncing = FALSE;
}

/* --- canvas -> document ------------------------------------------------- */

static gboolean
is_state_card (const gchar *id)
{
	return id != NULL && *id != '@';
}

static gboolean
on_can_connect (LrgNodeGraph *graph,
                LrgGraphPort *output,
                LrgGraphPort *input,
                gpointer      user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	const gchar        *a = lrg_graph_node_get_id (lrg_graph_port_get_node (output));
	const gchar        *b = lrg_graph_node_get_id (lrg_graph_port_get_node (input));

	/* the canvas rebuild wires owner and exit cards too */
	if (self->syncing)
		return TRUE;
	return g_strcmp0 (a, b) != 0 && is_state_card (b) && (is_state_card (a) || g_strcmp0 (a, OWNER) == 0);
}

static gchar *
card_owner (LrgEditorFsmEditor *self,
            const gchar        *card)
{
	if (g_strcmp0 (card, OWNER) == 0)
		return g_strdup (self->shown);
	return state_join (self->shown, card);
}

static void
on_link_added (LrgNodeGraph *graph,
               LrgGraphLink *link,
               gpointer      user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	const gchar        *a = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_output (link)));
	const gchar        *b = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_input (link)));
	g_autofree gchar   *owner = NULL;
	g_autofree gchar   *target = NULL;
	g_autofree gchar   *to = NULL;
	g_autoptr(GError)   error = NULL;
	gint                index;

	if (self->syncing)
		return;
	owner = card_owner (self, a);
	target = state_join (self->shown, b);
	to = target_text (self, owner, target);
	self->writing = TRUE;
	canvas_group (self, "Add Transition");
	index = lrg_editor_fsm_editor_add_transition (self, owner, to, NULL, -1.0, &error);
	self->writing = FALSE;
	if (index < 0)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	else
	{
		g_clear_pointer (&self->sel_state, g_free);
		g_free (self->sel_from);
		self->sel_from = g_steal_pointer (&owner);
		self->sel_index = (guint) index;
		self->sel_tr = TRUE;
	}
	self->canvas_stale = TRUE;
	self->grid_stale = TRUE;
}

static void
on_link_removed (LrgNodeGraph *graph,
                 LrgGraphLink *link,
                 gpointer      user_data)
{
	LrgEditorFsmEditor   *self = LRG_EDITOR_FSM_EDITOR (user_data);
	const gchar          *a = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_output (link)));
	const gchar          *b = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_input (link)));
	g_autoptr(GPtrArray)  gone = NULL;
	gint                  i;

	if (self->syncing)
		return;
	/* a deleted wire takes every transition it stands for, last first */
	gone = g_ptr_array_new ();
	for (i = 0; i < (gint) self->wires->len; i++)
	{
		Wire *w = g_ptr_array_index (self->wires, i);

		if (g_strcmp0 (w->from_card, a) == 0 && g_strcmp0 (w->to_card, b) == 0)
			g_ptr_array_add (gone, w);
	}
	self->writing = TRUE;
	canvas_group (self, "Delete Transition");
	for (i = (gint) gone->len - 1; i >= 0; i--)
	{
		Wire *w = g_ptr_array_index (gone, i);

		lrg_editor_fsm_editor_remove_transition (self, w->owner, w->index, NULL);
	}
	self->writing = FALSE;
	self->sel_tr = FALSE;
	self->canvas_stale = TRUE;
	self->grid_stale = TRUE;
}

static void
on_node_moved (LrgNodeGraph *graph,
               LrgGraphNode *card,
               gpointer      user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	const gchar        *id = lrg_graph_node_get_id (card);
	g_autofree gchar   *key = NULL;

	if (self->syncing || g_str_has_prefix (id, EXT_PREFIX))
		return;
	key = g_strcmp0 (id, OWNER) == 0 ? state_join (self->shown, OWNER) : state_join (self->shown, id);
	self->writing = TRUE;
	canvas_group (self, "Move State");
	write_pos (self, key, lrg_graph_node_get_x (card), lrg_graph_node_get_y (card));
	self->writing = FALSE;
}

static void
on_node_removed (LrgNodeGraph *graph,
                 LrgGraphNode *card,
                 gpointer      user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	const gchar        *id = lrg_graph_node_get_id (card);
	g_autofree gchar   *path = NULL;
	g_autoptr(GError)   error = NULL;

	if (self->syncing)
		return;
	self->canvas_stale = TRUE;
	if (!is_state_card (id))
		return;
	path = state_join (self->shown, id);
	if (!state_exists (self, path))
		return;
	self->writing = TRUE;
	canvas_group (self, "Delete State");
	if (!lrg_editor_fsm_editor_remove_state (self, path, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	self->writing = FALSE;
	self->grid_stale = TRUE;
}

/* A free child name of @parent from @base ("state", "state2", ...). */
static gchar *
free_name (LrgEditorFsmEditor *self,
           const gchar        *parent,
           const gchar        *base)
{
	g_autoptr(GPtrArray) kids = child_names (self, parent);
	guint                n, i;

	for (n = 1; n < 10000; n++)
	{
		gchar   *cand = n == 1 ? g_strdup (base) : g_strdup_printf ("%s%u", base, n);
		gboolean taken = FALSE;

		for (i = 0; i < kids->len && !taken; i++)
			taken = g_strcmp0 (g_ptr_array_index (kids, i), cand) == 0;
		if (!taken)
			return cand;
		g_free (cand);
	}
	return g_strdup_printf ("%s_%" G_GINT64_FORMAT, base, g_get_monotonic_time ());
}

static void
on_node_duplicated (LrgNodeGraph *graph,
                    LrgGraphNode *original,
                    LrgGraphNode *copy,
                    gpointer      user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	const gchar        *id = lrg_graph_node_get_id (original);
	g_autofree gchar   *path = NULL;
	g_autofree gchar   *yp = NULL;
	g_autofree gchar   *pyp = NULL;
	g_autofree gchar   *yaml = NULL;
	g_autofree gchar   *base = NULL;
	g_autofree gchar   *name = NULL;
	g_autofree gchar   *list = NULL;
	gint                n;

	self->canvas_stale = TRUE;
	if (self->syncing || !is_state_card (id))
		return;
	path = state_join (self->shown, id);
	yp = state_yaml (self, path);
	pyp = state_yaml (self, self->shown);
	if (yp == NULL || pyp == NULL)
		return;
	yaml = lrg_editor_document_get_yaml (doc_of (self), yp);
	base = g_strconcat (id, "_copy", NULL);
	name = free_name (self, self->shown, base);
	list = pj (pyp, "states");
	self->writing = TRUE;
	canvas_group (self, "Duplicate State");
	n = n_items (self, list);
	if (yaml != NULL && lrg_editor_document_insert_item (doc_of (self), list, -1, yaml, NULL))
	{
		g_autofree gchar *np = pji (list, (guint) n);
		g_autofree gchar *namep = pj (np, "name");
		g_autofree gchar *npath = state_join (self->shown, name);

		lrg_editor_document_set_string (doc_of (self), namep, name, 0, NULL);
		write_pos (self, npath, lrg_graph_node_get_x (copy), lrg_graph_node_get_y (copy));
	}
	self->writing = FALSE;
}

static void
on_link_dropped (LrgNodeGraph *graph,
                 LrgGraphPort *port,
                 gfloat        x,
                 gfloat        y,
                 gpointer      user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	const gchar        *from = lrg_graph_node_get_id (lrg_graph_port_get_node (port));
	g_autofree gchar   *owner = NULL;
	g_autofree gchar   *path = NULL;
	g_autoptr(GError)   error = NULL;
	gboolean            opened;

	if (self->syncing || lrg_graph_port_get_direction (port) != LRG_GRAPH_PORT_OUTPUT ||
	    g_str_has_prefix (from, EXT_PREFIX))
		return;
	owner = card_owner (self, from);
	opened = edit_begin (self, "Add State");
	path = lrg_editor_fsm_editor_add_state (self, self->shown, NULL, x, y, &error);
	if (path != NULL)
	{
		g_autofree gchar *to = target_text (self, owner, path);

		lrg_editor_fsm_editor_add_transition (self, owner, to, NULL, -1.0, NULL);
	}
	edit_end (self, opened);
	if (path == NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	else
		lrg_editor_fsm_editor_select_state (self, path);
}

static void
on_node_activated (LrgNodeGraph *graph,
                   LrgGraphNode *card,
                   gpointer      user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	const gchar        *id = lrg_graph_node_get_id (card);

	if (g_strcmp0 (id, OWNER) == 0)
		lrg_editor_fsm_editor_go_up (self);
	else if (g_str_has_prefix (id, EXT_PREFIX))
	{
		/* jump to the target's level and select it */
		guint w;

		for (w = 0; w < self->wires->len; w++)
		{
			Wire             *wi = g_ptr_array_index (self->wires, w);
			g_autofree gchar *tp = NULL;
			g_autofree gchar *target = NULL;

			if (g_strcmp0 (wi->to_card, id) != 0)
				continue;
			{
				g_autofree gchar *syp = state_yaml (self, wi->owner);
				g_autofree gchar *list = syp != NULL ? pj (syp, "transitions") : NULL;

				tp = list != NULL ? pji (list, wi->index) : NULL;
			}
			target = tp != NULL ? resolve_target (self, wi->owner, scalar_at (self, tp, "to")) : NULL;
			if (target != NULL)
				lrg_editor_fsm_editor_select_state (self, target);
			break;
		}
	}
	else
	{
		g_autofree gchar *path = state_join (self->shown, id);

		lrg_editor_fsm_editor_enter (self, path);
	}
}

static void
on_canvas_selection (LrgNodeGraph *graph,
                     gpointer      user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	g_autoptr(GList)    sel = NULL;
	LrgGraphLink       *link;
	const gchar        *id = NULL;

	if (self->syncing)
		return;
	sel = lrg_node_graph_get_selected_nodes (graph);
	link = lrg_node_graph_get_selected_link (graph);
	if (sel != NULL && sel->next == NULL)
		id = lrg_graph_node_get_id (sel->data);
	g_clear_pointer (&self->sel_state, g_free);
	self->sel_tr = FALSE;
	if (is_state_card (id))
		self->sel_state = state_join (self->shown, id);
	else if (id == NULL && link != NULL)
	{
		const gchar *a = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_output (link)));
		const gchar *b = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_input (link)));
		guint        w;

		for (w = 0; w < self->wires->len; w++)
		{
			Wire *wi = g_ptr_array_index (self->wires, w);

			if (g_strcmp0 (wi->from_card, a) == 0 && g_strcmp0 (wi->to_card, b) == 0)
			{
				g_free (self->sel_from);
				self->sel_from = g_strdup (wi->owner);
				self->sel_index = wi->index;
				self->sel_tr = TRUE;
				break;
			}
		}
	}
	self->grid_stale = TRUE;
}

static void
on_crumb (LrgBreadcrumb *crumbs,
          guint          index,
          gpointer       user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	g_auto(GStrv)       parts = NULL;
	g_autofree gchar   *path = NULL;

	if (self->syncing)
		return;
	/* segment 0 is the machine; segment k the k-th level down */
	parts = *self->shown != '\0' ? g_strsplit (self->shown, "/", -1) : g_new0 (gchar *, 1);
	if (index < g_strv_length (parts))
	{
		gchar *save = parts[index];

		parts[index] = NULL;
		path = g_strjoinv ("/", parts);
		parts[index] = save;
		lrg_editor_fsm_editor_enter (self, path);
	}
}

static void
rebuild_crumbs (LrgEditorFsmEditor *self)
{
	g_auto(GStrv) crumbs = lrg_editor_fsm_editor_dup_breadcrumbs (self);
	guint         i;

	self->syncing = TRUE;
	lrg_breadcrumb_remove_all (self->crumbs);
	for (i = 0; crumbs != NULL && crumbs[i] != NULL; i++)
		lrg_breadcrumb_append (self->crumbs, crumbs[i]);
	self->syncing = FALSE;
}

/* ==========================================================================
 * Live state
 * ========================================================================== */

static void
update_live (LrgEditorFsmEditor *self)
{
	LrgLogicMachine  *m = lrg_editor_fsm_editor_get_live_machine (self);
	LrgTheme         *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	g_autofree gchar *sig = NULL;
	g_auto(GStrv)     active = NULL;
	const gchar      *innermost = NULL;
	const gchar      *lf = NULL, *lt = NULL, *le = NULL;
	g_autofree gchar *from_card = NULL;
	g_autofree gchar *to_card = NULL;
	guint             i, n;

	sig = m != NULL ? g_strdup_printf ("%p:%" G_GUINT64_FORMAT ":%s", (gpointer) m, lrg_logic_machine_get_update_count (m),
	                                   self->shown)
	                : g_strdup ("-");
	if (g_strcmp0 (sig, self->live_sig) == 0)
		return;
	g_free (self->live_sig);
	self->live_sig = g_steal_pointer (&sig);

	if (m != NULL && lrg_logic_machine_is_running (m))
	{
		active = lrg_logic_machine_dup_active_path (m);
		innermost = lrg_logic_machine_get_active_state (m);
		if (lrg_logic_machine_get_last_transition (m, &lf, &lt, &le) != NULL)
		{
			from_card = card_of_state (self, lf);
			to_card = card_of_state (self, lt);
			if (from_card == NULL)
				from_card = g_strdup (OWNER);
		}
	}
	n = lrg_node_graph_get_n_nodes (self->canvas);
	for (i = 0; i < n; i++)
	{
		LrgGraphNode     *card = lrg_node_graph_get_nth_node (self->canvas, i);
		const gchar      *id = lrg_graph_node_get_id (card);
		LrgWidget        *body = g_hash_table_lookup (self->bodies, id);
		g_autofree gchar *path = NULL;
		g_autofree gchar *detail = NULL;
		GString          *text;
		gboolean          on = FALSE;

		if (!is_state_card (id))
		{
			/* the owner card lists how often its transitions fired */
			if (g_strcmp0 (id, OWNER) == 0 && body != NULL)
			{
				g_autoptr(GString) s = g_string_new (*self->shown == '\0' ? "machine-wide transitions"
				                                                          : "outranks the children");
				LrgLogicState     *st = m != NULL ? lrg_logic_machine_find_state (m, self->shown) : NULL;
				guint              k;

				for (k = 0; st != NULL && k < lrg_logic_state_get_transition_count (st) && k < 3; k++)
				{
					LrgLogicTransition *t = lrg_logic_state_get_transition (st, k);

					g_string_append_printf (s, "\n\xe2\x86\x92 %s \xc3\x97%" G_GUINT64_FORMAT,
					                        lrg_logic_transition_get_target (t),
					                        lrg_logic_transition_get_fire_count (t));
				}
				editor_ai_card_body_set (body, s->str, LRG_THEME_COLOR_TEXT_MUTED);
			}
			continue;
		}
		path = state_join (self->shown, id);
		on = active != NULL && g_strv_contains ((const gchar * const *) active, path);
		if (on && g_strcmp0 (path, innermost) == 0)
			lrg_graph_node_set_accent (card, lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS));
		else if (on)
			lrg_graph_node_set_accent (card, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		else
			lrg_graph_node_set_accent (card, NULL);
		if (body == NULL)
			continue;
		detail = state_detail (self, path);
		text = g_string_new (NULL);
		if (on)
			g_string_append_printf (text, "\xe2\x97\x8f active %.2f s\n", lrg_logic_machine_get_state_time (m, path));
		g_string_append (text, detail);
		if (m != NULL)
		{
			/* this state's transitions and their fire counts */
			LrgLogicState *st = lrg_logic_machine_find_state (m, path);
			guint          k;

			for (k = 0; st != NULL && k < lrg_logic_state_get_transition_count (st) && k < 2; k++)
			{
				LrgLogicTransition *t = lrg_logic_state_get_transition (st, k);

				g_string_append_printf (text, "\n\xe2\x86\x92 %s \xc3\x97%" G_GUINT64_FORMAT,
				                        lrg_logic_transition_get_target (t), lrg_logic_transition_get_fire_count (t));
			}
		}
		editor_ai_card_body_set (body, text->str, on ? LRG_THEME_COLOR_SUCCESS : LRG_THEME_COLOR_TEXT_MUTED);
		g_string_free (text, TRUE);
	}
	{
		g_autoptr(GList) links = lrg_node_graph_get_links (self->canvas);
		GList           *l;

		for (l = links; l != NULL; l = l->next)
		{
			LrgGraphLink *link = l->data;
			const gchar  *a = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_output (link)));
			const gchar  *b = lrg_graph_node_get_id (lrg_graph_port_get_node (lrg_graph_link_get_input (link)));

			lrg_graph_link_set_color (link, (to_card != NULL && g_strcmp0 (a, from_card) == 0 &&
			                                 g_strcmp0 (b, to_card) == 0)
			                                ? lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS) : NULL);
		}
	}
	{
		g_autofree gchar *line = NULL;

		if (m != NULL)
		{
			LrgAiComponent   *c = lrg_editor_ai_debug_panel_find_for_file (app_of (self),
			                        lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)), TRUE);
			g_autofree gchar *who = c != NULL ? editor_ai_dup_display_name (app_of (self), c) : NULL;

			line = g_strdup_printf ("\xe2\x97\x8f live \xc2\xb7 %s \xc2\xb7 %s \xc2\xb7 update %" G_GUINT64_FORMAT,
			                        who != NULL ? who : "?", innermost != NULL ? innermost : "(stopped)",
			                        lrg_logic_machine_get_update_count (m));
		}
		else
			line = g_strdup (app_of (self) != NULL && lrg_editor_app_is_playing (app_of (self))
			                 ? "playing \xc2\xb7 no entity runs this machine" : "");
		lrg_label_set_text (LRG_LABEL (self->live_label), line);
	}
}

/* ==========================================================================
 * Inspector
 * ========================================================================== */

static gchar *
sel_tr_path (LrgEditorFsmEditor *self)
{
	g_autofree gchar *syp = NULL;
	g_autofree gchar *list = NULL;
	gchar            *tp;

	if (!self->sel_tr)
		return NULL;
	syp = state_yaml (self, self->sel_from);
	if (syp == NULL)
		return NULL;
	list = pj (syp, "transitions");
	tp = pji (list, self->sel_index);
	if (!lrg_editor_document_has_path (doc_of (self), tp))
	{
		g_free (tp);
		return NULL;
	}
	return tp;
}

static gboolean
source_read (LrgEditorDataSource *source,
             const gchar         *name,
             GValue              *value,
             gpointer             user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);

	if (g_strcmp0 (name, "state-name") == 0 && self->sel_state != NULL)
	{
		g_value_set_string (value, state_name (self->sel_state));
		return TRUE;
	}
	if (g_strcmp0 (name, "state-initial") == 0 && self->sel_state != NULL)
	{
		g_autofree gchar     *parent = state_parent (self->sel_state);
		g_autofree gchar     *pyp = state_yaml (self, parent);
		const gchar          *init = pyp != NULL ? scalar_at (self, pyp, "initial") : NULL;
		g_autoptr(GPtrArray)  kids = child_names (self, parent);

		if (init == NULL && kids->len > 0)
			init = g_ptr_array_index (kids, 0);
		g_value_set_boolean (value, g_strcmp0 (init, state_name (self->sel_state)) == 0);
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
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	g_autoptr(GError)   error = NULL;

	if (g_strcmp0 (name, "state-name") == 0 && self->sel_state != NULL)
	{
		g_autofree gchar *keep = g_strdup (self->sel_state);
		g_autofree gchar *np = lrg_editor_fsm_editor_rename_state (self, keep, g_value_get_string (value), &error);

		if (np == NULL)
			lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return FALSE;
	}
	if (g_strcmp0 (name, "state-initial") == 0 && self->sel_state != NULL)
	{
		if (g_value_get_boolean (value) &&
		    !lrg_editor_fsm_editor_set_initial (self, self->sel_state, &error))
			lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return FALSE;
	}
	if (g_strcmp0 (name, "tr-event") == 0)
	{
		const gchar *v = g_value_get_string (value);

		/* an empty event makes the transition event-less */
		if (v == NULL || *v == '\0')
		{
			lrg_editor_fsm_editor_set_transition_field (self, self->sel_from, self->sel_index, "event", NULL, NULL);
			return FALSE;
		}
	}
	return TRUE;
}

static void
add_field (LrgEditorFsmEditor *self,
           const gchar        *name,
           const gchar        *path,
           GType               type,
           const gchar        *label,
           const gchar        *group,
           const gchar        *hint,
           const gchar        *def)
{
	lrg_editor_data_source_add_field (self->source, name, path, type, label, group, hint, def);
}

/* --- entry lists (actions, conditions): one line of YAML per entry ------- */

static void
on_entry_submitted (LrgTextInput *input,
                    gpointer      user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	const gchar        *path = g_object_get_data (G_OBJECT (input), "path");
	g_autoptr(GError)   error = NULL;

	if (self->syncing || path == NULL)
		return;
	if (!lrg_editor_document_replace (doc_of (self), path, lrg_text_input_get_text (input), &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	self->rows_stale = TRUE;
	self->canvas_stale = TRUE;
}

static void
on_entry_remove (LrgButton *button,
                 gpointer   user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	const gchar        *path = g_object_get_data (G_OBJECT (button), "path");
	g_autofree gchar   *list = g_path_get_dirname (path);
	gboolean            opened = edit_begin (self, "Remove Entry");

	lrg_editor_document_remove (doc_of (self), path, NULL);
	/* an empty list goes: the file stays as the engine writes it */
	if (lrg_editor_document_get_length (doc_of (self), list) == 0)
		lrg_editor_document_remove (doc_of (self), list, NULL);
	edit_end (self, opened);
	self->rows_stale = TRUE;
	self->canvas_stale = TRUE;
}

static void
on_entry_up (LrgButton *button,
             gpointer   user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	const gchar        *path = g_object_get_data (G_OBJECT (button), "path");
	g_autofree gchar   *list = g_path_get_dirname (path);
	g_autofree gchar   *base = g_path_get_basename (path);
	guint               i = (guint) g_ascii_strtoull (base, NULL, 10);

	if (i > 0)
		lrg_editor_document_move_item (doc_of (self), list, i, i - 1, NULL);
	self->rows_stale = TRUE;
	self->canvas_stale = TRUE;
}

static void
on_entry_add (LrgButton *button,
              gpointer   user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	const gchar        *owner = g_object_get_data (G_OBJECT (button), "owner");
	const gchar        *key = g_object_get_data (G_OBJECT (button), "key");
	const gchar        *tmpl = g_object_get_data (G_OBJECT (button), "template");
	g_autofree gchar   *list = pj (owner, key);
	gboolean            opened = edit_begin (self, "Add Entry");

	if (lrg_editor_document_get_kind (doc_of (self), list) != LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
	{
		if (lrg_editor_document_has_path (doc_of (self), list))
			lrg_editor_document_remove (doc_of (self), list, NULL);
		lrg_editor_document_add_key (doc_of (self), owner, key, "[]", -1, NULL);
	}
	lrg_editor_document_insert_item (doc_of (self), list, -1, tmpl, NULL);
	edit_end (self, opened);
	self->rows_stale = TRUE;
	self->canvas_stale = TRUE;
}

/* One editable list: a heading, one row per entry, a row of add buttons. */
static void
entry_list (LrgEditorFsmEditor *self,
            const gchar        *owner,
            const gchar        *key,
            const gchar        *heading,
            gboolean            conditions)
{
	static const gchar * const action_tmpl[][2] = {
		{ "+ action", "{action: name}" }, { "+ set", "{set: key, value: 0}" }, { "+ event", "{event: name}" },
		{ "+ script", "{script: name}" }
	};
	static const gchar * const cond_tmpl[][2] = {
		{ "+ key", "{key: key, op: eq, value: 0}" }, { "+ condition", "{condition: name}" },
		{ "+ script", "{script: name}" }
	};
	g_autofree gchar *list = pj (owner, key);
	gint              n = n_items (self, list);
	gint              i;
	LrgWidget        *adds = LRG_WIDGET (lrg_wrap_box_new ());
	guint             k, nt = conditions ? G_N_ELEMENTS (cond_tmpl) : G_N_ELEMENTS (action_tmpl);

	editor_ai_add (self->lists, editor_ai_heading (heading));
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *ip = pji (list, (guint) i);
		g_autofree gchar *text = editor_ai_flow_yaml (doc_of (self), ip);
		g_autofree gchar *rname = g_strdup_printf ("fsm-%s-%d", key, i);
		g_autofree gchar *iname = g_strdup_printf ("fsm-%s-%d-text", key, i);
		g_autofree gchar *uname = g_strdup_printf ("fsm-%s-%d-up", key, i);
		g_autofree gchar *xname = g_strdup_printf ("fsm-%s-%d-remove", key, i);
		LrgWidget        *row = LRG_WIDGET (lrg_hbox_new ());
		LrgTextInput     *input = lrg_text_input_new ();
		LrgWidget        *b;

		lrg_widget_set_name (row, rname);
		lrg_container_set_spacing (LRG_CONTAINER (row), 3.0f);
		lrg_widget_set_name (LRG_WIDGET (input), iname);
		lrg_widget_set_fill_weight (LRG_WIDGET (input), 1.0f);
		lrg_text_input_set_font_size (input, 14.0f);
		lrg_text_input_set_text (input, text);
		g_object_set_data_full (G_OBJECT (input), "path", g_strdup (ip), g_free);
		g_signal_connect (input, "submitted", G_CALLBACK (on_entry_submitted), self);
		editor_ai_add (row, LRG_WIDGET (input));
		b = editor_ai_button (uname, "\xe2\x86\x91", G_CALLBACK (on_entry_up), self);
		g_object_set_data_full (G_OBJECT (b), "path", g_strdup (ip), g_free);
		editor_ai_add (row, b);
		b = editor_ai_button (xname, "\xc3\x97", G_CALLBACK (on_entry_remove), self);
		g_object_set_data_full (G_OBJECT (b), "path", g_strdup (ip), g_free);
		editor_ai_add (row, b);
		editor_ai_add (self->lists, row);
	}
	lrg_container_set_spacing (LRG_CONTAINER (adds), 3.0f);
	for (k = 0; k < nt; k++)
	{
		const gchar      *label = conditions ? cond_tmpl[k][0] : action_tmpl[k][0];
		const gchar      *tmpl = conditions ? cond_tmpl[k][1] : action_tmpl[k][1];
		g_autofree gchar *bname = g_strdup_printf ("fsm-%s-add-%s", key, label + 2);
		LrgWidget        *b = editor_ai_button (bname, label, G_CALLBACK (on_entry_add), self);

		g_object_set_data_full (G_OBJECT (b), "owner", g_strdup (owner), g_free);
		g_object_set_data_full (G_OBJECT (b), "key", g_strdup (key), g_free);
		g_object_set_data_full (G_OBJECT (b), "template", g_strdup (tmpl), g_free);
		editor_ai_add (adds, b);
	}
	editor_ai_add (self->lists, adds);
}

static void
on_tr_row (LrgButton *button,
           gpointer   user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);

	lrg_editor_fsm_editor_select_transition (self, g_object_get_data (G_OBJECT (button), "owner"),
	                                         GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (button), "index")));
}

/* The owner's transitions as buttons that select them. */
static void
transition_rows (LrgEditorFsmEditor *self,
                 const gchar        *owner)
{
	g_autofree gchar *syp = state_yaml (self, owner);
	g_autofree gchar *list = syp != NULL ? pj (syp, "transitions") : NULL;
	gint              n = list != NULL ? n_items (self, list) : 0;
	gint              i;

	editor_ai_add (self->lists, editor_ai_heading ("TRANSITIONS (priority order; wire a card to add)"));
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *tp = pji (list, (guint) i);
		g_autofree gchar *text = transition_brief (self, tp);
		g_autofree gchar *bname = g_strdup_printf ("fsm-tr-%d", i);
		LrgWidget        *b = editor_ai_button (bname, text, G_CALLBACK (on_tr_row), self);

		g_object_set_data_full (G_OBJECT (b), "owner", g_strdup (owner), g_free);
		g_object_set_data (G_OBJECT (b), "index", GUINT_TO_POINTER ((guint) i));
		editor_ai_add (self->lists, b);
	}
	if (n == 0)
		editor_ai_add (self->lists, editor_ai_heading ("(none)"));
}

static gchar *
grid_signature (LrgEditorFsmEditor *self)
{
	g_autofree gchar *tp = sel_tr_path (self);

	return g_strdup_printf ("%s|%s|%d|%s|%u|%s", self->shown, self->sel_state, self->sel_tr, self->sel_from,
	                        self->sel_index, tp);
}

static void
rebuild_grid (LrgEditorFsmEditor *self)
{
	g_autofree gchar *title = NULL;
	g_autofree gchar *tp = sel_tr_path (self);
	g_autofree gchar *syp = self->sel_state != NULL ? state_yaml (self, self->sel_state) : NULL;

	self->grid_stale = FALSE;
	lrg_editor_data_source_clear (self->source);
	if (syp != NULL)
	{
		g_autofree gchar *np = pj (syp, "name");

		add_field (self, "state-name", np, G_TYPE_STRING, "Name", "State", NULL, NULL);
		add_field (self, "state-initial", np, G_TYPE_BOOLEAN, "Initial", "State", NULL, "false");
		title = g_strdup_printf ("State \xc2\xb7 %s", self->sel_state);
	}
	else if (tp != NULL)
	{
		g_autofree gchar *to = pj (tp, "to");
		g_autofree gchar *ev = pj (tp, "event");
		g_autofree gchar *af = pj (tp, "after");

		add_field (self, "tr-to", to, G_TYPE_STRING, "To", "Transition", NULL, NULL);
		add_field (self, "tr-event", ev, G_TYPE_STRING, "Event", "Transition", NULL, "");
		add_field (self, "tr-after", af, G_TYPE_DOUBLE, "After (s)", "Transition", "range:0,3600,0.05", "0");
		title = g_strdup_printf ("Transition \xc2\xb7 %s #%u", *self->sel_from != '\0' ? self->sel_from : "any",
		                         self->sel_index);
	}
	else
	{
		g_autofree gchar *lyp = state_yaml (self, self->shown);
		g_autofree gchar *ip = lyp != NULL ? pj (lyp, "initial") : NULL;
		g_autoptr(GPtrArray) kids = child_names (self, self->shown);
		g_autofree gchar *hint = NULL;

		g_ptr_array_add (kids, NULL);
		hint = kids->len > 1 ? g_strconcat ("enum-strings:", g_strjoinv (",", (gchar **) kids->pdata), NULL) : NULL;
		if (*self->shown == '\0')
			add_field (self, "machine-name", "/name", G_TYPE_STRING, "Name", "Machine", NULL, "");
		if (ip != NULL)
			add_field (self, "level-initial", ip, G_TYPE_STRING, "Initial", "Level", hint, NULL);
		title = *self->shown == '\0' ? g_strdup ("Machine") : g_strdup_printf ("Inside %s", self->shown);
	}
	lrg_label_set_text (LRG_LABEL (self->inspector_title), title);
	g_free (self->grid_sig);
	self->grid_sig = grid_signature (self);
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_property_grid_rebuild (self->grid);
	self->rows_stale = TRUE;
}

/* --- blackboard rows --------------------------------------------------- */

static void
on_key_default (LrgTextInput *input,
                gpointer      user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	const gchar        *key = g_object_get_data (G_OBJECT (input), "key");
	const gchar        *text = lrg_text_input_get_text (input);
	gint                n = n_items (self, "/blackboard");
	gint                i;

	if (self->syncing)
		return;
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *kp = pji ("/blackboard", (guint) i);
		g_autofree gchar *dp = NULL;

		if (g_strcmp0 (scalar_at (self, kp, "name"), key) != 0)
			continue;
		dp = pj (kp, "default");
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

static void
on_key_remove (LrgButton *button,
               gpointer   user_data)
{
	lrg_editor_fsm_editor_remove_key (LRG_EDITOR_FSM_EDITOR (user_data), g_object_get_data (G_OBJECT (button), "key"));
}

static void
on_key_add (LrgButton *button,
            gpointer   user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	g_autofree gchar   *name = g_strstrip (g_strdup (lrg_text_input_get_text (self->bb_name) != NULL
	                                                 ? lrg_text_input_get_text (self->bb_name) : ""));
	const gchar        *type = lrg_combo_box_get_selected_text (self->bb_type);
	g_autoptr(GError)   error = NULL;

	if (*name == '\0')
	{
		editor_app_set_status (app_of (self), "Type a key name first");
		return;
	}
	if (!lrg_editor_fsm_editor_add_key (self, name, type != NULL ? type : "float", NULL, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	else
		lrg_text_input_set_text (self->bb_name, "");
}

static void
on_send_event (LrgButton *button,
               gpointer   user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	const gchar        *ev = lrg_text_input_get_text (self->event_input);

	if (ev != NULL && *ev != '\0' && !lrg_editor_fsm_editor_send_live_event (self, ev))
		editor_app_set_status (app_of (self), "Nothing is live: play the level with an entity running this machine");
}

static gchar *
rows_signature (LrgEditorFsmEditor *self)
{
	g_autofree gchar *bb = editor_ai_flow_yaml (doc_of (self), "/blackboard");
	g_autofree gchar *grid = grid_signature (self);
	g_autofree gchar *tp = sel_tr_path (self);
	g_autofree gchar *syp = self->sel_state != NULL ? state_yaml (self, self->sel_state)
	                        : !self->sel_tr ? state_yaml (self, self->shown) : NULL;
	g_autofree gchar *tr = tp != NULL ? editor_ai_flow_yaml (doc_of (self), tp) : NULL;
	g_autofree gchar *st = NULL;
	GString          *s = g_string_new (NULL);
	guint             i;

	/* a state's lists and transitions (not its children: they are cards) */
	for (i = 0; syp != NULL && i < G_N_ELEMENTS (phase_keys); i++)
	{
		g_autofree gchar *lp = pj (syp, phase_keys[i]);
		g_autofree gchar *y = editor_ai_flow_yaml (doc_of (self), lp);

		g_string_append_printf (s, "%s;", y);
	}
	if (syp != NULL)
	{
		g_autofree gchar *lp = pj (syp, "transitions");
		g_autofree gchar *y = editor_ai_flow_yaml (doc_of (self), lp);

		g_string_append (s, y);
	}
	st = g_string_free (s, FALSE);
	return g_strdup_printf ("%s|%s|%s|%s", bb, grid, tr, st);
}

static void
rebuild_rows (LrgEditorFsmEditor *self)
{
	g_autofree gchar *tp = sel_tr_path (self);
	g_autofree gchar *syp = self->sel_state != NULL ? state_yaml (self, self->sel_state) : NULL;
	gint              n = n_items (self, "/blackboard");
	gint              i;

	self->rows_stale = FALSE;
	g_free (self->rows_sig);
	self->rows_sig = rows_signature (self);
	self->syncing = TRUE;

	lrg_container_remove_all (LRG_CONTAINER (self->lists));
	if (syp != NULL)
	{
		entry_list (self, syp, "enter", "ENTER", FALSE);
		entry_list (self, syp, "update", "UPDATE (every step)", FALSE);
		entry_list (self, syp, "exit", "EXIT", FALSE);
		transition_rows (self, self->sel_state);
	}
	else if (tp != NULL)
	{
		entry_list (self, tp, "when", "WHEN (all must hold)", TRUE);
		entry_list (self, tp, "do", "DO (between the exits and the entries)", FALSE);
	}
	else
	{
		g_autofree gchar *lyp = state_yaml (self, self->shown);

		if (lyp != NULL)
		{
			const gchar *who = *self->shown == '\0' ? "machine" : self->shown;
			gchar       *h;

			h = g_strdup_printf ("ENTER (%s)", who);
			entry_list (self, lyp, "enter", h, FALSE);
			g_free (h);
			h = g_strdup_printf ("UPDATE (%s)", who);
			entry_list (self, lyp, "update", h, FALSE);
			g_free (h);
			h = g_strdup_printf ("EXIT (%s)", who);
			entry_list (self, lyp, "exit", h, FALSE);
			g_free (h);
			transition_rows (self, self->shown);
		}
	}

	lrg_container_remove_all (LRG_CONTAINER (self->bb_rows));
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *kp = pji ("/blackboard", (guint) i);
		const gchar      *name = scalar_at (self, kp, "name");
		const gchar      *type = scalar_at (self, kp, "type");
		const gchar      *def = scalar_at (self, kp, "default");
		g_autofree gchar *rname = g_strdup_printf ("fsm-key-%s", name);
		g_autofree gchar *dname = g_strdup_printf ("fsm-key-%s-default", name);
		g_autofree gchar *xname = g_strdup_printf ("fsm-key-%s-remove", name);
		LrgWidget        *row = LRG_WIDGET (lrg_hbox_new ());
		LrgWidget        *label = LRG_WIDGET (lrg_label_new (name != NULL ? name : "?"));
		LrgWidget        *tl = editor_ai_heading (type != NULL ? type : "?");
		LrgWidget        *rm;

		lrg_widget_set_name (row, rname);
		lrg_container_set_spacing (LRG_CONTAINER (row), 4.0f);
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
		editor_ai_add (self->bb_rows, editor_ai_heading ("No keys declared"));
	self->syncing = FALSE;
}

/* ==========================================================================
 * Data editor vfuncs
 * ========================================================================== */

static void
fsm_refresh (LrgEditorDataEditor *editor,
             const gchar         *path)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (editor);
	g_autofree gchar   *sig = NULL;

	if (self->canvas == NULL)
		return;
	load_machine (self);
	if (!state_exists (self, self->shown))
	{
		g_free (self->shown);
		self->shown = g_strdup ("");
	}
	if (self->sel_state != NULL && !state_exists (self, self->sel_state))
		g_clear_pointer (&self->sel_state, g_free);
	if (self->sel_tr)
	{
		g_autofree gchar *tp = sel_tr_path (self);

		if (tp == NULL)
			self->sel_tr = FALSE;
	}
	if (!self->writing)
		self->canvas_stale = TRUE;
	rebuild_crumbs (self);
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

static void
add_issue (LrgEditorFsmEditor       *self,
           GPtrArray                *issues,
           LrgEditorProblemSeverity  severity,
           const gchar              *message)
{
	g_autofree gchar *path = editor_ai_error_path (message);
	guint             line = 0, col = 0;

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

static void
fsm_validate (LrgEditorDataEditor *editor,
              GPtrArray           *issues)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (editor);
	g_auto(GStrv)       unbound = NULL;
	guint               i;

	/* the document already ran the engine's loader and validate() */
	if (self->machine == NULL)
		return;
	unbound = lrg_logic_machine_dup_unbound (self->machine, NULL);
	for (i = 0; unbound != NULL && unbound[i] != NULL; i++)
	{
		g_autofree gchar *msg = g_strdup_printf ("%s is not registered in the editor (the game must register it)",
		                                         unbound[i]);

		add_issue (self, issues, LRG_EDITOR_PROBLEM_INFO, msg);
	}
}

/* Reveal: the state (or transition) holding a YAML path. */
static void
fsm_reveal (LrgEditorDataEditor *editor,
            const gchar         *path,
            guint                line)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (editor);
	g_auto(GStrv)       parts = lrg_editor_document_path_split (path, NULL);
	g_autofree gchar   *state = g_strdup ("");
	g_autofree gchar   *yp = g_strdup ("");
	guint               i;

	for (i = 0; parts != NULL && parts[i] != NULL && parts[i + 1] != NULL && g_strcmp0 (parts[i], "states") == 0; i += 2)
	{
		g_autofree gchar *list = pj (yp, "states");
		gchar            *sp = pj (list, parts[i + 1]);
		const gchar      *name = scalar_at (self, sp, "name");
		gchar            *next;

		if (name == NULL)
		{
			g_free (sp);
			break;
		}
		next = state_join (state, name);
		g_free (state);
		state = next;
		g_free (yp);
		yp = sp;
	}
	if (parts != NULL && parts[i] != NULL && parts[i + 1] != NULL && g_strcmp0 (parts[i], "transitions") == 0)
		lrg_editor_fsm_editor_select_transition (self, state, (guint) g_ascii_strtoull (parts[i + 1], NULL, 10));
	else if (*state != '\0')
		lrg_editor_fsm_editor_select_state (self, state);
}

static gchar *
fsm_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorFsmEditor   *self = LRG_EDITOR_FSM_EDITOR (editor);
	g_autoptr(GPtrArray)  kids = child_names (self, self->shown);

	return g_strdup_printf ("level=%s states=%u selected=%s live=%s", *self->shown != '\0' ? self->shown : "/",
	                        kids->len, self->sel_state != NULL ? self->sel_state : self->sel_tr ? "transition" : "-",
	                        lrg_editor_fsm_editor_get_live_machine (self) != NULL ? "yes" : "no");
}

static gboolean
fsm_save (LrgEditorDataEditor  *editor,
          GError              **error)
{
	flush_group (LRG_EDITOR_FSM_EDITOR (editor));
	return LRG_EDITOR_DATA_EDITOR_CLASS (lrg_editor_fsm_editor_parent_class)->save (editor, error);
}

static gboolean
fsm_undo (LrgEditorDataEditor *editor)
{
	flush_group (LRG_EDITOR_FSM_EDITOR (editor));
	return lrg_editor_document_undo (lrg_editor_data_editor_get_document (editor));
}

static gboolean
fsm_redo (LrgEditorDataEditor *editor)
{
	flush_group (LRG_EDITOR_FSM_EDITOR (editor));
	return lrg_editor_document_redo (lrg_editor_data_editor_get_document (editor));
}

static gboolean
lrg_editor_fsm_editor_step (LrgWidget *widget,
                            gfloat     delta_time)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (widget);
	gboolean            busy = LRG_WIDGET_CLASS (lrg_editor_fsm_editor_parent_class)->step (widget, delta_time);

	if (self->canvas_stale)
		rebuild_canvas (self);
	if (self->grid_stale)
		rebuild_grid (self);
	if (self->rows_stale)
		rebuild_rows (self);
	else
	{
		g_autofree gchar *sig = rows_signature (self);

		if (g_strcmp0 (sig, self->rows_sig) != 0)
			rebuild_rows (self);
	}
	if (lrg_breadcrumb_get_n_segments (self->crumbs) == 0)
		rebuild_crumbs (self);
	update_live (self);
	if (self->framed < 3 && lrg_widget_get_width (LRG_WIDGET (self->canvas)) > 200.0f)
	{
		self->framed++;
		editor_ai_frame_canvas (self->canvas);
	}
	return busy || lrg_editor_fsm_editor_get_live_machine (self) != NULL;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

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
on_up_chip (LrgWidget *chip,
            gpointer   user_data)
{
	lrg_editor_fsm_editor_go_up (LRG_EDITOR_FSM_EDITOR (user_data));
}

static void
on_add_state_chip (LrgWidget *chip,
                   gpointer   user_data)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (user_data);
	g_autoptr(GPtrArray) kids = child_names (self, self->shown);
	g_autofree gchar   *path = NULL;
	g_autoptr(GError)   error = NULL;

	path = lrg_editor_fsm_editor_add_state (self, self->shown, NULL, (gfloat) (kids->len % GRID_COLS) * GRID_DX,
	                                        (gfloat) (kids->len / GRID_COLS) * GRID_DY, &error);
	if (path == NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	else
		lrg_editor_fsm_editor_select_state (self, path);
}

static void
lrg_editor_fsm_editor_constructed (GObject *object)
{
	LrgEditorFsmEditor  *self = LRG_EDITOR_FSM_EDITOR (object);
	LrgEditorDataEditor *base = LRG_EDITOR_DATA_EDITOR (object);
	LrgEditorApp        *app;
	LrgWidget           *center, *top, *inspector, *inspector_scroll, *row;
	LrgSplitter         *split;
	static const gchar * const key_types[] = { "float", "int", "bool", "string", "object" };
	guint                i;

	G_OBJECT_CLASS (lrg_editor_fsm_editor_parent_class)->constructed (object);
	app = app_of (self);
	self->shown = g_strdup ("");
	load_machine (self);

	/* --- center: breadcrumbs + live line over the canvas --- */
	center = vbox_named ("fsm-center", 4.0f);
	top = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (top, "fsm-topbar");
	lrg_container_set_spacing (LRG_CONTAINER (top), 8.0f);
	lrg_container_set_padding (LRG_CONTAINER (top), 4.0f);
	self->crumbs = lrg_breadcrumb_new ();
	lrg_widget_set_name (LRG_WIDGET (self->crumbs), "fsm-crumbs");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->crumbs), 1.0f);
	g_signal_connect_object (self->crumbs, "activated", G_CALLBACK (on_crumb), self, 0);
	editor_ai_add (top, g_object_ref (LRG_WIDGET (self->crumbs)));
	g_object_unref (self->crumbs);
	self->live_label = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_name (self->live_label, "fsm-live");
	lrg_widget_set_valign (self->live_label, LRG_ALIGN_CENTER);
	editor_ai_add (top, g_object_ref (self->live_label));
	g_object_unref (self->live_label);
	editor_ai_add (center, top);
	self->canvas = lrg_node_graph_new ();
	lrg_widget_set_name (LRG_WIDGET (self->canvas), "fsm-canvas");
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

	/* --- right: inspector, lists, blackboard, live event --- */
	inspector = vbox_named ("fsm-inspector", 6.0f);
	lrg_container_set_padding (LRG_CONTAINER (inspector), 8.0f);
	self->inspector_title = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_name (self->inspector_title, "fsm-inspector-title");
	lrg_widget_add_class (self->inspector_title, "heading");
	editor_ai_add (inspector, g_object_ref (self->inspector_title));
	g_object_unref (self->inspector_title);
	self->source = lrg_editor_data_source_new (base);
	lrg_editor_data_source_set_read_func (self->source, source_read, self, NULL);
	lrg_editor_data_source_set_write_func (self->source, source_write, self, NULL);
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "fsm-grid");
	lrg_property_grid_set_label_width (self->grid, 100.0f);
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_editor_data_source_bind_grid (self->source, self->grid);
	editor_ai_add (inspector, g_object_ref (LRG_WIDGET (self->grid)));
	g_object_unref (self->grid);
	self->lists = vbox_named ("fsm-lists", 3.0f);
	editor_ai_add (inspector, g_object_ref (self->lists));
	g_object_unref (self->lists);

	editor_ai_add (inspector, editor_ai_heading ("BLACKBOARD"));
	self->bb_rows = vbox_named ("fsm-blackboard", 3.0f);
	editor_ai_add (inspector, g_object_ref (self->bb_rows));
	g_object_unref (self->bb_rows);
	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (row, "fsm-key-add-row");
	lrg_container_set_spacing (LRG_CONTAINER (row), 4.0f);
	self->bb_name = lrg_text_input_new_with_placeholder ("key");
	lrg_widget_set_name (LRG_WIDGET (self->bb_name), "fsm-key-name");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->bb_name), 1.0f);
	lrg_text_input_set_font_size (self->bb_name, 14.0f);
	editor_ai_add (row, g_object_ref (LRG_WIDGET (self->bb_name)));
	g_object_unref (self->bb_name);
	self->bb_type = lrg_combo_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->bb_type), "fsm-key-type");
	for (i = 0; i < G_N_ELEMENTS (key_types); i++)
		lrg_combo_box_append (self->bb_type, key_types[i]);
	lrg_combo_box_set_selected (self->bb_type, 0);
	editor_ai_add (row, g_object_ref (LRG_WIDGET (self->bb_type)));
	g_object_unref (self->bb_type);
	editor_ai_add (row, editor_ai_button ("fsm-key-add", "+ Key", G_CALLBACK (on_key_add), self));
	editor_ai_add (inspector, row);

	editor_ai_add (inspector, editor_ai_heading ("LIVE (while playing)"));
	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (row, "fsm-event-row");
	lrg_container_set_spacing (LRG_CONTAINER (row), 4.0f);
	self->event_input = lrg_text_input_new_with_placeholder ("event");
	lrg_widget_set_name (LRG_WIDGET (self->event_input), "fsm-event");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->event_input), 1.0f);
	lrg_text_input_set_font_size (self->event_input, 14.0f);
	editor_ai_add (row, g_object_ref (LRG_WIDGET (self->event_input)));
	g_object_unref (self->event_input);
	editor_ai_add (row, editor_ai_button ("fsm-event-send", "Send Event", G_CALLBACK (on_send_event), self));
	editor_ai_add (inspector, row);

	inspector_scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (inspector_scroll, "fsm-inspector-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (inspector_scroll), inspector);
	g_object_unref (inspector);

	split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (split), "fsm-split");
	lrg_splitter_set_start_child (split, center);
	g_object_unref (center);
	lrg_splitter_set_end_child (split, inspector_scroll);
	g_object_unref (inspector_scroll);
	lrg_splitter_set_default_fraction (split, 0.66f);
	lrg_splitter_set_fraction (split, 0.66f, FALSE);
	lrg_splitter_set_min_sizes (split, 360.0f, 300.0f);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (split));
	g_object_unref (split);

	lrg_editor_data_editor_add_tool (base, editor_chip_new ("fsm-add-state", LRG_ICON_PLUS, "State", on_add_state_chip,
	                                                        self));
	lrg_editor_data_editor_add_tool (base, editor_chip_new ("fsm-up", LRG_ICON_CHEVRON_UP, "Up", on_up_chip, self));

	rebuild_canvas (self);
	rebuild_crumbs (self);
	rebuild_grid (self);
	rebuild_rows (self);
	if (app != NULL)
		lrg_editor_fsm_editor_install (app);
}

static void
lrg_editor_fsm_editor_dispose (GObject *object)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (object);

	g_clear_handle_id (&self->group_idle, g_source_remove);
	if (self->group_open)
	{
		self->group_open = FALSE;
		lrg_editor_document_end_group (doc_of (self));
	}
	G_OBJECT_CLASS (lrg_editor_fsm_editor_parent_class)->dispose (object);
}

static void
lrg_editor_fsm_editor_finalize (GObject *object)
{
	LrgEditorFsmEditor *self = LRG_EDITOR_FSM_EDITOR (object);

	g_clear_object (&self->machine);
	g_clear_object (&self->source);
	g_ptr_array_unref (self->wires);
	g_hash_table_unref (self->bodies);
	g_free (self->load_error);
	g_free (self->shown);
	g_free (self->sel_state);
	g_free (self->sel_from);
	g_free (self->grid_sig);
	g_free (self->rows_sig);
	g_free (self->live_sig);
	G_OBJECT_CLASS (lrg_editor_fsm_editor_parent_class)->finalize (object);
}

static void
lrg_editor_fsm_editor_class_init (LrgEditorFsmEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_fsm_editor_constructed;
	object_class->dispose = lrg_editor_fsm_editor_dispose;
	object_class->finalize = lrg_editor_fsm_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_fsm_editor_step;
	data_class->refresh = fsm_refresh;
	data_class->validate = fsm_validate;
	data_class->reveal = fsm_reveal;
	data_class->get_state = fsm_get_state;
	data_class->save = fsm_save;
	data_class->undo = fsm_undo;
	data_class->redo = fsm_redo;
}

static void
lrg_editor_fsm_editor_init (LrgEditorFsmEditor *self)
{
	self->wires = g_ptr_array_new_with_free_func (wire_free);
	self->bodies = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorFsmEditor *
lrg_editor_fsm_editor_new (LrgEditorApp      *app,
                           LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_FSM_EDITOR, "app", app, "document", document, NULL);
}

LrgEditorFsmEditor *
lrg_editor_fsm_editor_open (LrgEditorApp  *app,
                            const gchar   *path,
                            GError       **error)
{
	LrgEditorDataEditor *e;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	e = lrg_editor_data_editor_open (app, path, error);
	if (e == NULL)
		return NULL;
	if (!LRG_IS_EDITOR_FSM_EDITOR (e))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s is not a logic state machine", path);
		return NULL;
	}
	return LRG_EDITOR_FSM_EDITOR (e);
}

LrgEditorFsmEditor *
lrg_editor_fsm_editor_create (LrgEditorApp  *app,
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
	if (g_str_has_suffix (base, ".rfsm"))
		base[strlen (base) - 5] = '\0';
	yname = editor_ai_yaml_str (name != NULL ? name : base);
	text = g_strdup_printf ("version: 1\nname: %s\ninitial: idle\nstates:\n- name: idle\n", yname);
	if (!editor_ai_create_file (app, path, text, &abs, error))
		return NULL;
	return lrg_editor_fsm_editor_open (app, abs, error);
}

LrgLogicMachine *
lrg_editor_fsm_editor_get_machine (LrgEditorFsmEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), NULL);
	return self->machine;
}

const gchar *
lrg_editor_fsm_editor_get_load_error (LrgEditorFsmEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), NULL);
	return self->load_error;
}

LrgNodeGraph *
lrg_editor_fsm_editor_get_canvas (LrgEditorFsmEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), NULL);
	return self->canvas;
}

const gchar *
lrg_editor_fsm_editor_get_shown (LrgEditorFsmEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), NULL);
	return self->shown;
}

gboolean
lrg_editor_fsm_editor_enter (LrgEditorFsmEditor *self,
                             const gchar        *state)
{
	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), FALSE);

	if (state == NULL || !state_exists (self, state))
		return FALSE;
	g_free (self->shown);
	self->shown = g_strdup (state);
	g_clear_pointer (&self->sel_state, g_free);
	self->sel_tr = FALSE;
	rebuild_canvas (self);
	rebuild_crumbs (self);
	self->grid_stale = TRUE;
	self->framed = 0;
	return TRUE;
}

gboolean
lrg_editor_fsm_editor_go_up (LrgEditorFsmEditor *self)
{
	g_autofree gchar *parent = NULL;
	g_autofree gchar *was = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), FALSE);

	if (*self->shown == '\0')
		return FALSE;
	was = g_strdup (self->shown);
	parent = state_parent (self->shown);
	lrg_editor_fsm_editor_enter (self, parent);
	/* coming up, the state we were in is selected */
	lrg_editor_fsm_editor_select_state (self, was);
	return TRUE;
}

gchar **
lrg_editor_fsm_editor_dup_breadcrumbs (LrgEditorFsmEditor *self)
{
	GStrvBuilder *b;
	const gchar  *name;
	gchar       **out;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), NULL);

	b = g_strv_builder_new ();
	name = scalar_at (self, "", "name");
	g_strv_builder_add (b, name != NULL ? name : "machine");
	if (*self->shown != '\0')
	{
		g_auto(GStrv) parts = g_strsplit (self->shown, "/", -1);

		g_strv_builder_addv (b, (const gchar **) parts);
	}
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);
	return out;
}

static void
collect_states (LrgEditorFsmEditor *self,
                const gchar        *path,
                GStrvBuilder       *b)
{
	g_autoptr(GPtrArray) kids = child_names (self, path);
	guint                i;

	for (i = 0; i < kids->len; i++)
	{
		g_autofree gchar *child = state_join (path, g_ptr_array_index (kids, i));

		g_strv_builder_add (b, child);
		collect_states (self, child, b);
	}
}

gchar **
lrg_editor_fsm_editor_dup_state_paths (LrgEditorFsmEditor *self)
{
	GStrvBuilder *b;
	gchar       **out;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), NULL);

	b = g_strv_builder_new ();
	collect_states (self, "", b);
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);
	return out;
}

gchar *
lrg_editor_fsm_editor_get_state_yaml_path (LrgEditorFsmEditor *self,
                                           const gchar        *state)
{
	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), NULL);
	return state_yaml (self, state);
}

gchar *
lrg_editor_fsm_editor_add_state (LrgEditorFsmEditor  *self,
                                 const gchar         *parent,
                                 const gchar         *name,
                                 gfloat               x,
                                 gfloat               y,
                                 GError             **error)
{
	g_autofree gchar     *pyp = NULL;
	g_autofree gchar     *list = NULL;
	g_autofree gchar     *nm = NULL;
	g_autofree gchar     *yn = NULL;
	g_autofree gchar     *text = NULL;
	g_autofree gchar     *path = NULL;
	g_autoptr(GPtrArray)  kids = NULL;
	gboolean              opened;
	gboolean              ok;
	guint                 i;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), NULL);

	if (parent == NULL)
		parent = "";
	pyp = state_yaml (self, parent);
	if (pyp == NULL)
	{
		not_found (error, "state", parent);
		return NULL;
	}
	kids = child_names (self, parent);
	nm = name != NULL ? g_strdup (name) : free_name (self, parent, "state");
	if (*nm == '\0' || strchr (nm, '/') != NULL)
	{
		fail_arg (error, "'%s' is not a state name (non-empty, no '/')", nm);
		return NULL;
	}
	for (i = 0; i < kids->len; i++)
		if (g_strcmp0 (g_ptr_array_index (kids, i), nm) == 0)
		{
			fail_arg (error, "'%s' already has a state '%s'", *parent != '\0' ? parent : "the machine", nm);
			return NULL;
		}
	yn = editor_ai_yaml_str (nm);
	text = g_strdup_printf ("{name: %s}", yn);
	list = pj (pyp, "states");
	path = state_join (parent, nm);
	opened = edit_begin (self, "Add State");
	if (lrg_editor_document_get_kind (doc_of (self), list) != LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
	{
		if (lrg_editor_document_has_path (doc_of (self), list))
			lrg_editor_document_remove (doc_of (self), list, NULL);
		lrg_editor_document_add_key (doc_of (self), pyp, "states", "[]", -1, NULL);
	}
	ok = lrg_editor_document_insert_item (doc_of (self), list, -1, text, error);
	if (ok)
		write_pos (self, path, x, y);
	edit_end (self, opened);
	self->canvas_stale = TRUE;
	return ok ? g_steal_pointer (&path) : NULL;
}

/* Every transition (owner path, index) whose target resolves to a state
 * under @path, collected before anything is removed. */
typedef struct
{
	gchar *owner;
	guint  index;
} TrRef;

static void
collect_targeting (LrgEditorFsmEditor *self,
                   const gchar        *owner,
                   const gchar        *path,
                   GPtrArray          *out)
{
	g_autofree gchar     *syp = state_yaml (self, owner);
	g_autofree gchar     *list = syp != NULL ? pj (syp, "transitions") : NULL;
	g_autoptr(GPtrArray)  kids = child_names (self, owner);
	gint                  n = list != NULL ? n_items (self, list) : 0;
	gint                  i;

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *tp = pji (list, (guint) i);
		g_autofree gchar *target = resolve_target (self, owner, scalar_at (self, tp, "to"));

		if (target != NULL && state_under (target, path) && !state_under (owner, path))
		{
			TrRef *r = g_new0 (TrRef, 1);

			r->owner = g_strdup (owner);
			r->index = (guint) i;
			g_ptr_array_add (out, r);
		}
	}
	for (i = 0; i < (gint) kids->len; i++)
	{
		g_autofree gchar *child = state_join (owner, g_ptr_array_index (kids, i));

		collect_targeting (self, child, path, out);
	}
}

static void
tr_ref_free (gpointer data)
{
	TrRef *r = data;

	g_free (r->owner);
	g_free (r);
}

gboolean
lrg_editor_fsm_editor_remove_state (LrgEditorFsmEditor  *self,
                                    const gchar         *state,
                                    GError             **error)
{
	g_autofree gchar     *keep = g_strdup (state);
	g_autofree gchar     *yp = NULL;
	g_autofree gchar     *parent = NULL;
	g_autofree gchar     *pyp = NULL;
	g_autoptr(GPtrArray)  refs = NULL;
	gboolean              opened;
	gboolean              ok;
	gint                  i;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), FALSE);

	if (keep == NULL || *keep == '\0' || (yp = state_yaml (self, keep)) == NULL)
		return not_found (error, "state", state);
	parent = state_parent (keep);
	refs = g_ptr_array_new_with_free_func (tr_ref_free);
	collect_targeting (self, "", keep, refs);
	opened = edit_begin (self, "Delete State");
	/* transitions first (last index first: earlier indexes stay valid) */
	for (i = (gint) refs->len - 1; i >= 0; i--)
	{
		TrRef            *r = g_ptr_array_index (refs, i);
		g_autofree gchar *syp = state_yaml (self, r->owner);
		g_autofree gchar *list = syp != NULL ? pj (syp, "transitions") : NULL;
		g_autofree gchar *tp = list != NULL ? pji (list, r->index) : NULL;

		if (tp != NULL)
			lrg_editor_document_remove (doc_of (self), tp, NULL);
	}
	pyp = state_yaml (self, parent);
	if (pyp != NULL && g_strcmp0 (scalar_at (self, pyp, "initial"), state_name (keep)) == 0)
	{
		g_autofree gchar *ip = pj (pyp, "initial");

		lrg_editor_document_remove (doc_of (self), ip, NULL);
	}
	g_free (yp);
	yp = state_yaml (self, keep);
	ok = yp != NULL && lrg_editor_document_remove (doc_of (self), yp, error);
	if (ok)
		rekey_positions (self, keep, NULL);
	edit_end (self, opened);
	if (self->sel_state != NULL && state_under (self->sel_state, keep))
		g_clear_pointer (&self->sel_state, g_free);
	self->canvas_stale = TRUE;
	return ok;
}

/* Every transition whose target is under @path, with that target. */
typedef struct
{
	gchar *owner;
	guint  index;
	gchar *target;
} TrTarget;

static void
tr_target_free (gpointer data)
{
	TrTarget *t = data;

	g_free (t->owner);
	g_free (t->target);
	g_free (t);
}

static void
collect_targets (LrgEditorFsmEditor *self,
                 const gchar        *owner,
                 const gchar        *path,
                 GPtrArray          *out)
{
	g_autofree gchar     *syp = state_yaml (self, owner);
	g_autofree gchar     *list = syp != NULL ? pj (syp, "transitions") : NULL;
	g_autoptr(GPtrArray)  kids = child_names (self, owner);
	gint                  n = list != NULL ? n_items (self, list) : 0;
	gint                  i;

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *tp = pji (list, (guint) i);
		g_autofree gchar *target = resolve_target (self, owner, scalar_at (self, tp, "to"));

		if (target != NULL && state_under (target, path))
		{
			TrTarget *t = g_new0 (TrTarget, 1);

			t->owner = g_strdup (owner);
			t->index = (guint) i;
			t->target = g_steal_pointer (&target);
			g_ptr_array_add (out, t);
		}
	}
	for (i = 0; i < (gint) kids->len; i++)
	{
		g_autofree gchar *child = state_join (owner, g_ptr_array_index (kids, i));

		collect_targets (self, child, path, out);
	}
}

gchar *
lrg_editor_fsm_editor_rename_state (LrgEditorFsmEditor  *self,
                                    const gchar         *state,
                                    const gchar         *name,
                                    GError             **error)
{
	g_autofree gchar     *keep = g_strdup (state);
	g_autofree gchar     *yp = NULL;
	g_autofree gchar     *parent = NULL;
	g_autofree gchar     *pyp = NULL;
	g_autofree gchar     *new_path = NULL;
	g_autofree gchar     *np = NULL;
	g_autoptr(GPtrArray)  targets = NULL;
	g_autoptr(GPtrArray)  kids = NULL;
	gboolean              was_initial;
	gboolean              opened;
	gboolean              ok;
	guint                 i;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), NULL);

	if (keep == NULL || *keep == '\0' || (yp = state_yaml (self, keep)) == NULL)
	{
		not_found (error, "state", state);
		return NULL;
	}
	if (name == NULL || *name == '\0' || strchr (name, '/') != NULL)
	{
		fail_arg (error, "'%s' is not a state name (non-empty, no '/')", name != NULL ? name : "");
		return NULL;
	}
	if (g_strcmp0 (state_name (keep), name) == 0)
		return g_strdup (keep);
	parent = state_parent (keep);
	kids = child_names (self, parent);
	for (i = 0; i < kids->len; i++)
		if (g_strcmp0 (g_ptr_array_index (kids, i), name) == 0)
		{
			fail_arg (error, "'%s' already has a state '%s'", *parent != '\0' ? parent : "the machine", name);
			return NULL;
		}
	new_path = state_join (parent, name);
	/* resolve every target before the rename changes what names mean */
	targets = g_ptr_array_new_with_free_func (tr_target_free);
	collect_targets (self, "", keep, targets);
	pyp = state_yaml (self, parent);
	was_initial = pyp != NULL && g_strcmp0 (scalar_at (self, pyp, "initial"), state_name (keep)) == 0;

	opened = edit_begin (self, "Rename State");
	np = pj (yp, "name");
	ok = lrg_editor_document_set_string (doc_of (self), np, name, 0, error);
	if (ok)
	{
		if (was_initial)
		{
			g_autofree gchar *ip = pj (pyp, "initial");

			lrg_editor_document_set_string (doc_of (self), ip, name, 0, NULL);
		}
		for (i = 0; i < targets->len; i++)
		{
			TrTarget         *t = g_ptr_array_index (targets, i);
			g_autofree gchar *moved = g_strconcat (new_path, t->target + strlen (keep), NULL);
			g_autofree gchar *owner = state_under (t->owner, keep)
			                          ? g_strconcat (new_path, t->owner + strlen (keep), NULL) : g_strdup (t->owner);
			g_autofree gchar *syp = state_yaml (self, owner);
			g_autofree gchar *list = syp != NULL ? pj (syp, "transitions") : NULL;
			g_autofree gchar *tp = list != NULL ? pji (list, t->index) : NULL;
			g_autofree gchar *top = tp != NULL ? pj (tp, "to") : NULL;
			g_autofree gchar *now = top != NULL ? resolve_target (self, owner, lrg_editor_document_get_scalar (doc_of (self),
			                                                                                                    top))
			                                    : NULL;

			/* only rewrite targets the rename broke */
			if (top != NULL && g_strcmp0 (now, moved) != 0)
			{
				g_autofree gchar *text = target_text (self, owner, moved);

				lrg_editor_document_set_string (doc_of (self), top, text, 0, NULL);
			}
		}
		rekey_positions (self, keep, new_path);
	}
	edit_end (self, opened);
	if (ok && self->sel_state != NULL && state_under (self->sel_state, keep))
	{
		gchar *s = g_strconcat (new_path, self->sel_state + strlen (keep), NULL);

		g_free (self->sel_state);
		self->sel_state = s;
	}
	if (ok && state_under (self->shown, keep) && *keep != '\0')
	{
		gchar *s = g_strconcat (new_path, self->shown + strlen (keep), NULL);

		g_free (self->shown);
		self->shown = s;
	}
	self->canvas_stale = TRUE;
	self->grid_stale = TRUE;
	return ok ? g_steal_pointer (&new_path) : NULL;
}

gboolean
lrg_editor_fsm_editor_set_initial (LrgEditorFsmEditor  *self,
                                   const gchar         *state,
                                   GError             **error)
{
	g_autofree gchar *parent = NULL;
	g_autofree gchar *pyp = NULL;
	g_autofree gchar *ip = NULL;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), FALSE);

	if (state == NULL || *state == '\0' || !state_exists (self, state))
		return not_found (error, "state", state);
	parent = state_parent (state);
	pyp = state_yaml (self, parent);
	ip = pj (pyp, "initial");
	if (lrg_editor_document_has_path (doc_of (self), ip))
		ok = lrg_editor_document_set_string (doc_of (self), ip, state_name (state), 0, error);
	else
	{
		g_autofree gchar *yn = editor_ai_yaml_str (state_name (state));
		g_auto(GStrv)     keys = lrg_editor_document_get_keys (doc_of (self), pyp);
		gint              pos = -1;
		guint             i;

		/* before the actions and children, as the engine writes it */
		for (i = 0; keys != NULL && keys[i] != NULL; i++)
			if (g_strcmp0 (keys[i], "enter") == 0 || g_strcmp0 (keys[i], "update") == 0 ||
			    g_strcmp0 (keys[i], "exit") == 0 || g_strcmp0 (keys[i], "transitions") == 0 ||
			    g_strcmp0 (keys[i], "states") == 0)
			{
				pos = (gint) i;
				break;
			}
		ok = lrg_editor_document_add_key (doc_of (self), pyp, "initial", yn, pos, error);
	}
	self->canvas_stale = TRUE;
	self->grid_stale = TRUE;
	return ok;
}

gboolean
lrg_editor_fsm_editor_set_actions (LrgEditorFsmEditor  *self,
                                   const gchar         *state,
                                   LrgLogicPhase        phase,
                                   const gchar         *yaml,
                                   GError             **error)
{
	g_autofree gchar *yp = NULL;
	g_autofree gchar *lp = NULL;
	const gchar      *key;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), FALSE);

	if (state == NULL || (yp = state_yaml (self, state)) == NULL)
		return not_found (error, "state", state);
	key = phase == LRG_LOGIC_PHASE_ENTER ? "enter" : phase == LRG_LOGIC_PHASE_UPDATE ? "update" : "exit";
	lp = pj (yp, key);
	self->canvas_stale = TRUE;
	self->rows_stale = TRUE;
	{
		g_autofree gchar *trimmed = g_strstrip (g_strdup (yaml != NULL ? yaml : "[]"));

		if (g_strcmp0 (trimmed, "[]") == 0)
			return !lrg_editor_document_has_path (doc_of (self), lp) ||
			       lrg_editor_document_remove (doc_of (self), lp, error);
	}
	if (lrg_editor_document_has_path (doc_of (self), lp))
		return lrg_editor_document_replace (doc_of (self), lp, yaml, error);
	return lrg_editor_document_add_key (doc_of (self), yp, key, yaml, -1, error);
}

gboolean
lrg_editor_fsm_editor_set_state_position (LrgEditorFsmEditor *self,
                                          const gchar        *state,
                                          gfloat              x,
                                          gfloat              y)
{
	gboolean opened;
	gboolean ok;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), FALSE);

	if (state == NULL || *state == '\0' || !state_exists (self, state))
		return FALSE;
	opened = edit_begin (self, "Move State");
	ok = write_pos (self, state, x, y);
	edit_end (self, opened);
	self->canvas_stale = TRUE;
	return ok;
}

gboolean
lrg_editor_fsm_editor_get_state_position (LrgEditorFsmEditor *self,
                                          const gchar        *state,
                                          gfloat             *x,
                                          gfloat             *y)
{
	gfloat                px = 0.0f, py = 0.0f;
	gboolean              saved;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), FALSE);

	saved = state != NULL && read_pos (self, state, &px, &py);
	if (!saved && state != NULL && *state != '\0')
	{
		g_autofree gchar     *parent = state_parent (state);
		g_autoptr(GPtrArray)  kids = child_names (self, parent);
		guint                 i;

		for (i = 0; i < kids->len; i++)
			if (g_strcmp0 (g_ptr_array_index (kids, i), state_name (state)) == 0)
				card_pos (self, state, i, &px, &py);
	}
	if (x != NULL)
		*x = px;
	if (y != NULL)
		*y = py;
	return saved;
}

static gchar *
transition_path (LrgEditorFsmEditor  *self,
                 const gchar         *from,
                 guint                index,
                 GError             **error)
{
	g_autofree gchar *syp = from != NULL ? state_yaml (self, from) : NULL;
	g_autofree gchar *list = NULL;
	gchar            *tp;

	if (syp == NULL)
	{
		not_found (error, "state", from);
		return NULL;
	}
	list = pj (syp, "transitions");
	tp = pji (list, index);
	if (!lrg_editor_document_has_path (doc_of (self), tp))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "'%s' has no transition %u",
		             *from != '\0' ? from : "the machine", index);
		g_free (tp);
		return NULL;
	}
	return tp;
}

gint
lrg_editor_fsm_editor_add_transition (LrgEditorFsmEditor  *self,
                                      const gchar         *from,
                                      const gchar         *to,
                                      const gchar         *event,
                                      gdouble              after,
                                      GError             **error)
{
	g_autofree gchar *syp = NULL;
	g_autofree gchar *list = NULL;
	g_autofree gchar *yt = NULL;
	GString          *text;
	gboolean          opened;
	gboolean          ok;
	gint              n;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), -1);

	if (from == NULL || (syp = state_yaml (self, from)) == NULL)
	{
		not_found (error, "state", from);
		return -1;
	}
	if (to == NULL || *to == '\0')
	{
		fail_arg (error, "a transition needs a target");
		return -1;
	}
	yt = editor_ai_yaml_str (to);
	text = g_string_new (NULL);
	g_string_append_printf (text, "{to: %s", yt);
	if (event != NULL && *event != '\0')
	{
		g_autofree gchar *ye = editor_ai_yaml_str (event);

		g_string_append_printf (text, ", event: %s", ye);
	}
	if (after >= 0.0)
	{
		g_autofree gchar *ya = editor_ai_num_str (after, TRUE);

		g_string_append_printf (text, ", after: %s", ya);
	}
	g_string_append_c (text, '}');
	list = pj (syp, "transitions");
	opened = edit_begin (self, "Add Transition");
	if (lrg_editor_document_get_kind (doc_of (self), list) != LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
	{
		g_auto(GStrv) keys = lrg_editor_document_get_keys (doc_of (self), syp);
		gint          pos = -1;
		guint         i;

		if (lrg_editor_document_has_path (doc_of (self), list))
			lrg_editor_document_remove (doc_of (self), list, NULL);
		for (i = 0; keys != NULL && keys[i] != NULL; i++)
			if (g_strcmp0 (keys[i], "states") == 0)
			{
				pos = (gint) i;
				break;
			}
		lrg_editor_document_add_key (doc_of (self), syp, "transitions", "[]", pos, NULL);
	}
	n = n_items (self, list);
	ok = lrg_editor_document_insert_item (doc_of (self), list, -1, text->str, error);
	edit_end (self, opened);
	g_string_free (text, TRUE);
	self->canvas_stale = TRUE;
	return ok ? n : -1;
}

gboolean
lrg_editor_fsm_editor_remove_transition (LrgEditorFsmEditor  *self,
                                         const gchar         *from,
                                         guint                index,
                                         GError             **error)
{
	g_autofree gchar *tp = NULL;
	g_autofree gchar *list = NULL;
	gboolean          opened;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), FALSE);

	tp = transition_path (self, from, index, error);
	if (tp == NULL)
		return FALSE;
	list = g_path_get_dirname (tp);
	opened = edit_begin (self, "Delete Transition");
	ok = lrg_editor_document_remove (doc_of (self), tp, error);
	if (ok && lrg_editor_document_get_length (doc_of (self), list) == 0)
		lrg_editor_document_remove (doc_of (self), list, NULL);
	edit_end (self, opened);
	if (self->sel_tr && g_strcmp0 (self->sel_from, from) == 0 && self->sel_index == index)
		self->sel_tr = FALSE;
	self->canvas_stale = TRUE;
	self->grid_stale = TRUE;
	return ok;
}

gboolean
lrg_editor_fsm_editor_set_transition_field (LrgEditorFsmEditor  *self,
                                            const gchar         *from,
                                            guint                index,
                                            const gchar         *key,
                                            const gchar         *yaml,
                                            GError             **error)
{
	static const gchar * const keys[] = { "to", "event", "after", "when", "do", NULL };
	g_autofree gchar *tp = NULL;
	g_autofree gchar *kp = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), FALSE);

	if (key == NULL || !g_strv_contains (keys, key))
		return fail_arg (error, "'%s' is not a transition key (to, event, after, when, do)", key != NULL ? key : "");
	tp = transition_path (self, from, index, error);
	if (tp == NULL)
		return FALSE;
	kp = pj (tp, key);
	self->canvas_stale = TRUE;
	self->rows_stale = TRUE;
	if (yaml == NULL)
	{
		if (g_strcmp0 (key, "to") == 0)
			return fail_arg (error, "a transition needs its target");
		return !lrg_editor_document_has_path (doc_of (self), kp) || lrg_editor_document_remove (doc_of (self), kp, error);
	}
	if (lrg_editor_document_has_path (doc_of (self), kp))
		return lrg_editor_document_replace (doc_of (self), kp, yaml, error);
	return lrg_editor_document_add_key (doc_of (self), tp, key, yaml, -1, error);
}

guint
lrg_editor_fsm_editor_get_transition_count (LrgEditorFsmEditor *self,
                                            const gchar        *from)
{
	g_autofree gchar *syp = NULL;
	g_autofree gchar *list = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), 0);

	syp = from != NULL ? state_yaml (self, from) : NULL;
	if (syp == NULL)
		return 0;
	list = pj (syp, "transitions");
	return (guint) n_items (self, list);
}

gboolean
lrg_editor_fsm_editor_add_key (LrgEditorFsmEditor  *self,
                               const gchar         *name,
                               const gchar         *type,
                               const gchar         *default_yaml,
                               GError             **error)
{
	static const gchar * const types[] = { "int", "float", "bool", "string", "object", NULL };
	g_autofree gchar *yn = NULL;
	g_autofree gchar *text = NULL;
	gint              n, i;
	gboolean          opened;
	gboolean          ok;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), FALSE);

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
		for (i = 0; keys != NULL && keys[i] != NULL; i++)
			if (g_strcmp0 (keys[i], "version") != 0 && g_strcmp0 (keys[i], "name") != 0)
			{
				pos = i;
				break;
			}
		lrg_editor_document_add_key (doc_of (self), "", "blackboard", "[]", pos, NULL);
	}
	ok = lrg_editor_document_insert_item (doc_of (self), "/blackboard", -1, text, error);
	edit_end (self, opened);
	self->rows_stale = TRUE;
	return ok;
}

gboolean
lrg_editor_fsm_editor_remove_key (LrgEditorFsmEditor *self,
                                  const gchar        *name)
{
	gint n, i;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), FALSE);

	n = n_items (self, "/blackboard");
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *kp = pji ("/blackboard", (guint) i);

		if (g_strcmp0 (scalar_at (self, kp, "name"), name) == 0)
		{
			gboolean ok = lrg_editor_document_remove (doc_of (self), kp, NULL);

			self->rows_stale = TRUE;
			return ok;
		}
	}
	return FALSE;
}

gboolean
lrg_editor_fsm_editor_select_state (LrgEditorFsmEditor *self,
                                    const gchar        *state)
{
	g_autofree gchar *parent = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), FALSE);

	self->sel_tr = FALSE;
	if (state == NULL)
	{
		g_clear_pointer (&self->sel_state, g_free);
		self->syncing = TRUE;
		lrg_node_graph_unselect_all (self->canvas);
		self->syncing = FALSE;
		self->grid_stale = TRUE;
		return TRUE;
	}
	if (*state == '\0' || !state_exists (self, state))
		return FALSE;
	parent = state_parent (state);
	if (g_strcmp0 (parent, self->shown) != 0)
	{
		g_free (self->shown);
		self->shown = g_strdup (parent);
		rebuild_crumbs (self);
		self->framed = 0;
	}
	g_free (self->sel_state);
	self->sel_state = g_strdup (state);
	rebuild_canvas (self);
	self->grid_stale = TRUE;
	return TRUE;
}

gboolean
lrg_editor_fsm_editor_select_transition (LrgEditorFsmEditor *self,
                                         const gchar        *from,
                                         guint               index)
{
	g_autofree gchar *tp = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), FALSE);

	tp = transition_path (self, from, index, NULL);
	if (tp == NULL)
		return FALSE;
	/* show the level the transition is drawn on */
	if (g_strcmp0 (from, self->shown) != 0)
	{
		g_autofree gchar *parent = state_parent (from);

		g_free (self->shown);
		self->shown = *from != '\0' ? g_strdup (parent) : g_strdup ("");
		rebuild_crumbs (self);
		self->framed = 0;
	}
	g_clear_pointer (&self->sel_state, g_free);
	g_free (self->sel_from);
	self->sel_from = g_strdup (from);
	self->sel_index = index;
	self->sel_tr = TRUE;
	rebuild_canvas (self);
	self->grid_stale = TRUE;
	return TRUE;
}

const gchar *
lrg_editor_fsm_editor_get_selected_state (LrgEditorFsmEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), NULL);
	return self->sel_state;
}

const gchar *
lrg_editor_fsm_editor_get_selected_transition (LrgEditorFsmEditor *self,
                                               guint              *index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), NULL);

	if (!self->sel_tr)
		return NULL;
	if (index != NULL)
		*index = self->sel_index;
	return self->sel_from;
}

LrgLogicMachine *
lrg_editor_fsm_editor_get_live_machine (LrgEditorFsmEditor *self)
{
	LrgEditorApp   *app;
	const gchar    *path;
	LrgAiComponent *c;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), NULL);

	app = app_of (self);
	path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self));
	if (app == NULL || path == NULL || !lrg_editor_app_is_playing (app))
		return NULL;
	c = lrg_editor_ai_debug_panel_find_for_file (app, path, TRUE);
	return c != NULL ? lrg_ai_component_get_machine (c) : NULL;
}

gboolean
lrg_editor_fsm_editor_send_live_event (LrgEditorFsmEditor *self,
                                       const gchar        *event)
{
	LrgLogicMachine *m;

	g_return_val_if_fail (LRG_IS_EDITOR_FSM_EDITOR (self), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	m = lrg_editor_fsm_editor_get_live_machine (self);
	if (m == NULL)
		return FALSE;
	lrg_logic_machine_send_event (m, event);
	return TRUE;
}

/* ==========================================================================
 * IPC: fsm.*
 * ========================================================================== */

static LrgEditorFsmEditor *
ipc_editor (LrgEditorApp  *app,
            JsonObject    *params,
            GError       **error)
{
	const gchar        *path = editor_ai_require_string (params, "path", error);
	LrgEditorFsmEditor *self;
	g_autoptr(GError)   err = NULL;

	if (path == NULL)
		return NULL;
	self = lrg_editor_fsm_editor_open (app, path, &err);
	if (self == NULL)
		editor_ai_fail (error, err);
	return self;
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

/* A JSON list member as YAML ("[{action: shout}]"); NULL when absent. */
static gchar *
list_yaml (JsonObject  *params,
           const gchar *name,
           gboolean    *present,
           GError     **error)
{
	JsonNode *n;

	*present = params != NULL && json_object_has_member (params, name);
	if (!*present)
		return NULL;
	n = json_object_get_member (params, name);
	if (JSON_NODE_HOLDS_NULL (n))
		return NULL;
	if (!JSON_NODE_HOLDS_ARRAY (n))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "'%s' must be a list", name);
		return NULL;
	}
	return editor_ai_json_to_yaml (n);
}

static JsonNode *
doc_json_or_empty (LrgEditorFsmEditor *self,
                   const gchar        *parent,
                   const gchar        *key)
{
	g_autofree gchar *p = pj (parent, key);

	if (lrg_editor_document_get_kind (doc_of (self), p) == LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
		return editor_ai_doc_to_json (doc_of (self), p);
	return json_node_init_array (json_node_alloc (), json_array_new ());
}

static void
transitions_json (LrgEditorFsmEditor *self,
                  const gchar        *owner,
                  JsonArray          *out,
                  LrgLogicMachine    *live)
{
	g_autofree gchar     *syp = state_yaml (self, owner);
	g_autofree gchar     *list = syp != NULL ? pj (syp, "transitions") : NULL;
	g_autoptr(GPtrArray)  kids = child_names (self, owner);
	LrgLogicState        *ls = live != NULL ? lrg_logic_machine_find_state (live, owner) : NULL;
	gint                  n = list != NULL ? n_items (self, list) : 0;
	gint                  i;

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *tp = pji (list, (guint) i);
		JsonObject       *t = json_object_new ();
		const gchar      *to = scalar_at (self, tp, "to");
		const gchar      *ev = scalar_at (self, tp, "event");
		g_autofree gchar *ap = pj (tp, "after");

		json_object_set_string_member (t, "from", owner);
		json_object_set_int_member (t, "index", i);
		json_object_set_string_member (t, "to", to != NULL ? to : "");
		if (ev != NULL)
			json_object_set_string_member (t, "event", ev);
		else
			json_object_set_null_member (t, "event");
		if (lrg_editor_document_has_path (doc_of (self), ap))
			json_object_set_member (t, "after", editor_ai_doc_to_json (doc_of (self), ap));
		else
			json_object_set_null_member (t, "after");
		json_object_set_member (t, "when", doc_json_or_empty (self, tp, "when"));
		json_object_set_member (t, "do", doc_json_or_empty (self, tp, "do"));
		json_object_set_int_member (t, "fires", (ls != NULL && (guint) i < lrg_logic_state_get_transition_count (ls))
		                                        ? (gint64) lrg_logic_transition_get_fire_count (
		                                              lrg_logic_state_get_transition (ls, (guint) i))
		                                        : 0);
		json_array_add_object_element (out, t);
	}
	for (i = 0; i < (gint) kids->len; i++)
	{
		g_autofree gchar *child = state_join (owner, g_ptr_array_index (kids, i));

		transitions_json (self, child, out, live);
	}
}

static JsonNode *
m_fsm_open (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	return self != NULL ? str_result ("path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self))) : NULL;
}

static JsonNode *
m_fsm_create (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	const gchar        *path = editor_ai_require_string (params, "path", error);
	LrgEditorFsmEditor *self;
	g_autoptr(GError)   err = NULL;

	if (path == NULL)
		return NULL;
	self = lrg_editor_fsm_editor_create (LRG_EDITOR_APP (user_data), path, editor_ai_param_string (params, "name"), &err);
	if (self == NULL)
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return str_result ("path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
}

static JsonNode *
m_fsm_get (LrgEditorServer  *server,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar        *machine = editor_ai_param_string (params, "machine");
	g_auto(GStrv)       states = NULL;
	g_auto(GStrv)       crumbs = NULL;
	JsonObject         *o, *sel;
	JsonArray          *sa, *ta, *ca;
	const gchar        *name;
	g_autofree gchar   *init = NULL;
	guint               i;

	if (self == NULL)
		return NULL;
	if (machine != NULL && !lrg_editor_fsm_editor_enter (self, machine))
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such state");
		return NULL;
	}
	o = json_object_new ();
	sa = json_array_new ();
	states = lrg_editor_fsm_editor_dup_state_paths (self);
	for (i = 0; states != NULL && states[i] != NULL; i++)
	{
		g_autofree gchar     *yp = state_yaml (self, states[i]);
		g_autofree gchar     *parent = state_parent (states[i]);
		g_autofree gchar     *pyp = state_yaml (self, parent);
		g_autoptr(GPtrArray)  sibs = child_names (self, parent);
		g_autoptr(GPtrArray)  kids = child_names (self, states[i]);
		const gchar          *pinit = pyp != NULL ? scalar_at (self, pyp, "initial") : NULL;
		JsonObject           *s = json_object_new ();
		gfloat                x, y;

		if (pinit == NULL && sibs->len > 0)
			pinit = g_ptr_array_index (sibs, 0);
		json_object_set_string_member (s, "name", state_name (states[i]));
		json_object_set_string_member (s, "path", states[i]);
		json_object_set_boolean_member (s, "initial", g_strcmp0 (pinit, state_name (states[i])) == 0);
		json_object_set_int_member (s, "children", kids->len);
		json_object_set_member (s, "enter", doc_json_or_empty (self, yp, "enter"));
		json_object_set_member (s, "update", doc_json_or_empty (self, yp, "update"));
		json_object_set_member (s, "exit", doc_json_or_empty (self, yp, "exit"));
		lrg_editor_fsm_editor_get_state_position (self, states[i], &x, &y);
		json_object_set_double_member (s, "x", x);
		json_object_set_double_member (s, "y", y);
		json_array_add_object_element (sa, s);
	}
	ta = json_array_new ();
	transitions_json (self, "", ta, lrg_editor_fsm_editor_get_live_machine (self));
	ca = json_array_new ();
	crumbs = lrg_editor_fsm_editor_dup_breadcrumbs (self);
	for (i = 0; crumbs != NULL && crumbs[i] != NULL; i++)
		json_array_add_string_element (ca, crumbs[i]);
	name = scalar_at (self, "", "name");
	init = g_strdup (scalar_at (self, "", "initial"));
	json_object_set_string_member (o, "path", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)));
	if (name != NULL)
		json_object_set_string_member (o, "name", name);
	else
		json_object_set_null_member (o, "name");
	json_object_set_string_member (o, "machine", self->shown);
	json_object_set_array_member (o, "breadcrumbs", ca);
	if (init != NULL)
		json_object_set_string_member (o, "initial", init);
	else
		json_object_set_null_member (o, "initial");
	json_object_set_member (o, "blackboard", doc_json_or_empty (self, "", "blackboard"));
	json_object_set_member (o, "enter", doc_json_or_empty (self, "", "enter"));
	json_object_set_member (o, "update", doc_json_or_empty (self, "", "update"));
	json_object_set_member (o, "exit", doc_json_or_empty (self, "", "exit"));
	json_object_set_array_member (o, "states", sa);
	json_object_set_array_member (o, "transitions", ta);
	sel = json_object_new ();
	if (self->sel_state != NULL)
		json_object_set_string_member (sel, "state", self->sel_state);
	else
		json_object_set_null_member (sel, "state");
	if (self->sel_tr)
	{
		json_object_set_string_member (sel, "from", self->sel_from);
		json_object_set_int_member (sel, "index", self->sel_index);
	}
	else
	{
		json_object_set_null_member (sel, "from");
		json_object_set_int_member (sel, "index", 0);
	}
	json_object_set_object_member (o, "selected", sel);
	json_object_set_boolean_member (o, "dirty", lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (self)));
	if (self->load_error != NULL)
		json_object_set_string_member (o, "load_error", self->load_error);
	return editor_ai_object_node (o);
}

static JsonNode *
m_fsm_enter (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar        *state = self != NULL ? editor_ai_require_string (params, "state", error) : NULL;

	if (state == NULL)
		return NULL;
	if (!lrg_editor_fsm_editor_enter (self, state))
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such state");
		return NULL;
	}
	return str_result ("machine", self->shown);
}

static JsonNode *
m_fsm_up (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	if (self == NULL)
		return NULL;
	lrg_editor_fsm_editor_go_up (self);
	return str_result ("machine", self->shown);
}

static JsonNode *
m_fsm_add_state (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar        *name = self != NULL ? editor_ai_require_string (params, "name", error) : NULL;
	const gchar        *parent = editor_ai_param_string (params, "parent");
	g_autofree gchar   *path = NULL;
	g_autoptr(GError)   err = NULL;
	gdouble             x = -1.0, y = -1.0;

	if (name == NULL)
		return NULL;
	if (!editor_ai_param_number (params, "x", &x) || !editor_ai_param_number (params, "y", &y))
	{
		g_autoptr(GPtrArray) kids = child_names (self, parent != NULL ? parent : "");

		x = (gdouble) (kids->len % GRID_COLS) * GRID_DX;
		y = (gdouble) (kids->len / GRID_COLS) * GRID_DY;
	}
	path = lrg_editor_fsm_editor_add_state (self, parent != NULL ? parent : "", name, (gfloat) x, (gfloat) y, &err);
	if (path == NULL)
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return str_result ("state", path);
}

static JsonNode *
m_fsm_remove_state (LrgEditorServer  *server,
                    JsonObject       *params,
                    gpointer          user_data,
                    GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar        *state = self != NULL ? editor_ai_require_string (params, "state", error) : NULL;
	g_autoptr(GError)   err = NULL;

	if (state == NULL)
		return NULL;
	if (!lrg_editor_fsm_editor_remove_state (self, state, &err))
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return bool_result ("removed", TRUE);
}

static JsonNode *
m_fsm_rename_state (LrgEditorServer  *server,
                    JsonObject       *params,
                    gpointer          user_data,
                    GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar        *state = self != NULL ? editor_ai_require_string (params, "state", error) : NULL;
	const gchar        *name = state != NULL ? editor_ai_require_string (params, "name", error) : NULL;
	g_autofree gchar   *path = NULL;
	g_autoptr(GError)   err = NULL;

	if (name == NULL)
		return NULL;
	path = lrg_editor_fsm_editor_rename_state (self, state, name, &err);
	if (path == NULL)
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return str_result ("state", path);
}

static JsonNode *
m_fsm_set_initial (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar        *state = self != NULL ? editor_ai_require_string (params, "state", error) : NULL;
	g_autoptr(GError)   err = NULL;

	if (state == NULL)
		return NULL;
	if (!lrg_editor_fsm_editor_set_initial (self, state, &err))
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return str_result ("state", state);
}

/* Writes params.when / params.do / to / event / after of a transition. */
static gboolean
apply_transition (LrgEditorFsmEditor  *self,
                  const gchar         *from,
                  guint                index,
                  JsonObject          *params,
                  GError             **error)
{
	static const gchar * const lists[] = { "when", "do" };
	gboolean                   ok = TRUE;
	gboolean                   present;
	gdouble                    after;
	guint                      i;

	if (json_object_has_member (params, "to"))
	{
		const gchar      *to = editor_ai_param_string (params, "to");
		g_autofree gchar *y = to != NULL ? editor_ai_yaml_str (to) : NULL;

		if (y == NULL)
			return fail_arg (error, "'to' must be a string");
		ok = lrg_editor_fsm_editor_set_transition_field (self, from, index, "to", y, error);
	}
	if (ok && json_object_has_member (params, "event"))
	{
		const gchar      *ev = editor_ai_param_string (params, "event");
		g_autofree gchar *y = (ev != NULL && *ev != '\0') ? editor_ai_yaml_str (ev) : NULL;

		ok = lrg_editor_fsm_editor_set_transition_field (self, from, index, "event", y, error);
	}
	if (ok && json_object_has_member (params, "after"))
	{
		g_autofree gchar *y = editor_ai_param_number (params, "after", &after) ? editor_ai_num_str (after, TRUE) : NULL;

		ok = lrg_editor_fsm_editor_set_transition_field (self, from, index, "after", y, error);
	}
	for (i = 0; ok && i < G_N_ELEMENTS (lists); i++)
	{
		g_autoptr(GError) lerr = NULL;
		g_autofree gchar *y = list_yaml (params, lists[i], &present, &lerr);

		if (lerr != NULL)
		{
			g_propagate_error (error, g_steal_pointer (&lerr));
			return FALSE;
		}
		if (present)
			ok = lrg_editor_fsm_editor_set_transition_field (self, from, index, lists[i],
			                                                 (y != NULL && g_strcmp0 (y, "[]") != 0) ? y : NULL,
			                                                 error);
	}
	return ok;
}

static JsonNode *
m_fsm_add_transition (LrgEditorServer  *server,
                      JsonObject       *params,
                      gpointer          user_data,
                      GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar        *from = self != NULL ? editor_ai_require_string (params, "from", error) : NULL;
	const gchar        *to = from != NULL ? editor_ai_require_string (params, "to", error) : NULL;
	g_autoptr(GError)   err = NULL;
	gdouble             after = -1.0;
	gboolean            opened;
	gint                index;
	JsonObject         *o;

	if (to == NULL)
		return NULL;
	editor_ai_param_number (params, "after", &after);
	opened = edit_begin (self, "Add Transition");
	index = lrg_editor_fsm_editor_add_transition (self, from, to, editor_ai_param_string (params, "event"), after, &err);
	if (index >= 0)
	{
		g_autoptr(JsonObject) rest = json_object_new ();

		if (json_object_has_member (params, "when"))
			json_object_set_member (rest, "when", json_node_copy (json_object_get_member (params, "when")));
		if (json_object_has_member (params, "do"))
			json_object_set_member (rest, "do", json_node_copy (json_object_get_member (params, "do")));
		if (!apply_transition (self, from, (guint) index, rest, &err))
			index = -1;
	}
	edit_end (self, opened);
	if (index < 0)
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "from", from);
	json_object_set_int_member (o, "index", index);
	return editor_ai_object_node (o);
}

static gboolean
ipc_transition (JsonObject   *params,
                const gchar **from,
                guint        *index,
                GError      **error)
{
	gdouble i;

	*from = editor_ai_require_string (params, "from", error);
	if (*from == NULL)
		return FALSE;
	if (!editor_ai_param_number (params, "index", &i) || i < 0.0)
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Missing number parameter 'index'");
		return FALSE;
	}
	*index = (guint) i;
	return TRUE;
}

static JsonNode *
m_fsm_set_transition (LrgEditorServer  *server,
                      JsonObject       *params,
                      gpointer          user_data,
                      GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar        *from = NULL;
	guint               index = 0;
	g_autoptr(GError)   err = NULL;
	gboolean            opened;
	gboolean            ok;
	JsonObject         *o;

	if (self == NULL || !ipc_transition (params, &from, &index, error))
		return NULL;
	opened = edit_begin (self, "Edit Transition");
	ok = apply_transition (self, from, index, params, &err);
	edit_end (self, opened);
	if (!ok)
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "from", from);
	json_object_set_int_member (o, "index", index);
	return editor_ai_object_node (o);
}

static JsonNode *
m_fsm_remove_transition (LrgEditorServer  *server,
                         JsonObject       *params,
                         gpointer          user_data,
                         GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar        *from = NULL;
	guint               index = 0;
	g_autoptr(GError)   err = NULL;

	if (self == NULL || !ipc_transition (params, &from, &index, error))
		return NULL;
	if (!lrg_editor_fsm_editor_remove_transition (self, from, index, &err))
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return bool_result ("removed", TRUE);
}

static JsonNode *
m_fsm_set_actions (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar        *state = self != NULL ? editor_ai_param_string (params, "state") : NULL;
	const gchar        *phase = self != NULL ? editor_ai_require_string (params, "phase", error) : NULL;
	LrgLogicPhase       ph;
	gboolean            present;
	g_autofree gchar   *y = NULL;
	g_autoptr(GError)   err = NULL;
	JsonObject         *o;

	if (phase == NULL)
		return NULL;
	if (g_strcmp0 (phase, "enter") == 0)
		ph = LRG_LOGIC_PHASE_ENTER;
	else if (g_strcmp0 (phase, "update") == 0)
		ph = LRG_LOGIC_PHASE_UPDATE;
	else if (g_strcmp0 (phase, "exit") == 0)
		ph = LRG_LOGIC_PHASE_EXIT;
	else
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "'phase' is enter, update or exit");
		return NULL;
	}
	y = list_yaml (params, "actions", &present, &err);
	if (err != NULL || !present)
	{
		if (err != NULL)
			g_propagate_error (error, g_steal_pointer (&err));
		else
			editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Missing list parameter 'actions'");
		return NULL;
	}
	if (!lrg_editor_fsm_editor_set_actions (self, state != NULL ? state : "", ph, y, &err))
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_int_member (o, "count", json_array_get_length (json_object_get_array_member (params, "actions")));
	return editor_ai_object_node (o);
}

static JsonNode *
m_fsm_set_position (LrgEditorServer  *server,
                    JsonObject       *params,
                    gpointer          user_data,
                    GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar        *state = self != NULL ? editor_ai_require_string (params, "state", error) : NULL;
	gdouble             x, y;

	if (state == NULL)
		return NULL;
	if (!editor_ai_param_number (params, "x", &x) || !editor_ai_param_number (params, "y", &y))
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Missing number parameters 'x' and 'y'");
		return NULL;
	}
	if (!lrg_editor_fsm_editor_set_state_position (self, state, (gfloat) x, (gfloat) y))
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such state");
		return NULL;
	}
	return str_result ("state", state);
}

static JsonNode *
m_fsm_select (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	JsonObject         *o;
	gboolean            ok;

	if (self == NULL)
		return NULL;
	if (json_object_has_member (params, "from"))
	{
		const gchar *from = NULL;
		guint        index = 0;

		if (!ipc_transition (params, &from, &index, error))
			return NULL;
		ok = lrg_editor_fsm_editor_select_transition (self, from, index);
	}
	else
		ok = lrg_editor_fsm_editor_select_state (self, editor_ai_param_string (params, "state"));
	if (!ok)
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No such state or transition");
		return NULL;
	}
	o = json_object_new ();
	if (self->sel_state != NULL)
		json_object_set_string_member (o, "state", self->sel_state);
	else
		json_object_set_null_member (o, "state");
	if (self->sel_tr)
	{
		json_object_set_string_member (o, "from", self->sel_from);
		json_object_set_int_member (o, "index", self->sel_index);
	}
	return editor_ai_object_node (o);
}

static JsonNode *
m_fsm_add_key (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar        *name = self != NULL ? editor_ai_require_string (params, "name", error) : NULL;
	const gchar        *type = name != NULL ? editor_ai_require_string (params, "type", error) : NULL;
	g_autofree gchar   *def = NULL;
	g_autoptr(GError)   err = NULL;

	if (type == NULL)
		return NULL;
	if (json_object_has_member (params, "default") && !editor_ai_param_is_null (params, "default"))
		def = editor_ai_json_to_yaml (json_object_get_member (params, "default"));
	if (!lrg_editor_fsm_editor_add_key (self, name, type, def, &err))
	{
		editor_ai_fail (error, err);
		return NULL;
	}
	return str_result ("name", name);
}

static JsonNode *
m_fsm_remove_key (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar        *name = self != NULL ? editor_ai_require_string (params, "name", error) : NULL;

	if (name == NULL)
		return NULL;
	return bool_result ("removed", lrg_editor_fsm_editor_remove_key (self, name));
}

static JsonNode *
m_fsm_validate (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);

	return self != NULL ? editor_ai_issues_result (LRG_EDITOR_DATA_EDITOR (self)) : NULL;
}

static JsonNode *
m_fsm_save (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GError)   err = NULL;

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
m_fsm_live (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorApp       *app = LRG_EDITOR_APP (user_data);
	LrgEditorFsmEditor *self = ipc_editor (app, params, error);
	LrgLogicMachine    *m;
	JsonObject         *o;
	JsonArray          *active, *fires;

	if (self == NULL)
		return NULL;
	m = lrg_editor_fsm_editor_get_live_machine (self);
	o = json_object_new ();
	active = json_array_new ();
	fires = json_array_new ();
	json_object_set_boolean_member (o, "playing", lrg_editor_app_is_playing (app));
	json_object_set_boolean_member (o, "attached", m != NULL);
	if (m != NULL)
	{
		LrgAiComponent   *c = lrg_editor_ai_debug_panel_find_for_file (app,
		                        lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (self)), TRUE);
		g_autofree gchar *who = c != NULL ? lrg_editor_ai_debug_panel_dup_entity_id (app, c) : NULL;
		g_auto(GStrv)     path = lrg_logic_machine_dup_active_path (m);
		g_auto(GStrv)     states = lrg_editor_fsm_editor_dup_state_paths (self);
		const gchar      *lf = NULL, *lt = NULL, *le = NULL;
		guint             i;

		if (who != NULL)
			json_object_set_string_member (o, "entity", who);
		else
			json_object_set_null_member (o, "entity");
		json_object_set_boolean_member (o, "running", lrg_logic_machine_is_running (m));
		for (i = 0; path != NULL && path[i] != NULL; i++)
		{
			JsonObject *a = json_object_new ();

			json_object_set_string_member (a, "state", path[i]);
			json_object_set_double_member (a, "time", lrg_logic_machine_get_state_time (m, path[i]));
			json_array_add_object_element (active, a);
		}
		if (lrg_logic_machine_get_last_transition (m, &lf, &lt, &le) != NULL)
		{
			JsonObject *t = json_object_new ();

			json_object_set_string_member (t, "from", lf != NULL ? lf : "");
			json_object_set_string_member (t, "to", lt != NULL ? lt : "");
			if (le != NULL)
				json_object_set_string_member (t, "event", le);
			else
				json_object_set_null_member (t, "event");
			json_object_set_object_member (o, "last_transition", t);
		}
		else
			json_object_set_null_member (o, "last_transition");
		/* fire counts of every transition, the root's first */
		for (i = 0; ; i++)
		{
			const gchar   *owner = i == 0 ? "" : (states != NULL ? states[i - 1] : NULL);
			LrgLogicState *st;
			guint          k;

			if (owner == NULL)
				break;
			st = lrg_logic_machine_find_state (m, owner);
			for (k = 0; st != NULL && k < lrg_logic_state_get_transition_count (st); k++)
			{
				LrgLogicTransition *tr = lrg_logic_state_get_transition (st, k);
				JsonObject         *f = json_object_new ();

				json_object_set_string_member (f, "from", owner);
				json_object_set_int_member (f, "index", k);
				json_object_set_string_member (f, "to", lrg_logic_transition_get_target (tr));
				json_object_set_int_member (f, "count", (gint64) lrg_logic_transition_get_fire_count (tr));
				json_array_add_object_element (fires, f);
			}
		}
	}
	else
	{
		json_object_set_null_member (o, "entity");
		json_object_set_boolean_member (o, "running", FALSE);
		json_object_set_null_member (o, "last_transition");
	}
	json_object_set_array_member (o, "active", active);
	json_object_set_array_member (o, "queued", json_array_new ());
	json_object_set_array_member (o, "fires", fires);
	return editor_ai_object_node (o);
}

static JsonNode *
m_fsm_send_event (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorFsmEditor *self = ipc_editor (LRG_EDITOR_APP (user_data), params, error);
	const gchar        *ev = self != NULL ? editor_ai_require_string (params, "event", error) : NULL;

	if (ev == NULL)
		return NULL;
	if (!lrg_editor_fsm_editor_send_live_event (self, ev))
	{
		editor_ai_fail_msg (error, LRG_EDITOR_SERVER_ERROR_FAILED,
		                    "Nothing is live: play the level with an entity running this machine");
		return NULL;
	}
	return bool_result ("sent", TRUE);
}

void
lrg_editor_fsm_editor_install (LrgEditorApp *app)
{
	static const struct { const gchar *name; LrgEditorServerMethod func; } methods[] = {
		{ "fsm.open", m_fsm_open },
		{ "fsm.create", m_fsm_create },
		{ "fsm.get", m_fsm_get },
		{ "fsm.enter", m_fsm_enter },
		{ "fsm.up", m_fsm_up },
		{ "fsm.add_state", m_fsm_add_state },
		{ "fsm.remove_state", m_fsm_remove_state },
		{ "fsm.rename_state", m_fsm_rename_state },
		{ "fsm.set_initial", m_fsm_set_initial },
		{ "fsm.add_transition", m_fsm_add_transition },
		{ "fsm.set_transition", m_fsm_set_transition },
		{ "fsm.remove_transition", m_fsm_remove_transition },
		{ "fsm.set_actions", m_fsm_set_actions },
		{ "fsm.set_position", m_fsm_set_position },
		{ "fsm.select", m_fsm_select },
		{ "fsm.add_key", m_fsm_add_key },
		{ "fsm.remove_key", m_fsm_remove_key },
		{ "fsm.validate", m_fsm_validate },
		{ "fsm.save", m_fsm_save },
		{ "fsm.live", m_fsm_live },
		{ "fsm.send_event", m_fsm_send_event }
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
