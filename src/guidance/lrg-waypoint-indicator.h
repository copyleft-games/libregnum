/* lrg-waypoint-indicator.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWaypointIndicator - where a world point shows on the screen for a
 * perspective view, and, when it is off the screen or behind the
 * viewer, the point on the screen's edge an arrow towards it stands at
 * (the way guidance games pin an off-screen quest marker to the edge).
 * Pure arithmetic: no window or graphics context is needed, so a HUD
 * can be laid out and tested headless.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../graphics/lrg-camera3d.h"

G_BEGIN_DECLS

/**
 * LRG_WAYPOINT_INDICATOR_NEAR:
 *
 * Points closer than this along the view direction (world units) count
 * as behind the viewer.
 */
#define LRG_WAYPOINT_INDICATOR_NEAR (0.01f)

/**
 * LrgWaypointIndicator:
 * @x: screen X in the units of the size passed (0 at the left)
 * @y: screen Y (0 at the top)
 * @depth: the point's distance along the view direction (negative
 *   behind the viewer)
 * @angle: radians of the direction from the screen's centre to the
 *   indicator: 0 to the right, %G_PI / 2 down, -%G_PI / 2 up
 * @on_screen: the point projects inside the screen less the margin
 * @behind: the point is behind the viewer
 *
 * The result of lrg_waypoint_indicator_compute().  When @on_screen is
 * %FALSE, (@x, @y) lies on the edge of the screen inset by the margin,
 * in the direction of the point.
 */
typedef struct
{
    gfloat   x;
    gfloat   y;
    gfloat   depth;
    gfloat   angle;
    gboolean on_screen;
    gboolean behind;
} LrgWaypointIndicator;

#define LRG_TYPE_WAYPOINT_INDICATOR (lrg_waypoint_indicator_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_waypoint_indicator_get_type (void) G_GNUC_CONST;

/**
 * lrg_waypoint_indicator_copy:
 * @self: an #LrgWaypointIndicator
 *
 * Returns: (transfer full): a copy
 */
LRG_AVAILABLE_IN_ALL
LrgWaypointIndicator *
lrg_waypoint_indicator_copy (const LrgWaypointIndicator *self);

/**
 * lrg_waypoint_indicator_free:
 * @self: (nullable): an #LrgWaypointIndicator from
 *   lrg_waypoint_indicator_copy()
 */
LRG_AVAILABLE_IN_ALL
void
lrg_waypoint_indicator_free (LrgWaypointIndicator *self);

/**
 * lrg_waypoint_indicator_compute:
 * @out: (out caller-allocates): the indicator
 * @eye: (array fixed-size=3): the camera position
 * @target: (array fixed-size=3): the point the camera looks at
 * @up: (array fixed-size=3): the camera's up vector
 * @fovy: the vertical field of view, degrees (raylib's fovy)
 * @width: the screen width (any unit: pixels, virtual units)
 * @height: the screen height, same unit
 * @margin: how far an edge indicator keeps from the edges, same unit
 * @point: (array fixed-size=3): the world point
 *
 * Projects @point as raylib's perspective camera does (a right-handed
 * view, the right vector forward x up, the aspect @width / @height).
 * A point in front and inside the screen less @margin is on screen at
 * its projection; any other point is placed on the inset edge in the
 * direction it lies in (for a point behind the viewer, its side seen
 * from the front; straight behind is straight down).
 *
 * Returns: %FALSE (and @out cleared) when the view is degenerate: @eye
 *   equal to @target, @up parallel to the view, a non-positive size or
 *   field of view
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_waypoint_indicator_compute (LrgWaypointIndicator *out,
                                const gfloat         *eye,
                                const gfloat         *target,
                                const gfloat         *up,
                                gfloat                fovy,
                                gfloat                width,
                                gfloat                height,
                                gfloat                margin,
                                const gfloat         *point);

/**
 * lrg_waypoint_indicator_compute_for_camera:
 * @out: (out caller-allocates): the indicator
 * @camera: a perspective #LrgCamera3D
 * @width: the screen width
 * @height: the screen height
 * @margin: the edge margin
 * @point: (array fixed-size=3): the world point
 *
 * lrg_waypoint_indicator_compute() with @camera's position, target, up
 * and field of view.
 *
 * Returns: %FALSE when the camera's view is degenerate
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_waypoint_indicator_compute_for_camera (LrgWaypointIndicator *out,
                                           LrgCamera3D          *camera,
                                           gfloat                width,
                                           gfloat                height,
                                           gfloat                margin,
                                           const gfloat         *point);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgWaypointIndicator, lrg_waypoint_indicator_free)

G_END_DECLS
