/* lrg-water-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWaterComponent: an LrgWaterBody built into an LrgWaterSurface in
 * world space (at the entity's position) and drawn as one transparent
 * custom render item.  The draw captures the frame drawn so far (for
 * screen-space reflection and refraction), binds the renderer's light
 * set and sun, and draws the surface on the component's wave clock.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <rlgl.h>

#include "lrg-water-component.h"
#include "../lrg-entity3d.h"
#include "../lrg-world3d.h"
#include "../lrg-world3d-renderer.h"
#include "../../graphics/lrg-light-set.h"

struct _LrgWaterComponent
{
	LrgComponent     parent_instance;

	LrgWaterBody    *body;
	gchar           *body_path;
	gboolean         load_failed;
	LrgWaterSurface *surface;
	gboolean         dirty;
	gfloat           built_at[3];
	gboolean         built_ok;
	gfloat           time;
	gboolean         capture;
	gint             layer;
};

G_DEFINE_FINAL_TYPE (LrgWaterComponent, lrg_water_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_BODY,
	PROP_BODY_PATH,
	PROP_TIME,
	PROP_CAPTURE,
	PROP_LAYER,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* What the draw callback needs (the list owns it). */
typedef struct
{
	LrgWaterComponent *self;   /* referenced */
	gfloat             eye[3];
	gfloat             time;
} WaterDraw;

static void
water_draw_free (gpointer data)
{
	WaterDraw *wd = (WaterDraw *) data;

	g_object_unref (wd->self);
	g_free (wd);
}

/* The body: explicit, else loaded from the path, else a default plane. */
static LrgWaterBody *
ensure_body (LrgWaterComponent *self)
{
	g_autoptr(GError) error = NULL;

	if (self->body != NULL)
		return self->body;
	if (self->body_path != NULL && !self->load_failed)
	{
		self->body = lrg_water_body_new_from_file (self->body_path, &error);
		if (self->body == NULL)
		{
			g_warning ("LrgWaterComponent: cannot load '%s': %s", self->body_path, error->message);
			self->load_failed = TRUE;
		}
	}
	if (self->body == NULL)
		self->body = lrg_water_body_new (LRG_WATER_BODY_PLANE);
	self->dirty = TRUE;
	return self->body;
}

/* Rebuilds the surface at the entity's world position when needed. */
static void
ensure_surface (LrgWaterComponent *self,
                const gfloat      *pos)
{
	g_autoptr(GError) error = NULL;

	ensure_body (self);
	if (!self->dirty && memcmp (pos, self->built_at, sizeof self->built_at) == 0)
		return;
	self->built_ok = lrg_water_body_build_surface (self->body, self->surface, pos[0], pos[1], pos[2], NULL, NULL,
	                                               &error);
	if (!self->built_ok)
		g_debug ("LrgWaterComponent: %s", error != NULL ? error->message : "nothing to build");
	memcpy (self->built_at, pos, sizeof self->built_at);
	self->dirty = FALSE;
}

static void
draw_water (const LrgRenderItem *item,
            LrgRenderCategory    category,
            gpointer             pass_data)
{
	WaterDraw             *wd = (WaterDraw *) item->user_data;
	LrgWorld3DRenderer    *renderer = LRG_IS_WORLD3D_RENDERER (pass_data) ? LRG_WORLD3D_RENDERER (pass_data) : NULL;
	LrgLightSet           *lights = renderer != NULL ? lrg_world3d_renderer_get_light_set (renderer) : NULL;
	g_autoptr(LrgLightSet) previous = NULL;

	if (category != LRG_RENDER_CATEGORY_TRANSPARENT)
		return;
	rlDrawRenderBatchActive ();

	/* the sun the renderer lit the scene with */
	if (renderer != NULL)
	{
		gfloat   sun[3];
		GrlColor glint = { 255, 242, 217, 255 };

		lrg_world3d_renderer_get_sun_direction (renderer, sun);
		lrg_water_surface_set_sun (wd->self->surface, sun, &glint);
	}
	if (wd->self->capture)
		lrg_water_surface_capture_scene (wd->self->surface);
	else
		lrg_water_surface_drop_scene (wd->self->surface);

	if (lrg_light_set_get_bound () != NULL)
		previous = g_object_ref (lrg_light_set_get_bound ());
	if (lights != NULL)
		lrg_light_set_bind (lights);
	lrg_water_surface_draw (wd->self->surface, wd->eye, wd->time);
	rlDrawRenderBatchActive ();
	if (previous != NULL)
		lrg_light_set_bind (previous);
	else
		lrg_light_set_unbind ();
}

static void
lrg_water_component_collect_render (LrgComponent  *component,
                                    LrgRenderList *list)
{
	LrgWaterComponent *self = LRG_WATER_COMPONENT (component);
	LrgEntity3D       *entity = lrg_component_get_entity (component);
	LrgRenderItem      item;
	LrgBoundingBox3D   local;
	WaterDraw         *wd;
	gfloat             pos[3], mn[3], mx[3], rise;
	guint              k;

	if (entity == NULL)
		return;
	lrg_entity3d_get_world_position (entity, pos);
	ensure_surface (self, pos);
	if (!self->built_ok || !lrg_water_body_get_bounds (self->body, mn, mx))
		return;

	/* the surface is in world space: an identity item with world bounds
	 * grown by the highest crest */
	lrg_render_item_init_for_entity (&item, entity, component);
	memset (item.world_matrix, 0, sizeof item.world_matrix);
	item.world_matrix[0] = item.world_matrix[5] = item.world_matrix[10] = item.world_matrix[15] = 1.0f;
	rise = lrg_water_surface_get_max_height (self->surface) * 1.5f + 0.01f;
	for (k = 0; k < 3; k++)
	{
		mn[k] += pos[k];
		mx[k] += pos[k];
	}
	local.min.x = mn[0] - rise;
	local.min.y = mn[1] - rise;
	local.min.z = mn[2] - rise;
	local.max.x = mx[0] + rise;
	local.max.y = mx[1] + rise;
	local.max.z = mx[2] + rise;
	item.kind = LRG_RENDER_ITEM_CUSTOM;
	item.flags = (LrgRenderItemFlags) (LRG_RENDER_ITEM_FLAG_TRANSPARENT | LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED);
	if (self->layer >= 0)
		item.layer = (guint) self->layer;
	item.sort_key = ((guint64) LRG_RENDER_ITEM_CUSTOM << 56) | 0x200u;
	lrg_render_item_set_local_bounds (&item, &local);
	if (!lrg_render_list_accepts (list, &item))
		return;

	wd = g_new0 (WaterDraw, 1);
	wd->self = g_object_ref (self);
	lrg_render_list_get_eye (list, wd->eye);
	wd->time = self->time;
	item.draw_func = draw_water;
	item.user_data = wd;
	item.user_data_destroy = water_draw_free;
	lrg_render_list_add (list, &item);
}

static gboolean
lrg_water_component_get_bounds (LrgComponent     *component,
                                LrgBoundingBox3D *out_local)
{
	LrgWaterComponent *self = LRG_WATER_COMPONENT (component);
	gfloat             mn[3], mx[3];

	if (!lrg_water_body_get_bounds (ensure_body (self), mn, mx))
		return FALSE;
	out_local->min.x = mn[0];
	out_local->min.y = mn[1];
	out_local->min.z = mn[2];
	out_local->max.x = mx[0];
	out_local->max.y = mx[1];
	out_local->max.z = mx[2];
	return TRUE;
}

static void
lrg_water_component_update (LrgComponent *component,
                            gfloat        delta)
{
	LrgWaterComponent *self = LRG_WATER_COMPONENT (component);

	if (isfinite (delta) && delta > 0.0f)
		self->time += delta;
}

static void
lrg_water_component_dispose (GObject *object)
{
	LrgWaterComponent *self = LRG_WATER_COMPONENT (object);

	g_clear_object (&self->body);
	g_clear_object (&self->surface);
	G_OBJECT_CLASS (lrg_water_component_parent_class)->dispose (object);
}

static void
lrg_water_component_finalize (GObject *object)
{
	LrgWaterComponent *self = LRG_WATER_COMPONENT (object);

	g_free (self->body_path);
	G_OBJECT_CLASS (lrg_water_component_parent_class)->finalize (object);
}

static void
lrg_water_component_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgWaterComponent *self = LRG_WATER_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_BODY:
		g_value_set_object (value, self->body);
		break;
	case PROP_BODY_PATH:
		g_value_set_string (value, self->body_path);
		break;
	case PROP_TIME:
		g_value_set_float (value, self->time);
		break;
	case PROP_CAPTURE:
		g_value_set_boolean (value, self->capture);
		break;
	case PROP_LAYER:
		g_value_set_int (value, self->layer);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_water_component_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgWaterComponent *self = LRG_WATER_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_BODY:
		lrg_water_component_set_body (self, g_value_get_object (value));
		break;
	case PROP_BODY_PATH:
		lrg_water_component_set_body_path (self, g_value_get_string (value));
		break;
	case PROP_TIME:
		lrg_water_component_set_time (self, g_value_get_float (value));
		break;
	case PROP_CAPTURE:
		lrg_water_component_set_capture (self, g_value_get_boolean (value));
		break;
	case PROP_LAYER:
		if (self->layer != g_value_get_int (value))
		{
			self->layer = g_value_get_int (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_water_component_class_init (LrgWaterComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_water_component_dispose;
	object_class->finalize = lrg_water_component_finalize;
	object_class->get_property = lrg_water_component_get_property;
	object_class->set_property = lrg_water_component_set_property;
	component_class->collect_render = lrg_water_component_collect_render;
	component_class->get_bounds = lrg_water_component_get_bounds;
	component_class->update = lrg_water_component_update;

	/**
	 * LrgWaterComponent:body:
	 *
	 * The water body drawn.
	 */
	properties[PROP_BODY] = g_param_spec_object ("body", NULL, "Water body", LRG_TYPE_WATER_BODY, rw);

	/**
	 * LrgWaterComponent:body-path:
	 *
	 * The `.rwater` file loaded when no body is set (the level node's
	 * asset).
	 */
	properties[PROP_BODY_PATH] = g_param_spec_string ("body-path", NULL, "Water body file", NULL, rw);

	/**
	 * LrgWaterComponent:time:
	 *
	 * The wave clock in seconds (advanced by update).
	 */
	properties[PROP_TIME] = g_param_spec_float ("time", NULL, "Wave clock", 0.0f, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgWaterComponent:capture:
	 *
	 * Whether the draw captures the frame for screen-space reflection and
	 * refraction.
	 */
	properties[PROP_CAPTURE] = g_param_spec_boolean ("capture", NULL, "Capture the frame", TRUE, rw);

	/**
	 * LrgWaterComponent:layer:
	 *
	 * Render layer override 0 .. 31, or -1 for the entity's layer.
	 */
	properties[PROP_LAYER] = g_param_spec_int ("layer", NULL, "Render layer (-1 entity)", -1, 31, -1, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_water_component_init (LrgWaterComponent *self)
{
	self->surface = lrg_water_surface_new ();
	self->capture = TRUE;
	self->layer = -1;
	self->dirty = TRUE;
}

LrgWaterComponent *
lrg_water_component_new (void)
{
	return (LrgWaterComponent *) g_object_new (LRG_TYPE_WATER_COMPONENT, NULL);
}

LrgWaterBody *
lrg_water_component_get_body (LrgWaterComponent *self)
{
	g_return_val_if_fail (LRG_IS_WATER_COMPONENT (self), NULL);
	return ensure_body (self);
}

void
lrg_water_component_set_body (LrgWaterComponent *self,
                              LrgWaterBody      *body)
{
	g_return_if_fail (LRG_IS_WATER_COMPONENT (self));
	if (g_set_object (&self->body, body))
	{
		self->dirty = TRUE;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BODY]);
	}
}

const gchar *
lrg_water_component_get_body_path (LrgWaterComponent *self)
{
	g_return_val_if_fail (LRG_IS_WATER_COMPONENT (self), NULL);
	return self->body_path;
}

void
lrg_water_component_set_body_path (LrgWaterComponent *self,
                                   const gchar       *path)
{
	g_return_if_fail (LRG_IS_WATER_COMPONENT (self));
	if (g_strcmp0 (self->body_path, path) == 0)
		return;
	g_free (self->body_path);
	self->body_path = g_strdup (path);
	self->load_failed = FALSE;
	g_clear_object (&self->body);
	self->dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BODY_PATH]);
}

LrgWaterSurface *
lrg_water_component_get_surface (LrgWaterComponent *self)
{
	g_return_val_if_fail (LRG_IS_WATER_COMPONENT (self), NULL);
	return self->surface;
}

gboolean
lrg_water_component_rebuild (LrgWaterComponent *self)
{
	LrgEntity3D *entity;
	gfloat       pos[3] = { 0.0f, 0.0f, 0.0f };

	g_return_val_if_fail (LRG_IS_WATER_COMPONENT (self), FALSE);
	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	if (entity != NULL)
		lrg_entity3d_get_world_position (entity, pos);
	self->dirty = TRUE;
	ensure_surface (self, pos);
	return self->built_ok;
}

gfloat
lrg_water_component_get_time (LrgWaterComponent *self)
{
	g_return_val_if_fail (LRG_IS_WATER_COMPONENT (self), 0.0f);
	return self->time;
}

void
lrg_water_component_set_time (LrgWaterComponent *self,
                              gfloat             time)
{
	g_return_if_fail (LRG_IS_WATER_COMPONENT (self));
	if (!isfinite (time) || time < 0.0f || self->time == time)
		return;
	self->time = time;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME]);
}

gboolean
lrg_water_component_get_capture (LrgWaterComponent *self)
{
	g_return_val_if_fail (LRG_IS_WATER_COMPONENT (self), FALSE);
	return self->capture;
}

void
lrg_water_component_set_capture (LrgWaterComponent *self,
                                 gboolean           capture)
{
	g_return_if_fail (LRG_IS_WATER_COMPONENT (self));
	capture = !!capture;
	if (self->capture == capture)
		return;
	self->capture = capture;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAPTURE]);
}

gboolean
lrg_water_component_level_at (LrgWaterComponent *self,
                              gfloat             x,
                              gfloat             z,
                              gfloat            *out_height)
{
	LrgEntity3D *entity;
	gfloat       pos[3] = { 0.0f, 0.0f, 0.0f }, level, offset[3];

	g_return_val_if_fail (LRG_IS_WATER_COMPONENT (self), FALSE);
	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	if (entity != NULL)
		lrg_entity3d_get_world_position (entity, pos);
	if (!lrg_water_body_contains (ensure_body (self), x - pos[0], z - pos[2], &level))
		return FALSE;
	ensure_surface (self, pos);
	lrg_water_surface_evaluate (self->surface, x, z, self->time, offset, NULL);
	if (out_height != NULL)
		*out_height = pos[1] + level + offset[1];
	return TRUE;
}
