/* lrg-trigger-polygon.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Polygon trigger zone implementation.
 */

#include "config.h"

#include <math.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TRIGGER2D
#include "../lrg-log.h"

#include "lrg-trigger-polygon.h"

/**
 * LrgTriggerPolygon:
 *
 * A polygon trigger zone.
 *
 * #LrgTriggerPolygon is a concrete implementation of #LrgTrigger2D that
 * defines an arbitrary polygon area for collision detection. The polygon
 * can be convex or concave.
 *
 * Since: 1.0
 */
struct _LrgTriggerPolygon
{
    LrgTrigger2D parent_instance;

    /* Vertices stored as x,y pairs in a flat array */
    GArray *vertices;

    /* Cached bounding box (invalidated on vertex changes) */
    gfloat   bounds_x;
    gfloat   bounds_y;
    gfloat   bounds_width;
    gfloat   bounds_height;
    gboolean bounds_dirty;
};

G_DEFINE_FINAL_TYPE (LrgTriggerPolygon, lrg_trigger_polygon, LRG_TYPE_TRIGGER2D)

enum
{
    PROP_0,
    PROP_VERTEX_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Internal helpers */

static void
lrg_trigger_polygon_invalidate_bounds (LrgTriggerPolygon *self)
{
    self->bounds_dirty = TRUE;
}

static void
lrg_trigger_polygon_update_bounds (LrgTriggerPolygon *self)
{
    guint   n_vertices;
    gfloat  min_x;
    gfloat  min_y;
    gfloat  max_x;
    gfloat  max_y;
    guint   i;
    gfloat *data;

    if (!self->bounds_dirty)
        return;

    n_vertices = self->vertices->len / 2;

    if (n_vertices == 0)
    {
        self->bounds_x      = 0.0f;
        self->bounds_y      = 0.0f;
        self->bounds_width  = 0.0f;
        self->bounds_height = 0.0f;
        self->bounds_dirty  = FALSE;
        return;
    }

    data  = (gfloat *)self->vertices->data;
    min_x = max_x = data[0];
    min_y = max_y = data[1];

    for (i = 1; i < n_vertices; i++)
    {
        gfloat x = data[i * 2];
        gfloat y = data[i * 2 + 1];

        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
    }

    self->bounds_x      = min_x;
    self->bounds_y      = min_y;
    self->bounds_width  = max_x - min_x;
    self->bounds_height = max_y - min_y;
    self->bounds_dirty  = FALSE;
}

/* Virtual method implementations */

static gboolean
lrg_trigger_polygon_test_point_impl (LrgTrigger2D *trigger,
                                     gfloat        px,
                                     gfloat        py)
{
    LrgTriggerPolygon *self = LRG_TRIGGER_POLYGON (trigger);
    guint   n_vertices;
    gfloat *data;
    guint   i;
    guint   j;
    gboolean inside;

    n_vertices = self->vertices->len / 2;

    /* Need at least 3 vertices for a valid polygon */
    if (n_vertices < 3)
        return FALSE;

    /* Quick bounding box test first */
    lrg_trigger_polygon_update_bounds (self);
    if (px < self->bounds_x ||
        px > self->bounds_x + self->bounds_width ||
        py < self->bounds_y ||
        py > self->bounds_y + self->bounds_height)
    {
        return FALSE;
    }

    /*
     * Ray casting algorithm for point-in-polygon test.
     * Cast a horizontal ray from the point and count edge crossings.
     * Odd number of crossings means inside.
     */
    data   = (gfloat *)self->vertices->data;
    inside = FALSE;

    for (i = 0, j = n_vertices - 1; i < n_vertices; j = i++)
    {
        gfloat xi = data[i * 2];
        gfloat yi = data[i * 2 + 1];
        gfloat xj = data[j * 2];
        gfloat yj = data[j * 2 + 1];

        if (((yi > py) != (yj > py)) &&
            (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
        {
            inside = !inside;
        }
    }

    return inside;
}

static void
lrg_trigger_polygon_get_bounds_impl (LrgTrigger2D *trigger,
                                     gfloat       *out_x,
                                     gfloat       *out_y,
                                     gfloat       *out_width,
                                     gfloat       *out_height)
{
    LrgTriggerPolygon *self = LRG_TRIGGER_POLYGON (trigger);

    lrg_trigger_polygon_update_bounds (self);

    if (out_x != NULL)
        *out_x = self->bounds_x;
    if (out_y != NULL)
        *out_y = self->bounds_y;
    if (out_width != NULL)
        *out_width = self->bounds_width;
    if (out_height != NULL)
        *out_height = self->bounds_height;
}

static LrgTrigger2DShape
lrg_trigger_polygon_get_shape_impl (LrgTrigger2D *trigger)
{
    (void)trigger;
    return LRG_TRIGGER2D_SHAPE_POLYGON;
}

/* GObject implementation */

static void
lrg_trigger_polygon_finalize (GObject *object)
{
    LrgTriggerPolygon *self = LRG_TRIGGER_POLYGON (object);

    g_clear_pointer (&self->vertices, g_array_unref);

    G_OBJECT_CLASS (lrg_trigger_polygon_parent_class)->finalize (object);
}

static void
lrg_trigger_polygon_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgTriggerPolygon *self = LRG_TRIGGER_POLYGON (object);

    switch (prop_id)
    {
    case PROP_VERTEX_COUNT:
        g_value_set_uint (value, self->vertices->len / 2);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_trigger_polygon_class_init (LrgTriggerPolygonClass *klass)
{
    GObjectClass      *object_class  = G_OBJECT_CLASS (klass);
    LrgTrigger2DClass *trigger_class = LRG_TRIGGER2D_CLASS (klass);

    object_class->finalize     = lrg_trigger_polygon_finalize;
    object_class->get_property = lrg_trigger_polygon_get_property;

    /* Override virtual methods */
    trigger_class->test_point = lrg_trigger_polygon_test_point_impl;
    trigger_class->get_bounds = lrg_trigger_polygon_get_bounds_impl;
    trigger_class->get_shape  = lrg_trigger_polygon_get_shape_impl;

    /**
     * LrgTriggerPolygon:vertex-count:
     *
     * The number of vertices in the polygon.
     *
     * Since: 1.0
     */
    properties[PROP_VERTEX_COUNT] =
        g_param_spec_uint ("vertex-count",
                           "Vertex Count",
                           "Number of vertices in the polygon",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_trigger_polygon_init (LrgTriggerPolygon *self)
{
    self->vertices = g_array_new (FALSE, FALSE, sizeof (gfloat));
    self->bounds_dirty = TRUE;
}

/* Public API */

/**
 * lrg_trigger_polygon_new:
 *
 * Creates a new empty polygon trigger zone.
 *
 * Returns: (transfer full): A new #LrgTriggerPolygon
 *
 * Since: 1.0
 */
LrgTriggerPolygon *
lrg_trigger_polygon_new (void)
{
    return g_object_new (LRG_TYPE_TRIGGER_POLYGON, NULL);
}

/**
 * lrg_trigger_polygon_new_with_id:
 * @id: Unique identifier for the trigger
 *
 * Creates a new empty polygon trigger zone with an ID.
 *
 * Returns: (transfer full): A new #LrgTriggerPolygon
 *
 * Since: 1.0
 */
LrgTriggerPolygon *
lrg_trigger_polygon_new_with_id (const gchar *id)
{
    return g_object_new (LRG_TYPE_TRIGGER_POLYGON,
                         "id", id,
                         NULL);
}

/**
 * lrg_trigger_polygon_new_from_points:
 * @points: (array length=n_points) (element-type gfloat): Array of x,y pairs
 * @n_points: Number of points
 *
 * Creates a new polygon trigger from an array of points.
 *
 * Returns: (transfer full): A new #LrgTriggerPolygon
 *
 * Since: 1.0
 */
LrgTriggerPolygon *
lrg_trigger_polygon_new_from_points (const gfloat *points,
                                     gsize         n_points)
{
    LrgTriggerPolygon *self;
    gsize i;

    self = lrg_trigger_polygon_new ();

    if (points != NULL && n_points > 0)
    {
        for (i = 0; i < n_points; i++)
        {
            lrg_trigger_polygon_add_vertex (self,
                                            points[i * 2],
                                            points[i * 2 + 1]);
        }
    }

    return self;
}

/**
 * lrg_trigger_polygon_add_vertex:
 * @self: A #LrgTriggerPolygon
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Adds a vertex to the polygon.
 *
 * Since: 1.0
 */
void
lrg_trigger_polygon_add_vertex (LrgTriggerPolygon *self,
                                gfloat             x,
                                gfloat             y)
{
    g_return_if_fail (LRG_IS_TRIGGER_POLYGON (self));

    g_array_append_val (self->vertices, x);
    g_array_append_val (self->vertices, y);
    lrg_trigger_polygon_invalidate_bounds (self);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VERTEX_COUNT]);
}

/**
 * lrg_trigger_polygon_insert_vertex:
 * @self: A #LrgTriggerPolygon
 * @index: Position to insert at
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Inserts a vertex at the specified position.
 *
 * Since: 1.0
 */
void
lrg_trigger_polygon_insert_vertex (LrgTriggerPolygon *self,
                                   guint              index,
                                   gfloat             x,
                                   gfloat             y)
{
    guint n_vertices;

    g_return_if_fail (LRG_IS_TRIGGER_POLYGON (self));

    n_vertices = self->vertices->len / 2;
    g_return_if_fail (index <= n_vertices);

    /* Insert x,y at position index*2 */
    g_array_insert_val (self->vertices, index * 2, x);
    g_array_insert_val (self->vertices, index * 2 + 1, y);
    lrg_trigger_polygon_invalidate_bounds (self);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VERTEX_COUNT]);
}

/**
 * lrg_trigger_polygon_remove_vertex:
 * @self: A #LrgTriggerPolygon
 * @index: Index of vertex to remove
 *
 * Removes a vertex at the specified index.
 *
 * Since: 1.0
 */
void
lrg_trigger_polygon_remove_vertex (LrgTriggerPolygon *self,
                                   guint              index)
{
    guint n_vertices;

    g_return_if_fail (LRG_IS_TRIGGER_POLYGON (self));

    n_vertices = self->vertices->len / 2;
    g_return_if_fail (index < n_vertices);

    /* Remove both x and y values */
    g_array_remove_range (self->vertices, index * 2, 2);
    lrg_trigger_polygon_invalidate_bounds (self);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VERTEX_COUNT]);
}

/**
 * lrg_trigger_polygon_set_vertex:
 * @self: A #LrgTriggerPolygon
 * @index: Index of vertex to modify
 * @x: New X coordinate
 * @y: New Y coordinate
 *
 * Sets the position of a vertex.
 *
 * Since: 1.0
 */
void
lrg_trigger_polygon_set_vertex (LrgTriggerPolygon *self,
                                guint              index,
                                gfloat             x,
                                gfloat             y)
{
    guint   n_vertices;
    gfloat *data;

    g_return_if_fail (LRG_IS_TRIGGER_POLYGON (self));

    n_vertices = self->vertices->len / 2;
    g_return_if_fail (index < n_vertices);

    data = (gfloat *)self->vertices->data;
    data[index * 2]     = x;
    data[index * 2 + 1] = y;
    lrg_trigger_polygon_invalidate_bounds (self);
}

/**
 * lrg_trigger_polygon_get_vertex:
 * @self: A #LrgTriggerPolygon
 * @index: Index of vertex to get
 * @out_x: (out) (nullable): Return location for X
 * @out_y: (out) (nullable): Return location for Y
 *
 * Gets the position of a vertex.
 *
 * Returns: %TRUE if the index was valid
 *
 * Since: 1.0
 */
gboolean
lrg_trigger_polygon_get_vertex (LrgTriggerPolygon *self,
                                guint              index,
                                gfloat            *out_x,
                                gfloat            *out_y)
{
    guint   n_vertices;
    gfloat *data;

    g_return_val_if_fail (LRG_IS_TRIGGER_POLYGON (self), FALSE);

    n_vertices = self->vertices->len / 2;
    if (index >= n_vertices)
        return FALSE;

    data = (gfloat *)self->vertices->data;

    if (out_x != NULL)
        *out_x = data[index * 2];
    if (out_y != NULL)
        *out_y = data[index * 2 + 1];

    return TRUE;
}

/**
 * lrg_trigger_polygon_get_vertex_count:
 * @self: A #LrgTriggerPolygon
 *
 * Gets the number of vertices in the polygon.
 *
 * Returns: The vertex count
 *
 * Since: 1.0
 */
guint
lrg_trigger_polygon_get_vertex_count (LrgTriggerPolygon *self)
{
    g_return_val_if_fail (LRG_IS_TRIGGER_POLYGON (self), 0);
    return self->vertices->len / 2;
}

/**
 * lrg_trigger_polygon_clear_vertices:
 * @self: A #LrgTriggerPolygon
 *
 * Removes all vertices from the polygon.
 *
 * Since: 1.0
 */
void
lrg_trigger_polygon_clear_vertices (LrgTriggerPolygon *self)
{
    g_return_if_fail (LRG_IS_TRIGGER_POLYGON (self));

    g_array_set_size (self->vertices, 0);
    lrg_trigger_polygon_invalidate_bounds (self);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VERTEX_COUNT]);
}

/**
 * lrg_trigger_polygon_translate:
 * @self: A #LrgTriggerPolygon
 * @dx: X offset
 * @dy: Y offset
 *
 * Moves all vertices by the specified offset.
 *
 * Since: 1.0
 */
void
lrg_trigger_polygon_translate (LrgTriggerPolygon *self,
                               gfloat             dx,
                               gfloat             dy)
{
    guint   n_vertices;
    gfloat *data;
    guint   i;

    g_return_if_fail (LRG_IS_TRIGGER_POLYGON (self));

    n_vertices = self->vertices->len / 2;
    data = (gfloat *)self->vertices->data;

    for (i = 0; i < n_vertices; i++)
    {
        data[i * 2]     += dx;
        data[i * 2 + 1] += dy;
    }

    lrg_trigger_polygon_invalidate_bounds (self);
}

/**
 * lrg_trigger_polygon_scale:
 * @self: A #LrgTriggerPolygon
 * @sx: X scale factor
 * @sy: Y scale factor
 *
 * Scales all vertices around the centroid.
 *
 * Since: 1.0
 */
void
lrg_trigger_polygon_scale (LrgTriggerPolygon *self,
                           gfloat             sx,
                           gfloat             sy)
{
    guint   n_vertices;
    gfloat *data;
    gfloat  cx;
    gfloat  cy;
    guint   i;

    g_return_if_fail (LRG_IS_TRIGGER_POLYGON (self));

    n_vertices = self->vertices->len / 2;
    if (n_vertices == 0)
        return;

    /* Get centroid */
    lrg_trigger_polygon_get_centroid (self, &cx, &cy);

    /* Scale around centroid */
    data = (gfloat *)self->vertices->data;

    for (i = 0; i < n_vertices; i++)
    {
        gfloat x = data[i * 2];
        gfloat y = data[i * 2 + 1];

        data[i * 2]     = cx + (x - cx) * sx;
        data[i * 2 + 1] = cy + (y - cy) * sy;
    }

    lrg_trigger_polygon_invalidate_bounds (self);
}

/**
 * lrg_trigger_polygon_rotate:
 * @self: A #LrgTriggerPolygon
 * @angle: Rotation angle in radians
 *
 * Rotates all vertices around the centroid.
 *
 * Since: 1.0
 */
void
lrg_trigger_polygon_rotate (LrgTriggerPolygon *self,
                            gfloat             angle)
{
    guint   n_vertices;
    gfloat *data;
    gfloat  cx;
    gfloat  cy;
    gfloat  cos_a;
    gfloat  sin_a;
    guint   i;

    g_return_if_fail (LRG_IS_TRIGGER_POLYGON (self));

    n_vertices = self->vertices->len / 2;
    if (n_vertices == 0)
        return;

    /* Get centroid */
    lrg_trigger_polygon_get_centroid (self, &cx, &cy);

    /* Precompute trig values */
    cos_a = cosf (angle);
    sin_a = sinf (angle);

    /* Rotate around centroid */
    data = (gfloat *)self->vertices->data;

    for (i = 0; i < n_vertices; i++)
    {
        gfloat x  = data[i * 2] - cx;
        gfloat y  = data[i * 2 + 1] - cy;
        gfloat rx = x * cos_a - y * sin_a;
        gfloat ry = x * sin_a + y * cos_a;

        data[i * 2]     = rx + cx;
        data[i * 2 + 1] = ry + cy;
    }

    lrg_trigger_polygon_invalidate_bounds (self);
}

/**
 * lrg_trigger_polygon_get_centroid:
 * @self: A #LrgTriggerPolygon
 * @out_x: (out) (nullable): Return location for centroid X
 * @out_y: (out) (nullable): Return location for centroid Y
 *
 * Gets the centroid (center of mass) of the polygon.
 *
 * Since: 1.0
 */
void
lrg_trigger_polygon_get_centroid (LrgTriggerPolygon *self,
                                  gfloat            *out_x,
                                  gfloat            *out_y)
{
    guint   n_vertices;
    gfloat *data;
    gfloat  sum_x;
    gfloat  sum_y;
    guint   i;

    g_return_if_fail (LRG_IS_TRIGGER_POLYGON (self));

    n_vertices = self->vertices->len / 2;

    if (n_vertices == 0)
    {
        if (out_x != NULL) *out_x = 0.0f;
        if (out_y != NULL) *out_y = 0.0f;
        return;
    }

    /* Simple average of vertices for centroid */
    data  = (gfloat *)self->vertices->data;
    sum_x = 0.0f;
    sum_y = 0.0f;

    for (i = 0; i < n_vertices; i++)
    {
        sum_x += data[i * 2];
        sum_y += data[i * 2 + 1];
    }

    if (out_x != NULL)
        *out_x = sum_x / (gfloat)n_vertices;
    if (out_y != NULL)
        *out_y = sum_y / (gfloat)n_vertices;
}

/**
 * lrg_trigger_polygon_get_area:
 * @self: A #LrgTriggerPolygon
 *
 * Gets the area of the polygon using the shoelace formula.
 *
 * Returns: The area (always positive)
 *
 * Since: 1.0
 */
gfloat
lrg_trigger_polygon_get_area (LrgTriggerPolygon *self)
{
    guint   n_vertices;
    gfloat *data;
    gfloat  area;
    guint   i;
    guint   j;

    g_return_val_if_fail (LRG_IS_TRIGGER_POLYGON (self), 0.0f);

    n_vertices = self->vertices->len / 2;
    if (n_vertices < 3)
        return 0.0f;

    /*
     * Shoelace formula:
     * Area = 0.5 * |sum((x[i] * y[i+1]) - (x[i+1] * y[i]))|
     */
    data = (gfloat *)self->vertices->data;
    area = 0.0f;

    for (i = 0, j = n_vertices - 1; i < n_vertices; j = i++)
    {
        gfloat xi = data[i * 2];
        gfloat yi = data[i * 2 + 1];
        gfloat xj = data[j * 2];
        gfloat yj = data[j * 2 + 1];

        area += (xj + xi) * (yj - yi);
    }

    return fabsf (area * 0.5f);
}

/**
 * lrg_trigger_polygon_is_convex:
 * @self: A #LrgTriggerPolygon
 *
 * Checks if the polygon is convex.
 *
 * Returns: %TRUE if the polygon is convex
 *
 * Since: 1.0
 */
gboolean
lrg_trigger_polygon_is_convex (LrgTriggerPolygon *self)
{
    guint   n_vertices;
    gfloat *data;
    gint    sign;
    guint   i;

    g_return_val_if_fail (LRG_IS_TRIGGER_POLYGON (self), FALSE);

    n_vertices = self->vertices->len / 2;
    if (n_vertices < 3)
        return FALSE;

    /*
     * A polygon is convex if all cross products of consecutive
     * edge vectors have the same sign.
     */
    data = (gfloat *)self->vertices->data;
    sign = 0;

    for (i = 0; i < n_vertices; i++)
    {
        guint i1 = (i + 1) % n_vertices;
        guint i2 = (i + 2) % n_vertices;

        gfloat x0 = data[i  * 2];
        gfloat y0 = data[i  * 2 + 1];
        gfloat x1 = data[i1 * 2];
        gfloat y1 = data[i1 * 2 + 1];
        gfloat x2 = data[i2 * 2];
        gfloat y2 = data[i2 * 2 + 1];

        /* Cross product of edge vectors */
        gfloat cross = (x1 - x0) * (y2 - y1) - (y1 - y0) * (x2 - x1);

        if (cross != 0.0f)
        {
            gint this_sign = (cross > 0.0f) ? 1 : -1;

            if (sign == 0)
                sign = this_sign;
            else if (sign != this_sign)
                return FALSE;
        }
    }

    return TRUE;
}

/**
 * lrg_trigger_polygon_is_valid:
 * @self: A #LrgTriggerPolygon
 *
 * Checks if the polygon is valid (has at least 3 vertices).
 * Note: Does not check for self-intersection.
 *
 * Returns: %TRUE if the polygon is valid
 *
 * Since: 1.0
 */
gboolean
lrg_trigger_polygon_is_valid (LrgTriggerPolygon *self)
{
    g_return_val_if_fail (LRG_IS_TRIGGER_POLYGON (self), FALSE);
    return (self->vertices->len / 2) >= 3;
}
