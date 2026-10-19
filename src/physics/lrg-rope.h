/* lrg-rope.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRope - a rope or chain as a verlet point chain: two anchored ends
 * (a wagon's singletree and a horse's collar, a drawbridge chain), gravity,
 * a fixed length kept by iterated distance constraints, and an optional
 * floor so slack lies on the ground instead of under it.  Headless: the
 * game steps it with its frame time and draws the points (thin
 * cylinders, a line strip); nothing here touches the graphics context.
 *
 * The points are in the caller's space (draw space or world, as long as
 * the anchors and the floor agree).  A rope whose anchors are further
 * apart than its length is stretched taut between them (the constraint
 * cannot be met; the rope pulls straight), which is how a wagon's
 * harness looks when the horse leads.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_ROPE (lrg_rope_get_type ())

/**
 * LRG_ROPE_MAX_SEGMENTS:
 *
 * The most segments of one rope.
 */
#define LRG_ROPE_MAX_SEGMENTS (256)

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRope, lrg_rope, LRG, ROPE, GObject)

/**
 * lrg_rope_new:
 * @segments: how many segments the rope has (2 .. %LRG_ROPE_MAX_SEGMENTS;
 *   it has one more point)
 * @length: the rope's length, > 0
 *
 * A rope hanging straight down from the origin until its anchors are set.
 *
 * Returns: (transfer full): a new #LrgRope
 */
LRG_AVAILABLE_IN_ALL
LrgRope *lrg_rope_new (guint  segments,
                       gfloat length);

/**
 * lrg_rope_set_anchors:
 * @self: an #LrgRope
 * @start: (array fixed-size=3): where the first point is held
 * @end: (array fixed-size=3): where the last point is held
 *
 * Moves the two ends (they are held exactly at these points on every
 * step).  The first call after creation or lrg_rope_reset() lays the rope
 * out along the straight line between them, sagged by its slack, so it
 * does not start as a tangle.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rope_set_anchors (LrgRope      *self,
                           const gfloat *start,
                           const gfloat *end);

/**
 * lrg_rope_set_gravity:
 * @self: an #LrgRope
 * @gravity: acceleration along Y in units per second squared (negative
 *   is down; default -9.8)
 */
LRG_AVAILABLE_IN_ALL
void lrg_rope_set_gravity (LrgRope *self,
                           gfloat   gravity);

/**
 * lrg_rope_set_damping:
 * @self: an #LrgRope
 * @damping: the share of velocity kept each second, 0 .. 1 (default 0.2:
 *   a rope settles quickly)
 */
LRG_AVAILABLE_IN_ALL
void lrg_rope_set_damping (LrgRope *self,
                           gfloat   damping);

/**
 * lrg_rope_set_iterations:
 * @self: an #LrgRope
 * @iterations: constraint passes per step, 1 .. 64 (default 12); more
 *   makes a taut rope stiffer
 */
LRG_AVAILABLE_IN_ALL
void lrg_rope_set_iterations (LrgRope *self,
                              guint    iterations);

/**
 * lrg_rope_set_floor:
 * @self: an #LrgRope
 * @enabled: whether the rope rests on a floor
 * @height: the floor's Y (a rope point never goes below it)
 */
LRG_AVAILABLE_IN_ALL
void lrg_rope_set_floor (LrgRope *self,
                         gboolean enabled,
                         gfloat   height);

/**
 * lrg_rope_step:
 * @self: an #LrgRope
 * @delta: seconds (clamped to 1/20; longer steps are taken in pieces)
 *
 * Verlet integration with gravity and damping, the floor, then the
 * distance constraints with the two ends re-pinned after each pass.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rope_step (LrgRope *self,
                    gfloat   delta);

/**
 * lrg_rope_reset:
 * @self: an #LrgRope
 *
 * Forgets the rope's shape and velocity; the next lrg_rope_set_anchors()
 * lays it out afresh.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rope_reset (LrgRope *self);

/**
 * lrg_rope_get_point_count:
 * @self: an #LrgRope
 *
 * Returns: segments + 1
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rope_get_point_count (LrgRope *self);

/**
 * lrg_rope_get_length:
 * @self: an #LrgRope
 *
 * Returns: the rope's length
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_rope_get_length (LrgRope *self);

/**
 * lrg_rope_get_points:
 * @self: an #LrgRope
 * @out_count: (out) (optional): the number of points
 *
 * Returns: (transfer none) (array length=out_count): 3 floats a point,
 *   the first at the start anchor; valid until the next step
 */
LRG_AVAILABLE_IN_ALL
const gfloat *lrg_rope_get_points (LrgRope *self,
                                   guint   *out_count);

/**
 * lrg_rope_get_tension:
 * @self: an #LrgRope
 *
 * Returns: how taut the rope is, 0 (the anchors are close, it hangs
 *   slack) to 1 (the anchors are a rope's length apart or more)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_rope_get_tension (LrgRope *self);

G_END_DECLS
