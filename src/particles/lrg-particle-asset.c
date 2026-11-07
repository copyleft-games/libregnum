/* lrg-particle-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The `.rparticles` format and its runtime.
 *
 * Layout of this file:
 *
 *  1. Enums and the error domain.
 *  2. Parameter tables: one ParamDesc per GObject property of a built-in
 *     module kind (and of LrgParticleRenderSettings).  The tables install
 *     the properties in a fixed order, hold their defaults, and store the
 *     values in a ParamValue array, so the 20 module kinds need no
 *     per-kind GObject boilerplate.  The YAML writer walks the same
 *     order, which keeps files stable and readable.
 *  3. The built-in module kinds: one KindDesc each (name, stage, table,
 *     vfuncs).  Each is registered at runtime as a GType
 *     ("LrgParticleModuleGravity", ...) deriving from an internal
 *     BuiltinModule class, which derives from the public
 *     LrgParticleModule.
 *  4. The kind registry (built-ins and lrg_particle_module_register_kind()).
 *  5. LrgParticleRenderSettings, LrgParticleAssetEmitter, LrgParticleAsset.
 *  6. YAML in and out: generic over GObject properties, with key paths
 *     in every message and unknown keys kept per mapping.
 *  7. LrgParticleEffect.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "lrg-particle-asset.h"
#include "../core/lrg-data-loader.h"
#include "../ui/lrg-property-source.h"
#include "../tween/lrg-keyframe-curve.h"
#include <yaml-glib.h>

#define DEG2RAD_F ((gfloat) G_PI / 180.0f)

/* qdata key of the unknown keys an object's mapping carried (YamlMapping) */
#define EXTRA_KEY "lrg-particle-asset-extra"

/* ==========================================================================
 * 1. Enums and errors
 * ========================================================================== */

G_DEFINE_QUARK (lrg-particle-asset-error-quark, lrg_particle_asset_error)

#define DEFINE_ENUM_TYPE(func, Name, ...)                                     \
GType                                                                         \
func (void)                                                                   \
{                                                                             \
	static gsize type_id = 0;                                             \
	if (g_once_init_enter (&type_id))                                     \
	{                                                                     \
		static const GEnumValue values[] = { __VA_ARGS__, { 0, NULL, NULL } }; \
		GType id = g_enum_register_static (Name, values);             \
		g_once_init_leave (&type_id, id);                             \
	}                                                                     \
	return (GType) type_id;                                               \
}

DEFINE_ENUM_TYPE (lrg_particle_shape_get_type, "LrgParticleShape",
	{ LRG_PARTICLE_SHAPE_POINT, "LRG_PARTICLE_SHAPE_POINT", "point" },
	{ LRG_PARTICLE_SHAPE_SPHERE, "LRG_PARTICLE_SHAPE_SPHERE", "sphere" },
	{ LRG_PARTICLE_SHAPE_HEMISPHERE, "LRG_PARTICLE_SHAPE_HEMISPHERE", "hemisphere" },
	{ LRG_PARTICLE_SHAPE_CONE, "LRG_PARTICLE_SHAPE_CONE", "cone" },
	{ LRG_PARTICLE_SHAPE_BOX, "LRG_PARTICLE_SHAPE_BOX", "box" },
	{ LRG_PARTICLE_SHAPE_CIRCLE, "LRG_PARTICLE_SHAPE_CIRCLE", "circle" },
	{ LRG_PARTICLE_SHAPE_MESH, "LRG_PARTICLE_SHAPE_MESH", "mesh" })

DEFINE_ENUM_TYPE (lrg_particle_collision_mode_get_type, "LrgParticleCollisionMode",
	{ LRG_PARTICLE_COLLISION_PLANE, "LRG_PARTICLE_COLLISION_PLANE", "plane" },
	{ LRG_PARTICLE_COLLISION_WORLD, "LRG_PARTICLE_COLLISION_WORLD", "world" })

DEFINE_ENUM_TYPE (lrg_particle_renderer_kind_get_type, "LrgParticleRendererKind",
	{ LRG_PARTICLE_RENDERER_NONE, "LRG_PARTICLE_RENDERER_NONE", "none" },
	{ LRG_PARTICLE_RENDERER_BILLBOARD, "LRG_PARTICLE_RENDERER_BILLBOARD", "billboard" },
	{ LRG_PARTICLE_RENDERER_RIBBON, "LRG_PARTICLE_RENDERER_RIBBON", "ribbon" },
	{ LRG_PARTICLE_RENDERER_MESH, "LRG_PARTICLE_RENDERER_MESH", "mesh" })

/* ==========================================================================
 * 2. Parameter tables
 * ========================================================================== */

typedef enum
{
	P_FLOAT,     /* gfloat, bounds */
	P_INT,       /* gint, bounds */
	P_UINT,      /* guint, bounds */
	P_BOOL,
	P_ENUM,      /* enum_type, default in d[0] */
	P_STRING,    /* nullable string, default NULL */
	P_RANGE,     /* GrlVector2: x = min, y = max, each within bounds */
	P_VEC3,      /* GrlVector3 */
	P_COLOR,     /* GrlColor, always set */
	P_COLOR_OPT, /* GrlColor, nullable */
	P_GRADIENT,  /* LrgGradientStops, nullable (runtime LrgColorGradient) */
	P_CURVE      /* LrgKeyframeCurve, nullable */
} ParamKind;

typedef struct
{
	const gchar *name;      /* property name = YAML key */
	ParamKind    kind;
	GType      (*enum_type) (void);
	gdouble      d[4];      /* defaults: float/int/bool/enum in d[0], range d[0..1], vec3 d[0..2], colour d[0..3] 0..1 */
	gdouble      min;
	gdouble      max;
	const gchar *blurb;
} ParamDesc;

/* One stored value; which member is used follows the ParamKind */
typedef struct
{
	gfloat            f[4];
	gint              i;
	gboolean          set;        /* P_COLOR_OPT: has a value */
	gchar            *s;
	LrgColorGradient *gradient;   /* P_GRADIENT (NULL: unset) */
	LrgKeyframeCurve *curve;      /* P_CURVE (NULL: unset) */
} ParamValue;

#define MAX_PARAMS (12)

static const gchar *
param_hint (const ParamDesc *d)
{
	switch (d->kind)
	{
	case P_RANGE:     return "vector2";
	case P_VEC3:      return "vector3";
	case P_COLOR:
	case P_COLOR_OPT: return "color";
	case P_GRADIENT:  return "gradient";
	case P_CURVE:     return "curve";
	default:          return NULL;
	}
}

/* Builds the GParamSpec of @d.  Every property is read-write with
 * explicit notify. */
static GParamSpec *
param_spec (const ParamDesc *d)
{
	GParamFlags flags = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS;
	GParamSpec *ps = NULL;
	const gchar *hint;

	switch (d->kind)
	{
	case P_FLOAT:
		ps = g_param_spec_float (d->name, NULL, d->blurb, (gfloat) d->min, (gfloat) d->max, (gfloat) d->d[0], flags);
		break;
	case P_INT:
		ps = g_param_spec_int (d->name, NULL, d->blurb, (gint) d->min, (gint) d->max, (gint) d->d[0], flags);
		break;
	case P_UINT:
		ps = g_param_spec_uint (d->name, NULL, d->blurb, (guint) d->min, (guint) d->max, (guint) d->d[0], flags);
		break;
	case P_BOOL:
		ps = g_param_spec_boolean (d->name, NULL, d->blurb, d->d[0] != 0.0, flags);
		break;
	case P_ENUM:
		ps = g_param_spec_enum (d->name, NULL, d->blurb, d->enum_type (), (gint) d->d[0], flags);
		break;
	case P_STRING:
		ps = g_param_spec_string (d->name, NULL, d->blurb, NULL, flags);
		break;
	case P_RANGE:
		ps = g_param_spec_boxed (d->name, NULL, d->blurb, GRL_TYPE_VECTOR2, flags);
		break;
	case P_VEC3:
		ps = g_param_spec_boxed (d->name, NULL, d->blurb, GRL_TYPE_VECTOR3, flags);
		break;
	case P_COLOR:
	case P_COLOR_OPT:
		ps = g_param_spec_boxed (d->name, NULL, d->blurb, GRL_TYPE_COLOR, flags);
		break;
	case P_GRADIENT:
		ps = g_param_spec_boxed (d->name, NULL, d->blurb, LRG_TYPE_GRADIENT_STOPS, flags);
		break;
	case P_CURVE:
		ps = g_param_spec_object (d->name, NULL, d->blurb, LRG_TYPE_KEYFRAME_CURVE, flags);
		break;
	default:
		g_assert_not_reached ();
	}
	hint = param_hint (d);
	if (hint != NULL)
		g_param_spec_set_qdata (ps, lrg_property_hint_quark (), (gpointer) hint);
	return ps;
}

/* Fills @v with @d's default. */
static void
param_value_init (ParamValue      *v,
                  const ParamDesc *d)
{
	gint k;

	memset (v, 0, sizeof *v);
	for (k = 0; k < 4; k++)
		v->f[k] = (gfloat) d->d[k];
	v->i = (gint) d->d[0];
	v->set = d->kind != P_COLOR_OPT;
}

static void
param_value_clear (ParamValue *v)
{
	g_clear_pointer (&v->s, g_free);
	g_clear_object (&v->gradient);
	g_clear_object (&v->curve);
}

static guint8
to_byte (gfloat v)
{
	if (!(v > 0.0f))
		return 0;
	if (v >= 1.0f)
		return 255;
	return (guint8) lrintf (v * 255.0f);
}

/* Copies @v into the GValue of @d's property. */
static void
param_get (const ParamValue *v,
           const ParamDesc  *d,
           GValue           *value)
{
	switch (d->kind)
	{
	case P_FLOAT:
		g_value_set_float (value, v->f[0]);
		break;
	case P_INT:
		g_value_set_int (value, v->i);
		break;
	case P_UINT:
		g_value_set_uint (value, (guint) v->i);
		break;
	case P_BOOL:
		g_value_set_boolean (value, v->i != 0);
		break;
	case P_ENUM:
		g_value_set_enum (value, v->i);
		break;
	case P_STRING:
		g_value_set_string (value, v->s);
		break;
	case P_RANGE:
		g_value_take_boxed (value, grl_vector2_new (v->f[0], v->f[1]));
		break;
	case P_VEC3:
		g_value_take_boxed (value, grl_vector3_new (v->f[0], v->f[1], v->f[2]));
		break;
	case P_COLOR:
	case P_COLOR_OPT:
		if (v->set)
			g_value_take_boxed (value, grl_color_new (to_byte (v->f[0]), to_byte (v->f[1]),
			                                          to_byte (v->f[2]), to_byte (v->f[3])));
		else
			g_value_set_boxed (value, NULL);
		break;
	case P_GRADIENT:
		if (v->gradient != NULL)
			g_value_take_boxed (value, lrg_color_gradient_to_stops (v->gradient));
		else
			g_value_set_boxed (value, NULL);
		break;
	case P_CURVE:
		g_value_set_object (value, v->curve);
		break;
	default:
		g_assert_not_reached ();
	}
}

/* Stores a GValue of @d's property in @v.  Returns TRUE when the stored
 * value changed (or may have: boxed and object values always count). */
static gboolean
param_set (ParamValue      *v,
           const ParamDesc *d,
           const GValue    *value)
{
	switch (d->kind)
	{
	case P_FLOAT:
	{
		gfloat f = g_value_get_float (value);

		if (v->f[0] == f)
			return FALSE;
		v->f[0] = f;
		return TRUE;
	}
	case P_INT:
	case P_ENUM:
	case P_BOOL:
	case P_UINT:
	{
		gint i = d->kind == P_INT ? g_value_get_int (value)
		       : d->kind == P_ENUM ? g_value_get_enum (value)
		       : d->kind == P_BOOL ? (g_value_get_boolean (value) ? 1 : 0)
		       : (gint) g_value_get_uint (value);

		if (v->i == i)
			return FALSE;
		v->i = i;
		return TRUE;
	}
	case P_STRING:
		return g_set_str (&v->s, g_value_get_string (value));
	case P_RANGE:
	{
		const GrlVector2 *r = g_value_get_boxed (value);
		gfloat            lo = r != NULL ? r->x : (gfloat) d->d[0];
		gfloat            hi = r != NULL ? r->y : (gfloat) d->d[1];

		/* keep min <= max */
		if (hi < lo)
		{
			gfloat t = lo;

			lo = hi;
			hi = t;
		}
		lo = CLAMP (lo, (gfloat) d->min, (gfloat) d->max);
		hi = CLAMP (hi, (gfloat) d->min, (gfloat) d->max);
		v->f[0] = lo;
		v->f[1] = hi;
		return TRUE;
	}
	case P_VEC3:
	{
		const GrlVector3 *r = g_value_get_boxed (value);

		if (r != NULL)
		{
			v->f[0] = r->x;
			v->f[1] = r->y;
			v->f[2] = r->z;
		}
		else
		{
			v->f[0] = (gfloat) d->d[0];
			v->f[1] = (gfloat) d->d[1];
			v->f[2] = (gfloat) d->d[2];
		}
		return TRUE;
	}
	case P_COLOR:
	case P_COLOR_OPT:
	{
		const GrlColor *c = g_value_get_boxed (value);

		if (c == NULL)
		{
			if (d->kind == P_COLOR_OPT)
				v->set = FALSE;
			else
			{
				gint k;

				for (k = 0; k < 4; k++)
					v->f[k] = (gfloat) d->d[k];
			}
			return TRUE;
		}
		v->set = TRUE;
		v->f[0] = (gfloat) c->r / 255.0f;
		v->f[1] = (gfloat) c->g / 255.0f;
		v->f[2] = (gfloat) c->b / 255.0f;
		v->f[3] = (gfloat) c->a / 255.0f;
		return TRUE;
	}
	case P_GRADIENT:
	{
		const LrgGradientStops *stops = g_value_get_boxed (value);

		if (stops == NULL)
			g_clear_object (&v->gradient);
		else if (v->gradient == NULL)
			v->gradient = lrg_color_gradient_new_from_stops (stops);
		else
			lrg_color_gradient_set_from_stops (v->gradient, stops);
		return TRUE;
	}
	case P_CURVE:
		return g_set_object (&v->curve, g_value_get_object (value));
	default:
		g_assert_not_reached ();
	}
	return FALSE;
}

/* ==========================================================================
 * 3. Built-in module kinds
 * ========================================================================== */

typedef struct _BuiltinModule      BuiltinModule;
typedef struct _BuiltinModuleClass BuiltinModuleClass;
typedef struct _KindDesc           KindDesc;

struct _KindDesc
{
	const gchar            *kind;       /* "gravity" */
	const gchar            *type_name;  /* "LrgParticleModuleGravity" */
	LrgParticleModuleStage  stage;
	const ParamDesc        *params;
	guint                   n_params;
	void     (*reset)           (BuiltinModule *self);
	guint    (*spawn_count)     (BuiltinModule *self, LrgParticleModuleContext *ctx);
	void     (*init_particle)   (BuiltinModule *self, LrgParticleModuleContext *ctx, LrgParticle *p);
	void     (*update_particle) (BuiltinModule *self, LrgParticleModuleContext *ctx, LrgParticle *p);
	void     (*particle_event)  (BuiltinModule *self, LrgParticleModuleContext *ctx, LrgParticle *p,
	                             LrgParticleEvent event);
	gboolean (*gpu_capable)     (BuiltinModule *self);
};

struct _BuiltinModule
{
	LrgParticleModule parent_instance;

	const KindDesc   *desc;
	ParamValue        v[MAX_PARAMS];

	/* run state (reset by reset()) */
	gdouble           acc;           /* spawn-rate accumulator */
	gint64            state_cycle;   /* burst: cycle seen */
	guint             state_next;    /* burst: next firing in the cycle */

	/* shape: mesh surface, 9 floats per triangle, cumulative areas */
	GArray           *tri;
	GArray           *cdf;
};

struct _BuiltinModuleClass
{
	LrgParticleModuleClass parent_class;

	const KindDesc        *desc;
	GParamSpec            *pspecs[MAX_PARAMS];
};

G_GNUC_INTERNAL GType builtin_module_get_type (void);
G_DEFINE_ABSTRACT_TYPE (BuiltinModule, builtin_module, LRG_TYPE_PARTICLE_MODULE)

#define BUILTIN(o) ((BuiltinModule *) (o))
#define IS_BUILTIN(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), builtin_module_get_type ()))

static void
builtin_module_finalize (GObject *object)
{
	BuiltinModule *self = BUILTIN (object);
	guint          i;

	for (i = 0; i < MAX_PARAMS; i++)
		param_value_clear (&self->v[i]);
	g_clear_pointer (&self->tri, g_array_unref);
	g_clear_pointer (&self->cdf, g_array_unref);

	G_OBJECT_CLASS (builtin_module_parent_class)->finalize (object);
}

static void
builtin_module_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	BuiltinModule *self = BUILTIN (object);

	if (prop_id == 0 || prop_id > self->desc->n_params)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}
	param_get (&self->v[prop_id - 1], &self->desc->params[prop_id - 1], value);
}

static void
builtin_module_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	BuiltinModule *self = BUILTIN (object);

	if (prop_id == 0 || prop_id > self->desc->n_params)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}
	if (param_set (&self->v[prop_id - 1], &self->desc->params[prop_id - 1], value))
		g_object_notify_by_pspec (object, pspec);
}

/* Vfunc trampolines: the public class's vfuncs call the KindDesc's */
static void
builtin_reset (LrgParticleModule *module)
{
	BuiltinModule *self = BUILTIN (module);

	self->acc = 0.0;
	self->state_cycle = -1;
	self->state_next = 0;
	if (self->desc->reset != NULL)
		self->desc->reset (self);
}

static guint
builtin_spawn_count (LrgParticleModule        *module,
                     LrgParticleModuleContext *ctx)
{
	BuiltinModule *self = BUILTIN (module);

	return self->desc->spawn_count != NULL ? self->desc->spawn_count (self, ctx) : 0;
}

static void
builtin_init_particle (LrgParticleModule        *module,
                       LrgParticleModuleContext *ctx,
                       LrgParticle              *p)
{
	BuiltinModule *self = BUILTIN (module);

	if (self->desc->init_particle != NULL)
		self->desc->init_particle (self, ctx, p);
}

static void
builtin_update_particle (LrgParticleModule        *module,
                         LrgParticleModuleContext *ctx,
                         LrgParticle              *p)
{
	BuiltinModule *self = BUILTIN (module);

	if (self->desc->update_particle != NULL)
		self->desc->update_particle (self, ctx, p);
}

static void
builtin_particle_event (LrgParticleModule        *module,
                        LrgParticleModuleContext *ctx,
                        LrgParticle              *p,
                        LrgParticleEvent          event)
{
	BuiltinModule *self = BUILTIN (module);

	if (self->desc->particle_event != NULL)
		self->desc->particle_event (self, ctx, p, event);
}

static void
builtin_module_class_init (BuiltinModuleClass *klass)
{
	GObjectClass           *object_class = G_OBJECT_CLASS (klass);
	LrgParticleModuleClass *module_class = LRG_PARTICLE_MODULE_CLASS (klass);

	object_class->finalize = builtin_module_finalize;
	object_class->get_property = builtin_module_get_property;
	object_class->set_property = builtin_module_set_property;
	module_class->reset = builtin_reset;
	module_class->spawn_count = builtin_spawn_count;
	module_class->init_particle = builtin_init_particle;
	module_class->update_particle = builtin_update_particle;
	module_class->particle_event = builtin_particle_event;
}

static void
builtin_module_init (BuiltinModule *self)
{
	self->state_cycle = -1;
}

/* class_init of each registered kind: installs the table's properties */
static void
kind_class_init (gpointer g_class,
                 gpointer class_data)
{
	BuiltinModuleClass *klass = g_class;
	const KindDesc     *desc = class_data;
	GObjectClass       *object_class = G_OBJECT_CLASS (g_class);
	guint               i;

	klass->desc = desc;
	LRG_PARTICLE_MODULE_CLASS (g_class)->stage = desc->stage;

	/* GObject resets get/set_property for every class (they are not
	 * inherited): each kind installs its own trampolines */
	object_class->get_property = builtin_module_get_property;
	object_class->set_property = builtin_module_set_property;
	for (i = 0; i < desc->n_params; i++)
	{
		klass->pspecs[i] = param_spec (&desc->params[i]);
		g_object_class_install_property (object_class, i + 1, klass->pspecs[i]);
	}
}

/* instance_init of each registered kind: defaults from the table */
static void
kind_instance_init (GTypeInstance *instance,
                    gpointer       g_class)
{
	BuiltinModule      *self = BUILTIN (instance);
	BuiltinModuleClass *klass = g_class;
	guint               i;

	self->desc = klass->desc;
	for (i = 0; i < klass->desc->n_params; i++)
		param_value_init (&self->v[i], &klass->desc->params[i]);
}

/* ----- parameter access for the behaviours ----- */

#define MF(self, idx)  ((self)->v[(idx)].f[0])
#define MI(self, idx)  ((self)->v[(idx)].i)
#define MV(self, idx)  ((self)->v[(idx)].f)

/* A value drawn from a range parameter */
static gfloat
prange (BuiltinModule            *self,
        guint                     i,
        LrgParticleModuleContext *ctx)
{
	return lrg_particle_rng_range (ctx->rng, self->v[i].f[0], self->v[i].f[1]);
}

/* A curve parameter at t, or 1 without a curve (or an empty one) */
static gfloat
pcurve (BuiltinModule *self,
        guint          i,
        gfloat         t)
{
	LrgKeyframeCurve *c = self->v[i].curve;

	if (c == NULL || lrg_keyframe_curve_get_key_count (c) == 0)
		return 1.0f;
	return lrg_keyframe_curve_sample (c, t);
}

static gfloat
life_t (const LrgParticle *p)
{
	if (!(p->max_life > 0.0f))
		return 1.0f;
	return CLAMP (p->age / p->max_life, 0.0f, 1.0f);
}

/* Rotates @v by the quaternion @q (x, y, z, w). */
static void
quat_rotate (const gfloat *q,
             const gfloat *v,
             gfloat       *out)
{
	gfloat tx, ty, tz;

	tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
	ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
	tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
	out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
	out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
	out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

static gfloat
v3_len (const gfloat *v)
{
	return sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static gboolean
v3_normalize (gfloat *v)
{
	gfloat l = v3_len (v);

	if (!(l > 1e-8f))
		return FALSE;
	v[0] /= l;
	v[1] /= l;
	v[2] /= l;
	return TRUE;
}

static void
v3_cross (const gfloat *a,
          const gfloat *b,
          gfloat       *out)
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

/* Two unit vectors orthogonal to the unit vector @n */
static void
basis_from (const gfloat *n,
            gfloat       *t,
            gfloat       *b)
{
	gfloat a[3] = { 1.0f, 0.0f, 0.0f };

	if (fabsf (n[0]) > 0.9f)
	{
		a[0] = 0.0f;
		a[1] = 1.0f;
	}
	v3_cross (n, a, t);
	v3_normalize (t);
	v3_cross (n, t, b);
}

/* A direction within @angle radians of the unit vector @dir (uniform
 * over the spherical cap). */
static void
cone_direction (LrgParticleRng *rng,
                const gfloat   *dir,
                gfloat          angle,
                gfloat         *out)
{
	gfloat t[3], b[3], cos_a, sin_a, phi;
	gint   k;

	if (!(angle > 0.0f))
	{
		memcpy (out, dir, 3 * sizeof (gfloat));
		return;
	}
	basis_from (dir, t, b);
	cos_a = 1.0f - lrg_particle_rng_next_float (rng) * (1.0f - cosf (angle));
	sin_a = sqrtf (MAX (0.0f, 1.0f - cos_a * cos_a));
	phi = 2.0f * (gfloat) G_PI * lrg_particle_rng_next_float (rng);
	for (k = 0; k < 3; k++)
		out[k] = dir[k] * cos_a + (t[k] * cosf (phi) + b[k] * sinf (phi)) * sin_a;
}

/* ----- spawn-rate ----- */

enum { RATE_RATE, RATE_CURVE, RATE_PER_DISTANCE };

static const ParamDesc rate_params[] = {
	{ "rate", P_FLOAT, NULL, { 10 }, 0, 1e6, "Particles per second" },
	{ "rate-curve", P_CURVE, NULL, { 0 }, 0, 0, "Rate multiplier over the emission cycle (0..1)" },
	{ "per-distance", P_FLOAT, NULL, { 0 }, 0, 1e6, "Extra particles per world unit the system moves" },
};

/* Accumulates fractional particles so any rate works at any frame rate */
static guint
rate_spawn (BuiltinModule            *self,
            LrgParticleModuleContext *ctx)
{
	gdouble n;

	self->acc += (gdouble) MF (self, RATE_RATE) * (gdouble) pcurve (self, RATE_CURVE, ctx->cycle_fraction) *
	             (gdouble) ctx->dt + (gdouble) MF (self, RATE_PER_DISTANCE) * (gdouble) ctx->moved;
	/* a little slack, so float step sizes (0.01f is not 0.01) do not
	 * lose the last particle of an exact count */
	n = floor (self->acc + 1e-6);
	self->acc -= n;
	return n > 0.0 ? (guint) MIN (n, 1e6) : 0;
}

/* ----- burst ----- */

enum { BURST_TIME, BURST_COUNT, BURST_CYCLES, BURST_INTERVAL, BURST_PROBABILITY };

static const ParamDesc burst_params[] = {
	{ "time", P_FLOAT, NULL, { 0 }, 0, 1e6, "Seconds into the emission cycle of the first firing" },
	{ "count", P_RANGE, NULL, { 10, 10 }, 0, 1e6, "Particles per firing [min, max]" },
	{ "cycles", P_UINT, NULL, { 1 }, 0, 100000, "Firings per emission cycle (0: every interval, forever)" },
	{ "interval", P_FLOAT, NULL, { 1 }, 0, 1e6, "Seconds between firings" },
	{ "probability", P_FLOAT, NULL, { 1 }, 0, 1, "Chance that a firing happens" },
};

/* An integer count from a range: uniform over min .. max inclusive */
static guint
range_count (BuiltinModule            *self,
             guint                     i,
             LrgParticleModuleContext *ctx)
{
	gfloat lo = floorf (self->v[i].f[0]);
	gfloat hi = floorf (self->v[i].f[1]);

	if (hi <= lo)
		return (guint) MAX (lo, 0.0f);
	return (guint) MIN (hi, lo + floorf (lrg_particle_rng_next_float (ctx->rng) * (hi - lo + 1.0f)));
}

static guint
burst_spawn (BuiltinModule            *self,
             LrgParticleModuleContext *ctx)
{
	guint  total = 0;
	guint  cycles = (guint) MI (self, BURST_CYCLES);
	gfloat interval = MF (self, BURST_INTERVAL);

	/* a new emission cycle re-arms the burst */
	if ((gint64) ctx->cycle != self->state_cycle)
	{
		self->state_cycle = (gint64) ctx->cycle;
		self->state_next = 0;
	}
	/* "forever" needs an interval to make progress */
	if (cycles == 0 && !(interval > 0.0f))
		cycles = 1;
	while ((cycles == 0 || self->state_next < cycles) &&
	       ctx->cycle_time >= MF (self, BURST_TIME) + (gfloat) self->state_next * interval)
	{
		gfloat prob = MF (self, BURST_PROBABILITY);

		if (prob >= 1.0f || lrg_particle_rng_next_float (ctx->rng) < prob)
			total += range_count (self, BURST_COUNT, ctx);
		self->state_next++;
		if (total > 1000000)
			break;
	}
	return total;
}

/* ----- shape ----- */

enum { SHAPE_SHAPE, SHAPE_RADIUS, SHAPE_THICKNESS, SHAPE_ANGLE, SHAPE_ARC, SHAPE_SIZE, SHAPE_OFFSET,
       SHAPE_MESH, SHAPE_RANDOMIZE };

static const ParamDesc shape_params[] = {
	{ "shape", P_ENUM, lrg_particle_shape_get_type, { LRG_PARTICLE_SHAPE_SPHERE }, 0, 0, "Emission shape" },
	{ "radius", P_FLOAT, NULL, { 1 }, 0, 1e6, "Radius of sphere, hemisphere, cone base and circle" },
	{ "thickness", P_FLOAT, NULL, { 1 }, 0, 1, "Emitting shell: 0 the surface only, 1 the whole volume" },
	{ "angle", P_FLOAT, NULL, { 25 }, 0, 90, "Cone opening half angle in degrees" },
	{ "arc", P_FLOAT, NULL, { 360 }, 0, 360, "Degrees of the azimuth used (around the Y axis)" },
	{ "size", P_VEC3, NULL, { 1, 1, 1 }, 0, 0, "Box size" },
	{ "offset", P_VEC3, NULL, { 0, 0, 0 }, 0, 0, "Shape centre relative to the system" },
	{ "mesh", P_STRING, NULL, { 0 }, 0, 0, "Mesh asset whose surface emits (shape: mesh)" },
	{ "randomize-direction", P_FLOAT, NULL, { 0 }, 0, 1, "Blend of the direction toward a random one" },
};

/* A radius with the shell thickness: uniform in volume between
 * (1 - thickness) * r and r, in @dims dimensions (2: disc, 3: ball) */
static gfloat
shell_radius (LrgParticleRng *rng,
              gfloat          r,
              gfloat          thickness,
              gint            dims)
{
	gfloat inner = r * (1.0f - thickness);
	gfloat u = lrg_particle_rng_next_float (rng);

	if (dims == 2)
		return sqrtf (inner * inner + u * (r * r - inner * inner));
	return cbrtf (inner * inner * inner + u * (r * r * r - inner * inner * inner));
}

/* Picks a point on the mesh surface by area; FALSE without triangles */
static gboolean
shape_mesh_point (BuiltinModule  *self,
                  LrgParticleRng *rng,
                  gfloat         *pos,
                  gfloat         *dir)
{
	const gfloat *cdf;
	const gfloat *t;
	gfloat        total, x, su, a, b;
	guint         lo, hi, n;
	gfloat        e1[3], e2[3];
	gint          k;

	if (self->cdf == NULL || self->cdf->len == 0)
		return FALSE;
	n = self->cdf->len;
	cdf = (const gfloat *) (const void *) self->cdf->data;
	total = cdf[n - 1];
	x = lrg_particle_rng_next_float (rng) * total;

	/* first triangle whose cumulative area exceeds x */
	lo = 0;
	hi = n - 1;
	while (lo < hi)
	{
		guint mid = (lo + hi) / 2;

		if (cdf[mid] <= x)
			lo = mid + 1;
		else
			hi = mid;
	}
	t = (const gfloat *) (const void *) self->tri->data + (gsize) lo * 9;

	/* uniform barycentric point */
	su = sqrtf (lrg_particle_rng_next_float (rng));
	b = lrg_particle_rng_next_float (rng);
	a = 1.0f - su;
	b = b * su;
	for (k = 0; k < 3; k++)
	{
		pos[k] = t[k] * a + t[3 + k] * (su - b) + t[6 + k] * b;
		e1[k] = t[3 + k] - t[k];
		e2[k] = t[6 + k] - t[k];
	}
	v3_cross (e1, e2, dir);
	if (!v3_normalize (dir))
	{
		dir[0] = 0.0f;
		dir[1] = 1.0f;
		dir[2] = 0.0f;
	}
	return TRUE;
}

static void
shape_init (BuiltinModule            *self,
            LrgParticleModuleContext *ctx,
            LrgParticle              *p)
{
	LrgParticleRng *rng = ctx->rng;
	gfloat          r = MF (self, SHAPE_RADIUS);
	gfloat          th = MF (self, SHAPE_THICKNESS);
	gfloat          arc = MF (self, SHAPE_ARC) * DEG2RAD_F;
	gfloat          pos[3] = { 0.0f, 0.0f, 0.0f };
	gfloat          dir[3] = { 0.0f, 1.0f, 0.0f };
	gfloat          world[3];
	gfloat          rnd = MF (self, SHAPE_RANDOMIZE);
	gint            k;

	switch ((LrgParticleShape) MI (self, SHAPE_SHAPE))
	{
	case LRG_PARTICLE_SHAPE_POINT:
		break;
	case LRG_PARTICLE_SHAPE_SPHERE:
	case LRG_PARTICLE_SHAPE_HEMISPHERE:
	{
		gfloat rr;

		lrg_particle_rng_unit_vector (rng, dir);
		/* the arc limits the azimuth around Y */
		if (arc < 2.0f * (gfloat) G_PI - 1e-4f)
		{
			gfloat h = sqrtf (dir[0] * dir[0] + dir[2] * dir[2]);
			gfloat phi = lrg_particle_rng_next_float (rng) * arc;

			dir[0] = h * cosf (phi);
			dir[2] = h * sinf (phi);
		}
		if (MI (self, SHAPE_SHAPE) == LRG_PARTICLE_SHAPE_HEMISPHERE)
			dir[1] = fabsf (dir[1]);
		rr = shell_radius (rng, r, th, 3);
		for (k = 0; k < 3; k++)
			pos[k] = dir[k] * rr;
		break;
	}
	case LRG_PARTICLE_SHAPE_CONE:
	case LRG_PARTICLE_SHAPE_CIRCLE:
	{
		gfloat phi = lrg_particle_rng_next_float (rng) * arc;
		gfloat rr = shell_radius (rng, r, th, 2);

		pos[0] = rr * cosf (phi);
		pos[2] = rr * sinf (phi);
		if (MI (self, SHAPE_SHAPE) == LRG_PARTICLE_SHAPE_CIRCLE)
		{
			/* outward in the plane */
			dir[0] = cosf (phi);
			dir[1] = 0.0f;
			dir[2] = sinf (phi);
		}
		else
		{
			gfloat angle = MF (self, SHAPE_ANGLE) * DEG2RAD_F;
			gfloat a;

			/* the cone's side through the point: the tilt grows with
			 * the distance from the axis; a point cone (radius 0)
			 * spreads uniformly over the opening */
			if (r > 0.0f)
				a = angle * (rr / r);
			else
				a = acosf (1.0f - lrg_particle_rng_next_float (rng) * (1.0f - cosf (angle)));
			dir[0] = sinf (a) * cosf (phi);
			dir[1] = cosf (a);
			dir[2] = sinf (a) * sinf (phi);
		}
		break;
	}
	case LRG_PARTICLE_SHAPE_BOX:
	{
		const gfloat *sz = MV (self, SHAPE_SIZE);

		for (k = 0; k < 3; k++)
			pos[k] = (lrg_particle_rng_next_float (rng) - 0.5f) * sz[k];
		break;
	}
	case LRG_PARTICLE_SHAPE_MESH:
		shape_mesh_point (self, rng, pos, dir);
		break;
	default:
		break;
	}

	if (rnd > 0.0f)
	{
		gfloat rv[3];

		lrg_particle_rng_unit_vector (rng, rv);
		for (k = 0; k < 3; k++)
			dir[k] = dir[k] * (1.0f - rnd) + rv[k] * rnd;
		if (!v3_normalize (dir))
			memcpy (dir, rv, sizeof rv);
	}

	/* into the system's frame */
	for (k = 0; k < 3; k++)
		pos[k] += MV (self, SHAPE_OFFSET)[k];
	quat_rotate (ctx->rotation, pos, world);
	quat_rotate (ctx->rotation, dir, ctx->direction);
	p->position_x = ctx->origin[0] + world[0];
	p->position_y = ctx->origin[1] + world[1];
	p->position_z = ctx->origin[2] + world[2];
}

static gboolean
shape_gpu (BuiltinModule *self)
{
	return MI (self, SHAPE_SHAPE) != LRG_PARTICLE_SHAPE_MESH;
}

/* ----- velocity ----- */

enum { VEL_SPEED, VEL_DIRECTION, VEL_SPREAD, VEL_INHERIT };

static const ParamDesc velocity_params[] = {
	{ "speed", P_RANGE, NULL, { 1, 1 }, -1e6, 1e6, "Initial speed [min, max]" },
	{ "direction", P_VEC3, NULL, { 0, 0, 0 }, 0, 0, "Fixed direction (system frame); zero uses the shape's" },
	{ "spread", P_FLOAT, NULL, { 0 }, 0, 180, "Random cone around the direction, half angle in degrees" },
	{ "inherit", P_FLOAT, NULL, { 0 }, -10, 10, "Share of the emitter's (or parent particle's) velocity" },
};

static void
velocity_init (BuiltinModule            *self,
               LrgParticleModuleContext *ctx,
               LrgParticle              *p)
{
	gfloat dir[3];
	gfloat out[3];
	gfloat speed;
	gfloat inherit = MF (self, VEL_INHERIT);

	if (v3_len (MV (self, VEL_DIRECTION)) > 1e-6f)
	{
		gfloat d[3];

		memcpy (d, MV (self, VEL_DIRECTION), sizeof d);
		v3_normalize (d);
		quat_rotate (ctx->rotation, d, dir);
	}
	else
		memcpy (dir, ctx->direction, sizeof dir);
	if (!v3_normalize (dir))
	{
		dir[0] = 0.0f;
		dir[1] = 1.0f;
		dir[2] = 0.0f;
	}
	cone_direction (ctx->rng, dir, MF (self, VEL_SPREAD) * DEG2RAD_F, out);
	speed = prange (self, VEL_SPEED, ctx);
	p->velocity_x = out[0] * speed + ctx->inherit_velocity[0] * inherit;
	p->velocity_y = out[1] * speed + ctx->inherit_velocity[1] * inherit;
	p->velocity_z = out[2] * speed + ctx->inherit_velocity[2] * inherit;
}

/* ----- lifetime, size, rotation ----- */

static const ParamDesc lifetime_params[] = {
	{ "lifetime", P_RANGE, NULL, { 1, 1 }, 0.001, 1e6, "Seconds a particle lives [min, max]" },
};

static void
lifetime_init (BuiltinModule            *self,
               LrgParticleModuleContext *ctx,
               LrgParticle              *p)
{
	p->max_life = MAX (prange (self, 0, ctx), 0.001f);
	p->life = p->max_life;
}

static const ParamDesc size_params[] = {
	{ "size", P_RANGE, NULL, { 1, 1 }, 0, 1e6, "Initial size [min, max]" },
};

static void
size_init (BuiltinModule            *self,
           LrgParticleModuleContext *ctx,
           LrgParticle              *p)
{
	p->size = prange (self, 0, ctx);
}

enum { ROT_ROTATION, ROT_ANGULAR };

static const ParamDesc rotation_params[] = {
	{ "rotation", P_RANGE, NULL, { 0, 0 }, -1e6, 1e6, "Initial rotation in degrees [min, max]" },
	{ "angular-velocity", P_RANGE, NULL, { 0, 0 }, -1e6, 1e6, "Initial spin in degrees per second [min, max]" },
};

static void
rotation_init (BuiltinModule            *self,
               LrgParticleModuleContext *ctx,
               LrgParticle              *p)
{
	p->rotation = prange (self, ROT_ROTATION, ctx) * DEG2RAD_F;
	p->rotation_velocity = prange (self, ROT_ANGULAR, ctx) * DEG2RAD_F;
}

/* ----- color ----- */

enum { COL_COLOR, COL_COLOR_MAX, COL_GRADIENT };

static const ParamDesc color_params[] = {
	{ "color", P_COLOR, NULL, { 1, 1, 1, 1 }, 0, 0, "Initial colour" },
	{ "color-max", P_COLOR_OPT, NULL, { 1, 1, 1, 1 }, 0, 0, "With it: a random blend between color and color-max" },
	{ "gradient", P_GRADIENT, NULL, { 0 }, 0, 0, "With it: a random sample of the gradient (wins over the colours)" },
};

static void
color_init (BuiltinModule            *self,
            LrgParticleModuleContext *ctx,
            LrgParticle              *p)
{
	gfloat c[4];
	gint   k;

	if (self->v[COL_GRADIENT].gradient != NULL)
		lrg_color_gradient_evaluate_fast (self->v[COL_GRADIENT].gradient,
		                                  lrg_particle_rng_next_float (ctx->rng), c);
	else if (self->v[COL_COLOR_MAX].set)
	{
		gfloat t = lrg_particle_rng_next_float (ctx->rng);

		for (k = 0; k < 4; k++)
			c[k] = self->v[COL_COLOR].f[k] + (self->v[COL_COLOR_MAX].f[k] - self->v[COL_COLOR].f[k]) * t;
	}
	else
		memcpy (c, self->v[COL_COLOR].f, sizeof c);
	p->color_r = c[0];
	p->color_g = c[1];
	p->color_b = c[2];
	p->color_a = c[3];
}

/* ----- over life ----- */

enum { SOL_CURVE, SOL_MULTIPLIER };

static const ParamDesc size_over_life_params[] = {
	{ "curve", P_CURVE, NULL, { 0 }, 0, 0, "Size multiplier over the normalized age" },
	{ "multiplier", P_FLOAT, NULL, { 1 }, 0, 1e6, "Constant factor" },
};

static void
size_over_life_update (BuiltinModule            *self,
                       LrgParticleModuleContext *ctx,
                       LrgParticle              *p)
{
	(void) ctx;
	p->size = p->start_size * pcurve (self, SOL_CURVE, life_t (p)) * MF (self, SOL_MULTIPLIER);
}

static const ParamDesc color_over_life_params[] = {
	{ "gradient", P_GRADIENT, NULL, { 0 }, 0, 0, "Colour multiplier (RGBA) over the normalized age" },
};

static void
color_over_life_update (BuiltinModule            *self,
                        LrgParticleModuleContext *ctx,
                        LrgParticle              *p)
{
	gfloat c[4];

	(void) ctx;
	if (self->v[0].gradient == NULL)
		return;
	lrg_color_gradient_evaluate_fast (self->v[0].gradient, life_t (p), c);
	p->color_r = p->start_color_r * c[0];
	p->color_g = p->start_color_g * c[1];
	p->color_b = p->start_color_b * c[2];
	p->color_a = p->start_color_a * c[3];
}

static const ParamDesc alpha_over_life_params[] = {
	{ "curve", P_CURVE, NULL, { 0 }, 0, 0, "Alpha multiplier over the normalized age" },
};

static void
alpha_over_life_update (BuiltinModule            *self,
                        LrgParticleModuleContext *ctx,
                        LrgParticle              *p)
{
	(void) ctx;
	p->color_a = p->start_color_a * pcurve (self, 0, life_t (p));
}

static const ParamDesc rotation_over_life_params[] = {
	{ "curve", P_CURVE, NULL, { 0 }, 0, 0, "Angular velocity multiplier over the normalized age" },
};

static void
rotation_over_life_update (BuiltinModule            *self,
                           LrgParticleModuleContext *ctx,
                           LrgParticle              *p)
{
	(void) ctx;
	p->rotation_velocity = p->start_rotation_velocity * pcurve (self, 0, life_t (p));
}

/* ----- forces ----- */

static const ParamDesc gravity_params[] = {
	{ "gravity", P_VEC3, NULL, { 0, -9.81, 0 }, 0, 0, "Acceleration in units per second squared" },
};

static void
gravity_update (BuiltinModule            *self,
                LrgParticleModuleContext *ctx,
                LrgParticle              *p)
{
	const gfloat *g = MV (self, 0);

	p->velocity_x += g[0] * ctx->dt;
	p->velocity_y += g[1] * ctx->dt;
	p->velocity_z += g[2] * ctx->dt;
}

enum { WIND_VELOCITY, WIND_STRENGTH, WIND_TURBULENCE };

static const ParamDesc wind_params[] = {
	{ "velocity", P_VEC3, NULL, { 2, 0, 0 }, 0, 0, "Wind velocity particles are pushed toward" },
	{ "strength", P_FLOAT, NULL, { 1 }, 0, 1e6, "How fast particles reach the wind's speed (1/s)" },
	{ "turbulence", P_FLOAT, NULL, { 0 }, 0, 10, "Gust amplitude as a share of the wind speed" },
};

static void
wind_update (BuiltinModule            *self,
             LrgParticleModuleContext *ctx,
             LrgParticle              *p)
{
	const gfloat *w = MV (self, WIND_VELOCITY);
	gfloat        gust;
	gfloat        k = MIN (1.0f, MF (self, WIND_STRENGTH) * ctx->dt);

	/* gusts: a slow sine per particle, phase from its random number */
	gust = 1.0f + MF (self, WIND_TURBULENCE) * sinf (ctx->time * 1.7f + p->random * 6.2831853f) *
	       cosf (ctx->time * 0.63f + p->random * 11.0f);
	p->velocity_x += (w[0] * gust - p->velocity_x) * k;
	p->velocity_y += (w[1] * gust - p->velocity_y) * k;
	p->velocity_z += (w[2] * gust - p->velocity_z) * k;
}

static const ParamDesc drag_params[] = {
	{ "drag", P_FLOAT, NULL, { 1 }, 0, 1e6, "Linear drag: velocity decays as exp (-drag * t)" },
};

static void
drag_update (BuiltinModule            *self,
             LrgParticleModuleContext *ctx,
             LrgParticle              *p)
{
	gfloat f = expf (-MF (self, 0) * ctx->dt);

	p->velocity_x *= f;
	p->velocity_y *= f;
	p->velocity_z *= f;
}

enum { TURB_AMPLITUDE, TURB_FREQUENCY, TURB_SCROLL, TURB_OCTAVES };

static const ParamDesc turbulence_params[] = {
	{ "amplitude", P_FLOAT, NULL, { 1 }, 0, 1e6, "Acceleration of the noise field" },
	{ "frequency", P_FLOAT, NULL, { 1 }, 0, 1e6, "Noise features per world unit" },
	{ "scroll-speed", P_FLOAT, NULL, { 0.5 }, -1e6, 1e6, "How fast the field changes over time" },
	{ "octaves", P_INT, NULL, { 1 }, 1, 4, "Noise octaves (each twice the frequency, half the amplitude)" },
};

/* Integer lattice hash to [-1, 1] */
static gfloat
lattice (gint x,
         gint y,
         gint z,
         gint channel)
{
	guint32 h = (guint32) x * 73856093u ^ (guint32) y * 19349663u ^ (guint32) z * 83492791u ^
	            (guint32) channel * 2654435761u;

	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return (gfloat) (h & 0xffffu) / 32767.5f - 1.0f;
}

/* Trilinear value noise with smoothstep weights, in [-1, 1] */
static gfloat
value_noise (gfloat x,
             gfloat y,
             gfloat z,
             gint   channel)
{
	gfloat fx = floorf (x), fy = floorf (y), fz = floorf (z);
	gint   ix = (gint) fx, iy = (gint) fy, iz = (gint) fz;
	gfloat tx = x - fx, ty = y - fy, tz = z - fz;
	gfloat c[8];
	gint   k;

	tx = tx * tx * (3.0f - 2.0f * tx);
	ty = ty * ty * (3.0f - 2.0f * ty);
	tz = tz * tz * (3.0f - 2.0f * tz);
	for (k = 0; k < 8; k++)
		c[k] = lattice (ix + (k & 1), iy + ((k >> 1) & 1), iz + ((k >> 2) & 1), channel);
	c[0] += (c[1] - c[0]) * tx;
	c[2] += (c[3] - c[2]) * tx;
	c[4] += (c[5] - c[4]) * tx;
	c[6] += (c[7] - c[6]) * tx;
	c[0] += (c[2] - c[0]) * ty;
	c[4] += (c[6] - c[4]) * ty;
	return c[0] + (c[4] - c[0]) * tz;
}

static void
turbulence_update (BuiltinModule            *self,
                   LrgParticleModuleContext *ctx,
                   LrgParticle              *p)
{
	gfloat amp = MF (self, TURB_AMPLITUDE);
	gfloat freq = MF (self, TURB_FREQUENCY);
	gfloat s = ctx->time * MF (self, TURB_SCROLL);
	gfloat a[3] = { 0.0f, 0.0f, 0.0f };
	gint   o, k;

	for (o = 0; o < MI (self, TURB_OCTAVES); o++)
	{
		gfloat f = freq * (gfloat) (1 << o);
		gfloat w = amp / (gfloat) (1 << o);

		for (k = 0; k < 3; k++)
			a[k] += w * value_noise (p->position_x * f + s, p->position_y * f + s * 0.7f,
			                         p->position_z * f - s * 0.3f, k + o * 3);
	}
	p->velocity_x += a[0] * ctx->dt;
	p->velocity_y += a[1] * ctx->dt;
	p->velocity_z += a[2] * ctx->dt;
}

enum { VORTEX_AXIS, VORTEX_CENTER, VORTEX_STRENGTH, VORTEX_PULL };

static const ParamDesc vortex_params[] = {
	{ "axis", P_VEC3, NULL, { 0, 1, 0 }, 0, 0, "Swirl axis (system frame)" },
	{ "center", P_VEC3, NULL, { 0, 0, 0 }, 0, 0, "A point on the axis, relative to the system" },
	{ "strength", P_FLOAT, NULL, { 1 }, -1e6, 1e6, "Tangential acceleration (positive: counter-clockwise about the axis)" },
	{ "pull", P_FLOAT, NULL, { 0 }, -1e6, 1e6, "Acceleration toward the axis" },
};

static void
vortex_update (BuiltinModule            *self,
               LrgParticleModuleContext *ctx,
               LrgParticle              *p)
{
	gfloat axis[3], ax[3], c[3], r[3], tang[3];
	gfloat d;
	gint   k;

	memcpy (ax, MV (self, VORTEX_AXIS), sizeof ax);
	if (!v3_normalize (ax))
		return;
	quat_rotate (ctx->rotation, ax, axis);
	quat_rotate (ctx->rotation, MV (self, VORTEX_CENTER), c);
	r[0] = p->position_x - (ctx->origin[0] + c[0]);
	r[1] = p->position_y - (ctx->origin[1] + c[1]);
	r[2] = p->position_z - (ctx->origin[2] + c[2]);

	/* the radial part: r without its component along the axis */
	d = r[0] * axis[0] + r[1] * axis[1] + r[2] * axis[2];
	for (k = 0; k < 3; k++)
		r[k] -= axis[k] * d;
	v3_cross (axis, r, tang);
	if (!v3_normalize (tang))
		return;
	v3_normalize (r);
	p->velocity_x += (tang[0] * MF (self, VORTEX_STRENGTH) - r[0] * MF (self, VORTEX_PULL)) * ctx->dt;
	p->velocity_y += (tang[1] * MF (self, VORTEX_STRENGTH) - r[1] * MF (self, VORTEX_PULL)) * ctx->dt;
	p->velocity_z += (tang[2] * MF (self, VORTEX_STRENGTH) - r[2] * MF (self, VORTEX_PULL)) * ctx->dt;
}

enum { ATT_POSITION, ATT_STRENGTH, ATT_RADIUS, ATT_FALLOFF, ATT_KILL };

static const ParamDesc attractor_params[] = {
	{ "position", P_VEC3, NULL, { 0, 0, 0 }, 0, 0, "Attractor position relative to the system" },
	{ "strength", P_FLOAT, NULL, { 5 }, -1e6, 1e6, "Acceleration toward it (negative repels)" },
	{ "radius", P_FLOAT, NULL, { 0 }, 0, 1e6, "Range of influence (0: unlimited)" },
	{ "falloff", P_FLOAT, NULL, { 0 }, 0, 4, "Distance exponent: 0 constant, 2 inverse square" },
	{ "kill-radius", P_FLOAT, NULL, { 0 }, 0, 1e6, "Particles closer than this die (0: never)" },
};

static void
attractor_update (BuiltinModule            *self,
                  LrgParticleModuleContext *ctx,
                  LrgParticle              *p)
{
	gfloat c[3], d[3], dist, a;

	quat_rotate (ctx->rotation, MV (self, ATT_POSITION), c);
	d[0] = ctx->origin[0] + c[0] - p->position_x;
	d[1] = ctx->origin[1] + c[1] - p->position_y;
	d[2] = ctx->origin[2] + c[2] - p->position_z;
	dist = v3_len (d);
	if (MF (self, ATT_RADIUS) > 0.0f && dist > MF (self, ATT_RADIUS))
		return;
	if (MF (self, ATT_KILL) > 0.0f && dist < MF (self, ATT_KILL))
	{
		p->life = 0.0f;
		p->alive = FALSE;
		return;
	}
	if (!(dist > 1e-6f))
		return;
	a = MF (self, ATT_STRENGTH) / powf (MAX (dist, 0.01f), MF (self, ATT_FALLOFF)) / dist;
	p->velocity_x += d[0] * a * ctx->dt;
	p->velocity_y += d[1] * a * ctx->dt;
	p->velocity_z += d[2] * a * ctx->dt;
}

/* ----- collision ----- */

enum { COLL_MODE, COLL_NORMAL, COLL_OFFSET, COLL_BOUNCE, COLL_FRICTION, COLL_LIFETIME_LOSS, COLL_KILL,
       COLL_RADIUS_SCALE, COLL_LAYER_MASK };

static const ParamDesc collision_params[] = {
	{ "mode", P_ENUM, lrg_particle_collision_mode_get_type, { LRG_PARTICLE_COLLISION_PLANE }, 0, 0, "Plane or world" },
	{ "normal", P_VEC3, NULL, { 0, 1, 0 }, 0, 0, "Plane normal (world space)" },
	{ "offset", P_FLOAT, NULL, { 0 }, -1e6, 1e6, "Plane offset: points p with dot (normal, p) = offset" },
	{ "bounce", P_FLOAT, NULL, { 0.5 }, 0, 2, "Share of the normal speed kept, reflected" },
	{ "friction", P_FLOAT, NULL, { 0 }, 0, 1, "Share of the tangential speed lost per hit" },
	{ "lifetime-loss", P_FLOAT, NULL, { 0 }, 0, 1, "Share of the lifetime lost per hit" },
	{ "kill", P_BOOL, NULL, { 0 }, 0, 0, "Kill on the first hit" },
	{ "radius-scale", P_FLOAT, NULL, { 0 }, 0, 10, "Collision radius as a share of half the particle size" },
	{ "layer-mask", P_UINT, NULL, { 4294967295.0 }, 0, 4294967295.0, "Layers the world raycast tests" },
};

static void
collision_respond (BuiltinModule            *self,
                   LrgParticleModuleContext *ctx,
                   LrgParticle              *p,
                   const gfloat             *point,
                   const gfloat             *n)
{
	gfloat vn = p->velocity_x * n[0] + p->velocity_y * n[1] + p->velocity_z * n[2];
	gfloat keep = 1.0f - MF (self, COLL_FRICTION);
	gfloat bounce = MF (self, COLL_BOUNCE);
	gfloat v[3];
	gint   k;

	/* split into normal and tangential parts; reflect and damp */
	v[0] = p->velocity_x - n[0] * vn;
	v[1] = p->velocity_y - n[1] * vn;
	v[2] = p->velocity_z - n[2] * vn;
	for (k = 0; k < 3; k++)
		v[k] = v[k] * keep - n[k] * vn * bounce;
	p->velocity_x = v[0];
	p->velocity_y = v[1];
	p->velocity_z = v[2];
	p->collisions++;
	p->life -= MF (self, COLL_LIFETIME_LOSS) * p->max_life;
	if (MI (self, COLL_KILL) || p->life <= 0.0f)
	{
		p->life = 0.0f;
		p->alive = FALSE;
	}
	lrg_particle_system_raise_event (ctx->system, p, LRG_PARTICLE_EVENT_COLLISION, point, n);
}

static void
collision_update (BuiltinModule            *self,
                  LrgParticleModuleContext *ctx,
                  LrgParticle              *p)
{
	gfloat radius = p->size * 0.5f * MF (self, COLL_RADIUS_SCALE);

	if (MI (self, COLL_MODE) == LRG_PARTICLE_COLLISION_PLANE)
	{
		gfloat n[3], dist, vn, point[3];

		memcpy (n, MV (self, COLL_NORMAL), sizeof n);
		if (!v3_normalize (n))
			return;
		dist = p->position_x * n[0] + p->position_y * n[1] + p->position_z * n[2] - MF (self, COLL_OFFSET) - radius;
		vn = p->velocity_x * n[0] + p->velocity_y * n[1] + p->velocity_z * n[2];
		if (dist >= 0.0f || vn >= 0.0f)
			return;
		/* push back onto the plane (plus the radius) */
		p->position_x -= n[0] * dist;
		p->position_y -= n[1] * dist;
		p->position_z -= n[2] * dist;
		point[0] = p->position_x - n[0] * radius;
		point[1] = p->position_y - n[1] * radius;
		point[2] = p->position_z - n[2] * radius;
		collision_respond (self, ctx, p, point, n);
	}
	else
	{
		gfloat from[3], to[3], point[3], n[3];

		to[0] = p->position_x;
		to[1] = p->position_y;
		to[2] = p->position_z;
		from[0] = to[0] - p->velocity_x * ctx->dt;
		from[1] = to[1] - p->velocity_y * ctx->dt;
		from[2] = to[2] - p->velocity_z * ctx->dt;
		if (!lrg_particle_system_raycast (ctx->system, from, to, (guint32) MI (self, COLL_LAYER_MASK), point, n))
			return;
		if (p->velocity_x * n[0] + p->velocity_y * n[1] + p->velocity_z * n[2] >= 0.0f)
			return;
		p->position_x = point[0] + n[0] * (radius + 1e-3f);
		p->position_y = point[1] + n[1] * (radius + 1e-3f);
		p->position_z = point[2] + n[2] * (radius + 1e-3f);
		collision_respond (self, ctx, p, point, n);
	}
}

static gboolean
collision_gpu (BuiltinModule *self)
{
	return MI (self, COLL_MODE) == LRG_PARTICLE_COLLISION_PLANE;
}

/* ----- sub-emitter ----- */

enum { SUB_TRIGGER, SUB_EMITTER, SUB_COUNT, SUB_PROBABILITY, SUB_INHERIT };

static const ParamDesc sub_emitter_params[] = {
	{ "trigger", P_ENUM, lrg_particle_event_get_type, { LRG_PARTICLE_EVENT_DEATH }, 0, 0, "Event that fires it" },
	{ "emitter", P_STRING, NULL, { 0 }, 0, 0, "Name of the emitter it spawns into" },
	{ "count", P_RANGE, NULL, { 1, 1 }, 0, 100000, "Particles per event [min, max]" },
	{ "probability", P_FLOAT, NULL, { 1 }, 0, 1, "Chance per event" },
	{ "inherit-velocity", P_FLOAT, NULL, { 0 }, -10, 10, "Share of the parent particle's velocity passed on" },
};

static void
sub_emitter_event (BuiltinModule            *self,
                   LrgParticleModuleContext *ctx,
                   LrgParticle              *p,
                   LrgParticleEvent          event)
{
	LrgParticleSystem *target;
	gfloat             vel[3];
	gfloat             k = MF (self, SUB_INHERIT);
	guint              n;

	if ((gint) event != MI (self, SUB_TRIGGER))
		return;
	target = lrg_particle_system_lookup (ctx->system, self->v[SUB_EMITTER].s);
	if (target == NULL || target == ctx->system)
		return;
	/* the draws come from the parent's generator, in event order */
	if (MF (self, SUB_PROBABILITY) < 1.0f && lrg_particle_rng_next_float (ctx->rng) >= MF (self, SUB_PROBABILITY))
		return;
	n = range_count (self, SUB_COUNT, ctx);
	vel[0] = p->velocity_x * k;
	vel[1] = p->velocity_y * k;
	vel[2] = p->velocity_z * k;
	if (n > 0)
		lrg_particle_system_spawn (target, ctx->event_point, vel, n);
}

static gboolean
never_gpu (BuiltinModule *self)
{
	(void) self;
	return FALSE;
}

/* ----- the table ----- */

#define KIND(name, tname, stage, params, ...) \
	{ name, tname, stage, params, G_N_ELEMENTS (params), __VA_ARGS__ }

static const KindDesc builtin_kinds[] = {
	KIND ("spawn-rate", "LrgParticleModuleSpawnRate", LRG_PARTICLE_MODULE_STAGE_SPAWN, rate_params,
	      NULL, rate_spawn, NULL, NULL, NULL, NULL),
	KIND ("burst", "LrgParticleModuleBurst", LRG_PARTICLE_MODULE_STAGE_SPAWN, burst_params,
	      NULL, burst_spawn, NULL, NULL, NULL, NULL),
	KIND ("shape", "LrgParticleModuleShape", LRG_PARTICLE_MODULE_STAGE_SHAPE, shape_params,
	      NULL, NULL, shape_init, NULL, NULL, shape_gpu),
	KIND ("velocity", "LrgParticleModuleVelocity", LRG_PARTICLE_MODULE_STAGE_INIT, velocity_params,
	      NULL, NULL, velocity_init, NULL, NULL, NULL),
	KIND ("lifetime", "LrgParticleModuleLifetime", LRG_PARTICLE_MODULE_STAGE_INIT, lifetime_params,
	      NULL, NULL, lifetime_init, NULL, NULL, NULL),
	KIND ("size", "LrgParticleModuleSize", LRG_PARTICLE_MODULE_STAGE_INIT, size_params,
	      NULL, NULL, size_init, NULL, NULL, NULL),
	KIND ("color", "LrgParticleModuleColor", LRG_PARTICLE_MODULE_STAGE_INIT, color_params,
	      NULL, NULL, color_init, NULL, NULL, NULL),
	KIND ("rotation", "LrgParticleModuleRotation", LRG_PARTICLE_MODULE_STAGE_INIT, rotation_params,
	      NULL, NULL, rotation_init, NULL, NULL, NULL),
	KIND ("size-over-life", "LrgParticleModuleSizeOverLife", LRG_PARTICLE_MODULE_STAGE_OVER_LIFE,
	      size_over_life_params, NULL, NULL, NULL, size_over_life_update, NULL, NULL),
	KIND ("color-over-life", "LrgParticleModuleColorOverLife", LRG_PARTICLE_MODULE_STAGE_OVER_LIFE,
	      color_over_life_params, NULL, NULL, NULL, color_over_life_update, NULL, NULL),
	KIND ("alpha-over-life", "LrgParticleModuleAlphaOverLife", LRG_PARTICLE_MODULE_STAGE_OVER_LIFE,
	      alpha_over_life_params, NULL, NULL, NULL, alpha_over_life_update, NULL, NULL),
	KIND ("rotation-over-life", "LrgParticleModuleRotationOverLife", LRG_PARTICLE_MODULE_STAGE_OVER_LIFE,
	      rotation_over_life_params, NULL, NULL, NULL, rotation_over_life_update, NULL, NULL),
	KIND ("gravity", "LrgParticleModuleGravity", LRG_PARTICLE_MODULE_STAGE_FORCE, gravity_params,
	      NULL, NULL, NULL, gravity_update, NULL, NULL),
	KIND ("wind", "LrgParticleModuleWind", LRG_PARTICLE_MODULE_STAGE_FORCE, wind_params,
	      NULL, NULL, NULL, wind_update, NULL, NULL),
	KIND ("drag", "LrgParticleModuleDrag", LRG_PARTICLE_MODULE_STAGE_FORCE, drag_params,
	      NULL, NULL, NULL, drag_update, NULL, NULL),
	KIND ("turbulence", "LrgParticleModuleTurbulence", LRG_PARTICLE_MODULE_STAGE_FORCE, turbulence_params,
	      NULL, NULL, NULL, turbulence_update, NULL, NULL),
	KIND ("vortex", "LrgParticleModuleVortex", LRG_PARTICLE_MODULE_STAGE_FORCE, vortex_params,
	      NULL, NULL, NULL, vortex_update, NULL, NULL),
	KIND ("attractor", "LrgParticleModuleAttractor", LRG_PARTICLE_MODULE_STAGE_FORCE, attractor_params,
	      NULL, NULL, NULL, attractor_update, NULL, NULL),
	KIND ("collision", "LrgParticleModuleCollision", LRG_PARTICLE_MODULE_STAGE_COLLISION, collision_params,
	      NULL, NULL, NULL, collision_update, NULL, collision_gpu),
	KIND ("sub-emitter", "LrgParticleModuleSubEmitter", LRG_PARTICLE_MODULE_STAGE_EVENT, sub_emitter_params,
	      NULL, NULL, NULL, NULL, sub_emitter_event, never_gpu),
};

/* ==========================================================================
 * 4. Kind registry
 * ========================================================================== */

typedef struct
{
	gchar *kind;
	GType  type;
} KindEntry;

static GPtrArray *kind_registry;   /* KindEntry *, registration order */
static GMutex     kind_lock;

/* Registers the built-ins once (under the lock) */
static void
ensure_kinds_locked (void)
{
	guint i;

	if (kind_registry != NULL)
		return;
	kind_registry = g_ptr_array_new ();
	for (i = 0; i < G_N_ELEMENTS (builtin_kinds); i++)
	{
		const KindDesc *d = &builtin_kinds[i];
		GTypeInfo       info;
		KindEntry      *e;

		memset (&info, 0, sizeof info);
		info.class_size = sizeof (BuiltinModuleClass);
		info.class_init = kind_class_init;
		info.class_data = d;
		info.instance_size = sizeof (BuiltinModule);
		info.instance_init = kind_instance_init;

		e = g_new0 (KindEntry, 1);
		e->kind = g_strdup (d->kind);
		e->type = g_type_from_name (d->type_name);
		if (e->type == G_TYPE_INVALID)
			e->type = g_type_register_static (builtin_module_get_type (), d->type_name, &info, 0);
		g_ptr_array_add (kind_registry, e);
	}
}

static KindEntry *
find_kind_locked (const gchar *kind)
{
	guint i;

	ensure_kinds_locked ();
	for (i = 0; i < kind_registry->len; i++)
	{
		KindEntry *e = g_ptr_array_index (kind_registry, i);

		if (g_strcmp0 (e->kind, kind) == 0)
			return e;
	}
	return NULL;
}

void
lrg_particle_module_register_kind (const gchar *kind,
                                   GType        type)
{
	KindEntry *e;

	g_return_if_fail (kind != NULL && *kind != '\0');
	g_return_if_fail (g_type_is_a (type, LRG_TYPE_PARTICLE_MODULE) && !G_TYPE_IS_ABSTRACT (type));

	g_mutex_lock (&kind_lock);
	e = find_kind_locked (kind);
	if (e == NULL)
	{
		e = g_new0 (KindEntry, 1);
		e->kind = g_strdup (kind);
		g_ptr_array_add (kind_registry, e);
	}
	e->type = type;
	g_mutex_unlock (&kind_lock);
}

GType
lrg_particle_module_lookup_kind (const gchar *kind)
{
	KindEntry *e;
	GType      type;

	g_return_val_if_fail (kind != NULL, G_TYPE_INVALID);

	g_mutex_lock (&kind_lock);
	e = find_kind_locked (kind);
	type = e != NULL ? e->type : G_TYPE_INVALID;
	g_mutex_unlock (&kind_lock);
	return type;
}

GStrv
lrg_particle_module_list_kinds (void)
{
	g_autoptr(GStrvBuilder) b = g_strv_builder_new ();
	guint                   i;

	g_mutex_lock (&kind_lock);
	ensure_kinds_locked ();
	for (i = 0; i < kind_registry->len; i++)
		g_strv_builder_add (b, ((KindEntry *) g_ptr_array_index (kind_registry, i))->kind);
	g_mutex_unlock (&kind_lock);
	return g_strv_builder_end (b);
}

const gchar *
lrg_particle_module_get_kind (LrgParticleModule *self)
{
	const gchar *kind = NULL;
	GType        type;
	guint        i;

	g_return_val_if_fail (LRG_IS_PARTICLE_MODULE (self), NULL);

	type = G_OBJECT_TYPE (self);
	g_mutex_lock (&kind_lock);
	ensure_kinds_locked ();
	for (i = 0; i < kind_registry->len; i++)
	{
		KindEntry *e = g_ptr_array_index (kind_registry, i);

		if (e->type == type)
		{
			kind = e->kind;   /* entries are never freed */
			break;
		}
	}
	g_mutex_unlock (&kind_lock);
	return kind;
}

LrgParticleModule *
lrg_particle_module_new (const gchar *kind)
{
	GType type;

	g_return_val_if_fail (kind != NULL, NULL);

	type = lrg_particle_module_lookup_kind (kind);
	if (type == G_TYPE_INVALID)
		return NULL;
	return g_object_new (type, NULL);
}

gboolean
lrg_particle_module_get_gpu_capable (LrgParticleModule *self)
{
	BuiltinModule *b;

	g_return_val_if_fail (LRG_IS_PARTICLE_MODULE (self), FALSE);

	if (!IS_BUILTIN (self))
		return FALSE;
	b = BUILTIN (self);
	return b->desc->gpu_capable != NULL ? b->desc->gpu_capable (b) : TRUE;
}

gboolean
lrg_particle_module_set_surface (LrgParticleModule *self,
                                 const gfloat      *positions,
                                 guint              n_floats,
                                 const guint       *indices,
                                 guint              n_indices)
{
	BuiltinModule *b;
	guint          n_vertices, n_tris, t;
	gfloat         total = 0.0f;

	g_return_val_if_fail (LRG_IS_PARTICLE_MODULE (self), FALSE);

	if (!IS_BUILTIN (self) || BUILTIN (self)->desc->init_particle != shape_init)
		return FALSE;
	b = BUILTIN (self);
	g_clear_pointer (&b->tri, g_array_unref);
	g_clear_pointer (&b->cdf, g_array_unref);
	if (positions == NULL || n_floats < 9)
		return FALSE;

	n_vertices = n_floats / 3;
	n_tris = indices != NULL ? n_indices / 3 : n_vertices / 3;
	b->tri = g_array_sized_new (FALSE, FALSE, sizeof (gfloat), n_tris * 9);
	b->cdf = g_array_sized_new (FALSE, FALSE, sizeof (gfloat), n_tris);

	/* keep the triangles with area, with their running total */
	for (t = 0; t < n_tris; t++)
	{
		gfloat corner[9];
		gfloat e1[3], e2[3], c[3], area;
		gint   k, j;

		for (j = 0; j < 3; j++)
		{
			guint vi = indices != NULL ? indices[t * 3 + (guint) j] : t * 3 + (guint) j;

			if (vi >= n_vertices)
				break;
			for (k = 0; k < 3; k++)
				corner[j * 3 + k] = positions[vi * 3 + (guint) k];
		}
		if (j < 3)
			continue;
		for (k = 0; k < 3; k++)
		{
			e1[k] = corner[3 + k] - corner[k];
			e2[k] = corner[6 + k] - corner[k];
		}
		v3_cross (e1, e2, c);
		area = 0.5f * v3_len (c);
		if (!(area > 1e-12f))
			continue;
		total += area;
		g_array_append_vals (b->tri, corner, 9);
		g_array_append_val (b->cdf, total);
	}
	return b->cdf->len > 0;
}

/* ==========================================================================
 * 5a. LrgParticleRenderSettings
 * ========================================================================== */

enum { RS_KIND, RS_BLEND, RS_LAYER, RS_TEXTURE, RS_COLUMNS, RS_ROWS, RS_FPS,
       RS_RIBBON_MODE, RS_WIDTH, RS_WIDTH_CURVE, RS_COLOR_GRADIENT, RS_UV_MODE, RS_UV_TILE_LENGTH,
       RS_MAX_POINTS, RS_MIN_DISTANCE, RS_TRAIL_LIFETIME,
       RS_MESH, RS_PRIMITIVE, RS_MESH_SCALE, RS_ALIGN, RS_LIT, RS_N };

static GType blend_mode_type (void) { return LRG_TYPE_PARTICLE_BLEND_MODE; }

static const ParamDesc render_params[RS_N] = {
	{ "kind", P_ENUM, lrg_particle_renderer_kind_get_type, { LRG_PARTICLE_RENDERER_BILLBOARD }, 0, 0, "How particles are drawn" },
	{ "blend", P_ENUM, blend_mode_type, { LRG_PARTICLE_BLEND_ALPHA }, 0, 0, "Blend mode" },
	{ "layer", P_UINT, NULL, { 0 }, 0, 31, "Render layer" },
	{ "texture", P_STRING, NULL, { 0 }, 0, 0, "Texture asset (billboards, ribbons, meshes)" },
	{ "columns", P_UINT, NULL, { 1 }, 1, 64, "Flipbook columns (billboards)" },
	{ "rows", P_UINT, NULL, { 1 }, 1, 64, "Flipbook rows (billboards)" },
	{ "fps", P_FLOAT, NULL, { 0 }, 0, 1000, "Flipbook frames per second; 0 plays the sheet once over the life" },
	{ "ribbon-mode", P_ENUM, lrg_particle_ribbon_mode_get_type, { LRG_PARTICLE_RIBBON_TRAIL }, 0, 0, "Trails or one connecting ribbon" },
	{ "width", P_FLOAT, NULL, { 0.2 }, 0, 1e6, "Ribbon width at particle size 1" },
	{ "width-curve", P_CURVE, NULL, { 0 }, 0, 0, "Ribbon width multiplier, head 0 to tail 1" },
	{ "color-gradient", P_GRADIENT, NULL, { 0 }, 0, 0, "Ribbon colour multiplier, head 0 to tail 1" },
	{ "uv-mode", P_ENUM, lrg_particle_ribbon_uv_mode_get_type, { LRG_PARTICLE_RIBBON_UV_STRETCH }, 0, 0, "How u runs along a ribbon" },
	{ "uv-tile-length", P_FLOAT, NULL, { 1 }, 0.0001, 1e6, "World units per texture repeat (uv-mode: tile)" },
	{ "max-points", P_UINT, NULL, { 32 }, 2, 1024, "Most points per trail" },
	{ "min-distance", P_FLOAT, NULL, { 0.05 }, 0, 1e6, "Spacing of trail points" },
	{ "trail-lifetime", P_FLOAT, NULL, { 0.5 }, 0.001, 1e6, "Seconds a trail point lives" },
	{ "mesh", P_STRING, NULL, { 0 }, 0, 0, "Mesh asset to instance (mesh renderer)" },
	{ "primitive", P_ENUM, lrg_particle_mesh_primitive_get_type, { LRG_PARTICLE_MESH_PRIMITIVE_CUBE }, 0, 0, "Built-in mesh without a mesh asset" },
	{ "mesh-scale", P_VEC3, NULL, { 1, 1, 1 }, 0, 0, "Mesh scale at particle size 1" },
	{ "align", P_ENUM, lrg_particle_mesh_align_get_type, { LRG_PARTICLE_MESH_ALIGN_TUMBLE }, 0, 0, "Mesh orientation" },
	{ "lit", P_BOOL, NULL, { 1 }, 0, 0, "Shade meshes by a light direction" },
};

struct _LrgParticleRenderSettings
{
	GObject    parent_instance;
	ParamValue v[RS_N];
};

G_DEFINE_FINAL_TYPE (LrgParticleRenderSettings, lrg_particle_render_settings, G_TYPE_OBJECT)

static GParamSpec *render_pspecs[RS_N];

static void
lrg_particle_render_settings_finalize (GObject *object)
{
	LrgParticleRenderSettings *self = LRG_PARTICLE_RENDER_SETTINGS (object);
	guint                      i;

	for (i = 0; i < RS_N; i++)
		param_value_clear (&self->v[i]);
	G_OBJECT_CLASS (lrg_particle_render_settings_parent_class)->finalize (object);
}

static void
lrg_particle_render_settings_get_property (GObject    *object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
	LrgParticleRenderSettings *self = LRG_PARTICLE_RENDER_SETTINGS (object);

	if (prop_id == 0 || prop_id > RS_N)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}
	param_get (&self->v[prop_id - 1], &render_params[prop_id - 1], value);
}

static void
lrg_particle_render_settings_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
	LrgParticleRenderSettings *self = LRG_PARTICLE_RENDER_SETTINGS (object);

	if (prop_id == 0 || prop_id > RS_N)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}
	if (param_set (&self->v[prop_id - 1], &render_params[prop_id - 1], value))
		g_object_notify_by_pspec (object, pspec);
}

static void
lrg_particle_render_settings_class_init (LrgParticleRenderSettingsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	guint         i;

	object_class->finalize = lrg_particle_render_settings_finalize;
	object_class->get_property = lrg_particle_render_settings_get_property;
	object_class->set_property = lrg_particle_render_settings_set_property;

	/* The properties (one per `renderer:` key) come from render_params;
	 * see lrg_particle_render_settings_new() and the format page for
	 * their meaning. */
	for (i = 0; i < RS_N; i++)
	{
		render_pspecs[i] = param_spec (&render_params[i]);
		g_object_class_install_property (object_class, i + 1, render_pspecs[i]);
	}
}

static void
lrg_particle_render_settings_init (LrgParticleRenderSettings *self)
{
	guint i;

	for (i = 0; i < RS_N; i++)
		param_value_init (&self->v[i], &render_params[i]);
}

LrgParticleRenderSettings *
lrg_particle_render_settings_new (void)
{
	return g_object_new (LRG_TYPE_PARTICLE_RENDER_SETTINGS, NULL);
}

LrgParticleRendererKind
lrg_particle_render_settings_get_kind (LrgParticleRenderSettings *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_RENDER_SETTINGS (self), LRG_PARTICLE_RENDERER_NONE);
	return (LrgParticleRendererKind) self->v[RS_KIND].i;
}

void
lrg_particle_render_settings_set_kind (LrgParticleRenderSettings *self,
                                       LrgParticleRendererKind    kind)
{
	g_return_if_fail (LRG_IS_PARTICLE_RENDER_SETTINGS (self));
	g_object_set (self, "kind", kind, NULL);
}

LrgParticleBlendMode
lrg_particle_render_settings_get_blend (LrgParticleRenderSettings *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_RENDER_SETTINGS (self), LRG_PARTICLE_BLEND_ALPHA);
	return (LrgParticleBlendMode) self->v[RS_BLEND].i;
}

void
lrg_particle_render_settings_set_blend (LrgParticleRenderSettings *self,
                                        LrgParticleBlendMode       blend)
{
	g_return_if_fail (LRG_IS_PARTICLE_RENDER_SETTINGS (self));
	g_object_set (self, "blend", blend, NULL);
}

const gchar *
lrg_particle_render_settings_get_texture (LrgParticleRenderSettings *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_RENDER_SETTINGS (self), NULL);
	return self->v[RS_TEXTURE].s;
}

void
lrg_particle_render_settings_set_texture (LrgParticleRenderSettings *self,
                                          const gchar               *texture)
{
	g_return_if_fail (LRG_IS_PARTICLE_RENDER_SETTINGS (self));
	g_object_set (self, "texture", texture, NULL);
}

/* ==========================================================================
 * 5b. LrgParticleAssetEmitter
 * ========================================================================== */

struct _LrgParticleAssetEmitter
{
	GObject                    parent_instance;

	gchar                     *name;
	guint                      max_particles;
	gboolean                   enabled;
	gfloat                     start_delay;
	LrgParticleRenderSettings *renderer;
	GPtrArray                 *modules;   /* LrgParticleModule *, owned */
};

G_DEFINE_FINAL_TYPE (LrgParticleAssetEmitter, lrg_particle_asset_emitter, G_TYPE_OBJECT)

enum
{
	EM_PROP_0,
	EM_PROP_NAME,
	EM_PROP_MAX_PARTICLES,
	EM_PROP_ENABLED,
	EM_PROP_START_DELAY,
	EM_PROP_RENDERER,
	EM_N_PROPS
};

enum
{
	EM_SIGNAL_CHANGED,
	EM_N_SIGNALS
};

static GParamSpec *emitter_props[EM_N_PROPS];
static guint       emitter_signals[EM_N_SIGNALS];

static void
emitter_changed (LrgParticleAssetEmitter *self)
{
	g_signal_emit (self, emitter_signals[EM_SIGNAL_CHANGED], 0);
}

/* Any property change of a module or the render settings is an edit */
static void
on_child_notify (GObject    *child,
                 GParamSpec *pspec,
                 gpointer    user_data)
{
	(void) child;
	(void) pspec;
	emitter_changed (LRG_PARTICLE_ASSET_EMITTER (user_data));
}

static void
lrg_particle_asset_emitter_finalize (GObject *object)
{
	LrgParticleAssetEmitter *self = LRG_PARTICLE_ASSET_EMITTER (object);
	guint                    i;

	for (i = 0; i < self->modules->len; i++)
		g_signal_handlers_disconnect_by_data (g_ptr_array_index (self->modules, i), self);
	g_signal_handlers_disconnect_by_data (self->renderer, self);
	g_clear_pointer (&self->modules, g_ptr_array_unref);
	g_clear_object (&self->renderer);
	g_clear_pointer (&self->name, g_free);
	G_OBJECT_CLASS (lrg_particle_asset_emitter_parent_class)->finalize (object);
}

static void
lrg_particle_asset_emitter_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
	LrgParticleAssetEmitter *self = LRG_PARTICLE_ASSET_EMITTER (object);

	switch (prop_id)
	{
	case EM_PROP_NAME:          g_value_set_string (value, self->name); break;
	case EM_PROP_MAX_PARTICLES: g_value_set_uint (value, self->max_particles); break;
	case EM_PROP_ENABLED:       g_value_set_boolean (value, self->enabled); break;
	case EM_PROP_START_DELAY:   g_value_set_float (value, self->start_delay); break;
	case EM_PROP_RENDERER:      g_value_set_object (value, self->renderer); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_particle_asset_emitter_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
	LrgParticleAssetEmitter *self = LRG_PARTICLE_ASSET_EMITTER (object);

	switch (prop_id)
	{
	case EM_PROP_NAME:
		lrg_particle_asset_emitter_set_name (self, g_value_get_string (value));
		break;
	case EM_PROP_MAX_PARTICLES:
		lrg_particle_asset_emitter_set_max_particles (self, g_value_get_uint (value));
		break;
	case EM_PROP_ENABLED:
		lrg_particle_asset_emitter_set_enabled (self, g_value_get_boolean (value));
		break;
	case EM_PROP_START_DELAY:
		lrg_particle_asset_emitter_set_start_delay (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_particle_asset_emitter_class_init (LrgParticleAssetEmitterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS;

	object_class->finalize = lrg_particle_asset_emitter_finalize;
	object_class->get_property = lrg_particle_asset_emitter_get_property;
	object_class->set_property = lrg_particle_asset_emitter_set_property;

	/**
	 * LrgParticleAssetEmitter:name:
	 *
	 * The emitter's name, unique in its asset.
	 */
	emitter_props[EM_PROP_NAME] =
		g_param_spec_string ("name", "Name", "Emitter name", "emitter", rw);

	/**
	 * LrgParticleAssetEmitter:max-particles:
	 *
	 * The particle capacity of the emitter's system.
	 */
	emitter_props[EM_PROP_MAX_PARTICLES] =
		g_param_spec_uint ("max-particles", "Max Particles", "Particle capacity", 1, 1000000, 1000, rw);

	/**
	 * LrgParticleAssetEmitter:enabled:
	 *
	 * Whether the emitter is simulated and drawn.
	 */
	emitter_props[EM_PROP_ENABLED] =
		g_param_spec_boolean ("enabled", "Enabled", "Whether the emitter runs", TRUE, rw);

	/**
	 * LrgParticleAssetEmitter:start-delay:
	 *
	 * Seconds after play before the emitter emits.
	 */
	emitter_props[EM_PROP_START_DELAY] =
		g_param_spec_float ("start-delay", "Start Delay", "Seconds before emitting", 0.0f, 1e6f, 0.0f, rw);

	/**
	 * LrgParticleAssetEmitter:renderer:
	 *
	 * The emitter's `renderer:` block (read-only: edit its properties).
	 */
	emitter_props[EM_PROP_RENDERER] =
		g_param_spec_object ("renderer", "Renderer", "Render settings", LRG_TYPE_PARTICLE_RENDER_SETTINGS,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, EM_N_PROPS, emitter_props);

	/**
	 * LrgParticleAssetEmitter::changed:
	 * @self: the emitter
	 *
	 * Emitted after any edit: a property of the emitter, of its render
	 * settings or of any module, or the module stack itself.
	 */
	emitter_signals[EM_SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_particle_asset_emitter_init (LrgParticleAssetEmitter *self)
{
	self->name = g_strdup ("emitter");
	self->max_particles = 1000;
	self->enabled = TRUE;
	self->renderer = lrg_particle_render_settings_new ();
	self->modules = g_ptr_array_new_with_free_func (g_object_unref);
	g_signal_connect (self->renderer, "notify", G_CALLBACK (on_child_notify), self);
}

LrgParticleAssetEmitter *
lrg_particle_asset_emitter_new (const gchar *name)
{
	return g_object_new (LRG_TYPE_PARTICLE_ASSET_EMITTER, "name", name != NULL ? name : "emitter", NULL);
}

const gchar *
lrg_particle_asset_emitter_get_name (LrgParticleAssetEmitter *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self), NULL);
	return self->name;
}

void
lrg_particle_asset_emitter_set_name (LrgParticleAssetEmitter *self,
                                     const gchar             *name)
{
	g_return_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self));
	g_return_if_fail (name != NULL && *name != '\0');

	if (!g_set_str (&self->name, name))
		return;
	g_object_notify_by_pspec (G_OBJECT (self), emitter_props[EM_PROP_NAME]);
	emitter_changed (self);
}

guint
lrg_particle_asset_emitter_get_max_particles (LrgParticleAssetEmitter *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self), 0);
	return self->max_particles;
}

void
lrg_particle_asset_emitter_set_max_particles (LrgParticleAssetEmitter *self,
                                              guint                    max_particles)
{
	g_return_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self));

	max_particles = CLAMP (max_particles, 1u, 1000000u);
	if (self->max_particles == max_particles)
		return;
	self->max_particles = max_particles;
	g_object_notify_by_pspec (G_OBJECT (self), emitter_props[EM_PROP_MAX_PARTICLES]);
	emitter_changed (self);
}

gboolean
lrg_particle_asset_emitter_get_enabled (LrgParticleAssetEmitter *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self), FALSE);
	return self->enabled;
}

void
lrg_particle_asset_emitter_set_enabled (LrgParticleAssetEmitter *self,
                                        gboolean                 enabled)
{
	g_return_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self));

	enabled = !!enabled;
	if (self->enabled == enabled)
		return;
	self->enabled = enabled;
	g_object_notify_by_pspec (G_OBJECT (self), emitter_props[EM_PROP_ENABLED]);
	emitter_changed (self);
}

gfloat
lrg_particle_asset_emitter_get_start_delay (LrgParticleAssetEmitter *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self), 0.0f);
	return self->start_delay;
}

void
lrg_particle_asset_emitter_set_start_delay (LrgParticleAssetEmitter *self,
                                            gfloat                   delay)
{
	g_return_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self));

	delay = MAX (delay, 0.0f);
	if (self->start_delay == delay)
		return;
	self->start_delay = delay;
	g_object_notify_by_pspec (G_OBJECT (self), emitter_props[EM_PROP_START_DELAY]);
	emitter_changed (self);
}

LrgParticleRenderSettings *
lrg_particle_asset_emitter_get_renderer (LrgParticleAssetEmitter *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self), NULL);
	return self->renderer;
}

guint
lrg_particle_asset_emitter_get_n_modules (LrgParticleAssetEmitter *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self), 0);
	return self->modules->len;
}

LrgParticleModule *
lrg_particle_asset_emitter_get_module (LrgParticleAssetEmitter *self,
                                       guint                    index)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self), NULL);
	if (index >= self->modules->len)
		return NULL;
	return g_ptr_array_index (self->modules, index);
}

LrgParticleModule *
lrg_particle_asset_emitter_find_module (LrgParticleAssetEmitter *self,
                                        const gchar             *kind)
{
	guint i;

	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self), NULL);
	g_return_val_if_fail (kind != NULL, NULL);

	for (i = 0; i < self->modules->len; i++)
	{
		LrgParticleModule *m = g_ptr_array_index (self->modules, i);

		if (g_strcmp0 (lrg_particle_module_get_kind (m), kind) == 0)
			return m;
	}
	return NULL;
}

void
lrg_particle_asset_emitter_insert_module (LrgParticleAssetEmitter *self,
                                          LrgParticleModule       *module,
                                          gint                     index)
{
	g_return_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self));
	g_return_if_fail (LRG_IS_PARTICLE_MODULE (module));

	if (index < 0 || (guint) index > self->modules->len)
		index = (gint) self->modules->len;
	g_ptr_array_insert (self->modules, index, g_object_ref (module));
	g_signal_connect (module, "notify", G_CALLBACK (on_child_notify), self);
	emitter_changed (self);
}

LrgParticleModule *
lrg_particle_asset_emitter_add_module (LrgParticleAssetEmitter *self,
                                       const gchar             *kind)
{
	g_autoptr(LrgParticleModule) m = NULL;

	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self), NULL);

	m = lrg_particle_module_new (kind);
	if (m == NULL)
		return NULL;
	lrg_particle_asset_emitter_insert_module (self, m, -1);
	return m;
}

gboolean
lrg_particle_asset_emitter_remove_module (LrgParticleAssetEmitter *self,
                                          guint                    index)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self), FALSE);

	if (index >= self->modules->len)
		return FALSE;
	g_signal_handlers_disconnect_by_data (g_ptr_array_index (self->modules, index), self);
	g_ptr_array_remove_index (self->modules, index);
	emitter_changed (self);
	return TRUE;
}

gboolean
lrg_particle_asset_emitter_move_module (LrgParticleAssetEmitter *self,
                                        guint                    from,
                                        guint                    to)
{
	gpointer m;

	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (self), FALSE);

	if (from >= self->modules->len || to >= self->modules->len)
		return FALSE;
	if (from == to)
		return TRUE;
	m = g_ptr_array_steal_index (self->modules, from);
	g_ptr_array_insert (self->modules, (gint) to, m);
	emitter_changed (self);
	return TRUE;
}

/* ==========================================================================
 * 5c. LrgParticleAsset
 * ========================================================================== */

struct _LrgParticleAsset
{
	GObject    parent_instance;

	gchar     *name;
	guint64    seed;
	gfloat     duration;
	gboolean   loop;
	gboolean   prewarm;
	gboolean   local_space;
	GPtrArray *emitters;      /* LrgParticleAssetEmitter *, owned */
	GPtrArray *unknown_keys;  /* gchar *, paths from the last load */
};

G_DEFINE_FINAL_TYPE (LrgParticleAsset, lrg_particle_asset, G_TYPE_OBJECT)

enum
{
	AS_PROP_0,
	AS_PROP_NAME,
	AS_PROP_SEED,
	AS_PROP_DURATION,
	AS_PROP_LOOP,
	AS_PROP_PREWARM,
	AS_PROP_LOCAL_SPACE,
	AS_N_PROPS
};

enum
{
	AS_SIGNAL_CHANGED,
	AS_N_SIGNALS
};

static GParamSpec *asset_props[AS_N_PROPS];
static guint       asset_signals[AS_N_SIGNALS];

static void
asset_changed (LrgParticleAsset *self)
{
	g_signal_emit (self, asset_signals[AS_SIGNAL_CHANGED], 0);
}

static void
on_emitter_changed (LrgParticleAssetEmitter *emitter,
                    gpointer                 user_data)
{
	(void) emitter;
	asset_changed (LRG_PARTICLE_ASSET (user_data));
}

static void
lrg_particle_asset_finalize (GObject *object)
{
	LrgParticleAsset *self = LRG_PARTICLE_ASSET (object);
	guint             i;

	for (i = 0; i < self->emitters->len; i++)
		g_signal_handlers_disconnect_by_data (g_ptr_array_index (self->emitters, i), self);
	g_clear_pointer (&self->emitters, g_ptr_array_unref);
	g_clear_pointer (&self->unknown_keys, g_ptr_array_unref);
	g_clear_pointer (&self->name, g_free);
	G_OBJECT_CLASS (lrg_particle_asset_parent_class)->finalize (object);
}

static void
lrg_particle_asset_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	LrgParticleAsset *self = LRG_PARTICLE_ASSET (object);

	switch (prop_id)
	{
	case AS_PROP_NAME:        g_value_set_string (value, self->name); break;
	case AS_PROP_SEED:        g_value_set_uint64 (value, self->seed); break;
	case AS_PROP_DURATION:    g_value_set_float (value, self->duration); break;
	case AS_PROP_LOOP:        g_value_set_boolean (value, self->loop); break;
	case AS_PROP_PREWARM:     g_value_set_boolean (value, self->prewarm); break;
	case AS_PROP_LOCAL_SPACE: g_value_set_boolean (value, self->local_space); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_particle_asset_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	LrgParticleAsset *self = LRG_PARTICLE_ASSET (object);

	switch (prop_id)
	{
	case AS_PROP_NAME:
		lrg_particle_asset_set_name (self, g_value_get_string (value));
		break;
	case AS_PROP_SEED:
		lrg_particle_asset_set_seed (self, g_value_get_uint64 (value));
		break;
	case AS_PROP_DURATION:
		lrg_particle_asset_set_duration (self, g_value_get_float (value));
		break;
	case AS_PROP_LOOP:
		lrg_particle_asset_set_loop (self, g_value_get_boolean (value));
		break;
	case AS_PROP_PREWARM:
		if (self->prewarm != g_value_get_boolean (value))
		{
			self->prewarm = g_value_get_boolean (value);
			g_object_notify_by_pspec (object, pspec);
			asset_changed (self);
		}
		break;
	case AS_PROP_LOCAL_SPACE:
		if (self->local_space != g_value_get_boolean (value))
		{
			self->local_space = g_value_get_boolean (value);
			g_object_notify_by_pspec (object, pspec);
			asset_changed (self);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_particle_asset_class_init (LrgParticleAssetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS;

	object_class->finalize = lrg_particle_asset_finalize;
	object_class->get_property = lrg_particle_asset_get_property;
	object_class->set_property = lrg_particle_asset_set_property;

	/**
	 * LrgParticleAsset:name:
	 *
	 * Display name of the effect.
	 */
	asset_props[AS_PROP_NAME] = g_param_spec_string ("name", "Name", "Display name", NULL, rw);

	/**
	 * LrgParticleAsset:seed:
	 *
	 * Seed of the effect's generators.
	 */
	asset_props[AS_PROP_SEED] = g_param_spec_uint64 ("seed", "Seed", "Random seed", 0, G_MAXUINT64, 0, rw);

	/**
	 * LrgParticleAsset:duration:
	 *
	 * Seconds per emission cycle; 0 emits forever.
	 */
	asset_props[AS_PROP_DURATION] =
		g_param_spec_float ("duration", "Duration", "Seconds per emission cycle (0: endless)", 0.0f, 1e6f, 0.0f, rw);

	/**
	 * LrgParticleAsset:loop:
	 *
	 * Whether emission cycles repeat.
	 */
	asset_props[AS_PROP_LOOP] = g_param_spec_boolean ("loop", "Loop", "Whether cycles repeat", TRUE, rw);

	/**
	 * LrgParticleAsset:prewarm:
	 *
	 * Whether a looping effect starts as if it had already run one cycle.
	 */
	asset_props[AS_PROP_PREWARM] =
		g_param_spec_boolean ("prewarm", "Prewarm", "Start as if one cycle had run", FALSE, rw);

	/**
	 * LrgParticleAsset:local-space:
	 *
	 * Whether live particles follow the effect when it moves.
	 */
	asset_props[AS_PROP_LOCAL_SPACE] =
		g_param_spec_boolean ("local-space", "Local Space", "Particles follow the effect", FALSE, rw);

	g_object_class_install_properties (object_class, AS_N_PROPS, asset_props);

	/**
	 * LrgParticleAsset::changed:
	 * @self: the asset
	 *
	 * Emitted after any edit of the asset, an emitter, a module or render
	 * settings.  Effects with #LrgParticleEffect:auto-reload reload on it.
	 */
	asset_signals[AS_SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_particle_asset_init (LrgParticleAsset *self)
{
	self->loop = TRUE;
	self->emitters = g_ptr_array_new_with_free_func (g_object_unref);
	self->unknown_keys = g_ptr_array_new_with_free_func (g_free);
}

LrgParticleAsset *
lrg_particle_asset_new (void)
{
	return g_object_new (LRG_TYPE_PARTICLE_ASSET, NULL);
}

const gchar *
lrg_particle_asset_get_name (LrgParticleAsset *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (self), NULL);
	return self->name;
}

void
lrg_particle_asset_set_name (LrgParticleAsset *self,
                             const gchar      *name)
{
	g_return_if_fail (LRG_IS_PARTICLE_ASSET (self));
	if (!g_set_str (&self->name, name))
		return;
	g_object_notify_by_pspec (G_OBJECT (self), asset_props[AS_PROP_NAME]);
	asset_changed (self);
}

guint64
lrg_particle_asset_get_seed (LrgParticleAsset *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (self), 0);
	return self->seed;
}

void
lrg_particle_asset_set_seed (LrgParticleAsset *self,
                             guint64           seed)
{
	g_return_if_fail (LRG_IS_PARTICLE_ASSET (self));
	if (self->seed == seed)
		return;
	self->seed = seed;
	g_object_notify_by_pspec (G_OBJECT (self), asset_props[AS_PROP_SEED]);
	asset_changed (self);
}

gfloat
lrg_particle_asset_get_duration (LrgParticleAsset *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (self), 0.0f);
	return self->duration;
}

void
lrg_particle_asset_set_duration (LrgParticleAsset *self,
                                 gfloat            duration)
{
	g_return_if_fail (LRG_IS_PARTICLE_ASSET (self));
	duration = MAX (duration, 0.0f);
	if (self->duration == duration)
		return;
	self->duration = duration;
	g_object_notify_by_pspec (G_OBJECT (self), asset_props[AS_PROP_DURATION]);
	asset_changed (self);
}

gboolean
lrg_particle_asset_get_loop (LrgParticleAsset *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (self), TRUE);
	return self->loop;
}

void
lrg_particle_asset_set_loop (LrgParticleAsset *self,
                             gboolean          loop)
{
	g_return_if_fail (LRG_IS_PARTICLE_ASSET (self));
	loop = !!loop;
	if (self->loop == loop)
		return;
	self->loop = loop;
	g_object_notify_by_pspec (G_OBJECT (self), asset_props[AS_PROP_LOOP]);
	asset_changed (self);
}

guint
lrg_particle_asset_get_n_emitters (LrgParticleAsset *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (self), 0);
	return self->emitters->len;
}

LrgParticleAssetEmitter *
lrg_particle_asset_get_emitter (LrgParticleAsset *self,
                                guint             index)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (self), NULL);
	if (index >= self->emitters->len)
		return NULL;
	return g_ptr_array_index (self->emitters, index);
}

LrgParticleAssetEmitter *
lrg_particle_asset_find_emitter (LrgParticleAsset *self,
                                 const gchar      *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (self), NULL);

	for (i = 0; name != NULL && i < self->emitters->len; i++)
	{
		LrgParticleAssetEmitter *e = g_ptr_array_index (self->emitters, i);

		if (g_strcmp0 (e->name, name) == 0)
			return e;
	}
	return NULL;
}

void
lrg_particle_asset_add_emitter (LrgParticleAsset        *self,
                                LrgParticleAssetEmitter *emitter)
{
	g_return_if_fail (LRG_IS_PARTICLE_ASSET (self));
	g_return_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (emitter));

	g_ptr_array_add (self->emitters, g_object_ref (emitter));
	g_signal_connect (emitter, "changed", G_CALLBACK (on_emitter_changed), self);
	asset_changed (self);
}

gboolean
lrg_particle_asset_remove_emitter (LrgParticleAsset *self,
                                   guint             index)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (self), FALSE);

	if (index >= self->emitters->len)
		return FALSE;
	g_signal_handlers_disconnect_by_data (g_ptr_array_index (self->emitters, index), self);
	g_ptr_array_remove_index (self->emitters, index);
	asset_changed (self);
	return TRUE;
}

gboolean
lrg_particle_asset_move_emitter (LrgParticleAsset *self,
                                 guint             from,
                                 guint             to)
{
	gpointer e;

	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (self), FALSE);

	if (from >= self->emitters->len || to >= self->emitters->len)
		return FALSE;
	if (from == to)
		return TRUE;
	e = g_ptr_array_steal_index (self->emitters, from);
	g_ptr_array_insert (self->emitters, (gint) to, e);
	asset_changed (self);
	return TRUE;
}

GStrv
lrg_particle_asset_get_unknown_keys (LrgParticleAsset *self)
{
	g_autoptr(GStrvBuilder) b = g_strv_builder_new ();
	guint                   i;

	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (self), NULL);

	for (i = 0; i < self->unknown_keys->len; i++)
		g_strv_builder_add (b, g_ptr_array_index (self->unknown_keys, i));
	return g_strv_builder_end (b);
}

gboolean
lrg_particle_asset_validate (LrgParticleAsset  *self,
                             GError           **error)
{
	guint i, j;

	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (self), FALSE);

	for (i = 0; i < self->emitters->len; i++)
	{
		LrgParticleAssetEmitter *e = g_ptr_array_index (self->emitters, i);

		if (e->name == NULL || *e->name == '\0')
		{
			g_set_error (error, LRG_PARTICLE_ASSET_ERROR, LRG_PARTICLE_ASSET_ERROR_REFERENCE,
			             "/emitters/%u/name: an emitter needs a name", i);
			return FALSE;
		}
		for (j = 0; j < i; j++)
			if (g_strcmp0 (((LrgParticleAssetEmitter *) g_ptr_array_index (self->emitters, j))->name,
			               e->name) == 0)
			{
				g_set_error (error, LRG_PARTICLE_ASSET_ERROR, LRG_PARTICLE_ASSET_ERROR_REFERENCE,
				             "/emitters/%u/name: '%s' is already the name of emitter %u", i, e->name, j);
				return FALSE;
			}
	}
	for (i = 0; i < self->emitters->len; i++)
	{
		LrgParticleAssetEmitter *e = g_ptr_array_index (self->emitters, i);

		for (j = 0; j < e->modules->len; j++)
		{
			LrgParticleModule *m = g_ptr_array_index (e->modules, j);
			g_autofree gchar  *target = NULL;

			if (g_strcmp0 (lrg_particle_module_get_kind (m), "sub-emitter") != 0)
				continue;
			g_object_get (m, "emitter", &target, NULL);
			if (target == NULL || lrg_particle_asset_find_emitter (self, target) == NULL)
			{
				g_set_error (error, LRG_PARTICLE_ASSET_ERROR, LRG_PARTICLE_ASSET_ERROR_REFERENCE,
				             "/emitters/%u/modules/%u/emitter: no emitter called '%s'", i, j,
				             target != NULL ? target : "");
				return FALSE;
			}
			if (g_strcmp0 (target, e->name) == 0)
			{
				g_set_error (error, LRG_PARTICLE_ASSET_ERROR, LRG_PARTICLE_ASSET_ERROR_REFERENCE,
				             "/emitters/%u/modules/%u/emitter: an emitter cannot be its own sub-emitter", i, j);
				return FALSE;
			}
		}
	}
	return TRUE;
}

/* ==========================================================================
 * 6. YAML in and out
 * ========================================================================== */

typedef struct
{
	const gchar *source;
	GError     **error;
	GPtrArray   *unknown;   /* gchar * key paths */
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
	g_autofree gchar *msg = NULL;
	va_list           args;

	va_start (args, format);
	msg = g_strdup_vprintf (format, args);
	va_end (args);
	g_set_error (ctx->error, LRG_PARTICLE_ASSET_ERROR, code, "%s: %s: %s", ctx->source,
	             (path != NULL && *path != '\0') ? path : "/", msg);
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
is_null_node (YamlNode *node)
{
	const gchar *s;

	if (node == NULL || yaml_node_is_null (node))
		return TRUE;
	s = scalar_of (node);
	return s != NULL && (*s == '\0' || g_strcmp0 (s, "~") == 0 || g_strcmp0 (s, "null") == 0);
}

static gboolean
parse_number (const gchar *s,
              gdouble     *out)
{
	gchar *end = NULL;

	if (s == NULL || *s == '\0')
		return FALSE;
	*out = g_ascii_strtod (s, &end);
	return end != s && *end == '\0' && isfinite (*out);
}

/* Reads a sequence of exactly @n numbers (or 1 .. @n when @min_n < @n) */
static gboolean
read_numbers (Ctx         *ctx,
              YamlNode    *node,
              const gchar *path,
              guint        min_n,
              guint        n,
              gdouble     *out,
              guint       *out_n)
{
	YamlSequence *seq;
	guint         len, i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path, "expected a list of %u numbers", n);
	seq = yaml_node_get_sequence (node);
	len = yaml_sequence_get_length (seq);
	if (len < min_n || len > n)
		return fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path, "expected a list of %u numbers, got %u items",
		             n, len);
	for (i = 0; i < len; i++)
		if (!parse_number (scalar_of (yaml_sequence_get_element (seq, i)), &out[i]))
			return fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path, "item %u is not a number", i);
	if (out_n != NULL)
		*out_n = len;
	return TRUE;
}

/* "#rgb", "#rrggbb", "#rrggbbaa" */
static gboolean
parse_hex_color (const gchar *s,
                 GrlColor    *c)
{
	guint8 v[4] = { 255, 255, 255, 255 };
	gsize  len, i;

	if (s == NULL || s[0] != '#')
		return FALSE;
	s++;
	len = strlen (s);
	if (len == 3)
	{
		for (i = 0; i < 3; i++)
		{
			gint x = g_ascii_xdigit_value (s[i]);

			if (x < 0)
				return FALSE;
			v[i] = (guint8) (x * 17);
		}
	}
	else if (len == 6 || len == 8)
	{
		for (i = 0; i < len / 2; i++)
		{
			gint hi = g_ascii_xdigit_value (s[i * 2]);
			gint lo = g_ascii_xdigit_value (s[i * 2 + 1]);

			if (hi < 0 || lo < 0)
				return FALSE;
			v[i] = (guint8) (hi * 16 + lo);
		}
	}
	else
		return FALSE;
	c->r = v[0];
	c->g = v[1];
	c->b = v[2];
	c->a = v[3];
	return TRUE;
}

/* Curve text: "t value [easing]; ..." (a bare number is a constant) */
static LrgKeyframeCurve *
parse_curve (const gchar  *text,
             gchar       **message)
{
	g_autoptr(LrgKeyframeCurve) curve = lrg_keyframe_curve_new ();
	g_auto(GStrv)               parts = NULL;
	gdouble                     constant;
	guint                       i;

	if (parse_number (text, &constant))
	{
		lrg_keyframe_curve_add_key (curve, 0.0f, (gfloat) constant, LRG_EASING_LINEAR);
		return g_steal_pointer (&curve);
	}
	parts = g_strsplit (text, ";", -1);
	for (i = 0; parts[i] != NULL; i++)
	{
		g_auto(GStrv) f = NULL;
		gchar        *piece = g_strstrip (parts[i]);
		gdouble       t, v;
		gint          easing = LRG_EASING_LINEAR;
		guint         n;

		if (*piece == '\0')
			continue;
		f = g_strsplit_set (piece, " \t", -1);
		/* drop empty fields from repeated blanks */
		n = 0;
		{
			guint r;

			for (r = 0; f[r] != NULL; r++)
			{
				if (*f[r] == '\0')
				{
					g_free (f[r]);
					continue;
				}
				f[n++] = f[r];
			}
			f[n] = NULL;
		}
		if (n < 2 || n > 3 || !parse_number (f[0], &t) || !parse_number (f[1], &v))
		{
			*message = g_strdup_printf ("key %u: expected \"time value [easing]\", got '%s'", i, piece);
			return NULL;
		}
		if (n == 3)
		{
			GEnumClass *ec = g_type_class_ref (LRG_TYPE_EASING_TYPE);
			GEnumValue *ev = g_enum_get_value_by_nick (ec, f[2]);

			if (ev == NULL)
			{
				*message = g_strdup_printf ("key %u: unknown easing '%s' (expected linear, ease-in-quad, "
				                            "ease-out-quad, ... ease-in-out-bounce)", i, f[2]);
				g_type_class_unref (ec);
				return NULL;
			}
			easing = ev->value;
			g_type_class_unref (ec);
		}
		lrg_keyframe_curve_add_key (curve, (gfloat) t, (gfloat) v, (LrgEasingType) easing);
	}
	if (lrg_keyframe_curve_get_key_count (curve) == 0)
	{
		*message = g_strdup ("a curve needs at least one key");
		return NULL;
	}
	return g_steal_pointer (&curve);
}

static gchar *
format_float (gfloat v)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	/* %.9g reads back to the same float */
	g_ascii_formatd (buf, sizeof buf, "%.9g", (gdouble) v);
	return g_strdup (buf);
}

static gchar *
curve_to_string (LrgKeyframeCurve *curve)
{
	GString *out = g_string_new (NULL);
	guint    i, n = lrg_keyframe_curve_get_key_count (curve);

	for (i = 0; i < n; i++)
	{
		gfloat            t = 0.0f, v = 0.0f;
		LrgEasingType     e = LRG_EASING_LINEAR;
		g_autofree gchar *ts = NULL;
		g_autofree gchar *vs = NULL;

		lrg_keyframe_curve_get_key (curve, i, &t, &v, &e);
		ts = format_float (t);
		vs = format_float (v);
		g_string_append_printf (out, "%s%s %s", i > 0 ? "; " : "", ts, vs);
		if (e != LRG_EASING_LINEAR)
		{
			GEnumClass *ec = g_type_class_ref (LRG_TYPE_EASING_TYPE);
			GEnumValue *ev = g_enum_get_value (ec, (gint) e);

			if (ev != NULL)
				g_string_append_printf (out, " %s", ev->value_nick);
			g_type_class_unref (ec);
		}
	}
	return g_string_free (out, FALSE);
}

/* The table entry of a built-in property, for range bounds */
static const ParamDesc *
find_desc (GObject     *object,
           const gchar *name)
{
	const ParamDesc *table = NULL;
	guint            n = 0, i;

	if (IS_BUILTIN (object))
	{
		table = BUILTIN (object)->desc->params;
		n = BUILTIN (object)->desc->n_params;
	}
	else if (LRG_IS_PARTICLE_RENDER_SETTINGS (object))
	{
		table = render_params;
		n = RS_N;
	}
	for (i = 0; i < n; i++)
		if (g_strcmp0 (table[i].name, name) == 0)
			return &table[i];
	return NULL;
}

/* Parses @node as the value of @pspec into @out (initialised here). */
static gboolean
read_value (Ctx         *ctx,
            GObject     *object,
            GParamSpec  *pspec,
            YamlNode    *node,
            const gchar *path,
            GValue      *out)
{
	GType        type = G_PARAM_SPEC_VALUE_TYPE (pspec);
	GType        fund = G_TYPE_FUNDAMENTAL (type);
	const gchar *s = scalar_of (node);
	gdouble      d = 0.0;

	g_value_init (out, type);

	/* nullable values: strings, colours, gradients, curves */
	if (is_null_node (node) && (fund == G_TYPE_STRING || type == GRL_TYPE_COLOR ||
	                            type == LRG_TYPE_GRADIENT_STOPS || type == LRG_TYPE_KEYFRAME_CURVE))
		return TRUE;

	if (type == G_TYPE_BOOLEAN)
	{
		if (g_strcmp0 (s, "true") == 0 || g_strcmp0 (s, "yes") == 0 || g_strcmp0 (s, "on") == 0)
			g_value_set_boolean (out, TRUE);
		else if (g_strcmp0 (s, "false") == 0 || g_strcmp0 (s, "no") == 0 || g_strcmp0 (s, "off") == 0)
			g_value_set_boolean (out, FALSE);
		else
			return fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path, "expected true or false");
		return TRUE;
	}
	if (type == G_TYPE_FLOAT || type == G_TYPE_DOUBLE)
	{
		gdouble lo, hi;

		if (!parse_number (s, &d))
			return fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path, "expected a number");
		lo = type == G_TYPE_FLOAT ? (gdouble) G_PARAM_SPEC_FLOAT (pspec)->minimum : G_PARAM_SPEC_DOUBLE (pspec)->minimum;
		hi = type == G_TYPE_FLOAT ? (gdouble) G_PARAM_SPEC_FLOAT (pspec)->maximum : G_PARAM_SPEC_DOUBLE (pspec)->maximum;
		if (d < lo || d > hi)
			return fail (ctx, LRG_PARTICLE_ASSET_ERROR_VALUE, path, "expected a number from %g to %g, got %s",
			             lo, hi, s);
		if (type == G_TYPE_FLOAT)
			g_value_set_float (out, (gfloat) d);
		else
			g_value_set_double (out, d);
		return TRUE;
	}
	if (type == G_TYPE_INT || type == G_TYPE_UINT || type == G_TYPE_UINT64 || type == G_TYPE_INT64)
	{
		gchar  *end = NULL;
		gint64  iv = 0;
		guint64 uv = 0;
		gboolean neg = s != NULL && s[0] == '-';

		if (s == NULL || *s == '\0')
			return fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path, "expected a whole number");
		if (type == G_TYPE_UINT || type == G_TYPE_UINT64)
		{
			if (neg)
				return fail (ctx, LRG_PARTICLE_ASSET_ERROR_VALUE, path, "expected a whole number of 0 or more");
			uv = g_ascii_strtoull (s, &end, 10);
		}
		else
			iv = g_ascii_strtoll (s, &end, 10);
		if (end == s || *end != '\0')
			return fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path, "expected a whole number, got '%s'", s);
		if (type == G_TYPE_INT)
		{
			GParamSpecInt *ps = G_PARAM_SPEC_INT (pspec);

			if (iv < ps->minimum || iv > ps->maximum)
				return fail (ctx, LRG_PARTICLE_ASSET_ERROR_VALUE, path, "expected a whole number from %d to %d",
				             ps->minimum, ps->maximum);
			g_value_set_int (out, (gint) iv);
		}
		else if (type == G_TYPE_UINT)
		{
			GParamSpecUInt *ps = G_PARAM_SPEC_UINT (pspec);

			if (uv < ps->minimum || uv > ps->maximum)
				return fail (ctx, LRG_PARTICLE_ASSET_ERROR_VALUE, path, "expected a whole number from %u to %u",
				             ps->minimum, ps->maximum);
			g_value_set_uint (out, (guint) uv);
		}
		else if (type == G_TYPE_UINT64)
			g_value_set_uint64 (out, uv);
		else
			g_value_set_int64 (out, iv);
		return TRUE;
	}
	if (fund == G_TYPE_ENUM)
	{
		GEnumClass *ec = G_PARAM_SPEC_ENUM (pspec)->enum_class;
		GEnumValue *ev = s != NULL ? g_enum_get_value_by_nick (ec, s) : NULL;

		if (ev == NULL)
		{
			g_autoptr(GString) nicks = g_string_new (NULL);
			guint              i;

			for (i = 0; i < ec->n_values; i++)
				g_string_append_printf (nicks, "%s%s", i > 0 ? ", " : "", ec->values[i].value_nick);
			return fail (ctx, LRG_PARTICLE_ASSET_ERROR_VALUE, path, "expected one of %s, got '%s'", nicks->str,
			             s != NULL ? s : "");
		}
		g_value_set_enum (out, ev->value);
		return TRUE;
	}
	if (fund == G_TYPE_STRING)
	{
		if (s == NULL)
			return fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path, "expected a string");
		g_value_set_string (out, s);
		return TRUE;
	}
	if (type == GRL_TYPE_VECTOR2)
	{
		const ParamDesc *pd = find_desc (object, pspec->name);
		gdouble          v[2];
		guint            n = 0;

		if (s != NULL)
		{
			if (!parse_number (s, &v[0]))
				return fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path, "expected a number or [min, max]");
			v[1] = v[0];
		}
		else if (!read_numbers (ctx, node, path, 2, 2, v, &n))
			return FALSE;
		if (v[0] > v[1])
			return fail (ctx, LRG_PARTICLE_ASSET_ERROR_VALUE, path, "the minimum %g is above the maximum %g",
			             v[0], v[1]);
		if (pd != NULL && (v[0] < pd->min || v[1] > pd->max))
			return fail (ctx, LRG_PARTICLE_ASSET_ERROR_VALUE, path, "expected values from %g to %g", pd->min,
			             pd->max);
		g_value_take_boxed (out, grl_vector2_new ((gfloat) v[0], (gfloat) v[1]));
		return TRUE;
	}
	if (type == GRL_TYPE_VECTOR3)
	{
		gdouble v[3];

		if (!read_numbers (ctx, node, path, 3, 3, v, NULL))
			return FALSE;
		g_value_take_boxed (out, grl_vector3_new ((gfloat) v[0], (gfloat) v[1], (gfloat) v[2]));
		return TRUE;
	}
	if (type == GRL_TYPE_COLOR)
	{
		GrlColor c;

		if (s != NULL)
		{
			if (!parse_hex_color (s, &c))
				return fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path,
				             "expected a #rrggbb or #rrggbbaa colour, got '%s'", s);
		}
		else
		{
			gdouble v[4] = { 1.0, 1.0, 1.0, 1.0 };
			guint   n = 0, i;

			if (!read_numbers (ctx, node, path, 3, 4, v, &n))
				return FALSE;
			for (i = 0; i < n; i++)
				if (v[i] < 0.0 || v[i] > 1.0)
					return fail (ctx, LRG_PARTICLE_ASSET_ERROR_VALUE, path, "colour channels go from 0 to 1");
			c.r = to_byte ((gfloat) v[0]);
			c.g = to_byte ((gfloat) v[1]);
			c.b = to_byte ((gfloat) v[2]);
			c.a = to_byte ((gfloat) v[3]);
		}
		g_value_take_boxed (out, grl_color_new (c.r, c.g, c.b, c.a));
		return TRUE;
	}
	if (type == LRG_TYPE_GRADIENT_STOPS)
	{
		g_autoptr(LrgColorGradient) g = NULL;
		g_autoptr(GError)           err = NULL;

		if (s == NULL)
			return fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path,
			             "expected a gradient: \"0 #rrggbbaa; 1 #rrggbbaa\"");
		g = lrg_color_gradient_new_from_string (s, &err);
		if (g == NULL)
			return fail (ctx, LRG_PARTICLE_ASSET_ERROR_VALUE, path, "%s", err->message);
		g_value_take_boxed (out, lrg_color_gradient_to_stops (g));
		return TRUE;
	}
	if (type == LRG_TYPE_KEYFRAME_CURVE)
	{
		g_autofree gchar *message = NULL;
		LrgKeyframeCurve *curve;

		if (s == NULL)
			return fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path,
			             "expected a curve: a number or \"time value [easing]; ...\"");
		curve = parse_curve (s, &message);
		if (curve == NULL)
			return fail (ctx, LRG_PARTICLE_ASSET_ERROR_VALUE, path, "%s", message);
		g_value_take_object (out, curve);
		return TRUE;
	}
	return fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path, "values of type %s cannot be read",
	             g_type_name (type));
}

/* Keeps an unknown member on @object for the writer, and records it */
static void
keep_unknown (Ctx         *ctx,
              GObject     *object,
              const gchar *key,
              YamlNode    *node,
              const gchar *path)
{
	YamlMapping *extra = g_object_get_data (object, EXTRA_KEY);

	if (extra == NULL)
	{
		extra = yaml_mapping_new ();
		g_object_set_data_full (object, EXTRA_KEY, extra, (GDestroyNotify) yaml_mapping_unref);
	}
	yaml_mapping_set_member (extra, key, node);
	if (ctx->unknown != NULL)
		g_ptr_array_add (ctx->unknown, g_strdup (path));
}

static gboolean
key_in (const gchar         *key,
        const gchar * const *keys)
{
	guint i;

	for (i = 0; keys != NULL && keys[i] != NULL; i++)
		if (g_strcmp0 (key, keys[i]) == 0)
			return TRUE;
	return FALSE;
}

/* Reads every member of @map into @object's writable properties; members
 * in @skip are the caller's; anything else is kept as unknown. */
static gboolean
read_props (Ctx                 *ctx,
            GObject             *object,
            YamlMapping         *map,
            const gchar         *path,
            const gchar * const *skip)
{
	guint i, n = yaml_mapping_get_size (map);

	for (i = 0; i < n; i++)
	{
		const gchar      *key = yaml_mapping_get_key (map, i);
		YamlNode         *node = yaml_mapping_get_value (map, i);
		g_autofree gchar *p = g_strdup_printf ("%s/%s", path, key);
		GParamSpec       *ps;
		g_auto(GValue)    v = G_VALUE_INIT;

		if (key_in (key, skip))
			continue;
		ps = g_object_class_find_property (G_OBJECT_GET_CLASS (object), key);
		if (ps == NULL || (ps->flags & G_PARAM_WRITABLE) == 0 || (ps->flags & G_PARAM_CONSTRUCT_ONLY) != 0)
		{
			keep_unknown (ctx, object, key, node, p);
			continue;
		}
		if (!read_value (ctx, object, ps, node, p, &v))
			return FALSE;
		g_object_set_property (object, key, &v);
	}
	return TRUE;
}

static void
set_take (YamlMapping *map,
          const gchar *key,
          YamlNode    *node)
{
	yaml_mapping_set_member (map, key, node);
	yaml_node_unref (node);
}

static YamlNode *
color_node (const GrlColor *c)
{
	g_autofree gchar *s = NULL;

	if (c->a == 255)
		s = g_strdup_printf ("#%02x%02x%02x", c->r, c->g, c->b);
	else
		s = g_strdup_printf ("#%02x%02x%02x%02x", c->r, c->g, c->b, c->a);
	return lrg_data_yaml_string (s);
}

static YamlNode *
floats_node (const gfloat *v,
             guint         n)
{
	YamlNode     *node = yaml_node_new_sequence (NULL);
	YamlSequence *seq = yaml_node_get_sequence (node);
	guint         i;

	for (i = 0; i < n; i++)
	{
		YamlNode *e = lrg_data_yaml_float (v[i]);

		yaml_sequence_add_element (seq, e);
		yaml_node_unref (e);
	}
	return node;
}

/* The YAML for a property value (NULL for an unset nullable value) */
static YamlNode *
value_node (const GValue *v)
{
	GType type = G_VALUE_TYPE (v);

	if (type == G_TYPE_BOOLEAN)
		return lrg_data_yaml_boolean (g_value_get_boolean (v));
	if (type == G_TYPE_FLOAT)
		return lrg_data_yaml_float (g_value_get_float (v));
	if (type == G_TYPE_DOUBLE)
		return lrg_data_yaml_double (g_value_get_double (v));
	if (type == G_TYPE_INT)
		return lrg_data_yaml_int (g_value_get_int (v));
	if (type == G_TYPE_UINT)
		return lrg_data_yaml_int ((gint64) g_value_get_uint (v));
	if (type == G_TYPE_INT64)
		return lrg_data_yaml_int (g_value_get_int64 (v));
	if (type == G_TYPE_UINT64)
	{
		/* above G_MAXINT64 as text: lrg_data_yaml_int is signed */
		gchar buf[32];

		g_snprintf (buf, sizeof buf, "%" G_GUINT64_FORMAT, g_value_get_uint64 (v));
		return yaml_node_new_scalar (buf, YAML_SCALAR_STYLE_PLAIN);
	}
	if (G_TYPE_IS_ENUM (type))
	{
		GEnumClass *ec = g_type_class_ref (type);
		GEnumValue *ev = g_enum_get_value (ec, g_value_get_enum (v));
		YamlNode   *node = lrg_data_yaml_string (ev != NULL ? ev->value_nick : "");

		g_type_class_unref (ec);
		return node;
	}
	if (type == G_TYPE_STRING)
		return g_value_get_string (v) != NULL ? lrg_data_yaml_string (g_value_get_string (v)) : NULL;
	if (type == GRL_TYPE_VECTOR2)
	{
		const GrlVector2 *r = g_value_get_boxed (v);
		gfloat            f[2];

		if (r == NULL)
			return NULL;
		if (r->x == r->y)
			return lrg_data_yaml_float (r->x);
		f[0] = r->x;
		f[1] = r->y;
		return floats_node (f, 2);
	}
	if (type == GRL_TYPE_VECTOR3)
	{
		const GrlVector3 *r = g_value_get_boxed (v);
		gfloat            f[3];

		if (r == NULL)
			return NULL;
		f[0] = r->x;
		f[1] = r->y;
		f[2] = r->z;
		return floats_node (f, 3);
	}
	if (type == GRL_TYPE_COLOR)
		return g_value_get_boxed (v) != NULL ? color_node (g_value_get_boxed (v)) : NULL;
	if (type == LRG_TYPE_GRADIENT_STOPS)
	{
		const LrgGradientStops *stops = g_value_get_boxed (v);
		g_autoptr(LrgColorGradient) g = NULL;
		g_autofree gchar *s = NULL;

		if (stops == NULL)
			return NULL;
		g = lrg_color_gradient_new_from_stops (stops);
		s = lrg_color_gradient_to_string (g);
		return lrg_data_yaml_string (s);
	}
	if (type == LRG_TYPE_KEYFRAME_CURVE)
	{
		LrgKeyframeCurve *c = g_value_get_object (v);
		g_autofree gchar *s = NULL;

		if (c == NULL)
			return NULL;
		s = curve_to_string (c);
		return lrg_data_yaml_string (s);
	}
	return NULL;
}

static gint
compare_pspec_names (gconstpointer a,
                     gconstpointer b)
{
	const GParamSpec *pa = *(GParamSpec * const *) a;
	const GParamSpec *pb = *(GParamSpec * const *) b;

	return strcmp (pa->name, pb->name);
}

/* The writable properties of @object in file order: table order for the
 * built-ins and the render settings, by name otherwise ("enabled" of the
 * module base class is handled by the caller). */
static GPtrArray *
ordered_pspecs (GObject *object)
{
	GPtrArray *out = g_ptr_array_new ();
	GObjectClass *klass = G_OBJECT_GET_CLASS (object);

	if (IS_BUILTIN (object))
	{
		const KindDesc *d = BUILTIN (object)->desc;
		guint           i;

		for (i = 0; i < d->n_params; i++)
			g_ptr_array_add (out, g_object_class_find_property (klass, d->params[i].name));
	}
	else if (LRG_IS_PARTICLE_RENDER_SETTINGS (object))
	{
		guint i;

		for (i = 0; i < RS_N; i++)
			g_ptr_array_add (out, render_pspecs[i]);
	}
	else
	{
		g_autofree GParamSpec **all = NULL;
		guint                   n, i;

		all = g_object_class_list_properties (klass, &n);
		for (i = 0; i < n; i++)
		{
			GParamSpec *ps = all[i];

			if ((ps->flags & G_PARAM_READWRITE) != G_PARAM_READWRITE || (ps->flags & G_PARAM_CONSTRUCT_ONLY) != 0 ||
			    g_strcmp0 (ps->name, "enabled") == 0)
				continue;
			g_ptr_array_add (out, ps);
		}
		g_ptr_array_sort (out, compare_pspec_names);
	}
	return out;
}

/* A fresh instance of @object's type, for "is this the default" checks */
static GObject *
pristine_for (GHashTable *cache,
              GObject    *object)
{
	GType    type = G_OBJECT_TYPE (object);
	GObject *p = g_hash_table_lookup (cache, GSIZE_TO_POINTER (type));

	if (p == NULL)
	{
		p = g_object_new (type, NULL);
		g_hash_table_insert (cache, GSIZE_TO_POINTER (type), p);
	}
	return p;
}

/* Writes @object's non-default properties into @map, in file order */
static void
write_props (GHashTable  *cache,
             GObject     *object,
             YamlMapping *map)
{
	g_autoptr(GPtrArray) pspecs = ordered_pspecs (object);
	GObject             *pristine = pristine_for (cache, object);
	guint                i;

	for (i = 0; i < pspecs->len; i++)
	{
		GParamSpec    *ps = g_ptr_array_index (pspecs, i);
		g_auto(GValue) v = G_VALUE_INIT;
		g_auto(GValue) d = G_VALUE_INIT;
		YamlNode      *node;

		if (ps == NULL)
			continue;
		g_value_init (&v, G_PARAM_SPEC_VALUE_TYPE (ps));
		g_value_init (&d, G_PARAM_SPEC_VALUE_TYPE (ps));
		g_object_get_property (object, ps->name, &v);
		g_object_get_property (pristine, ps->name, &d);
		if (lrg_property_values_equal (&v, &d))
			continue;
		node = value_node (&v);
		if (node != NULL)
			set_take (map, ps->name, node);
	}
}

/* Appends the unknown members kept on @object at load */
static void
write_extra (GObject     *object,
             YamlMapping *map)
{
	YamlMapping *extra = g_object_get_data (object, EXTRA_KEY);
	guint        i, n;

	if (extra == NULL)
		return;
	n = yaml_mapping_get_size (extra);
	for (i = 0; i < n; i++)
	{
		const gchar *key = yaml_mapping_get_key (extra, i);

		if (!yaml_mapping_has_member (map, key))
			yaml_mapping_set_member (map, key, yaml_mapping_get_value (extra, i));
	}
}

static YamlNode *
module_to_node (GHashTable        *cache,
                LrgParticleModule *module,
                GError           **error)
{
	YamlMapping *map;
	YamlNode    *node;
	const gchar *kind = lrg_particle_module_get_kind (module);

	if (kind == NULL)
	{
		g_set_error (error, LRG_PARTICLE_ASSET_ERROR, LRG_PARTICLE_ASSET_ERROR_UNKNOWN_MODULE,
		             "module type %s is not registered (lrg_particle_module_register_kind())",
		             G_OBJECT_TYPE_NAME (module));
		return NULL;
	}
	map = yaml_mapping_new ();
	set_take (map, "type", lrg_data_yaml_string (kind));
	write_props (cache, G_OBJECT (module), map);
	if (!lrg_particle_module_get_enabled (module))
		set_take (map, "enabled", lrg_data_yaml_boolean (FALSE));
	write_extra (G_OBJECT (module), map);
	node = yaml_node_new_mapping (map);
	yaml_mapping_unref (map);
	return node;
}

static YamlNode *
emitter_to_node (GHashTable              *cache,
                 LrgParticleAssetEmitter *e,
                 GError                 **error)
{
	YamlMapping *map = yaml_mapping_new ();
	YamlMapping *rmap;
	YamlNode    *node;
	guint        i;

	set_take (map, "name", lrg_data_yaml_string (e->name));
	if (e->max_particles != 1000)
		set_take (map, "max-particles", lrg_data_yaml_int (e->max_particles));
	if (!e->enabled)
		set_take (map, "enabled", lrg_data_yaml_boolean (FALSE));
	if (e->start_delay != 0.0f)
		set_take (map, "start-delay", lrg_data_yaml_float (e->start_delay));

	/* renderer: only when something differs from the defaults */
	rmap = yaml_mapping_new ();
	write_props (cache, G_OBJECT (e->renderer), rmap);
	write_extra (G_OBJECT (e->renderer), rmap);
	if (yaml_mapping_get_size (rmap) > 0)
	{
		node = yaml_node_new_mapping (rmap);
		set_take (map, "renderer", node);
	}
	yaml_mapping_unref (rmap);

	/* modules, in stack order */
	{
		YamlNode     *seq_node = yaml_node_new_sequence (NULL);
		YamlSequence *seq = yaml_node_get_sequence (seq_node);

		for (i = 0; i < e->modules->len; i++)
		{
			YamlNode *m = module_to_node (cache, g_ptr_array_index (e->modules, i), error);

			if (m == NULL)
			{
				yaml_node_unref (seq_node);
				yaml_mapping_unref (map);
				return NULL;
			}
			yaml_sequence_add_element (seq, m);
			yaml_node_unref (m);
		}
		set_take (map, "modules", seq_node);
	}
	write_extra (G_OBJECT (e), map);
	node = yaml_node_new_mapping (map);
	yaml_mapping_unref (map);
	return node;
}

static YamlNode *
asset_to_node (LrgParticleAsset  *self,
               GError           **error)
{
	g_autoptr(GHashTable) cache = g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);
	YamlMapping          *map = yaml_mapping_new ();
	YamlNode             *seq_node;
	YamlSequence         *seq;
	YamlNode             *node;
	guint                 i;

	set_take (map, "version", lrg_data_yaml_int (LRG_PARTICLE_ASSET_FORMAT_VERSION));
	if (self->name != NULL)
		set_take (map, "name", lrg_data_yaml_string (self->name));
	if (self->seed != 0)
	{
		g_auto(GValue) v = G_VALUE_INIT;

		g_value_init (&v, G_TYPE_UINT64);
		g_value_set_uint64 (&v, self->seed);
		set_take (map, "seed", value_node (&v));
	}
	if (self->duration != 0.0f)
		set_take (map, "duration", lrg_data_yaml_float (self->duration));
	if (!self->loop)
		set_take (map, "loop", lrg_data_yaml_boolean (FALSE));
	if (self->prewarm)
		set_take (map, "prewarm", lrg_data_yaml_boolean (TRUE));
	if (self->local_space)
		set_take (map, "local-space", lrg_data_yaml_boolean (TRUE));

	seq_node = yaml_node_new_sequence (NULL);
	seq = yaml_node_get_sequence (seq_node);
	for (i = 0; i < self->emitters->len; i++)
	{
		YamlNode *e = emitter_to_node (cache, g_ptr_array_index (self->emitters, i), error);

		if (e == NULL)
		{
			yaml_node_unref (seq_node);
			yaml_mapping_unref (map);
			return NULL;
		}
		yaml_sequence_add_element (seq, e);
		yaml_node_unref (e);
	}
	set_take (map, "emitters", seq_node);
	write_extra (G_OBJECT (self), map);
	node = yaml_node_new_mapping (map);
	yaml_mapping_unref (map);
	return node;
}

gchar *
lrg_particle_asset_save_to_data (LrgParticleAsset  *self,
                                 gsize             *length,
                                 GError           **error)
{
	YamlNode *root;
	gchar    *text;

	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	root = asset_to_node (self, error);
	if (root == NULL)
		return NULL;
	text = lrg_data_yaml_to_data (root, length, error);
	yaml_node_unref (root);
	return text;
}

gboolean
lrg_particle_asset_save_to_file (LrgParticleAsset  *self,
                                 const gchar       *path,
                                 GError           **error)
{
	g_autofree gchar *text = NULL;
	gsize             length = 0;

	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = lrg_particle_asset_save_to_data (self, &length, error);
	if (text == NULL)
		return FALSE;
	return g_file_set_contents (path, text, (gssize) length, error);
}

static LrgParticleModule *
read_module (Ctx         *ctx,
             YamlNode    *node,
             const gchar *path)
{
	static const gchar * const skip[] = { "type", NULL };
	g_autoptr(LrgParticleModule) m = NULL;
	YamlMapping                 *map;
	const gchar                 *kind;
	g_autofree gchar            *tp = g_strdup_printf ("%s/type", path);

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path, "expected a module mapping with a 'type'");
		return NULL;
	}
	map = yaml_node_get_mapping (node);
	kind = scalar_of (yaml_mapping_get_member (map, "type"));
	if (kind == NULL || *kind == '\0')
	{
		fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, tp, "missing module type");
		return NULL;
	}
	m = lrg_particle_module_new (kind);
	if (m == NULL)
	{
		g_auto(GStrv)     kinds = lrg_particle_module_list_kinds ();
		g_autofree gchar *list = g_strjoinv (", ", kinds);

		fail (ctx, LRG_PARTICLE_ASSET_ERROR_UNKNOWN_MODULE, tp, "unknown module '%s' (known: %s)", kind, list);
		return NULL;
	}
	if (!read_props (ctx, G_OBJECT (m), map, path, skip))
		return NULL;
	return g_steal_pointer (&m);
}

static LrgParticleAssetEmitter *
read_emitter (Ctx         *ctx,
              YamlNode    *node,
              const gchar *path)
{
	static const gchar * const skip[] = { "renderer", "modules", NULL };
	g_autoptr(LrgParticleAssetEmitter) e = NULL;
	YamlMapping                       *map;
	YamlNode                          *member;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path, "expected an emitter mapping");
		return NULL;
	}
	map = yaml_node_get_mapping (node);
	{
		const gchar *name = scalar_of (yaml_mapping_get_member (map, "name"));

		if (name == NULL || *name == '\0')
		{
			fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, path, "an emitter needs a non-empty 'name'");
			return NULL;
		}
	}
	e = lrg_particle_asset_emitter_new (NULL);
	if (!read_props (ctx, G_OBJECT (e), map, path, skip))
		return NULL;

	member = yaml_mapping_get_member (map, "renderer");
	if (member != NULL && !is_null_node (member))
	{
		g_autofree gchar *rp = g_strdup_printf ("%s/renderer", path);

		if (yaml_node_get_node_type (member) != YAML_NODE_MAPPING)
		{
			fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, rp, "expected a mapping");
			return NULL;
		}
		if (!read_props (ctx, G_OBJECT (e->renderer), yaml_node_get_mapping (member), rp, NULL))
			return NULL;
	}

	member = yaml_mapping_get_member (map, "modules");
	if (member != NULL && !is_null_node (member))
	{
		g_autofree gchar *mp = g_strdup_printf ("%s/modules", path);
		YamlSequence     *seq;
		guint             i, n;

		if (yaml_node_get_node_type (member) != YAML_NODE_SEQUENCE)
		{
			fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, mp, "expected a list of modules");
			return NULL;
		}
		seq = yaml_node_get_sequence (member);
		n = yaml_sequence_get_length (seq);
		for (i = 0; i < n; i++)
		{
			g_autofree gchar            *ip = g_strdup_printf ("%s/%u", mp, i);
			g_autoptr(LrgParticleModule) m = read_module (ctx, yaml_sequence_get_element (seq, i), ip);

			if (m == NULL)
				return NULL;
			lrg_particle_asset_emitter_insert_module (e, m, -1);
		}
	}
	return g_steal_pointer (&e);
}

static LrgParticleAsset *
read_asset (Ctx      *ctx,
            YamlNode *root)
{
	static const gchar * const skip[] = { "version", "emitters", NULL };
	g_autoptr(LrgParticleAsset) self = NULL;
	YamlMapping                *map;
	YamlNode                   *member;
	YamlSequence               *seq;
	guint                       i, n;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, "/", "a particle effect must be a YAML mapping");
		return NULL;
	}
	map = yaml_node_get_mapping (root);

	member = yaml_mapping_get_member (map, "version");
	if (member != NULL)
	{
		gdouble v;

		if (!parse_number (scalar_of (member), &v) || v != floor (v) || v < 1)
		{
			fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, "/version", "expected a whole number of 1 or more");
			return NULL;
		}
		if (v > LRG_PARTICLE_ASSET_FORMAT_VERSION)
		{
			fail (ctx, LRG_PARTICLE_ASSET_ERROR_VERSION, "/version",
			      "version %g is newer than this build reads (%d)", v, LRG_PARTICLE_ASSET_FORMAT_VERSION);
			return NULL;
		}
	}

	member = yaml_mapping_get_member (map, "emitters");
	if (member == NULL)
	{
		fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, "/", "not a particle effect: no 'emitters' list");
		return NULL;
	}

	self = lrg_particle_asset_new ();
	if (!read_props (ctx, G_OBJECT (self), map, "", skip))
		return NULL;

	if (!is_null_node (member))
	{
		if (yaml_node_get_node_type (member) != YAML_NODE_SEQUENCE)
		{
			fail (ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, "/emitters", "expected a list of emitters");
			return NULL;
		}
		seq = yaml_node_get_sequence (member);
		n = yaml_sequence_get_length (seq);
		for (i = 0; i < n; i++)
		{
			g_autofree gchar                  *ep = g_strdup_printf ("/emitters/%u", i);
			g_autoptr(LrgParticleAssetEmitter) e = read_emitter (ctx, yaml_sequence_get_element (seq, i), ep);

			if (e == NULL)
				return NULL;
			lrg_particle_asset_add_emitter (self, e);
		}
	}
	return g_steal_pointer (&self);
}

LrgParticleAsset *
lrg_particle_asset_new_from_data (const gchar  *data,
                                  gssize        length,
                                  const gchar  *source,
                                  GError      **error)
{
	g_autoptr(YamlParser)       parser = NULL;
	g_autoptr(GError)           local = NULL;
	g_autoptr(LrgParticleAsset) self = NULL;
	g_autoptr(GPtrArray)        unknown = g_ptr_array_new_with_free_func (g_free);
	Ctx                         ctx;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	ctx.source = source != NULL ? source : "<data>";
	ctx.error = error;
	ctx.unknown = unknown;

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		fail (&ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, "/", "not YAML: %s", local->message);
		return NULL;
	}
	if (yaml_parser_get_n_documents (parser) != 1)
	{
		fail (&ctx, LRG_PARTICLE_ASSET_ERROR_PARSE, "/", "expected exactly one YAML document");
		return NULL;
	}
	self = read_asset (&ctx, yaml_parser_get_root (parser));
	if (self == NULL)
		return NULL;

	/* cross-emitter checks, with the source in the message */
	if (!lrg_particle_asset_validate (self, &local))
	{
		g_set_error (error, LRG_PARTICLE_ASSET_ERROR, local->code, "%s: %s", ctx.source, local->message);
		return NULL;
	}
	g_ptr_array_extend_and_steal (self->unknown_keys, g_steal_pointer (&unknown));
	return g_steal_pointer (&self);
}

LrgParticleAsset *
lrg_particle_asset_new_from_file (const gchar  *path,
                                  GError      **error)
{
	g_autofree gchar *text = NULL;
	g_autofree gchar *base = NULL;
	gsize             length = 0;
	GError           *local = NULL;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents (path, &text, &length, &local))
	{
		g_set_error (error, LRG_PARTICLE_ASSET_ERROR, LRG_PARTICLE_ASSET_ERROR_IO, "%s", local->message);
		g_error_free (local);
		return NULL;
	}
	base = g_path_get_basename (path);
	return lrg_particle_asset_new_from_data (text, (gssize) length, base, error);
}

/* ==========================================================================
 * 7. LrgParticleEffect
 * ========================================================================== */

typedef struct
{
	LrgParticleSystem       *system;
	LrgParticleAssetEmitter *emitter;     /* ref */
	LrgParticleRendererKind  kind;
	GObject                 *renderer;    /* nullable */
	gboolean                 enabled;
	gchar                   *texture_ref; /* what `texture` was resolved from */
	gboolean                 texture_done;
	gchar                   *mesh_ref;
	gboolean                 mesh_done;
} Entry;

struct _LrgParticleEffect
{
	GObject                parent_instance;

	LrgParticleAsset      *asset;
	gulong                 changed_id;
	GPtrArray             *entries;       /* Entry * */
	gboolean               playing;
	gboolean               stopped;
	gboolean               finished;
	gfloat                 time;
	gboolean               seed_override;
	guint64                seed;
	gfloat                 position[3];
	gfloat                 rotation[4];
	LrgAssetManager       *manager;
	LrgParticleRaycastFunc raycast;
	gpointer               raycast_data;
	GDestroyNotify         raycast_destroy;
	gboolean               auto_reload;
};

G_DEFINE_FINAL_TYPE (LrgParticleEffect, lrg_particle_effect, G_TYPE_OBJECT)

enum
{
	FX_PROP_0,
	FX_PROP_ASSET,
	FX_PROP_AUTO_RELOAD,
	FX_N_PROPS
};

enum
{
	FX_SIGNAL_FINISHED,
	FX_N_SIGNALS
};

static GParamSpec *effect_props[FX_N_PROPS];
static guint       effect_signals[FX_N_SIGNALS];

static void
entry_free (gpointer data)
{
	Entry *e = data;

	g_clear_object (&e->system);
	g_clear_object (&e->emitter);
	g_clear_object (&e->renderer);
	g_free (e->texture_ref);
	g_free (e->mesh_ref);
	g_free (e);
}

/* splitmix64: decorrelated per-emitter seeds from one effect seed */
static guint64
mix_seed (guint64 seed,
          guint   index)
{
	guint64 z = seed + G_GUINT64_CONSTANT (0x9e3779b97f4a7c15) * (guint64) (index + 1);

	z = (z ^ (z >> 30)) * G_GUINT64_CONSTANT (0xbf58476d1ce4e5b9);
	z = (z ^ (z >> 27)) * G_GUINT64_CONSTANT (0x94d049bb133111eb);
	return z ^ (z >> 31);
}

static guint64
effect_seed (LrgParticleEffect *self)
{
	return self->seed_override ? self->seed : self->asset->seed;
}

/* Sub-emitters find sibling systems through this */
static LrgParticleSystem *
effect_lookup (const gchar *name,
               gpointer     user_data)
{
	return lrg_particle_effect_find_system (LRG_PARTICLE_EFFECT (user_data), name);
}

/* Fills a shape module's mesh surface from its `mesh` asset when it can */
static void
resolve_shape_mesh (LrgParticleEffect *self,
                    LrgParticleModule *m)
{
	g_autofree gchar *mesh = NULL;
	GrlModel         *model;
	Model            *rl;
	Mesh             *rm;
	g_autofree guint *idx = NULL;
	gint              i;

	if (!IS_BUILTIN (m) || BUILTIN (m)->desc->init_particle != shape_init ||
	    MI (BUILTIN (m), SHAPE_SHAPE) != LRG_PARTICLE_SHAPE_MESH)
		return;
	g_object_get (m, "mesh", &mesh, NULL);
	if (mesh == NULL || self->manager == NULL || !IsWindowReady ())
		return;
	model = lrg_asset_manager_load_model (self->manager, mesh, NULL);
	if (model == NULL)
		return;
	rl = (Model *) grl_model_get_handle (model);
	if (rl == NULL || rl->meshCount < 1 || rl->meshes[0].vertices == NULL)
		return;
	rm = &rl->meshes[0];
	if (rm->indices != NULL)
	{
		idx = g_new (guint, (gsize) rm->triangleCount * 3);
		for (i = 0; i < rm->triangleCount * 3; i++)
			idx[i] = rm->indices[i];
	}
	lrg_particle_module_set_surface (m, rm->vertices, (guint) rm->vertexCount * 3, idx,
	                                 idx != NULL ? (guint) rm->triangleCount * 3 : 0);
}

/* Copies run state of an old module into its replacement (same kind at
 * the same index), so a live edit does not re-fire past bursts */
static void
carry_state (LrgParticleModule *from,
             LrgParticleModule *to)
{
	if (from == NULL || to == NULL || !IS_BUILTIN (from) || !IS_BUILTIN (to) ||
	    BUILTIN (from)->desc != BUILTIN (to)->desc)
		return;
	BUILTIN (to)->acc = BUILTIN (from)->acc;
	BUILTIN (to)->state_cycle = BUILTIN (from)->state_cycle;
	BUILTIN (to)->state_next = BUILTIN (from)->state_next;
}

/* A shape module's explicit surface (lrg_particle_module_set_surface())
 * travels with its copies */
static void
copy_surface (LrgParticleModule *from,
              LrgParticleModule *to)
{
	if (!IS_BUILTIN (from) || !IS_BUILTIN (to) || BUILTIN (from)->tri == NULL)
		return;
	g_clear_pointer (&BUILTIN (to)->tri, g_array_unref);
	g_clear_pointer (&BUILTIN (to)->cdf, g_array_unref);
	BUILTIN (to)->tri = g_array_ref (BUILTIN (from)->tri);
	BUILTIN (to)->cdf = g_array_ref (BUILTIN (from)->cdf);
}

/* (Re)fills @e's module stack with copies of its emitter's modules */
static void
entry_sync_modules (LrgParticleEffect *self,
                    Entry             *e,
                    gboolean           keep_state)
{
	g_autoptr(GPtrArray) old = g_ptr_array_new_with_free_func (g_object_unref);
	guint                i;

	for (i = 0; i < lrg_particle_system_get_n_modules (e->system); i++)
		g_ptr_array_add (old, g_object_ref (lrg_particle_system_get_module (e->system, i)));
	lrg_particle_system_clear_modules (e->system);
	for (i = 0; i < e->emitter->modules->len; i++)
	{
		g_autoptr(LrgParticleModule) copy = lrg_particle_module_copy (g_ptr_array_index (e->emitter->modules, i));

		if (keep_state && i < old->len)
			carry_state (g_ptr_array_index (old, i), copy);
		copy_surface (g_ptr_array_index (e->emitter->modules, i), copy);
		resolve_shape_mesh (self, copy);
		lrg_particle_system_add_module (e->system, copy);
	}
}

/* Applies the render settings to the system and the renderer, making a
 * renderer of the right kind when it changed */
static void
entry_sync_renderer (Entry *e)
{
	LrgParticleRenderSettings *rs = e->emitter->renderer;
	LrgParticleRendererKind    kind = (LrgParticleRendererKind) rs->v[RS_KIND].i;

	lrg_particle_system_set_blend_mode (e->system, (LrgParticleBlendMode) rs->v[RS_BLEND].i);
	lrg_particle_system_set_layer (e->system, (guint) rs->v[RS_LAYER].i);

	if (kind != e->kind || e->renderer == NULL)
	{
		g_clear_object (&e->renderer);
		e->kind = kind;
		e->texture_done = FALSE;
		e->mesh_done = FALSE;
		switch (kind)
		{
		case LRG_PARTICLE_RENDERER_BILLBOARD:
			e->renderer = G_OBJECT (lrg_particle_billboard_renderer_new ());
			lrg_particle_system_set_render_mode (e->system, LRG_PARTICLE_RENDER_BILLBOARD);
			break;
		case LRG_PARTICLE_RENDERER_RIBBON:
			e->renderer = G_OBJECT (lrg_particle_ribbon_renderer_new ());
			lrg_particle_system_set_render_mode (e->system, LRG_PARTICLE_RENDER_TRAIL);
			break;
		case LRG_PARTICLE_RENDERER_MESH:
			e->renderer = G_OBJECT (lrg_particle_mesh_renderer_new ());
			lrg_particle_system_set_render_mode (e->system, LRG_PARTICLE_RENDER_MESH);
			break;
		default:
			break;
		}
	}

	/* resolve the texture / mesh again when the reference changed */
	if (g_strcmp0 (e->texture_ref, rs->v[RS_TEXTURE].s) != 0)
		e->texture_done = FALSE;
	if (g_strcmp0 (e->mesh_ref, rs->v[RS_MESH].s) != 0)
		e->mesh_done = FALSE;

	if (LRG_IS_PARTICLE_BILLBOARD_RENDERER (e->renderer))
		lrg_particle_billboard_renderer_set_frames (LRG_PARTICLE_BILLBOARD_RENDERER (e->renderer),
		                                            (guint) rs->v[RS_COLUMNS].i, (guint) rs->v[RS_ROWS].i,
		                                            rs->v[RS_FPS].f[0]);
	else if (LRG_IS_PARTICLE_RIBBON_RENDERER (e->renderer))
	{
		LrgParticleRibbonRenderer *r = LRG_PARTICLE_RIBBON_RENDERER (e->renderer);
		g_autoptr(LrgKeyframeCurve) curve = NULL;
		g_autoptr(LrgColorGradient) grad = NULL;

		if (lrg_particle_ribbon_renderer_get_mode (r) != (LrgParticleRibbonMode) rs->v[RS_RIBBON_MODE].i)
			lrg_particle_ribbon_renderer_set_mode (r, (LrgParticleRibbonMode) rs->v[RS_RIBBON_MODE].i);
		lrg_particle_ribbon_renderer_set_width (r, rs->v[RS_WIDTH].f[0]);
		if (rs->v[RS_WIDTH_CURVE].curve != NULL)
			curve = lrg_keyframe_curve_copy (rs->v[RS_WIDTH_CURVE].curve);
		lrg_particle_ribbon_renderer_set_width_curve (r, curve);
		if (rs->v[RS_COLOR_GRADIENT].gradient != NULL)
			grad = lrg_color_gradient_copy (rs->v[RS_COLOR_GRADIENT].gradient);
		lrg_particle_ribbon_renderer_set_color_gradient (r, grad);
		lrg_particle_ribbon_renderer_set_uv_mode (r, (LrgParticleRibbonUvMode) rs->v[RS_UV_MODE].i);
		lrg_particle_ribbon_renderer_set_uv_tile_length (r, rs->v[RS_UV_TILE_LENGTH].f[0]);
		lrg_particle_ribbon_renderer_set_max_points (r, (guint) rs->v[RS_MAX_POINTS].i);
		lrg_particle_ribbon_renderer_set_min_distance (r, rs->v[RS_MIN_DISTANCE].f[0]);
		lrg_particle_ribbon_renderer_set_trail_lifetime (r, rs->v[RS_TRAIL_LIFETIME].f[0]);
	}
	else if (LRG_IS_PARTICLE_MESH_RENDERER (e->renderer))
	{
		LrgParticleMeshRenderer *r = LRG_PARTICLE_MESH_RENDERER (e->renderer);
		const gfloat            *s = rs->v[RS_MESH_SCALE].f;

		lrg_particle_mesh_renderer_set_primitive (r, (LrgParticleMeshPrimitive) rs->v[RS_PRIMITIVE].i);
		lrg_particle_mesh_renderer_set_scale (r, s[0], s[1], s[2]);
		lrg_particle_mesh_renderer_set_align (r, (LrgParticleMeshAlign) rs->v[RS_ALIGN].i);
		lrg_particle_mesh_renderer_set_lit (r, rs->v[RS_LIT].i != 0);
	}
}

/* Effect-wide settings on one system */
static void
entry_sync_system (LrgParticleEffect *self,
                   Entry             *e)
{
	lrg_particle_system_set_start_delay (e->system, e->emitter->start_delay);
	lrg_particle_system_set_duration (e->system, self->asset->duration);
	lrg_particle_system_set_loop (e->system, self->asset->loop);
	lrg_particle_system_set_local_space (e->system, self->asset->local_space);
	e->enabled = e->emitter->enabled;
}

static Entry *
entry_new (LrgParticleEffect       *self,
           LrgParticleAssetEmitter *emitter,
           guint                    index)
{
	Entry *e = g_new0 (Entry, 1);

	e->emitter = g_object_ref (emitter);
	e->system = lrg_particle_system_new (emitter->max_particles);
	e->kind = LRG_PARTICLE_RENDERER_NONE;
	lrg_particle_system_set_name (e->system, emitter->name);
	lrg_particle_system_set_seed (e->system, mix_seed (effect_seed (self), index));
	lrg_particle_system_set_position (e->system, self->position[0], self->position[1], self->position[2]);
	lrg_particle_system_set_rotation (e->system, self->rotation);
	lrg_particle_system_set_lookup_func (e->system, effect_lookup, self, NULL);
	if (self->raycast != NULL)
		lrg_particle_system_set_raycast_func (e->system, self->raycast, self->raycast_data, NULL);
	entry_sync_system (self, e);
	entry_sync_modules (self, e, FALSE);
	entry_sync_renderer (e);
	return e;
}

static void
rebuild (LrgParticleEffect *self)
{
	guint i;

	g_ptr_array_set_size (self->entries, 0);
	for (i = 0; i < self->asset->emitters->len; i++)
		g_ptr_array_add (self->entries, entry_new (self, g_ptr_array_index (self->asset->emitters, i), i));
}

static void
on_asset_changed (LrgParticleAsset *asset,
                  gpointer          user_data)
{
	LrgParticleEffect *self = LRG_PARTICLE_EFFECT (user_data);

	(void) asset;
	if (self->auto_reload)
		lrg_particle_effect_reload (self);
}

static void
lrg_particle_effect_dispose (GObject *object)
{
	LrgParticleEffect *self = LRG_PARTICLE_EFFECT (object);

	if (self->asset != NULL && self->changed_id != 0)
		g_signal_handler_disconnect (self->asset, self->changed_id);
	self->changed_id = 0;
	if (self->entries != NULL)
		g_ptr_array_set_size (self->entries, 0);
	g_clear_object (&self->asset);
	g_clear_object (&self->manager);
	G_OBJECT_CLASS (lrg_particle_effect_parent_class)->dispose (object);
}

static void
lrg_particle_effect_finalize (GObject *object)
{
	LrgParticleEffect *self = LRG_PARTICLE_EFFECT (object);

	g_clear_pointer (&self->entries, g_ptr_array_unref);
	if (self->raycast_destroy != NULL)
		self->raycast_destroy (self->raycast_data);
	G_OBJECT_CLASS (lrg_particle_effect_parent_class)->finalize (object);
}

static void
lrg_particle_effect_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgParticleEffect *self = LRG_PARTICLE_EFFECT (object);

	switch (prop_id)
	{
	case FX_PROP_ASSET:       g_value_set_object (value, self->asset); break;
	case FX_PROP_AUTO_RELOAD: g_value_set_boolean (value, self->auto_reload); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_particle_effect_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgParticleEffect *self = LRG_PARTICLE_EFFECT (object);

	switch (prop_id)
	{
	case FX_PROP_ASSET:
		self->asset = g_value_dup_object (value);
		break;
	case FX_PROP_AUTO_RELOAD:
		lrg_particle_effect_set_auto_reload (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_particle_effect_constructed (GObject *object)
{
	LrgParticleEffect *self = LRG_PARTICLE_EFFECT (object);

	G_OBJECT_CLASS (lrg_particle_effect_parent_class)->constructed (object);
	if (self->asset == NULL)
		self->asset = lrg_particle_asset_new ();
	self->changed_id = g_signal_connect (self->asset, "changed", G_CALLBACK (on_asset_changed), self);
	rebuild (self);
}

static void
lrg_particle_effect_class_init (LrgParticleEffectClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_particle_effect_dispose;
	object_class->finalize = lrg_particle_effect_finalize;
	object_class->get_property = lrg_particle_effect_get_property;
	object_class->set_property = lrg_particle_effect_set_property;
	object_class->constructed = lrg_particle_effect_constructed;

	/**
	 * LrgParticleEffect:asset:
	 *
	 * The asset the effect plays (construct-only).
	 */
	effect_props[FX_PROP_ASSET] =
		g_param_spec_object ("asset", "Asset", "The effect played", LRG_TYPE_PARTICLE_ASSET,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgParticleEffect:auto-reload:
	 *
	 * Whether every #LrgParticleAsset::changed reloads the effect (the
	 * editor's live preview).
	 */
	effect_props[FX_PROP_AUTO_RELOAD] =
		g_param_spec_boolean ("auto-reload", "Auto Reload", "Reload on asset edits", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, FX_N_PROPS, effect_props);

	/**
	 * LrgParticleEffect::finished:
	 * @self: the effect
	 *
	 * Emitted once when a playing effect has nothing left alive (a
	 * non-looping effect after its last particle; a looping one only when
	 * it has nothing that emits).  The effect pauses itself.
	 */
	effect_signals[FX_SIGNAL_FINISHED] =
		g_signal_new ("finished", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_particle_effect_init (LrgParticleEffect *self)
{
	self->entries = g_ptr_array_new_with_free_func (entry_free);
	self->stopped = TRUE;
	self->rotation[3] = 1.0f;
}

LrgParticleEffect *
lrg_particle_effect_new (LrgParticleAsset *asset)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (asset), NULL);
	return g_object_new (LRG_TYPE_PARTICLE_EFFECT, "asset", asset, NULL);
}

LrgParticleEffect *
lrg_particle_asset_instantiate (LrgParticleAsset *self)
{
	return lrg_particle_effect_new (self);
}

LrgParticleAsset *
lrg_particle_effect_get_asset (LrgParticleEffect *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_EFFECT (self), NULL);
	return self->asset;
}

guint
lrg_particle_effect_get_n_systems (LrgParticleEffect *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_EFFECT (self), 0);
	return self->entries->len;
}

LrgParticleSystem *
lrg_particle_effect_get_system (LrgParticleEffect *self,
                                guint              index)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_EFFECT (self), NULL);
	if (index >= self->entries->len)
		return NULL;
	return ((Entry *) g_ptr_array_index (self->entries, index))->system;
}

LrgParticleSystem *
lrg_particle_effect_find_system (LrgParticleEffect *self,
                                 const gchar       *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_PARTICLE_EFFECT (self), NULL);

	for (i = 0; name != NULL && i < self->entries->len; i++)
	{
		Entry *e = g_ptr_array_index (self->entries, i);

		if (g_strcmp0 (lrg_particle_system_get_name (e->system), name) == 0)
			return e->system;
	}
	return NULL;
}

GObject *
lrg_particle_effect_get_renderer (LrgParticleEffect *self,
                                  guint              index)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_EFFECT (self), NULL);
	if (index >= self->entries->len)
		return NULL;
	return ((Entry *) g_ptr_array_index (self->entries, index))->renderer;
}

void
lrg_particle_effect_play (LrgParticleEffect *self)
{
	guint i;

	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));

	if (self->playing)
		return;
	if (self->stopped)
	{
		/* a fresh start: seeds again (an override may have changed) */
		for (i = 0; i < self->entries->len; i++)
		{
			Entry *e = g_ptr_array_index (self->entries, i);

			lrg_particle_system_set_seed (e->system, mix_seed (effect_seed (self), i));
		}
		self->time = 0.0f;
		self->finished = FALSE;
	}
	for (i = 0; i < self->entries->len; i++)
		lrg_particle_system_play (((Entry *) g_ptr_array_index (self->entries, i))->system);
	self->playing = TRUE;
	if (self->stopped)
	{
		self->stopped = FALSE;
		/* prewarm: run one cycle of a looping effect at 30 Hz */
		if (self->asset->prewarm && self->asset->loop && self->asset->duration > 0.0f)
		{
			lrg_particle_effect_simulate (self, self->asset->duration, 1.0f / 30.0f);
			self->time = 0.0f;
		}
	}
}

void
lrg_particle_effect_pause (LrgParticleEffect *self)
{
	guint i;

	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));

	for (i = 0; i < self->entries->len; i++)
		lrg_particle_system_pause (((Entry *) g_ptr_array_index (self->entries, i))->system);
	self->playing = FALSE;
}

void
lrg_particle_effect_stop (LrgParticleEffect *self)
{
	guint i;

	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));

	for (i = 0; i < self->entries->len; i++)
	{
		Entry *e = g_ptr_array_index (self->entries, i);

		lrg_particle_system_stop (e->system);
		if (LRG_IS_PARTICLE_RIBBON_RENDERER (e->renderer))
			lrg_particle_ribbon_renderer_clear (LRG_PARTICLE_RIBBON_RENDERER (e->renderer));
	}
	self->playing = FALSE;
	self->stopped = TRUE;
	self->time = 0.0f;
}

void
lrg_particle_effect_restart (LrgParticleEffect *self)
{
	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));
	lrg_particle_effect_stop (self);
	lrg_particle_effect_play (self);
}

gboolean
lrg_particle_effect_is_playing (LrgParticleEffect *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_EFFECT (self), FALSE);
	return self->playing;
}

gboolean
lrg_particle_effect_is_alive (LrgParticleEffect *self)
{
	guint i;

	g_return_val_if_fail (LRG_IS_PARTICLE_EFFECT (self), FALSE);

	for (i = 0; i < self->entries->len; i++)
	{
		Entry *e = g_ptr_array_index (self->entries, i);

		if (!e->enabled)
			continue;
		if (lrg_particle_system_is_alive (e->system))
			return TRUE;
		if (LRG_IS_PARTICLE_RIBBON_RENDERER (e->renderer) &&
		    lrg_particle_ribbon_renderer_get_n_trails (LRG_PARTICLE_RIBBON_RENDERER (e->renderer)) > 0)
			return TRUE;
	}
	return FALSE;
}

void
lrg_particle_effect_update (LrgParticleEffect *self,
                            gfloat             delta_time)
{
	guint i;

	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));

	if (!self->playing || !(delta_time > 0.0f))
		return;
	self->time += delta_time;
	for (i = 0; i < self->entries->len; i++)
	{
		Entry *e = g_ptr_array_index (self->entries, i);

		if (!e->enabled)
			continue;
		lrg_particle_system_update (e->system, delta_time);
		if (LRG_IS_PARTICLE_RIBBON_RENDERER (e->renderer))
			lrg_particle_ribbon_renderer_update (LRG_PARTICLE_RIBBON_RENDERER (e->renderer), e->system,
			                                     delta_time);
	}
	if (!self->finished && !lrg_particle_effect_is_alive (self))
	{
		self->finished = TRUE;
		lrg_particle_effect_pause (self);
		g_signal_emit (self, effect_signals[FX_SIGNAL_FINISHED], 0);
	}
}

void
lrg_particle_effect_simulate (LrgParticleEffect *self,
                              gfloat             seconds,
                              gfloat             step)
{
	gfloat left = seconds;

	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));
	g_return_if_fail (step > 0.0f);

	if (self->stopped)
		lrg_particle_effect_play (self);
	while (left > 1e-6f && self->playing)
	{
		gfloat dt = MIN (step, left);

		lrg_particle_effect_update (self, dt);
		left -= dt;
	}
}

void
lrg_particle_effect_seek (LrgParticleEffect *self,
                          gfloat             time,
                          gfloat             step)
{
	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));
	g_return_if_fail (step > 0.0f);

	lrg_particle_effect_restart (self);
	lrg_particle_effect_simulate (self, MAX (time, 0.0f), step);
}

gfloat
lrg_particle_effect_get_time (LrgParticleEffect *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_EFFECT (self), 0.0f);
	return self->time;
}

guint64
lrg_particle_effect_get_seed (LrgParticleEffect *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_EFFECT (self), 0);
	return effect_seed (self);
}

void
lrg_particle_effect_set_seed (LrgParticleEffect *self,
                              guint64            seed)
{
	guint i;

	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));

	self->seed_override = TRUE;
	self->seed = seed;
	for (i = 0; i < self->entries->len; i++)
		lrg_particle_system_set_seed (((Entry *) g_ptr_array_index (self->entries, i))->system, mix_seed (seed, i));
}

void
lrg_particle_effect_set_position (LrgParticleEffect *self,
                                  gfloat             x,
                                  gfloat             y,
                                  gfloat             z)
{
	guint i;

	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));

	self->position[0] = x;
	self->position[1] = y;
	self->position[2] = z;
	for (i = 0; i < self->entries->len; i++)
		lrg_particle_system_set_position (((Entry *) g_ptr_array_index (self->entries, i))->system, x, y, z);
}

void
lrg_particle_effect_get_position (LrgParticleEffect *self,
                                  gfloat            *x,
                                  gfloat            *y,
                                  gfloat            *z)
{
	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));
	if (x != NULL)
		*x = self->position[0];
	if (y != NULL)
		*y = self->position[1];
	if (z != NULL)
		*z = self->position[2];
}

void
lrg_particle_effect_set_rotation (LrgParticleEffect *self,
                                  const gfloat      *rotation)
{
	guint i;

	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));
	g_return_if_fail (rotation != NULL);

	memcpy (self->rotation, rotation, sizeof self->rotation);
	for (i = 0; i < self->entries->len; i++)
	{
		LrgParticleSystem *s = ((Entry *) g_ptr_array_index (self->entries, i))->system;

		lrg_particle_system_set_rotation (s, rotation);
		lrg_particle_system_get_rotation (s, self->rotation);   /* normalized */
	}
}

void
lrg_particle_effect_set_asset_manager (LrgParticleEffect *self,
                                       LrgAssetManager   *manager)
{
	guint i;

	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));

	if (!g_set_object (&self->manager, manager))
		return;
	for (i = 0; i < self->entries->len; i++)
	{
		Entry *e = g_ptr_array_index (self->entries, i);
		guint  k;

		e->texture_done = FALSE;
		e->mesh_done = FALSE;
		for (k = 0; k < lrg_particle_system_get_n_modules (e->system); k++)
			resolve_shape_mesh (self, lrg_particle_system_get_module (e->system, k));
	}
}

void
lrg_particle_effect_set_raycast_func (LrgParticleEffect     *self,
                                      LrgParticleRaycastFunc func,
                                      gpointer               user_data,
                                      GDestroyNotify         destroy)
{
	guint i;

	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));

	if (self->raycast_destroy != NULL)
		self->raycast_destroy (self->raycast_data);
	self->raycast = func;
	self->raycast_data = user_data;
	self->raycast_destroy = destroy;
	for (i = 0; i < self->entries->len; i++)
		lrg_particle_system_set_raycast_func (((Entry *) g_ptr_array_index (self->entries, i))->system,
		                                      func, user_data, NULL);
}

void
lrg_particle_effect_reload (LrgParticleEffect *self)
{
	gboolean same;
	guint    i;

	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));

	/* the same emitters (identity or name, order, capacity)? */
	same = self->entries->len == self->asset->emitters->len;
	for (i = 0; same && i < self->entries->len; i++)
	{
		Entry                   *e = g_ptr_array_index (self->entries, i);
		LrgParticleAssetEmitter *em = g_ptr_array_index (self->asset->emitters, i);

		same = (e->emitter == em || g_strcmp0 (e->emitter->name, em->name) == 0) &&
		       lrg_particle_system_get_max_particles (e->system) == em->max_particles;
	}

	if (same)
	{
		for (i = 0; i < self->entries->len; i++)
		{
			Entry *e = g_ptr_array_index (self->entries, i);

			g_set_object (&e->emitter, g_ptr_array_index (self->asset->emitters, i));
			lrg_particle_system_set_name (e->system, e->emitter->name);
			entry_sync_system (self, e);
			entry_sync_modules (self, e, TRUE);
			entry_sync_renderer (e);
		}
		return;
	}

	/* structure changed: rebuild, restarting a playing effect */
	{
		gboolean was_playing = self->playing;

		self->playing = FALSE;
		self->stopped = TRUE;
		rebuild (self);
		if (was_playing)
			lrg_particle_effect_play (self);
	}
}

gboolean
lrg_particle_effect_get_auto_reload (LrgParticleEffect *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_EFFECT (self), FALSE);
	return self->auto_reload;
}

void
lrg_particle_effect_set_auto_reload (LrgParticleEffect *self,
                                     gboolean           auto_reload)
{
	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));

	auto_reload = !!auto_reload;
	if (self->auto_reload == auto_reload)
		return;
	self->auto_reload = auto_reload;
	g_object_notify_by_pspec (G_OBJECT (self), effect_props[FX_PROP_AUTO_RELOAD]);
}

guint
lrg_particle_effect_get_active_count (LrgParticleEffect *self)
{
	guint i, n = 0;

	g_return_val_if_fail (LRG_IS_PARTICLE_EFFECT (self), 0);

	for (i = 0; i < self->entries->len; i++)
		n += lrg_particle_system_get_active_count (((Entry *) g_ptr_array_index (self->entries, i))->system);
	return n;
}

gboolean
lrg_particle_effect_get_bounds (LrgParticleEffect *self,
                                gfloat            *out_min,
                                gfloat            *out_max)
{
	gboolean any = FALSE;
	guint    i;
	gint     k;

	g_return_val_if_fail (LRG_IS_PARTICLE_EFFECT (self), FALSE);
	g_return_val_if_fail (out_min != NULL && out_max != NULL, FALSE);

	memcpy (out_min, self->position, 3 * sizeof (gfloat));
	memcpy (out_max, self->position, 3 * sizeof (gfloat));
	for (i = 0; i < self->entries->len; i++)
	{
		Entry *e = g_ptr_array_index (self->entries, i);
		gfloat lo[3], hi[3];

		if (!e->enabled || !lrg_particle_system_get_bounds (e->system, lo, hi))
			continue;
		for (k = 0; k < 3; k++)
		{
			if (!any || lo[k] < out_min[k])
				out_min[k] = lo[k];
			if (!any || hi[k] > out_max[k])
				out_max[k] = hi[k];
		}
		any = TRUE;
	}
	return any;
}

/* Loads the texture and mesh references through the manager (GPU) */
static void
entry_resolve (LrgParticleEffect *self,
               Entry             *e)
{
	LrgParticleRenderSettings *rs = e->emitter->renderer;

	if (!e->texture_done)
	{
		GrlTexture *tex = NULL;

		if (rs->v[RS_TEXTURE].s != NULL && self->manager != NULL)
			tex = lrg_asset_manager_load_texture (self->manager, rs->v[RS_TEXTURE].s, NULL);
		if (LRG_IS_PARTICLE_BILLBOARD_RENDERER (e->renderer))
			lrg_particle_billboard_renderer_set_texture (LRG_PARTICLE_BILLBOARD_RENDERER (e->renderer), tex);
		else if (LRG_IS_PARTICLE_RIBBON_RENDERER (e->renderer))
			lrg_particle_ribbon_renderer_set_texture (LRG_PARTICLE_RIBBON_RENDERER (e->renderer), tex);
		else if (LRG_IS_PARTICLE_MESH_RENDERER (e->renderer))
			lrg_particle_mesh_renderer_set_texture (LRG_PARTICLE_MESH_RENDERER (e->renderer), tex);
		g_free (e->texture_ref);
		e->texture_ref = g_strdup (rs->v[RS_TEXTURE].s);
		e->texture_done = TRUE;
	}
	if (!e->mesh_done && LRG_IS_PARTICLE_MESH_RENDERER (e->renderer))
	{
		GrlModel *model = NULL;

		if (rs->v[RS_MESH].s != NULL && self->manager != NULL)
			model = lrg_asset_manager_load_model (self->manager, rs->v[RS_MESH].s, NULL);
		lrg_particle_mesh_renderer_set_model (LRG_PARTICLE_MESH_RENDERER (e->renderer), model);
		g_free (e->mesh_ref);
		e->mesh_ref = g_strdup (rs->v[RS_MESH].s);
		e->mesh_done = TRUE;
	}
}

static void
entry_draw (LrgParticleEffect *self,
            Entry             *e,
            const gfloat      *eye,
            const gfloat      *up)
{
	if (!e->enabled || e->renderer == NULL)
		return;
	entry_resolve (self, e);
	if (LRG_IS_PARTICLE_BILLBOARD_RENDERER (e->renderer))
		lrg_particle_billboard_renderer_draw_view (LRG_PARTICLE_BILLBOARD_RENDERER (e->renderer), e->system,
		                                           eye, up);
	else if (LRG_IS_PARTICLE_RIBBON_RENDERER (e->renderer))
		lrg_particle_ribbon_renderer_draw (LRG_PARTICLE_RIBBON_RENDERER (e->renderer), e->system, eye);
	else if (LRG_IS_PARTICLE_MESH_RENDERER (e->renderer))
		lrg_particle_mesh_renderer_draw (LRG_PARTICLE_MESH_RENDERER (e->renderer), e->system);
}

void
lrg_particle_effect_draw (LrgParticleEffect *self,
                          const gfloat      *eye,
                          const gfloat      *up)
{
	guint i;

	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));
	g_return_if_fail (eye != NULL && up != NULL);

	for (i = 0; i < self->entries->len; i++)
		entry_draw (self, g_ptr_array_index (self->entries, i), eye, up);
}

void
lrg_particle_effect_draw_emitter (LrgParticleEffect *self,
                                  guint              index,
                                  const gfloat      *eye,
                                  const gfloat      *up)
{
	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));
	g_return_if_fail (eye != NULL && up != NULL);

	if (index < self->entries->len)
		entry_draw (self, g_ptr_array_index (self->entries, index), eye, up);
}

gboolean
lrg_particle_effect_get_emitter_bounds (LrgParticleEffect *self,
                                        guint              index,
                                        gfloat            *out_min,
                                        gfloat            *out_max)
{
	Entry   *e;
	gboolean any;
	gint     k;

	g_return_val_if_fail (LRG_IS_PARTICLE_EFFECT (self), FALSE);
	g_return_val_if_fail (out_min != NULL && out_max != NULL, FALSE);

	if (index >= self->entries->len)
		return FALSE;
	e = g_ptr_array_index (self->entries, index);
	if (!e->enabled || e->renderer == NULL)
		return FALSE;
	any = lrg_particle_system_get_bounds (e->system, out_min, out_max);

	/* ribbons: fading trails outlive their particles; pad by the width */
	if (LRG_IS_PARTICLE_RIBBON_RENDERER (e->renderer))
	{
		LrgParticleRibbonRenderer *r = LRG_PARTICLE_RIBBON_RENDERER (e->renderer);
		g_autoptr(GArray)          v = g_array_new (FALSE, FALSE, sizeof (gfloat));
		static const gfloat        eye[3] = { 0.0f, 1000.0f, 0.0f };
		guint                      i;

		lrg_particle_ribbon_renderer_build (r, e->system, eye, v);
		for (i = 0; i + 2 < v->len; i += LRG_PARTICLE_RIBBON_FLOATS_PER_VERTEX)
		{
			for (k = 0; k < 3; k++)
			{
				gfloat c = g_array_index (v, gfloat, i + (guint) k);

				if (!any || c < out_min[k])
					out_min[k] = c;
				if (!any || c > out_max[k])
					out_max[k] = c;
			}
			any = TRUE;
		}
		for (k = 0; any && k < 3; k++)
		{
			out_min[k] -= lrg_particle_ribbon_renderer_get_width (r);
			out_max[k] += lrg_particle_ribbon_renderer_get_width (r);
		}
	}
	return any;
}

void
lrg_particle_effect_draw_camera (LrgParticleEffect *self,
                                 GrlCamera3D       *camera)
{
	g_autoptr(GrlVector3) pos = NULL;
	g_autoptr(GrlVector3) upv = NULL;
	gfloat                eye[3], up[3];

	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));
	g_return_if_fail (GRL_IS_CAMERA3D (camera));

	pos = grl_camera3d_get_position (camera);
	upv = grl_camera3d_get_up (camera);
	eye[0] = pos->x;
	eye[1] = pos->y;
	eye[2] = pos->z;
	up[0] = upv->x;
	up[1] = upv->y;
	up[2] = upv->z;
	lrg_particle_effect_draw (self, eye, up);
}

void
lrg_particle_effect_release_gpu (LrgParticleEffect *self)
{
	guint i;

	g_return_if_fail (LRG_IS_PARTICLE_EFFECT (self));

	for (i = 0; i < self->entries->len; i++)
	{
		Entry *e = g_ptr_array_index (self->entries, i);

		if (LRG_IS_PARTICLE_BILLBOARD_RENDERER (e->renderer))
			lrg_particle_billboard_renderer_release_gpu (LRG_PARTICLE_BILLBOARD_RENDERER (e->renderer));
		else if (LRG_IS_PARTICLE_RIBBON_RENDERER (e->renderer))
			lrg_particle_ribbon_renderer_release_gpu (LRG_PARTICLE_RIBBON_RENDERER (e->renderer));
		else if (LRG_IS_PARTICLE_MESH_RENDERER (e->renderer))
			lrg_particle_mesh_renderer_release_gpu (LRG_PARTICLE_MESH_RENDERER (e->renderer));
		e->texture_done = FALSE;
		e->mesh_done = FALSE;
	}
}
