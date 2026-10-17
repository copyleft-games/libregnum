/* lrg-waypoint.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWaypoint - a point in the world the player is guided to (a quest
 * target, the door that leads towards it, a map pin), with a label, a
 * colour and an arrival radius, and the arithmetic every guidance
 * display needs: distance, heading, arrival and a readable distance.
 * It knows nothing about drawing; see lrg-waypoint-indicator.h and
 * lrg-beacon.h for the screen arrow and the 3D beacon.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LRG_WAYPOINT_DEFAULT_RADIUS:
 *
 * The arrival radius of a new waypoint, in world units (metres).
 */
#define LRG_WAYPOINT_DEFAULT_RADIUS (1.5f)

/**
 * LrgWaypoint:
 *
 * A reference-counted, mutable waypoint.  Positions are world units
 * with Y up; headings are degrees in the XZ plane with 0 looking along
 * +Z and 90 along +X (clockwise seen from above), the convention of a
 * yaw that turns a +Z forward vector.
 */

#define LRG_TYPE_WAYPOINT (lrg_waypoint_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_waypoint_get_type (void) G_GNUC_CONST;

/**
 * lrg_waypoint_new:
 * @id: (nullable): an identifier for the caller ("quest:12", "pin:3")
 *
 * Creates a waypoint at the origin with no label, white, with
 * %LRG_WAYPOINT_DEFAULT_RADIUS.
 *
 * Returns: (transfer full): a new #LrgWaypoint
 */
LRG_AVAILABLE_IN_ALL
LrgWaypoint *
lrg_waypoint_new (const gchar *id);

/**
 * lrg_waypoint_ref:
 * @self: an #LrgWaypoint
 *
 * Returns: (transfer full): @self
 */
LRG_AVAILABLE_IN_ALL
LrgWaypoint *
lrg_waypoint_ref (LrgWaypoint *self);

/**
 * lrg_waypoint_unref:
 * @self: (transfer full): an #LrgWaypoint
 *
 * Drops a reference; the last one frees the waypoint.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_waypoint_unref (LrgWaypoint *self);

/**
 * lrg_waypoint_copy:
 * @self: an #LrgWaypoint
 *
 * Returns: (transfer full): an independent copy
 */
LRG_AVAILABLE_IN_ALL
LrgWaypoint *
lrg_waypoint_copy (LrgWaypoint *self);

/**
 * lrg_waypoint_get_id:
 * @self: an #LrgWaypoint
 *
 * Returns: (transfer none) (nullable): the identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_waypoint_get_id (LrgWaypoint *self);

/**
 * lrg_waypoint_set_label:
 * @self: an #LrgWaypoint
 * @label: (nullable): what the waypoint is ("The Dirty Pit")
 */
LRG_AVAILABLE_IN_ALL
void
lrg_waypoint_set_label (LrgWaypoint *self,
                        const gchar *label);

/**
 * lrg_waypoint_get_label:
 * @self: an #LrgWaypoint
 *
 * Returns: (transfer none) (nullable): the label
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_waypoint_get_label (LrgWaypoint *self);

/**
 * lrg_waypoint_set_position:
 * @self: an #LrgWaypoint
 * @x: world X
 * @y: world Y (the foot of the waypoint)
 * @z: world Z
 */
LRG_AVAILABLE_IN_ALL
void
lrg_waypoint_set_position (LrgWaypoint *self,
                           gfloat       x,
                           gfloat       y,
                           gfloat       z);

/**
 * lrg_waypoint_get_position:
 * @self: an #LrgWaypoint
 * @out: (out caller-allocates) (array fixed-size=3): the position
 */
LRG_AVAILABLE_IN_ALL
void
lrg_waypoint_get_position (LrgWaypoint *self,
                           gfloat      *out);

/**
 * lrg_waypoint_set_color:
 * @self: an #LrgWaypoint
 * @r: red
 * @g: green
 * @b: blue
 * @a: alpha
 */
LRG_AVAILABLE_IN_ALL
void
lrg_waypoint_set_color (LrgWaypoint *self,
                        guint8       r,
                        guint8       g,
                        guint8       b,
                        guint8       a);

/**
 * lrg_waypoint_get_color:
 * @self: an #LrgWaypoint
 * @r: (out) (optional): red
 * @g: (out) (optional): green
 * @b: (out) (optional): blue
 * @a: (out) (optional): alpha
 */
LRG_AVAILABLE_IN_ALL
void
lrg_waypoint_get_color (LrgWaypoint *self,
                        guint8      *r,
                        guint8      *g,
                        guint8      *b,
                        guint8      *a);

/**
 * lrg_waypoint_set_radius:
 * @self: an #LrgWaypoint
 * @radius: the arrival radius in world units (negative becomes 0)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_waypoint_set_radius (LrgWaypoint *self,
                         gfloat       radius);

/**
 * lrg_waypoint_get_radius:
 * @self: an #LrgWaypoint
 *
 * Returns: the arrival radius
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_waypoint_get_radius (LrgWaypoint *self);

/**
 * lrg_waypoint_distance_from:
 * @self: an #LrgWaypoint
 * @from: (array fixed-size=3): a world position
 *
 * Returns: the straight-line distance from @from to the waypoint
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_waypoint_distance_from (LrgWaypoint  *self,
                            const gfloat *from);

/**
 * lrg_waypoint_heading_from:
 * @self: an #LrgWaypoint
 * @from: (array fixed-size=3): a world position
 *
 * The compass heading from @from to the waypoint in the XZ plane:
 * degrees in [0, 360), 0 along +Z, 90 along +X.  0 when the two
 * coincide in the plane.
 *
 * Returns: the heading
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_waypoint_heading_from (LrgWaypoint  *self,
                           const gfloat *from);

/**
 * lrg_waypoint_is_reached:
 * @self: an #LrgWaypoint
 * @from: (array fixed-size=3): a world position
 *
 * Returns: %TRUE when @from lies within the arrival radius in the XZ
 *   plane and within four radii (at least 2 units) vertically
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_waypoint_is_reached (LrgWaypoint  *self,
                         const gfloat *from);

/**
 * lrg_waypoint_relative_heading:
 * @heading: a compass heading, degrees
 * @facing: the heading the viewer faces, degrees
 *
 * Returns: @heading seen from @facing, wrapped to (-180, 180]: negative
 *   to the left, positive to the right
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_waypoint_relative_heading (gfloat heading,
                               gfloat facing);

/**
 * lrg_waypoint_format_distance:
 * @distance: a distance in world units (metres)
 *
 * A short readable distance: whole metres below 1000 ("7 m", "312 m"),
 * then kilometres with one decimal below 10 km ("1.4 km") and whole
 * kilometres beyond ("27 km").  Negative and non-finite distances are
 * "0 m".
 *
 * Returns: (transfer full): the text
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_waypoint_format_distance (gfloat distance);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgWaypoint, lrg_waypoint_unref)

G_END_DECLS
