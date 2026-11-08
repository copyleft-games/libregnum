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
#ifndef __GI_SCANNER__
#define LRG_VSCRIPT_PRIVATE
#endif
#include "lrg-vscript-program.h"
#include "../ai/lrg-bt-asset.h"
#include <yaml-glib.h>
#include <string.h>
#include <math.h>

#ifdef LRG_HAS_GI
#ifndef __GI_SCANNER__
#define VSCRIPT_HAVE_GI 1
#include <girepository/girepository.h>
#endif
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
	const gchar      *dot;
	gchar            *p;
	guint             n;

	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), NULL);
	g_return_val_if_fail (stem != NULL, NULL);

	/* "flow.branch" -> "branch"; anything else invalid becomes '_' */
	dot = strrchr (stem, '.');
	clean = g_strdup (dot != NULL ? dot + 1 : stem);
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
	gint       slot;
	guint      i;

	g_return_if_fail (LRG_IS_VSCRIPT_GRAPH (self));
	g_return_if_fail (name != NULL);

	/* the getter can return NULL on a failed precondition, so index the
	 * arrays this function already knows are allocated */
	if (direction != LRG_VSCRIPT_PIN_INPUT && direction != LRG_VSCRIPT_PIN_OUTPUT)
		return;
	slot = direction == LRG_VSCRIPT_PIN_OUTPUT ? 1 : 0;
	pins = self->iface[slot];
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

/* ==========================================================================
 * Node catalogue
 * ========================================================================== */

#define KIND_EVENT LRG_VSCRIPT_KIND_EVENT
#define KIND_PURE  LRG_VSCRIPT_KIND_PURE

typedef struct
{
	const gchar *kind;
	const gchar *title;
	const gchar *category;
	const gchar *description;
	guint        flags;
	const gchar *pins;   /* NULL: computed in dup_node_pins() */
} KindInfo;

static const KindInfo KIND_TABLE[] = {
	{ "event.start", "Start", "Events", "Runs once when the script loads", KIND_EVENT, "o:then:exec" },
	{ "event.update", "Update", "Events", "Runs every frame", KIND_EVENT, "o:then:exec o:dt:float" },
	{ "event.fixed_update", "Fixed Update", "Events", "Runs on the fixed step", KIND_EVENT, "o:then:exec o:dt:float" },
	{ "event.custom", "Custom Event", "Events", "A named event", KIND_EVENT, NULL },
	{ "event.signal", "Signal", "Events", "A GObject signal", KIND_EVENT, NULL },
	{ "event.input", "Input", "Events", "An input action", KIND_EVENT, "o:then:exec o:pressed:bool o:value:float" },
	{ "value.bool", "Bool", "Values", "A boolean literal", KIND_PURE, "o:value:bool" },
	{ "value.int", "Int", "Values", "An integer literal", KIND_PURE, "o:value:int" },
	{ "value.float", "Float", "Values", "A float literal", KIND_PURE, "o:value:float" },
	{ "value.string", "String", "Values", "A string literal", KIND_PURE, "o:value:string" },
	{ "value.list", "List", "Values", "A list built from the items parameter", KIND_PURE, "o:value:list" },
	{ "value.self", "Self", "Values", "The host object", KIND_PURE, "o:value:object" },
	{ "var.get", "Get Variable", "Variables", "Reads a graph variable", KIND_PURE, NULL },
	{ "var.set", "Set Variable", "Variables", "Writes a graph variable", 0, NULL },
	{ "blackboard.get", "Get Blackboard", "Blackboard", "Reads a blackboard key", KIND_PURE, "i:key:string o:value:any" },
	{ "blackboard.set", "Set Blackboard", "Blackboard", "Writes a blackboard key", 0, "i:exec:exec i:key:string i:value:any o:then:exec" },
	{ "flow.branch", "Branch", "Flow", "Follows true or false", 0, "i:exec:exec i:condition:bool o:true:exec o:false:exec" },
	{ "flow.sequence", "Sequence", "Flow", "Runs several exec outputs in order", 0, NULL },
	{ "flow.while", "While", "Flow", "Repeats its body while the condition is true", 0, "i:exec:exec i:condition:bool o:body:exec o:completed:exec" },
	{ "flow.for", "For", "Flow", "Counts from first to last", 0, "i:exec:exec i:first:int i:last:int o:body:exec o:index:int o:completed:exec" },
	{ "flow.for_each", "For Each", "Flow", "Visits every element of a list", 0, "i:exec:exec i:list:list o:body:exec o:element:any o:index:int o:completed:exec" },
	{ "flow.delay", "Delay", "Flow", "Waits a number of seconds of accumulated dt", 0, "i:exec:exec i:seconds:float o:then:exec" },
	{ "flow.wait_frames", "Wait Frames", "Flow", "Waits a number of update frames", 0, "i:exec:exec i:frames:int o:then:exec" },
	{ "math.add", "Add", "Math", "a + b", KIND_PURE, "i:a:float i:b:float o:result:float" },
	{ "math.sub", "Subtract", "Math", "a - b", KIND_PURE, "i:a:float i:b:float o:result:float" },
	{ "math.mul", "Multiply", "Math", "a * b", KIND_PURE, "i:a:float i:b:float o:result:float" },
	{ "math.div", "Divide", "Math", "a / b (0 when b is 0)", KIND_PURE, "i:a:float i:b:float o:result:float" },
	{ "math.mod", "Modulo", "Math", "a mod b (0 when b is 0)", KIND_PURE, "i:a:float i:b:float o:result:float" },
	{ "math.min", "Min", "Math", "The smaller of a and b", KIND_PURE, "i:a:float i:b:float o:result:float" },
	{ "math.max", "Max", "Math", "The larger of a and b", KIND_PURE, "i:a:float i:b:float o:result:float" },
	{ "math.pow", "Power", "Math", "a raised to b", KIND_PURE, "i:a:float i:b:float o:result:float" },
	{ "math.neg", "Negate", "Math", "-a", KIND_PURE, "i:a:float o:result:float" },
	{ "math.abs", "Absolute", "Math", "Absolute value of a", KIND_PURE, "i:a:float o:result:float" },
	{ "math.floor", "Floor", "Math", "Floor of a", KIND_PURE, "i:a:float o:result:float" },
	{ "math.ceil", "Ceil", "Math", "Ceiling of a", KIND_PURE, "i:a:float o:result:float" },
	{ "math.round", "Round", "Math", "Round half away from zero", KIND_PURE, "i:a:float o:result:float" },
	{ "math.sqrt", "Square Root", "Math", "Square root of a (0 when a < 0)", KIND_PURE, "i:a:float o:result:float" },
	{ "math.sin", "Sine", "Math", "Sine of a", KIND_PURE, "i:a:float o:result:float" },
	{ "math.cos", "Cosine", "Math", "Cosine of a", KIND_PURE, "i:a:float o:result:float" },
	{ "math.clamp", "Clamp", "Math", "Clamps a between min and max", KIND_PURE, "i:a:float i:min:float i:max:float o:result:float" },
	{ "math.lerp", "Lerp", "Math", "Linear blend of a and b by t", KIND_PURE, "i:a:float i:b:float i:t:float o:result:float" },
	{ "math.random", "Random", "Math", "A seeded value in [0, 1)", KIND_PURE, "o:result:float" },
	{ "math.random_range", "Random Range", "Math", "A seeded value in [min, max)", KIND_PURE, "i:min:float i:max:float o:result:float" },
	{ "math.random_int", "Random Int", "Math", "A seeded integer in [min, max]", KIND_PURE, "i:min:int i:max:int o:result:int" },
	{ "logic.and", "And", "Logic", "a and b", KIND_PURE, "i:a:bool i:b:bool o:result:bool" },
	{ "logic.or", "Or", "Logic", "a or b", KIND_PURE, "i:a:bool i:b:bool o:result:bool" },
	{ "logic.xor", "Xor", "Logic", "a xor b", KIND_PURE, "i:a:bool i:b:bool o:result:bool" },
	{ "logic.not", "Not", "Logic", "not a", KIND_PURE, "i:a:bool o:result:bool" },
	{ "compare.eq", "Equal", "Compare", "a == b", KIND_PURE, "i:a:any i:b:any o:result:bool" },
	{ "compare.ne", "Not Equal", "Compare", "a != b", KIND_PURE, "i:a:any i:b:any o:result:bool" },
	{ "compare.lt", "Less", "Compare", "a < b", KIND_PURE, "i:a:any i:b:any o:result:bool" },
	{ "compare.le", "Less Or Equal", "Compare", "a <= b", KIND_PURE, "i:a:any i:b:any o:result:bool" },
	{ "compare.gt", "Greater", "Compare", "a > b", KIND_PURE, "i:a:any i:b:any o:result:bool" },
	{ "compare.ge", "Greater Or Equal", "Compare", "a >= b", KIND_PURE, "i:a:any i:b:any o:result:bool" },
	{ "string.concat", "Concat", "String", "a followed by b", KIND_PURE, "i:a:string i:b:string o:result:string" },
	{ "string.from", "To String", "String", "Any value as text", KIND_PURE, "i:value:any o:result:string" },
	{ "string.length", "Length", "String", "UTF-8 character length", KIND_PURE, "i:text:string o:result:int" },
	{ "string.contains", "Contains", "String", "Whether text contains part", KIND_PURE, "i:text:string i:part:string o:result:bool" },
	{ "string.format", "Format", "String", "Replaces {name} placeholders", KIND_PURE, NULL },
	{ "debug.log", "Log", "Debug", "Appends a line to the script log", 0, "i:exec:exec i:text:any o:then:exec" },
	{ "gi.call", "Call", "GI", "Calls an introspected function", 0, NULL },
	{ "gi.get", "Get Property", "GI", "Reads a GObject property", KIND_PURE, "i:object:object o:value:any" },
	{ "gi.set", "Set Property", "GI", "Writes a GObject property", 0, "i:exec:exec i:object:object i:value:any o:then:exec" },
	{ "gi.connect", "Connect Signal", "GI", "Connects a GObject signal to a custom event", 0, "i:exec:exec i:object:object o:then:exec" },
	{ "gi.emit", "Emit Signal", "GI", "Emits a GObject signal", 0, NULL },
	{ "subgraph.input", "Subgraph Input", "Subgraph", "The entry of a subgraph", 0, NULL },
	{ "subgraph.output", "Subgraph Output", "Subgraph", "The exit of a subgraph", 0, NULL },
	{ "subgraph.call", "Call Subgraph", "Subgraph", "Calls a subgraph or a file", 0, NULL },
	{ "script.call", "Call Function", "Script", "Calls a function the host registered", 0, NULL }
};

static const KindInfo *
kind_lookup (const gchar *kind)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (KIND_TABLE); i++)
	{
		if (g_strcmp0 (KIND_TABLE[i].kind, kind) == 0)
			return &KIND_TABLE[i];
	}
	return NULL;
}

gchar **
lrg_vscript_node_types_list (void)
{
	GStrvBuilder *b = g_strv_builder_new ();
	gchar       **out;
	guint         i;

	for (i = 0; i < G_N_ELEMENTS (KIND_TABLE); i++)
		g_strv_builder_add (b, KIND_TABLE[i].kind);
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);
	return out;
}

gboolean
lrg_vscript_node_type_exists (const gchar *kind)
{
	return kind_lookup (kind) != NULL;
}

const gchar *
lrg_vscript_node_type_get_title (const gchar *kind)
{
	const KindInfo *k = kind_lookup (kind);

	return k != NULL ? k->title : NULL;
}

const gchar *
lrg_vscript_node_type_get_category (const gchar *kind)
{
	const KindInfo *k = kind_lookup (kind);

	return k != NULL ? k->category : NULL;
}

const gchar *
lrg_vscript_node_type_get_description (const gchar *kind)
{
	const KindInfo *k = kind_lookup (kind);

	return k != NULL ? k->description : NULL;
}

guint
lrg_vscript_priv_kind_flags (const gchar *kind)
{
	const KindInfo *k = kind_lookup (kind);

	return k != NULL ? k->flags : 0;
}

gboolean
lrg_vscript_node_type_is_event (const gchar *kind)
{
	return (lrg_vscript_priv_kind_flags (kind) & KIND_EVENT) != 0;
}

gboolean
lrg_vscript_node_type_is_pure (const gchar *kind)
{
	return (lrg_vscript_priv_kind_flags (kind) & KIND_PURE) != 0;
}

/* "i:exec:exec i:condition:bool o:true:exec" */
static void
add_spec_pins (GPtrArray   *out,
               const gchar *spec)
{
	gchar **parts;
	guint   i;

	if (spec == NULL)
		return;
	parts = g_strsplit (spec, " ", -1);
	for (i = 0; parts[i] != NULL; i++)
	{
		gchar                  **f;
		LrgVscriptPinDirection   dir;
		gboolean                 exec;

		if (parts[i][0] == '\0')
			continue;
		f = g_strsplit (parts[i], ":", 3);
		if (f[0] != NULL && f[1] != NULL && f[2] != NULL)
		{
			dir = f[0][0] == 'o' ? LRG_VSCRIPT_PIN_OUTPUT : LRG_VSCRIPT_PIN_INPUT;
			exec = g_strcmp0 (f[2], "exec") == 0;
			g_ptr_array_add (out, lrg_vscript_pin_new (f[1], dir, exec, f[2]));
		}
		g_strfreev (f);
	}
	g_strfreev (parts);
}

/* "name:type,other:type" — a missing type is "any" */
static void
add_arg_pins (GPtrArray              *out,
              const gchar            *spec,
              LrgVscriptPinDirection  dir)
{
	gchar **parts;
	guint   i;

	parts = g_strsplit (spec != NULL ? spec : "", ",", -1);
	for (i = 0; parts[i] != NULL; i++)
	{
		gchar *colon;
		gchar *name;
		const gchar *type;

		if (parts[i][0] == '\0')
			continue;
		colon = strchr (parts[i], ':');
		if (colon != NULL)
		{
			*colon = '\0';
			type = colon + 1;
		}
		else
			type = "any";
		name = g_strstrip (parts[i]);
		if (name[0] == '\0')
			continue;
		g_ptr_array_add (out, lrg_vscript_pin_new (name, dir, FALSE, type[0] != '\0' ? type : "any"));
	}
	g_strfreev (parts);
}

static void
add_exec_pair (GPtrArray *out,
               gboolean   with_in,
               gboolean   with_out)
{
	if (with_in)
		g_ptr_array_add (out, lrg_vscript_pin_new ("exec", LRG_VSCRIPT_PIN_INPUT, TRUE, "exec"));
	if (with_out)
		g_ptr_array_add (out, lrg_vscript_pin_new ("then", LRG_VSCRIPT_PIN_OUTPUT, TRUE, "exec"));
}

/* {name} placeholders, left to right, skipping "{{" */
static void
add_format_pins (GPtrArray   *out,
                 const gchar *format)
{
	const gchar *p;

	p = format != NULL ? format : "";
	while ((p = strchr (p, '{')) != NULL)
	{
		const gchar *end;
		gchar       *name;

		if (p[1] == '{')
		{
			p += 2;
			continue;
		}
		end = strchr (p + 1, '}');
		if (end == NULL)
			break;
		name = g_strndup (p + 1, (gsize) (end - (p + 1)));
		g_strstrip (name);
		if (name[0] != '\0')
			g_ptr_array_add (out, lrg_vscript_pin_new (name, LRG_VSCRIPT_PIN_INPUT, FALSE, "any"));
		g_free (name);
		p = end + 1;
	}
	g_ptr_array_add (out, lrg_vscript_pin_new ("result", LRG_VSCRIPT_PIN_OUTPUT, FALSE, "string"));
}

/* ==========================================================================
 * Values
 * ========================================================================== */

static void
gvalue_free (gpointer data)
{
	GValue *value = (GValue *) data;

	if (G_IS_VALUE (value))
		g_value_unset (value);
	g_free (value);
}

static gpointer
list_copy_boxed (gpointer boxed)
{
	GPtrArray *src = (GPtrArray *) boxed;
	GPtrArray *dst = g_ptr_array_new_with_free_func (gvalue_free);
	guint      i;

	for (i = 0; src != NULL && i < src->len; i++)
	{
		GValue *in = (GValue *) g_ptr_array_index (src, i);
		GValue *out = g_new0 (GValue, 1);

		if (G_IS_VALUE (in))
		{
			g_value_init (out, G_VALUE_TYPE (in));
			g_value_copy (in, out);
		}
		g_ptr_array_add (dst, out);
	}
	return dst;
}

static void
list_free_boxed (gpointer boxed)
{
	if (boxed != NULL)
		g_ptr_array_unref ((GPtrArray *) boxed);
}

GType
lrg_vscript_priv_list_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		GType id = g_boxed_type_register_static ("LrgVscriptList", list_copy_boxed, list_free_boxed);

		g_once_init_leave (&type_id, (gsize) id);
	}
	return (GType) type_id;
}

GPtrArray *
lrg_vscript_priv_list_new (void)
{
	return g_ptr_array_new_with_free_func (gvalue_free);
}

void
lrg_vscript_priv_list_append (GPtrArray    *list,
                              const GValue *value)
{
	GValue *copy;

	g_return_if_fail (list != NULL);

	copy = g_new0 (GValue, 1);
	if (value != NULL && G_IS_VALUE (value))
	{
		g_value_init (copy, G_VALUE_TYPE (value));
		g_value_copy (value, copy);
	}
	g_ptr_array_add (list, copy);
}

static gboolean
text_is_bool (const gchar *text,
              gboolean    *out)
{
	if (g_strcmp0 (text, "true") == 0 || g_strcmp0 (text, "True") == 0 || g_strcmp0 (text, "TRUE") == 0)
		*out = TRUE;
	else if (g_strcmp0 (text, "false") == 0 || g_strcmp0 (text, "False") == 0 || g_strcmp0 (text, "FALSE") == 0)
		*out = FALSE;
	else
		return FALSE;
	return TRUE;
}

static gboolean
text_is_int (const gchar *text,
             gint64      *out)
{
	gchar *end = NULL;
	gint64 i;

	if (text == NULL || text[0] == '\0')
		return FALSE;
	i = g_ascii_strtoll (text, &end, 10);
	if (end == text || *end != '\0')
		return FALSE;
	*out = i;
	return TRUE;
}

static gboolean
text_is_float (const gchar *text,
               gdouble     *out)
{
	gchar  *end = NULL;
	gdouble d;

	if (text == NULL || strchr (text, '.') == NULL)
		return FALSE;
	d = g_ascii_strtod (text, &end);
	if (end == text || *end != '\0')
		return FALSE;
	*out = d;
	return TRUE;
}

GVariant *
lrg_vscript_priv_parse_literal (const gchar *text)
{
	gboolean b;
	gint64   i;
	gdouble  d;

	if (text == NULL)
		return NULL;
	if (text_is_bool (text, &b))
		return g_variant_ref_sink (g_variant_new_boolean (b));
	if (text_is_int (text, &i))
		return g_variant_ref_sink (g_variant_new_int64 (i));
	if (text_is_float (text, &d))
		return g_variant_ref_sink (g_variant_new_double (d));
	return g_variant_ref_sink (g_variant_new_string (text));
}

void
lrg_vscript_priv_variant_to_value (GVariant *variant,
                                   GValue   *value)
{
	g_return_if_fail (value != NULL);

	if (G_IS_VALUE (value))
		g_value_unset (value);
	if (variant == NULL)
	{
		g_value_init (value, G_TYPE_INT64);
		g_value_set_int64 (value, 0);
		return;
	}
	if (g_variant_is_of_type (variant, G_VARIANT_TYPE_BOOLEAN))
	{
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, g_variant_get_boolean (variant));
	}
	else if (g_variant_is_of_type (variant, G_VARIANT_TYPE_INT64))
	{
		g_value_init (value, G_TYPE_INT64);
		g_value_set_int64 (value, g_variant_get_int64 (variant));
	}
	else if (g_variant_is_of_type (variant, G_VARIANT_TYPE_INT32))
	{
		g_value_init (value, G_TYPE_INT64);
		g_value_set_int64 (value, g_variant_get_int32 (variant));
	}
	else if (g_variant_is_of_type (variant, G_VARIANT_TYPE_UINT32))
	{
		g_value_init (value, G_TYPE_INT64);
		g_value_set_int64 (value, (gint64) g_variant_get_uint32 (variant));
	}
	else if (g_variant_is_of_type (variant, G_VARIANT_TYPE_UINT64))
	{
		g_value_init (value, G_TYPE_INT64);
		g_value_set_int64 (value, (gint64) g_variant_get_uint64 (variant));
	}
	else if (g_variant_is_of_type (variant, G_VARIANT_TYPE_DOUBLE))
	{
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, g_variant_get_double (variant));
	}
	else if (g_variant_is_of_type (variant, G_VARIANT_TYPE_STRING))
	{
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, g_variant_get_string (variant, NULL));
	}
	else
	{
		g_value_init (value, G_TYPE_INT64);
		g_value_set_int64 (value, 0);
	}
}

GVariant *
lrg_vscript_priv_value_to_variant (const GValue *value)
{
	if (value == NULL || !G_IS_VALUE (value))
		return NULL;
	if (G_VALUE_HOLDS_BOOLEAN (value))
		return g_variant_ref_sink (g_variant_new_boolean (g_value_get_boolean (value)));
	if (G_VALUE_HOLDS_INT64 (value))
		return g_variant_ref_sink (g_variant_new_int64 (g_value_get_int64 (value)));
	if (G_VALUE_HOLDS_INT (value))
		return g_variant_ref_sink (g_variant_new_int64 (g_value_get_int (value)));
	if (G_VALUE_HOLDS_DOUBLE (value))
		return g_variant_ref_sink (g_variant_new_double (g_value_get_double (value)));
	if (G_VALUE_HOLDS_FLOAT (value))
		return g_variant_ref_sink (g_variant_new_double (g_value_get_float (value)));
	if (G_VALUE_HOLDS_STRING (value))
		return g_variant_ref_sink (g_variant_new_string (g_value_get_string (value) != NULL ? g_value_get_string (value) : ""));
	return NULL;
}

gboolean
lrg_vscript_priv_value_is_number (const GValue *value)
{
	if (value == NULL || !G_IS_VALUE (value))
		return FALSE;
	return G_VALUE_HOLDS_INT (value) || G_VALUE_HOLDS_UINT (value) || G_VALUE_HOLDS_INT64 (value) ||
	       G_VALUE_HOLDS_UINT64 (value) || G_VALUE_HOLDS_LONG (value) || G_VALUE_HOLDS_FLOAT (value) ||
	       G_VALUE_HOLDS_DOUBLE (value);
}

gboolean
lrg_vscript_priv_value_to_bool (const GValue *value)
{
	const gchar *s;

	if (value == NULL || !G_IS_VALUE (value))
		return FALSE;
	if (G_VALUE_HOLDS_BOOLEAN (value))
		return g_value_get_boolean (value);
	if (lrg_vscript_priv_value_is_number (value))
		return lrg_vscript_priv_value_to_double (value) != 0.0;
	if (G_VALUE_HOLDS_STRING (value))
	{
		s = g_value_get_string (value);
		if (s == NULL || s[0] == '\0')
			return FALSE;
		if (g_ascii_strcasecmp (s, "false") == 0 || g_strcmp0 (s, "0") == 0)
			return FALSE;
		return TRUE;
	}
	return FALSE;
}

gint64
lrg_vscript_priv_value_to_int (const GValue *value)
{
	if (value == NULL || !G_IS_VALUE (value))
		return 0;
	if (G_VALUE_HOLDS_BOOLEAN (value))
		return g_value_get_boolean (value) ? 1 : 0;
	if (G_VALUE_HOLDS_INT64 (value))
		return g_value_get_int64 (value);
	if (G_VALUE_HOLDS_INT (value))
		return g_value_get_int (value);
	if (G_VALUE_HOLDS_UINT (value))
		return (gint64) g_value_get_uint (value);
	if (G_VALUE_HOLDS_UINT64 (value))
		return (gint64) g_value_get_uint64 (value);
	if (G_VALUE_HOLDS_DOUBLE (value))
		return (gint64) g_value_get_double (value);
	if (G_VALUE_HOLDS_FLOAT (value))
		return (gint64) g_value_get_float (value);
	if (G_VALUE_HOLDS_STRING (value))
		return g_ascii_strtoll (g_value_get_string (value) != NULL ? g_value_get_string (value) : "0", NULL, 10);
	return 0;
}

gdouble
lrg_vscript_priv_value_to_double (const GValue *value)
{
	if (value == NULL || !G_IS_VALUE (value))
		return 0.0;
	if (G_VALUE_HOLDS_DOUBLE (value))
		return g_value_get_double (value);
	if (G_VALUE_HOLDS_FLOAT (value))
		return g_value_get_float (value);
	if (G_VALUE_HOLDS_BOOLEAN (value))
		return g_value_get_boolean (value) ? 1.0 : 0.0;
	if (G_VALUE_HOLDS_STRING (value))
		return g_ascii_strtod (g_value_get_string (value) != NULL ? g_value_get_string (value) : "0", NULL);
	return (gdouble) lrg_vscript_priv_value_to_int (value);
}

gchar *
lrg_vscript_priv_value_to_string (const GValue *value)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	if (value == NULL || !G_IS_VALUE (value))
		return g_strdup ("");
	if (G_VALUE_HOLDS_STRING (value))
		return g_strdup (g_value_get_string (value) != NULL ? g_value_get_string (value) : "");
	if (G_VALUE_HOLDS_BOOLEAN (value))
		return g_strdup (g_value_get_boolean (value) ? "true" : "false");
	if (G_VALUE_HOLDS_DOUBLE (value) || G_VALUE_HOLDS_FLOAT (value))
	{
		g_ascii_dtostr (buf, sizeof buf, lrg_vscript_priv_value_to_double (value));
		return g_strdup (buf);
	}
	if (lrg_vscript_priv_value_is_number (value) || G_VALUE_HOLDS_BOOLEAN (value))
		return g_strdup_printf ("%" G_GINT64_FORMAT, lrg_vscript_priv_value_to_int (value));
	return g_strdup ("");
}

void
lrg_vscript_priv_zero_value (const gchar *type,
                             GValue      *value)
{
	g_return_if_fail (value != NULL);

	if (G_IS_VALUE (value))
		g_value_unset (value);
	if (g_strcmp0 (type, "bool") == 0)
	{
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, FALSE);
	}
	else if (g_strcmp0 (type, "float") == 0)
	{
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, 0.0);
	}
	else if (g_strcmp0 (type, "string") == 0)
	{
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, "");
	}
	else if (g_strcmp0 (type, "object") == 0)
	{
		g_value_init (value, G_TYPE_OBJECT);
		g_value_set_object (value, NULL);
	}
	else if (g_strcmp0 (type, "list") == 0)
	{
		g_value_init (value, lrg_vscript_priv_list_get_type ());
		g_value_take_boxed (value, lrg_vscript_priv_list_new ());
	}
	else
	{
		/* any, int, exec and unknown types */
		g_value_init (value, G_TYPE_INT64);
		g_value_set_int64 (value, 0);
	}
}

static gboolean
type_is_builtin (const gchar *type)
{
	return g_strcmp0 (type, "any") == 0 || g_strcmp0 (type, "bool") == 0 || g_strcmp0 (type, "int") == 0 ||
	       g_strcmp0 (type, "float") == 0 || g_strcmp0 (type, "string") == 0 || g_strcmp0 (type, "object") == 0 ||
	       g_strcmp0 (type, "list") == 0 || g_strcmp0 (type, "exec") == 0;
}

gboolean
lrg_vscript_priv_types_compatible (const gchar *from_type,
                                   const gchar *to_type)
{
	const gchar *from = from_type != NULL ? from_type : "any";
	const gchar *to = to_type != NULL ? to_type : "any";

	if (g_strcmp0 (from, "exec") == 0 || g_strcmp0 (to, "exec") == 0)
		return g_strcmp0 (from, "exec") == 0 && g_strcmp0 (to, "exec") == 0;
	if (g_strcmp0 (from, "any") == 0 || g_strcmp0 (to, "any") == 0)
		return TRUE;
	if (g_strcmp0 (from, "int") == 0 && g_strcmp0 (to, "float") == 0)
		return TRUE;
	if (g_strcmp0 (from, "list") == 0 || g_strcmp0 (to, "list") == 0)
		return g_strcmp0 (from, "list") == 0 && g_strcmp0 (to, "list") == 0;
	if (g_strcmp0 (from, "object") == 0 || g_strcmp0 (to, "object") == 0)
		return TRUE;
	if (!type_is_builtin (from) || !type_is_builtin (to))
		return g_strcmp0 (from, to) == 0 || g_strcmp0 (from, "object") == 0 || g_strcmp0 (to, "object") == 0;
	return g_strcmp0 (from, to) == 0;
}

/* ==========================================================================
 * GObject-Introspection (compiled out of the scanner and of non-GI builds)
 * ========================================================================== */

#ifdef VSCRIPT_HAVE_GI

static GIRepository *
vs_repo (void)
{
	static GIRepository *repo = NULL;
	static gsize         once = 0;

	if (g_once_init_enter (&once))
	{
		static const gchar *nss[] = { "GLib", "GObject", "Gio", "Libregnum", "Graylib", NULL };
		guint               i;

#if GLIB_CHECK_VERSION (2, 86, 0)
		repo = gi_repository_dup_default ();
#else
		repo = gi_repository_new ();
#endif
		for (i = 0; nss[i] != NULL; i++)
		{
			g_autoptr(GError) error = NULL;

			gi_repository_require (repo, nss[i], NULL, (GIRepositoryLoadFlags) 0, &error);
		}
		g_once_init_leave (&once, 1);
	}
	return repo;
}

static gboolean
query_words_match (const gchar *full,
                   gchar      **words)
{
	g_autofree gchar *lower = NULL;
	guint             i;

	lower = g_ascii_strdown (full, -1);
	for (i = 0; words != NULL && words[i] != NULL; i++)
	{
		g_autofree gchar *w = NULL;

		if (words[i][0] == '\0')
			continue;
		w = g_ascii_strdown (words[i], -1);
		if (g_strstr_len (lower, -1, w) == NULL)
			return FALSE;
	}
	return TRUE;
}

static void
consider_name (GPtrArray   *hits,
               GHashTable  *seen,
               const gchar *full,
               gchar      **words)
{
	if (full == NULL || g_hash_table_contains (seen, full))
		return;
	if (!query_words_match (full, words))
		return;
	g_hash_table_add (seen, g_strdup (full));
	g_ptr_array_add (hits, g_strdup (full));
}

static gint
name_cmp (gconstpointer a,
          gconstpointer b,
          gpointer      user_data)
{
	const gchar * const *sa = (const gchar * const *) a;
	const gchar * const *sb = (const gchar * const *) b;

	(void) user_data;
	return strcmp (*sa, *sb);
}

static gchar **
gi_search_impl (const gchar *query)
{
	GIRepository *repo;
	GHashTable   *seen;
	GPtrArray    *hits;
	gchar       **words;
	char        **nss;
	size_t        n_ns = 0;
	size_t        s;
	gchar       **out;

	repo = vs_repo ();
	seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	hits = g_ptr_array_new_with_free_func (g_free);
	words = g_strsplit (query != NULL ? query : "", " ", -1);
	nss = gi_repository_get_loaded_namespaces (repo, &n_ns);
	for (s = 0; nss != NULL && s < n_ns; s++)
	{
		unsigned int n_info;
		unsigned int k;

		n_info = gi_repository_get_n_infos (repo, nss[s]);
		for (k = 0; k < n_info; k++)
		{
			GIBaseInfo *info = gi_repository_get_info (repo, nss[s], k);

			if (info == NULL)
				continue;
			if (GI_IS_FUNCTION_INFO (info))
			{
				g_autofree gchar *full = g_strdup_printf ("%s.%s", nss[s], gi_base_info_get_name (info));

				consider_name (hits, seen, full, words);
			}
			else if (GI_IS_OBJECT_INFO (info))
			{
				unsigned int m;
				unsigned int nm = gi_object_info_get_n_methods ((GIObjectInfo *) info);

				for (m = 0; m < nm; m++)
				{
					GIFunctionInfo   *fn = gi_object_info_get_method ((GIObjectInfo *) info, m);
					g_autofree gchar *full = NULL;

					if (fn == NULL)
						continue;
					full = g_strdup_printf ("%s.%s.%s", nss[s], gi_base_info_get_name (info), gi_base_info_get_name ((GIBaseInfo *) fn));
					consider_name (hits, seen, full, words);
					gi_base_info_unref (fn);
				}
			}
			gi_base_info_unref (info);
		}
	}
	g_strfreev (nss);
	g_strfreev (words);
	g_hash_table_unref (seen);
	g_ptr_array_add (hits, NULL);
	out = (gchar **) g_ptr_array_free (hits, FALSE);
	return out;
}

static void
gi_search_more (GPtrArray  *hits,
                GHashTable *seen,
                gchar     **words)
{
	GIRepository *repo = vs_repo ();
	char         **nss;
	size_t         n_ns = 0;
	size_t         s;

	nss = gi_repository_get_loaded_namespaces (repo, &n_ns);
	for (s = 0; nss != NULL && s < n_ns; s++)
	{
		unsigned int n_info = gi_repository_get_n_infos (repo, nss[s]);
		unsigned int k;

		for (k = 0; k < n_info; k++)
		{
			GIBaseInfo *info = gi_repository_get_info (repo, nss[s], k);
			unsigned int m;
			unsigned int nm = 0;

			if (GI_IS_INTERFACE_INFO (info))
				nm = gi_interface_info_get_n_methods ((GIInterfaceInfo *) info);
			else if (GI_IS_STRUCT_INFO (info))
				nm = gi_struct_info_get_n_methods ((GIStructInfo *) info);
			for (m = 0; m < nm; m++)
			{
				GIFunctionInfo   *fn = NULL;
				g_autofree gchar *full = NULL;

				if (GI_IS_INTERFACE_INFO (info))
					fn = gi_interface_info_get_method ((GIInterfaceInfo *) info, m);
				else
					fn = gi_struct_info_get_method ((GIStructInfo *) info, m);
				if (fn == NULL)
					continue;
				full = g_strdup_printf ("%s.%s.%s", nss[s], gi_base_info_get_name (info),
				                        gi_base_info_get_name ((GIBaseInfo *) fn));
				consider_name (hits, seen, full, words);
				gi_base_info_unref (fn);
			}
			if (info != NULL)
				gi_base_info_unref (info);
		}
	}
	g_strfreev (nss);
}

static GIBaseInfo *
gi_find_callable (const gchar *function,
                  GError     **error)
{
	GIRepository *repo;
	gchar       **parts;
	GIBaseInfo   *info = NULL;
	guint         n;

	repo = vs_repo ();
	parts = g_strsplit (function != NULL ? function : "", ".", -1);
	n = g_strv_length (parts);
	if (n == 2)
		info = gi_repository_find_by_name (repo, parts[0], parts[1]);
	else if (n == 3)
	{
		GIBaseInfo *type = gi_repository_find_by_name (repo, parts[0], parts[1]);

		if (GI_IS_OBJECT_INFO (type))
			info = (GIBaseInfo *) gi_object_info_find_method ((GIObjectInfo *) type, parts[2]);
		else if (GI_IS_INTERFACE_INFO (type))
			info = (GIBaseInfo *) gi_interface_info_find_method ((GIInterfaceInfo *) type, parts[2]);
		else if (GI_IS_STRUCT_INFO (type))
			info = (GIBaseInfo *) gi_struct_info_find_method ((GIStructInfo *) type, parts[2]);
		if (type != NULL)
			gi_base_info_unref (type);
	}
	g_strfreev (parts);
	if (!GI_IS_CALLABLE_INFO (info))
	{
		if (info != NULL)
			gi_base_info_unref (info);
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_NOT_FOUND, "no GI callable '%s'", function != NULL ? function : "");
		return NULL;
	}
	return info;
}

static const gchar *
tag_pin_type (GITypeTag   tag,
              GIBaseInfo *iface)
{
	if (tag == GI_TYPE_TAG_BOOLEAN)
		return "bool";
	if (tag == GI_TYPE_TAG_FLOAT || tag == GI_TYPE_TAG_DOUBLE)
		return "float";
	if (tag == GI_TYPE_TAG_UTF8 || tag == GI_TYPE_TAG_FILENAME)
		return "string";
	if (tag == GI_TYPE_TAG_INTERFACE && iface != NULL && (GI_IS_ENUM_INFO (iface) || GI_IS_FLAGS_INFO (iface)))
		return "int";
	if (tag == GI_TYPE_TAG_INTERFACE)
		return "object";
	if (tag == GI_TYPE_TAG_VOID)
		return NULL;
	if (tag == GI_TYPE_TAG_INT8 || tag == GI_TYPE_TAG_UINT8 || tag == GI_TYPE_TAG_INT16 || tag == GI_TYPE_TAG_UINT16 ||
	    tag == GI_TYPE_TAG_INT32 || tag == GI_TYPE_TAG_UINT32 || tag == GI_TYPE_TAG_INT64 || tag == GI_TYPE_TAG_UINT64)
		return "int";
	return NULL;
}

static gboolean
tag_supported (GITypeTag tag)
{
	return tag_pin_type (tag, NULL) != NULL || tag == GI_TYPE_TAG_VOID || tag == GI_TYPE_TAG_INTERFACE;
}

static gboolean
arg_to_gi (const GValue *value,
           GITypeInfo   *ti,
           GIArgument   *out,
           GError      **error)
{
	GITypeTag   tag = gi_type_info_get_tag (ti);
	GIBaseInfo *iface = NULL;

	memset (out, 0, sizeof *out);
	if (tag == GI_TYPE_TAG_INTERFACE)
		iface = gi_type_info_get_interface (ti);
	if (tag == GI_TYPE_TAG_INTERFACE && iface != NULL && (GI_IS_ENUM_INFO (iface) || GI_IS_FLAGS_INFO (iface)))
		tag = GI_TYPE_TAG_INT32;
	if (iface != NULL)
		gi_base_info_unref (iface);
	if (!tag_supported (tag) || tag == GI_TYPE_TAG_VOID)
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_UNSUPPORTED, "GI argument type is not supported");
		return FALSE;
	}
	if (tag == GI_TYPE_TAG_BOOLEAN)
		out->v_boolean = lrg_vscript_priv_value_to_bool (value);
	else if (tag == GI_TYPE_TAG_FLOAT)
		out->v_float = (gfloat) lrg_vscript_priv_value_to_double (value);
	else if (tag == GI_TYPE_TAG_DOUBLE)
		out->v_double = lrg_vscript_priv_value_to_double (value);
	else if (tag == GI_TYPE_TAG_UTF8 || tag == GI_TYPE_TAG_FILENAME)
		out->v_string = (char *) (value != NULL && G_VALUE_HOLDS_STRING (value) ? g_value_get_string (value) : "");
	else if (tag == GI_TYPE_TAG_INTERFACE)
		out->v_pointer = value != NULL && G_VALUE_HOLDS_OBJECT (value) ? g_value_get_object (value) : NULL;
	else if (tag == GI_TYPE_TAG_INT64 || tag == GI_TYPE_TAG_UINT64)
		out->v_int64 = lrg_vscript_priv_value_to_int (value);
	else
		out->v_int32 = (gint32) lrg_vscript_priv_value_to_int (value);
	return TRUE;
}

static void
gi_to_value (GITypeTag    tag,
             GIArgument  *arg,
             GITransfer   transfer,
             GValue      *out)
{
	if (G_IS_VALUE (out))
		g_value_unset (out);
	if (tag == GI_TYPE_TAG_BOOLEAN)
	{
		g_value_init (out, G_TYPE_BOOLEAN);
		g_value_set_boolean (out, arg->v_boolean);
	}
	else if (tag == GI_TYPE_TAG_FLOAT)
	{
		g_value_init (out, G_TYPE_DOUBLE);
		g_value_set_double (out, arg->v_float);
	}
	else if (tag == GI_TYPE_TAG_DOUBLE)
	{
		g_value_init (out, G_TYPE_DOUBLE);
		g_value_set_double (out, arg->v_double);
	}
	else if (tag == GI_TYPE_TAG_UTF8 || tag == GI_TYPE_TAG_FILENAME)
	{
		g_value_init (out, G_TYPE_STRING);
		g_value_set_string (out, arg->v_string != NULL ? arg->v_string : "");
		if ((transfer == GI_TRANSFER_EVERYTHING || transfer == GI_TRANSFER_CONTAINER) && arg->v_string != NULL)
			g_free (arg->v_string);
	}
	else if (tag == GI_TYPE_TAG_INTERFACE)
	{
		g_value_init (out, G_TYPE_OBJECT);
		g_value_set_object (out, arg->v_pointer);
	}
	else if (tag == GI_TYPE_TAG_INT64 || tag == GI_TYPE_TAG_UINT64)
	{
		g_value_init (out, G_TYPE_INT64);
		g_value_set_int64 (out, arg->v_int64);
	}
	else if (tag == GI_TYPE_TAG_VOID)
	{
		g_value_init (out, G_TYPE_INT64);
		g_value_set_int64 (out, 0);
	}
	else
	{
		g_value_init (out, G_TYPE_INT64);
		g_value_set_int64 (out, arg->v_int32);
	}
}

static void
gi_add_signature_pins (GPtrArray      *out,
                       GICallableInfo *info,
                       gboolean        pure)
{
	GITypeInfo *ret;
	GITypeTag   rtag;
	gboolean    method;
	gboolean    ctor;
	unsigned int i;
	unsigned int n;

	ctor = GI_IS_FUNCTION_INFO (info) && (gi_function_info_get_flags ((GIFunctionInfo *) info) & GI_FUNCTION_IS_CONSTRUCTOR) != 0;
	method = gi_callable_info_is_method (info) && !ctor;
	if (!pure)
		g_ptr_array_add (out, lrg_vscript_pin_new ("exec", LRG_VSCRIPT_PIN_INPUT, TRUE, "exec"));
	if (method)
		g_ptr_array_add (out, lrg_vscript_pin_new ("object", LRG_VSCRIPT_PIN_INPUT, FALSE, "object"));
	n = gi_callable_info_get_n_args (info);
	for (i = 0; i < n; i++)
	{
		GIArgInfo *arg = gi_callable_info_get_arg (info, i);
		GITypeInfo *ti;
		const gchar *ptype;

		if (arg == NULL)
			continue;
		if (gi_arg_info_get_direction (arg) != GI_DIRECTION_IN)
		{
			gi_base_info_unref (arg);
			continue;
		}
		ti = gi_arg_info_get_type_info (arg);
		ptype = tag_pin_type (gi_type_info_get_tag (ti), NULL);
		if (gi_type_info_get_tag (ti) == GI_TYPE_TAG_INTERFACE)
		{
			GIBaseInfo *iface = gi_type_info_get_interface (ti);

			ptype = tag_pin_type (GI_TYPE_TAG_INTERFACE, iface);
			if (iface != NULL)
				gi_base_info_unref (iface);
		}
		g_ptr_array_add (out, lrg_vscript_pin_new (gi_base_info_get_name ((GIBaseInfo *) arg), LRG_VSCRIPT_PIN_INPUT, FALSE,
		                                          ptype != NULL ? ptype : "any"));
		gi_base_info_unref (ti);
		gi_base_info_unref (arg);
	}
	ret = gi_callable_info_get_return_type (info);
	rtag = gi_type_info_get_tag (ret);
	if (rtag != GI_TYPE_TAG_VOID || gi_type_info_is_pointer (ret))
	{
		const gchar *ptype = tag_pin_type (rtag, NULL);

		if (rtag == GI_TYPE_TAG_INTERFACE)
		{
			GIBaseInfo *iface = gi_type_info_get_interface (ret);

			ptype = tag_pin_type (rtag, iface);
			if (iface != NULL)
				gi_base_info_unref (iface);
		}
		g_ptr_array_add (out, lrg_vscript_pin_new ("result", LRG_VSCRIPT_PIN_OUTPUT, FALSE, ptype != NULL ? ptype : "any"));
	}
	gi_base_info_unref (ret);
	if (!pure)
		g_ptr_array_add (out, lrg_vscript_pin_new ("then", LRG_VSCRIPT_PIN_OUTPUT, TRUE, "exec"));
}

#endif /* VSCRIPT_HAVE_GI */

gboolean
lrg_vscript_gi_available (void)
{
#ifdef VSCRIPT_HAVE_GI
	return TRUE;
#else
	return FALSE;
#endif
}

gchar **
lrg_vscript_gi_search (const gchar *query,
                       guint        limit)
{
#ifdef VSCRIPT_HAVE_GI
	GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	GPtrArray  *hits;
	gchar     **words;
	gchar     **out;
	gchar     **first;

	/* object methods and free functions, then interfaces and structs */
	first = gi_search_impl (query);
	hits = g_ptr_array_new_with_free_func (g_free);
	words = g_strsplit (query != NULL ? query : "", " ", -1);
	{
		guint i;

		for (i = 0; first[i] != NULL; i++)
			consider_name (hits, seen, first[i], words);
	}
	gi_search_more (hits, seen, words);
	g_strfreev (words);
	g_strfreev (first);
	g_hash_table_unref (seen);
	if (limit == 0)
		limit = 100;
	if (hits->len > 1)
		g_sort_array (hits->pdata, hits->len, sizeof (gpointer), name_cmp, NULL);
	if (hits->len > limit)
		g_ptr_array_remove_range (hits, limit, hits->len - limit);
	g_ptr_array_add (hits, NULL);
	out = (gchar **) g_ptr_array_free (hits, FALSE);
	return out;
#else
	(void) query;
	(void) limit;
	return g_new0 (gchar *, 1);
#endif
}

gboolean
lrg_vscript_priv_gi_invoke (const gchar   *function,
                            const GValue  *in_values,
                            guint          n_in,
                            GArray        *out_values,
                            GError       **error)
{
#ifdef VSCRIPT_HAVE_GI
	GIBaseInfo     *info;
	GICallableInfo *callable;
	GIArgument      in_args[64];
	GIArgument      ret_arg;
	GITypeInfo     *ret_ti;
	gboolean        method;
	gboolean        ctor;
	unsigned int    n_args;
	unsigned int    i;
	guint           in_i;
	gboolean        ok;

	g_return_val_if_fail (out_values != NULL, FALSE);

	info = gi_find_callable (function, error);
	if (info == NULL)
		return FALSE;
	callable = (GICallableInfo *) info;
	ctor = GI_IS_FUNCTION_INFO (info) && (gi_function_info_get_flags ((GIFunctionInfo *) info) & GI_FUNCTION_IS_CONSTRUCTOR) != 0;
	method = gi_callable_info_is_method (callable) && !ctor;
	memset (in_args, 0, sizeof in_args);
	memset (&ret_arg, 0, sizeof ret_arg);
	in_i = 0;
	if (method)
	{
		if (n_in < 1 || in_i >= 64)
		{
			gi_base_info_unref (info);
			g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_INVALID, "gi call '%s' needs an instance", function);
			return FALSE;
		}
		in_args[in_i].v_pointer = G_VALUE_HOLDS_OBJECT (&in_values[0]) ? g_value_get_object (&in_values[0]) : NULL;
		in_i++;
	}
	n_args = gi_callable_info_get_n_args (callable);
	for (i = 0; i < n_args; i++)
	{
		GIArgInfo  *arg = gi_callable_info_get_arg (callable, i);
		GITypeInfo *ti;
		guint       src;

		if (gi_arg_info_get_direction (arg) != GI_DIRECTION_IN)
		{
			gi_base_info_unref (arg);
			gi_base_info_unref (info);
			g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_UNSUPPORTED, "GI out arguments are not supported");
			return FALSE;
		}
		ti = gi_arg_info_get_type_info (arg);
		src = method ? in_i : in_i;
		if (src >= n_in || in_i >= 64)
		{
			gi_base_info_unref (ti);
			gi_base_info_unref (arg);
			gi_base_info_unref (info);
			g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_INVALID, "gi call '%s' is missing an argument", function);
			return FALSE;
		}
		if (!arg_to_gi (&in_values[src], ti, &in_args[in_i], error))
		{
			gi_base_info_unref (ti);
			gi_base_info_unref (arg);
			gi_base_info_unref (info);
			return FALSE;
		}
		in_i++;
		gi_base_info_unref (ti);
		gi_base_info_unref (arg);
	}
	ret_ti = gi_callable_info_get_return_type (callable);
	ok = gi_callable_info_invoke (callable, NULL, in_args, in_i, NULL, 0, &ret_arg, error);
	if (!ok && error != NULL && *error != NULL && (*error)->domain != DOMAIN)
	{
		GError *gi_err = g_steal_pointer (error);

		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_RUNTIME, "%s", gi_err->message);
		g_error_free (gi_err);
	}
	else if (!ok && (error == NULL || *error == NULL))
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_RUNTIME, "GI call '%s' failed", function != NULL ? function : "");
	if (ok)
	{
		GITypeTag tag = gi_type_info_get_tag (ret_ti);
		GValue    boxed = G_VALUE_INIT;

		if (tag == GI_TYPE_TAG_INTERFACE)
		{
			GIBaseInfo *iface = gi_type_info_get_interface (ret_ti);

			if (iface != NULL && (GI_IS_ENUM_INFO (iface) || GI_IS_FLAGS_INFO (iface)))
				tag = GI_TYPE_TAG_INT32;
			if (iface != NULL)
				gi_base_info_unref (iface);
		}
		if (tag != GI_TYPE_TAG_VOID || gi_type_info_is_pointer (ret_ti))
		{
			gi_to_value (tag, &ret_arg, gi_callable_info_get_caller_owns (callable), &boxed);
			g_array_append_val (out_values, boxed);
		}
	}
	gi_base_info_unref (ret_ti);
	gi_base_info_unref (info);
	return ok;
#else
	(void) function;
	(void) in_values;
	(void) n_in;
	(void) out_values;
	g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_UNSUPPORTED, "GObject-Introspection is not available");
	return FALSE;
#endif
}



/* ==========================================================================
 * LrgVscriptProgram
 * ========================================================================== */

static gboolean
vs_fail (GError      **error,
         gint          code,
         const gchar  *path,
         const gchar  *fmt,
         ...)
{
	g_autofree gchar *msg = NULL;
	va_list           ap;

	va_start (ap, fmt);
	msg = g_strdup_vprintf (fmt, ap);
	va_end (ap);
	if (path != NULL && path[0] != '\0')
		g_set_error (error, DOMAIN, code, "%s: %s", path, msg);
	else
		g_set_error_literal (error, DOMAIN, code, msg);
	return FALSE;
}

static YamlMapping *
vs_map (YamlNode     *node,
	const gchar  *path,
	GError      **error)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, path, "expected a mapping");
		return NULL;
	}
	return yaml_node_get_mapping (node);
}

static YamlSequence *
vs_seq (YamlNode     *node,
	const gchar  *path,
	GError      **error)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
	{
		vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, path, "expected a sequence");
		return NULL;
	}
	return yaml_node_get_sequence (node);
}

static gboolean
vs_string (YamlMapping  *map,
           const gchar  *path,
           const gchar  *key,
           gboolean      required,
           const gchar **out,
           GError      **error)
{
	g_autofree gchar *p = lrg_ai_priv_sub_path (path, key);
	YamlNode         *n = yaml_mapping_get_member (map, key);

	*out = NULL;
	if (n == NULL)
	{
		if (required)
			return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, path, "missing '%s'", key);
		return TRUE;
	}
	if (yaml_node_get_node_type (n) != YAML_NODE_SCALAR || yaml_node_get_scalar (n) == NULL)
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, p, "expected a scalar");
	*out = yaml_node_get_scalar (n);
	return TRUE;
}

static gboolean
vs_number (YamlMapping  *map,
           const gchar  *path,
           const gchar  *key,
           gdouble      *out,
           gboolean     *present,
           GError      **error)
{
	g_autofree gchar *p = lrg_ai_priv_sub_path (path, key);
	const gchar      *t = NULL;
	gchar            *end = NULL;
	gdouble           v;

	if (present != NULL)
		*present = FALSE;
	if (!vs_string (map, path, key, FALSE, &t, error))
		return FALSE;
	if (t == NULL)
		return TRUE;
	if (t[0] == '\0' || strchr (t, 'x') != NULL || strchr (t, 'X') != NULL)
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, p, "expected a number");
	v = g_ascii_strtod (t, &end);
	if (end == t || *end != '\0' || !isfinite (v))
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, p, "expected a number");
	*out = v;
	if (present != NULL)
		*present = TRUE;
	return TRUE;
}

static gboolean
vs_int (YamlMapping  *map,
	const gchar  *path,
	const gchar  *key,
	gint64       *out,
	gboolean     *present,
	GError      **error)
{
	g_autofree gchar *p = lrg_ai_priv_sub_path (path, key);
	const gchar      *t = NULL;
	const gchar      *s;
	gchar            *end = NULL;

	if (present != NULL)
		*present = FALSE;
	if (!vs_string (map, path, key, FALSE, &t, error))
		return FALSE;
	if (t == NULL)
		return TRUE;
	s = t;
	if (*s == '+' || *s == '-')
		s++;
	if (*s == '\0')
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, p, "expected an integer");
	for (; *s != '\0'; s++)
	{
		if (!g_ascii_isdigit (*s))
			return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, p, "expected an integer");
	}
	*out = g_ascii_strtoll (t, &end, 10);
	if (end == t || *end != '\0')
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, p, "expected an integer");
	if (present != NULL)
		*present = TRUE;
	return TRUE;
}

static gboolean
split_pin_addr (const gchar  *text,
		const gchar  *path,
		gchar       **node,
		gchar       **pin,
		GError      **error)
{
	const gchar *dot;

	*node = NULL;
	*pin = NULL;
	dot = text != NULL ? strchr (text, '.') : NULL;
	if (dot == NULL || dot == text || dot[1] == '\0')
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, path, "expected node.pin");
	*node = g_strndup (text, (gsize) (dot - text));
	*pin = g_strdup (dot + 1);
	return TRUE;
}

/* --- create_node (catalogue kinds only) -------------------------------- */

LrgVscriptNode *
lrg_vscript_graph_create_node (LrgVscriptGraph  *self,
                               const gchar      *kind,
                               const gchar      *id,
                               gdouble           x,
                               gdouble           y,
                               GError          **error)
{
	g_autofree gchar *owned = NULL;
	const gchar      *use;
	LrgVscriptNode   *node;

	g_return_val_if_fail (LRG_IS_VSCRIPT_GRAPH (self), NULL);
	g_return_val_if_fail (kind != NULL, NULL);

	if (!lrg_vscript_node_type_exists (kind))
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_INVALID, "unknown node kind '%s'", kind);
		return NULL;
	}
	if (id == NULL)
	{
		owned = lrg_vscript_graph_dup_free_id (self, kind);
		use = owned;
	}
	else
		use = id;
	node = lrg_vscript_node_new (use, kind);
	lrg_vscript_node_set_position (node, x, y);
	if (!lrg_vscript_graph_add_node (self, node, error))
	{
		g_object_unref (node);
		return NULL;
	}
	g_object_unref (node);
	return lrg_vscript_graph_get_node (self, use);
}

/* --- variables ----------------------------------------------------------- */

typedef struct
{
	gchar       *name;
	gchar       *type;
	GVariant    *def;
	YamlMapping *extra;
} Variable;

static void
variable_free (gpointer data)
{
	Variable *v = (Variable *) data;

	if (v == NULL)
		return;
	g_free (v->name);
	g_free (v->type);
	if (v->def != NULL)
		g_variant_unref (v->def);
	g_clear_pointer (&v->extra, yaml_mapping_unref);
	g_free (v);
}

struct _LrgVscriptProgram
{
	GObject          parent_instance;

	gchar           *name;
	gchar           *description;
	LrgVscriptView   view;
	guint32          seed;
	guint            step_budget;
	gdouble          fixed_step;
	gchar           *base_dir;
	LrgVscriptGraph *main_graph;
	GPtrArray       *subgraphs;   /* LrgVscriptGraph */
	GPtrArray       *variables;   /* Variable */
	YamlMapping     *extra;
};

G_DEFINE_FINAL_TYPE (LrgVscriptProgram, lrg_vscript_program, G_TYPE_OBJECT)

static void
lrg_vscript_program_finalize (GObject *object)
{
	LrgVscriptProgram *self = LRG_VSCRIPT_PROGRAM (object);

	g_free (self->name);
	g_free (self->description);
	g_free (self->base_dir);
	g_clear_object (&self->main_graph);
	g_ptr_array_unref (self->subgraphs);
	g_ptr_array_unref (self->variables);
	g_clear_pointer (&self->extra, yaml_mapping_unref);
	G_OBJECT_CLASS (lrg_vscript_program_parent_class)->finalize (object);
}

static void
lrg_vscript_program_class_init (LrgVscriptProgramClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_vscript_program_finalize;
}

static void
lrg_vscript_program_init (LrgVscriptProgram *self)
{
	self->view = LRG_VSCRIPT_VIEW_GRAPH;
	self->step_budget = LRG_VSCRIPT_DEFAULT_STEP_BUDGET;
	self->fixed_step = DEFAULT_FIXED_STEP;
	self->main_graph = graph_new ("");
	self->subgraphs = g_ptr_array_new_with_free_func (g_object_unref);
	self->variables = g_ptr_array_new_with_free_func (variable_free);
}

static Variable *
variable_find (LrgVscriptProgram *self,
               const gchar       *name,
               guint             *index)
{
	guint i;

	for (i = 0; i < self->variables->len; i++)
	{
		Variable *v = (Variable *) g_ptr_array_index (self->variables, i);

		if (g_strcmp0 (v->name, name) == 0)
		{
			if (index != NULL)
				*index = i;
			return v;
		}
	}
	return NULL;
}

LrgVscriptProgram *
lrg_vscript_program_new (void)
{
	return g_object_new (LRG_TYPE_VSCRIPT_PROGRAM, NULL);
}

const gchar *
lrg_vscript_program_get_name (LrgVscriptProgram *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), NULL);
	return self->name;
}

void
lrg_vscript_program_set_name (LrgVscriptProgram *self,
                              const gchar       *name)
{
	g_return_if_fail (LRG_IS_VSCRIPT_PROGRAM (self));
	g_free (self->name);
	self->name = (name != NULL && name[0] != '\0') ? g_strdup (name) : NULL;
}

const gchar *
lrg_vscript_program_get_description (LrgVscriptProgram *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), NULL);
	return self->description;
}

void
lrg_vscript_program_set_description (LrgVscriptProgram *self,
                                     const gchar       *description)
{
	g_return_if_fail (LRG_IS_VSCRIPT_PROGRAM (self));
	g_free (self->description);
	self->description = (description != NULL && description[0] != '\0') ? g_strdup (description) : NULL;
}

LrgVscriptView
lrg_vscript_program_get_view (LrgVscriptProgram *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), LRG_VSCRIPT_VIEW_GRAPH);
	return self->view;
}

void
lrg_vscript_program_set_view (LrgVscriptProgram *self,
                              LrgVscriptView     view)
{
	g_return_if_fail (LRG_IS_VSCRIPT_PROGRAM (self));
	if (view != LRG_VSCRIPT_VIEW_GRAPH && view != LRG_VSCRIPT_VIEW_BLOCKS)
		return;
	self->view = view;
}

guint32
lrg_vscript_program_get_seed (LrgVscriptProgram *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), 0);
	return self->seed;
}

void
lrg_vscript_program_set_seed (LrgVscriptProgram *self,
                              guint32            seed)
{
	g_return_if_fail (LRG_IS_VSCRIPT_PROGRAM (self));
	self->seed = seed;
}

guint
lrg_vscript_program_get_step_budget (LrgVscriptProgram *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), LRG_VSCRIPT_DEFAULT_STEP_BUDGET);
	return self->step_budget;
}

void
lrg_vscript_program_set_step_budget (LrgVscriptProgram *self,
                                     guint              budget)
{
	g_return_if_fail (LRG_IS_VSCRIPT_PROGRAM (self));
	self->step_budget = budget > 0 ? budget : LRG_VSCRIPT_DEFAULT_STEP_BUDGET;
}

gdouble
lrg_vscript_program_get_fixed_step (LrgVscriptProgram *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), DEFAULT_FIXED_STEP);
	return self->fixed_step;
}

void
lrg_vscript_program_set_fixed_step (LrgVscriptProgram *self,
                                    gdouble            seconds)
{
	g_return_if_fail (LRG_IS_VSCRIPT_PROGRAM (self));
	if (!(seconds > 0.0))
		return;
	self->fixed_step = seconds;
}

const gchar *
lrg_vscript_program_get_base_dir (LrgVscriptProgram *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), NULL);
	return self->base_dir;
}

void
lrg_vscript_program_set_base_dir (LrgVscriptProgram *self,
                                  const gchar       *dir)
{
	g_return_if_fail (LRG_IS_VSCRIPT_PROGRAM (self));
	g_free (self->base_dir);
	self->base_dir = (dir != NULL && dir[0] != '\0') ? g_strdup (dir) : NULL;
}

LrgVscriptGraph *
lrg_vscript_program_get_main (LrgVscriptProgram *self)
{
	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), NULL);
	return self->main_graph;
}

LrgVscriptGraph *
lrg_vscript_program_get_graph (LrgVscriptProgram *self,
                               const gchar       *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), NULL);
	if (name == NULL || name[0] == '\0')
		return self->main_graph;
	for (i = 0; i < self->subgraphs->len; i++)
	{
		LrgVscriptGraph *g = (LrgVscriptGraph *) g_ptr_array_index (self->subgraphs, i);

		if (g_strcmp0 (g->name, name) == 0)
			return g;
	}
	return NULL;
}

LrgVscriptGraph *
lrg_vscript_program_add_subgraph (LrgVscriptProgram  *self,
                                  const gchar        *name,
                                  GError            **error)
{
	LrgVscriptGraph *g;

	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), NULL);

	if (!id_is_valid (name))
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_INVALID,
		             "'%s' is not a valid subgraph name", name != NULL ? name : "");
		return NULL;
	}
	if (lrg_vscript_program_get_graph (self, name) != NULL)
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_INVALID, "a subgraph '%s' already exists", name);
		return NULL;
	}
	g = graph_new (name);
	if (lrg_vscript_graph_create_node (g, "subgraph.input", "input", 0.0, 0.0, error) == NULL ||
	    lrg_vscript_graph_create_node (g, "subgraph.output", "output", 220.0, 0.0, error) == NULL)
	{
		g_object_unref (g);
		return NULL;
	}
	g_ptr_array_add (self->subgraphs, g);
	return g;
}

gboolean
lrg_vscript_program_remove_subgraph (LrgVscriptProgram *self,
                                     const gchar       *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), FALSE);

	for (i = 0; i < self->subgraphs->len; i++)
	{
		LrgVscriptGraph *g = (LrgVscriptGraph *) g_ptr_array_index (self->subgraphs, i);

		if (g_strcmp0 (g->name, name) == 0)
		{
			g_ptr_array_remove_index (self->subgraphs, i);
			return TRUE;
		}
	}
	return FALSE;
}

gchar **
lrg_vscript_program_dup_subgraph_names (LrgVscriptProgram *self)
{
	GStrvBuilder *b;
	gchar       **out;
	guint         i;

	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), NULL);

	b = g_strv_builder_new ();
	for (i = 0; i < self->subgraphs->len; i++)
	{
		LrgVscriptGraph *g = (LrgVscriptGraph *) g_ptr_array_index (self->subgraphs, i);

		g_strv_builder_add (b, g->name);
	}
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);
	return out;
}

void
lrg_vscript_program_set_variable (LrgVscriptProgram *self,
                                  const gchar       *name,
                                  const gchar       *type,
                                  GVariant          *default_value)
{
	Variable *v;

	g_return_if_fail (LRG_IS_VSCRIPT_PROGRAM (self));
	g_return_if_fail (name != NULL && name[0] != '\0');

	if (type == NULL || type[0] == '\0')
		type = "any";
	if (default_value != NULL)
		g_variant_ref_sink (default_value);
	v = variable_find (self, name, NULL);
	if (v == NULL)
	{
		v = g_new0 (Variable, 1);
		v->name = g_strdup (name);
		g_ptr_array_add (self->variables, v);
	}
	else if (v->def != NULL)
		g_variant_unref (v->def);
	g_free (v->type);
	v->type = g_strdup (type);
	v->def = default_value;
}

gboolean
lrg_vscript_program_remove_variable (LrgVscriptProgram *self,
                                     const gchar       *name)
{
	guint index;

	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), FALSE);

	if (variable_find (self, name, &index) == NULL)
		return FALSE;
	g_ptr_array_remove_index (self->variables, index);
	return TRUE;
}

gchar **
lrg_vscript_program_dup_variable_names (LrgVscriptProgram *self)
{
	GStrvBuilder *b;
	gchar       **out;
	guint         i;

	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), NULL);

	b = g_strv_builder_new ();
	for (i = 0; i < self->variables->len; i++)
		g_strv_builder_add (b, ((Variable *) g_ptr_array_index (self->variables, i))->name);
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);
	return out;
}

const gchar *
lrg_vscript_program_get_variable_type (LrgVscriptProgram *self,
                                       const gchar       *name)
{
	Variable *v;

	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), NULL);

	v = variable_find (self, name, NULL);
	return v != NULL ? v->type : NULL;
}

GVariant *
lrg_vscript_program_get_variable_default (LrgVscriptProgram *self,
                                          const gchar       *name)
{
	Variable *v;

	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), NULL);

	v = variable_find (self, name, NULL);
	return v != NULL ? v->def : NULL;
}

/* --- writer -------------------------------------------------------------- */

static void
put_text (YamlMapping *map,
          const gchar *key,
          const gchar *text)
{
	if (text != NULL && text[0] != '\0')
		lrg_ai_priv_put (map, key, lrg_ai_priv_variant_to_node (g_variant_new_string (text)));
}

static YamlNode *
write_dict (GPtrArray *dict)
{
	YamlNode    *node;
	YamlMapping *map;
	guint        i;

	if (dict == NULL || dict->len == 0)
		return NULL;
	node = yaml_node_new_mapping (NULL);
	map = yaml_node_get_mapping (node);
	for (i = 0; i < dict->len; i++)
	{
		Entry *e = (Entry *) g_ptr_array_index (dict, i);

		lrg_ai_priv_put (map, e->key, lrg_ai_priv_variant_to_node (e->value));
	}
	return node;
}

static void
seq_add (YamlNode *seqnode,
         YamlNode *elem)
{
	yaml_sequence_add_element (yaml_node_get_sequence (seqnode), elem);
	yaml_node_unref (elem);
}

static void
put_seq (YamlMapping *map,
         const gchar *key,
         YamlNode    *seqnode)
{
	if (yaml_sequence_get_length (yaml_node_get_sequence (seqnode)) == 0)
		yaml_node_unref (seqnode);
	else
		lrg_ai_priv_put (map, key, seqnode);
}

static YamlNode *
write_node (LrgVscriptNode *node)
{
	YamlNode    *yn = yaml_node_new_mapping (NULL);
	YamlMapping *map = yaml_node_get_mapping (yn);
	YamlNode    *params;
	YamlNode    *inputs;

	lrg_ai_priv_put (map, "id", lrg_ai_priv_variant_to_node (g_variant_new_string (node->id)));
	lrg_ai_priv_put (map, "type", lrg_ai_priv_variant_to_node (g_variant_new_string (node->kind)));
	lrg_ai_priv_put (map, "x", lrg_ai_priv_number_node (node->x));
	lrg_ai_priv_put (map, "y", lrg_ai_priv_number_node (node->y));
	params = write_dict (node->params);
	if (params != NULL)
		lrg_ai_priv_put (map, "params", params);
	inputs = write_dict (node->inputs);
	if (inputs != NULL)
		lrg_ai_priv_put (map, "inputs", inputs);
	lrg_ai_priv_copy_extra (map, node->extra);
	return yn;
}

static YamlNode *
write_link (LrgVscriptLink *link)
{
	YamlNode         *yn = yaml_node_new_mapping (NULL);
	YamlMapping      *map = yaml_node_get_mapping (yn);
	g_autofree gchar *from = g_strdup_printf ("%s.%s", link->from_node, link->from_pin);
	g_autofree gchar *to = g_strdup_printf ("%s.%s", link->to_node, link->to_pin);

	lrg_ai_priv_put (map, "from", lrg_ai_priv_variant_to_node (g_variant_new_string (from)));
	lrg_ai_priv_put (map, "to", lrg_ai_priv_variant_to_node (g_variant_new_string (to)));
	lrg_ai_priv_copy_extra (map, (YamlMapping *) link->extra);
	return yn;
}

static YamlNode *
write_comment (LrgVscriptComment *comment)
{
	YamlNode    *yn = yaml_node_new_mapping (NULL);
	YamlMapping *map = yaml_node_get_mapping (yn);

	put_text (map, "id", comment->id);
	lrg_ai_priv_put (map, "text", lrg_ai_priv_variant_to_node (g_variant_new_string (comment->text != NULL ? comment->text : "")));
	lrg_ai_priv_put (map, "x", lrg_ai_priv_number_node (comment->x));
	lrg_ai_priv_put (map, "y", lrg_ai_priv_number_node (comment->y));
	lrg_ai_priv_put (map, "width", lrg_ai_priv_number_node (comment->width));
	lrg_ai_priv_put (map, "height", lrg_ai_priv_number_node (comment->height));
	put_text (map, "color", comment->color);
	lrg_ai_priv_copy_extra (map, (YamlMapping *) comment->extra);
	return yn;
}

static YamlNode *
write_iface_pin (LrgVscriptPin *pin)
{
	YamlNode    *yn = yaml_node_new_mapping (NULL);
	YamlMapping *map = yaml_node_get_mapping (yn);

	lrg_ai_priv_put (map, "name", lrg_ai_priv_variant_to_node (g_variant_new_string (pin->name)));
	lrg_ai_priv_put (map, "type", lrg_ai_priv_variant_to_node (g_variant_new_string (pin->type)));
	put_text (map, "label", pin->label);
	lrg_ai_priv_copy_extra (map, (YamlMapping *) pin->extra);
	return yn;
}

static YamlNode *
write_variable (Variable *var)
{
	YamlNode    *yn = yaml_node_new_mapping (NULL);
	YamlMapping *map = yaml_node_get_mapping (yn);

	lrg_ai_priv_put (map, "name", lrg_ai_priv_variant_to_node (g_variant_new_string (var->name)));
	lrg_ai_priv_put (map, "type", lrg_ai_priv_variant_to_node (g_variant_new_string (var->type)));
	if (var->def != NULL)
		lrg_ai_priv_put (map, "default", lrg_ai_priv_variant_to_node (var->def));
	lrg_ai_priv_copy_extra (map, var->extra);
	return yn;
}

static void
write_iface (YamlMapping *map,
             const gchar *key,
             GPtrArray   *pins)
{
	YamlNode *seq;
	guint     i;

	if (pins == NULL || pins->len == 0)
		return;
	seq = yaml_node_new_sequence (NULL);
	for (i = 0; i < pins->len; i++)
		seq_add (seq, write_iface_pin ((LrgVscriptPin *) g_ptr_array_index (pins, i)));
	lrg_ai_priv_put (map, key, seq);
}

static void
write_graph_lists (YamlMapping     *map,
                   LrgVscriptGraph *graph)
{
	YamlNode *seq;
	guint     i;

	seq = yaml_node_new_sequence (NULL);
	for (i = 0; i < graph->nodes->len; i++)
		seq_add (seq, write_node ((LrgVscriptNode *) g_ptr_array_index (graph->nodes, i)));
	put_seq (map, "nodes", seq);

	seq = yaml_node_new_sequence (NULL);
	for (i = 0; i < graph->links->len; i++)
		seq_add (seq, write_link ((LrgVscriptLink *) g_ptr_array_index (graph->links, i)));
	put_seq (map, "links", seq);

	seq = yaml_node_new_sequence (NULL);
	for (i = 0; i < graph->comments->len; i++)
		seq_add (seq, write_comment ((LrgVscriptComment *) g_ptr_array_index (graph->comments, i)));
	put_seq (map, "comments", seq);
}

static YamlNode *
write_subgraph (LrgVscriptGraph *graph)
{
	YamlNode    *yn = yaml_node_new_mapping (NULL);
	YamlMapping *map = yaml_node_get_mapping (yn);

	lrg_ai_priv_put (map, "name", lrg_ai_priv_variant_to_node (g_variant_new_string (graph->name)));
	write_iface (map, "inputs", graph->iface[0]);
	write_iface (map, "outputs", graph->iface[1]);
	write_graph_lists (map, graph);
	lrg_ai_priv_copy_extra (map, graph->extra);
	return yn;
}

gchar *
lrg_vscript_program_to_data (LrgVscriptProgram  *self,
                             GError            **error)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	g_autoptr(GError)        local = NULL;
	YamlMapping             *map;
	YamlNode                *seq;
	gchar                   *text;
	guint                    i;

	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), NULL);

	root = yaml_node_new_mapping (NULL);
	map = yaml_node_get_mapping (root);
	yaml_mapping_set_int_member (map, "version", LRG_VSCRIPT_FORMAT_VERSION);
	put_text (map, "name", self->name);
	put_text (map, "description", self->description);
	lrg_ai_priv_put (map, "view", lrg_ai_priv_variant_to_node (g_variant_new_string (
	                         self->view == LRG_VSCRIPT_VIEW_BLOCKS ? "blocks" : "graph")));
	if (self->seed != 0)
		yaml_mapping_set_int_member (map, "seed", (gint64) self->seed);
	if (self->step_budget != LRG_VSCRIPT_DEFAULT_STEP_BUDGET)
		yaml_mapping_set_int_member (map, "step_budget", (gint64) self->step_budget);
	if (self->fixed_step != DEFAULT_FIXED_STEP)
		lrg_ai_priv_put (map, "fixed_step", lrg_ai_priv_number_node (self->fixed_step));

	seq = yaml_node_new_sequence (NULL);
	for (i = 0; i < self->variables->len; i++)
		seq_add (seq, write_variable ((Variable *) g_ptr_array_index (self->variables, i)));
	put_seq (map, "variables", seq);
	write_graph_lists (map, self->main_graph);

	seq = yaml_node_new_sequence (NULL);
	for (i = 0; i < self->subgraphs->len; i++)
		seq_add (seq, write_subgraph ((LrgVscriptGraph *) g_ptr_array_index (self->subgraphs, i)));
	put_seq (map, "subgraphs", seq);
	lrg_ai_priv_copy_extra (map, self->extra);

	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	text = yaml_generator_to_data (gen, NULL, &local);
	if (text == NULL)
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_IO, "%s", local != NULL ? local->message : "write failed");
		return NULL;
	}
	return text;
}

/* --- parser -------------------------------------------------------------- */

static gboolean
parse_scalar_dict (YamlNode   *node,
                   const gchar *path,
                   GPtrArray  *dict,
                   GError    **error)
{
	YamlMapping      *map;
	g_autoptr(GList)  keys = NULL;
	GList            *l;

	map = vs_map (node, path, error);
	if (map == NULL)
		return FALSE;
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		g_autofree gchar *p = lrg_ai_priv_sub_path (path, (const gchar *) l->data);
		YamlNode         *v = yaml_mapping_get_member (map, (const gchar *) l->data);
		GVariant         *value;

		if (v == NULL || yaml_node_get_node_type (v) != YAML_NODE_SCALAR)
			return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, p, "expected a scalar");
		value = lrg_ai_priv_scalar_to_variant (v);
		dict_set (dict, (const gchar *) l->data, value);
		g_variant_unref (value);
	}
	return TRUE;
}

static gboolean
parse_optional_dict (YamlMapping *map,
                     const gchar *path,
                     const gchar *key,
                     GPtrArray   *dict,
                     GError     **error)
{
	YamlNode         *n = yaml_mapping_get_member (map, key);
	g_autofree gchar *p = lrg_ai_priv_sub_path (path, key);

	if (n == NULL)
		return TRUE;
	return parse_scalar_dict (n, p, dict, error);
}

static gboolean
parse_node (YamlNode         *yn,
            const gchar      *path,
            LrgVscriptGraph  *graph,
            GError          **error)
{
	static const gchar * const known[] = { "id", "type", "x", "y", "params", "inputs", NULL };
	YamlMapping *map;
	const gchar *id = NULL;
	const gchar *type = NULL;
	gdouble      x = 0.0;
	gdouble      y = 0.0;
	LrgVscriptNode *node;

	map = vs_map (yn, path, error);
	if (map == NULL)
		return FALSE;
	if (!vs_string (map, path, "id", TRUE, &id, error))
		return FALSE;
	if (!vs_string (map, path, "type", TRUE, &type, error))
		return FALSE;
	if (!id_is_valid (id))
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, path, "'%s' is not a valid node id", id);
	if (type[0] == '\0')
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, path, "missing node type");
	if (g_hash_table_contains (graph->by_id, id))
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, path, "a node '%s' already exists", id);
	if (!vs_number (map, path, "x", &x, NULL, error))
		return FALSE;
	if (!vs_number (map, path, "y", &y, NULL, error))
		return FALSE;
	node = lrg_vscript_node_new (id, type);
	lrg_vscript_node_set_position (node, x, y);
	if (!parse_optional_dict (map, path, "params", node->params, error) ||
	    !parse_optional_dict (map, path, "inputs", node->inputs, error))
	{
		g_object_unref (node);
		return FALSE;
	}
	node->extra = lrg_ai_priv_collect_extra (map, known);
	if (!lrg_vscript_graph_add_node (graph, node, error))
	{
		g_object_unref (node);
		return FALSE;
	}
	g_object_unref (node);
	return TRUE;
}

static gboolean
parse_link (YamlNode         *yn,
            const gchar      *path,
            LrgVscriptGraph  *graph,
            GError          **error)
{
	static const gchar * const known[] = { "from", "to", NULL };
	YamlMapping        *map;
	const gchar        *from = NULL;
	const gchar        *to = NULL;
	g_autofree gchar   *from_node = NULL;
	g_autofree gchar   *from_pin = NULL;
	g_autofree gchar   *to_node = NULL;
	g_autofree gchar   *to_pin = NULL;
	g_autofree gchar   *fp = NULL;
	g_autofree gchar   *tp = NULL;
	LrgVscriptLink     *link;

	map = vs_map (yn, path, error);
	if (map == NULL)
		return FALSE;
	if (!vs_string (map, path, "from", TRUE, &from, error))
		return FALSE;
	if (!vs_string (map, path, "to", TRUE, &to, error))
		return FALSE;
	fp = lrg_ai_priv_sub_path (path, "from");
	tp = lrg_ai_priv_sub_path (path, "to");
	if (!split_pin_addr (from, fp, &from_node, &from_pin, error))
		return FALSE;
	if (!split_pin_addr (to, tp, &to_node, &to_pin, error))
		return FALSE;
	link = lrg_vscript_link_new (from_node, from_pin, to_node, to_pin);
	link->extra = lrg_ai_priv_collect_extra (map, known);
	g_ptr_array_add (graph->links, link);
	return TRUE;
}

static gchar *
free_comment_id (LrgVscriptGraph *graph)
{
	guint n;

	if (lrg_vscript_graph_get_comment (graph, "comment") == NULL)
		return g_strdup ("comment");
	for (n = 2;; n++)
	{
		gchar *id = g_strdup_printf ("comment%u", n);

		if (lrg_vscript_graph_get_comment (graph, id) == NULL)
			return id;
		g_free (id);
	}
}

static gboolean
parse_comment (YamlNode         *yn,
               const gchar      *path,
               LrgVscriptGraph  *graph,
               GError          **error)
{
	static const gchar * const known[] = { "id", "text", "x", "y", "width", "height", "color", NULL };
	YamlMapping       *map;
	const gchar       *id = NULL;
	const gchar       *text = NULL;
	const gchar       *color = NULL;
	gdouble            x = 0.0;
	gdouble            y = 0.0;
	gdouble            w = 0.0;
	gdouble            h = 0.0;
	LrgVscriptComment *comment;
	g_autofree gchar  *gen = NULL;

	map = vs_map (yn, path, error);
	if (map == NULL)
		return FALSE;
	if (!vs_string (map, path, "id", FALSE, &id, error))
		return FALSE;
	if (!vs_string (map, path, "text", FALSE, &text, error))
		return FALSE;
	if (!vs_string (map, path, "color", FALSE, &color, error))
		return FALSE;
	if (!vs_number (map, path, "x", &x, NULL, error) ||
	    !vs_number (map, path, "y", &y, NULL, error) ||
	    !vs_number (map, path, "width", &w, NULL, error) ||
	    !vs_number (map, path, "height", &h, NULL, error))
		return FALSE;
	if (id == NULL || id[0] == '\0')
	{
		gen = free_comment_id (graph);
		id = gen;
	}
	comment = lrg_vscript_comment_new (id, text, x, y, w, h);
	if (color != NULL && color[0] != '\0')
		comment->color = g_strdup (color);
	comment->extra = lrg_ai_priv_collect_extra (map, known);
	g_ptr_array_add (graph->comments, comment);
	return TRUE;
}

static gboolean
parse_named_list (YamlMapping     *map,
                  const gchar     *path,
                  const gchar     *key,
                  LrgVscriptGraph *graph,
                  gboolean       (*parse_one) (YamlNode *, const gchar *, LrgVscriptGraph *, GError **),
                  GError         **error)
{
	YamlNode         *n = yaml_mapping_get_member (map, key);
	g_autofree gchar *p = lrg_ai_priv_sub_path (path, key);
	YamlSequence     *seq;
	guint             i;

	if (n == NULL)
		return TRUE;
	seq = vs_seq (n, p, error);
	if (seq == NULL)
		return FALSE;
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		g_autofree gchar *ip = lrg_ai_priv_index_path (path, key, i);

		if (!parse_one (yaml_sequence_get_element (seq, i), ip, graph, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
parse_iface_list (YamlMapping             *map,
                  const gchar             *path,
                  const gchar             *key,
                  LrgVscriptGraph         *graph,
                  LrgVscriptPinDirection   dir,
                  GError                 **error)
{
	static const gchar * const known[] = { "name", "type", "label", NULL };
	YamlNode         *n = yaml_mapping_get_member (map, key);
	g_autofree gchar *p = lrg_ai_priv_sub_path (path, key);
	YamlSequence     *seq;
	gint              slot;
	guint             i;

	if (n == NULL)
		return TRUE;
	seq = vs_seq (n, p, error);
	if (seq == NULL)
		return FALSE;
	slot = dir == LRG_VSCRIPT_PIN_OUTPUT ? 1 : 0;
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		g_autofree gchar *ip = lrg_ai_priv_index_path (path, key, i);
		YamlMapping      *pm;
		const gchar      *name = NULL;
		const gchar      *type = NULL;
		const gchar      *label = NULL;
		LrgVscriptPin    *pin;
		gboolean          exec;

		pm = vs_map (yaml_sequence_get_element (seq, i), ip, error);
		if (pm == NULL)
			return FALSE;
		if (!vs_string (pm, ip, "name", TRUE, &name, error))
			return FALSE;
		if (!vs_string (pm, ip, "type", FALSE, &type, error))
			return FALSE;
		if (!vs_string (pm, ip, "label", FALSE, &label, error))
			return FALSE;
		exec = g_strcmp0 (type, "exec") == 0;
		pin = lrg_vscript_pin_new (name, dir, exec, type);
		if (label != NULL && label[0] != '\0')
			pin->label = g_strdup (label);
		pin->extra = lrg_ai_priv_collect_extra (pm, known);
		g_ptr_array_add (graph->iface[slot], pin);
	}
	return TRUE;
}

static gboolean
parse_variable (YamlNode          *yn,
		const gchar       *path,
		LrgVscriptProgram *prog,
		GError           **error)
{
	static const gchar * const known[] = { "name", "type", "default", NULL };
	YamlMapping      *map;
	const gchar      *name = NULL;
	const gchar      *type = NULL;
	YamlNode         *def;
	g_autofree gchar *dp = NULL;
	Variable         *v;

	map = vs_map (yn, path, error);
	if (map == NULL)
		return FALSE;
	if (!vs_string (map, path, "name", TRUE, &name, error))
		return FALSE;
	if (!vs_string (map, path, "type", FALSE, &type, error))
		return FALSE;
	if (name[0] == '\0')
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, path, "missing variable name");
	if (variable_find (prog, name, NULL) != NULL)
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, path, "variable '%s' already exists", name);
	v = g_new0 (Variable, 1);
	v->name = g_strdup (name);
	v->type = g_strdup (type != NULL && type[0] != '\0' ? type : "any");
	def = yaml_mapping_get_member (map, "default");
	if (def != NULL)
	{
		dp = lrg_ai_priv_sub_path (path, "default");
		if (yaml_node_get_node_type (def) != YAML_NODE_SCALAR)
		{
			variable_free (v);
			return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, dp, "expected a scalar");
		}
		v->def = lrg_ai_priv_scalar_to_variant (def);
	}
	v->extra = lrg_ai_priv_collect_extra (map, known);
	g_ptr_array_add (prog->variables, v);
	return TRUE;
}

static gboolean
parse_graph_lists (YamlMapping     *map,
                   const gchar     *path,
                   LrgVscriptGraph *graph,
                   GError         **error)
{
	return parse_named_list (map, path, "nodes", graph, parse_node, error) &&
	       parse_named_list (map, path, "links", graph, parse_link, error) &&
	       parse_named_list (map, path, "comments", graph, parse_comment, error);
}

static gboolean
parse_subgraph (YamlNode          *yn,
		const gchar       *path,
		LrgVscriptProgram *prog,
		GError           **error)
{
	static const gchar * const known[] = { "name", "inputs", "outputs", "nodes", "links", "comments", NULL };
	YamlMapping     *map;
	const gchar     *name = NULL;
	LrgVscriptGraph *graph;

	map = vs_map (yn, path, error);
	if (map == NULL)
		return FALSE;
	if (!vs_string (map, path, "name", TRUE, &name, error))
		return FALSE;
	if (!id_is_valid (name))
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, path, "'%s' is not a valid subgraph name", name);
	if (lrg_vscript_program_get_graph (prog, name) != NULL)
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, path, "a subgraph '%s' already exists", name);
	graph = graph_new (name);
	if (!parse_iface_list (map, path, "inputs", graph, LRG_VSCRIPT_PIN_INPUT, error) ||
	    !parse_iface_list (map, path, "outputs", graph, LRG_VSCRIPT_PIN_OUTPUT, error) ||
	    !parse_graph_lists (map, path, graph, error))
	{
		g_object_unref (graph);
		return FALSE;
	}
	graph->extra = lrg_ai_priv_collect_extra (map, known);
	g_ptr_array_add (prog->subgraphs, graph);
	return TRUE;
}

static gboolean
parse_version (YamlMapping *map,
               GError     **error)
{
	YamlNode        *n = yaml_mapping_get_member (map, "version");
	YamlScalarStyle  style;
	const gchar     *t;
	const gchar     *s;
	gchar           *end = NULL;
	gint64           v;

	if (n == NULL)
		return TRUE;
	if (yaml_node_get_node_type (n) != YAML_NODE_SCALAR || yaml_node_get_scalar (n) == NULL)
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, "version", "expected the number 1");
	style = yaml_node_get_scalar_style (n);
	if (style == YAML_SCALAR_STYLE_SINGLE_QUOTED || style == YAML_SCALAR_STYLE_DOUBLE_QUOTED ||
	    style == YAML_SCALAR_STYLE_LITERAL || style == YAML_SCALAR_STYLE_FOLDED)
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, "version", "expected the number 1");
	t = yaml_node_get_scalar (n);
	s = t;
	if (*s == '+' || *s == '-')
		s++;
	if (*s == '\0')
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, "version", "expected the number 1");
	for (; *s != '\0'; s++)
	{
		if (!g_ascii_isdigit (*s))
			return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, "version", "expected the number 1");
	}
	v = g_ascii_strtoll (t, &end, 10);
	if (end == t || *end != '\0' || v != LRG_VSCRIPT_FORMAT_VERSION)
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, "version", "expected the number 1");
	return TRUE;
}

static gboolean
parse_program (LrgVscriptProgram *self,
               YamlNode          *root,
               GError           **error)
{
	static const gchar * const known[] = {
		"version", "name", "description", "view", "seed", "step_budget", "fixed_step",
		"variables", "nodes", "links", "comments", "subgraphs", NULL
	};
	YamlMapping  *map;
	const gchar  *text = NULL;
	gboolean      present = FALSE;
	gint64        i64 = 0;
	gdouble       step = 0.0;
	YamlNode     *n;
	YamlSequence *seq;
	guint         i;

	map = vs_map (root, "", error);
	if (map == NULL)
		return FALSE;
	if (!parse_version (map, error))
		return FALSE;
	if (!vs_string (map, "", "name", FALSE, &text, error))
		return FALSE;
	lrg_vscript_program_set_name (self, text);
	text = NULL;
	if (!vs_string (map, "", "description", FALSE, &text, error))
		return FALSE;
	lrg_vscript_program_set_description (self, text);
	text = NULL;
	if (!vs_string (map, "", "view", FALSE, &text, error))
		return FALSE;
	if (text == NULL || g_strcmp0 (text, "graph") == 0)
		self->view = LRG_VSCRIPT_VIEW_GRAPH;
	else if (g_strcmp0 (text, "blocks") == 0)
		self->view = LRG_VSCRIPT_VIEW_BLOCKS;
	else
		return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, "view", "expected \"graph\" or \"blocks\"");
	if (!vs_int (map, "", "seed", &i64, &present, error))
		return FALSE;
	if (present)
	{
		if (i64 < 0 || i64 > (gint64) G_MAXUINT32)
			return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, "seed", "seed is out of range");
		self->seed = (guint32) i64;
	}
	present = FALSE;
	if (!vs_int (map, "", "step_budget", &i64, &present, error))
		return FALSE;
	if (present)
	{
		if (i64 < 0)
			return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, "step_budget", "expected an integer");
		lrg_vscript_program_set_step_budget (self, (guint) i64);
	}
	present = FALSE;
	if (!vs_number (map, "", "fixed_step", &step, &present, error))
		return FALSE;
	if (present)
	{
		if (!(step > 0.0))
			return vs_fail (error, LRG_VSCRIPT_ERROR_INVALID, "fixed_step", "expected a number greater than 0");
		self->fixed_step = step;
	}
	n = yaml_mapping_get_member (map, "variables");
	if (n != NULL)
	{
		seq = vs_seq (n, "variables", error);
		if (seq == NULL)
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar *ip = lrg_ai_priv_index_path ("", "variables", i);

			if (!parse_variable (yaml_sequence_get_element (seq, i), ip, self, error))
				return FALSE;
		}
	}
	if (!parse_graph_lists (map, "", self->main_graph, error))
		return FALSE;
	n = yaml_mapping_get_member (map, "subgraphs");
	if (n != NULL)
	{
		seq = vs_seq (n, "subgraphs", error);
		if (seq == NULL)
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar *ip = lrg_ai_priv_index_path ("", "subgraphs", i);

			if (!parse_subgraph (yaml_sequence_get_element (seq, i), ip, self, error))
				return FALSE;
		}
	}
	self->extra = lrg_ai_priv_collect_extra (map, known);
	return TRUE;
}

LrgVscriptProgram *
lrg_vscript_program_new_from_data (const gchar  *data,
                                   gssize        length,
                                   GError      **error)
{
	g_autoptr(YamlParser)       parser = NULL;
	g_autoptr(GError)           local = NULL;
	g_autoptr(LrgVscriptProgram) prog = NULL;
	YamlNode                    *root;

	g_return_val_if_fail (data != NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_PARSE, "%s",
		             local != NULL ? local->message : "the document is not YAML");
		return NULL;
	}
	root = yaml_parser_get_root (parser);
	if (root == NULL)
	{
		g_set_error_literal (error, DOMAIN, LRG_VSCRIPT_ERROR_PARSE, "the document is empty");
		return NULL;
	}
	prog = lrg_vscript_program_new ();
	if (!parse_program (prog, root, error))
		return NULL;
	return g_steal_pointer (&prog);
}

LrgVscriptProgram *
lrg_vscript_program_new_from_file (const gchar  *path,
                                   GError      **error)
{
	g_autofree gchar        *text = NULL;
	g_autofree gchar        *dir = NULL;
	g_autoptr(GError)        local = NULL;
	gsize                    len = 0;
	LrgVscriptProgram       *prog;

	g_return_val_if_fail (path != NULL, NULL);

	if (!g_file_get_contents (path, &text, &len, &local))
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_IO, "%s: %s", path, local->message);
		return NULL;
	}
	prog = lrg_vscript_program_new_from_data (text, (gssize) len, error);
	if (prog == NULL)
	{
		g_prefix_error (error, "%s: ", path);
		return NULL;
	}
	dir = g_path_get_dirname (path);
	lrg_vscript_program_set_base_dir (prog, dir);
	return prog;
}

gboolean
lrg_vscript_program_save_to_file (LrgVscriptProgram  *self,
                                  const gchar        *path,
                                  GError            **error)
{
	g_autofree gchar *text = NULL;
	g_autofree gchar *dir = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = lrg_vscript_program_to_data (self, error);
	if (text == NULL)
		return FALSE;
	if (!g_file_set_contents (path, text, -1, &local))
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_IO, "%s: %s", path, local->message);
		return FALSE;
	}
	dir = g_path_get_dirname (path);
	lrg_vscript_program_set_base_dir (self, dir);
	return TRUE;
}

LrgVscriptProgram *
lrg_vscript_program_duplicate (LrgVscriptProgram *self)
{
	g_autofree gchar        *text = NULL;
	g_autoptr(GError)        error = NULL;
	LrgVscriptProgram       *copy;

	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), NULL);

	text = lrg_vscript_program_to_data (self, &error);
	if (text == NULL)
		return lrg_vscript_program_new ();
	copy = lrg_vscript_program_new_from_data (text, -1, &error);
	if (copy == NULL)
		return lrg_vscript_program_new ();
	lrg_vscript_program_set_base_dir (copy, self->base_dir);
	return copy;
}

/* --- pins ---------------------------------------------------------------- */

static gboolean
kind_is (const gchar *kind,
         const gchar *expect)
{
	return g_strcmp0 (kind, expect) == 0;
}

static void
add_gi_call_fallback (GPtrArray      *pins,
                      LrgVscriptNode *node,
                      gboolean        pure)
{
	const gchar *args = lrg_vscript_node_get_param_string (node, "args");
	const gchar *returns = lrg_vscript_node_get_param_string (node, "returns");
	gboolean     have_args;
	gboolean     have_ret;

	have_args = args != NULL && args[0] != '\0';
	have_ret = returns != NULL && returns[0] != '\0';
	if (!have_args && !have_ret)
	{
		if (!pure)
			add_exec_pair (pins, TRUE, TRUE);
		return;
	}
	if (!pure)
		g_ptr_array_add (pins, lrg_vscript_pin_new ("exec", LRG_VSCRIPT_PIN_INPUT, TRUE, "exec"));
	if (have_args)
		add_arg_pins (pins, args, LRG_VSCRIPT_PIN_INPUT);
	if (have_ret)
		g_ptr_array_add (pins, lrg_vscript_pin_new ("result", LRG_VSCRIPT_PIN_OUTPUT, FALSE, returns));
	if (!pure)
		g_ptr_array_add (pins, lrg_vscript_pin_new ("then", LRG_VSCRIPT_PIN_OUTPUT, TRUE, "exec"));
}

static void
copy_iface (GPtrArray              *dst,
            GPtrArray              *src,
            LrgVscriptPinDirection  dir)
{
	guint i;

	if (src == NULL)
		return;
	for (i = 0; i < src->len; i++)
	{
		LrgVscriptPin *p = (LrgVscriptPin *) g_ptr_array_index (src, i);
		LrgVscriptPin *c = lrg_vscript_pin_new (p->name, dir, p->exec, p->type);

		c->label = g_strdup (p->label);
		g_ptr_array_add (dst, c);
	}
}

static gchar *
resolve_file (LrgVscriptProgram *self,
              const gchar       *file)
{
	if (file == NULL || file[0] == '\0')
		return NULL;
	if (g_path_is_absolute (file) || self->base_dir == NULL)
		return g_strdup (file);
	return g_build_filename (self->base_dir, file, NULL);
}

/* @loaded is set when the callee is a file's main graph; the caller keeps it alive */
static LrgVscriptGraph *
callee_graph (LrgVscriptProgram  *self,
              LrgVscriptNode     *node,
              LrgVscriptProgram **loaded)
{
	const gchar      *name;
	const gchar      *file;
	g_autofree gchar *path = NULL;

	*loaded = NULL;
	name = lrg_vscript_node_get_param_string (node, "name");
	if (name != NULL && name[0] != '\0')
		return lrg_vscript_program_get_graph (self, name);
	file = lrg_vscript_node_get_param_string (node, "file");
	path = resolve_file (self, file);
	if (path == NULL)
		return NULL;
	*loaded = lrg_vscript_program_new_from_file (path, NULL);
	if (*loaded == NULL)
		return NULL;
	return lrg_vscript_program_get_main (*loaded);
}

static const gchar *
var_type_of (LrgVscriptProgram *self,
             LrgVscriptNode    *node)
{
	const gchar *name = lrg_vscript_node_get_param_string (node, "name");
	const gchar *type = NULL;

	if (name != NULL)
		type = lrg_vscript_program_get_variable_type (self, name);
	if (type == NULL)
		type = lrg_vscript_node_get_param_string (node, "type");
	if (type == NULL || type[0] == '\0')
		type = "any";
	return type;
}

static void
add_numbered_exec (GPtrArray *pins,
                   gint64     count)
{
	gint64 i;

	g_ptr_array_add (pins, lrg_vscript_pin_new ("exec", LRG_VSCRIPT_PIN_INPUT, TRUE, "exec"));
	for (i = 0; i < count; i++)
	{
		gchar *name = g_strdup_printf ("%" G_GINT64_FORMAT, i);

		g_ptr_array_add (pins, lrg_vscript_pin_new (name, LRG_VSCRIPT_PIN_OUTPUT, TRUE, "exec"));
		g_free (name);
	}
}

static void
add_script_call_pins (GPtrArray      *pins,
                      LrgVscriptNode *node)
{
	gint64       n = param_int (node, "args", 0);
	gboolean     pure = param_bool (node, "pure");
	const gchar *returns = lrg_vscript_node_get_param_string (node, "returns");
	gint64       i;

	if (n < 0)
		n = 0;
	if (n > 16)
		n = 16;
	if (!pure)
		g_ptr_array_add (pins, lrg_vscript_pin_new ("exec", LRG_VSCRIPT_PIN_INPUT, TRUE, "exec"));
	for (i = 0; i < n; i++)
	{
		gchar *name = g_strdup_printf ("arg%" G_GINT64_FORMAT, i);

		g_ptr_array_add (pins, lrg_vscript_pin_new (name, LRG_VSCRIPT_PIN_INPUT, FALSE, "any"));
		g_free (name);
	}
	if (returns != NULL && returns[0] != '\0')
		g_ptr_array_add (pins, lrg_vscript_pin_new ("result", LRG_VSCRIPT_PIN_OUTPUT, FALSE, returns));
	if (!pure)
		g_ptr_array_add (pins, lrg_vscript_pin_new ("then", LRG_VSCRIPT_PIN_OUTPUT, TRUE, "exec"));
}

GPtrArray *
lrg_vscript_program_dup_node_pins (LrgVscriptProgram *self,
                                   LrgVscriptGraph   *graph,
                                   LrgVscriptNode    *node)
{
	GPtrArray      *pins;
	const gchar    *kind;
	const KindInfo *info;

	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), NULL);
	g_return_val_if_fail (LRG_IS_VSCRIPT_NODE (node), NULL);

	if (graph == NULL)
		graph = self->main_graph;
	pins = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_vscript_pin_free);
	kind = node->kind;
	info = kind_lookup (kind);
	if (info != NULL && info->pins != NULL)
		add_spec_pins (pins, info->pins);
	else if (kind_is (kind, "event.custom"))
	{
		g_ptr_array_add (pins, lrg_vscript_pin_new ("then", LRG_VSCRIPT_PIN_OUTPUT, TRUE, "exec"));
		add_arg_pins (pins, lrg_vscript_node_get_param_string (node, "args"), LRG_VSCRIPT_PIN_OUTPUT);
	}
	else if (kind_is (kind, "event.signal"))
	{
		g_ptr_array_add (pins, lrg_vscript_pin_new ("then", LRG_VSCRIPT_PIN_OUTPUT, TRUE, "exec"));
		g_ptr_array_add (pins, lrg_vscript_pin_new ("sender", LRG_VSCRIPT_PIN_OUTPUT, FALSE, "object"));
		add_arg_pins (pins, lrg_vscript_node_get_param_string (node, "args"), LRG_VSCRIPT_PIN_OUTPUT);
	}
	else if (kind_is (kind, "flow.sequence"))
	{
		gint64 n = param_int (node, "count", 2);

		if (n < 1)
			n = 1;
		if (n > 16)
			n = 16;
		add_numbered_exec (pins, n);
	}
	else if (kind_is (kind, "string.format"))
		add_format_pins (pins, lrg_vscript_node_get_param_string (node, "format"));
	else if (kind_is (kind, "var.get"))
		g_ptr_array_add (pins, lrg_vscript_pin_new ("value", LRG_VSCRIPT_PIN_OUTPUT, FALSE, var_type_of (self, node)));
	else if (kind_is (kind, "var.set"))
	{
		g_ptr_array_add (pins, lrg_vscript_pin_new ("exec", LRG_VSCRIPT_PIN_INPUT, TRUE, "exec"));
		g_ptr_array_add (pins, lrg_vscript_pin_new ("value", LRG_VSCRIPT_PIN_INPUT, FALSE, var_type_of (self, node)));
		g_ptr_array_add (pins, lrg_vscript_pin_new ("then", LRG_VSCRIPT_PIN_OUTPUT, TRUE, "exec"));
	}
	else if (kind_is (kind, "gi.call"))
	{
		gboolean pure = param_bool (node, "pure");

#ifdef VSCRIPT_HAVE_GI
		{
			const gchar *fn = lrg_vscript_node_get_param_string (node, "function");
			GIBaseInfo  *gi = NULL;

			if (fn != NULL && fn[0] != '\0')
				gi = gi_find_callable (fn, NULL);
			if (GI_IS_CALLABLE_INFO (gi))
				gi_add_signature_pins (pins, (GICallableInfo *) gi, pure);
			else
				add_gi_call_fallback (pins, node, pure);
			if (gi != NULL)
				gi_base_info_unref (gi);
		}
#else
		add_gi_call_fallback (pins, node, pure);
#endif
	}
	else if (kind_is (kind, "gi.emit"))
	{
		g_ptr_array_add (pins, lrg_vscript_pin_new ("exec", LRG_VSCRIPT_PIN_INPUT, TRUE, "exec"));
		g_ptr_array_add (pins, lrg_vscript_pin_new ("object", LRG_VSCRIPT_PIN_INPUT, FALSE, "object"));
		add_arg_pins (pins, lrg_vscript_node_get_param_string (node, "args"), LRG_VSCRIPT_PIN_INPUT);
		g_ptr_array_add (pins, lrg_vscript_pin_new ("then", LRG_VSCRIPT_PIN_OUTPUT, TRUE, "exec"));
	}
	else if (kind_is (kind, "subgraph.input"))
	{
		copy_iface (pins, lrg_vscript_graph_get_interface (graph, LRG_VSCRIPT_PIN_INPUT), LRG_VSCRIPT_PIN_OUTPUT);
		g_ptr_array_add (pins, lrg_vscript_pin_new ("then", LRG_VSCRIPT_PIN_OUTPUT, TRUE, "exec"));
	}
	else if (kind_is (kind, "subgraph.output"))
	{
		g_ptr_array_add (pins, lrg_vscript_pin_new ("exec", LRG_VSCRIPT_PIN_INPUT, TRUE, "exec"));
		copy_iface (pins, lrg_vscript_graph_get_interface (graph, LRG_VSCRIPT_PIN_OUTPUT), LRG_VSCRIPT_PIN_INPUT);
	}
	else if (kind_is (kind, "subgraph.call"))
	{
		g_autoptr(LrgVscriptProgram) loaded = NULL;
		LrgVscriptGraph             *callee = callee_graph (self, node, &loaded);

		g_ptr_array_add (pins, lrg_vscript_pin_new ("exec", LRG_VSCRIPT_PIN_INPUT, TRUE, "exec"));
		if (callee != NULL)
		{
			copy_iface (pins, lrg_vscript_graph_get_interface (callee, LRG_VSCRIPT_PIN_INPUT), LRG_VSCRIPT_PIN_INPUT);
			copy_iface (pins, lrg_vscript_graph_get_interface (callee, LRG_VSCRIPT_PIN_OUTPUT), LRG_VSCRIPT_PIN_OUTPUT);
		}
		g_ptr_array_add (pins, lrg_vscript_pin_new ("then", LRG_VSCRIPT_PIN_OUTPUT, TRUE, "exec"));
	}
	else if (kind_is (kind, "script.call"))
		add_script_call_pins (pins, node);
	else
		add_exec_pair (pins, TRUE, TRUE);
	return pins;
}

static LrgVscriptPin *
pin_named (GPtrArray              *pins,
           const gchar            *name,
           LrgVscriptPinDirection  dir)
{
	guint i;

	if (pins == NULL || name == NULL)
		return NULL;
	for (i = 0; i < pins->len; i++)
	{
		LrgVscriptPin *p = (LrgVscriptPin *) g_ptr_array_index (pins, i);

		if (p->direction == dir && g_strcmp0 (p->name, name) == 0)
			return p;
	}
	return NULL;
}

static gboolean
data_reaches (LrgVscriptProgram *prog,
              LrgVscriptGraph   *graph,
              const gchar       *start,
              const gchar       *target,
              const gchar       *skip_from,
              const gchar       *skip_from_pin,
              const gchar       *skip_to,
              const gchar       *skip_to_pin)
{
	GHashTable *seen = g_hash_table_new (g_str_hash, g_str_equal);
	GQueue     *q = g_queue_new ();
	gboolean    hit = FALSE;

	g_queue_push_tail (q, (gpointer) start);
	while (!hit && !g_queue_is_empty (q))
	{
		const gchar *id = (const gchar *) g_queue_pop_head (q);
		guint        i;

		if (g_hash_table_contains (seen, id))
			continue;
		g_hash_table_add (seen, (gpointer) id);
		for (i = 0; i < graph->links->len; i++)
		{
			LrgVscriptLink       *l = (LrgVscriptLink *) g_ptr_array_index (graph->links, i);
			LrgVscriptNode       *n;
			g_autoptr(GPtrArray)  pins = NULL;
			LrgVscriptPin        *pin;

			if (g_strcmp0 (l->from_node, id) != 0)
				continue;
			if (g_strcmp0 (l->from_node, skip_from) == 0 && g_strcmp0 (l->from_pin, skip_from_pin) == 0 &&
			    g_strcmp0 (l->to_node, skip_to) == 0 && g_strcmp0 (l->to_pin, skip_to_pin) == 0)
				continue;
			n = lrg_vscript_graph_get_node (graph, id);
			if (n == NULL)
				continue;
			pins = lrg_vscript_program_dup_node_pins (prog, graph, n);
			pin = pin_named (pins, l->from_pin, LRG_VSCRIPT_PIN_OUTPUT);
			if (pin == NULL || pin->exec)
				continue;
			if (g_strcmp0 (l->to_node, target) == 0)
			{
				hit = TRUE;
				break;
			}
			g_queue_push_tail (q, l->to_node);
		}
	}
	g_queue_free (q);
	g_hash_table_unref (seen);
	return hit;
}

static void
drop_exclusive (LrgVscriptGraph      *graph,
		const LrgVscriptPin  *from_pin,
		const gchar          *from_node,
		const gchar          *from_name,
		const LrgVscriptPin  *to_pin,
		const gchar          *to_node,
		const gchar          *to_name)
{
	guint i;

	for (i = graph->links->len; i > 0; i--)
	{
		LrgVscriptLink *l = (LrgVscriptLink *) g_ptr_array_index (graph->links, i - 1);
		gboolean        drop = FALSE;

		if (!to_pin->exec && g_strcmp0 (l->to_node, to_node) == 0 && g_strcmp0 (l->to_pin, to_name) == 0)
			drop = TRUE;
		if (from_pin->exec && g_strcmp0 (l->from_node, from_node) == 0 && g_strcmp0 (l->from_pin, from_name) == 0)
			drop = TRUE;
		if (drop)
			g_ptr_array_remove_index (graph->links, i - 1);
	}
}

gboolean
lrg_vscript_program_connect (LrgVscriptProgram  *self,
                             LrgVscriptGraph    *graph,
                             const gchar        *from_node,
                             const gchar        *from_pin,
                             const gchar        *to_node,
                             const gchar        *to_pin,
                             GError            **error)
{
	LrgVscriptNode       *src;
	LrgVscriptNode       *dst;
	g_autoptr(GPtrArray)  src_pins = NULL;
	g_autoptr(GPtrArray)  dst_pins = NULL;
	LrgVscriptPin        *outp;
	LrgVscriptPin        *inp;

	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), FALSE);

	if (graph == NULL)
		graph = self->main_graph;
	src = lrg_vscript_graph_get_node (graph, from_node);
	dst = lrg_vscript_graph_get_node (graph, to_node);
	if (src == NULL)
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_NOT_FOUND, "no node '%s'", from_node != NULL ? from_node : "");
		return FALSE;
	}
	if (dst == NULL)
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_NOT_FOUND, "no node '%s'", to_node != NULL ? to_node : "");
		return FALSE;
	}
	src_pins = lrg_vscript_program_dup_node_pins (self, graph, src);
	dst_pins = lrg_vscript_program_dup_node_pins (self, graph, dst);
	outp = pin_named (src_pins, from_pin, LRG_VSCRIPT_PIN_OUTPUT);
	inp = pin_named (dst_pins, to_pin, LRG_VSCRIPT_PIN_INPUT);
	if (outp == NULL)
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_NOT_FOUND, "no pin '%s' on node '%s'", from_pin, from_node);
		return FALSE;
	}
	if (inp == NULL)
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_NOT_FOUND, "no pin '%s' on node '%s'", to_pin, to_node);
		return FALSE;
	}
	if (outp->exec != inp->exec)
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_INVALID, "type mismatch: %s.%s (%s) -> %s.%s (%s)",
		             from_node, from_pin, outp->type, to_node, to_pin, inp->type);
		return FALSE;
	}
	if (!outp->exec && !lrg_vscript_priv_types_compatible (outp->type, inp->type))
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_INVALID, "type mismatch: %s.%s (%s) -> %s.%s (%s)",
		             from_node, from_pin, outp->type, to_node, to_pin, inp->type);
		return FALSE;
	}
	if (link_index (graph, from_node, from_pin, to_node, to_pin) >= 0)
		return TRUE;
	if (!outp->exec && g_strcmp0 (from_node, to_node) == 0)
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_INVALID, "data cycle: %s.%s -> %s.%s",
		             from_node, from_pin, to_node, to_pin);
		return FALSE;
	}
	if (!outp->exec && data_reaches (self, graph, to_node, from_node, from_node, from_pin, to_node, to_pin))
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_INVALID, "data cycle: %s.%s -> %s.%s",
		             from_node, from_pin, to_node, to_pin);
		return FALSE;
	}
	drop_exclusive (graph, outp, from_node, from_pin, inp, to_node, to_pin);
	if (!lrg_vscript_graph_add_link (graph, from_node, from_pin, to_node, to_pin))
	{
		g_set_error (error, DOMAIN, LRG_VSCRIPT_ERROR_INVALID, "could not link %s.%s -> %s.%s",
		             from_node, from_pin, to_node, to_pin);
		return FALSE;
	}
	return TRUE;
}

/* --- validation ---------------------------------------------------------- */

static void
add_issue (GPtrArray          *found,
           LrgVscriptSeverity  severity,
           const gchar        *graph,
           const gchar        *node,
           const gchar        *path,
           const gchar        *fmt,
           ...)
{
	g_autofree gchar *msg = NULL;
	va_list           ap;

	va_start (ap, fmt);
	msg = g_strdup_vprintf (fmt, ap);
	va_end (ap);
	g_ptr_array_add (found, issue_new (severity, graph, node, path, msg));
}

static guint
count_pin (GPtrArray   *links,
           const gchar *node,
           const gchar *pin,
           gboolean     into)
{
	guint i;
	guint n = 0;

	for (i = 0; i < links->len; i++)
	{
		LrgVscriptLink *l = (LrgVscriptLink *) g_ptr_array_index (links, i);
		const gchar    *ln = into ? l->to_node : l->from_node;
		const gchar    *lp = into ? l->to_pin : l->from_pin;

		if (g_strcmp0 (ln, node) == 0 && g_strcmp0 (lp, pin) == 0)
			n++;
	}
	return n;
}

static gboolean
first_pin (GPtrArray   *links,
           guint        index,
           const gchar *node,
           const gchar *pin,
           gboolean     into)
{
	guint i;

	for (i = 0; i < index; i++)
	{
		LrgVscriptLink *l = (LrgVscriptLink *) g_ptr_array_index (links, i);
		const gchar    *ln = into ? l->to_node : l->from_node;
		const gchar    *lp = into ? l->to_pin : l->from_pin;

		if (g_strcmp0 (ln, node) == 0 && g_strcmp0 (lp, pin) == 0)
			return FALSE;
	}
	return TRUE;
}

static void
validate_node (LrgVscriptProgram *self,
               LrgVscriptGraph   *graph,
               LrgVscriptNode    *node,
               const gchar       *path,
               GPtrArray         *found)
{
	const gchar *kind = node->kind;
	const gchar *gname = graph->name;

	if (!lrg_vscript_node_type_exists (kind))
	{
		add_issue (found, LRG_VSCRIPT_SEVERITY_ERROR, gname, node->id, path, "unknown node kind '%s'", kind);
		return;
	}
	if ((kind_is (kind, "var.get") || kind_is (kind, "var.set")))
	{
		const gchar *name = lrg_vscript_node_get_param_string (node, "name");

		if (name == NULL || name[0] == '\0')
			add_issue (found, LRG_VSCRIPT_SEVERITY_ERROR, gname, node->id, path, "variable node has no name");
	}
	if (kind_is (kind, "event.custom"))
	{
		const gchar *name = lrg_vscript_node_get_param_string (node, "name");

		if (name == NULL || name[0] == '\0')
			add_issue (found, LRG_VSCRIPT_SEVERITY_WARNING, gname, node->id, path, "custom event has no name");
	}
	if (kind_is (kind, "event.input"))
	{
		const gchar *action = lrg_vscript_node_get_param_string (node, "action");

		if (action == NULL || action[0] == '\0')
			add_issue (found, LRG_VSCRIPT_SEVERITY_WARNING, gname, node->id, path, "input event has no action");
	}
	if (kind_is (kind, "event.signal"))
	{
		const gchar *signal = lrg_vscript_node_get_param_string (node, "signal");

		if (signal == NULL || signal[0] == '\0')
			add_issue (found, LRG_VSCRIPT_SEVERITY_WARNING, gname, node->id, path, "signal event has no signal");
	}
	if (kind_is (kind, "subgraph.call"))
	{
		const gchar *name = lrg_vscript_node_get_param_string (node, "name");
		const gchar *file = lrg_vscript_node_get_param_string (node, "file");
		gboolean     have_name = name != NULL && name[0] != '\0';
		gboolean     have_file = file != NULL && file[0] != '\0';

		if (!have_name && !have_file)
			add_issue (found, LRG_VSCRIPT_SEVERITY_ERROR, gname, node->id, path, "subgraph call needs a name or a file");
		else if (have_file)
		{
			g_autofree gchar         *full = resolve_file (self, file);
			g_autoptr(GError)         local = NULL;
			g_autoptr(LrgVscriptProgram) loaded = NULL;

			loaded = full != NULL ? lrg_vscript_program_new_from_file (full, &local) : NULL;
			if (loaded == NULL)
				add_issue (found, LRG_VSCRIPT_SEVERITY_ERROR, gname, node->id, path, "cannot load '%s'", file);
		}
		else if (lrg_vscript_program_get_graph (self, name) == NULL)
			add_issue (found, LRG_VSCRIPT_SEVERITY_ERROR, gname, node->id, path, "no subgraph '%s'", name);
	}
	if (kind_is (kind, "gi.call"))
	{
		const gchar *fn = lrg_vscript_node_get_param_string (node, "function");
		const gchar *args = lrg_vscript_node_get_param_string (node, "args");
		const gchar *returns = lrg_vscript_node_get_param_string (node, "returns");
		gboolean     have_fn = fn != NULL && fn[0] != '\0';
		gboolean     have_sig = (args != NULL && args[0] != '\0') || (returns != NULL && returns[0] != '\0');

		if (lrg_vscript_gi_available ())
		{
#ifdef VSCRIPT_HAVE_GI
			if (have_fn)
			{
				GIBaseInfo *gi = gi_find_callable (fn, NULL);

				if (gi == NULL)
					add_issue (found, LRG_VSCRIPT_SEVERITY_ERROR, gname, node->id, path,
					           "no GI callable '%s'", fn);
				else
					gi_base_info_unref (gi);
			}
			else if (!have_sig)
				add_issue (found, LRG_VSCRIPT_SEVERITY_WARNING, gname, node->id, path, "gi.call has no signature");
#endif
		}
		else if (!have_sig)
			add_issue (found, LRG_VSCRIPT_SEVERITY_WARNING, gname, node->id, path, "gi.call has no signature");
	}
}

static void
validate_links (LrgVscriptProgram *self,
		LrgVscriptGraph   *graph,
		const gchar       *prefix,
		GPtrArray         *found)
{
	guint i;

	for (i = 0; i < graph->links->len; i++)
	{
		LrgVscriptLink       *l = (LrgVscriptLink *) g_ptr_array_index (graph->links, i);
		g_autofree gchar     *path = lrg_ai_priv_index_path (prefix, "links", i);
		LrgVscriptNode       *src = lrg_vscript_graph_get_node (graph, l->from_node);
		LrgVscriptNode       *dst = lrg_vscript_graph_get_node (graph, l->to_node);
		g_autoptr(GPtrArray)  sp = NULL;
		g_autoptr(GPtrArray)  dp = NULL;
		LrgVscriptPin        *outp;
		LrgVscriptPin        *inp;

		if (src == NULL)
			add_issue (found, LRG_VSCRIPT_SEVERITY_ERROR, graph->name, l->from_node, path, "no node '%s'", l->from_node);
		if (dst == NULL)
			add_issue (found, LRG_VSCRIPT_SEVERITY_ERROR, graph->name, l->to_node, path, "no node '%s'", l->to_node);
		if (src == NULL || dst == NULL)
			continue;
		sp = lrg_vscript_program_dup_node_pins (self, graph, src);
		dp = lrg_vscript_program_dup_node_pins (self, graph, dst);
		outp = pin_named (sp, l->from_pin, LRG_VSCRIPT_PIN_OUTPUT);
		inp = pin_named (dp, l->to_pin, LRG_VSCRIPT_PIN_INPUT);
		if (outp == NULL)
			add_issue (found, LRG_VSCRIPT_SEVERITY_ERROR, graph->name, l->from_node, path,
			           "no pin '%s' on node '%s'", l->from_pin, l->from_node);
		if (inp == NULL)
			add_issue (found, LRG_VSCRIPT_SEVERITY_ERROR, graph->name, l->to_node, path,
			           "no pin '%s' on node '%s'", l->to_pin, l->to_node);
		if (outp == NULL || inp == NULL)
			continue;
		if (outp->exec != inp->exec || (!outp->exec && !lrg_vscript_priv_types_compatible (outp->type, inp->type)))
			add_issue (found, LRG_VSCRIPT_SEVERITY_ERROR, graph->name, l->to_node, path,
			           "type mismatch: %s.%s (%s) -> %s.%s (%s)",
			           l->from_node, l->from_pin, outp->type, l->to_node, l->to_pin, inp->type);
		if (!inp->exec && count_pin (graph->links, l->to_node, l->to_pin, TRUE) > 1 &&
		    first_pin (graph->links, i, l->to_node, l->to_pin, TRUE))
			add_issue (found, LRG_VSCRIPT_SEVERITY_ERROR, graph->name, l->to_node, path,
			           "more than one link into %s.%s", l->to_node, l->to_pin);
		if (outp->exec && count_pin (graph->links, l->from_node, l->from_pin, FALSE) > 1 &&
		    first_pin (graph->links, i, l->from_node, l->from_pin, FALSE))
			add_issue (found, LRG_VSCRIPT_SEVERITY_ERROR, graph->name, l->from_node, path,
			           "more than one link from %s.%s", l->from_node, l->from_pin);
		if (!outp->exec && (g_strcmp0 (l->from_node, l->to_node) == 0 ||
		                    data_reaches (self, graph, l->to_node, l->from_node,
		                                  l->from_node, l->from_pin, l->to_node, l->to_pin)))
			add_issue (found, LRG_VSCRIPT_SEVERITY_ERROR, graph->name, l->from_node, path,
			           "data cycle: %s.%s -> %s.%s", l->from_node, l->from_pin, l->to_node, l->to_pin);
	}
}

static void
validate_graph (LrgVscriptProgram *self,
		LrgVscriptGraph   *graph,
		const gchar       *prefix,
		GPtrArray         *found)
{
	guint i;

	for (i = 0; i < graph->nodes->len; i++)
	{
		LrgVscriptNode   *node = (LrgVscriptNode *) g_ptr_array_index (graph->nodes, i);
		g_autofree gchar *path = lrg_ai_priv_index_path (prefix, "nodes", i);

		validate_node (self, graph, node, path, found);
	}
	validate_links (self, graph, prefix, found);
}

static GPtrArray *
bucket_issues (GPtrArray *found)
{
	GPtrArray          *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_vscript_issue_free);
	LrgVscriptSeverity  sev;
	guint               i;

	for (sev = LRG_VSCRIPT_SEVERITY_ERROR; sev >= LRG_VSCRIPT_SEVERITY_INFO; sev--)
	{
		for (i = 0; i < found->len; i++)
		{
			LrgVscriptIssue *issue = (LrgVscriptIssue *) g_ptr_array_index (found, i);

			if (issue->severity == sev)
				g_ptr_array_add (out, issue);
		}
		if (sev == LRG_VSCRIPT_SEVERITY_INFO)
			break;
	}
	g_ptr_array_free (found, TRUE);
	return out;
}

gboolean
lrg_vscript_program_validate (LrgVscriptProgram  *self,
                              GPtrArray         **issues)
{
	GPtrArray *found;
	GPtrArray *ordered;
	gboolean   ok = TRUE;
	guint      i;

	g_return_val_if_fail (LRG_IS_VSCRIPT_PROGRAM (self), FALSE);

	found = g_ptr_array_new ();
	validate_graph (self, self->main_graph, "", found);
	for (i = 0; i < self->subgraphs->len; i++)
	{
		g_autofree gchar *prefix = lrg_ai_priv_index_path ("", "subgraphs", i);

		validate_graph (self, (LrgVscriptGraph *) g_ptr_array_index (self->subgraphs, i), prefix, found);
	}
	for (i = 0; i < found->len; i++)
	{
		LrgVscriptIssue *issue = (LrgVscriptIssue *) g_ptr_array_index (found, i);

		if (issue->severity == LRG_VSCRIPT_SEVERITY_ERROR)
			ok = FALSE;
	}
	ordered = bucket_issues (found);
	if (issues != NULL)
		*issues = ordered;
	else
		g_ptr_array_unref (ordered);
	return ok;
}
