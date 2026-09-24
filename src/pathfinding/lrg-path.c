/* lrg-path.c
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Path result implementation.
 */

#include "lrg-path.h"
#include <math.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PATHFIND
#include "lrg-log.h"

struct _LrgPath
{
    GArray  *points;
    gfloat   total_cost;
};

G_DEFINE_BOXED_TYPE (LrgPath, lrg_path,
                     lrg_path_copy,
                     lrg_path_free)

/*
 * lrg_path_new:
 *
 * Creates a new empty path.
 *
 * Returns: (transfer full): A new #LrgPath
 */
LrgPath *
lrg_path_new (void)
{
    LrgPath *self;

    self = g_slice_new0 (LrgPath);
    self->points = g_array_new (FALSE, FALSE, sizeof (LrgPathPoint));
    self->total_cost = 0.0f;

    return self;
}

/*
 * lrg_path_copy:
 * @self: an #LrgPath
 *
 * Creates a deep copy of a path.
 *
 * Returns: (transfer full): A copy of @self
 */
LrgPath *
lrg_path_copy (const LrgPath *self)
{
    LrgPath *copy;
    guint i;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_slice_new0 (LrgPath);
    copy->points = g_array_new (FALSE, FALSE, sizeof (LrgPathPoint));
    copy->total_cost = self->total_cost;

    /* Copy all points */
    for (i = 0; i < self->points->len; i++)
    {
        LrgPathPoint *pt = &g_array_index (self->points, LrgPathPoint, i);
        g_array_append_val (copy->points, *pt);
    }

    return copy;
}

/*
 * lrg_path_free:
 * @self: an #LrgPath
 *
 * Frees a path and all its points.
 */
void
lrg_path_free (LrgPath *self)
{
    if (self == NULL)
        return;

    g_array_free (self->points, TRUE);
    g_slice_free (LrgPath, self);
}

/*
 * lrg_path_append:
 * @self: an #LrgPath
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Appends a point to the end of the path.
 */
void
lrg_path_append (LrgPath *self,
                 gint     x,
                 gint     y)
{
    LrgPathPoint pt;

    g_return_if_fail (self != NULL);

    pt.x = x;
    pt.y = y;
    g_array_append_val (self->points, pt);
}

/*
 * lrg_path_prepend:
 * @self: an #LrgPath
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Prepends a point to the beginning of the path.
 */
void
lrg_path_prepend (LrgPath *self,
                  gint     x,
                  gint     y)
{
    LrgPathPoint pt;

    g_return_if_fail (self != NULL);

    pt.x = x;
    pt.y = y;
    g_array_prepend_val (self->points, pt);
}

/*
 * lrg_path_get_length:
 * @self: an #LrgPath
 *
 * Gets the number of points in the path.
 *
 * Returns: Number of points
 */
guint
lrg_path_get_length (const LrgPath *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->points->len;
}

/*
 * lrg_path_is_empty:
 * @self: an #LrgPath
 *
 * Checks if the path has no points.
 *
 * Returns: %TRUE if the path is empty
 */
gboolean
lrg_path_is_empty (const LrgPath *self)
{
    g_return_val_if_fail (self != NULL, TRUE);
    return self->points->len == 0;
}

/*
 * lrg_path_get_point:
 * @self: an #LrgPath
 * @index: Point index
 * @x: (out): X coordinate
 * @y: (out): Y coordinate
 *
 * Gets a point at the specified index.
 *
 * Returns: %TRUE if the index is valid
 */
gboolean
lrg_path_get_point (const LrgPath *self,
                    guint          index,
                    gint          *x,
                    gint          *y)
{
    LrgPathPoint *pt;

    g_return_val_if_fail (self != NULL, FALSE);

    if (index >= self->points->len)
        return FALSE;

    pt = &g_array_index (self->points, LrgPathPoint, index);

    if (x != NULL)
        *x = pt->x;
    if (y != NULL)
        *y = pt->y;

    return TRUE;
}

/*
 * lrg_path_get_start:
 * @self: an #LrgPath
 * @x: (out): X coordinate
 * @y: (out): Y coordinate
 *
 * Gets the starting point (first point) of the path.
 *
 * Returns: %TRUE if the path is not empty
 */
gboolean
lrg_path_get_start (const LrgPath *self,
                    gint          *x,
                    gint          *y)
{
    return lrg_path_get_point (self, 0, x, y);
}

/*
 * lrg_path_get_end:
 * @self: an #LrgPath
 * @x: (out): X coordinate
 * @y: (out): Y coordinate
 *
 * Gets the ending point (last point) of the path.
 *
 * Returns: %TRUE if the path is not empty
 */
gboolean
lrg_path_get_end (const LrgPath *self,
                  gint          *x,
                  gint          *y)
{
    g_return_val_if_fail (self != NULL, FALSE);

    if (self->points->len == 0)
        return FALSE;

    return lrg_path_get_point (self, self->points->len - 1, x, y);
}

/*
 * lrg_path_reverse:
 * @self: an #LrgPath
 *
 * Reverses the order of points in the path.
 */
void
lrg_path_reverse (LrgPath *self)
{
    guint i;
    guint j;

    g_return_if_fail (self != NULL);

    if (self->points->len <= 1)
        return;

    /* Swap points from both ends toward the middle */
    for (i = 0, j = self->points->len - 1; i < j; i++, j--)
    {
        LrgPathPoint temp;
        LrgPathPoint *pi = &g_array_index (self->points, LrgPathPoint, i);
        LrgPathPoint *pj = &g_array_index (self->points, LrgPathPoint, j);

        temp = *pi;
        *pi = *pj;
        *pj = temp;
    }
}

/*
 * lrg_path_clear:
 * @self: an #LrgPath
 *
 * Removes all points from the path and resets the cost.
 */
void
lrg_path_clear (LrgPath *self)
{
    g_return_if_fail (self != NULL);

    g_array_set_size (self->points, 0);
    self->total_cost = 0.0f;
}

/*
 * lrg_path_get_total_cost:
 * @self: an #LrgPath
 *
 * Gets the total cost of traversing this path.
 *
 * Returns: Total path cost
 */
gfloat
lrg_path_get_total_cost (const LrgPath *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->total_cost;
}

/*
 * lrg_path_set_total_cost:
 * @self: an #LrgPath
 * @cost: Total cost
 *
 * Sets the total cost of traversing this path.
 */
void
lrg_path_set_total_cost (LrgPath *self,
                         gfloat   cost)
{
    g_return_if_fail (self != NULL);
    self->total_cost = cost;
}

/*
 * lrg_path_foreach:
 * @self: an #LrgPath
 * @func: (scope call): Callback function for each point
 * @user_data: (closure): User data for callback
 *
 * Visits a snapshot of the waypoints in their original order and with their
 * original indices. Callbacks may modify or free @self without affecting this
 * iteration. Nested calls take their own snapshot of the current path.
 * The snapshot requires temporary memory proportional to the waypoint count.
 */
void
lrg_path_foreach (const LrgPath      *self,
                  LrgPathForeachFunc  func,
                  gpointer            user_data)
{
    g_autoptr(GArray) points = NULL;
    guint i;

    g_return_if_fail (self != NULL);
    g_return_if_fail (func != NULL);

    /* Copy values before calling user code, which may even free self. */
    points = g_array_sized_new (FALSE, FALSE, sizeof (LrgPathPoint),
                                self->points->len);
    g_array_append_vals (points, self->points->data, self->points->len);

    for (i = 0; i < points->len; i++)
    {
        const LrgPathPoint *pt = &g_array_index (points, LrgPathPoint, i);
        func (pt->x, pt->y, i, user_data);
    }
}

gboolean
lrg_path_set_point (LrgPath *self,
                    guint   index,
                    gint    x,
                    gint    y)
{
    LrgPathPoint *point;

    g_return_val_if_fail (self != NULL, FALSE);
    if (index >= self->points->len)
        return FALSE;
    point = &g_array_index (self->points, LrgPathPoint, index);
    if (point->x != x || point->y != y)
    {
        point->x = x;
        point->y = y;
        self->total_cost = 0.0f;
    }
    return TRUE;
}

gboolean
lrg_path_remove_point (LrgPath *self,
                       guint   index)
{
    g_return_val_if_fail (self != NULL, FALSE);
    if (index >= self->points->len)
        return FALSE;
    g_array_remove_index (self->points, index);
    self->total_cost = 0.0f;
    return TRUE;
}

void
lrg_path_truncate (LrgPath *self,
                   guint   length)
{
    g_return_if_fail (self != NULL);
    if (length < self->points->len)
    {
        g_array_set_size (self->points, length);
        self->total_cost = 0.0f;
    }
}

void
lrg_path_append_path (LrgPath       *self,
                      const LrgPath *other)
{
    g_autoptr(LrgPath) copy = NULL;

    g_return_if_fail (self != NULL);
    g_return_if_fail (other != NULL);
    g_return_if_fail (other->points->len <= G_MAXUINT - self->points->len);
    if (other->points->len == 0)
        return;
    if (self == other)
    {
        copy = lrg_path_copy (other);
        other = copy;
    }
    g_array_append_vals (self->points, other->points->data, other->points->len);
    self->total_cost = 0.0f;
}

gdouble
lrg_path_get_distance (const LrgPath *self)
{
    gdouble distance = 0.0;
    guint i;

    g_return_val_if_fail (self != NULL, 0.0);
    for (i = 1; i < self->points->len; i++)
    {
        const LrgPathPoint *a = &g_array_index (self->points, LrgPathPoint, i - 1);
        const LrgPathPoint *b = &g_array_index (self->points, LrgPathPoint, i);

        distance += hypot ((gdouble) b->x - a->x, (gdouble) b->y - a->y);
    }
    return distance;
}
