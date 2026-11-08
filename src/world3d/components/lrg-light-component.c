/* lrg-light-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLightComponent: keeps an #LrgLight3DDesc registered with the owning
 * entity's world, built from the properties and the entity's world
 * transform.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-light-component.h"
#include "../lrg-entity3d.h"
#include "../../graphics/lrg-camera.h"

struct _LrgLightComponent
{
	LrgComponent    parent_instance;

	LrgLight3DKind  kind;
	gfloat          color[3];
	gfloat          intensity;
	gfloat          range;
	gfloat          inner_angle;   /* degrees (half-angle) */
	gfloat          outer_angle;   /* degrees (half-angle) */
	gboolean        cast_shadows;
	gfloat          shadow_bias;   /* world units, 0: the shadow renderer's */
	guint32         layer_mask;
	LrgLightMode    bake_mode;     /* 0.9.0 M3 */

	LrgWorld3D     *world;         /* weak: the world the light is registered with */
};

G_DEFINE_FINAL_TYPE (LrgLightComponent, lrg_light_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_KIND,
	PROP_COLOR_R,
	PROP_COLOR_G,
	PROP_COLOR_B,
	PROP_INTENSITY,
	PROP_RANGE,
	PROP_INNER_ANGLE,
	PROP_OUTER_ANGLE,
	PROP_CAST_SHADOWS,
	PROP_LAYER_MASK,
	PROP_SHADOW_BIAS,
	PROP_BAKE_MODE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Registration
 * ========================================================================== */

/* Remembers @world weakly (it may be finalized before the component). */
static void
set_world (LrgLightComponent *self,
           LrgWorld3D        *world)
{
	if (self->world == world)
		return;
	if (self->world != NULL)
		g_object_remove_weak_pointer (G_OBJECT (self->world), (gpointer *) &self->world);
	self->world = world;
	if (self->world != NULL)
		g_object_add_weak_pointer (G_OBJECT (self->world), (gpointer *) &self->world);
}

static void
lrg_light_component_world_entered (LrgComponent *component,
                                   LrgWorld3D   *world)
{
	LrgLightComponent *self = LRG_LIGHT_COMPONENT (component);

	set_world (self, world);
	lrg_light_component_sync (self);
}

static void
lrg_light_component_world_exited (LrgComponent *component,
                                  LrgWorld3D   *world)
{
	LrgLightComponent *self = LRG_LIGHT_COMPONENT (component);

	lrg_world3d_unregister_light (world, G_OBJECT (self));
	if (self->world == world)
		set_world (self, NULL);
}

/* The entity may have moved this frame: refresh the world's copy. */
static void
lrg_light_component_late_update (LrgComponent *component,
                                 gfloat        delta)
{
	(void) delta;
	lrg_light_component_sync (LRG_LIGHT_COMPONENT (component));
}

/* Any property change (and enabling/disabling) re-registers at once so a
 * light edited between frames is right for the next render. */
static void
on_notify (GObject    *object,
           GParamSpec *pspec,
           gpointer    user_data)
{
	(void) pspec;
	(void) user_data;
	lrg_light_component_sync (LRG_LIGHT_COMPONENT (object));
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_light_component_dispose (GObject *object)
{
	LrgLightComponent *self = LRG_LIGHT_COMPONENT (object);

	/* The registry holds no reference on us: unregister before we go */
	if (self->world != NULL)
		lrg_world3d_unregister_light (self->world, object);
	set_world (self, NULL);

	G_OBJECT_CLASS (lrg_light_component_parent_class)->dispose (object);
}

static void
lrg_light_component_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgLightComponent *self = LRG_LIGHT_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_KIND:         g_value_set_enum (value, (gint) self->kind); break;
	case PROP_COLOR_R:      g_value_set_float (value, self->color[0]); break;
	case PROP_COLOR_G:      g_value_set_float (value, self->color[1]); break;
	case PROP_COLOR_B:      g_value_set_float (value, self->color[2]); break;
	case PROP_INTENSITY:    g_value_set_float (value, self->intensity); break;
	case PROP_RANGE:        g_value_set_float (value, self->range); break;
	case PROP_INNER_ANGLE:  g_value_set_float (value, self->inner_angle); break;
	case PROP_OUTER_ANGLE:  g_value_set_float (value, self->outer_angle); break;
	case PROP_CAST_SHADOWS: g_value_set_boolean (value, self->cast_shadows); break;
	case PROP_LAYER_MASK:   g_value_set_uint (value, (guint) self->layer_mask); break;
	case PROP_SHADOW_BIAS:  g_value_set_float (value, self->shadow_bias); break;
	case PROP_BAKE_MODE:    g_value_set_enum (value, (gint) self->bake_mode); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

/* Stores a float and notifies when it changed. */
static void
set_float (LrgLightComponent *self,
           gfloat            *slot,
           gfloat             value,
           guint              prop)
{
	if (*slot == value)
		return;
	*slot = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
lrg_light_component_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgLightComponent *self = LRG_LIGHT_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_KIND:
		lrg_light_component_set_kind (self, (LrgLight3DKind) g_value_get_enum (value));
		break;
	case PROP_COLOR_R: set_float (self, &self->color[0], g_value_get_float (value), prop_id); break;
	case PROP_COLOR_G: set_float (self, &self->color[1], g_value_get_float (value), prop_id); break;
	case PROP_COLOR_B: set_float (self, &self->color[2], g_value_get_float (value), prop_id); break;
	case PROP_INTENSITY:
		lrg_light_component_set_intensity (self, g_value_get_float (value));
		break;
	case PROP_RANGE:
		lrg_light_component_set_range (self, g_value_get_float (value));
		break;
	case PROP_INNER_ANGLE:
		set_float (self, &self->inner_angle, g_value_get_float (value), prop_id);
		break;
	case PROP_OUTER_ANGLE:
		set_float (self, &self->outer_angle, g_value_get_float (value), prop_id);
		break;
	case PROP_CAST_SHADOWS:
		lrg_light_component_set_cast_shadows (self, g_value_get_boolean (value));
		break;
	case PROP_LAYER_MASK:
		lrg_light_component_set_layer_mask (self, (guint32) g_value_get_uint (value));
		break;
	case PROP_SHADOW_BIAS:
		lrg_light_component_set_shadow_bias (self, g_value_get_float (value));
		break;
	case PROP_BAKE_MODE:
		lrg_light_component_set_bake_mode (self, (LrgLightMode) g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_light_component_class_init (LrgLightComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_light_component_dispose;
	object_class->get_property = lrg_light_component_get_property;
	object_class->set_property = lrg_light_component_set_property;

	component_class->world_entered = lrg_light_component_world_entered;
	component_class->world_exited = lrg_light_component_world_exited;
	component_class->late_update = lrg_light_component_late_update;

	/**
	 * LrgLightComponent:kind:
	 *
	 * Directional, point or spot.
	 */
	properties[PROP_KIND] =
		g_param_spec_enum ("kind", NULL, "Light kind", LRG_TYPE_LIGHT3D_KIND, LRG_LIGHT3D_POINT, rw);

	/**
	 * LrgLightComponent:color-r:
	 *
	 * Linear red, 0 .. 1.
	 */
	properties[PROP_COLOR_R] = g_param_spec_float ("color-r", NULL, "Red", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgLightComponent:color-g:
	 *
	 * Linear green, 0 .. 1.
	 */
	properties[PROP_COLOR_G] = g_param_spec_float ("color-g", NULL, "Green", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgLightComponent:color-b:
	 *
	 * Linear blue, 0 .. 1.
	 */
	properties[PROP_COLOR_B] = g_param_spec_float ("color-b", NULL, "Blue", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgLightComponent:intensity:
	 *
	 * Multiplier on the colour, in display space: the light set
	 * linearizes colour x intensity (gamma 2.2), so 1 is the colour
	 * itself and 2 is about 4.6 times the linear radiance.  Not a
	 * photometric unit; a directional light's colour x intensity
	 * saturates at white.
	 */
	properties[PROP_INTENSITY] =
		g_param_spec_float ("intensity", NULL, "Intensity", 0.0f, G_MAXFLOAT, 1.0f, rw);

	/**
	 * LrgLightComponent:range:
	 *
	 * Attenuation range of point and spot lights, world units.
	 */
	properties[PROP_RANGE] =
		g_param_spec_float ("range", NULL, "Range", 0.001f, G_MAXFLOAT, 10.0f, rw);

	/**
	 * LrgLightComponent:inner-angle:
	 *
	 * Spot half-angle of full intensity, degrees.
	 */
	properties[PROP_INNER_ANGLE] =
		g_param_spec_float ("inner-angle", NULL, "Spot inner half-angle (degrees)", 0.0f, 89.0f, 30.0f, rw);

	/**
	 * LrgLightComponent:outer-angle:
	 *
	 * Spot half-angle where the light falls to zero, degrees.
	 */
	properties[PROP_OUTER_ANGLE] =
		g_param_spec_float ("outer-angle", NULL, "Spot outer half-angle (degrees)", 0.0f, 89.0f, 45.0f, rw);

	/**
	 * LrgLightComponent:cast-shadows:
	 *
	 * Whether the renderer should render a shadow map for the light.
	 */
	properties[PROP_CAST_SHADOWS] =
		g_param_spec_boolean ("cast-shadows", NULL, "Cast shadows", FALSE, rw);

	/**
	 * LrgLightComponent:layer-mask:
	 *
	 * Render layers the light illuminates.
	 */
	properties[PROP_LAYER_MASK] =
		g_param_spec_uint ("layer-mask", NULL, "Lit render layers", 0, G_MAXUINT32,
		                   LRG_RENDER_LAYERS_MASK_ALL, rw);

	/**
	 * LrgLightComponent:shadow-bias:
	 *
	 * Depth bias of the light's shadow cube or spot map in world units;
	 * 0 uses the shadow renderer's (lrg_shadow_renderer_set_bias()).
	 * Raise it against shadow acne, lower it against light leaking
	 * ("peter-panning").
	 */
	properties[PROP_SHADOW_BIAS] =
		g_param_spec_float ("shadow-bias", NULL, "Shadow depth bias (world units)", 0.0f, 100.0f, 0.0f, rw);

	/**
	 * LrgLightComponent:bake-mode:
	 *
	 * How the light takes part in baked lighting (since 0.9.0):
	 * realtime (ignored by the baker), mixed (the baker adds its bounced
	 * light; direct light stays realtime) or baked (direct and bounced
	 * light go into the lightmaps; skipped at runtime on lightmapped
	 * draws).  Node parameter `bake_mode`.
	 */
	properties[PROP_BAKE_MODE] =
		g_param_spec_enum ("bake-mode", NULL, "Bake mode", LRG_TYPE_LIGHT_MODE,
		                   LRG_LIGHT_MODE_REALTIME, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_light_component_init (LrgLightComponent *self)
{
	self->kind = LRG_LIGHT3D_POINT;
	self->color[0] = self->color[1] = self->color[2] = 1.0f;
	self->intensity = 1.0f;
	self->range = 10.0f;
	self->inner_angle = 30.0f;
	self->outer_angle = 45.0f;
	self->layer_mask = LRG_RENDER_LAYERS_MASK_ALL;

	g_signal_connect (self, "notify", G_CALLBACK (on_notify), NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgLightComponent *
lrg_light_component_new (void)
{
	return (LrgLightComponent *) g_object_new (LRG_TYPE_LIGHT_COMPONENT, NULL);
}

LrgLightComponent *
lrg_light_component_new_with_kind (LrgLight3DKind kind)
{
	return (LrgLightComponent *) g_object_new (LRG_TYPE_LIGHT_COMPONENT, "kind", kind, NULL);
}

LrgLight3DKind
lrg_light_component_get_kind (LrgLightComponent *self)
{
	g_return_val_if_fail (LRG_IS_LIGHT_COMPONENT (self), LRG_LIGHT3D_POINT);

	return self->kind;
}

void
lrg_light_component_set_kind (LrgLightComponent *self,
                              LrgLight3DKind     kind)
{
	g_return_if_fail (LRG_IS_LIGHT_COMPONENT (self));

	if (self->kind == kind)
		return;
	self->kind = kind;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KIND]);
}

void
lrg_light_component_get_color (LrgLightComponent *self,
                               gfloat            *out)
{
	g_return_if_fail (LRG_IS_LIGHT_COMPONENT (self));
	g_return_if_fail (out != NULL);

	memcpy (out, self->color, sizeof self->color);
}

void
lrg_light_component_set_color (LrgLightComponent *self,
                               gfloat             r,
                               gfloat             g,
                               gfloat             b)
{
	g_return_if_fail (LRG_IS_LIGHT_COMPONENT (self));

	g_object_freeze_notify (G_OBJECT (self));
	set_float (self, &self->color[0], CLAMP (r, 0.0f, 1.0f), PROP_COLOR_R);
	set_float (self, &self->color[1], CLAMP (g, 0.0f, 1.0f), PROP_COLOR_G);
	set_float (self, &self->color[2], CLAMP (b, 0.0f, 1.0f), PROP_COLOR_B);
	g_object_thaw_notify (G_OBJECT (self));
}

gfloat
lrg_light_component_get_intensity (LrgLightComponent *self)
{
	g_return_val_if_fail (LRG_IS_LIGHT_COMPONENT (self), 0.0f);

	return self->intensity;
}

void
lrg_light_component_set_intensity (LrgLightComponent *self,
                                   gfloat             intensity)
{
	g_return_if_fail (LRG_IS_LIGHT_COMPONENT (self));

	set_float (self, &self->intensity, MAX (intensity, 0.0f), PROP_INTENSITY);
}

gfloat
lrg_light_component_get_range (LrgLightComponent *self)
{
	g_return_val_if_fail (LRG_IS_LIGHT_COMPONENT (self), 0.0f);

	return self->range;
}

void
lrg_light_component_set_range (LrgLightComponent *self,
                               gfloat             range)
{
	g_return_if_fail (LRG_IS_LIGHT_COMPONENT (self));

	set_float (self, &self->range, MAX (range, 0.001f), PROP_RANGE);
}

void
lrg_light_component_set_spot_angles (LrgLightComponent *self,
                                     gfloat             inner_degrees,
                                     gfloat             outer_degrees)
{
	g_return_if_fail (LRG_IS_LIGHT_COMPONENT (self));

	inner_degrees = CLAMP (inner_degrees, 0.0f, 89.0f);
	outer_degrees = CLAMP (outer_degrees, inner_degrees, 89.0f);
	g_object_freeze_notify (G_OBJECT (self));
	set_float (self, &self->inner_angle, inner_degrees, PROP_INNER_ANGLE);
	set_float (self, &self->outer_angle, outer_degrees, PROP_OUTER_ANGLE);
	g_object_thaw_notify (G_OBJECT (self));
}

gboolean
lrg_light_component_get_cast_shadows (LrgLightComponent *self)
{
	g_return_val_if_fail (LRG_IS_LIGHT_COMPONENT (self), FALSE);

	return self->cast_shadows;
}

void
lrg_light_component_set_cast_shadows (LrgLightComponent *self,
                                      gboolean           cast_shadows)
{
	g_return_if_fail (LRG_IS_LIGHT_COMPONENT (self));

	cast_shadows = !!cast_shadows;
	if (self->cast_shadows == cast_shadows)
		return;
	self->cast_shadows = cast_shadows;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAST_SHADOWS]);
}

void
lrg_light_component_fill_desc (LrgLightComponent *self,
                               LrgLight3DDesc    *out)
{
	LrgEntity3D *entity;
	gfloat       inner;
	gfloat       outer;

	g_return_if_fail (LRG_IS_LIGHT_COMPONENT (self));
	g_return_if_fail (out != NULL);

	lrg_light3d_desc_init (out);

	/* Properties; angles go to radians, inner never exceeds outer */
	inner = MIN (self->inner_angle, self->outer_angle);
	outer = self->outer_angle;
	out->kind = self->kind;
	memcpy (out->color, self->color, sizeof out->color);
	out->intensity = self->intensity;
	out->range = self->range;
	out->inner_angle = inner * (gfloat) G_PI / 180.0f;
	out->outer_angle = outer * (gfloat) G_PI / 180.0f;
	out->cast_shadows = self->cast_shadows;
	out->layer_mask = self->layer_mask;
	out->bake_mode = self->bake_mode;
	out->source = G_OBJECT (self);
	out->enabled = lrg_component_get_enabled (LRG_COMPONENT (self));

	/* Placement: the light shines down the entity's local -Z */
	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	if (entity != NULL)
	{
		lrg_entity3d_get_world_position (entity, out->position);
		lrg_entity3d_get_forward (entity, out->direction);
		out->enabled = out->enabled && lrg_entity3d_is_active_in_hierarchy (entity);
	}
}

void
lrg_light_component_sync (LrgLightComponent *self)
{
	LrgLight3DDesc desc;

	g_return_if_fail (LRG_IS_LIGHT_COMPONENT (self));

	if (self->world == NULL)
		return;
	lrg_light_component_fill_desc (self, &desc);
	lrg_world3d_register_light (self->world, G_OBJECT (self), &desc);
}

void
lrg_light_component_get_spot_angles (LrgLightComponent *self,
                                     gfloat            *out_inner_degrees,
                                     gfloat            *out_outer_degrees)
{
	g_return_if_fail (LRG_IS_LIGHT_COMPONENT (self));

	if (out_inner_degrees != NULL)
		*out_inner_degrees = MIN (self->inner_angle, self->outer_angle);
	if (out_outer_degrees != NULL)
		*out_outer_degrees = self->outer_angle;
}

gfloat
lrg_light_component_get_shadow_bias (LrgLightComponent *self)
{
	g_return_val_if_fail (LRG_IS_LIGHT_COMPONENT (self), 0.0f);

	return self->shadow_bias;
}

void
lrg_light_component_set_shadow_bias (LrgLightComponent *self,
                                     gfloat             bias)
{
	g_return_if_fail (LRG_IS_LIGHT_COMPONENT (self));

	set_float (self, &self->shadow_bias, isfinite (bias) ? CLAMP (bias, 0.0f, 100.0f) : 0.0f,
	           PROP_SHADOW_BIAS);
}

guint32
lrg_light_component_get_layer_mask (LrgLightComponent *self)
{
	g_return_val_if_fail (LRG_IS_LIGHT_COMPONENT (self), 0);

	return self->layer_mask;
}

void
lrg_light_component_set_layer_mask (LrgLightComponent *self,
                                    guint32            mask)
{
	g_return_if_fail (LRG_IS_LIGHT_COMPONENT (self));

	if (self->layer_mask == mask)
		return;
	self->layer_mask = mask;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYER_MASK]);
}

guint
lrg_light_component_add_to_light_set (LrgLightComponent *self,
                                      LrgLightSet       *lights)
{
	LrgLight3DDesc desc;

	g_return_val_if_fail (LRG_IS_LIGHT_COMPONENT (self), LRG_LIGHT_SET_INVALID_LIGHT);
	g_return_val_if_fail (LRG_IS_LIGHT_SET (lights), LRG_LIGHT_SET_INVALID_LIGHT);

	/* the description carries everything but the bias, which
	 * lrg_light_set_add_light_desc() reads back from desc.source */
	lrg_light_component_fill_desc (self, &desc);
	return lrg_light_set_add_light_desc (lights, &desc);
}

LrgLightMode
lrg_light_component_get_bake_mode (LrgLightComponent *self)
{
	g_return_val_if_fail (LRG_IS_LIGHT_COMPONENT (self), LRG_LIGHT_MODE_REALTIME);

	return self->bake_mode;
}

void
lrg_light_component_set_bake_mode (LrgLightComponent *self,
                                   LrgLightMode       mode)
{
	g_return_if_fail (LRG_IS_LIGHT_COMPONENT (self));

	if (self->bake_mode == mode)
		return;
	self->bake_mode = mode;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BAKE_MODE]);
}
