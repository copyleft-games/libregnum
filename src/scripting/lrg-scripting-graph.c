/* lrg-scripting-graph.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interpreter for .rvscript programs. Control flow is an explicit stack
 * of frames: a step budget stops a loop, and delay / wait_frames park a
 * copy of that stack on a fiber. Fibers advance only from update(), by
 * the dt the host passes (or by one frame), never from the wall clock.
 * math.random reads the program seed through LrgRandomStream.
 *
 * Breakpoints park the same way and return. They do not call
 * lrg_script_debugger_report_node(): that blocks the calling thread.
 */

#include "config.h"

#ifndef __GI_SCANNER__
#define LRG_VSCRIPT_PRIVATE 1
#endif

#include "lrg-scripting-graph.h"

#include "../ai/lrg-blackboard.h"
#include "../core/lrg-random-stream.h"

#include <math.h>
#include <string.h>

typedef enum
{
	ACT_OK = 0,
	ACT_ERROR,
	ACT_YIELD,
	ACT_PAUSED
} Act;

typedef struct
{
	LrgVscriptGraph *graph;
	LrgVscriptGraph *callee;
	gchar           *node_id;
	guint            phase;
	gint             index;
	gint             limit;
	gboolean         broke;
	gboolean         is_call;
} VsFrame;

typedef struct
{
	GArray  *stack;
	gdouble  left;
	gboolean frames_wait;
	guint    born;
} Fiber;

typedef struct
{
	gchar   *event;
	gchar   *signal;
	GArray  *values;
} Pending;

typedef struct
{
	LrgScriptingGraph *owner;
	GObject           *object;
	gulong             id;
	gchar             *event;
	gchar             *signal;
} Hook;

typedef struct
{
	LrgScriptingCFunction func;
	gpointer              user_data;
} HostFn;

typedef struct
{
	gchar *path;
	gchar *node;
} StaticBp;

typedef struct
{
	LrgVscriptProgram *program;
	gchar             *path;
	gchar             *rng_state;
	GHashTable        *vars;
	GPtrArray         *log;
	GPtrArray         *fibers;
	GPtrArray         *held;
	gdouble            acc;
	gdouble            clock;
	gboolean           paused;
	gchar             *paused_node;
	GArray            *paused_stack;
} Snap;

struct _LrgScriptingGraph
{
	LrgScripting       parent_instance;
	LrgVscriptProgram *program;
	gchar             *path;
	LrgRandomStream   *rng;
	GHashTable        *vars;
	GHashTable        *extras;
	GObject           *self_object;
	LrgBlackboard     *blackboard;
	GHashTable        *hosts;
	GPtrArray         *log;
	GPtrArray         *fibers;
	GHashTable        *hits;
	GPtrArray         *active;
	GQueue            *recent;
	GPtrArray         *bps;
	GPtrArray         *held;
	GPtrArray         *hooks;
	GArray            *pending;
	GHashTable        *out_cache;
	GHashTable        *pure_cache;
	GHashTable        *pure_done;
	GHashTable        *pulling;
	GArray            *stack;
	GArray            *paused_stack;
	GValue             last_result;
	gboolean           have_result;
	gboolean           running;
	gboolean           paused;
	gchar             *paused_node;
	guint              steps;
	guint              last_steps;
	gdouble            acc;
	gdouble            clock;
	guint              update_gen;
	gint               pull_depth;
};

G_DEFINE_FINAL_TYPE (LrgScriptingGraph, lrg_scripting_graph, LRG_TYPE_SCRIPTING)

static GPtrArray *live_graphs;
static GPtrArray *static_bps;
static guint      sig_executed;
static guint      sig_logged;

static void fiber_free (gpointer data);
static void hook_free (gpointer data);
static void lrg_scripting_graph_class_init (LrgScriptingGraphClass *klass);
static void lrg_scripting_graph_init (LrgScriptingGraph *self);
static Act  run_stack (LrgScriptingGraph *self, GArray *stack, GError **error);
static Act  dispatch_kind (LrgScriptingGraph *self, const gchar *kind, const gchar *param, const gchar *match, const GValue *args, guint n_args, gdouble dt, gboolean have_dt, GError **error);

static void
value_free (gpointer data)
{
	GValue *value = (GValue *) data;

	if (value != NULL && G_IS_VALUE (value))
		g_value_unset (value);
	g_free (value);
}

static void
gvalue_clear (gpointer data)
{
	GValue *value = (GValue *) data;

	if (G_IS_VALUE (value))
		g_value_unset (value);
}

static void
frame_clear (gpointer data)
{
	VsFrame *frame = (VsFrame *) data;

	g_free (frame->node_id);
	frame->node_id = NULL;
}

static void
value_set_copy (GValue       *dst,
                const GValue *src)
{
	if (G_IS_VALUE (dst))
		g_value_unset (dst);
	if (src != NULL && G_IS_VALUE (src))
	{
		g_value_init (dst, G_VALUE_TYPE (src));
		g_value_copy (src, dst);
	}
}

static GArray *
stack_new (void)
{
	GArray *stack;

	stack = g_array_new (FALSE, TRUE, sizeof (VsFrame));
	g_array_set_clear_func (stack, frame_clear);
	return stack;
}

static GArray *
stack_copy (GArray *stack)
{
	GArray *copy;
	guint   i;

	copy = stack_new ();
	for (i = 0; i < stack->len; i++)
	{
		VsFrame src = g_array_index (stack, VsFrame, i);
		VsFrame dst = src;

		dst.node_id = g_strdup (src.node_id);
		g_array_append_val (copy, dst);
	}
	return copy;
}

static void
fiber_free (gpointer data)
{
	Fiber *fiber = (Fiber *) data;

	if (fiber->stack != NULL)
		g_array_free (fiber->stack, TRUE);
	g_free (fiber);
}

static void
pending_clear_one (Pending *pending)
{
	g_free (pending->event);
	g_free (pending->signal);
	if (pending->values != NULL)
		g_array_free (pending->values, TRUE);
	pending->event = NULL;
	pending->signal = NULL;
	pending->values = NULL;
}

static void
hook_object_died (gpointer data,
                  GObject *dead)
{
	Hook *hook = (Hook *) data;

	(void) dead;
	hook->object = NULL;
	hook->id = 0;
}

static void
hook_free (gpointer data)
{
	Hook *hook = (Hook *) data;

	if (hook->object != NULL)
	{
		g_object_weak_unref (hook->object, hook_object_died, hook);
		if (hook->id != 0)
			g_signal_handler_disconnect (hook->object, hook->id);
	}
	g_free (hook->event);
	g_free (hook->signal);
	g_free (hook);
}

static void
static_bp_free (gpointer data)
{
	StaticBp *bp = (StaticBp *) data;

	g_free (bp->path);
	g_free (bp->node);
	g_free (bp);
}

static void
track_graph (LrgScriptingGraph *self)
{
	if (live_graphs == NULL)
		live_graphs = g_ptr_array_new ();
	g_ptr_array_add (live_graphs, self);
}

static void
untrack_graph (LrgScriptingGraph *self)
{
	if (live_graphs == NULL)
		return;
	g_ptr_array_remove (live_graphs, self);
}

static Act
fail (LrgScriptingGraph *self,
      GError           **error,
      gint               code,
      const gchar       *fmt,
      ...)
{
	va_list ap;
	gchar *msg;

	(void) self;
	if (error != NULL && *error == NULL)
	{
		va_start (ap, fmt);
		msg = g_strdup_vprintf (fmt, ap);
		va_end (ap);
		g_set_error_literal (error, LRG_VSCRIPT_ERROR, code, msg);
		g_free (msg);
	}
	return ACT_ERROR;
}

static gchar *
cache_key (LrgVscriptGraph *graph,
           const gchar     *node,
           const gchar     *pin)
{
	return g_strdup_printf ("%p\037%s\037%s", (void *) graph, node != NULL ? node : "", pin != NULL ? pin : "");
}

static void
cache_put (LrgScriptingGraph *self,
           GHashTable        *table,
           LrgVscriptGraph   *graph,
           const gchar       *node,
           const gchar       *pin,
           const GValue      *value)
{
	GValue *copy;
	gchar  *key;

	copy = g_new0 (GValue, 1);
	value_set_copy (copy, value);
	key = cache_key (graph, node, pin);
	g_hash_table_insert (table, key, copy);
	if (g_strcmp0 (pin, "result") == 0)
	{
		value_set_copy (&self->last_result, value);
		self->have_result = G_IS_VALUE (&self->last_result);
	}
}

static gboolean
cache_get (GHashTable      *table,
           LrgVscriptGraph *graph,
           const gchar     *node,
           const gchar     *pin,
           GValue          *out)
{
	gchar  *key;
	GValue *found;

	key = cache_key (graph, node, pin);
	found = (GValue *) g_hash_table_lookup (table, key);
	g_free (key);
	if (found == NULL)
		return FALSE;
	value_set_copy (out, found);
	return TRUE;
}

static void
note_enter (LrgScriptingGraph *self,
            LrgVscriptGraph   *graph,
            const gchar       *node)
{
	guint *count;
	const gchar *gname;

	count = (guint *) g_hash_table_lookup (self->hits, node);
	if (count == NULL)
	{
		count = g_new0 (guint, 1);
		g_hash_table_insert (self->hits, g_strdup (node), count);
	}
	*count += 1;
	g_ptr_array_add (self->active, g_strdup (node));
	gname = lrg_vscript_graph_get_name (graph);
	if (gname == NULL)
		gname = "";
	g_signal_emit (self, sig_executed, 0, gname, node);
}

static gboolean
budget_left (LrgScriptingGraph *self,
             GError           **error)
{
	guint budget;

	budget = LRG_VSCRIPT_DEFAULT_STEP_BUDGET;
	if (self->program != NULL)
		budget = lrg_vscript_program_get_step_budget (self->program);
	if (self->steps >= budget)
	{
		fail (self, error, LRG_VSCRIPT_ERROR_BUDGET,
		      "step budget of %u exhausted", budget);
		return FALSE;
	}
	return TRUE;
}

static VsFrame *
frame_at (GArray *stack,
          guint   index)
{
	return &g_array_index (stack, VsFrame, index);
}

static gboolean
push_link (GArray          *stack,
           LrgVscriptGraph *graph,
           const gchar     *node,
           const gchar     *pin)
{
	const LrgVscriptLink *link;
	VsFrame               frame;

	link = lrg_vscript_graph_find_link_from (graph, node, pin);
	if (link == NULL)
		return FALSE;
	memset (&frame, 0, sizeof frame);
	frame.graph = graph;
	frame.node_id = g_strdup (link->to_node);
	g_array_append_val (stack, frame);
	return TRUE;
}

static void
follow_pin (GArray          *stack,
            LrgVscriptGraph *graph,
            const gchar     *node,
            const gchar     *pin)
{
	g_array_remove_index (stack, stack->len - 1);
	push_link (stack, graph, node, pin);
}

static VsFrame *
find_call (GArray          *stack,
           LrgVscriptGraph *callee)
{
	guint i;

	if (stack == NULL)
		return NULL;
	for (i = stack->len; i > 0; i--)
	{
		VsFrame *frame = frame_at (stack, i - 1);

		if (frame->is_call && frame->callee == callee)
			return frame;
	}
	return NULL;
}

static LrgVscriptPin *
find_pin (GPtrArray              *pins,
          const gchar            *name,
          LrgVscriptPinDirection  dir)
{
	guint i;

	for (i = 0; pins != NULL && i < pins->len; i++)
	{
		LrgVscriptPin *pin = (LrgVscriptPin *) g_ptr_array_index (pins, i);

		if (pin->direction == dir && g_strcmp0 (pin->name, name) == 0)
			return pin;
	}
	return NULL;
}

static gint64
param_int (LrgVscriptNode *node,
           const gchar    *key,
           gint64          fallback)
{
	GVariant *variant;
	GValue    value = G_VALUE_INIT;
	gint64    out;

	variant = lrg_vscript_node_get_param (node, key);
	if (variant == NULL)
		return fallback;
	lrg_vscript_priv_variant_to_value (variant, &value);
	out = lrg_vscript_priv_value_to_int (&value);
	g_value_unset (&value);
	return out;
}

static void
set_double (GValue  *value,
            gdouble  number)
{
	if (G_IS_VALUE (value))
		g_value_unset (value);
	g_value_init (value, G_TYPE_DOUBLE);
	g_value_set_double (value, number);
}

static void
set_int (GValue *value,
         gint64  number)
{
	if (G_IS_VALUE (value))
		g_value_unset (value);
	g_value_init (value, G_TYPE_INT64);
	g_value_set_int64 (value, number);
}

static void
set_bool (GValue  *value,
          gboolean flag)
{
	if (G_IS_VALUE (value))
		g_value_unset (value);
	g_value_init (value, G_TYPE_BOOLEAN);
	g_value_set_boolean (value, flag);
}

static void
set_string (GValue      *value,
            const gchar *text)
{
	if (G_IS_VALUE (value))
		g_value_unset (value);
	g_value_init (value, G_TYPE_STRING);
	g_value_set_string (value, text != NULL ? text : "");
}

static gboolean eval_pure (LrgScriptingGraph *self, LrgVscriptGraph *graph, LrgVscriptNode *node, GError **error);
static gboolean pull_input (LrgScriptingGraph *self, LrgVscriptGraph *graph, LrgVscriptNode *node, const gchar *pin, GValue *out, GError **error);

static gboolean
pull_input (LrgScriptingGraph *self,
            LrgVscriptGraph   *graph,
            LrgVscriptNode    *node,
            const gchar       *pin_name,
            GValue            *out,
            GError           **error)
{
	g_autoptr(GPtrArray) pins = NULL;
	LrgVscriptPin        *pin;
	const LrgVscriptLink *link;
	LrgVscriptNode       *src;
	const gchar          *src_kind;
	GVariant             *literal;

	pins = lrg_vscript_program_dup_node_pins (self->program, graph, node);
	pin = find_pin (pins, pin_name, LRG_VSCRIPT_PIN_INPUT);
	link = lrg_vscript_graph_find_link_into (graph, lrg_vscript_node_get_id (node), pin_name);
	if (link != NULL)
	{
		src = lrg_vscript_graph_get_node (graph, link->from_node);
		if (src == NULL)
			return fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "missing source '%s'", link->from_node) == ACT_OK;
		src_kind = lrg_vscript_node_get_kind (src);
		if (g_strcmp0 (src_kind, "subgraph.input") == 0)
		{
			VsFrame *call = find_call (self->stack, graph);
			LrgVscriptNode *caller;

			if (call == NULL)
			{
				lrg_vscript_priv_zero_value (pin != NULL ? pin->type : "any", out);
				return TRUE;
			}
			caller = lrg_vscript_graph_get_node (call->graph, call->node_id);
			if (caller == NULL)
				return fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "missing subgraph call") == ACT_OK;
			return pull_input (self, call->graph, caller, link->from_pin, out, error);
		}
		if (lrg_vscript_node_type_is_pure (src_kind))
		{
			if (!eval_pure (self, graph, src, error))
				return FALSE;
			if (!cache_get (self->pure_cache, graph, link->from_node, link->from_pin, out))
				lrg_vscript_priv_zero_value (pin != NULL ? pin->type : "any", out);
			return TRUE;
		}
		if (!cache_get (self->out_cache, graph, link->from_node, link->from_pin, out))
			lrg_vscript_priv_zero_value (pin != NULL ? pin->type : "any", out);
		return TRUE;
	}
	literal = lrg_vscript_node_get_input (node, pin_name);
	if (literal != NULL)
	{
		lrg_vscript_priv_variant_to_value (literal, out);
		return TRUE;
	}
	lrg_vscript_priv_zero_value (pin != NULL ? pin->type : "any", out);
	return TRUE;
}

static gboolean
store_var (LrgScriptingGraph *self,
           const gchar       *name,
           const GValue      *value)
{
	GValue *copy;

	if (name == NULL || name[0] == '\0')
		return FALSE;
	copy = g_new0 (GValue, 1);
	value_set_copy (copy, value);
	g_hash_table_insert (self->vars, g_strdup (name), copy);
	return TRUE;
}

static int
cmp_values (const GValue *a,
            const GValue *b)
{
	g_autofree gchar *sa = NULL;
	g_autofree gchar *sb = NULL;
	gdouble           da;
	gdouble           db;

	if (lrg_vscript_priv_value_is_number (a) && lrg_vscript_priv_value_is_number (b))
	{
		da = lrg_vscript_priv_value_to_double (a);
		db = lrg_vscript_priv_value_to_double (b);
		if (da < db)
			return -1;
		if (da > db)
			return 1;
		return 0;
	}
	if (G_VALUE_HOLDS_BOOLEAN (a) && G_VALUE_HOLDS_BOOLEAN (b))
		return (g_value_get_boolean (a) ? 1 : 0) - (g_value_get_boolean (b) ? 1 : 0);
	sa = lrg_vscript_priv_value_to_string (a);
	sb = lrg_vscript_priv_value_to_string (b);
	return strcmp (sa, sb);
}

static gboolean
eval_math (LrgScriptingGraph *self,
           LrgVscriptGraph   *graph,
           LrgVscriptNode    *node,
           const gchar       *kind,
           GError           **error)
{
	GValue  a = G_VALUE_INIT;
	GValue  b = G_VALUE_INIT;
	GValue  c = G_VALUE_INIT;
	GValue  out = G_VALUE_INIT;
	gdouble da;
	gdouble db;
	gdouble dc;
	gdouble r;
	gboolean ok = TRUE;

	if (g_strcmp0 (kind, "math.random") == 0)
	{
		set_double (&out, lrg_random_stream_next_double (self->rng));
		cache_put (self, self->pure_cache, graph, lrg_vscript_node_get_id (node), "result", &out);
		g_value_unset (&out);
		return TRUE;
	}
	if (g_str_has_prefix (kind, "math.random"))
	{
		if (!pull_input (self, graph, node, "min", &a, error) ||
		    !pull_input (self, graph, node, "max", &b, error))
			ok = FALSE;
		else if (g_strcmp0 (kind, "math.random_int") == 0)
		{
			gint64 lo = lrg_vscript_priv_value_to_int (&a);
			gint64 hi = lrg_vscript_priv_value_to_int (&b);
			gint64 tmp;
			gint64 result;

			if (hi < lo)
			{
				tmp = lo;
				lo = hi;
				hi = tmp;
			}
			if ((lo < 0 && hi > G_MAXINT64 + lo) || (hi - lo) >= (gint64) G_MAXUINT32)
				result = lo;
			else
				result = lo + (gint64) lrg_random_stream_bounded (self->rng, (guint32) (hi - lo) + 1);
			set_int (&out, result);
		}
		else
		{
			da = lrg_vscript_priv_value_to_double (&a);
			db = lrg_vscript_priv_value_to_double (&b);
			set_double (&out, da + lrg_random_stream_next_double (self->rng) * (db - da));
		}
	}
	else if (g_strcmp0 (kind, "math.neg") == 0 || g_strcmp0 (kind, "math.abs") == 0 ||
	         g_strcmp0 (kind, "math.floor") == 0 || g_strcmp0 (kind, "math.ceil") == 0 ||
	         g_strcmp0 (kind, "math.round") == 0 || g_strcmp0 (kind, "math.sqrt") == 0 ||
	         g_strcmp0 (kind, "math.sin") == 0 || g_strcmp0 (kind, "math.cos") == 0)
	{
		if (!pull_input (self, graph, node, "a", &a, error))
			ok = FALSE;
		else
		{
			da = lrg_vscript_priv_value_to_double (&a);
			r = 0.0;
			if (g_strcmp0 (kind, "math.neg") == 0)
				r = -da;
			else if (g_strcmp0 (kind, "math.abs") == 0)
				r = fabs (da);
			else if (g_strcmp0 (kind, "math.floor") == 0)
				r = floor (da);
			else if (g_strcmp0 (kind, "math.ceil") == 0)
				r = ceil (da);
			else if (g_strcmp0 (kind, "math.round") == 0)
				r = da >= 0.0 ? floor (da + 0.5) : ceil (da - 0.5);
			else if (g_strcmp0 (kind, "math.sqrt") == 0)
				r = da < 0.0 ? 0.0 : sqrt (da);
			else if (g_strcmp0 (kind, "math.sin") == 0)
				r = sin (da);
			else
				r = cos (da);
			set_double (&out, r);
		}
	}
	else
	{
		if (!pull_input (self, graph, node, "a", &a, error) ||
		    !pull_input (self, graph, node, "b", &b, error))
			ok = FALSE;
		else if (g_strcmp0 (kind, "math.clamp") == 0 || g_strcmp0 (kind, "math.lerp") == 0)
		{
			if (!pull_input (self, graph, node, g_strcmp0 (kind, "math.clamp") == 0 ? "min" : "t", &c, error))
				ok = FALSE;
			else if (g_strcmp0 (kind, "math.clamp") == 0)
			{
				GValue mx = G_VALUE_INIT;

				if (!pull_input (self, graph, node, "max", &mx, error))
					ok = FALSE;
				else
				{
					da = lrg_vscript_priv_value_to_double (&a);
					db = lrg_vscript_priv_value_to_double (&c);
					dc = lrg_vscript_priv_value_to_double (&mx);
					if (da < db)
						da = db;
					if (da > dc)
						da = dc;
					set_double (&out, da);
				}
				if (G_IS_VALUE (&mx))
					g_value_unset (&mx);
			}
			else
			{
				da = lrg_vscript_priv_value_to_double (&a);
				db = lrg_vscript_priv_value_to_double (&b);
				dc = lrg_vscript_priv_value_to_double (&c);
				set_double (&out, da + (db - da) * dc);
			}
		}
		else
		{
			da = lrg_vscript_priv_value_to_double (&a);
			db = lrg_vscript_priv_value_to_double (&b);
			r = 0.0;
			if (g_strcmp0 (kind, "math.add") == 0)
				r = da + db;
			else if (g_strcmp0 (kind, "math.sub") == 0)
				r = da - db;
			else if (g_strcmp0 (kind, "math.mul") == 0)
				r = da * db;
			else if (g_strcmp0 (kind, "math.div") == 0)
				r = db == 0.0 ? 0.0 : da / db;
			else if (g_strcmp0 (kind, "math.mod") == 0)
				r = db == 0.0 ? 0.0 : fmod (da, db);
			else if (g_strcmp0 (kind, "math.min") == 0)
				r = da < db ? da : db;
			else if (g_strcmp0 (kind, "math.max") == 0)
				r = da > db ? da : db;
			else
				r = pow (da, db);
			set_double (&out, r);
		}
	}
	if (ok && G_IS_VALUE (&out))
		cache_put (self, self->pure_cache, graph, lrg_vscript_node_get_id (node), "result", &out);
	if (G_IS_VALUE (&a))
		g_value_unset (&a);
	if (G_IS_VALUE (&b))
		g_value_unset (&b);
	if (G_IS_VALUE (&c))
		g_value_unset (&c);
	if (G_IS_VALUE (&out))
		g_value_unset (&out);
	return ok;
}

static gboolean
eval_pure (LrgScriptingGraph *self,
           LrgVscriptGraph   *graph,
           LrgVscriptNode    *node,
           GError           **error)
{
	const gchar *kind;
	const gchar *id;
	gchar       *done_key;
	GValue       out = G_VALUE_INIT;
	GValue       a = G_VALUE_INIT;
	GValue       b = G_VALUE_INIT;
	gboolean     ok = TRUE;

	kind = lrg_vscript_node_get_kind (node);
	id = lrg_vscript_node_get_id (node);
	done_key = g_strdup_printf ("%p\037%s", (void *) graph, id);
	if (g_hash_table_contains (self->pure_done, done_key))
	{
		g_free (done_key);
		return TRUE;
	}
	if (g_hash_table_contains (self->pulling, done_key))
	{
		g_free (done_key);
		fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "data cycle at '%s'", id);
		return FALSE;
	}
	if (!budget_left (self, error))
	{
		g_free (done_key);
		return FALSE;
	}
	if (self->pull_depth > 64)
	{
		g_free (done_key);
		fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "data pull too deep");
		return FALSE;
	}
	self->steps++;
	g_hash_table_insert (self->pulling, g_strdup (done_key), GINT_TO_POINTER (1));
	self->pull_depth++;
	note_enter (self, graph, id);

	if (g_str_has_prefix (kind, "value."))
	{
		GVariant *variant = lrg_vscript_node_get_param (node, "value");

		if (g_strcmp0 (kind, "value.self") == 0)
		{
			g_value_init (&out, G_TYPE_OBJECT);
			g_value_set_object (&out, self->self_object);
		}
		else if (g_strcmp0 (kind, "value.list") == 0)
		{
			const gchar *items = lrg_vscript_node_get_param_string (node, "items");
			g_auto(GStrv) parts = g_strsplit (items != NULL ? items : "", ",", -1);
			GPtrArray *list = lrg_vscript_priv_list_new ();
			guint i;

			for (i = 0; parts[i] != NULL; i++)
			{
				g_autofree gchar *piece = g_strdup (parts[i]);
				g_autoptr(GVariant) lit = NULL;
				GValue one = G_VALUE_INIT;

				g_strstrip (piece);
				if (piece[0] == '\0')
					continue;
				lit = lrg_vscript_priv_parse_literal (piece);
				lrg_vscript_priv_variant_to_value (lit, &one);
				lrg_vscript_priv_list_append (list, &one);
				g_value_unset (&one);
			}
			g_value_init (&out, lrg_vscript_priv_list_get_type ());
			g_value_take_boxed (&out, list);
		}
		else if (variant != NULL)
			lrg_vscript_priv_variant_to_value (variant, &out);
		else if (g_strcmp0 (kind, "value.bool") == 0)
			set_bool (&out, FALSE);
		else if (g_strcmp0 (kind, "value.int") == 0)
			set_int (&out, 0);
		else if (g_strcmp0 (kind, "value.float") == 0)
			set_double (&out, 0.0);
		else
			set_string (&out, "");
		cache_put (self, self->pure_cache, graph, id, "value", &out);
	}
	else if (g_strcmp0 (kind, "var.get") == 0)
	{
		const gchar *name = lrg_vscript_node_get_param_string (node, "name");
		GValue *slot = name != NULL ? (GValue *) g_hash_table_lookup (self->vars, name) : NULL;
		const gchar *type = lrg_vscript_program_get_variable_type (self->program, name);

		if (slot != NULL)
			value_set_copy (&out, slot);
		else
			lrg_vscript_priv_zero_value (type != NULL ? type : "any", &out);
		cache_put (self, self->pure_cache, graph, id, "value", &out);
	}
	else if (g_strcmp0 (kind, "blackboard.get") == 0)
	{
		if (self->blackboard == NULL)
			ok = fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "no blackboard") == ACT_OK;
		else if (!pull_input (self, graph, node, "key", &a, error))
			ok = FALSE;
		else
		{
			g_autofree gchar *key = lrg_vscript_priv_value_to_string (&a);
			g_autoptr(GVariant) variant = lrg_blackboard_dup_variant (self->blackboard, key);

			lrg_vscript_priv_variant_to_value (variant, &out);
			cache_put (self, self->pure_cache, graph, id, "value", &out);
		}
	}
	else if (g_str_has_prefix (kind, "math."))
		ok = eval_math (self, graph, node, kind, error);
	else if (g_str_has_prefix (kind, "logic."))
	{
		gboolean ba;
		gboolean bb;
		gboolean r = FALSE;

		if (!pull_input (self, graph, node, "a", &a, error))
			ok = FALSE;
		else
		{
			ba = lrg_vscript_priv_value_to_bool (&a);
			if (g_strcmp0 (kind, "logic.not") == 0)
				r = !ba;
			else if (!pull_input (self, graph, node, "b", &b, error))
				ok = FALSE;
			else
			{
				bb = lrg_vscript_priv_value_to_bool (&b);
				if (g_strcmp0 (kind, "logic.and") == 0)
					r = ba && bb;
				else if (g_strcmp0 (kind, "logic.or") == 0)
					r = ba || bb;
				else
					r = ba != bb;
			}
			if (ok)
			{
				set_bool (&out, r);
				cache_put (self, self->pure_cache, graph, id, "result", &out);
			}
		}
	}
	else if (g_str_has_prefix (kind, "compare."))
	{
		gint cmp;
		gboolean r = FALSE;

		if (!pull_input (self, graph, node, "a", &a, error) ||
		    !pull_input (self, graph, node, "b", &b, error))
			ok = FALSE;
		else
		{
			cmp = cmp_values (&a, &b);
			if (g_strcmp0 (kind, "compare.eq") == 0)
				r = cmp == 0;
			else if (g_strcmp0 (kind, "compare.ne") == 0)
				r = cmp != 0;
			else if (g_strcmp0 (kind, "compare.lt") == 0)
				r = cmp < 0;
			else if (g_strcmp0 (kind, "compare.le") == 0)
				r = cmp <= 0;
			else if (g_strcmp0 (kind, "compare.gt") == 0)
				r = cmp > 0;
			else
				r = cmp >= 0;
			set_bool (&out, r);
			cache_put (self, self->pure_cache, graph, id, "result", &out);
		}
	}
	else if (g_strcmp0 (kind, "string.concat") == 0)
	{
		g_autofree gchar *sa = NULL;
		g_autofree gchar *sb = NULL;
		g_autofree gchar *both = NULL;

		if (!pull_input (self, graph, node, "a", &a, error) ||
		    !pull_input (self, graph, node, "b", &b, error))
			ok = FALSE;
		else
		{
			sa = lrg_vscript_priv_value_to_string (&a);
			sb = lrg_vscript_priv_value_to_string (&b);
			both = g_strconcat (sa, sb, NULL);
			set_string (&out, both);
			cache_put (self, self->pure_cache, graph, id, "result", &out);
		}
	}
	else if (g_strcmp0 (kind, "string.from") == 0)
	{
		g_autofree gchar *text = NULL;

		/* any value as its text form (the same text debug.log prints) */
		if (!pull_input (self, graph, node, "value", &a, error))
			ok = FALSE;
		else
		{
			text = lrg_vscript_priv_value_to_string (&a);
			set_string (&out, text);
			cache_put (self, self->pure_cache, graph, id, "result", &out);
		}
	}
	else if (g_strcmp0 (kind, "string.length") == 0)
	{
		g_autofree gchar *text = NULL;

		if (!pull_input (self, graph, node, "text", &a, error))
			ok = FALSE;
		else
		{
			text = lrg_vscript_priv_value_to_string (&a);
			set_int (&out, (gint64) g_utf8_strlen (text, -1));
			cache_put (self, self->pure_cache, graph, id, "result", &out);
		}
	}
	else if (g_strcmp0 (kind, "string.contains") == 0)
	{
		g_autofree gchar *text = NULL;
		g_autofree gchar *part = NULL;

		if (!pull_input (self, graph, node, "text", &a, error) ||
		    !pull_input (self, graph, node, "part", &b, error))
			ok = FALSE;
		else
		{
			text = lrg_vscript_priv_value_to_string (&a);
			part = lrg_vscript_priv_value_to_string (&b);
			set_bool (&out, g_strrstr (text, part) != NULL);
			cache_put (self, self->pure_cache, graph, id, "result", &out);
		}
	}
	else if (g_strcmp0 (kind, "string.format") == 0)
	{
		const gchar *fmt = lrg_vscript_node_get_param_string (node, "format");
		GString *built = g_string_new ("");
		const gchar *p = fmt != NULL ? fmt : "";

		while (ok && *p != '\0')
		{
			if (p[0] == '{' && p[1] == '{')
			{
				g_string_append_c (built, '{');
				p += 2;
			}
			else if (p[0] == '}' && p[1] == '}')
			{
				g_string_append_c (built, '}');
				p += 2;
			}
			else if (p[0] == '{')
			{
				const gchar *end = strchr (p + 1, '}');
				g_autofree gchar *name = NULL;
				GValue arg = G_VALUE_INIT;
				g_autofree gchar *text = NULL;

				if (end == NULL)
				{
					g_string_append (built, p);
					break;
				}
				name = g_strndup (p + 1, (gsize) (end - (p + 1)));
				g_strstrip (name);
				if (!pull_input (self, graph, node, name, &arg, error))
					ok = FALSE;
				else
				{
					text = lrg_vscript_priv_value_to_string (&arg);
					g_string_append (built, text);
					g_value_unset (&arg);
				}
				p = end + 1;
			}
			else
			{
				g_string_append_c (built, *p);
				p++;
			}
		}
		if (ok)
		{
			set_string (&out, built->str);
			cache_put (self, self->pure_cache, graph, id, "result", &out);
		}
		g_string_free (built, TRUE);
	}
	else if (g_strcmp0 (kind, "gi.get") == 0)
	{
		const gchar *prop = lrg_vscript_node_get_param_string (node, "property");
		GParamSpec *pspec = NULL;

		if (prop == NULL)
			prop = lrg_vscript_node_get_param_string (node, "name");
		if (!pull_input (self, graph, node, "object", &a, error))
			ok = FALSE;
		else if (!G_VALUE_HOLDS_OBJECT (&a) || g_value_get_object (&a) == NULL || prop == NULL)
			ok = fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "gi.get needs an object and a property") == ACT_OK;
		else
		{
			pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (g_value_get_object (&a)), prop);
			if (pspec == NULL)
				ok = fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "no property '%s'", prop) == ACT_OK;
			else
			{
				g_value_init (&out, pspec->value_type);
				g_object_get_property (G_OBJECT (g_value_get_object (&a)), prop, &out);
				cache_put (self, self->pure_cache, graph, id, "value", &out);
			}
		}
	}
	else if (g_strcmp0 (kind, "gi.call") == 0)
	{
		g_autoptr(GPtrArray) pins = lrg_vscript_program_dup_node_pins (self->program, graph, node);
		GArray *inputs = g_array_new (FALSE, TRUE, sizeof (GValue));
		GArray *outputs = g_array_new (FALSE, TRUE, sizeof (GValue));
		const gchar *fn = lrg_vscript_node_get_param_string (node, "function");
		guint i;

		g_array_set_clear_func (inputs, gvalue_clear);
		g_array_set_clear_func (outputs, gvalue_clear);
		for (i = 0; ok && i < pins->len; i++)
		{
			LrgVscriptPin *pin = (LrgVscriptPin *) g_ptr_array_index (pins, i);
			GValue one = G_VALUE_INIT;

			if (pin->exec || pin->direction != LRG_VSCRIPT_PIN_INPUT)
				continue;
			if (!pull_input (self, graph, node, pin->name, &one, error))
				ok = FALSE;
			else
				g_array_append_val (inputs, one);
		}
		if (ok)
		{
			if (!lrg_vscript_priv_gi_invoke (fn, inputs->len > 0 ? &g_array_index (inputs, GValue, 0) : NULL,
			                                 inputs->len, outputs, error))
				ok = FALSE;
			else if (outputs->len > 0)
				cache_put (self, self->pure_cache, graph, id, "result", &g_array_index (outputs, GValue, 0));
		}
		g_array_free (inputs, TRUE);
		g_array_free (outputs, TRUE);
	}
	else
		ok = fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "cannot evaluate '%s'", kind) == ACT_OK;

	self->pull_depth--;
	g_hash_table_remove (self->pulling, done_key);
	if (ok)
		g_hash_table_insert (self->pure_done, done_key, GINT_TO_POINTER (1));
	else
		g_free (done_key);
	if (G_IS_VALUE (&out))
		g_value_unset (&out);
	if (G_IS_VALUE (&a))
		g_value_unset (&a);
	if (G_IS_VALUE (&b))
		g_value_unset (&b);
	return ok;
}

static void
clear_pure (LrgScriptingGraph *self)
{
	g_hash_table_remove_all (self->pure_cache);
	g_hash_table_remove_all (self->pure_done);
	g_hash_table_remove_all (self->pulling);
}

static gboolean
breakpoint_hit (LrgScriptingGraph *self,
                const gchar       *node)
{
	guint i;

	for (i = 0; i < self->bps->len; i++)
	{
		if (g_strcmp0 ((const gchar *) g_ptr_array_index (self->bps, i), node) == 0)
			return TRUE;
	}
	if (self->path == NULL || static_bps == NULL)
		return FALSE;
	for (i = 0; i < static_bps->len; i++)
	{
		StaticBp *bp = (StaticBp *) g_ptr_array_index (static_bps, i);

		if (g_strcmp0 (bp->path, self->path) == 0 && g_strcmp0 (bp->node, node) == 0)
			return TRUE;
	}
	return FALSE;
}

static Act
park (LrgScriptingGraph *self,
      GArray            *stack,
      const gchar       *node)
{
	g_array_free (self->paused_stack, TRUE);
	self->paused_stack = stack_copy (stack);
	self->paused = TRUE;
	g_free (self->paused_node);
	self->paused_node = g_strdup (node);
	return ACT_PAUSED;
}

static void
yield_fiber (LrgScriptingGraph *self,
             GArray            *stack,
             gdouble            left,
             gboolean           frames_wait)
{
	Fiber *fiber = g_new0 (Fiber, 1);

	fiber->stack = stack_copy (stack);
	fiber->left = left;
	fiber->frames_wait = frames_wait;
	fiber->born = self->update_gen;
	g_ptr_array_add (self->fibers, fiber);
}

static gchar *
find_kind_id (LrgVscriptGraph *graph,
              const gchar     *kind)
{
	guint i;

	for (i = 0; i < lrg_vscript_graph_get_n_nodes (graph); i++)
	{
		LrgVscriptNode *node = lrg_vscript_graph_get_nth_node (graph, i);

		if (g_strcmp0 (lrg_vscript_node_get_kind (node), kind) == 0)
			return g_strdup (lrg_vscript_node_get_id (node));
	}
	return NULL;
}

static LrgVscriptProgram *
open_relative (LrgScriptingGraph *self,
               const gchar       *file)
{
	g_autoptr(LrgVscriptProgram) prog = NULL;
	g_autofree gchar *path = NULL;
	const gchar *base;
	g_auto(GStrv) paths = NULL;
	guint i;

	if (file == NULL || file[0] == '\0')
		return NULL;
	if (g_path_is_absolute (file))
		return lrg_vscript_program_new_from_file (file, NULL);
	base = self->program != NULL ? lrg_vscript_program_get_base_dir (self->program) : NULL;
	if (base != NULL)
	{
		path = g_build_filename (base, file, NULL);
		prog = lrg_vscript_program_new_from_file (path, NULL);
		if (prog != NULL)
			return g_steal_pointer (&prog);
	}
	paths = lrg_scripting_get_search_paths (LRG_SCRIPTING (self));
	for (i = 0; paths != NULL && paths[i] != NULL; i++)
	{
		g_autofree gchar *one = g_build_filename (paths[i], file, NULL);

		prog = lrg_vscript_program_new_from_file (one, NULL);
		if (prog != NULL)
			return g_steal_pointer (&prog);
	}
	return NULL;
}

static void
hook_marshal (GClosure *closure,
              GValue   *return_value,
              guint     n_param_values,
              const GValue *param_values,
              gpointer  invocation_hint,
              gpointer  marshal_data)
{
	Hook *hook = (Hook *) closure->data;
	LrgScriptingGraph *self;
	Pending pending;
	guint i;

	(void) return_value;
	(void) invocation_hint;
	(void) marshal_data;
	if (hook == NULL || hook->owner == NULL)
		return;
	self = hook->owner;
	memset (&pending, 0, sizeof pending);
	pending.event = g_strdup (hook->event);
	pending.signal = g_strdup (hook->signal);
	pending.values = g_array_new (FALSE, TRUE, sizeof (GValue));
	g_array_set_clear_func (pending.values, gvalue_clear);
	for (i = 0; i < n_param_values; i++)
	{
		GValue copy = G_VALUE_INIT;

		value_set_copy (&copy, &param_values[i]);
		g_array_append_val (pending.values, copy);
	}
	if (self->running)
	{
		g_array_append_val (self->pending, pending);
		return;
	}
	/* Delivered below by the statement loop when running; outside a
	 * dispatch, run it as its own call so a signal after start still logs. */
	{
		GArray *saved_pending = self->pending;
		guint saved_steps = self->steps;
		gboolean saved_result = self->have_result;
		GError *error = NULL;
		const GValue *args;
		guint n_args;

		self->pending = g_array_new (FALSE, TRUE, sizeof (Pending));
		self->running = TRUE;
		self->steps = 0;
		self->have_result = FALSE;
		g_hash_table_remove_all (self->hits);
		g_hash_table_remove_all (self->out_cache);
		clear_pure (self);
		args = pending.values->len > 0 ? &g_array_index (pending.values, GValue, 0) : NULL;
		n_args = pending.values->len;
		if (pending.event != NULL && pending.event[0] != '\0')
			dispatch_kind (self, "event.custom", "name", pending.event, args, n_args, 0.0, FALSE, &error);
		dispatch_kind (self, "event.signal", "signal", pending.signal, args, n_args, 0.0, FALSE, &error);
		self->last_steps = self->steps;
		self->steps = saved_steps;
		self->have_result = saved_result;
		self->running = FALSE;
		g_clear_error (&error);
		g_array_free (self->pending, TRUE);
		self->pending = saved_pending;
		pending_clear_one (&pending);
	}
}

static gboolean
connect_signal (LrgScriptingGraph *self,
                GObject           *object,
                const gchar       *signal,
                const gchar       *event,
                GError           **error)
{
	Hook *hook;
	GClosure *closure;
	guint id;

	if (object == NULL || signal == NULL || g_signal_lookup (signal, G_OBJECT_TYPE (object)) == 0)
		return fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "unknown signal '%s'", signal != NULL ? signal : "") == ACT_OK;
	hook = g_new0 (Hook, 1);
	hook->owner = self;
	hook->object = object;
	hook->event = g_strdup (event);
	hook->signal = g_strdup (signal);
	closure = g_closure_new_simple (sizeof (GClosure), hook);
	g_closure_set_marshal (closure, hook_marshal);
	id = g_signal_connect_closure (object, signal, closure, FALSE);
	g_closure_unref (closure);
	if (id == 0)
	{
		hook_free (hook);
		return fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "could not connect '%s'", signal) == ACT_OK;
	}
	hook->id = id;
	g_object_weak_ref (object, hook_object_died, hook);
	g_ptr_array_add (self->hooks, hook);
	return TRUE;
}

static Act
emit_signal_node (LrgScriptingGraph *self,
                  LrgVscriptGraph   *graph,
                  LrgVscriptNode    *node,
                  GError           **error)
{
	GValue object = G_VALUE_INIT;
	const gchar *signal;
	GObject *obj;
	guint sig_id;
	GSignalQuery query;
	GValue *argv;
	GValue ret = G_VALUE_INIT;
	g_autoptr(GPtrArray) pins = NULL;
	guint i;
	guint slot;
	gboolean ok = TRUE;

	signal = lrg_vscript_node_get_param_string (node, "signal");
	if (!pull_input (self, graph, node, "object", &object, error))
		return ACT_ERROR;
	if (!G_VALUE_HOLDS_OBJECT (&object) || (obj = G_OBJECT (g_value_get_object (&object))) == NULL)
	{
		gvalue_clear (&object);
		return fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "gi.emit needs an object");
	}
	sig_id = signal != NULL ? g_signal_lookup (signal, G_OBJECT_TYPE (obj)) : 0;
	if (sig_id == 0)
	{
		gvalue_clear (&object);
		return fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "unknown signal '%s'", signal != NULL ? signal : "");
	}
	g_signal_query (sig_id, &query);
	argv = g_new0 (GValue, query.n_params + 1);
	g_value_init (&argv[0], G_OBJECT_TYPE (obj));
	g_value_set_object (&argv[0], obj);
	pins = lrg_vscript_program_dup_node_pins (self->program, graph, node);
	slot = 0;
	for (i = 0; i < pins->len && slot < query.n_params; i++)
	{
		LrgVscriptPin *pin = (LrgVscriptPin *) g_ptr_array_index (pins, i);
		GValue pulled = G_VALUE_INIT;

		if (pin->exec || pin->direction != LRG_VSCRIPT_PIN_INPUT || g_strcmp0 (pin->name, "object") == 0)
			continue;
		if (!pull_input (self, graph, node, pin->name, &pulled, error))
		{
			ok = FALSE;
			break;
		}
		g_value_init (&argv[slot + 1], query.param_types[slot]);
		if (G_IS_VALUE (&pulled) && G_VALUE_TYPE (&pulled) == query.param_types[slot])
			g_value_copy (&pulled, &argv[slot + 1]);
		else if (G_IS_VALUE (&pulled) && g_value_transform (&pulled, &argv[slot + 1]))
			;
		else
			ok = fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "cannot convert signal argument '%s'", pin->name) == ACT_OK;
		gvalue_clear (&pulled);
		slot++;
		if (!ok)
			break;
	}
	if (ok && query.return_type != G_TYPE_NONE)
		g_value_init (&ret, query.return_type);
	if (ok)
		g_signal_emitv (argv, sig_id, 0, query.return_type != G_TYPE_NONE ? &ret : NULL);
	for (i = 0; i < query.n_params + 1; i++)
		gvalue_clear (&argv[i]);
	g_free (argv);
	gvalue_clear (&ret);
	gvalue_clear (&object);
	return ok ? ACT_OK : ACT_ERROR;
}

static Act
call_host_node (LrgScriptingGraph *self,
                LrgVscriptGraph   *graph,
                LrgVscriptNode    *node,
                GError           **error)
{
	const gchar *name = lrg_vscript_node_get_param_string (node, "name");
	HostFn *host;
	gint64 n;
	gint64 i;
	GValue *args;
	GValue ret = G_VALUE_INIT;
	gboolean ok;

	host = name != NULL ? (HostFn *) g_hash_table_lookup (self->hosts, name) : NULL;
	if (host == NULL)
		return fail (self, error, LRG_VSCRIPT_ERROR_NOT_FOUND, "no host function '%s'", name != NULL ? name : "");
	n = param_int (node, "args", 0);
	if (n < 0)
		n = 0;
	if (n > 16)
		n = 16;
	args = g_new0 (GValue, (gsize) n);
	ok = TRUE;
	for (i = 0; i < n; i++)
	{
		gchar pin[16];

		g_snprintf (pin, sizeof pin, "arg%" G_GINT64_FORMAT, i);
		if (!pull_input (self, graph, node, pin, &args[i], error))
		{
			ok = FALSE;
			break;
		}
	}
	if (ok)
	{
		ok = host->func (LRG_SCRIPTING (self), (guint) n, n > 0 ? args : NULL, &ret, host->user_data, error);
		if (!ok && (error == NULL || *error == NULL))
			fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "host function '%s' failed", name);
		else if (ok && G_IS_VALUE (&ret))
			cache_put (self, self->out_cache, graph, lrg_vscript_node_get_id (node), "result", &ret);
	}
	for (i = 0; i < n; i++)
		gvalue_clear (&args[i]);
	g_free (args);
	gvalue_clear (&ret);
	return ok ? ACT_OK : ACT_ERROR;
}

static Act
exec_gi_call (LrgScriptingGraph *self,
              LrgVscriptGraph   *graph,
              LrgVscriptNode    *node,
              GError           **error)
{
	if (!eval_pure (self, graph, node, error))
		return ACT_ERROR;
	/* eval_pure cached into the pure cache; statements read out_cache.
	 * Copy result across when the node is wired as a statement. */
	{
		GValue out = G_VALUE_INIT;

		if (cache_get (self->pure_cache, graph, lrg_vscript_node_get_id (node), "result", &out))
		{
			cache_put (self, self->out_cache, graph, lrg_vscript_node_get_id (node), "result", &out);
			g_value_unset (&out);
		}
	}
	return ACT_OK;
}

static void
seed_event (LrgScriptingGraph *self,
            LrgVscriptGraph   *graph,
            LrgVscriptNode    *node,
            const GValue      *args,
            guint              n_args,
            gdouble            dt,
            gboolean           have_dt)
{
	const gchar *kind = lrg_vscript_node_get_kind (node);
	const gchar *id = lrg_vscript_node_get_id (node);
	GValue boxed = G_VALUE_INIT;

	if (have_dt && (g_strcmp0 (kind, "event.update") == 0 || g_strcmp0 (kind, "event.fixed_update") == 0))
	{
		set_double (&boxed, dt);
		cache_put (self, self->out_cache, graph, id, "dt", &boxed);
		g_value_unset (&boxed);
	}
	else if (g_strcmp0 (kind, "event.input") == 0)
	{
		const gchar *action = lrg_vscript_node_get_param_string (node, "action");
		g_autofree gchar *pk = g_strdup_printf ("input.%s.pressed", action != NULL ? action : "");
		g_autofree gchar *vk = g_strdup_printf ("input.%s.value", action != NULL ? action : "");
		GValue *pressed = (GValue *) g_hash_table_lookup (self->extras, pk);
		GValue *axis = (GValue *) g_hash_table_lookup (self->extras, vk);

		if (pressed != NULL)
			cache_put (self, self->out_cache, graph, id, "pressed", pressed);
		else
		{
			set_bool (&boxed, FALSE);
			cache_put (self, self->out_cache, graph, id, "pressed", &boxed);
			g_value_unset (&boxed);
		}
		if (axis != NULL)
			cache_put (self, self->out_cache, graph, id, "value", axis);
		else
		{
			set_double (&boxed, 0.0);
			cache_put (self, self->out_cache, graph, id, "value", &boxed);
			g_value_unset (&boxed);
		}
	}
	else if (g_strcmp0 (kind, "event.custom") == 0 || g_strcmp0 (kind, "event.signal") == 0)
	{
		g_autoptr(GPtrArray) pins = lrg_vscript_program_dup_node_pins (self->program, graph, node);
		guint i;
		guint slot = 0;
		guint base = g_strcmp0 (kind, "event.signal") == 0 ? 1 : 0;

		if (g_strcmp0 (kind, "event.signal") == 0 && n_args > 0)
			cache_put (self, self->out_cache, graph, id, "sender", &args[0]);
		for (i = 0; i < pins->len; i++)
		{
			LrgVscriptPin *pin = (LrgVscriptPin *) g_ptr_array_index (pins, i);

			if (pin->exec || pin->direction != LRG_VSCRIPT_PIN_OUTPUT)
				continue;
			if (g_strcmp0 (pin->name, "sender") == 0)
				continue;
			if (base + slot < n_args)
				cache_put (self, self->out_cache, graph, id, pin->name, &args[base + slot]);
			slot++;
		}
	}
}

static Act
drive (LrgScriptingGraph *self,
       GArray            *stack,
       GError           **error)
{
	VsFrame *frame;
	LrgVscriptNode *node;
	const gchar *kind;
	const gchar *id;
	GValue a = G_VALUE_INIT;
	GValue b = G_VALUE_INIT;
	Act act = ACT_OK;
	gboolean ok = TRUE;

	if (stack->len == 0)
		return ACT_OK;
	frame = frame_at (stack, stack->len - 1);
	node = lrg_vscript_graph_get_node (frame->graph, frame->node_id);
	if (node == NULL)
	{
		g_array_remove_index (stack, stack->len - 1);
		return ACT_OK;
	}
	kind = lrg_vscript_node_get_kind (node);
	id = lrg_vscript_node_get_id (node);

	if (frame->phase != 0)
	{
		if (g_strcmp0 (kind, "flow.while") == 0)
		{
			frame->phase = 0;
			return ACT_OK;
		}
		if (g_strcmp0 (kind, "flow.for") == 0 || g_strcmp0 (kind, "flow.for_each") == 0)
		{
			frame->index++;
			if (frame->index > frame->limit)
			{
				follow_pin (stack, frame->graph, id, "completed");
				return ACT_OK;
			}
			if (!budget_left (self, error))
				return ACT_ERROR;
			self->steps++;
			note_enter (self, frame->graph, id);
			clear_pure (self);
			if (g_strcmp0 (kind, "flow.for") == 0)
			{
				set_int (&a, frame->index);
				cache_put (self, self->out_cache, frame->graph, id, "index", &a);
				g_value_unset (&a);
			}
			else
			{
				GValue list = G_VALUE_INIT;
				GPtrArray *items = NULL;

				if (!cache_get (self->out_cache, frame->graph, id, "list", &list))
				{
					follow_pin (stack, frame->graph, id, "completed");
					return ACT_OK;
				}
				if (G_VALUE_HOLDS (&list, lrg_vscript_priv_list_get_type ()))
					items = (GPtrArray *) g_value_get_boxed (&list);
				if (items == NULL || frame->index >= (gint) items->len)
				{
					g_value_unset (&list);
					follow_pin (stack, frame->graph, id, "completed");
					return ACT_OK;
				}
				cache_put (self, self->out_cache, frame->graph, id, "element", (GValue *) g_ptr_array_index (items, (guint) frame->index));
				set_int (&a, frame->index);
				cache_put (self, self->out_cache, frame->graph, id, "index", &a);
				g_value_unset (&a);
				g_value_unset (&list);
			}
			frame->phase = 1;
			if (!push_link (stack, frame->graph, id, "body"))
				frame->phase = 2;
			return ACT_OK;
		}
		if (g_strcmp0 (kind, "flow.sequence") == 0)
		{
			while (frame->index < frame->limit)
			{
				gchar pin[16];
				gint slot = frame->index;

				frame->index++;
				g_snprintf (pin, sizeof pin, "%d", slot);
				if (push_link (stack, frame->graph, id, pin))
					return ACT_OK;
				frame = frame_at (stack, stack->len - 1);
			}
			g_array_remove_index (stack, stack->len - 1);
			return ACT_OK;
		}
		if (g_strcmp0 (kind, "flow.delay") == 0 || g_strcmp0 (kind, "flow.wait_frames") == 0 ||
		    g_strcmp0 (kind, "subgraph.call") == 0 || g_strcmp0 (kind, "debug.log") == 0 ||
		    g_str_has_prefix (kind, "event.") || g_strcmp0 (kind, "var.set") == 0 ||
		    g_strcmp0 (kind, "blackboard.set") == 0 || g_strcmp0 (kind, "gi.set") == 0 ||
		    g_strcmp0 (kind, "gi.connect") == 0 || g_strcmp0 (kind, "gi.emit") == 0 ||
		    g_strcmp0 (kind, "gi.call") == 0 || g_strcmp0 (kind, "script.call") == 0 ||
		    g_strcmp0 (kind, "subgraph.output") == 0)
		{
			follow_pin (stack, frame->graph, id, "then");
			return ACT_OK;
		}
		g_array_remove_index (stack, stack->len - 1);
		return ACT_OK;
	}

	if (breakpoint_hit (self, id) && !frame->broke)
	{
		frame->broke = TRUE;
		return park (self, stack, id);
	}
	if (!budget_left (self, error))
		return ACT_ERROR;
	self->steps++;
	frame->broke = FALSE;
	note_enter (self, frame->graph, id);
	clear_pure (self);

	if (g_str_has_prefix (kind, "event.") || g_strcmp0 (kind, "subgraph.input") == 0)
	{
		follow_pin (stack, frame->graph, id, "then");
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "debug.log") == 0)
	{
		g_autofree gchar *line = NULL;

		if (!pull_input (self, frame->graph, node, "text", &a, error))
			return ACT_ERROR;
		line = lrg_vscript_priv_value_to_string (&a);
		g_ptr_array_add (self->log, g_strdup (line));
		g_signal_emit (self, sig_logged, 0, line);
		g_value_unset (&a);
		follow_pin (stack, frame->graph, id, "then");
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "flow.branch") == 0)
	{
		gboolean cond;

		if (!pull_input (self, frame->graph, node, "condition", &a, error))
			return ACT_ERROR;
		cond = lrg_vscript_priv_value_to_bool (&a);
		g_value_unset (&a);
		follow_pin (stack, frame->graph, id, cond ? "true" : "false");
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "flow.while") == 0)
	{
		gboolean cond;

		if (!pull_input (self, frame->graph, node, "condition", &a, error))
			return ACT_ERROR;
		cond = lrg_vscript_priv_value_to_bool (&a);
		g_value_unset (&a);
		if (!cond)
		{
			follow_pin (stack, frame->graph, id, "completed");
			return ACT_OK;
		}
		frame = frame_at (stack, stack->len - 1);
		frame->phase = 1;
		if (!push_link (stack, frame->graph, id, "body"))
			frame_at (stack, stack->len - 1)->phase = 0;
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "flow.sequence") == 0)
	{
		gint64 n = param_int (node, "count", 2);

		if (n < 1)
			n = 1;
		if (n > 16)
			n = 16;
		frame->index = 0;
		frame->limit = (gint) n;
		frame->phase = 1;
		while (frame->index < frame->limit)
		{
			gchar pin[16];
			gint slot = frame->index;

			frame->index++;
			g_snprintf (pin, sizeof pin, "%d", slot);
			if (push_link (stack, frame->graph, id, pin))
				return ACT_OK;
			frame = frame_at (stack, stack->len - 1);
		}
		g_array_remove_index (stack, stack->len - 1);
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "flow.for") == 0)
	{
		gint64 first;
		gint64 last;

		if (!pull_input (self, frame->graph, node, "first", &a, error) ||
		    !pull_input (self, frame->graph, node, "last", &b, error))
		{
			gvalue_clear (&a);
			gvalue_clear (&b);
			return ACT_ERROR;
		}
		first = lrg_vscript_priv_value_to_int (&a);
		last = lrg_vscript_priv_value_to_int (&b);
		g_value_unset (&a);
		g_value_unset (&b);
		if (first > last)
		{
			follow_pin (stack, frame->graph, id, "completed");
			return ACT_OK;
		}
		frame = frame_at (stack, stack->len - 1);
		frame->index = (gint) first;
		frame->limit = (gint) last;
		set_int (&a, first);
		cache_put (self, self->out_cache, frame->graph, id, "index", &a);
		g_value_unset (&a);
		frame->phase = 1;
		if (!push_link (stack, frame->graph, id, "body"))
			frame_at (stack, stack->len - 1)->phase = 2;
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "flow.for_each") == 0)
	{
		GPtrArray *items = NULL;

		if (!pull_input (self, frame->graph, node, "list", &a, error))
			return ACT_ERROR;
		cache_put (self, self->out_cache, frame->graph, id, "list", &a);
		if (G_VALUE_HOLDS (&a, lrg_vscript_priv_list_get_type ()))
			items = (GPtrArray *) g_value_get_boxed (&a);
		if (items == NULL || items->len == 0)
		{
			g_value_unset (&a);
			follow_pin (stack, frame->graph, id, "completed");
			return ACT_OK;
		}
		frame = frame_at (stack, stack->len - 1);
		frame->index = 0;
		frame->limit = (gint) items->len - 1;
		cache_put (self, self->out_cache, frame->graph, id, "element", (GValue *) g_ptr_array_index (items, 0));
		set_int (&b, 0);
		cache_put (self, self->out_cache, frame->graph, id, "index", &b);
		g_value_unset (&a);
		g_value_unset (&b);
		frame->phase = 1;
		if (!push_link (stack, frame->graph, id, "body"))
			frame_at (stack, stack->len - 1)->phase = 2;
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "flow.delay") == 0 || g_strcmp0 (kind, "flow.wait_frames") == 0)
	{
		gdouble left;

		if (!pull_input (self, frame->graph, node, g_strcmp0 (kind, "flow.delay") == 0 ? "seconds" : "frames", &a, error))
			return ACT_ERROR;
		left = g_strcmp0 (kind, "flow.delay") == 0 ? lrg_vscript_priv_value_to_double (&a) : (gdouble) lrg_vscript_priv_value_to_int (&a);
		g_value_unset (&a);
		if (left < 0.0)
			left = 0.0;
		frame->phase = 1;
		yield_fiber (self, stack, left, g_strcmp0 (kind, "flow.wait_frames") == 0);
		return ACT_YIELD;
	}
	if (g_strcmp0 (kind, "var.set") == 0)
	{
		const gchar *name = lrg_vscript_node_get_param_string (node, "name");

		if (name == NULL)
			return fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "var.set needs a name");
		if (!pull_input (self, frame->graph, node, "value", &a, error))
			return ACT_ERROR;
		store_var (self, name, &a);
		g_value_unset (&a);
		follow_pin (stack, frame->graph, id, "then");
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "blackboard.set") == 0)
	{
		g_autofree gchar *key = NULL;
		g_autoptr(GVariant) variant = NULL;

		if (self->blackboard == NULL)
			return fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "no blackboard");
		if (!pull_input (self, frame->graph, node, "key", &a, error) ||
		    !pull_input (self, frame->graph, node, "value", &b, error))
		{
			gvalue_clear (&a);
			gvalue_clear (&b);
			return ACT_ERROR;
		}
		key = lrg_vscript_priv_value_to_string (&a);
		variant = lrg_vscript_priv_value_to_variant (&b);
		if (variant == NULL)
			ok = fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "blackboard value is not storable") == ACT_OK;
		else if (!lrg_blackboard_set_variant (self->blackboard, key, g_steal_pointer (&variant)))
			ok = fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "blackboard rejected '%s'", key) == ACT_OK;
		g_value_unset (&a);
		g_value_unset (&b);
		if (!ok)
			return ACT_ERROR;
		follow_pin (stack, frame->graph, id, "then");
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "gi.call") == 0)
	{
		act = exec_gi_call (self, frame->graph, node, error);
		if (act != ACT_OK)
			return act;
		follow_pin (stack, frame->graph, id, "then");
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "gi.set") == 0)
	{
		const gchar *prop = lrg_vscript_node_get_param_string (node, "property");
		GParamSpec *pspec;
		GValue converted = G_VALUE_INIT;

		if (prop == NULL)
			prop = lrg_vscript_node_get_param_string (node, "name");
		if (!pull_input (self, frame->graph, node, "object", &a, error) ||
		    !pull_input (self, frame->graph, node, "value", &b, error))
		{
			gvalue_clear (&a);
			gvalue_clear (&b);
			return ACT_ERROR;
		}
		if (!G_VALUE_HOLDS_OBJECT (&a) || g_value_get_object (&a) == NULL || prop == NULL)
			ok = FALSE;
		else
		{
			pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (g_value_get_object (&a)), prop);
			if (pspec == NULL)
				ok = FALSE;
			else
			{
				g_value_init (&converted, pspec->value_type);
				if (G_VALUE_TYPE (&b) == pspec->value_type)
					g_value_copy (&b, &converted);
				else if (!g_value_transform (&b, &converted))
					ok = FALSE;
				if (ok)
					g_object_set_property (G_OBJECT (g_value_get_object (&a)), prop, &converted);
			}
		}
		gvalue_clear (&a);
		gvalue_clear (&b);
		gvalue_clear (&converted);
		if (!ok)
			return fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "gi.set failed");
		follow_pin (stack, frame->graph, id, "then");
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "gi.connect") == 0)
	{
		const gchar *signal = lrg_vscript_node_get_param_string (node, "signal");
		const gchar *event = lrg_vscript_node_get_param_string (node, "event");

		if (!pull_input (self, frame->graph, node, "object", &a, error))
			return ACT_ERROR;
		if (!G_VALUE_HOLDS_OBJECT (&a) || g_value_get_object (&a) == NULL)
		{
			g_value_unset (&a);
			return fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "gi.connect needs an object");
		}
		ok = connect_signal (self, G_OBJECT (g_value_get_object (&a)), signal, event, error);
		g_value_unset (&a);
		if (!ok)
			return ACT_ERROR;
		follow_pin (stack, frame->graph, id, "then");
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "gi.emit") == 0)
	{
		act = emit_signal_node (self, frame->graph, node, error);
		if (act != ACT_OK)
			return act;
		follow_pin (stack, frame->graph, id, "then");
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "script.call") == 0)
	{
		act = call_host_node (self, frame->graph, node, error);
		if (act != ACT_OK)
			return act;
		follow_pin (stack, frame->graph, id, "then");
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "subgraph.output") == 0)
	{
		VsFrame *call = find_call (stack, frame->graph);
		g_autoptr(GPtrArray) pins = lrg_vscript_program_dup_node_pins (self->program, frame->graph, node);
		guint i;

		for (i = 0; ok && call != NULL && i < pins->len; i++)
		{
			LrgVscriptPin *pin = (LrgVscriptPin *) g_ptr_array_index (pins, i);

			if (pin->exec || pin->direction != LRG_VSCRIPT_PIN_INPUT)
				continue;
			if (!pull_input (self, frame->graph, node, pin->name, &a, error))
				ok = FALSE;
			else
			{
				cache_put (self, self->out_cache, call->graph, call->node_id, pin->name, &a);
				g_value_unset (&a);
			}
		}
		if (!ok)
			return ACT_ERROR;
		g_array_remove_index (stack, stack->len - 1);
		return ACT_OK;
	}
	if (g_strcmp0 (kind, "subgraph.call") == 0)
	{
		const gchar *name = lrg_vscript_node_get_param_string (node, "name");
		const gchar *file = lrg_vscript_node_get_param_string (node, "file");
		LrgVscriptGraph *callee = NULL;
		g_autofree gchar *entry = NULL;
		VsFrame child;

		if (name != NULL && name[0] != '\0')
			callee = lrg_vscript_program_get_graph (self->program, name);
		else if (file != NULL && file[0] != '\0')
		{
			LrgVscriptProgram *loaded = open_relative (self, file);

			if (loaded == NULL)
				return fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "cannot load subgraph '%s'", file);
			g_ptr_array_add (self->held, loaded);
			callee = lrg_vscript_program_get_main (loaded);
		}
		if (callee == NULL)
			return fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "subgraph.call has no callee");
		entry = find_kind_id (callee, "subgraph.input");
		if (entry == NULL)
			return fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "subgraph has no input node");
		frame = frame_at (stack, stack->len - 1);
		frame->is_call = TRUE;
		frame->callee = callee;
		frame->phase = 1;
		memset (&child, 0, sizeof child);
		child.graph = callee;
		child.node_id = g_steal_pointer (&entry);
		g_array_append_val (stack, child);
		return ACT_OK;
	}
	if (lrg_vscript_node_type_is_pure (kind))
	{
		if (!eval_pure (self, frame->graph, node, error))
			return ACT_ERROR;
		g_array_remove_index (stack, stack->len - 1);
		return ACT_OK;
	}
	return fail (self, error, LRG_VSCRIPT_ERROR_RUNTIME, "no interpreter for '%s'", kind);
}

static Act
drain_pending (LrgScriptingGraph *self,
               GError           **error)
{
	while (self->pending->len > 0)
	{
		Pending pending = g_array_index (self->pending, Pending, 0);
		const GValue *args;
		guint n_args;
		Act act;

		g_array_remove_index (self->pending, 0);
		args = pending.values != NULL && pending.values->len > 0 ? &g_array_index (pending.values, GValue, 0) : NULL;
		n_args = pending.values != NULL ? pending.values->len : 0;
		act = ACT_OK;
		if (pending.event != NULL && pending.event[0] != '\0')
			act = dispatch_kind (self, "event.custom", "name", pending.event, args, n_args, 0.0, FALSE, error);
		if (act == ACT_OK)
			act = dispatch_kind (self, "event.signal", "signal", pending.signal, args, n_args, 0.0, FALSE, error);
		pending_clear_one (&pending);
		if (act == ACT_ERROR || act == ACT_PAUSED)
			return act;
	}
	return ACT_OK;
}

static Act
run_stack (LrgScriptingGraph *self,
           GArray            *stack,
           GError           **error)
{
	GArray *saved = self->stack;
	Act act = ACT_OK;

	self->stack = stack;
	while (act == ACT_OK && stack->len > 0)
	{
		act = drive (self, stack, error);
		if (act == ACT_OK)
			act = drain_pending (self, error);
	}
	self->stack = saved;
	return act;
}

static gboolean
node_matches (LrgVscriptNode *node,
              const gchar    *kind,
              const gchar    *param,
              const gchar    *match)
{
	if (g_strcmp0 (lrg_vscript_node_get_kind (node), kind) != 0)
		return FALSE;
	if (param == NULL)
		return TRUE;
	return g_strcmp0 (lrg_vscript_node_get_param_string (node, param), match) == 0;
}

static Act
dispatch_kind (LrgScriptingGraph *self,
               const gchar       *kind,
               const gchar       *param,
               const gchar       *match,
               const GValue      *args,
               guint              n_args,
               gdouble            dt,
               gboolean           have_dt,
               GError           **error)
{
	LrgVscriptGraph *graph;
	guint i;

	if (self->program == NULL)
		return ACT_OK;
	graph = lrg_vscript_program_get_main (self->program);
	for (i = 0; i < lrg_vscript_graph_get_n_nodes (graph); i++)
	{
		LrgVscriptNode *node = lrg_vscript_graph_get_nth_node (graph, i);
		GArray *stack;
		VsFrame frame;
		Act act;

		if (!node_matches (node, kind, param, match))
			continue;
		seed_event (self, graph, node, args, n_args, dt, have_dt);
		stack = stack_new ();
		memset (&frame, 0, sizeof frame);
		frame.graph = graph;
		frame.node_id = g_strdup (lrg_vscript_node_get_id (node));
		g_array_append_val (stack, frame);
		act = run_stack (self, stack, error);
		g_array_free (stack, TRUE);
		if (act == ACT_ERROR || act == ACT_PAUSED)
			return act;
	}
	return ACT_OK;
}

static Act
resume_fibers (LrgScriptingGraph *self,
               gdouble            dt,
               GError           **error)
{
	guint i = 0;

	while (i < self->fibers->len)
	{
		Fiber *fiber = (Fiber *) g_ptr_array_index (self->fibers, i);
		Act act;

		if (fiber->born == self->update_gen)
		{
			i++;
			continue;
		}
		if (fiber->frames_wait)
			fiber->left -= 1.0;
		else
			fiber->left -= dt;
		if (fiber->left > 0.0)
		{
			i++;
			continue;
		}
		g_ptr_array_remove_index (self->fibers, i);
		act = run_stack (self, fiber->stack, error);
		fiber->stack = NULL;
		fiber_free (fiber);
		if (act == ACT_ERROR || act == ACT_PAUSED)
			return act;
	}
	return ACT_OK;
}

static void
begin_call (LrgScriptingGraph *self)
{
	if (self->active->len > 0)
	{
		GPtrArray *frame = g_ptr_array_new_with_free_func (g_free);
		guint i;

		for (i = 0; i < self->active->len; i++)
			g_ptr_array_add (frame, g_strdup ((const gchar *) g_ptr_array_index (self->active, i)));
		g_queue_push_tail (self->recent, frame);
		while (g_queue_get_length (self->recent) > 10)
			g_ptr_array_unref ((GPtrArray *) g_queue_pop_head (self->recent));
		g_ptr_array_set_size (self->active, 0);
	}
	g_hash_table_remove_all (self->hits);
	g_hash_table_remove_all (self->out_cache);
	clear_pure (self);
	if (G_IS_VALUE (&self->last_result))
		g_value_unset (&self->last_result);
	self->have_result = FALSE;
	self->steps = 0;
	self->running = TRUE;
}

static void
finish_call (LrgScriptingGraph *self,
             GValue            *return_value,
             gboolean           ok)
{
	self->last_steps = self->steps;
	self->running = FALSE;
	if (!ok || return_value == NULL || G_IS_VALUE (return_value))
		return;
	if (self->have_result)
	{
		g_value_init (return_value, G_VALUE_TYPE (&self->last_result));
		g_value_copy (&self->last_result, return_value);
	}
	else
	{
		g_value_init (return_value, G_TYPE_BOOLEAN);
		g_value_set_boolean (return_value, TRUE);
	}
}

static gboolean
event_named (LrgScriptingGraph *self,
             const gchar       *kind,
             const gchar       *param,
             const gchar       *name)
{
	LrgVscriptGraph *graph;
	guint i;

	if (self->program == NULL)
		return FALSE;
	graph = lrg_vscript_program_get_main (self->program);
	for (i = 0; i < lrg_vscript_graph_get_n_nodes (graph); i++)
	{
		if (node_matches (lrg_vscript_graph_get_nth_node (graph, i), kind, param, name))
			return TRUE;
	}
	return FALSE;
}

static void
reset_vars (LrgScriptingGraph *self)
{
	g_auto(GStrv) names = NULL;
	guint i;

	g_hash_table_remove_all (self->vars);
	if (self->program == NULL)
		return;
	names = lrg_vscript_program_dup_variable_names (self->program);
	for (i = 0; names != NULL && names[i] != NULL; i++)
	{
		GVariant *def = lrg_vscript_program_get_variable_default (self->program, names[i]);
		const gchar *type = lrg_vscript_program_get_variable_type (self->program, names[i]);
		GValue *slot = g_new0 (GValue, 1);

		if (def != NULL)
			lrg_vscript_priv_variant_to_value (def, slot);
		else
			lrg_vscript_priv_zero_value (type != NULL ? type : "any", slot);
		g_hash_table_insert (self->vars, g_strdup (names[i]), slot);
	}
}

static void
clear_runtime (LrgScriptingGraph *self,
               gboolean           globals)
{
	if (self->paused_stack != NULL)
		g_array_free (self->paused_stack, TRUE);
	self->paused_stack = stack_new ();
	self->paused = FALSE;
	g_clear_pointer (&self->paused_node, g_free);
	g_ptr_array_set_size (self->fibers, 0);
	g_ptr_array_set_size (self->log, 0);
	g_hash_table_remove_all (self->hits);
	g_ptr_array_set_size (self->active, 0);
	while (g_queue_get_length (self->recent) > 0)
		g_ptr_array_unref ((GPtrArray *) g_queue_pop_head (self->recent));
	g_hash_table_remove_all (self->out_cache);
	clear_pure (self);
	g_ptr_array_set_size (self->held, 0);
	if (self->pending != NULL)
	{
		while (self->pending->len > 0)
		{
			Pending pending = g_array_index (self->pending, Pending, 0);

			pending_clear_one (&pending);
			g_array_remove_index (self->pending, 0);
		}
	}
	self->acc = 0.0;
	self->clock = 0.0;
	self->steps = 0;
	self->last_steps = 0;
	self->have_result = FALSE;
	if (G_IS_VALUE (&self->last_result))
		g_value_unset (&self->last_result);
	if (globals)
	{
		g_hash_table_remove_all (self->extras);
		g_hash_table_remove_all (self->hosts);
		g_clear_object (&self->self_object);
		g_clear_object (&self->blackboard);
		g_ptr_array_set_size (self->bps, 0);
	}
}

static Snap *
snap_take (LrgScriptingGraph *self)
{
	Snap *snap = g_new0 (Snap, 1);

	snap->program = g_steal_pointer (&self->program);
	snap->path = g_steal_pointer (&self->path);
	if (self->rng != NULL)
		snap->rng_state = lrg_random_stream_snapshot (self->rng);
	snap->vars = self->vars;
	self->vars = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, value_free);
	snap->log = self->log;
	self->log = g_ptr_array_new_with_free_func (g_free);
	snap->fibers = self->fibers;
	self->fibers = g_ptr_array_new_with_free_func (fiber_free);
	snap->held = self->held;
	self->held = g_ptr_array_new_with_free_func (g_object_unref);
	snap->acc = self->acc;
	snap->clock = self->clock;
	snap->paused = self->paused;
	snap->paused_node = g_steal_pointer (&self->paused_node);
	snap->paused_stack = self->paused_stack;
	self->paused_stack = stack_new ();
	self->paused = FALSE;
	self->acc = 0.0;
	self->clock = 0.0;
	return snap;
}

static void
snap_restore (LrgScriptingGraph *self,
              Snap              *snap)
{
	g_clear_object (&self->program);
	g_free (self->path);
	self->program = g_steal_pointer (&snap->program);
	self->path = g_steal_pointer (&snap->path);
	if (snap->rng_state != NULL && self->rng != NULL)
		lrg_random_stream_restore (self->rng, snap->rng_state, NULL);
	g_hash_table_unref (self->vars);
	self->vars = snap->vars;
	snap->vars = NULL;
	g_ptr_array_unref (self->log);
	self->log = snap->log;
	snap->log = NULL;
	g_ptr_array_unref (self->fibers);
	self->fibers = snap->fibers;
	snap->fibers = NULL;
	g_ptr_array_unref (self->held);
	self->held = snap->held;
	snap->held = NULL;
	self->acc = snap->acc;
	self->clock = snap->clock;
	self->paused = snap->paused;
	g_free (self->paused_node);
	self->paused_node = g_steal_pointer (&snap->paused_node);
	g_array_free (self->paused_stack, TRUE);
	self->paused_stack = snap->paused_stack;
	snap->paused_stack = NULL;
	g_free (snap->rng_state);
	g_free (snap);
}

static void
snap_discard (Snap *snap)
{
	g_clear_object (&snap->program);
	g_free (snap->path);
	g_free (snap->rng_state);
	if (snap->vars != NULL)
		g_hash_table_unref (snap->vars);
	if (snap->log != NULL)
		g_ptr_array_unref (snap->log);
	if (snap->fibers != NULL)
		g_ptr_array_unref (snap->fibers);
	if (snap->held != NULL)
		g_ptr_array_unref (snap->held);
	g_free (snap->paused_node);
	if (snap->paused_stack != NULL)
		g_array_free (snap->paused_stack, TRUE);
	g_free (snap);
}

static gboolean
adopt_program (LrgScriptingGraph *self,
               LrgVscriptProgram *program,
               const gchar       *path,
               GError           **error)
{
	Snap *snap;
	Act act;
	guint32 seed;

	snap = snap_take (self);
	self->program = program;
	self->path = g_strdup (path);
	seed = lrg_vscript_program_get_seed (program);
	g_clear_object (&self->rng);
	self->rng = lrg_random_stream_new ((guint64) seed, 1);
	reset_vars (self);
	begin_call (self);
	act = dispatch_kind (self, "event.start", NULL, NULL, NULL, 0, 0.0, FALSE, error);
	finish_call (self, NULL, act != ACT_ERROR);
	if (act == ACT_ERROR)
	{
		g_clear_object (&self->program);
		g_clear_pointer (&self->path, g_free);
		snap_restore (self, snap);
		return FALSE;
	}
	snap_discard (snap);
	return TRUE;
}

static gboolean
graph_load_file (LrgScripting *base,
                 const gchar  *path,
                 GError      **error)
{
	LrgScriptingGraph *self = LRG_SCRIPTING_GRAPH (base);
	LrgVscriptProgram *program;
	GError *local = NULL;

	program = lrg_vscript_program_new_from_file (path, &local);
	if (program == NULL)
	{
		g_propagate_error (error, local);
		return FALSE;
	}
	return adopt_program (self, program, path, error);
}

static gboolean
graph_load_string (LrgScripting *base,
                   const gchar  *name,
                   const gchar  *code,
                   GError      **error)
{
	LrgScriptingGraph *self = LRG_SCRIPTING_GRAPH (base);
	LrgVscriptProgram *program;
	GError *local = NULL;

	(void) name;
	program = lrg_vscript_program_new_from_data (code, -1, &local);
	if (program == NULL)
	{
		g_propagate_error (error, local);
		return FALSE;
	}
	return adopt_program (self, program, NULL, error);
}

static gboolean
graph_call (LrgScripting *base,
            const gchar  *func_name,
            GValue       *return_value,
            guint         n_args,
            const GValue *args,
            GError      **error)
{
	LrgScriptingGraph *self = LRG_SCRIPTING_GRAPH (base);
	HostFn *host;
	Act act = ACT_OK;
	gdouble dt;
	gdouble step;
	guint guard;

	if (self->paused)
	{
		g_set_error (error, LRG_VSCRIPT_ERROR, LRG_VSCRIPT_ERROR_RUNTIME, "paused");
		return FALSE;
	}
	host = (HostFn *) g_hash_table_lookup (self->hosts, func_name);
	if (self->program != NULL &&
	    (g_strcmp0 (func_name, "start") == 0 || g_strcmp0 (func_name, "update") == 0 ||
	     g_strcmp0 (func_name, "fixed_update") == 0 ||
	     event_named (self, "event.custom", "name", func_name) ||
	     event_named (self, "event.input", "action", func_name)))
		host = NULL;
	if (host != NULL)
	{
		GValue ret = G_VALUE_INIT;
		gboolean ok = host->func (base, n_args, args, &ret, host->user_data, error);

		if (ok && return_value != NULL && !G_IS_VALUE (return_value))
		{
			if (G_IS_VALUE (&ret))
			{
				g_value_init (return_value, G_VALUE_TYPE (&ret));
				g_value_copy (&ret, return_value);
			}
			else
			{
				g_value_init (return_value, G_TYPE_BOOLEAN);
				g_value_set_boolean (return_value, TRUE);
			}
		}
		gvalue_clear (&ret);
		return ok;
	}
	if (self->program == NULL)
	{
		g_set_error (error, LRG_VSCRIPT_ERROR, LRG_VSCRIPT_ERROR_NOT_FOUND, "no program is loaded");
		return FALSE;
	}
	begin_call (self);
	if (g_strcmp0 (func_name, "update") == 0)
	{
		dt = n_args > 0 ? lrg_vscript_priv_value_to_double (&args[0]) : lrg_vscript_program_get_fixed_step (self->program);
		if (dt != dt || dt < 0.0)
			dt = 0.0;
		self->update_gen++;
		self->clock += dt;
		act = resume_fibers (self, dt, error);
		if (act == ACT_OK)
			act = dispatch_kind (self, "event.update", NULL, NULL, args, n_args, dt, TRUE, error);
		step = lrg_vscript_program_get_fixed_step (self->program);
		if (step < 1e-12)
			step = 1.0 / 60.0;
		self->acc += dt;
		guard = 0;
		while (act == ACT_OK && self->acc >= step && guard < 1000000)
		{
			act = dispatch_kind (self, "event.fixed_update", NULL, NULL, NULL, 0, step, TRUE, error);
			self->acc -= step;
			guard++;
		}
		if (act == ACT_OK && self->acc >= step)
			act = fail (self, error, LRG_VSCRIPT_ERROR_BUDGET, "fixed step did not catch up");
	}
	else if (g_strcmp0 (func_name, "start") == 0)
		act = dispatch_kind (self, "event.start", NULL, NULL, NULL, 0, 0.0, FALSE, error);
	else if (g_strcmp0 (func_name, "fixed_update") == 0)
	{
		dt = n_args > 0 ? lrg_vscript_priv_value_to_double (&args[0]) : lrg_vscript_program_get_fixed_step (self->program);
		act = dispatch_kind (self, "event.fixed_update", NULL, NULL, args, n_args, dt, TRUE, error);
	}
	else if (event_named (self, "event.custom", "name", func_name))
		act = dispatch_kind (self, "event.custom", "name", func_name, args, n_args, 0.0, FALSE, error);
	else if (event_named (self, "event.input", "action", func_name))
		act = dispatch_kind (self, "event.input", "action", func_name, args, n_args, 0.0, FALSE, error);
	else
		act = fail (self, error, LRG_VSCRIPT_ERROR_NOT_FOUND, "no event '%s'", func_name);
	finish_call (self, return_value, act != ACT_ERROR);
	return act != ACT_ERROR;
}

static gboolean
graph_register (LrgScripting         *base,
                const gchar          *name,
                LrgScriptingCFunction func,
                gpointer              user_data,
                GError              **error)
{
	LrgScriptingGraph *self = LRG_SCRIPTING_GRAPH (base);
	HostFn *host;

	(void) error;
	host = g_new0 (HostFn, 1);
	host->func = func;
	host->user_data = user_data;
	g_hash_table_insert (self->hosts, g_strdup (name), host);
	return TRUE;
}

static gboolean
graph_get_global (LrgScripting *base,
                  const gchar  *name,
                  GValue       *value,
                  GError      **error)
{
	LrgScriptingGraph *self = LRG_SCRIPTING_GRAPH (base);
	GValue *slot;

	if (g_strcmp0 (name, "self") == 0)
	{
		g_value_init (value, G_TYPE_OBJECT);
		g_value_set_object (value, self->self_object);
		return TRUE;
	}
	if (g_strcmp0 (name, "blackboard") == 0)
	{
		g_value_init (value, LRG_TYPE_BLACKBOARD);
		g_value_set_object (value, self->blackboard);
		return TRUE;
	}
	slot = (GValue *) g_hash_table_lookup (self->vars, name);
	if (slot == NULL)
		slot = (GValue *) g_hash_table_lookup (self->extras, name);
	if (slot == NULL || !G_IS_VALUE (slot))
	{
		g_set_error (error, LRG_VSCRIPT_ERROR, LRG_VSCRIPT_ERROR_NOT_FOUND, "no global '%s'", name);
		return FALSE;
	}
	g_value_init (value, G_VALUE_TYPE (slot));
	g_value_copy (slot, value);
	return TRUE;
}

static gboolean
graph_set_global (LrgScripting *base,
                  const gchar  *name,
                  const GValue *value,
                  GError      **error)
{
	LrgScriptingGraph *self = LRG_SCRIPTING_GRAPH (base);
	GValue *copy;

	(void) error;
	if (g_strcmp0 (name, "self") == 0)
	{
		g_clear_object (&self->self_object);
		if (G_VALUE_HOLDS_OBJECT (value))
			self->self_object = g_value_dup_object (value);
		return TRUE;
	}
	if (g_strcmp0 (name, "blackboard") == 0)
	{
		g_clear_object (&self->blackboard);
		if (G_VALUE_HOLDS (value, LRG_TYPE_BLACKBOARD))
			self->blackboard = g_value_dup_object (value);
		return TRUE;
	}
	copy = g_new0 (GValue, 1);
	value_set_copy (copy, value);
	if (self->program != NULL && lrg_vscript_program_get_variable_type (self->program, name) != NULL)
		g_hash_table_insert (self->vars, g_strdup (name), copy);
	else if (g_hash_table_contains (self->vars, name))
		g_hash_table_insert (self->vars, g_strdup (name), copy);
	else
		g_hash_table_insert (self->extras, g_strdup (name), copy);
	return TRUE;
}

static void
graph_reset (LrgScripting *base)
{
	LrgScriptingGraph *self = LRG_SCRIPTING_GRAPH (base);

	g_clear_object (&self->program);
	g_clear_pointer (&self->path, g_free);
	clear_runtime (self, TRUE);
	g_hash_table_remove_all (self->vars);
	g_clear_object (&self->rng);
	self->rng = lrg_random_stream_new (0, 1);
	while (self->hooks->len > 0)
	{
		Hook *hook = (Hook *) g_ptr_array_index (self->hooks, 0);

		g_ptr_array_remove_index (self->hooks, 0);
		hook->owner = NULL;
		hook_free (hook);
	}
}

static void
graph_add_path (LrgScripting *base,
                const gchar  *path)
{
	(void) base;
	(void) path;
}

static gboolean
graph_has (LrgScripting *base,
           const gchar  *func_name)
{
	LrgScriptingGraph *self = LRG_SCRIPTING_GRAPH (base);

	if (g_hash_table_contains (self->hosts, func_name))
		return TRUE;
	if (self->program == NULL)
		return FALSE;
	if (g_strcmp0 (func_name, "start") == 0 || g_strcmp0 (func_name, "update") == 0 ||
	    g_strcmp0 (func_name, "fixed_update") == 0)
		return TRUE;
	if (event_named (self, "event.custom", "name", func_name))
		return TRUE;
	if (event_named (self, "event.input", "action", func_name))
		return TRUE;
	return FALSE;
}

static void
graph_finalize (GObject *object)
{
	LrgScriptingGraph *self = LRG_SCRIPTING_GRAPH (object);

	untrack_graph (self);
	while (self->hooks->len > 0)
	{
		Hook *hook = (Hook *) g_ptr_array_index (self->hooks, 0);

		g_ptr_array_remove_index (self->hooks, 0);
		hook->owner = NULL;
		hook_free (hook);
	}
	g_clear_object (&self->program);
	g_clear_object (&self->rng);
	g_clear_object (&self->self_object);
	g_clear_object (&self->blackboard);
	g_free (self->path);
	g_free (self->paused_node);
	g_hash_table_unref (self->vars);
	g_hash_table_unref (self->extras);
	g_hash_table_unref (self->hosts);
	g_hash_table_unref (self->hits);
	g_hash_table_unref (self->out_cache);
	g_hash_table_unref (self->pure_cache);
	g_hash_table_unref (self->pure_done);
	g_hash_table_unref (self->pulling);
	g_ptr_array_unref (self->log);
	g_ptr_array_unref (self->fibers);
	g_ptr_array_unref (self->active);
	g_ptr_array_unref (self->bps);
	g_ptr_array_unref (self->held);
	g_ptr_array_unref (self->hooks);
	while (g_queue_get_length (self->recent) > 0)
		g_ptr_array_unref ((GPtrArray *) g_queue_pop_head (self->recent));
	g_queue_free (self->recent);
	while (self->pending->len > 0)
	{
		Pending pending = g_array_index (self->pending, Pending, 0);

		pending_clear_one (&pending);
		g_array_remove_index (self->pending, 0);
	}
	g_array_free (self->pending, TRUE);
	if (self->paused_stack != NULL)
		g_array_free (self->paused_stack, TRUE);
	if (G_IS_VALUE (&self->last_result))
		g_value_unset (&self->last_result);
	G_OBJECT_CLASS (lrg_scripting_graph_parent_class)->finalize (object);
}

static void
lrg_scripting_graph_class_init (LrgScriptingGraphClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	LrgScriptingClass *scripting_class = LRG_SCRIPTING_CLASS (klass);

	object_class->finalize = graph_finalize;
	scripting_class->load_file = graph_load_file;
	scripting_class->load_string = graph_load_string;
	scripting_class->call_function = graph_call;
	scripting_class->register_function = graph_register;
	scripting_class->get_global = graph_get_global;
	scripting_class->set_global = graph_set_global;
	scripting_class->reset = graph_reset;
	scripting_class->add_search_path = graph_add_path;
	scripting_class->has_function = graph_has;

	sig_executed = g_signal_new ("executed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
	                             0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
	sig_logged = g_signal_new ("logged", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
	                           0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_scripting_graph_init (LrgScriptingGraph *self)
{
	self->rng = lrg_random_stream_new (0, 1);
	self->vars = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, value_free);
	self->extras = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, value_free);
	self->hosts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->log = g_ptr_array_new_with_free_func (g_free);
	self->fibers = g_ptr_array_new_with_free_func (fiber_free);
	self->hits = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->active = g_ptr_array_new_with_free_func (g_free);
	self->recent = g_queue_new ();
	self->bps = g_ptr_array_new_with_free_func (g_free);
	self->held = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	self->hooks = g_ptr_array_new ();
	self->pending = g_array_new (FALSE, TRUE, sizeof (Pending));
	self->out_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, value_free);
	self->pure_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, value_free);
	self->pure_done = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->pulling = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->paused_stack = stack_new ();
	track_graph (self);
}

LrgScriptingGraph *
lrg_scripting_graph_new (void)
{
	return g_object_new (LRG_TYPE_SCRIPTING_GRAPH, NULL);
}

LrgVscriptProgram *
lrg_scripting_graph_get_program (LrgScriptingGraph *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPTING_GRAPH (self), NULL);
	return self->program;
}

gchar **
lrg_scripting_graph_dup_log (LrgScriptingGraph *self)
{
	GStrvBuilder *builder;
	guint i;

	g_return_val_if_fail (LRG_IS_SCRIPTING_GRAPH (self), NULL);
	builder = g_strv_builder_new ();
	for (i = 0; i < self->log->len; i++)
		g_strv_builder_add (builder, (const gchar *) g_ptr_array_index (self->log, i));
	return g_strv_builder_end (builder);
}

guint
lrg_scripting_graph_get_steps (LrgScriptingGraph *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPTING_GRAPH (self), 0);
	return self->last_steps;
}

guint
lrg_scripting_graph_get_hit (LrgScriptingGraph *self,
                             const gchar       *node)
{
	guint *count;

	g_return_val_if_fail (LRG_IS_SCRIPTING_GRAPH (self), 0);
	g_return_val_if_fail (node != NULL, 0);
	count = (guint *) g_hash_table_lookup (self->hits, node);
	return count != NULL ? *count : 0;
}

gchar **
lrg_scripting_graph_dup_active (LrgScriptingGraph *self)
{
	GStrvBuilder *builder;
	GPtrArray *src;
	guint i;

	g_return_val_if_fail (LRG_IS_SCRIPTING_GRAPH (self), NULL);
	src = self->active;
	if (src->len == 0 && g_queue_get_length (self->recent) > 0)
		src = (GPtrArray *) g_queue_peek_tail (self->recent);
	builder = g_strv_builder_new ();
	for (i = 0; src != NULL && i < src->len; i++)
		g_strv_builder_add (builder, (const gchar *) g_ptr_array_index (src, i));
	return g_strv_builder_end (builder);
}

GPtrArray *
lrg_scripting_graph_dup_instances (void)
{
	GPtrArray *out;
	guint i;

	out = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = 0; live_graphs != NULL && i < live_graphs->len; i++)
		g_ptr_array_add (out, g_object_ref (g_ptr_array_index (live_graphs, i)));
	return out;
}

gboolean
lrg_scripting_graph_set_breakpoint (LrgScriptingGraph *self,
                                    const gchar       *path,
                                    const gchar       *node,
                                    gboolean           enabled)
{
	guint i;

	g_return_val_if_fail (node != NULL, FALSE);
	if (self != NULL)
		g_return_val_if_fail (LRG_IS_SCRIPTING_GRAPH (self), FALSE);
	if (path != NULL)
	{
		if (static_bps == NULL)
			static_bps = g_ptr_array_new_with_free_func (static_bp_free);
		for (i = 0; i < static_bps->len; i++)
		{
			StaticBp *bp = (StaticBp *) g_ptr_array_index (static_bps, i);

			if (g_strcmp0 (bp->path, path) == 0 && g_strcmp0 (bp->node, node) == 0)
			{
				if (!enabled)
					g_ptr_array_remove_index (static_bps, i);
				return enabled;
			}
		}
		if (enabled)
		{
			StaticBp *bp = g_new0 (StaticBp, 1);

			bp->path = g_strdup (path);
			bp->node = g_strdup (node);
			g_ptr_array_add (static_bps, bp);
		}
	}
	if (self != NULL)
	{
		for (i = 0; i < self->bps->len; i++)
		{
			if (g_strcmp0 ((const gchar *) g_ptr_array_index (self->bps, i), node) == 0)
			{
				if (!enabled && path == NULL)
					g_ptr_array_remove_index (self->bps, i);
				return enabled;
			}
		}
		if (enabled && (path == NULL || (self->path != NULL && g_strcmp0 (self->path, path) == 0)))
			g_ptr_array_add (self->bps, g_strdup (node));
	}
	return enabled;
}

gboolean
lrg_scripting_graph_get_breakpoint (LrgScriptingGraph *self,
                                    const gchar       *node)
{
	g_return_val_if_fail (LRG_IS_SCRIPTING_GRAPH (self), FALSE);
	g_return_val_if_fail (node != NULL, FALSE);
	return breakpoint_hit (self, node);
}

gboolean
lrg_scripting_graph_is_paused (LrgScriptingGraph *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPTING_GRAPH (self), FALSE);
	return self->paused;
}

const gchar *
lrg_scripting_graph_get_paused_node (LrgScriptingGraph *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPTING_GRAPH (self), NULL);
	return self->paused_node;
}

const gchar *
lrg_scripting_graph_get_path (LrgScriptingGraph *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPTING_GRAPH (self), NULL);
	return self->path;
}

GVariant *
lrg_scripting_graph_dup_variables (LrgScriptingGraph *self)
{
	GVariantBuilder builder;
	GHashTableIter  iter;
	gpointer        key;
	gpointer        value;

	g_return_val_if_fail (LRG_IS_SCRIPTING_GRAPH (self), NULL);
	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
	if (self->vars != NULL)
	{
		g_hash_table_iter_init (&iter, self->vars);
		while (g_hash_table_iter_next (&iter, &key, &value))
		{
			GVariant *v = lrg_vscript_priv_value_to_variant ((const GValue *) value);

			if (v == NULL)
				continue;
			g_variant_builder_add (&builder, "{sv}", (const gchar *) key, v);
			g_variant_unref (v);
		}
	}
	return g_variant_ref_sink (g_variant_builder_end (&builder));
}

gboolean
lrg_scripting_graph_continue (LrgScriptingGraph *self,
                              GError           **error)
{
	GArray *stack;
	Act act;

	g_return_val_if_fail (LRG_IS_SCRIPTING_GRAPH (self), FALSE);
	if (!self->paused || self->paused_stack == NULL || self->paused_stack->len == 0)
	{
		g_set_error (error, LRG_VSCRIPT_ERROR, LRG_VSCRIPT_ERROR_RUNTIME, "not paused");
		return FALSE;
	}
	stack = self->paused_stack;
	self->paused_stack = stack_new ();
	self->paused = FALSE;
	g_clear_pointer (&self->paused_node, g_free);
	self->steps = 0;
	self->running = TRUE;
	act = run_stack (self, stack, error);
	g_array_free (stack, TRUE);
	finish_call (self, NULL, act != ACT_ERROR);
	return act != ACT_ERROR;
}
