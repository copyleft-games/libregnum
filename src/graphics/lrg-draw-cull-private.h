/* lrg-draw-cull-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * CPU-side visibility helpers for 2D drawing. Not installed, not
 * introspected.
 *
 * lrg_draw_cull_visible_rect_2d() answers "which part of the z = 0 plane
 * lands inside the current viewport?" by reading rlgl's live matrices
 * (projection x modelview x transform), so it accounts for whatever is in
 * effect at the call site: BeginMode2D cameras (target, offset, zoom,
 * rotation), render textures, letterbox viewports and rlPushMatrix
 * transforms, without the caller passing any of it in. Callers use the
 * rectangle to skip geometry that cannot be seen (tilemaps, big sprite
 * sets) instead of handing it all to the batcher.
 *
 * rlgl's accumulated transform is only applied while a matrix is pushed,
 * and it is restored to the value it had before the first push when the
 * stack empties again -- identity, since nothing else writes it -- so
 * multiplying it in unconditionally is always correct.
 */

#pragma once

#include <glib.h>
#include <math.h>
#include <raylib.h>
#include <rlgl.h>
#include <graylib.h>

G_BEGIN_DECLS

/*
 * lrg_draw_cull_mul:
 *
 * Column-major 4x4 product a * b, converted field by field (raylib's
 * Matrix fields are named by storage index but declared in row order, so
 * it must never be treated as a float[16]).
 */
static inline Matrix
lrg_draw_cull_mul (Matrix a,
                   Matrix b)
{
    Matrix r;

    r.m0  = a.m0 * b.m0  + a.m4 * b.m1  + a.m8  * b.m2  + a.m12 * b.m3;
    r.m1  = a.m1 * b.m0  + a.m5 * b.m1  + a.m9  * b.m2  + a.m13 * b.m3;
    r.m2  = a.m2 * b.m0  + a.m6 * b.m1  + a.m10 * b.m2  + a.m14 * b.m3;
    r.m3  = a.m3 * b.m0  + a.m7 * b.m1  + a.m11 * b.m2  + a.m15 * b.m3;
    r.m4  = a.m0 * b.m4  + a.m4 * b.m5  + a.m8  * b.m6  + a.m12 * b.m7;
    r.m5  = a.m1 * b.m4  + a.m5 * b.m5  + a.m9  * b.m6  + a.m13 * b.m7;
    r.m6  = a.m2 * b.m4  + a.m6 * b.m5  + a.m10 * b.m6  + a.m14 * b.m7;
    r.m7  = a.m3 * b.m4  + a.m7 * b.m5  + a.m11 * b.m6  + a.m15 * b.m7;
    r.m8  = a.m0 * b.m8  + a.m4 * b.m9  + a.m8  * b.m10 + a.m12 * b.m11;
    r.m9  = a.m1 * b.m8  + a.m5 * b.m9  + a.m9  * b.m10 + a.m13 * b.m11;
    r.m10 = a.m2 * b.m8  + a.m6 * b.m9  + a.m10 * b.m10 + a.m14 * b.m11;
    r.m11 = a.m3 * b.m8  + a.m7 * b.m9  + a.m11 * b.m10 + a.m15 * b.m11;
    r.m12 = a.m0 * b.m12 + a.m4 * b.m13 + a.m8  * b.m14 + a.m12 * b.m15;
    r.m13 = a.m1 * b.m12 + a.m5 * b.m13 + a.m9  * b.m14 + a.m13 * b.m15;
    r.m14 = a.m2 * b.m12 + a.m6 * b.m13 + a.m10 * b.m14 + a.m14 * b.m15;
    r.m15 = a.m3 * b.m12 + a.m7 * b.m13 + a.m11 * b.m14 + a.m15 * b.m15;

    return r;
}

/*
 * lrg_draw_cull_visible_rect_2d:
 * @out: (out caller-allocates): the visible area, in the coordinates the
 *   caller is about to draw in
 *
 * Computes the axis-aligned bounds of the viewport unprojected onto the
 * z = 0 plane.
 *
 * Returns: %TRUE when @out is valid; %FALSE when the current projection
 *   is not an affine 2D one (a perspective 3D mode, a degenerate matrix),
 *   in which case the caller must not cull.
 */
static inline gboolean
lrg_draw_cull_visible_rect_2d (GrlRectangle *out)
{
    Matrix  m;
    gfloat  a, b, c, d, e, f;
    gfloat  det;
    gfloat  min_x, min_y, max_x, max_y;
    guint   i;
    static const gfloat corners[4][2] = {
        { -1.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f }
    };

    /* clip = P * MV * T * v */
    m = lrg_draw_cull_mul (rlGetMatrixProjection (),
                           lrg_draw_cull_mul (rlGetMatrixModelview (),
                                              rlGetMatrixTransform ()));

    /* Only an affine map of the plane can be inverted this way: the w row
     * must be (0, 0, *, 1) for points on z = 0. */
    if (fabsf (m.m3) > 1e-6f || fabsf (m.m7) > 1e-6f ||
        fabsf (m.m15 - 1.0f) > 1e-6f)
        return FALSE;

    /* For z = 0:  ndc.x = a x + b y + c,  ndc.y = d x + e y + f */
    a = m.m0;  b = m.m4;  c = m.m12;
    d = m.m1;  e = m.m5;  f = m.m13;

    det = a * e - b * d;
    if (fabsf (det) < 1e-12f || !isfinite (det))
        return FALSE;

    /* Unproject the four NDC corners and take their bounds (the view may
     * be rotated, so all four are needed). */
    min_x = min_y = G_MAXFLOAT;
    max_x = max_y = -G_MAXFLOAT;
    for (i = 0; i < 4; i++)
    {
        gfloat nx = corners[i][0] - c;
        gfloat ny = corners[i][1] - f;
        gfloat x = ( e * nx - b * ny) / det;
        gfloat y = (-d * nx + a * ny) / det;

        min_x = MIN (min_x, x);
        min_y = MIN (min_y, y);
        max_x = MAX (max_x, x);
        max_y = MAX (max_y, y);
    }

    out->x = min_x;
    out->y = min_y;
    out->width = max_x - min_x;
    out->height = max_y - min_y;

    return isfinite (out->width) && isfinite (out->height);
}

G_END_DECLS
