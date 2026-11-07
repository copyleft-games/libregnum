/* lrg-terrain-brush.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTerrainBrush: sculpt and paint dabs on an LrgTerrainAsset, with
 * undo-friendly before/after edits per dab and per stroke.  See
 * lrg-terrain-brush.h and docs/modules/graphics/terrain-brush.org.
 *
 * A dab:
 *   1. finds its region (samples within the radius for height and
 *      weight ops, cells whose centre is within it otherwise),
 *   2. snapshots the region (lrg_terrain_asset_capture_edit()),
 *   3. changes the asset's arrays in place, sample by sample, with the
 *      weight w = falloff (distance / radius),
 *   4. captures the region again as "after", emits region-changed once
 *      and returns the edit (or NULL when nothing changed).
 * A stroke snapshots the whole data set the brush edits at its start
 * and remembers the union of its dabs' regions; its end returns one edit
 * of that union with "before" taken from the snapshot.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-terrain-brush.h"
#include "lrg-terrain-asset-private.h"
#include "lrg-hash-noise-private.h"

GType
lrg_terrain_brush_op_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TERRAIN_BRUSH_RAISE, "LRG_TERRAIN_BRUSH_RAISE", "raise" },
			{ LRG_TERRAIN_BRUSH_LOWER, "LRG_TERRAIN_BRUSH_LOWER", "lower" },
			{ LRG_TERRAIN_BRUSH_FLATTEN, "LRG_TERRAIN_BRUSH_FLATTEN", "flatten" },
			{ LRG_TERRAIN_BRUSH_SMOOTH, "LRG_TERRAIN_BRUSH_SMOOTH", "smooth" },
			{ LRG_TERRAIN_BRUSH_NOISE, "LRG_TERRAIN_BRUSH_NOISE", "noise" },
			{ LRG_TERRAIN_BRUSH_PAINT, "LRG_TERRAIN_BRUSH_PAINT", "paint" },
			{ LRG_TERRAIN_BRUSH_HOLE, "LRG_TERRAIN_BRUSH_HOLE", "hole" },
			{ LRG_TERRAIN_BRUSH_FILL, "LRG_TERRAIN_BRUSH_FILL", "fill" },
			{ LRG_TERRAIN_BRUSH_FOLIAGE_PAINT, "LRG_TERRAIN_BRUSH_FOLIAGE_PAINT", "foliage-paint" },
			{ LRG_TERRAIN_BRUSH_FOLIAGE_ERASE, "LRG_TERRAIN_BRUSH_FOLIAGE_ERASE", "foliage-erase" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgTerrainBrushOp"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_terrain_brush_falloff_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TERRAIN_BRUSH_FALLOFF_LINEAR, "LRG_TERRAIN_BRUSH_FALLOFF_LINEAR", "linear" },
			{ LRG_TERRAIN_BRUSH_FALLOFF_SMOOTH, "LRG_TERRAIN_BRUSH_FALLOFF_SMOOTH", "smooth" },
			{ LRG_TERRAIN_BRUSH_FALLOFF_SPHERE, "LRG_TERRAIN_BRUSH_FALLOFF_SPHERE", "sphere" },
			{ LRG_TERRAIN_BRUSH_FALLOFF_SHARP, "LRG_TERRAIN_BRUSH_FALLOFF_SHARP", "sharp" },
			{ LRG_TERRAIN_BRUSH_FALLOFF_CONSTANT, "LRG_TERRAIN_BRUSH_FALLOFF_CONSTANT", "constant" },
			{ LRG_TERRAIN_BRUSH_FALLOFF_CURVE, "LRG_TERRAIN_BRUSH_FALLOFF_CURVE", "curve" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgTerrainBrushFalloff"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

struct _LrgTerrainBrush
{
	GObject                 parent_instance;

	LrgTerrainBrushOp       op;
	gfloat                  radius;
	gfloat                  strength;
	LrgTerrainBrushFalloff  falloff;
	LrgKeyframeCurve       *curve;
	gfloat                  target;
	gboolean                sample_target;
	guint                   layer;
	gfloat                  noise_scale;
	guint32                 seed;

	/* stroke */
	gboolean                stroking;
	LrgTerrainAsset        *stroke_asset;
	LrgTerrainEditKind      stroke_kind;
	guint                   stroke_layer;
	LrgTerrainEdit         *snapshot;       /* the whole data set at begin */
	gboolean                touched;
	guint                   ux0, uz0, ux1, uz1;   /* union, exclusive end */
};

G_DEFINE_FINAL_TYPE (LrgTerrainBrush, lrg_terrain_brush, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_OP,
	PROP_RADIUS,
	PROP_STRENGTH,
	PROP_FALLOFF,
	PROP_FALLOFF_CURVE,
	PROP_TARGET_HEIGHT,
	PROP_SAMPLE_TARGET,
	PROP_LAYER,
	PROP_NOISE_SCALE,
	PROP_SEED,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
drop_stroke (LrgTerrainBrush *self)
{
	self->stroking = FALSE;
	self->touched = FALSE;
	g_clear_object (&self->stroke_asset);
	g_clear_pointer (&self->snapshot, lrg_terrain_edit_free);
}

static void
lrg_terrain_brush_finalize (GObject *object)
{
	LrgTerrainBrush *self = LRG_TERRAIN_BRUSH (object);

	drop_stroke (self);
	g_clear_object (&self->curve);
	G_OBJECT_CLASS (lrg_terrain_brush_parent_class)->finalize (object);
}

static void
lrg_terrain_brush_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgTerrainBrush *self = LRG_TERRAIN_BRUSH (object);

	switch (prop_id)
	{
	case PROP_OP:
		g_value_set_enum (value, (gint) self->op);
		break;
	case PROP_RADIUS:
		g_value_set_float (value, self->radius);
		break;
	case PROP_STRENGTH:
		g_value_set_float (value, self->strength);
		break;
	case PROP_FALLOFF:
		g_value_set_enum (value, (gint) self->falloff);
		break;
	case PROP_FALLOFF_CURVE:
		g_value_set_object (value, self->curve);
		break;
	case PROP_TARGET_HEIGHT:
		g_value_set_float (value, self->target);
		break;
	case PROP_SAMPLE_TARGET:
		g_value_set_boolean (value, self->sample_target);
		break;
	case PROP_LAYER:
		g_value_set_uint (value, self->layer);
		break;
	case PROP_NOISE_SCALE:
		g_value_set_float (value, self->noise_scale);
		break;
	case PROP_SEED:
		g_value_set_uint (value, self->seed);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_terrain_brush_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgTerrainBrush *self = LRG_TERRAIN_BRUSH (object);

	switch (prop_id)
	{
	case PROP_OP:
		lrg_terrain_brush_set_op (self, (LrgTerrainBrushOp) g_value_get_enum (value));
		break;
	case PROP_RADIUS:
		lrg_terrain_brush_set_radius (self, g_value_get_float (value));
		break;
	case PROP_STRENGTH:
		lrg_terrain_brush_set_strength (self, g_value_get_float (value));
		break;
	case PROP_FALLOFF:
		lrg_terrain_brush_set_falloff (self, (LrgTerrainBrushFalloff) g_value_get_enum (value));
		break;
	case PROP_FALLOFF_CURVE:
		lrg_terrain_brush_set_falloff_curve (self, g_value_get_object (value));
		break;
	case PROP_TARGET_HEIGHT:
		lrg_terrain_brush_set_target_height (self, g_value_get_float (value));
		break;
	case PROP_SAMPLE_TARGET:
		lrg_terrain_brush_set_sample_target (self, g_value_get_boolean (value));
		break;
	case PROP_LAYER:
		lrg_terrain_brush_set_layer (self, g_value_get_uint (value));
		break;
	case PROP_NOISE_SCALE:
		lrg_terrain_brush_set_noise_scale (self, g_value_get_float (value));
		break;
	case PROP_SEED:
		lrg_terrain_brush_set_seed (self, g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_terrain_brush_class_init (LrgTerrainBrushClass *klass)
{
	GObjectClass     *object_class = G_OBJECT_CLASS (klass);
	const GParamFlags rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->finalize = lrg_terrain_brush_finalize;
	object_class->get_property = lrg_terrain_brush_get_property;
	object_class->set_property = lrg_terrain_brush_set_property;

	/**
	 * LrgTerrainBrush:op:
	 *
	 * What a dab does.
	 */
	properties[PROP_OP] = g_param_spec_enum ("op", NULL, "Operation", LRG_TYPE_TERRAIN_BRUSH_OP,
	                                         LRG_TERRAIN_BRUSH_RAISE, rw);

	/**
	 * LrgTerrainBrush:radius:
	 *
	 * The brush radius in local world units.
	 */
	properties[PROP_RADIUS] = g_param_spec_float ("radius", NULL, "Radius", 0.001f, 100000.0f, 4.0f, rw);

	/**
	 * LrgTerrainBrush:strength:
	 *
	 * Height per dab (raise, lower, noise) or blend rate (other ops).
	 */
	properties[PROP_STRENGTH] = g_param_spec_float ("strength", NULL, "Strength", 0.0f, 100000.0f, 1.0f, rw);

	/**
	 * LrgTerrainBrush:falloff:
	 *
	 * The falloff shape from centre to rim.
	 */
	properties[PROP_FALLOFF] = g_param_spec_enum ("falloff", NULL, "Falloff", LRG_TYPE_TERRAIN_BRUSH_FALLOFF,
	                                              LRG_TERRAIN_BRUSH_FALLOFF_SMOOTH, rw);

	/**
	 * LrgTerrainBrush:falloff-curve:
	 *
	 * The curve of %LRG_TERRAIN_BRUSH_FALLOFF_CURVE.
	 */
	properties[PROP_FALLOFF_CURVE] = g_param_spec_object ("falloff-curve", NULL, "Falloff curve",
	                                                      LRG_TYPE_KEYFRAME_CURVE, rw);

	/**
	 * LrgTerrainBrush:target-height:
	 *
	 * The height flatten moves towards.
	 */
	properties[PROP_TARGET_HEIGHT] = g_param_spec_float ("target-height", NULL, "Flatten target",
	                                                     -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgTerrainBrush:sample-target:
	 *
	 * Whether a stroke takes the flatten target from the terrain under
	 * its first position.
	 */
	properties[PROP_SAMPLE_TARGET] = g_param_spec_boolean ("sample-target", NULL, "Sample the flatten target",
	                                                       FALSE, rw);

	/**
	 * LrgTerrainBrush:layer:
	 *
	 * The surface layer (paint) or foliage layer (foliage ops).
	 */
	properties[PROP_LAYER] = g_param_spec_uint ("layer", NULL, "Layer", 0, G_MAXUINT, 0, rw);

	/**
	 * LrgTerrainBrush:noise-scale:
	 *
	 * The noise feature size in world units.
	 */
	properties[PROP_NOISE_SCALE] = g_param_spec_float ("noise-scale", NULL, "Noise scale", 0.001f, 100000.0f,
	                                                   8.0f, rw);

	/**
	 * LrgTerrainBrush:seed:
	 *
	 * The noise seed.
	 */
	properties[PROP_SEED] = g_param_spec_uint ("seed", NULL, "Noise seed", 0, G_MAXUINT32, 0, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_terrain_brush_init (LrgTerrainBrush *self)
{
	self->radius = 4.0f;
	self->strength = 1.0f;
	self->falloff = LRG_TERRAIN_BRUSH_FALLOFF_SMOOTH;
	self->noise_scale = 8.0f;
}

LrgTerrainBrush *
lrg_terrain_brush_new (LrgTerrainBrushOp op)
{
	return (LrgTerrainBrush *) g_object_new (LRG_TYPE_TERRAIN_BRUSH, "op", op, NULL);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

LrgTerrainBrushOp
lrg_terrain_brush_get_op (LrgTerrainBrush *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), LRG_TERRAIN_BRUSH_RAISE);
	return self->op;
}

void
lrg_terrain_brush_set_op (LrgTerrainBrush   *self,
                          LrgTerrainBrushOp  op)
{
	g_return_if_fail (LRG_IS_TERRAIN_BRUSH (self));
	if (self->op == op)
		return;
	self->op = op;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OP]);
}

gfloat
lrg_terrain_brush_get_radius (LrgTerrainBrush *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), 0.0f);
	return self->radius;
}

void
lrg_terrain_brush_set_radius (LrgTerrainBrush *self,
                              gfloat           radius)
{
	g_return_if_fail (LRG_IS_TERRAIN_BRUSH (self));
	if (!(radius > 0.0f) || !isfinite (radius) || self->radius == radius)
		return;
	self->radius = radius;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]);
}

gfloat
lrg_terrain_brush_get_strength (LrgTerrainBrush *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), 0.0f);
	return self->strength;
}

void
lrg_terrain_brush_set_strength (LrgTerrainBrush *self,
                                gfloat           strength)
{
	g_return_if_fail (LRG_IS_TERRAIN_BRUSH (self));
	if (!(strength >= 0.0f) || !isfinite (strength) || self->strength == strength)
		return;
	self->strength = strength;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STRENGTH]);
}

LrgTerrainBrushFalloff
lrg_terrain_brush_get_falloff (LrgTerrainBrush *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), LRG_TERRAIN_BRUSH_FALLOFF_SMOOTH);
	return self->falloff;
}

void
lrg_terrain_brush_set_falloff (LrgTerrainBrush        *self,
                               LrgTerrainBrushFalloff  falloff)
{
	g_return_if_fail (LRG_IS_TERRAIN_BRUSH (self));
	if (self->falloff == falloff)
		return;
	self->falloff = falloff;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FALLOFF]);
}

LrgKeyframeCurve *
lrg_terrain_brush_get_falloff_curve (LrgTerrainBrush *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), NULL);
	return self->curve;
}

void
lrg_terrain_brush_set_falloff_curve (LrgTerrainBrush  *self,
                                     LrgKeyframeCurve *curve)
{
	g_return_if_fail (LRG_IS_TERRAIN_BRUSH (self));
	if (g_set_object (&self->curve, curve))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FALLOFF_CURVE]);
}

gfloat
lrg_terrain_brush_get_target_height (LrgTerrainBrush *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), 0.0f);
	return self->target;
}

void
lrg_terrain_brush_set_target_height (LrgTerrainBrush *self,
                                     gfloat           height)
{
	g_return_if_fail (LRG_IS_TERRAIN_BRUSH (self));
	if (!isfinite (height) || self->target == height)
		return;
	self->target = height;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_HEIGHT]);
}

gboolean
lrg_terrain_brush_get_sample_target (LrgTerrainBrush *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), FALSE);
	return self->sample_target;
}

void
lrg_terrain_brush_set_sample_target (LrgTerrainBrush *self,
                                     gboolean         sample)
{
	g_return_if_fail (LRG_IS_TERRAIN_BRUSH (self));
	sample = !!sample;
	if (self->sample_target == sample)
		return;
	self->sample_target = sample;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SAMPLE_TARGET]);
}

guint
lrg_terrain_brush_get_layer (LrgTerrainBrush *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), 0);
	return self->layer;
}

void
lrg_terrain_brush_set_layer (LrgTerrainBrush *self,
                             guint            layer)
{
	g_return_if_fail (LRG_IS_TERRAIN_BRUSH (self));
	if (self->layer == layer)
		return;
	self->layer = layer;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYER]);
}

gfloat
lrg_terrain_brush_get_noise_scale (LrgTerrainBrush *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), 0.0f);
	return self->noise_scale;
}

void
lrg_terrain_brush_set_noise_scale (LrgTerrainBrush *self,
                                   gfloat           scale)
{
	g_return_if_fail (LRG_IS_TERRAIN_BRUSH (self));
	if (!(scale > 0.0f) || !isfinite (scale) || self->noise_scale == scale)
		return;
	self->noise_scale = scale;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NOISE_SCALE]);
}

guint32
lrg_terrain_brush_get_seed (LrgTerrainBrush *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), 0);
	return self->seed;
}

void
lrg_terrain_brush_set_seed (LrgTerrainBrush *self,
                            guint32          seed)
{
	g_return_if_fail (LRG_IS_TERRAIN_BRUSH (self));
	if (self->seed == seed)
		return;
	self->seed = seed;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SEED]);
}

/* ==========================================================================
 * Falloff, regions, noise
 * ========================================================================== */

gfloat
lrg_terrain_brush_get_weight (LrgTerrainBrush *self,
                              gfloat           distance)
{
	gfloat t, w;

	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), 0.0f);
	if (!(distance < self->radius))
		return 0.0f;
	t = MAX (distance, 0.0f) / self->radius;
	switch (self->falloff)
	{
	case LRG_TERRAIN_BRUSH_FALLOFF_LINEAR:
		w = 1.0f - t;
		break;
	case LRG_TERRAIN_BRUSH_FALLOFF_SPHERE:
		w = sqrtf (MAX (1.0f - t * t, 0.0f));
		break;
	case LRG_TERRAIN_BRUSH_FALLOFF_SHARP:
		w = (1.0f - t) * (1.0f - t);
		break;
	case LRG_TERRAIN_BRUSH_FALLOFF_CONSTANT:
		w = 1.0f;
		break;
	case LRG_TERRAIN_BRUSH_FALLOFF_CURVE:
		w = self->curve != NULL ? lrg_keyframe_curve_sample (self->curve, t) : 1.0f - t;
		break;
	case LRG_TERRAIN_BRUSH_FALLOFF_SMOOTH:
	default:
		w = 1.0f - t * t * (3.0f - 2.0f * t);
		break;
	}
	return isfinite (w) ? CLAMP (w, 0.0f, 1.0f) : 0.0f;
}

LrgTerrainEditKind
lrg_terrain_brush_get_edit_kind (LrgTerrainBrush *self,
                                 LrgTerrainAsset *asset)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), LRG_TERRAIN_EDIT_HEIGHTS);
	switch (self->op)
	{
	case LRG_TERRAIN_BRUSH_PAINT:
		if (asset != NULL && lrg_terrain_asset_get_splat_mode (asset) == LRG_TERRAIN_SPLAT_MODE_IDS)
			return LRG_TERRAIN_EDIT_MATERIAL_IDS;
		return LRG_TERRAIN_EDIT_SPLAT;
	case LRG_TERRAIN_BRUSH_HOLE:
	case LRG_TERRAIN_BRUSH_FILL:
		return LRG_TERRAIN_EDIT_HOLES;
	case LRG_TERRAIN_BRUSH_FOLIAGE_PAINT:
	case LRG_TERRAIN_BRUSH_FOLIAGE_ERASE:
		return LRG_TERRAIN_EDIT_FOLIAGE;
	case LRG_TERRAIN_BRUSH_RAISE:
	case LRG_TERRAIN_BRUSH_LOWER:
	case LRG_TERRAIN_BRUSH_FLATTEN:
	case LRG_TERRAIN_BRUSH_SMOOTH:
	case LRG_TERRAIN_BRUSH_NOISE:
	default:
		return LRG_TERRAIN_EDIT_HEIGHTS;
	}
}

/* Whether an edit kind is addressed in samples (else cells). */
static gboolean
kind_on_samples (LrgTerrainEditKind kind)
{
	return kind == LRG_TERRAIN_EDIT_HEIGHTS || kind == LRG_TERRAIN_EDIT_SPLAT;
}

gboolean
lrg_terrain_brush_get_region (LrgTerrainBrush *self,
                              LrgTerrainAsset *asset,
                              gfloat           x,
                              gfloat           z,
                              guint           *out_x,
                              guint           *out_z,
                              guint           *out_width,
                              guint           *out_height)
{
	gfloat cell, off;
	guint  lim_x, lim_z;
	gint   x0, z0, x1, z1;

	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), FALSE);
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (asset), FALSE);
	cell = lrg_terrain_asset_get_cell_size (asset);
	if (kind_on_samples (lrg_terrain_brush_get_edit_kind (self, asset)))
	{
		lrg_terrain_asset_get_samples (asset, &lim_x, &lim_z);
		off = 0.0f;
	}
	else
	{
		lrg_terrain_asset_get_cells (asset, &lim_x, &lim_z);
		off = 0.5f;   /* cell centres */
	}
	if (!isfinite (x) || !isfinite (z))
		return FALSE;

	/* indices whose position lies within the radius */
	x0 = (gint) ceilf ((x - self->radius) / cell - off);
	z0 = (gint) ceilf ((z - self->radius) / cell - off);
	x1 = (gint) floorf ((x + self->radius) / cell - off);
	z1 = (gint) floorf ((z + self->radius) / cell - off);
	x0 = MAX (x0, 0);
	z0 = MAX (z0, 0);
	x1 = MIN (x1, (gint) lim_x - 1);
	z1 = MIN (z1, (gint) lim_z - 1);
	if (x1 < x0 || z1 < z0)
		return FALSE;
	if (out_x != NULL)
		*out_x = (guint) x0;
	if (out_z != NULL)
		*out_z = (guint) z0;
	if (out_width != NULL)
		*out_width = (guint) (x1 - x0 + 1);
	if (out_height != NULL)
		*out_height = (guint) (z1 - z0 + 1);
	return TRUE;
}

/* Smooth value noise in [0, 1]: hashed lattice values, quintic blend. */
static gfloat
value_noise (guint32 seed,
             gfloat  x,
             gfloat  z)
{
	gfloat  fx = floorf (x), fz = floorf (z);
	gint64  ix = (gint64) fx, iz = (gint64) fz;
	gdouble tx = lrg_hash_fade ((gdouble) (x - fx)), tz = lrg_hash_fade ((gdouble) (z - fz));
	gdouble a = lrg_hash_unit (lrg_hash_2d (seed, ix, iz));
	gdouble b = lrg_hash_unit (lrg_hash_2d (seed, ix + 1, iz));
	gdouble c = lrg_hash_unit (lrg_hash_2d (seed, ix, iz + 1));
	gdouble d = lrg_hash_unit (lrg_hash_2d (seed, ix + 1, iz + 1));

	return (gfloat) ((a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz);
}

/* ==========================================================================
 * Dabs
 * ========================================================================== */

/* Height ops over the region (samples). */
static void
dab_heights (LrgTerrainBrush *self,
             LrgTerrainAsset *asset,
             gfloat           x,
             gfloat           z,
             gfloat           amount,
             guint            rx,
             guint            rz,
             guint            rw,
             guint            rh)
{
	gfloat            *h = _lrg_terrain_asset_heights (asset);
	gfloat             cell = lrg_terrain_asset_get_cell_size (asset);
	g_autofree gfloat *src = NULL;
	guint              nx, nz, i, j, sx0, sz0, sx1, sz1, sw;

	lrg_terrain_asset_get_samples (asset, &nx, &nz);

	/* smoothing reads the 3 x 3 neighbourhood as it was before the dab */
	sx0 = rx > 0 ? rx - 1 : 0;
	sz0 = rz > 0 ? rz - 1 : 0;
	sx1 = MIN (rx + rw + 1, nx);
	sz1 = MIN (rz + rh + 1, nz);
	sw = sx1 - sx0;
	if (self->op == LRG_TERRAIN_BRUSH_SMOOTH)
	{
		src = g_new (gfloat, (gsize) sw * (sz1 - sz0));
		for (j = sz0; j < sz1; j++)
			memcpy (src + (gsize) (j - sz0) * sw, h + (gsize) j * nx + sx0, sizeof (gfloat) * sw);
	}

	for (j = rz; j < rz + rh; j++)
		for (i = rx; i < rx + rw; i++)
		{
			gfloat  px = (gfloat) i * cell, pz = (gfloat) j * cell;
			gfloat  d = sqrtf ((px - x) * (px - x) + (pz - z) * (pz - z));
			gfloat  w = lrg_terrain_brush_get_weight (self, d);
			gfloat *v = &h[(gsize) j * nx + i];
			gfloat  k = MIN (self->strength * amount * w, 1.0f);

			if (w <= 0.0f)
				continue;
			switch (self->op)
			{
			case LRG_TERRAIN_BRUSH_RAISE:
				*v += self->strength * amount * w;
				break;
			case LRG_TERRAIN_BRUSH_LOWER:
				*v -= self->strength * amount * w;
				break;
			case LRG_TERRAIN_BRUSH_FLATTEN:
				*v += (self->target - *v) * k;
				break;
			case LRG_TERRAIN_BRUSH_SMOOTH:
			{
				gfloat sum = 0.0f;
				guint  n = 0, a, b;

				for (b = (j > 0 ? j - 1 : j); b <= MIN (j + 1, nz - 1); b++)
					for (a = (i > 0 ? i - 1 : i); a <= MIN (i + 1, nx - 1); a++)
					{
						sum += src[(gsize) (b - sz0) * sw + (a - sx0)];
						n++;
					}
				*v = src[(gsize) (j - sz0) * sw + (i - sx0)];
				*v += (sum / (gfloat) n - *v) * k;
				break;
			}
			case LRG_TERRAIN_BRUSH_NOISE:
				*v += self->strength * amount * w *
				      (2.0f * value_noise (self->seed, px / self->noise_scale, pz / self->noise_scale) - 1.0f);
				break;
			default:
				break;
			}
		}
}

/* Weight painting over the region (samples). */
static void
dab_weights (LrgTerrainBrush *self,
             LrgTerrainAsset *asset,
             gfloat           x,
             gfloat           z,
             gfloat           amount,
             guint            rx,
             guint            rz,
             guint            rw,
             guint            rh)
{
	guint8 *weights = _lrg_terrain_asset_weights (asset);
	gfloat  cell = lrg_terrain_asset_get_cell_size (asset);
	guint   n_layers = lrg_terrain_asset_get_n_layers (asset);
	guint   nx, i, j, l;
	guint   target = MIN (self->layer, n_layers - 1);

	lrg_terrain_asset_get_samples (asset, &nx, NULL);
	for (j = rz; j < rz + rh; j++)
		for (i = rx; i < rx + rw; i++)
		{
			gfloat  px = (gfloat) i * cell, pz = (gfloat) j * cell;
			gfloat  w = lrg_terrain_brush_get_weight (self, sqrtf ((px - x) * (px - x) + (pz - z) * (pz - z)));
			gfloat  k = MIN (self->strength * amount * w, 1.0f);
			guint8 *s = weights + ((gsize) j * nx + i) * LRG_TERRAIN_WEIGHT_STRIDE;
			guint   old_t = s[target], new_t, rest_old, rest_new;

			if (k <= 0.0f || old_t >= 255)
				continue;

			/* the target grows by at least one step so slow strokes still
			 * progress; the others share what is left in proportion */
			new_t = old_t + MAX (1u, (guint) lrintf ((gfloat) (255 - old_t) * k));
			new_t = MIN (new_t, 255u);
			rest_old = 255 - old_t;
			rest_new = 255 - new_t;
			for (l = 0; l < n_layers; l++)
			{
				if (l == target)
					continue;
				s[l] = rest_old > 0 ? (guint8) (((guint) s[l] * rest_new + rest_old / 2) / rest_old) : 0;
			}
			s[target] = (guint8) new_t;
			_lrg_terrain_normalize_weights (s, n_layers);
		}
}

/* Cell ops over the region (cells; the cell centre's weight decides). */
static void
dab_cells (LrgTerrainBrush    *self,
           LrgTerrainAsset    *asset,
           LrgTerrainEditKind  kind,
           gfloat              x,
           gfloat              z,
           gfloat              amount,
           guint               rx,
           guint               rz,
           guint               rw,
           guint               rh)
{
	gfloat  cell = lrg_terrain_asset_get_cell_size (asset);
	guint   cx, i, j;
	guint8 *data;

	lrg_terrain_asset_get_cells (asset, &cx, NULL);
	if (kind == LRG_TERRAIN_EDIT_HOLES)
		data = _lrg_terrain_asset_holes (asset);
	else if (kind == LRG_TERRAIN_EDIT_MATERIAL_IDS)
		data = _lrg_terrain_asset_ids (asset);
	else
		data = _lrg_foliage_density (lrg_terrain_asset_get_foliage (asset), self->layer);
	if (data == NULL)
		return;

	for (j = rz; j < rz + rh; j++)
		for (i = rx; i < rx + rw; i++)
		{
			gfloat  px = ((gfloat) i + 0.5f) * cell, pz = ((gfloat) j + 0.5f) * cell;
			gfloat  w = lrg_terrain_brush_get_weight (self, sqrtf ((px - x) * (px - x) + (pz - z) * (pz - z)));
			gfloat  k = MIN (self->strength * amount * w, 1.0f);
			guint8 *v = &data[(gsize) j * cx + i];

			switch (self->op)
			{
			case LRG_TERRAIN_BRUSH_HOLE:
				if (w >= 0.5f)
					*v = 1;
				break;
			case LRG_TERRAIN_BRUSH_FILL:
				if (w >= 0.5f)
					*v = 0;
				break;
			case LRG_TERRAIN_BRUSH_PAINT:
				if (w >= 0.5f)
					*v = (guint8) MIN (self->layer, lrg_terrain_asset_get_n_layers (asset) - 1);
				break;
			case LRG_TERRAIN_BRUSH_FOLIAGE_PAINT:
				if (k > 0.0f && *v < 255)
					*v = (guint8) MIN (255u, *v + MAX (1u, (guint) lrintf ((gfloat) (255 - *v) * k)));
				break;
			case LRG_TERRAIN_BRUSH_FOLIAGE_ERASE:
				if (k > 0.0f && *v > 0)
					*v = (guint8) (*v - MIN ((guint) *v, MAX (1u, (guint) lrintf ((gfloat) *v * k))));
				break;
			default:
				break;
			}
		}
}

LrgTerrainEdit *
lrg_terrain_brush_apply (LrgTerrainBrush *self,
                         LrgTerrainAsset *asset,
                         gfloat           x,
                         gfloat           z,
                         gfloat           amount)
{
	LrgTerrainEdit     *edit;
	LrgTerrainEditKind  kind;
	guint               rx, rz, rw, rh;

	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), NULL);
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (asset), NULL);
	if (!(amount > 0.0f) || !isfinite (amount))
		return NULL;
	if (!lrg_terrain_brush_get_region (self, asset, x, z, &rx, &rz, &rw, &rh))
		return NULL;
	kind = lrg_terrain_brush_get_edit_kind (self, asset);
	edit = lrg_terrain_asset_capture_edit (asset, kind, self->layer, rx, rz, rw, rh);
	if (edit == NULL)
		return NULL;

	if (kind == LRG_TERRAIN_EDIT_HEIGHTS)
		dab_heights (self, asset, x, z, amount, rx, rz, rw, rh);
	else if (kind == LRG_TERRAIN_EDIT_SPLAT)
		dab_weights (self, asset, x, z, amount, rx, rz, rw, rh);
	else
		dab_cells (self, asset, kind, x, z, amount, rx, rz, rw, rh);

	lrg_terrain_edit_capture_after (edit, asset);
	if (lrg_terrain_edit_is_empty (edit))
	{
		lrg_terrain_edit_free (edit);
		return NULL;
	}
	_lrg_terrain_asset_emit_changed (asset, kind, lrg_terrain_edit_get_layer (edit), rx, rz, rw, rh);

	/* fold into the stroke */
	if (self->stroking && asset == self->stroke_asset && kind == self->stroke_kind)
	{
		if (!self->touched)
		{
			self->ux0 = rx;
			self->uz0 = rz;
			self->ux1 = rx + rw;
			self->uz1 = rz + rh;
			self->touched = TRUE;
		}
		else
		{
			self->ux0 = MIN (self->ux0, rx);
			self->uz0 = MIN (self->uz0, rz);
			self->ux1 = MAX (self->ux1, rx + rw);
			self->uz1 = MAX (self->uz1, rz + rh);
		}
	}
	return edit;
}

/* ==========================================================================
 * Strokes
 * ========================================================================== */

void
lrg_terrain_brush_begin_stroke (LrgTerrainBrush *self,
                                LrgTerrainAsset *asset,
                                gfloat           x,
                                gfloat           z)
{
	gfloat h;

	g_return_if_fail (LRG_IS_TERRAIN_BRUSH (self));
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (asset));
	drop_stroke (self);
	self->stroke_asset = g_object_ref (asset);
	self->stroke_kind = lrg_terrain_brush_get_edit_kind (self, asset);
	self->stroke_layer = self->stroke_kind == LRG_TERRAIN_EDIT_FOLIAGE ? self->layer : 0;
	self->snapshot = lrg_terrain_asset_capture_edit (asset, self->stroke_kind, self->layer, 0, 0, G_MAXUINT,
	                                                 G_MAXUINT);
	self->stroking = TRUE;
	if (self->sample_target && self->op == LRG_TERRAIN_BRUSH_FLATTEN &&
	    lrg_terrain_asset_sample_height (asset, x, z, &h))
		lrg_terrain_brush_set_target_height (self, h);
}

gboolean
lrg_terrain_brush_is_stroking (LrgTerrainBrush *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), FALSE);
	return self->stroking;
}

LrgTerrainEdit *
lrg_terrain_brush_end_stroke (LrgTerrainBrush *self)
{
	LrgTerrainEdit    *now, *result;
	g_autoptr(GBytes)  before = NULL;
	const guint8      *snap;
	guint8            *buf;
	gsize              elem;
	guint              snap_w, uw, uh, r;

	g_return_val_if_fail (LRG_IS_TERRAIN_BRUSH (self), NULL);
	if (!self->stroking || !self->touched || self->snapshot == NULL)
	{
		drop_stroke (self);
		return NULL;
	}

	/* after: the union as it is now; before: the same rows of the
	 * snapshot taken at begin */
	uw = self->ux1 - self->ux0;
	uh = self->uz1 - self->uz0;
	now = lrg_terrain_asset_capture_edit (self->stroke_asset, self->stroke_kind, self->stroke_layer, self->ux0,
	                                      self->uz0, uw, uh);
	if (now == NULL)
	{
		drop_stroke (self);
		return NULL;
	}
	elem = _lrg_terrain_edit_elem_size (self->stroke_kind);
	lrg_terrain_edit_get_region (self->snapshot, NULL, NULL, &snap_w, NULL);
	snap = g_bytes_get_data (lrg_terrain_edit_get_before (self->snapshot), NULL);
	buf = g_malloc ((gsize) uw * uh * elem);
	for (r = 0; r < uh; r++)
		memcpy (buf + (gsize) r * uw * elem, snap + ((gsize) (self->uz0 + r) * snap_w + self->ux0) * elem,
		        (gsize) uw * elem);
	before = g_bytes_new_take (buf, (gsize) uw * uh * elem);
	result = _lrg_terrain_edit_new (self->stroke_kind, self->stroke_layer, self->ux0, self->uz0, uw, uh, before,
	                                lrg_terrain_edit_get_after (now));
	lrg_terrain_edit_free (now);
	drop_stroke (self);
	if (lrg_terrain_edit_is_empty (result))
	{
		lrg_terrain_edit_free (result);
		return NULL;
	}
	return result;
}
