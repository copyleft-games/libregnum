/* lrg-icosphere3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D icosphere shape.
 */

#include "lrg-icosphere3d.h"

#include <rlgl.h>

#ifndef RAD2DEG
#define RAD2DEG (180.0f / 3.14159265358979323846f)
#endif

/**
 * LrgIcoSphere3D:
 *
 * A 3D icosphere shape.
 *
 * An icosphere is a geodesic sphere created by subdividing an icosahedron.
 * This implementation uses a UV sphere mesh with settings that approximate
 * icosphere geometry. The subdivisions parameter controls the detail level.
 */
struct _LrgIcoSphere3D
{
	LrgShape3D parent_instance;

	gfloat radius;
	gint   subdivisions;

	/* Cached mesh and model */
	GrlMesh  *mesh;
	GrlModel *model;
	gboolean  mesh_dirty;
};

G_DEFINE_FINAL_TYPE (LrgIcoSphere3D, lrg_icosphere3d, LRG_TYPE_SHAPE3D)

enum
{
	PROP_0,
	PROP_RADIUS,
	PROP_SUBDIVISIONS,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Private Methods
 * ========================================================================== */

/*
 * Convert subdivisions to rings/slices for UV sphere mesh.
 * Each subdivision level roughly doubles the polygon count.
 * Subdivision 1 = 8 segments, 2 = 16, 3 = 32, etc.
 */
static gint
subdivisions_to_segments (gint subdivisions)
{
	return 4 * (1 << subdivisions);
}

static void
lrg_icosphere3d_update_mesh (LrgIcoSphere3D *self)
{
	gint segments;

	if (!self->mesh_dirty)
		return;

	g_clear_object (&self->model);
	g_clear_object (&self->mesh);

	segments = subdivisions_to_segments (self->subdivisions);

	self->mesh = grl_mesh_new_sphere (self->radius, segments, segments);

	if (self->mesh != NULL)
	{
		grl_mesh_upload (self->mesh, FALSE);
		self->model = grl_model_new_from_mesh (self->mesh);
	}

	self->mesh_dirty = FALSE;
}

/* ==========================================================================
 * Virtual Method Implementation
 * ========================================================================== */

static void
lrg_icosphere3d_draw (LrgShape *shape,
                      gfloat    delta)
{
	LrgIcoSphere3D        *self      = LRG_ICOSPHERE3D (shape);
	GrlVector3            *pos       = lrg_shape3d_get_position (LRG_SHAPE3D (self));
	GrlVector3            *rot       = lrg_shape3d_get_rotation (LRG_SHAPE3D (self));
	GrlVector3            *scl       = lrg_shape3d_get_scale (LRG_SHAPE3D (self));
	GrlColor              *color     = lrg_shape_get_color (shape);
	gboolean               wireframe = lrg_shape3d_get_wireframe (LRG_SHAPE3D (self));
	g_autoptr(GrlVector3)  origin    = grl_vector3_new (0.0f, 0.0f, 0.0f);

	lrg_icosphere3d_update_mesh (self);

	if (self->model == NULL)
		return;

	rlPushMatrix ();

	/* Apply transforms: translate, rotate (XYZ order), scale */
	rlTranslatef (pos->x, pos->y, pos->z);
	rlRotatef (rot->x * RAD2DEG, 1.0f, 0.0f, 0.0f);
	rlRotatef (rot->y * RAD2DEG, 0.0f, 1.0f, 0.0f);
	rlRotatef (rot->z * RAD2DEG, 0.0f, 0.0f, 1.0f);
	rlScalef (scl->x, scl->y, scl->z);

	/* Draw at origin (position handled by matrix) */
	if (wireframe)
		grl_model_draw_wires (self->model, origin, 1.0f, color);
	else
		grl_model_draw (self->model, origin, 1.0f, color);

	rlPopMatrix ();
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_icosphere3d_finalize (GObject *object)
{
	LrgIcoSphere3D *self = LRG_ICOSPHERE3D (object);

	g_clear_object (&self->model);
	g_clear_object (&self->mesh);

	G_OBJECT_CLASS (lrg_icosphere3d_parent_class)->finalize (object);
}

static void
lrg_icosphere3d_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgIcoSphere3D *self = LRG_ICOSPHERE3D (object);

	switch (prop_id)
	{
	case PROP_RADIUS:
		g_value_set_float (value, self->radius);
		break;
	case PROP_SUBDIVISIONS:
		g_value_set_int (value, self->subdivisions);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_icosphere3d_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgIcoSphere3D *self = LRG_ICOSPHERE3D (object);

	switch (prop_id)
	{
	case PROP_RADIUS:
		self->radius = g_value_get_float (value);
		self->mesh_dirty = TRUE;
		break;
	case PROP_SUBDIVISIONS:
		self->subdivisions = g_value_get_int (value);
		self->mesh_dirty = TRUE;
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_icosphere3d_class_init (LrgIcoSphere3DClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	LrgShapeClass *shape_class  = LRG_SHAPE_CLASS (klass);

	object_class->finalize     = lrg_icosphere3d_finalize;
	object_class->get_property = lrg_icosphere3d_get_property;
	object_class->set_property = lrg_icosphere3d_set_property;

	shape_class->draw = lrg_icosphere3d_draw;

	/**
	 * LrgIcoSphere3D:radius:
	 *
	 * The icosphere's radius.
	 */
	properties[PROP_RADIUS] =
		g_param_spec_float ("radius",
		                    "Radius",
		                    "The icosphere's radius",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgIcoSphere3D:subdivisions:
	 *
	 * The number of subdivisions (detail level).
	 * Higher values create smoother spheres but use more polygons.
	 * Typical values: 1 (low), 2 (medium), 3 (high), 4 (very high).
	 */
	properties[PROP_SUBDIVISIONS] =
		g_param_spec_int ("subdivisions",
		                  "Subdivisions",
		                  "Number of subdivisions (detail level)",
		                  1, 6, 2,
		                  G_PARAM_READWRITE |
		                  G_PARAM_CONSTRUCT |
		                  G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_icosphere3d_init (LrgIcoSphere3D *self)
{
	self->radius       = 1.0f;
	self->subdivisions = 2;
	self->mesh         = NULL;
	self->model        = NULL;
	self->mesh_dirty   = TRUE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_icosphere3d_new:
 *
 * Creates a new icosphere at the origin with radius 1.0 and 2 subdivisions.
 *
 * Returns: (transfer full): A new #LrgIcoSphere3D
 */
LrgIcoSphere3D *
lrg_icosphere3d_new (void)
{
	return g_object_new (LRG_TYPE_ICOSPHERE3D, NULL);
}

/**
 * lrg_icosphere3d_new_at:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius: sphere radius
 *
 * Creates a new icosphere at the specified position with given radius.
 *
 * Returns: (transfer full): A new #LrgIcoSphere3D
 */
LrgIcoSphere3D *
lrg_icosphere3d_new_at (gfloat x,
                        gfloat y,
                        gfloat z,
                        gfloat radius)
{
	LrgIcoSphere3D *sphere;

	sphere = g_object_new (LRG_TYPE_ICOSPHERE3D,
	                       "radius", radius,
	                       NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (sphere), x, y, z);

	return sphere;
}

/**
 * lrg_icosphere3d_new_full:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius: sphere radius
 * @subdivisions: number of subdivisions (detail level)
 * @color: (transfer none): the sphere color
 *
 * Creates a new icosphere with full configuration.
 *
 * Returns: (transfer full): A new #LrgIcoSphere3D
 */
LrgIcoSphere3D *
lrg_icosphere3d_new_full (gfloat    x,
                          gfloat    y,
                          gfloat    z,
                          gfloat    radius,
                          gint      subdivisions,
                          GrlColor *color)
{
	LrgIcoSphere3D *sphere;

	sphere = g_object_new (LRG_TYPE_ICOSPHERE3D,
	                       "radius", radius,
	                       "subdivisions", subdivisions,
	                       "color", color,
	                       NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (sphere), x, y, z);

	return sphere;
}

/* Property accessors */

/**
 * lrg_icosphere3d_get_radius:
 * @self: an #LrgIcoSphere3D
 *
 * Gets the icosphere's radius.
 *
 * Returns: The radius
 */
gfloat
lrg_icosphere3d_get_radius (LrgIcoSphere3D *self)
{
	g_return_val_if_fail (LRG_IS_ICOSPHERE3D (self), 0.0f);

	return self->radius;
}

/**
 * lrg_icosphere3d_set_radius:
 * @self: an #LrgIcoSphere3D
 * @radius: the radius value
 *
 * Sets the icosphere's radius.
 */
void
lrg_icosphere3d_set_radius (LrgIcoSphere3D *self,
                            gfloat          radius)
{
	g_return_if_fail (LRG_IS_ICOSPHERE3D (self));
	g_return_if_fail (radius >= 0.0f);

	if (self->radius != radius)
	{
		self->radius = radius;
		self->mesh_dirty = TRUE;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]);
	}
}

/**
 * lrg_icosphere3d_get_subdivisions:
 * @self: an #LrgIcoSphere3D
 *
 * Gets the number of subdivisions.
 *
 * Returns: The number of subdivisions
 */
gint
lrg_icosphere3d_get_subdivisions (LrgIcoSphere3D *self)
{
	g_return_val_if_fail (LRG_IS_ICOSPHERE3D (self), 0);

	return self->subdivisions;
}

/**
 * lrg_icosphere3d_set_subdivisions:
 * @self: an #LrgIcoSphere3D
 * @subdivisions: the number of subdivisions
 *
 * Sets the number of subdivisions.
 */
void
lrg_icosphere3d_set_subdivisions (LrgIcoSphere3D *self,
                                  gint            subdivisions)
{
	g_return_if_fail (LRG_IS_ICOSPHERE3D (self));
	g_return_if_fail (subdivisions >= 1 && subdivisions <= 6);

	if (self->subdivisions != subdivisions)
	{
		self->subdivisions = subdivisions;
		self->mesh_dirty = TRUE;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBDIVISIONS]);
	}
}
