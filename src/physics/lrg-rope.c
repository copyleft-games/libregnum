/* lrg-rope.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Position-based dynamics (Jakobsen's verlet chain): every point carries
 * its last position, a step moves each by its implied velocity plus
 * gravity, then the segment constraints pull neighbours to the rest
 * length, the ends pinned.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "physics/lrg-rope.h"

#define MAX_STEP (0.05f)

struct _LrgRope
{
    GObject   parent_instance;

    guint     segments;
    gfloat    length;
    gfloat   *points;       /* 3 per point */
    gfloat   *previous;     /* 3 per point */
    gfloat    anchor_a[3];
    gfloat    anchor_b[3];
    gboolean  anchored;     /* the anchors were set and the rope laid out */
    gfloat    gravity;
    gfloat    damping;
    guint     iterations;
    gboolean  floor_on;
    gfloat    floor_y;
};

G_DEFINE_FINAL_TYPE (LrgRope, lrg_rope, G_TYPE_OBJECT)

static void
lrg_rope_finalize (GObject *object)
{
    LrgRope *self = LRG_ROPE (object);

    g_free (self->points);
    g_free (self->previous);

    G_OBJECT_CLASS (lrg_rope_parent_class)->finalize (object);
}

static void
lrg_rope_class_init (LrgRopeClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_rope_finalize;
}

static void
lrg_rope_init (LrgRope *self)
{
    self->gravity = -9.8f;
    self->damping = 0.2f;
    self->iterations = 12;
}

/* hang the rope straight down from the origin */
static void
hang (LrgRope *self)
{
    guint i;
    gfloat step = self->length / (gfloat)self->segments;

    for (i = 0; i <= self->segments; i++)
    {
        self->points[i * 3 + 0] = 0.0f;
        self->points[i * 3 + 1] = -step * (gfloat)i;
        self->points[i * 3 + 2] = 0.0f;
    }
    memcpy (self->previous, self->points, (gsize)(self->segments + 1) * 3 * sizeof (gfloat));
}

LrgRope *
lrg_rope_new (guint  segments,
              gfloat length)
{
    LrgRope *self;

    g_return_val_if_fail (segments >= 2 && segments <= LRG_ROPE_MAX_SEGMENTS, NULL);
    g_return_val_if_fail (length > 0.0f && isfinite (length), NULL);

    self = g_object_new (LRG_TYPE_ROPE, NULL);
    self->segments = segments;
    self->length = length;
    self->points = g_new0 (gfloat, (gsize)(segments + 1) * 3);
    self->previous = g_new0 (gfloat, (gsize)(segments + 1) * 3);
    hang (self);
    return self;
}

static gfloat
distance3 (const gfloat *a,
           const gfloat *b)
{
    gfloat dx = a[0] - b[0];
    gfloat dy = a[1] - b[1];
    gfloat dz = a[2] - b[2];

    return sqrtf (dx * dx + dy * dy + dz * dz);
}

/* lay the rope out between the anchors, sagging by what slack it has */
static void
lay_out (LrgRope *self)
{
    gfloat chord = distance3 (self->anchor_a, self->anchor_b);
    gfloat slack = MAX (self->length - chord, 0.0f);
    /* a parabola's arc length is about chord + 8 s^2 / (3 chord), so the
     * sag of the slack is s = sqrt (3 chord slack / 8) */
    gfloat sag = chord > 1e-4f ? sqrtf (3.0f * chord * slack / 8.0f) : slack * 0.5f;
    guint i;

    for (i = 0; i <= self->segments; i++)
    {
        gfloat t = (gfloat)i / (gfloat)self->segments;
        guint k;

        for (k = 0; k < 3; k++)
            self->points[i * 3 + k] = self->anchor_a[k] + (self->anchor_b[k] - self->anchor_a[k]) * t;
        self->points[i * 3 + 1] -= sag * 4.0f * t * (1.0f - t);
    }
    memcpy (self->previous, self->points, (gsize)(self->segments + 1) * 3 * sizeof (gfloat));
    self->anchored = TRUE;
}

void
lrg_rope_set_anchors (LrgRope      *self,
                      const gfloat *start,
                      const gfloat *end)
{
    g_return_if_fail (LRG_IS_ROPE (self));
    g_return_if_fail (start != NULL && end != NULL);

    memcpy (self->anchor_a, start, 3 * sizeof (gfloat));
    memcpy (self->anchor_b, end, 3 * sizeof (gfloat));
    if (!self->anchored)
        lay_out (self);
}

void
lrg_rope_set_gravity (LrgRope *self,
                      gfloat   gravity)
{
    g_return_if_fail (LRG_IS_ROPE (self));
    self->gravity = isfinite (gravity) ? gravity : -9.8f;
}

void
lrg_rope_set_damping (LrgRope *self,
                      gfloat   damping)
{
    g_return_if_fail (LRG_IS_ROPE (self));
    self->damping = CLAMP (damping, 0.0f, 1.0f);
}

void
lrg_rope_set_iterations (LrgRope *self,
                         guint    iterations)
{
    g_return_if_fail (LRG_IS_ROPE (self));
    self->iterations = CLAMP (iterations, 1u, 64u);
}

void
lrg_rope_set_floor (LrgRope *self,
                    gboolean enabled,
                    gfloat   height)
{
    g_return_if_fail (LRG_IS_ROPE (self));
    self->floor_on = enabled;
    self->floor_y = height;
}

void
lrg_rope_reset (LrgRope *self)
{
    g_return_if_fail (LRG_IS_ROPE (self));

    self->anchored = FALSE;
    hang (self);
}

/* one substep of @dt seconds */
static void
substep (LrgRope *self,
         gfloat   dt)
{
    guint n = self->segments + 1;
    gfloat rest = self->length / (gfloat)self->segments;
    gfloat keep = powf (self->damping, dt);   /* the share of velocity kept over dt */
    guint i;
    guint pass;

    /* integrate (the two ends are re-pinned below) */
    for (i = 0; i < n; i++)
    {
        gfloat *p = self->points + i * 3;
        gfloat *q = self->previous + i * 3;
        gfloat vx = (p[0] - q[0]) * keep;
        gfloat vy = (p[1] - q[1]) * keep;
        gfloat vz = (p[2] - q[2]) * keep;

        q[0] = p[0];
        q[1] = p[1];
        q[2] = p[2];
        p[0] += vx;
        p[1] += vy + self->gravity * dt * dt;
        p[2] += vz;
    }

    for (pass = 0; pass < self->iterations; pass++)
    {
        /* the ends first, so the chain pulls toward them */
        memcpy (self->points, self->anchor_a, 3 * sizeof (gfloat));
        memcpy (self->points + self->segments * 3, self->anchor_b, 3 * sizeof (gfloat));

        for (i = 0; i < self->segments; i++)
        {
            gfloat *a = self->points + i * 3;
            gfloat *b = a + 3;
            gfloat dx = b[0] - a[0];
            gfloat dy = b[1] - a[1];
            gfloat dz = b[2] - a[2];
            gfloat d = sqrtf (dx * dx + dy * dy + dz * dz);
            gfloat diff;
            gfloat wa = i == 0 ? 0.0f : 0.5f;
            gfloat wb = i + 1 == self->segments ? 0.0f : 0.5f;
            gfloat total;

            if (d < 1e-7f)
                continue;
            /* a pinned end takes none of the correction; the other point all of it */
            total = wa + wb;
            if (total <= 0.0f)
                continue;
            diff = (d - rest) / d;
            a[0] += dx * diff * (wa / total);
            a[1] += dy * diff * (wa / total);
            a[2] += dz * diff * (wa / total);
            b[0] -= dx * diff * (wb / total);
            b[1] -= dy * diff * (wb / total);
            b[2] -= dz * diff * (wb / total);
        }
        if (self->floor_on)
        {
            for (i = 0; i < n; i++)
                self->points[i * 3 + 1] = MAX (self->points[i * 3 + 1], self->floor_y);
        }
    }
    memcpy (self->points, self->anchor_a, 3 * sizeof (gfloat));
    memcpy (self->points + self->segments * 3, self->anchor_b, 3 * sizeof (gfloat));
}

void
lrg_rope_step (LrgRope *self,
               gfloat   delta)
{
    g_return_if_fail (LRG_IS_ROPE (self));

    if (!self->anchored || !(delta > 0.0f) || !isfinite (delta))
        return;
    while (delta > 1e-6f)
    {
        gfloat dt = MIN (delta, MAX_STEP);

        substep (self, dt);
        delta -= dt;
    }
}

guint
lrg_rope_get_point_count (LrgRope *self)
{
    g_return_val_if_fail (LRG_IS_ROPE (self), 0);
    return self->segments + 1;
}

gfloat
lrg_rope_get_length (LrgRope *self)
{
    g_return_val_if_fail (LRG_IS_ROPE (self), 0.0f);
    return self->length;
}

const gfloat *
lrg_rope_get_points (LrgRope *self,
                     guint   *out_count)
{
    g_return_val_if_fail (LRG_IS_ROPE (self), NULL);

    if (out_count != NULL)
        *out_count = self->segments + 1;
    return self->points;
}

gfloat
lrg_rope_get_tension (LrgRope *self)
{
    gfloat chord;

    g_return_val_if_fail (LRG_IS_ROPE (self), 0.0f);

    chord = distance3 (self->anchor_a, self->anchor_b);
    return CLAMP (chord / self->length, 0.0f, 1.0f);
}
