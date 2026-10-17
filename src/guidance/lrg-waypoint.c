/* lrg-waypoint.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWaypoint: a reference-counted record (g_atomic_rc_box) with the
 * distance and heading arithmetic of guidance displays.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "guidance/lrg-waypoint.h"

#include <math.h>
#include <string.h>

struct _LrgWaypoint
{
    gchar  *id;
    gchar  *label;
    gfloat  position[3];
    guint8  color[4];
    gfloat  radius;
};

/* frees the members when the last reference goes */
static void
waypoint_clear (gpointer data)
{
    LrgWaypoint *self = (LrgWaypoint *) data;

    g_free (self->id);
    g_free (self->label);
}

LrgWaypoint *
lrg_waypoint_ref (LrgWaypoint *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return (LrgWaypoint *) g_atomic_rc_box_acquire (self);
}

void
lrg_waypoint_unref (LrgWaypoint *self)
{
    g_return_if_fail (self != NULL);

    g_atomic_rc_box_release_full (self, waypoint_clear);
}

G_DEFINE_BOXED_TYPE (LrgWaypoint, lrg_waypoint, lrg_waypoint_ref, lrg_waypoint_unref)

LrgWaypoint *
lrg_waypoint_new (const gchar *id)
{
    LrgWaypoint *self;

    self = g_atomic_rc_box_new0 (LrgWaypoint);
    self->id = g_strdup (id);
    self->color[0] = 255;
    self->color[1] = 255;
    self->color[2] = 255;
    self->color[3] = 255;
    self->radius = LRG_WAYPOINT_DEFAULT_RADIUS;
    return self;
}

LrgWaypoint *
lrg_waypoint_copy (LrgWaypoint *self)
{
    LrgWaypoint *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = lrg_waypoint_new (self->id);
    copy->label = g_strdup (self->label);
    memcpy (copy->position, self->position, sizeof (copy->position));
    memcpy (copy->color, self->color, sizeof (copy->color));
    copy->radius = self->radius;
    return copy;
}

const gchar *
lrg_waypoint_get_id (LrgWaypoint *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->id;
}

void
lrg_waypoint_set_label (LrgWaypoint *self,
                        const gchar *label)
{
    g_return_if_fail (self != NULL);

    if (g_strcmp0 (self->label, label) == 0)
        return;
    g_free (self->label);
    self->label = g_strdup (label);
}

const gchar *
lrg_waypoint_get_label (LrgWaypoint *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->label;
}

void
lrg_waypoint_set_position (LrgWaypoint *self,
                           gfloat       x,
                           gfloat       y,
                           gfloat       z)
{
    g_return_if_fail (self != NULL);

    self->position[0] = x;
    self->position[1] = y;
    self->position[2] = z;
}

void
lrg_waypoint_get_position (LrgWaypoint *self,
                           gfloat      *out)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (out != NULL);

    memcpy (out, self->position, sizeof (self->position));
}

void
lrg_waypoint_set_color (LrgWaypoint *self,
                        guint8       r,
                        guint8       g,
                        guint8       b,
                        guint8       a)
{
    g_return_if_fail (self != NULL);

    self->color[0] = r;
    self->color[1] = g;
    self->color[2] = b;
    self->color[3] = a;
}

void
lrg_waypoint_get_color (LrgWaypoint *self,
                        guint8      *r,
                        guint8      *g,
                        guint8      *b,
                        guint8      *a)
{
    g_return_if_fail (self != NULL);

    if (r != NULL)
        *r = self->color[0];
    if (g != NULL)
        *g = self->color[1];
    if (b != NULL)
        *b = self->color[2];
    if (a != NULL)
        *a = self->color[3];
}

void
lrg_waypoint_set_radius (LrgWaypoint *self,
                         gfloat       radius)
{
    g_return_if_fail (self != NULL);

    self->radius = (radius > 0.0f && isfinite (radius)) ? radius : 0.0f;
}

gfloat
lrg_waypoint_get_radius (LrgWaypoint *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);

    return self->radius;
}

gfloat
lrg_waypoint_distance_from (LrgWaypoint  *self,
                            const gfloat *from)
{
    gdouble dx;
    gdouble dy;
    gdouble dz;

    g_return_val_if_fail (self != NULL, 0.0f);
    g_return_val_if_fail (from != NULL, 0.0f);

    dx = (gdouble) self->position[0] - (gdouble) from[0];
    dy = (gdouble) self->position[1] - (gdouble) from[1];
    dz = (gdouble) self->position[2] - (gdouble) from[2];
    return (gfloat) sqrt (dx * dx + dy * dy + dz * dz);
}

gfloat
lrg_waypoint_heading_from (LrgWaypoint  *self,
                           const gfloat *from)
{
    gdouble dx;
    gdouble dz;
    gdouble heading;

    g_return_val_if_fail (self != NULL, 0.0f);
    g_return_val_if_fail (from != NULL, 0.0f);

    dx = (gdouble) self->position[0] - (gdouble) from[0];
    dz = (gdouble) self->position[2] - (gdouble) from[2];
    if (fabs (dx) < 1e-6 && fabs (dz) < 1e-6)
        return 0.0f;
    /* atan2 (x, z): 0 along +Z, a quarter turn towards +X is +90 */
    heading = atan2 (dx, dz) * 180.0 / G_PI;
    if (heading < 0.0)
        heading += 360.0;
    if (heading >= 360.0)
        heading -= 360.0;
    return (gfloat) heading;
}

gboolean
lrg_waypoint_is_reached (LrgWaypoint  *self,
                         const gfloat *from)
{
    gdouble dx;
    gdouble dz;
    gdouble dy;
    gdouble vertical;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (from != NULL, FALSE);

    dx = (gdouble) self->position[0] - (gdouble) from[0];
    dz = (gdouble) self->position[2] - (gdouble) from[2];
    dy = fabs ((gdouble) self->position[1] - (gdouble) from[1]);
    /* a floor above or below is not the spot: allow a few radii of height */
    vertical = MAX (2.0, 4.0 * (gdouble) self->radius);
    return sqrt (dx * dx + dz * dz) <= (gdouble) self->radius && dy <= vertical;
}

gfloat
lrg_waypoint_relative_heading (gfloat heading,
                               gfloat facing)
{
    gdouble relative;

    if (!isfinite (heading) || !isfinite (facing))
        return 0.0f;
    relative = fmod ((gdouble) heading - (gdouble) facing, 360.0);
    if (relative <= -180.0)
        relative += 360.0;
    else if (relative > 180.0)
        relative -= 360.0;
    return (gfloat) relative;
}

gchar *
lrg_waypoint_format_distance (gfloat distance)
{
    if (!isfinite (distance) || distance < 0.0f)
        distance = 0.0f;
    if (distance < 999.5f)
        return g_strdup_printf ("%d m", (gint) floorf (distance + 0.5f));
    if (distance < 9950.0f)
    {
        gchar number[G_ASCII_DTOSTR_BUF_SIZE];

        /* the locale's decimal comma would vary the text: always a point */
        g_ascii_formatd (number, sizeof (number), "%.1f", (gdouble) distance / 1000.0);
        return g_strdup_printf ("%s km", number);
    }
    return g_strdup_printf ("%d km", (gint) floor ((gdouble) distance / 1000.0 + 0.5));
}
