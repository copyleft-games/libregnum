/* lrg-beacon.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBeacon - a guidance beacon drawn in 3D space: a tall translucent
 * column of light over a point with a bobbing marker near its foot,
 * the "follow the light" waypoint of open-world games.  The column
 * widens with distance so it stays visible across a whole map and
 * fades out as the viewer arrives.  lrg_beacon_compute() is pure (the
 * shape for a viewer); lrg_beacon_draw() draws a shape inside a 3D
 * camera pass.
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
 * LrgBeaconStyle:
 * @min_radius: the column's radius up close (world units)
 * @radius_per_unit: radius added per unit of distance, so the column
 *   keeps roughly the same width on the screen far away
 * @max_radius: the widest column
 * @height: the column's height above its foot
 * @fade_near: closer than this the beacon is not drawn
 * @fade_far: from here on the beacon has its full @alpha (between
 *   @fade_near and @fade_far it fades in)
 * @alpha: the column's opacity, 0 .. 1 (the marker is twice as
 *   opaque, at most 1)
 * @marker_lift: the marker's height above the foot
 * @marker_size: the marker's size up close (it grows with the column)
 * @bob_amplitude: how far the marker bobs up and down
 * @bob_period: seconds of one bob (0: no bob)
 *
 * How a beacon looks.  lrg_beacon_style_init() gives the defaults.
 */
typedef struct
{
    gfloat min_radius;
    gfloat radius_per_unit;
    gfloat max_radius;
    gfloat height;
    gfloat fade_near;
    gfloat fade_far;
    gfloat alpha;
    gfloat marker_lift;
    gfloat marker_size;
    gfloat bob_amplitude;
    gfloat bob_period;
} LrgBeaconStyle;

#define LRG_TYPE_BEACON_STYLE (lrg_beacon_style_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_beacon_style_get_type (void) G_GNUC_CONST;

/**
 * lrg_beacon_style_init:
 * @style: (out caller-allocates): the style to fill
 *
 * The default style: radius 0.15 up close plus 0.004 per unit, at most
 * 30; 80 units tall; hidden within 1.5 units, full from 6; opacity
 * 0.35; the marker 2.2 units up, 0.3 big, bobbing 0.12 every 2 seconds.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_beacon_style_init (LrgBeaconStyle *style);

/**
 * lrg_beacon_style_copy:
 * @style: an #LrgBeaconStyle
 *
 * Returns: (transfer full): a copy
 */
LRG_AVAILABLE_IN_ALL
LrgBeaconStyle *
lrg_beacon_style_copy (const LrgBeaconStyle *style);

/**
 * lrg_beacon_style_free:
 * @style: (nullable): a style from lrg_beacon_style_copy()
 */
LRG_AVAILABLE_IN_ALL
void
lrg_beacon_style_free (LrgBeaconStyle *style);

/**
 * LrgBeaconShape:
 * @foot: the column's foot (world)
 * @radius: the column's radius
 * @height: the column's height
 * @alpha: the column's opacity, 0 .. 1
 * @marker: the marker's centre
 * @marker_size: the marker's half height
 * @distance: the viewer's distance from the foot
 *
 * What lrg_beacon_compute() worked out for one viewer.
 */
typedef struct
{
    gfloat foot[3];
    gfloat radius;
    gfloat height;
    gfloat alpha;
    gfloat marker[3];
    gfloat marker_size;
    gfloat distance;
} LrgBeaconShape;

/**
 * lrg_beacon_compute:
 * @style: (nullable): the look, %NULL for the defaults
 * @eye: (array fixed-size=3): the viewer's position
 * @foot: (array fixed-size=3): the beacon's foot
 * @time: seconds of a running clock (the marker's bob)
 * @out: (out caller-allocates): the shape
 *
 * Returns: %FALSE when the beacon is not to be drawn (the viewer stands
 *   within @fade_near, or a position is not finite); @out is filled
 *   either way
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_beacon_compute (const LrgBeaconStyle *style,
                    const gfloat         *eye,
                    const gfloat         *foot,
                    gdouble               time,
                    LrgBeaconShape       *out);

/**
 * lrg_beacon_draw:
 * @shape: a shape from lrg_beacon_compute()
 * @r: red
 * @g: green
 * @b: blue
 *
 * Draws the column (alpha blended, without writing depth, so what
 * stands in front still hides it) and the marker (a double cone) in the
 * colour.  Call it inside a 3D camera pass after the opaque world.
 * Needs a graphics context.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_beacon_draw (const LrgBeaconShape *shape,
                 guint8                r,
                 guint8                g,
                 guint8                b);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgBeaconStyle, lrg_beacon_style_free)

G_END_DECLS
