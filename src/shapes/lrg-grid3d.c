/* lrg-grid3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D grid shape.
 */

#include "lrg-grid3d.h"

#include <rlgl.h>

#ifndef RAD2DEG
#define RAD2DEG (180.0f / 3.14159265358979323846f)
#endif

/**
 * LrgGrid3D:
 *
 * A 3D grid shape.
 *
 * Renders a grid centered at (0, 0, 0) on the XZ plane.
 * Note: The grid is always drawn at the origin regardless of position
 * settings, as this matches graylib's grid drawing behavior.
 */
struct _LrgGrid3D
{
	LrgShape3D parent_instance;

	gint   slices;
	gfloat spacing;
};

G_DEFINE_FINAL_TYPE (LrgGrid3D, lrg_grid3d, LRG_TYPE_SHAPE3D)

enum
{
	PROP_0,
	PROP_SLICES,
	PROP_SPACING,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Virtual Method Implementation
 * ========================================================================== */

static void
lrg_grid3d_draw (LrgShape *shape,
                 gfloat    delta)
{
	LrgGrid3D  *self = LRG_GRID3D (shape);
	GrlVector3 *pos  = lrg_shape3d_get_position (LRG_SHAPE3D (self));
	GrlVector3 *rot  = lrg_shape3d_get_rotation (LRG_SHAPE3D (self));
	GrlVector3 *scl  = lrg_shape3d_get_scale (LRG_SHAPE3D (self));

	rlPushMatrix ();

	/* Apply transforms: translate, rotate (XYZ order), scale */
	rlTranslatef (pos->x, pos->y, pos->z);
	rlRotatef (rot->x * RAD2DEG, 1.0f, 0.0f, 0.0f);
	rlRotatef (rot->y * RAD2DEG, 0.0f, 1.0f, 0.0f);
	rlRotatef (rot->z * RAD2DEG, 0.0f, 0.0f, 1.0f);
	rlScalef (scl->x, scl->y, scl->z);

	/* grl_draw_grid always draws centered at origin */
	grl_draw_grid (self->slices, self->spacing);

	rlPopMatrix ();
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_grid3d_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	LrgGrid3D *self = LRG_GRID3D (object);

	switch (prop_id)
	{
	case PROP_SLICES:
		g_value_set_int (value, self->slices);
		break;
	case PROP_SPACING:
		g_value_set_float (value, self->spacing);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_grid3d_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	LrgGrid3D *self = LRG_GRID3D (object);

	switch (prop_id)
	{
	case PROP_SLICES:
		self->slices = g_value_get_int (value);
		break;
	case PROP_SPACING:
		self->spacing = g_value_get_float (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_grid3d_class_init (LrgGrid3DClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	LrgShapeClass *shape_class  = LRG_SHAPE_CLASS (klass);

	object_class->get_property = lrg_grid3d_get_property;
	object_class->set_property = lrg_grid3d_set_property;

	shape_class->draw = lrg_grid3d_draw;

	/**
	 * LrgGrid3D:slices:
	 *
	 * The number of grid divisions.
	 */
	properties[PROP_SLICES] =
		g_param_spec_int ("slices",
		                  "Slices",
		                  "Number of grid divisions",
		                  1, 1000, 10,
		                  G_PARAM_READWRITE |
		                  G_PARAM_CONSTRUCT |
		                  G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGrid3D:spacing:
	 *
	 * The spacing between grid lines.
	 */
	properties[PROP_SPACING] =
		g_param_spec_float ("spacing",
		                    "Spacing",
		                    "Spacing between grid lines",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_grid3d_init (LrgGrid3D *self)
{
	self->slices  = 10;
	self->spacing = 1.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_grid3d_new:
 *
 * Creates a new grid with 10 slices and 1.0 spacing.
 *
 * Returns: (transfer full): A new #LrgGrid3D
 */
LrgGrid3D *
lrg_grid3d_new (void)
{
	return g_object_new (LRG_TYPE_GRID3D, NULL);
}

/**
 * lrg_grid3d_new_sized:
 * @slices: number of grid divisions
 * @spacing: spacing between grid lines
 *
 * Creates a new grid with specified dimensions.
 *
 * Returns: (transfer full): A new #LrgGrid3D
 */
LrgGrid3D *
lrg_grid3d_new_sized (gint   slices,
                      gfloat spacing)
{
	return g_object_new (LRG_TYPE_GRID3D,
	                     "slices", slices,
	                     "spacing", spacing,
	                     NULL);
}

/* Property accessors */

/**
 * lrg_grid3d_get_slices:
 * @self: an #LrgGrid3D
 *
 * Gets the number of grid divisions.
 *
 * Returns: The number of slices
 */
gint
lrg_grid3d_get_slices (LrgGrid3D *self)
{
	g_return_val_if_fail (LRG_IS_GRID3D (self), 0);

	return self->slices;
}

/**
 * lrg_grid3d_set_slices:
 * @self: an #LrgGrid3D
 * @slices: the number of slices
 *
 * Sets the number of grid divisions.
 */
void
lrg_grid3d_set_slices (LrgGrid3D *self,
                       gint       slices)
{
	g_return_if_fail (LRG_IS_GRID3D (self));
	g_return_if_fail (slices >= 1);

	if (self->slices != slices)
	{
		self->slices = slices;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SLICES]);
	}
}

/**
 * lrg_grid3d_get_spacing:
 * @self: an #LrgGrid3D
 *
 * Gets the spacing between grid lines.
 *
 * Returns: The spacing
 */
gfloat
lrg_grid3d_get_spacing (LrgGrid3D *self)
{
	g_return_val_if_fail (LRG_IS_GRID3D (self), 0.0f);

	return self->spacing;
}

/**
 * lrg_grid3d_set_spacing:
 * @self: an #LrgGrid3D
 * @spacing: the spacing value
 *
 * Sets the spacing between grid lines.
 */
void
lrg_grid3d_set_spacing (LrgGrid3D *self,
                        gfloat     spacing)
{
	g_return_if_fail (LRG_IS_GRID3D (self));
	g_return_if_fail (spacing >= 0.0f);

	if (self->spacing != spacing)
	{
		self->spacing = spacing;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPACING]);
	}
}
