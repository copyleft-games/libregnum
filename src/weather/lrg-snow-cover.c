/* lrg-snow-cover.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A linear accumulation model: the level integrates snowfall minus
 * melt, tracks are dents that fill with new snow and fade as it melts.
 * The tracks live in a ring of LRG_SNOW_COVER_MAX_TRACKS kept in age
 * order.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "weather/lrg-snow-cover.h"

/* tracks fill this many times faster than the level builds */
#define TRACK_FILL_FACTOR (4.0)

typedef struct
{
    gdouble x;
    gdouble z;
    gdouble radius;
    gdouble depth;
} SnowTrack;

struct _LrgSnowCover
{
    GObject   parent_instance;

    gdouble   level;
    gdouble   build;     /* per hour */
    gdouble   melt;      /* per hour */
    SnowTrack tracks[LRG_SNOW_COVER_MAX_TRACKS];
    guint     n_tracks;  /* tracks[0] is the oldest */
};

G_DEFINE_FINAL_TYPE (LrgSnowCover, lrg_snow_cover, G_TYPE_OBJECT)

static void
lrg_snow_cover_class_init (LrgSnowCoverClass *klass)
{
}

static void
lrg_snow_cover_init (LrgSnowCover *self)
{
    self->build = 1.0 / 6.0;
    self->melt = 1.0 / 12.0;
}

LrgSnowCover *
lrg_snow_cover_new (void)
{
    return g_object_new (LRG_TYPE_SNOW_COVER, NULL);
}

void
lrg_snow_cover_set_rates (LrgSnowCover *self,
                          gdouble       build_per_hour,
                          gdouble       melt_per_hour)
{
    g_return_if_fail (LRG_IS_SNOW_COVER (self));

    if (isfinite (build_per_hour) && build_per_hour >= 0.0)
        self->build = build_per_hour;
    if (isfinite (melt_per_hour) && melt_per_hour >= 0.0)
        self->melt = melt_per_hour;
}

void
lrg_snow_cover_update (LrgSnowCover *self,
                       gdouble       seconds,
                       gdouble       snowfall,
                       gdouble       warmth)
{
    gdouble hours;
    gdouble fill;
    guint   i;
    guint   kept;

    g_return_if_fail (LRG_IS_SNOW_COVER (self));

    if (!isfinite (seconds) || seconds <= 0.0 || !isfinite (snowfall) || !isfinite (warmth))
        return;
    snowfall = CLAMP (snowfall, 0.0, 1.0);
    warmth = CLAMP (warmth, 0.0, 1.0);
    hours = seconds / 3600.0;

    self->level = CLAMP (self->level + (snowfall * self->build - warmth * self->melt) * hours,
                         0.0, 1.0);

    /* tracks fill with new snow and fade with the melt; bare ground
     * keeps none */
    if (self->level <= 0.0)
    {
        self->n_tracks = 0;
        return;
    }
    fill = (snowfall * TRACK_FILL_FACTOR * self->build + warmth * self->melt) * hours;
    kept = 0;
    for (i = 0; i < self->n_tracks; i++)
    {
        SnowTrack t = self->tracks[i];

        t.depth -= fill;
        if (t.depth > 0.0)
            self->tracks[kept++] = t;
    }
    self->n_tracks = kept;
}

gdouble
lrg_snow_cover_get_level (LrgSnowCover *self)
{
    g_return_val_if_fail (LRG_IS_SNOW_COVER (self), 0.0);
    return self->level;
}

void
lrg_snow_cover_set_level (LrgSnowCover *self,
                          gdouble       level)
{
    g_return_if_fail (LRG_IS_SNOW_COVER (self));

    if (!isfinite (level))
        return;
    self->level = CLAMP (level, 0.0, 1.0);
    if (self->level <= 0.0)
        self->n_tracks = 0;
}

gboolean
lrg_snow_cover_stamp (LrgSnowCover *self,
                      gdouble       x,
                      gdouble       z,
                      gdouble       radius,
                      gdouble       depth)
{
    SnowTrack *newest;

    g_return_val_if_fail (LRG_IS_SNOW_COVER (self), FALSE);

    if (self->level <= 0.0 || !isfinite (x) || !isfinite (z) || !isfinite (radius) ||
        radius <= 0.0 || !isfinite (depth) || depth <= 0.0)
        return FALSE;
    depth = MIN (depth, 1.0);

    /* standing on the newest track deepens it */
    if (self->n_tracks > 0)
    {
        newest = &self->tracks[self->n_tracks - 1];
        if (hypot (newest->x - x, newest->z - z) < newest->radius * 0.5)
        {
            newest->depth = MAX (newest->depth, depth);
            return TRUE;
        }
    }

    /* full: the oldest goes */
    if (self->n_tracks == LRG_SNOW_COVER_MAX_TRACKS)
    {
        memmove (&self->tracks[0], &self->tracks[1],
                 sizeof (SnowTrack) * (LRG_SNOW_COVER_MAX_TRACKS - 1));
        self->n_tracks--;
    }
    newest = &self->tracks[self->n_tracks++];
    newest->x = x;
    newest->z = z;
    newest->radius = radius;
    newest->depth = depth;
    return TRUE;
}

guint
lrg_snow_cover_get_track_count (LrgSnowCover *self)
{
    g_return_val_if_fail (LRG_IS_SNOW_COVER (self), 0);
    return self->n_tracks;
}

gboolean
lrg_snow_cover_get_track (LrgSnowCover *self,
                          guint         index,
                          gdouble      *x,
                          gdouble      *z,
                          gdouble      *radius,
                          gdouble      *depth)
{
    const SnowTrack *t;

    g_return_val_if_fail (LRG_IS_SNOW_COVER (self), FALSE);

    if (index >= self->n_tracks)
        return FALSE;
    t = &self->tracks[index];
    if (x != NULL)
        *x = t->x;
    if (z != NULL)
        *z = t->z;
    if (radius != NULL)
        *radius = t->radius;
    if (depth != NULL)
        *depth = t->depth;
    return TRUE;
}

void
lrg_snow_cover_clear_tracks (LrgSnowCover *self)
{
    g_return_if_fail (LRG_IS_SNOW_COVER (self));
    self->n_tracks = 0;
}

gdouble
lrg_snow_cover_get_depth_at (LrgSnowCover *self,
                             gdouble       x,
                             gdouble       z)
{
    gdouble dent = 0.0;
    guint   i;

    g_return_val_if_fail (LRG_IS_SNOW_COVER (self), 0.0);

    for (i = 0; i < self->n_tracks; i++)
    {
        const SnowTrack *t = &self->tracks[i];
        gdouble          d = hypot (t->x - x, t->z - z);
        gdouble          f;

        if (d >= t->radius)
            continue;
        /* full inside half the radius, smooth to nothing at the rim */
        f = (d - t->radius * 0.5) / (t->radius * 0.5);
        f = CLAMP (f, 0.0, 1.0);
        f = 1.0 - f * f * (3.0 - 2.0 * f);
        dent = MAX (dent, t->depth * f);
    }
    return self->level * (1.0 - dent);
}
