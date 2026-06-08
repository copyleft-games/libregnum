/* lrg-reel-path.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-path.h"
#include <math.h>

#define REEL_PATH_TOLERANCE 0.25f

gdouble
lrg_reel_path_length (GrlPath *path)
{
    GrlVector2 *pts;
    guint      *splen = NULL;
    guint       nsub = 0;
    guint       total = 0;
    gdouble     len = 0.0;
    guint       s;
    guint       idx = 0;

    g_return_val_if_fail (path != NULL, 0.0);

    pts = grl_path_get_flattened (path, REEL_PATH_TOLERANCE, &splen, &nsub, &total);
    if (pts == NULL)
        return 0.0;

    for (s = 0; s < nsub; s++)
    {
        guint count = splen[s];
        guint i;

        for (i = 1; i < count; i++)
        {
            gdouble dx = pts[idx + i].x - pts[idx + i - 1].x;
            gdouble dy = pts[idx + i].y - pts[idx + i - 1].y;

            len += sqrt (dx * dx + dy * dy);
        }
        idx += count;
    }

    g_free (pts);
    g_free (splen);

    return len;
}

gboolean
lrg_reel_path_point_at (GrlPath *path,
                        gdouble  t,
                        gdouble *out_x,
                        gdouble *out_y,
                        gdouble *out_angle)
{
    GrlVector2 *pts;
    guint      *splen = NULL;
    guint       nsub = 0;
    guint       total = 0;
    gdouble     length;
    gdouble     target;
    gdouble     acc = 0.0;
    guint       s;
    guint       idx = 0;
    gboolean    found = FALSE;

    g_return_val_if_fail (path != NULL, FALSE);

    t = CLAMP (t, 0.0, 1.0);

    pts = grl_path_get_flattened (path, REEL_PATH_TOLERANCE, &splen, &nsub, &total);
    if (pts == NULL || total == 0)
    {
        g_free (pts);
        g_free (splen);
        return FALSE;
    }

    length = lrg_reel_path_length (path);
    if (length <= 0.0)
    {
        /* Degenerate path: report the first point. */
        if (out_x != NULL) *out_x = pts[0].x;
        if (out_y != NULL) *out_y = pts[0].y;
        if (out_angle != NULL) *out_angle = 0.0;
        g_free (pts);
        g_free (splen);
        return TRUE;
    }

    target = t * length;

    for (s = 0; s < nsub && !found; s++)
    {
        guint count = splen[s];
        guint i;

        for (i = 1; i < count; i++)
        {
            gdouble dx = pts[idx + i].x - pts[idx + i - 1].x;
            gdouble dy = pts[idx + i].y - pts[idx + i - 1].y;
            gdouble seg = sqrt (dx * dx + dy * dy);

            if (acc + seg >= target || (s == nsub - 1 && i == count - 1))
            {
                gdouble local = (seg > 0.0) ? (target - acc) / seg : 0.0;

                local = CLAMP (local, 0.0, 1.0);
                if (out_x != NULL)
                    *out_x = pts[idx + i - 1].x + dx * local;
                if (out_y != NULL)
                    *out_y = pts[idx + i - 1].y + dy * local;
                if (out_angle != NULL)
                    *out_angle = atan2 (dy, dx);
                found = TRUE;
                break;
            }
            acc += seg;
        }
        idx += count;
    }

    g_free (pts);
    g_free (splen);

    return found;
}

GrlPath *
lrg_reel_path_evolve (GrlPath *path,
                      gdouble  t)
{
    GrlVector2 *pts;
    guint      *splen = NULL;
    guint       nsub = 0;
    guint       total = 0;
    GrlPath    *out;
    gdouble     length;
    gdouble     target;
    gdouble     acc = 0.0;
    guint       s;
    guint       idx = 0;
    gboolean    done = FALSE;

    g_return_val_if_fail (path != NULL, NULL);

    t = CLAMP (t, 0.0, 1.0);
    out = grl_path_new ();

    pts = grl_path_get_flattened (path, REEL_PATH_TOLERANCE, &splen, &nsub, &total);
    if (pts == NULL || total == 0)
    {
        g_free (pts);
        g_free (splen);
        return out;
    }

    length = lrg_reel_path_length (path);
    target = t * length;

    for (s = 0; s < nsub && !done; s++)
    {
        guint count = splen[s];
        guint i;

        if (count == 0)
            continue;

        grl_path_move_to (out, pts[idx].x, pts[idx].y);

        for (i = 1; i < count; i++)
        {
            gdouble dx = pts[idx + i].x - pts[idx + i - 1].x;
            gdouble dy = pts[idx + i].y - pts[idx + i - 1].y;
            gdouble seg = sqrt (dx * dx + dy * dy);

            if (acc + seg >= target)
            {
                gdouble local = (seg > 0.0) ? (target - acc) / seg : 0.0;

                local = CLAMP (local, 0.0, 1.0);
                grl_path_line_to (out,
                                  pts[idx + i - 1].x + dx * local,
                                  pts[idx + i - 1].y + dy * local);
                done = TRUE;
                break;
            }

            grl_path_line_to (out, pts[idx + i].x, pts[idx + i].y);
            acc += seg;
        }
        idx += count;
    }

    g_free (pts);
    g_free (splen);

    return out;
}
