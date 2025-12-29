/* lrg-road.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <math.h>

#include "lrg-road.h"

struct _LrgRoad
{
    gchar *id;
    GArray *waypoints;
    gboolean one_way;
    guint lane_count;
    gfloat cached_length;
    gboolean length_dirty;
};

G_DEFINE_BOXED_TYPE (LrgRoad, lrg_road, lrg_road_copy, lrg_road_free)

static void
invalidate_length_cache (LrgRoad *road)
{
    road->length_dirty = TRUE;
    road->cached_length = 0.0f;
}

LrgRoad *
lrg_road_new (const gchar *id)
{
    LrgRoad *road;

    g_return_val_if_fail (id != NULL, NULL);

    road = g_slice_new0 (LrgRoad);
    road->id = g_strdup (id);
    road->waypoints = g_array_new (FALSE, TRUE, sizeof (LrgRoadWaypoint));
    road->one_way = FALSE;
    road->lane_count = 2;
    road->cached_length = 0.0f;
    road->length_dirty = TRUE;

    return road;
}

LrgRoad *
lrg_road_copy (const LrgRoad *road)
{
    LrgRoad *copy;

    g_return_val_if_fail (road != NULL, NULL);

    copy = g_slice_new (LrgRoad);
    copy->id = g_strdup (road->id);
    copy->waypoints = g_array_copy (road->waypoints);
    copy->one_way = road->one_way;
    copy->lane_count = road->lane_count;
    copy->cached_length = road->cached_length;
    copy->length_dirty = road->length_dirty;

    return copy;
}

void
lrg_road_free (LrgRoad *road)
{
    if (road == NULL)
        return;

    g_free (road->id);
    g_array_unref (road->waypoints);
    g_slice_free (LrgRoad, road);
}

const gchar *
lrg_road_get_id (const LrgRoad *road)
{
    g_return_val_if_fail (road != NULL, NULL);

    return road->id;
}

guint
lrg_road_add_waypoint (LrgRoad *road,
                       gfloat   x,
                       gfloat   y,
                       gfloat   z,
                       gfloat   width,
                       gfloat   speed_limit)
{
    LrgRoadWaypoint wp;

    g_return_val_if_fail (road != NULL, 0);

    wp.x = x;
    wp.y = y;
    wp.z = z;
    wp.width = width;
    wp.speed_limit = speed_limit;

    g_array_append_val (road->waypoints, wp);
    invalidate_length_cache (road);

    return road->waypoints->len - 1;
}

const LrgRoadWaypoint *
lrg_road_get_waypoint (const LrgRoad *road,
                       guint          index)
{
    g_return_val_if_fail (road != NULL, NULL);

    if (index >= road->waypoints->len)
        return NULL;

    return &g_array_index (road->waypoints, LrgRoadWaypoint, index);
}

guint
lrg_road_get_waypoint_count (const LrgRoad *road)
{
    g_return_val_if_fail (road != NULL, 0);

    return road->waypoints->len;
}

void
lrg_road_clear_waypoints (LrgRoad *road)
{
    g_return_if_fail (road != NULL);

    g_array_set_size (road->waypoints, 0);
    invalidate_length_cache (road);
}

gboolean
lrg_road_interpolate (const LrgRoad *road,
                      gfloat         t,
                      gfloat        *x,
                      gfloat        *y,
                      gfloat        *z)
{
    guint segment_count;
    gfloat segment_t;
    guint segment_index;
    gfloat local_t;
    const LrgRoadWaypoint *wp0;
    const LrgRoadWaypoint *wp1;

    g_return_val_if_fail (road != NULL, FALSE);

    if (road->waypoints->len < 2)
        return FALSE;

    t = CLAMP (t, 0.0f, 1.0f);

    segment_count = road->waypoints->len - 1;
    segment_t = t * segment_count;
    segment_index = (guint)segment_t;

    if (segment_index >= segment_count)
        segment_index = segment_count - 1;

    local_t = segment_t - (gfloat)segment_index;

    wp0 = &g_array_index (road->waypoints, LrgRoadWaypoint, segment_index);
    wp1 = &g_array_index (road->waypoints, LrgRoadWaypoint, segment_index + 1);

    if (x != NULL)
        *x = wp0->x + (wp1->x - wp0->x) * local_t;
    if (y != NULL)
        *y = wp0->y + (wp1->y - wp0->y) * local_t;
    if (z != NULL)
        *z = wp0->z + (wp1->z - wp0->z) * local_t;

    return TRUE;
}

gboolean
lrg_road_get_direction_at (const LrgRoad *road,
                           gfloat         t,
                           gfloat        *dx,
                           gfloat        *dy,
                           gfloat        *dz)
{
    guint segment_count;
    guint segment_index;
    const LrgRoadWaypoint *wp0;
    const LrgRoadWaypoint *wp1;
    gfloat dir_x, dir_y, dir_z;
    gfloat length;

    g_return_val_if_fail (road != NULL, FALSE);

    if (road->waypoints->len < 2)
        return FALSE;

    t = CLAMP (t, 0.0f, 1.0f);

    segment_count = road->waypoints->len - 1;
    segment_index = (guint)(t * segment_count);

    if (segment_index >= segment_count)
        segment_index = segment_count - 1;

    wp0 = &g_array_index (road->waypoints, LrgRoadWaypoint, segment_index);
    wp1 = &g_array_index (road->waypoints, LrgRoadWaypoint, segment_index + 1);

    dir_x = wp1->x - wp0->x;
    dir_y = wp1->y - wp0->y;
    dir_z = wp1->z - wp0->z;

    /* Normalize */
    length = sqrtf (dir_x * dir_x + dir_y * dir_y + dir_z * dir_z);
    if (length > 0.0001f)
    {
        dir_x /= length;
        dir_y /= length;
        dir_z /= length;
    }

    if (dx != NULL)
        *dx = dir_x;
    if (dy != NULL)
        *dy = dir_y;
    if (dz != NULL)
        *dz = dir_z;

    return TRUE;
}

gfloat
lrg_road_get_width_at (const LrgRoad *road,
                       gfloat         t)
{
    guint segment_count;
    gfloat segment_t;
    guint segment_index;
    gfloat local_t;
    const LrgRoadWaypoint *wp0;
    const LrgRoadWaypoint *wp1;

    g_return_val_if_fail (road != NULL, 0.0f);

    if (road->waypoints->len == 0)
        return 0.0f;

    if (road->waypoints->len == 1)
        return g_array_index (road->waypoints, LrgRoadWaypoint, 0).width;

    t = CLAMP (t, 0.0f, 1.0f);

    segment_count = road->waypoints->len - 1;
    segment_t = t * segment_count;
    segment_index = (guint)segment_t;

    if (segment_index >= segment_count)
        segment_index = segment_count - 1;

    local_t = segment_t - (gfloat)segment_index;

    wp0 = &g_array_index (road->waypoints, LrgRoadWaypoint, segment_index);
    wp1 = &g_array_index (road->waypoints, LrgRoadWaypoint, segment_index + 1);

    return wp0->width + (wp1->width - wp0->width) * local_t;
}

gfloat
lrg_road_get_speed_limit_at (const LrgRoad *road,
                             gfloat         t)
{
    guint segment_count;
    gfloat segment_t;
    guint segment_index;
    gfloat local_t;
    const LrgRoadWaypoint *wp0;
    const LrgRoadWaypoint *wp1;

    g_return_val_if_fail (road != NULL, 0.0f);

    if (road->waypoints->len == 0)
        return 0.0f;

    if (road->waypoints->len == 1)
        return g_array_index (road->waypoints, LrgRoadWaypoint, 0).speed_limit;

    t = CLAMP (t, 0.0f, 1.0f);

    segment_count = road->waypoints->len - 1;
    segment_t = t * segment_count;
    segment_index = (guint)segment_t;

    if (segment_index >= segment_count)
        segment_index = segment_count - 1;

    local_t = segment_t - (gfloat)segment_index;

    wp0 = &g_array_index (road->waypoints, LrgRoadWaypoint, segment_index);
    wp1 = &g_array_index (road->waypoints, LrgRoadWaypoint, segment_index + 1);

    return wp0->speed_limit + (wp1->speed_limit - wp0->speed_limit) * local_t;
}

gfloat
lrg_road_get_length (const LrgRoad *road)
{
    LrgRoad *mutable_road;
    guint i;
    gfloat total_length;

    g_return_val_if_fail (road != NULL, 0.0f);

    if (!road->length_dirty)
        return road->cached_length;

    if (road->waypoints->len < 2)
        return 0.0f;

    total_length = 0.0f;

    for (i = 0; i < road->waypoints->len - 1; i++)
    {
        const LrgRoadWaypoint *wp0;
        const LrgRoadWaypoint *wp1;
        gfloat dx, dy, dz;

        wp0 = &g_array_index (road->waypoints, LrgRoadWaypoint, i);
        wp1 = &g_array_index (road->waypoints, LrgRoadWaypoint, i + 1);

        dx = wp1->x - wp0->x;
        dy = wp1->y - wp0->y;
        dz = wp1->z - wp0->z;

        total_length += sqrtf (dx * dx + dy * dy + dz * dz);
    }

    /* Cache the result (cast away const for caching) */
    mutable_road = (LrgRoad *)road;
    mutable_road->cached_length = total_length;
    mutable_road->length_dirty = FALSE;

    return total_length;
}

gboolean
lrg_road_find_nearest_point (const LrgRoad *road,
                             gfloat         x,
                             gfloat         y,
                             gfloat         z,
                             gfloat        *out_t,
                             gfloat        *out_distance)
{
    guint i;
    gfloat best_t;
    gfloat best_dist_sq;
    guint segment_count;

    g_return_val_if_fail (road != NULL, FALSE);

    if (road->waypoints->len < 2)
        return FALSE;

    best_t = 0.0f;
    best_dist_sq = G_MAXFLOAT;
    segment_count = road->waypoints->len - 1;

    for (i = 0; i < segment_count; i++)
    {
        const LrgRoadWaypoint *wp0;
        const LrgRoadWaypoint *wp1;
        gfloat seg_x, seg_y, seg_z;
        gfloat to_point_x, to_point_y, to_point_z;
        gfloat seg_len_sq;
        gfloat t_local;
        gfloat proj_x, proj_y, proj_z;
        gfloat dist_sq;
        gfloat global_t;

        wp0 = &g_array_index (road->waypoints, LrgRoadWaypoint, i);
        wp1 = &g_array_index (road->waypoints, LrgRoadWaypoint, i + 1);

        /* Segment vector */
        seg_x = wp1->x - wp0->x;
        seg_y = wp1->y - wp0->y;
        seg_z = wp1->z - wp0->z;

        /* Vector from segment start to point */
        to_point_x = x - wp0->x;
        to_point_y = y - wp0->y;
        to_point_z = z - wp0->z;

        seg_len_sq = seg_x * seg_x + seg_y * seg_y + seg_z * seg_z;

        if (seg_len_sq < 0.0001f)
        {
            /* Degenerate segment */
            t_local = 0.0f;
        }
        else
        {
            /* Project point onto segment line */
            t_local = (to_point_x * seg_x + to_point_y * seg_y + to_point_z * seg_z) / seg_len_sq;
            t_local = CLAMP (t_local, 0.0f, 1.0f);
        }

        /* Closest point on segment */
        proj_x = wp0->x + seg_x * t_local;
        proj_y = wp0->y + seg_y * t_local;
        proj_z = wp0->z + seg_z * t_local;

        /* Distance squared */
        dist_sq = (x - proj_x) * (x - proj_x) +
                  (y - proj_y) * (y - proj_y) +
                  (z - proj_z) * (z - proj_z);

        if (dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            global_t = ((gfloat)i + t_local) / (gfloat)segment_count;
            best_t = global_t;
        }
    }

    if (out_t != NULL)
        *out_t = best_t;
    if (out_distance != NULL)
        *out_distance = sqrtf (best_dist_sq);

    return TRUE;
}

void
lrg_road_set_one_way (LrgRoad  *road,
                      gboolean  one_way)
{
    g_return_if_fail (road != NULL);

    road->one_way = one_way;
}

gboolean
lrg_road_is_one_way (const LrgRoad *road)
{
    g_return_val_if_fail (road != NULL, FALSE);

    return road->one_way;
}

void
lrg_road_set_lane_count (LrgRoad *road,
                         guint    lanes)
{
    g_return_if_fail (road != NULL);
    g_return_if_fail (lanes > 0);

    road->lane_count = lanes;
}

guint
lrg_road_get_lane_count (const LrgRoad *road)
{
    g_return_val_if_fail (road != NULL, 0);

    return road->lane_count;
}
