/* lrg-physics-layers.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhysicsLayers: 32 named collision layers and a symmetric collide
 * matrix, plus the physics: block of project.ryaml.
 *
 * The matrix is kept as 32 rows of 32 bits: bit b of rows[a] is set when
 * layers a and b collide. Every write updates the mirrored bit so
 * rows[a] bit b == rows[b] bit a always holds, and the broadphase can
 * test a pair with one AND.
 */

#include "config.h"

#include "lrg-physics-layers.h"
#include "../core/lrg-data-loader.h"

#include <math.h>
#include <string.h>
#include <yaml-glib.h>

struct _LrgPhysicsLayers
{
	GObject parent_instance;

	gchar   *names[LRG_PHYSICS_LAYERS_MAX];  /* NULL = unnamed slot */
	guint32  rows[LRG_PHYSICS_LAYERS_MAX];   /* symmetric bit matrix */
};

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgPhysicsLayers, lrg_physics_layers, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-physics-layers-error-quark, lrg_physics_layers_error)

/* ------------------------------------------------------------------ */
/* Enum types                                                          */
/* ------------------------------------------------------------------ */

GType
lrg_physics_layer_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_PHYSICS_LAYER_DEFAULT, "LRG_PHYSICS_LAYER_DEFAULT", "default" },
			{ LRG_PHYSICS_LAYER_STATIC, "LRG_PHYSICS_LAYER_STATIC", "static" },
			{ LRG_PHYSICS_LAYER_DYNAMIC, "LRG_PHYSICS_LAYER_DYNAMIC", "dynamic" },
			{ LRG_PHYSICS_LAYER_KINEMATIC, "LRG_PHYSICS_LAYER_KINEMATIC", "kinematic" },
			{ LRG_PHYSICS_LAYER_CHARACTER, "LRG_PHYSICS_LAYER_CHARACTER", "character" },
			{ LRG_PHYSICS_LAYER_TRIGGER, "LRG_PHYSICS_LAYER_TRIGGER", "trigger" },
			{ LRG_PHYSICS_LAYER_DEBRIS, "LRG_PHYSICS_LAYER_DEBRIS", "debris" },
			{ LRG_PHYSICS_LAYER_RAGDOLL, "LRG_PHYSICS_LAYER_RAGDOLL", "ragdoll" },
			{ LRG_PHYSICS_LAYER_PROJECTILE, "LRG_PHYSICS_LAYER_PROJECTILE", "projectile" },
			{ 0, NULL, NULL }
		};
		GType type = g_enum_register_static (g_intern_static_string ("LrgPhysicsLayer"), values);

		g_once_init_leave (&type_id, type);
	}

	return (GType) type_id;
}

GType
lrg_physics_layers_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_PHYSICS_LAYERS_ERROR_PARSE, "LRG_PHYSICS_LAYERS_ERROR_PARSE", "parse" },
			{ LRG_PHYSICS_LAYERS_ERROR_FORMAT, "LRG_PHYSICS_LAYERS_ERROR_FORMAT", "format" },
			{ LRG_PHYSICS_LAYERS_ERROR_UNKNOWN_LAYER, "LRG_PHYSICS_LAYERS_ERROR_UNKNOWN_LAYER", "unknown-layer" },
			{ LRG_PHYSICS_LAYERS_ERROR_DUPLICATE, "LRG_PHYSICS_LAYERS_ERROR_DUPLICATE", "duplicate" },
			{ LRG_PHYSICS_LAYERS_ERROR_FULL, "LRG_PHYSICS_LAYERS_ERROR_FULL", "full" },
			{ 0, NULL, NULL }
		};
		GType type = g_enum_register_static (g_intern_static_string ("LrgPhysicsLayersError"), values);

		g_once_init_leave (&type_id, type);
	}

	return (GType) type_id;
}

/* ------------------------------------------------------------------ */
/* Small helpers on a bare table (names + rows)                        */
/* ------------------------------------------------------------------ */

/* The state a load builds before it commits: same layout as the object
 * fields so commit is a swap. */
typedef struct
{
	gchar   *names[LRG_PHYSICS_LAYERS_MAX];
	guint32  rows[LRG_PHYSICS_LAYERS_MAX];
} LayerTable;

static void
table_init (LayerTable *t)
{
	guint i;

	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
	{
		t->names[i] = NULL;
		t->rows[i] = LRG_PHYSICS_LAYERS_ALL;
	}
}

static void
table_clear (LayerTable *t)
{
	guint i;

	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
		g_clear_pointer (&t->names[i], g_free);
}

/* matrix_set: sets cell (a, b) and its mirror in @rows */
static void
matrix_set (guint32  *rows,
            guint     a,
            guint     b,
            gboolean  collides)
{
	if (collides)
	{
		rows[a] |= LRG_PHYSICS_LAYER_BIT (b);
		rows[b] |= LRG_PHYSICS_LAYER_BIT (a);
	}
	else
	{
		rows[a] &= ~LRG_PHYSICS_LAYER_BIT (b);
		rows[b] &= ~LRG_PHYSICS_LAYER_BIT (a);
	}
}

/* parse_index: TRUE when @text is a decimal number below the slot count
 * (it may also be a too-large number: then *out is set beyond range) */
static gboolean
parse_index (const gchar *text,
             guint       *out)
{
	guint64 v;

	if (text == NULL || *text == '\0')
		return FALSE;
	if (!g_ascii_string_to_unsigned (text, 10, 0, G_MAXUINT, &v, NULL))
		return FALSE;
	*out = (guint) v;
	return TRUE;
}

/* name_valid: a usable layer name is non-empty, not "all" and not a
 * number (a number would be ambiguous with an index reference) */
static gboolean
name_valid (const gchar  *name,
            GError      **error)
{
	guint dummy;

	if (name == NULL || *name == '\0')
	{
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		             "Layer name is empty");
		return FALSE;
	}
	if (g_strcmp0 (name, "all") == 0 || parse_index (name, &dummy))
	{
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		             "Layer name '%s' is reserved (\"all\" and numbers name masks and indices)",
		             name);
		return FALSE;
	}
	return TRUE;
}

/* table_find: slot named @name, or -1 */
static gint
table_find (gchar * const *names,
            const gchar   *name)
{
	guint i;

	if (name == NULL)
		return -1;
	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
		if (names[i] != NULL && strcmp (names[i], name) == 0)
			return (gint) i;
	return -1;
}

/* table_resolve: name or decimal index to a slot */
static gint
table_resolve (gchar * const  *names,
               const gchar    *ref,
               GError        **error)
{
	guint idx;
	gint  found;

	if (parse_index (ref, &idx))
	{
		if (idx >= LRG_PHYSICS_LAYERS_MAX)
		{
			g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_UNKNOWN_LAYER,
			             "Layer index %u is out of range (0..%u)", idx,
			             LRG_PHYSICS_LAYERS_MAX - 1u);
			return -1;
		}
		return (gint) idx;
	}

	found = table_find (names, ref);
	if (found < 0)
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_UNKNOWN_LAYER,
		             "Unknown physics layer '%s'", ref != NULL ? ref : "(null)");
	return found;
}

/* emit_changed: one signal per user-visible edit */
static void
emit_changed (LrgPhysicsLayers *self)
{
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* ------------------------------------------------------------------ */
/* GObject                                                             */
/* ------------------------------------------------------------------ */

static void
lrg_physics_layers_finalize (GObject *object)
{
	LrgPhysicsLayers *self = LRG_PHYSICS_LAYERS (object);
	guint             i;

	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
		g_clear_pointer (&self->names[i], g_free);

	G_OBJECT_CLASS (lrg_physics_layers_parent_class)->finalize (object);
}

static void
lrg_physics_layers_class_init (LrgPhysicsLayersClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_physics_layers_finalize;

	/**
	 * LrgPhysicsLayers::changed:
	 * @self: the #LrgPhysicsLayers
	 *
	 * Emitted after a name or matrix cell changed (once per call, and
	 * only when something actually differs). Worlds and editors that
	 * cache rows listen to it.
	 */
	signals[SIGNAL_CHANGED] = g_signal_new ("changed",
	                                        G_TYPE_FROM_CLASS (klass),
	                                        G_SIGNAL_RUN_LAST,
	                                        0, NULL, NULL, NULL,
	                                        G_TYPE_NONE, 0);
}

static void
lrg_physics_layers_init (LrgPhysicsLayers *self)
{
	guint i;

	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
		self->rows[i] = LRG_PHYSICS_LAYERS_ALL;
}

LrgPhysicsLayers *
lrg_physics_layers_new (void)
{
	return g_object_new (LRG_TYPE_PHYSICS_LAYERS, NULL);
}

LrgPhysicsLayers *
lrg_physics_layers_new_default (void)
{
	LrgPhysicsLayers *self = lrg_physics_layers_new ();

	lrg_physics_layers_reset_default (self);
	return self;
}

/* table_equal_to: compares the object against a bare table */
static gboolean
table_equal_to (LrgPhysicsLayers *self,
                gchar * const    *names,
                const guint32    *rows)
{
	guint i;

	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
	{
		if (self->rows[i] != rows[i])
			return FALSE;
		if (g_strcmp0 (self->names[i], names[i]) != 0)
			return FALSE;
	}
	return TRUE;
}

/* commit_table: takes ownership of @t's names, replaces the object's
 * table, and emits changed when it differed */
static void
commit_table (LrgPhysicsLayers *self,
              LayerTable       *t)
{
	gboolean same = table_equal_to (self, t->names, t->rows);
	guint    i;

	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
	{
		g_free (self->names[i]);
		self->names[i] = g_steal_pointer (&t->names[i]);
		self->rows[i] = t->rows[i];
	}
	if (!same)
		emit_changed (self);
}

LrgPhysicsLayers *
lrg_physics_layers_copy (LrgPhysicsLayers *self)
{
	LrgPhysicsLayers *copy;

	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), NULL);

	copy = lrg_physics_layers_new ();
	lrg_physics_layers_assign (copy, self);
	return copy;
}

void
lrg_physics_layers_assign (LrgPhysicsLayers *self,
                           LrgPhysicsLayers *other)
{
	LayerTable t;
	guint      i;

	g_return_if_fail (LRG_IS_PHYSICS_LAYERS (self));
	g_return_if_fail (LRG_IS_PHYSICS_LAYERS (other));

	if (self == other)
		return;
	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
	{
		t.names[i] = g_strdup (other->names[i]);
		t.rows[i] = other->rows[i];
	}
	commit_table (self, &t);
}

gboolean
lrg_physics_layers_equal (LrgPhysicsLayers *self,
                          LrgPhysicsLayers *other)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), FALSE);
	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (other), FALSE);

	return table_equal_to (self, other->names, other->rows);
}

void
lrg_physics_layers_reset (LrgPhysicsLayers *self)
{
	LayerTable t;

	g_return_if_fail (LRG_IS_PHYSICS_LAYERS (self));

	table_init (&t);
	commit_table (self, &t);
}

void
lrg_physics_layers_reset_default (LrgPhysicsLayers *self)
{
	/* The default set: one name per LrgPhysicsLayer value, then the
	 * pairs that should not collide in a typical 3D game. */
	static const gchar * const default_names[] = {
		"default", "static", "dynamic", "kinematic", "character",
		"trigger", "debris", "ragdoll", "projectile"
	};
	static const guint no_collide[][2] = {
		{ LRG_PHYSICS_LAYER_STATIC, LRG_PHYSICS_LAYER_STATIC },
		{ LRG_PHYSICS_LAYER_STATIC, LRG_PHYSICS_LAYER_KINEMATIC },
		{ LRG_PHYSICS_LAYER_KINEMATIC, LRG_PHYSICS_LAYER_KINEMATIC },
		{ LRG_PHYSICS_LAYER_STATIC, LRG_PHYSICS_LAYER_TRIGGER },
		{ LRG_PHYSICS_LAYER_TRIGGER, LRG_PHYSICS_LAYER_TRIGGER },
		{ LRG_PHYSICS_LAYER_CHARACTER, LRG_PHYSICS_LAYER_DEBRIS },
		{ LRG_PHYSICS_LAYER_TRIGGER, LRG_PHYSICS_LAYER_DEBRIS },
		{ LRG_PHYSICS_LAYER_DEBRIS, LRG_PHYSICS_LAYER_DEBRIS },
		{ LRG_PHYSICS_LAYER_DEBRIS, LRG_PHYSICS_LAYER_PROJECTILE }
	};
	LayerTable t;
	guint      i;

	g_return_if_fail (LRG_IS_PHYSICS_LAYERS (self));

	table_init (&t);
	for (i = 0; i < G_N_ELEMENTS (default_names); i++)
		t.names[i] = g_strdup (default_names[i]);
	for (i = 0; i < G_N_ELEMENTS (no_collide); i++)
		matrix_set (t.rows, no_collide[i][0], no_collide[i][1], FALSE);
	commit_table (self, &t);
}

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

gboolean
lrg_physics_layers_set_name (LrgPhysicsLayers  *self,
                             guint              index,
                             const gchar       *name,
                             GError           **error)
{
	gint other;

	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (index >= LRG_PHYSICS_LAYERS_MAX)
	{
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_UNKNOWN_LAYER,
		             "Layer index %u is out of range (0..%u)", index,
		             LRG_PHYSICS_LAYERS_MAX - 1u);
		return FALSE;
	}

	/* Unnaming a slot */
	if (name == NULL || *name == '\0')
	{
		if (self->names[index] != NULL)
		{
			g_clear_pointer (&self->names[index], g_free);
			emit_changed (self);
		}
		return TRUE;
	}

	if (!name_valid (name, error))
		return FALSE;
	other = table_find (self->names, name);
	if (other == (gint) index)
		return TRUE;
	if (other >= 0)
	{
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_DUPLICATE,
		             "Layer name '%s' is already used by slot %d", name, other);
		return FALSE;
	}

	g_free (self->names[index]);
	self->names[index] = g_strdup (name);
	emit_changed (self);
	return TRUE;
}

const gchar *
lrg_physics_layers_get_name (LrgPhysicsLayers *self,
                             guint             index)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), NULL);

	if (index >= LRG_PHYSICS_LAYERS_MAX)
		return NULL;
	return self->names[index];
}

gint
lrg_physics_layers_add (LrgPhysicsLayers  *self,
                        const gchar       *name,
                        GError           **error)
{
	guint i;

	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), -1);
	g_return_val_if_fail (error == NULL || *error == NULL, -1);

	if (!name_valid (name, error))
		return -1;
	if (table_find (self->names, name) >= 0)
	{
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_DUPLICATE,
		             "Layer name '%s' is already used", name);
		return -1;
	}
	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
	{
		if (self->names[i] == NULL)
		{
			self->names[i] = g_strdup (name);
			emit_changed (self);
			return (gint) i;
		}
	}
	g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FULL,
	             "All %u physics layer slots are named", LRG_PHYSICS_LAYERS_MAX);
	return -1;
}

gint
lrg_physics_layers_find (LrgPhysicsLayers *self,
                         const gchar      *name)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), -1);

	return table_find (self->names, name);
}

gint
lrg_physics_layers_resolve (LrgPhysicsLayers  *self,
                            const gchar       *name_or_index,
                            GError           **error)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), -1);
	g_return_val_if_fail (error == NULL || *error == NULL, -1);

	return table_resolve (self->names, name_or_index, error);
}

guint
lrg_physics_layers_get_count (LrgPhysicsLayers *self)
{
	guint i;

	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), 0);

	for (i = LRG_PHYSICS_LAYERS_MAX; i > 0; i--)
		if (self->names[i - 1] != NULL)
			return i;
	return 0;
}

/* ------------------------------------------------------------------ */
/* Matrix                                                              */
/* ------------------------------------------------------------------ */

void
lrg_physics_layers_set_collides (LrgPhysicsLayers *self,
                                 guint             a,
                                 guint             b,
                                 gboolean          collides)
{
	guint32 before;

	g_return_if_fail (LRG_IS_PHYSICS_LAYERS (self));
	g_return_if_fail (a < LRG_PHYSICS_LAYERS_MAX && b < LRG_PHYSICS_LAYERS_MAX);

	before = self->rows[a];
	matrix_set (self->rows, a, b, collides);
	if (self->rows[a] != before)
		emit_changed (self);
}

gboolean
lrg_physics_layers_get_collides (LrgPhysicsLayers *self,
                                 guint             a,
                                 guint             b)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), FALSE);

	if (a >= LRG_PHYSICS_LAYERS_MAX || b >= LRG_PHYSICS_LAYERS_MAX)
		return FALSE;
	return (self->rows[a] & LRG_PHYSICS_LAYER_BIT (b)) != 0;
}

guint32
lrg_physics_layers_get_collide_mask (LrgPhysicsLayers *self,
                                     guint             layer)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), 0);

	if (layer >= LRG_PHYSICS_LAYERS_MAX)
		return 0;
	return self->rows[layer];
}

void
lrg_physics_layers_set_collide_mask (LrgPhysicsLayers *self,
                                     guint             layer,
                                     guint32           mask)
{
	guint32 before[LRG_PHYSICS_LAYERS_MAX];
	guint   i;

	g_return_if_fail (LRG_IS_PHYSICS_LAYERS (self));
	g_return_if_fail (layer < LRG_PHYSICS_LAYERS_MAX);

	memcpy (before, self->rows, sizeof (before));
	/* Writing the row through matrix_set updates the column too */
	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
		matrix_set (self->rows, layer, i, (mask & LRG_PHYSICS_LAYER_BIT (i)) != 0);
	if (memcmp (before, self->rows, sizeof (before)) != 0)
		emit_changed (self);
}

gboolean
lrg_physics_layers_should_collide (LrgPhysicsLayers *self,
                                   guint             layer_a,
                                   guint32           mask_a,
                                   guint             layer_b,
                                   guint32           mask_b)
{
	g_return_val_if_fail (self == NULL || LRG_IS_PHYSICS_LAYERS (self), FALSE);

	if (layer_a >= LRG_PHYSICS_LAYERS_MAX || layer_b >= LRG_PHYSICS_LAYERS_MAX)
		return FALSE;
	if ((mask_a & LRG_PHYSICS_LAYER_BIT (layer_b)) == 0
	    || (mask_b & LRG_PHYSICS_LAYER_BIT (layer_a)) == 0)
		return FALSE;
	if (self != NULL && (self->rows[layer_a] & LRG_PHYSICS_LAYER_BIT (layer_b)) == 0)
		return FALSE;
	return TRUE;
}

/* ------------------------------------------------------------------ */
/* Masks                                                               */
/* ------------------------------------------------------------------ */

gboolean
lrg_physics_layers_mask_from_names (LrgPhysicsLayers    *self,
                                    const gchar * const *names,
                                    guint32             *out_mask,
                                    GError             **error)
{
	guint32 mask = 0;
	guint   i;

	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), FALSE);
	g_return_val_if_fail (out_mask != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	for (i = 0; names != NULL && names[i] != NULL; i++)
	{
		gint idx;

		if (strcmp (names[i], "all") == 0)
		{
			mask = LRG_PHYSICS_LAYERS_ALL;
			continue;
		}
		idx = table_resolve (self->names, names[i], error);
		if (idx < 0)
			return FALSE;
		mask |= LRG_PHYSICS_LAYER_BIT (idx);
	}
	*out_mask = mask;
	return TRUE;
}

/* layer_ref: the name of a slot, or its decimal index when unnamed */
static gchar *
layer_ref (gchar * const *names,
           guint          index)
{
	if (names[index] != NULL)
		return g_strdup (names[index]);
	return g_strdup_printf ("%u", index);
}

GStrv
lrg_physics_layers_mask_to_names (LrgPhysicsLayers *self,
                                  guint32           mask)
{
	g_autoptr(GStrvBuilder) builder = NULL;
	guint                   i;

	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), NULL);

	builder = g_strv_builder_new ();
	if (mask == LRG_PHYSICS_LAYERS_ALL)
	{
		g_strv_builder_add (builder, "all");
		return g_strv_builder_end (builder);
	}
	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
		if ((mask & LRG_PHYSICS_LAYER_BIT (i)) != 0)
			g_strv_builder_take (builder, layer_ref (self->names, i));
	return g_strv_builder_end (builder);
}

/* ------------------------------------------------------------------ */
/* YAML: physics: block                                                */
/*                                                                     */
/*   physics:                                                          */
/*     layers: [default, static, "", trigger]   # position = index     */
/*     no_collide:                                                     */
/*       - [static, static]                                            */
/*       - [trigger, 2]           # names or indices                   */
/* ------------------------------------------------------------------ */

/* scalar_text: the text of a scalar node; "" for null; NULL for a
 * mapping or sequence */
static const gchar *
scalar_text (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) == YAML_NODE_NULL)
		return "";
	if (yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node) != NULL ? yaml_node_get_scalar (node) : "";
}

/* load_layers: fills t->names from the layers: sequence */
static gboolean
load_layers (LayerTable  *t,
             YamlNode    *node,
             GError     **error)
{
	YamlSequence *seq;
	guint         i;
	guint         n;

	if (yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
	{
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		             "physics.layers must be a sequence of names");
		return FALSE;
	}
	seq = yaml_node_get_sequence (node);
	n = yaml_sequence_get_length (seq);
	if (n > LRG_PHYSICS_LAYERS_MAX)
	{
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		             "physics.layers has %u entries; at most %u are allowed",
		             n, LRG_PHYSICS_LAYERS_MAX);
		return FALSE;
	}

	for (i = 0; i < n; i++)
	{
		const gchar *name = scalar_text (yaml_sequence_get_element (seq, i));

		if (name == NULL)
		{
			g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
			             "physics.layers[%u] is not a name", i);
			return FALSE;
		}
		if (*name == '\0')
			continue;   /* unnamed slot kept as a gap */
		if (!name_valid (name, error))
			return FALSE;
		if (table_find (t->names, name) >= 0)
		{
			g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_DUPLICATE,
			             "physics.layers names '%s' twice", name);
			return FALSE;
		}
		t->names[i] = g_strdup (name);
	}
	return TRUE;
}

/* load_no_collide: clears matrix cells from the no_collide: pair list */
static gboolean
load_no_collide (LayerTable  *t,
                 YamlNode    *node,
                 GError     **error)
{
	YamlSequence *seq;
	guint         i;
	guint         n;

	if (yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
	{
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		             "physics.no_collide must be a sequence of [layer, layer] pairs");
		return FALSE;
	}
	seq = yaml_node_get_sequence (node);
	n = yaml_sequence_get_length (seq);

	for (i = 0; i < n; i++)
	{
		YamlNode     *pair_node = yaml_sequence_get_element (seq, i);
		YamlSequence *pair;
		const gchar  *ra;
		const gchar  *rb;
		gint          a;
		gint          b;

		if (pair_node == NULL || yaml_node_get_node_type (pair_node) != YAML_NODE_SEQUENCE
		    || yaml_sequence_get_length (yaml_node_get_sequence (pair_node)) != 2)
		{
			g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
			             "physics.no_collide[%u] must be a [layer, layer] pair", i);
			return FALSE;
		}
		pair = yaml_node_get_sequence (pair_node);
		ra = scalar_text (yaml_sequence_get_element (pair, 0));
		rb = scalar_text (yaml_sequence_get_element (pair, 1));
		if (ra == NULL || rb == NULL || *ra == '\0' || *rb == '\0')
		{
			g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
			             "physics.no_collide[%u] entries must be layer names or indices", i);
			return FALSE;
		}
		a = table_resolve (t->names, ra, error);
		if (a < 0)
			return FALSE;
		b = table_resolve (t->names, rb, error);
		if (b < 0)
			return FALSE;
		matrix_set (t->rows, (guint) a, (guint) b, FALSE);
	}
	return TRUE;
}

gboolean
lrg_physics_layers_load_yaml (LrgPhysicsLayers  *self,
                              YamlNode          *node,
                              GError           **error)
{
	LayerTable   t;
	YamlMapping *map;
	YamlNode    *member;

	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	table_init (&t);

	/* No block (or an empty "physics:") means the default-constructed
	 * table: nothing named, everything collides */
	if (node == NULL || yaml_node_get_node_type (node) == YAML_NODE_NULL)
	{
		commit_table (self, &t);
		return TRUE;
	}
	if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		             "physics: must be a mapping");
		return FALSE;
	}
	map = yaml_node_get_mapping (node);

	/* Names first: no_collide may refer to them */
	member = yaml_mapping_get_member (map, "layers");
	if (member != NULL && yaml_node_get_node_type (member) != YAML_NODE_NULL
	    && !load_layers (&t, member, error))
	{
		table_clear (&t);
		return FALSE;
	}
	member = yaml_mapping_get_member (map, "no_collide");
	if (member != NULL && yaml_node_get_node_type (member) != YAML_NODE_NULL
	    && !load_no_collide (&t, member, error))
	{
		table_clear (&t);
		return FALSE;
	}

	commit_table (self, &t);
	return TRUE;
}

YamlNode *
lrg_physics_layers_find_block (YamlNode *root)
{
	YamlMapping *map;
	YamlNode    *block;
	YamlNode    *project;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
		return NULL;
	map = yaml_node_get_mapping (root);

	block = yaml_mapping_get_member (map, "physics");
	if (block != NULL)
		return block;

	project = yaml_mapping_get_member (map, "project");
	if (project != NULL && yaml_node_get_node_type (project) == YAML_NODE_MAPPING)
		return yaml_mapping_get_member (yaml_node_get_mapping (project), "physics");
	return NULL;
}

void
lrg_physics_layers_write_yaml (LrgPhysicsLayers *self,
                               YamlMapping      *map)
{
	g_autoptr(YamlSequence) layers = NULL;
	g_autoptr(YamlSequence) pairs = NULL;
	guint                   count;
	guint                   a;
	guint                   b;

	g_return_if_fail (LRG_IS_PHYSICS_LAYERS (self));
	g_return_if_fail (map != NULL);

	/* layers: one entry per slot up to the highest named one, "" gaps */
	layers = yaml_sequence_new ();
	count = lrg_physics_layers_get_count (self);
	for (a = 0; a < count; a++)
		yaml_sequence_add_string_element (layers, self->names[a] != NULL ? self->names[a] : "");

	/* no_collide: every cleared cell of the upper triangle, row-major,
	 * as names (or indices for unnamed slots) */
	pairs = yaml_sequence_new ();
	for (a = 0; a < LRG_PHYSICS_LAYERS_MAX; a++)
	{
		for (b = a; b < LRG_PHYSICS_LAYERS_MAX; b++)
		{
			g_autoptr(YamlSequence) pair = NULL;

			if ((self->rows[a] & LRG_PHYSICS_LAYER_BIT (b)) != 0)
				continue;
			pair = yaml_sequence_new ();
			if (self->names[a] != NULL)
				yaml_sequence_add_string_element (pair, self->names[a]);
			else
				yaml_sequence_add_int_element (pair, (gint64) a);
			if (self->names[b] != NULL)
				yaml_sequence_add_string_element (pair, self->names[b]);
			else
				yaml_sequence_add_int_element (pair, (gint64) b);
			yaml_sequence_add_sequence_element (pairs, pair);
		}
	}

	yaml_mapping_set_sequence_member (map, "layers", layers);
	yaml_mapping_set_sequence_member (map, "no_collide", pairs);
}

gboolean
lrg_physics_layers_load_from_data (LrgPhysicsLayers  *self,
                                   const gchar       *data,
                                   gssize             length,
                                   GError           **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GError)     local = NULL;
	YamlNode             *root;
	YamlNode             *block;

	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_PARSE,
		             "Cannot parse physics layers YAML: %s", local->message);
		return FALSE;
	}
	root = yaml_parser_get_root (parser);

	/* A project file (physics: at the root or under project:), or the
	 * block itself (a mapping holding layers: or no_collide:) */
	block = lrg_physics_layers_find_block (root);
	if (block == NULL && root != NULL && yaml_node_get_node_type (root) == YAML_NODE_MAPPING)
	{
		YamlMapping *map = yaml_node_get_mapping (root);

		if (yaml_mapping_has_member (map, "layers") || yaml_mapping_has_member (map, "no_collide"))
			block = root;
	}
	return lrg_physics_layers_load_yaml (self, block, error);
}

gboolean
lrg_physics_layers_load_from_file (LrgPhysicsLayers  *self,
                                   const gchar       *path,
                                   GError           **error)
{
	g_autofree gchar *data = NULL;
	gsize             length = 0;

	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_file_get_contents (path, &data, &length, error))
		return FALSE;
	return lrg_physics_layers_load_from_data (self, data, (gssize) length, error);
}

gchar *
lrg_physics_layers_to_data (LrgPhysicsLayers  *self,
                            GError           **error)
{
	g_autoptr(YamlMapping) root_map = NULL;
	g_autoptr(YamlMapping) physics = NULL;
	g_autoptr(YamlNode)    root = NULL;

	g_return_val_if_fail (LRG_IS_PHYSICS_LAYERS (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	root_map = yaml_mapping_new ();
	physics = yaml_mapping_new ();
	lrg_physics_layers_write_yaml (self, physics);
	yaml_mapping_set_mapping_member (root_map, "physics", physics);
	root = yaml_node_new_mapping (root_map);

	return lrg_data_yaml_to_data (root, NULL, error);
}

/* ================================================================== */
/* LrgPhysicsSettings                                                  */
/* ================================================================== */
/*
 * The world settings of the physics: block, next to layers/no_collide:
 *
 *     physics:
 *       gravity: [0, -9.81, 0]
 *       fixed_dt: 0.0166667
 *       max_substeps: 4
 *       iterations: 10
 *       sleep_threshold: 0.0009
 *       sleep_substeps: 30
 *
 * Reading is lenient about what is missing (defaults) and strict about
 * what is there (a value of the wrong shape or out of range fails the
 * load, so a typo is reported instead of silently simulating with a
 * default). Writing replaces only these six keys.
 */

#define PROJECT_MANIFEST "project.ryaml"

G_DEFINE_BOXED_TYPE (LrgPhysicsSettings, lrg_physics_settings,
                     lrg_physics_settings_copy, lrg_physics_settings_free)

LrgPhysicsSettings *
lrg_physics_settings_new (void)
{
	LrgPhysicsSettings *self = g_new0 (LrgPhysicsSettings, 1);

	lrg_physics_settings_init_default (self);
	return self;
}

LrgPhysicsSettings *
lrg_physics_settings_copy (const LrgPhysicsSettings *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return (LrgPhysicsSettings *) g_memdup2 (self, sizeof (LrgPhysicsSettings));
}

void
lrg_physics_settings_free (LrgPhysicsSettings *self)
{
	g_free (self);
}

void
lrg_physics_settings_init_default (LrgPhysicsSettings *self)
{
	g_return_if_fail (self != NULL);

	/* the step limits and sleep values match lrg_dynamics_world_init()
	 * and the contact solver's default velocity iterations; gravity is
	 * the Y-up Earth value a game wants (a bare world has none) */
	memset (self, 0, sizeof (LrgPhysicsSettings));
	self->gravity[0] = 0.0f;
	self->gravity[1] = -9.81f;
	self->gravity[2] = 0.0f;
	self->fixed_dt = 1.0f / 60.0f;
	self->max_substeps = 4;
	self->iterations = 10;
	self->sleep_threshold = 9.0e-4f;
	self->sleep_substeps = 30;
}

gboolean
lrg_physics_settings_equal (const LrgPhysicsSettings *a,
                            const LrgPhysicsSettings *b)
{
	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	/* field by field: the reserved tail is not compared */
	return a->gravity[0] == b->gravity[0] && a->gravity[1] == b->gravity[1] && a->gravity[2] == b->gravity[2]
	       && a->fixed_dt == b->fixed_dt && a->max_substeps == b->max_substeps
	       && a->iterations == b->iterations && a->sleep_threshold == b->sleep_threshold
	       && a->sleep_substeps == b->sleep_substeps;
}

gboolean
lrg_physics_settings_validate (const LrgPhysicsSettings  *self,
                               GError                   **error)
{
	g_return_val_if_fail (self != NULL, FALSE);

	if (!isfinite (self->gravity[0]) || !isfinite (self->gravity[1]) || !isfinite (self->gravity[2]))
	{
		g_set_error_literal (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		                     "physics gravity must be finite");
		return FALSE;
	}
	/* written so a NaN fails too */
	if (!(self->fixed_dt > 0.0f && self->fixed_dt <= 0.1f))
	{
		g_set_error_literal (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		                     "physics fixed_dt must be in (0, 0.1] seconds");
		return FALSE;
	}
	if (self->max_substeps < 1 || self->max_substeps > LRG_PHYSICS_SETTINGS_MAX_SUBSTEPS)
	{
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		             "physics max_substeps must be 1 to %u", (guint) LRG_PHYSICS_SETTINGS_MAX_SUBSTEPS);
		return FALSE;
	}
	if (self->iterations < 1 || self->iterations > LRG_PHYSICS_SETTINGS_MAX_ITERATIONS)
	{
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		             "physics iterations must be 1 to %u", (guint) LRG_PHYSICS_SETTINGS_MAX_ITERATIONS);
		return FALSE;
	}
	if (!(self->sleep_threshold >= 0.0f) || !isfinite (self->sleep_threshold))
	{
		g_set_error_literal (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		                     "physics sleep_threshold must be zero or positive");
		return FALSE;
	}
	if (self->sleep_substeps < 1)
	{
		g_set_error_literal (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		                     "physics sleep_substeps must be at least 1");
		return FALSE;
	}
	return TRUE;
}

/* read_number: the number in member @key of @map into @out. A missing
 * member leaves @out alone and succeeds; anything but a numeric scalar
 * fails with FORMAT. */
static gboolean
read_number (YamlMapping  *map,
             const gchar  *key,
             gdouble      *out,
             GError      **error)
{
	YamlNode    *node = yaml_mapping_get_member (map, key);
	const gchar *text;
	gchar       *end = NULL;
	gdouble      value;

	if (node == NULL || yaml_node_get_node_type (node) == YAML_NODE_NULL)
		return TRUE;
	text = scalar_text (node);
	if (text != NULL && *text != '\0')
	{
		value = g_ascii_strtod (text, &end);
		if (end != NULL && end != text && *end == '\0')
		{
			*out = value;
			return TRUE;
		}
	}
	g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
	             "physics %s is not a number", key);
	return FALSE;
}

/* read_count: like read_number, for a non-negative whole number */
static gboolean
read_count (YamlMapping  *map,
            const gchar  *key,
            guint        *out,
            GError      **error)
{
	gdouble value = (gdouble) *out;

	if (!read_number (map, key, &value, error))
		return FALSE;
	if (value < 0.0 || value > (gdouble) G_MAXUINT || value != floor (value))
	{
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		             "physics %s must be a whole number", key);
		return FALSE;
	}
	*out = (guint) value;
	return TRUE;
}

gboolean
lrg_physics_settings_load_yaml (LrgPhysicsSettings  *self,
                                YamlNode            *node,
                                GError             **error)
{
	LrgPhysicsSettings  s;
	YamlMapping        *map;
	YamlNode           *gravity;
	gdouble             value;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* everything starts at its default, so missing keys need no code */
	lrg_physics_settings_init_default (&s);
	if (node == NULL || yaml_node_get_node_type (node) == YAML_NODE_NULL)
	{
		*self = s;
		return TRUE;
	}
	if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		                     "the physics: block is not a mapping");
		return FALSE;
	}
	map = yaml_node_get_mapping (node);

	/* gravity: a three-number sequence */
	gravity = yaml_mapping_get_member (map, "gravity");
	if (gravity != NULL && yaml_node_get_node_type (gravity) != YAML_NODE_NULL)
	{
		YamlSequence *seq;
		guint         k;

		if (yaml_node_get_node_type (gravity) != YAML_NODE_SEQUENCE
		    || yaml_sequence_get_length (yaml_node_get_sequence (gravity)) != 3)
		{
			g_set_error_literal (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
			                     "physics gravity must be a sequence of three numbers");
			return FALSE;
		}
		seq = yaml_node_get_sequence (gravity);
		for (k = 0; k < 3; k++)
		{
			const gchar *text = scalar_text (yaml_sequence_get_element (seq, k));
			gchar       *end = NULL;

			value = text != NULL ? g_ascii_strtod (text, &end) : 0.0;
			if (text == NULL || *text == '\0' || end == text || *end != '\0')
			{
				g_set_error_literal (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
				                     "physics gravity must be a sequence of three numbers");
				return FALSE;
			}
			s.gravity[k] = (gfloat) value;
		}
	}

	/* the scalar keys */
	value = (gdouble) s.fixed_dt;
	if (!read_number (map, "fixed_dt", &value, error))
		return FALSE;
	s.fixed_dt = (gfloat) value;
	value = (gdouble) s.sleep_threshold;
	if (!read_number (map, "sleep_threshold", &value, error))
		return FALSE;
	s.sleep_threshold = (gfloat) value;
	if (!read_count (map, "max_substeps", &s.max_substeps, error)
	    || !read_count (map, "iterations", &s.iterations, error)
	    || !read_count (map, "sleep_substeps", &s.sleep_substeps, error))
		return FALSE;

	/* ranges last, so the message names the bad field */
	if (!lrg_physics_settings_validate (&s, error))
		return FALSE;
	*self = s;
	return TRUE;
}

/* float_node: a plain scalar holding the shortest decimal text that
 * reads back as @value (6 to 9 significant digits, C locale), so
 * -3.7f is written "-3.7" and not "-3.70000005" */
static YamlNode *
float_node (gfloat value)
{
	gchar  buf[G_ASCII_DTOSTR_BUF_SIZE];
	guint  precision;

	for (precision = 6; precision <= 9; precision++)
	{
		gchar format[8];

		g_snprintf (format, sizeof (format), "%%.%ug", precision);
		g_ascii_formatd (buf, sizeof (buf), format, (gdouble) value);
		if ((gfloat) g_ascii_strtod (buf, NULL) == value)
			break;
	}
	return yaml_node_new_scalar (buf, YAML_SCALAR_STYLE_PLAIN);
}

void
lrg_physics_settings_write_yaml (const LrgPhysicsSettings *self,
                                 YamlMapping              *map)
{
	g_autoptr(YamlSequence) gravity = NULL;
	guint                   k;

	g_return_if_fail (self != NULL);
	g_return_if_fail (map != NULL);

	gravity = yaml_sequence_new ();
	for (k = 0; k < 3; k++)
	{
		g_autoptr(YamlNode) n = float_node (self->gravity[k]);

		yaml_sequence_add_element (gravity, n);
	}
	yaml_mapping_set_sequence_member (map, "gravity", gravity);
	{
		g_autoptr(YamlNode) fixed_dt = float_node (self->fixed_dt);
		g_autoptr(YamlNode) sleep = float_node (self->sleep_threshold);

		yaml_mapping_set_member (map, "fixed_dt", fixed_dt);
		yaml_mapping_set_int_member (map, "max_substeps", (gint64) self->max_substeps);
		yaml_mapping_set_int_member (map, "iterations", (gint64) self->iterations);
		yaml_mapping_set_member (map, "sleep_threshold", sleep);
		yaml_mapping_set_int_member (map, "sleep_substeps", (gint64) self->sleep_substeps);
	}
}

gboolean
lrg_physics_settings_load_from_data (LrgPhysicsSettings  *self,
                                     const gchar         *data,
                                     gssize               length,
                                     GError             **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GError)     local = NULL;
	YamlNode             *root;
	YamlNode             *block;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_PARSE,
		             "Cannot parse physics settings YAML: %s", local->message);
		return FALSE;
	}
	root = yaml_parser_get_root (parser);

	/* a project file (physics: at the root or under project:), or the
	 * block itself (a mapping without project:/physics: keys) */
	block = lrg_physics_layers_find_block (root);
	if (block == NULL && root != NULL && yaml_node_get_node_type (root) == YAML_NODE_MAPPING
	    && !yaml_mapping_has_member (yaml_node_get_mapping (root), "project"))
		block = root;
	return lrg_physics_settings_load_yaml (self, block, error);
}

gboolean
lrg_physics_settings_load_from_file (LrgPhysicsSettings  *self,
                                     const gchar         *path,
                                     GError             **error)
{
	g_autofree gchar *data = NULL;
	gsize             length = 0;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_file_get_contents (path, &data, &length, error))
		return FALSE;
	return lrg_physics_settings_load_from_data (self, data, (gssize) length, error);
}

gchar *
lrg_physics_settings_write_data (const LrgPhysicsSettings  *self,
                                 LrgPhysicsLayers          *layers,
                                 const gchar               *data,
                                 gssize                     length,
                                 GError                   **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(YamlNode)   root = NULL;
	g_autoptr(GError)     local = NULL;
	YamlNode             *block;
	YamlMapping          *physics;

	g_return_val_if_fail (self != NULL, NULL);
	g_return_val_if_fail (layers == NULL || LRG_IS_PHYSICS_LAYERS (layers), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!lrg_physics_settings_validate (self, error))
		return NULL;

	/* a mutable copy of the current document (parsed trees are sealed),
	 * or a fresh root mapping */
	if (data != NULL)
	{
		parser = yaml_parser_new ();
		if (!yaml_parser_load_from_data (parser, data, length, &local))
		{
			g_set_error (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_PARSE,
			             "Cannot parse the project file: %s", local->message);
			return NULL;
		}
		if (yaml_parser_get_root (parser) != NULL)
			root = yaml_node_copy (yaml_parser_get_root (parser));
	}
	if (root == NULL || yaml_node_get_node_type (root) == YAML_NODE_NULL)
	{
		g_autoptr(YamlMapping) empty = yaml_mapping_new ();

		g_clear_pointer (&root, yaml_node_unref);
		root = yaml_node_new_mapping (empty);
	}
	if (yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		                     "the project file's root is not a mapping");
		return NULL;
	}

	/* the block where it is (root or nested), or a new one at the root */
	block = lrg_physics_layers_find_block (root);
	if (block == NULL || yaml_node_get_node_type (block) == YAML_NODE_NULL)
	{
		g_autoptr(YamlMapping) fresh = yaml_mapping_new ();

		yaml_mapping_set_mapping_member (yaml_node_get_mapping (root), "physics", fresh);
		block = lrg_physics_layers_find_block (root);
	}
	if (block == NULL || yaml_node_get_node_type (block) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, LRG_PHYSICS_LAYERS_ERROR, LRG_PHYSICS_LAYERS_ERROR_FORMAT,
		                     "the physics: block is not a mapping");
		return NULL;
	}
	physics = yaml_node_get_mapping (block);

	if (layers != NULL)
		lrg_physics_layers_write_yaml (layers, physics);
	lrg_physics_settings_write_yaml (self, physics);
	return lrg_data_yaml_to_data (root, NULL, error);
}

gboolean
lrg_physics_settings_save_to_file (const LrgPhysicsSettings  *self,
                                   LrgPhysicsLayers          *layers,
                                   const gchar               *path,
                                   GError                   **error)
{
	g_autofree gchar *current = NULL;
	g_autofree gchar *text = NULL;
	gsize             length = 0;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* a missing file starts empty; any other read error is reported */
	if (g_file_test (path, G_FILE_TEST_EXISTS)
	    && !g_file_get_contents (path, &current, &length, error))
		return FALSE;
	text = lrg_physics_settings_write_data (self, layers, current, (gssize) length, error);
	if (text == NULL)
		return FALSE;
	return g_file_set_contents (path, text, -1, error);
}

gchar *
lrg_physics_settings_find_project (const gchar *start)
{
	g_autofree gchar *dir = NULL;
	guint             depth;

	if (start == NULL || *start == '\0')
		return NULL;

	/* start at the directory itself, or at a file's directory */
	dir = g_canonicalize_filename (start, NULL);
	if (!g_file_test (dir, G_FILE_TEST_IS_DIR))
	{
		gchar *parent = g_path_get_dirname (dir);

		g_free (dir);
		dir = parent;
	}

	/* up to the filesystem root (bounded, in case of odd paths) */
	for (depth = 0; depth < 64 && dir != NULL; depth++)
	{
		g_autofree gchar *manifest = g_build_filename (dir, PROJECT_MANIFEST, NULL);
		g_autofree gchar *parent = NULL;

		if (g_file_test (manifest, G_FILE_TEST_IS_REGULAR))
			return g_steal_pointer (&manifest);
		parent = g_path_get_dirname (dir);
		if (g_strcmp0 (parent, dir) == 0)
			break;
		g_free (dir);
		dir = g_steal_pointer (&parent);
	}
	return NULL;
}
