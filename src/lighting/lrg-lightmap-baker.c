/* lrg-lightmap-baker.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLightmapBaker: the CPU path tracer that bakes lightmaps and probe
 * grids.  See the header for the pipeline.  Layout of this file:
 *
 *   settings      LrgLightmapBakeSettings (a property table)
 *   inputs        BakeInput / BakeLight / ProbeVolume snapshots
 *   scene         world-space geometry, BVH, lights and sky (Scene)
 *   tracer        RNG, sampling, direct light, paths, texel estimator
 *   atlas         packing and texel rasterization
 *   postprocess   a-trous denoise, dilation, seam stitching
 *   probes        SH projection of probe rays
 *   driver        thread pool, progress, cancellation, results
 *   public API    the baker object
 *
 * Units: light colours are the display-space values the lit shader gets
 * (colour x intensity); the shader linearizes colour x falloff once, so
 * the baker does exactly that (pow 2.2) to match realtime lighting.  A
 * texel holds irradiance / pi in linear radiance.  With cosine-weighted
 * sampling the indirect part of that is the plain mean of the incoming
 * radiance samples.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-lightmap-baker.h"
#include "lrg-bake-bvh.h"
#include "../core/lrg-math3d.h"
#include "../graphics/lrg-light-set.h"
#include "../graphics/lrg-gltf-geometry.h"
#include "../graphics/lrg-hdr-image.h"
#include "../ecs/lrg-component.h"
#include "../world3d/lrg-entity3d.h"
#include "../world3d/components/lrg-mesh-renderer-component.h"
#include "../graphics/lrg-material.h"
#include "../scene/lrg-material3d.h"

#include <graylib.h>
#include <raylib.h>

#define BAKE_PI          (3.14159265358979323846f)
#define SKY_FACE_SIZE    (64u)
#define BACKFACE_LIMIT   (0.1f)    /* texel rays hitting backs before it is "inside" */
#define PROBE_BACKFACE   (0.25f)   /* probe rays hitting backs before it is invalid */
#define DENOISE_ITERS    (4)
#define SEAM_ITERS       (8)

/* ==========================================================================
 * Enums
 * ========================================================================== */

GType
lrg_lightmap_bake_phase_get_type (void)
{
	static gsize type_id = 0;
	static const GEnumValue values[] = {
		{ LRG_LIGHTMAP_BAKE_PHASE_PREPARE, "LRG_LIGHTMAP_BAKE_PHASE_PREPARE", "prepare" },
		{ LRG_LIGHTMAP_BAKE_PHASE_LIGHTMAPS, "LRG_LIGHTMAP_BAKE_PHASE_LIGHTMAPS", "lightmaps" },
		{ LRG_LIGHTMAP_BAKE_PHASE_POSTPROCESS, "LRG_LIGHTMAP_BAKE_PHASE_POSTPROCESS", "postprocess" },
		{ LRG_LIGHTMAP_BAKE_PHASE_PROBES, "LRG_LIGHTMAP_BAKE_PHASE_PROBES", "probes" },
		{ LRG_LIGHTMAP_BAKE_PHASE_DONE, "LRG_LIGHTMAP_BAKE_PHASE_DONE", "done" },
		{ 0, NULL, NULL }
	};

	if (g_once_init_enter (&type_id))
	{
		GType id = g_enum_register_static (g_intern_static_string ("LrgLightmapBakePhase"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

GType
lrg_lightmap_bake_object_flags_get_type (void)
{
	static gsize type_id = 0;
	static const GFlagsValue values[] = {
		{ LRG_LIGHTMAP_BAKE_OBJECT_NONE, "LRG_LIGHTMAP_BAKE_OBJECT_NONE", "none" },
		{ LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE, "LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE", "receive" },
		{ LRG_LIGHTMAP_BAKE_OBJECT_CONTRIBUTE, "LRG_LIGHTMAP_BAKE_OBJECT_CONTRIBUTE", "contribute" },
		{ LRG_LIGHTMAP_BAKE_OBJECT_DOUBLE_SIDED, "LRG_LIGHTMAP_BAKE_OBJECT_DOUBLE_SIDED", "double-sided" },
		{ 0, NULL, NULL }
	};

	if (g_once_init_enter (&type_id))
	{
		GType id = g_flags_register_static (g_intern_static_string ("LrgLightmapBakeObjectFlags"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

/* ==========================================================================
 * Settings
 * ========================================================================== */

/* The plain values a bake reads (snapshotted when it starts). */
typedef struct
{
	gfloat   texels_per_unit;
	guint    samples;
	guint    bounces;
	guint    max_lightmap_size;
	guint    padding;
	gboolean denoise;
	gfloat   probe_spacing;
	guint    probe_samples;
	gboolean auto_probe_volume;
	guint    seed;
	guint    threads;
	gfloat   light_radius;
	gfloat   sun_angle;
} BakeValues;

struct _LrgLightmapBakeSettings
{
	GObject    parent_instance;
	BakeValues v;
};

G_DEFINE_FINAL_TYPE (LrgLightmapBakeSettings, lrg_lightmap_bake_settings, G_TYPE_OBJECT)

enum
{
	SPROP_0,
	SPROP_TEXELS_PER_UNIT,
	SPROP_SAMPLES,
	SPROP_BOUNCES,
	SPROP_MAX_LIGHTMAP_SIZE,
	SPROP_PADDING,
	SPROP_DENOISE,
	SPROP_PROBE_SPACING,
	SPROP_PROBE_SAMPLES,
	SPROP_AUTO_PROBE_VOLUME,
	SPROP_SEED,
	SPROP_THREADS,
	SPROP_LIGHT_RADIUS,
	SPROP_SUN_ANGLE,
	N_SPROPS
};

static GParamSpec *settings_props[N_SPROPS];

/* where each property lives in BakeValues */
static gpointer
settings_field (LrgLightmapBakeSettings *self,
                guint                    prop_id)
{
	switch (prop_id)
	{
	case SPROP_TEXELS_PER_UNIT: return &self->v.texels_per_unit;
	case SPROP_SAMPLES: return &self->v.samples;
	case SPROP_BOUNCES: return &self->v.bounces;
	case SPROP_MAX_LIGHTMAP_SIZE: return &self->v.max_lightmap_size;
	case SPROP_PADDING: return &self->v.padding;
	case SPROP_DENOISE: return &self->v.denoise;
	case SPROP_PROBE_SPACING: return &self->v.probe_spacing;
	case SPROP_PROBE_SAMPLES: return &self->v.probe_samples;
	case SPROP_AUTO_PROBE_VOLUME: return &self->v.auto_probe_volume;
	case SPROP_SEED: return &self->v.seed;
	case SPROP_THREADS: return &self->v.threads;
	case SPROP_LIGHT_RADIUS: return &self->v.light_radius;
	case SPROP_SUN_ANGLE: return &self->v.sun_angle;
	default: return NULL;
	}
}

static void
lrg_lightmap_bake_settings_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
	gpointer field = settings_field (LRG_LIGHTMAP_BAKE_SETTINGS (object), prop_id);

	if (field == NULL)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}
	if (pspec->value_type == G_TYPE_FLOAT)
		g_value_set_float (value, *(gfloat *)field);
	else if (pspec->value_type == G_TYPE_UINT)
		g_value_set_uint (value, *(guint *)field);
	else
		g_value_set_boolean (value, *(gboolean *)field);
}

static void
lrg_lightmap_bake_settings_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
	gpointer field = settings_field (LRG_LIGHTMAP_BAKE_SETTINGS (object), prop_id);
	gboolean changed;

	if (field == NULL)
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}
	if (pspec->value_type == G_TYPE_FLOAT)
	{
		gfloat f = g_value_get_float (value);

		changed = *(gfloat *)field != f;
		*(gfloat *)field = f;
	}
	else if (pspec->value_type == G_TYPE_UINT)
	{
		guint u = g_value_get_uint (value);

		changed = *(guint *)field != u;
		*(guint *)field = u;
	}
	else
	{
		gboolean b = g_value_get_boolean (value);

		changed = *(gboolean *)field != b;
		*(gboolean *)field = b;
	}
	if (changed)
		g_object_notify_by_pspec (object, pspec);
}

#define SFLAGS (G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS)

static void
lrg_lightmap_bake_settings_class_init (LrgLightmapBakeSettingsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = lrg_lightmap_bake_settings_get_property;
	object_class->set_property = lrg_lightmap_bake_settings_set_property;

	/**
	 * LrgLightmapBakeSettings:texels-per-unit:
	 *
	 * Lightmap texels per world unit along a surface (times each
	 * object's lightmap scale).
	 */
	settings_props[SPROP_TEXELS_PER_UNIT] =
		g_param_spec_float ("texels-per-unit", NULL, NULL, 0.01f, 4096.0f, 8.0f, SFLAGS);

	/**
	 * LrgLightmapBakeSettings:samples:
	 *
	 * Paths (and shadow samples per light) per texel.
	 */
	settings_props[SPROP_SAMPLES] =
		g_param_spec_uint ("samples", NULL, NULL, 1, 65536, 64, SFLAGS);

	/**
	 * LrgLightmapBakeSettings:bounces:
	 *
	 * Indirect bounces (0 = direct light only; 1 = light reflected once,
	 * plus emissive surfaces and the sky seen directly).
	 */
	settings_props[SPROP_BOUNCES] =
		g_param_spec_uint ("bounces", NULL, NULL, 0, 16, 2, SFLAGS);

	/**
	 * LrgLightmapBakeSettings:max-lightmap-size:
	 *
	 * Edge of an atlas page in texels (pages shrink to the smallest power
	 * of two that holds everything when one page suffices).
	 */
	settings_props[SPROP_MAX_LIGHTMAP_SIZE] =
		g_param_spec_uint ("max-lightmap-size", NULL, NULL, 32, 8192, 1024, SFLAGS);

	/**
	 * LrgLightmapBakeSettings:padding:
	 *
	 * Texels left around each object's rectangle in the atlas (filled by
	 * dilation, so bilinear filtering never reads a neighbour).
	 */
	settings_props[SPROP_PADDING] =
		g_param_spec_uint ("padding", NULL, NULL, 0, 16, 2, SFLAGS);

	/**
	 * LrgLightmapBakeSettings:denoise:
	 *
	 * Runs the edge-preserving a-trous filter over the traced texels.
	 */
	settings_props[SPROP_DENOISE] =
		g_param_spec_boolean ("denoise", NULL, NULL, TRUE, SFLAGS);

	/**
	 * LrgLightmapBakeSettings:probe-spacing:
	 *
	 * World distance between probes of volumes without an explicit
	 * resolution.
	 */
	settings_props[SPROP_PROBE_SPACING] =
		g_param_spec_float ("probe-spacing", NULL, NULL, 0.01f, 10000.0f, 2.0f, SFLAGS);

	/**
	 * LrgLightmapBakeSettings:probe-samples:
	 *
	 * Rays per probe.
	 */
	settings_props[SPROP_PROBE_SAMPLES] =
		g_param_spec_uint ("probe-samples", NULL, NULL, 16, 65536, 256, SFLAGS);

	/**
	 * LrgLightmapBakeSettings:auto-probe-volume:
	 *
	 * Without explicit probe volumes, bake one covering the static
	 * geometry.
	 */
	settings_props[SPROP_AUTO_PROBE_VOLUME] =
		g_param_spec_boolean ("auto-probe-volume", NULL, NULL, TRUE, SFLAGS);

	/**
	 * LrgLightmapBakeSettings:seed:
	 *
	 * Seed of every random number of the bake (same seed, same result).
	 */
	settings_props[SPROP_SEED] =
		g_param_spec_uint ("seed", NULL, NULL, 0, G_MAXUINT, 1, SFLAGS);

	/**
	 * LrgLightmapBakeSettings:threads:
	 *
	 * Worker threads (0 = one per processor).
	 */
	settings_props[SPROP_THREADS] =
		g_param_spec_uint ("threads", NULL, NULL, 0, 256, 0, SFLAGS);

	/**
	 * LrgLightmapBakeSettings:light-radius:
	 *
	 * Default emitter radius of point and spot lights (soft shadows), in
	 * world units.
	 */
	settings_props[SPROP_LIGHT_RADIUS] =
		g_param_spec_float ("light-radius", NULL, NULL, 0.0f, 100.0f, 0.05f, SFLAGS);

	/**
	 * LrgLightmapBakeSettings:sun-angle:
	 *
	 * Default angular radius of directional lights in degrees.
	 */
	settings_props[SPROP_SUN_ANGLE] =
		g_param_spec_float ("sun-angle", NULL, NULL, 0.0f, 45.0f, 0.5f, SFLAGS);

	g_object_class_install_properties (object_class, N_SPROPS, settings_props);
}

static void
lrg_lightmap_bake_settings_init (LrgLightmapBakeSettings *self)
{
}

LrgLightmapBakeSettings *
lrg_lightmap_bake_settings_new (void)
{
	return g_object_new (LRG_TYPE_LIGHTMAP_BAKE_SETTINGS, NULL);
}

LrgLightmapBakeSettings *
lrg_lightmap_bake_settings_copy (LrgLightmapBakeSettings *self)
{
	LrgLightmapBakeSettings *copy;

	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKE_SETTINGS (self), NULL);

	copy = lrg_lightmap_bake_settings_new ();
	copy->v = self->v;
	return copy;
}

void
lrg_lightmap_bake_settings_write_to_lightmap (LrgLightmapBakeSettings *self,
                                              LrgLightmap3D           *lightmap)
{
	guint i;

	g_return_if_fail (LRG_IS_LIGHTMAP_BAKE_SETTINGS (self));
	g_return_if_fail (LRG_IS_LIGHTMAP3D (lightmap));

	for (i = 1; i < N_SPROPS; i++)
	{
		GParamSpec      *pspec = settings_props[i];
		gpointer         field = settings_field (self, i);
		g_autofree gchar *key = g_strdelimit (g_strdup (pspec->name), "-", '_');
		gdouble          value;

		if (pspec->value_type == G_TYPE_FLOAT)
			value = (gdouble)*(gfloat *)field;
		else if (pspec->value_type == G_TYPE_UINT)
			value = (gdouble)*(guint *)field;
		else
			value = *(gboolean *)field ? 1.0 : 0.0;
		lrg_lightmap3d_set_setting (lightmap, key, value);
	}
}

/* ==========================================================================
 * Inputs
 * ========================================================================== */

typedef struct
{
	gchar     *guid;
	LrgUvMesh *mesh;
	gfloat     matrix[16];
	gfloat     albedo[3];     /* linear */
	gfloat     emission[3];   /* linear radiance */
	gfloat     scale;
	guint      flags;
} BakeInput;

static void
bake_input_free (gpointer data)
{
	BakeInput *in = data;

	g_free (in->guid);
	g_clear_object (&in->mesh);
	g_free (in);
}

static BakeInput *
bake_input_copy (const BakeInput *src)
{
	BakeInput *in = g_memdup2 (src, sizeof (BakeInput));

	in->guid = g_strdup (src->guid);
	in->mesh = g_object_ref (src->mesh);
	return in;
}

typedef struct
{
	LrgLight3DKind kind;
	gfloat         pos[3];
	gfloat         dir[3];        /* unit, the way the light travels */
	gfloat         color[3];      /* display colour x intensity */
	gfloat         range;
	gfloat         inner_deg;
	gfloat         outer_deg;
	gfloat         radius;        /* world units; degrees for the sun */
	gboolean       baked;         /* direct light stored in texels */
	gfloat         sun_rad[3];    /* pow (color, 2.2): the linear colour */
	gfloat         sun_cos;       /* cos of its angular radius */
} BakeLight;

typedef struct
{
	gfloat min[3];
	gfloat max[3];
	guint  n[3];
} ProbeVolume;

/* ==========================================================================
 * Scene
 * ========================================================================== */

typedef struct
{
	gchar     *guid;
	LrgUvMesh *mesh;          /* the mesh with uv2 the texels map to */
	guint      flags;
	gfloat     albedo[3];
	gfloat     emission[3];
	gboolean   generated;     /* uv2 from the unwrapper */
	gfloat     scale;         /* lightmap scale */
	gdouble    area;          /* world surface area */
	gdouble    uv_area;       /* uv2 coverage */
	guint      first_vert;
	guint      first_tri;
	guint      n_tris;
	/* atlas placement (receivers) */
	guint      res;
	guint      page;
	guint      rect[4];       /* x, y, w, h including padding */
	gfloat     so[4];
	gfloat     texel_world;
} SceneObj;

typedef struct
{
	BakeValues  v;
	SceneObj   *objs;
	guint       n_objs;

	gfloat     *pos;          /* world positions */
	gfloat     *nrm;          /* world normals */
	gfloat     *auv;          /* atlas coordinates in texels (receivers) */
	guint       n_verts;
	guint32    *idx;
	guint32    *tri_obj;
	gfloat     *tri_ng;       /* unit geometric normal per triangle */
	guint       n_tris;

	LrgBakeBvh *bvh;
	guint32    *bvh_map;      /* BVH triangle -> scene triangle */
	gfloat      bmin[3];
	gfloat      bmax[3];
	gboolean    has_bounds;
	gfloat      eps;

	BakeLight  *lights;
	guint       n_lights;

	/* emissive triangles for next-event estimation of emissive surfaces */
	guint32    *emit_tri;     /* scene triangle */
	gfloat     *emit_cdf;     /* cumulative area x luminance, normalized */
	gfloat     *emit_area;
	guint       n_emit;

	gfloat     *sky;          /* 6 x SKY_FACE_SIZE^2 x 3, or NULL */
	gfloat      sky_flat[3];

	guint       n_pages;
	guint       page_size;
} Scene;

static void
scene_free (Scene *s)
{
	guint i;

	if (s == NULL)
		return;
	for (i = 0; i < s->n_objs; i++)
	{
		g_free (s->objs[i].guid);
		g_clear_object (&s->objs[i].mesh);
	}
	g_free (s->objs);
	g_free (s->pos);
	g_free (s->nrm);
	g_free (s->auv);
	g_free (s->idx);
	g_free (s->tri_obj);
	g_free (s->tri_ng);
	g_clear_object (&s->bvh);
	g_free (s->bvh_map);
	g_free (s->lights);
	g_free (s->emit_tri);
	g_free (s->emit_cdf);
	g_free (s->emit_area);
	g_free (s->sky);
	g_free (s);
}

/* --- small vector math --------------------------------------------------- */

static inline gfloat
dot3 (const gfloat *a,
      const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void
cross3 (const gfloat *a,
        const gfloat *b,
        gfloat       *out)
{
	gfloat x = a[1] * b[2] - a[2] * b[1];
	gfloat y = a[2] * b[0] - a[0] * b[2];
	gfloat z = a[0] * b[1] - a[1] * b[0];

	out[0] = x;
	out[1] = y;
	out[2] = z;
}

static inline gfloat
normalize3 (gfloat *v)
{
	gfloat len = sqrtf (dot3 (v, v));

	if (len > 1e-30f)
	{
		v[0] /= len;
		v[1] /= len;
		v[2] /= len;
	}
	return len;
}

static gfloat
to_linear (gfloat c)
{
	return c > 0.0f ? powf (c, 2.2f) : 0.0f;
}

static gfloat
luminance (const gfloat *c)
{
	return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

/* --- sky cube: our own face mapping, filled and read here only ------------ */

static guint
dir_to_face (const gfloat *d,
             gfloat       *s,
             gfloat       *t)
{
	gfloat ax = fabsf (d[0]);
	gfloat ay = fabsf (d[1]);
	gfloat az = fabsf (d[2]);
	gfloat ma;
	gfloat u;
	gfloat v;
	guint  f;

	if (ax >= ay && ax >= az)
	{
		f = d[0] >= 0.0f ? 0u : 1u;
		ma = ax;
		u = d[2];
		v = d[1];
	}
	else if (ay >= az)
	{
		f = d[1] >= 0.0f ? 2u : 3u;
		ma = ay;
		u = d[0];
		v = d[2];
	}
	else
	{
		f = d[2] >= 0.0f ? 4u : 5u;
		ma = az;
		u = d[0];
		v = d[1];
	}
	ma = MAX (ma, 1e-30f);
	*s = 0.5f * (u / ma + 1.0f);
	*t = 0.5f * (v / ma + 1.0f);
	return f;
}

static void
face_to_dir (guint   f,
             gfloat  s,
             gfloat  t,
             gfloat *d)
{
	gfloat u = s * 2.0f - 1.0f;
	gfloat v = t * 2.0f - 1.0f;
	gfloat sign = (f % 2u == 0u) ? 1.0f : -1.0f;

	switch (f / 2u)
	{
	case 0:
		d[0] = sign;
		d[1] = v;
		d[2] = u;
		break;
	case 1:
		d[0] = u;
		d[1] = sign;
		d[2] = v;
		break;
	default:
		d[0] = u;
		d[1] = v;
		d[2] = sign;
		break;
	}
	normalize3 (d);
}

/* Samples the environment into a cube (main thread, before the bake). */
static gfloat *
sample_sky_cube (LrgEnvironment *env)
{
	gfloat *sky = g_new (gfloat, (gsize)6 * SKY_FACE_SIZE * SKY_FACE_SIZE * 3);
	guint   f;
	guint   x;
	guint   y;

	for (f = 0; f < 6; f++)
	{
		for (y = 0; y < SKY_FACE_SIZE; y++)
		{
			for (x = 0; x < SKY_FACE_SIZE; x++)
			{
				gfloat d[3];

				face_to_dir (f, ((gfloat)x + 0.5f) / SKY_FACE_SIZE, ((gfloat)y + 0.5f) / SKY_FACE_SIZE, d);
				lrg_environment_sample_sky (env, d,
				                            sky + (((gsize)f * SKY_FACE_SIZE + y) * SKY_FACE_SIZE + x) * 3u);
			}
		}
	}
	return sky;
}

static void
sky_lookup (const Scene  *s,
            const gfloat *d,
            gfloat       *out)
{
	gfloat       fs;
	gfloat       ft;
	guint        f;
	guint        x;
	guint        y;
	const gfloat *c;

	if (s->sky == NULL)
	{
		out[0] = s->sky_flat[0];
		out[1] = s->sky_flat[1];
		out[2] = s->sky_flat[2];
		return;
	}
	f = dir_to_face (d, &fs, &ft);
	x = MIN ((guint)(fs * SKY_FACE_SIZE), SKY_FACE_SIZE - 1u);
	y = MIN ((guint)(ft * SKY_FACE_SIZE), SKY_FACE_SIZE - 1u);
	c = s->sky + (((gsize)f * SKY_FACE_SIZE + y) * SKY_FACE_SIZE + x) * 3u;
	out[0] = c[0];
	out[1] = c[1];
	out[2] = c[2];
}

/* --- light conversion ------------------------------------------------------ */

/*
 * A light description as the bake sees it; colours follow
 * lrg_light_set_add_light_desc(): the sun's colour x intensity clamped
 * to display range, a point light's colour clamped then times its
 * intensity.
 */
static void
light_from_desc (BakeLight            *l,
                 const LrgLight3DDesc *desc,
                 gfloat                radius)
{
	gint k;

	memset (l, 0, sizeof *l);
	l->kind = desc->kind;
	memcpy (l->pos, desc->position, sizeof l->pos);
	memcpy (l->dir, desc->direction, sizeof l->dir);
	if (normalize3 (l->dir) <= 1e-30f)
	{
		l->dir[0] = 0.0f;
		l->dir[1] = -1.0f;
		l->dir[2] = 0.0f;
	}
	l->range = desc->range > 0.0f ? desc->range : 10.0f;
	l->inner_deg = desc->inner_angle * 180.0f / BAKE_PI;
	l->outer_deg = desc->outer_angle * 180.0f / BAKE_PI;
	l->radius = MAX (radius, 0.0f);
	l->baked = desc->bake_mode == LRG_LIGHT_MODE_BAKED;
	for (k = 0; k < 3; k++)
	{
		if (desc->kind == LRG_LIGHT3D_DIRECTIONAL)
			l->color[k] = CLAMP (desc->color[k] * MAX (desc->intensity, 0.0f), 0.0f, 1.0f);
		else
			l->color[k] = CLAMP (desc->color[k], 0.0f, 1.0f) * MAX (desc->intensity, 0.0f);
		l->sun_rad[k] = to_linear (l->color[k]);
	}
	l->sun_cos = cosf (MIN (l->radius, 45.0f) * BAKE_PI / 180.0f);
}

/* ==========================================================================
 * Tracer
 * ========================================================================== */

/* A small counter-based generator: state hashed per texel / probe. */
typedef struct
{
	guint32 state;
} Rng;

static inline guint32
hash32 (guint32 x)
{
	/* lowbias32 (Chris Wellons) */
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

static inline void
rng_seed (Rng     *r,
          guint32  seed,
          guint32  stream,
          guint32  index)
{
	r->state = hash32 (seed * 0x9e3779b9U ^ hash32 (stream * 0x85ebca6bU ^ hash32 (index + 0x632be5abU)));
}

static inline gfloat
rng_next (Rng *r)
{
	r->state = r->state * 747796405U + 2891336453U;
	return (gfloat)(hash32 (r->state) >> 8) * (1.0f / 16777216.0f);
}

/* orthonormal basis around a unit normal (Duff et al. 2017) */
static inline void
onb (const gfloat *n,
     gfloat       *t,
     gfloat       *b)
{
	gfloat sign = n[2] >= 0.0f ? 1.0f : -1.0f;
	gfloat a = -1.0f / (sign + n[2]);
	gfloat bb = n[0] * n[1] * a;

	t[0] = 1.0f + sign * n[0] * n[0] * a;
	t[1] = sign * bb;
	t[2] = -sign * n[0];
	b[0] = bb;
	b[1] = sign + n[1] * n[1] * a;
	b[2] = -n[1];
}

/* cosine-weighted direction around @n from two uniforms */
static inline void
cosine_dir (const gfloat *n,
            gfloat        u1,
            gfloat        u2,
            gfloat       *out)
{
	gfloat t[3];
	gfloat b[3];
	gfloat r = sqrtf (u1);
	gfloat phi = 2.0f * BAKE_PI * u2;
	gfloat x = r * cosf (phi);
	gfloat y = r * sinf (phi);
	gfloat z = sqrtf (MAX (0.0f, 1.0f - u1));
	gint   k;

	onb (n, t, b);
	for (k = 0; k < 3; k++)
		out[k] = t[k] * x + b[k] * y + n[k] * z;
}

/* a point offset off the surface along the geometric normal */
static inline void
offset_point (const Scene  *s,
              const gfloat *p,
              const gfloat *ng,
              gfloat       *out)
{
	out[0] = p[0] + ng[0] * s->eps;
	out[1] = p[1] + ng[1] * s->eps;
	out[2] = p[2] + ng[2] * s->eps;
}

/*
 * Adds the direct light of one light at @p (shading normal @n,
 * geometric normal @ng) with one jittered shadow ray.
 */
static void
light_direct (const Scene     *s,
              const BakeLight *l,
              const gfloat    *p,
              const gfloat    *n,
              const gfloat    *ng,
              Rng             *rng,
              gfloat          *out)
{
	gfloat o[3];
	gfloat ld[3];
	gfloat nl;
	gint   k;

	offset_point (s, p, ng, o);
	if (l->kind == LRG_LIGHT3D_DIRECTIONAL)
	{
		gfloat c[3];

		c[0] = -l->dir[0];
		c[1] = -l->dir[1];
		c[2] = -l->dir[2];
		if (l->sun_cos < 1.0f)
		{
			/* a uniform direction inside the sun's cone */
			gfloat t[3];
			gfloat b[3];
			gfloat ct = 1.0f - rng_next (rng) * (1.0f - l->sun_cos);
			gfloat st = sqrtf (MAX (0.0f, 1.0f - ct * ct));
			gfloat ph = 2.0f * BAKE_PI * rng_next (rng);

			onb (c, t, b);
			for (k = 0; k < 3; k++)
				ld[k] = t[k] * st * cosf (ph) + b[k] * st * sinf (ph) + c[k] * ct;
		}
		else
			memcpy (ld, c, sizeof ld);
		nl = dot3 (n, ld);
		if (nl <= 0.0f || dot3 (ng, ld) <= 0.0f)
			return;
		if (lrg_bake_bvh_occluded (s->bvh, o, ld, 0.0f, G_MAXFLOAT))
			return;
		for (k = 0; k < 3; k++)
			out[k] += l->sun_rad[k] * nl;
		return;
	}
	else
	{
		gfloat dc[3];
		gfloat dist;
		gfloat kk;
		gfloat fd;
		gfloat q[3];
		gfloat dq;

		/* falloff and cone from the light's centre, like the shader */
		dc[0] = l->pos[0] - p[0];
		dc[1] = l->pos[1] - p[1];
		dc[2] = l->pos[2] - p[2];
		dist = sqrtf (dot3 (dc, dc));
		kk = 1.0f - dist / l->range;
		if (kk <= 0.0f)
			return;
		fd = kk * kk;
		if (l->kind == LRG_LIGHT3D_SPOT && dist > 1e-4f)
			fd *= lrg_light_set_spot_factor (l->inner_deg, l->outer_deg, -dot3 (l->dir, dc) / dist);
		if (fd <= 0.0f)
			return;

		/* the shadow ray aims at a random point of the emitting sphere */
		q[0] = l->pos[0];
		q[1] = l->pos[1];
		q[2] = l->pos[2];
		if (l->radius > 0.0f)
		{
			gfloat z = 1.0f - 2.0f * rng_next (rng);
			gfloat rr = sqrtf (MAX (0.0f, 1.0f - z * z));
			gfloat ph = 2.0f * BAKE_PI * rng_next (rng);
			gfloat sc = l->radius * cbrtf (rng_next (rng));

			q[0] += rr * cosf (ph) * sc;
			q[1] += rr * sinf (ph) * sc;
			q[2] += z * sc;
		}
		ld[0] = q[0] - o[0];
		ld[1] = q[1] - o[1];
		ld[2] = q[2] - o[2];
		dq = normalize3 (ld);
		if (dq <= 1e-6f)
			return;
		nl = dist > 1e-4f ? dot3 (n, dc) / dist : 1.0f;
		if (nl <= 0.0f || dot3 (ng, ld) <= 0.0f)
			return;
		if (lrg_bake_bvh_occluded (s->bvh, o, ld, 0.0f, dq * (1.0f - 1e-4f) - s->eps))
			return;
		/* pow (c fd, 2.2) = pow (c, 2.2) pow (fd, 2.2): one powf per sample */
		fd = to_linear (fd) * nl;
		for (k = 0; k < 3; k++)
			out[k] += l->sun_rad[k] * fd;
	}
}

static void
direct_light (const Scene  *s,
              const gfloat *p,
              const gfloat *n,
              const gfloat *ng,
              gboolean      baked_only,
              Rng          *rng,
              gfloat       *out)
{
	guint i;

	for (i = 0; i < s->n_lights; i++)
	{
		if (baked_only && !s->lights[i].baked)
			continue;
		light_direct (s, &s->lights[i], p, n, ng, rng, out);
	}
}

/*
 * Adds the light of emissive surfaces at @p with one sample: a triangle
 * picked in proportion to its area x emitted luminance, a uniform point
 * on it, converted from area to solid angle measure.  Emission reaches
 * texels and path vertices only through here, never by paths hitting an
 * emitter (that would count it twice), except for probe rays seeing an
 * emitter directly.
 */
static void
emissive_direct (const Scene  *s,
                 const gfloat *p,
                 const gfloat *n,
                 const gfloat *ng,
                 Rng          *rng,
                 gfloat       *out)
{
	gfloat          u = rng_next (rng);
	guint           lo = 0;
	guint           hi;
	guint           tri;
	const guint32  *ix;
	const SceneObj *ob;
	gfloat          r1;
	gfloat          r2;
	gfloat          q[3];
	gfloat          o[3];
	gfloat          d[3];
	gfloat          d2;
	gfloat          dist;
	gfloat          cx;
	gfloat          cl;
	gfloat          pdf_pick;
	gfloat          g;
	const gfloat   *nl;
	gint            k;

	if (s->n_emit == 0)
		return;
	/* pick a triangle by binary search on the CDF */
	hi = s->n_emit - 1u;
	while (lo < hi)
	{
		guint mid = (lo + hi) / 2u;

		if (s->emit_cdf[mid] < u)
			lo = mid + 1u;
		else
			hi = mid;
	}
	pdf_pick = s->emit_cdf[lo] - (lo > 0 ? s->emit_cdf[lo - 1u] : 0.0f);
	if (pdf_pick <= 0.0f)
		return;
	tri = s->emit_tri[lo];
	ix = s->idx + (gsize)tri * 3u;
	ob = &s->objs[s->tri_obj[tri]];

	/* a uniform point on the triangle */
	r1 = sqrtf (rng_next (rng));
	r2 = rng_next (rng);
	for (k = 0; k < 3; k++)
		q[k] = s->pos[(gsize)ix[0] * 3u + (guint)k] * (1.0f - r1)
		     + s->pos[(gsize)ix[1] * 3u + (guint)k] * (r1 * (1.0f - r2))
		     + s->pos[(gsize)ix[2] * 3u + (guint)k] * (r1 * r2);
	offset_point (s, p, ng, o);
	for (k = 0; k < 3; k++)
		d[k] = q[k] - o[k];
	d2 = dot3 (d, d);
	dist = sqrtf (d2);
	if (dist <= 1e-6f)
		return;
	for (k = 0; k < 3; k++)
		d[k] /= dist;
	cx = dot3 (n, d);
	if (cx <= 0.0f || dot3 (ng, d) <= 0.0f)
		return;
	nl = s->tri_ng + (gsize)tri * 3u;
	cl = -dot3 (nl, d);
	if (cl <= 0.0f)
	{
		if (!(ob->flags & LRG_LIGHTMAP_BAKE_OBJECT_DOUBLE_SIDED))
			return;
		cl = -cl;
	}
	if (lrg_bake_bvh_occluded (s->bvh, o, d, 0.0f, dist * (1.0f - 1e-4f) - s->eps))
		return;
	/* E / pi = Le cos_x cos_l / (pi d^2) / pdf_area, pdf_area = pick / area */
	g = cx * cl / (BAKE_PI * MAX (d2, 1e-6f)) * s->emit_area[lo] / pdf_pick;
	for (k = 0; k < 3; k++)
		out[k] += ob->emission[k] * g;
}

/*
 * Surface data at a hit: position, shading and geometric normals facing
 * the ray's side (double-sided objects show their front to both sides).
 * Returns the object, or -1 for a single-sided back face.
 */
static gint
hit_surface (const Scene      *s,
             const gfloat     *o,
             const gfloat     *d,
             const LrgBakeHit *hit,
             gfloat           *p,
             gfloat           *n,
             gfloat           *ng)
{
	guint          tri = s->bvh_map[hit->triangle];
	guint          obj = s->tri_obj[tri];
	const guint32 *ix = s->idx + (gsize)tri * 3u;
	gfloat         w0 = 1.0f - hit->u - hit->v;
	gint           k;

	if (hit->backface && !(s->objs[obj].flags & LRG_LIGHTMAP_BAKE_OBJECT_DOUBLE_SIDED))
		return -1;
	for (k = 0; k < 3; k++)
	{
		p[k] = o[k] + d[k] * hit->t;
		n[k] = s->nrm[(gsize)ix[0] * 3u + (guint)k] * w0 + s->nrm[(gsize)ix[1] * 3u + (guint)k] * hit->u
		     + s->nrm[(gsize)ix[2] * 3u + (guint)k] * hit->v;
		ng[k] = s->tri_ng[(gsize)tri * 3u + (guint)k];
	}
	normalize3 (n);
	if (hit->backface)
	{
		for (k = 0; k < 3; k++)
		{
			n[k] = -n[k];
			ng[k] = -ng[k];
		}
	}
	if (dot3 (n, ng) <= 0.0f)
		memcpy (n, ng, 3 * sizeof (gfloat));
	return (gint)obj;
}

/*
 * Radiance arriving at @o from direction @d through up to @depth
 * surfaces: emission and direct light (baked and mixed) of every surface
 * on the path, the sky when the path escapes.  Returns FALSE when the
 * first hit is a single-sided back face (the origin is inside geometry).
 */
static gboolean
trace_path (const Scene  *s,
            const gfloat *origin,
            const gfloat *dir,
            guint         depth,
            gboolean      first_emission,
            Rng          *rng,
            gfloat       *out)
{
	gfloat o[3];
	gfloat d[3];
	gfloat thr[3] = { 1.0f, 1.0f, 1.0f };
	guint  b;
	gint   k;

	memcpy (o, origin, sizeof o);
	memcpy (d, dir, sizeof d);
	out[0] = out[1] = out[2] = 0.0f;
	for (b = 0; b < depth; b++)
	{
		LrgBakeHit      hit;
		gfloat          p[3];
		gfloat          n[3];
		gfloat          ng[3];
		gfloat          dl[3] = { 0.0f, 0.0f, 0.0f };
		const SceneObj *ob;
		gint            obj;

		if (!lrg_bake_bvh_intersect (s->bvh, o, d, 0.0f, G_MAXFLOAT, &hit))
		{
			gfloat sky[3];

			sky_lookup (s, d, sky);
			for (k = 0; k < 3; k++)
				out[k] += thr[k] * sky[k];
			return TRUE;
		}
		obj = hit_surface (s, o, d, &hit, p, n, ng);
		if (obj < 0)
			return b > 0;
		ob = &s->objs[obj];

		/* albedo x (direct light of baked and mixed lights and of emissive
		 * surfaces); an emitter seen directly only counts for probes */
		direct_light (s, p, n, ng, FALSE, rng, dl);
		emissive_direct (s, p, n, ng, rng, dl);
		for (k = 0; k < 3; k++)
			out[k] += thr[k] * ((b == 0 && first_emission ? ob->emission[k] : 0.0f) + ob->albedo[k] * dl[k]);
		if (b + 1u >= depth)
			break;

		/* continue with a cosine sample: the albedo is the whole weight */
		for (k = 0; k < 3; k++)
			thr[k] *= ob->albedo[k];
		if (MAX (thr[0], MAX (thr[1], thr[2])) <= 0.0f)
			break;
		cosine_dir (n, rng_next (rng), rng_next (rng), d);
		if (dot3 (d, ng) <= 0.0f)
		{
			gfloat f = 2.0f * dot3 (d, ng);

			for (k = 0; k < 3; k++)
				d[k] -= f * ng[k];
		}
		offset_point (s, p, ng, o);
	}
	return TRUE;
}

/*
 * The texel estimator: mean over @samples of (baked direct light + one
 * stratified cosine path).  Outputs the mean, the variance of the mean's
 * luminance and the fraction of paths that started into a back face.
 */
static void
estimate (const Scene  *s,
          const gfloat *p,
          const gfloat *n_in,
          const gfloat *ng_in,
          guint32       stream,
          guint32       index,
          gfloat       *out_rgb,
          gfloat       *out_var,
          gfloat       *out_back)
{
	Rng     rng;
	gfloat  n[3];
	gfloat  ng[3];
	gfloat  o[3];
	gdouble sum[3] = { 0.0, 0.0, 0.0 };
	gdouble l1 = 0.0;
	gdouble l2 = 0.0;
	guint   samples = MAX (s->v.samples, 1u);
	guint   cols = (guint)ceil (sqrt ((gdouble)samples));
	guint   rows = (samples + cols - 1u) / cols;
	guint   back = 0;
	guint   i;
	gint    k;

	memcpy (n, n_in, sizeof n);
	memcpy (ng, ng_in, sizeof ng);
	normalize3 (n);
	normalize3 (ng);
	if (dot3 (n, ng) <= 0.0f)
		memcpy (n, ng, sizeof n);
	offset_point (s, p, ng, o);
	rng_seed (&rng, s->v.seed, stream, index);

	for (i = 0; i < samples; i++)
	{
		gfloat v[3] = { 0.0f, 0.0f, 0.0f };
		gfloat lum;

		direct_light (s, p, n, ng, TRUE, &rng, v);
		if (s->v.bounces > 0)
		{
			gfloat d[3];
			gfloat li[3];
			gfloat u1;
			gfloat u2;

			emissive_direct (s, p, n, ng, &rng, v);
			u1 = ((gfloat)(i % cols) + rng_next (&rng)) / (gfloat)cols;
			u2 = ((gfloat)(i / cols) + rng_next (&rng)) / (gfloat)rows;

			cosine_dir (n, u1, u2, d);
			if (dot3 (d, ng) <= 0.0f)
			{
				gfloat f = 2.0f * dot3 (d, ng);

				for (k = 0; k < 3; k++)
					d[k] -= f * ng[k];
			}
			if (!trace_path (s, o, d, s->v.bounces, FALSE, &rng, li))
				back++;
			for (k = 0; k < 3; k++)
				v[k] += li[k];
		}
		for (k = 0; k < 3; k++)
			sum[k] += v[k];
		lum = luminance (v);
		l1 += lum;
		l2 += (gdouble)lum * lum;
	}
	for (k = 0; k < 3; k++)
		out_rgb[k] = (gfloat)(sum[k] / samples);
	l1 /= samples;
	*out_var = (gfloat)MAX (0.0, (l2 / samples - l1 * l1) / samples);
	*out_back = (gfloat)back / (gfloat)samples;
}

/* the 9 real L2 basis functions, the same constants as LrgEnvironment */
static void
sh_basis (const gfloat *d,
          gfloat       *y)
{
	y[0] = 0.282095f;
	y[1] = 0.488603f * d[1];
	y[2] = 0.488603f * d[2];
	y[3] = 0.488603f * d[0];
	y[4] = 1.092548f * d[0] * d[1];
	y[5] = 1.092548f * d[1] * d[2];
	y[6] = 0.315392f * (3.0f * d[2] * d[2] - 1.0f);
	y[7] = 1.092548f * d[0] * d[2];
	y[8] = 0.546274f * (d[0] * d[0] - d[1] * d[1]);
}

/*
 * One probe: stratified uniform sphere rays, radiance projected onto the
 * L2 basis, convolved with the clamped cosine and divided by pi
 * (lrg_environment_sh_project_cubemap()'s convention).  Returns FALSE
 * when too many rays hit back faces (the probe is inside geometry).
 */
static gboolean
probe_sh (const Scene  *s,
          const gfloat *p,
          guint32       index,
          gfloat       *out_sh)
{
	static const gdouble band[9] = { 1.0, 2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0,
	                                 0.25, 0.25, 0.25, 0.25, 0.25 };
	Rng     rng;
	gdouble acc[27];
	guint   samples = MAX (s->v.probe_samples, 1u);
	guint   cols = (guint)ceil (sqrt ((gdouble)samples));
	guint   rows = (samples + cols - 1u) / cols;
	guint   depth = MAX (s->v.bounces, 1u);
	guint   back = 0;
	guint   i;
	gint    j;

	memset (acc, 0, sizeof acc);
	rng_seed (&rng, s->v.seed, 0x50524f42u, index);
	for (i = 0; i < samples; i++)
	{
		gfloat u1 = ((gfloat)(i % cols) + rng_next (&rng)) / (gfloat)cols;
		gfloat u2 = ((gfloat)(i / cols) + rng_next (&rng)) / (gfloat)rows;
		gfloat z = 1.0f - 2.0f * u1;
		gfloat r = sqrtf (MAX (0.0f, 1.0f - z * z));
		gfloat ph = 2.0f * BAKE_PI * u2;
		gfloat d[3];
		gfloat li[3];
		gfloat y[9];

		d[0] = r * cosf (ph);
		d[1] = r * sinf (ph);
		d[2] = z;
		if (!trace_path (s, p, d, depth, TRUE, &rng, li))
			back++;
		sh_basis (d, y);
		for (j = 0; j < 9; j++)
		{
			acc[j * 3] += li[0] * y[j];
			acc[j * 3 + 1] += li[1] * y[j];
			acc[j * 3 + 2] += li[2] * y[j];
		}
	}
	for (j = 0; j < 27; j++)
		out_sh[j] = (gfloat)(acc[j] * (4.0 * G_PI / samples) * band[j / 3]);
	return (gfloat)back / (gfloat)samples <= PROBE_BACKFACE;
}

/* ==========================================================================
 * Scene building
 * ========================================================================== */

/*
 * Geometry: unwrap where needed, transform into world space, build the
 * BVH over contributing triangles.  @inputs are BakeInput *.
 */
static Scene *
scene_build (GPtrArray        *inputs,
             const BakeValues *v,
             const BakeLight  *lights,
             guint             n_lights,
             gfloat           *sky,
             const gfloat     *sky_flat,
             GError          **error)
{
	Scene    *s = g_new0 (Scene, 1);
	GArray   *bvh_idx;
	GArray   *bvh_map;
	guint     nv = 0;
	guint     nt = 0;
	guint     i;
	gfloat    ext;

	s->v = *v;
	s->sky = sky;
	memcpy (s->sky_flat, sky_flat, sizeof s->sky_flat);
	s->lights = g_memdup2 (lights, MAX (n_lights, 1u) * sizeof (BakeLight));
	s->n_lights = n_lights;
	s->n_objs = inputs->len;
	s->objs = g_new0 (SceneObj, MAX (inputs->len, 1u));

	/* the mesh each object bakes with */
	for (i = 0; i < inputs->len; i++)
	{
		BakeInput *in = g_ptr_array_index (inputs, i);
		SceneObj  *ob = &s->objs[i];
		LrgUvMesh *mesh = in->mesh;
		g_autoptr(LrgUvMesh) unwrapped = NULL;

		ob->guid = g_strdup (in->guid);
		ob->flags = in->flags;
		ob->scale = in->scale;
		memcpy (ob->albedo, in->albedo, sizeof ob->albedo);
		memcpy (ob->emission, in->emission, sizeof ob->emission);
		if ((in->flags & LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE) && lrg_uv_mesh_get_uv2 (mesh, NULL) == NULL)
		{
			g_autoptr(LrgUvUnwrap) unwrap = lrg_uv_unwrap_new ();
			GError *local = NULL;

			unwrapped = lrg_uv_unwrap_run (unwrap, mesh, &local);
			if (unwrapped == NULL)
			{
				g_propagate_prefixed_error (error, local, "unwrapping %s: ", in->guid);
				scene_free (s);
				return NULL;
			}
			mesh = unwrapped;
			ob->generated = TRUE;
		}
		if (lrg_uv_mesh_get_normals (mesh, NULL) == NULL)
		{
			/* a private copy gets computed normals */
			LrgUvMesh *copy = lrg_uv_mesh_new (lrg_uv_mesh_get_positions (mesh, NULL),
			                                   (gsize)lrg_uv_mesh_get_n_vertices (mesh) * 3u,
			                                   lrg_uv_mesh_get_indices (mesh, NULL),
			                                   (gsize)lrg_uv_mesh_get_n_triangles (mesh) * 3u);
			const gfloat *uv2 = lrg_uv_mesh_get_uv2 (mesh, NULL);

			if (uv2 != NULL)
				lrg_uv_mesh_set_uv2 (copy, uv2, (gsize)lrg_uv_mesh_get_n_vertices (mesh) * 2u);
			lrg_uv_mesh_compute_normals (copy);
			g_clear_object (&unwrapped);
			unwrapped = copy;
			mesh = copy;
		}
		ob->mesh = g_object_ref (mesh);
		ob->area = lrg_uv_mesh_get_surface_area (mesh, in->matrix);
		ob->uv_area = lrg_uv_mesh_get_uv2_area (mesh);
		ob->first_vert = nv;
		ob->first_tri = nt;
		ob->n_tris = lrg_uv_mesh_get_n_triangles (mesh);
		nv += lrg_uv_mesh_get_n_vertices (mesh);
		nt += ob->n_tris;
	}

	/* world-space arrays */
	s->n_verts = nv;
	s->n_tris = nt;
	s->pos = g_new (gfloat, (gsize)MAX (nv, 1u) * 3u);
	s->nrm = g_new (gfloat, (gsize)MAX (nv, 1u) * 3u);
	s->auv = g_new0 (gfloat, (gsize)MAX (nv, 1u) * 2u);
	s->idx = g_new (guint32, (gsize)MAX (nt, 1u) * 3u);
	s->tri_obj = g_new (guint32, MAX (nt, 1u));
	s->tri_ng = g_new (gfloat, (gsize)MAX (nt, 1u) * 3u);
	bvh_idx = g_array_new (FALSE, FALSE, sizeof (guint32));
	bvh_map = g_array_new (FALSE, FALSE, sizeof (guint32));
	s->bmin[0] = s->bmin[1] = s->bmin[2] = G_MAXFLOAT;
	s->bmax[0] = s->bmax[1] = s->bmax[2] = -G_MAXFLOAT;
	for (i = 0; i < s->n_objs; i++)
	{
		BakeInput     *in = g_ptr_array_index (inputs, i);
		SceneObj      *ob = &s->objs[i];
		const gfloat  *mp = lrg_uv_mesh_get_positions (ob->mesh, NULL);
		const gfloat  *mn = lrg_uv_mesh_get_normals (ob->mesh, NULL);
		const guint32 *mi = lrg_uv_mesh_get_indices (ob->mesh, NULL);
		guint          mv = lrg_uv_mesh_get_n_vertices (ob->mesh);
		gfloat         inv[16];
		gfloat         nm[16];
		gboolean       mirror;
		guint          k;
		gint           c;

		/* normals go through the inverse transpose */
		if (!lrg_math3d_invert (in->matrix, inv))
			lrg_math3d_identity (inv);
		lrg_math3d_transpose (inv, nm);
		nm[3] = nm[7] = nm[11] = 0.0f;
		nm[12] = nm[13] = nm[14] = 0.0f;
		nm[15] = 1.0f;
		{
			const gfloat *m = in->matrix;
			gfloat det = m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2])
			           + m[8] * (m[1] * m[6] - m[5] * m[2]);

			mirror = det < 0.0f;
		}
		for (k = 0; k < mv; k++)
		{
			gfloat *wp = s->pos + (gsize)(ob->first_vert + k) * 3u;
			gfloat *wn = s->nrm + (gsize)(ob->first_vert + k) * 3u;

			lrg_math3d_transform_point (in->matrix, mp + (gsize)k * 3u, wp);
			lrg_math3d_transform_direction (nm, mn + (gsize)k * 3u, wn);
			if (normalize3 (wn) <= 1e-30f)
			{
				wn[0] = 0.0f;
				wn[1] = 1.0f;
				wn[2] = 0.0f;
			}
			for (c = 0; c < 3; c++)
			{
				s->bmin[c] = MIN (s->bmin[c], wp[c]);
				s->bmax[c] = MAX (s->bmax[c], wp[c]);
			}
		}
		for (k = 0; k < ob->n_tris; k++)
		{
			guint32 *t = s->idx + (gsize)(ob->first_tri + k) * 3u;
			gfloat  *g = s->tri_ng + (gsize)(ob->first_tri + k) * 3u;
			gfloat   e1[3];
			gfloat   e2[3];
			guint32  st = ob->first_tri + k;

			/* a mirroring transform flips the winding back */
			t[0] = ob->first_vert + mi[k * 3u];
			t[1] = ob->first_vert + mi[k * 3u + (mirror ? 2u : 1u)];
			t[2] = ob->first_vert + mi[k * 3u + (mirror ? 1u : 2u)];
			for (c = 0; c < 3; c++)
			{
				e1[c] = s->pos[(gsize)t[1] * 3u + (guint)c] - s->pos[(gsize)t[0] * 3u + (guint)c];
				e2[c] = s->pos[(gsize)t[2] * 3u + (guint)c] - s->pos[(gsize)t[0] * 3u + (guint)c];
			}
			cross3 (e1, e2, g);
			if (normalize3 (g) <= 1e-30f)
			{
				g[0] = 0.0f;
				g[1] = 1.0f;
				g[2] = 0.0f;
			}
			s->tri_obj[st] = i;
			if (ob->flags & LRG_LIGHTMAP_BAKE_OBJECT_CONTRIBUTE)
			{
				g_array_append_vals (bvh_idx, t, 3);
				g_array_append_val (bvh_map, st);
			}
		}
	}
	s->has_bounds = nv > 0;
	if (!s->has_bounds)
	{
		memset (s->bmin, 0, sizeof s->bmin);
		memset (s->bmax, 0, sizeof s->bmax);
	}
	ext = MAX (MAX (s->bmax[0] - s->bmin[0], s->bmax[1] - s->bmin[1]), s->bmax[2] - s->bmin[2]);
	s->eps = MAX (ext * 2e-5f, 1e-5f);

	/* emissive contributing triangles and their sampling CDF */
	{
		GArray *et = g_array_new (FALSE, FALSE, sizeof (guint32));
		GArray *ec = g_array_new (FALSE, FALSE, sizeof (gfloat));
		GArray *ea = g_array_new (FALSE, FALSE, sizeof (gfloat));
		gdouble total = 0.0;
		guint   t;

		for (t = 0; t < nt; t++)
		{
			const SceneObj *ob = &s->objs[s->tri_obj[t]];
			gfloat          lum = luminance (ob->emission);
			gfloat          e1[3];
			gfloat          e2[3];
			gfloat          cr[3];
			gfloat          area;
			gfloat          c;
			gint            q;

			if (lum <= 0.0f || !(ob->flags & LRG_LIGHTMAP_BAKE_OBJECT_CONTRIBUTE))
				continue;
			for (q = 0; q < 3; q++)
			{
				e1[q] = s->pos[(gsize)s->idx[t * 3u + 1u] * 3u + (guint)q] - s->pos[(gsize)s->idx[t * 3u] * 3u + (guint)q];
				e2[q] = s->pos[(gsize)s->idx[t * 3u + 2u] * 3u + (guint)q] - s->pos[(gsize)s->idx[t * 3u] * 3u + (guint)q];
			}
			cross3 (e1, e2, cr);
			area = 0.5f * sqrtf (dot3 (cr, cr));
			if (area <= 1e-12f)
				continue;
			total += (gdouble)area * lum;
			c = (gfloat)total;
			g_array_append_val (et, t);
			g_array_append_val (ec, c);
			g_array_append_val (ea, area);
		}
		s->n_emit = et->len;
		if (total > 0.0)
		{
			guint q;

			for (q = 0; q < ec->len; q++)
				g_array_index (ec, gfloat, q) = (gfloat)(g_array_index (ec, gfloat, q) / total);
			g_array_index (ec, gfloat, ec->len - 1u) = 1.0f;
		}
		s->emit_tri = (guint32 *)(gpointer)g_array_free (et, FALSE);
		s->emit_cdf = (gfloat *)(gpointer)g_array_free (ec, FALSE);
		s->emit_area = (gfloat *)(gpointer)g_array_free (ea, FALSE);
	}

	s->bvh = lrg_bake_bvh_new ();
	lrg_bake_bvh_build (s->bvh, s->pos, (gsize)nv * 3u, (const guint32 *)(gpointer)bvh_idx->data,
	                    bvh_idx->len);
	g_array_unref (bvh_idx);
	s->bvh_map = (guint32 *)(gpointer)g_array_free (bvh_map, FALSE);
	return s;
}

/* ==========================================================================
 * Atlas: sizing, packing, rasterization
 * ========================================================================== */

typedef struct
{
	gfloat  p[3];
	gfloat  n[3];
	gfloat  ng[3];
	guint32 obj;
	guint32 page;
	guint32 x;
	guint32 y;
} Texel;

/* Per page working buffers. */
typedef struct
{
	gfloat  *rgb;       /* 3 per texel */
	gint32  *texel;     /* texel index or -1 */
	gint32  *owner;     /* object whose rectangle holds the texel, or -1 */
} Page;

static void
pages_free (Page  *pages,
            guint  n)
{
	guint i;

	if (pages == NULL)
		return;
	for (i = 0; i < n; i++)
	{
		g_free (pages[i].rgb);
		g_free (pages[i].texel);
		g_free (pages[i].owner);
	}
	g_free (pages);
}

/*
 * Sizes every receiver (texels_per_unit x scale over its world area per
 * uv2 area), packs the padded squares into pages (shrinking a lone page
 * to the smallest power of two that holds everything) and maps each
 * vertex's uv2 to atlas texel coordinates.
 */
static gboolean
pack_atlas (Scene   *s,
            GError **error)
{
	g_autofree guint *sizes = NULL;
	g_autofree guint *place = NULL;
	g_autofree guint *recv = NULL;
	guint             n = 0;
	guint             pad = s->v.padding;
	guint             max = s->v.max_lightmap_size;
	guint             page = max;
	guint             n_pages = 0;
	guint             i;

	sizes = g_new (guint, (gsize)MAX (s->n_objs, 1u) * 2u);
	place = g_new (guint, (gsize)MAX (s->n_objs, 1u) * 3u);
	recv = g_new (guint, MAX (s->n_objs, 1u));
	for (i = 0; i < s->n_objs; i++)
	{
		SceneObj *ob = &s->objs[i];
		gdouble   res;
		guint     r;
		guint     min_res;
		guint     max_res = max > 2u * pad + 4u ? max - 2u * pad : 4u;

		if (!(ob->flags & LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE))
			continue;
		res = ceil (sqrt (ob->area / MAX (ob->uv_area, 1e-6)) * s->v.texels_per_unit * MAX (ob->scale, 0.0f));
		/* unwrapped meshes keep their chart gaps >= 2 texels */
		min_res = ob->generated ? LRG_UV_UNWRAP_DEFAULT_RESOLUTION / MAX (LRG_UV_UNWRAP_DEFAULT_PADDING, 1) : 4u;
		r = (guint)CLAMP (res, (gdouble)MIN (min_res, max_res), (gdouble)max_res);
		ob->res = r;
		sizes[n * 2u] = r + 2u * pad;
		sizes[n * 2u + 1u] = r + 2u * pad;
		recv[n++] = i;
	}
	s->page_size = page;
	s->n_pages = 0;
	if (n == 0)
		return TRUE;

	if (!lrg_uv_unwrap_pack_rects (sizes, (gsize)n * 2u, page, LRG_LIGHTMAP3D_MAX_PAGES, place, &n_pages))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
		             "the lightmaps need more than %d pages of %u texels: lower texels-per-unit",
		             LRG_LIGHTMAP3D_MAX_PAGES, page);
		return FALSE;
	}
	/* one page: shrink it while everything still fits */
	while (n_pages == 1u && page / 2u >= 32u)
	{
		guint p2 = 0;

		if (!lrg_uv_unwrap_pack_rects (sizes, (gsize)n * 2u, page / 2u, 1, place, &p2))
			break;
		page /= 2u;
	}
	if (!lrg_uv_unwrap_pack_rects (sizes, (gsize)n * 2u, page, LRG_LIGHTMAP3D_MAX_PAGES, place, &n_pages))
		return FALSE;
	s->page_size = page;
	s->n_pages = n_pages;

	for (i = 0; i < n; i++)
	{
		SceneObj     *ob = &s->objs[recv[i]];
		const gfloat *uv2 = lrg_uv_mesh_get_uv2 (ob->mesh, NULL);
		guint         nv = lrg_uv_mesh_get_n_vertices (ob->mesh);
		guint         k;

		ob->page = place[i * 3u + 2u];
		ob->rect[0] = place[i * 3u];
		ob->rect[1] = place[i * 3u + 1u];
		ob->rect[2] = ob->res + 2u * pad;
		ob->rect[3] = ob->res + 2u * pad;
		ob->so[0] = (gfloat)ob->res / (gfloat)page;
		ob->so[1] = (gfloat)ob->res / (gfloat)page;
		ob->so[2] = (gfloat)(ob->rect[0] + pad) / (gfloat)page;
		ob->so[3] = (gfloat)(ob->rect[1] + pad) / (gfloat)page;
		ob->texel_world = (gfloat)sqrt (ob->area / (MAX (ob->uv_area, 1e-6) * (gdouble)ob->res * ob->res));
		for (k = 0; k < nv; k++)
		{
			gfloat *a = s->auv + (gsize)(ob->first_vert + k) * 2u;

			a[0] = uv2[k * 2u] * (gfloat)ob->res + (gfloat)(ob->rect[0] + pad);
			a[1] = uv2[k * 2u + 1u] * (gfloat)ob->res + (gfloat)(ob->rect[1] + pad);
		}
	}
	return TRUE;
}

/* closest point of segment a-b to c: parameter and squared distance */
static void
closest_seg (const gfloat *a,
             const gfloat *b,
             const gfloat *c,
             gfloat       *out_t,
             gfloat       *out_d2)
{
	gfloat dx = b[0] - a[0];
	gfloat dy = b[1] - a[1];
	gfloat len2 = dx * dx + dy * dy;
	gfloat t = len2 > 0.0f ? ((c[0] - a[0]) * dx + (c[1] - a[1]) * dy) / len2 : 0.0f;
	gfloat px;
	gfloat py;

	t = CLAMP (t, 0.0f, 1.0f);
	px = a[0] + dx * t - c[0];
	py = a[1] + dy * t - c[1];
	*out_t = t;
	*out_d2 = px * px + py * py;
}

/*
 * Conservative rasterization: a texel belongs to the triangle covering
 * its centre, or else to the nearest triangle passing within 0.75 texel
 * (its sample point clamped onto that triangle), so texels a bilinear
 * lookup at a chart edge reads are all traced.
 */
static Texel *
raster_atlas (Scene  *s,
              Page   *pages,
              guint  *out_n)
{
	gsize   area = (gsize)s->page_size * s->page_size;
	GArray *texels = g_array_new (FALSE, FALSE, sizeof (Texel));
	guint   pg;
	guint   i;

	for (pg = 0; pg < s->n_pages; pg++)
	{
		g_autofree gfloat *best_d = g_new (gfloat, area);
		g_autofree gint32 *best_t = g_new (gint32, area);
		g_autofree gfloat *best_b = g_new (gfloat, area * 2u);
		Page              *page = &pages[pg];
		gsize              k;

		page->rgb = g_new0 (gfloat, area * 3u);
		page->texel = g_new (gint32, area);
		page->owner = g_new (gint32, area);
		for (k = 0; k < area; k++)
		{
			best_d[k] = G_MAXFLOAT;
			best_t[k] = -1;
			page->texel[k] = -1;
			page->owner[k] = -1;
		}

		for (i = 0; i < s->n_objs; i++)
		{
			SceneObj *ob = &s->objs[i];
			guint     t;
			guint     x;
			guint     y;

			if (!(ob->flags & LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE) || ob->page != pg)
				continue;
			for (y = ob->rect[1]; y < ob->rect[1] + ob->rect[3]; y++)
				for (x = ob->rect[0]; x < ob->rect[0] + ob->rect[2]; x++)
					page->owner[(gsize)y * s->page_size + x] = (gint32)i;

			for (t = ob->first_tri; t < ob->first_tri + ob->n_tris; t++)
			{
				const gfloat *a0 = s->auv + (gsize)s->idx[t * 3u] * 2u;
				const gfloat *a1 = s->auv + (gsize)s->idx[t * 3u + 1u] * 2u;
				const gfloat *a2 = s->auv + (gsize)s->idx[t * 3u + 2u] * 2u;
				gfloat v0[2];
				gfloat v1[2];
				gfloat d00;
				gfloat d01;
				gfloat d11;
				gfloat den;
				gint   x0, y0, x1, y1, xx, yy;

				v0[0] = a1[0] - a0[0];
				v0[1] = a1[1] - a0[1];
				v1[0] = a2[0] - a0[0];
				v1[1] = a2[1] - a0[1];
				d00 = v0[0] * v0[0] + v0[1] * v0[1];
				d01 = v0[0] * v1[0] + v0[1] * v1[1];
				d11 = v1[0] * v1[0] + v1[1] * v1[1];
				den = d00 * d11 - d01 * d01;
				if (fabsf (den) < 1e-12f)
					continue;

				x0 = (gint)floorf (MIN (a0[0], MIN (a1[0], a2[0])) - 1.0f);
				y0 = (gint)floorf (MIN (a0[1], MIN (a1[1], a2[1])) - 1.0f);
				x1 = (gint)ceilf (MAX (a0[0], MAX (a1[0], a2[0])) + 1.0f);
				y1 = (gint)ceilf (MAX (a0[1], MAX (a1[1], a2[1])) + 1.0f);
				x0 = MAX (x0, (gint)ob->rect[0]);
				y0 = MAX (y0, (gint)ob->rect[1]);
				x1 = MIN (x1, (gint)(ob->rect[0] + ob->rect[2]) - 1);
				y1 = MIN (y1, (gint)(ob->rect[1] + ob->rect[3]) - 1);
				for (yy = y0; yy <= y1; yy++)
				{
					for (xx = x0; xx <= x1; xx++)
					{
						gfloat c[2];
						gfloat v2[2];
						gfloat bv;
						gfloat bw;
						gfloat bu;
						gfloat dist;
						gsize  at = (gsize)yy * s->page_size + (gsize)xx;

						c[0] = (gfloat)xx + 0.5f;
						c[1] = (gfloat)yy + 0.5f;
						v2[0] = c[0] - a0[0];
						v2[1] = c[1] - a0[1];
						bv = (d11 * (v2[0] * v0[0] + v2[1] * v0[1]) - d01 * (v2[0] * v1[0] + v2[1] * v1[1])) / den;
						bw = (d00 * (v2[0] * v1[0] + v2[1] * v1[1]) - d01 * (v2[0] * v0[0] + v2[1] * v0[1])) / den;
						bu = 1.0f - bv - bw;
						if (bu >= -1e-5f && bv >= -1e-5f && bw >= -1e-5f)
							dist = 0.0f;
						else
						{
							/* nearest point on the three edges */
							gfloat tt;
							gfloat d2;
							gfloat best = G_MAXFLOAT;

							closest_seg (a0, a1, c, &tt, &d2);
							if (d2 < best)
							{
								best = d2;
								bu = 1.0f - tt;
								bv = tt;
								bw = 0.0f;
							}
							closest_seg (a1, a2, c, &tt, &d2);
							if (d2 < best)
							{
								best = d2;
								bu = 0.0f;
								bv = 1.0f - tt;
								bw = tt;
							}
							closest_seg (a2, a0, c, &tt, &d2);
							if (d2 < best)
							{
								best = d2;
								bu = tt;
								bv = 0.0f;
								bw = 1.0f - tt;
							}
							dist = sqrtf (best);
							if (dist >= 0.75f)
								continue;
						}
						if (dist < best_d[at])
						{
							best_d[at] = dist;
							best_t[at] = (gint32)t;
							best_b[at * 2u] = CLAMP (bv, 0.0f, 1.0f);
							best_b[at * 2u + 1u] = CLAMP (bw, 0.0f, 1.0f);
						}
					}
				}
			}
		}

		/* one texel record per covered texel, in page / row order */
		for (k = 0; k < area; k++)
		{
			Texel          tx;
			guint          t;
			const guint32 *ix;
			gfloat         bv;
			gfloat         bw;
			gfloat         bu;
			gint           c;

			if (best_t[k] < 0)
				continue;
			t = (guint)best_t[k];
			ix = s->idx + (gsize)t * 3u;
			bv = best_b[k * 2u];
			bw = best_b[k * 2u + 1u];
			if (bv + bw > 1.0f)
			{
				gfloat sum = bv + bw;

				bv /= sum;
				bw /= sum;
			}
			bu = 1.0f - bv - bw;
			for (c = 0; c < 3; c++)
			{
				tx.p[c] = s->pos[(gsize)ix[0] * 3u + (guint)c] * bu + s->pos[(gsize)ix[1] * 3u + (guint)c] * bv
				        + s->pos[(gsize)ix[2] * 3u + (guint)c] * bw;
				tx.n[c] = s->nrm[(gsize)ix[0] * 3u + (guint)c] * bu + s->nrm[(gsize)ix[1] * 3u + (guint)c] * bv
				        + s->nrm[(gsize)ix[2] * 3u + (guint)c] * bw;
				tx.ng[c] = s->tri_ng[(gsize)t * 3u + (guint)c];
			}
			if (normalize3 (tx.n) <= 1e-30f)
				memcpy (tx.n, tx.ng, sizeof tx.n);
			tx.obj = s->tri_obj[t];
			tx.page = pg;
			tx.x = (guint32)(k % s->page_size);
			tx.y = (guint32)(k / s->page_size);
			page->texel[k] = (gint32)texels->len;
			g_array_append_val (texels, tx);
		}
	}
	*out_n = texels->len;
	return (Texel *)(gpointer)g_array_free (texels, FALSE);
}

/* ==========================================================================
 * Post-processing
 * ========================================================================== */

/*
 * Edge-preserving a-trous wavelet filter (Dammertz et al. 2010, with the
 * variance-guided luminance weight of SVGF): 4 passes of a 5 x 5 B3
 * kernel at strides 1, 2, 4, 8 texels, weighted by normal similarity,
 * world distance and luminance difference against the texel's standard
 * deviation; texels of other objects and texels inside geometry are
 * never mixed in.
 */
static void
denoise (const Scene *s,
         const Page  *pages,
         const Texel *texels,
         guint        n_texels,
         gfloat      *rgb,
         gfloat      *var,
         const guint8 *invalid)
{
	static const gfloat h[5] = { 1.0f / 16.0f, 1.0f / 4.0f, 3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f };
	g_autofree gfloat *out = g_new (gfloat, (gsize)MAX (n_texels, 1u) * 3u);
	g_autofree gfloat *vout = g_new (gfloat, MAX (n_texels, 1u));
	gint               it;
	guint              i;

	g_autofree gfloat *vf = g_new (gfloat, MAX (n_texels, 1u));

	for (it = 0; it < DENOISE_ITERS; it++)
	{
		gint step = 1 << it;

		/*
		 * The luminance weight uses the variance blurred over the 3 x 3
		 * neighbourhood (as SVGF does): a texel whose few samples all
		 * missed a small bright emitter has variance 0 and would
		 * otherwise refuse every neighbour and stay a dark speckle.
		 */
		for (i = 0; i < n_texels; i++)
		{
			const Texel *ti = &texels[i];
			const Page  *pg = &pages[ti->page];
			gdouble      acc = 0.0;
			guint        cnt = 0;
			gint         dx;
			gint         dy;

			for (dy = -1; dy <= 1; dy++)
			{
				for (dx = -1; dx <= 1; dx++)
				{
					gint   x = (gint)ti->x + dx;
					gint   y = (gint)ti->y + dy;
					gint32 j;

					if (x < 0 || y < 0 || x >= (gint)s->page_size || y >= (gint)s->page_size)
						continue;
					j = pg->texel[(gsize)y * s->page_size + (gsize)x];
					if (j < 0 || invalid[j] || texels[j].obj != ti->obj)
						continue;
					acc += var[j];
					cnt++;
				}
			}
			vf[i] = cnt > 0 ? (gfloat)(acc / cnt) : var[i];
		}

		for (i = 0; i < n_texels; i++)
		{
			const Texel *ti = &texels[i];
			const Page  *pg = &pages[ti->page];
			gfloat       li = luminance (rgb + (gsize)i * 3u);
			gfloat       sig = 4.0f * sqrtf (MAX (vf[i], 0.0f)) + 1e-3f * (1.0f + li);
			gfloat       sp = 2.0f * (gfloat)step * MAX (s->objs[ti->obj].texel_world, 1e-6f);
			gdouble      sum[3] = { 0.0, 0.0, 0.0 };
			gdouble      wsum = 0.0;
			gdouble      vsum = 0.0;
			gint         dx;
			gint         dy;

			if (invalid[i])
			{
				memcpy (out + (gsize)i * 3u, rgb + (gsize)i * 3u, 3 * sizeof (gfloat));
				vout[i] = var[i];
				continue;
			}
			for (dy = -2; dy <= 2; dy++)
			{
				gint y = (gint)ti->y + dy * step;

				if (y < 0 || y >= (gint)s->page_size)
					continue;
				for (dx = -2; dx <= 2; dx++)
				{
					gint         x = (gint)ti->x + dx * step;
					gint32       j;
					const Texel *tj;
					gfloat       dp[3];
					gfloat       wn;
					gfloat       w;

					if (x < 0 || x >= (gint)s->page_size)
						continue;
					j = pg->texel[(gsize)y * s->page_size + (gsize)x];
					if (j < 0 || invalid[j])
						continue;
					tj = &texels[j];
					if (tj->obj != ti->obj)
						continue;
					wn = MAX (0.0f, dot3 (ti->n, tj->n));
					wn = wn * wn;
					wn = wn * wn;
					wn = wn * wn;
					wn = wn * wn;
					wn = wn * wn;   /* ^32 */
					dp[0] = ti->p[0] - tj->p[0];
					dp[1] = ti->p[1] - tj->p[1];
					dp[2] = ti->p[2] - tj->p[2];
					w = h[dx + 2] * h[dy + 2] * wn * expf (-dot3 (dp, dp) / (2.0f * sp * sp))
					    * expf (-fabsf (li - luminance (rgb + (gsize)j * 3u)) / sig);
					sum[0] += w * rgb[(gsize)j * 3u];
					sum[1] += w * rgb[(gsize)j * 3u + 1u];
					sum[2] += w * rgb[(gsize)j * 3u + 2u];
					wsum += w;
					vsum += (gdouble)w * w * var[j];
				}
			}
			if (wsum > 0.0)
			{
				out[(gsize)i * 3u] = (gfloat)(sum[0] / wsum);
				out[(gsize)i * 3u + 1u] = (gfloat)(sum[1] / wsum);
				out[(gsize)i * 3u + 2u] = (gfloat)(sum[2] / wsum);
				vout[i] = (gfloat)(vsum / (wsum * wsum));
			}
			else
			{
				memcpy (out + (gsize)i * 3u, rgb + (gsize)i * 3u, 3 * sizeof (gfloat));
				vout[i] = var[i];
			}
		}
		memcpy (rgb, out, (gsize)n_texels * 3u * sizeof (gfloat));
		memcpy (var, vout, (gsize)n_texels * sizeof (gfloat));
	}
}

/*
 * Fills every texel of an object's rectangle that has no valid value
 * (gutters, texels inside geometry) from the mean of its valid
 * 8-neighbours of the same object, ring by ring.
 */
static void
dilate (const Scene *s,
        Page        *pg,
        guint8      *filled)
{
	gsize              area = (gsize)s->page_size * s->page_size;
	g_autofree guint8 *next = g_new (guint8, area);
	gint               pass;

	for (pass = 0; pass < 256; pass++)
	{
		gboolean changed = FALSE;
		gsize    k;

		memcpy (next, filled, area);
		for (k = 0; k < area; k++)
		{
			gint    x = (gint)(k % s->page_size);
			gint    y = (gint)(k / s->page_size);
			gint32  own = pg->owner[k];
			gfloat  acc[3] = { 0.0f, 0.0f, 0.0f };
			gint    cnt = 0;
			gint    dx;
			gint    dy;

			if (filled[k] || own < 0)
				continue;
			for (dy = -1; dy <= 1; dy++)
			{
				for (dx = -1; dx <= 1; dx++)
				{
					gint  xx = x + dx;
					gint  yy = y + dy;
					gsize j;

					if ((dx == 0 && dy == 0) || xx < 0 || yy < 0 || xx >= (gint)s->page_size
					    || yy >= (gint)s->page_size)
						continue;
					j = (gsize)yy * s->page_size + (gsize)xx;
					if (!filled[j] || pg->owner[j] != own)
						continue;
					acc[0] += pg->rgb[j * 3u];
					acc[1] += pg->rgb[j * 3u + 1u];
					acc[2] += pg->rgb[j * 3u + 2u];
					cnt++;
				}
			}
			if (cnt > 0)
			{
				pg->rgb[k * 3u] = acc[0] / (gfloat)cnt;
				pg->rgb[k * 3u + 1u] = acc[1] / (gfloat)cnt;
				pg->rgb[k * 3u + 2u] = acc[2] / (gfloat)cnt;
				next[k] = 1;
				changed = TRUE;
			}
		}
		/* newly filled texels only become sources on the next ring */
		memcpy (filled, next, area);
		if (!changed)
			break;
	}
}

/* bilinear taps of a texel-space point: indices and weights (-1 = none) */
static void
bilinear_taps (const Scene  *s,
               const Page   *pg,
               gint32        obj,
               const gfloat *c,
               gsize        *idx,
               gfloat       *w)
{
	gfloat fx = c[0] - 0.5f;
	gfloat fy = c[1] - 0.5f;
	gint   x0 = (gint)floorf (fx);
	gint   y0 = (gint)floorf (fy);
	gfloat ax = fx - (gfloat)x0;
	gfloat ay = fy - (gfloat)y0;
	gint   k;

	for (k = 0; k < 4; k++)
	{
		gint x = x0 + (k & 1);
		gint y = y0 + (k >> 1);

		w[k] = ((k & 1) ? ax : 1.0f - ax) * ((k >> 1) ? ay : 1.0f - ay);
		if (x < 0 || y < 0 || x >= (gint)s->page_size || y >= (gint)s->page_size)
		{
			idx[k] = G_MAXSIZE;
			continue;
		}
		idx[k] = (gsize)y * s->page_size + (gsize)x;
		if (pg->owner[idx[k]] != obj)
			idx[k] = G_MAXSIZE;
	}
}

/*
 * Seam stitching: along every edge two triangles share in space but not
 * in uv2, nudge the texels under both sides (Gauss-Seidel, half steps)
 * until bilinear lookups on either side agree, so no line shows where
 * charts meet.
 */
static void
stitch_seams (const Scene *s,
              Page        *pages,
              guint        obj)
{
	const SceneObj *ob = &s->objs[obj];
	g_autoptr(GHashTable) edges = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
	g_autoptr(GHashTable) weld = g_hash_table_new_full (g_bytes_hash, g_bytes_equal,
	                                                    (GDestroyNotify)g_bytes_unref, NULL);
	g_autoptr(GArray) seams = g_array_new (FALSE, FALSE, sizeof (gfloat));
	g_autofree guint32 *canon = NULL;
	Page   *pg = &pages[ob->page];
	guint   nv = lrg_uv_mesh_get_n_vertices (ob->mesh);
	gfloat  cell;
	guint   v;
	guint   t;
	gint    it;

	/* weld the object's vertices by world position */
	cell = MAX (s->eps * 0.5f, 1e-7f);
	canon = g_new (guint32, MAX (nv, 1u));
	for (v = 0; v < nv; v++)
	{
		const gfloat *p = s->pos + (gsize)(ob->first_vert + v) * 3u;
		gint64        q[3];
		GBytes       *key;
		gpointer      found;
		gint          c;

		for (c = 0; c < 3; c++)
			q[c] = (gint64)floor ((gdouble)p[c] / cell + 0.5);
		key = g_bytes_new (q, sizeof q);
		if (g_hash_table_lookup_extended (weld, key, NULL, &found))
		{
			canon[v] = GPOINTER_TO_UINT (found);
			g_bytes_unref (key);
		}
		else
		{
			canon[v] = v;
			g_hash_table_insert (weld, key, GUINT_TO_POINTER (v));
		}
	}

	/* seam edges: (a0, a1) on one side, (b0, b1) on the other */
	for (t = ob->first_tri; t < ob->first_tri + ob->n_tris; t++)
	{
		gint e;

		for (e = 0; e < 3; e++)
		{
			guint32  va = s->idx[t * 3u + (guint)e];
			guint32  vb = s->idx[t * 3u + (guint)((e + 1) % 3)];
			guint32  ca = canon[va - ob->first_vert];
			guint32  cb = canon[vb - ob->first_vert];
			gint64   key;
			gpointer found;

			if (ca == cb)
				continue;
			key = ((gint64)MIN (ca, cb) << 32) | (gint64)MAX (ca, cb);
			if (g_hash_table_lookup_extended (edges, &key, NULL, &found))
			{
				guint   packed = GPOINTER_TO_UINT (found);
				guint32 ot;
				gint    oe;
				guint32 oa;
				guint32 ob2;
				const gfloat *pa0;
				const gfloat *pa1;
				const gfloat *pb0;
				const gfloat *pb1;
				gfloat  rec[8];

				if (packed == 0)
					continue;
				ot = (packed - 1u) / 3u;
				oe = (gint)((packed - 1u) % 3u);
				oa = s->idx[ot * 3u + (guint)oe];
				ob2 = s->idx[ot * 3u + (guint)((oe + 1) % 3)];
				/* match the other side's ends to ours */
				if (canon[oa - ob->first_vert] != ca)
				{
					guint32 tmp = oa;

					oa = ob2;
					ob2 = tmp;
				}
				pa0 = s->auv + (gsize)va * 2u;
				pa1 = s->auv + (gsize)vb * 2u;
				pb0 = s->auv + (gsize)oa * 2u;
				pb1 = s->auv + (gsize)ob2 * 2u;
				if (fabsf (pa0[0] - pb0[0]) + fabsf (pa0[1] - pb0[1]) + fabsf (pa1[0] - pb1[0])
				    + fabsf (pa1[1] - pb1[1]) > 1e-3f)
				{
					memcpy (rec, pa0, 2 * sizeof (gfloat));
					memcpy (rec + 2, pa1, 2 * sizeof (gfloat));
					memcpy (rec + 4, pb0, 2 * sizeof (gfloat));
					memcpy (rec + 6, pb1, 2 * sizeof (gfloat));
					g_array_append_vals (seams, rec, 8);
				}
				g_hash_table_replace (edges, g_memdup2 (&key, sizeof key), GUINT_TO_POINTER (0u));
			}
			else
				g_hash_table_insert (edges, g_memdup2 (&key, sizeof key),
				                     GUINT_TO_POINTER ((t * 3u + (guint)e) + 1u));
		}
	}

	for (it = 0; it < SEAM_ITERS; it++)
	{
		guint r;

		for (r = 0; r < seams->len / 8u; r++)
		{
			const gfloat *rec = &g_array_index (seams, gfloat, r * 8u);
			gfloat la = hypotf (rec[2] - rec[0], rec[3] - rec[1]);
			gfloat lb = hypotf (rec[6] - rec[4], rec[7] - rec[5]);
			guint  ns = (guint)ceilf (MAX (la, lb) * 2.0f) + 1u;
			guint  k;

			for (k = 0; k < ns; k++)
			{
				gfloat tt = ((gfloat)k + 0.5f) / (gfloat)ns;
				gfloat ca[2];
				gfloat cb[2];
				gsize  ia[4];
				gsize  ib[4];
				gfloat wa[4];
				gfloat wb[4];
				gfloat va[3] = { 0.0f, 0.0f, 0.0f };
				gfloat vb[3] = { 0.0f, 0.0f, 0.0f };
				gfloat sa = 0.0f;
				gfloat sb = 0.0f;
				gfloat qa = 0.0f;
				gfloat qb = 0.0f;
				gint   j;
				gint   c;

				ca[0] = rec[0] + (rec[2] - rec[0]) * tt;
				ca[1] = rec[1] + (rec[3] - rec[1]) * tt;
				cb[0] = rec[4] + (rec[6] - rec[4]) * tt;
				cb[1] = rec[5] + (rec[7] - rec[5]) * tt;
				bilinear_taps (s, pg, (gint32)obj, ca, ia, wa);
				bilinear_taps (s, pg, (gint32)obj, cb, ib, wb);
				for (j = 0; j < 4; j++)
				{
					if (ia[j] != G_MAXSIZE)
					{
						for (c = 0; c < 3; c++)
							va[c] += wa[j] * pg->rgb[ia[j] * 3u + (guint)c];
						sa += wa[j];
						qa += wa[j] * wa[j];
					}
					if (ib[j] != G_MAXSIZE)
					{
						for (c = 0; c < 3; c++)
							vb[c] += wb[j] * pg->rgb[ib[j] * 3u + (guint)c];
						sb += wb[j];
						qb += wb[j] * wb[j];
					}
				}
				if (sa <= 0.0f || sb <= 0.0f)
					continue;
				for (c = 0; c < 3; c++)
				{
					gfloat a = va[c] / sa;
					gfloat b = vb[c] / sb;
					gfloat m = 0.5f * (a + b);

					/* half of the step that would make each side hit the mean */
					for (j = 0; j < 4; j++)
					{
						if (ia[j] != G_MAXSIZE)
							pg->rgb[ia[j] * 3u + (guint)c] += 0.5f * (m - a) * wa[j] * sa / qa;
						if (ib[j] != G_MAXSIZE)
							pg->rgb[ib[j] * 3u + (guint)c] += 0.5f * (m - b) * wb[j] * sb / qb;
					}
				}
			}
		}
	}
	/* stitching never makes light negative */
	{
		gsize k;
		gsize area = (gsize)s->page_size * s->page_size;

		for (k = 0; k < area; k++)
		{
			if (pg->owner[k] != (gint32)obj)
				continue;
			pg->rgb[k * 3u] = MAX (pg->rgb[k * 3u], 0.0f);
			pg->rgb[k * 3u + 1u] = MAX (pg->rgb[k * 3u + 1u], 0.0f);
			pg->rgb[k * 3u + 2u] = MAX (pg->rgb[k * 3u + 2u], 0.0f);
		}
	}
}

/* ==========================================================================
 * Driver
 * ========================================================================== */

typedef enum
{
	WORK_TEXELS,
	WORK_PROBES
} WorkKind;

typedef struct _BakeJob BakeJob;

struct _BakeJob
{
	/* inputs (owned) */
	GPtrArray        *inputs;
	BakeLight        *lights;
	guint             n_lights;
	ProbeVolume      *volumes;
	guint             n_volumes;
	BakeValues        v;
	gfloat           *sky;
	gfloat            sky_flat[3];

	/* where progress goes */
	LrgLightmapBaker *baker;
	GMainContext     *context;
	gboolean          sync;
	GCancellable     *cancellable;
	guint             serial;
	gint              last_permille;
	gint              last_phase;

	/* thread pool rendezvous */
	GMutex            lock;
	GCond             cond;
	guint             done;

	/* work */
	Scene            *scene;
	Page             *pages;
	Texel            *texels;
	guint             n_texels;
	gfloat           *t_rgb;
	gfloat           *t_var;
	guint8           *t_invalid;
	gfloat           *probe_pos;
	gfloat           *probe_sh;
	guint8           *probe_valid;
	guint             n_probes;

	/* results */
	LrgLightmap3D    *lightmap;
	GPtrArray        *grids;
	gdouble           seconds;
};

typedef struct
{
	BakeJob  *job;
	WorkKind  kind;
	guint     start;
	guint     end;
} WorkItem;

static void
bake_job_free (gpointer data)
{
	BakeJob *job = data;

	if (job == NULL)
		return;
	g_clear_pointer (&job->inputs, g_ptr_array_unref);
	g_free (job->lights);
	g_free (job->volumes);
	g_free (job->sky);
	g_clear_object (&job->baker);
	g_clear_pointer (&job->context, g_main_context_unref);
	g_clear_object (&job->cancellable);
	g_mutex_clear (&job->lock);
	g_cond_clear (&job->cond);
	/* the pages count lives in the scene: free them first */
	if (job->pages != NULL)
		pages_free (job->pages, job->scene != NULL ? job->scene->n_pages : 0);
	scene_free (job->scene);
	g_free (job->texels);
	g_free (job->t_rgb);
	g_free (job->t_var);
	g_free (job->t_invalid);
	g_free (job->probe_pos);
	g_free (job->probe_sh);
	g_free (job->probe_valid);
	g_clear_object (&job->lightmap);
	g_clear_pointer (&job->grids, g_ptr_array_unref);
	g_free (job);
}

/* --- the baker object (declared here for the progress plumbing) --------- */

struct _LrgLightmapBaker
{
	GObject                  parent_instance;

	LrgLightmapBakeSettings *settings;
	GPtrArray               *inputs;     /* BakeInput * */
	GArray                  *lights;     /* BakeLight */
	GArray                  *volumes;    /* ProbeVolume */
	LrgEnvironment          *environment;
	gfloat                   sky_flat[3];

	gboolean                 running;
	guint                    serial;
	gdouble                  last_fraction;

	/* results of the last bake */
	LrgLightmap3D           *lightmap;
	GPtrArray               *grids;
	gfloat                 **page_rgb;
	guint                    n_pages;
	guint                    page_size;
	Scene                   *scene;      /* for evaluate() and object meshes */
	gboolean                 scene_dirty;
	gdouble                  bake_time;
	guint                    n_texels;
};

G_DEFINE_FINAL_TYPE (LrgLightmapBaker, lrg_lightmap_baker, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_SETTINGS,
	PROP_RUNNING,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_PROGRESS,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* main-thread side of a progress report */
typedef struct
{
	LrgLightmapBaker     *baker;
	guint                 serial;
	LrgLightmapBakePhase  phase;
	gdouble               fraction;
} ProgressMsg;

static void
emit_progress (LrgLightmapBaker     *self,
               guint                 serial,
               LrgLightmapBakePhase  phase,
               gdouble               fraction)
{
	/* stale (an older bake) or out of order: drop, so it stays monotonic */
	if (serial != self->serial || fraction < self->last_fraction)
		return;
	self->last_fraction = fraction;
	g_signal_emit (self, signals[SIGNAL_PROGRESS], 0, phase, fraction);
}

static gboolean
progress_idle (gpointer data)
{
	ProgressMsg *m = data;

	emit_progress (m->baker, m->serial, m->phase, m->fraction);
	return G_SOURCE_REMOVE;
}

static void
progress_msg_free (gpointer data)
{
	ProgressMsg *m = data;

	g_object_unref (m->baker);
	g_free (m);
}

/* Reports from the driver thread (rate limited to 0.2% steps). */
static void
report (BakeJob              *job,
        LrgLightmapBakePhase  phase,
        gdouble               fraction)
{
	gint permille = (gint)(CLAMP (fraction, 0.0, 1.0) * 500.0);

	if (permille <= job->last_permille && (gint)phase == job->last_phase)
		return;
	job->last_permille = MAX (permille, job->last_permille);
	job->last_phase = (gint)phase;
	if (job->sync)
		emit_progress (job->baker, job->serial, phase, fraction);
	else
	{
		ProgressMsg *m = g_new (ProgressMsg, 1);

		m->baker = g_object_ref (job->baker);
		m->serial = job->serial;
		m->phase = phase;
		m->fraction = fraction;
		g_main_context_invoke_full (job->context, G_PRIORITY_DEFAULT, progress_idle, m, progress_msg_free);
	}
}

/* --- pool workers ----------------------------------------------------- */

static void
work_texels (BakeJob *job,
             guint    start,
             guint    end)
{
	const Scene *s = job->scene;
	guint        i;

	for (i = start; i < end; i++)
	{
		const Texel *t = &job->texels[i];
		gfloat       back;

		if (g_cancellable_is_cancelled (job->cancellable))
			return;
		estimate (s, t->p, t->n, t->ng, 0x54455845u, i, job->t_rgb + (gsize)i * 3u, &job->t_var[i], &back);
		job->t_invalid[i] = back > BACKFACE_LIMIT;
	}
}

static void
work_probes (BakeJob *job,
             guint    start,
             guint    end)
{
	guint i;

	for (i = start; i < end; i++)
	{
		if (g_cancellable_is_cancelled (job->cancellable))
			return;
		job->probe_valid[i] = probe_sh (job->scene, job->probe_pos + (gsize)i * 3u, i,
		                                job->probe_sh + (gsize)i * 27u);
	}
}

static void
pool_func (gpointer data,
           gpointer user_data)
{
	WorkItem *w = data;
	BakeJob  *job = w->job;

	if (!g_cancellable_is_cancelled (job->cancellable))
	{
		if (w->kind == WORK_TEXELS)
			work_texels (job, w->start, w->end);
		else
			work_probes (job, w->start, w->end);
	}
	g_mutex_lock (&job->lock);
	job->done += w->end - w->start;
	g_cond_signal (&job->cond);
	g_mutex_unlock (&job->lock);
}

/*
 * Splits [0, total) into chunks on the pool and waits, reporting
 * progress between @f0 and @f1 every 50 ms.
 */
static void
run_parallel (BakeJob              *job,
              GThreadPool          *pool,
              WorkKind              kind,
              guint                 total,
              guint                 n_threads,
              LrgLightmapBakePhase  phase,
              gdouble               f0,
              gdouble               f1)
{
	g_autofree WorkItem *items = NULL;
	guint                chunk;
	guint                n_items;
	guint                i;

	if (total == 0)
		return;
	chunk = CLAMP (total / (n_threads * 32u), kind == WORK_TEXELS ? 16u : 1u, 1024u);
	n_items = (total + chunk - 1u) / chunk;
	items = g_new (WorkItem, n_items);
	job->done = 0;
	for (i = 0; i < n_items; i++)
	{
		items[i].job = job;
		items[i].kind = kind;
		items[i].start = i * chunk;
		items[i].end = MIN (total, (i + 1u) * chunk);
		g_thread_pool_push (pool, &items[i], NULL);
	}

	g_mutex_lock (&job->lock);
	while (job->done < total)
	{
		gint64 until = g_get_monotonic_time () + 50 * G_TIME_SPAN_MILLISECOND;
		guint  done;

		g_cond_wait_until (&job->cond, &job->lock, until);
		done = job->done;
		g_mutex_unlock (&job->lock);
		report (job, phase, f0 + (f1 - f0) * (gdouble)done / (gdouble)total);
		g_mutex_lock (&job->lock);
	}
	g_mutex_unlock (&job->lock);
}

static guint
axis_count (gfloat extent,
            gfloat spacing)
{
	guint n = (guint)floorf (extent / MAX (spacing, 1e-3f)) + 1u;

	return CLAMP (n, 2u, (guint)LRG_PROBE_GRID_MAX_RESOLUTION);
}

static gboolean
check_cancel (BakeJob  *job,
              GError  **error)
{
	return g_cancellable_set_error_if_cancelled (job->cancellable, error);
}

/* The whole bake, on the driver thread. */
static gboolean
run_bake (BakeJob  *job,
          GError  **error)
{
	GThreadPool *pool;
	Scene       *s;
	gint64       t0 = g_get_monotonic_time ();
	guint        n_threads = job->v.threads > 0 ? job->v.threads : (guint)g_get_num_processors ();
	ProbeVolume *vols;
	guint        n_vols;
	ProbeVolume  autovol;
	gdouble      l_end;
	gsize        area;
	guint        i;

	report (job, LRG_LIGHTMAP_BAKE_PHASE_PREPARE, 0.0);

	/* 1. prepare */
	s = scene_build (job->inputs, &job->v, job->lights, job->n_lights, job->sky, job->sky_flat, error);
	job->sky = NULL;   /* the scene owns it now (or freed it on failure) */
	if (s == NULL)
		return FALSE;
	job->scene = s;
	if (!pack_atlas (s, error))
		return FALSE;
	job->pages = g_new0 (Page, MAX (s->n_pages, 1u));
	job->texels = raster_atlas (s, job->pages, &job->n_texels);
	job->t_rgb = g_new0 (gfloat, (gsize)MAX (job->n_texels, 1u) * 3u);
	job->t_var = g_new0 (gfloat, MAX (job->n_texels, 1u));
	job->t_invalid = g_new0 (guint8, MAX (job->n_texels, 1u));
	if (check_cancel (job, error))
		return FALSE;

	/* the probe volumes: explicit, or one around the static geometry */
	vols = job->volumes;
	n_vols = job->n_volumes;
	if (n_vols == 0 && job->v.auto_probe_volume && s->has_bounds)
	{
		gint k;

		for (k = 0; k < 3; k++)
		{
			autovol.min[k] = s->bmin[k];
			autovol.max[k] = MAX (s->bmax[k], s->bmin[k] + 1e-3f);
			autovol.n[k] = 0;
		}
		vols = &autovol;
		n_vols = 1;
	}
	l_end = n_vols > 0 ? 0.80 : 0.95;
	report (job, LRG_LIGHTMAP_BAKE_PHASE_PREPARE, 0.05);
	g_debug ("lightmap bake: %s done at %.3f s", "prepare", (gdouble)(g_get_monotonic_time () - t0) / (gdouble)G_TIME_SPAN_SECOND);

	pool = g_thread_pool_new (pool_func, job, (gint)MAX (n_threads, 1u), TRUE, error);
	if (pool == NULL)
		return FALSE;

	/* 2. lightmap texels */
	run_parallel (job, pool, WORK_TEXELS, job->n_texels, n_threads, LRG_LIGHTMAP_BAKE_PHASE_LIGHTMAPS,
	              0.05, l_end);
	if (check_cancel (job, error))
	{
		g_thread_pool_free (pool, TRUE, TRUE);
		return FALSE;
	}

	/* 3. post-processing */
	report (job, LRG_LIGHTMAP_BAKE_PHASE_POSTPROCESS, l_end);
	g_debug ("lightmap bake: %s done at %.3f s", "trace", (gdouble)(g_get_monotonic_time () - t0) / (gdouble)G_TIME_SPAN_SECOND);
	if (job->v.denoise)
		denoise (s, job->pages, job->texels, job->n_texels, job->t_rgb, job->t_var, job->t_invalid);
	area = (gsize)s->page_size * s->page_size;
	for (i = 0; i < s->n_pages; i++)
	{
		g_autofree guint8 *filled = g_new0 (guint8, area);
		Page              *pg = &job->pages[i];
		gsize              k;

		for (k = 0; k < area; k++)
		{
			gint32 t = pg->texel[k];

			if (t >= 0 && !job->t_invalid[t])
			{
				memcpy (pg->rgb + k * 3u, job->t_rgb + (gsize)t * 3u, 3 * sizeof (gfloat));
				filled[k] = 1;
			}
		}
		dilate (s, pg, filled);
	}
	g_debug ("lightmap bake: %s done at %.3f s", "denoise+dilate", (gdouble)(g_get_monotonic_time () - t0) / (gdouble)G_TIME_SPAN_SECOND);
	for (i = 0; i < s->n_objs; i++)
		if ((s->objs[i].flags & LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE) && s->objs[i].n_tris > 0)
			stitch_seams (s, job->pages, i);
	report (job, LRG_LIGHTMAP_BAKE_PHASE_POSTPROCESS, l_end + 0.05);
	g_debug ("lightmap bake: %s done at %.3f s", "seams", (gdouble)(g_get_monotonic_time () - t0) / (gdouble)G_TIME_SPAN_SECOND);

	/* 4. probes */
	job->grids = g_ptr_array_new_with_free_func (g_object_unref);
	if (n_vols > 0)
	{
		guint v;
		guint at = 0;

		for (v = 0; v < n_vols; v++)
		{
			ProbeVolume  *pv = &vols[v];
			LrgProbeGrid *grid;
			guint         n[3];
			gint          k;

			for (k = 0; k < 3; k++)
				n[k] = pv->n[k] > 0 ? MIN (pv->n[k], (guint)LRG_PROBE_GRID_MAX_RESOLUTION)
				                    : axis_count (pv->max[k] - pv->min[k], job->v.probe_spacing);
			grid = lrg_probe_grid_new_with_bounds (pv->min, pv->max, n[0], n[1], n[2]);
			g_ptr_array_add (job->grids, grid);
			job->n_probes += lrg_probe_grid_get_n_probes (grid);
		}
		job->probe_pos = g_new (gfloat, (gsize)MAX (job->n_probes, 1u) * 3u);
		job->probe_sh = g_new0 (gfloat, (gsize)MAX (job->n_probes, 1u) * 27u);
		job->probe_valid = g_new0 (guint8, MAX (job->n_probes, 1u));
		for (v = 0; v < job->grids->len; v++)
		{
			LrgProbeGrid *grid = g_ptr_array_index (job->grids, v);
			guint         np = lrg_probe_grid_get_n_probes (grid);
			guint         k;

			for (k = 0; k < np; k++)
				lrg_probe_grid_get_probe_position (grid, k, job->probe_pos + (gsize)(at + k) * 3u);
			at += np;
		}
		run_parallel (job, pool, WORK_PROBES, job->n_probes, n_threads, LRG_LIGHTMAP_BAKE_PHASE_PROBES,
		              l_end + 0.05, 1.0);
	}
	g_thread_pool_free (pool, FALSE, TRUE);
	if (check_cancel (job, error))
		return FALSE;

	/* results */
	job->lightmap = lrg_lightmap3d_new ();
	for (i = 0; i < s->n_pages; i++)
	{
		g_autoptr(LrgHdrImage) img = lrg_hdr_image_new_from_data (s->page_size, s->page_size,
		                                                          job->pages[i].rgb);

		lrg_lightmap3d_add_page (job->lightmap, img);
	}
	for (i = 0; i < s->n_objs; i++)
	{
		const SceneObj *ob = &s->objs[i];

		if ((ob->flags & LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE) && ob->guid != NULL)
			lrg_lightmap3d_set_entry (job->lightmap, ob->guid, ob->page, ob->so);
	}
	{
		g_autoptr(LrgLightmapBakeSettings) used = lrg_lightmap_bake_settings_new ();

		used->v = job->v;
		lrg_lightmap_bake_settings_write_to_lightmap (used, job->lightmap);
	}
	{
		guint at = 0;
		guint v;

		for (v = 0; v < job->grids->len; v++)
		{
			LrgProbeGrid *grid = g_ptr_array_index (job->grids, v);
			guint         np = lrg_probe_grid_get_n_probes (grid);
			guint         k;

			for (k = 0; k < np; k++)
				lrg_probe_grid_set_probe (grid, k, job->probe_sh + (gsize)(at + k) * 27u,
				                          job->probe_valid[at + k]);
			lrg_probe_grid_notify_changed (grid);
			at += np;
		}
	}
	g_debug ("lightmap bake: %s done at %.3f s", "probes+results", (gdouble)(g_get_monotonic_time () - t0) / (gdouble)G_TIME_SPAN_SECOND);
	job->seconds = (gdouble)(g_get_monotonic_time () - t0) / (gdouble)G_TIME_SPAN_SECOND;
	return TRUE;
}

/* ==========================================================================
 * LrgLightmapBaker
 * ========================================================================== */

static void
clear_results (LrgLightmapBaker *self)
{
	guint i;

	g_clear_object (&self->lightmap);
	g_clear_pointer (&self->grids, g_ptr_array_unref);
	for (i = 0; i < self->n_pages; i++)
		g_free (self->page_rgb[i]);
	g_clear_pointer (&self->page_rgb, g_free);
	self->n_pages = 0;
	self->page_size = 0;
}

static void
lrg_lightmap_baker_finalize (GObject *object)
{
	LrgLightmapBaker *self = LRG_LIGHTMAP_BAKER (object);

	clear_results (self);
	scene_free (self->scene);
	g_clear_object (&self->settings);
	g_clear_pointer (&self->inputs, g_ptr_array_unref);
	g_clear_pointer (&self->lights, g_array_unref);
	g_clear_pointer (&self->volumes, g_array_unref);
	g_clear_object (&self->environment);
	G_OBJECT_CLASS (lrg_lightmap_baker_parent_class)->finalize (object);
}

static void
lrg_lightmap_baker_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	LrgLightmapBaker *self = LRG_LIGHTMAP_BAKER (object);

	switch (prop_id)
	{
	case PROP_SETTINGS:
		g_value_set_object (value, self->settings);
		break;
	case PROP_RUNNING:
		g_value_set_boolean (value, self->running);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_lightmap_baker_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	LrgLightmapBaker *self = LRG_LIGHTMAP_BAKER (object);

	switch (prop_id)
	{
	case PROP_SETTINGS:
		lrg_lightmap_baker_set_settings (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_lightmap_baker_class_init (LrgLightmapBakerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_lightmap_baker_finalize;
	object_class->get_property = lrg_lightmap_baker_get_property;
	object_class->set_property = lrg_lightmap_baker_set_property;

	/**
	 * LrgLightmapBaker:settings:
	 *
	 * The #LrgLightmapBakeSettings the next bake reads.
	 */
	properties[PROP_SETTINGS] =
		g_param_spec_object ("settings", NULL, NULL, LRG_TYPE_LIGHTMAP_BAKE_SETTINGS,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgLightmapBaker:running:
	 *
	 * Whether a bake runs.
	 */
	properties[PROP_RUNNING] =
		g_param_spec_boolean ("running", NULL, NULL, FALSE,
		                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgLightmapBaker::progress:
	 * @self: the baker
	 * @phase: the #LrgLightmapBakePhase running
	 * @fraction: overall progress, 0 .. 1, never decreasing within a bake
	 *
	 * Emitted while baking, at most every 0.2 % and every 50 ms, on the
	 * main context the bake was started from (the calling thread for
	 * lrg_lightmap_baker_bake()); last with %LRG_LIGHTMAP_BAKE_PHASE_DONE
	 * and 1 on success.
	 */
	signals[SIGNAL_PROGRESS] =
		g_signal_new ("progress", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, LRG_TYPE_LIGHTMAP_BAKE_PHASE, G_TYPE_DOUBLE);
}

static void
lrg_lightmap_baker_init (LrgLightmapBaker *self)
{
	self->settings = lrg_lightmap_bake_settings_new ();
	self->inputs = g_ptr_array_new_with_free_func (bake_input_free);
	self->lights = g_array_new (FALSE, FALSE, sizeof (BakeLight));
	self->volumes = g_array_new (FALSE, FALSE, sizeof (ProbeVolume));
	self->scene_dirty = TRUE;
}

LrgLightmapBaker *
lrg_lightmap_baker_new (void)
{
	return g_object_new (LRG_TYPE_LIGHTMAP_BAKER, NULL);
}

LrgLightmapBakeSettings *
lrg_lightmap_baker_get_settings (LrgLightmapBaker *self)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), NULL);
	return self->settings;
}

void
lrg_lightmap_baker_set_settings (LrgLightmapBaker        *self,
                                 LrgLightmapBakeSettings *settings)
{
	g_return_if_fail (LRG_IS_LIGHTMAP_BAKER (self));
	g_return_if_fail (LRG_IS_LIGHTMAP_BAKE_SETTINGS (settings));

	if (g_set_object (&self->settings, settings))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SETTINGS]);
}

void
lrg_lightmap_baker_clear (LrgLightmapBaker *self)
{
	g_return_if_fail (LRG_IS_LIGHTMAP_BAKER (self));
	g_return_if_fail (!self->running);

	g_ptr_array_set_size (self->inputs, 0);
	g_array_set_size (self->lights, 0);
	g_array_set_size (self->volumes, 0);
	g_clear_object (&self->environment);
	memset (self->sky_flat, 0, sizeof self->sky_flat);
	self->scene_dirty = TRUE;
}

gboolean
lrg_lightmap_baker_add_mesh (LrgLightmapBaker           *self,
                             const gchar                *guid,
                             LrgUvMesh                  *mesh,
                             const gfloat               *matrix,
                             const gfloat               *albedo,
                             const gfloat               *emission,
                             gfloat                      lightmap_scale,
                             LrgLightmapBakeObjectFlags  flags)
{
	BakeInput *in;
	guint      i;
	gint       k;

	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), FALSE);
	g_return_val_if_fail (LRG_IS_UV_MESH (mesh), FALSE);
	g_return_val_if_fail (!self->running, FALSE);

	if (lrg_uv_mesh_get_n_triangles (mesh) == 0)
		return FALSE;
	if ((flags & LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE) && (guid == NULL || *guid == '\0'))
		return FALSE;
	if (guid != NULL)
	{
		for (i = 0; i < self->inputs->len; i++)
		{
			BakeInput *o = g_ptr_array_index (self->inputs, i);

			if (g_strcmp0 (o->guid, guid) == 0)
				return FALSE;
		}
	}

	in = g_new0 (BakeInput, 1);
	in->guid = g_strdup (guid);
	in->mesh = g_object_ref (mesh);
	if (matrix != NULL)
		memcpy (in->matrix, matrix, sizeof in->matrix);
	else
		lrg_math3d_identity (in->matrix);
	for (k = 0; k < 3; k++)
	{
		in->albedo[k] = albedo != NULL ? to_linear (CLAMP (albedo[k], 0.0f, 1.0f)) : 1.0f;
		in->emission[k] = emission != NULL ? to_linear (MAX (emission[k], 0.0f)) : 0.0f;
	}
	in->scale = isfinite (lightmap_scale) ? MAX (lightmap_scale, 0.0f) : 1.0f;
	in->flags = (guint)flags;
	/* scale 0: the object only blocks and bounces light (probe lit) */
	if (in->scale <= 0.0f)
		in->flags &= ~(guint)LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE;
	g_ptr_array_add (self->inputs, in);
	self->scene_dirty = TRUE;
	return TRUE;
}

gboolean
lrg_lightmap_baker_add_light (LrgLightmapBaker     *self,
                              const LrgLight3DDesc *desc,
                              gfloat                source_radius)
{
	BakeLight l;
	gfloat    radius;

	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), FALSE);
	g_return_val_if_fail (desc != NULL, FALSE);
	g_return_val_if_fail (!self->running, FALSE);

	if (!desc->enabled || desc->bake_mode == LRG_LIGHT_MODE_REALTIME)
		return FALSE;
	if (source_radius >= 0.0f && isfinite (source_radius))
		radius = source_radius;
	else
		radius = desc->kind == LRG_LIGHT3D_DIRECTIONAL ? self->settings->v.sun_angle
		                                                : self->settings->v.light_radius;
	light_from_desc (&l, desc, radius);
	g_array_append_val (self->lights, l);
	self->scene_dirty = TRUE;
	return TRUE;
}

void
lrg_lightmap_baker_set_environment (LrgLightmapBaker *self,
                                    LrgEnvironment   *environment)
{
	g_return_if_fail (LRG_IS_LIGHTMAP_BAKER (self));
	g_return_if_fail (environment == NULL || LRG_IS_ENVIRONMENT (environment));

	g_set_object (&self->environment, environment);
	self->scene_dirty = TRUE;
}

void
lrg_lightmap_baker_set_sky_color (LrgLightmapBaker *self,
                                  gfloat            r,
                                  gfloat            g,
                                  gfloat            b)
{
	g_return_if_fail (LRG_IS_LIGHTMAP_BAKER (self));

	self->sky_flat[0] = MAX (r, 0.0f);
	self->sky_flat[1] = MAX (g, 0.0f);
	self->sky_flat[2] = MAX (b, 0.0f);
	self->scene_dirty = TRUE;
}

void
lrg_lightmap_baker_add_probe_volume (LrgLightmapBaker *self,
                                     const gfloat     *min,
                                     const gfloat     *max,
                                     guint             nx,
                                     guint             ny,
                                     guint             nz)
{
	ProbeVolume pv;
	gint        k;

	g_return_if_fail (LRG_IS_LIGHTMAP_BAKER (self));
	g_return_if_fail (min != NULL && max != NULL);
	g_return_if_fail (!self->running);

	for (k = 0; k < 3; k++)
	{
		pv.min[k] = MIN (min[k], max[k]);
		pv.max[k] = MAX (min[k], max[k]);
	}
	pv.n[0] = nx;
	pv.n[1] = ny;
	pv.n[2] = nz;
	g_array_append_val (self->volumes, pv);
}

/* --- world collection ------------------------------------------------- */

/* the item parameters LrgMeshRendererComponent submits for a primitive */
static void
primitive_params (LrgMeshRendererComponent *mr,
                  LrgPrimitiveType          prim,
                  gfloat                   *params)
{
	gfloat sx = 1.0f, sy = 1.0f, sz = 1.0f, radius = 0.5f, height = 1.0f, major = 0.375f, minor = 0.125f;

	g_object_get (mr, "size-x", &sx, "size-y", &sy, "size-z", &sz, "radius", &radius, "height", &height,
	              "major-radius", &major, "minor-radius", &minor, NULL);
	memset (params, 0, 4 * sizeof (gfloat));
	switch (prim)
	{
	case LRG_PRIMITIVE_CUBE:
		params[0] = sx;
		params[1] = sy;
		params[2] = sz;
		break;
	case LRG_PRIMITIVE_UV_SPHERE:
	case LRG_PRIMITIVE_ICO_SPHERE:
	case LRG_PRIMITIVE_CIRCLE:
	case LRG_PRIMITIVE_CIRCLE_2D:
		params[0] = radius;
		break;
	case LRG_PRIMITIVE_CYLINDER:
	case LRG_PRIMITIVE_CONE:
		params[0] = radius;
		params[1] = height;
		break;
	case LRG_PRIMITIVE_TORUS:
		params[0] = major;
		params[1] = minor;
		break;
	case LRG_PRIMITIVE_PLANE:
		params[0] = sx;
		params[1] = sz;
		break;
	case LRG_PRIMITIVE_RECTANGLE_2D:
		params[0] = sx;
		params[1] = sy;
		break;
	default:
		break;
	}
}

/* Concatenates meshes (with a transform each) into one LrgUvMesh. */
typedef struct
{
	GArray  *pos;
	GArray  *nrm;
	GArray  *uv2;
	GArray  *idx;
	gboolean all_uv2;
	gboolean all_nrm;
} Merge;

static void
merge_init (Merge *m)
{
	m->pos = g_array_new (FALSE, FALSE, sizeof (gfloat));
	m->nrm = g_array_new (FALSE, FALSE, sizeof (gfloat));
	m->uv2 = g_array_new (FALSE, FALSE, sizeof (gfloat));
	m->idx = g_array_new (FALSE, FALSE, sizeof (guint32));
	m->all_uv2 = TRUE;
	m->all_nrm = TRUE;
}

static void
merge_add (Merge         *m,
           const gfloat  *pos,
           const gfloat  *nrm,
           const gfloat  *uv2,
           guint          nv,
           const guint32 *idx,
           guint          ni,
           const gfloat  *matrix)
{
	guint base = m->pos->len / 3u;
	guint i;

	for (i = 0; i < nv; i++)
	{
		gfloat p[3];

		if (matrix != NULL)
			lrg_math3d_transform_point (matrix, pos + (gsize)i * 3u, p);
		else
			memcpy (p, pos + (gsize)i * 3u, sizeof p);
		g_array_append_vals (m->pos, p, 3);
		if (nrm != NULL)
		{
			gfloat n[3];

			if (matrix != NULL)
				lrg_math3d_transform_direction (matrix, nrm + (gsize)i * 3u, n);
			else
				memcpy (n, nrm + (gsize)i * 3u, sizeof n);
			normalize3 (n);
			g_array_append_vals (m->nrm, n, 3);
		}
		if (uv2 != NULL)
			g_array_append_vals (m->uv2, uv2 + (gsize)i * 2u, 2);
	}
	if (nrm == NULL)
		m->all_nrm = FALSE;
	if (uv2 == NULL)
		m->all_uv2 = FALSE;
	if (idx != NULL)
	{
		for (i = 0; i < ni; i++)
		{
			guint32 v = base + idx[i];

			g_array_append_val (m->idx, v);
		}
	}
	else
	{
		for (i = 0; i < nv - nv % 3u; i++)
		{
			guint32 v = base + i;

			g_array_append_val (m->idx, v);
		}
	}
}

static LrgUvMesh *
merge_finish (Merge *m)
{
	LrgUvMesh *mesh = NULL;

	if (m->idx->len >= 3u)
	{
		mesh = lrg_uv_mesh_new ((const gfloat *)(gpointer)m->pos->data, m->pos->len,
		                        (const guint32 *)(gpointer)m->idx->data, m->idx->len);
		if (mesh != NULL && m->all_nrm && m->nrm->len == m->pos->len)
			lrg_uv_mesh_set_normals (mesh, (const gfloat *)(gpointer)m->nrm->data, m->nrm->len);
		if (mesh != NULL && m->all_uv2 && m->uv2->len * 3u == m->pos->len * 2u)
			lrg_uv_mesh_set_uv2 (mesh, (const gfloat *)(gpointer)m->uv2->data, m->uv2->len);
	}
	g_array_unref (m->pos);
	g_array_unref (m->nrm);
	g_array_unref (m->uv2);
	g_array_unref (m->idx);
	return mesh;
}

/* Object-space geometry of a mesh renderer, or NULL. */
static LrgUvMesh *
mesh_renderer_geometry (LrgMeshRendererComponent *mr)
{
	GrlModel    *grl = lrg_mesh_renderer_component_get_model (mr);
	const gchar *asset = lrg_mesh_renderer_component_get_mesh_asset (mr);
	Merge        m;

	/* a loaded model: every mesh with the model's own transform */
	if (grl != NULL && grl_model_get_handle (grl) != NULL)
	{
		const Model *model = (const Model *)grl_model_get_handle (grl);
		gfloat       mt[16];
		gint         i;

		mt[0] = model->transform.m0;   mt[1] = model->transform.m1;
		mt[2] = model->transform.m2;   mt[3] = model->transform.m3;
		mt[4] = model->transform.m4;   mt[5] = model->transform.m5;
		mt[6] = model->transform.m6;   mt[7] = model->transform.m7;
		mt[8] = model->transform.m8;   mt[9] = model->transform.m9;
		mt[10] = model->transform.m10; mt[11] = model->transform.m11;
		mt[12] = model->transform.m12; mt[13] = model->transform.m13;
		mt[14] = model->transform.m14; mt[15] = model->transform.m15;
		merge_init (&m);
		for (i = 0; i < model->meshCount; i++)
		{
			const Mesh *me = &model->meshes[i];
			g_autofree guint32 *idx = NULL;
			gint        k;

			if (me->vertices == NULL || me->vertexCount <= 0)
				continue;
			if (me->indices != NULL)
			{
				idx = g_new (guint32, (gsize)MAX (me->triangleCount, 1) * 3u);
				for (k = 0; k < me->triangleCount * 3; k++)
					idx[k] = me->indices[k];
			}
			merge_add (&m, me->vertices, me->normals, me->texcoords2, (guint)me->vertexCount, idx,
			           (guint)MAX (me->triangleCount, 0) * 3u, mt);
		}
		return merge_finish (&m);
	}

	/* an asset path readable headless */
	if (asset != NULL && *asset != '\0')
	{
		g_autoptr(LrgGltfGeometry) geo = NULL;
		guint                      p;

		if (!g_file_test (asset, G_FILE_TEST_IS_REGULAR))
			return NULL;
		geo = lrg_gltf_geometry_new_from_file (asset, NULL);
		if (geo == NULL)
			return NULL;
		merge_init (&m);
		for (p = 0; p < lrg_gltf_geometry_get_primitive_count (geo); p++)
		{
			guint          nf = 0;
			guint          ni = 0;
			const gfloat  *pos = lrg_gltf_geometry_get_positions (geo, p, &nf);
			const guint32 *idx = lrg_gltf_geometry_get_indices (geo, p, &ni);

			if (pos == NULL || nf < 9u)
				continue;
			merge_add (&m, pos, lrg_gltf_geometry_get_normals (geo, p), NULL, nf / 3u, idx, ni, NULL);
		}
		return merge_finish (&m);
	}

	/* the built-in primitive */
	{
		LrgPrimitiveType prim = lrg_mesh_renderer_component_get_primitive (mr);
		gfloat           params[4];

		primitive_params (mr, prim, params);
		return lrg_uv_mesh_new_primitive (prim, params);
	}
}

/* Display-space albedo / emission and sidedness from a material slot. */
static void
material_colors (GObject  *mat,
                 gfloat   *albedo,
                 gfloat   *emission,
                 gboolean *double_sided)
{
	albedo[0] = albedo[1] = albedo[2] = 1.0f;
	emission[0] = emission[1] = emission[2] = 0.0f;
	*double_sided = FALSE;
	if (mat != NULL && LRG_IS_MATERIAL (mat))
	{
		gfloat base[4];
		gfloat strength = lrg_material_get_emission_strength (LRG_MATERIAL (mat));
		gint   k;

		lrg_material_get_base_color (LRG_MATERIAL (mat), base);
		memcpy (albedo, base, 3 * sizeof (gfloat));
		lrg_material_get_emission_color (LRG_MATERIAL (mat), emission);
		for (k = 0; k < 3; k++)
			emission[k] *= strength;
		*double_sided = lrg_material_get_double_sided (LRG_MATERIAL (mat));
	}
	else if (mat != NULL && LRG_IS_MATERIAL3D (mat))
	{
		gfloat a;
		gfloat strength = lrg_material3d_get_emission_strength (LRG_MATERIAL3D (mat));
		gint   k;

		lrg_material3d_get_color (LRG_MATERIAL3D (mat), &albedo[0], &albedo[1], &albedo[2], &a);
		lrg_material3d_get_emission_color (LRG_MATERIAL3D (mat), &emission[0], &emission[1], &emission[2], &a);
		for (k = 0; k < 3; k++)
			emission[k] *= strength;
	}
}

guint
lrg_lightmap_baker_add_world (LrgLightmapBaker *self,
                              LrgWorld3D       *world)
{
	g_autoptr(GPtrArray) comps = NULL;
	g_autoptr(GPtrArray) lights = NULL;
	GPtrArray           *grids;
	guint                added = 0;
	guint                i;

	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), 0);
	g_return_val_if_fail (LRG_IS_WORLD3D (world), 0);
	g_return_val_if_fail (!self->running, 0);

	/* static mesh renderers */
	comps = lrg_world3d_get_components_of_type (world, LRG_TYPE_MESH_RENDERER_COMPONENT);
	for (i = 0; i < comps->len; i++)
	{
		LrgMeshRendererComponent *mr = g_ptr_array_index (comps, i);
		LrgEntity3D              *ent = lrg_component_get_entity (LRG_COMPONENT (mr));
		g_autoptr(LrgUvMesh)      mesh = NULL;
		const gchar              *guid;
		gfloat                    matrix[16];
		gfloat                    albedo[3];
		gfloat                    emission[3];
		gboolean                  two_sided;
		guint                     flags = LRG_LIGHTMAP_BAKE_OBJECT_CONTRIBUTE;

		if (ent == NULL || !lrg_entity3d_is_active_in_hierarchy (ent)
		    || !lrg_component_get_enabled (LRG_COMPONENT (mr))
		    || !lrg_mesh_renderer_component_get_static_lighting (mr))
			continue;
		mesh = mesh_renderer_geometry (mr);
		if (mesh == NULL)
		{
			g_warning ("lightmap baker: no geometry for %s (load its model first)",
			           lrg_entity3d_get_guid (ent) != NULL ? lrg_entity3d_get_guid (ent) : "an entity");
			continue;
		}
		guid = lrg_entity3d_get_guid (ent);
		if (guid != NULL && *guid != '\0')
			flags |= LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE;
		material_colors (lrg_mesh_renderer_component_get_effective_material (mr), albedo, emission, &two_sided);
		if (two_sided)
			flags |= LRG_LIGHTMAP_BAKE_OBJECT_DOUBLE_SIDED;
		lrg_entity3d_get_world_matrix (ent, matrix);
		if (lrg_lightmap_baker_add_mesh (self, guid, mesh, matrix, albedo, emission,
		                                 lrg_mesh_renderer_component_get_lightmap_scale (mr),
		                                 (LrgLightmapBakeObjectFlags)flags))
			added++;
	}

	/* lights that are not realtime */
	lights = lrg_world3d_get_lights (world);
	for (i = 0; i < lights->len; i++)
	{
		const LrgLight3DDesc *d = g_ptr_array_index (lights, i);
		gfloat                radius = -1.0f;

		if (d->source != NULL && G_IS_OBJECT (d->source))
		{
			GParamSpec *ps = g_object_class_find_property (G_OBJECT_GET_CLASS (d->source), "source-radius");

			if (ps != NULL && ps->value_type == G_TYPE_FLOAT)
				g_object_get (d->source, "source-radius", &radius, NULL);
		}
		lrg_lightmap_baker_add_light (self, d, radius);
	}

	/* sky and probe volumes */
	if (lrg_world3d_get_environment (world) != NULL)
		lrg_lightmap_baker_set_environment (self, lrg_world3d_get_environment (world));
	grids = lrg_world3d_get_probe_grids (world);
	for (i = 0; grids != NULL && i < grids->len; i++)
	{
		LrgProbeGrid *g = g_ptr_array_index (grids, i);
		gfloat        mn[3];
		gfloat        mx[3];
		guint         n[3];

		lrg_probe_grid_get_bounds (g, mn, mx);
		lrg_probe_grid_get_resolution (g, &n[0], &n[1], &n[2]);
		lrg_lightmap_baker_add_probe_volume (self, mn, mx, n[0], n[1], n[2]);
	}
	return added;
}

guint
lrg_lightmap_baker_get_n_objects (LrgLightmapBaker *self)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), 0);
	return self->inputs->len;
}

guint
lrg_lightmap_baker_get_n_lights (LrgLightmapBaker *self)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), 0);
	return self->lights->len;
}

/* --- bake ----------------------------------------------------------------- */

/* Snapshots the inputs (main thread). */
static BakeJob *
job_new (LrgLightmapBaker *self,
         GCancellable     *cancellable,
         gboolean          sync)
{
	BakeJob *job = g_new0 (BakeJob, 1);
	guint    i;

	job->inputs = g_ptr_array_new_with_free_func (bake_input_free);
	for (i = 0; i < self->inputs->len; i++)
		g_ptr_array_add (job->inputs, bake_input_copy (g_ptr_array_index (self->inputs, i)));
	job->n_lights = self->lights->len;
	job->lights = g_memdup2 (self->lights->data, MAX (self->lights->len, 1u) * sizeof (BakeLight));
	job->n_volumes = self->volumes->len;
	job->volumes = g_memdup2 (self->volumes->data, MAX (self->volumes->len, 1u) * sizeof (ProbeVolume));
	job->v = self->settings->v;
	job->sky = self->environment != NULL ? sample_sky_cube (self->environment) : NULL;
	memcpy (job->sky_flat, self->sky_flat, sizeof job->sky_flat);
	job->baker = g_object_ref (self);
	job->context = g_main_context_ref_thread_default ();
	job->sync = sync;
	job->cancellable = cancellable != NULL ? g_object_ref (cancellable) : g_cancellable_new ();
	job->serial = ++self->serial;
	job->last_permille = -1;
	job->last_phase = -1;
	g_mutex_init (&job->lock);
	g_cond_init (&job->cond);
	self->last_fraction = 0.0;
	return job;
}

static void
set_running (LrgLightmapBaker *self,
             gboolean          running)
{
	if (self->running == running)
		return;
	self->running = running;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RUNNING]);
}

/* Moves a finished job's results into the baker (main thread). */
static void
install_results (LrgLightmapBaker *self,
                 BakeJob          *job)
{
	Scene *s = job->scene;
	guint  i;

	clear_results (self);
	self->lightmap = g_steal_pointer (&job->lightmap);
	self->grids = g_steal_pointer (&job->grids);
	self->n_pages = s->n_pages;
	self->page_size = s->page_size;
	self->page_rgb = g_new0 (gfloat *, MAX (s->n_pages, 1u));
	for (i = 0; i < s->n_pages; i++)
		self->page_rgb[i] = g_steal_pointer (&job->pages[i].rgb);
	scene_free (self->scene);
	self->scene = g_steal_pointer (&job->scene);
	pages_free (job->pages, s->n_pages);
	job->pages = NULL;
	self->scene_dirty = FALSE;
	self->bake_time = job->seconds;
	self->n_texels = job->n_texels;
}

gboolean
lrg_lightmap_baker_bake (LrgLightmapBaker  *self,
                         GCancellable      *cancellable,
                         GError           **error)
{
	BakeJob  *job;
	gboolean  ok;

	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), FALSE);

	if (self->running)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PENDING, "a bake is already running");
		return FALSE;
	}
	set_running (self, TRUE);
	job = job_new (self, cancellable, TRUE);
	ok = run_bake (job, error);
	if (ok)
	{
		install_results (self, job);
		emit_progress (self, job->serial, LRG_LIGHTMAP_BAKE_PHASE_DONE, 1.0);
	}
	bake_job_free (job);
	set_running (self, FALSE);
	return ok;
}

static void
bake_thread (GTask        *task,
             gpointer      source,
             gpointer      task_data,
             GCancellable *cancellable)
{
	BakeJob *job = task_data;
	GError  *error = NULL;

	if (run_bake (job, &error))
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, error);
}

/* the inner task finished: install, then complete the caller's task */
static void
bake_inner_done (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
	LrgLightmapBaker *self = LRG_LIGHTMAP_BAKER (source);
	GTask            *outer = user_data;
	BakeJob          *job = g_task_get_task_data (G_TASK (result));
	GError           *error = NULL;

	if (g_task_propagate_boolean (G_TASK (result), &error))
	{
		install_results (self, job);
		emit_progress (self, job->serial, LRG_LIGHTMAP_BAKE_PHASE_DONE, 1.0);
		set_running (self, FALSE);
		g_task_return_boolean (outer, TRUE);
	}
	else
	{
		set_running (self, FALSE);
		g_task_return_error (outer, error);
	}
	g_object_unref (outer);
}

void
lrg_lightmap_baker_bake_async (LrgLightmapBaker    *self,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
	GTask   *outer;
	GTask   *inner;
	BakeJob *job;

	g_return_if_fail (LRG_IS_LIGHTMAP_BAKER (self));

	outer = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (outer, lrg_lightmap_baker_bake_async);
	if (self->running)
	{
		g_task_return_new_error (outer, G_IO_ERROR, G_IO_ERROR_PENDING, "a bake is already running");
		g_object_unref (outer);
		return;
	}
	set_running (self, TRUE);
	job = job_new (self, cancellable, FALSE);
	inner = g_task_new (self, job->cancellable, bake_inner_done, outer);
	/* the inner task reports cancellation itself through run_bake */
	g_task_set_check_cancellable (inner, FALSE);
	g_task_set_task_data (inner, job, bake_job_free);
	g_task_run_in_thread (inner, bake_thread);
	g_object_unref (inner);
}

gboolean
lrg_lightmap_baker_bake_finish (LrgLightmapBaker  *self,
                                GAsyncResult      *result,
                                GError           **error)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), FALSE);
	g_return_val_if_fail (g_task_is_valid (result, self), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

gboolean
lrg_lightmap_baker_is_running (LrgLightmapBaker *self)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), FALSE);
	return self->running;
}

LrgLightmap3D *
lrg_lightmap_baker_get_lightmap (LrgLightmapBaker *self)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), NULL);
	return self->lightmap;
}

GPtrArray *
lrg_lightmap_baker_get_probe_grids (LrgLightmapBaker *self)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), NULL);

	if (self->grids == NULL)
		self->grids = g_ptr_array_new_with_free_func (g_object_unref);
	return self->grids;
}

LrgUvMesh *
lrg_lightmap_baker_get_object_mesh (LrgLightmapBaker *self,
                                    const gchar      *guid)
{
	guint i;

	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), NULL);
	g_return_val_if_fail (guid != NULL, NULL);

	if (self->scene == NULL)
		return NULL;
	for (i = 0; i < self->scene->n_objs; i++)
		if (g_strcmp0 (self->scene->objs[i].guid, guid) == 0)
			return self->scene->objs[i].mesh;
	return NULL;
}

const gfloat *
lrg_lightmap_baker_get_page_data (LrgLightmapBaker *self,
                                  guint             page,
                                  guint            *out_width,
                                  guint            *out_height)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), NULL);

	if (page >= self->n_pages)
		return NULL;
	if (out_width != NULL)
		*out_width = self->page_size;
	if (out_height != NULL)
		*out_height = self->page_size;
	return self->page_rgb[page];
}

gboolean
lrg_lightmap_baker_evaluate (LrgLightmapBaker *self,
                             const gfloat     *position,
                             const gfloat     *normal,
                             gfloat           *out_rgb)
{
	gfloat var;
	gfloat back;

	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), FALSE);
	g_return_val_if_fail (position != NULL && normal != NULL && out_rgb != NULL, FALSE);

	out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
	if (self->running)
		return FALSE;
	if (self->scene == NULL || self->scene_dirty)
	{
		g_autoptr(GPtrArray) inputs = g_ptr_array_new_with_free_func (bake_input_free);
		guint                i;
		Scene               *s;

		for (i = 0; i < self->inputs->len; i++)
			g_ptr_array_add (inputs, bake_input_copy (g_ptr_array_index (self->inputs, i)));
		s = scene_build (inputs, &self->settings->v, (const BakeLight *)(gpointer)self->lights->data,
		                 self->lights->len,
		                 self->environment != NULL ? sample_sky_cube (self->environment) : NULL,
		                 self->sky_flat, NULL);
		if (s == NULL)
			return FALSE;
		scene_free (self->scene);
		self->scene = s;
		self->scene_dirty = FALSE;
	}
	/* the current sampling settings, the scene's geometry */
	self->scene->v = self->settings->v;
	estimate (self->scene, position, normal, normal, 0x4556414cu, 0, out_rgb, &var, &back);
	return TRUE;
}

gdouble
lrg_lightmap_baker_get_bake_time (LrgLightmapBaker *self)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), 0.0);
	return self->bake_time;
}

guint
lrg_lightmap_baker_get_n_texels (LrgLightmapBaker *self)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP_BAKER (self), 0);
	return self->n_texels;
}
