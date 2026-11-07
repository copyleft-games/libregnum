/* lrg-decal.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDecal and LrgDecalSet: forward (clustered) decals.
 *
 * A decal is a box (the unit cube under a world matrix) that projects
 * along its local -Y.  The lit shader calls lrg_decals_apply() after
 * sampling the material and before lighting; for each decal covering
 * the fragment it maps the fragment into the box with the inverse
 * matrix (three rows in the data texture), fades by the angle between
 * the geometric normal and the box's up axis and by the depth in the
 * box, and blends colour, normal and roughness/metallic.
 *
 * LrgDecalSet::select() picks the decals of the frame (headless):
 * enabled, in range, ordered by sort order then insertion, and binned
 * into a square XZ grid around the camera with optional vertical
 * layers, the same layout as the light set's light grid (each cell lists
 * up to `slots` decals in application order; when a cell overflows the
 * highest sort orders stay).  upload() packs the decal rows and the
 * cell lists into ONE float texture (decal rows first, cell rows from
 * lrgDecalCellRow on) and copies the decals' textures into an atlas of
 * three stacked bands (albedo, normal, ORM) by reading them back from
 * the GPU and resampling them on the CPU, so it works at any point of a
 * frame, even inside a render pass.  The hook thus needs two texture
 * units.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-decal.h"
#include "lrg-decal-glsl-private.h"
#include "lrg-light-set-private.h"
#include "../core/lrg-math3d.h"

/* ==========================================================================
 * Enumerations
 * ========================================================================== */

GType
lrg_decal_blend_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_DECAL_BLEND_ALPHA, "LRG_DECAL_BLEND_ALPHA", "alpha" },
			{ LRG_DECAL_BLEND_MULTIPLY, "LRG_DECAL_BLEND_MULTIPLY", "multiply" },
			{ LRG_DECAL_BLEND_ADD, "LRG_DECAL_BLEND_ADD", "add" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgDecalBlend"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_decal_channels_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_DECAL_CHANNEL_NONE, "LRG_DECAL_CHANNEL_NONE", "none" },
			{ LRG_DECAL_CHANNEL_ALBEDO, "LRG_DECAL_CHANNEL_ALBEDO", "albedo" },
			{ LRG_DECAL_CHANNEL_NORMAL, "LRG_DECAL_CHANNEL_NORMAL", "normal" },
			{ LRG_DECAL_CHANNEL_ROUGHNESS, "LRG_DECAL_CHANNEL_ROUGHNESS", "roughness" },
			{ LRG_DECAL_CHANNEL_METALLIC, "LRG_DECAL_CHANNEL_METALLIC", "metallic" },
			{ LRG_DECAL_CHANNEL_ALL, "LRG_DECAL_CHANNEL_ALL", "all" },
			{ 0, NULL, NULL }
		};
		GType id = g_flags_register_static (g_intern_static_string ("LrgDecalChannels"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * LrgDecal
 * ========================================================================== */

struct _LrgDecal
{
	GObject           parent_instance;

	gfloat            matrix[16];     /* unit box -> world, column-major */
	gfloat            inverse[16];    /* world -> unit box */
	gboolean          invertible;

	gfloat            color[4];
	gfloat            opacity;
	gfloat            fade_angle;     /* degrees */
	gfloat            fade_width;     /* degrees */
	gfloat            depth_fade;
	gint              sort_order;
	guint32           layer_mask;
	LrgDecalBlend     blend;
	LrgDecalChannels  channels;
	gfloat            normal_strength;
	gfloat            roughness;
	gfloat            metallic;
	gboolean          enabled;
	GrlTexture       *textures[3];    /* albedo, normal, ORM */
	gfloat            uv_rect[4];

	guint             version;
};

G_DEFINE_FINAL_TYPE (LrgDecal, lrg_decal, G_TYPE_OBJECT)

enum
{
	DPROP_0,
	DPROP_COLOR_R,
	DPROP_COLOR_G,
	DPROP_COLOR_B,
	DPROP_COLOR_A,
	DPROP_OPACITY,
	DPROP_FADE_ANGLE,
	DPROP_FADE_WIDTH,
	DPROP_DEPTH_FADE,
	DPROP_SORT_ORDER,
	DPROP_LAYER_MASK,
	DPROP_BLEND,
	DPROP_CHANNELS,
	DPROP_NORMAL_STRENGTH,
	DPROP_ROUGHNESS,
	DPROP_METALLIC,
	DPROP_ENABLED,
	DPROP_ALBEDO_TEXTURE,
	DPROP_NORMAL_TEXTURE,
	DPROP_ORM_TEXTURE,
	N_DPROPS
};

static GParamSpec *decal_props[N_DPROPS];

/* changed: bumps the version and notifies @prop (0: no property) */
static void
changed (LrgDecal *self,
         guint     prop)
{
	self->version++;
	if (prop != 0)
		g_object_notify_by_pspec (G_OBJECT (self), decal_props[prop]);
}

/* set_float_prop: stores @value clamped to [@lo, @hi], notifying on change */
static void
set_float_prop (LrgDecal *self,
                gfloat   *field,
                gfloat    value,
                gfloat    lo,
                gfloat    hi,
                guint     prop)
{
	value = CLAMP (value, lo, hi);
	if (*field == value)
		return;
	*field = value;
	changed (self, prop);
}

/* set_texture_slot: swaps the texture of slot @slot (0 albedo, 1
 * normal, 2 ORM) */
static void
set_texture_slot (LrgDecal   *self,
                  guint       slot,
                  GrlTexture *texture,
                  guint       prop)
{
	if (g_set_object (&self->textures[slot], texture))
		changed (self, prop);
}

static void
lrg_decal_dispose (GObject *object)
{
	LrgDecal *self = LRG_DECAL (object);

	g_clear_object (&self->textures[0]);
	g_clear_object (&self->textures[1]);
	g_clear_object (&self->textures[2]);

	G_OBJECT_CLASS (lrg_decal_parent_class)->dispose (object);
}

static void
lrg_decal_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	LrgDecal *self = LRG_DECAL (object);

	switch (prop_id)
	{
	case DPROP_COLOR_R:
	case DPROP_COLOR_G:
	case DPROP_COLOR_B:
	case DPROP_COLOR_A:
		g_value_set_float (value, self->color[prop_id - DPROP_COLOR_R]);
		break;
	case DPROP_OPACITY:
		g_value_set_float (value, self->opacity);
		break;
	case DPROP_FADE_ANGLE:
		g_value_set_float (value, self->fade_angle);
		break;
	case DPROP_FADE_WIDTH:
		g_value_set_float (value, self->fade_width);
		break;
	case DPROP_DEPTH_FADE:
		g_value_set_float (value, self->depth_fade);
		break;
	case DPROP_SORT_ORDER:
		g_value_set_int (value, self->sort_order);
		break;
	case DPROP_LAYER_MASK:
		g_value_set_uint (value, self->layer_mask);
		break;
	case DPROP_BLEND:
		g_value_set_enum (value, (gint) self->blend);
		break;
	case DPROP_CHANNELS:
		g_value_set_flags (value, (guint) self->channels);
		break;
	case DPROP_NORMAL_STRENGTH:
		g_value_set_float (value, self->normal_strength);
		break;
	case DPROP_ROUGHNESS:
		g_value_set_float (value, self->roughness);
		break;
	case DPROP_METALLIC:
		g_value_set_float (value, self->metallic);
		break;
	case DPROP_ENABLED:
		g_value_set_boolean (value, self->enabled);
		break;
	case DPROP_ALBEDO_TEXTURE:
		g_value_set_object (value, self->textures[0]);
		break;
	case DPROP_NORMAL_TEXTURE:
		g_value_set_object (value, self->textures[1]);
		break;
	case DPROP_ORM_TEXTURE:
		g_value_set_object (value, self->textures[2]);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_decal_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	LrgDecal *self = LRG_DECAL (object);

	switch (prop_id)
	{
	case DPROP_COLOR_R:
	case DPROP_COLOR_G:
	case DPROP_COLOR_B:
	case DPROP_COLOR_A:
		set_float_prop (self, &self->color[prop_id - DPROP_COLOR_R], g_value_get_float (value),
		                0.0f, 1.0f, prop_id);
		break;
	case DPROP_OPACITY:
		lrg_decal_set_opacity (self, g_value_get_float (value));
		break;
	case DPROP_FADE_ANGLE:
		lrg_decal_set_fade_angle (self, g_value_get_float (value));
		break;
	case DPROP_FADE_WIDTH:
		lrg_decal_set_fade_width (self, g_value_get_float (value));
		break;
	case DPROP_DEPTH_FADE:
		lrg_decal_set_depth_fade (self, g_value_get_float (value));
		break;
	case DPROP_SORT_ORDER:
		lrg_decal_set_sort_order (self, g_value_get_int (value));
		break;
	case DPROP_LAYER_MASK:
		lrg_decal_set_layer_mask (self, g_value_get_uint (value));
		break;
	case DPROP_BLEND:
		lrg_decal_set_blend (self, (LrgDecalBlend) g_value_get_enum (value));
		break;
	case DPROP_CHANNELS:
		lrg_decal_set_channels (self, (LrgDecalChannels) g_value_get_flags (value));
		break;
	case DPROP_NORMAL_STRENGTH:
		lrg_decal_set_normal_strength (self, g_value_get_float (value));
		break;
	case DPROP_ROUGHNESS:
		lrg_decal_set_roughness (self, g_value_get_float (value));
		break;
	case DPROP_METALLIC:
		lrg_decal_set_metallic (self, g_value_get_float (value));
		break;
	case DPROP_ENABLED:
		lrg_decal_set_enabled (self, g_value_get_boolean (value));
		break;
	case DPROP_ALBEDO_TEXTURE:
		lrg_decal_set_albedo_texture (self, g_value_get_object (value));
		break;
	case DPROP_NORMAL_TEXTURE:
		lrg_decal_set_normal_texture (self, g_value_get_object (value));
		break;
	case DPROP_ORM_TEXTURE:
		lrg_decal_set_orm_texture (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_decal_class_init (LrgDecalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   flags = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_decal_dispose;
	object_class->get_property = lrg_decal_get_property;
	object_class->set_property = lrg_decal_set_property;

	/**
	 * LrgDecal:color-r:
	 *
	 * Red of the decal colour (sRGB-encoded, 0 .. 1).
	 */
	decal_props[DPROP_COLOR_R] =
		g_param_spec_float ("color-r", NULL, "Colour red", 0.0f, 1.0f, 1.0f, flags);

	/**
	 * LrgDecal:color-g:
	 *
	 * Green of the decal colour.
	 */
	decal_props[DPROP_COLOR_G] =
		g_param_spec_float ("color-g", NULL, "Colour green", 0.0f, 1.0f, 1.0f, flags);

	/**
	 * LrgDecal:color-b:
	 *
	 * Blue of the decal colour.
	 */
	decal_props[DPROP_COLOR_B] =
		g_param_spec_float ("color-b", NULL, "Colour blue", 0.0f, 1.0f, 1.0f, flags);

	/**
	 * LrgDecal:color-a:
	 *
	 * Alpha of the decal colour (scales the coverage).
	 */
	decal_props[DPROP_COLOR_A] =
		g_param_spec_float ("color-a", NULL, "Colour alpha", 0.0f, 1.0f, 1.0f, flags);

	/**
	 * LrgDecal:opacity:
	 *
	 * Overall strength, 0 .. 1.
	 */
	decal_props[DPROP_OPACITY] =
		g_param_spec_float ("opacity", NULL, "Opacity", 0.0f, 1.0f, 1.0f, flags);

	/**
	 * LrgDecal:fade-angle:
	 *
	 * Angle in degrees between the surface normal and the box's up axis
	 * up to which the decal is at full strength.
	 */
	decal_props[DPROP_FADE_ANGLE] =
		g_param_spec_float ("fade-angle", NULL, "Full-strength angle", 0.0f, 180.0f, 60.0f, flags);

	/**
	 * LrgDecal:fade-width:
	 *
	 * Degrees over which the decal fades out past the fade angle.
	 */
	decal_props[DPROP_FADE_WIDTH] =
		g_param_spec_float ("fade-width", NULL, "Fade width", 0.0f, 180.0f, 20.0f, flags);

	/**
	 * LrgDecal:depth-fade:
	 *
	 * Share of the half depth over which the decal fades towards the
	 * box's top and bottom faces.
	 */
	decal_props[DPROP_DEPTH_FADE] =
		g_param_spec_float ("depth-fade", NULL, "Depth fade", 0.0f, 1.0f, 0.0f, flags);

	/**
	 * LrgDecal:sort-order:
	 *
	 * Application order; higher draws on top.
	 */
	decal_props[DPROP_SORT_ORDER] =
		g_param_spec_int ("sort-order", NULL, "Sort order", G_MININT, G_MAXINT, 0, flags);

	/**
	 * LrgDecal:layer-mask:
	 *
	 * Render layers whose surfaces receive the decal.
	 */
	decal_props[DPROP_LAYER_MASK] =
		g_param_spec_uint ("layer-mask", NULL, "Receiving layers", 0, G_MAXUINT32, G_MAXUINT32,
		                   flags);

	/**
	 * LrgDecal:blend:
	 *
	 * How the colour combines with the albedo.
	 */
	decal_props[DPROP_BLEND] =
		g_param_spec_enum ("blend", NULL, "Blend mode", LRG_TYPE_DECAL_BLEND,
		                   LRG_DECAL_BLEND_ALPHA, flags);

	/**
	 * LrgDecal:channels:
	 *
	 * The surface properties the decal writes.
	 */
	decal_props[DPROP_CHANNELS] =
		g_param_spec_flags ("channels", NULL, "Written channels", LRG_TYPE_DECAL_CHANNELS,
		                    LRG_DECAL_CHANNEL_ALBEDO | LRG_DECAL_CHANNEL_NORMAL, flags);

	/**
	 * LrgDecal:normal-strength:
	 *
	 * How far the normal turns to the decal's normal map.
	 */
	decal_props[DPROP_NORMAL_STRENGTH] =
		g_param_spec_float ("normal-strength", NULL, "Normal strength", 0.0f, 1.0f, 1.0f, flags);

	/**
	 * LrgDecal:roughness:
	 *
	 * Roughness written without an ORM map.
	 */
	decal_props[DPROP_ROUGHNESS] =
		g_param_spec_float ("roughness", NULL, "Roughness", 0.0f, 1.0f, 0.5f, flags);

	/**
	 * LrgDecal:metallic:
	 *
	 * Metallic written without an ORM map.
	 */
	decal_props[DPROP_METALLIC] =
		g_param_spec_float ("metallic", NULL, "Metallic", 0.0f, 1.0f, 0.0f, flags);

	/**
	 * LrgDecal:enabled:
	 *
	 * Whether the decal draws.
	 */
	decal_props[DPROP_ENABLED] =
		g_param_spec_boolean ("enabled", NULL, "Draws", TRUE, flags);

	/**
	 * LrgDecal:albedo-texture:
	 *
	 * Albedo and coverage map.
	 */
	decal_props[DPROP_ALBEDO_TEXTURE] =
		g_param_spec_object ("albedo-texture", NULL, "Albedo map", GRL_TYPE_TEXTURE, flags);

	/**
	 * LrgDecal:normal-texture:
	 *
	 * Tangent-space normal map.
	 */
	decal_props[DPROP_NORMAL_TEXTURE] =
		g_param_spec_object ("normal-texture", NULL, "Normal map", GRL_TYPE_TEXTURE, flags);

	/**
	 * LrgDecal:orm-texture:
	 *
	 * Occlusion/roughness/metallic map.
	 */
	decal_props[DPROP_ORM_TEXTURE] =
		g_param_spec_object ("orm-texture", NULL, "ORM map", GRL_TYPE_TEXTURE, flags);

	g_object_class_install_properties (object_class, N_DPROPS, decal_props);
}

static void
lrg_decal_init (LrgDecal *self)
{
	lrg_math3d_identity (self->matrix);
	lrg_math3d_identity (self->inverse);
	self->invertible = TRUE;
	self->color[0] = self->color[1] = self->color[2] = self->color[3] = 1.0f;
	self->opacity = 1.0f;
	self->fade_angle = 60.0f;
	self->fade_width = 20.0f;
	self->layer_mask = G_MAXUINT32;
	self->blend = LRG_DECAL_BLEND_ALPHA;
	self->channels = LRG_DECAL_CHANNEL_ALBEDO | LRG_DECAL_CHANNEL_NORMAL;
	self->normal_strength = 1.0f;
	self->roughness = 0.5f;
	self->enabled = TRUE;
	self->uv_rect[2] = 1.0f;
	self->uv_rect[3] = 1.0f;
}

LrgDecal *
lrg_decal_new (void)
{
	return (LrgDecal *) g_object_new (LRG_TYPE_DECAL, NULL);
}

void
lrg_decal_set_transform (LrgDecal     *self,
                         const gfloat *matrix)
{
	g_return_if_fail (LRG_IS_DECAL (self));
	g_return_if_fail (matrix != NULL);

	if (memcmp (self->matrix, matrix, sizeof self->matrix) == 0)
		return;
	memcpy (self->matrix, matrix, sizeof self->matrix);
	self->invertible = lrg_math3d_invert (self->matrix, self->inverse);
	changed (self, 0);
}

void
lrg_decal_get_transform (LrgDecal *self,
                         gfloat   *out_matrix)
{
	g_return_if_fail (LRG_IS_DECAL (self));
	g_return_if_fail (out_matrix != NULL);

	memcpy (out_matrix, self->matrix, sizeof self->matrix);
}

void
lrg_decal_set_trs (LrgDecal     *self,
                   const gfloat *position,
                   const gfloat *rotation,
                   const gfloat *size)
{
	static const gfloat identity_q[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	gfloat m[16];

	g_return_if_fail (LRG_IS_DECAL (self));
	g_return_if_fail (position != NULL && size != NULL);

	lrg_math3d_compose_quat (position, rotation != NULL ? rotation : identity_q, size, m);
	lrg_decal_set_transform (self, m);
}

void
lrg_decal_get_world_bounds (LrgDecal *self,
                            gfloat   *out_min,
                            gfloat   *out_max)
{
	guint c;
	guint k;

	g_return_if_fail (LRG_IS_DECAL (self));
	g_return_if_fail (out_min != NULL && out_max != NULL);

	/* the eight corners of the unit box through the matrix */
	for (c = 0; c < 8; c++)
	{
		gfloat corner[3];
		gfloat w[3];

		corner[0] = (c & 1) ? 0.5f : -0.5f;
		corner[1] = (c & 2) ? 0.5f : -0.5f;
		corner[2] = (c & 4) ? 0.5f : -0.5f;
		lrg_math3d_transform_point (self->matrix, corner, w);
		for (k = 0; k < 3; k++)
		{
			if (c == 0 || w[k] < out_min[k])
				out_min[k] = w[k];
			if (c == 0 || w[k] > out_max[k])
				out_max[k] = w[k];
		}
	}
}

gboolean
lrg_decal_project_point (LrgDecal     *self,
                         const gfloat *point,
                         gfloat       *out_local)
{
	gfloat l[3];

	g_return_val_if_fail (LRG_IS_DECAL (self), FALSE);
	g_return_val_if_fail (point != NULL, FALSE);

	lrg_math3d_transform_point (self->inverse, point, l);
	if (out_local != NULL)
		memcpy (out_local, l, sizeof l);
	return self->invertible && fabsf (l[0]) <= 0.5f && fabsf (l[1]) <= 0.5f
	       && fabsf (l[2]) <= 0.5f;
}

/* smoothstep: GLSL's smoothstep */
static gdouble
smoothstep (gdouble e0,
            gdouble e1,
            gdouble x)
{
	gdouble t = CLAMP ((x - e0) / (e1 - e0), 0.0, 1.0);

	return t * t * (3.0 - 2.0 * t);
}

/* fade_cosines: the cosines of the full-strength angle and of the angle
 * where the decal is gone (the shader's pm.y and pm.z) */
static void
fade_cosines (LrgDecal *self,
              gfloat   *out_start,
              gfloat   *out_end)
{
	gdouble a = (gdouble) self->fade_angle * G_PI / 180.0;
	gdouble b = MIN ((gdouble) (self->fade_angle + self->fade_width), 180.0) * G_PI / 180.0;

	*out_start = (gfloat) cos (a);
	*out_end = (gfloat) cos (b);
}

gfloat
lrg_decal_get_coverage (LrgDecal     *self,
                        const gfloat *point,
                        const gfloat *normal)
{
	gfloat  l[3];
	gdouble a;

	g_return_val_if_fail (LRG_IS_DECAL (self), 0.0f);
	g_return_val_if_fail (point != NULL, 0.0f);

	if (!self->enabled || !lrg_decal_project_point (self, point, l))
		return 0.0f;
	a = self->opacity;

	/* angle fade: the normal against the box's up axis (row 1 of the
	 * inverse, column-major element (1, j) = inverse[j * 4 + 1]) */
	if (normal != NULL)
	{
		gdouble up[3];
		gdouble ul, nl, d;
		gfloat  cs, ce;

		up[0] = self->inverse[1];
		up[1] = self->inverse[5];
		up[2] = self->inverse[9];
		ul = sqrt (up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
		nl = sqrt ((gdouble) normal[0] * normal[0] + (gdouble) normal[1] * normal[1]
		           + (gdouble) normal[2] * normal[2]);
		if (ul <= 0.0 || nl <= 0.0)
			return 0.0f;
		d = (up[0] * normal[0] + up[1] * normal[1] + up[2] * normal[2]) / (ul * nl);
		fade_cosines (self, &cs, &ce);
		a *= smoothstep ((gdouble) ce - 1e-4, (gdouble) cs, d);
	}

	/* depth fade towards the top and bottom faces */
	if (self->depth_fade > 0.0f)
		a *= 1.0 - smoothstep (1.0 - self->depth_fade, 1.0, fabs ((gdouble) l[1]) * 2.0);

	return (gfloat) CLAMP (a * self->color[3], 0.0, 1.0);
}

void
lrg_decal_set_color (LrgDecal *self,
                     gfloat    r,
                     gfloat    g,
                     gfloat    b,
                     gfloat    a)
{
	g_return_if_fail (LRG_IS_DECAL (self));

	g_object_freeze_notify (G_OBJECT (self));
	set_float_prop (self, &self->color[0], r, 0.0f, 1.0f, DPROP_COLOR_R);
	set_float_prop (self, &self->color[1], g, 0.0f, 1.0f, DPROP_COLOR_G);
	set_float_prop (self, &self->color[2], b, 0.0f, 1.0f, DPROP_COLOR_B);
	set_float_prop (self, &self->color[3], a, 0.0f, 1.0f, DPROP_COLOR_A);
	g_object_thaw_notify (G_OBJECT (self));
}

void
lrg_decal_get_color (LrgDecal *self,
                     gfloat   *out_rgba)
{
	g_return_if_fail (LRG_IS_DECAL (self));
	g_return_if_fail (out_rgba != NULL);

	memcpy (out_rgba, self->color, sizeof self->color);
}

void
lrg_decal_set_opacity (LrgDecal *self,
                       gfloat    opacity)
{
	g_return_if_fail (LRG_IS_DECAL (self));

	set_float_prop (self, &self->opacity, opacity, 0.0f, 1.0f, DPROP_OPACITY);
}

gfloat
lrg_decal_get_opacity (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), 0.0f);

	return self->opacity;
}

void
lrg_decal_set_fade_angle (LrgDecal *self,
                          gfloat    degrees)
{
	g_return_if_fail (LRG_IS_DECAL (self));

	set_float_prop (self, &self->fade_angle, degrees, 0.0f, 180.0f, DPROP_FADE_ANGLE);
}

gfloat
lrg_decal_get_fade_angle (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), 0.0f);

	return self->fade_angle;
}

void
lrg_decal_set_fade_width (LrgDecal *self,
                          gfloat    degrees)
{
	g_return_if_fail (LRG_IS_DECAL (self));

	set_float_prop (self, &self->fade_width, degrees, 0.0f, 180.0f, DPROP_FADE_WIDTH);
}

gfloat
lrg_decal_get_fade_width (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), 0.0f);

	return self->fade_width;
}

void
lrg_decal_set_depth_fade (LrgDecal *self,
                          gfloat    fade)
{
	g_return_if_fail (LRG_IS_DECAL (self));

	set_float_prop (self, &self->depth_fade, fade, 0.0f, 1.0f, DPROP_DEPTH_FADE);
}

gfloat
lrg_decal_get_depth_fade (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), 0.0f);

	return self->depth_fade;
}

void
lrg_decal_set_sort_order (LrgDecal *self,
                          gint      order)
{
	g_return_if_fail (LRG_IS_DECAL (self));

	if (self->sort_order == order)
		return;
	self->sort_order = order;
	changed (self, DPROP_SORT_ORDER);
}

gint
lrg_decal_get_sort_order (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), 0);

	return self->sort_order;
}

void
lrg_decal_set_layer_mask (LrgDecal *self,
                          guint32   mask)
{
	g_return_if_fail (LRG_IS_DECAL (self));

	if (self->layer_mask == mask)
		return;
	self->layer_mask = mask;
	changed (self, DPROP_LAYER_MASK);
}

guint32
lrg_decal_get_layer_mask (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), 0);

	return self->layer_mask;
}

void
lrg_decal_set_blend (LrgDecal      *self,
                     LrgDecalBlend  blend)
{
	g_return_if_fail (LRG_IS_DECAL (self));
	g_return_if_fail (blend <= LRG_DECAL_BLEND_ADD);

	if (self->blend == blend)
		return;
	self->blend = blend;
	changed (self, DPROP_BLEND);
}

LrgDecalBlend
lrg_decal_get_blend (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), LRG_DECAL_BLEND_ALPHA);

	return self->blend;
}

void
lrg_decal_set_channels (LrgDecal         *self,
                        LrgDecalChannels  channels)
{
	g_return_if_fail (LRG_IS_DECAL (self));

	channels &= LRG_DECAL_CHANNEL_ALL;
	if (self->channels == channels)
		return;
	self->channels = channels;
	changed (self, DPROP_CHANNELS);
}

LrgDecalChannels
lrg_decal_get_channels (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), LRG_DECAL_CHANNEL_NONE);

	return self->channels;
}

void
lrg_decal_set_normal_strength (LrgDecal *self,
                               gfloat    strength)
{
	g_return_if_fail (LRG_IS_DECAL (self));

	set_float_prop (self, &self->normal_strength, strength, 0.0f, 1.0f, DPROP_NORMAL_STRENGTH);
}

gfloat
lrg_decal_get_normal_strength (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), 0.0f);

	return self->normal_strength;
}

void
lrg_decal_set_roughness (LrgDecal *self,
                         gfloat    roughness)
{
	g_return_if_fail (LRG_IS_DECAL (self));

	set_float_prop (self, &self->roughness, roughness, 0.0f, 1.0f, DPROP_ROUGHNESS);
}

gfloat
lrg_decal_get_roughness (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), 0.0f);

	return self->roughness;
}

void
lrg_decal_set_metallic (LrgDecal *self,
                        gfloat    metallic)
{
	g_return_if_fail (LRG_IS_DECAL (self));

	set_float_prop (self, &self->metallic, metallic, 0.0f, 1.0f, DPROP_METALLIC);
}

gfloat
lrg_decal_get_metallic (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), 0.0f);

	return self->metallic;
}

void
lrg_decal_set_enabled (LrgDecal *self,
                       gboolean  enabled)
{
	g_return_if_fail (LRG_IS_DECAL (self));

	enabled = !!enabled;
	if (self->enabled == enabled)
		return;
	self->enabled = enabled;
	changed (self, DPROP_ENABLED);
}

gboolean
lrg_decal_get_enabled (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), FALSE);

	return self->enabled;
}

void
lrg_decal_set_albedo_texture (LrgDecal   *self,
                              GrlTexture *texture)
{
	g_return_if_fail (LRG_IS_DECAL (self));
	g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));

	set_texture_slot (self, 0, texture, DPROP_ALBEDO_TEXTURE);
}

GrlTexture *
lrg_decal_get_albedo_texture (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), NULL);

	return self->textures[0];
}

void
lrg_decal_set_normal_texture (LrgDecal   *self,
                              GrlTexture *texture)
{
	g_return_if_fail (LRG_IS_DECAL (self));
	g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));

	set_texture_slot (self, 1, texture, DPROP_NORMAL_TEXTURE);
}

GrlTexture *
lrg_decal_get_normal_texture (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), NULL);

	return self->textures[1];
}

void
lrg_decal_set_orm_texture (LrgDecal   *self,
                           GrlTexture *texture)
{
	g_return_if_fail (LRG_IS_DECAL (self));
	g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));

	set_texture_slot (self, 2, texture, DPROP_ORM_TEXTURE);
}

GrlTexture *
lrg_decal_get_orm_texture (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), NULL);

	return self->textures[2];
}

void
lrg_decal_set_uv_rect (LrgDecal *self,
                       gfloat    x,
                       gfloat    y,
                       gfloat    width,
                       gfloat    height)
{
	gfloat r[4];

	g_return_if_fail (LRG_IS_DECAL (self));

	r[0] = CLAMP (x, 0.0f, 1.0f);
	r[1] = CLAMP (y, 0.0f, 1.0f);
	r[2] = CLAMP (width, 0.0f, 1.0f - r[0]);
	r[3] = CLAMP (height, 0.0f, 1.0f - r[1]);
	if (memcmp (r, self->uv_rect, sizeof r) == 0)
		return;
	memcpy (self->uv_rect, r, sizeof r);
	changed (self, 0);
}

void
lrg_decal_get_uv_rect (LrgDecal *self,
                       gfloat   *out_rect)
{
	g_return_if_fail (LRG_IS_DECAL (self));
	g_return_if_fail (out_rect != NULL);

	memcpy (out_rect, self->uv_rect, sizeof self->uv_rect);
}

guint
lrg_decal_get_version (LrgDecal *self)
{
	g_return_val_if_fail (LRG_IS_DECAL (self), 0);

	return self->version;
}

/* ==========================================================================
 * LrgDecalSet
 * ========================================================================== */

/* one atlas tile: the texture combination it holds (owned refs) */
typedef struct
{
	gboolean    used;
	gboolean    current;   /* blitted into the GPU atlas */
	GrlTexture *textures[3];
} AtlasTile;

/* one selection candidate */
typedef struct
{
	LrgDecal *decal;
	guint     insertion;
	gdouble   distance;   /* camera to bounds */
	gfloat    lo[3];
	gfloat    hi[3];
} Candidate;

struct _LrgDecalSet
{
	GObject     parent_instance;

	GPtrArray  *decals;          /* owned refs, insertion order */
	gdouble     origin[3];

	/* grid (cells == 0: none) */
	guint       grid_cells;
	gfloat      grid_cell;
	guint       grid_layers;
	gfloat      grid_layer_height;
	guint       grid_slots;
	gboolean    grid_built;
	gdouble     grid_x0;
	gdouble     grid_z0;
	gdouble     grid_y0;
	guint      *cell_ids;        /* cells * cells * layers * slots */
	guint8     *cell_counts;

	gfloat      max_distance;
	guint32     receiver;

	/* the last selection (owned refs) in application order */
	GPtrArray  *active;
	guint       select_serial;

	/* atlas */
	guint       atlas_size;
	guint       atlas_tiles;
	AtlasTile  *tiles;           /* atlas_tiles squared */
	gint       *active_tile;     /* per active decal, -1: none */
	gboolean    warned_tiles;

	/* GPU */
	guint       data_tex;
	gint        data_w;
	gint        data_h;
	gfloat     *data_buf;
	gsize       data_len;
	guint       atlas_tex;
	guint       atlas_gpu_size;
	guint       uploaded_serial;
	gint        cell_row;
};

G_DEFINE_FINAL_TYPE (LrgDecalSet, lrg_decal_set, G_TYPE_OBJECT)

/* tiles_free: drops every tile's textures (and the GPU copies' validity) */
static void
tiles_free (LrgDecalSet *self)
{
	guint i;
	guint n = self->atlas_tiles * self->atlas_tiles;

	if (self->tiles == NULL)
		return;
	for (i = 0; i < n; i++)
	{
		g_clear_object (&self->tiles[i].textures[0]);
		g_clear_object (&self->tiles[i].textures[1]);
		g_clear_object (&self->tiles[i].textures[2]);
	}
	g_clear_pointer (&self->tiles, g_free);
}

static void
gpu_free (LrgDecalSet *self)
{
	guint i;

	if (IsWindowReady ())
	{
		if (self->data_tex != 0)
			rlUnloadTexture (self->data_tex);
		if (self->atlas_tex != 0)
			rlUnloadTexture (self->atlas_tex);
	}
	self->data_tex = 0;
	self->data_w = 0;
	self->data_h = 0;
	self->atlas_tex = 0;
	self->atlas_gpu_size = 0;
	self->uploaded_serial = 0;
	if (self->tiles != NULL)
		for (i = 0; i < self->atlas_tiles * self->atlas_tiles; i++)
			self->tiles[i].current = FALSE;
}

static void
lrg_decal_set_dispose (GObject *object)
{
	LrgDecalSet *self = LRG_DECAL_SET (object);

	gpu_free (self);
	tiles_free (self);
	g_clear_pointer (&self->decals, g_ptr_array_unref);
	g_clear_pointer (&self->active, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_decal_set_parent_class)->dispose (object);
}

static void
lrg_decal_set_finalize (GObject *object)
{
	LrgDecalSet *self = LRG_DECAL_SET (object);

	g_free (self->cell_ids);
	g_free (self->cell_counts);
	g_free (self->active_tile);
	g_free (self->data_buf);

	G_OBJECT_CLASS (lrg_decal_set_parent_class)->finalize (object);
}

static void
lrg_decal_set_class_init (LrgDecalSetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_decal_set_dispose;
	object_class->finalize = lrg_decal_set_finalize;
}

static void
lrg_decal_set_init (LrgDecalSet *self)
{
	self->decals = g_ptr_array_new_with_free_func (g_object_unref);
	self->active = g_ptr_array_new_with_free_func (g_object_unref);
	self->grid_layers = 1;
	self->grid_layer_height = 4.0f;
	self->grid_slots = 8;
	self->receiver = G_MAXUINT32;
	self->atlas_size = 1024;
	self->atlas_tiles = 4;
	self->select_serial = 1;
	self->cell_row = 1;
}

LrgDecalSet *
lrg_decal_set_new (void)
{
	return (LrgDecalSet *) g_object_new (LRG_TYPE_DECAL_SET, NULL);
}

void
lrg_decal_set_add (LrgDecalSet *self,
                   LrgDecal    *decal)
{
	g_return_if_fail (LRG_IS_DECAL_SET (self));
	g_return_if_fail (LRG_IS_DECAL (decal));

	if (g_ptr_array_find (self->decals, decal, NULL))
		return;
	g_ptr_array_add (self->decals, g_object_ref (decal));
}

gboolean
lrg_decal_set_remove (LrgDecalSet *self,
                      LrgDecal    *decal)
{
	g_return_val_if_fail (LRG_IS_DECAL_SET (self), FALSE);
	g_return_val_if_fail (LRG_IS_DECAL (decal), FALSE);

	/* keep the insertion order (it breaks sort-order ties) */
	return g_ptr_array_remove (self->decals, decal);
}

void
lrg_decal_set_clear (LrgDecalSet *self)
{
	g_return_if_fail (LRG_IS_DECAL_SET (self));

	g_ptr_array_set_size (self->decals, 0);
}

guint
lrg_decal_set_get_count (LrgDecalSet *self)
{
	g_return_val_if_fail (LRG_IS_DECAL_SET (self), 0);

	return self->decals->len;
}

LrgDecal *
lrg_decal_set_get_decal (LrgDecalSet *self,
                         guint        index)
{
	g_return_val_if_fail (LRG_IS_DECAL_SET (self), NULL);

	if (index >= self->decals->len)
		return NULL;
	return g_ptr_array_index (self->decals, index);
}

void
lrg_decal_set_set_origin (LrgDecalSet *self,
                          gdouble      x,
                          gdouble      y,
                          gdouble      z)
{
	g_return_if_fail (LRG_IS_DECAL_SET (self));

	if (self->origin[0] == x && self->origin[1] == y && self->origin[2] == z)
		return;
	self->origin[0] = x;
	self->origin[1] = y;
	self->origin[2] = z;
	self->select_serial++;
}

void
lrg_decal_set_get_origin (LrgDecalSet *self,
                          gdouble     *out_x,
                          gdouble     *out_y,
                          gdouble     *out_z)
{
	g_return_if_fail (LRG_IS_DECAL_SET (self));

	if (out_x != NULL)
		*out_x = self->origin[0];
	if (out_y != NULL)
		*out_y = self->origin[1];
	if (out_z != NULL)
		*out_z = self->origin[2];
}

/* grid_alloc: (re)allocates the cell lists for the grid's shape */
static void
grid_alloc (LrgDecalSet *self)
{
	gsize n;

	g_clear_pointer (&self->cell_ids, g_free);
	g_clear_pointer (&self->cell_counts, g_free);
	self->grid_built = FALSE;
	self->select_serial++;
	if (self->grid_cells == 0)
		return;
	n = (gsize) self->grid_cells * self->grid_cells * self->grid_layers;
	self->cell_ids = g_new (guint, n * self->grid_slots);
	self->cell_counts = g_new0 (guint8, n);
}

void
lrg_decal_set_set_grid (LrgDecalSet *self,
                        guint        cells,
                        gfloat       cell_size)
{
	g_return_if_fail (LRG_IS_DECAL_SET (self));
	g_return_if_fail (cells == 0 || (cells >= 2 && cells <= LRG_DECAL_SET_MAX_CELLS));
	g_return_if_fail (cells == 0 || cell_size > 0.0f);

	self->grid_cells = cells;
	self->grid_cell = cell_size;
	grid_alloc (self);
}

guint
lrg_decal_set_get_grid_cells (LrgDecalSet *self)
{
	g_return_val_if_fail (LRG_IS_DECAL_SET (self), 0);

	return self->grid_cells;
}

void
lrg_decal_set_set_grid_layers (LrgDecalSet *self,
                               guint        layers,
                               gfloat       layer_height)
{
	g_return_if_fail (LRG_IS_DECAL_SET (self));
	g_return_if_fail (layers >= 1 && layers <= LRG_DECAL_SET_MAX_LAYERS);
	g_return_if_fail (layer_height > 0.0f);

	self->grid_layers = layers;
	self->grid_layer_height = layer_height;
	grid_alloc (self);
}

void
lrg_decal_set_set_grid_slots (LrgDecalSet *self,
                              guint        slots)
{
	g_return_if_fail (LRG_IS_DECAL_SET (self));
	g_return_if_fail (slots >= 4 && slots <= LRG_DECAL_SET_MAX_SLOTS && slots % 4 == 0);

	self->grid_slots = slots;
	grid_alloc (self);
}

guint
lrg_decal_set_get_grid_slots (LrgDecalSet *self)
{
	g_return_val_if_fail (LRG_IS_DECAL_SET (self), 0);

	return self->grid_slots;
}

void
lrg_decal_set_set_max_distance (LrgDecalSet *self,
                                gfloat       distance)
{
	g_return_if_fail (LRG_IS_DECAL_SET (self));

	self->max_distance = MAX (distance, 0.0f);
}

gfloat
lrg_decal_set_get_max_distance (LrgDecalSet *self)
{
	g_return_val_if_fail (LRG_IS_DECAL_SET (self), 0.0f);

	return self->max_distance;
}

void
lrg_decal_set_set_receiver_layers (LrgDecalSet *self,
                                   guint32      mask)
{
	g_return_if_fail (LRG_IS_DECAL_SET (self));

	self->receiver = mask;
}

guint32
lrg_decal_set_get_receiver_layers (LrgDecalSet *self)
{
	g_return_val_if_fail (LRG_IS_DECAL_SET (self), 0);

	return self->receiver;
}

void
lrg_decal_set_set_atlas (LrgDecalSet *self,
                         guint        size,
                         guint        tiles)
{
	g_return_if_fail (LRG_IS_DECAL_SET (self));
	g_return_if_fail (size >= 64 && size <= 4096);
	g_return_if_fail (tiles >= 1 && tiles <= 16);

	if (self->atlas_size == size && self->atlas_tiles == tiles)
		return;
	gpu_free (self);
	tiles_free (self);
	self->atlas_size = size;
	self->atlas_tiles = tiles;
	self->warned_tiles = FALSE;
	self->select_serial++;
}

guint
lrg_decal_set_get_atlas_size (LrgDecalSet *self)
{
	g_return_val_if_fail (LRG_IS_DECAL_SET (self), 0);

	return self->atlas_size;
}

guint
lrg_decal_set_get_atlas_tiles (LrgDecalSet *self)
{
	g_return_val_if_fail (LRG_IS_DECAL_SET (self), 0);

	return self->atlas_tiles;
}

/* ------------------------------------------------------------------------
 * Selection
 * ------------------------------------------------------------------------ */

/* box_distance: from (@x, @y, @z) to the box [@lo, @hi] (0 inside) */
static gdouble
box_distance (const gfloat *lo,
              const gfloat *hi,
              gdouble       x,
              gdouble       y,
              gdouble       z)
{
	gdouble p[3];
	gdouble d = 0.0;
	guint   k;

	p[0] = x;
	p[1] = y;
	p[2] = z;
	for (k = 0; k < 3; k++)
	{
		gdouble e = 0.0;

		if (p[k] < (gdouble) lo[k])
			e = (gdouble) lo[k] - p[k];
		else if (p[k] > (gdouble) hi[k])
			e = p[k] - (gdouble) hi[k];
		d += e * e;
	}
	return sqrt (d);
}

/* by_distance: nearest first, insertion breaking ties */
static gint
by_distance (gconstpointer a,
             gconstpointer b)
{
	const Candidate *ca = a;
	const Candidate *cb = b;

	if (ca->distance != cb->distance)
		return ca->distance < cb->distance ? -1 : 1;
	return (ca->insertion < cb->insertion) ? -1 : (ca->insertion > cb->insertion);
}

/* by_order: application order (sort order, then insertion) */
static gint
by_order (gconstpointer a,
          gconstpointer b)
{
	const Candidate *ca = a;
	const Candidate *cb = b;
	gint             oa = lrg_decal_get_sort_order (ca->decal);
	gint             ob = lrg_decal_get_sort_order (cb->decal);

	if (oa != ob)
		return oa < ob ? -1 : 1;
	return (ca->insertion < cb->insertion) ? -1 : (ca->insertion > cb->insertion);
}

/* layer_of: the grid layer of world height @y, clamped (outer layers
 * reach on without end, as in the light set) */
static gint
layer_of (LrgDecalSet *self,
          gdouble      y)
{
	if (self->grid_layers <= 1)
		return 0;
	return (gint) CLAMP (floor ((y - self->grid_y0) / (gdouble) self->grid_layer_height), 0.0,
	                     (gdouble) (self->grid_layers - 1));
}

/* build_cells:
 * Fills the cell lists from the active decals (application order).
 * Walking from the last (highest order) down keeps the highest orders
 * when a cell is full; each list is then reversed into application
 * order. */
static void
build_cells (LrgDecalSet *self,
             GArray      *chosen)
{
	guint   n = self->grid_cells;
	guint   slots = self->grid_slots;
	gdouble cell = (gdouble) self->grid_cell;
	gsize   n_cells = (gsize) n * n * self->grid_layers;
	gsize   c;
	gint    i;

	memset (self->cell_counts, 0, n_cells);
	for (i = (gint) chosen->len - 1; i >= 0; i--)
	{
		Candidate *cd = &g_array_index (chosen, Candidate, i);
		gint       gx0, gx1, gz0, gz1, l0, l1, gx, gz, l;

		gx0 = (gint) CLAMP (floor (((gdouble) cd->lo[0] - self->grid_x0) / cell), 0.0, (gdouble) n - 1);
		gx1 = (gint) CLAMP (floor (((gdouble) cd->hi[0] - self->grid_x0) / cell), 0.0, (gdouble) n - 1);
		gz0 = (gint) CLAMP (floor (((gdouble) cd->lo[2] - self->grid_z0) / cell), 0.0, (gdouble) n - 1);
		gz1 = (gint) CLAMP (floor (((gdouble) cd->hi[2] - self->grid_z0) / cell), 0.0, (gdouble) n - 1);
		l0 = layer_of (self, (gdouble) cd->lo[1]);
		l1 = layer_of (self, (gdouble) cd->hi[1]);
		for (l = l0; l <= l1; l++)
			for (gz = gz0; gz <= gz1; gz++)
				for (gx = gx0; gx <= gx1; gx++)
				{
					gsize cc = ((gsize) l * n + (gsize) gz) * n + (gsize) gx;

					if (self->cell_counts[cc] < slots)
						self->cell_ids[cc * slots + self->cell_counts[cc]++] = (guint) i;
				}
	}

	/* reverse each list into application order */
	for (c = 0; c < n_cells; c++)
	{
		guint *ids = self->cell_ids + c * slots;
		guint  cnt = self->cell_counts[c];
		guint  k;

		for (k = 0; k < cnt / 2; k++)
		{
			guint t = ids[k];

			ids[k] = ids[cnt - 1 - k];
			ids[cnt - 1 - k] = t;
		}
	}
	self->grid_built = TRUE;
}

guint
lrg_decal_set_select (LrgDecalSet *self,
                      gdouble      camera_x,
                      gdouble      camera_y,
                      gdouble      camera_z)
{
	g_autoptr(GArray) chosen = NULL;
	guint   i;
	guint   limit;
	gdouble gx0 = 0.0, gz0 = 0.0, extent = 0.0;

	g_return_val_if_fail (LRG_IS_DECAL_SET (self), 0);

	/* the grid around the camera's cell, as the light set places it */
	if (self->grid_cells > 0)
	{
		gdouble cell = (gdouble) self->grid_cell;
		gdouble lh = (gdouble) self->grid_layer_height;
		guint   n = self->grid_cells;

		gx0 = (floor (camera_x / cell) - (gdouble) (n / 2)) * cell;
		gz0 = (floor (camera_z / cell) - (gdouble) (n / 2)) * cell;
		self->grid_x0 = gx0;
		self->grid_z0 = gz0;
		self->grid_y0 = (floor (camera_y / lh) - (gdouble) (self->grid_layers / 2)) * lh;
		extent = cell * (gdouble) n;
	}

	/* candidates: drawable, in the grid or the distance limit */
	chosen = g_array_new (FALSE, FALSE, sizeof (Candidate));
	for (i = 0; i < self->decals->len; i++)
	{
		LrgDecal  *d = g_ptr_array_index (self->decals, i);
		Candidate  cd;

		if (!d->enabled || !d->invertible || d->opacity <= 0.0f || d->color[3] <= 0.0f
		    || d->channels == LRG_DECAL_CHANNEL_NONE)
			continue;
		cd.decal = d;
		cd.insertion = i;
		lrg_decal_get_world_bounds (d, cd.lo, cd.hi);
		cd.distance = box_distance (cd.lo, cd.hi, camera_x, camera_y, camera_z);
		if (self->max_distance > 0.0f && cd.distance > (gdouble) self->max_distance)
			continue;
		if (self->grid_cells > 0
		    && ((gdouble) cd.hi[0] < gx0 || (gdouble) cd.lo[0] >= gx0 + extent
		        || (gdouble) cd.hi[2] < gz0 || (gdouble) cd.lo[2] >= gz0 + extent))
			continue;
		g_array_append_val (chosen, cd);
	}

	/* over the limit: the nearest stay */
	limit = (self->grid_cells > 0) ? LRG_DECAL_SET_MAX_DECALS : LRG_DECAL_SET_MAX_UNGRIDDED;
	if (chosen->len > limit)
	{
		g_array_sort (chosen, by_distance);
		g_array_set_size (chosen, limit);
	}
	g_array_sort (chosen, by_order);

	/* the active list in application order */
	g_ptr_array_set_size (self->active, 0);
	for (i = 0; i < chosen->len; i++)
		g_ptr_array_add (self->active,
		                 g_object_ref (g_array_index (chosen, Candidate, i).decal));
	if (self->grid_cells > 0)
		build_cells (self, chosen);
	self->select_serial++;
	return self->active->len;
}

guint
lrg_decal_set_get_active_count (LrgDecalSet *self)
{
	g_return_val_if_fail (LRG_IS_DECAL_SET (self), 0);

	return self->active->len;
}

LrgDecal *
lrg_decal_set_get_active_decal (LrgDecalSet *self,
                                guint        index)
{
	g_return_val_if_fail (LRG_IS_DECAL_SET (self), NULL);

	if (index >= self->active->len)
		return NULL;
	return g_ptr_array_index (self->active, index);
}

/* cell_at: the cluster index of world (@x, @y, @z), -1 outside */
static gssize
cell_at (LrgDecalSet *self,
         gdouble      x,
         gdouble      y,
         gdouble      z)
{
	gdouble gx, gz;
	gint    l;

	if (self->grid_cells == 0 || !self->grid_built)
		return -1;
	gx = floor ((x - self->grid_x0) / (gdouble) self->grid_cell);
	gz = floor ((z - self->grid_z0) / (gdouble) self->grid_cell);
	if (!(gx >= 0.0 && gz >= 0.0 && gx < (gdouble) self->grid_cells
	      && gz < (gdouble) self->grid_cells))
		return -1;
	l = layer_of (self, y);
	return ((gssize) l * (gssize) self->grid_cells + (gssize) gz) * (gssize) self->grid_cells
	       + (gssize) gx;
}

guint
lrg_decal_set_get_cluster_decal_count (LrgDecalSet *self,
                                       gdouble      x,
                                       gdouble      y,
                                       gdouble      z)
{
	gssize c;

	g_return_val_if_fail (LRG_IS_DECAL_SET (self), 0);

	c = cell_at (self, x, y, z);
	return c < 0 ? 0 : self->cell_counts[c];
}

LrgDecal *
lrg_decal_set_get_cluster_decal (LrgDecalSet *self,
                                 gdouble      x,
                                 gdouble      y,
                                 gdouble      z,
                                 guint        index)
{
	gssize c;

	g_return_val_if_fail (LRG_IS_DECAL_SET (self), NULL);

	c = cell_at (self, x, y, z);
	if (c < 0 || index >= self->cell_counts[c])
		return NULL;
	return g_ptr_array_index (self->active, self->cell_ids[(gsize) c * self->grid_slots + index]);
}

/* ------------------------------------------------------------------------
 * GPU upload
 * ------------------------------------------------------------------------ */

/* has_textures: whether @d uses the atlas at all */
static gboolean
has_textures (LrgDecal *d)
{
	return d->textures[0] != NULL || d->textures[1] != NULL || d->textures[2] != NULL;
}

/* tile_matches: whether tile @t holds exactly @d's textures */
static gboolean
tile_matches (const AtlasTile *t,
              LrgDecal        *d)
{
	return t->used && t->textures[0] == d->textures[0] && t->textures[1] == d->textures[1]
	       && t->textures[2] == d->textures[2];
}

/* assign_tiles:
 * Gives every active decal with textures a tile: the tile already
 * holding its combination, else a tile no active decal needs.  Tiles
 * whose combination is no longer used are recycled. */
static void
assign_tiles (LrgDecalSet *self)
{
	guint     n_tiles = self->atlas_tiles * self->atlas_tiles;
	gboolean *needed;
	guint     i, t;

	if (self->tiles == NULL)
		self->tiles = g_new0 (AtlasTile, n_tiles);
	g_free (self->active_tile);
	self->active_tile = g_new (gint, MAX (self->active->len, 1u));
	needed = g_new0 (gboolean, n_tiles);

	/* pass 1: combinations already in a tile */
	for (i = 0; i < self->active->len; i++)
	{
		LrgDecal *d = g_ptr_array_index (self->active, i);

		self->active_tile[i] = -1;
		if (!has_textures (d))
			continue;
		for (t = 0; t < n_tiles; t++)
			if (tile_matches (&self->tiles[t], d))
			{
				self->active_tile[i] = (gint) t;
				needed[t] = TRUE;
				break;
			}
	}

	/* pass 2: new combinations into tiles nobody needs */
	for (i = 0; i < self->active->len; i++)
	{
		LrgDecal *d = g_ptr_array_index (self->active, i);
		guint     k;

		if (!has_textures (d) || self->active_tile[i] >= 0)
			continue;
		/* the same new combination earlier in this pass */
		for (k = 0; k < i; k++)
			if (self->active_tile[k] >= 0 && tile_matches (&self->tiles[self->active_tile[k]], d))
				break;
		if (k < i)
		{
			self->active_tile[i] = self->active_tile[k];
			continue;
		}
		for (t = 0; t < n_tiles; t++)
			if (!needed[t])
				break;
		if (t == n_tiles)
		{
			if (!self->warned_tiles)
			{
				g_warning ("LrgDecalSet: more texture combinations than the %u atlas tiles; "
				           "extra decals draw with their colour only", n_tiles);
				self->warned_tiles = TRUE;
			}
			continue;
		}
		for (k = 0; k < 3; k++)
			g_set_object (&self->tiles[t].textures[k], d->textures[k]);
		self->tiles[t].used = TRUE;
		self->tiles[t].current = FALSE;
		needed[t] = TRUE;
		self->active_tile[i] = (gint) t;
	}
	g_free (needed);
}

/* blit_tile:
 * Copies tile @t's textures into the three atlas bands: each texture is
 * read back, converted to RGBA8 and resampled (bilinear) to the tile
 * size on the CPU, then written with glTexSubImage2D.  Image row 0 (the
 * top) lands at the tile's lowest v, so the atlas samples like the
 * source texture. */
static void
blit_tile (LrgDecalSet *self,
           guint        t)
{
	guint tile = self->atlas_size / self->atlas_tiles;
	gint  tx = (gint) ((t % self->atlas_tiles) * tile);
	gint  ty = (gint) ((t / self->atlas_tiles) * tile);
	guint band;

	for (band = 0; band < 3; band++)
	{
		GrlTexture *tex = self->tiles[t].textures[band];
		Texture2D  *handle;
		Image       img;

		if (tex == NULL)
			continue;
		handle = (Texture2D *) grl_texture_get_handle (tex);
		if (handle == NULL || handle->id == 0)
			continue;
		rlActiveTextureSlot (0);
		img = LoadImageFromTexture (*handle);
		if (img.data == NULL)
			continue;
		if (img.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
			ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
		if (img.width != (gint) tile || img.height != (gint) tile)
			ImageResize (&img, (gint) tile, (gint) tile);
		rlUpdateTexture (self->atlas_tex, tx, (gint) (band * self->atlas_size) + ty,
		                 (gint) tile, (gint) tile, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, img.data);
		UnloadImage (img);
	}
	self->tiles[t].current = TRUE;
}

/* ensure_atlas: the S x 3S RGBA8 atlas texture, zero filled */
static gboolean
ensure_atlas (LrgDecalSet *self)
{
	gpointer zeros;
	guint    t;

	if (self->atlas_tex != 0 && self->atlas_gpu_size == self->atlas_size)
		return TRUE;
	if (self->atlas_tex != 0)
		rlUnloadTexture (self->atlas_tex);
	rlActiveTextureSlot (0);
	zeros = g_malloc0 ((gsize) self->atlas_size * self->atlas_size * 3 * 4);
	self->atlas_tex = rlLoadTexture (zeros, (gint) self->atlas_size, (gint) self->atlas_size * 3,
	                                 PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
	g_free (zeros);
	if (self->atlas_tex == 0)
		return FALSE;
	rlTextureParameters (self->atlas_tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
	rlTextureParameters (self->atlas_tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
	rlTextureParameters (self->atlas_tex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
	rlTextureParameters (self->atlas_tex, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
	self->atlas_gpu_size = self->atlas_size;
	if (self->tiles != NULL)
		for (t = 0; t < self->atlas_tiles * self->atlas_tiles; t++)
			self->tiles[t].current = FALSE;
	return TRUE;
}

/* fill_decal_row:
 * The eight texels of active decal @i (layout in
 * lrg-decal-glsl-private.h).  The inverse matrix is shifted into draw
 * space: local = M^-1 (p_draw + origin), so each row's w gains
 * dot (row.xyz, origin). */
static void
fill_decal_row (LrgDecalSet *self,
                guint        i,
                gfloat      *t)
{
	LrgDecal *d = g_ptr_array_index (self->active, i);
	gint      tile = self->active_tile != NULL ? self->active_tile[i] : -1;
	gfloat    cs, ce;
	guint     r;
	guint     flags = 0;

	for (r = 0; r < 3; r++)
	{
		gdouble w;

		t[r * 4 + 0] = d->inverse[0 + r];
		t[r * 4 + 1] = d->inverse[4 + r];
		t[r * 4 + 2] = d->inverse[8 + r];
		w = (gdouble) d->inverse[12 + r] + (gdouble) d->inverse[0 + r] * self->origin[0]
		    + (gdouble) d->inverse[4 + r] * self->origin[1]
		    + (gdouble) d->inverse[8 + r] * self->origin[2];
		t[r * 4 + 3] = (gfloat) w;
	}
	memcpy (t + 12, d->color, 4 * sizeof (gfloat));

	/* the atlas rect of the tile with the uv rect folded in */
	if (tile >= 0)
	{
		gfloat side = (gfloat) self->atlas_tiles;
		gfloat u0 = (gfloat) (tile % (gint) self->atlas_tiles) / side;
		gfloat v0 = (gfloat) (tile / (gint) self->atlas_tiles) / side;

		t[16] = u0 + d->uv_rect[0] / side;
		t[17] = v0 + d->uv_rect[1] / side;
		t[18] = d->uv_rect[2] / side;
		t[19] = d->uv_rect[3] / side;
		if (d->textures[0] != NULL)
			flags |= 1;
		if (d->textures[1] != NULL)
			flags |= 2;
		if (d->textures[2] != NULL)
			flags |= 4;
	}
	else
	{
		t[16] = t[17] = 0.0f;
		t[18] = t[19] = 1.0f;
	}
	if (d->channels & LRG_DECAL_CHANNEL_ALBEDO)
		flags |= 8;
	if (d->channels & LRG_DECAL_CHANNEL_NORMAL)
		flags |= 16;
	if (d->channels & LRG_DECAL_CHANNEL_ROUGHNESS)
		flags |= 32;
	if (d->channels & LRG_DECAL_CHANNEL_METALLIC)
		flags |= 64;

	fade_cosines (d, &cs, &ce);
	t[20] = d->opacity;
	t[21] = cs;
	t[22] = ce;
	t[23] = (gfloat) flags;
	t[24] = d->normal_strength;
	t[25] = d->roughness;
	t[26] = d->metallic;
	t[27] = (gfloat) d->blend;
	t[28] = (gfloat) (d->layer_mask & 0xffffu);
	t[29] = (gfloat) (d->layer_mask >> 16);
	t[30] = d->depth_fade;
	t[31] = 0.0f;
}

gboolean
lrg_decal_set_upload (LrgDecalSet *self)
{
	guint  n = self->grid_cells;
	guint  per = self->grid_slots / 4;
	gint   width, rows, cell_rows;
	gsize  len;
	guint  i;
	gsize  c;
	gboolean any_textures = FALSE;

	g_return_val_if_fail (LRG_IS_DECAL_SET (self), FALSE);

	if (!IsWindowReady ())
		return FALSE;
	if (self->uploaded_serial == self->select_serial && self->data_tex != 0)
		return TRUE;

	/* the atlas tiles of the selection, copying new combinations */
	for (i = 0; i < self->active->len; i++)
		if (has_textures (g_ptr_array_index (self->active, i)))
			any_textures = TRUE;
	if (any_textures && ensure_atlas (self))
	{
		guint t;

		assign_tiles (self);
		for (t = 0; t < self->atlas_tiles * self->atlas_tiles; t++)
			if (self->tiles[t].used && !self->tiles[t].current)
				blit_tile (self, t);
	}
	else
	{
		g_clear_pointer (&self->active_tile, g_free);
	}

	/* one float texture: decal rows, then the cell rows */
	cell_rows = (n > 0 && self->grid_built) ? (gint) (n * self->grid_layers) : 0;
	width = MAX (LRG_DECAL_DATA_TEXELS, (gint) (n * per));
	self->cell_row = (gint) MAX (self->active->len, 1u);
	rows = self->cell_row + cell_rows;
	len = (gsize) width * (gsize) rows * 4;
	if (self->data_len < len)
	{
		g_free (self->data_buf);
		self->data_buf = g_new (gfloat, len);
		self->data_len = len;
	}
	memset (self->data_buf, 0, len * sizeof (gfloat));
	for (i = 0; i < self->active->len; i++)
		fill_decal_row (self, i, self->data_buf + (gsize) i * (gsize) width * 4);
	if (cell_rows > 0)
	{
		gfloat *cells = self->data_buf + (gsize) self->cell_row * (gsize) width * 4;
		gsize   n_cells = (gsize) n * n * self->grid_layers;

		/* cluster (x, z, layer) at texels x * per .. of row layer * n + z,
		 * four ids per texel, -1 after the last */
		for (c = 0; c < n_cells; c++)
		{
			gsize  gx = c % n;
			gsize  row = c / n;
			gfloat *dst = cells + (row * (gsize) width + gx * per) * 4;
			guint  s;

			for (s = 0; s < self->grid_slots; s++)
				dst[s] = (s < self->cell_counts[c])
				         ? (gfloat) self->cell_ids[c * self->grid_slots + s] : -1.0f;
		}
	}

	rlActiveTextureSlot (0);
	if (self->data_tex != 0 && (self->data_w != width || self->data_h != rows))
	{
		rlUnloadTexture (self->data_tex);
		self->data_tex = 0;
	}
	if (self->data_tex == 0)
	{
		self->data_tex = rlLoadTexture (self->data_buf, width, rows,
		                                RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
		if (self->data_tex == 0)
			return FALSE;
		rlTextureParameters (self->data_tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
		rlTextureParameters (self->data_tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
		self->data_w = width;
		self->data_h = rows;
	}
	else
	{
		rlUpdateTexture (self->data_tex, 0, 0, width, rows,
		                 RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, self->data_buf);
	}
	self->uploaded_serial = self->select_serial;
	return TRUE;
}

void
lrg_decal_set_release (LrgDecalSet *self)
{
	g_return_if_fail (LRG_IS_DECAL_SET (self));

	gpu_free (self);
	tiles_free (self);
	g_clear_pointer (&self->active_tile, g_free);
}

/* ------------------------------------------------------------------------
 * Lit-shader hook
 * ------------------------------------------------------------------------ */

typedef struct
{
	gint on;
	gint count;
	gint grid;
	gint receiver;
	gint data;
	gint cell_row;
	gint atlas;
	gint grid_params;
	gint grid_layers;
	gint atlas_texel;
	gint textures;
} HookLocations;

/* program id -> HookLocations; drawing is single threaded */
static GHashTable *hook_cache = NULL;

static const HookLocations *
hook_locations (guint shader_id)
{
	HookLocations *l;

	if (hook_cache == NULL)
		hook_cache = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	l = g_hash_table_lookup (hook_cache, GUINT_TO_POINTER (shader_id));
	if (l == NULL)
	{
		l = g_new (HookLocations, 1);
		l->on = rlGetLocationUniform (shader_id, "lrgDecalOn");
		l->count = rlGetLocationUniform (shader_id, "lrgDecalCount");
		l->grid = rlGetLocationUniform (shader_id, "lrgDecalGrid");
		l->receiver = rlGetLocationUniform (shader_id, "lrgDecalReceiver");
		l->data = rlGetLocationUniform (shader_id, "lrgDecalData");
		l->cell_row = rlGetLocationUniform (shader_id, "lrgDecalCellRow");
		l->atlas = rlGetLocationUniform (shader_id, "lrgDecalAtlas");
		l->grid_params = rlGetLocationUniform (shader_id, "lrgDecalGridParams");
		l->grid_layers = rlGetLocationUniform (shader_id, "lrgDecalGridLayers");
		l->atlas_texel = rlGetLocationUniform (shader_id, "lrgDecalAtlasTexel");
		l->textures = rlGetLocationUniform (shader_id, "lrgDecalTextures");
		g_hash_table_insert (hook_cache, GUINT_TO_POINTER (shader_id), l);
	}
	return l;
}

/* set_int: an int uniform when the location exists */
static void
set_int (gint loc,
         gint value)
{
	if (loc >= 0)
		rlSetUniform (loc, &value, RL_SHADER_UNIFORM_INT, 1);
}

void
lrg_decal_set_bind_private (LrgDecalSet *decals,
                            guint        shader_id,
                            gint        *texture_unit)
{
	const HookLocations *l;
	gint                 unit;
	gboolean             atlas;

	g_return_if_fail (decals == NULL || LRG_IS_DECAL_SET (decals));
	g_return_if_fail (texture_unit != NULL);

	if (shader_id == 0)
		return;
	l = hook_locations (shader_id);
	if (l->on < 0)
		return;     /* the program has no (live) hook */

	/* inactive: nothing selected, no upload, or no free hook unit; the
	 * samplers then point at unit 0 (texture0, also a sampler2D) */
	unit = *texture_unit;
	if (decals == NULL || decals->active->len == 0 || !lrg_light_set_hook_unit_usable (unit)
	    || !lrg_decal_set_upload (decals) || decals->data_tex == 0)
	{
		rlEnableShader (shader_id);
		set_int (l->data, 0);
		set_int (l->atlas, 0);
		set_int (l->on, 0);
		return;
	}
	rlEnableShader (shader_id);

	/* data + cells on the first unit; the atlas on the next when a
	 * decal uses it and that unit is free too, else the atlas sampler
	 * shares the data unit (no decal then has a texture flag) */
	atlas = decals->atlas_tex != 0 && decals->active_tile != NULL
	        && lrg_light_set_hook_unit_usable (unit + 1);
	rlActiveTextureSlot (unit);
	rlEnableTexture (decals->data_tex);
	if (atlas)
	{
		rlActiveTextureSlot (unit + 1);
		rlEnableTexture (decals->atlas_tex);
	}
	rlActiveTextureSlot (0);
	set_int (l->data, unit);
	set_int (l->atlas, atlas ? unit + 1 : unit);
	*texture_unit = unit + (atlas ? 2 : 1);

	/* without the atlas the shader ignores the rows' texture flags */
	set_int (l->textures, atlas ? 1 : 0);
	set_int (l->count, (gint) MIN (decals->active->len, (guint) LRG_DECAL_SET_MAX_UNGRIDDED));
	set_int (l->grid, (decals->grid_cells > 0 && decals->grid_built) ? 1 : 0);
	set_int (l->receiver, (gint) decals->receiver);
	set_int (l->cell_row, decals->cell_row);
	if (l->grid_params >= 0)
	{
		gfloat p[4];

		p[0] = (gfloat) (decals->grid_x0 - decals->origin[0]);
		p[1] = (gfloat) (decals->grid_z0 - decals->origin[2]);
		p[2] = decals->grid_cell;
		p[3] = (gfloat) decals->grid_cells;
		rlSetUniform (l->grid_params, p, RL_SHADER_UNIFORM_VEC4, 1);
	}
	if (l->grid_layers >= 0)
	{
		gfloat p[4];

		p[0] = (gfloat) (decals->grid_y0 - decals->origin[1]);
		p[1] = decals->grid_layer_height;
		p[2] = (gfloat) decals->grid_layers;
		p[3] = (gfloat) decals->grid_slots;
		rlSetUniform (l->grid_layers, p, RL_SHADER_UNIFORM_VEC4, 1);
	}
	if (l->atlas_texel >= 0)
	{
		gfloat p[2];

		p[0] = 1.0f / (gfloat) decals->atlas_size;
		p[1] = 1.0f / (gfloat) decals->atlas_size;
		rlSetUniform (l->atlas_texel, p, RL_SHADER_UNIFORM_VEC2, 1);
	}
	set_int (l->on, 1);
}

gint
lrg_decal_set_bind (LrgDecalSet *self,
                    guint        shader_id,
                    gint         first_unit)
{
	gint unit = first_unit;

	g_return_val_if_fail (LRG_IS_DECAL_SET (self), first_unit);

	lrg_decal_set_bind_private (self, shader_id, &unit);
	return unit;
}

void
lrg_decal_set_bind_inactive (guint shader_id)
{
	gint unit = 0;

	lrg_decal_set_bind_private (NULL, shader_id, &unit);
}

const gchar *
lrg_decal_set_get_glsl (void)
{
	return LRG_DECAL_GLSL;
}
