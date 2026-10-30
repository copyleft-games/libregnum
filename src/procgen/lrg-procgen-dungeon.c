/* lrg-procgen-dungeon.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A YAML-definable map recipe: generator + post-processing + spawn rules
 * + tile rules. The document format is in docs/modules/procgen/yaml.org.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PROCGEN

#include "lrg-procgen-dungeon.h"
#include "lrg-procgen-wfc.h"
#include "../lrg-log.h"

#include <yaml-glib.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	gchar    *kind;
	guint     count;
	guint     min_distance;
	gboolean  rooms_only;
} SpawnRule;

struct _LrgProcgenDungeon
{
	GObject              parent_instance;

	LrgProcgenGenerator *generator;
	LrgProcgenTileRules *tile_rules;
	GArray              *spawn_rules;   /* SpawnRule */
	guint64              seed;
	guint                cull_min_size;
	gboolean             keep_largest;
	gboolean             connect;
	gboolean             place_start_exit;
};

G_DEFINE_FINAL_TYPE (LrgProcgenDungeon, lrg_procgen_dungeon, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_GENERATOR,
	PROP_SEED,
	PROP_CULL_MIN_SIZE,
	PROP_KEEP_LARGEST,
	PROP_CONNECT,
	PROP_PLACE_START_EXIT,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
spawn_rule_clear (gpointer data)
{
	SpawnRule *rule = data;

	g_clear_pointer (&rule->kind, g_free);
}

static void
lrg_procgen_dungeon_finalize (GObject *object)
{
	LrgProcgenDungeon *self = LRG_PROCGEN_DUNGEON (object);

	g_clear_object (&self->generator);
	g_clear_object (&self->tile_rules);
	g_clear_pointer (&self->spawn_rules, g_array_unref);
	G_OBJECT_CLASS (lrg_procgen_dungeon_parent_class)->finalize (object);
}

static void
lrg_procgen_dungeon_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgProcgenDungeon *self = LRG_PROCGEN_DUNGEON (object);

	switch (prop_id)
	{
	case PROP_GENERATOR:
		g_value_set_object (value, self->generator);
		break;
	case PROP_SEED:
		g_value_set_uint64 (value, self->seed);
		break;
	case PROP_CULL_MIN_SIZE:
		g_value_set_uint (value, self->cull_min_size);
		break;
	case PROP_KEEP_LARGEST:
		g_value_set_boolean (value, self->keep_largest);
		break;
	case PROP_CONNECT:
		g_value_set_boolean (value, self->connect);
		break;
	case PROP_PLACE_START_EXIT:
		g_value_set_boolean (value, self->place_start_exit);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_procgen_dungeon_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgProcgenDungeon *self = LRG_PROCGEN_DUNGEON (object);

	switch (prop_id)
	{
	case PROP_GENERATOR:
		if (g_value_get_object (value) != NULL)
			lrg_procgen_dungeon_set_generator (self, g_value_get_object (value));
		break;
	case PROP_SEED:
		lrg_procgen_dungeon_set_seed (self, g_value_get_uint64 (value));
		break;
	case PROP_CULL_MIN_SIZE:
		lrg_procgen_dungeon_set_cull_min_size (self, g_value_get_uint (value));
		break;
	case PROP_KEEP_LARGEST:
		lrg_procgen_dungeon_set_keep_largest (self, g_value_get_boolean (value));
		break;
	case PROP_CONNECT:
		lrg_procgen_dungeon_set_connect (self, g_value_get_boolean (value));
		break;
	case PROP_PLACE_START_EXIT:
		lrg_procgen_dungeon_set_place_start_exit (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_procgen_dungeon_class_init (LrgProcgenDungeonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_procgen_dungeon_finalize;
	object_class->get_property = lrg_procgen_dungeon_get_property;
	object_class->set_property = lrg_procgen_dungeon_set_property;

	/**
	 * LrgProcgenDungeon:generator:
	 *
	 * The map generator.
	 */
	properties[PROP_GENERATOR] =
		g_param_spec_object ("generator", NULL, NULL, LRG_TYPE_PROCGEN_GENERATOR,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenDungeon:seed:
	 *
	 * The default seed a game may use (lrg_procgen_dungeon_generate()
	 * always takes its seed as an argument).
	 */
	properties[PROP_SEED] =
		g_param_spec_uint64 ("seed", NULL, NULL, 0, G_MAXUINT64, 0,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenDungeon:cull-min-size:
	 *
	 * Regions with fewer cells are filled with wall (0 = off).
	 */
	properties[PROP_CULL_MIN_SIZE] =
		g_param_spec_uint ("cull-min-size", NULL, NULL, 0, G_MAXINT32, 0,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenDungeon:keep-largest:
	 *
	 * Whether only the largest region is kept (instead of connecting).
	 */
	properties[PROP_KEEP_LARGEST] =
		g_param_spec_boolean ("keep-largest", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenDungeon:connect:
	 *
	 * Whether disjoint regions are joined by corridors, guaranteeing a
	 * connected map.
	 */
	properties[PROP_CONNECT] =
		g_param_spec_boolean ("connect", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenDungeon:place-start-exit:
	 *
	 * Whether the start and exit are placed on a farthest pair.
	 */
	properties[PROP_PLACE_START_EXIT] =
		g_param_spec_boolean ("place-start-exit", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_procgen_dungeon_init (LrgProcgenDungeon *self)
{
	self->tile_rules = lrg_procgen_tile_rules_new ();
	self->spawn_rules = g_array_new (FALSE, TRUE, sizeof (SpawnRule));
	g_array_set_clear_func (self->spawn_rules, spawn_rule_clear);
	self->connect = TRUE;
	self->place_start_exit = TRUE;
}

LrgProcgenDungeon *
lrg_procgen_dungeon_new (LrgProcgenGenerator *generator)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GENERATOR (generator), NULL);
	return g_object_new (LRG_TYPE_PROCGEN_DUNGEON, "generator", generator, NULL);
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

LrgProcgenGenerator *
lrg_procgen_dungeon_get_generator (LrgProcgenDungeon *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_DUNGEON (self), NULL);
	return self->generator;
}

void
lrg_procgen_dungeon_set_generator (LrgProcgenDungeon   *self,
                                   LrgProcgenGenerator *generator)
{
	g_return_if_fail (LRG_IS_PROCGEN_DUNGEON (self));
	g_return_if_fail (LRG_IS_PROCGEN_GENERATOR (generator));
	if (g_set_object (&self->generator, generator))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GENERATOR]);
}

LrgProcgenTileRules *
lrg_procgen_dungeon_get_tile_rules (LrgProcgenDungeon *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_DUNGEON (self), NULL);
	return self->tile_rules;
}

guint64
lrg_procgen_dungeon_get_seed (LrgProcgenDungeon *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_DUNGEON (self), 0);
	return self->seed;
}

void
lrg_procgen_dungeon_set_seed (LrgProcgenDungeon *self,
                              guint64            seed)
{
	g_return_if_fail (LRG_IS_PROCGEN_DUNGEON (self));
	if (self->seed == seed)
		return;
	self->seed = seed;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SEED]);
}

guint
lrg_procgen_dungeon_get_cull_min_size (LrgProcgenDungeon *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_DUNGEON (self), 0);
	return self->cull_min_size;
}

void
lrg_procgen_dungeon_set_cull_min_size (LrgProcgenDungeon *self,
                                       guint              size)
{
	g_return_if_fail (LRG_IS_PROCGEN_DUNGEON (self));
	if (self->cull_min_size == size)
		return;
	self->cull_min_size = size;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CULL_MIN_SIZE]);
}

gboolean
lrg_procgen_dungeon_get_keep_largest (LrgProcgenDungeon *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_DUNGEON (self), FALSE);
	return self->keep_largest;
}

void
lrg_procgen_dungeon_set_keep_largest (LrgProcgenDungeon *self,
                                      gboolean           keep)
{
	g_return_if_fail (LRG_IS_PROCGEN_DUNGEON (self));
	keep = keep ? TRUE : FALSE;
	if (self->keep_largest == keep)
		return;
	self->keep_largest = keep;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KEEP_LARGEST]);
}

gboolean
lrg_procgen_dungeon_get_connect (LrgProcgenDungeon *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_DUNGEON (self), FALSE);
	return self->connect;
}

void
lrg_procgen_dungeon_set_connect (LrgProcgenDungeon *self,
                                 gboolean           connect)
{
	g_return_if_fail (LRG_IS_PROCGEN_DUNGEON (self));
	connect = connect ? TRUE : FALSE;
	if (self->connect == connect)
		return;
	self->connect = connect;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONNECT]);
}

gboolean
lrg_procgen_dungeon_get_place_start_exit (LrgProcgenDungeon *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_DUNGEON (self), FALSE);
	return self->place_start_exit;
}

void
lrg_procgen_dungeon_set_place_start_exit (LrgProcgenDungeon *self,
                                          gboolean           place)
{
	g_return_if_fail (LRG_IS_PROCGEN_DUNGEON (self));
	place = place ? TRUE : FALSE;
	if (self->place_start_exit == place)
		return;
	self->place_start_exit = place;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLACE_START_EXIT]);
}

void
lrg_procgen_dungeon_add_spawn_rule (LrgProcgenDungeon *self,
                                    const gchar       *kind,
                                    guint              count,
                                    guint              min_distance,
                                    gboolean           rooms_only)
{
	SpawnRule rule;

	g_return_if_fail (LRG_IS_PROCGEN_DUNGEON (self));
	g_return_if_fail (kind != NULL);

	rule.kind = g_strdup (kind);
	rule.count = count;
	rule.min_distance = min_distance;
	rule.rooms_only = rooms_only ? TRUE : FALSE;
	g_array_append_val (self->spawn_rules, rule);
}

guint
lrg_procgen_dungeon_get_spawn_rule_count (LrgProcgenDungeon *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_DUNGEON (self), 0);
	return self->spawn_rules->len;
}

gboolean
lrg_procgen_dungeon_get_spawn_rule (LrgProcgenDungeon  *self,
                                    guint               index,
                                    const gchar       **out_kind,
                                    guint              *out_count,
                                    guint              *out_min_distance,
                                    gboolean           *out_rooms_only)
{
	SpawnRule *rule;

	g_return_val_if_fail (LRG_IS_PROCGEN_DUNGEON (self), FALSE);
	if (index >= self->spawn_rules->len)
		return FALSE;
	rule = &g_array_index (self->spawn_rules, SpawnRule, index);
	if (out_kind != NULL)
		*out_kind = rule->kind;
	if (out_count != NULL)
		*out_count = rule->count;
	if (out_min_distance != NULL)
		*out_min_distance = rule->min_distance;
	if (out_rooms_only != NULL)
		*out_rooms_only = rule->rooms_only;
	return TRUE;
}

void
lrg_procgen_dungeon_clear_spawn_rules (LrgProcgenDungeon *self)
{
	g_return_if_fail (LRG_IS_PROCGEN_DUNGEON (self));
	g_array_set_size (self->spawn_rules, 0);
}

/* ==========================================================================
 * Generation
 * ========================================================================== */

LrgProcgenGrid *
lrg_procgen_dungeon_generate (LrgProcgenDungeon  *self,
                              guint64             seed,
                              GError            **error)
{
	g_autoptr(LrgRandomStream) rng = NULL;
	g_autoptr(LrgProcgenGrid) grid = NULL;
	guint i;

	g_return_val_if_fail (LRG_IS_PROCGEN_DUNGEON (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* one stream for the whole recipe */
	rng = lrg_random_stream_new (seed, LRG_PROCGEN_STREAM);
	grid = lrg_procgen_generator_generate_with_rng (self->generator, rng, error);
	if (grid == NULL)
		return NULL;

	/* --- post-processing --- */
	if (self->cull_min_size > 0)
		lrg_procgen_grid_cull_regions (grid, self->cull_min_size, LRG_PROCGEN_CELL_WALL);
	if (self->keep_largest)
		lrg_procgen_grid_keep_largest_region (grid, LRG_PROCGEN_CELL_WALL);
	else if (self->connect)
		lrg_procgen_grid_connect_regions (grid, LRG_PROCGEN_CELL_CORRIDOR);
	if (self->place_start_exit)
		lrg_procgen_grid_place_start_exit (grid);

	/* --- spawn points, in rule order --- */
	for (i = 0; i < self->spawn_rules->len; i++)
	{
		SpawnRule *rule = &g_array_index (self->spawn_rules, SpawnRule, i);

		lrg_procgen_grid_place_spawns (grid, rng, rule->kind, rule->count,
		                               rule->min_distance, rule->rooms_only);
	}
	lrg_procgen_grid_label_regions (grid);
	return g_steal_pointer (&grid);
}

LrgTilemapLayer *
lrg_procgen_dungeon_build_layer (LrgProcgenDungeon *self,
                                 LrgProcgenGrid    *grid)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_DUNGEON (self), NULL);
	return lrg_procgen_tile_rules_build_layer (self->tile_rules, grid);
}

/* ==========================================================================
 * YAML: reading
 * ========================================================================== */

/* sets an invalid-config error prefixed with where it happened */
static void G_GNUC_PRINTF (3, 4)
config_error (GError      **error,
              const gchar  *where,
              const gchar  *format,
              ...)
{
	g_autofree gchar *message = NULL;
	va_list args;

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);
	g_set_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG,
	             "%s: %s", where, message);
}

/* the text of a scalar node, or NULL */
static const gchar *
scalar_text (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

/* the mapping of a node, or NULL with an error */
static YamlMapping *
want_mapping (YamlNode     *node,
              const gchar  *where,
              GError      **error)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		config_error (error, where, "expected a mapping");
		return NULL;
	}
	return yaml_node_get_mapping (node);
}

/* the sequence of a node, or NULL with an error */
static YamlSequence *
want_sequence (YamlNode     *node,
               const gchar  *where,
               GError      **error)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
	{
		config_error (error, where, "expected a sequence");
		return NULL;
	}
	return yaml_node_get_sequence (node);
}

/* rejects keys not in the NULL-terminated @allowed list */
static gboolean
check_keys (YamlMapping         *mapping,
            const gchar * const *allowed,
            const gchar         *where,
            GError             **error)
{
	g_autoptr(GList) keys = yaml_mapping_get_members (mapping);
	GList *l;

	for (l = keys; l != NULL; l = l->next)
		if (!g_strv_contains (allowed, l->data))
		{
			config_error (error, where, "unknown key '%s'", (const gchar *) l->data);
			return FALSE;
		}
	return TRUE;
}

static gboolean
parse_uint64 (YamlNode     *node,
              guint64       max,
              const gchar  *where,
              guint64      *out,
              GError      **error)
{
	const gchar *text = scalar_text (node);

	if (text == NULL ||
	    !g_ascii_string_to_unsigned (text, 10, 0, max, out, NULL))
	{
		config_error (error, where, "expected an integer from 0 to %" G_GUINT64_FORMAT, max);
		return FALSE;
	}
	return TRUE;
}

static gboolean
parse_uint (YamlNode     *node,
            const gchar  *where,
            guint        *out,
            GError      **error)
{
	guint64 value;

	if (!parse_uint64 (node, G_MAXUINT, where, &value, error))
		return FALSE;
	*out = (guint) value;
	return TRUE;
}

static gboolean
parse_bool (YamlNode     *node,
            const gchar  *where,
            gboolean     *out,
            GError      **error)
{
	const gchar *text = scalar_text (node);

	if (text != NULL && (g_ascii_strcasecmp (text, "true") == 0 ||
	                     g_ascii_strcasecmp (text, "yes") == 0 ||
	                     g_ascii_strcasecmp (text, "on") == 0 ||
	                     g_strcmp0 (text, "1") == 0))
		*out = TRUE;
	else if (text != NULL && (g_ascii_strcasecmp (text, "false") == 0 ||
	                          g_ascii_strcasecmp (text, "no") == 0 ||
	                          g_ascii_strcasecmp (text, "off") == 0 ||
	                          g_strcmp0 (text, "0") == 0))
		*out = FALSE;
	else
	{
		config_error (error, where, "expected true or false");
		return FALSE;
	}
	return TRUE;
}

/* an enum value by nick (or full name) */
static gboolean
parse_enum (YamlNode     *node,
            GType         type,
            const gchar  *where,
            gint         *out,
            GError      **error)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	const gchar *text = scalar_text (node);
	GEnumValue *value = NULL;

	if (text != NULL)
		value = g_enum_get_value_by_nick (klass, text);
	if (value == NULL && text != NULL)
		value = g_enum_get_value_by_name (klass, text);
	if (value == NULL)
	{
		config_error (error, where, "unknown %s value '%s'", g_type_name (type),
		              text != NULL ? text : "(not a scalar)");
		return FALSE;
	}
	*out = value->value;
	return TRUE;
}

/* a cell tag: an LrgProcgenCell nick or an integer 0..255 */
static gboolean
parse_tag (YamlNode     *node,
           const gchar  *where,
           guint8       *out,
           GError      **error)
{
	const gchar *text = scalar_text (node);
	guint64 value;
	gint nick;

	if (text != NULL && g_ascii_string_to_unsigned (text, 10, 0, 255, &value, NULL))
	{
		*out = (guint8) value;
		return TRUE;
	}
	if (!parse_enum (node, LRG_TYPE_PROCGEN_CELL, where, &nick, error))
		return FALSE;
	*out = (guint8) nick;
	return TRUE;
}

/* sets one generator property from a scalar, validating its range */
static gboolean
set_param (GObject      *object,
           const gchar  *name,
           YamlNode     *node,
           GError      **error)
{
	g_auto(GValue) value = G_VALUE_INIT;
	g_autofree gchar *where = g_strdup_printf ("params.%s", name);
	GParamSpec *pspec;
	GType type;
	const gchar *text;

	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), name);
	if (pspec == NULL || (pspec->flags & G_PARAM_WRITABLE) == 0 ||
	    pspec->owner_type == LRG_TYPE_PROCGEN_GENERATOR)
	{
		config_error (error, where, "%s has no such parameter", G_OBJECT_TYPE_NAME (object));
		return FALSE;
	}
	text = scalar_text (node);
	type = G_PARAM_SPEC_VALUE_TYPE (pspec);
	g_value_init (&value, type);

	if (type == G_TYPE_BOOLEAN)
	{
		gboolean b;

		if (!parse_bool (node, where, &b, error))
			return FALSE;
		g_value_set_boolean (&value, b);
	}
	else if (type == G_TYPE_UINT || type == G_TYPE_UINT64 || type == G_TYPE_ULONG)
	{
		guint64 u;

		if (!parse_uint64 (node, G_MAXUINT64, where, &u, error))
			return FALSE;
		if (type == G_TYPE_UINT64)
			g_value_set_uint64 (&value, u);
		else if (type == G_TYPE_ULONG)
			g_value_set_ulong (&value, (gulong) MIN (u, (guint64) G_MAXULONG));
		else
			g_value_set_uint (&value, (guint) MIN (u, (guint64) G_MAXUINT));
		if (u > G_MAXUINT && type == G_TYPE_UINT)
		{
			config_error (error, where, "value out of range");
			return FALSE;
		}
	}
	else if (type == G_TYPE_INT || type == G_TYPE_INT64 || type == G_TYPE_LONG)
	{
		gint64 i;

		if (text == NULL || !g_ascii_string_to_signed (text, 10, G_MININT64, G_MAXINT64, &i, NULL))
		{
			config_error (error, where, "expected an integer");
			return FALSE;
		}
		if (type == G_TYPE_INT64)
			g_value_set_int64 (&value, i);
		else if (type == G_TYPE_LONG)
			g_value_set_long (&value, (glong) CLAMP (i, G_MINLONG, G_MAXLONG));
		else
			g_value_set_int (&value, (gint) CLAMP (i, G_MININT, G_MAXINT));
	}
	else if (type == G_TYPE_DOUBLE || type == G_TYPE_FLOAT)
	{
		gchar *end = NULL;
		gdouble d = text != NULL ? g_ascii_strtod (text, &end) : 0.0;

		if (text == NULL || end == text || *end != '\0')
		{
			config_error (error, where, "expected a number");
			return FALSE;
		}
		if (type == G_TYPE_DOUBLE)
			g_value_set_double (&value, d);
		else
			g_value_set_float (&value, (gfloat) d);
	}
	else if (type == G_TYPE_STRING)
	{
		if (text == NULL)
		{
			config_error (error, where, "expected a string");
			return FALSE;
		}
		g_value_set_string (&value, text);
	}
	else if (G_TYPE_IS_ENUM (type))
	{
		gint e;

		if (!parse_enum (node, type, where, &e, error))
			return FALSE;
		g_value_set_enum (&value, e);
	}
	else
	{
		config_error (error, where, "parameters of type %s cannot be set from YAML",
		              g_type_name (type));
		return FALSE;
	}

	/* out of the property's range: refuse rather than clamp */
	if (g_param_value_validate (pspec, &value))
	{
		config_error (error, where, "value out of range");
		return FALSE;
	}
	g_object_set_property (object, name, &value);
	return TRUE;
}

/* the wfc: section — sample, tiles, rules */
static gboolean
load_wfc (LrgProcgenWfc  *wfc,
          YamlMapping    *section,
          GError        **error)
{
	static const gchar * const keys[] = { "sample", "tiles", "rules", NULL };
	static const gchar * const tile_keys[] = { "name", "tag", "weight", NULL };
	static const gchar * const rule_keys[] = { "a", "b", "dir", NULL };
	YamlSequence *seq;
	guint i;

	if (!check_keys (section, keys, "wfc", error))
		return FALSE;

	/* --- a sample grid: learn tiles and rules from it --- */
	if (yaml_mapping_has_member (section, "sample"))
	{
		const gchar *text = scalar_text (yaml_mapping_get_member (section, "sample"));
		g_autoptr(LrgProcgenGrid) sample = NULL;
		g_autoptr(GError) local = NULL;

		if (text == NULL)
		{
			config_error (error, "wfc.sample", "expected a block of text");
			return FALSE;
		}
		sample = lrg_procgen_grid_new_from_string (text, &local);
		if (sample == NULL)
		{
			config_error (error, "wfc.sample", "%s", local->message);
			return FALSE;
		}
		lrg_procgen_wfc_learn (wfc, sample);
	}

	/* --- explicit tiles --- */
	if (yaml_mapping_has_member (section, "tiles"))
	{
		if ((seq = want_sequence (yaml_mapping_get_member (section, "tiles"), "wfc.tiles", error)) == NULL)
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			YamlMapping *tile;
			const gchar *name;
			const gchar *weight_text = NULL;
			guint8 tag = LRG_PROCGEN_CELL_FLOOR;
			gdouble weight = 1.0;

			if ((tile = want_mapping (yaml_sequence_get_element (seq, i), "wfc.tiles[]", error)) == NULL ||
			    !check_keys (tile, tile_keys, "wfc.tiles[]", error))
				return FALSE;
			name = scalar_text (yaml_mapping_get_member (tile, "name"));
			if (name == NULL)
			{
				config_error (error, "wfc.tiles[]", "a tile needs a name");
				return FALSE;
			}
			if (yaml_mapping_has_member (tile, "tag") &&
			    !parse_tag (yaml_mapping_get_member (tile, "tag"), "wfc.tiles[].tag", &tag, error))
				return FALSE;
			if (yaml_mapping_has_member (tile, "weight"))
			{
				gchar *end = NULL;

				weight_text = scalar_text (yaml_mapping_get_member (tile, "weight"));
				weight = weight_text != NULL ? g_ascii_strtod (weight_text, &end) : 0.0;
				if (weight_text == NULL || end == weight_text || *end != '\0' || !(weight > 0.0))
				{
					config_error (error, "wfc.tiles[].weight", "expected a positive number");
					return FALSE;
				}
			}
			if (lrg_procgen_wfc_add_tile (wfc, name, tag, weight) < 0)
			{
				config_error (error, "wfc.tiles[]", "duplicate tile '%s' or more than %d tiles",
				              name, LRG_PROCGEN_WFC_MAX_TILES);
				return FALSE;
			}
		}
	}

	/* --- adjacency rules: b may lie at dir of a --- */
	if (yaml_mapping_has_member (section, "rules"))
	{
		if ((seq = want_sequence (yaml_mapping_get_member (section, "rules"), "wfc.rules", error)) == NULL)
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			YamlMapping *rule;
			const gchar *dir;
			gint a;
			gint b;

			if ((rule = want_mapping (yaml_sequence_get_element (seq, i), "wfc.rules[]", error)) == NULL ||
			    !check_keys (rule, rule_keys, "wfc.rules[]", error))
				return FALSE;
			a = lrg_procgen_wfc_find_tile (wfc, scalar_text (yaml_mapping_get_member (rule, "a")));
			b = lrg_procgen_wfc_find_tile (wfc, scalar_text (yaml_mapping_get_member (rule, "b")));
			dir = scalar_text (yaml_mapping_get_member (rule, "dir"));
			if (a < 0 || b < 0)
			{
				config_error (error, "wfc.rules[]", "a and b must name tiles");
				return FALSE;
			}
			if (dir == NULL || g_strcmp0 (dir, "all") == 0)
			{
				guint d;

				for (d = 0; d < 4; d++)
					lrg_procgen_wfc_allow (wfc, (guint) a, (guint) b, (LrgProcgenDirection) d);
			}
			else if (g_strcmp0 (dir, "horizontal") == 0)
			{
				lrg_procgen_wfc_allow (wfc, (guint) a, (guint) b, LRG_PROCGEN_DIRECTION_RIGHT);
				lrg_procgen_wfc_allow (wfc, (guint) a, (guint) b, LRG_PROCGEN_DIRECTION_LEFT);
			}
			else if (g_strcmp0 (dir, "vertical") == 0)
			{
				lrg_procgen_wfc_allow (wfc, (guint) a, (guint) b, LRG_PROCGEN_DIRECTION_UP);
				lrg_procgen_wfc_allow (wfc, (guint) a, (guint) b, LRG_PROCGEN_DIRECTION_DOWN);
			}
			else
			{
				gint d;

				if (!parse_enum (yaml_mapping_get_member (rule, "dir"), LRG_TYPE_PROCGEN_DIRECTION,
				                 "wfc.rules[].dir", &d, error))
					return FALSE;
				lrg_procgen_wfc_allow (wfc, (guint) a, (guint) b, (LrgProcgenDirection) d);
			}
		}
	}
	return TRUE;
}

/* the post: section */
static gboolean
load_post (LrgProcgenDungeon  *self,
           YamlMapping        *section,
           GError            **error)
{
	static const gchar * const keys[] = { "cull-min-size", "keep-largest", "connect", "start-exit", NULL };
	gboolean b;
	guint u;

	if (!check_keys (section, keys, "post", error))
		return FALSE;
	if (yaml_mapping_has_member (section, "cull-min-size"))
	{
		if (!parse_uint (yaml_mapping_get_member (section, "cull-min-size"), "post.cull-min-size", &u, error))
			return FALSE;
		lrg_procgen_dungeon_set_cull_min_size (self, MIN (u, (guint) G_MAXINT32));
	}
	if (yaml_mapping_has_member (section, "keep-largest"))
	{
		if (!parse_bool (yaml_mapping_get_member (section, "keep-largest"), "post.keep-largest", &b, error))
			return FALSE;
		lrg_procgen_dungeon_set_keep_largest (self, b);
	}
	if (yaml_mapping_has_member (section, "connect"))
	{
		if (!parse_bool (yaml_mapping_get_member (section, "connect"), "post.connect", &b, error))
			return FALSE;
		lrg_procgen_dungeon_set_connect (self, b);
	}
	if (yaml_mapping_has_member (section, "start-exit"))
	{
		if (!parse_bool (yaml_mapping_get_member (section, "start-exit"), "post.start-exit", &b, error))
			return FALSE;
		lrg_procgen_dungeon_set_place_start_exit (self, b);
	}
	return TRUE;
}

/* the spawns: section */
static gboolean
load_spawns (LrgProcgenDungeon  *self,
             YamlSequence       *seq,
             GError            **error)
{
	static const gchar * const keys[] = { "kind", "count", "min-distance", "rooms-only", NULL };
	guint i;

	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		YamlMapping *spawn;
		const gchar *kind;
		guint count = 1;
		guint min_distance = 0;
		gboolean rooms_only = FALSE;

		if ((spawn = want_mapping (yaml_sequence_get_element (seq, i), "spawns[]", error)) == NULL ||
		    !check_keys (spawn, keys, "spawns[]", error))
			return FALSE;
		kind = scalar_text (yaml_mapping_get_member (spawn, "kind"));
		if (kind == NULL || *kind == '\0')
		{
			config_error (error, "spawns[]", "a spawn rule needs a kind");
			return FALSE;
		}
		if ((yaml_mapping_has_member (spawn, "count") &&
		     !parse_uint (yaml_mapping_get_member (spawn, "count"), "spawns[].count", &count, error)) ||
		    (yaml_mapping_has_member (spawn, "min-distance") &&
		     !parse_uint (yaml_mapping_get_member (spawn, "min-distance"), "spawns[].min-distance",
		                  &min_distance, error)) ||
		    (yaml_mapping_has_member (spawn, "rooms-only") &&
		     !parse_bool (yaml_mapping_get_member (spawn, "rooms-only"), "spawns[].rooms-only",
		                  &rooms_only, error)))
			return FALSE;
		lrg_procgen_dungeon_add_spawn_rule (self, kind, count, min_distance, rooms_only);
	}
	return TRUE;
}

/* the tiles: section */
static gboolean
load_tiles (LrgProcgenTileRules  *rules,
            YamlMapping          *section,
            GError              **error)
{
	static const gchar * const keys[] = { "default", "edge-connects", "rules", NULL };
	static const gchar * const rule_keys[] = { "tag", "tile", "autotile", "connects", "variants", NULL };
	static const gchar * const variant_keys[] = { "mask", "tile", NULL };
	YamlSequence *seq;
	guint i;
	guint j;

	if (!check_keys (section, keys, "tiles", error))
		return FALSE;
	if (yaml_mapping_has_member (section, "default"))
	{
		guint tile;

		if (!parse_uint (yaml_mapping_get_member (section, "default"), "tiles.default", &tile, error))
			return FALSE;
		lrg_procgen_tile_rules_set_default_tile (rules, tile);
	}
	if (yaml_mapping_has_member (section, "edge-connects"))
	{
		gboolean b;

		if (!parse_bool (yaml_mapping_get_member (section, "edge-connects"), "tiles.edge-connects", &b, error))
			return FALSE;
		lrg_procgen_tile_rules_set_edge_connects (rules, b);
	}
	if (!yaml_mapping_has_member (section, "rules"))
		return TRUE;

	if ((seq = want_sequence (yaml_mapping_get_member (section, "rules"), "tiles.rules", error)) == NULL)
		return FALSE;
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		YamlMapping *rule;
		guint8 tag;
		guint tile = 0;
		gint autotile = LRG_PROCGEN_AUTOTILE_NONE;

		if ((rule = want_mapping (yaml_sequence_get_element (seq, i), "tiles.rules[]", error)) == NULL ||
		    !check_keys (rule, rule_keys, "tiles.rules[]", error))
			return FALSE;
		if (!parse_tag (yaml_mapping_get_member (rule, "tag"), "tiles.rules[].tag", &tag, error) ||
		    !parse_uint (yaml_mapping_get_member (rule, "tile"), "tiles.rules[].tile", &tile, error))
			return FALSE;
		if (yaml_mapping_has_member (rule, "autotile") &&
		    !parse_enum (yaml_mapping_get_member (rule, "autotile"), LRG_TYPE_PROCGEN_AUTOTILE,
		                 "tiles.rules[].autotile", &autotile, error))
			return FALSE;
		lrg_procgen_tile_rules_set_rule (rules, tag, tile, (LrgProcgenAutotile) autotile);

		/* tags that count as matching neighbours */
		if (yaml_mapping_has_member (rule, "connects"))
		{
			YamlSequence *others;

			if ((others = want_sequence (yaml_mapping_get_member (rule, "connects"),
			                             "tiles.rules[].connects", error)) == NULL)
				return FALSE;
			for (j = 0; j < yaml_sequence_get_length (others); j++)
			{
				guint8 other;

				if (!parse_tag (yaml_sequence_get_element (others, j), "tiles.rules[].connects[]",
				                &other, error))
					return FALSE;
				lrg_procgen_tile_rules_set_connects (rules, tag, other, TRUE);
			}
		}

		/* explicit tiles for some masks */
		if (yaml_mapping_has_member (rule, "variants"))
		{
			YamlSequence *variants;

			if ((variants = want_sequence (yaml_mapping_get_member (rule, "variants"),
			                               "tiles.rules[].variants", error)) == NULL)
				return FALSE;
			for (j = 0; j < yaml_sequence_get_length (variants); j++)
			{
				YamlMapping *variant;
				guint64 mask;
				guint vtile;

				if ((variant = want_mapping (yaml_sequence_get_element (variants, j),
				                             "tiles.rules[].variants[]", error)) == NULL ||
				    !check_keys (variant, variant_keys, "tiles.rules[].variants[]", error) ||
				    !parse_uint64 (yaml_mapping_get_member (variant, "mask"), 255,
				                   "tiles.rules[].variants[].mask", &mask, error) ||
				    !parse_uint (yaml_mapping_get_member (variant, "tile"),
				                 "tiles.rules[].variants[].tile", &vtile, error))
					return FALSE;
				lrg_procgen_tile_rules_set_variant (rules, tag, (guint) mask, vtile);
			}
		}
	}
	return TRUE;
}

LrgProcgenDungeon *
lrg_procgen_dungeon_new_from_data (const gchar  *data,
                                   gssize        length,
                                   GError      **error)
{
	static const gchar * const keys[] = {
		"version", "generator", "width", "height", "seed",
		"params", "wfc", "post", "spawns", "tiles", NULL
	};
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(LrgProcgenDungeon) self = NULL;
	g_autoptr(LrgProcgenGenerator) generator = NULL;
	g_autoptr(GError) local = NULL;
	YamlMapping *root;
	YamlNode *node;
	const gchar *name;
	GType type;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* --- the document --- */
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG,
		             "invalid YAML: %s", local->message);
		return NULL;
	}
	if ((root = want_mapping (yaml_parser_get_root (parser), "document", error)) == NULL ||
	    !check_keys (root, keys, "document", error))
		return NULL;
	if (yaml_mapping_has_member (root, "version"))
	{
		guint64 version;

		if (!parse_uint64 (yaml_mapping_get_member (root, "version"), G_MAXUINT, "version", &version, error))
			return NULL;
		if (version == 0 || version > LRG_PROCGEN_DUNGEON_FORMAT_VERSION)
		{
			config_error (error, "version", "unsupported version %" G_GUINT64_FORMAT, version);
			return NULL;
		}
	}

	/* --- the generator and its size --- */
	name = scalar_text (yaml_mapping_get_member (root, "generator"));
	if (name == NULL)
	{
		config_error (error, "generator", "missing generator name");
		return NULL;
	}
	type = lrg_procgen_generator_type_from_name (name);
	if (type == G_TYPE_INVALID)
	{
		config_error (error, "generator", "unknown generator '%s'", name);
		return NULL;
	}
	generator = g_object_new (type, NULL);
	if ((node = yaml_mapping_get_member (root, "width")) != NULL)
	{
		guint64 w;

		if (!parse_uint64 (node, 4096, "width", &w, error))
			return NULL;
		if (w == 0)
		{
			config_error (error, "width", "must be at least 1");
			return NULL;
		}
		lrg_procgen_generator_set_width (generator, (guint) w);
	}
	if ((node = yaml_mapping_get_member (root, "height")) != NULL)
	{
		guint64 h;

		if (!parse_uint64 (node, 4096, "height", &h, error))
			return NULL;
		if (h == 0)
		{
			config_error (error, "height", "must be at least 1");
			return NULL;
		}
		lrg_procgen_generator_set_height (generator, (guint) h);
	}
	self = lrg_procgen_dungeon_new (generator);
	if ((node = yaml_mapping_get_member (root, "seed")) != NULL)
	{
		guint64 seed;

		if (!parse_uint64 (node, G_MAXUINT64, "seed", &seed, error))
			return NULL;
		lrg_procgen_dungeon_set_seed (self, seed);
	}

	/* --- generator parameters, by property name --- */
	if ((node = yaml_mapping_get_member (root, "params")) != NULL)
	{
		YamlMapping *params;
		g_autoptr(GList) param_keys = NULL;
		GList *l;

		if ((params = want_mapping (node, "params", error)) == NULL)
			return NULL;
		param_keys = yaml_mapping_get_members (params);
		for (l = param_keys; l != NULL; l = l->next)
			if (!set_param (G_OBJECT (generator), l->data, yaml_mapping_get_member (params, l->data), error))
				return NULL;
	}

	/* --- the wave function collapse model --- */
	if ((node = yaml_mapping_get_member (root, "wfc")) != NULL)
	{
		YamlMapping *section;

		if (!LRG_IS_PROCGEN_WFC (generator))
		{
			config_error (error, "wfc", "only the wfc generator takes a wfc section");
			return NULL;
		}
		if ((section = want_mapping (node, "wfc", error)) == NULL ||
		    !load_wfc (LRG_PROCGEN_WFC (generator), section, error))
			return NULL;
	}

	/* --- post-processing, spawn rules, tile rules --- */
	if ((node = yaml_mapping_get_member (root, "post")) != NULL)
	{
		YamlMapping *section;

		if ((section = want_mapping (node, "post", error)) == NULL || !load_post (self, section, error))
			return NULL;
	}
	if ((node = yaml_mapping_get_member (root, "spawns")) != NULL)
	{
		YamlSequence *seq;

		if ((seq = want_sequence (node, "spawns", error)) == NULL || !load_spawns (self, seq, error))
			return NULL;
	}
	if ((node = yaml_mapping_get_member (root, "tiles")) != NULL)
	{
		YamlMapping *section;

		if ((section = want_mapping (node, "tiles", error)) == NULL ||
		    !load_tiles (self->tile_rules, section, error))
			return NULL;
	}
	return g_steal_pointer (&self);
}

LrgProcgenDungeon *
lrg_procgen_dungeon_new_from_file (const gchar  *path,
                                   GError      **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;
	LrgProcgenDungeon *self;
	gsize length;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents (path, &text, &length, error))
		return NULL;
	self = lrg_procgen_dungeon_new_from_data (text, (gssize) length, &local);
	if (self == NULL)
		g_set_error (error, local->domain, local->code, "%s: %s", path, local->message);
	return self;
}

/* ==========================================================================
 * YAML: writing
 * ========================================================================== */

/* a tag as its LrgProcgenCell nick, or as an integer */
static YamlNode *
tag_node (guint8 tag)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_PROCGEN_CELL);
	GEnumValue *value = g_enum_get_value (klass, tag);

	if (value != NULL)
		return yaml_node_new_string (value->value_nick);
	return yaml_node_new_int (tag);
}

static const gchar *
enum_nick (GType type,
           gint  value)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	GEnumValue *v = g_enum_get_value (klass, value);

	/* nicks are static strings owned by the registered type */
	return v != NULL ? v->value_nick : NULL;
}

/* orders param specs by name, so documents do not depend on GLib's pool order */
static gint
compare_pspec_names (gconstpointer a,
                     gconstpointer b)
{
	const GParamSpec * const *pa = a;
	const GParamSpec * const *pb = b;

	return g_strcmp0 ((*pa)->name, (*pb)->name);
}

/* the generator's own parameters (every read-write property below the base) */
static YamlMapping *
params_mapping (LrgProcgenGenerator *generator)
{
	YamlMapping *params = yaml_mapping_new ();
	g_autofree GParamSpec **specs = NULL;
	guint n;
	guint i;

	specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (generator), &n);
	qsort (specs, n, sizeof (GParamSpec *), compare_pspec_names);
	for (i = 0; i < n; i++)
	{
		g_auto(GValue) value = G_VALUE_INIT;
		GParamSpec *pspec = specs[i];
		GType type = G_PARAM_SPEC_VALUE_TYPE (pspec);

		if (pspec->owner_type == LRG_TYPE_PROCGEN_GENERATOR ||
		    (pspec->flags & G_PARAM_READWRITE) != G_PARAM_READWRITE)
			continue;
		g_value_init (&value, type);
		g_object_get_property (G_OBJECT (generator), pspec->name, &value);
		if (type == G_TYPE_BOOLEAN)
			yaml_mapping_set_boolean_member (params, pspec->name, g_value_get_boolean (&value));
		else if (type == G_TYPE_UINT)
			yaml_mapping_set_int_member (params, pspec->name, g_value_get_uint (&value));
		else if (type == G_TYPE_INT)
			yaml_mapping_set_int_member (params, pspec->name, g_value_get_int (&value));
		else if (type == G_TYPE_INT64)
			yaml_mapping_set_int_member (params, pspec->name, g_value_get_int64 (&value));
		else if (type == G_TYPE_LONG)
			yaml_mapping_set_int_member (params, pspec->name, g_value_get_long (&value));
		else if (type == G_TYPE_UINT64 || type == G_TYPE_ULONG)
		{
			g_autofree gchar *text = g_strdup_printf ("%" G_GUINT64_FORMAT,
			                                          type == G_TYPE_UINT64 ? g_value_get_uint64 (&value)
			                                                                : (guint64) g_value_get_ulong (&value));
			g_autoptr(YamlNode) node = yaml_node_new_scalar (text, YAML_SCALAR_STYLE_PLAIN);

			yaml_mapping_set_member (params, pspec->name, node);
		}
		else if (type == G_TYPE_DOUBLE)
			yaml_mapping_set_double_member (params, pspec->name, g_value_get_double (&value));
		else if (type == G_TYPE_FLOAT)
			yaml_mapping_set_double_member (params, pspec->name, g_value_get_float (&value));
		else if (type == G_TYPE_STRING && g_value_get_string (&value) != NULL)
			yaml_mapping_set_string_member (params, pspec->name, g_value_get_string (&value));
		else if (G_TYPE_IS_ENUM (type) && enum_nick (type, g_value_get_enum (&value)) != NULL)
			yaml_mapping_set_string_member (params, pspec->name, enum_nick (type, g_value_get_enum (&value)));
	}
	return params;
}

static YamlMapping *
wfc_mapping (LrgProcgenWfc *wfc)
{
	YamlMapping *section = yaml_mapping_new ();
	g_autoptr(YamlSequence) tiles = yaml_sequence_new ();
	g_autoptr(YamlSequence) rules = yaml_sequence_new ();
	guint n = lrg_procgen_wfc_get_tile_count (wfc);
	guint a;
	guint b;
	guint d;

	for (a = 0; a < n; a++)
	{
		g_autoptr(YamlMapping) tile = yaml_mapping_new ();
		g_autoptr(YamlNode) tag = tag_node (lrg_procgen_wfc_get_tile_tag (wfc, a));

		yaml_mapping_set_string_member (tile, "name", lrg_procgen_wfc_get_tile_name (wfc, a));
		yaml_mapping_set_member (tile, "tag", tag);
		yaml_mapping_set_double_member (tile, "weight", lrg_procgen_wfc_get_tile_weight (wfc, a));
		yaml_sequence_add_mapping_element (tiles, tile);
	}

	/* rules are symmetric: right and down describe every adjacency */
	for (a = 0; a < n; a++)
		for (b = 0; b < n; b++)
			for (d = LRG_PROCGEN_DIRECTION_RIGHT; d <= LRG_PROCGEN_DIRECTION_DOWN; d++)
			{
				g_autoptr(YamlMapping) rule = NULL;

				if (!lrg_procgen_wfc_is_allowed (wfc, a, b, (LrgProcgenDirection) d))
					continue;
				rule = yaml_mapping_new ();
				yaml_mapping_set_string_member (rule, "a", lrg_procgen_wfc_get_tile_name (wfc, a));
				yaml_mapping_set_string_member (rule, "b", lrg_procgen_wfc_get_tile_name (wfc, b));
				yaml_mapping_set_string_member (rule, "dir", enum_nick (LRG_TYPE_PROCGEN_DIRECTION, (gint) d));
				yaml_sequence_add_mapping_element (rules, rule);
			}
	yaml_mapping_set_sequence_member (section, "tiles", tiles);
	yaml_mapping_set_sequence_member (section, "rules", rules);
	return section;
}

static YamlMapping *
tiles_mapping (LrgProcgenTileRules *rules)
{
	YamlMapping *section = yaml_mapping_new ();
	g_autoptr(YamlSequence) list = yaml_sequence_new ();
	guint tag;

	yaml_mapping_set_int_member (section, "default", lrg_procgen_tile_rules_get_default_tile (rules));
	yaml_mapping_set_boolean_member (section, "edge-connects",
	                                 lrg_procgen_tile_rules_get_edge_connects (rules));
	for (tag = 0; tag < 256; tag++)
	{
		g_autoptr(YamlMapping) rule = NULL;
		g_autoptr(YamlSequence) connects = NULL;
		g_autoptr(YamlSequence) variants = NULL;
		g_autoptr(YamlNode) tag_value = NULL;
		LrgProcgenAutotile autotile;
		guint tile;
		guint other;
		guint mask;

		if (!lrg_procgen_tile_rules_get_rule (rules, (guint8) tag, &tile, &autotile))
			continue;
		rule = yaml_mapping_new ();
		tag_value = tag_node ((guint8) tag);
		yaml_mapping_set_member (rule, "tag", tag_value);
		yaml_mapping_set_int_member (rule, "tile", tile);
		if (autotile != LRG_PROCGEN_AUTOTILE_NONE)
			yaml_mapping_set_string_member (rule, "autotile",
			                                enum_nick (LRG_TYPE_PROCGEN_AUTOTILE, autotile));

		connects = yaml_sequence_new ();
		for (other = 0; other < 256; other++)
			if (lrg_procgen_tile_rules_get_connects (rules, (guint8) tag, (guint8) other))
			{
				g_autoptr(YamlNode) node = tag_node ((guint8) other);

				yaml_sequence_add_element (connects, node);
			}
		if (yaml_sequence_get_length (connects) > 0)
			yaml_mapping_set_sequence_member (rule, "connects", connects);

		variants = yaml_sequence_new ();
		for (mask = 0; mask < 256; mask++)
		{
			guint vtile;

			if (lrg_procgen_tile_rules_get_variant (rules, (guint8) tag, mask, &vtile))
			{
				g_autoptr(YamlMapping) variant = yaml_mapping_new ();

				yaml_mapping_set_int_member (variant, "mask", mask);
				yaml_mapping_set_int_member (variant, "tile", vtile);
				yaml_sequence_add_mapping_element (variants, variant);
			}
		}
		if (yaml_sequence_get_length (variants) > 0)
			yaml_mapping_set_sequence_member (rule, "variants", variants);
		yaml_sequence_add_mapping_element (list, rule);
	}
	yaml_mapping_set_sequence_member (section, "rules", list);
	return section;
}

gchar *
lrg_procgen_dungeon_to_data (LrgProcgenDungeon  *self,
                             GError            **error)
{
	g_autoptr(YamlMapping) root = NULL;
	g_autoptr(YamlMapping) params = NULL;
	g_autoptr(YamlMapping) post = NULL;
	g_autoptr(YamlMapping) tiles = NULL;
	g_autoptr(YamlSequence) spawns = NULL;
	g_autoptr(YamlNode) root_node = NULL;
	g_autoptr(YamlGenerator) generator = NULL;
	gsize length;
	guint i;

	g_return_val_if_fail (LRG_IS_PROCGEN_DUNGEON (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* --- header: version, generator, size, seed --- */
	root = yaml_mapping_new ();
	yaml_mapping_set_int_member (root, "version", LRG_PROCGEN_DUNGEON_FORMAT_VERSION);
	yaml_mapping_set_string_member (root, "generator",
	                                lrg_procgen_generator_type_to_name (G_OBJECT_TYPE (self->generator)));
	yaml_mapping_set_int_member (root, "width", lrg_procgen_generator_get_width (self->generator));
	yaml_mapping_set_int_member (root, "height", lrg_procgen_generator_get_height (self->generator));
	{
		g_autofree gchar *seed = g_strdup_printf ("%" G_GUINT64_FORMAT, self->seed);
		g_autoptr(YamlNode) node = yaml_node_new_scalar (seed, YAML_SCALAR_STYLE_PLAIN);

		yaml_mapping_set_member (root, "seed", node);
	}

	/* --- generator parameters and model --- */
	params = params_mapping (self->generator);
	if (yaml_mapping_get_size (params) > 0)
		yaml_mapping_set_mapping_member (root, "params", params);
	if (LRG_IS_PROCGEN_WFC (self->generator))
	{
		g_autoptr(YamlMapping) wfc = wfc_mapping (LRG_PROCGEN_WFC (self->generator));

		yaml_mapping_set_mapping_member (root, "wfc", wfc);
	}

	/* --- post-processing and spawn rules --- */
	post = yaml_mapping_new ();
	yaml_mapping_set_int_member (post, "cull-min-size", self->cull_min_size);
	yaml_mapping_set_boolean_member (post, "keep-largest", self->keep_largest);
	yaml_mapping_set_boolean_member (post, "connect", self->connect);
	yaml_mapping_set_boolean_member (post, "start-exit", self->place_start_exit);
	yaml_mapping_set_mapping_member (root, "post", post);

	spawns = yaml_sequence_new ();
	for (i = 0; i < self->spawn_rules->len; i++)
	{
		SpawnRule *rule = &g_array_index (self->spawn_rules, SpawnRule, i);
		g_autoptr(YamlMapping) spawn = yaml_mapping_new ();

		yaml_mapping_set_string_member (spawn, "kind", rule->kind);
		yaml_mapping_set_int_member (spawn, "count", rule->count);
		yaml_mapping_set_int_member (spawn, "min-distance", rule->min_distance);
		yaml_mapping_set_boolean_member (spawn, "rooms-only", rule->rooms_only);
		yaml_sequence_add_mapping_element (spawns, spawn);
	}
	if (yaml_sequence_get_length (spawns) > 0)
		yaml_mapping_set_sequence_member (root, "spawns", spawns);

	/* --- tile rules --- */
	tiles = tiles_mapping (self->tile_rules);
	yaml_mapping_set_mapping_member (root, "tiles", tiles);

	root_node = yaml_node_new_mapping (root);
	generator = yaml_generator_new ();
	yaml_generator_set_root (generator, root_node);
	return yaml_generator_to_data (generator, &length, error);
}

gboolean
lrg_procgen_dungeon_save_to_file (LrgProcgenDungeon  *self,
                                  const gchar        *path,
                                  GError            **error)
{
	g_autofree gchar *data = NULL;

	g_return_val_if_fail (LRG_IS_PROCGEN_DUNGEON (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	data = lrg_procgen_dungeon_to_data (self, error);
	if (data == NULL)
		return FALSE;
	return g_file_set_contents (path, data, -1, error);
}
