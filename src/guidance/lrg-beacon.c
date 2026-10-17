/* lrg-beacon.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The beacon's shape for a viewer (pure) and its drawing through
 * graylib: a translucent cylinder that does not write depth, and a
 * double cone marker.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "guidance/lrg-beacon.h"

#include <graylib.h>
#include <math.h>
#include <string.h>

/* cylinder sides: smooth enough up close, cheap far away */
#define BEACON_SLICES (16)

LrgBeaconStyle *
lrg_beacon_style_copy (const LrgBeaconStyle *style)
{
    g_return_val_if_fail (style != NULL, NULL);

    return (LrgBeaconStyle *) g_memdup2 (style, sizeof (LrgBeaconStyle));
}

void
lrg_beacon_style_free (LrgBeaconStyle *style)
{
    g_free (style);
}

G_DEFINE_BOXED_TYPE (LrgBeaconStyle, lrg_beacon_style, lrg_beacon_style_copy,
                     lrg_beacon_style_free)

void
lrg_beacon_style_init (LrgBeaconStyle *style)
{
    g_return_if_fail (style != NULL);

    style->min_radius = 0.15f;
    style->radius_per_unit = 0.004f;
    style->max_radius = 30.0f;
    style->height = 80.0f;
    style->fade_near = 1.5f;
    style->fade_far = 6.0f;
    style->alpha = 0.35f;
    style->marker_lift = 2.2f;
    style->marker_size = 0.3f;
    style->bob_amplitude = 0.12f;
    style->bob_period = 2.0f;
}

gboolean
lrg_beacon_compute (const LrgBeaconStyle *style,
                    const gfloat         *eye,
                    const gfloat         *foot,
                    gdouble               time,
                    LrgBeaconShape       *out)
{
    LrgBeaconStyle defaults;
    gdouble dx;
    gdouble dy;
    gdouble dz;
    gdouble distance;
    gdouble fade;
    gdouble radius;
    gdouble bob;
    guint i;

    g_return_val_if_fail (eye != NULL, FALSE);
    g_return_val_if_fail (foot != NULL, FALSE);
    g_return_val_if_fail (out != NULL, FALSE);

    if (style == NULL)
    {
        lrg_beacon_style_init (&defaults);
        style = &defaults;
    }
    memset (out, 0, sizeof (*out));
    for (i = 0; i < 3; i++)
    {
        if (!isfinite (eye[i]) || !isfinite (foot[i]))
            return FALSE;
    }
    memcpy (out->foot, foot, sizeof (out->foot));

    dx = (gdouble) foot[0] - (gdouble) eye[0];
    dy = (gdouble) foot[1] - (gdouble) eye[1];
    dz = (gdouble) foot[2] - (gdouble) eye[2];
    distance = sqrt (dx * dx + dy * dy + dz * dz);
    out->distance = (gfloat) distance;

    /* --- the column widens with distance, within its limits --- */
    radius = (gdouble) style->min_radius + distance * (gdouble) style->radius_per_unit;
    radius = MIN (radius, (gdouble) MAX (style->max_radius, style->min_radius));
    out->radius = (gfloat) radius;
    out->height = MAX (0.0f, style->height);

    /* --- it fades in between fade_near and fade_far --- */
    if (distance <= (gdouble) style->fade_near)
        fade = 0.0;
    else if (distance >= (gdouble) style->fade_far || style->fade_far <= style->fade_near)
        fade = 1.0;
    else
        fade = (distance - (gdouble) style->fade_near) /
               ((gdouble) style->fade_far - (gdouble) style->fade_near);
    out->alpha = (gfloat) CLAMP (fade * (gdouble) style->alpha, 0.0, 1.0);

    /* --- the marker bobs over the foot, as big as the column allows --- */
    bob = 0.0;
    if (style->bob_period > 0.0f && isfinite (time))
        bob = sin (time * 2.0 * G_PI / (gdouble) style->bob_period) * (gdouble) style->bob_amplitude;
    out->marker_size = (gfloat) MAX ((gdouble) style->marker_size, radius * 1.5);
    out->marker[0] = foot[0];
    out->marker[1] = (gfloat) ((gdouble) foot[1] + (gdouble) style->marker_lift +
                               (gdouble) out->marker_size + bob);
    out->marker[2] = foot[2];
    return out->alpha > 0.0f;
}

/*
 * flush_batch:
 * Draws what raylib's render batch holds, so a depth mask change
 * applies to exactly the beacon's triangles.  graylib has no public
 * rlDrawRenderBatchActive(); EndScissorMode() is that call followed by
 * disabling the scissor test, which no 3D pass uses.
 */
static void
flush_batch (void)
{
    grl_draw_end_scissor_mode ();
}

void
lrg_beacon_draw (const LrgBeaconShape *shape,
                 guint8                r,
                 guint8                g,
                 guint8                b)
{
    GrlVector3 base;
    GrlVector3 tip;
    GrlColor column;
    GrlColor marker;
    gfloat marker_alpha;

    g_return_if_fail (shape != NULL);

    if (!(shape->alpha > 0.0f))
        return;
    column.r = r;
    column.g = g;
    column.b = b;
    column.a = (guint8) CLAMP (shape->alpha * 255.0f, 0.0f, 255.0f);
    marker_alpha = MIN (1.0f, shape->alpha * 2.0f);
    marker.r = r;
    marker.g = g;
    marker.b = b;
    marker.a = (guint8) CLAMP (marker_alpha * 255.0f, 0.0f, 255.0f);

    /* translucent: blended, and the world in front still hides it */
    grl_draw_begin_blend_mode (GRL_BLEND_ALPHA);
    flush_batch ();
    grl_rlgl_disable_depth_mask ();
    grl_rlgl_disable_backface_culling ();

    base.x = shape->foot[0];
    base.y = shape->foot[1];
    base.z = shape->foot[2];
    grl_draw_cylinder (&base, shape->radius, shape->radius, shape->height, BEACON_SLICES, &column);

    /* the marker: a cone up and a cone down from its middle */
    tip.x = shape->marker[0];
    tip.y = shape->marker[1];
    tip.z = shape->marker[2];
    grl_draw_cylinder (&tip, 0.0f, shape->marker_size * 0.6f, shape->marker_size, 4, &marker);
    tip.y -= shape->marker_size;
    grl_draw_cylinder (&tip, shape->marker_size * 0.6f, 0.0f, shape->marker_size, 4, &marker);

    flush_batch ();
    grl_rlgl_enable_backface_culling ();
    grl_rlgl_enable_depth_mask ();
    grl_draw_end_blend_mode ();
}
