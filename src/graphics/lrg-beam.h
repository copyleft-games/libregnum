/* lrg-beam.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBeam: an animated line or arc between two points that can move --
 * a phone call between two characters, a link between two machines, a
 * tether.  A quadratic curve with a bend, a colour and width, dashes
 * that flow from one end to the other, and a progress for drawing it
 * out.  The geometry (samples, length, dash spans) is computed without
 * a window; drawing works in a 2D camera mode or a 3D one.
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

/**
 * LrgBeamBend:
 * @LRG_BEAM_BEND_PERPENDICULAR: the arc bulges sideways in the x/y plane,
 *   to the left of the line from start to end (a 2D map, y down: a beam
 *   going right bulges up); a negative height bulges the other way
 * @LRG_BEAM_BEND_AXIS: the arc bulges along a fixed axis (default +y, up
 *   in a 3D world)
 *
 * Which way an arced #LrgBeam bends.
 *
 * Since: 0.11.0
 */
typedef enum
{
	LRG_BEAM_BEND_PERPENDICULAR,
	LRG_BEAM_BEND_AXIS
} LrgBeamBend;

LRG_AVAILABLE_IN_ALL
GType lrg_beam_bend_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BEAM_BEND (lrg_beam_bend_get_type ())

#define LRG_TYPE_BEAM (lrg_beam_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBeam, lrg_beam, LRG, BEAM, GObject)

/**
 * lrg_beam_new:
 *
 * Creates a straight, solid, white beam 2 units wide with 24 segments.
 *
 * Returns: (transfer full): a new #LrgBeam
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgBeam * lrg_beam_new (void);

/**
 * lrg_beam_set_endpoints:
 * @self: an #LrgBeam
 * @start_x: start x
 * @start_y: start y
 * @start_z: start z (0 in 2D)
 * @end_x: end x
 * @end_y: end y
 * @end_z: end z
 *
 * Moves both ends; call it every frame for ends that move.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_beam_set_endpoints (LrgBeam *self,
                             gfloat   start_x,
                             gfloat   start_y,
                             gfloat   start_z,
                             gfloat   end_x,
                             gfloat   end_y,
                             gfloat   end_z);

/**
 * lrg_beam_set_color:
 * @self: an #LrgBeam
 * @color: the colour (alpha included)
 *
 * Sets the colour.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_beam_set_color (LrgBeam        *self,
                         const GrlColor *color);

/**
 * lrg_beam_set_width:
 * @self: an #LrgBeam
 * @width: thickness in world units (pixels at 2D zoom 1); 0 draws
 *   hairlines
 *
 * Sets the thickness.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_beam_set_width (LrgBeam *self,
                         gfloat   width);

/**
 * lrg_beam_set_arc:
 * @self: an #LrgBeam
 * @height: how far the middle of the arc is pulled from the straight
 *   line, in world units (0: straight)
 * @bend: which way it bends
 *
 * Bends the beam into a quadratic arc.  The curve's highest point is
 * @height from the line (its control point is 2 x @height out).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_beam_set_arc (LrgBeam     *self,
                       gfloat       height,
                       LrgBeamBend  bend);

/**
 * lrg_beam_set_arc_axis:
 * @self: an #LrgBeam
 * @x: axis x
 * @y: axis y
 * @z: axis z
 *
 * Sets the direction %LRG_BEAM_BEND_AXIS bends towards (normalised; a
 * zero vector is ignored).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_beam_set_arc_axis (LrgBeam *self,
                            gfloat   x,
                            gfloat   y,
                            gfloat   z);

/**
 * lrg_beam_set_dash:
 * @self: an #LrgBeam
 * @dash_length: length of each dash (> 0)
 * @gap_length: length of each gap; 0 makes the beam solid
 * @speed: how fast dashes travel from start to end, units per second
 *   (negative flows back)
 *
 * Makes the beam dashed and animated.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_beam_set_dash (LrgBeam *self,
                        gfloat   dash_length,
                        gfloat   gap_length,
                        gfloat   speed);

/**
 * lrg_beam_set_segments:
 * @self: an #LrgBeam
 * @segments: straight pieces the curve is drawn with (>= 1)
 *
 * Sets how smooth the arc is.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_beam_set_segments (LrgBeam *self,
                            guint    segments);

/**
 * lrg_beam_set_progress:
 * @self: an #LrgBeam
 * @progress: the drawn fraction of the beam from its start, 0 to 1
 *
 * Draws only the beginning of the beam, to grow it from the caller to
 * the callee (or shrink it away).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_beam_set_progress (LrgBeam *self,
                            gfloat   progress);

/**
 * lrg_beam_get_progress:
 * @self: an #LrgBeam
 *
 * Returns: the drawn fraction
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_beam_get_progress (LrgBeam *self);

/**
 * lrg_beam_update:
 * @self: an #LrgBeam
 * @delta: seconds
 *
 * Moves the dashes along.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_beam_update (LrgBeam *self,
                      gfloat   delta);

/**
 * lrg_beam_sample:
 * @self: an #LrgBeam
 * @t: curve parameter, 0 (start) to 1 (end)
 * @x: (out) (optional): x
 * @y: (out) (optional): y
 * @z: (out) (optional): z
 *
 * Evaluates the curve.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_beam_sample (LrgBeam *self,
                      gfloat   t,
                      gfloat  *x,
                      gfloat  *y,
                      gfloat  *z);

/**
 * lrg_beam_get_length:
 * @self: an #LrgBeam
 *
 * Returns: the length of the drawn polyline (the whole beam)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_beam_get_length (LrgBeam *self);

/**
 * lrg_beam_get_n_dashes:
 * @self: an #LrgBeam
 *
 * Returns: how many visible pieces the beam is drawn as now (1 for a
 *   solid beam, 0 when nothing is drawn)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_beam_get_n_dashes (LrgBeam *self);

/**
 * lrg_beam_get_dash:
 * @self: an #LrgBeam
 * @index: a piece index, from the start
 * @from: (out) (optional): where it starts, as a distance along the beam
 * @to: (out) (optional): where it ends
 *
 * Reads one visible piece.
 *
 * Returns: %FALSE when @index is out of range
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_beam_get_dash (LrgBeam *self,
                            guint    index,
                            gfloat  *from,
                            gfloat  *to);

/**
 * lrg_beam_draw_2d:
 * @self: an #LrgBeam
 *
 * Draws with x and y (z ignored), inside or outside a 2D camera mode.
 * Needs a GL context; does nothing without one.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_beam_draw_2d (LrgBeam *self);

/**
 * lrg_beam_draw_3d:
 * @self: an #LrgBeam
 *
 * Draws in 3D, inside a 3D camera mode: cylinders of the beam's width,
 * or lines when the width is 0.  Needs a GL context.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_beam_draw_3d (LrgBeam *self);

G_END_DECLS
