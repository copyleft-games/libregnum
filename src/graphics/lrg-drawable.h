/* lrg-drawable.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for objects that can be rendered.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_DRAWABLE (lrg_drawable_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgDrawable, lrg_drawable, LRG, DRAWABLE, GObject)

/**
 * LrgDrawableInterface:
 * @parent_iface: Parent interface
 * @draw: Draw the object with the given delta time
 * @get_bounds: Get the bounding rectangle (optional, may return zeros)
 *
 * Interface for objects that can be rendered.
 */
struct _LrgDrawableInterface
{
	GTypeInterface parent_iface;

	/*< public >*/

	/**
	 * LrgDrawableInterface::draw:
	 * @self: an #LrgDrawable
	 * @delta: time since last frame in seconds
	 *
	 * Render the drawable object.
	 */
	void (*draw) (LrgDrawable *self,
	              gfloat       delta);

	/**
	 * LrgDrawableInterface::get_bounds:
	 * @self: an #LrgDrawable
	 * @out_bounds: (out): location to store the bounding rectangle
	 *
	 * Get the bounding rectangle of the drawable.
	 * This is optional and implementations may leave out_bounds unchanged.
	 */
	void (*get_bounds) (LrgDrawable  *self,
	                    GrlRectangle *out_bounds);
};

/**
 * lrg_drawable_draw:
 * @self: an #LrgDrawable
 * @delta: time since last frame in seconds
 *
 * Render the drawable object.
 *
 * This calls the draw() virtual method on the interface.
 */
LRG_AVAILABLE_IN_ALL
void lrg_drawable_draw (LrgDrawable *self,
                        gfloat       delta);

/**
 * lrg_drawable_get_bounds:
 * @self: an #LrgDrawable
 * @out_bounds: (out): location to store the bounding rectangle
 *
 * Get the bounding rectangle of the drawable.
 *
 * If the implementation doesn't provide bounds, out_bounds is
 * initialized to zeros.
 */
LRG_AVAILABLE_IN_ALL
void lrg_drawable_get_bounds (LrgDrawable  *self,
                              GrlRectangle *out_bounds);

G_END_DECLS
