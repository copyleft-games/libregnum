/* lrg-waypoint-indicator.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The projection of a world point for raylib's perspective camera and
 * the edge clamp of off-screen points.  The camera basis is raylib's
 * (GetCameraForward / GetCameraRight / the true up), so a point the
 * game draws at the screen centre is reported there.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "guidance/lrg-waypoint-indicator.h"

#include <math.h>
#include <string.h>

LrgWaypointIndicator *
lrg_waypoint_indicator_copy (const LrgWaypointIndicator *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return (LrgWaypointIndicator *) g_memdup2 (self, sizeof (LrgWaypointIndicator));
}

void
lrg_waypoint_indicator_free (LrgWaypointIndicator *self)
{
    g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgWaypointIndicator, lrg_waypoint_indicator,
                     lrg_waypoint_indicator_copy, lrg_waypoint_indicator_free)

/* --- small vector helpers (double precision) --- */

static gdouble
dot3 (const gdouble *a,
      const gdouble *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void
cross3 (const gdouble *a,
        const gdouble *b,
        gdouble       *out)
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/* normalises @v in place; FALSE when it has no length */
static gboolean
normalize3 (gdouble *v)
{
    gdouble length;

    length = sqrt (dot3 (v, v));
    if (length < 1e-9 || !isfinite (length))
        return FALSE;
    v[0] /= length;
    v[1] /= length;
    v[2] /= length;
    return TRUE;
}

/*
 * clamp_to_edge:
 * Puts the indicator on the rectangle inset by @margin, from the
 * screen's centre along (@dx, @dy) (screen orientation: +y down).
 */
static void
clamp_to_edge (LrgWaypointIndicator *out,
               gdouble               dx,
               gdouble               dy,
               gdouble               width,
               gdouble               height,
               gdouble               margin)
{
    gdouble half_w;
    gdouble half_h;
    gdouble t;

    half_w = MAX (0.0, width * 0.5 - margin);
    half_h = MAX (0.0, height * 0.5 - margin);
    if (fabs (dx) < 1e-12 && fabs (dy) < 1e-12)
        dy = 1.0;
    /* the first edge the ray from the centre meets */
    t = G_MAXDOUBLE;
    if (fabs (dx) > 1e-12)
        t = MIN (t, half_w / fabs (dx));
    if (fabs (dy) > 1e-12)
        t = MIN (t, half_h / fabs (dy));
    out->x = (gfloat) (width * 0.5 + dx * t);
    out->y = (gfloat) (height * 0.5 + dy * t);
    out->angle = (gfloat) atan2 (dy, dx);
}

gboolean
lrg_waypoint_indicator_compute (LrgWaypointIndicator *out,
                                const gfloat         *eye,
                                const gfloat         *target,
                                const gfloat         *up,
                                gfloat                fovy,
                                gfloat                width,
                                gfloat                height,
                                gfloat                margin,
                                const gfloat         *point)
{
    gdouble forward[3];
    gdouble right[3];
    gdouble true_up[3];
    gdouble world_up[3];
    gdouble v[3];
    gdouble x_view;
    gdouble y_view;
    gdouble z_view;
    gdouble tan_half;
    gdouble aspect;
    gdouble sx;
    gdouble sy;
    gdouble m;

    g_return_val_if_fail (out != NULL, FALSE);
    g_return_val_if_fail (eye != NULL && target != NULL && up != NULL && point != NULL, FALSE);

    memset (out, 0, sizeof (*out));
    if (!(width > 0.0f) || !(height > 0.0f) || !(fovy > 0.0f) || fovy >= 180.0f)
        return FALSE;

    /* --- the camera basis (raylib: right = forward x up) --- */
    forward[0] = (gdouble) target[0] - (gdouble) eye[0];
    forward[1] = (gdouble) target[1] - (gdouble) eye[1];
    forward[2] = (gdouble) target[2] - (gdouble) eye[2];
    world_up[0] = up[0];
    world_up[1] = up[1];
    world_up[2] = up[2];
    if (!normalize3 (forward))
        return FALSE;
    cross3 (forward, world_up, right);
    if (!normalize3 (right))
        return FALSE;
    cross3 (right, forward, true_up);

    /* --- the point in view space --- */
    v[0] = (gdouble) point[0] - (gdouble) eye[0];
    v[1] = (gdouble) point[1] - (gdouble) eye[1];
    v[2] = (gdouble) point[2] - (gdouble) eye[2];
    x_view = dot3 (v, right);
    y_view = dot3 (v, true_up);
    z_view = dot3 (v, forward);
    out->depth = (gfloat) z_view;
    out->behind = z_view < (gdouble) LRG_WAYPOINT_INDICATOR_NEAR;

    m = MAX (0.0, (gdouble) margin);
    tan_half = tan ((gdouble) fovy * 0.5 * G_PI / 180.0);
    aspect = (gdouble) width / (gdouble) height;

    /* --- in front: the perspective projection --- */
    if (!out->behind)
    {
        gdouble ndc_x;
        gdouble ndc_y;

        ndc_x = x_view / (z_view * tan_half * aspect);
        ndc_y = y_view / (z_view * tan_half);
        sx = (ndc_x + 1.0) * 0.5 * (gdouble) width;
        sy = (1.0 - ndc_y) * 0.5 * (gdouble) height;
        if (sx >= m && sx <= (gdouble) width - m && sy >= m && sy <= (gdouble) height - m)
        {
            out->x = (gfloat) sx;
            out->y = (gfloat) sy;
            out->on_screen = TRUE;
            out->angle = (gfloat) atan2 (sy - (gdouble) height * 0.5, sx - (gdouble) width * 0.5);
            return TRUE;
        }
        clamp_to_edge (out, sx - (gdouble) width * 0.5, sy - (gdouble) height * 0.5,
                       (gdouble) width, (gdouble) height, m);
        return TRUE;
    }

    /*
     * --- behind: its side as seen from the front, scaled like the
     *     screen (x by the aspect), so a point behind and to the left
     *     pins to the left edge; straight behind falls to the bottom
     */
    sx = x_view / aspect;
    sy = -y_view;
    if (fabs (sx) < 1e-9 && fabs (sy) < 1e-9)
        sy = 1.0;
    else if (sy < 0.0)
        sy = fabs (sx) > 1e-9 ? 0.0 : 1.0;
    clamp_to_edge (out, sx, sy, (gdouble) width, (gdouble) height, m);
    return TRUE;
}

gboolean
lrg_waypoint_indicator_compute_for_camera (LrgWaypointIndicator *out,
                                           LrgCamera3D          *camera,
                                           gfloat                width,
                                           gfloat                height,
                                           gfloat                margin,
                                           const gfloat         *point)
{
    g_autoptr(GrlVector3) position = NULL;
    g_autoptr(GrlVector3) target = NULL;
    g_autoptr(GrlVector3) up = NULL;
    gfloat eye_v[3];
    gfloat target_v[3];
    gfloat up_v[3];

    g_return_val_if_fail (out != NULL, FALSE);
    g_return_val_if_fail (LRG_IS_CAMERA3D (camera), FALSE);
    g_return_val_if_fail (point != NULL, FALSE);

    position = lrg_camera3d_get_position (camera);
    target = lrg_camera3d_get_target (camera);
    up = lrg_camera3d_get_up (camera);
    eye_v[0] = position->x;
    eye_v[1] = position->y;
    eye_v[2] = position->z;
    target_v[0] = target->x;
    target_v[1] = target->y;
    target_v[2] = target->z;
    up_v[0] = up->x;
    up_v[1] = up->y;
    up_v[2] = up->z;
    return lrg_waypoint_indicator_compute (out, eye_v, target_v, up_v,
                                           lrg_camera3d_get_fovy (camera),
                                           width, height, margin, point);
}
