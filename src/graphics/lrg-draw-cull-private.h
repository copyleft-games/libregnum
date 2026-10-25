/* lrg-draw-cull-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * CPU-side visibility helpers for 2D and 3D drawing. Not installed, not
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
 *
 * The 3D helpers (LrgDrawFrustum) extract the six clip planes of the same
 * P x MV x T product (Gribb & Hartmann) and test axis-aligned boxes
 * against them. Anything a test rejects lies wholly outside the clip
 * volume, so the GPU would clip every one of its triangles anyway: culling
 * with them never changes a pixel, as long as the vertex stage writes
 * gl_Position = mvp * position for the box's points (callers widen their
 * boxes by whatever their vertex shaders add, such as waves or wind) and
 * no depth clamp is enabled. Shadow passes set their light's matrices
 * through rlgl, so the same call culls cascades and cube faces.
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

/*
 * LrgDrawFrustum:
 * @planes: six planes (a, b, c, d), normals pointing inwards and
 *   normalised: a point p is inside a plane when a p.x + b p.y + c p.z + d
 *   is not negative
 * @n_planes: planes in use (a degenerate plane, such as an infinite far
 *   plane, is dropped)
 */
typedef struct
{
    gfloat planes[6][4];
    guint  n_planes;
} LrgDrawFrustum;

/*
 * lrg_draw_cull_frustum_from_matrix:
 * @out: (out caller-allocates): the frustum
 * @clip: a column-major clip matrix (object space to clip space)
 *
 * Extracts the planes of -w <= x, y, z <= w in the object space @clip
 * maps from.
 *
 * Returns: %FALSE when @clip is not finite, in which case the caller
 *   must not cull
 */
static inline gboolean
lrg_draw_cull_frustum_from_matrix (LrgDrawFrustum *out,
                                   Matrix          clip)
{
    /* the rows of the clip matrix, by element number (row i is
     * m[i], m[i + 4], m[i + 8], m[i + 12]) */
    gfloat r0[4];
    gfloat r1[4];
    gfloat r2[4];
    gfloat r3[4];
    guint  i;
    guint  k;

    r0[0] = clip.m0;  r0[1] = clip.m4;  r0[2] = clip.m8;  r0[3] = clip.m12;
    r1[0] = clip.m1;  r1[1] = clip.m5;  r1[2] = clip.m9;  r1[3] = clip.m13;
    r2[0] = clip.m2;  r2[1] = clip.m6;  r2[2] = clip.m10; r2[3] = clip.m14;
    r3[0] = clip.m3;  r3[1] = clip.m7;  r3[2] = clip.m11; r3[3] = clip.m15;

    out->n_planes = 0;
    for (i = 0; i < 6; i++)
    {
        const gfloat *r = i < 2 ? r0 : i < 4 ? r1 : r2;
        gfloat        sign = (i & 1) == 0 ? 1.0f : -1.0f;
        gfloat        p[4];
        gfloat        len;

        /* left/bottom/near: w + x/y/z >= 0; right/top/far: w - ... >= 0 */
        for (k = 0; k < 4; k++)
            p[k] = r3[k] + sign * r[k];
        for (k = 0; k < 4; k++)
            if (!isfinite (p[k]))
                return FALSE;
        len = sqrtf (p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (!(len > 1e-20f))
            continue;
        for (k = 0; k < 4; k++)
            out->planes[out->n_planes][k] = p[k] / len;
        out->n_planes++;
    }
    return TRUE;
}

/*
 * lrg_draw_cull_frustum_current:
 * @out: (out caller-allocates): the frustum, in the coordinates the caller
 *   is about to draw in (before rlgl's push/pop transform)
 *
 * Extracts the frustum of rlgl's live matrices (P x MV x T).
 *
 * Returns: %FALSE when culling is not possible (stereo rendering draws
 *   each eye with its own projection, a non-finite matrix)
 */
static inline gboolean
lrg_draw_cull_frustum_current (LrgDrawFrustum *out)
{
    if (rlIsStereoRenderEnabled ())
        return FALSE;
    return lrg_draw_cull_frustum_from_matrix (out,
        lrg_draw_cull_mul (rlGetMatrixProjection (),
                           lrg_draw_cull_mul (rlGetMatrixModelview (),
                                              rlGetMatrixTransform ())));
}

/*
 * lrg_draw_cull_frustum_box_visible:
 * @frustum: a frustum
 * @min: (array fixed-size=3): the box's lower corner
 * @max: (array fixed-size=3): the box's upper corner
 *
 * Conservative box test: a box is rejected only when it lies wholly on
 * the outer side of one plane, by more than a small tolerance (relative
 * to the box's size and distance, so float rounding here can never cull
 * what the GPU would draw).
 *
 * Returns: %FALSE when the box is certainly outside
 */
static inline gboolean
lrg_draw_cull_frustum_box_visible (const LrgDrawFrustum *frustum,
                                   const gfloat         *min,
                                   const gfloat         *max)
{
    guint i;

    for (i = 0; i < frustum->n_planes; i++)
    {
        const gfloat *p = frustum->planes[i];
        gfloat        x = p[0] >= 0.0f ? max[0] : min[0];
        gfloat        y = p[1] >= 0.0f ? max[1] : min[1];
        gfloat        z = p[2] >= 0.0f ? max[2] : min[2];
        gfloat        d = p[0] * x + p[1] * y + p[2] * z + p[3];
        gfloat        scale = fabsf (p[0] * x) + fabsf (p[1] * y) + fabsf (p[2] * z)
                              + fabsf (p[3]);

        /* the nearest corner is behind the plane: so is the whole box */
        if (d < -(1e-4f + 1e-5f * scale))
            return FALSE;
    }
    return TRUE;
}

/*
 * lrg_draw_cull_frustum_box_visible_offset:
 * @frustum: a frustum
 * @min: (array fixed-size=3): the box's lower corner, before @offset
 * @max: (array fixed-size=3): the box's upper corner, before @offset
 * @offset: (array fixed-size=3): a translation applied to the box (a
 *   chunk-local box drawn with a translation as its model matrix)
 *
 * lrg_draw_cull_frustum_box_visible() of the translated box.
 *
 * Returns: %FALSE when the box is certainly outside
 */
static inline gboolean
lrg_draw_cull_frustum_box_visible_offset (const LrgDrawFrustum *frustum,
                                          const gfloat         *min,
                                          const gfloat         *max,
                                          const gfloat         *offset)
{
    gfloat lo[3];
    gfloat hi[3];
    guint  k;

    for (k = 0; k < 3; k++)
    {
        lo[k] = min[k] + offset[k];
        hi[k] = max[k] + offset[k];
    }
    return lrg_draw_cull_frustum_box_visible (frustum, lo, hi);
}

G_END_DECLS
