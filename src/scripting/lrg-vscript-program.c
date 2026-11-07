/* lrg-vscript-program.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVscriptProgram: the `.rvscript` visual script format (see the
 * header for the model).
 *
 * File layout (canonical order, which is also the write order):
 *
 *   version: 1
 *   name: door
 *   description: Opens the door when the player is near
 *   view: graph                 # or blocks
 *   seed: 7                     # only when not 0
 *   step_budget: 5000           # only when not the default
 *   fixed_step: 0.02            # only when not 1/60
 *   variables:
 *   - {name: opened, type: bool, default: false}
 *   nodes:
 *   - {id: start, type: event.start, x: 0.0, y: 0.0, params: {...}, inputs: {...}}
 *   links:
 *   - {from: start.then, to: log.exec}
 *   comments:
 *   - {id: c1, text: Setup, x: .., y: .., width: .., height: .., color: "#..."}
 *   subgraphs:
 *   - name: clamp01
 *     inputs: [{name: x, type: float}]
 *     outputs: [{name: y, type: float}]
 *     nodes: [...]
 *     links: [...]
 *     comments: [...]
 *
 * Every mapping keeps the keys it does not know (an "extra" YamlMapping)
 * and writes them back after the known ones, so files written by newer
 * tools survive a round trip. The scalar helpers (typing plain scalars,
 * writing numbers with a decimal point, key paths in errors) are the AI
 * formats' (lrg-bt-asset.c), so all the 0.9.0 YAML assets read and write
 * scalars the same way.
 *
 * The node catalogue is a static table; most kinds have fixed pins given
 * as a compact spec ("i:exec:exec i:condition:bool o:true:exec"), the
 * others (sequence counts, custom event arguments, format placeholders,
 * GI signatures, subgraph interfaces, signal arguments, property types)
 * are computed in node_pins().
 *
 * GObject-Introspection (`gi.call` pins and invocation, the palette
 * search) is compiled only with the GI runtime (LRG_HAS_GI); the scanner
 * never sees it (__GI_SCANNER__), so the Libregnum typelib does not depend
 * on GIRepository.
 */

#include "config.h"

#define LRG_AI_PRIVATE
#define LRG_VSCRIPT_PRIVATE
#include "lrg-vscript-program.h"
#include "../ai/lrg-bt-asset.h"
#include <yaml-glib.h>
#include <string.h>
#include <math.h>

#if defined(LRG_HAS_GI) && !defined(__GI_SCANNER__)
#define VSCRIPT_HAVE_GI 1
#include <girepository/girepository.h>
#endif

#define DOMAIN              LRG_VSCRIPT_ERROR
#define DEFAULT_FIXED_STEP  (1.0 / 60.0)

/* ==========================================================================
 * Enums, error domain, boxed types
 * ========================================================================== */

G_DEFINE_QUARK (lrg-vscript-error-quark, lrg_vscript_error)

#define DEFINE_ENUM(func, Name, ...)                                         \
	GType                                                                    \
	func (void)                                                              \
	{                                                                        \
		static gsize type_id = 0;                                            \
		if (g_once_init_enter (&type_id))                                    \
		{                                                                    \
			static const GEnumValue values[] = { __VA_ARGS__, { 0, NULL, NULL } }; \
			GType id = g_enum_register_static (g_intern_static_string (Name), values); \
			g_once_init_leave (&type_id, id);                                \
		}                                                                    \
		return (GType) type_id;                                              \
	}

DEFINE_ENUM (lrg_vscript_error_get_type, "LrgVscriptError",
             { LRG_VSCRIPT_ERROR_PARSE, "LRG_VSCRIPT_ERROR_PARSE", "parse" },
             { LRG_VSCRIPT_ERROR_INVALID, "LRG_VSCRIPT_ERROR_INVALID", "invalid" },
             { LRG_VSCRIPT_ERROR_IO, "LRG_VSCRIPT_ERROR_IO", "io" },
             { LRG_VSCRIPT_ERROR_NOT_FOUND, "LRG_VSCRIPT_ERROR_NOT_FOUND", "not-found" },
             { LRG_VSCRIPT_ERROR_RUNTIME, "LRG_VSCRIPT_ERROR_RUNTIME", "runtime" },
             { LRG_VSCRIPT_ERROR_BUDGET, "LRG_VSCRIPT_ERROR_BUDGET", "budget" },
             { LRG_VSCRIPT_ERROR_UNSUPPORTED, "LRG_VSCRIPT_ERROR_UNSUPPORTED", "unsupported" })

DEFINE_ENUM (lrg_vscript_pin_direction_get_type, "LrgVscriptPinDirection",
             { LRG_VSCRIPT_PIN_INPUT, "LRG_VSCRIPT_PIN_INPUT", "input" },
             { LRG_VSCRIPT_PIN_OUTPUT, "LRG_VSCRIPT_PIN_OUTPUT", "output" })

DEFINE_ENUM (lrg_vscript_view_get_type, "LrgVscriptView",
             { LRG_VSCRIPT_VIEW_GRAPH, "LRG_VSCRIPT_VIEW_GRAPH", "graph" },
             { LRG_VSCRIPT_VIEW_BLOCKS, "LRG_VSCRIPT_VIEW_BLOCKS", "blocks" })

DEFINE_ENUM (lrg_vscript_severity_get_type, "LrgVscriptSeverity",
             { LRG_VSCRIPT_SEVERITY_INFO, "LRG_VSCRIPT_SEVERITY_INFO", "info" },
             { LRG_VSCRIPT_SEVERITY_WARNING, "LRG_VSCRIPT_SEVERITY_WARNING", "warning" },
             { LRG_VSCRIPT_SEVERITY_ERROR, "LRG_VSCRIPT_SEVERITY_ERROR", "error" })

#undef DEFINE_ENUM

/* --- pins ----------------------------------------------------------------- */

LrgVscriptPin *
lrg_vscript_pin_new (const gchar            *name,
                     LrgVscriptPinDirection  direction,
                     gboolean                exec,
                     const gchar            *type)
{
	LrgVscriptPin *self;

	g_return_val_if_fail (name != NULL, NULL);

	self = g_new0 (LrgVscriptPin, 1);
	self->name = g_strdup (name);
	self->direction = direction;
	self->exec = exec;
	self->type = g_strdup (exec ? "exec" : (type != NULL ? type : "any"));
	return self;
}

LrgVscriptPin *
lrg_vscript_pin_copy (const LrgVscriptPin *self)
{
	LrgVscriptPin *copy;

	g_return_val_if_fail (self != NULL, NULL);

	copy = lrg_vscript_pin_new (self->name, self->direction, self->exec, self->type);
	copy->label = g_strdup (self->label);
	if (self->extra != NULL)
		copy->extra = yaml_mapping_ref ((YamlMapping *) self->extra);
	return copy;
}

void
lrg_vscript_pin_free (LrgVscriptPin *self)
{
	if (self == NULL)
		return;
	g_free (self->name);
	g_free (self->label);
	g_free (self->type);
	g_clear_pointer ((YamlMapping **) &self->extra, yaml_mapping_unref);
	g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgVscriptPin, lrg_vscript_pin, lrg_vscript_pin_copy, lrg_vscript_pin_free)

/* --- links ---------------------------------------------------------------- */

LrgVscriptLink *
lrg_vscript_link_new (const gchar *from_node,
                      const gchar *from_pin,
                      const gchar *to_node,
                      const gchar *to_pin)
{
	LrgVscriptLink *self;

	g_return_val_if_fail (from_node != NULL && from_pin != NULL, NULL);
	g_return_val_if_fail (to_node != NULL && to_pin != NULL, NULL);

	self = g_new0 (LrgVscriptLink, 1);
	self->from_node = g_strdup (from_node);
	self->from_pin = g_strdup (from_pin);
	self->to_node = g_strdup (to_node);
	self->to_pin = g_strdup (to_pin);
	return self;
}

LrgVscriptLink *
lrg_vscript_link_copy (const LrgVscriptLink *self)
{
	LrgVscriptLink *copy;

	g_return_val_if_fail (self != NULL, NULL);

	copy = lrg_vscript_link_new (self->from_node, self->from_pin, self->to_node, self->to_pin);
	if (self->extra != NULL)
		copy->extra = yaml_mapping_ref ((YamlMapping *) self->extra);
	return copy;
}

void
lrg_vscript_link_free (LrgVscriptLink *self)
{
	if (self == NULL)
		return;
	g_free (self->from_node);
	g_free (self->from_pin);
	g_free (self->to_node);
	g_free (self->to_pin);
	g_clear_pointer ((YamlMapping **) &self->extra, yaml_mapping_unref);
	g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgVscriptLink, lrg_vscript_link, lrg_vscript_link_copy, lrg_vscript_link_free)

/* --- comments ------------------------------------------------------------- */

LrgVscriptComment *
lrg_vscript_comment_new (const gchar *id,
                         const gchar *text,
                         gdouble      x,
                         gdouble      y,
                         gdouble      width,
                         gdouble      height)
{
	LrgVscriptComment *self = g_new0 (LrgVscriptComment, 1);

	self->id = g_strdup (id);
	self->text = g_strdup (text != NULL ? text : "");
	self->x = x;
	self->y = y;
	self->width = width;
	self->height = height;
	return self;
}

LrgVscriptComment *
lrg_vscript_comment_copy (const LrgVscriptComment *self)
{
	LrgVscriptComment *copy;

	g_return_val_if_fail (self != NULL, NULL);

	copy = lrg_vscript_comment_new (self->id, self->text, self->x, self->y, self->width, self->height);
	copy->color = g_strdup (self->color);
	if (self->extra != NULL)
		copy->extra = yaml_mapping_ref ((YamlMapping *) self->extra);
	return copy;
}

void
lrg_vscript_comment_free (LrgVscriptComment *self)
{
	if (self == NULL)
		return;
	g_free (self->id);
	g_free (self->text);
	g_free (self->color);
	g_clear_pointer ((YamlMapping **) &self->extra, yaml_mapping_unref);
	g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgVscriptComment, lrg_vscript_comment, lrg_vscript_comment_copy, lrg_vscript_comment_free)

/* --- issues --------------------------------------------------------------- */

static LrgVscriptIssue *
issue_new (LrgVscriptSeverity  severity,
           const gchar        *graph,
           const gchar        *node,
           const gchar        *path,
           const gchar        *message)
{
	LrgVscriptIssue *self = g_new0 (LrgVscriptIssue, 1);

	self->severity = severity;
	self->graph = g_strdup (graph != NULL ? graph : "");
	self->node = g_strdup (node);
	self->path = g_strdup (path != NULL ? path : "");
	self->message = g_strdup (message);
	return self;
}

LrgVscriptIssue *
lrg_vscript_issue_copy (const LrgVscriptIssue *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return issue_new (self->severity, self->graph, self->node, self->path, self->message);
}

void
lrg_vscript_issue_free (LrgVscriptIssue *self)
{
	if (self == NULL)
		return;
	g_free (self->graph);
	g_free (self->node);
	g_free (self->path);
	g_free (self->message);
	g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgVscriptIssue, lrg_vscript_issue, lrg_vscript_issue_copy, lrg_vscript_issue_free)

/* ==========================================================================
 * A small ordered string -> GVariant dictionary (params, input literals)
 * ========================================================================== */

typedef struct
{
	gchar    *key;
	GVariant *value;
} Entry;

static void
entry_free (gpointer data)
{
	Entry *e = data;

	g_free (e->key);
	g_variant_unref (e->value);
	g_free (e);
}

static GPtrArray *
dict_new (void)
{
	return g_ptr_array_new_with_free_func (entry_free);
}

static Entry *
dict_find (GPtrArray   *dict,
           const gchar *key,
           guint       *index)
{
	guint i;

	for (i = 0; dict != NULL && i < dict->len; i++)
	{
		Entry *e = g_ptr_array_index (dict, i);

		if (g_strcmp0 (e->key, key) == 0)
		{
			if (index != NULL)
				*index = i;
			return e;
		}
	}
	return NULL;
}

/* sets (consuming a floating ref) or, with a NULL value, removes */
static void
dict_set (GPtrArray   *dict,
          const gchar *key,
          GVariant    *value)
{
	guint  index;
	Entry *e = dict_find (dict, key, &index);

	if (value == NULL)
	{
		if (e != NULL)
			g_ptr_array_remove_index (dict, index);
		return;
	}
	g_variant_ref_sink (value);
	if (e != NULL)
	{
		g_variant_unref (e->value);
		e->value = value;
		return;
	}
	e = g_new0 (Entry, 1);
	e->key = g_strdup (key);
	e->value = value;
	g_ptr_array_add (dict, e);
}

static gchar **
dict_keys (GPtrArray *dict)
{
	GStrvBuilder *b = g_strv_builder_new ();
	gchar       **out;
	guint         i;

	for (i = 0; dict != NULL && i < dict->len; i++)
		g_strv_builder_add (b, ((Entry *) g_ptr_array_index (dict, i))->key);
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);
	return out;
}

/* only scalars can be written: b, integers, d, s */
static gboolean
variant_is_scalar (GVariant *v)
{
	return g_variant_is_of_type (v, G_VARIANT_TYPE_BOOLEAN) || g_variant_is_of_type (v, G_VARIANT_TYPE_INT64) ||
	       g_variant_is_of_type (v, G_VARIANT_TYPE_INT32) || g_variant_is_of_type (v, G_VARIANT_TYPE_UINT32) ||
	       g_variant_is_of_type (v, G_VARIANT_TYPE_UINT64) || g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE) ||
	       g_variant_is_of_type (v, G_VARIANT_TYPE_STRING);
}

/* ==========================================================================
 * Ids
 * ========================================================================== */

static gboolean
id_is_valid (const gchar *id)
{
	const gchar *p;

	if (id == NULL || *id == '\0')
		return FALSE;
	for (p = id; *p != '\0'; p++)
	{
		if (!g_ascii_isalnum (*p) && *p != '_' && *p != '-')
			return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * LrgVscriptNode
 * ========================================================================== */

struct _LrgVscriptNode
{
	GObject      parent_instance;

	gchar       *id;
	gchar       *kind;
	gdouble      x;
	gdouble      y;
	GPtrArray   *params;    /* Entry */
	GPtrArray   *inputs;    /* Entry */
	YamlMapping *extra;
	gboolean     in_graph;
};

G_DEFINE_FINAL_TYPE (LrgVscriptNode, lrg_vscript_node, G_TYPE_OBJECT)

static void
lrg_vscript_node_finalize (GObject *object)
{
	LrgVscriptNode *self = LRG_VSCRIPT_NODE (object);

	g_free (self->id);
	g_free (self->kind);
	g_ptr_array_unref (self->params);
	g_ptr_array_unref (self->inputs);
	g_clear_pointer (&self->extra, yaml_mapping_unref);
	G_OBJECT_CLASS (lrg_vscript_node_parent_class)->finalize (object);
}

static void
lrg_vscript_node_class_init (LrgVscriptNodeClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_vscript_node_finalize;
}

static void
lrg_vscript_node_init (LrgVscriptNode *self)
{
	self->params = dict_new ();
	self->inputs = dict_new ();
}

LrgVscriptNode *
lrg_vscript_node_new (const gchar *id,
                      const gchar *kind)
{
	LrgVscriptNode *self;

	g_return_val_if_fail (id != NULL, NULL);
	g_return_val_if_fail (kind != NULL, NULL);

	self = g_object_new (LRG_TYPE_VSCRIPT_NODE, NULL);
	self->id = g_strdup (id);
	self->kind = g_strdup (kind);
	return self;
}

const gchar *
lrg_vscript_node_get_id (LrgVscriptNode *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_NODE (self), NULL);
	return self->id;
}

const gchar *
lrg_vscript_node_get_kind (LrgVscriptNode *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_NODE (self), NULL);
	return self->kind;
}

void
lrg_vscript_node_get_position (LrgVscriptNode *self,
                               gdouble        *x,
                               gdouble        *y)
{
	g_return_if_fail (LRG_IS_VSCRIPT_NODE (self));
	if (x != NULL)
		*x = self->x;
	if (y != NULL)
		*y = self->y;
}

void
lrg_vscript_node_set_position (LrgVscriptNode *self,
                               gdouble         x,
                               gdouble         y)
{
	g_return_if_fail (LRG_IS_VSCRIPT_NODE (self));
	self->x = x;
	self->y = y;
}

GVariant *
lrg_vscript_node_get_param (LrgVscriptNode *self,
                            const gchar    *key)
{
	Entry *e;

	g_return_val_if_fail (LRG_IS_VSCRIPT_NODE (self), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	e = dict_find (self->params, key, NULL);
	return e != NULL ? e->value : NULL;
}

const gchar *
lrg_vscript_node_get_param_string (LrgVscriptNode *self,
                                   const gchar    *key)
{
	GVariant *v = lrg_vscript_node_get_param (self, key);

	return (v != NULL && g_variant_is_of_type (v, G_VARIANT_TYPE_STRING)) ? g_variant_get_string (v, NULL) : NULL;
}

/* an integer parameter (numbers and numeric strings accepted) */
static gint64
param_int (LrgVscriptNode *node,
           const gchar    *key,
           gint64          fallback)
{
	GVariant *v = lrg_vscript_node_get_param (node, key);

	if (v == NULL)
		return fallback;
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT64))
		return g_variant_get_int64 (v);
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT32))
		return g_variant_get_int32 (v);
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE))
		return (gint64) g_variant_get_double (v);
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_STRING))
		return g_ascii_strtoll (g_variant_get_string (v, NULL), NULL, 10);
	return fallback;
}

static gboolean
param_bool (LrgVscriptNode *node,
            const gchar    *key)
{
	GVariant *v = lrg_vscript_node_get_param (node, key);

	return v != NULL && g_variant_is_of_type (v, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean (v);
}

void
lrg_vscript_node_set_param (LrgVscriptNode *self,
                            const gchar    *key,
                            GVariant       *value)
{
	g_return_if_fail (LRG_IS_VSCRIPT_NODE (self));
	g_return_if_fail (key != NULL);
	g_return_if_fail (value == NULL || variant_is_scalar (value));

	dict_set (self->params, key, value);
}

gchar **
lrg_vscript_node_dup_param_keys (LrgVscriptNode *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_NODE (self), NULL);
	return dict_keys (self->params);
}

GVariant *
lrg_vscript_node_get_input (LrgVscriptNode *self,
                            const gchar    *pin)
{
	Entry *e;

	g_return_val_if_fail (LRG_IS_VSCRIPT_NODE (self), NULL);
	g_return_val_if_fail (pin != NULL, NULL);

	e = dict_find (self->inputs, pin, NULL);
	return e != NULL ? e->value : NULL;
}

void
lrg_vscript_node_set_input (LrgVscriptNode *self,
                            const gchar    *pin,
                            GVariant       *value)
{
	g_return_if_fail (LRG_IS_VSCRIPT_NODE (self));
	g_return_if_fail (pin != NULL);
	g_return_if_fail (value == NULL || variant_is_scalar (value));

	dict_set (self->inputs, pin, value);
}

gchar **
lrg_vscript_node_dup_input_keys (LrgVscriptNode *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_NODE (self), NULL);
	return dict_keys (self->inputs);
}

/* ==========================================================================
 * LrgVscriptGraph
 * ========================================================================== */

struct _LrgVscriptGraph
{
	GObject      parent_instance;

	gchar       *name;
	GPtrArray   *nodes;      /* LrgVscriptNode (owned) */
	GHashTable  *by_id;      /* id -> node (borrowed) */
	GPtrArray   *links;      /* LrgVscriptLink */
	GPtrArray   *comments;   /* LrgVscriptComment */
	GPtrArray   *iface[2];   /* LrgVscriptPin, by direction */
	YamlMapping *extra;
};

G_DEFINE_FINAL_TYPE (LrgVscriptGraph, lrg_vscript_graph, G_TYPE_OBJECT)

static void
lrg_vscript_graph_finalize (GObject *object)
{
	LrgVscriptGraph *self = LRG_VSCRIPT_GRAPH (object);

	g_free (self->name);
	g_hash_table_unref (self->by_id);
	g_ptr_array_unref (self->nodes);
	g_ptr_array_unref (self->links);
	g_ptr_array_unref (self->comments);
	g_ptr_array_unref (self->iface[0]);
	g_ptr_array_unref (self->iface[1]);
	g_clear_pointer (&self->extra, yaml_mapping_unref);
	G_OBJECT_CLASS (lrg_vscript_graph_parent_class)->finalize (object);
}

static void
lrg_vscript_graph_class_init (LrgVscriptGraphClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_vscript_graph_finalize;
}

static void
lrg_vscript_graph_init (LrgVscriptGraph *self)
{
	self->nodes = g_ptr_array_new_with_free_func (g_object_unref);
	self->by_id = g_hash_table_new (g_str_hash, g_str_equal);
	self->links = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_vscript_link_free);
	self->comments = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_vscript_comment_free);
	self->iface[0] = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_vscript_pin_free);
	self->iface[1] = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_vscript_pin_free);
}

static LrgVscriptGraph *
graph_new (const gchar *name)
{
	LrgVscriptGraph *self = g_object_new (LRG_TYPE_VSCRIPT_GRAPH, NULL);

	self->name = g_strdup (name != NULL ? name : "");
	return self;
}

const gchar *
lrg_vscript_graph_get_name (LrgVscriptGraph *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), NULL);
	return self->name;
}

guint
lrg_vscript_graph_get_n_nodes (LrgVscriptGraph *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), 0);
	return self->nodes->len;
}

LrgVscriptNode *
lrg_vscript_graph_get_nth_node (LrgVscriptGraph *self,
                                guint            index)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), NULL);
	return index < self->nodes->len ? g_ptr_array_index (self->nodes, index) : NULL;
}

LrgVscriptNode *
lrg_vscript_graph_get_node (LrgVscriptGraph *self,
                            const gchar     *id)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), NULL);
	return id != NULL ? g_hash_table_lookup (self->by_id, id) : NULL;
}

gboolean
lrg_vscript_graph_add_node (LrgVscriptGraph  *self,
                            LrgVscriptNode   *node,
                            GError          **error)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), FALSE);
	g_return_val_if_fail (LRG_IS_VSCRIPT_NODE (node), FALSE);
	g_return_val_if_fail (!node->in_graph, FALSE);

	if (!id_is_valid (node->id))
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_INVALID,
		             "'%s' is not a valid node id (letters, digits, '_' and '-')", node->id);
		return FALSE;
	}
	if (g_hash_table_contains (self->by_id, node->id))
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_INVALID, "a node '%s' already exists", node->id);
		return FALSE;
	}
	node->in_graph = TRUE;
	g_ptr_array_add (self->nodes, g_object_ref (node));
	g_hash_table_insert (self->by_id, node->id, node);
	return TRUE;
}

gchar *
lrg_vscript_graph_dup_free_id (LrgVscriptGraph *self,
                               const gchar     *stem)
{
	g_autofree gchar *clean = NULL;
	gchar            *p;
	guint             n;

	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), NULL);
	g_return_val_if_fail (stem != NULL, NULL);

	/* "flow.branch" -> "branch"; anything else invalid becomes '_' */
	p = strrchr (stem, '.');
	clean = g_strdup (p != NULL ? p + 1 : stem);
	for (p = clean; *p != '\0'; p++)
	{
		if (!g_ascii_isalnum (*p) && *p != '_' && *p != '-')
			*p = '_';
	}
	if (*clean == '\0')
	{
		g_free (clean);
		clean = g_strdup ("node");
	}
	if (!g_hash_table_contains (self->by_id, clean))
		return g_steal_pointer (&clean);
	for (n = 2;; n++)
	{
		gchar *id = g_strdup_printf ("%s%u", clean, n);

		if (!g_hash_table_contains (self->by_id, id))
			return id;
		g_free (id);
	}
}

gboolean
lrg_vscript_graph_remove_node (LrgVscriptGraph *self,
                               const gchar     *id)
{
	LrgVscriptNode *node;
	guint           i;

	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), FALSE);

	node = lrg_vscript_graph_get_node (self, id);
	if (node == NULL)
		return FALSE;

	/* the links touching it go first (they name the id) */
	for (i = self->links->len; i > 0; i--)
	{
		LrgVscriptLink *l = g_ptr_array_index (self->links, i - 1);

		if (g_strcmp0 (l->from_node, id) == 0 || g_strcmp0 (l->to_node, id) == 0)
			g_ptr_array_remove_index (self->links, i - 1);
	}
	g_hash_table_remove (self->by_id, node->id);
	node->in_graph = FALSE;
	g_ptr_array_remove (self->nodes, node);
	return TRUE;
}

gboolean
lrg_vscript_graph_rename_node (LrgVscriptGraph  *self,
                               const gchar      *id,
                               const gchar      *new_id,
                               GError          **error)
{
	LrgVscriptNode *node;
	guint           i;

	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), FALSE);
	g_return_val_if_fail (new_id != NULL, FALSE);

	node = lrg_vscript_graph_get_node (self, id);
	if (node == NULL)
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_NOT_FOUND, "no node '%s'", id);
		return FALSE;
	}
	if (g_strcmp0 (id, new_id) == 0)
		return TRUE;
	if (!id_is_valid (new_id))
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_INVALID,
		             "'%s' is not a valid node id (letters, digits, '_' and '-')", new_id);
		return FALSE;
	}
	if (g_hash_table_contains (self->by_id, new_id))
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_INVALID, "a node '%s' already exists", new_id);
		return FALSE;
	}
	for (i = 0; i < self->links->len; i++)
	{
		LrgVscriptLink *l = g_ptr_array_index (self->links, i);

		if (g_strcmp0 (l->from_node, id) == 0)
		{
			g_free (l->from_node);
			l->from_node = g_strdup (new_id);
		}
		if (g_strcmp0 (l->to_node, id) == 0)
		{
			g_free (l->to_node);
			l->to_node = g_strdup (new_id);
		}
	}
	g_hash_table_remove (self->by_id, node->id);
	g_free (node->id);
	node->id = g_strdup (new_id);
	g_hash_table_insert (self->by_id, node->id, node);
	return TRUE;
}

GPtrArray *
lrg_vscript_graph_get_links (LrgVscriptGraph *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), NULL);
	return self->links;
}

static gint
link_index (LrgVscriptGraph *self,
            const gchar     *from_node,
            const gchar     *from_pin,
            const gchar     *to_node,
            const gchar     *to_pin)
{
	guint i;

	for (i = 0; i < self->links->len; i++)
	{
		LrgVscriptLink *l = g_ptr_array_index (self->links, i);

		if (g_strcmp0 (l->from_node, from_node) == 0 && g_strcmp0 (l->from_pin, from_pin) == 0 &&
		    g_strcmp0 (l->to_node, to_node) == 0 && g_strcmp0 (l->to_pin, to_pin) == 0)
			return (gint) i;
	}
	return -1;
}

gboolean
lrg_vscript_graph_add_link (LrgVscriptGraph *self,
                            const gchar     *from_node,
                            const gchar     *from_pin,
                            const gchar     *to_node,
                            const gchar     *to_pin)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), FALSE);
	g_return_val_if_fail (from_pin != NULL && to_pin != NULL, FALSE);

	if (lrg_vscript_graph_get_node (self, from_node) == NULL || lrg_vscript_graph_get_node (self, to_node) == NULL)
		return FALSE;
	if (link_index (self, from_node, from_pin, to_node, to_pin) >= 0)
		return FALSE;
	g_ptr_array_add (self->links, lrg_vscript_link_new (from_node, from_pin, to_node, to_pin));
	return TRUE;
}

gboolean
lrg_vscript_graph_remove_link (LrgVscriptGraph *self,
                               const gchar     *from_node,
                               const gchar     *from_pin,
                               const gchar     *to_node,
                               const gchar     *to_pin)
{
	gint i;

	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), FALSE);

	i = link_index (self, from_node, from_pin, to_node, to_pin);
	if (i < 0)
		return FALSE;
	g_ptr_array_remove_index (self->links, (guint) i);
	return TRUE;
}

const LrgVscriptLink *
lrg_vscript_graph_find_link_into (LrgVscriptGraph *self,
                                  const gchar     *to_node,
                                  const gchar     *to_pin)
{
	guint i;

	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), NULL);

	for (i = 0; i < self->links->len; i++)
	{
		LrgVscriptLink *l = g_ptr_array_index (self->links, i);

		if (g_strcmp0 (l->to_node, to_node) == 0 && g_strcmp0 (l->to_pin, to_pin) == 0)
			return l;
	}
	return NULL;
}

const LrgVscriptLink *
lrg_vscript_graph_find_link_from (LrgVscriptGraph *self,
                                  const gchar     *from_node,
                                  const gchar     *from_pin)
{
	guint i;

	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), NULL);

	for (i = 0; i < self->links->len; i++)
	{
		LrgVscriptLink *l = g_ptr_array_index (self->links, i);

		if (g_strcmp0 (l->from_node, from_node) == 0 && g_strcmp0 (l->from_pin, from_pin) == 0)
			return l;
	}
	return NULL;
}

GPtrArray *
lrg_vscript_graph_get_comments (LrgVscriptGraph *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), NULL);
	return self->comments;
}

LrgVscriptComment *
lrg_vscript_graph_get_comment (LrgVscriptGraph *self,
                               const gchar     *id)
{
	guint i;

	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), NULL);

	for (i = 0; id != NULL && i < self->comments->len; i++)
	{
		LrgVscriptComment *c = g_ptr_array_index (self->comments, i);

		if (g_strcmp0 (c->id, id) == 0)
			return c;
	}
	return NULL;
}

const gchar *
lrg_vscript_graph_add_comment (LrgVscriptGraph         *self,
                               const LrgVscriptComment *comment)
{
	LrgVscriptComment *c;

	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), NULL);
	g_return_val_if_fail (comment != NULL, NULL);

	c = lrg_vscript_comment_copy (comment);
	if (!id_is_valid (c->id) || lrg_vscript_graph_get_comment (self, c->id) != NULL)
	{
		guint n;

		for (n = self->comments->len + 1;; n++)
		{
			g_autofree gchar *id = g_strdup_printf ("comment%u", n);

			if (lrg_vscript_graph_get_comment (self, id) == NULL)
			{
				g_free (c->id);
				c->id = g_steal_pointer (&id);
				break;
			}
		}
	}
	g_ptr_array_add (self->comments, c);
	return c->id;
}

gboolean
lrg_vscript_graph_remove_comment (LrgVscriptGraph *self,
                                  const gchar     *id)
{
	LrgVscriptComment *c;

	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), FALSE);

	c = lrg_vscript_graph_get_comment (self, id);
	return c != NULL && g_ptr_array_remove (self->comments, c);
}

GPtrArray *
lrg_vscript_graph_get_interface (LrgVscriptGraph        *self,
                                 LrgVscriptPinDirection  direction)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), NULL);
	return self->iface[direction == LRG_VSCRIPT_PIN_OUTPUT ? 1 : 0];
}

void
lrg_vscript_graph_set_interface_pin (LrgVscriptGraph        *self,
                                     LrgVscriptPinDirection  direction,
                                     const gchar            *name,
                                     const gchar            *type)
{
	GPtrArray *pins;
	guint      i;

	g_return_if_fail (LRG_IS_VSCRIPT_GRAPH (self));
	g_return_if_fail (name != NULL);

	pins = lrg_vscript_graph_get_interface (self, direction);
	for (i = 0; i < pins->len; i++)
	{
		LrgVscriptPin *p = g_ptr_array_index (pins, i);

		if (g_strcmp0 (p->name, name) != 0)
			continue;
		if (type == NULL)
			g_ptr_array_remove_index (pins, i);
		else
		{
			g_free (p->type);
			p->type = g_strdup (type);
		}
		return;
	}
	if (type != NULL)
		g_ptr_array_add (pins, lrg_vscript_pin_new (name, direction, FALSE, type));
}
