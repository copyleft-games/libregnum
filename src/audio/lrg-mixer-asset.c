/* lrg-mixer-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMixerAsset: the `.rmixer` mixer layout.
 *
 *   version: 1
 *   name: Main mix
 *   default_snapshot: explore        # optional, applied by apply()
 *   buses:                           # master first (added when missing)
 *     - name: master
 *       volume: 0                    # dB, -144 (or -inf) .. +12.04
 *     - name: music
 *       parent: master               # default master
 *       volume: -6
 *       mute: false                  # written only when true
 *       solo: false                  # written only when true
 *       sends:                       # post-fader
 *         - target: reverb
 *           level: -12               # dB
 *       effects:                     # in processing order
 *         - type: low-pass           # registered name or GType name
 *           id: muffle               # default: the type; unique per bus
 *           enabled: true            # written only when false
 *           params:                  # effect properties (kebab or snake)
 *             cutoff: 18000
 *   snapshots:
 *     - name: underwater
 *       blend: 0.5                   # seconds
 *       volumes: { music: -12 }      # bus -> dB
 *       sends:
 *         - { source: music, target: reverb, level: -3 }
 *       params: { music/muffle/cutoff: 800 }   # bus/effect/param -> value
 *   ducking:
 *     - trigger: dialogue
 *       target: music
 *       threshold: -30               # dB
 *       depth: -12                   # dB
 *       attack: 0.05                 # seconds
 *       release: 0.4                 # seconds
 *
 * Strict: wrong shapes, unknown buses, effects or parameters, out of
 * range values and cyclic routing are errors whose messages carry the
 * key path.  Keys this version does not know (root, bus, send, effect,
 * snapshot, snapshot send and ducking-rule mappings) are kept as YAML
 * nodes and written back after the known ones.
 */

#include "config.h"

#include "lrg-mixer-asset.h"
#include "lrg-low-pass.h"
#include "lrg-high-pass.h"
#include "lrg-echo.h"
#include "lrg-reverb.h"
#include "lrg-chorus.h"
#include "lrg-pitch-shift.h"
#include "../core/lrg-data-loader.h"
#include "../physics/lrg-physics-layers.h"
#include <yaml-glib.h>
#include <math.h>
#include <string.h>

#define MAX_DB        (LRG_AUDIO_MIXER_MAX_GAIN_DB)
#define MAX_DUCK_TIME (10.0)

/* ==========================================================================
 * Data model
 * ========================================================================== */

typedef struct
{
	gchar  *name;      /* canonical (kebab-case) property name */
	GValue  value;     /* of the property's value type */
} Param;

typedef struct
{
	gchar       *type_name;
	GType        gtype;
	gchar       *id;
	gboolean     enabled;
	GPtrArray   *params;   /* Param, in first-set order */
	YamlMapping *extra;
} EffectDef;

typedef struct
{
	gchar       *target;
	gdouble      level;    /* dB */
	YamlMapping *extra;
} SendDef;

typedef struct
{
	gchar       *name;
	gchar       *parent;   /* NULL for master */
	gdouble      volume;   /* dB */
	gboolean     mute;
	gboolean     solo;
	GPtrArray   *sends;    /* SendDef */
	GPtrArray   *effects;  /* EffectDef */
	YamlMapping *extra;
	gint         src_index; /* index in the file's buses list, -1 when not from a file */
} BusDef;

typedef struct
{
	gchar   *bus;
	gdouble  db;
} SnapVolume;

typedef struct
{
	gchar       *source;
	gchar       *target;
	gdouble      db;
	YamlMapping *extra;
} SnapSend;

typedef struct
{
	gchar   *bus;
	gchar   *effect;
	gchar   *param;
	gdouble  value;
} SnapParam;

typedef struct
{
	gchar       *name;
	gdouble      blend;
	GPtrArray   *volumes;  /* SnapVolume */
	GPtrArray   *sends;    /* SnapSend */
	GPtrArray   *params;   /* SnapParam */
	YamlMapping *extra;
} SnapshotDef;

typedef struct
{
	gchar       *trigger;
	gchar       *target;
	gdouble      threshold;
	gdouble      depth;
	gdouble      attack;
	gdouble      release;
	YamlMapping *extra;
} DuckDef;

struct _LrgMixerAsset
{
	GObject      parent_instance;

	gchar       *name;
	gchar       *default_snapshot;
	GPtrArray   *buses;      /* BusDef, master first */
	GPtrArray   *snapshots;  /* SnapshotDef */
	GPtrArray   *ducks;      /* DuckDef */
	YamlMapping *extra;
};

G_DEFINE_FINAL_TYPE (LrgMixerAsset, lrg_mixer_asset, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-mixer-asset-error-quark, lrg_mixer_asset_error)

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

GType
lrg_mixer_asset_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_MIXER_ASSET_ERROR_PARSE, "LRG_MIXER_ASSET_ERROR_PARSE", "parse" },
			{ LRG_MIXER_ASSET_ERROR_INVALID_VALUE, "LRG_MIXER_ASSET_ERROR_INVALID_VALUE", "invalid-value" },
			{ LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS, "LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS", "unknown-bus" },
			{ LRG_MIXER_ASSET_ERROR_UNKNOWN_EFFECT, "LRG_MIXER_ASSET_ERROR_UNKNOWN_EFFECT", "unknown-effect" },
			{ LRG_MIXER_ASSET_ERROR_UNKNOWN_PARAM, "LRG_MIXER_ASSET_ERROR_UNKNOWN_PARAM", "unknown-param" },
			{ LRG_MIXER_ASSET_ERROR_ROUTING, "LRG_MIXER_ASSET_ERROR_ROUTING", "routing" },
			{ LRG_MIXER_ASSET_ERROR_UNKNOWN_SNAPSHOT, "LRG_MIXER_ASSET_ERROR_UNKNOWN_SNAPSHOT", "unknown-snapshot" },
			{ LRG_MIXER_ASSET_ERROR_IO, "LRG_MIXER_ASSET_ERROR_IO", "io" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgMixerAssetError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* --- free / copy --------------------------------------------------------- */

static void
extra_unref (YamlMapping *extra)
{
	if (extra != NULL)
		yaml_mapping_unref (extra);
}

static YamlMapping *
extra_ref (YamlMapping *extra)
{
	return extra != NULL ? yaml_mapping_ref (extra) : NULL;
}

static void
param_free (gpointer data)
{
	Param *p = data;

	g_free (p->name);
	g_value_unset (&p->value);
	g_free (p);
}

static Param *
param_new (const gchar  *name,
           const GValue *value)
{
	Param *p = g_new0 (Param, 1);

	p->name = g_strdup (name);
	g_value_init (&p->value, G_VALUE_TYPE (value));
	g_value_copy (value, &p->value);
	return p;
}

static void
effect_def_free (gpointer data)
{
	EffectDef *e = data;

	g_free (e->type_name);
	g_free (e->id);
	g_ptr_array_unref (e->params);
	extra_unref (e->extra);
	g_free (e);
}

static EffectDef *
effect_def_new (const gchar *type_name,
                GType        gtype,
                const gchar *id)
{
	EffectDef *e = g_new0 (EffectDef, 1);

	e->type_name = g_strdup (type_name);
	e->gtype = gtype;
	e->id = g_strdup (id != NULL ? id : type_name);
	e->enabled = TRUE;
	e->params = g_ptr_array_new_with_free_func (param_free);
	return e;
}

static EffectDef *
effect_def_copy (const EffectDef *src)
{
	EffectDef *e = effect_def_new (src->type_name, src->gtype, src->id);
	guint      i;

	e->enabled = src->enabled;
	for (i = 0; i < src->params->len; i++)
	{
		Param *p = g_ptr_array_index (src->params, i);

		g_ptr_array_add (e->params, param_new (p->name, &p->value));
	}
	e->extra = extra_ref (src->extra);
	return e;
}

static void
send_def_free (gpointer data)
{
	SendDef *s = data;

	g_free (s->target);
	extra_unref (s->extra);
	g_free (s);
}

static SendDef *
send_def_new (const gchar *target,
              gdouble      level)
{
	SendDef *s = g_new0 (SendDef, 1);

	s->target = g_strdup (target);
	s->level = level;
	return s;
}

static void
bus_def_free (gpointer data)
{
	BusDef *b = data;

	g_free (b->name);
	g_free (b->parent);
	g_ptr_array_unref (b->sends);
	g_ptr_array_unref (b->effects);
	extra_unref (b->extra);
	g_free (b);
}

static BusDef *
bus_def_new (const gchar *name,
             const gchar *parent)
{
	BusDef *b = g_new0 (BusDef, 1);

	b->name = g_strdup (name);
	b->parent = g_strdup (parent);
	b->sends = g_ptr_array_new_with_free_func (send_def_free);
	b->effects = g_ptr_array_new_with_free_func (effect_def_free);
	b->src_index = -1;
	return b;
}

static BusDef *
bus_def_copy (const BusDef *src)
{
	BusDef *b = bus_def_new (src->name, src->parent);
	guint   i;

	b->src_index = src->src_index;
	b->volume = src->volume;
	b->mute = src->mute;
	b->solo = src->solo;
	for (i = 0; i < src->sends->len; i++)
	{
		SendDef *s = g_ptr_array_index (src->sends, i);
		SendDef *c = send_def_new (s->target, s->level);

		c->extra = extra_ref (s->extra);
		g_ptr_array_add (b->sends, c);
	}
	for (i = 0; i < src->effects->len; i++)
		g_ptr_array_add (b->effects, effect_def_copy (g_ptr_array_index (src->effects, i)));
	b->extra = extra_ref (src->extra);
	return b;
}

static void
snap_volume_free (gpointer data)
{
	SnapVolume *v = data;

	g_free (v->bus);
	g_free (v);
}

static void
snap_send_free (gpointer data)
{
	SnapSend *s = data;

	g_free (s->source);
	g_free (s->target);
	extra_unref (s->extra);
	g_free (s);
}

static void
snap_param_free (gpointer data)
{
	SnapParam *p = data;

	g_free (p->bus);
	g_free (p->effect);
	g_free (p->param);
	g_free (p);
}

static void
snapshot_def_free (gpointer data)
{
	SnapshotDef *s = data;

	g_free (s->name);
	g_ptr_array_unref (s->volumes);
	g_ptr_array_unref (s->sends);
	g_ptr_array_unref (s->params);
	extra_unref (s->extra);
	g_free (s);
}

static SnapshotDef *
snapshot_def_new (const gchar *name,
                  gdouble      blend)
{
	SnapshotDef *s = g_new0 (SnapshotDef, 1);

	s->name = g_strdup (name);
	s->blend = blend;
	s->volumes = g_ptr_array_new_with_free_func (snap_volume_free);
	s->sends = g_ptr_array_new_with_free_func (snap_send_free);
	s->params = g_ptr_array_new_with_free_func (snap_param_free);
	return s;
}

static SnapshotDef *
snapshot_def_copy (const SnapshotDef *src)
{
	SnapshotDef *s = snapshot_def_new (src->name, src->blend);
	guint        i;

	for (i = 0; i < src->volumes->len; i++)
	{
		SnapVolume *v = g_ptr_array_index (src->volumes, i);
		SnapVolume *c = g_new0 (SnapVolume, 1);

		c->bus = g_strdup (v->bus);
		c->db = v->db;
		g_ptr_array_add (s->volumes, c);
	}
	for (i = 0; i < src->sends->len; i++)
	{
		SnapSend *v = g_ptr_array_index (src->sends, i);
		SnapSend *c = g_new0 (SnapSend, 1);

		c->source = g_strdup (v->source);
		c->target = g_strdup (v->target);
		c->db = v->db;
		c->extra = extra_ref (v->extra);
		g_ptr_array_add (s->sends, c);
	}
	for (i = 0; i < src->params->len; i++)
	{
		SnapParam *v = g_ptr_array_index (src->params, i);
		SnapParam *c = g_new0 (SnapParam, 1);

		c->bus = g_strdup (v->bus);
		c->effect = g_strdup (v->effect);
		c->param = g_strdup (v->param);
		c->value = v->value;
		g_ptr_array_add (s->params, c);
	}
	s->extra = extra_ref (src->extra);
	return s;
}

static void
duck_def_free (gpointer data)
{
	DuckDef *d = data;

	g_free (d->trigger);
	g_free (d->target);
	extra_unref (d->extra);
	g_free (d);
}

static DuckDef *
duck_def_copy (const DuckDef *src)
{
	DuckDef *d = g_new0 (DuckDef, 1);

	*d = *src;
	d->trigger = g_strdup (src->trigger);
	d->target = g_strdup (src->target);
	d->extra = extra_ref (src->extra);
	return d;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_mixer_asset_finalize (GObject *object)
{
	LrgMixerAsset *self = LRG_MIXER_ASSET (object);

	g_free (self->name);
	g_free (self->default_snapshot);
	g_ptr_array_unref (self->buses);
	g_ptr_array_unref (self->snapshots);
	g_ptr_array_unref (self->ducks);
	extra_unref (self->extra);

	G_OBJECT_CLASS (lrg_mixer_asset_parent_class)->finalize (object);
}

static void
lrg_mixer_asset_class_init (LrgMixerAssetClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_mixer_asset_finalize;

	/**
	 * LrgMixerAsset::changed:
	 * @self: the layout
	 *
	 * Emitted after every successful edit through the API (not while
	 * loading), so a mixer panel can re-apply the layout and refresh.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
		              NULL, G_TYPE_NONE, 0);
}

static void
lrg_mixer_asset_init (LrgMixerAsset *self)
{
	self->buses = g_ptr_array_new_with_free_func (bus_def_free);
	self->snapshots = g_ptr_array_new_with_free_func (snapshot_def_free);
	self->ducks = g_ptr_array_new_with_free_func (duck_def_free);
	g_ptr_array_add (self->buses, bus_def_new ("master", NULL));
}

static void
changed (LrgMixerAsset *self)
{
	guint i;

	/* after an edit, error paths name the in-memory positions */
	for (i = 0; i < self->buses->len; i++)
		((BusDef *) g_ptr_array_index (self->buses, i))->src_index = -1;
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* ==========================================================================
 * Effect type registry
 * ========================================================================== */

static GMutex      registry_lock;
static GHashTable *registry;   /* name -> GType (as pointer) */

static void
ensure_registry_locked (void)
{
	if (registry != NULL)
		return;
	registry = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
#define REG(n, t) g_hash_table_insert (registry, g_strdup (n), GSIZE_TO_POINTER ((gsize) (t)))
	REG ("low-pass", LRG_TYPE_LOW_PASS);
	REG ("high-pass", LRG_TYPE_HIGH_PASS);
	REG ("echo", LRG_TYPE_ECHO);
	REG ("reverb", LRG_TYPE_REVERB);
	REG ("chorus", LRG_TYPE_CHORUS);
	REG ("pitch-shift", LRG_TYPE_PITCH_SHIFT);
#undef REG
}

void
lrg_mixer_asset_register_effect_type (const gchar *name,
                                      GType        type)
{
	g_return_if_fail (name != NULL && *name != '\0');
	g_return_if_fail (g_type_is_a (type, LRG_TYPE_AUDIO_EFFECT));
	g_return_if_fail (!G_TYPE_IS_ABSTRACT (type));

	g_mutex_lock (&registry_lock);
	ensure_registry_locked ();
	g_hash_table_insert (registry, g_strdup (name), GSIZE_TO_POINTER ((gsize) type));
	g_mutex_unlock (&registry_lock);
}

GType
lrg_mixer_asset_lookup_effect_type (const gchar *name)
{
	gpointer found = NULL;
	gboolean hit;
	GType    t;

	g_return_val_if_fail (name != NULL, G_TYPE_INVALID);

	g_mutex_lock (&registry_lock);
	ensure_registry_locked ();
	hit = g_hash_table_lookup_extended (registry, name, NULL, &found);
	g_mutex_unlock (&registry_lock);
	if (hit)
		return (GType) GPOINTER_TO_SIZE (found);

	/* the GType name of a concrete effect */
	t = g_type_from_name (name);
	if (t != G_TYPE_INVALID && g_type_is_a (t, LRG_TYPE_AUDIO_EFFECT) && !G_TYPE_IS_ABSTRACT (t))
		return t;
	return G_TYPE_INVALID;
}

gchar **
lrg_mixer_asset_list_effect_types (void)
{
	g_autoptr(GPtrArray) names = g_ptr_array_new ();
	GHashTableIter       iter;
	gpointer             key;

	g_mutex_lock (&registry_lock);
	ensure_registry_locked ();
	g_hash_table_iter_init (&iter, registry);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_ptr_array_add (names, g_strdup (key));
	g_mutex_unlock (&registry_lock);

	g_ptr_array_sort_values (names, (GCompareFunc) g_strcmp0);
	g_ptr_array_add (names, NULL);
	return (gchar **) g_ptr_array_free (g_steal_pointer (&names), FALSE);
}

/* ==========================================================================
 * Parameters
 * ========================================================================== */

/* kebab-case copy of @name */
static gchar *
canonical_name (const gchar *name)
{
	gchar *c = g_strdup (name);

	g_strdelimit (c, "_", '-');
	return c;
}

/* A settable effect parameter: readable, writable, not construct-only and
 * not one of LrgAudioEffect's own (enabled is an entry key) */
static GParamSpec *
find_param_pspec (GType        effect_type,
                  const gchar *name)
{
	g_autoptr(GTypeClass) klass = NULL;
	g_autofree gchar     *canon = NULL;
	GParamSpec           *pspec;

	if (name == NULL || effect_type == G_TYPE_INVALID)
		return NULL;
	canon = canonical_name (name);
	klass = g_type_class_ref (effect_type);
	pspec = g_object_class_find_property (G_OBJECT_CLASS (klass), canon);
	if (pspec == NULL || (pspec->flags & G_PARAM_WRITABLE) == 0 || (pspec->flags & G_PARAM_READABLE) == 0
	    || (pspec->flags & G_PARAM_CONSTRUCT_ONLY) != 0 || pspec->owner_type == LRG_TYPE_AUDIO_EFFECT
	    || pspec->owner_type == G_TYPE_OBJECT)
		return NULL;
	return pspec;
}

static gboolean
is_numeric_type (GType type)
{
	GType fund = G_TYPE_FUNDAMENTAL (type);

	return fund == G_TYPE_FLOAT || fund == G_TYPE_DOUBLE || fund == G_TYPE_INT || fund == G_TYPE_UINT;
}

/* the value of @v as a double (numbers, booleans, enums) */
static gdouble
value_to_double (const GValue *v)
{
	g_auto(GValue) d = G_VALUE_INIT;

	if (G_VALUE_HOLDS_ENUM (v))
		return (gdouble) g_value_get_enum (v);
	g_value_init (&d, G_TYPE_DOUBLE);
	if (!g_value_transform (v, &d))
		return 0.0;
	return g_value_get_double (&d);
}

/* parses a number; "-inf", "-.inf" and "-Infinity" are -infinity */
static gboolean
parse_number (const gchar *text,
              gdouble     *out)
{
	gchar  *end = NULL;
	gdouble v;

	if (text == NULL || *text == '\0')
		return FALSE;
	if (g_ascii_strcasecmp (text, "-inf") == 0 || g_ascii_strcasecmp (text, "-.inf") == 0
	    || g_ascii_strcasecmp (text, "-infinity") == 0)
	{
		*out = -INFINITY;
		return TRUE;
	}
	v = g_ascii_strtod (text, &end);
	if (end == text || *end != '\0' || isnan (v))
		return FALSE;
	*out = v;
	return TRUE;
}

static gboolean
parse_bool (const gchar *text,
            gboolean    *out)
{
	if (text == NULL)
		return FALSE;
	if (g_ascii_strcasecmp (text, "true") == 0 || g_ascii_strcasecmp (text, "yes") == 0
	    || g_ascii_strcasecmp (text, "on") == 0)
		*out = TRUE;
	else if (g_ascii_strcasecmp (text, "false") == 0 || g_ascii_strcasecmp (text, "no") == 0
	         || g_ascii_strcasecmp (text, "off") == 0)
		*out = FALSE;
	else
		return FALSE;
	return TRUE;
}

/*
 * value_from_text:
 * Converts @text to @out (initialised here to @pspec's type) and checks
 * the property's range.  Returns FALSE with @why set on failure.
 */
static gboolean
value_from_text (GParamSpec   *pspec,
                 const gchar  *text,
                 GValue       *out,
                 const gchar **why)
{
	GType   type = pspec->value_type;
	GType   fund = G_TYPE_FUNDAMENTAL (type);
	gdouble d = 0.0;

	g_value_init (out, type);
	*why = "expected a number";
	switch (fund)
	{
	case G_TYPE_BOOLEAN:
	{
		gboolean b;

		*why = "expected true or false";
		if (!parse_bool (text, &b))
			return FALSE;
		g_value_set_boolean (out, b);
		break;
	}
	case G_TYPE_ENUM:
	{
		g_autoptr(GEnumClass) eclass = g_type_class_ref (type);
		GEnumValue           *ev = NULL;

		*why = "expected one of the enumeration's names";
		if (text != NULL)
			ev = g_enum_get_value_by_nick (eclass, text);
		if (ev == NULL && text != NULL)
			ev = g_enum_get_value_by_name (eclass, text);
		if (ev == NULL && parse_number (text, &d) && d == floor (d))
			ev = g_enum_get_value (eclass, (gint) d);
		if (ev == NULL)
			return FALSE;
		g_value_set_enum (out, ev->value);
		break;
	}
	case G_TYPE_STRING:
		g_value_set_string (out, text != NULL ? text : "");
		break;
	case G_TYPE_FLOAT:
	case G_TYPE_DOUBLE:
	case G_TYPE_INT:
	case G_TYPE_UINT:
	case G_TYPE_INT64:
	case G_TYPE_UINT64:
		if (!parse_number (text, &d) || !isfinite (d))
			return FALSE;
		if (fund != G_TYPE_FLOAT && fund != G_TYPE_DOUBLE && d != floor (d))
		{
			*why = "expected a whole number";
			return FALSE;
		}
		if (fund == G_TYPE_FLOAT)
			g_value_set_float (out, (gfloat) d);
		else if (fund == G_TYPE_DOUBLE)
			g_value_set_double (out, d);
		else if (fund == G_TYPE_INT)
			g_value_set_int (out, (gint) CLAMP (d, (gdouble) G_MININT, (gdouble) G_MAXINT));
		else if (fund == G_TYPE_UINT)
			g_value_set_uint (out, (guint) CLAMP (d, 0.0, (gdouble) G_MAXUINT));
		else if (fund == G_TYPE_INT64)
			g_value_set_int64 (out, (gint64) d);
		else
			g_value_set_uint64 (out, (guint64) MAX (d, 0.0));
		break;
	default:
		*why = "parameter type not supported in .rmixer";
		return FALSE;
	}
	if (g_param_value_validate (pspec, out))
	{
		*why = "out of the parameter's range";
		return FALSE;
	}
	return TRUE;
}

static YamlNode *
value_to_node (const GValue *v)
{
	switch (G_TYPE_FUNDAMENTAL (G_VALUE_TYPE (v)))
	{
	case G_TYPE_BOOLEAN:
		return lrg_data_yaml_boolean (g_value_get_boolean (v));
	case G_TYPE_FLOAT:
		return lrg_data_yaml_float (g_value_get_float (v));
	case G_TYPE_DOUBLE:
		return lrg_data_yaml_double (g_value_get_double (v));
	case G_TYPE_INT:
		return lrg_data_yaml_int (g_value_get_int (v));
	case G_TYPE_UINT:
		return lrg_data_yaml_int ((gint64) g_value_get_uint (v));
	case G_TYPE_INT64:
		return lrg_data_yaml_int (g_value_get_int64 (v));
	case G_TYPE_UINT64:
		return lrg_data_yaml_int ((gint64) g_value_get_uint64 (v));
	case G_TYPE_ENUM:
	{
		g_autoptr(GEnumClass) eclass = g_type_class_ref (G_VALUE_TYPE (v));
		GEnumValue           *ev = g_enum_get_value (eclass, g_value_get_enum (v));

		if (ev != NULL)
			return lrg_data_yaml_string (ev->value_nick);
		return lrg_data_yaml_int (g_value_get_enum (v));
	}
	case G_TYPE_STRING:
	default:
		return lrg_data_yaml_string (G_VALUE_HOLDS_STRING (v) && g_value_get_string (v) != NULL
		                             ? g_value_get_string (v) : "");
	}
}

static Param *
effect_find_param (EffectDef   *e,
                   const gchar *name)
{
	g_autofree gchar *canon = canonical_name (name);
	guint             i;

	for (i = 0; i < e->params->len; i++)
	{
		Param *p = g_ptr_array_index (e->params, i);

		if (g_strcmp0 (p->name, canon) == 0)
			return p;
	}
	return NULL;
}

/* sets (replacing in place, keeping the order) */
static void
effect_set_param (EffectDef    *e,
                  const gchar  *name,
                  const GValue *value)
{
	Param *p = effect_find_param (e, name);

	if (p != NULL)
	{
		g_value_unset (&p->value);
		g_value_init (&p->value, G_VALUE_TYPE (value));
		g_value_copy (value, &p->value);
		return;
	}
	g_ptr_array_add (e->params, param_new (name, value));
}

/* ==========================================================================
 * Lookups
 * ========================================================================== */

static gint
bus_index (LrgMixerAsset *self,
           const gchar   *name)
{
	guint i;

	if (name == NULL)
		return -1;
	for (i = 0; i < self->buses->len; i++)
		if (g_strcmp0 (((BusDef *) g_ptr_array_index (self->buses, i))->name, name) == 0)
			return (gint) i;
	return -1;
}

static BusDef *
find_bus (LrgMixerAsset *self,
          const gchar   *name)
{
	gint i = bus_index (self, name);

	return i >= 0 ? g_ptr_array_index (self->buses, i) : NULL;
}

static gint
effect_index (BusDef      *bus,
              const gchar *id)
{
	guint i;

	if (bus == NULL || id == NULL)
		return -1;
	for (i = 0; i < bus->effects->len; i++)
		if (g_strcmp0 (((EffectDef *) g_ptr_array_index (bus->effects, i))->id, id) == 0)
			return (gint) i;
	return -1;
}

static EffectDef *
find_effect (LrgMixerAsset *self,
             const gchar   *bus,
             const gchar   *id)
{
	BusDef *b = find_bus (self, bus);
	gint    i = effect_index (b, id);

	return i >= 0 ? g_ptr_array_index (b->effects, i) : NULL;
}

static SendDef *
find_send (BusDef      *bus,
           const gchar *target)
{
	guint i;

	for (i = 0; bus != NULL && i < bus->sends->len; i++)
	{
		SendDef *s = g_ptr_array_index (bus->sends, i);

		if (g_strcmp0 (s->target, target) == 0)
			return s;
	}
	return NULL;
}

static SnapshotDef *
find_snapshot (LrgMixerAsset *self,
               const gchar   *name)
{
	guint i;

	for (i = 0; name != NULL && i < self->snapshots->len; i++)
	{
		SnapshotDef *s = g_ptr_array_index (self->snapshots, i);

		if (g_strcmp0 (s->name, name) == 0)
			return s;
	}
	return NULL;
}

static gdouble
clamp_db (gdouble db)
{
	if (isnan (db))
		return 0.0;
	return CLAMP (db, LRG_AUDIO_SILENCE_DB, MAX_DB);
}

/* ==========================================================================
 * YAML in
 * ========================================================================== */

typedef struct
{
	const gchar  *source;
	GError      **error;
} Ctx;

static gboolean
fail (Ctx         *ctx,
      gint         code,
      const gchar *path,
      const gchar *format,
      ...) G_GNUC_PRINTF (4, 5);

static gboolean
fail (Ctx         *ctx,
      gint         code,
      const gchar *path,
      const gchar *format,
      ...)
{
	g_autofree gchar *text = NULL;
	va_list           args;

	va_start (args, format);
	text = g_strdup_vprintf (format, args);
	va_end (args);
	g_set_error (ctx->error, LRG_MIXER_ASSET_ERROR, code, "%s: %s: %s", ctx->source,
	             path != NULL && *path != '\0' ? path : "/", text);
	return FALSE;
}

static const gchar *
scalar_of (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

static gboolean
is_empty_node (YamlNode *node)
{
	const gchar *s;

	if (node == NULL || yaml_node_get_node_type (node) == YAML_NODE_NULL)
		return TRUE;
	s = scalar_of (node);
	return s != NULL && (*s == '\0' || g_strcmp0 (s, "~") == 0 || g_strcmp0 (s, "null") == 0);
}

static gboolean
read_string (Ctx          *ctx,
             YamlNode     *node,
             const gchar  *path,
             const gchar **out)
{
	const gchar *s = scalar_of (node);

	if (s == NULL || *s == '\0')
		return fail (ctx, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, path, "expected a nonempty string");
	*out = s;
	return TRUE;
}

static gboolean
read_number (Ctx         *ctx,
             YamlNode    *node,
             const gchar *path,
             gdouble      min,
             gdouble      max,
             gdouble     *out)
{
	gdouble v;

	if (!parse_number (scalar_of (node), &v))
		return fail (ctx, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, path, "expected a number");
	if (v < min || v > max)
		return fail (ctx, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, path,
		             "%g is out of range (%g .. %g)", v, min, max);
	*out = v;
	return TRUE;
}

/* a level in dB: -inf and anything at or below -144 is silence */
static gboolean
read_db (Ctx         *ctx,
         YamlNode    *node,
         const gchar *path,
         gdouble      max,
         gdouble     *out)
{
	gdouble v;

	if (!parse_number (scalar_of (node), &v))
		return fail (ctx, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, path, "expected a level in dB");
	if (v > max + 1e-9)
		return fail (ctx, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, path,
		             "%g dB is above the maximum %g dB", v, max);
	*out = MAX (v, LRG_AUDIO_SILENCE_DB);
	*out = MIN (*out, max);
	return TRUE;
}

static gboolean
read_bool (Ctx         *ctx,
           YamlNode    *node,
           const gchar *path,
           gboolean    *out)
{
	if (!parse_bool (scalar_of (node), out))
		return fail (ctx, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, path, "expected true or false");
	return TRUE;
}

static gboolean
expect_mapping (Ctx         *ctx,
                YamlNode    *node,
                const gchar *path)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return fail (ctx, LRG_MIXER_ASSET_ERROR_PARSE, path, "expected a mapping");
	return TRUE;
}

static gboolean
expect_sequence (Ctx         *ctx,
                 YamlNode    *node,
                 const gchar *path)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return fail (ctx, LRG_MIXER_ASSET_ERROR_PARSE, path, "expected a list");
	return TRUE;
}

/* keeps an unknown key for the round trip */
static void
keep_extra (YamlMapping **extra,
            const gchar  *key,
            YamlNode     *member)
{
	if (*extra == NULL)
		*extra = yaml_mapping_new ();
	yaml_mapping_set_member (*extra, key, member);
}

static EffectDef *
read_effect (Ctx         *ctx,
             YamlNode    *node,
             const gchar *path)
{
	g_autoptr(GList)  keys = NULL;
	YamlMapping      *map;
	EffectDef        *e;
	const gchar      *type_name = NULL;
	const gchar      *id = NULL;
	GType             gtype;
	GList            *l;
	g_autofree gchar *p = NULL;

	if (!expect_mapping (ctx, node, path))
		return NULL;
	map = yaml_node_get_mapping (node);
	p = g_strconcat (path, "/type", NULL);
	if (!yaml_mapping_has_member (map, "type"))
	{
		fail (ctx, LRG_MIXER_ASSET_ERROR_PARSE, path, "missing 'type'");
		return NULL;
	}
	if (!read_string (ctx, yaml_mapping_get_member (map, "type"), p, &type_name))
		return NULL;
	gtype = lrg_mixer_asset_lookup_effect_type (type_name);
	if (gtype == G_TYPE_INVALID)
	{
		fail (ctx, LRG_MIXER_ASSET_ERROR_UNKNOWN_EFFECT, p, "unknown effect type '%s'", type_name);
		return NULL;
	}
	if (yaml_mapping_has_member (map, "id"))
	{
		g_free (p);
		p = g_strconcat (path, "/id", NULL);
		if (!read_string (ctx, yaml_mapping_get_member (map, "id"), p, &id))
			return NULL;
		if (strchr (id, '/') != NULL)
		{
			fail (ctx, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, p, "an effect id cannot contain '/'");
			return NULL;
		}
	}

	e = effect_def_new (type_name, gtype, id);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *member = yaml_mapping_get_member (map, key);
		g_autofree gchar *kp = g_strdup_printf ("%s/%s", path, key);

		if (g_strcmp0 (key, "type") == 0 || g_strcmp0 (key, "id") == 0)
			continue;
		if (g_strcmp0 (key, "enabled") == 0)
		{
			if (!read_bool (ctx, member, kp, &e->enabled))
				goto bad;
		}
		else if (g_strcmp0 (key, "params") == 0)
		{
			g_autoptr(GList)  pkeys = NULL;
			YamlMapping      *pmap;
			GList            *pl;

			if (is_empty_node (member))
				continue;
			if (!expect_mapping (ctx, member, kp))
				goto bad;
			pmap = yaml_node_get_mapping (member);
			pkeys = yaml_mapping_get_members (pmap);
			for (pl = pkeys; pl != NULL; pl = pl->next)
			{
				g_autofree gchar *pp = g_strdup_printf ("%s/%s", kp, (const gchar *) pl->data);
				g_auto(GValue)    v = G_VALUE_INIT;
				GParamSpec       *pspec = find_param_pspec (gtype, pl->data);
				const gchar      *why = NULL;
				const gchar      *text;

				if (pspec == NULL)
				{
					fail (ctx, LRG_MIXER_ASSET_ERROR_UNKNOWN_PARAM, pp, "%s has no parameter '%s'",
					      type_name, (const gchar *) pl->data);
					goto bad;
				}
				text = scalar_of (yaml_mapping_get_member (pmap, pl->data));
				if (text == NULL)
				{
					fail (ctx, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, pp, "expected a single value");
					goto bad;
				}
				if (!value_from_text (pspec, text, &v, &why))
				{
					fail (ctx, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, pp, "'%s': %s", text, why);
					goto bad;
				}
				effect_set_param (e, pspec->name, &v);
			}
		}
		else
			keep_extra (&e->extra, key, member);
	}
	return e;

bad:
	effect_def_free (e);
	return NULL;
}

static SendDef *
read_send (Ctx         *ctx,
           YamlNode    *node,
           const gchar *path)
{
	g_autoptr(GList)  keys = NULL;
	YamlMapping      *map;
	SendDef          *s;
	const gchar      *target = NULL;
	GList            *l;
	g_autofree gchar *tp = g_strconcat (path, "/target", NULL);

	if (!expect_mapping (ctx, node, path))
		return NULL;
	map = yaml_node_get_mapping (node);
	if (!yaml_mapping_has_member (map, "target"))
	{
		fail (ctx, LRG_MIXER_ASSET_ERROR_PARSE, path, "missing 'target'");
		return NULL;
	}
	if (!read_string (ctx, yaml_mapping_get_member (map, "target"), tp, &target))
		return NULL;
	s = send_def_new (target, 0.0);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		g_autofree gchar *kp = g_strdup_printf ("%s/%s", path, key);

		if (g_strcmp0 (key, "target") == 0)
			continue;
		if (g_strcmp0 (key, "level") == 0)
		{
			if (!read_db (ctx, yaml_mapping_get_member (map, key), kp, MAX_DB, &s->level))
			{
				send_def_free (s);
				return NULL;
			}
		}
		else
			keep_extra (&s->extra, key, yaml_mapping_get_member (map, key));
	}
	return s;
}

static BusDef *
read_bus (Ctx         *ctx,
          YamlNode    *node,
          const gchar *path)
{
	g_autoptr(GList)  keys = NULL;
	YamlMapping      *map;
	BusDef           *b;
	const gchar      *name = NULL;
	GList            *l;
	g_autofree gchar *np = g_strconcat (path, "/name", NULL);

	if (!expect_mapping (ctx, node, path))
		return NULL;
	map = yaml_node_get_mapping (node);
	if (!yaml_mapping_has_member (map, "name"))
	{
		fail (ctx, LRG_MIXER_ASSET_ERROR_PARSE, path, "missing 'name'");
		return NULL;
	}
	if (!read_string (ctx, yaml_mapping_get_member (map, "name"), np, &name))
		return NULL;
	b = bus_def_new (name, g_strcmp0 (name, "master") == 0 ? NULL : "master");
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *member = yaml_mapping_get_member (map, key);
		g_autofree gchar *kp = g_strdup_printf ("%s/%s", path, key);

		if (g_strcmp0 (key, "name") == 0)
			continue;
		if (g_strcmp0 (key, "parent") == 0)
		{
			const gchar *parent = NULL;

			if (!read_string (ctx, member, kp, &parent))
				goto bad;
			if (b->parent == NULL)
			{
				fail (ctx, LRG_MIXER_ASSET_ERROR_ROUTING, kp, "master has no parent");
				goto bad;
			}
			g_free (b->parent);
			b->parent = g_strdup (parent);
		}
		else if (g_strcmp0 (key, "volume") == 0)
		{
			if (!read_db (ctx, member, kp, MAX_DB, &b->volume))
				goto bad;
		}
		else if (g_strcmp0 (key, "mute") == 0)
		{
			if (!read_bool (ctx, member, kp, &b->mute))
				goto bad;
		}
		else if (g_strcmp0 (key, "solo") == 0)
		{
			if (!read_bool (ctx, member, kp, &b->solo))
				goto bad;
		}
		else if (g_strcmp0 (key, "sends") == 0 || g_strcmp0 (key, "effects") == 0)
		{
			gboolean      sends = key[0] == 's';
			YamlSequence *seq;
			guint         i;

			if (is_empty_node (member))
				continue;
			if (!expect_sequence (ctx, member, kp))
				goto bad;
			seq = yaml_node_get_sequence (member);
			for (i = 0; i < yaml_sequence_get_length (seq); i++)
			{
				g_autofree gchar *ip = g_strdup_printf ("%s/%u", kp, i);
				YamlNode         *el = yaml_sequence_get_element (seq, i);

				if (sends)
				{
					SendDef *s = read_send (ctx, el, ip);

					if (s == NULL)
						goto bad;
					if (find_send (b, s->target) != NULL)
					{
						fail (ctx, LRG_MIXER_ASSET_ERROR_ROUTING, ip, "a second send to '%s'", s->target);
						send_def_free (s);
						goto bad;
					}
					g_ptr_array_add (b->sends, s);
				}
				else
				{
					EffectDef *e = read_effect (ctx, el, ip);

					if (e == NULL)
						goto bad;
					if (effect_index (b, e->id) >= 0)
					{
						fail (ctx, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, ip,
						      "effect id '%s' is used twice on bus '%s' (give one an 'id')", e->id, name);
						effect_def_free (e);
						goto bad;
					}
					g_ptr_array_add (b->effects, e);
				}
			}
		}
		else
			keep_extra (&b->extra, key, member);
	}
	return b;

bad:
	bus_def_free (b);
	return NULL;
}

static SnapshotDef *
read_snapshot (Ctx         *ctx,
               YamlNode    *node,
               const gchar *path)
{
	g_autoptr(GList)  keys = NULL;
	YamlMapping      *map;
	SnapshotDef      *s;
	const gchar      *name = NULL;
	GList            *l;
	g_autofree gchar *np = g_strconcat (path, "/name", NULL);

	if (!expect_mapping (ctx, node, path))
		return NULL;
	map = yaml_node_get_mapping (node);
	if (!yaml_mapping_has_member (map, "name"))
	{
		fail (ctx, LRG_MIXER_ASSET_ERROR_PARSE, path, "missing 'name'");
		return NULL;
	}
	if (!read_string (ctx, yaml_mapping_get_member (map, "name"), np, &name))
		return NULL;
	s = snapshot_def_new (name, 0.0);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *member = yaml_mapping_get_member (map, key);
		g_autofree gchar *kp = g_strdup_printf ("%s/%s", path, key);

		if (g_strcmp0 (key, "name") == 0)
			continue;
		if (g_strcmp0 (key, "blend") == 0)
		{
			if (!read_number (ctx, member, kp, 0.0, 3600.0, &s->blend))
				goto bad;
		}
		else if (g_strcmp0 (key, "volumes") == 0 || g_strcmp0 (key, "params") == 0)
		{
			gboolean          volumes = key[0] == 'v';
			g_autoptr(GList)  vkeys = NULL;
			YamlMapping      *vmap;
			GList            *vl;

			if (is_empty_node (member))
				continue;
			if (!expect_mapping (ctx, member, kp))
				goto bad;
			vmap = yaml_node_get_mapping (member);
			vkeys = yaml_mapping_get_members (vmap);
			for (vl = vkeys; vl != NULL; vl = vl->next)
			{
				const gchar      *vkey = vl->data;
				g_autofree gchar *vp = g_strdup_printf ("%s/%s", kp, vkey);
				gdouble           v;

				if (volumes)
				{
					SnapVolume *sv;

					if (!read_db (ctx, yaml_mapping_get_member (vmap, vkey), vp, MAX_DB, &v))
						goto bad;
					sv = g_new0 (SnapVolume, 1);
					sv->bus = g_strdup (vkey);
					sv->db = v;
					g_ptr_array_add (s->volumes, sv);
				}
				else
				{
					g_auto(GStrv) parts = g_strsplit (vkey, "/", -1);
					SnapParam    *sp;

					if (g_strv_length (parts) != 3 || *parts[0] == '\0' || *parts[1] == '\0'
					    || *parts[2] == '\0')
					{
						fail (ctx, LRG_MIXER_ASSET_ERROR_PARSE, vp, "expected a 'bus/effect/param' key");
						goto bad;
					}
					if (!parse_number (scalar_of (yaml_mapping_get_member (vmap, vkey)), &v) || !isfinite (v))
					{
						fail (ctx, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, vp, "expected a number");
						goto bad;
					}
					sp = g_new0 (SnapParam, 1);
					sp->bus = g_strdup (parts[0]);
					sp->effect = g_strdup (parts[1]);
					sp->param = canonical_name (parts[2]);
					sp->value = v;
					g_ptr_array_add (s->params, sp);
				}
			}
		}
		else if (g_strcmp0 (key, "sends") == 0)
		{
			YamlSequence *seq;
			guint         i;

			if (is_empty_node (member))
				continue;
			if (!expect_sequence (ctx, member, kp))
				goto bad;
			seq = yaml_node_get_sequence (member);
			for (i = 0; i < yaml_sequence_get_length (seq); i++)
			{
				g_autofree gchar *ip = g_strdup_printf ("%s/%u", kp, i);
				YamlNode         *el = yaml_sequence_get_element (seq, i);
				g_autoptr(GList)  skeys = NULL;
				YamlMapping      *smap;
				GList            *sl;
				SnapSend         *ss;
				const gchar      *src = NULL;
				const gchar      *dst = NULL;
				gboolean          has_level = FALSE;

				if (!expect_mapping (ctx, el, ip))
					goto bad;
				smap = yaml_node_get_mapping (el);
				ss = g_new0 (SnapSend, 1);
				g_ptr_array_add (s->sends, ss);
				skeys = yaml_mapping_get_members (smap);
				for (sl = skeys; sl != NULL; sl = sl->next)
				{
					const gchar      *skey = sl->data;
					g_autofree gchar *sp = g_strdup_printf ("%s/%s", ip, skey);
					YamlNode         *sm = yaml_mapping_get_member (smap, skey);

					if (g_strcmp0 (skey, "source") == 0)
					{
						if (!read_string (ctx, sm, sp, &src))
							goto bad;
					}
					else if (g_strcmp0 (skey, "target") == 0)
					{
						if (!read_string (ctx, sm, sp, &dst))
							goto bad;
					}
					else if (g_strcmp0 (skey, "level") == 0)
					{
						if (!read_db (ctx, sm, sp, MAX_DB, &ss->db))
							goto bad;
						has_level = TRUE;
					}
					else
						keep_extra (&ss->extra, skey, sm);
				}
				if (src == NULL || dst == NULL || !has_level)
				{
					fail (ctx, LRG_MIXER_ASSET_ERROR_PARSE, ip, "a snapshot send needs source, target and level");
					goto bad;
				}
				ss->source = g_strdup (src);
				ss->target = g_strdup (dst);
			}
		}
		else
			keep_extra (&s->extra, key, member);
	}
	return s;

bad:
	snapshot_def_free (s);
	return NULL;
}

static DuckDef *
read_duck (Ctx         *ctx,
           YamlNode    *node,
           const gchar *path)
{
	g_autoptr(GList)  keys = NULL;
	YamlMapping      *map;
	DuckDef          *d;
	GList            *l;
	const gchar      *trigger = NULL;
	const gchar      *target = NULL;

	if (!expect_mapping (ctx, node, path))
		return NULL;
	map = yaml_node_get_mapping (node);
	d = g_new0 (DuckDef, 1);
	d->threshold = -30.0;
	d->depth = -12.0;
	d->attack = 0.05;
	d->release = 0.5;
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *member = yaml_mapping_get_member (map, key);
		g_autofree gchar *kp = g_strdup_printf ("%s/%s", path, key);
		gboolean          ok = TRUE;

		if (g_strcmp0 (key, "trigger") == 0)
			ok = read_string (ctx, member, kp, &trigger);
		else if (g_strcmp0 (key, "target") == 0)
			ok = read_string (ctx, member, kp, &target);
		else if (g_strcmp0 (key, "threshold") == 0)
			ok = read_db (ctx, member, kp, MAX_DB, &d->threshold);
		else if (g_strcmp0 (key, "depth") == 0)
			ok = read_db (ctx, member, kp, 0.0, &d->depth);
		else if (g_strcmp0 (key, "attack") == 0)
			ok = read_number (ctx, member, kp, 0.0, MAX_DUCK_TIME, &d->attack);
		else if (g_strcmp0 (key, "release") == 0)
			ok = read_number (ctx, member, kp, 0.0, MAX_DUCK_TIME, &d->release);
		else
			keep_extra (&d->extra, key, member);
		if (!ok)
		{
			duck_def_free (d);
			return NULL;
		}
	}
	if (trigger == NULL || target == NULL)
	{
		fail (ctx, LRG_MIXER_ASSET_ERROR_PARSE, path, "a ducking rule needs a trigger and a target");
		duck_def_free (d);
		return NULL;
	}
	d->trigger = g_strdup (trigger);
	d->target = g_strdup (target);
	return d;
}

static gboolean validate_with (LrgMixerAsset *self,
                               Ctx           *ctx);

static gboolean
read_asset (LrgMixerAsset *self,
            Ctx           *ctx,
            YamlNode      *root)
{
	g_autoptr(GList) keys = NULL;
	YamlMapping     *map;
	GList           *l;
	gboolean         has_buses = FALSE;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
		return fail (ctx, LRG_MIXER_ASSET_ERROR_PARSE, "/", "a mixer layout must be a YAML mapping");
	map = yaml_node_get_mapping (root);

	/* the master bus is re-read from the file when it lists one */
	g_ptr_array_set_size (self->buses, 0);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *member = yaml_mapping_get_member (map, key);
		g_autofree gchar *kp = g_strdup_printf ("/%s", key);

		if (g_strcmp0 (key, "version") == 0)
		{
			gdouble v;

			if (!read_number (ctx, member, kp, 1.0, 1e9, &v))
				return FALSE;
			if (v > LRG_MIXER_ASSET_FORMAT_VERSION)
				return fail (ctx, LRG_MIXER_ASSET_ERROR_PARSE, kp,
				             "version %g is newer than this build reads (%d)", v,
				             LRG_MIXER_ASSET_FORMAT_VERSION);
		}
		else if (g_strcmp0 (key, "name") == 0)
		{
			g_free (self->name);
			self->name = is_empty_node (member) ? NULL : g_strdup (scalar_of (member));
		}
		else if (g_strcmp0 (key, "default_snapshot") == 0)
		{
			const gchar *s = NULL;

			if (is_empty_node (member))
				continue;
			if (!read_string (ctx, member, kp, &s))
				return FALSE;
			g_free (self->default_snapshot);
			self->default_snapshot = g_strdup (s);
		}
		else if (g_strcmp0 (key, "buses") == 0 || g_strcmp0 (key, "snapshots") == 0
		         || g_strcmp0 (key, "ducking") == 0)
		{
			YamlSequence *seq;
			guint         i;

			if (key[0] == 'b')
				has_buses = TRUE;
			if (is_empty_node (member))
				continue;
			if (!expect_sequence (ctx, member, kp))
				return FALSE;
			seq = yaml_node_get_sequence (member);
			for (i = 0; i < yaml_sequence_get_length (seq); i++)
			{
				g_autofree gchar *ip = g_strdup_printf ("%s/%u", kp, i);
				YamlNode         *el = yaml_sequence_get_element (seq, i);

				if (key[0] == 'b')
				{
					BusDef *b = read_bus (ctx, el, ip);

					if (b == NULL)
						return FALSE;
					b->src_index = (gint) i;
					if (bus_index (self, b->name) >= 0)
					{
						fail (ctx, LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS, ip, "bus '%s' is defined twice", b->name);
						bus_def_free (b);
						return FALSE;
					}
					/* master always first */
					if (b->parent == NULL)
						g_ptr_array_insert (self->buses, 0, b);
					else
						g_ptr_array_add (self->buses, b);
				}
				else if (key[0] == 's')
				{
					SnapshotDef *s = read_snapshot (ctx, el, ip);

					if (s == NULL)
						return FALSE;
					if (find_snapshot (self, s->name) != NULL)
					{
						fail (ctx, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, ip, "snapshot '%s' is defined twice",
						      s->name);
						snapshot_def_free (s);
						return FALSE;
					}
					g_ptr_array_add (self->snapshots, s);
				}
				else
				{
					DuckDef *d = read_duck (ctx, el, ip);

					if (d == NULL)
						return FALSE;
					g_ptr_array_add (self->ducks, d);
				}
			}
		}
		else
			keep_extra (&self->extra, key, member);
	}
	if (!has_buses)
		return fail (ctx, LRG_MIXER_ASSET_ERROR_PARSE, "/", "not a mixer layout: no 'buses' list");
	if (bus_index (self, "master") < 0)
		g_ptr_array_insert (self->buses, 0, bus_def_new ("master", NULL));
	return validate_with (self, ctx);
}

LrgMixerAsset *
lrg_mixer_asset_new_from_data (const gchar  *data,
                               gssize        length,
                               GError      **error)
{
	g_autoptr(LrgMixerAsset) self = NULL;
	g_autoptr(YamlParser)    parser = NULL;
	g_autoptr(GError)        local = NULL;
	Ctx                      ctx = { "<data>", error };

	g_return_val_if_fail (data != NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_PARSE, "<data>: %s",
		             local->message);
		return NULL;
	}
	self = lrg_mixer_asset_new ();
	if (!read_asset (self, &ctx, yaml_parser_get_root (parser)))
		return NULL;
	return g_steal_pointer (&self);
}

LrgMixerAsset *
lrg_mixer_asset_new_from_file (const gchar  *path,
                               GError      **error)
{
	g_autoptr(LrgMixerAsset) self = NULL;
	g_autoptr(YamlParser)    parser = NULL;
	g_autoptr(GError)        local = NULL;
	g_autofree gchar        *text = NULL;
	gsize                    length = 0;
	Ctx                      ctx = { path, error };

	g_return_val_if_fail (path != NULL, NULL);

	if (!g_file_get_contents (path, &text, &length, &local))
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_IO, "%s: %s", path,
		             local->message);
		return NULL;
	}
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, text, (gssize) length, &local))
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_PARSE, "%s: %s", path,
		             local->message);
		return NULL;
	}
	self = lrg_mixer_asset_new ();
	if (!read_asset (self, &ctx, yaml_parser_get_root (parser)))
		return NULL;
	return g_steal_pointer (&self);
}

/* ==========================================================================
 * Validation (shared by the loader and lrg_mixer_asset_validate ())
 * ========================================================================== */

/*
 * Graph: one node per bus; edges child -> parent, send source -> target
 * (base and snapshot sends) and duck trigger -> target.  Each edge is
 * checked against the ones before it, so the error names the first one
 * that closes a loop.
 */
typedef struct
{
	guint  n;
	GArray *edges;   /* pairs of guint (from, to) */
} Graph;

static gboolean
graph_reaches (Graph    *g,
               guint     from,
               guint     to,
               gboolean *seen)
{
	guint i;

	if (from == to)
		return TRUE;
	if (seen[from])
		return FALSE;
	seen[from] = TRUE;
	for (i = 0; i + 1 < g->edges->len; i += 2)
		if (g_array_index (g->edges, guint, i) == from
		    && graph_reaches (g, g_array_index (g->edges, guint, i + 1), to, seen))
			return TRUE;
	return FALSE;
}

/* adds @from -> @to unless it closes a loop */
static gboolean
graph_add (Graph *g,
           guint  from,
           guint  to)
{
	g_autofree gboolean *seen = g_new0 (gboolean, g->n);

	if (graph_reaches (g, to, from, seen))
		return FALSE;
	g_array_append_val (g->edges, from);
	g_array_append_val (g->edges, to);
	return TRUE;
}

/* the index a bus has in the file (its position when edited in memory) */
static guint
bus_path_index (LrgMixerAsset *self,
                guint          i)
{
	BusDef *b = g_ptr_array_index (self->buses, i);

	return b->src_index >= 0 ? (guint) b->src_index : i;
}

static gboolean
validate_with (LrgMixerAsset *self,
               Ctx           *ctx)
{
	g_autoptr(GArray) edges = g_array_new (FALSE, FALSE, sizeof (guint));
	Graph             graph;
	guint             i, j;

	graph.n = self->buses->len;
	graph.edges = edges;

	/* buses: master first without a parent, every parent known, a tree */
	if (self->buses->len == 0 || ((BusDef *) g_ptr_array_index (self->buses, 0))->parent != NULL)
		return fail (ctx, LRG_MIXER_ASSET_ERROR_ROUTING, "/buses", "the master bus is missing");
	for (i = 1; i < self->buses->len; i++)
	{
		BusDef           *b = g_ptr_array_index (self->buses, i);
		g_autofree gchar *p = g_strdup_printf ("/buses/%u/parent", bus_path_index (self, i));
		gint              pi = bus_index (self, b->parent);

		if (b->parent == NULL)
			return fail (ctx, LRG_MIXER_ASSET_ERROR_ROUTING, p, "only master has no parent");
		if (pi < 0)
			return fail (ctx, LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS, p, "unknown bus '%s'", b->parent);
		if (!graph_add (&graph, i, (guint) pi))
			return fail (ctx, LRG_MIXER_ASSET_ERROR_ROUTING, p, "'%s' -> '%s' makes a parent loop",
			             b->name, b->parent);
	}

	/* base sends */
	for (i = 0; i < self->buses->len; i++)
	{
		BusDef *b = g_ptr_array_index (self->buses, i);

		for (j = 0; j < b->sends->len; j++)
		{
			SendDef          *s = g_ptr_array_index (b->sends, j);
			g_autofree gchar *p = g_strdup_printf ("/buses/%u/sends/%u/target", bus_path_index (self, i), j);
			gint              ti = bus_index (self, s->target);

			if (i == 0)
				return fail (ctx, LRG_MIXER_ASSET_ERROR_ROUTING, p, "master cannot send");
			if (ti < 0)
				return fail (ctx, LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS, p, "unknown bus '%s'", s->target);
			if (g_strcmp0 (s->target, b->parent) == 0 || ti == (gint) i)
				return fail (ctx, LRG_MIXER_ASSET_ERROR_ROUTING, p,
				             "a send cannot go to the bus itself or its parent");
			if (!graph_add (&graph, i, (guint) ti))
				return fail (ctx, LRG_MIXER_ASSET_ERROR_ROUTING, p, "send '%s' -> '%s' makes a loop", b->name,
				             s->target);
		}
	}

	/* ducking rules */
	for (i = 0; i < self->ducks->len; i++)
	{
		DuckDef          *d = g_ptr_array_index (self->ducks, i);
		g_autofree gchar *p = g_strdup_printf ("/ducking/%u", i);
		gint              ti = bus_index (self, d->trigger);
		gint              gi = bus_index (self, d->target);

		if (ti < 0)
			return fail (ctx, LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS, p, "unknown trigger bus '%s'", d->trigger);
		if (gi < 0)
			return fail (ctx, LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS, p, "unknown target bus '%s'", d->target);
		if (ti == gi)
			return fail (ctx, LRG_MIXER_ASSET_ERROR_ROUTING, p, "a bus cannot duck itself");
		for (j = 0; j < i; j++)
		{
			DuckDef *o = g_ptr_array_index (self->ducks, j);

			if (g_strcmp0 (o->trigger, d->trigger) == 0 && g_strcmp0 (o->target, d->target) == 0)
				return fail (ctx, LRG_MIXER_ASSET_ERROR_ROUTING, p, "a second rule '%s' -> '%s'",
				             d->trigger, d->target);
		}
		if (!graph_add (&graph, (guint) ti, (guint) gi))
			return fail (ctx, LRG_MIXER_ASSET_ERROR_ROUTING, p,
			             "'%s' feeds '%s', so it cannot be ducked by it", d->target, d->trigger);
	}

	/* snapshots */
	for (i = 0; i < self->snapshots->len; i++)
	{
		SnapshotDef *s = g_ptr_array_index (self->snapshots, i);

		for (j = 0; j < s->volumes->len; j++)
		{
			SnapVolume       *v = g_ptr_array_index (s->volumes, j);
			g_autofree gchar *p = g_strdup_printf ("/snapshots/%u/volumes/%s", i, v->bus);

			if (bus_index (self, v->bus) < 0)
				return fail (ctx, LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS, p, "unknown bus '%s'", v->bus);
		}
		for (j = 0; j < s->sends->len; j++)
		{
			SnapSend         *v = g_ptr_array_index (s->sends, j);
			g_autofree gchar *p = g_strdup_printf ("/snapshots/%u/sends/%u", i, j);
			gint              si = bus_index (self, v->source);
			gint              ti = bus_index (self, v->target);
			BusDef           *src;

			if (si < 0 || ti < 0)
				return fail (ctx, LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS, p, "unknown bus '%s'",
				             si < 0 ? v->source : v->target);
			src = g_ptr_array_index (self->buses, si);
			if (find_send (src, v->target) != NULL)
				continue;
			if (si == 0 || si == ti || g_strcmp0 (src->parent, v->target) == 0)
				return fail (ctx, LRG_MIXER_ASSET_ERROR_ROUTING, p, "send '%s' -> '%s' is not allowed",
				             v->source, v->target);
			if (!graph_add (&graph, (guint) si, (guint) ti))
				return fail (ctx, LRG_MIXER_ASSET_ERROR_ROUTING, p, "send '%s' -> '%s' makes a loop",
				             v->source, v->target);
		}
		for (j = 0; j < s->params->len; j++)
		{
			SnapParam        *v = g_ptr_array_index (s->params, j);
			g_autofree gchar *p = g_strdup_printf ("/snapshots/%u/params/%s/%s/%s", i, v->bus, v->effect,
			                                       v->param);
			EffectDef        *e;
			GParamSpec       *pspec;

			if (bus_index (self, v->bus) < 0)
				return fail (ctx, LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS, p, "unknown bus '%s'", v->bus);
			e = find_effect (self, v->bus, v->effect);
			if (e == NULL)
				return fail (ctx, LRG_MIXER_ASSET_ERROR_UNKNOWN_EFFECT, p, "bus '%s' has no effect '%s'",
				             v->bus, v->effect);
			pspec = find_param_pspec (e->gtype, v->param);
			if (pspec == NULL)
				return fail (ctx, LRG_MIXER_ASSET_ERROR_UNKNOWN_PARAM, p, "%s has no parameter '%s'",
				             e->type_name, v->param);
			if (!is_numeric_type (pspec->value_type))
				return fail (ctx, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, p,
				             "only numeric parameters can be in a snapshot");
		}
	}
	if (self->default_snapshot != NULL && find_snapshot (self, self->default_snapshot) == NULL)
		return fail (ctx, LRG_MIXER_ASSET_ERROR_UNKNOWN_SNAPSHOT, "/default_snapshot",
		             "unknown snapshot '%s'", self->default_snapshot);
	return TRUE;
}

gboolean
lrg_mixer_asset_validate (LrgMixerAsset  *self,
                          GError        **error)
{
	Ctx ctx = { "<mixer>", error };

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	return validate_with (self, &ctx);
}

/* ==========================================================================
 * YAML out
 * ========================================================================== */

static void
set_node (YamlMapping *map,
          const gchar *key,
          YamlNode    *node)
{
	yaml_mapping_set_member (map, key, node);
	yaml_node_unref (node);
}

static void
copy_extra (YamlMapping *map,
            YamlMapping *extra)
{
	g_autoptr(GList) keys = NULL;
	GList           *l;

	if (extra == NULL)
		return;
	keys = yaml_mapping_get_members (extra);
	for (l = keys; l != NULL; l = l->next)
		yaml_mapping_set_member (map, l->data, yaml_mapping_get_member (extra, l->data));
}

/* a new mapping node; *out_map borrows its mapping */
static YamlNode *
new_map_node (YamlMapping **out_map)
{
	YamlNode *node = yaml_node_new_mapping (NULL);

	*out_map = yaml_node_get_mapping (node);
	return node;
}

static YamlNode *
new_seq_node (YamlSequence **out_seq)
{
	YamlNode *node = yaml_node_new_sequence (NULL);

	*out_seq = yaml_node_get_sequence (node);
	return node;
}

static YamlNode *
bus_to_node (BusDef *b)
{
	YamlMapping *map;
	YamlNode    *node = new_map_node (&map);
	guint        i, j;

	set_node (map, "name", lrg_data_yaml_string (b->name));
	if (b->parent != NULL)
		set_node (map, "parent", lrg_data_yaml_string (b->parent));
	set_node (map, "volume", lrg_data_yaml_double (b->volume));
	if (b->mute)
		set_node (map, "mute", lrg_data_yaml_boolean (TRUE));
	if (b->solo)
		set_node (map, "solo", lrg_data_yaml_boolean (TRUE));
	if (b->sends->len > 0)
	{
		YamlSequence *seq;
		YamlNode     *snode = new_seq_node (&seq);

		for (i = 0; i < b->sends->len; i++)
		{
			SendDef     *s = g_ptr_array_index (b->sends, i);
			YamlMapping *smap;
			YamlNode    *el = new_map_node (&smap);

			set_node (smap, "target", lrg_data_yaml_string (s->target));
			set_node (smap, "level", lrg_data_yaml_double (s->level));
			copy_extra (smap, s->extra);
			yaml_sequence_add_element (seq, el);
			yaml_node_unref (el);
		}
		set_node (map, "sends", snode);
	}
	if (b->effects->len > 0)
	{
		YamlSequence *seq;
		YamlNode     *enode = new_seq_node (&seq);

		for (i = 0; i < b->effects->len; i++)
		{
			EffectDef   *e = g_ptr_array_index (b->effects, i);
			YamlMapping *emap;
			YamlNode    *el = new_map_node (&emap);

			set_node (emap, "type", lrg_data_yaml_string (e->type_name));
			if (g_strcmp0 (e->id, e->type_name) != 0)
				set_node (emap, "id", lrg_data_yaml_string (e->id));
			if (!e->enabled)
				set_node (emap, "enabled", lrg_data_yaml_boolean (FALSE));
			if (e->params->len > 0)
			{
				YamlMapping *pmap;
				YamlNode    *pnode = new_map_node (&pmap);

				for (j = 0; j < e->params->len; j++)
				{
					Param *p = g_ptr_array_index (e->params, j);

					set_node (pmap, p->name, value_to_node (&p->value));
				}
				set_node (emap, "params", pnode);
			}
			copy_extra (emap, e->extra);
			yaml_sequence_add_element (seq, el);
			yaml_node_unref (el);
		}
		set_node (map, "effects", enode);
	}
	copy_extra (map, b->extra);
	return node;
}

static YamlNode *
snapshot_to_node (SnapshotDef *s)
{
	YamlMapping *map;
	YamlNode    *node = new_map_node (&map);
	guint        i;

	set_node (map, "name", lrg_data_yaml_string (s->name));
	set_node (map, "blend", lrg_data_yaml_double (s->blend));
	if (s->volumes->len > 0)
	{
		YamlMapping *vmap;
		YamlNode    *vnode = new_map_node (&vmap);

		for (i = 0; i < s->volumes->len; i++)
		{
			SnapVolume *v = g_ptr_array_index (s->volumes, i);

			set_node (vmap, v->bus, lrg_data_yaml_double (v->db));
		}
		set_node (map, "volumes", vnode);
	}
	if (s->sends->len > 0)
	{
		YamlSequence *seq;
		YamlNode     *snode = new_seq_node (&seq);

		for (i = 0; i < s->sends->len; i++)
		{
			SnapSend    *v = g_ptr_array_index (s->sends, i);
			YamlMapping *smap;
			YamlNode    *el = new_map_node (&smap);

			set_node (smap, "source", lrg_data_yaml_string (v->source));
			set_node (smap, "target", lrg_data_yaml_string (v->target));
			set_node (smap, "level", lrg_data_yaml_double (v->db));
			copy_extra (smap, v->extra);
			yaml_sequence_add_element (seq, el);
			yaml_node_unref (el);
		}
		set_node (map, "sends", snode);
	}
	if (s->params->len > 0)
	{
		YamlMapping *pmap;
		YamlNode    *pnode = new_map_node (&pmap);

		for (i = 0; i < s->params->len; i++)
		{
			SnapParam        *v = g_ptr_array_index (s->params, i);
			g_autofree gchar *key = g_strdup_printf ("%s/%s/%s", v->bus, v->effect, v->param);

			set_node (pmap, key, lrg_data_yaml_double (v->value));
		}
		set_node (map, "params", pnode);
	}
	copy_extra (map, s->extra);
	return node;
}

static YamlNode *
duck_to_node (DuckDef *d)
{
	YamlMapping *map;
	YamlNode    *node = new_map_node (&map);

	set_node (map, "trigger", lrg_data_yaml_string (d->trigger));
	set_node (map, "target", lrg_data_yaml_string (d->target));
	set_node (map, "threshold", lrg_data_yaml_double (d->threshold));
	set_node (map, "depth", lrg_data_yaml_double (d->depth));
	set_node (map, "attack", lrg_data_yaml_double (d->attack));
	set_node (map, "release", lrg_data_yaml_double (d->release));
	copy_extra (map, d->extra);
	return node;
}

gchar *
lrg_mixer_asset_to_data (LrgMixerAsset  *self,
                         gsize          *length,
                         GError        **error)
{
	g_autoptr(YamlNode) root = NULL;
	YamlMapping        *map;
	guint               i;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), NULL);

	root = new_map_node (&map);
	set_node (map, "version", lrg_data_yaml_int (LRG_MIXER_ASSET_FORMAT_VERSION));
	if (self->name != NULL)
		set_node (map, "name", lrg_data_yaml_string (self->name));
	if (self->default_snapshot != NULL)
		set_node (map, "default_snapshot", lrg_data_yaml_string (self->default_snapshot));

	{
		YamlSequence *seq;
		YamlNode     *bnode = new_seq_node (&seq);

		for (i = 0; i < self->buses->len; i++)
		{
			YamlNode *el = bus_to_node (g_ptr_array_index (self->buses, i));

			yaml_sequence_add_element (seq, el);
			yaml_node_unref (el);
		}
		set_node (map, "buses", bnode);
	}
	if (self->snapshots->len > 0)
	{
		YamlSequence *seq;
		YamlNode     *snode = new_seq_node (&seq);

		for (i = 0; i < self->snapshots->len; i++)
		{
			YamlNode *el = snapshot_to_node (g_ptr_array_index (self->snapshots, i));

			yaml_sequence_add_element (seq, el);
			yaml_node_unref (el);
		}
		set_node (map, "snapshots", snode);
	}
	if (self->ducks->len > 0)
	{
		YamlSequence *seq;
		YamlNode     *dnode = new_seq_node (&seq);

		for (i = 0; i < self->ducks->len; i++)
		{
			YamlNode *el = duck_to_node (g_ptr_array_index (self->ducks, i));

			yaml_sequence_add_element (seq, el);
			yaml_node_unref (el);
		}
		set_node (map, "ducking", dnode);
	}
	copy_extra (map, self->extra);
	return lrg_data_yaml_to_data (root, length, error);
}

gboolean
lrg_mixer_asset_save_to_file (LrgMixerAsset  *self,
                              const gchar    *path,
                              GError        **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;
	gsize             length = 0;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = lrg_mixer_asset_to_data (self, &length, error);
	if (text == NULL)
		return FALSE;
	if (!g_file_set_contents (path, text, (gssize) length, &local))
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_IO, "%s: %s", path,
		             local->message);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Construction and simple accessors
 * ========================================================================== */

LrgMixerAsset *
lrg_mixer_asset_new (void)
{
	return g_object_new (LRG_TYPE_MIXER_ASSET, NULL);
}

LrgMixerAsset *
lrg_mixer_asset_copy (LrgMixerAsset *self)
{
	LrgMixerAsset *copy;
	guint          i;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), NULL);

	copy = lrg_mixer_asset_new ();
	g_ptr_array_set_size (copy->buses, 0);
	copy->name = g_strdup (self->name);
	copy->default_snapshot = g_strdup (self->default_snapshot);
	for (i = 0; i < self->buses->len; i++)
		g_ptr_array_add (copy->buses, bus_def_copy (g_ptr_array_index (self->buses, i)));
	for (i = 0; i < self->snapshots->len; i++)
		g_ptr_array_add (copy->snapshots, snapshot_def_copy (g_ptr_array_index (self->snapshots, i)));
	for (i = 0; i < self->ducks->len; i++)
		g_ptr_array_add (copy->ducks, duck_def_copy (g_ptr_array_index (self->ducks, i)));
	copy->extra = extra_ref (self->extra);
	return copy;
}

const gchar *
lrg_mixer_asset_get_name (LrgMixerAsset *self)
{
	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), NULL);
	return self->name;
}

void
lrg_mixer_asset_set_name (LrgMixerAsset *self,
                          const gchar   *name)
{
	g_return_if_fail (LRG_IS_MIXER_ASSET (self));
	if (g_strcmp0 (self->name, name) == 0)
		return;
	g_free (self->name);
	self->name = g_strdup (name);
	changed (self);
}

/* --- buses --------------------------------------------------------------- */

gboolean
lrg_mixer_asset_add_bus (LrgMixerAsset *self,
                         const gchar   *name,
                         const gchar   *parent)
{
	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	if (parent == NULL)
		parent = "master";
	if (name == NULL || *name == '\0' || bus_index (self, name) >= 0 || bus_index (self, parent) < 0)
		return FALSE;
	g_ptr_array_add (self->buses, bus_def_new (name, parent));
	changed (self);
	return TRUE;
}

gboolean
lrg_mixer_asset_remove_bus (LrgMixerAsset *self,
                            const gchar   *name)
{
	gint  index;
	guint i, j;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	index = bus_index (self, name);
	if (index <= 0)
		return FALSE;
	for (i = 0; i < self->buses->len; i++)
		if (g_strcmp0 (((BusDef *) g_ptr_array_index (self->buses, i))->parent, name) == 0)
			return FALSE;

	/* every reference goes with it */
	for (i = 0; i < self->buses->len; i++)
	{
		BusDef *b = g_ptr_array_index (self->buses, i);

		for (j = b->sends->len; j > 0; j--)
			if (g_strcmp0 (((SendDef *) g_ptr_array_index (b->sends, j - 1))->target, name) == 0)
				g_ptr_array_remove_index (b->sends, j - 1);
	}
	for (i = 0; i < self->snapshots->len; i++)
	{
		SnapshotDef *s = g_ptr_array_index (self->snapshots, i);

		for (j = s->volumes->len; j > 0; j--)
			if (g_strcmp0 (((SnapVolume *) g_ptr_array_index (s->volumes, j - 1))->bus, name) == 0)
				g_ptr_array_remove_index (s->volumes, j - 1);
		for (j = s->sends->len; j > 0; j--)
		{
			SnapSend *v = g_ptr_array_index (s->sends, j - 1);

			if (g_strcmp0 (v->source, name) == 0 || g_strcmp0 (v->target, name) == 0)
				g_ptr_array_remove_index (s->sends, j - 1);
		}
		for (j = s->params->len; j > 0; j--)
			if (g_strcmp0 (((SnapParam *) g_ptr_array_index (s->params, j - 1))->bus, name) == 0)
				g_ptr_array_remove_index (s->params, j - 1);
	}
	for (j = self->ducks->len; j > 0; j--)
	{
		DuckDef *d = g_ptr_array_index (self->ducks, j - 1);

		if (g_strcmp0 (d->trigger, name) == 0 || g_strcmp0 (d->target, name) == 0)
			g_ptr_array_remove_index (self->ducks, j - 1);
	}
	g_ptr_array_remove_index (self->buses, (guint) index);
	changed (self);
	return TRUE;
}

gchar **
lrg_mixer_asset_get_bus_names (LrgMixerAsset *self)
{
	GPtrArray *names;
	guint      i;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), NULL);
	names = g_ptr_array_new ();
	for (i = 0; i < self->buses->len; i++)
		g_ptr_array_add (names, g_strdup (((BusDef *) g_ptr_array_index (self->buses, i))->name));
	g_ptr_array_add (names, NULL);
	return (gchar **) g_ptr_array_free (names, FALSE);
}

gboolean
lrg_mixer_asset_has_bus (LrgMixerAsset *self,
                         const gchar   *name)
{
	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	return bus_index (self, name) >= 0;
}

const gchar *
lrg_mixer_asset_get_bus_parent (LrgMixerAsset *self,
                                const gchar   *name)
{
	BusDef *b;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), NULL);
	b = find_bus (self, name);
	return b != NULL ? b->parent : NULL;
}

gboolean
lrg_mixer_asset_set_bus_parent (LrgMixerAsset *self,
                                const gchar   *name,
                                const gchar   *parent)
{
	BusDef      *b;
	const gchar *walk;
	guint        depth;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	if (parent == NULL)
		parent = "master";
	b = find_bus (self, name);
	if (b == NULL || b->parent == NULL || bus_index (self, parent) < 0)
		return FALSE;

	/* the new parent must not be the bus or below it */
	for (walk = parent, depth = 0; walk != NULL && depth <= self->buses->len; depth++)
	{
		BusDef *w;

		if (g_strcmp0 (walk, name) == 0)
			return FALSE;
		w = find_bus (self, walk);
		walk = w != NULL ? w->parent : NULL;
	}
	if (g_strcmp0 (b->parent, parent) == 0)
		return TRUE;
	g_free (b->parent);
	b->parent = g_strdup (parent);
	changed (self);
	return TRUE;
}

gboolean
lrg_mixer_asset_set_bus_volume (LrgMixerAsset *self,
                                const gchar   *name,
                                gdouble        volume_db)
{
	BusDef *b;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	b = find_bus (self, name);
	if (b == NULL)
		return FALSE;
	b->volume = clamp_db (volume_db);
	changed (self);
	return TRUE;
}

gdouble
lrg_mixer_asset_get_bus_volume (LrgMixerAsset *self,
                                const gchar   *name)
{
	BusDef *b;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), LRG_AUDIO_SILENCE_DB);
	b = find_bus (self, name);
	return b != NULL ? b->volume : LRG_AUDIO_SILENCE_DB;
}

gboolean
lrg_mixer_asset_set_bus_mute (LrgMixerAsset *self,
                              const gchar   *name,
                              gboolean       mute)
{
	BusDef *b;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	b = find_bus (self, name);
	if (b == NULL)
		return FALSE;
	b->mute = !!mute;
	changed (self);
	return TRUE;
}

gboolean
lrg_mixer_asset_get_bus_mute (LrgMixerAsset *self,
                              const gchar   *name)
{
	BusDef *b;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	b = find_bus (self, name);
	return b != NULL && b->mute;
}

gboolean
lrg_mixer_asset_set_bus_solo (LrgMixerAsset *self,
                              const gchar   *name,
                              gboolean       solo)
{
	BusDef *b;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	b = find_bus (self, name);
	if (b == NULL)
		return FALSE;
	b->solo = !!solo;
	changed (self);
	return TRUE;
}

gboolean
lrg_mixer_asset_get_bus_solo (LrgMixerAsset *self,
                              const gchar   *name)
{
	BusDef *b;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	b = find_bus (self, name);
	return b != NULL && b->solo;
}

/* --- sends --------------------------------------------------------------- */

gboolean
lrg_mixer_asset_set_send (LrgMixerAsset *self,
                          const gchar   *source,
                          const gchar   *target,
                          gdouble        level_db)
{
	BusDef  *b;
	SendDef *s;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	b = find_bus (self, source);
	if (b == NULL || b->parent == NULL || bus_index (self, target) < 0 || g_strcmp0 (source, target) == 0)
		return FALSE;
	s = find_send (b, target);
	if (s == NULL)
		g_ptr_array_add (b->sends, send_def_new (target, clamp_db (level_db)));
	else
		s->level = clamp_db (level_db);
	changed (self);
	return TRUE;
}

gboolean
lrg_mixer_asset_remove_send (LrgMixerAsset *self,
                             const gchar   *source,
                             const gchar   *target)
{
	BusDef *b;
	guint   i;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	b = find_bus (self, source);
	for (i = 0; b != NULL && i < b->sends->len; i++)
		if (g_strcmp0 (((SendDef *) g_ptr_array_index (b->sends, i))->target, target) == 0)
		{
			g_ptr_array_remove_index (b->sends, i);
			changed (self);
			return TRUE;
		}
	return FALSE;
}

gboolean
lrg_mixer_asset_get_send (LrgMixerAsset *self,
                          const gchar   *source,
                          const gchar   *target,
                          gdouble       *out_level_db)
{
	SendDef *s;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	s = find_send (find_bus (self, source), target);
	if (s == NULL)
		return FALSE;
	if (out_level_db != NULL)
		*out_level_db = s->level;
	return TRUE;
}

gchar **
lrg_mixer_asset_get_send_targets (LrgMixerAsset *self,
                                  const gchar   *source)
{
	GPtrArray *names;
	BusDef    *b;
	guint      i;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), NULL);
	names = g_ptr_array_new ();
	b = find_bus (self, source);
	for (i = 0; b != NULL && i < b->sends->len; i++)
		g_ptr_array_add (names, g_strdup (((SendDef *) g_ptr_array_index (b->sends, i))->target));
	g_ptr_array_add (names, NULL);
	return (gchar **) g_ptr_array_free (names, FALSE);
}

/* --- effects ------------------------------------------------------------- */

gboolean
lrg_mixer_asset_add_effect (LrgMixerAsset  *self,
                            const gchar    *bus,
                            const gchar    *type_name,
                            const gchar    *id,
                            GError        **error)
{
	BusDef *b;
	GType   gtype;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	g_return_val_if_fail (type_name != NULL, FALSE);

	b = find_bus (self, bus);
	if (b == NULL)
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS, "unknown bus '%s'",
		             bus != NULL ? bus : "(null)");
		return FALSE;
	}
	gtype = lrg_mixer_asset_lookup_effect_type (type_name);
	if (gtype == G_TYPE_INVALID)
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_UNKNOWN_EFFECT,
		             "unknown effect type '%s'", type_name);
		return FALSE;
	}
	if (id == NULL)
		id = type_name;
	if (*id == '\0' || strchr (id, '/') != NULL || effect_index (b, id) >= 0)
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_INVALID_VALUE,
		             "bus '%s': effect id '%s' is empty, contains '/' or is taken", bus, id);
		return FALSE;
	}
	g_ptr_array_add (b->effects, effect_def_new (type_name, gtype, id));
	changed (self);
	return TRUE;
}

gboolean
lrg_mixer_asset_remove_effect (LrgMixerAsset *self,
                               const gchar   *bus,
                               const gchar   *id)
{
	BusDef *b;
	gint    index;
	guint   i, j;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	b = find_bus (self, bus);
	index = effect_index (b, id);
	if (index < 0)
		return FALSE;
	for (i = 0; i < self->snapshots->len; i++)
	{
		SnapshotDef *s = g_ptr_array_index (self->snapshots, i);

		for (j = s->params->len; j > 0; j--)
		{
			SnapParam *p = g_ptr_array_index (s->params, j - 1);

			if (g_strcmp0 (p->bus, bus) == 0 && g_strcmp0 (p->effect, id) == 0)
				g_ptr_array_remove_index (s->params, j - 1);
		}
	}
	g_ptr_array_remove_index (b->effects, (guint) index);
	changed (self);
	return TRUE;
}

gboolean
lrg_mixer_asset_move_effect (LrgMixerAsset *self,
                             const gchar   *bus,
                             const gchar   *id,
                             guint          index)
{
	BusDef    *b;
	gint       from;
	EffectDef *e;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	b = find_bus (self, bus);
	from = effect_index (b, id);
	if (from < 0)
		return FALSE;
	e = g_ptr_array_steal_index (b->effects, (guint) from);
	g_ptr_array_insert (b->effects, (gint) MIN (index, b->effects->len), e);
	changed (self);
	return TRUE;
}

gchar **
lrg_mixer_asset_get_effect_ids (LrgMixerAsset *self,
                                const gchar   *bus)
{
	GPtrArray *names;
	BusDef    *b;
	guint      i;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), NULL);
	names = g_ptr_array_new ();
	b = find_bus (self, bus);
	for (i = 0; b != NULL && i < b->effects->len; i++)
		g_ptr_array_add (names, g_strdup (((EffectDef *) g_ptr_array_index (b->effects, i))->id));
	g_ptr_array_add (names, NULL);
	return (gchar **) g_ptr_array_free (names, FALSE);
}

const gchar *
lrg_mixer_asset_get_effect_type (LrgMixerAsset *self,
                                 const gchar   *bus,
                                 const gchar   *id)
{
	EffectDef *e;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), NULL);
	e = find_effect (self, bus, id);
	return e != NULL ? e->type_name : NULL;
}

gboolean
lrg_mixer_asset_set_effect_enabled (LrgMixerAsset *self,
                                    const gchar   *bus,
                                    const gchar   *id,
                                    gboolean       enabled)
{
	EffectDef *e;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	e = find_effect (self, bus, id);
	if (e == NULL)
		return FALSE;
	e->enabled = !!enabled;
	changed (self);
	return TRUE;
}

gboolean
lrg_mixer_asset_get_effect_enabled (LrgMixerAsset *self,
                                    const gchar   *bus,
                                    const gchar   *id)
{
	EffectDef *e;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	e = find_effect (self, bus, id);
	return e != NULL && e->enabled;
}

gboolean
lrg_mixer_asset_set_effect_param (LrgMixerAsset  *self,
                                  const gchar    *bus,
                                  const gchar    *id,
                                  const gchar    *param,
                                  const GValue   *value,
                                  GError        **error)
{
	EffectDef     *e;
	GParamSpec    *pspec;
	g_auto(GValue) v = G_VALUE_INIT;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	g_return_val_if_fail (param != NULL, FALSE);
	g_return_val_if_fail (G_IS_VALUE (value), FALSE);

	e = find_effect (self, bus, id);
	if (e == NULL)
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_UNKNOWN_EFFECT,
		             "bus '%s' has no effect '%s'", bus != NULL ? bus : "(null)", id != NULL ? id : "(null)");
		return FALSE;
	}
	pspec = find_param_pspec (e->gtype, param);
	if (pspec == NULL)
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_UNKNOWN_PARAM,
		             "%s has no parameter '%s'", e->type_name, param);
		return FALSE;
	}
	g_value_init (&v, pspec->value_type);
	if (G_VALUE_HOLDS_STRING (value) && !G_VALUE_HOLDS_STRING (&v))
	{
		const gchar *why = NULL;

		g_value_unset (&v);
		if (!value_from_text (pspec, g_value_get_string (value), &v, &why))
		{
			g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, "%s: %s", param, why);
			return FALSE;
		}
	}
	else if (!g_value_transform (value, &v) || g_param_value_validate (pspec, &v))
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_INVALID_VALUE,
		             "%s: wrong type or out of the parameter's range", param);
		return FALSE;
	}
	effect_set_param (e, pspec->name, &v);
	changed (self);
	return TRUE;
}

gboolean
lrg_mixer_asset_get_effect_param (LrgMixerAsset *self,
                                  const gchar   *bus,
                                  const gchar   *id,
                                  const gchar   *param,
                                  GValue        *out_value)
{
	EffectDef *e;
	Param     *p;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	g_return_val_if_fail (out_value != NULL, FALSE);
	e = find_effect (self, bus, id);
	p = (e != NULL && param != NULL) ? effect_find_param (e, param) : NULL;
	if (p == NULL)
		return FALSE;
	g_value_init (out_value, G_VALUE_TYPE (&p->value));
	g_value_copy (&p->value, out_value);
	return TRUE;
}

gboolean
lrg_mixer_asset_unset_effect_param (LrgMixerAsset *self,
                                    const gchar   *bus,
                                    const gchar   *id,
                                    const gchar   *param)
{
	EffectDef *e;
	Param     *p;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	e = find_effect (self, bus, id);
	p = (e != NULL && param != NULL) ? effect_find_param (e, param) : NULL;
	if (p == NULL)
		return FALSE;
	g_ptr_array_remove (e->params, p);
	changed (self);
	return TRUE;
}

/* --- snapshots ----------------------------------------------------------- */

gboolean
lrg_mixer_asset_add_snapshot (LrgMixerAsset *self,
                              const gchar   *name,
                              gdouble        blend_seconds)
{
	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	if (name == NULL || *name == '\0' || find_snapshot (self, name) != NULL || !isfinite (blend_seconds)
	    || blend_seconds < 0.0)
		return FALSE;
	g_ptr_array_add (self->snapshots, snapshot_def_new (name, blend_seconds));
	changed (self);
	return TRUE;
}

gboolean
lrg_mixer_asset_remove_snapshot (LrgMixerAsset *self,
                                 const gchar   *name)
{
	SnapshotDef *s;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	s = find_snapshot (self, name);
	if (s == NULL)
		return FALSE;
	if (g_strcmp0 (self->default_snapshot, name) == 0)
		g_clear_pointer (&self->default_snapshot, g_free);
	g_ptr_array_remove (self->snapshots, s);
	changed (self);
	return TRUE;
}

gchar **
lrg_mixer_asset_get_snapshot_names (LrgMixerAsset *self)
{
	GPtrArray *names;
	guint      i;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), NULL);
	names = g_ptr_array_new ();
	for (i = 0; i < self->snapshots->len; i++)
		g_ptr_array_add (names, g_strdup (((SnapshotDef *) g_ptr_array_index (self->snapshots, i))->name));
	g_ptr_array_add (names, NULL);
	return (gchar **) g_ptr_array_free (names, FALSE);
}

gboolean
lrg_mixer_asset_set_snapshot_blend (LrgMixerAsset *self,
                                    const gchar   *name,
                                    gdouble        blend_seconds)
{
	SnapshotDef *s;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	s = find_snapshot (self, name);
	if (s == NULL || !isfinite (blend_seconds) || blend_seconds < 0.0)
		return FALSE;
	s->blend = blend_seconds;
	changed (self);
	return TRUE;
}

gdouble
lrg_mixer_asset_get_snapshot_blend (LrgMixerAsset *self,
                                    const gchar   *name)
{
	SnapshotDef *s;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), -1.0);
	s = find_snapshot (self, name);
	return s != NULL ? s->blend : -1.0;
}

static SnapVolume *
snap_find_volume (SnapshotDef *s,
                  const gchar *bus)
{
	guint i;

	for (i = 0; s != NULL && i < s->volumes->len; i++)
	{
		SnapVolume *v = g_ptr_array_index (s->volumes, i);

		if (g_strcmp0 (v->bus, bus) == 0)
			return v;
	}
	return NULL;
}

static SnapSend *
snap_find_send (SnapshotDef *s,
                const gchar *source,
                const gchar *target)
{
	guint i;

	for (i = 0; s != NULL && i < s->sends->len; i++)
	{
		SnapSend *v = g_ptr_array_index (s->sends, i);

		if (g_strcmp0 (v->source, source) == 0 && g_strcmp0 (v->target, target) == 0)
			return v;
	}
	return NULL;
}

static SnapParam *
snap_find_param (SnapshotDef *s,
                 const gchar *bus,
                 const gchar *effect,
                 const gchar *param)
{
	g_autofree gchar *canon = canonical_name (param);
	guint             i;

	for (i = 0; s != NULL && i < s->params->len; i++)
	{
		SnapParam *v = g_ptr_array_index (s->params, i);

		if (g_strcmp0 (v->bus, bus) == 0 && g_strcmp0 (v->effect, effect) == 0
		    && g_strcmp0 (v->param, canon) == 0)
			return v;
	}
	return NULL;
}

gboolean
lrg_mixer_asset_set_snapshot_volume (LrgMixerAsset *self,
                                     const gchar   *snapshot,
                                     const gchar   *bus,
                                     gdouble        volume_db)
{
	SnapshotDef *s;
	SnapVolume  *v;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	s = find_snapshot (self, snapshot);
	if (s == NULL || bus_index (self, bus) < 0)
		return FALSE;
	v = snap_find_volume (s, bus);
	if (isnan (volume_db))
	{
		if (v != NULL)
			g_ptr_array_remove (s->volumes, v);
	}
	else if (v != NULL)
		v->db = clamp_db (volume_db);
	else
	{
		v = g_new0 (SnapVolume, 1);
		v->bus = g_strdup (bus);
		v->db = clamp_db (volume_db);
		g_ptr_array_add (s->volumes, v);
	}
	changed (self);
	return TRUE;
}

gboolean
lrg_mixer_asset_get_snapshot_volume (LrgMixerAsset *self,
                                     const gchar   *snapshot,
                                     const gchar   *bus,
                                     gdouble       *out_volume_db)
{
	SnapVolume *v;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	v = snap_find_volume (find_snapshot (self, snapshot), bus);
	if (v == NULL)
		return FALSE;
	if (out_volume_db != NULL)
		*out_volume_db = v->db;
	return TRUE;
}

gboolean
lrg_mixer_asset_set_snapshot_send (LrgMixerAsset *self,
                                   const gchar   *snapshot,
                                   const gchar   *source,
                                   const gchar   *target,
                                   gdouble        level_db)
{
	SnapshotDef *s;
	SnapSend    *v;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	s = find_snapshot (self, snapshot);
	if (s == NULL || bus_index (self, source) <= 0 || bus_index (self, target) < 0
	    || g_strcmp0 (source, target) == 0)
		return FALSE;
	v = snap_find_send (s, source, target);
	if (isnan (level_db))
	{
		if (v != NULL)
			g_ptr_array_remove (s->sends, v);
	}
	else if (v != NULL)
		v->db = clamp_db (level_db);
	else
	{
		v = g_new0 (SnapSend, 1);
		v->source = g_strdup (source);
		v->target = g_strdup (target);
		v->db = clamp_db (level_db);
		g_ptr_array_add (s->sends, v);
	}
	changed (self);
	return TRUE;
}

gboolean
lrg_mixer_asset_get_snapshot_send (LrgMixerAsset *self,
                                   const gchar   *snapshot,
                                   const gchar   *source,
                                   const gchar   *target,
                                   gdouble       *out_level_db)
{
	SnapSend *v;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	v = snap_find_send (find_snapshot (self, snapshot), source, target);
	if (v == NULL)
		return FALSE;
	if (out_level_db != NULL)
		*out_level_db = v->db;
	return TRUE;
}

gboolean
lrg_mixer_asset_set_snapshot_param (LrgMixerAsset  *self,
                                    const gchar    *snapshot,
                                    const gchar    *bus,
                                    const gchar    *effect,
                                    const gchar    *param,
                                    gdouble         value,
                                    GError        **error)
{
	SnapshotDef *s;
	EffectDef   *e;
	GParamSpec  *pspec;
	SnapParam   *v;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	s = find_snapshot (self, snapshot);
	if (s == NULL)
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_UNKNOWN_SNAPSHOT,
		             "unknown snapshot '%s'", snapshot != NULL ? snapshot : "(null)");
		return FALSE;
	}
	e = find_effect (self, bus, effect);
	if (e == NULL)
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_UNKNOWN_EFFECT,
		             "bus '%s' has no effect '%s'", bus != NULL ? bus : "(null)",
		             effect != NULL ? effect : "(null)");
		return FALSE;
	}
	pspec = find_param_pspec (e->gtype, param);
	if (pspec == NULL || !is_numeric_type (pspec->value_type))
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_UNKNOWN_PARAM,
		             "%s has no numeric parameter '%s'", e->type_name, param != NULL ? param : "(null)");
		return FALSE;
	}
	v = snap_find_param (s, bus, effect, pspec->name);
	if (isnan (value))
	{
		if (v != NULL)
			g_ptr_array_remove (s->params, v);
	}
	else if (!isfinite (value))
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_INVALID_VALUE,
		             "%s: expected a finite number", param);
		return FALSE;
	}
	else if (v != NULL)
		v->value = value;
	else
	{
		v = g_new0 (SnapParam, 1);
		v->bus = g_strdup (bus);
		v->effect = g_strdup (effect);
		v->param = g_strdup (pspec->name);
		v->value = value;
		g_ptr_array_add (s->params, v);
	}
	changed (self);
	return TRUE;
}

gboolean
lrg_mixer_asset_get_snapshot_param (LrgMixerAsset *self,
                                    const gchar   *snapshot,
                                    const gchar   *bus,
                                    const gchar   *effect,
                                    const gchar   *param,
                                    gdouble       *out_value)
{
	SnapParam *v;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	if (param == NULL)
		return FALSE;
	v = snap_find_param (find_snapshot (self, snapshot), bus, effect, param);
	if (v == NULL)
		return FALSE;
	if (out_value != NULL)
		*out_value = v->value;
	return TRUE;
}

gboolean
lrg_mixer_asset_set_default_snapshot (LrgMixerAsset *self,
                                      const gchar   *name)
{
	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	if (name != NULL && find_snapshot (self, name) == NULL)
		return FALSE;
	g_free (self->default_snapshot);
	self->default_snapshot = g_strdup (name);
	changed (self);
	return TRUE;
}

const gchar *
lrg_mixer_asset_get_default_snapshot (LrgMixerAsset *self)
{
	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), NULL);
	return self->default_snapshot;
}

/* --- ducking ------------------------------------------------------------- */

gboolean
lrg_mixer_asset_set_duck (LrgMixerAsset *self,
                          const gchar   *trigger,
                          const gchar   *target,
                          gdouble        threshold_db,
                          gdouble        depth_db,
                          gdouble        attack,
                          gdouble        release)
{
	DuckDef *d = NULL;
	guint    i;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	if (bus_index (self, trigger) < 0 || bus_index (self, target) < 0 || g_strcmp0 (trigger, target) == 0
	    || isnan (threshold_db) || isnan (depth_db) || depth_db > 0.0 || !isfinite (attack) || attack < 0.0
	    || attack > MAX_DUCK_TIME || !isfinite (release) || release < 0.0 || release > MAX_DUCK_TIME)
		return FALSE;
	for (i = 0; i < self->ducks->len; i++)
	{
		DuckDef *o = g_ptr_array_index (self->ducks, i);

		if (g_strcmp0 (o->trigger, trigger) == 0 && g_strcmp0 (o->target, target) == 0)
			d = o;
	}
	if (d == NULL)
	{
		d = g_new0 (DuckDef, 1);
		d->trigger = g_strdup (trigger);
		d->target = g_strdup (target);
		g_ptr_array_add (self->ducks, d);
	}
	d->threshold = clamp_db (threshold_db);
	d->depth = clamp_db (depth_db);
	d->attack = attack;
	d->release = release;
	changed (self);
	return TRUE;
}

gboolean
lrg_mixer_asset_remove_duck (LrgMixerAsset *self,
                             const gchar   *trigger,
                             const gchar   *target)
{
	guint i;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	for (i = 0; i < self->ducks->len; i++)
	{
		DuckDef *d = g_ptr_array_index (self->ducks, i);

		if (g_strcmp0 (d->trigger, trigger) == 0 && g_strcmp0 (d->target, target) == 0)
		{
			g_ptr_array_remove_index (self->ducks, i);
			changed (self);
			return TRUE;
		}
	}
	return FALSE;
}

guint
lrg_mixer_asset_get_duck_count (LrgMixerAsset *self)
{
	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), 0);
	return self->ducks->len;
}

gboolean
lrg_mixer_asset_get_duck (LrgMixerAsset  *self,
                          guint           index,
                          const gchar   **out_trigger,
                          const gchar   **out_target,
                          gdouble        *out_threshold_db,
                          gdouble        *out_depth_db,
                          gdouble        *out_attack,
                          gdouble        *out_release)
{
	DuckDef *d;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	if (index >= self->ducks->len)
		return FALSE;
	d = g_ptr_array_index (self->ducks, index);
	if (out_trigger != NULL)
		*out_trigger = d->trigger;
	if (out_target != NULL)
		*out_target = d->target;
	if (out_threshold_db != NULL)
		*out_threshold_db = d->threshold;
	if (out_depth_db != NULL)
		*out_depth_db = d->depth;
	if (out_attack != NULL)
		*out_attack = d->attack;
	if (out_release != NULL)
		*out_release = d->release;
	return TRUE;
}

/* ==========================================================================
 * Runtime
 * ========================================================================== */

static gboolean
mixer_error (GError      **error,
             gint          code,
             const gchar  *format,
             ...) G_GNUC_PRINTF (3, 4);

static gboolean
mixer_error (GError      **error,
             gint          code,
             const gchar  *format,
             ...)
{
	va_list args;

	va_start (args, format);
	if (error != NULL)
		*error = g_error_new_valist (LRG_MIXER_ASSET_ERROR, code, format, args);
	va_end (args);
	return FALSE;
}

/* a fresh effect instance with the layout's parameters */
static LrgAudioEffect *
build_effect (EffectDef *e)
{
	LrgAudioEffect *effect = g_object_new (e->gtype, NULL);
	guint           i;

	for (i = 0; i < e->params->len; i++)
	{
		Param *p = g_ptr_array_index (e->params, i);

		g_object_set_property (G_OBJECT (effect), p->name, &p->value);
	}
	lrg_audio_effect_set_enabled (effect, e->enabled);
	return effect;
}

gboolean
lrg_mixer_asset_apply (LrgMixerAsset  *self,
                       LrgAudioMixer  *mixer,
                       GError        **error)
{
	g_autofree gboolean *added = NULL;
	guint                remaining;
	guint                i, j;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (mixer), FALSE);

	if (!lrg_mixer_asset_validate (self, error))
		return FALSE;

	/* 1. buses, parents first: a pass adds every bus whose parent exists */
	added = g_new0 (gboolean, self->buses->len);
	added[0] = TRUE;
	remaining = self->buses->len - 1;
	while (remaining > 0)
	{
		guint progress = 0;

		for (i = 1; i < self->buses->len; i++)
		{
			BusDef *b = g_ptr_array_index (self->buses, i);

			if (added[i] || !lrg_audio_mixer_has_bus (mixer, b->parent))
				continue;
			if (lrg_audio_mixer_has_bus (mixer, b->name))
			{
				if (g_strcmp0 (lrg_audio_mixer_get_bus_parent (mixer, b->name), b->parent) != 0)
					return mixer_error (error, LRG_MIXER_ASSET_ERROR_ROUTING,
					                    "bus '%s' exists in the mixer under '%s', not '%s'", b->name,
					                    lrg_audio_mixer_get_bus_parent (mixer, b->name), b->parent);
			}
			else if (!lrg_audio_mixer_add_bus (mixer, b->name, b->parent))
				return mixer_error (error, LRG_MIXER_ASSET_ERROR_ROUTING, "the mixer refused bus '%s'",
				                    b->name);
			added[i] = TRUE;
			remaining--;
			progress++;
		}
		if (progress == 0)
			return mixer_error (error, LRG_MIXER_ASSET_ERROR_ROUTING, "bus parents do not resolve");
	}

	/* 2. faders, mutes, solos; effect chains rebuilt; old sends cleared */
	lrg_audio_mixer_clear_ducks (mixer);
	for (i = 0; i < self->buses->len; i++)
	{
		BusDef              *b = g_ptr_array_index (self->buses, i);
		LrgAudioEffectChain *chain = lrg_audio_mixer_get_bus_chain (mixer, b->name);
		g_auto(GStrv)        sends = lrg_audio_mixer_list_sends (mixer, b->name);

		lrg_audio_mixer_set_bus (mixer, b->name, lrg_audio_db_to_gain (b->volume), b->mute);
		lrg_audio_mixer_set_bus_solo (mixer, b->name, b->solo);
		for (j = 0; sends[j] != NULL; j++)
			lrg_audio_mixer_set_send (mixer, b->name, sends[j], 0.0);
		while (lrg_audio_effect_chain_get_count (chain) > 0)
			lrg_audio_effect_chain_remove (chain, lrg_audio_effect_chain_get_effect (chain, 0));
		for (j = 0; j < b->effects->len; j++)
		{
			g_autoptr(LrgAudioEffect) effect = build_effect (g_ptr_array_index (b->effects, j));

			lrg_audio_effect_chain_add (chain, effect);
		}
	}

	/* 3. sends, then ducking rules (both can only fail on a mixer whose
	 * own extra buses make a loop) */
	for (i = 1; i < self->buses->len; i++)
	{
		BusDef *b = g_ptr_array_index (self->buses, i);

		for (j = 0; j < b->sends->len; j++)
		{
			SendDef *s = g_ptr_array_index (b->sends, j);
			gdouble  gain = lrg_audio_db_to_gain (s->level);

			if (gain > 0.0 && !lrg_audio_mixer_set_send (mixer, b->name, s->target, gain))
				return mixer_error (error, LRG_MIXER_ASSET_ERROR_ROUTING, "the mixer refused send '%s' -> '%s'",
				                    b->name, s->target);
		}
	}
	for (i = 0; i < self->ducks->len; i++)
	{
		DuckDef *d = g_ptr_array_index (self->ducks, i);

		if (!lrg_audio_mixer_set_duck (mixer, d->trigger, d->target, d->threshold, d->depth, d->attack,
		                               d->release))
			return mixer_error (error, LRG_MIXER_ASSET_ERROR_ROUTING,
			                    "the mixer refused ducking rule '%s' -> '%s'", d->trigger, d->target);
	}

	/* 4. the default snapshot, at once */
	if (self->default_snapshot != NULL)
		return lrg_mixer_asset_apply_snapshot (self, mixer, self->default_snapshot, 0.0, error);
	return TRUE;
}

LrgAudioMixer *
lrg_mixer_asset_create_mixer (LrgMixerAsset  *self,
                              guint           sample_rate,
                              guint           channels,
                              GError        **error)
{
	g_autoptr(LrgAudioMixer) mixer = NULL;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), NULL);

	mixer = lrg_audio_mixer_new (sample_rate, channels);
	if (mixer == NULL)
	{
		g_set_error (error, LRG_MIXER_ASSET_ERROR, LRG_MIXER_ASSET_ERROR_INVALID_VALUE,
		             "invalid mixer format %u Hz, %u channels", sample_rate, channels);
		return NULL;
	}
	if (!lrg_mixer_asset_apply (self, mixer, error))
		return NULL;
	return g_steal_pointer (&mixer);
}

/* the base (layout) value of a parameter as a double: the stored value,
 * else the property's default */
static gdouble
base_param_value (EffectDef   *e,
                  const gchar *param)
{
	Param      *p = effect_find_param (e, param);
	GParamSpec *pspec;

	if (p != NULL)
		return value_to_double (&p->value);
	pspec = find_param_pspec (e->gtype, param);
	if (pspec == NULL)
		return 0.0;
	return value_to_double (g_param_spec_get_default_value (pspec));
}

gboolean
lrg_mixer_asset_blend_snapshots (LrgMixerAsset       *self,
                                 LrgAudioMixer       *mixer,
                                 const gchar * const *names,
                                 const gdouble       *weights,
                                 guint                n_snapshots,
                                 gdouble              blend_seconds,
                                 GError             **error)
{
	g_autofree SnapshotDef **snaps = NULL;
	gdouble                  total = 0.0;
	guint                    i, j, k;

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (mixer), FALSE);
	g_return_val_if_fail (n_snapshots == 0 || (names != NULL && weights != NULL), FALSE);

	/* resolve the snapshots and the weights */
	snaps = g_new0 (SnapshotDef *, MAX (n_snapshots, 1u));
	for (i = 0; i < n_snapshots; i++)
	{
		if (!isfinite (weights[i]) || weights[i] < 0.0)
			return mixer_error (error, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, "weight %u is not >= 0", i);
		total += weights[i];
		if (names[i] == NULL || *names[i] == '\0')
			continue;
		snaps[i] = find_snapshot (self, names[i]);
		if (snaps[i] == NULL)
			return mixer_error (error, LRG_MIXER_ASSET_ERROR_UNKNOWN_SNAPSHOT, "unknown snapshot '%s'",
			                    names[i]);
	}
	if (!(total > 0.0))
		return mixer_error (error, LRG_MIXER_ASSET_ERROR_INVALID_VALUE, "the weights add up to 0");
	if (!isfinite (blend_seconds) || blend_seconds < 0.0)
		blend_seconds = 0.0;

	/* bus volumes: weighted mean in dB of override-or-base */
	for (i = 0; i < self->buses->len; i++)
	{
		BusDef *b = g_ptr_array_index (self->buses, i);
		gdouble db = 0.0;

		for (k = 0; k < n_snapshots; k++)
		{
			SnapVolume *v = snap_find_volume (snaps[k], b->name);

			db += weights[k] * (v != NULL ? v->db : b->volume);
		}
		if (!lrg_audio_mixer_ramp_bus_gain (mixer, b->name, lrg_audio_db_to_gain (db / total), blend_seconds))
			return mixer_error (error, LRG_MIXER_ASSET_ERROR_UNKNOWN_BUS,
			                    "bus '%s' is not in the mixer (apply the layout first)", b->name);
	}

	/* sends: every base send and every send any snapshot names */
	for (i = 0; i < self->buses->len; i++)
	{
		BusDef           *b = g_ptr_array_index (self->buses, i);
		g_autoptr(GPtrArray) targets = g_ptr_array_new ();

		for (j = 0; j < b->sends->len; j++)
			g_ptr_array_add (targets, ((SendDef *) g_ptr_array_index (b->sends, j))->target);
		for (k = 0; k < self->snapshots->len; k++)
		{
			SnapshotDef *s = g_ptr_array_index (self->snapshots, k);

			for (j = 0; j < s->sends->len; j++)
			{
				SnapSend *v = g_ptr_array_index (s->sends, j);

				if (g_strcmp0 (v->source, b->name) == 0
				    && !g_ptr_array_find_with_equal_func (targets, v->target, g_str_equal, NULL))
					g_ptr_array_add (targets, v->target);
			}
		}
		for (j = 0; j < targets->len; j++)
		{
			const gchar *target = g_ptr_array_index (targets, j);
			SendDef     *base = find_send (b, target);
			gdouble      db = 0.0;
			gdouble      gain;

			for (k = 0; k < n_snapshots; k++)
			{
				SnapSend *v = snap_find_send (snaps[k], b->name, target);

				db += weights[k] * (v != NULL ? v->db : base != NULL ? base->level : LRG_AUDIO_SILENCE_DB);
			}
			gain = lrg_audio_db_to_gain (db / total);
			if (gain == 0.0 && lrg_audio_mixer_get_send (mixer, b->name, target) == 0.0)
				continue;
			if (!lrg_audio_mixer_ramp_send (mixer, b->name, target, gain, blend_seconds))
				return mixer_error (error, LRG_MIXER_ASSET_ERROR_ROUTING, "the mixer refused send '%s' -> '%s'",
				                    b->name, target);
		}
	}

	/* parameters: every one any snapshot overrides */
	for (k = 0; k < self->snapshots->len; k++)
	{
		SnapshotDef *s = g_ptr_array_index (self->snapshots, k);

		for (j = 0; j < s->params->len; j++)
		{
			SnapParam *v = g_ptr_array_index (s->params, j);
			BusDef    *b = find_bus (self, v->bus);
			gint       ei = effect_index (b, v->effect);
			EffectDef *e;
			gdouble    value = 0.0;
			gboolean   seen = FALSE;
			guint      m;

			if (ei < 0)
				continue;
			/* once per parameter: skip it in later snapshots */
			for (m = 0; m < k && !seen; m++)
				seen = snap_find_param (g_ptr_array_index (self->snapshots, m), v->bus, v->effect,
				                        v->param) != NULL;
			if (seen)
				continue;
			e = g_ptr_array_index (b->effects, ei);
			for (i = 0; i < n_snapshots; i++)
			{
				SnapParam *o = snap_find_param (snaps[i], v->bus, v->effect, v->param);

				value += weights[i] * (o != NULL ? o->value : base_param_value (e, v->param));
			}
			if (!lrg_audio_mixer_ramp_effect_param (mixer, v->bus, (guint) ei, v->param, value / total,
			                                        blend_seconds))
				return mixer_error (error, LRG_MIXER_ASSET_ERROR_UNKNOWN_EFFECT,
				                    "the mixer has no effect %u '%s' on bus '%s' (apply the layout first)",
				                    (guint) ei, v->effect, v->bus);
		}
	}
	return TRUE;
}

gboolean
lrg_mixer_asset_apply_snapshot (LrgMixerAsset  *self,
                                LrgAudioMixer  *mixer,
                                const gchar    *name,
                                gdouble         blend_seconds,
                                GError        **error)
{
	const gchar *names[1];
	gdouble      weights[1] = { 1.0 };

	g_return_val_if_fail (LRG_IS_MIXER_ASSET (self), FALSE);
	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (mixer), FALSE);

	if (name != NULL && *name != '\0')
	{
		SnapshotDef *s = find_snapshot (self, name);

		if (s == NULL)
			return mixer_error (error, LRG_MIXER_ASSET_ERROR_UNKNOWN_SNAPSHOT, "unknown snapshot '%s'", name);
		if (blend_seconds < 0.0)
			blend_seconds = s->blend;
	}
	else if (blend_seconds < 0.0)
		blend_seconds = 0.0;
	names[0] = name != NULL ? name : "";
	return lrg_mixer_asset_blend_snapshots (self, mixer, names, weights, 1, blend_seconds, error);
}

/* ==========================================================================
 * Project default mixer
 * ========================================================================== */

/* the audio: block of a project root (root level, then inside project:) */
static YamlNode *
find_audio_block (YamlNode *root)
{
	YamlMapping *map;
	YamlNode    *block;
	YamlNode    *project;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
		return NULL;
	map = yaml_node_get_mapping (root);
	block = yaml_mapping_get_member (map, "audio");
	if (block != NULL)
		return block;
	project = yaml_mapping_get_member (map, "project");
	if (project != NULL && yaml_node_get_node_type (project) == YAML_NODE_MAPPING)
		return yaml_mapping_get_member (yaml_node_get_mapping (project), "audio");
	return NULL;
}

gchar *
lrg_mixer_asset_find_project_mixer (const gchar *start)
{
	g_autofree gchar     *manifest = NULL;
	g_autofree gchar     *dir = NULL;
	g_autoptr(YamlParser) parser = NULL;
	YamlNode             *block;
	const gchar          *path;

	manifest = lrg_physics_settings_find_project (start);
	if (manifest == NULL)
		return NULL;
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_file (parser, manifest, NULL))
		return NULL;
	block = find_audio_block (yaml_parser_get_root (parser));
	if (block == NULL || yaml_node_get_node_type (block) != YAML_NODE_MAPPING)
		return NULL;
	path = scalar_of (yaml_mapping_get_member (yaml_node_get_mapping (block), "mixer"));
	if (path == NULL || *path == '\0')
		return NULL;
	if (g_path_is_absolute (path))
		return g_strdup (path);
	dir = g_path_get_dirname (manifest);
	return g_canonicalize_filename (path, dir);
}

LrgMixerAsset *
lrg_mixer_asset_new_for_project (const gchar  *start,
                                 GError      **error)
{
	g_autofree gchar *path = lrg_mixer_asset_find_project_mixer (start);

	if (path == NULL)
		return NULL;
	return lrg_mixer_asset_new_from_file (path, error);
}
