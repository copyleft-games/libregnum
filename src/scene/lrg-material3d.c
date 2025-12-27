/* lrg-material3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * PBR material for 3D scene objects.
 */

#include "lrg-material3d.h"

/**
 * LrgMaterial3D:
 *
 * A physically-based rendering (PBR) material compatible with
 * Blender's Principled BSDF shader. Stores color as floats
 * for full precision during round-trip serialization.
 */
struct _LrgMaterial3D
{
	GObject parent_instance;

	gfloat color[4];           /* Base color RGBA (0.0-1.0) */
	gfloat roughness;          /* Surface roughness (0.0-1.0) */
	gfloat metallic;           /* Metallic factor (0.0-1.0) */
	gfloat emission_color[4];  /* Emission color RGBA (0.0-1.0) */
	gfloat emission_strength;  /* Emission intensity (0.0+) */
};

G_DEFINE_FINAL_TYPE (LrgMaterial3D, lrg_material3d, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_ROUGHNESS,
	PROP_METALLIC,
	PROP_EMISSION_STRENGTH,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Overrides
 * ========================================================================== */

static void
lrg_material3d_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgMaterial3D *self = LRG_MATERIAL3D (object);

	switch (prop_id)
	{
	case PROP_ROUGHNESS:
		g_value_set_float (value, self->roughness);
		break;
	case PROP_METALLIC:
		g_value_set_float (value, self->metallic);
		break;
	case PROP_EMISSION_STRENGTH:
		g_value_set_float (value, self->emission_strength);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_material3d_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgMaterial3D *self = LRG_MATERIAL3D (object);

	switch (prop_id)
	{
	case PROP_ROUGHNESS:
		self->roughness = g_value_get_float (value);
		break;
	case PROP_METALLIC:
		self->metallic = g_value_get_float (value);
		break;
	case PROP_EMISSION_STRENGTH:
		self->emission_strength = g_value_get_float (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_material3d_class_init (LrgMaterial3DClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = lrg_material3d_get_property;
	object_class->set_property = lrg_material3d_set_property;

	/**
	 * LrgMaterial3D:roughness:
	 *
	 * The surface roughness of the material.
	 * 0.0 = perfectly smooth (mirror), 1.0 = fully rough (diffuse).
	 */
	properties[PROP_ROUGHNESS] =
		g_param_spec_float ("roughness",
		                    "Roughness",
		                    "Surface roughness (0.0-1.0)",
		                    0.0f, 1.0f, 0.5f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgMaterial3D:metallic:
	 *
	 * The metallic factor of the material.
	 * 0.0 = dielectric, 1.0 = fully metallic.
	 */
	properties[PROP_METALLIC] =
		g_param_spec_float ("metallic",
		                    "Metallic",
		                    "Metallic factor (0.0-1.0)",
		                    0.0f, 1.0f, 0.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgMaterial3D:emission-strength:
	 *
	 * The emission intensity of the material.
	 * 0.0 = no emission, higher values = brighter glow.
	 */
	properties[PROP_EMISSION_STRENGTH] =
		g_param_spec_float ("emission-strength",
		                    "Emission Strength",
		                    "Emission intensity (0.0+)",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_material3d_init (LrgMaterial3D *self)
{
	/* Default: white opaque material */
	self->color[0] = 1.0f;
	self->color[1] = 1.0f;
	self->color[2] = 1.0f;
	self->color[3] = 1.0f;

	self->roughness = 0.5f;
	self->metallic  = 0.0f;

	/* Default: no emission */
	self->emission_color[0] = 0.0f;
	self->emission_color[1] = 0.0f;
	self->emission_color[2] = 0.0f;
	self->emission_color[3] = 1.0f;
	self->emission_strength = 0.0f;
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_material3d_new:
 *
 * Creates a new #LrgMaterial3D with default values.
 *
 * Returns: (transfer full): A new #LrgMaterial3D
 */
LrgMaterial3D *
lrg_material3d_new (void)
{
	return g_object_new (LRG_TYPE_MATERIAL3D, NULL);
}

/**
 * lrg_material3d_new_with_color:
 * @r: Red component (0.0-1.0)
 * @g: Green component (0.0-1.0)
 * @b: Blue component (0.0-1.0)
 * @a: Alpha component (0.0-1.0)
 *
 * Creates a new #LrgMaterial3D with the specified color.
 *
 * Returns: (transfer full): A new #LrgMaterial3D
 */
LrgMaterial3D *
lrg_material3d_new_with_color (gfloat r,
                               gfloat g,
                               gfloat b,
                               gfloat a)
{
	LrgMaterial3D *self = lrg_material3d_new ();

	lrg_material3d_set_color (self, r, g, b, a);

	return self;
}

/* ==========================================================================
 * Color Accessors
 * ========================================================================== */

/**
 * lrg_material3d_set_color:
 * @self: an #LrgMaterial3D
 * @r: Red component (0.0-1.0)
 * @g: Green component (0.0-1.0)
 * @b: Blue component (0.0-1.0)
 * @a: Alpha component (0.0-1.0)
 *
 * Sets the base color of the material.
 */
void
lrg_material3d_set_color (LrgMaterial3D *self,
                          gfloat         r,
                          gfloat         g,
                          gfloat         b,
                          gfloat         a)
{
	g_return_if_fail (LRG_IS_MATERIAL3D (self));

	self->color[0] = r;
	self->color[1] = g;
	self->color[2] = b;
	self->color[3] = a;
}

/**
 * lrg_material3d_get_color:
 * @self: an #LrgMaterial3D
 * @r: (out) (optional): Location for red component
 * @g: (out) (optional): Location for green component
 * @b: (out) (optional): Location for blue component
 * @a: (out) (optional): Location for alpha component
 *
 * Gets the base color of the material.
 */
void
lrg_material3d_get_color (LrgMaterial3D *self,
                          gfloat        *r,
                          gfloat        *g,
                          gfloat        *b,
                          gfloat        *a)
{
	g_return_if_fail (LRG_IS_MATERIAL3D (self));

	if (r != NULL)
		*r = self->color[0];
	if (g != NULL)
		*g = self->color[1];
	if (b != NULL)
		*b = self->color[2];
	if (a != NULL)
		*a = self->color[3];
}

/**
 * lrg_material3d_get_color_grl:
 * @self: an #LrgMaterial3D
 *
 * Gets the base color as a GrlColor for use with graylib rendering.
 * Converts float components (0.0-1.0) to unsigned char (0-255).
 *
 * Returns: (transfer full): A new #GrlColor
 */
GrlColor *
lrg_material3d_get_color_grl (LrgMaterial3D *self)
{
	g_return_val_if_fail (LRG_IS_MATERIAL3D (self), NULL);

	return grl_color_new ((guchar)(self->color[0] * 255.0f),
	                      (guchar)(self->color[1] * 255.0f),
	                      (guchar)(self->color[2] * 255.0f),
	                      (guchar)(self->color[3] * 255.0f));
}

/* ==========================================================================
 * PBR Accessors
 * ========================================================================== */

/**
 * lrg_material3d_get_roughness:
 * @self: an #LrgMaterial3D
 *
 * Gets the roughness value of the material.
 *
 * Returns: The roughness value (0.0-1.0)
 */
gfloat
lrg_material3d_get_roughness (LrgMaterial3D *self)
{
	g_return_val_if_fail (LRG_IS_MATERIAL3D (self), 0.5f);

	return self->roughness;
}

/**
 * lrg_material3d_set_roughness:
 * @self: an #LrgMaterial3D
 * @roughness: The roughness value (0.0-1.0)
 *
 * Sets the roughness value of the material.
 */
void
lrg_material3d_set_roughness (LrgMaterial3D *self,
                              gfloat         roughness)
{
	g_return_if_fail (LRG_IS_MATERIAL3D (self));

	if (self->roughness != roughness)
	{
		self->roughness = CLAMP (roughness, 0.0f, 1.0f);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROUGHNESS]);
	}
}

/**
 * lrg_material3d_get_metallic:
 * @self: an #LrgMaterial3D
 *
 * Gets the metallic value of the material.
 *
 * Returns: The metallic value (0.0-1.0)
 */
gfloat
lrg_material3d_get_metallic (LrgMaterial3D *self)
{
	g_return_val_if_fail (LRG_IS_MATERIAL3D (self), 0.0f);

	return self->metallic;
}

/**
 * lrg_material3d_set_metallic:
 * @self: an #LrgMaterial3D
 * @metallic: The metallic value (0.0-1.0)
 *
 * Sets the metallic value of the material.
 */
void
lrg_material3d_set_metallic (LrgMaterial3D *self,
                             gfloat         metallic)
{
	g_return_if_fail (LRG_IS_MATERIAL3D (self));

	if (self->metallic != metallic)
	{
		self->metallic = CLAMP (metallic, 0.0f, 1.0f);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_METALLIC]);
	}
}

/* ==========================================================================
 * Emission Accessors
 * ========================================================================== */

/**
 * lrg_material3d_set_emission_color:
 * @self: an #LrgMaterial3D
 * @r: Red component (0.0-1.0)
 * @g: Green component (0.0-1.0)
 * @b: Blue component (0.0-1.0)
 * @a: Alpha component (0.0-1.0)
 *
 * Sets the emission color of the material.
 */
void
lrg_material3d_set_emission_color (LrgMaterial3D *self,
                                   gfloat         r,
                                   gfloat         g,
                                   gfloat         b,
                                   gfloat         a)
{
	g_return_if_fail (LRG_IS_MATERIAL3D (self));

	self->emission_color[0] = r;
	self->emission_color[1] = g;
	self->emission_color[2] = b;
	self->emission_color[3] = a;
}

/**
 * lrg_material3d_get_emission_color:
 * @self: an #LrgMaterial3D
 * @r: (out) (optional): Location for red component
 * @g: (out) (optional): Location for green component
 * @b: (out) (optional): Location for blue component
 * @a: (out) (optional): Location for alpha component
 *
 * Gets the emission color of the material.
 */
void
lrg_material3d_get_emission_color (LrgMaterial3D *self,
                                   gfloat        *r,
                                   gfloat        *g,
                                   gfloat        *b,
                                   gfloat        *a)
{
	g_return_if_fail (LRG_IS_MATERIAL3D (self));

	if (r != NULL)
		*r = self->emission_color[0];
	if (g != NULL)
		*g = self->emission_color[1];
	if (b != NULL)
		*b = self->emission_color[2];
	if (a != NULL)
		*a = self->emission_color[3];
}

/**
 * lrg_material3d_get_emission_strength:
 * @self: an #LrgMaterial3D
 *
 * Gets the emission strength of the material.
 *
 * Returns: The emission strength (0.0+)
 */
gfloat
lrg_material3d_get_emission_strength (LrgMaterial3D *self)
{
	g_return_val_if_fail (LRG_IS_MATERIAL3D (self), 0.0f);

	return self->emission_strength;
}

/**
 * lrg_material3d_set_emission_strength:
 * @self: an #LrgMaterial3D
 * @strength: The emission strength (0.0+)
 *
 * Sets the emission strength of the material.
 */
void
lrg_material3d_set_emission_strength (LrgMaterial3D *self,
                                      gfloat         strength)
{
	g_return_if_fail (LRG_IS_MATERIAL3D (self));

	if (self->emission_strength != strength)
	{
		self->emission_strength = MAX (strength, 0.0f);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EMISSION_STRENGTH]);
	}
}
