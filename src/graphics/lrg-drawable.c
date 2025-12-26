/* lrg-drawable.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for objects that can be rendered.
 */

#include "config.h"
#include "lrg-drawable.h"

/**
 * SECTION:lrg-drawable
 * @title: LrgDrawable
 * @short_description: Interface for renderable objects
 *
 * #LrgDrawable is an interface that can be implemented by any
 * object that can be rendered. It provides a standard way to
 * draw objects and query their bounding rectangles.
 *
 * This interface is useful for:
 * - ECS components that need to be rendered
 * - Game objects that implement custom rendering
 * - Integration with World3D visibility culling
 *
 * ## Implementing LrgDrawable
 *
 * To implement #LrgDrawable, provide implementations of the
 * draw() virtual method. The get_bounds() method is optional.
 *
 * |[<!-- language="C" -->
 * static void
 * my_object_draw (LrgDrawable *drawable, gfloat delta)
 * {
 *     MyObject *self = MY_OBJECT (drawable);
 *     // Draw the object using grl_draw_* functions
 * }
 *
 * static void
 * my_object_drawable_init (LrgDrawableInterface *iface)
 * {
 *     iface->draw = my_object_draw;
 * }
 * ]|
 */

G_DEFINE_INTERFACE (LrgDrawable, lrg_drawable, G_TYPE_OBJECT)

static void
lrg_drawable_default_init (LrgDrawableInterface *iface)
{
	/* Default implementations are NULL, subclasses must provide draw() */
}

/**
 * lrg_drawable_draw:
 * @self: an #LrgDrawable
 * @delta: time since last frame in seconds
 *
 * Render the drawable object.
 *
 * This calls the draw() virtual method on the interface.
 * If the implementation doesn't provide a draw method,
 * this function does nothing.
 */
void
lrg_drawable_draw (LrgDrawable *self,
                   gfloat       delta)
{
	LrgDrawableInterface *iface;

	g_return_if_fail (LRG_IS_DRAWABLE (self));

	iface = LRG_DRAWABLE_GET_IFACE (self);

	if (iface->draw != NULL)
		iface->draw (self, delta);
}

/**
 * lrg_drawable_get_bounds:
 * @self: an #LrgDrawable
 * @out_bounds: (out): location to store the bounding rectangle
 *
 * Get the bounding rectangle of the drawable.
 *
 * If the implementation doesn't provide bounds, out_bounds is
 * initialized to zeros (x=0, y=0, width=0, height=0).
 */
void
lrg_drawable_get_bounds (LrgDrawable  *self,
                         GrlRectangle *out_bounds)
{
	LrgDrawableInterface *iface;

	g_return_if_fail (LRG_IS_DRAWABLE (self));
	g_return_if_fail (out_bounds != NULL);

	/* Initialize to zero bounds by default */
	out_bounds->x = 0.0f;
	out_bounds->y = 0.0f;
	out_bounds->width = 0.0f;
	out_bounds->height = 0.0f;

	iface = LRG_DRAWABLE_GET_IFACE (self);

	if (iface->get_bounds != NULL)
		iface->get_bounds (self, out_bounds);
}
