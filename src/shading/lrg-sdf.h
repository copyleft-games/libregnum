/* lrg-sdf.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Signed distance functions for CPU shading.
 *
 * Pure functions (no state, no allocation, safe from any thread) that
 * return the signed distance from a point to a 2D or 3D primitive:
 * negative inside, zero on the surface, positive outside.  Most follow
 * Inigo Quilez's reference formulations ("2D distance functions",
 * "distance functions", iquilezles.org).  On top of the primitives
 * there are boolean and smooth (polynomial smooth-min) operators, and
 * scene helpers that take a distance callback: a tetrahedral normal, a
 * sphere tracer, soft shadows, ambient occlusion, and the analytic
 * pixel coverage used for anti-aliased 2D fills
 * (lrg_image_canvas_fill_sdf()).
 *
 * Vectors are plain float arrays (p[0] = x, p[1] = y, p[2] = z).  2D
 * primitives are centred on the origin unless they take explicit
 * points; translate or rotate the query point before calling them.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Callbacks
 * ========================================================================== */

/**
 * LrgSdf2Func:
 * @p: (array fixed-size=2): the query point (x, y)
 * @user_data: (closure): data passed to the function that calls this one
 *
 * A 2D signed distance field: the signed distance from @p to the
 * shape, negative inside.
 *
 * Returns: the signed distance
 *
 * Since: 0.14
 */
typedef gfloat (*LrgSdf2Func) (const gfloat p[2],
                               gpointer     user_data);

/**
 * LrgSdf3Func:
 * @p: (array fixed-size=3): the query point (x, y, z)
 * @user_data: (closure): data passed to the function that calls this one
 *
 * A 3D signed distance field (a "scene"): the signed distance from @p
 * to the nearest surface, negative inside.  For the helpers that march
 * along rays it should be a true distance or a lower bound of it (a
 * Lipschitz constant of at most 1), or they may step through surfaces.
 *
 * Returns: the signed distance
 *
 * Since: 0.14
 */
typedef gfloat (*LrgSdf3Func) (const gfloat p[3],
                               gpointer     user_data);

/* ==========================================================================
 * 2D primitives
 * ========================================================================== */

/**
 * lrg_sdf2_circle:
 * @p: (array fixed-size=2): the query point
 * @r: the radius
 *
 * Exact distance to a circle of radius @r centred on the origin.
 *
 * Returns: the signed distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf2_circle (const gfloat p[2],
                 gfloat       r);

/**
 * lrg_sdf2_ellipse:
 * @p: (array fixed-size=2): the query point
 * @ab: (array fixed-size=2): the semi-axes along x and y
 *
 * Exact distance to an axis-aligned ellipse centred on the origin,
 * using Inigo Quilez's analytic solution (the closest point is the root
 * of a quartic, solved in closed form, in double precision).  Equal
 * semi-axes give the circle distance; a semi-axis of zero or less gives
 * the distance to the flat segment along the other axis.
 *
 * Returns: the signed distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf2_ellipse (const gfloat p[2],
                  const gfloat ab[2]);

/**
 * lrg_sdf2_box:
 * @p: (array fixed-size=2): the query point
 * @b: (array fixed-size=2): the half-extents along x and y
 *
 * Exact distance to an axis-aligned box centred on the origin.
 *
 * Returns: the signed distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf2_box (const gfloat p[2],
              const gfloat b[2]);

/**
 * lrg_sdf2_round_box:
 * @p: (array fixed-size=2): the query point
 * @b: (array fixed-size=2): the half-extents along x and y
 * @r: the corner radius, clamped into 0 .. min (@b)
 *
 * Exact distance to a box with rounded corners.  The outer size stays
 * @b; the corners are carved in, not added on.
 *
 * Returns: the signed distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf2_round_box (const gfloat p[2],
                    const gfloat b[2],
                    gfloat       r);

/**
 * lrg_sdf2_segment:
 * @p: (array fixed-size=2): the query point
 * @a: (array fixed-size=2): the first end point
 * @b: (array fixed-size=2): the second end point
 *
 * Exact (unsigned) distance to the line segment from @a to @b.  Equal
 * end points give the distance to that point.
 *
 * Returns: the distance, never negative
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf2_segment (const gfloat p[2],
                  const gfloat a[2],
                  const gfloat b[2]);

/**
 * lrg_sdf2_capsule:
 * @p: (array fixed-size=2): the query point
 * @a: (array fixed-size=2): the first end point
 * @b: (array fixed-size=2): the second end point
 * @r: the radius
 *
 * Exact distance to a 2D capsule (a stadium): the segment from @a to
 * @b thickened by @r.
 *
 * Returns: the signed distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf2_capsule (const gfloat p[2],
                  const gfloat a[2],
                  const gfloat b[2],
                  gfloat       r);

/**
 * lrg_sdf2_triangle:
 * @p: (array fixed-size=2): the query point
 * @a: (array fixed-size=2): the first corner
 * @b: (array fixed-size=2): the second corner
 * @c: (array fixed-size=2): the third corner
 *
 * Exact distance to a triangle, in either winding.  A degenerate
 * (zero-area) triangle gives the unsigned distance to its edges.
 *
 * Returns: the signed distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf2_triangle (const gfloat p[2],
                   const gfloat a[2],
                   const gfloat b[2],
                   const gfloat c[2]);

/* ==========================================================================
 * 3D primitives
 * ========================================================================== */

/**
 * lrg_sdf3_sphere:
 * @p: (array fixed-size=3): the query point
 * @r: the radius
 *
 * Exact distance to a sphere of radius @r centred on the origin.
 *
 * Returns: the signed distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf3_sphere (const gfloat p[3],
                 gfloat       r);

/**
 * lrg_sdf3_ellipsoid:
 * @p: (array fixed-size=3): the query point
 * @r: (array fixed-size=3): the semi-axes along x, y and z
 *
 * Distance *bound* to an axis-aligned ellipsoid centred on the origin
 * (Inigo Quilez's k0 (k0 - 1) / k1 estimate).  It is not the exact
 * distance: it is exact on the axes and on the surface (it is zero
 * exactly there and has the right sign everywhere), and it never
 * overestimates by much, which is what ray marching and smooth
 * blending need.  Semi-axes are clamped to a tiny positive value.
 *
 * Returns: the signed distance estimate
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf3_ellipsoid (const gfloat p[3],
                    const gfloat r[3]);

/**
 * lrg_sdf3_box:
 * @p: (array fixed-size=3): the query point
 * @b: (array fixed-size=3): the half-extents along x, y and z
 *
 * Exact distance to an axis-aligned box centred on the origin.
 *
 * Returns: the signed distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf3_box (const gfloat p[3],
              const gfloat b[3]);

/**
 * lrg_sdf3_round_box:
 * @p: (array fixed-size=3): the query point
 * @b: (array fixed-size=3): the half-extents along x, y and z
 * @r: the edge radius, clamped into 0 .. min (@b)
 *
 * Exact distance to a box with rounded edges and corners.  The outer
 * size stays @b.
 *
 * Returns: the signed distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf3_round_box (const gfloat p[3],
                    const gfloat b[3],
                    gfloat       r);

/**
 * lrg_sdf3_capsule:
 * @p: (array fixed-size=3): the query point
 * @a: (array fixed-size=3): the first end point
 * @b: (array fixed-size=3): the second end point
 * @r: the radius
 *
 * Exact distance to a capsule: the segment from @a to @b thickened by
 * @r.  Equal end points give a sphere.
 *
 * Returns: the signed distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf3_capsule (const gfloat p[3],
                  const gfloat a[3],
                  const gfloat b[3],
                  gfloat       r);

/**
 * lrg_sdf3_round_cone:
 * @p: (array fixed-size=3): the query point
 * @r1: the radius of the bottom sphere, centred on the origin
 * @r2: the radius of the top sphere, centred at (0, @h, 0)
 * @h: the distance between the two sphere centres along +y
 *
 * Exact distance to a round cone: the convex hull of two spheres on
 * the y axis (a tapered capsule).  When one sphere contains the other
 * (|@r1 - @r2| >= @h) the hull is the larger sphere and that distance
 * is returned.
 *
 * Returns: the signed distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf3_round_cone (const gfloat p[3],
                     gfloat       r1,
                     gfloat       r2,
                     gfloat       h);

/**
 * lrg_sdf3_torus:
 * @p: (array fixed-size=3): the query point
 * @t: (array fixed-size=2): the major radius (centre of the tube, in the
 *   xz plane) and the minor radius (the tube)
 *
 * Exact distance to a torus around the y axis.
 *
 * Returns: the signed distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf3_torus (const gfloat p[3],
                const gfloat t[2]);

/* ==========================================================================
 * Operators
 * ========================================================================== */

/**
 * lrg_sdf_union:
 * @d1: a distance
 * @d2: another distance
 *
 * Union of two shapes: min (@d1, @d2).  Exact outside, a bound inside.
 *
 * Returns: the combined distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf_union (gfloat d1,
               gfloat d2);

/**
 * lrg_sdf_subtract:
 * @d1: the distance to the shape to keep
 * @d2: the distance to the shape to cut away
 *
 * The first shape minus the second: max (@d1, -@d2).  A bound.
 *
 * Returns: the combined distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf_subtract (gfloat d1,
                  gfloat d2);

/**
 * lrg_sdf_intersect:
 * @d1: a distance
 * @d2: another distance
 *
 * Intersection of two shapes: max (@d1, @d2).  A bound.
 *
 * Returns: the combined distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf_intersect (gfloat d1,
                   gfloat d2);

/**
 * lrg_sdf_smooth_union:
 * @d1: a distance
 * @d2: another distance
 * @k: the blend radius (in distance units); 0 or less is a hard union
 *
 * Union with a smooth fillet, using the quadratic polynomial smooth
 * minimum: h = max (k - |d1 - d2|, 0) / k, result = min (d1, d2) -
 * h * h * k / 4.  Where the two distances differ by more than @k the
 * result is exactly min (@d1, @d2); the blend never removes material
 * (it is always <= the hard union) and adds at most @k / 4.
 *
 * Returns: the blended distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf_smooth_union (gfloat d1,
                      gfloat d2,
                      gfloat k);

/**
 * lrg_sdf_smooth_subtract:
 * @d1: the distance to the shape to keep
 * @d2: the distance to the shape to cut away
 * @k: the blend radius; 0 or less is a hard subtraction
 *
 * The first shape minus the second with a rounded seam:
 * -lrg_sdf_smooth_union (-@d1, @d2, @k).
 *
 * Returns: the blended distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf_smooth_subtract (gfloat d1,
                         gfloat d2,
                         gfloat k);

/**
 * lrg_sdf_smooth_intersect:
 * @d1: a distance
 * @d2: another distance
 * @k: the blend radius; 0 or less is a hard intersection
 *
 * Intersection with a rounded seam:
 * -lrg_sdf_smooth_union (-@d1, -@d2, @k).
 *
 * Returns: the blended distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf_smooth_intersect (gfloat d1,
                          gfloat d2,
                          gfloat k);

/**
 * lrg_sdf_round:
 * @d: a distance
 * @r: the rounding radius
 *
 * Grows a shape by @r, rounding its convex corners: @d - @r.
 *
 * Returns: the rounded distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf_round (gfloat d,
               gfloat r);

/**
 * lrg_sdf_onion:
 * @d: a distance
 * @t: the half-thickness of the shell
 *
 * Turns a solid into a shell of thickness 2 @t around its surface:
 * |@d| - @t.  Nest it for concentric rings.
 *
 * Returns: the shell distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf_onion (gfloat d,
               gfloat t);

/* ==========================================================================
 * Scene helpers
 * ========================================================================== */

/**
 * lrg_sdf2_coverage:
 * @d: the signed distance at a pixel centre
 * @pixel_width: the size of one pixel in the units of @d
 *
 * Analytic anti-aliasing: the fraction of a pixel covered by the shape,
 * clamp (0.5 - @d / @pixel_width, 0, 1), i.e. a one-pixel linear ramp
 * centred on the edge.  A @pixel_width of 0 or less gives a hard step
 * (1 when @d <= 0); a NaN @d gives 0.
 *
 * Returns: the coverage, 0 .. 1
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf2_coverage (gfloat d,
                   gfloat pixel_width);

/**
 * lrg_sdf3_normal:
 * @func: (scope call) (closure user_data): the scene
 * @user_data: (nullable): data for @func
 * @p: (array fixed-size=3): the point (normally on the surface)
 * @eps: the sampling offset; 0 or less uses 1e-3
 * @out: (out caller-allocates) (array fixed-size=3): the unit normal
 *
 * Estimates the surface normal (the normalised gradient of @func) with
 * Inigo Quilez's tetrahedral difference: four samples at @p plus @eps
 * times the corners (1,-1,-1), (-1,-1,1), (-1,1,-1) and (1,1,1).  When
 * the gradient vanishes or is not finite, @out is set to (0, 0, 0).
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sdf3_normal (LrgSdf3Func  func,
                 gpointer     user_data,
                 const gfloat p[3],
                 gfloat       eps,
                 gfloat       out[3]);

/**
 * lrg_sdf3_raymarch:
 * @func: (scope call) (closure user_data): the scene
 * @user_data: (nullable): data for @func
 * @ro: (array fixed-size=3): the ray origin
 * @rd: (array fixed-size=3): the ray direction (normalised internally)
 * @tmax: the largest distance along the ray to search
 * @max_steps: the step budget (clamped into 1 .. 100000)
 * @out_t: (out) (optional): the hit distance along the ray, or the
 *   distance reached when nothing was hit
 *
 * Sphere-traces the ray ro + t rd from t = 0: each step advances by
 * the distance @func reports.  A hit is a distance below
 * 1e-4 x max (1, t).  A ray that starts inside a shape hits at t = 0.
 *
 * Returns: %TRUE when a surface was hit within @tmax and @max_steps;
 *   %FALSE otherwise, or for a zero-length or non-finite direction or a
 *   non-finite distance
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_sdf3_raymarch (LrgSdf3Func  func,
                   gpointer     user_data,
                   const gfloat ro[3],
                   const gfloat rd[3],
                   gfloat       tmax,
                   gint         max_steps,
                   gfloat      *out_t);

/**
 * lrg_sdf3_soft_shadow:
 * @func: (scope call) (closure user_data): the scene
 * @user_data: (nullable): data for @func
 * @ro: (array fixed-size=3): the shaded point (offset it slightly from
 *   the surface, or use @tmin)
 * @rd: (array fixed-size=3): the direction towards the light
 *   (normalised internally)
 * @tmin: where to start marching (skips the surface itself)
 * @tmax: where to stop (the light's distance)
 * @k: the hardness: larger is sharper (8 is soft, 32 is crisp)
 *
 * Penumbra estimate along a shadow ray (Inigo Quilez's improved soft
 * shadow, which triangulates the nearest occluder from consecutive
 * samples to avoid banding).  At most 256 steps.
 *
 * Returns: the light visibility, 1 (fully lit) .. 0 (fully shadowed);
 *   1 for a zero-length direction
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf3_soft_shadow (LrgSdf3Func  func,
                      gpointer     user_data,
                      const gfloat ro[3],
                      const gfloat rd[3],
                      gfloat       tmin,
                      gfloat       tmax,
                      gfloat       k);

/**
 * lrg_sdf3_ambient_occlusion:
 * @func: (scope call) (closure user_data): the scene
 * @user_data: (nullable): data for @func
 * @p: (array fixed-size=3): the surface point
 * @n: (array fixed-size=3): the unit surface normal at @p
 *
 * Distance-field ambient occlusion: five samples along the normal, at
 * 0.01 .. 0.13 units, each comparing the distance actually found with
 * the distance expected in open space.  The sample spacing is in scene
 * units and suits objects about one unit across; scale the scene (or
 * the query) for very different sizes.
 *
 * Returns: the ambient visibility, 1 (open) .. 0 (fully occluded)
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sdf3_ambient_occlusion (LrgSdf3Func  func,
                            gpointer     user_data,
                            const gfloat p[3],
                            const gfloat n[3]);

G_END_DECLS
