/* lrg-torus3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D torus shape.
 */

#include "lrg-torus3d.h"

#include <rlgl.h>

#ifndef RAD2DEG
#define RAD2DEG (180.0f / 3.14159265358979323846f)
#endif

/**
 * LrgTorus3D:
 *
 * A 3D torus (donut) shape.
 *
 * Renders a torus using mesh generation and model drawing.
 * The torus is defined by a major radius (center to tube center)
 * and minor radius (tube thickness).
 */
struct _LrgTorus3D
{
	LrgShape3D parent_instance;

	gfloat major_radius;
	gfloat minor_radius;
	gint   major_segments;
	gint   minor_segments;

	/* Cached mesh and model */
	GrlMesh  *mesh;
	GrlModel *model;
	gboolean  mesh_dirty;
};

G_DEFINE_FINAL_TYPE (LrgTorus3D, lrg_torus3d, LRG_TYPE_SHAPE3D)

enum
{
	PROP_0,
	PROP_MAJOR_RADIUS,
	PROP_MINOR_RADIUS,
	PROP_MAJOR_SEGMENTS,
	PROP_MINOR_SEGMENTS,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Private Methods
 * ========================================================================== */

static void
lrg_torus3d_update_mesh (LrgTorus3D *self)
{
	if (!self->mesh_dirty)
		return;

	g_clear_object (&self->model);
	g_clear_object (&self->mesh);

	/*
	 * grl_mesh_new_torus takes:
	 * - radius: major radius
	 * - size: minor radius (tube size)
	 * - rad_seg: radial segments (major)
	 * - sides: number of sides (minor)
	 */
	self->mesh = grl_mesh_new_torus (self->major_radius,
	                                 self->minor_radius,
	                                 self->major_segments,
	                                 self->minor_segments);

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
lrg_torus3d_draw (LrgShape *shape,
                  gfloat    delta)
{
	LrgTorus3D            *self      = LRG_TORUS3D (shape);
	GrlVector3            *pos       = lrg_shape3d_get_position (LRG_SHAPE3D (self));
	GrlVector3            *rot       = lrg_shape3d_get_rotation (LRG_SHAPE3D (self));
	GrlVector3            *scl       = lrg_shape3d_get_scale (LRG_SHAPE3D (self));
	GrlColor              *color     = lrg_shape_get_color (shape);
	gboolean               wireframe = lrg_shape3d_get_wireframe (LRG_SHAPE3D (self));
	g_autoptr(GrlVector3)  origin    = grl_vector3_new (0.0f, 0.0f, 0.0f);

	lrg_torus3d_update_mesh (self);

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
lrg_torus3d_finalize (GObject *object)
{
	LrgTorus3D *self = LRG_TORUS3D (object);

	g_clear_object (&self->model);
	g_clear_object (&self->mesh);

	G_OBJECT_CLASS (lrg_torus3d_parent_class)->finalize (object);
}

static void
lrg_torus3d_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	LrgTorus3D *self = LRG_TORUS3D (object);

	switch (prop_id)
	{
	case PROP_MAJOR_RADIUS:
		g_value_set_float (value, self->major_radius);
		break;
	case PROP_MINOR_RADIUS:
		g_value_set_float (value, self->minor_radius);
		break;
	case PROP_MAJOR_SEGMENTS:
		g_value_set_int (value, self->major_segments);
		break;
	case PROP_MINOR_SEGMENTS:
		g_value_set_int (value, self->minor_segments);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_torus3d_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	LrgTorus3D *self = LRG_TORUS3D (object);

	switch (prop_id)
	{
	case PROP_MAJOR_RADIUS:
		self->major_radius = g_value_get_float (value);
		self->mesh_dirty = TRUE;
		break;
	case PROP_MINOR_RADIUS:
		self->minor_radius = g_value_get_float (value);
		self->mesh_dirty = TRUE;
		break;
	case PROP_MAJOR_SEGMENTS:
		self->major_segments = g_value_get_int (value);
		self->mesh_dirty = TRUE;
		break;
	case PROP_MINOR_SEGMENTS:
		self->minor_segments = g_value_get_int (value);
		self->mesh_dirty = TRUE;
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_torus3d_class_init (LrgTorus3DClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	LrgShapeClass *shape_class  = LRG_SHAPE_CLASS (klass);

	object_class->finalize     = lrg_torus3d_finalize;
	object_class->get_property = lrg_torus3d_get_property;
	object_class->set_property = lrg_torus3d_set_property;

	shape_class->draw = lrg_torus3d_draw;

	/**
	 * LrgTorus3D:major-radius:
	 *
	 * The major radius (center to tube center).
	 */
	properties[PROP_MAJOR_RADIUS] =
		g_param_spec_float ("major-radius",
		                    "Major Radius",
		                    "Distance from center to tube center",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgTorus3D:minor-radius:
	 *
	 * The minor radius (tube radius).
	 */
	properties[PROP_MINOR_RADIUS] =
		g_param_spec_float ("minor-radius",
		                    "Minor Radius",
		                    "The tube radius",
		                    0.0f, G_MAXFLOAT, 0.25f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgTorus3D:major-segments:
	 *
	 * The number of segments around the torus.
	 */
	properties[PROP_MAJOR_SEGMENTS] =
		g_param_spec_int ("major-segments",
		                  "Major Segments",
		                  "Segments around the torus",
		                  3, 256, 32,
		                  G_PARAM_READWRITE |
		                  G_PARAM_CONSTRUCT |
		                  G_PARAM_STATIC_STRINGS);

	/**
	 * LrgTorus3D:minor-segments:
	 *
	 * The number of segments around the tube.
	 */
	properties[PROP_MINOR_SEGMENTS] =
		g_param_spec_int ("minor-segments",
		                  "Minor Segments",
		                  "Segments around the tube",
		                  3, 256, 16,
		                  G_PARAM_READWRITE |
		                  G_PARAM_CONSTRUCT |
		                  G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_torus3d_init (LrgTorus3D *self)
{
	self->major_radius   = 1.0f;
	self->minor_radius   = 0.25f;
	self->major_segments = 32;
	self->minor_segments = 16;
	self->mesh           = NULL;
	self->model          = NULL;
	self->mesh_dirty     = TRUE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_torus3d_new:
 *
 * Creates a new torus at the origin with default dimensions.
 *
 * Returns: (transfer full): A new #LrgTorus3D
 */
LrgTorus3D *
lrg_torus3d_new (void)
{
	return g_object_new (LRG_TYPE_TORUS3D, NULL);
}

/**
 * lrg_torus3d_new_at:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @major_radius: distance from center to tube center
 * @minor_radius: tube radius
 *
 * Creates a new torus at the specified position with given radii.
 *
 * Returns: (transfer full): A new #LrgTorus3D
 */
LrgTorus3D *
lrg_torus3d_new_at (gfloat x,
                    gfloat y,
                    gfloat z,
                    gfloat major_radius,
                    gfloat minor_radius)
{
	LrgTorus3D *torus;

	torus = g_object_new (LRG_TYPE_TORUS3D,
	                      "major-radius", major_radius,
	                      "minor-radius", minor_radius,
	                      NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (torus), x, y, z);

	return torus;
}

/**
 * lrg_torus3d_new_full:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @major_radius: distance from center to tube center
 * @minor_radius: tube radius
 * @major_segments: segments around the torus
 * @minor_segments: segments around the tube
 * @color: (transfer none): the torus color
 *
 * Creates a new torus with full configuration.
 *
 * Returns: (transfer full): A new #LrgTorus3D
 */
LrgTorus3D *
lrg_torus3d_new_full (gfloat    x,
                      gfloat    y,
                      gfloat    z,
                      gfloat    major_radius,
                      gfloat    minor_radius,
                      gint      major_segments,
                      gint      minor_segments,
                      GrlColor *color)
{
	LrgTorus3D *torus;

	torus = g_object_new (LRG_TYPE_TORUS3D,
	                      "major-radius", major_radius,
	                      "minor-radius", minor_radius,
	                      "major-segments", major_segments,
	                      "minor-segments", minor_segments,
	                      "color", color,
	                      NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (torus), x, y, z);

	return torus;
}

/* Property accessors */

/**
 * lrg_torus3d_get_major_radius:
 * @self: an #LrgTorus3D
 *
 * Gets the major radius.
 *
 * Returns: The major radius
 */
gfloat
lrg_torus3d_get_major_radius (LrgTorus3D *self)
{
	g_return_val_if_fail (LRG_IS_TORUS3D (self), 0.0f);

	return self->major_radius;
}

/**
 * lrg_torus3d_set_major_radius:
 * @self: an #LrgTorus3D
 * @radius: the major radius
 *
 * Sets the major radius.
 */
void
lrg_torus3d_set_major_radius (LrgTorus3D *self,
                              gfloat      radius)
{
	g_return_if_fail (LRG_IS_TORUS3D (self));
	g_return_if_fail (radius >= 0.0f);

	if (self->major_radius != radius)
	{
		self->major_radius = radius;
		self->mesh_dirty = TRUE;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAJOR_RADIUS]);
	}
}

/**
 * lrg_torus3d_get_minor_radius:
 * @self: an #LrgTorus3D
 *
 * Gets the minor radius.
 *
 * Returns: The minor radius
 */
gfloat
lrg_torus3d_get_minor_radius (LrgTorus3D *self)
{
	g_return_val_if_fail (LRG_IS_TORUS3D (self), 0.0f);

	return self->minor_radius;
}

/**
 * lrg_torus3d_set_minor_radius:
 * @self: an #LrgTorus3D
 * @radius: the minor radius
 *
 * Sets the minor radius.
 */
void
lrg_torus3d_set_minor_radius (LrgTorus3D *self,
                              gfloat      radius)
{
	g_return_if_fail (LRG_IS_TORUS3D (self));
	g_return_if_fail (radius >= 0.0f);

	if (self->minor_radius != radius)
	{
		self->minor_radius = radius;
		self->mesh_dirty = TRUE;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MINOR_RADIUS]);
	}
}

/**
 * lrg_torus3d_get_major_segments:
 * @self: an #LrgTorus3D
 *
 * Gets the number of major segments.
 *
 * Returns: The number of major segments
 */
gint
lrg_torus3d_get_major_segments (LrgTorus3D *self)
{
	g_return_val_if_fail (LRG_IS_TORUS3D (self), 0);

	return self->major_segments;
}

/**
 * lrg_torus3d_set_major_segments:
 * @self: an #LrgTorus3D
 * @segments: the number of major segments
 *
 * Sets the number of major segments.
 */
void
lrg_torus3d_set_major_segments (LrgTorus3D *self,
                                gint        segments)
{
	g_return_if_fail (LRG_IS_TORUS3D (self));
	g_return_if_fail (segments >= 3);

	if (self->major_segments != segments)
	{
		self->major_segments = segments;
		self->mesh_dirty = TRUE;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAJOR_SEGMENTS]);
	}
}

/**
 * lrg_torus3d_get_minor_segments:
 * @self: an #LrgTorus3D
 *
 * Gets the number of minor segments.
 *
 * Returns: The number of minor segments
 */
gint
lrg_torus3d_get_minor_segments (LrgTorus3D *self)
{
	g_return_val_if_fail (LRG_IS_TORUS3D (self), 0);

	return self->minor_segments;
}

/**
 * lrg_torus3d_set_minor_segments:
 * @self: an #LrgTorus3D
 * @segments: the number of minor segments
 *
 * Sets the number of minor segments.
 */
void
lrg_torus3d_set_minor_segments (LrgTorus3D *self,
                                gint        segments)
{
	g_return_if_fail (LRG_IS_TORUS3D (self));
	g_return_if_fail (segments >= 3);

	if (self->minor_segments != segments)
	{
		self->minor_segments = segments;
		self->mesh_dirty = TRUE;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MINOR_SEGMENTS]);
	}
}
